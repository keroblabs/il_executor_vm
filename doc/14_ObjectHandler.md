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
knowledge.** It manipulates opaque *type ids* and opaque *reference bitmaps*
handed to it by the embedder. `cli_exec` is one embedder; Ruffus VM could be
another; a future project could be a third.

Concretely this forbids, inside the library:

- any mention of `MethodDef`, `TypeDef`, tokens, or IL opcodes;
- any use of `malloc` — every region is caller-supplied (§9);
- any `printf` — diagnostics go through host callbacks;
- any assumption that a type id means anything other than "index the
  embedder understands".

What the library *does* own, and what the embedder must therefore not
duplicate: object identity, object lifetime, the layout of the heap, the
layout of the execution stack, and the definition of a frame.

### 1.1 Layout

The decomposition is Ruffus's, kept deliberately: one module per
responsibility, one prefix per module.

```
source/objecthandler/
    obj_handler.h         /* umbrella header — includes the modules below  */
    obj_handler_config.h  /* the single configuration + debug switches     */
    objects_types.h       /* object_id_t, spaces, kinds — shared vocabulary*/
    objects.c/.h          /* identity, spaces, handles, resolution, create */
    heap.c/.h             /* segments, payload allocator, free list        */
    stack.c/.h            /* thread stacks, frames, slots, scratch arena   */
    obj_string.c/.h       /* string objects and their operations          */
    value.c/.h            /* boxing, unboxing, numeric conversion, compare */
    array.c/.h            /* arrays of primitives and of references        */
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
| `stack_` / `STACK_` | `stack.c` | thread stack regions, frames, evaluation slots, the frame scratch arena |
| `obj_string_` / `OBJ_STRING_` | `obj_string.c` | `System.String` payloads, interning, string operations |
| `value_` / `VALUE_` | `value.c` | boxing/unboxing, numeric conversion and comparison |
| `array_` / `ARRAY_` | `array.c` | array payloads, element addressing, bulk copy |
| `gc_` / `GC_` | `gc.c` | mark, sweep, roots, rooting scopes, collection trigger |
| `obj_handler_` / `OBJ_HANDLER_` | `obj_handler.h` | library lifecycle only: `init`, `shutdown`, config and host structs, init status codes |

Two consequences worth stating, because the `oh_` draft got them wrong:

- **Object kinds and slot kinds are different enumerations.** They were both
  `OH_K_*`, which was a latent bug. They are now `OBJECT_K_*` (what a heap
  block holds) and `STACK_K_*` (what an evaluation slot holds).
- **Allocation is named after where the memory comes from**, not after what
  is built in it: `heap_alloc()` and `stack_scratch_alloc()` are the two
  allocators; `objects_create()`, `array_create()`, `obj_string_create()`
  and `value_box()` are the typed constructors that call them.

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

The exception, stated plainly: the **flat** spaces — `OBJECT_SPACE_STATIC`,
`OBJECT_SPACE_FRAME`, and flat `OBJECT_SPACE_CONST` entries — use the index
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
| `0x0010` | `OBJECT_SPACE_CONST` | immutable image | literal-pool index | no | **no** |
| `0x0011` | `OBJECT_SPACE_STATIC` | immortal | byte offset in the statics slab | no — **root** | yes |
| `0x0012` | `OBJECT_SPACE_FRAME` | scoped | byte offset in a thread stack region | no — **root**, LIFO | yes |
| `0x0013` | `OBJECT_SPACE_NATIVE` | foreign | native descriptor index | no | per descriptor |
| `0x0014`–`0x00FF` | reserved | — | — | — | — |
| `0x0100`–`0xFFFF` | embedder-defined | — | — | — | — |

The embedder range is what makes the library reusable: a project with, say,
an external SPI-attached PSRAM region registers it as a space, hands
ObjectHandler a resolver, and every reference to it works through the same
`object_id_t` as everything else.

`OBJECT_SPACE_FRAME` is **reserved and unused in v1** — the frame scratch
arena is tier 2 and is deferred (§6). It keeps its tag from the first commit
so that enabling it later renumbers nothing.

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
} object_handle_t;     /* 8 bytes release, 12 debug */

enum {                                      /* flags */
    OBJECT_H_INUSE   = 0x01,
    OBJECT_H_PINNED  = 0x02,                /* must not move                  */
    OBJECT_H_MARK    = 0x04,                /* GC mark bit — see §7.2         */
    OBJECT_H_SCANNED = 0x08,                /* grey→black, on worklist overflow */
    OBJECT_H_HASREFS = 0x10,                /* payload contains object ids    */
};

enum {                                      /* object kind — what the block is */
    OBJECT_K_FREE = 0, OBJECT_K_OBJECT, OBJECT_K_ARRAY, OBJECT_K_STRING,
    OBJECT_K_BOX, OBJECT_K_RAW,
};
```

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

