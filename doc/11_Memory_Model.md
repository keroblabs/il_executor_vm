# Memory Model

Contract document. Everything in `cli_exec` and every native intrinsic codes
against this; `cli_pack` is obliged to emit the data it requires.

Companion to [00_Introduction.md §3.3](00_Introduction.md) (summary) and
[12_VXB_Format.md](12_VXB_Format.md) (the on-disk encoding of what §9 below
demands).

The mechanism behind §§4–7 is specified in
[14_ObjectHandler.md](14_ObjectHandler.md), the standalone library that owns
objects, the heap, the stack and the collector. This document states the
*policy* — what lives where, what the emitter must produce, and what native
code must obey. Where the two overlap, 14 is normative.

The `vee_*` and `oh_*` namings used in earlier drafts are retired
throughout; a name is prefixed with the ObjectHandler module that owns it —
`objects_`, `heap_`, `stack_`, `obj_string_`, `value_`, `array_`, `gc_`. See
[14_ObjectHandler.md §1.2](14_ObjectHandler.md).

Status: **contract — write before the code.** Items marked *(open)* are
deliberately unresolved and must not be guessed at in code; raise them before
implementing anything that depends on them.

---

## 1. Vocabulary

Four words are used precisely throughout this project and must not be used
loosely:

| Term | Meaning |
|---|---|
| **immutable image** | Bytes produced by `cli_pack` that are never written at run time. Read in place from flash on an MCU; `mmap`ed read-only on the host. |
| **immortal** | Allocated once at startup, never freed. Mutable. Always a GC root. |
| **collectable** | Lives in the managed heap. Subject to the collector. |
| **scoped** | Lives for the duration of an activation frame. |

"Static" is **not** used as a memory classification — it is too overloaded
(C `static`, IL `static` field, "statically allocated"). Where the IL concept
is meant, this document says *static field*; where the memory concept is
meant, it says *immortal*.

Each of the four classes corresponds to an ObjectHandler **space**, and the
space tag is carried in every reference — so an object's class is readable
from the reference itself. See
[14_ObjectHandler.md §3.1](14_ObjectHandler.md).

---

## 2. Residency map

Every byte `cli_exec` touches belongs to exactly one of these five regions.

| # | Region | Class | Space | Medium | Sized by | Scanned by GC |
|---|---|---|---|---|---|---|
| 1 | Image | immutable image | `CONST` | flash / mapped file | the `.vxb` file | no |
| 2 | Statics slab | immortal | `STATIC` | RAM | image header | **yes — root** |
| 3 | Handle table | immortal | — | RAM | image header (§4.5) | it *is* the object index |
| 4 | Heap segment | collectable | `HEAP+0` | RAM | image header (§4.5) | yes |
| 5 | Thread stack regions | scoped | `FRAME` | RAM | image header | **yes — root** |

Region 5 holds, per thread, a value stack, a frame stack and a scratch arena
([14 §6](14_ObjectHandler.md)). A sixth space, `NATIVE`, exists for
C-owned memory that never belongs to any of these regions — peripheral
registers, DMA buffers — and is neither scanned nor collected.

There is **one** collectable segment in v1. The reference can address 16, and
the library can be given more, but no build — MCU or host — uses a second
one until measurement asks for it ([14 §5.5](14_ObjectHandler.md)).

### 2.1 What lives in the image (region 1)

- type records, vtables, interface maps
- field maps and **reference bitmaps** (§9)
- IL code, rewritten so operands are direct indices
- EH clause tables
- string literal *bytes*
- `FieldRVA` payloads

None of this is copied to RAM. On an MCU the executor holds pointers directly
into flash. This is the single largest RAM saving the `.vxb` design buys and
it is why the format is fixed-width, aligned, and free of relocations.

### 2.2 What does not exist at run time

No metadata heaps, no coded indices, no compressed integers, no token table,
no assembly list, no type-name strings unless reflection is enabled. If
`cli_exec` needs any of these, the design has regressed.

---

## 3. Determining the immortal set — `cli_pack`'s job

The size of region 2 is computed at pack time and written to the header. The
executor performs **no** dynamic allocation for it.

### 3.1 Static field selection

Walk the `Field` table; a row contributes storage to the statics slab when:

```
 (Flags & fdStatic)      /* 0x0010 */
 && !(Flags & fdLiteral)  /* 0x0040 — compile-time constant, no storage    */
 && !(Flags & fdHasFieldRVA) /* 0x0100 — data lives in the image, region 1 */
```

Group by declaring type, order by declaration, assign offsets honouring each
field's natural alignment, and pad the per-type run to the widest member.
The per-type base offset is recorded in the type record so `ldsfld`/`stsfld`
can be rewritten to an absolute slab offset at pack time.

### 3.2 Literal strings

Every reachable `ldstr` operand is resolved against `#US`, deduplicated, and
appended to the literal pool. Pool entries are immutable image data
(region 1); the **`System.String` objects** that wrap them are immortal
(region 2) and their handles live in a fixed-size table sized by the number of
distinct literals.

Literals are never collected. This matches .NET's interning behaviour for
`ldstr` and removes a whole class of lifetime questions from the hot path.

### 3.3 Reachability

The immortal set, and the image as a whole, is restricted to what is
reachable from the entry point: follow `call`, `callvirt`, `newobj`, `ldftn`,
`ldvirtftn`, every `.cctor` of a type that is instantiated or whose statics
are touched, and every vtable slot occupant of every instantiated type.
Unreachable types and methods are dropped.

**Reflection defeats this.** If a build enables reflection, reachability
analysis must be disabled and the whole closure retained. This is the
strongest argument for excluding reflection from MCU targets.

---

## 4. The handle heap

> **Normative reference: [14_ObjectHandler.md](14_ObjectHandler.md).** The
> heap, the reference representation, the stack and the collector are owned
> by the **ObjectHandler** library and specified there. This section states
> only what `cli_exec` and the emitter must know, and why.
>
> The `vee_*` and `oh_*` namings used in earlier drafts are retired; the
> prefix is the owning module.

### 4.1 Principle

An object reference is **never** a machine pointer. It is an **object id**: a
32-bit tagged value carrying a **space**, saying which region owns the
object, and an **index** within that space.

```c
typedef uint32_t object_id_t;        /* 0 == null, always */

#define OBJECT_NULL         ((object_id_t)0)
#define OBJECT_SPACE(id)    ((uint16_t)((id) >> 16))
#define OBJECT_INDEX(id)    ((uint16_t)((id) & 0xFFFFu))
```

Everything that outlives a single expression — every field, every static,
every array element, every byref base — holds an object id and nothing else
([14 §2.1.1](14_ObjectHandler.md)). Raw pointers exist only inside one opcode
handler, between two safepoints.

For collectable spaces the index selects a **handle**, so moving an object
requires rewriting exactly one word. There is no pointer fixup anywhere in
the system — not in the heap, not on any stack, not in native code. That is
what makes compaction tractable on a part with no MMU.

For non-collectable spaces — image constants, the statics slab, frame scratch,
foreign memory — the index is a direct offset or table index. This is the
generalisation of Ruffus's `CONST_OBJECT_ID` threshold
([13 §5.1](13_Ruffus_VM_Reuse.md)), and it is what lets one reference type
designate an object wherever it lives.

Space catalogue, handle layout and resolution:
[14_ObjectHandler.md §§2–3](14_ObjectHandler.md).

### 4.2 The heap is 16-bit, deliberately

A heap segment is at most **64 KB**, because that is the size that fits the
target class and because it halves every stored offset. The 32-bit object id
allows up to 16 segments, so a host build can scale without widening
anything; v1 uses segment 0 on every target
([14 §5.5](14_ObjectHandler.md)).

Live blocks carry **no header** — the handle holds offset, size, type and
flags — so per-object overhead is 8 bytes, against 16 in the earlier draft of
this document and 20–24 in Ruffus. See
[14_ObjectHandler.md §5.1](14_ObjectHandler.md).

### 4.3 Resolution and the once-per-opcode rule

```c
static inline void * objects_ptr(object_id_t id);      /* 14 §3.3 */
```

**Rule: resolve once per opcode, never cache across a dispatch.**

Because the collector runs only at safepoints (§7.3) and safepoints occur
only between opcodes, an opcode handler may resolve a reference once and use
the raw pointer freely for the remainder of its own execution. It must
**not** store that pointer anywhere that outlives the handler.

This is what keeps the indirection off the critical path: the cost is one
extra load per opcode, not per field access.

