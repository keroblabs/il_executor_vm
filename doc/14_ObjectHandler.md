# ObjectHandler — the object, heap, stack and collector library

Contract document. Specifies **ObjectHandler**, a standalone C library that
owns every managed object, the managed heap, the per-thread execution stack,
and the garbage collector.

It is derived from `ruffusvm/ObjectHandler/` but is a **new implementation**,
not a port. It is written to be usable outside Ruffus CLI.

Supersedes the `vee_*` and `oh_*` namings used in earlier drafts of
[11_Memory_Model.md](11_Memory_Model.md) and
[00_Introduction.md](00_Introduction.md): the prefix of a name is now the
**module that owns it** (§1.2). Read with
[13_Ruffus_VM_Reuse.md](13_Ruffus_VM_Reuse.md) §6, which this document
resolves.

Status: **contract — write before the code.** Items marked *(open)* must not
be guessed at in code.

---

## 1. Why it is a separate library

Ruffus VM already treats `ObjectHandler/` as a component: `heap.c` compiles
standalone when `WANT_OBJECT_HANDLER` is undefined, and the whole directory
has no `#include` of any VM header. That boundary was worth having and is
worth strengthening.

**Rule: ObjectHandler contains no CIL, no ECMA-335, and no `.vxb`
knowledge** beyond the primitive values a stack slot holds (§6.7). It
manipulates opaque *type ids* and opaque *reference bitmaps* handed to it by
the embedder. `cli_exec` is one embedder; Ruffus VM could be another; a
future project could be a third.

Concretely this forbids, inside the library:

- any mention of `MethodDef`, `TypeDef`, tokens, or IL opcodes;
- any use of `malloc` — every region is caller-supplied (§9);
- any `printf` — diagnostics go through host callbacks;
- any assumption that a type id means anything other than "index the
  embedder understands".

What the library *does* own, and what the embedder must therefore not
duplicate: object identity, object lifetime, the layout of the heap, the
layout of the execution stack, the definition of a frame, and how slot values
compare and convert (§6.7). That last is mechanism: which comparison or
conversion an opcode performs, and whether its operand types are valid under
§III.1.5, is the interpreter's policy (B6).

**Application, threads and the instance (A2, A14).** An application is one
CLI binary, packed into one image, and it runs on one OS thread; its managed
threads are green threads the VM schedules on that thread
([17 §1](17_Threading_Model.md)). Ruffus CLI runs one application, and
running several at once is not a use case. The library's state for the
application lives in an `obj_handler_t` the embedder provides — the space
table, the heap segments, where the constants are, the host callbacks — and
every call takes it first (§8). That is for the reason §8 gives against
hidden state, not to run several applications. The instance is not
re-entrant and is used only from the application's thread; interrupts and
peripheral callbacks reach the application through its queue (17 §9), never
through the library. No host callback (§7.5) may call back into the library.

### 1.1 Layout

The decomposition is Ruffus's, kept deliberately: one module per
responsibility, one prefix per module.

```
source/object_handler/
    obj_handler.h         /* umbrella header — includes the modules below  */
    obj_handler_config.h  /* the single configuration + debug switches     */
    objects_types.h       /* object_id_t, spaces, kinds — shared vocabulary*/
    objects.c/.h          /* identity, spaces, handles, resolution, create */
    heap.c/.h             /* segments, payload allocator, free list        */
    stack.c/.h            /* managed-thread stacks, frames, typed slots    */
    obj_string.c/.h       /* string objects and their operations          */
    value.c/.h            /* boxing, unboxing, numeric conversion, compare */
    array.c/.h            /* one-dimensional arrays of inline elements     */
    gc.c/.h               /* tricolour mark/sweep, roots, rooting scopes   */
```

The shared header is `objects_types.h`, not `types.h`: the name carries its
owning module (§1.2), and `source/types.h` already exists one directory up.
The library's include path contains only this directory, so that collision is
impossible rather than merely avoided —
[18_ObjectHandler_Build_Plan.md](18_ObjectHandler_Build_Plan.md) D3.

The string module is `obj_string`, not `strings`, because that collision runs
the other way: `strings.h` is a POSIX header that glibc's and newlib's
`<string.h>` include in their default GNU modes, so a library file of that
name on the `-I` path would shadow it. No include-path discipline prevents
that; only the name does. It also moves the prefix off `str` + lowercase,
which C11 §7.31.13 reserves for `<string.h>` — [18 D3](18_ObjectHandler_Build_Plan.md).

`objects_types.h` and every module header must compile against `<stdint.h>`
and `<string.h>` only. A CI job builds the directory on its own with
`-Wall -Wextra -Werror -pedantic` and no include path into `source/` — that
job is what keeps the boundary honest.

**Compiler baseline (A5).** C99, and the same sources are also built as C11.
Warnings are `-Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes
-Wmissing-prototypes`. `OBJECT_LIKELY` and `OBJECT_UNLIKELY` live in
`objects_types.h` — module-prefixed, so they cannot collide with an
embedder's `LIKELY` — and use `__builtin_expect` on GCC and Clang, a plain
expression elsewhere. Headers are wrapped in `extern "C"`, and every file has
LF line endings.

### 1.2 Naming

**A name is prefixed with the module that owns it.** There is no
library-wide prefix: `oh_`, and the `vee_` before it, are both retired. The
rule is Ruffus's and it is kept because it makes the owning `.c` file
readable from the call site.

`obj_handler_` and `obj_string_` are not a library prefix creeping back:
`obj_handler` and `obj_string` are module names, and each prefix still names
exactly one file. §1.1 says why the string module is not called `strings`.

| Prefix | Module | Owns |
|---|---|---|
| `objects_` / `OBJECT_` | `objects.c` | object identity, `object_id_t`, spaces, handle table, resolution, generic create/clone/promote/pin, the write barrier |
| `heap_` / `HEAP_` | `heap.c` | segments, payload allocation, free list, size-class recycler |
| `stack_` / `STACK_` | `stack.c` | each managed thread's value and frame stacks, evaluation slots, the thread ring; the frame scratch arena with tier 2 |
| `obj_string_` / `OBJ_STRING_` | `obj_string.c` | `System.String` payloads, interning, string operations |
| `value_` / `VALUE_` | `value.c` | boxing/unboxing, numeric conversion and comparison |
| `array_` / `ARRAY_` | `array.c` | array payloads (§5.7), the one range check, bulk copy and clear |
| `gc_` / `GC_` | `gc.c` | mark, sweep, roots, rooting scopes, collection trigger |
| `obj_handler_` / `OBJ_HANDLER_` | `obj_handler.h` | library lifecycle only: `init`, `shutdown`, config and host structs, init status codes |

Two consequences worth stating, because the `oh_` draft got them wrong:

- **Object kinds and slot kinds are different enumerations.** They were both
  `OH_K_*`, which was a latent bug. They are now `OBJECT_K_*` (what a heap
  block holds) and `STACK_K_*` (what an evaluation slot holds).
- **Allocation is named after where the memory comes from**, not after what
  is built in it: `heap_alloc()` is the v1 allocator, and
  `stack_scratch_alloc()` joins it with tier 2 (§6.3); `objects_create()`,
  `array_create()`, `obj_string_create()` and `value_box()` are the typed
  constructors that call them.

---

## 2. References — `object_id_t`

### 2.1 Shape

A reference is 32 bits, split into a **space tag** and an **index**. It is
called an *object id*, not a pointer, and that is the whole point: it is
Ruffus's `object_t` widened, not a machine address.

```c
typedef uint32_t object_id_t;

#define OBJECT_NULL             ((object_id_t)0)

#define OBJECT_SPACE(id)        ((uint16_t)((id) >> 16))
#define OBJECT_INDEX(id)        ((uint16_t)((id) & 0xFFFFu))
#define OBJECT_ID(space, idx)   (((object_id_t)(space) << 16) | (uint16_t)(idx))
```

**Index 0 is reserved in every space.** That is what lets `OBJECT_NULL` be a
natural zero while still allowing space 0 to be a real, usable space. Ruffus
gets the same property by computing `&memory[id - 1]`; reserving the index is
cheaper and clearer.

**A debug build re-splits the same 32 bits** to carry a handle *generation*,
so that a stale id is rejected rather than silently aliasing a recycled
handle (§3.2):

```c
#if OBJECT_DEBUG_REFS               /* 8-bit space | 8-bit gen | 16-bit index */
#define OBJECT_SPACE(id)        ((uint16_t)((id) >> 24))
#define OBJECT_GEN(id)          ((uint8_t)(((id) >> 16) & 0xFFu))
#define OBJECT_INDEX(id)        ((uint16_t)((id) & 0xFFFFu))
#define OBJECT_ID(space, idx)   (((object_id_t)(space) << 24) | (uint16_t)(idx))
#define OBJECT_ID_GEN(space, gen, idx)                      \
        (((object_id_t)(space) << 24) |                     \
         ((object_id_t)(gen)   << 16) | (uint16_t)(idx))
#else
#define OBJECT_GEN(id)                  ((uint8_t)0)
#define OBJECT_ID_GEN(space, gen, idx)  OBJECT_ID((space), (idx))
#endif
```

Three properties make this safe to have in one build and not the other:

- **The width never changes**, so every structure holding an id — object
  field, array element, stack slot — is byte-identical in both builds. A
  64-bit debug id would have changed all of them and stopped the debug build
  from rehearsing the release one.
- **`OBJECT_ID` stamps generation 0, and a live handle's generation is never
  0** (§3.2). So a hand-fabricated heap id is always rejected, which is
  correct: heap ids come from `heap_alloc` via `OBJECT_ID_GEN` and from
  nowhere else. Flat spaces have no handle and no check, so `OBJECT_ID` is the
  right constructor for them.
- **No code may open-code `>> 16`.** Ids are built and taken apart only
  through these macros, in the library and in the tests.

The one thing it costs: a debug build cannot use an embedder-defined space
above `0x00FF` (§3.1). There are none in v1.
[18 D2](18_ObjectHandler_Build_Plan.md) carries the arithmetic and the
worked example.

### 2.1.1 Everything that outlives an expression is an object id

**Rule: the only reference form stored anywhere in the system is an
`object_id_t`.** Object fields, static fields, array elements, string
references, byref bases, monitor keys and native descriptors all hold object
ids. No raw pointer is ever stored; raw pointers exist only inside one opcode
handler or one intrinsic, and only between two safepoints (§3.3).

The deliberate exception is the **evaluation stack**, where an `int32` is an
`int32` and not an object id (§6.1). That is not a violation of the rule —
an unboxed primitive is not an object — and it is the one place Ruffus's
"everything is an `object_t`" costs more than it buys: `1 + 2` there means
two allocations, two resolutions and one piece of garbage. Objects are
always ids; *temporaries of primitive type are not objects*.

### 2.2 The space tag is *where*, not *what*

The upper 16 bits say **which region the object lives in**, not what type it
is. The managed type stays where Ruffus put it — in the object's own record
(here, in the handle, §3.2). Ruffus keeps the class reference in the object
itself (`object_data_t.cref`, read back by `object_get_class_ref()`); the
handle is the same idea moved out of the payload.

This distinction matters because the two answers different questions:

| Question | Answered by | Cost |
|---|---|---|
| Where do I fetch the bytes from? | space tag, in the reference | shift + table index |
| What is this object? | `type` field in the handle | one load |
| Is it collectable? | space tag | compare |

Putting the type in the reference would make `castclass` free but would make
every reference to a given type indistinguishable from every other, which
breaks identity. Putting the space in the reference is the useful half.

### 2.3 The 64 KB decision — an initial profile, not a contract

The heap stays 16-bit addressable **in the first release**, deliberately,
because that is the size that fits an MCU and because it halves every stored
offset. It is a profile choice, and §2.3.1 is what earns the right to make it
one.

The 32-bit object id buys three things Ruffus's 16-bit `object_t` could not:

1. **Non-heap objects become first-class.** Constants, statics, frame
   temporaries and foreign memory all get real references instead of being
   special-cased with a magic threshold. Ruffus's `CONST_OBJECT_ID 0x8000`
   is exactly this idea, cramped into one bit and costing half the heap.