extern object_space_t object_spaces[OBJECT_SPACE_COUNT];

static inline void * objects_ptr(object_id_t id)
{
    const object_space_t * sp = &object_spaces[OBJECT_SPACE(id)];

    if (LIKELY(sp->handles != NULL))                    /* heap segment      */
        return sp->base + sp->handles[OBJECT_INDEX(id)].offset;

    if (LIKELY(sp->base != NULL))                       /* flat space        */
        return sp->base + OBJECT_INDEX(id);

    return objects_resolve_indirect(id);                /* native descriptor */
}
```

One predictable branch and two loads on the heap path; one branch and one
add on constants, statics and frame scratch. The
[once-per-opcode rule](11_Memory_Model.md#44-resolution-and-the-once-per-opcode-rule)
is unchanged and is what keeps this off the critical path.

`OBJECT_SPACE_COUNT` is a compile-time profile constant. An MCU build sets it
to `0x0014`, so `object_spaces` is 20 records — 320 bytes — and the embedder
range costs nothing.

### 3.4 Debug resolution

`OBJECT_DEBUG_REFS` replaces `objects_ptr` with a checked version that
validates `OBJECT_SPACE(id) < OBJECT_SPACE_COUNT`,
`OBJECT_INDEX(id) < sp->limit`, `flags & OBJECT_H_INUSE`, and — for handled
spaces — `OBJECT_GEN(id) == handles[i].gen`.

The last of those is the one that earns its keep. The first three catch a
*malformed* id; only the generation catches a **stale** one, which otherwise
resolves to a different, valid, live object and corrupts it in silence. The
sweep bumps the generation of every handle it frees, and freed handles are
returned to the tail of the free list so that reuse is as late as possible.

It is on by default in host builds. The release path above is what ships to
the device — but note §0.2 of
[18_ObjectHandler_Build_Plan.md](18_ObjectHandler_Build_Plan.md): a *device
debug build* is the same configuration with these switches on, which is what
makes the checks usable on real hardware when something is wrong.

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
`objects_ptr(b.id) + b.offset`, uniformly, for all regions.

`offset` is 16 bits because a v1 object is (§2.3). When the payload widens,
the offset widens with it and moves out of `stack_slot_t.aux` (§6.1) into the
union, which is 8 bytes either way — so the slot does not grow.

Managed pointers remain forbidden in object fields, static fields and array
elements, per ECMA-335. The collector depends on that: reference bitmaps
describe `object_id_t` only.

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
object_id_t heap_alloc(uint16_t space, uint16_t type, uint8_t kind, uint32_t bytes);
```

**`bytes` is 32 bits even though the v1 payload is not.** The allocator's own
arithmetic stays 16-bit inside the segment; the parameter is wide so that
lifting the cap (§2.3.1) is a change to `heap.c` and not to every call site in
every embedder. The only behaviour the extra width adds in v1 is that an
oversized request fails cleanly at step 1 instead of truncating.

1. Round `bytes` up to 8; reject anything larger than the segment payload.
2. **Size-class cache** — *deferred* (§6.3: it is an alternative to
   compaction, not a layer under it). v1 falls through to step 3.
