# Ruffus VM heritage — what Ruffus CLI inherits, adapts, and rejects

Reference document. Captures the analysis of the existing **Ruffus VM** (a
Java virtual machine for embedded targets, written in C around 2016) and the
decisions about what **Ruffus CLI** takes from it.

Read alongside [11_Memory_Model.md](11_Memory_Model.md) and
[12_VXB_Format.md](12_VXB_Format.md) — both are shaped by what is recorded
here.

The five open questions this analysis raised (§10) have since been answered
in four documents that this one is the origin of:
[14_ObjectHandler.md](14_ObjectHandler.md),
[15_Pack_Time_IL_Transforms.md](15_Pack_Time_IL_Transforms.md),
[16_Native_Call_Protocol.md](16_Native_Call_Protocol.md) and
[17_Threading_Model.md](17_Threading_Model.md).

Source analysed: `/home/wifi/projects/ruffusvm` — 45,749 lines of C plus a
5,230-line Java tool.

---

## 1. Why this matters

Ruffus VM is not merely "a similar project". It is **the same architecture
Ruffus CLI is trying to build**, already implemented, shipped, and proven:

- a **host-side tool** that digests class files into a compact binary image;
- a **portable C executor** that reads only that image and never parses the
  original format;
- **execute-in-place** from ROM/flash with no relocation;
- **operand rewriting at pack time** so the runtime does no symbol resolution;
- a **declarative native-binding mechanism** with generated dispatch;
- a **tricolour garbage collector** over a handle-addressed heap;
- an **OS abstraction layer** isolating platform code.

Every one of those bullets is a design decision already taken independently
in [00_Introduction.md](00_Introduction.md) and
[11_Memory_Model.md](11_Memory_Model.md). The architecture is therefore
validated rather than speculative, and the remaining risk sits almost
entirely in CIL-vs-Java semantic differences (§6).

---

## 2. Provenance and licensing

### 2.1 The NanoVM inheritance

23 files carry a NanoVM copyright header (Till Harbaum, 2005, **GPL v2**):

```
RuffusVM/RuffusVMTool/src/RuffusTool/{AccessFlags, ClassFileReader, ClassInfo,
ClassLoader, CodeInfo, CodeTranslator, CommonInfo, Config, ConstPool,
ConstPoolEntry, ConstPoolEntryError, ConvertException, Debug, ExceptionInfo,
FieldInfo, InnerClassInfo, LineNumberInfo, LocalVariableInfo, MethodIdTable,
MethodInfo, NativeMapper, RuffusVMTool, UVMWriter}.java
```

**All 23 are in the Java tool. Zero C source files carry a GPL header.**

### 2.2 Consequence for Ruffus CLI

This is the cleanest possible boundary, because the GPL'd code is exactly the
code Ruffus CLI does not need:

- `RuffusVMTool` reads **Java class files**. `cli_pack` reads **PE/ECMA-335**,
  and that parser is already written — 7,000 lines, complete, tested (see
  [99_phase0_work.md](99_phase0_work.md)).
- Therefore **no NanoVM-derived code is ported**, and Ruffus CLI is GPL-free
  by construction.

**Rules going forward:**

1. Never copy a file from `RuffusVMTool/` into Ruffus CLI.
2. Where a *design* is reused (operand rewriting, method-ID dedup), it is
   reimplemented in C from the description in this document — not translated
   line by line.
3. Add a `LICENSE`/`NOTICE` to the `ruffusvm` repository recording the
   boundary, while the analysis is fresh.

---

## 3. Obsolescence — and why it mostly does not matter

Ruffus VM targets an old Java and can no longer be used as a Java VM. The
important finding is that **the rot is confined to the part being replaced.**

| Obsolete element | Lives in | Impact on Ruffus CLI |
|---|---|---|
| `javac.source/target=1.7` | `RuffusVMTool` | **None** — tool discarded |
| Java 7 class-file format assumptions | `ClassFileReader`, `ConstPool` | **None** — we parse ECMA-335 |
| `Vector`, `StringTokenizer` (deprecated) | `RuffusVMTool` | **None** |
| Java 7 library semantics | `java_classes/` (~25,000 lines) | **None** — replaced by `Native.*` |
| NetBeans `nbproject/`, VS2010, Xcode projects | build tree | **None** — CMake already used |