2. **The heap can be segmented without widening anything.** Up to 16
   segments of 64 KB each — 1 MB — with identical code and identical
   reference width. An MCU profile uses segment 0 only and pays nothing.
3. **Bound checks become free.** The space table carries a limit; a
   malformed reference lands in an unused space and is rejected on the
   first access rather than corrupting the heap.

### 2.3.1 What the id abstracts, and how the cap is lifted later

**An id in a handled space is already a pure identity**: the index selects a
handle slot, and nothing outside `objects.c` and `heap.c` may infer an
address, a size or an ordering from it. That single property is what the rest
of the system is written against, and it is what the two upgrades need:

- **Compaction** (§5.3) moves payloads and rewrites `handles[i].offset`. No
  id anywhere changes, so no embedder observes the move.
- **Growth past 64 KB** widens `object_handle_t.offset` and `.size` to 32
  bits and re-splits the id — 8-bit space / 24-bit index, or a per-space
  index width — inside `objects_types.h`. Every allocation API is *already*
  32-bit (§5.2, §8), so the signatures an embedder compiled against do not
  move.
- **Ruffus CLI, or any other embedder, is unaffected by either**, because
  §2.1.1 forbids it from storing anything but an `object_id_t` and §3.3 is
  the only way to turn one into bytes.

The exception, stated plainly: the **flat** spaces — `OBJECT_SPACE_STATIC`
and `OBJECT_SPACE_FRAME` — use the index
*as* a byte offset (§3.3). Those ids are not pure identities, which is
precisely why those regions are immortal, immovable and capped at 64 KB each.
Making one of them movable means giving it a handle table: the same local
change, with no renumbering of the catalogue.

So the order of work is **ship the 64 KB non-moving profile, then measure,
then widen** — not design for a heap size no image has yet asked for.

---

## 3. Spaces

### 3.1 Catalogue

| Tag | Name | Class ([11 §1](11_Memory_Model.md)) | Index means | Collected | Writable |
|---|---|---|---|---|---|
| `0x0000`–`0x000F` | `OBJECT_SPACE_HEAP + n` | collectable | handle slot in segment *n* | **yes** | yes |
| `0x0010` | `OBJECT_SPACE_CONST` | immutable image | constant-table index (§3.5) | no | **no** |
| `0x0011` | `OBJECT_SPACE_STATIC` | immortal | byte offset in the statics slab | no — **root** | yes |
| `0x0012` | `OBJECT_SPACE_FRAME` | scoped | byte offset in a thread stack region | no — **root**, LIFO | yes |
| `0x0013` | `OBJECT_SPACE_NATIVE` | foreign | native descriptor index | no | per descriptor |
| `0x0014` | `OBJECT_SPACE_STACK` | thread-relative | slot in the value stack of the thread that holds the id (§4.1) | no — **root** | yes |
| `0x0015`–`0x00FF` | reserved | — | — | — | — |
| `0x0100`–`0xFFFF` | embedder-defined | — | — | — | — |

The embedder range is what makes the library reusable: a project with, say,
an external SPI-attached PSRAM region registers it as a space, hands
ObjectHandler a resolver, and every reference to it works through the same
`object_id_t` as everything else.

`OBJECT_SPACE_FRAME` is **reserved and unused in v1** — the frame scratch
arena is tier 2 and is deferred (§6). It keeps its tag from the first commit
so that enabling it later renumbers nothing.

`OBJECT_SPACE_STACK` is **thread-relative** (B8): its index names a slot in
one managed thread's value stack, which a table with one entry per space
cannot say. Its entry stays empty, so `objects_ptr` sends such an id to
`on_corrupt`; `stack_byref_ptr`, which is given the thread, resolves it
(§4.1). Tier 2's `OBJECT_SPACE_FRAME` ids are per-thread as well, and §6.3
will have to resolve them the same way.

### 3.2 Handle slot

Heap spaces are **handled**: the index selects a fixed-size record, and the
record says where the bytes are.

```c
typedef struct {
    uint16_t offset;   /* byte offset into the segment payload;
                          when free: index of the next free handle          */
    uint16_t size;     /* payload bytes                                     */
    uint16_t type;     /* embedder type id; meaningless to ObjectHandler    */
    uint8_t  flags;    /* OBJECT_H_* below                                  */
    uint8_t  kind;     /* OBJECT_K_* below                                  */
#if OBJECT_DEBUG_REFS
    uint8_t  gen;      /* 1..255, never 0; bumped on free — §2.1, §3.4      */
#endif
} object_handle_t;     /* 8 bytes release, 10 debug */

enum {                                      /* flags */
    OBJECT_H_INUSE   = 0x01,
    OBJECT_H_ASCII   = 0x02,                /* strings: every byte < 0x80 — §5.6 */
    OBJECT_H_MARK    = 0x04,                /* GC mark bit — see §7.2         */
    OBJECT_H_SCANNED = 0x08,                /* grey→black, on worklist overflow */
    OBJECT_H_HASREFS = 0x10,                /* payload contains object ids    */
    OBJECT_H_PINNED  = 0xE0,                /* pin count, 0–7; nonzero: must not move */
};

enum {                                      /* object kind — what the block is */
    OBJECT_K_FREE = 0, OBJECT_K_OBJECT, OBJECT_K_ARRAY, OBJECT_K_STRING,
    OBJECT_K_BOX, OBJECT_K_RAW,
};
```

**A pin is counted, not flagged (A4).** `objects_pin` adds one to the count
held in `OBJECT_H_PINNED` and `objects_unpin` takes one away, so a `fixed`
block and a native call can pin the same buffer. An eighth nested pin, or an
unpin at zero, goes to `on_corrupt`. Bit `0x02` is `OBJECT_H_ASCII`, for
strings (§5.6).

`offset` and `size` are 16 bits because the v1 payload is (§2.3), not because
anything depends on it: both are private to `heap.c`, and widening them to 32
bits takes the handle to 12 bytes and changes no other file.

This is Ruffus's two-block scheme with the blocks separated. Ruffus allocates
the small indirection record *inside* the heap
(`heap_object_malloc` → `_internal_malloc(sizeof(uint8_t *))` then a second
`_internal_malloc` for the payload, linked by `heap_header_t3.data`). Pulling
those records into a dense array is strictly better: they stop fragmenting
the payload region, they stop carrying an 8-byte block header each, and the
index into the array becomes the reference.

**Doc 13 §6.2 is corrected by this.** It recorded that Ruffus's references
are raw heap offsets and that compaction is therefore impossible. That is
true only of value/data objects; class and array objects already go through
`heap_header_t3.data`, and `heap_object_resize()` already moves their
payloads and rewrites that one pointer. Ruffus was two-thirds of the way to a
handle table. This design finishes the job.

### 3.3 Resolution

```c
typedef struct {
    uint8_t         * base;     /* payload base; NULL => indirect space      */
    object_handle_t * handles;  /* non-NULL => handled space                 */
    uint32_t          limit;    /* highest valid index + 1 — 65536 must fit  */
    uint16_t          flags;    /* OBJECT_SP_* : COLLECTED | WRITABLE | ROOT */
} object_space_t;

static inline void * objects_ptr(obj_handler_t * oh, object_id_t id)
{
    const object_space_t * sp = &oh->spaces[OBJECT_SPACE(id)];

    if (OBJECT_LIKELY(sp->handles != NULL))             /* heap segment      */
        return sp->base + sp->handles[OBJECT_INDEX(id)].offset;

    if (OBJECT_LIKELY(sp->base != NULL))                /* flat space        */
        return sp->base + OBJECT_INDEX(id);

    return objects_resolve_indirect(oh, id);            /* native descriptor */
}
```