3. Free list — first fit, address-ordered.
4. Bump the segment top.
5. On failure: collect, retry once; then try the next segment; then `on_oom`.
6. Take a handle from the handle free list; on exhaustion, collect and retry.
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
| String literals | constant pool | **image bytes + immortal wrappers** |

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

Folding the byref offset into `aux` keeps the union at exactly 8 bytes. That
is not a v1 saving — natural alignment pads the struct to 16 either way — but
it is the precondition for the structure-of-arrays split (a `uint8_t kind[]`
beside a `uint64_t value[]`, 9 bytes per slot) that
[11_Memory_Model.md §6.2](11_Memory_Model.md) records as a later
optimisation. Do the `aux` trick now, defer the split.

The tag is not overhead. It is what makes the collector precise over the
stack with no stack maps — the structural advantage an interpreter has over a
JIT.

### 6.2 Tier 1 — one contiguous stack, frames overlaid, arguments not copied

Ruffus already keeps `stack`, `sp`, `stack_base` and `locals` as four
pointers into one physical array. That instinct is right and should be taken
further.

```c
typedef struct stack_frame_t {
    struct stack_frame_t * prev;
    stack_slot_t         * args;    /* this + declared parameters           */
    stack_slot_t         * locals;
    stack_slot_t         * stack;   /* evaluation stack base                */
    uint16_t               scratch; /* frame scratch high-water — §6.3;
                                       reserved, always 0 in v1             */
    uint16_t               method;  /* embedder method id                   */
    const uint8_t        * ip;
} stack_frame_t;
```

Per thread there are two bump-allocated stacks in one region: a value stack
of `stack_slot_t`, and a control stack of `stack_frame_t`.

The point of the arrangement is that **a call copies nothing**:

```
 caller:  [ args | locals | eval ...................... a0 a1 a2 ]
                                                        ^
 callee:                                               args
          [ args | locals | eval ... ]
```

`call` sets `callee->args = caller->sp - arg_slots`, then bumps past
`locals` and `stack`. `ret` writes the return value into `callee->args[0]`
and sets `caller->sp = callee->args + (returns ? 1 : 0)`. Argument passing
costs two pointer arithmetic operations regardless of argument count, and
the evaluation stack of the caller *is* the argument area of the callee.

Two consequences worth stating, because they are easy to lose later:

- The frame record must **not** be interleaved with the slots. Keeping frames
  on their own stack is what lets the callee's argument window overlap the
  caller's evaluation stack.
- Overflow is checked on push against the region limit. There is no guard
  page on an MCU.

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
`if (!(object_spaces[OBJECT_SPACE(id)].flags & OBJECT_SP_COLLECTED)) continue;`
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
the reason they exist. `value_compare()` is worth reading once for its
mixed-type promotion table and then reimplementing against slots — which is
why `value.c` keeps the name and loses the allocation.

Also rejected: `instance_get_current()` inside every accessor. Ruffus reaches
the stack through a hidden global on the hottest path in the VM; here the
thread is a parameter (§8).

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
    gc_request();            /* collect at the next safepoint */
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
| Interned literals | walk the literal handle table | all entries are object ids |
| Native scopes | walk the scope chain | every entry is an object id |

"Every thread's" rather than "the thread's" — see
[17_Threading_Model.md](17_Threading_Model.md). Green threads make this
enumeration *easier*, not harder: all of them are parked at their own
safepoints by construction.

### 7.5 Host interface

The collector cannot know which words of a payload are object ids. The
embedder tells it:

```c
typedef struct {
    /* Reference bitmap for a type: bit i set => word i of the payload
       is an object_id_t. Return NULL if the type contains no ids.      */
    const uint8_t * (*type_refmap)(uint16_t type, uint32_t * words);

    void (*on_oom)(void);
    void (*on_corrupt)(const char * where, object_id_t id);
} obj_handler_host_t;
```

Three callbacks, all required (§9.2). Everything else the collector needs is
in the handle or, for an array, in its payload header
([18 §7](18_ObjectHandler_Build_Plan.md)), which is why there is no
`array_shape` callback.