The single exception is a handler that calls a native intrinsic that may
allocate — `NAT_ALLOC` in
[16_Native_Call_Protocol.md §5](16_Native_Call_Protocol.md). Such a handler
must re-resolve after the call. See §8.

### 4.4 Allocation

```c
object_id_t heap_alloc(uint16_t space, uint16_t type,
                       uint8_t kind, uint16_t bytes);
object_id_t stack_scratch_alloc(stack_thread_t * t, uint16_t type,
                                uint8_t kind, uint16_t bytes);
```

Procedure, free-list policy and the size-class recycler:
[14_ObjectHandler.md §5.2](14_ObjectHandler.md). Two properties this document
depends on:

- **New payloads are zeroed.** The CLI guarantees it and the collector
  requires it — an unzeroed object-id field would be scanned as a garbage
  index.
- **Short-lived objects need not reach the heap.** `box` sites proved
  non-escaping by `cli_pack`, and temporary value types, allocate in the
  frame scratch space and are released by `ret` in one store
  ([14 §6.3](14_ObjectHandler.md)). The promoting write barrier makes this
  safe whether or not the analysis is any good. `localloc` is excluded: its
  size is data-dependent, so it allocates from the heap
  ([14 §6.4](14_ObjectHandler.md)).

### 4.5 Sizing — decided

Heap payload, handle table, statics slab, stack region and scratch arena are
all fixed at startup, and none of them is resized at run time. The numbers
come from **both** sources, with the header carrying the minimum:

| Region | Computed by `cli_pack` from | Override |
|---|---|---|
| Statics slab | the static-field layout pass — exact | none; it is exact |
| Literal table | count of distinct `ldstr` operands — exact | none |
| Value + frame stack per thread | deepest reachable call chain × (`MaxStack` + locals + args), × a **recursion budget** for cyclic call graphs | board manifest |
| Scratch arena per thread | Σ scratch-classified box sizes on the longest path ([14 §6.4](14_ObjectHandler.md)) | board manifest |
| Heap payload | *not computable* — allocation is data-dependent | board manifest / launch flag; header carries a minimum |
| Handle table | `payload_size / 32` ([14 §9.1](14_ObjectHandler.md)) | board manifest |

The only genuinely unanswerable one is heap payload, and that is why some
configuration input was always unavoidable. Everything else the packer knows
exactly, because `MaxStack` and `LocalVarSig` are in the method header and
are already decoded ([method_parser.c](../source/methods/method_parser.c)).

The handle table is a visible line item — 8 bytes per live object — and
appears in the header beside the heap it indexes.

### 4.6 One policy for MCU and host

**The host build runs under the same constraints as the device.** Same single
64 KB segment, same handle-table rule, same 75 % collection threshold
([14 §7.3](14_ObjectHandler.md)), same non-moving collector, same fixed stack
regions, same absence of `malloc` after startup.

This is not caution, it is the point of having a host build: a host that
collected differently, or grew its heap, would stop being a rehearsal and
become a second runtime whose measurements do not transfer. The host's only
licences are diagnostic — `OBJECT_DEBUG_REFS`, `HEAP_DEBUG`, allocation
histograms — and none of them changes a policy.

A larger host configuration is a *configuration*, set through the same
manifest a board uses, not a different code path.

---

## 5. Interior pointers

`ldflda`, `ldelema` and `ldloca` produce ECMA's `&` type, which can designate
the interior of a heap object, a slot in the statics slab, or a frame slot.

```c
typedef struct {
    object_id_t id;      /* base object or region                        */
    uint16_t    offset;  /* byte offset from that object's payload base  */
    uint16_t    _pad;
} object_byref_t;        /* 8 bytes */
```

**One byref kind, not three.** The earlier draft carried the region in the
evaluation-stack tag (`BYREF_HEAP`, `BYREF_STATIC`, `BYREF_FRAME`) because a
bare handle could not express which region was meant. The space tag in
`object_id_t` now says it, so `objects_byref_ptr(b)` is
`objects_ptr(b.id) + b.offset` uniformly and the interpreter has one case
instead of three.

A byref into a heap object keeps its target alive and survives compaction
unchanged, because an object id is what is stored. Byrefs into statics and
frames point at regions that are already scanned as roots, so they impose no
additional liveness obligation.