One predictable branch and two loads on the heap path; one branch and one
add on statics and frame scratch. Constants go through their table, outside
the inline path (§3.5). The
[once-per-opcode rule](11_Memory_Model.md#44-resolution-and-the-once-per-opcode-rule)
is unchanged and is what keeps this off the critical path.

`OBJECT_SPACE_COUNT` is a compile-time profile constant. An MCU build sets it
to `0x0015`, so an instance's space table is 21 records — 336 bytes — and the
embedder range costs nothing.

### 3.4 Debug resolution

`OBJECT_DEBUG_REFS` replaces `objects_ptr` with a checked version that
validates `OBJECT_SPACE(id) < OBJECT_SPACE_COUNT`,
`OBJECT_INDEX(id) < sp->limit`, `flags & OBJECT_H_INUSE`, and — for handled
spaces — `OBJECT_GEN(id) == handles[i].gen`.

The last of those is the one that earns its keep. The first three catch a
*malformed* id; only the generation catches a **stale** one, which otherwise
resolves to a different, valid, live object and corrupts it in silence.
`heap_release` — which the sweep calls — bumps the generation of every handle
it frees (A8), and freed handles are returned to the tail of the free list so
that reuse is as late as possible.

It is on by default in host builds. The release path above is what ships to
the device — but note §0.2 of
[18_ObjectHandler_Build_Plan.md](18_ObjectHandler_Build_Plan.md): a *device
debug build* is the same configuration with these switches on, which is what
makes the checks usable on real hardware when something is wrong.

**Outside `objects_ptr`, ids are checked in every build.** The accessors that
are not inline — `objects_type_of`, `_size_of`, `_kind_of`, `_clone`, `_pin`,
`_unpin` and `heap_release` — validate the space, the index,
`OBJECT_H_INUSE` and, in debug, the generation, and send a bad id to
`on_corrupt`. `objects_ptr` is the one path that resolves blind in release,
because it is the hot one.

### 3.5 Statics and constants (D7)

Decided 2026-09-28. `obj_handler_init` configures both spaces from the
config (§9); `objects_space_register` accepts neither.

**The statics slab — `OBJECT_SPACE_STATIC`, flat.** `OBJECT_ID(STATIC, off)`
resolves to `statics + off`: a static field's id is its byte offset, which is
what lets `cli_pack` rewrite `ldsfld` to one. Bytes 0–7 are reserved, so
`cli_pack` lays fields out from offset 8 and index 0 stays invalid. The slab
is 8-aligned, a multiple of 8 and at most 64 KB. `statics_refmap` has one bit
per word — bit *i* set when the word at offset 4·*i* is an `object_id_t` — and
is NULL when no static holds an id; words 0 and 1 are never marked. The
embedder fills the slab and ObjectHandler never writes it. A reference word
holds `OBJECT_NULL` or a live id and never image data, because an id's bits
differ between debug and release builds (§2.1). A static that a folded
`.cctor` sets to a literal is therefore written at start-up by `cli_exec`,
from a fix-up list in the image ([15 §5.1](15_Pack_Time_IL_Transforms.md)).

**The constant table — `OBJECT_SPACE_CONST`, indirect.** An index selects an
entry of a table in the image, so a constant's id is a pure identity, like a
heap id. Resolution reads the entry, off the inline path of §3.3. The table
is one image format, identical in debug and release builds, little-endian:

```c
typedef struct {            /* entry i of the table at const_base; entry 0 reserved */
    uint32_t offset;        /* payload offset from const_base; 8-aligned, past the table */
    uint32_t size;          /* payload bytes */
    uint16_t type;          /* embedder type id */
    uint8_t  kind;          /* OBJECT_K_*, never FREE */
    uint8_t  flags;         /* OBJECT_H_ASCII for an ASCII string, else 0 */
} object_const_t;           /* 12 bytes */
```

Payloads are laid out exactly as on the heap: a string is its UTF-8 bytes and
a NUL (§5.6), an array starts with its `array_header_t` (§5.7). A constant is
immutable, never collected, and **never holds an id**, which is why
constants are not a root. `obj_handler_init` checks every entry — kind,
flags, bounds, a string's NUL, an array's header, and that an object, array
or box constant has a NULL refmap — and returns `OBJ_HANDLER_E_CONST` for the
first bad one (§9.2).

**Literals are constants, read in place.** `obj_string_intern(i)` returns
`OBJECT_ID(CONST, i)` once it has checked that entry *i* is a string: no copy
and no allocation, so `ldstr` can neither run out of memory nor trigger a
collection. This supersedes A6: the literal table and its root are gone.

**What accepts a constant.** Every reader: `objects_ptr`, `objects_type_of`,
`_size_of` and `_kind_of` read the entry, and `array_length`,
`array_range_ptr`, the string functions and `value_unbox` work unchanged.
Pinning one does nothing. Writing into one — `array_copy` or `array_clear`
into a constant array — goes to `on_corrupt`, and so does `heap_release`.
`objects_clone` copies a constant into the heap. A `FieldRVA` array is
emitted as a constant only when `cli_pack` proves nothing writes it
([15 §5.1](15_Pack_Time_IL_Transforms.md)).

---

## 4. Interior pointers

With a space tag in the object id, [11_Memory_Model.md §5](11_Memory_Model.md)
collapses. The three byref *kinds* — heap, static, frame — are no longer
needed in the stack-slot tag, because the id already says which region it
designates.

```c
typedef struct {
    object_id_t id;     /* base object or region; OBJECT_NULL is never valid */
    uint16_t    offset; /* byte offset from that object's payload base       */
    uint16_t    _pad;
} object_byref_t;       /* 8 bytes */
```

A single `STACK_K_BYREF` slot kind replaces three. `objects_byref_ptr()` is
`objects_ptr(b.id) + b.offset`, uniformly, for every region but a thread's
stack (§4.1).

`offset` is 16 bits because a v1 object is (§2.3). When the payload widens,
the offset widens with it and moves out of `stack_slot_t.aux` (§6.1) into the
union, which is 8 bytes either way — so the slot does not grow.

Managed pointers remain forbidden in object fields, static fields and array
elements, per ECMA-335. The collector depends on that: reference bitmaps
describe `object_id_t` only.

### 4.1 A byref to a local or an argument (B8)

`ldloca` and `ldarga` point into a stack slot, and a slot belongs to one
managed thread's value stack (§6.2). The space table has one entry per
space, not one per thread, so no space could say *which* stack. A property
of CIL solves it: **a managed pointer never leaves its thread.** It cannot be
stored in a field, a static or an array element (above); a verifiable method
cannot return one to its own locals or arguments; and a thread starts from a
delegate, which cannot capture one. So a byref to a slot is only ever used by
the thread whose stack holds the slot, and the interpreter always has that
thread's record in hand.

Decided 2026-09-29:

- **A thread-relative space, `OBJECT_SPACE_STACK` (§3.1).** The id's index is
  the slot's position in the value stack of the thread that holds the byref,
  and the offset is a byte offset into that slot's value. `stack_slot_byref(t,
  slot)` makes one for `ldloca` and `ldarga`; `stack_byref_ptr(t, b)` resolves
  any byref — one into the stack through `t`, every other one through
  `objects_byref_ptr`. The interpreter uses it for every indirect access, and
  a native resolves a byref argument through its `exec_thread_t`
  ([16 §4.1](16_Native_Call_Protocol.md)). The index is 16 bits, so a thread
  has at most 65536 slots. Two stack byrefs compare by slot and offset, and a
  stack byref and any other byref are unordered (§6.7).
- **The collector passes over it.** The space is not collected, and the slot
  it designates is scanned as a root already.
- **Locals are typed from the frame's entry.** `stack_frame_push` takes each
  local's slot kind, which `cli_pack` emits per method (§6.2). A store
  through a byref writes the value and not the kind, so into an `EMPTY` local
  it would hide an id from the collector.
- **A local or an argument holds its declared type's storage form**, as a
  field would: the type's bytes at the start of the slot's value,
  little-endian. `int8` to `uint16`, `bool` and `char` fill the low bytes of
  an `I4` slot, and a `float32` is a single in the low four bytes of an `R8`
  slot; every other type is stored as the evaluation stack holds it.
  ECMA-335 treats locals and arguments as locations of their declared type
  (§III.1.1.1), so `ldloc` and `ldarg` widen, `stloc` and `starg` narrow,
  and `call` narrows each argument to its parameter's type. A byref to a
  local then behaves exactly like a byref to a field: `ldind` and `stind`
  move raw bytes wherever the byref points, a reference still passing through
  `objects_write_ref`. The widening load is needed in any case, because
  `stind.i1` cannot tell `int8` from `uint8`.

---

## 5. The heap

### 5.1 Layout

Two caller-supplied regions per segment:

```
handles[]   dense array of object_handle_t, handle_count entries
payload[]   ≤ 64 KB of 8-byte-aligned blocks  — v1 profile, §2.3
```

**Live blocks carry no header at all.** The handle already holds the offset,
the size, the type and the flags; a per-block header would duplicate all
four. Free blocks store their own link and size *in their own bytes*:

```c
typedef struct {            /* written into the free block itself */
    uint16_t next;          /* offset of next free block; 0 == end */
    uint16_t size;
} heap_free_t;
```

Minimum block is 8 bytes, so a free block always has room for this.

**Overhead arithmetic**, for a 16-byte object:

| Design | Per-object overhead |
|---|---|
| Ruffus (`heap_header_t` 8 B + indirection block 8 B header + 4–8 B data ptr) | 20–24 B |
| [11_Memory_Model.md §4](11_Memory_Model.md) (8 B handle + 8 B object header) | 16 B |
| **This design** (8 B handle, no block header) | **8 B** |

On a 64 KB heap that is the difference between roughly 2,000 and roughly
2,700 live 16-byte objects.

`HEAP_DEBUG` restores an 8-byte guarded header with a signature word and a
back-reference, for exactly the corruption detection Ruffus's `signature`
field provides. Because all addressing is through handles, turning it on
changes no semantics.

### 5.2 Allocation

```c
object_id_t heap_alloc(obj_handler_t * oh, uint16_t space, uint16_t type, uint8_t kind,
                       uint32_t bytes);
```

**`bytes` is 32 bits even though the v1 payload is not.** The allocator's own
arithmetic stays 16-bit inside the segment; the parameter is wide so that
lifting the cap (§2.3.1) is a change to `heap.c` and not to every call site in
every embedder. The only behaviour the extra width adds in v1 is that an
oversized request fails cleanly at step 1 instead of truncating.

1. Round `bytes` up to 8. A request larger than the segment payload can never
   fit, and returns `OBJECT_NULL` — the only failure that does (A10).
2. **Size-class cache** — *deferred* (§6.3: it is an alternative to
   compaction, not a layer under it). v1 falls through to step 3.
3. Free list — first fit, address-ordered.
4. Bump the segment top.
5. On failure: collect, retry once; then try the next segment; then
   `on_no_memory`, which does not return.
6. Take a handle from the handle free list; on exhaustion, collect, retry
   once, then `on_no_memory`. The handle is secured before the payload is
   carved, so an `on_no_memory` that `longjmp`s never strands a block.
7. **Zero the payload.** Required by the CLI and by the collector — an
   unzeroed object-id field would be marked as a garbage handle index.

First fit plus address-ordered coalescing is the **decided** baseline.
Anything beyond it is a fragmentation question, not a speed question, and is
answered in §5.4.

### 5.3 Compaction

**Out of scope for the first release, and the intended successor to §5.4** —
cheap to add later because of §5.1 and §2.3.1, which is the reason the object
id exists:

1. Collect the live handles into an offset-ordered list (the sweep already
   walks the handle table; ordering is one sort over live handles, and
   compaction is rare).
2. Slide payloads toward the segment base, skipping `OBJECT_H_PINNED`.
3. Rewrite `handles[i].offset`.

No object id anywhere in the system changes. No pointer fixup in the heap, on
any stack, or in native code. Turning compaction on later is a change to
`heap.c` and `gc.c` and to nothing else; that is what buys the right to defer
it.

The two rules it depends on are in force from v1, so they cost nothing when
it lands: `OBJECT_H_PINNED` is honoured for any native call that holds a raw
pointer (`objects_pin`, §8), and no raw pointer survives a safepoint
(§2.1.1). Writing those late is how a moving collector becomes impossible.

### 5.4 Fragmentation, and what the free-list policy is actually about

Without compaction, the allocator's placement policy is the *only* defence
against external fragmentation — the state where 20 KB is free, and a 200-byte
request still fails because no single run is that long. On a 64 KB heap with
no MMU that is the realistic failure mode, far more likely than genuinely
running out of memory. Speed is not the issue: a first-fit walk over a
well-coalesced list is a handful of loads.

| Policy | External fragmentation | Cost | Verdict |
|---|---|---|---|
| First fit, address-ordered, coalescing | moderate; small blocks cluster at the base | one walk | **v1 baseline** |
| Best fit | lower, but leaves unusable slivers | full walk | not obviously better; measure |
| Segregated free lists per size class | low for the covered classes, worse internally | O(1) | deferred with the recycler (§6.3) |
| Bump-only + compaction | none | needs a moving collector | **intended successor** — §5.3 |

The measurement that decides it, taken from the host build, is not
"allocations per second". It is, at every collection:

- the allocation size histogram,
- peak live bytes against heap size,
- **largest free block ÷ total free bytes** — the fragmentation ratio proper.

**Order of work.** v1 ships first fit, non-moving, and instruments those
three numbers. If the ratio stays high, first fit is finished and nothing more
is needed. If it decays over a long run, the answer is compaction (§5.3), not
a cleverer free list — so the measurement decides *when*, not *what*.
Choosing a successor policy before those numbers exist is guessing.

### 5.5 Segments — one collectable heap in v1

The object id reserves 16 heap spaces, so the design *can* carry several
segments. Whether it *should* partition them by object category — one segment
for class instances, another for arrays and strings, another for long-lived
structures — is worth settling explicitly, because the instinct is a good one
and the arithmetic on a small part is against it.

**What Ruffus does**, for reference: one unified heap per application
instance (`app_instance_t.heap_data`), holding class instances, arrays,
strings, boxed primitives and native objects together. `vm_new()` asks
`app_class_total_fields(cref)` for the field count and calls
`object_create(cref, T_CLASS, total_fields)`, which allocates **one
contiguous block**: a 4-byte class reference followed by `total_fields`
inline `object_t` handles. Statics are a separate flat `object_t[]` from
`heap_malloc`. There are no pools and no generations.

**What changes here, and why it removes most of the motivation.** The three
categories that would most obviously deserve their own segment are not on the
heap at all:

| Category | Ruffus | Here |
|---|---|---|
| Class structures — type records, vtables, field maps, interface maps | built in RAM at load | **image, `OBJECT_SPACE_CONST`** — read in place from flash, never allocated |
| Static fields | heap block of `object_t` | **statics slab, `OBJECT_SPACE_STATIC`** — immortal, sized at pack time |
| Short-lived temporaries and boxes | heap | **frame scratch, `OBJECT_SPACE_FRAME`** — LIFO, released by `ret` |
| String literals | constant pool | **image bytes, read in place as constants** — §3.5 |

The space tag already gives "different lifecycle ⇒ different region" for
everything whose lifecycle is statically known. What is left in the
collectable heap is exactly the set whose lifetime is *not* statically known,
and splitting that set by type does not make any lifetime more predictable.

**The cost of partitioning fixed RAM.** Two segments of 8 KB are strictly
worse than one segment of 16 KB: a program that allocates 12 KB of arrays and
2 KB of instances fails in the first arrangement and succeeds in the second.
A segment cannot borrow from its neighbour, so every partition converts spare
capacity into reserved waste, and each one costs its own handle table, free
list and recycler.

**Decision for v1: one collectable segment, on MCU and on host alike**
([11 §4.6](11_Memory_Model.md)). The mechanism stays; the policy does not use
it. Two candidate partitions are recorded for when measurement asks for them:

1. **Ref-free payloads** — `byte[]`, `int[]`, string bodies, `FieldRVA`
   copies. A segment whose objects are all `!OBJECT_H_HASREFS` never has its
   payloads scanned during marking, and is the only split that makes the
   *collector* faster rather than merely tidier.
2. **Large, long-lived buffers** — anything over a few hundred bytes, kept
   away from the small-object region so that instance churn cannot strand
   them. This is fragmentation isolation, and it competes directly with
   compaction (§5.3); do one or the other, not both.

Both are enabled by registering a second space and choosing it in
`heap_alloc` from `kind` and `bytes`. Neither is written before
§5.4's fragmentation ratio says it is needed.

### 5.6 String payloads — UTF-8

ECMA-335 defines `System.String` as UTF-16, and the `#US` heap is UTF-16LE.
This design stores string payloads as **UTF-8**, selected by
`OBJ_STRING_ENCODING` (default `UTF8`) and **the same on MCU and host**, for
the reason in [11 §4.6](11_Memory_Model.md): a host build that stored strings
differently would stop being a rehearsal for the device.