---

## 8. Public API

One header per module; `obj_handler.h` includes them all, exactly as Ruffus's
`obj_handler.h` does.

```c
/* ---- obj_handler.h : library lifecycle ------------------------------ */
obj_handler_status_t obj_handler_init(const obj_handler_config_t * cfg,
                                      const obj_handler_host_t   * host);
void                 obj_handler_shutdown(void);

/* ---- objects.h : identity, resolution, lifetime --------------------- */
static inline void * objects_ptr(object_id_t id);
static inline void * objects_byref_ptr(object_byref_t b);
uint16_t    objects_type_of(object_id_t id);
uint32_t    objects_size_of(object_id_t id);
uint8_t     objects_kind_of(object_id_t id);

object_id_t objects_create(uint16_t type, uint32_t bytes);   /* newobj      */
object_id_t objects_clone(object_id_t id);
object_id_t objects_promote(object_id_t id);                 /* scratch->heap */
static inline object_id_t objects_escape(object_id_t id);
static inline void objects_write_ref(object_id_t * slot, object_id_t value);

void *      objects_pin(object_id_t id);
void        objects_unpin(object_id_t id);
void        objects_space_register(uint16_t tag, const object_space_t * desc);

/* ---- heap.h : the payload allocator --------------------------------- */
object_id_t heap_alloc(uint16_t space, uint16_t type,
                       uint8_t kind, uint32_t bytes);
void        heap_stats(heap_stats_t * out);
uint32_t    heap_in_use(void);
uint32_t    heap_largest_free(void);        /* fragmentation ratio — §5.4 */

/* ---- stack.h : threads, frames, slots, scratch ---------------------- */
stack_thread_t * stack_thread_create(uint32_t stack_bytes,
                                     uint32_t scratch_bytes);
stack_frame_t  * stack_frame_push(stack_thread_t * t, uint16_t method,
                                  uint8_t arg_slots, uint8_t local_slots,
                                  uint8_t max_stack);
void             stack_frame_pop(stack_thread_t * t);
object_id_t      stack_scratch_alloc(stack_thread_t * t, uint16_t type,
                                     uint8_t kind, uint32_t bytes);
/* push/pop/peek per slot kind are static inline in stack.h              */

/* ---- array.h / obj_string.h / value.h : typed constructors ----------- */
object_id_t array_create(uint16_t type, uint16_t elem_size, uint32_t count);
uint32_t    array_length(object_id_t id);
void *      array_element_ptr(object_id_t id, uint32_t index);

object_id_t obj_string_create(const char * utf8, uint32_t len); /* §5.6 */
object_id_t obj_string_intern(uint16_t literal_index);
object_id_t obj_string_concat(object_id_t a, object_id_t b);
uint32_t    obj_string_length(object_id_t id);

object_id_t value_box(uint16_t type, const void * src, uint32_t bytes);
void        value_unbox(object_id_t id, void * dst, uint32_t bytes);
int32_t     value_compare(const stack_slot_t * a, const stack_slot_t * b);

/* ---- gc.h : the collector ------------------------------------------- */
void     gc_collect(void);
void     gc_request(void);                  /* set the flag — §7.3        */
void     gc_stats(gc_stats_t * out);
```

Every byte count and element count in this API is 32-bit, and stays 32-bit
when the v1 payload cap is lifted (§2.3.1). Sizes that describe a *type* —
`type`, `elem_size`, `literal_index` — stay 16-bit because widening them is an
embedder question, not a heap question.