Managed pointers are **not** permitted in object fields, static fields or
array elements — ECMA-335 restricts them to locals, arguments and return
values. The collector relies on that restriction: reference bitmaps describe
`object_id_t` only, and would otherwise need a second encoding for byref
slots.

---

## 6. Frames and the evaluation stack

Owned by ObjectHandler; specified in
[14_ObjectHandler.md §6](14_ObjectHandler.md). Summarised here because the
emitter must size it and the collector must scan it.

### 6.1 One region per thread, frames overlaid

A thread owns a contiguous slice of a pre-sized region — never `malloc`ed —
holding a value stack of `stack_slot_t`, a control stack of `stack_frame_t`,
and a scratch arena. An MCU build makes the region a static array and
performs no dynamic allocation before `main`.

Frames and slots are on **separate stacks**, which is what allows a call to
copy nothing: `callee->args = caller->sp - arg_slots`, so the caller's top
evaluation slots *are* the callee's argument window. Argument passing costs
two pointer operations regardless of argument count.

Stack overflow is detected on push against the region limit and raises
`StackOverflowException`. There is no guard page on an MCU; the check is
explicit. Because a thread's recursion budget is now an image-header quantity
rather than a property of the host C stack, overflow is diagnosable and
survivable.

### 6.2 Evaluation-stack slot

ECMA-335 §III.1.1 requires the stack to be dynamically typed. The tag is not
optional — it is what makes GC scanning precise with no stack maps, which is
the structural advantage an interpreter has over a JIT.

```c
enum {                     /* slot kinds — STACK_K_*, distinct from OBJECT_K_* */
    STACK_K_EMPTY = 0,
    STACK_K_I4, STACK_K_I8, STACK_K_R8, STACK_K_NINT,
    STACK_K_REF,           /* O  — u.id                                    */
    STACK_K_BYREF,         /* &  — u.byref_base + aux; one kind, not three */
};

typedef struct {
    uint8_t  kind;
    uint8_t  flags;
    uint16_t aux;          /* byref offset, folded into the padding        */
    union {
        int32_t     i4;
        int64_t     i8;
        double      r8;
        int32_t     nint;  /* native int is 32-bit on every target         */
        object_id_t id;
        object_id_t byref_base;
    } u;
} stack_slot_t;            /* 16 bytes */
```

This replaces the current [`stack_node_t`](../source/framework/stack.h), which
carries 16 bytes of payload where IL needs at most 8. **Re-size it before
Slice A locks it in.**

Putting the byref offset in `aux` keeps the union at exactly 8 bytes, which
is the precondition for the structure-of-arrays split below. Do it now even
though natural alignment pads the struct to 16 either way.

Type promotion follows §III.1.5 and is done at the load/store boundary, not
carried in the slot: `bool`, `int8`, `int16`, `char` are promoted to
`STACK_K_I4`; `float32` is promoted to `STACK_K_R8`. Narrowing happens in the
store opcode.

> **Optimisation, not v1.** Splitting tags into a parallel byte array
> (structure-of-arrays) shrinks slots to 9 bytes and makes GC scanning a
> tight byte loop. Revisit if stack-region pressure becomes real; the
> array-of-structures form is far easier to debug first.

### 6.3 Primitives never allocate

Ruffus pushes a heap object for every integer
([13 §6.1](13_Ruffus_VM_Reuse.md)). The tagged slot above is what removes
that, and it is the single largest behavioural difference between the two
runtimes. What still allocates — `box`, temporary value types — is handled by
the frame scratch space and the size-class recycler in
[14_ObjectHandler.md §6.3](14_ObjectHandler.md), not by making the heap
faster. `localloc` is the exception and goes to the heap, because its size is
only known at run time.

This is the one deliberate exception to "everything is an object id"
([14 §2.1.1](14_ObjectHandler.md)), and it is worth being exact about why it
is not an inconsistency: an unboxed `int32` on the evaluation stack is not an
object, has no identity, and cannot be referenced. The moment it *becomes* an
object — `box` — it gets an object id like everything else.

---

## 7. Garbage collection

### 7.1 Algorithm

**Tricolour mark-sweep. Precise. Non-moving in v1. Stop-the-world.**

- White: not yet proven reachable.
- Grey: proven reachable, referents not yet scanned.
- Black: proven reachable, referents scanned.