**Why.** Embedded literals are overwhelmingly ASCII, where UTF-8 halves every
string body — on a 64 KB heap that is a heap-capacity decision, not a
micro-optimisation. It also lets a literal be read **in place from flash**
through `OBJECT_SPACE_CONST` with no copy and no byte-swapping, and it removes
the conversion at every native boundary, where the file and stream layers want
bytes.

**What it costs, stated plainly.** `String.Length`, `Chars[i]`, `ToCharArray`
and `IndexOf` are defined over UTF-16 code units. Over UTF-8 bytes they are
either byte-indexed (wrong for non-ASCII) or O(n) (correct and slow). This is
a **deliberate deviation from ECMA-335**, and it is confined entirely to text
outside U+0000–U+007F.

**The packer keeps the deviation out of reach.** `cli_pack` decodes `#US` and:

| Literal content | `cli_pack` |
|---|---|
| U+0000–U+007F only | convert to UTF-8, emit — one byte per `char`, exact CLI semantics |
| any code point ≥ U+0080 | **error**: assembly, token and the offending literal are named |
| unpaired surrogate | **error** — not representable, never silently replaced |
| `--allow-non-ascii` | convert, emit, and set an image flag that makes `Length` and `Chars[i]` O(n)-correct at run time |

The default is the error, deliberately: an ASCII-only program gets exact
semantics at one byte per character, and a program that needs more has to say
so. Nothing is silently wrong either way. Run-time strings follow the same
rule — `obj_string_create` takes UTF-8, the `char`-level API decodes rather
than indexes, and interning compares bytes.

**Layout and length (D9, 2026-09-28).** A string's payload is its UTF-8
bytes followed by one NUL, with no header: `handle.size` is the byte count
plus one, and a native can hand the payload to C without copying.
`OBJECT_H_ASCII` (§3.2) is set at creation when every byte is below 0x80.
`obj_string_length` counts UTF-16 code units, as ECMA-335 defines `Length`:
the byte count when `OBJECT_H_ASCII` is set, a decode otherwise, so a
supplementary character counts two. `obj_string_char_at` returns the UTF-16
unit at an index — a surrogate, for either half of a supplementary
character — or -1 out of range, which the interpreter turns into
`IndexOutOfRangeException`. `obj_string_create` validates strictly: overlong
forms, truncated sequences, surrogates and code points above U+10FFFF return
`OBJECT_NULL`, and the caller throws or substitutes U+FFFD first. The type id
of every string is `obj_handler_config_t.string_type` (§9). A literal is a
constant, and `obj_string_intern` returns its id (§3.5).

### 5.7 Array payloads — one-dimensional, inline elements

An array is an ordinary object with a known payload layout: an 8-byte header,
then `count` elements stored inline. It is one-dimensional and zero-based,
and its length never changes.

```c
typedef struct {            /* first 8 bytes of an OBJECT_K_ARRAY payload */
    uint16_t count;
    uint16_t elem_size;
    uint32_t _reserved;     /* keeps elements 8-byte aligned               */
} array_header_t;
```

The header is declared in `objects_types.h`, not `array.h`, because the
collector reads it and `gc.c` is written before `array.c`
([18 §5](18_ObjectHandler_Build_Plan.md)). It is 8 bytes rather than 4 because
a 4-byte header would misalign every `int64` and `double` element, which
faults on some ARM configurations. `count` and `elem_size` are 16-bit for the
reason `object_handle_t.offset` is (§2.3), and widen with it.

**Rule: for an `OBJECT_K_ARRAY`, `type_refmap(type)` describes one element,
and the collector applies it `count` times.** The handle's `type` is the
embedder's id for the *array* type, not the element type — the element type's
refmap would describe a `String` object, not a reference to one.

| Element | Example | `elem_size` | Refmap for the array type | Collector |
|---|---|---|---|---|
| primitive | `byte[]`, `int[]`, `double[]` | 1–8 | NULL, so `OBJECT_H_HASREFS` is clear | never scans it; contiguous, so it can be pinned for I/O |
| reference | `object[]`, `string[]`, `int[][]` | 4 | bit 0 alone | scans every element as an id |
| struct | `Point[]`, a `Dictionary`'s `Entry[]` | the struct's size | the struct's refmap | scans those words in each element |

That rule is why there is no `array_shape` callback (§7.5) and no "elements
are references" flag. It puts two obligations on the embedder: a non-NULL
element refmap covers exactly `elem_size` bytes (a debug build checks this at
creation), and every id inside an element is 4-byte aligned, because refmaps
work in whole words.

**Not an array of ids only.** That would box every `byte[]` and `int[]`
element: `new byte[1024]` would cost about 20 KB and 1,025 handles — half the
default table (§9.1) — instead of about 1 KB and one handle, and no buffer
could reach a peripheral as contiguous memory ([11 §8.4](11_Memory_Model.md)).

**Out of scope, deliberately:**

- **Resizing.** CIL arrays have a fixed length; `List<T>` grows in managed
  code by allocating a new array. Ruffus's in-place `array_insert` and
  `array_delete` are not carried over.
- **Rank above 1.** `int[,]` is an embedder construct — a small object holding
  the bounds, plus one one-dimensional array. The reserved header word stays
  reserved.
- **Element conversion.** `array_copy` moves bytes between arrays of the same
  `elem_size`; a widening `Array.Copy` is a loop in the embedder.

**Element access.** One function returns the address of any element, whatever
its type. CIL has a dozen `ldelem`/`stelem` forms plus structs of any size, and
which form an access is — so which promotion or narrowing it needs (§III.1.5)
— is the interpreter's knowledge, not the library's (§1): the library hands
out an address and the interpreter does the typed load or store:

| CIL | Interpreter | ObjectHandler |
|---|---|---|
| `ldlen` | push the length | `array_length` |
| `ldelem.*` | read through the pointer, then promote | `array_element_ptr` |
| `stelem.*` of a primitive | narrow, then write through the pointer | `array_element_ptr` |
| `stelem.ref` | check the element type, then store through the barrier | `array_element_ptr`, then `objects_write_ref` |
| `ldelem` / `stelem` of a struct | copy `elem_size` bytes | `array_element_ptr` |
| `ldelema` | push a byref slot | `array_element_byref` |
| a native taking a buffer | pin, then use the range | `array_range_ptr` |

A pointer from `array_element_ptr` is valid until the next safepoint, like any
resolved pointer ([11 §4.3](11_Memory_Model.md)). `ldelema` is the one case
where the address must outlive that, because it sits in a stack slot, so it
gets an `object_byref_t` instead — the array's id and the element's offset —
which survives compaction (§4). A struct copied into an element carries its
ids with it; §11 records what that means for the barrier.

**The range rule.** The range check lives in `array_range_ptr`, and
`array_element_ptr` and `array_element_byref` go through it. They are the only
functions that treat a bad index as the *program's* error: they return NULL,
or a byref whose `id` is `OBJECT_NULL`, and the interpreter throws
`IndexOutOfRangeException`. The check compares in elements, so no index or
count can wrap past it. Anything else passed in wrong — a bad range to
`array_copy` or `array_clear`, an id that is not an array, `OBJECT_NULL` —
means the embedder skipped a check, and goes to `on_corrupt` (§9.2).

An array whose bytes live in the image, such as a `FieldRVA` initialiser
([15 §5.1](15_Pack_Time_IL_Transforms.md)), uses the same layout as a
constant: its type, kind and size come from its constant-table entry (§3.5).

---

## 6. The stack, and the allocation question

This section answers the two questions raised against Ruffus's stack: what a
better arrangement looks like, and whether a cache can make "every operation
allocates" acceptable.

**The short answer to the second is: do not make allocation cheap enough to
tolerate — make most of it not happen, and make the rest LIFO.** Three tiers,
in order of how much they remove.

### 6.0 What v1 builds, and what it defers

"Working release first" is the right rule. It does not, however, put all
three tiers on the far side of the line, because **tiers 0 and 1 are not
optimisations — they are the data structure**:

| Tier | v1 | Why |
|---|---|---|
| 0 — typed slots (§6.1) | **in** | A tagged union is *less* code than boxing every `int`, not more: the alternative is porting `value_create_decimal`, its allocation path and its GC interaction, then deleting all of it and rewriting every opcode handler. It is also what makes the collector precise over the stack with no stack maps (§7.4) |
| 1 — contiguous stack, overlapped frames (§6.2) | **in** | Four pointers into one array, and a call that copies nothing. Copying arguments instead is more code *and* has to be undone later |
| 2 — frame scratch + promotion (§6.3, §6.5) | **deferred** | A real optimisation resting on a real analysis (§6.4). Heap allocation is the correct fallback for every one of its cases, by construction |
| 2 — size-class recycler (§6.3) | **deferred** | §5.2 step 2; it competes with compaction rather than complementing it |

So v1 heap-allocates every `box`, every short-lived `newobj` and every
value-type temporary, and collects them. That is the "allocate freely"
release — but note what it is *not*: it is excessive **object** allocation,
not excessive **arithmetic** allocation. `1 + 2` still costs nothing, because
that is a property of the slot, not of the arena. Ruffus's actual problem was
the second one.

**Three things must exist in v1 anyway**, so that tier 2 stays a local change:

- `OBJECT_SPACE_FRAME` stays in the catalogue (§3.1), unused. Adding a space
  later must not renumber one.
- Every reference store goes through `objects_write_ref` from the first
  commit, even though `objects_escape` is the identity function in v1. A
  barrier retro-fitted across a working interpreter is how escapes get missed.
- `cli_pack` reserves the per-method `scratch_bytes` field (§6.4) and emits
  zero. Reserving an image field is free; adding one later is a format break.

### 6.1 Tier 0 — typed slots, so primitives never allocate

Ruffus pushes `value_create_decimal(v)` for every integer, so `1 + 2`
allocates a heap object, dereferences two more, and produces garbage. That is
defensible for slow Java control logic and not defensible for CIL, which has
natively typed stack slots (§III.1.1).

```c
enum {                                  /* slot kind — what the slot holds */
    STACK_K_EMPTY = 0,
    STACK_K_I4, STACK_K_I8, STACK_K_R8, STACK_K_NINT,
    STACK_K_REF,                    /* O — u.id                     */
    STACK_K_BYREF,                  /* & — u.byref_base + aux (§4)  */
};

typedef struct {
    uint8_t  kind;
    uint8_t  flags;                 /* reserved                     */
    uint16_t aux;                   /* byref offset — see below     */
    union {
        int32_t     i4;
        int64_t     i8;
        double      r8;
        int32_t     nint;           /* native int is 32-bit — 15 §11 */
        object_id_t id;
        object_id_t byref_base;
    } u;
} stack_slot_t;                     /* 16 bytes, 8-byte aligned     */
```

Both are declared in `objects_types.h`, not `stack.h`: `value.c` reads slots
(§6.7) and is built before `stack.c`, as the array header is declared there
for the collector (§5.7).

Folding the byref offset into `aux` keeps the union at exactly 8 bytes. That
is not a v1 saving — natural alignment pads the struct to 16 either way — but
it is the precondition for the structure-of-arrays split (a `uint8_t kind[]`
beside a `uint64_t value[]`, 9 bytes per slot) that
[11_Memory_Model.md §6.2](11_Memory_Model.md) records as a later
optimisation. Do the `aux` trick now, defer the split.

The tag is not overhead. It is what makes the collector precise over the
stack with no stack maps — the structural advantage an interpreter has over a
JIT.

A slot that is a local or an argument is a *location* of its declared type:
it holds that type's storage form, which differs from the stack form only
for the short integers and `float32` (§4.1).

### 6.2 Tier 1 — one contiguous stack, frames overlaid, arguments not copied

Ruffus already keeps `stack`, `sp`, `stack_base` and `locals` as four
pointers into one physical array. That instinct is right and should be taken
further.