**The C runtime contains no Java version knowledge at all.** It consumes a
pre-digested image; the class-file format never reaches it. This is precisely
the benefit of the two-binary split, demonstrated a decade before we chose it.

### 3.1 Genuine C-side issues that do carry over

| Issue | Severity | Action |
|---|---|---|
| `types.h` redefines `uint32_t` etc. **after** including `<stdint.h>` | **High** | Fix — see §8.1; already inherited by `cli_parser` |
| No magic number or version validation in `app_load()` | **High** | `.vxb` must have both — see §5.1 |
| Little-endian hardcoded, no negotiation | Medium | Record in header; decide policy in doc 12 |
| Struct alignment assumed to match file layout | Medium | `.vxb` records must be explicitly sized and aligned |
| GCC/MSVC-only attribute macros (`NAKED`, `WEAK`, `INLINE`) | Low | Add a Clang branch |
| `(a << 16)` macros without explicit `uint32_t` cast | Low | Cast explicitly |

---

## 4. Architecture mapping

| Ruffus VM | Ruffus CLI | Notes |
|---|---|---|
| `RuffusVMTool/` (Java, 5,230 lines) | **`cli_pack`** (C) | Already exists as the ECMA-335 parser; needs the emitter stage |
| `RuffusVM/Source/vm/` + `ObjectHandler/` | **`cli_exec`** | ~8,000 relevant lines |
| image loaded by `app_load()` | **`.vxb`** | Format shape reusable; add magic/version/TLV |
| `app_header_t` | `.vxb` header | §5.1 |
| `CodeTranslator.constantRelocate()` | IL operand rewriting | §5.3 |
| `MethodIdTable.build()` | vtable slot assignment | §5.4 |
| `NativeClasses/*.native` + `NativeMapper` | `native_bcl` binding ids | §5.5 |
| `Boards/Linux`, `Boards/MacOS` | `framework/platform/` | Port nearly as-is |
| `ObjectHandler/` (as a separable component) | **`source/objecthandler/`** | Concept kept and enforced — [14](14_ObjectHandler.md) |
| `ObjectHandler/heap.c` | `objecthandler/heap.c` | Design ported, file rewritten — §6.2 |
| `ObjectHandler/gc.c` | `objecthandler/gc.c` | Port algorithm, replace data structures |
| `ObjectHandler/objects.c`, `strings.c`, `arrays_type.c` | `objecthandler/objects.c`, `strings.c`, `array.c` | Module split and prefixes kept — [14 §1.2](14_ObjectHandler.md) |
| `ObjectHandler/stack.h`, `value_type.c` | — | **Rejected** — see §6.1 |
| `vm/threads.c` | `cli_exec` scheduler | Model followed closely — [17](17_Threading_Model.md) |
| `java_classes/` | `Native.*` assemblies | Pattern reused, content discarded |

---

## 5. Proven designs inherited

### 5.1 Image format

From `RuffusVM/Source/vm/datatypes.h`:

```c
typedef struct {
    uint16_t total_methods;
    uint16_t total_const;
    uint16_t total_classes;
    uint16_t total_statics;
    uint16_t reserved;
    uint16_t main;              /* index of the entry method */
    uint32_t constant_offset;
    uint32_t method_offset;
    app_class_t class_list[];   /* inline, immediately after the header */
} app_header_t;                 /* 20 bytes + variable tail */

typedef struct {
    uint16_t super;             /* index into class_list; 0 == Object */
    uint16_t fields;            /* instance field count */
} app_class_t;                  /* 4 bytes */

typedef struct {
    uint32_t code_index;        /* offset into the code section */
    uint32_t id;                /* (class_index << 16) | method_id */
    uint16_t index;
    uint8_t  flags;             /* NATIVE | STATIC | SYNCHRONIZED | CLINIT */
    uint8_t  args;              /* includes 'this' */
    int8_t   max_locals;
    uint8_t  max_stack;
    uint8_t  max_exception;
    uint8_t  max_debug;
    uint8_t  max_argrefs;
    uint8_t  reserved1;
    uint16_t code_size;
} app_method_t;                 /* 20 bytes */
```