Initialise by greying the root set (§7.2) and whitening everything else.
Repeatedly take a grey object, grey each white object it references, and
blacken it. When no grey objects remain, every white object is unreachable
and is swept.

The mark bit lives **in the handle**, not in a side bitmap. Marking already
loads the handle to reach `offset` and `type`, so a side bitmap would double
the memory traffic to save one bit per object, and the sweep's linear walk
over the 8-byte handle array has the same access pattern a bitmap scan would
have had. This supersedes the side-bitmap decision in earlier drafts; see
[14_ObjectHandler.md §7.2](14_ObjectHandler.md).

A reference into a non-collected space — constants, statics, frame scratch,
foreign memory — is skipped with one flag test. This is Ruffus's
`if (object >= CONST_OBJECT_ID) continue;` generalised, written once instead
of in five places.

The grey set is an **explicit, pre-allocated, bounded worklist**. Never scan
the heap looking for grey objects. On overflow, set a flag, drain the
worklist, then re-scan marked-but-unscanned objects to completion — bounded
memory beats elegance on a device.

### 7.2 Root set

Exactly five sources. There are no others; any reference reachable only from
somewhere else is a bug in this list.

| Root | Enumeration | Precision source |
|---|---|---|
| Statics slab | walk the slab | statics reference bitmap (§9) |
| Interned literal strings | walk the literal handle table | all entries are object ids |
| Thread value stacks | for each thread in the ring, scan `stack_base`…`sp` | **self-describing via `kind`** |
| Thread scratch arenas | for each thread, walk the arena | per-object type refmaps |
| Native scopes | walk the scope chain (§8.2) | every entry is an id by construction |

"For each thread" holds even in a single-threaded build, where the ring has
length one. Writing it that way from Slice A is what makes
[17_Threading_Model.md](17_Threading_Model.md) additive rather than a
rewrite.

### 7.3 Safepoints

**The collector runs only at a safepoint. A safepoint occurs only between
opcode dispatches, or inside `heap_alloc` called from a native intrinsic that
has declared a GC scope.**

Consequences that the rest of the system depends on:

- an opcode handler may hold raw pointers for its own duration (§4.3);
- no interrupt, timer callback, or signal handler may allocate — it must
  queue work for the interpreter loop instead;
- every thread must be at a safepoint before marking begins. With green
  threads this is satisfied **by construction** — a managed thread only ever
  stops at a safepoint — so there is no suspension protocol, no signal, and
  no poll page. See [17_Threading_Model.md §5](17_Threading_Model.md).

This resolves what earlier drafts left open. It holds because there is
exactly one OS thread; real parallelism would reintroduce all of it, which is
why the image records `CAP_SINGLE_OS_THREAD`.

### 7.4 When it runs

**Occupancy threshold, not allocation failure.** `heap_alloc` requests a
collection when a segment passes 75 % of its payload bytes or 75 % of its
handles; the request sets a flag and the collection happens at the next
safepoint. Allocation failure remains a backstop that forces an immediate
collect-and-retry. Thresholds, damping and rationale:
[14_ObjectHandler.md §7.3](14_ObjectHandler.md).

Waiting for failure would guarantee that every collection happens on a full
heap, with the longest possible mark phase, at the least convenient moment.

### 7.5 Non-moving first, compaction later

**Compaction is out of scope for the first release.** v1 sweeps into a free
list and does not move objects. This is deliberate and it is affordable
*because* references are object ids: enabling compaction later rewrites
`handles[i].offset` and nothing else — no pointer in the heap, on any stack,
or in native code changes. The policy can therefore be revisited on
measurement ([14 §5.4](14_ObjectHandler.md)) instead of being guessed now.

When compaction lands it will: sweep, then slide live payloads toward the
segment base in address order, rewriting `handles[i].offset` for each, and
skipping any object with `OBJECT_H_PINNED`. No other structure changes, and
no object id anywhere in the system changes.

### 7.6 Write barrier

```c
static inline object_id_t objects_escape(object_id_t id)
{
    return (OBJECT_SPACE(id) == OBJECT_SPACE_FRAME) ? objects_promote(id) : id;
}

static inline void objects_write_ref(object_id_t * slot, object_id_t value)
{
    *slot = objects_escape(value);
}
```

**Every reference store in the executor and in every intrinsic goes through
this from Slice A.** Stores to object fields, static fields, and array
elements are all covered.