**One stack per managed thread (OH-4).** The heap, the statics and the
constants belong to the application; the stack is the one piece of state
each managed thread has for itself. Its record, `stack_thread_t`, is passed
to every per-thread call (§8), so switching threads is the scheduler passing
a different record: there is no current-thread pointer to switch, in a
global or in the instance. The VM owns the record — it is the `ctx` of its
`exec_thread_t` ([17 §3](17_Threading_Model.md)) — and supplies the
thread's memory, an array of `stack_slot_t` and an array of `stack_frame_t`,
to `stack_thread_init`; `stack_thread_end` takes the thread off the ring
(§7.4) and hands the arrays back. The library allocates neither, so
`stack.c` has no static storage.

```c
typedef struct {
    stack_slot_t  * args;           /* this + declared parameters           */
    stack_slot_t  * locals;         /* typed zeros from the push            */
    stack_slot_t  * stack;          /* evaluation stack base                */
    const uint8_t * ip;             /* the VM's; never read by the library  */
    uint16_t        method;         /* embedder method id                   */
    uint16_t        max_stack;      /* evaluation slots reserved            */
} stack_frame_t;
```

Per thread there are two stacks, each an array the VM supplies: a value
stack of `stack_slot_t`, and a control stack of `stack_frame_t`. Frames are
an array, so a frame's caller is the frame below it and needs no link. `sp`
points one past the top slot, so a thread's live slots are
`[stack_base, sp)`.

The point of the arrangement is that **a call copies nothing**:

```
 caller:  [ args | locals | eval ...................... a0 a1 a2 ]
                                                        ^
 callee:                                               args
          [ args | locals | eval ... ]
```

`stack_frame_push` sets `callee->args = caller->sp - arg_slots`, makes each
local a zero of the kind `local_kinds` gives it — `NULL` leaves them `EMPTY`,
for a stack used without CIL — and bumps past them to `stack`.
`stack_frame_pop(t, ret_slots)` moves the return value into
`callee->args[0]` and sets `caller->sp = callee->args + ret_slots`,
`ret_slots` being 0 or 1. Argument passing costs two pointer arithmetic
operations regardless of argument count, and the evaluation stack of the
caller *is* the argument area of the callee. A fresh local is a zero of its
kind — a `REF` local holds `OBJECT_NULL` — so a stale id left by a deeper
call that has returned is never scanned as a root, and `ldloc` of a local
never stored pushes a typed zero (§4.1).

Two consequences worth stating, because they are easy to lose later:

- The frame record must **not** be interleaved with the slots. Keeping frames
  on their own stack is what lets the callee's argument window overlap the
  caller's evaluation stack.
- **Overflow is checked once per call, in every build.** `stack_frame_push`
  reserves the callee's locals and its `max_stack` evaluation slots, and
  returns `NULL` when they, or one more frame, do not fit; the VM then throws
  `StackOverflowException` in that thread. CIL's verifier bounds every
  method by its `MaxStack`, so no push inside the frame can pass the array.
  A debug build also checks every push and pop against the frame and the
  slot kind, and reports a violation to `on_corrupt`. An embedder without
  `MaxStack` — one that pushes with no frame at all — asks `stack_room(t)`
  first. There is no guard page on an MCU.

### 6.3 Tier 2 *(deferred — §6.0)* — the scratch space, and the recycler

Not built in v1. It is specified here in full because the decision that
matters now is which *hooks* v1 carries, and those are listed in §6.0.

What still allocates after tier 0: `box`, `newobj` of short-lived objects,
`localloc`, temporary value-type instances, `constrained.` boxing, and
string temporaries. The question was whether a stack-adjacent cache can make
these cheap. It can for all but `localloc`, whose size is only known at run
time (§6.4), and the space tag from §2 is what makes it safe.

**Frame scratch — `OBJECT_SPACE_FRAME`.** Each thread's stack region ends with
a scratch arena. `stack_scratch_alloc(bytes)` bumps a pointer and returns
`OBJECT_ID(OBJECT_SPACE_FRAME, offset)`. `ret` restores the frame's `scratch`
high-water mark, releasing everything the frame allocated in one store.

- Allocation cost: a bounds check and an add.
- Release cost: one store, per frame, for any number of objects.
- Collector cost: none — `OBJECT_SPACE_FRAME` is not a collected space, and
  the scratch arena is scanned as a root along with the rest of the stack.

**This is exactly Ruffus's `CONST_OBJECT_ID` test generalised.** Ruffus
writes `if (object >= CONST_OBJECT_ID) continue;` in five places in `gc.c`.
Here the collector writes
`if (!(oh->spaces[OBJECT_SPACE(id)].flags & OBJECT_SP_COLLECTED)) continue;`
once, and gets constants, statics, scratch, foreign memory and every
embedder-defined space for the same instruction.

**Escape rule.** A scratch object must never outlive its frame. Two
mechanisms, and the second one is the one that must be trusted:

1. *Pack-time escape analysis.* `cli_pack` marks a `box` site as scratch when
   its result is consumed within the same frame and never stored to a field,
   a static, an array element, or returned. The conservative default is heap.
   This is a fold; see
   [15_Pack_Time_IL_Transforms.md](15_Pack_Time_IL_Transforms.md) §4.
2. *Promoting write barrier.* Every reference store already goes through a
   barrier ([11_Memory_Model.md §7.6](11_Memory_Model.md)). Give it a body:

```c
static inline object_id_t objects_escape(object_id_t id)
{
    return (OBJECT_SPACE(id) == OBJECT_SPACE_FRAME) ? objects_promote(id) : id;
}

static inline void objects_write_ref(object_id_t * slot, object_id_t value)
{
    *slot = objects_escape(value);      /* copies to the heap if escaping */
}
```

Both exist in v1 with `OBJECT_SPACE_FRAME` never produced, so `objects_escape`
folds to the identity and the barrier to a plain store — at zero cost, and
with every call site already in place (§6.0).

`ret` applies `objects_escape` to a returned object id, **before the frame is
torn down** (§6.5). The analysis in (1) only determines how often
`objects_promote` fires; **correctness does not depend on it.** That is the
property that makes the optimisation safe to ship before the analysis is
good.

**Size-class recycler.** For objects that genuinely reach the heap, keep a
small LIFO cache of recently swept blocks per size class — 8, 16, 24, 32, 48,
64 bytes — refilled by the sweep and drained by `heap_alloc` step 2.
Allocation of the common sizes becomes an array pop with no free-list walk.
Cache depth is a profile constant; 8 entries per class costs 6 × 8 × 2 bytes
of offsets.

**Deferred, and possibly permanently.** The recycler is a workaround for a
non-moving heap: it exists to avoid a free-list walk and to keep same-sized
blocks reusable in place. Compaction (§5.3) removes both reasons at once — a
compacted segment allocates by bumping a pointer, which is already O(1) and
already defragmented, and a size-class cache in front of it is pure overhead
with a residency cost. So the two are alternatives, not layers. v1 ships
neither, measures §5.4, and the ratio decides which one is worth writing.

### 6.4 Sizing the scratch arena from the image *(deferred with §6.3)*

The packer can compute a defensible number, and most of it is already
decoded. Per method, from metadata alone:

| Quantity | Source | Exact? |
|---|---|---|
| evaluation slots | `MaxStack` in the fat method header — already parsed as [`max_stack`](../source/methods/methods_type.h); 8 implied for the tiny format | **yes**, it is a verifier-enforced bound |
| local slots | `LocalVarSig` via `local_var_sig_tok`, already decoded by [method_parser.c](../source/methods/method_parser.c) | **yes** |
| argument slots | the `MethodDef` signature, already decoded | **yes** |
| scratch bytes | Σ instance sizes of the `box` sites and value-type temporaries classified scratch on the longest path through the method — the layout pass knows every size | **yes for boxes** |
| `localloc` bytes | operand is a run-time value | **no** |

So a per-method `scratch_bytes` is computable, and a per-thread figure is the
maximum over the deepest reachable call chain — the same walk that sizes the
value stack. Two things defeat an exact answer, and both are the same class
of problem the stack region already has:

- **Recursion** makes the chain unbounded, so the deepest-chain figure is
  multiplied by a configured recursion budget, exactly as in
  [11 §4.5](11_Memory_Model.md).
- **`localloc`** is data-dependent and is therefore *never* placed in
  scratch: it allocates from the heap or throws. That keeps the estimate
  honest.

**Decision:** `cli_pack` emits a per-method `scratch_bytes` and an image-wide
recommended per-thread scratch size; the header carries it as a minimum, the
launch flag or board manifest may raise it. **Scratch overflow falls back to
`heap_alloc` and is never an error** — that fallback is mandatory, which is
what lets the estimate be an estimate.

**In v1 the fields are reserved and zero** (§6.0). The packer computes
nothing; the image format is already correct for the day it does.

### 6.5 Promotion on return *(deferred with §6.3)*

A returned value is the one way a scratch object legitimately outlives the
frame that made it: `ret` restores the scratch high-water mark, so the bytes
behind a returned `OBJECT_SPACE_FRAME` id are handed to the next call. The
case is concrete — a method that boxes an `int` and returns it as `object`.

Two points, which is all the question was ever about:

1. **Ordering.** `objects_escape` on the return value runs **before**
   `stack_frame_pop`, while the callee frame is still linked and its slots
   are still scannable, because promotion allocates and allocation can
   collect. Promoting after the pop would scan a frame that has already been
   released.
2. **Frequency.** It should never fire. A `box` whose result is returned is
   not a non-escaping box, so §6.3 rule (1) classifies it as heap at pack
   time. The barrier at `ret` is the backstop for a wrong classification, not
   the normal path — debug builds count promotions at `ret` and a non-zero
   count is a packer bug worth investigating, not a run-time fault.

Only the return value is touched. Everything else the frame allocated dies
with the high-water mark, which is the point of the arena.

### 6.6 What this rejects

`ruffusvm/ObjectHandler/stack.h` and `value_type.c` are not ported, in any
form. `stack_push_integer`, `value_create_decimal` and the whole
`value_get_*` family exist only to make boxing bearable, and tier 0 removes
the reason they exist. `value_compare()` keeps its name and loses the
allocation, but there are no rules to port with it: Ruffus refuses operands
of different types and has no promotion table, so §6.7 writes the rules from
ECMA-335 (B6).

Also rejected: `instance_get_current()` inside every accessor. Ruffus reaches
the stack through a hidden global on the hottest path in the VM; here the
thread is a parameter (§8).

### 6.7 Normalisation — comparing and converting slots (B6)

Decided 2026-09-29. `value.c` compares any two slots and converts any
numeric slot to any primitive type. That is mechanism, and the library owns
it (§1); which comparison or conversion an opcode performs, and whether its
operand types are valid under §III.1.5, is the interpreter's policy. Ruffus
has no promotion table to port — its `value_compare` refuses operands of
different types — so the rules are written here, from ECMA-335.

```c
enum { VALUE_LESS = -1, VALUE_EQUAL = 0, VALUE_GREATER = 1,
       VALUE_UNORDERED = 2 };                   /* value_compare's result */
enum { VALUE_UNSIGNED = 0x01,                   /* integers read unsigned */
       VALUE_CHECKED  = 0x02 };                 /* value_convert: must fit */
enum { VALUE_T_INT8 = 1, VALUE_T_UINT8, VALUE_T_INT16, VALUE_T_UINT16,
       VALUE_T_INT32, VALUE_T_UINT32, VALUE_T_INT64, VALUE_T_UINT64,
       VALUE_T_FLOAT32, VALUE_T_FLOAT64, VALUE_T_NATIVE_INT,
       VALUE_T_NATIVE_UINT, VALUE_T_COUNT };    /* value_convert's targets */
```

**Comparison.** `value_compare(oh, a, b, flags)` normalises both operands to
a common form and orders them:

| Operands | Common form | Order |
|---|---|---|
| `I4`, `NINT`, `I8`, in any mix | 64 bits: sign-extended, or zero-extended from the operand's own width under `VALUE_UNSIGNED` | signed, or unsigned under `VALUE_UNSIGNED` |
| an integer and an `R8` | the integer as `value_convert` to `VALUE_T_FLOAT64` makes it | IEEE: `-0.0` equals `+0.0`, and a NaN is `VALUE_UNORDERED` |
| two `R8` | double | the same |
| two `REF` | the id | `OBJECT_NULL` below every object |
| two `BYREF` | the address each designates (§4); for two stack byrefs, their slot and offset (§4.1) | unsigned; equal when both designate the same byte. A stack byref and any other byref are `VALUE_UNORDERED`: they never designate the same byte, and without the thread there is no address to order |
| any other pair | none | `VALUE_UNORDERED` |