Constant pool is a 4-byte header array (`type`, `offset`) plus a data
section, deduplicated across all classes, 4-byte aligned.

**What Ruffus CLI takes:** the overall shape — counts, an entry-method
*index* rather than a token, section offsets, fixed-size records indexed
directly, method header carrying `max_stack`/`max_locals`/`args`/`code_size`
inline.

**What Ruffus CLI must add:**

- **magic number and format version** — `app_load()` casts the buffer with
  no validation whatsoever, which is the single worst flaw in the design;
- **TLV section directory** for forward compatibility (Ruffus has fixed
  sections only);
- **reference bitmaps** for precise GC ([11_Memory_Model.md §9](11_Memory_Model.md));
- **EH clause table** (Ruffus stores exception entries per method; CIL needs
  filter clauses too);
- **value-type layout** — no Java analogue (§6.1);
- wider counters — `uint16_t` caps everything at 65,535.

### 5.2 Execute in place

`app_load()` does `vm_app->file_header = (app_header_t *)buffer` and carries a
`from_rom` flag; methods are reached by `&methods_ptr[index]` and the PC is a
raw `uint8_t *` into the code section. No relocation, no copy to RAM.

This is exactly the XIP model described in
[00_Introduction.md §2](00_Introduction.md), demonstrated working.

### 5.3 Operand rewriting at pack time

`CodeTranslator.translate()` performs two classes of transformation:

**Index relocation** — every `LDC`, `LDC2_W`, `GETSTATIC`, `PUTSTATIC`,
`GETFIELD`, `PUTFIELD`, `INVOKEVIRTUAL`, `INVOKESPECIAL`, `INVOKESTATIC`,
`INVOKEINTERFACE` operand is rewritten from a class-local constant-pool index
to a **global merged-pool index**.

**Opcode normalisation** — instructions the runtime need not distinguish are
folded together:

```
ASTORE → ISTORE          ACONST_NULL → ICONST_0
ARETURN → IRETURN        IFNULL → IFEQ,  IFNONNULL → IFNE
I2B, I2C, I2S → NOP
```

**For Ruffus CLI:** the relocation strategy is directly applicable and is
already planned ([00_Introduction.md §3.2](00_Introduction.md)). Opcode
normalisation is *also* attractive for CIL — `ldarg.0`/`ldloc.0` short forms
could be folded, `nop` elided, and prefix bytes resolved at pack time — but
it must be applied with care because CIL `conv.*` opcodes are **not**
redundant the way Java's `I2B`/`I2C`/`I2S` are. CIL narrowing is semantically
required.

A parameter-length table (`PARAMETER_BYTES[256]`) drives operand parsing.
Ruffus CLI needs the same, for both the single-byte and `0xFE`-prefixed CIL
opcode spaces — the data already exists in
[il_opcodes.h](../source/methods/il_opcodes.h).

### 5.4 Method-ID deduplication for virtual dispatch

`MethodIdTable.build()` assigns **one ID to every method sharing a name and
signature across the whole program**, so a virtual call is a single table
index rather than a per-class search.

This is the same outcome the vtable construction in
[Virtual_Method_Tables.md](../docs-archive/Virtual_Method_Tables.md) produces,
reached by a different route. Worth comparing the two approaches when the
emitter's vtable pass is written — Ruffus's global-ID scheme may be more
compact for an MCU than per-class vtables, at the cost of a wider dispatch
table.

### 5.5 Native binding

Declarative `.native` files, e.g. `NativeClasses/lang_Math.native`:

```
class java/lang/Math 5

field  E:D 0
field  PI:D 1

method abs:(D)D
method abs:(F)F
method abs:(I)I
```

`NativeMapper.java` parses these at pack time, assigns class IDs from
`NATIVE_CLASS_BASE = 8192`, and packs method IDs as
`(class_id << shift) | method_id`. The C side has a generated enum plus a
dispatch switch (`native.c`), and implementations follow a uniform
convention:

```c
void native_method_lang_math_abs_double(void)
{
    object_t a = stack_pop();
    double v = value_get_double(a);
    if (v < 0) v *= -1;
    stack_push_double(v);
}
```

Arguments arrive on the operand stack; the function pops them, computes, and
pushes the result. No arguments are passed in C parameters at all.

**For Ruffus CLI:** the *mechanism* — declaration file, pack-time ID
assignment, generated dispatch, binding id stored in the method record — is
exactly the design in [00_Introduction.md §3.6](00_Introduction.md) and
should be reused. The `.native` syntax needs replacing with .NET type
signatures, and this dovetails with the `nativegen` tool already specified in
[10_Native_Assembly_Layout.md §5.2](10_Native_Assembly_Layout.md).

The zero-argument C prototype is worth reconsidering: doc 00 §6.6 specifies
an explicit `(thread, args, arg_count, ret)` signature, which is easier to
test and less error-prone than implicit stack manipulation.

### 5.6 Tricolour collector

`ObjectHandler/gc.c` implements exactly the algorithm described in
[11_Memory_Model.md §7](11_Memory_Model.md). Its own comment states it:

> *apply tricolor process: Blacken a greyed object by greying all the white
> objects it references directly until there are no more grey objects. All
> the remaining white objects have been demonstrated not to be reachable
> hence can be deleted.*

Roots are greyed first — statics, then every thread's stack — and everything
else starts white. Constants, null, and native objects are skipped.

---

## 6. The three divergences

These are where Ruffus VM's implementation cannot be carried across.

### 6.1 Boxed primitives on the evaluation stack — the serious one

Ruffus's stack is an array of `object_t` handles, and **primitives are heap
objects**:

```c
static INLINE void stack_push_integer(int32_t v)
{
    stack_push(value_create_decimal(v));          /* allocates */
}

static INLINE int32_t value_get_decimal(object_t o)
{
    return *((int32_t *)object_get_ptr(o));       /* dereferences */
}
```

`instance_t.sp` is `object_t *`. Every arithmetic result allocates.

This is defensible for Java on a microcontroller running slow control logic —
it makes GC scanning trivial because every slot is a reference. It is
**unacceptable for CIL**, which has natively typed stack slots (§III.1.1:
`int32`, `int64`, `native int`, `F`, `O`, `&`) and user-defined value types
stored inline.

**Consequence:** `ObjectHandler/stack.h` and `value_type.c` are rejected. The
tagged `stack_slot_t` in
[11_Memory_Model.md §6.2](11_Memory_Model.md) stands as designed, and the
interpreter core is written fresh rather than ported. Ruffus's frame/thread
structures remain useful as a *model* for layout and lifetime — and more than
a model for threading, where
[17_Threading_Model.md](17_Threading_Model.md) follows them closely.

What replaces boxing is not a faster allocator but three tiers that remove
most of the allocation altogether — tagged slots, a frame scratch space, and
a size-class recycler. See
[14_ObjectHandler.md §6](14_ObjectHandler.md).

### 6.2 `object_t` is a heap offset, not a handle index

```c
typedef uint16_t object_t;                          /* instance.h:64 */

static heap_header_t * _locate_item(object_t id)
{
    return (heap_header_t *)(&P_HEAP_DATA.memory[id - 1]);
}
```

A reference is a **16-bit byte offset into the heap buffer**, so the heap is
capped at 64 KB and a reference designates a block, not a slot.