The barrier now has a body, and it earns its keep immediately: it is what
makes frame-scratch allocation ([14 §6.3](14_ObjectHandler.md)) safe without
trusting the packer's escape analysis. A scratch id escaping into a heap
object, a static, or a return value is promoted on the spot — and at `ret`,
before the frame is torn down ([14 §6.5](14_ObjectHandler.md)).

The original reason still stands as well: incremental or concurrent marking
requires the tricolour invariant (*a black object must never hold the only
reference to a white object*), and retrofitting a barrier across a finished
codebase means auditing every store. Extending the body later must be a
one-function change.

Stores to evaluation-stack slots, args and locals do **not** need the barrier:
they are roots, re-scanned on every cycle, never black, and are exactly where
a scratch id is legitimately allowed to live.

### 7.7 Deferred

- **Finalizers.** Not in v1. They require a resurrection-safe second pass and
  a finalizer queue, and they interact badly with a single-threaded
  interpreter. If `IDisposable`-style cleanup is needed, it is explicit.
- **Weak references.** Not in v1.
- **Generational collection.** *(open)* — dependent on survival measurements
  the host build will produce, per
  [10_Native_Assembly_Layout.md §9.5](10_Native_Assembly_Layout.md).

---

## 8. Contracts for native code

Every intrinsic author must know these. A violation manifests as heap
corruption long after the fact.

### 8.1 Arguments are already rooted

An intrinsic's arguments arrive as a window into the caller's evaluation
stack, which is a root. They need no registration and stay valid for the
whole call. The window is passed explicitly — see
[16_Native_Call_Protocol.md §4.1](16_Native_Call_Protocol.md) — rather than
reached through a hidden "current thread" global.

### 8.2 Objects you allocate must be rooted

An object allocated inside an intrinsic is reachable from nothing until it is
stored somewhere reachable. If the intrinsic allocates again before that, the
first object can be collected.

```c
GC_SCOPE(2);                      /* declares 2 rooted slots, links the scope */
GC_KEEP(0, objects_create(...));  /* rooted from here on */
GC_KEEP(1, objects_create(...));  /* safe: slot 0 is rooted across this call  */
...
GC_SCOPE_END();
```

Because references are object ids, the scope only needs to preserve
*liveness* — never addresses. That is a substantially smaller obligation than
in a pointer-based runtime, and it is why the rule is stated as "root what
you allocate" rather than "root everything you touch".

An intrinsic that performs no allocation needs no scope at all, and declares
that fact as `NAT_LEAF` so the executor can skip the surrounding machinery
entirely.

### 8.3 Reference stores go through the barrier

`objects_write_ref()`, always. See §7.6.

### 8.4 Buffers handed to the OS must be pinned

Once compaction exists, an object's address can change between two calls. Any
raw pointer passed to a syscall must be pinned for the duration:

```c
void * p = objects_pin(id);   /* sets OBJECT_H_PINNED, returns raw address */
platform_uart_write(p, len);
objects_unpin(id);
```

Pins must not outlive the intrinsic, and an intrinsic must never block while
holding one. A long-lived pin fragments the heap in exactly the way this
design exists to avoid.

### 8.5 Re-resolve after calling out

If an opcode handler calls anything that can allocate, any raw pointer it
resolved before the call is stale. Re-resolve from the object id.

---

## 9. What `cli_pack` must emit

The collector's precision rests entirely on four bitmaps. Producing them is
the emitter's whole GC contribution; the data is already available from the
existing signature decoder and layout pass.

| Emitted data | Granularity | Consumer | Derived from |
|---|---|---|---|
| **Instance reference bitmap** | per type | marking an object's fields | field layout pass; bit *i* set if the word at offset *i×4* is an `object_id_t` |
| **Array element bitmap** | per array type | marking array elements | the element's layout: bit 0 alone for a reference element, the struct's bitmap for a struct element, none for a primitive — [14 §5.7](14_ObjectHandler.md) |
| **Statics reference bitmap** | whole slab | root scan | §3.1 layout |
| **Args/locals reference bitmaps** | per method | frame root scan | method signature and `LOCAL_SIG`, already decoded by [signature_parser.c](../source/methods/signature_parser.c) |
| Statics slab size + alignment | image | startup sizing | §3.1 |
| Stack region size | image | startup sizing | deepest reachable call chain × (`MaxStack` + locals + args) × recursion budget — §4.5 |
| Per-method `scratch_bytes` + per-thread arena size | image | startup sizing | scratch-classified box sites — [14 §6.4](14_ObjectHandler.md) |
| Handle-table capacity | image | startup sizing | `payload_size / 32` — [14 §9.1](14_ObjectHandler.md) |
| Heap payload minimum + GC thresholds | image | startup sizing | board manifest overrides — §4.5, [14 §7.3](14_ObjectHandler.md) |
| Literal pool + count | image | interning table sizing | §3.2 |
| Per-type instance size and alignment; element size for array types | per type | `heap_alloc`, `array_create` | layout pass |
| Escape classification per `box` site | per instruction | scratch vs heap allocation | [15 §4](15_Pack_Time_IL_Transforms.md) |