So `I4 5` equals `NINT 5`: the kinds stay distinct (§6.1), and their values
compare, as §III.1.5 has it. An integer meets a double through the
conversion `conv.r8` performs, so it loses precision above 2^53, as C# does.
A byref never equals a number: no byref is null (§4), and no host pointer
enters a `native int` ([15 §11](15_Pack_Time_IL_Transforms.md)). `EMPTY` or
an unknown kind goes to `on_corrupt` and compares `VALUE_UNORDERED`. The
interpreter maps each opcode onto the result, passing `VALUE_UNSIGNED` for
the `.un` forms:

| Opcode | True when the result is |
|---|---|
| `ceq`, `beq` | `EQUAL` |
| `bne.un` | not `EQUAL` |
| `cgt`, `bgt` / `clt`, `blt` | `GREATER` / `LESS` |
| `cgt.un`, `bgt.un` / `clt.un`, `blt.un` | `GREATER` or `UNORDERED` / `LESS` or `UNORDERED` |
| `bge`, `ble` | `GREATER` or `EQUAL` / `LESS` or `EQUAL` |
| `bge.un`, `ble.un` | not `LESS` / not `GREATER` |

`cgt.un x, null` is then the null test ECMA-335 intends, since null is below
every object.

**Conversion.** `value_convert(oh, src, to, flags, dst)` converts an `I4`,
`I8`, `NINT` or `R8` to one of the twelve targets, and leaves the result in
the target's stack kind:

| Target | Result |
|---|---|
| `INT8`, `UINT8`, `INT16`, `UINT16`, `INT32`, `UINT32` | `I4`, extended by the target's signedness |
| `INT64`, `UINT64` | `I8` |
| `NATIVE_INT`, `NATIVE_UINT` | `NINT`, 32 bits ([15 §11](15_Pack_Time_IL_Transforms.md)) |
| `FLOAT32`, `FLOAT64` | `R8`; `FLOAT32` rounded to float precision |

- An integer source is read as a comparison reads it: sign-extended, or
  zero-extended from its own width when `VALUE_UNSIGNED` is set or an
  unchecked conversion goes to an unsigned target — Table III.8 zero-extends
  an `int32` converted to `unsigned int64`. The target keeps the low bits,
  extended by its own signedness. Converted to a float, an integer is rounded
  once, straight to the target precision.
- A float source is truncated toward zero. Unchecked, a value outside the
  target's range saturates at its bounds and a NaN becomes 0. ECMA-335 leaves
  that result unspecified and C leaves it undefined; saturation is what
  CoreCLR does since .NET 9, and for 8- and 16-bit targets since .NET 11. A
  double beyond float's range becomes an infinity. `VALUE_UNSIGNED` has no
  effect on a float source.
- `VALUE_CHECKED`: a value that does not fit the target, or a NaN, returns 0
  and leaves `dst` untouched, and the interpreter throws
  `OverflowException`. A float target always fits.
- `src` and `dst` may be the same slot. A `REF`, `BYREF` or `EMPTY` source,
  or an unknown target, goes to `on_corrupt` and returns 0: a reference has
  no integer form here, since a `native int` is 32 bits and no host pointer
  may enter one.

The interpreter's map: `conv.X` passes no flag, `conv.ovf.X`
`VALUE_CHECKED`, and `conv.ovf.X.un` `VALUE_CHECKED | VALUE_UNSIGNED`;
`conv.r.un` is `VALUE_T_FLOAT64` with `VALUE_UNSIGNED`.

---

## 7. Collector

### 7.1 Algorithm

Unchanged from [11_Memory_Model.md §7](11_Memory_Model.md) and from Ruffus's
`gc.c`: **tricolour mark-sweep, precise, non-moving in v1,
stop-the-world at a safepoint.** Ruffus proves the algorithm; only its data
structures are replaced.

### 7.2 Structures

| Ruffus | Here | Why |
|---|---|---|
| three explicit lists searched linearly (`_object_tc_gc_search`), O(n²) | mark bit in `object_handle_t.flags` | the handle table is already the dense index; a scan is a strided byte test |
| grey set found by re-scanning | bounded pre-allocated worklist of `uint16_t` handle indices | no re-scan, bounded RAM |
| `if (object >= CONST_OBJECT_ID)` in five places | one space-flag test | §6.3 |

[11_Memory_Model.md §7.1](11_Memory_Model.md) specified a *side* mark bitmap
keyed by handle index. **That is superseded here:** the mark bit goes in the
handle, because marking already loads the handle to reach `offset` and
`type`, and a side bitmap would double the memory traffic to save one bit per
object. The sweep walks the 8-byte handle array linearly, which is the same
access pattern a bitmap would have had.

On worklist overflow: set a flag, drain, then re-scan handles that are
`OBJECT_H_MARK` but not `OBJECT_H_SCANNED` until the flag clears. Bounded
memory beats elegance on a device.

### 7.3 When the collector runs — the trigger

Allocation failure alone is the wrong trigger: it guarantees that every
collection happens at the least convenient moment, on a full heap, with the
longest possible mark phase, in the middle of whatever the program was doing.

**Decision: both triggers, and every number configurable.** An occupancy
threshold tested in `heap_alloc`, *and* an unconditional collect on allocation
failure.

```c
if (segment->used + bytes > segment->collect_at)
    gc_request(oh);          /* collect at the next safepoint */
```

| Knob | Default | Note |
|---|---|---|
| `gc_threshold_pct` | **75 %** of segment payload bytes | config field, per-segment; image header supplies it |
| `gc_handle_pct` | **75 %** of handle-table entries | a program can exhaust handles with the payload half empty |
| `gc_recover_pct` | **25 %** — below this a collection counts as unproductive | damping input |
| `gc_damp_step_pct` / `gc_damp_cap_pct` | **+10** points, capped at **90 %** | stops a near-full heap from collecting on every allocation |
| `gc_threshold_pct = 0` | — | disables the threshold; allocation failure becomes the only trigger |

The defaults are starting values, not measurements, which is exactly why they
are fields rather than constants: a board manifest or a launch flag can move
any of them without a rebuild of the library. The same defaults apply on MCU
and host ([11 §4.6](11_Memory_Model.md)); a host build that collected
differently would stop being a rehearsal for the device.

**The two paths differ in kind, and that is the point:**

- The threshold **requests**: `gc_request()` sets `gc_requested` and the
  collection happens at the next safepoint
  ([17 §4.4](17_Threading_Model.md)), never in the middle of an allocation
  sequence.
- Allocation failure **collects, immediately and synchronously**, then
  retries once (§5.2 step 5). There is nothing to defer to — the caller
  cannot proceed. The requirement this places on callers is the one §2.1.1
  already states: every live id must be reachable from a slot or a root
  before `heap_alloc` is called, because that call can collect.

So the threshold can be wrong, or switched off entirely, without being fatal.

### 7.4 Roots

| Root | Enumeration | Precision |
|---|---|---|
| Every thread's value stack | walk threads, then slots | self-describing via `stack_slot_t.kind` |
| Every thread's frame scratch *(tier 2 — §6.3; absent in v1)* | walk the scratch arena | scratch object refmaps |
| Statics slab | walk the slab | statics reference bitmap |
| Constants | none: they hold no ids (§3.5) | — |
| Native scopes | walk the scope chain | every entry is an object id |

"Every thread's" rather than "the thread's" — see
[17_Threading_Model.md](17_Threading_Model.md). Green threads make this
enumeration *easier*, not harder: all of them are parked at their own
safepoints by construction.

**How `gc.c` reaches the stacks (A7).** `stack.c` registers one root-scan
function with `gc.c` when it initialises; `gc.c` never includes `stack.h`.
The collector (OH-3) can therefore be tested against a fake scanner, and
the stack (OH-4) against a recording visitor, whichever comes first.

**The seam (OH-4).** `stack_thread_init` registers `stack_scan_roots(oh,
visit)` through `gc_register_root_scan`. Called with the collector's
visitor, it reports every non-null `STACK_K_REF` id and `STACK_K_BYREF` base
in `[stack_base, sp)` of every thread on the instance's ring; which of them
are collectable is the collector's to decide. The ring's head is in
`obj_handler_t`, and `stack_thread_end` takes a thread off it, so an ended
thread's slots stop being roots.

**No `[ThreadStatic]` in v1 (A12).** `cli_pack` rejects the attribute and
names the field, so the statics slab stays one root shared by every thread
([17 §11](17_Threading_Model.md)).

### 7.5 Host interface

The collector cannot know which words of a payload are object ids. The
embedder tells it:

```c
typedef struct {
    /* Reference bitmap for a type: bit i set => word i of the payload
       is an object_id_t. Return NULL if the type contains no ids.
       For an array type, the bitmap of one element (§5.7).           */
    const uint8_t * (*type_refmap)(obj_handler_t * oh, uint16_t type, uint32_t * words);

    void (*on_no_memory)(obj_handler_t * oh);   /* still no room after a collection;
                                                   must not return — §5.2 */
    void (*on_corrupt)(obj_handler_t * oh, const char * where, object_id_t id);
} obj_handler_host_t;
```

Three callbacks, all required (§9.2). Each receives the instance that calls
it, so the embedder finds its own state from it — by embedding the
`obj_handler_t` in its application record, say — without a global (I2).
Everything else the collector needs is in the handle or, for an array, in its
payload header (§5.7), which is why there is no `array_shape` callback.

**What the collector scans, per kind (A3):**

| Kind | Scanned |
|---|---|
| `OBJECT_K_OBJECT` | by the type's refmap |
| `OBJECT_K_BOX` | by the boxed value type's refmap |
| `OBJECT_K_ARRAY` | by the element refmap, `count` times — §5.7 |
| `OBJECT_K_STRING`, `OBJECT_K_RAW` | never: `heap_alloc` does not set `OBJECT_H_HASREFS` for them |

**Every id in a payload is 4-byte aligned (A11).** Refmap bit *i* marks the
word at offset 4·*i*, so an id anywhere else could not be described.
`cli_pack`'s layout pass guarantees it for fields, statics and struct
elements ([11 §9](11_Memory_Model.md)).

---

## 8. Public API

One header per module; `obj_handler.h` includes them all, exactly as Ruffus's
`obj_handler.h` does.