Every accessor takes its `stack_thread_t *` explicitly. Ruffus's
`instance_get_current()` inside every stack accessor is a hidden global load
on the hottest path in the VM; the thread pointer is already in the
interpreter's hands and should be passed.

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

    const uint8_t * const_base;     /* immutable image, may be flash      */
    uint32_t        const_count;

    uint8_t       * stack_mem;      uint32_t stack_size;     /* all threads */

    uint16_t        worklist_len;   uint16_t * worklist;
} obj_handler_config_t;
```

The size fields are 32-bit for the reason in §2.3.1 — the struct is the
embedder's compile-time contract, and it should not be the thing that breaks
when the cap is lifted. v1 validates each against its 16-bit profile limit in
`obj_handler_init` and refuses anything larger with a status code (§9.2).

An MCU build declares these as `static` arrays and performs no dynamic
allocation before `main`. A host build `mmap`s the image and `malloc`s the
rest once. Neither path is inside the library.

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

A **debug** build's handle is 12 bytes rather than 8 (§3.2), so the same
`handle_count` costs half as much again — 16 KB becomes 24 KB. That is the
price of the generation check on a device debug build, and it is the reason
the field is debug-only.

### 9.2 Initialisation — `obj_handler_status_t`

`obj_handler_init` is the only function that returns a status. Every other
failure follows [18 D1](18_ObjectHandler_Build_Plan.md): allocation returns
`OBJECT_NULL`, `on_oom` does not return, and an embedder bug goes to
`on_corrupt`. Init is the exception because it runs before there is an
`on_corrupt` to trust — a NULL `on_corrupt` is one of the things it rejects.

**Rule: until `obj_handler_init` returns `OBJ_HANDLER_OK`, errors are
returned; after that, an embedder bug goes to `on_corrupt`.** This is also why
`objects_space_register` returns `void` (§8).

```c
typedef enum {
    OBJ_HANDLER_OK = 0,
    OBJ_HANDLER_E_ARG_NULL,       /* cfg or host is NULL                          */
    OBJ_HANDLER_E_ALREADY_INIT,   /* already initialised; shut down first         */
    OBJ_HANDLER_E_SEGMENTS,       /* segments == 0 or > HEAP_SEGMENTS             */
    OBJ_HANDLER_E_PAYLOAD_BASE,   /* payload NULL or not 8-byte aligned           */
    OBJ_HANDLER_E_PAYLOAD_SIZE,   /* < 16, > HEAP_MAX_PAYLOAD, or not a multiple of 8 */
    OBJ_HANDLER_E_HANDLES,        /* handles NULL, or handle_count < 2 or > 65536 */
    OBJ_HANDLER_E_WORKLIST,       /* worklist NULL or worklist_len == 0           */
    OBJ_HANDLER_E_HOST_CALLBACK,  /* type_refmap, on_oom or on_corrupt is NULL    */
} obj_handler_status_t;
```

| Property | Rule |
|---|---|
| Granularity | one code per thing the embedder has to fix; its comment says what |
| Order | checks run in enum order and the first failure is returned, so a test can trip exactly one check and assert its code |
| Atomicity | every check runs before any region or library state is written; a failed init leaves the library uninitialised, and init may simply be called again |
| Stability | `OBJ_HANDLER_OK` is 0; codes are appended, never renumbered |
| Growth | a code arrives with the slice that first reads its field — `E_GC_PCT`, `E_STATICS` and `E_CONST` with OH-3 (D7), `E_STACK` with OH-4 — so no code exists without a check and a test |

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
| `instance_get_current()` inside stack accessors | **rejected** — thread passed explicitly |
| Heap ≤ 64 KB | **kept as the v1 profile**, per segment; the id abstracts it — §2.3.1 |

---

## 11. Decisions and what remains open

**Resolved in this revision:**

| Was open | Now |
|---|---|
| Library naming | Prefix is the owning module; `oh_`/`vee_` retired — §1.2. The string module is `obj_string`, because `strings.h` would shadow the POSIX header — §1.1 |
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

**Still open:**

1. **Finalizers and weak references** — out of v1
   ([11 §7.6](11_Memory_Model.md)); revisit only if a native resource needs
   them, and prefer explicit disposal.
2. **The constants** *(open, no longer blocking)* — the 75 % thresholds, the
   25 % / 10-point / 90 % damping, and the 8/16/24/32/48/64 size classes are
   all starting values. They are configuration fields now (§7.3, §9), so the
   first host run's allocation histogram and fragmentation ratio (§5.4)
   replace them without a design change.