The evaluation stack needs **nothing** — it describes itself through
`stack_slot_t.kind`.

Encoding of all of the above is specified in
[12_VXB_Format.md](12_VXB_Format.md).

---

## 10. Open questions

Carried forward deliberately. None blocks Slice A; all block Slice C.

**Resolved:**

- ~~`vee_ref_t` representation~~ — **decided.** A 32-bit object id carrying a
  space tag and an index; handle-indexed for collectable spaces, direct for
  the rest. The heap stays 16-bit addressable on purpose. See
  [14_ObjectHandler.md §2](14_ObjectHandler.md).
- ~~Threading~~ — **decided.** Green threads on one OS thread, preemptively
  scheduled at safepoints. See [17_Threading_Model.md](17_Threading_Model.md).
- ~~Naming~~ — **decided.** No library-wide prefix; the prefix is the owning
  ObjectHandler module ([14 §1.2](14_ObjectHandler.md)).
- ~~Sizing~~ — **decided.** Everything but the heap payload is computed by
  `cli_pack`; the payload minimum is in the header and the board manifest may
  raise it (§4.5).
- ~~Free-list policy~~ — **decided.** First fit with address-ordered
  coalescing, plus the size-class recycler. It is a fragmentation question,
  and the fragmentation ratio decides any successor
  ([14 §5.4](14_ObjectHandler.md)).
- ~~Collection trigger~~ — **decided.** 75 % occupancy, requested as a flag,
  collected at the next safepoint (§7.4, [14 §7.3](14_ObjectHandler.md)).
- ~~Compaction~~ — **decided out of the first release** (§7.5). Revisitable
  later at the cost of one file, because references are object ids.
- ~~Heap segment count~~ — **decided.** One collectable segment, MCU and host
  alike ([14 §5.5](14_ObjectHandler.md)).
- ~~Scratch arena sizing~~ — **decided.** Computed by `cli_pack`; overflow
  falls back to the heap ([14 §6.4](14_ObjectHandler.md)).
- ~~MCU/host divergence~~ — **decided.** One policy for both (§4.6).

**Still open:**

1. **Generational collection** (§7.7) — dependent on survival measurements
   the host build will produce. Nothing else in the design assumes it either
   way.
2. **String representation** — UTF-16 as ECMA requires, or UTF-8 with
   conversion at the native boundary. A 64 KB heap makes this a memory
   decision ([14 §11](14_ObjectHandler.md)).
3. **Recursion budget** (§4.5) — the multiplier applied to the deepest
   acyclic call chain. Needs one measured program, not an argument.

---

## 11. Slice mapping

| Slice | What this document requires |
|---|---|
| **A** | `stack_slot_t` re-sized; per-thread stack region with overlaid frames; `object_id_t`/`object_byref_t` declared; `objects_write_ref` present; root enumeration written as a walk over a thread ring of length one; `EXEC_POLL` sites present with empty bodies. No heap, no collector. |
| **B** | Statics slab and its bitmap; instance layouts and instance bitmaps; bump allocator, still no collection. |
| **C** | Handle table; `heap_alloc`; frame scratch space and the promoting barrier; full tricolour collector with the occupancy trigger; literal interning; native rooting scopes; `String`/`Array` layouts. |
| **D** | Frame unwinding must release native scopes, pins, and scratch high-water marks correctly. |
| **E** | Monomorphised generic types get instance bitmaps like any other type — no collector change. |
| *post-milestone* | Threading ([17](17_Threading_Model.md)) becomes additive if the Slice A and C items above were honoured. |