```c
/* ---- obj_handler.h : library lifecycle ------------------------------ */
obj_handler_status_t obj_handler_init(obj_handler_t * oh,
                                      const obj_handler_config_t * cfg,
                                      const obj_handler_host_t   * host);
void                 obj_handler_shutdown(obj_handler_t * oh);

/* ---- objects.h : identity, resolution, lifetime --------------------- */
static inline void * objects_ptr(obj_handler_t * oh, object_id_t id);
static inline void * objects_byref_ptr(obj_handler_t * oh, object_byref_t b);
uint16_t    objects_type_of(obj_handler_t * oh, object_id_t id);
uint32_t    objects_size_of(obj_handler_t * oh, object_id_t id);
uint8_t     objects_kind_of(obj_handler_t * oh, object_id_t id);

object_id_t objects_create(obj_handler_t * oh, uint16_t type, uint32_t bytes); /* newobj */
object_id_t objects_clone(obj_handler_t * oh, object_id_t id);
object_id_t objects_promote(obj_handler_t * oh, object_id_t id);       /* scratch->heap */
static inline object_id_t objects_escape(obj_handler_t * oh, object_id_t id);
static inline void objects_write_ref(obj_handler_t * oh, object_id_t * slot,
                                     object_id_t value);

void *      objects_pin(obj_handler_t * oh, object_id_t id);
void        objects_unpin(obj_handler_t * oh, object_id_t id);
void        objects_space_register(obj_handler_t * oh, uint16_t tag,
                                   const object_space_t * desc);

/* ---- heap.h : the payload allocator --------------------------------- */
object_id_t heap_alloc(obj_handler_t * oh, uint16_t space, uint16_t type,
                       uint8_t kind, uint32_t bytes);
void        heap_stats(obj_handler_t * oh, heap_stats_t * out);
uint32_t    heap_in_use(obj_handler_t * oh);
uint32_t    heap_largest_free(obj_handler_t * oh);  /* fragmentation ratio — §5.4 */

/* ---- stack.h : managed-thread stacks, frames, slots ----------------- */
int              stack_thread_init(obj_handler_t * oh, stack_thread_t * t,
                                   stack_slot_t * slots, uint32_t slot_count,
                                   stack_frame_t * frames, uint32_t frame_count);
void             stack_thread_end(stack_thread_t * t);
stack_frame_t  * stack_frame_push(stack_thread_t * t, uint16_t method,
                                  uint16_t arg_slots, uint16_t local_slots,
                                  uint16_t max_stack,
                                  const uint8_t * local_kinds); /* NULL: overflow */
void             stack_frame_pop(stack_thread_t * t, uint8_t ret_slots);
void             stack_scan_roots(obj_handler_t * oh, gc_visit_fn visit);
int              stack_verify(obj_handler_t * oh);
/* static inline in stack.h: push, pop and peek per slot kind and for a
   whole slot; stack_dup, stack_drop, stack_depth, stack_room;
   stack_slot_byref and stack_byref_ptr for byrefs to slots (§4.1)      */

/* ---- array.h / obj_string.h / value.h : typed constructors ----------- */
object_id_t array_create(obj_handler_t * oh, uint16_t type, uint16_t elem_size,
                         uint32_t count);                                     /* §5.7 */
uint32_t    array_length(obj_handler_t * oh, object_id_t id);
void *      array_range_ptr(obj_handler_t * oh, object_id_t id, uint32_t index,
                            uint32_t count);                     /* NULL: out of range */
static inline void * array_element_ptr(obj_handler_t * oh, object_id_t id,
                                       uint32_t index);          /* a range of one */
object_byref_t array_element_byref(obj_handler_t * oh, object_id_t id,
                                   uint32_t index);                         /* ldelema */
void        array_copy(obj_handler_t * oh, object_id_t dst, uint32_t dst_index,
                       object_id_t src, uint32_t src_index, uint32_t count);
void        array_clear(obj_handler_t * oh, object_id_t id, uint32_t index, uint32_t count);

object_id_t obj_string_create(obj_handler_t * oh, const char * utf8, uint32_t len); /* §5.6 */
object_id_t obj_string_intern(obj_handler_t * oh, uint16_t literal_index);
object_id_t obj_string_concat(obj_handler_t * oh, object_id_t a, object_id_t b);
uint32_t    obj_string_length(obj_handler_t * oh, object_id_t id);
int32_t     obj_string_char_at(obj_handler_t * oh, object_id_t id,
                               uint32_t index);                  /* -1: out of range */

object_id_t value_box(obj_handler_t * oh, uint16_t type, const void * src, uint32_t bytes);
void        value_unbox(obj_handler_t * oh, object_id_t id, void * dst, uint32_t bytes);
int32_t     value_compare(obj_handler_t * oh, const stack_slot_t * a,
                          const stack_slot_t * b, uint32_t flags);        /* §6.7 */
int         value_convert(obj_handler_t * oh, const stack_slot_t * src, uint8_t to,
                          uint32_t flags, stack_slot_t * dst);  /* 0: not converted */

/* ---- gc.h : the collector ------------------------------------------- */
void     gc_collect(obj_handler_t * oh);
void     gc_request(obj_handler_t * oh);    /* set the flag — §7.3        */
void     gc_stats(obj_handler_t * oh, gc_stats_t * out);
void     gc_register_root_scan(obj_handler_t * oh, gc_scan_fn scan);  /* §7.4 */
```

Every byte count and element count in this API is 32-bit, and stays 32-bit
when the v1 payload cap is lifted (§2.3.1). Sizes that describe a *type* —
`type`, `elem_size`, `literal_index` — stay 16-bit because widening them is an
embedder question, not a heap question. So do the counts that describe a
*method's frame* — `arg_slots`, `local_slots`, `max_stack` — because CIL's
are: `MaxStack` and the `ldarg` and `ldloc` indices are 16-bit fields.

**Every call takes its instance first, and every per-thread stack call its
`stack_thread_t *` (I2).** A thread belongs to the instance that created it,
so the per-thread calls need nothing more. Ruffus reaches both through hidden
globals — `app_instance`, switched between applications, and
`instance_get_current()` inside every stack accessor — which costs a load on
the hottest path in the VM. Both are
already in the interpreter's hands and are passed; a native reaches the
instance through the `exec_thread_t *` its thunk receives
([16](16_Native_Call_Protocol.md)). Measured on Cortex-M4 at `-O2`,
resolving an id takes 9 instructions with the instance passed, 10 through
file-scope globals, and 11 through a switched current-instance pointer;
`__thread` becomes a call to `__emutls_get_address`, which allocates.

---

## 9. Embedding contract

Every region is supplied by the caller. The library calls no allocator.

```c
typedef struct {
    struct {
        uint8_t         * payload;  uint32_t payload_size;  /* <= 64 KB, v1 */
        object_handle_t * handles;  uint32_t handle_count;
        uint8_t           gc_threshold_pct;                  /* §7.3     */
        uint8_t           gc_handle_pct;
    } segment[HEAP_SEGMENTS];
    uint8_t         segments;       /* 1 in v1, MCU and host — §5.5     */

    uint8_t         gc_recover_pct, gc_damp_step_pct, gc_damp_cap_pct;

    uint8_t       * statics;        uint32_t statics_size;
    const uint8_t * statics_refmap;

    const uint8_t * const_base;     /* the constant table, may be flash — §3.5 */
    uint32_t        const_count;    /* entries, reserved entry 0 included; 0: none */
    uint32_t        const_size;     /* table and payloads, in bytes */

    uint16_t        string_type;    /* System.String's type id — D9, §5.6 */

    uint16_t        worklist_len;   uint16_t * worklist;
} obj_handler_config_t;
```

Thread stacks are not in it. The VM supplies each managed thread's slots and
frames to `stack_thread_init` (§6.2), so how many threads there are and how
large each stack is are the VM's to decide ([17 §8](17_Threading_Model.md)).

The size fields are 32-bit for the reason in §2.3.1 — the struct is the
embedder's compile-time contract, and it should not be the thing that breaks
when the cap is lifted. v1 validates each against its 16-bit profile limit in
`obj_handler_init` and refuses anything larger with a status code (§9.2).

An MCU build declares these as `static` arrays and performs no dynamic
allocation before `main`. A host build `mmap`s the image and `malloc`s the
rest once. Neither path is inside the library.

**The instance is one more region (I1).** The embedder declares the
application's `obj_handler_t` — `static obj_handler_t app_oh;` on an MCU,
zeroed as static storage is — and passes it to `obj_handler_init` with the
config. Its fields are the library's; the embedder only
allocates it. It is 452 bytes on Cortex-M4 with the v1 profile, and
`obj_handler_shutdown` zeroes it again.

### 9.1 Sizing the handle table

The handle table does not grow and is not resized — it is a fixed array
chosen once, like every other region here. The only question was what number
to put in the header, and the arithmetic answers it: at 8 bytes per handle,
`handle_count` is a direct RAM line item.

| `handle_count` | Table RAM | Mean object size it assumes, on a 64 KB payload |
|---|---|---|
| 1024 | 8 KB | 64 bytes |
| **2048** | **16 KB** | **32 bytes** |
| 4096 | 32 KB | 16 bytes — the table now costs half the heap |

**Decision: `handle_count = payload_size / 32`, rounded to a power of two, as
the header default**, overridable by the board manifest. A program that
exhausts handles while payload remains free is collecting at the wrong
threshold, which is what `gc_handle_pct` (§7.3) exists to catch. `cli_pack`
emits the default; nothing at run time changes it.

A **debug** build's handle is 10 bytes rather than 8 (§3.2) — 9, padded to
the 2-byte alignment of its fields — so the same `handle_count` costs a
quarter as much again: 16 KB becomes 20 KB. That is the
price of the generation check on a device debug build, and it is the reason
the field is debug-only.

### 9.2 Initialisation — `obj_handler_status_t`

`obj_handler_init` is the only function that returns a status. Every other
failure follows [18 D1](18_ObjectHandler_Build_Plan.md): allocation returns
`OBJECT_NULL` only for a request that can never fit, and `obj_string_create`
also for bytes that are not valid UTF-8 (§5.6); `on_no_memory` does not return;
and an embedder bug goes to `on_corrupt` — after which the library
returns a safe value and carries on, in every build. Aborting is the
embedder's choice, made inside `on_corrupt` (A13). Init is the exception
because it runs before there is an `on_corrupt` to trust — a NULL
`on_corrupt` is one of the things it rejects.

**Rule: until `obj_handler_init` returns `OBJ_HANDLER_OK`, errors are
returned; after that, an embedder bug goes to `on_corrupt`.** This is also why
`objects_space_register` returns `void` (§8).

```c
typedef enum {
    OBJ_HANDLER_OK = 0,
    OBJ_HANDLER_E_ARG_NULL,       /* oh, cfg or host is NULL                      */
    OBJ_HANDLER_E_ALREADY_INIT,   /* this instance is initialised; shut it down first */
    OBJ_HANDLER_E_SEGMENTS,       /* segments == 0 or > HEAP_SEGMENTS             */
    OBJ_HANDLER_E_PAYLOAD_BASE,   /* payload NULL or not 8-byte aligned           */
    OBJ_HANDLER_E_PAYLOAD_SIZE,   /* < 16, > HEAP_MAX_PAYLOAD, or not a multiple of 8 */
    OBJ_HANDLER_E_HANDLES,        /* handles NULL, or handle_count < 2 or > 65536 */
    OBJ_HANDLER_E_WORKLIST,       /* worklist NULL or worklist_len == 0           */
    OBJ_HANDLER_E_HOST_CALLBACK,  /* type_refmap, on_no_memory or on_corrupt is NULL */
    OBJ_HANDLER_E_STATICS,        /* statics malformed, or its refmap marks bytes 0-7 — §3.5 */
    OBJ_HANDLER_E_CONST,          /* the constant table or one of its entries — §3.5 */
} obj_handler_status_t;
```

| Property | Rule |
|---|---|
| Granularity | one code per thing the embedder has to fix; its comment says what |
| Order | checks run in enum order and the first failure is returned, so a test can trip exactly one check and assert its code |
| Atomicity | every check runs before any region or instance field is written; a failed init leaves the instance uninitialised, and init may simply be called again |
| Stability | `OBJ_HANDLER_OK` is 0; codes are appended, never renumbered |
| Growth | a code arrives with the slice that first reads its field — `E_STATICS` and `E_CONST` with D7, `E_GC_PCT` with OH-3 — so no code exists without a check and a test. OH-4 adds none: a thread's memory arrives after init, so a bad one goes to `on_corrupt` |

The limits behind the codes:

- **`HEAP_MAX_PAYLOAD` is 65536** — the "≤ 64 KB" of §5.1 and §9. 65536 does
  not fit in 16 bits, so the segment's bump top and `used` count are 32-bit
  inside `heap.c`; stored offsets and sizes stay 16-bit, because neither can
  exceed 65528.
- **16 bytes minimum** — the reserved 8-byte sentinel at offset 0
  ([18 §4](18_ObjectHandler_Build_Plan.md)) plus one 8-byte block.
- **`handle_count` at most 65536** — a 16-bit index, index 0 reserved (§2.1).

---

## 10. Inheritance summary