**Correction, found on a closer reading of `heap.c`.** Ruffus is not as far
from a handle table as this first appeared. `heap_object_malloc()` allocates
class, array and string objects as **two blocks**: a tiny record holding a
`uint8_t * data` pointer (`heap_header_t3`), plus a separate payload block.
`heap_object_resize()` already moves the payload and rewrites that one
pointer. Only value and data objects (`field_ref <= T_LINKED`) are stored
inline. So payload compaction was always possible; what was missing was
collecting those records into a dense array instead of scattering them
through the heap.

**Decision, taken rather than deferred.** A reference becomes 32 bits: a
16-bit **space** tag plus a 16-bit index, where the index selects a handle in
collectable spaces and a direct offset elsewhere. This:

- keeps the 64 KB heap that suits the target, and makes it a per-segment
  limit rather than a program-wide one;
- finishes Ruffus's two-block design by giving the records their own dense
  array, which is what makes compaction a rewrite of one field;
- generalises `CONST_OBJECT_ID` (§5.1) from one magic bit into a real space
  tag, so constants, statics, frame temporaries and foreign memory all become
  ordinary references;
- removes the per-object heap header entirely, cutting overhead from Ruffus's
  20–24 bytes to 8.

Specified in [14_ObjectHandler.md §§2–5](14_ObjectHandler.md). The open
question this section previously raised in
[11_Memory_Model.md §10](11_Memory_Model.md) is closed.

### 6.3 GC data structures

`gc.c` maintains three explicit lists — white, grey, black — and searches
them linearly (`_object_tc_gc_search`). That is O(n²) in live objects.

Keep the algorithm; replace the structures with the in-handle mark bit and
bounded worklist specified in
[14_ObjectHandler.md §7.2](14_ObjectHandler.md).

---

## 7. Reuse inventory

| Component | Lines | Verdict |
|---|---|---|
| Two-binary architecture, image shape, XIP | — | **Take wholesale** |
| Operand-rewrite strategy (`CodeTranslator`) | — | **Take design**, reimplement in C — [15](15_Pack_Time_IL_Transforms.md) |
| Method-ID dedup (`MethodIdTable`) | — | **Take design**, compare with per-class vtables |
| `.native` declaration + generated dispatch | — | **Take design**, new syntax + explicit protocol — [16](16_Native_Call_Protocol.md) |
| `Boards/` platform layer | ~600 | **Port nearly as-is** → `framework/platform/` |
| `ObjectHandler/` as a separable component | — | **Take the boundary**, enforce it in CI — [14 §1](14_ObjectHandler.md) |
| `ObjectHandler/heap.c` | 818 | **Take the design** (two-block objects, first-fit + coalescing); rewrite the file — §6.2 |
| `ObjectHandler/gc.c` | 267 | **Port algorithm**, replace data structures |
| `objects.c`, `instance.c`, `arrays_type.c`, `strings.c` | 1,488 | **Port with CIL rework** — value types, byrefs |
| `vm/threads.c` | ~500 | **Take the model** — states, slices, monitors — [17](17_Threading_Model.md) |
| `vm/exceptions.c` | ~250 | **Reference only** — patterns for deferred work |
| `vm/vm.c`, `vm/application.c` | 1,193 | **Reference** — loader and dispatch model; note the per-instruction timer read that §4.1 of [17](17_Threading_Model.md) replaces |
| `ObjectHandler/stack.h`, `value_type.c` | 578 | **Reject** — boxing model (§6.1) |
| `java_classes/` | ~25,000 | **Reject** — Java semantics |
| `RuffusVMTool/` | 5,230 | **Reject** — GPL, Java, already superseded |

Approximately **3,000–3,500 lines directly portable**, plus validated designs
for the packer, image format, native bridge, collector and scheduler.

---

## 8. Carried-over defects to fix

### 8.1 `types.h` type redefinition

`ruffusvm/ObjectHandler/types.h` includes `<stdint.h>` and then redefines
`uint8_t`, `uint16_t`, `uint32_t`, `int32_t`, `uint64_t` etc. behind
architecture `#ifdef`s.