| From Ruffus | Verdict |
|---|---|
| ObjectHandler as a separable component with no VM includes | **kept and enforced by CI** |
| One module per responsibility, prefix = module (`objects_`, `heap_`, `stack_`, `obj_string_`, `value_`, `array_`, `gc_`) | **kept** — §1.2 |
| Two-block object/payload split (`heap_header_t3.data`) | **kept**, promoted to a dense handle array |
| Magic-threshold non-heap references (`CONST_OBJECT_ID`) | **generalised** to the space tag |
| First-fit + coalescing allocator | **kept**; size-class recycling deferred — §6.3 |
| One unified heap, no pools | **kept** — §5.5 |
| Class instance = one contiguous block of fields (`object_create`) | **kept**; the class structures themselves move to the image |
| Statics as a flat array of object ids | **kept**, moved to the immortal slab |
| Tricolour mark-sweep | **kept** |
| Three linear colour lists | **replaced** — mark bit + bounded worklist |
| `signature` corruption checks on every block | **kept as a debug profile**, off in release |
| `object_t` as `uint16_t` | **widened** to `object_id_t` |
| Boxed primitives on the stack (`stack.h`, `value_type.c`) | **rejected** — §6.1 |
| `stack_push_*`, `stack_pop_*` and `stack_peek_*`, one per Java type | **kept as a shape** — one per slot kind, over typed slots; narrow types are promoted, so they need none — §6.1, §8 |
| `instance_get_current()` inside stack accessors | **rejected** — thread passed explicitly |
| Each thread's stack taken from the heap (`stack_init`) | **replaced** — the VM supplies each thread's slots and frames — §6.2 |
| `app_instance`, one global pointer switched between applications | **replaced** — one application, whose `obj_handler_t` is passed to every call — §1, §8 |
| Heap ≤ 64 KB | **kept as the v1 profile**, per segment; the id abstracts it — §2.3.1 |
| Arrays as growable lists (`array_insert`, `array_delete`, in-place resize) | **rejected** — CIL arrays have a fixed length; `List<T>` grows in managed code — §5.7 |
| Element size re-derived from a mode byte (`T_ARRAY + mode`, `T_DATA`) | **replaced** — `elem_size` in the header, element layout from the type's refmap — §5.7 |

---

## 11. Decisions and what remains open

**Resolved in this revision:**

| Was open | Now |
|---|---|
| Library naming | Prefix is the owning module; `oh_`/`vee_` retired — §1.2. The string module is `obj_string`, because `strings.h` would shadow the POSIX header — §1.1. The out-of-memory callback is `on_no_memory`, not `on_oom` (2026-09-29) — §7.5 |
| 64 KB heap, id width | **An initial profile, not a contract.** Heap ids are already pure identities, so compaction and a wider heap are changes to `heap.c`/`objects_types.h` that no embedder sees — §2.3.1 |
| Allocation API width | Every byte and element count is **32-bit** from the first commit, so lifting the cap is not an API break — §5.2, §8 |
| Segment selection policy | **One collectable segment**, MCU and host alike; two candidate splits recorded — §5.5 |
| Free-list policy | **First fit + address-ordered coalescing** in v1; the fragmentation ratio decides whether the successor is ever needed, and the successor is compaction — §5.4 |
| Compaction | **Out of the first release; the intended successor to §5.4.** Its two preconditions (pinning, no raw pointer across a safepoint) hold from v1 — §5.3 |
| Stack tiers in v1 | **Tiers 0 and 1 in, tier 2 deferred.** v1 heap-allocates every box and temporary; it does *not* box arithmetic, because that is the slot's job, not the arena's — §6.0 |
| Size-class recycler | **Deferred**, and an *alternative* to compaction rather than a layer beneath it — §6.3 |
| Scratch arena sizing | Specified; deferred with tier 2. `cli_pack` reserves `scratch_bytes` and emits zero so the image format does not break later — §6.4 |
| Handle-table sizing | `payload_size / 32`, fixed at startup, never resized — §9.1 |
| Promotion during `ret` | Promote **before** `stack_frame_pop`; unreachable in v1, since nothing allocates in `OBJECT_SPACE_FRAME` — §6.5 |
| Collection trigger | **Both**, and configurable: 75 % occupancy requested as a flag, *plus* an immediate forced collect-and-retry on allocation failure — §7.3 |
| Debug object identity | **A generation counter in the id, debug builds only**: 8-bit space / 8-bit generation / 16-bit index, same 32-bit width, so a stale id is rejected at its first use instead of aliasing a recycled handle — §2.1, §3.2, §3.4 |
| String representation | **UTF-8**, identical on MCU and host. `cli_pack` converts ASCII literals and **errors** on any code point ≥ U+0080 or any unpaired surrogate, unless `--allow-non-ascii` is given — §5.6 |
| Init failure reporting | **`obj_handler_status_t`**, returned by `obj_handler_init` only — one code per thing the embedder has to fix, appended per slice. Before init errors are returned; after it, embedder bugs go to `on_corrupt` — §9.2 |
| Array model | **One-dimensional arrays of inline elements** — primitives, ids or structs — with a fixed length. The type's refmap describes one element; `array_range_ptr` is the only range check — §5.7 |
| Pre-OH-1 review (2026-09-28) | [19](19_Session_Checklist.md) A2–A13, accepted as recommended: one calling thread (§1); C99 baseline (§1.1); a pin count (§3.2); `heap_release` bumps the generation (§3.4); `OBJECT_NULL` only for a request that can never fit (§5.2); stack roots through a registered scanner, no `[ThreadStatic]` (§7.4); scan rule per kind, 4-aligned ids (§7.5); the intern table in the config (§9); `on_corrupt` always returns (§9.2) |
| Coding OH-1 and OH-2 (2026-09-28) | The debug handle is **10 bytes**, not 12 (§3.2, §9.1). Non-inline accessors check ids in every build (§3.4). A flat-space id has no handle, so no type, size or kind until D7: the accessors send it to `on_corrupt`. `objects_space_register` refuses the heap tags, `OBJECT_SPACE_FRAME`, handled descriptors, a limit outside 1–65536 and re-registration, and does nothing before init. `heap_release` refuses a pinned object. A zero-byte object gets a handle with offset 0 and no block. OH-2 reads the two segment percentages before `E_GC_PCT` exists; a value above 100 only puts the threshold out of reach |
| String layout (D9, 2026-09-28) | UTF-8 bytes plus a NUL, no header; `OBJECT_H_ASCII` makes `Length` O(1) for ASCII; `Length` and `obj_string_char_at` count UTF-16 units; invalid UTF-8 returns `OBJECT_NULL`; the type id comes from the config — §5.6. Interning waits for D7 |
| Statics and constants (D7, 2026-09-28) | Statics: a flat space, bytes 0–7 reserved, one refmap bit per word, filled by the embedder, never holding image ids. Constants: an indirect space over a table of 12-byte entries in the image, immutable, never collected, never holding an id. Literals are constants, so `obj_string_intern` allocates nothing; this supersedes A6. B3 settled: a `CONST` index selects a table entry. `E_STATICS` and `E_CONST` — §3.5 |
| Coding D7 (2026-09-28) | Every reader goes through one library-internal view, `objects_info`, which answers from a heap handle or a constant entry alike. A constant id with a non-zero generation is refused in debug. Init checks the statics before the constants, in enum order: the slab 8-aligned, 16 to 65536 bytes, a multiple of 8, words 0 and 1 unmarked; the table 8-aligned, at most 65536 entries, `const_size` covering the entries. `objects_space_register` now accepts only `OBJECT_SPACE_NATIVE` |
| Coding OH-5 (2026-09-28) | `array_create` rejects `count` above 65535, and a `count × elem_size` that cannot fit, before multiplying. `array_copy` sends each id of a reference array through `objects_write_ref`, in overlap-safe order; other elements move by `memmove`, struct ids included (item 3 below). An out-of-range `array_element_byref` has `id` `OBJECT_NULL`. `value_unbox` requires a live box of exactly the size asked. `objects_clone` keeps `OBJECT_H_ASCII` |
| Application and instance (A14, 2026-09-28) | The owner's model: an application is one CLI binary, packed into one image and run on one OS thread; its managed threads are green threads the VM schedules; running several applications at once is not a use case. **All library state is in `obj_handler_t`**, which every call takes first and every host callback receives, so the library keeps no hidden globals (§8). Built before OH-3 and OH-4 — §1, §7.5, §8, §9 |
| Value normalisation (B6, 2026-09-29) | **The library normalises; the interpreter chooses.** `value_compare` orders any two slots and `value_convert` converts any number to any primitive type; which one an opcode needs, and whether its operands are valid under §III.1.5, is the interpreter's. Integers compare in 64 bits, signed or unsigned; an integer meets a double through the conversion; references compare by id, byrefs by address, any other mix is `VALUE_UNORDERED`. Conversions follow Table III.8, and a float out of range saturates, NaN to 0. `stack_slot_t` is declared in `objects_types.h` — §1, §6.1, §6.7 |
| Coding value normalisation (2026-09-29) | `VALUE_T_*` start at 1, so a zeroed target is refused: 0, `VALUE_T_COUNT` and above go to `on_corrupt`. `value_convert` builds its result in a zeroed slot — `flags` and `aux` 0 — and writes `dst` only when it returns 1. Integers are read and narrowed as 64-bit patterns, with no implementation-defined conversion. The sanitized debug build adds `float-cast-overflow`, which GCC's `undefined` group leaves out, so a double cast out of range cannot pass unnoticed — §6.7 |
| Stack (OH-4, 2026-09-29) | [19](19_Session_Checklist.md) OH-4, accepted as recommended, built before OH-3. The record is `stack_thread_t`, passed to every per-thread call: no current-thread pointer anywhere. The VM supplies each thread's slot and frame arrays and ends a thread with `stack_thread_end`, so `stack_mem` leaves the config. Overflow is checked once per call in every build, per push and pop in debug; `stack_room` serves frameless use. From Ruffus's API come push, pop and peek per slot kind, `dup`, `drop` and `depth` — not `stack_scanf`, `stack_push_string` or `stack_dump`. The tier-2 hooks leave the API, the frame counts are 16-bit, and `stack_frame_pop` takes `ret_slots`. A registered scanner reaches the stacks (A7) — §6.2, §7.4, §8, §9 |
| Coding the stack (OH-4, 2026-09-29) | `stack_thread_init` takes a record zeroed, as static storage is, or ended, and refuses a live one, as init refuses a ready instance; `stack_thread_end` keeps the record's instance, so a later misuse is still reported. A frame carries its `max_stack`, which bounds a debug push and is what `stack_room` reports inside a frame. `stack_frame_push` sends a call with fewer slots than arguments to `on_corrupt`. The per-push checks sit under a fifth debug switch, `STACK_DEBUG`, and go through one library-internal `stack_check`; a peek index is checked in 64 bits, so `0xFFFFFFFF` cannot wrap to the top. `stack_scan_roots` skips `OBJECT_NULL` and reports ids of every space. `stack_verify` checks each thread's record, each frame's window against its caller's, `sp` on a slot boundary within the top frame's `max_stack`, and every live slot's kind. On Cortex-M4 a thread record is 40 bytes, a frame 20, and the instance 452, from 444 — §6.2, §7.4, §9 |
| Byrefs to locals (B8, 2026-09-29) | [19](19_Session_Checklist.md) B8, accepted as recommended. A byref to a local or an argument lives in the thread-relative space `OBJECT_SPACE_STACK`: its index is the slot in the holding thread's value stack, and `stack_byref_ptr(t, b)` resolves it with the thread — sound because a managed pointer never leaves its thread. Locals are typed from the frame's entry: `stack_frame_push` takes `local_kinds`. A local or an argument holds its declared type's storage form, so `ldloc`, `ldarg` and `call` widen and narrow, and a byref to a local behaves like a byref to a field — §3.1, §4.1, §6.2, §6.7 |

**Still open:**

1. **Finalizers and weak references** — out of v1
   ([11 §7.6](11_Memory_Model.md)); revisit only if a native resource needs
   them, and prefer explicit disposal.
2. **The constants** *(open, no longer blocking)* — the 75 % thresholds, the
   25 % / 10-point / 90 % damping, and the 8/16/24/32/48/64 size classes are
   all starting values. They are configuration fields now (§7.3, §9), so the
   first host run's allocation histogram and fragmentation ratio (§5.4)
   replace them without a design change.
3. **Value-type copies and the barrier** *(open, not blocking v1)* —
   `stobj`, `cpobj`, and `stfld` or `stelem` of a struct write the struct's
   ids with a byte copy. That is correct while the barrier is the identity
   (§6.0); tier 2 needs the copy to send each id the type's refmap marks
   through `objects_write_ref`. Decide with tier 2, for every value-type copy
   at once, not per array (§5.7).