**`cli_parser/source/types.h` contains the same block** — these are sibling
files from a shared base. It compiles today only because C11 permits
repeating an identical typedef; it will break on a stricter compiler, or
silently produce wrong widths on a target where the `#ifdef` guesses are
wrong.

**Action:** fix in `cli_parser` now, before the executor multiplies the
dependency. Rely on `<stdint.h>`; keep only the project-specific aliases
(`bool_t`, `u32_t`, …) that genuinely add value.

### 8.2 No image validation

`app_load()` casts the input buffer straight to `app_header_t *` with no
magic, no version, no size check. A truncated or foreign file produces
undefined behaviour.

**Action:** `.vxb` carries a magic number, a format version the executor
refuses to exceed, a total-size field, and a required-section mask. Specified
in [12_VXB_Format.md](12_VXB_Format.md).

---

## 9. Effect on the plan

The slice plan in [00_Introduction.md §4](00_Introduction.md) is unchanged in
shape, but several slices now start from working code rather than a blank
file:

| Slice | Was | Now |
|---|---|---|
| **A** | build frames + eval stack from scratch | new `stack_slot_t` core (no boxing), using Ruffus's frame/thread layout as the model |
| **B** | write layout + allocator | **rebuild `heap.c`** as ObjectHandler's allocator; add CIL value-type layout |
| **C** | write handle heap + collector + native bridge | **port `gc.c`'s algorithm**; new data structures; reuse the `.native` binding design with an explicit protocol |
| — | design platform abstraction | **`Boards/` already exists** |
| — | invent `.vxb` | **adapt `app_header_t`** + magic, version, TLV, bitmaps, EH |
| *post-milestone* | threading undecided | **`threads.c` is the model**, preemption at safepoints — [17](17_Threading_Model.md) |

Note the change to B and C. The earlier plan said "port `heap.c`"; after
§6.2, what is ported is the *design* — the two-block object layout and the
first-fit-plus-coalescing allocator — into the new
[ObjectHandler](14_ObjectHandler.md), rather than the file. `gc.c`'s
algorithm is still a direct inheritance.

Immediate sequence:

1. Fix `types.h` in `cli_parser` (§8.1).
2. Write [12_VXB_Format.md](12_VXB_Format.md) as an *adaptation* of
   `app_header_t` rather than a fresh design.
3. Port `Boards/` → `framework/platform/`.
4. Slice A.

---

## 10. Open questions this analysis raised

All five were taken up and decided. This section is kept as the record of
where each was answered.

| # | Question | Outcome |
|---|---|---|
| 1 | **Reference representation** (§6.2) — direct heap offset or handle table | **Decided: both.** A 32-bit reference with a 16-bit space tag; handle-indexed in collectable spaces, direct offset elsewhere. The 64 KB heap is kept deliberately. [14_ObjectHandler.md §2](14_ObjectHandler.md) |
| 2 | **Global method IDs vs per-class vtables** (§5.4) | Still open. Compare when the emitter's dispatch pass is written; note that [15 §5](15_Pack_Time_IL_Transforms.md) devirtualises many call sites at pack time, which shrinks whichever structure is chosen |
| 3 | **Opcode normalisation for CIL** (§5.3) | **Decided.** Five classes of fold, with `conv.*` explicitly protected and superinstructions capability-gated. [15_Pack_Time_IL_Transforms.md](15_Pack_Time_IL_Transforms.md) |
| 4 | **Native calling convention** (§5.5) | **Decided.** Explicit stack window, pack-time shape selection, a dedicated `calln` opcode, behaviour flags, and an ABI hash. [16_Native_Call_Protocol.md](16_Native_Call_Protocol.md) |
| 5 | **Threading** | **Decided.** Green threads on one OS thread, preempted at safepoints, with `threads.c` as the reference for states and monitors. [17_Threading_Model.md](17_Threading_Model.md) |

Two further findings came out of answering them, and are recorded above
rather than here: the correction to §6.2 about Ruffus's two-block object
layout, and the observation that green threads make precise stop-the-world
collection *easier* than a single-threaded design would suggest
([17 §5](17_Threading_Model.md)).
