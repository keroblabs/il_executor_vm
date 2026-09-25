# Pack-time IL transforms — folding, normalisation, superinstructions

Contract document. Specifies what `cli_pack` is allowed to do to an IL
instruction stream, what it is forbidden to do, and how the result is
verified.

Ruffus VM's `CodeTranslator` proves the approach works
([13_Ruffus_VM_Reuse.md §5.3](13_Ruffus_VM_Reuse.md)); this document decides
how far it can be taken for CIL, where Java's assumptions do not hold.

Read with [12_VXB_Format.md](12_VXB_Format.md) (which carries the capability
mask this document depends on) and
[il_opcodes.h](../source/methods/il_opcodes.h).

---

## 1. Why fold at all

In a switch-dispatch interpreter, the cost of an instruction is dominated by
dispatch, not by work. A `nop`, an `ldarg.0`, and an `add` all cost roughly a
mispredicted indirect branch. Therefore:

- **Removing an instruction is worth more than simplifying one.**
- **Fusing two instructions into one removes a whole dispatch.**
- Shrinking operands matters for flash, not for speed.

That ranking is what orders the classes below. It is also why "fold `ldarg.0`
and `ldarg` into one handler" — the obvious reading of Ruffus's
normalisation — is the *wrong* direction for CIL: it trades a free dispatch
table entry for a mandatory operand fetch on the single most frequent
instruction in any program.

### 1.1 The rule that constrains everything

> `cli_pack` may change *how* the program is encoded. It may never change
> *what* the program observably does, including which exceptions it throws
> and in what order.

Java tolerated `I2B`/`I2C`/`I2S` → `NOP` because those instructions are
redundant given Java's stack typing. CIL's `conv.*` are **semantically
required**: `conv.i1` on `0x1234` must produce `0x34`. The single most
important thing this document does is refuse that fold.

---

## 2. Classification

| Class | Nature | Requires | Default |
|---|---|---|---|
| **1** | Encoding canonicalisation | offset map | on |
| **2** | Dead-instruction removal | offset map | on |
| **3** | Type-proved redundancy | abstract stack typing | on |
| **4** | Prefix resolution | abstract stack typing | on |
| **5** | Superinstruction fusion | capability mask + dumper | **off** |
| **X** | Forbidden | — | never |

---

## 3. Class 1 — encoding canonicalisation

Always safe; changes bytes, not behaviour. Shrinks the image, which on a
flash-constrained part is the point.

| Transform | Note |
|---|---|
| `ldarg`/`ldarg.s`/`ldarg.0-3` → the **narrowest** form the index allows | and the same for `ldloc`, `stloc`, `starg`, `ldloca`, `ldarga` |
| `ldc.i4 <n>` → `ldc.i4.m1`, `ldc.i4.0-8`, `ldc.i4.s` where representable | |
| `ldc.i8 <n>` where `n` fits int32 → `ldc.i4.*` + `conv.i8` | only when it is smaller; rarely is |
| long branch → short branch | **iterate to fixpoint** — narrowing a branch shortens the code, which can make other branches narrowable |
| `switch` with all targets equal → `pop; br` | |

The branch-narrowing fixpoint is the only part with a subtlety: shrink
optimistically, then re-check, and stop when a pass changes nothing.
Two or three passes is typical; cap it and fall back to long forms.

---

## 4. Class 2 — dead-instruction removal

| Transform | Condition |
|---|---|
| remove `nop` | always — `mcs`/`csc` debug builds are full of them |
| remove `br` / `br.s` whose target is the next instruction | always |
| remove `dup; pop` | always |
| remove code after `ret`, `br`, `throw`, `rethrow`, `endfinally` up to the next branch target or EH boundary | always |
| remove a `call` to an **empty non-virtual method** and pop its arguments | body is exactly `ret` (or `ldarg.0; ret` for a trivial getter → replace with `ldfld`) |
| mark a `box` site as **scratch** | result never stored to a field/static/array element and never returned — see [14_ObjectHandler.md §6.3](14_ObjectHandler.md) |

The empty-call fold matters more than it looks. Every `newobj` of a class
whose base is `System.Object` calls `System.Object..ctor`, which is empty.
That is one call, one frame push, one frame pop, and one `ret` removed from
every single object construction in the program. Ruffus does not do this.

The scratch-box marking is the only Class 2 transform whose failure mode is
interesting, and it has none: getting it wrong costs a promotion at run time,
not a bug, because the promoting write barrier is unconditional.

---

## 5. Class 3 — type-proved redundancy

These require an **abstract stack-typing pass** — a per-instruction
simulation of the evaluation stack carrying ECMA §III.1.1 types plus, where
known, an exact `types[]` index. The packer needs that pass anyway, to emit
the args/locals reference bitmaps
([11_Memory_Model.md §9](11_Memory_Model.md)) and to validate the stream.

| Transform | Condition | Value |
|---|---|---|
| remove `conv.i4` | stack top is already `int32` | common after `ldlen`, `ldfld` of an int |
| remove `conv.r8` | stack top is already `F` | |
| remove `conv.i`/`conv.u` | always — **`native int` is 32-bit on every target** (§11) | `IntPtr`-heavy code |
| `castclass T` → nothing | abstract type is provably a subtype of `T` **and** provably non-null, or `T` is the exact type | |
| `isinst T` → `ldc.i4.0`/keep | only removable when provably null; otherwise keep — it yields a value | |
| `box T; unbox.any T` → nothing | no intervening use of the boxed reference | |
| `callvirt M` → `call M` | receiver type is sealed, or `M` is non-virtual, or the exact type is known — **but keep the null check** | large win: turns a vtable index into a direct index |
| `ldsfld` of a `static readonly` primitive → `ldc.*` | the declaring `.cctor` is provably a sequence of constant stores — §5.1 | |

The `callvirt → call` fold must preserve the null check, because `callvirt`
throws `NullReferenceException` on a null receiver and `call` does not. Emit
`call.nn` (call with null check) rather than dropping it, or keep `callvirt`
and let the executor see a devirtualised method index. **Decision: emit the
direct method index in the `callvirt` operand and set a `DEVIRT` flag bit**
— the handler then skips vtable lookup but keeps the null test. One bit, no
new opcode, no lost check. Bit placement is settled in §11.

### 5.1 `.cctor` folding, by example

This is the one transform that removes work the program never sees: a class
initialiser that only stores constants can be *run at pack time*, its results
written straight into the statics slab image, and the type marked
`CCTOR_DONE` so no lazy-initialisation guard is emitted at any `ldsfld`,
`stsfld` or `newobj` site for it.

**Example 1 — the common case.**

```csharp
static class Board {
    public static readonly int  Baud    = 115200;
    public static readonly int  RxPin   = 3;
    public static readonly bool Inverted = false;
}
```

`csc`/`mcs` emit:

```
.cctor:
    ldc.i4     115200
    stsfld     int32 Board::Baud
    ldc.i4.3
    stsfld     int32 Board::RxPin
    ldc.i4.0
    stsfld     bool  Board::Inverted
    ret
```

The packer walks this with a **mini-interpreter over a restricted subset**,
keeping an abstract stack of constants. Every instruction is in the subset,
the body terminates, so:

- the three values are written into the statics slab image at their laid-out
  offsets;
- `Board` is marked `CCTOR_DONE` and its `.cctor` is dropped from the image
  entirely;
- every `ldsfld int32 Board::Baud` in the program becomes `ldc.i4 115200`,
  which then narrows in Class 1 and often folds into an arithmetic
  superinstruction afterwards.

A `for` loop reading `Board.RxPin` goes from *load static, check initialised,
dereference slab* to nothing at all.

**Example 2 — the one that looks foldable and is not.**

```csharp
static class Lookup {
    public static readonly byte[] Gamma = { 0, 2, 4, 8, 16 };
    public static readonly DateTime Boot = DateTime.Now;
}
```

`Gamma` compiles to `newarr; dup; ldtoken <FieldRVA blob>; call
RuntimeHelpers::InitializeArray`. That *is* foldable, but not by the constant
interpreter: it is handled by the existing `FieldRVA` path — the bytes are
already in the image (region 1, [11 §2.1](11_Memory_Model.md)), and the
packer emits an immortal array wrapper over them, with no `.cctor` at all.

`Boot` is not foldable by anything: `DateTime.Now` is a call with an
observable result that differs per run. One unfoldable store **disqualifies
the whole `.cctor`**, because partial execution would run half a class
initialiser at pack time and the rest at startup, in an order the program can
observe.

**The subset.** A `.cctor` is foldable only if every instruction in it is one
of:

| Allowed | Note |
|---|---|
| `ldc.i4*`, `ldc.i8`, `ldc.r4`, `ldc.r8`, `ldnull` | the constants |
| `ldstr` | resolves to a literal-pool index, already immortal |
| `conv.*` (non-`ovf`), `neg`, `not`, `add`, `sub`, `mul`, `shl`, `shr`, `and`, `or`, `xor` | evaluated with CIL semantics, not C's |
| `stsfld` to a field **of the declaring type** | a store elsewhere is a side effect |
| `dup`, `pop`, `nop` | |
| `ret` | must be the only exit |

Anything else — any `call`, any branch, any `newobj`, any `throw`, any
`div`/`rem` (they can throw), any store to another type's static — stops the
fold. No loops, so termination is trivially guaranteed; no analysis of order
between classes is needed, because a foldable `.cctor` by construction reads
nothing.

**Why it is still gated.** The interpreter must implement CIL arithmetic
exactly — `int32` wraparound, `conv.i1` narrowing, `shr` versus `shr.un` — and
a packer that gets one of those wrong produces a program that is wrong before
it ever runs, silently. The differential-execution test (§9.3) is what makes
this safe: pack twice, once with folding and once without, and compare the
statics slab byte for byte.

---

## 6. Class 4 — prefix resolution

CIL prefixes are the clearest case of work that belongs on the host: they
modify the *next* instruction, so leaving them in the stream costs a whole
extra dispatch to set a flag.

**Decision: no prefix byte survives into `.vxb`.** Each is either discarded
or folded into a flag bit on the instruction it modified.

| Prefix | Resolution | Reason |
|---|---|---|
| `readonly.` (`0xFE 0x1E`) | **discard** | verification-only |
| `no.` (`0xFE 0x19`) | **discard** | suppresses checks we perform anyway |
| `tail.` (`0xFE 0x14`) | **discard**, record `HAS_TAILCALL` in the method flags | a hint; we do not implement tail calls in v1, and dropping it is legal. The method flag exists so deep-recursion failures can be diagnosed |
| `volatile.` (`0xFE 0x13`) | **discard** | see below |
| `unaligned.` (`0xFE 0x12`) | **fold to a flag bit** on the `ldind`/`stind`/`cpblk`/`initblk` it precedes | ARMv7-M faults on unaligned `LDRD`; the executor must route through `memcpy` |
| `constrained.` (`0xFE 0x16`) | **resolve at pack time** | see below |

**`volatile.` is discardable, and that is a consequence of
[17_Threading_Model.md](17_Threading_Model.md), not an accident.** Managed
threads are green threads multiplexed onto one OS thread, and a switch can
only happen at a safepoint. There is no other observer and no memory
reordering across a safepoint, so a volatile load is indistinguishable from a
plain one. If true OS-level parallelism is ever added, this row is the first
thing that must change — record it in the capability mask as
`CAP_SINGLE_OS_THREAD` so an image built under this assumption cannot be run
by an executor that violates it.

**`constrained. T; callvirt M`** is resolved by the packer, because `T` is
always a static token:

| `T` | Rewrite |
|---|---|
| reference type | `callvirt M` (the byref is dereferenced; emit `ldind.ref` before) |
| value type overriding `M` | direct `call T::M` on the byref — **no boxing at all** |
| value type not overriding `M` | `ldobj T; box T; callvirt M` — and the `box` is a scratch box (§4) because the reference cannot escape a call to an `Object` method |

The third row is the common case for `ToString()`/`GetHashCode()` on a
struct, and routing it through the scratch space turns the classic
"`foreach` over a `List<int>` allocates" pattern into zero heap traffic.

---

## 7. Class 5 — superinstructions

A superinstruction removes a dispatch. That makes it the highest-value
transform available and the most dangerous, because every one is a new
opcode that `cli_exec`, `cli_dump` and the test harness must all agree on.

### 7.1 Rules

1. **Encoding — new opcodes, never repurposed ones.** Superinstructions live
   at `0xFE 0x80`–`0xFE 0xFF`. ECMA uses `0xFE 0x00`–`0xFE 0x1E`; leaving
   `0x1F`–`0x7F` free keeps room for the standard to grow. **No existing
   opcode ever changes meaning.** Reusing, say, `ckfinite`'s encoding for a
   fused sequence would save one byte of table space and cost every tool
   that reads the stream — `cli_dump`, the differential test, a future
   debugger — a special case that cannot be detected from the bytes. The
   reserved range is free; there is nothing to buy by repurposing.
2. **Optional by construction.** The image header carries
   `CAP_SUPEROPS` plus a *superinstruction set version*. `cli_pack` emits
   them only when the target profile declares support. An executor that does
   not support them refuses the image at load — it must never silently
   mis-execute one.
3. **Reversible.** `cli_dump` expands every superinstruction back to its
   component sequence. A dump of a folded image and a dump of an unfolded
   image must be textually identical apart from a marker column.
4. **Measured, not guessed.** No superinstruction is added without a profile
   from the host build showing the sequence in the top 20 by dynamic count.
   The list below is the *candidate* list, not the approved list.
5. **Never across a boundary.** A sequence may not be fused if any member
   after the first is a branch target, is the first or last instruction of a
   protected region, or is a safepoint the scheduler needs
   ([17_Threading_Model.md §4](17_Threading_Model.md)).

### 7.2 Candidates

| Sequence | Fused | Why it is frequent |
|---|---|---|
| `ldarg.0; ldfld f` | `ldfld.this f` | every instance field read |
| `ldarg.0; ldarg.1; stfld f` | `stfld.this.arg f, 1` | every trivial setter |
| `ldloc.N; ldc.i4.M; add; stloc.N` | `inc.loc N, M` | every `for` loop |
| `ldc.i4.M; add` | `add.ci M` | pointer and index arithmetic |
| `ldloc.a; ldloc.b; blt.s L` | `blt.ll a, b, L` | every `for` loop condition |
| `ldnull; ceq` | `isnull` | every null test written as `== null` |
| `ldloc.N; ldloc.M; ldelem.i4` | `ldelem.ll` | every array read in a loop |
| `dup; brtrue.s L; pop` | `?? L` | null-coalescing |

Note how many are loop bodies. The dynamic count of a loop body is what
justifies fusion; straight-line code does not.

### 7.3 The cost being accepted

Each superinstruction is an entry in three places (`cli_pack` matcher,
`cli_exec` handler, `cli_dump` expander) and a row in the conformance test
matrix. Eight of them is a manageable tax. Forty is a second instruction set.
**Cap the approved set at 16** and require a profile delta to raise it.

---

## 8. Class X — forbidden

Removing a check is not an optimisation; it is a different program.

| Never | Because |
|---|---|
| remove or reorder `conv.ovf.*` | must throw `OverflowException` |
| remove `div`, `div.un`, `rem`, `rem.un` | must throw `DivideByZeroException` |
| remove `ckfinite` | must throw `ArithmeticException` |
| remove an array bounds check, a null check, or a store type check | observable |
| remove `castclass` without a proof of subtype **and** non-nullness | observable |
| reorder any two instructions with side effects | field/array/static writes, calls, allocations |
| fold across a protected-region boundary | the unwinder addresses regions by IL offset |
| fold across a safepoint | a thread switch must be able to land there |
| fold `conv.i1`/`i2`/`u1`/`u2`/`i4`/`u4` into `nop` because Java could | CIL narrowing is arithmetic, not a type assertion |

---

## 9. Machinery

### 9.1 Pass order

```
1  decode        → instruction list, one node per instruction
2  type          → abstract stack types at every instruction
3  fold          → classes 2, 3, 4 (semantic); iterate to fixpoint
4  fuse          → class 5, if enabled
5  relocate      → operands: token → index, ldstr → literal index
6  narrow        → class 1; iterate to fixpoint
7  emit          → bytes + offset map
8  fixup         → branch targets, switch tables, EH clauses
```

Steps 3 and 6 are separate because semantic folds change instruction counts
and encoding narrowing changes only sizes; interleaving them makes the
fixpoint hard to reason about.

### 9.2 The offset map

Every transform maintains `old_offset → new_offset`. Three consumers, and all
three are easy to get subtly wrong:

- **Branch targets and `switch` tables** — map the target offset.
- **EH clauses** — `TryOffset`, `HandlerOffset` and `FilterOffset` map
  directly; `TryLength` and `HandlerLength` **do not**. Map the *exclusive
  end* (`offset + length`) as an offset in its own right and subtract, or a
  removed `nop` at the end of a `try` silently shrinks the protected region.
- **Debug line mapping**, if it is ever emitted.

An instruction that is a branch target or an EH boundary is **pinned**: it
may be re-encoded but never removed and never fused into a predecessor. Mark
those nodes before step 3 and the rest of the pipeline stays simple.

### 9.3 Verification

Three layers, in increasing cost:

1. **Invariant** — after every pass, re-run the abstract typing. The stack
   depth and type at each surviving instruction must match the pre-fold
   value at its original offset. A fold that changes either is rejected, and
   the pass is reported rather than silently skipped.
2. **Round trip** — `cli_dump` of the folded image, with superinstructions
   expanded, diffed against `cli_dump` of the same assembly packed with all
   folding disabled. Only the expected differences may appear.
3. **Differential execution** — the regression runner packs each fixture
   twice, folded and unfolded, runs both, and compares output, exit code and
   the exception trace. A `--no-fold` switch on `cli_pack` is therefore not a
   debugging convenience; it is a required product feature.

---

## 10. Effect on the image

Rough expectation for a `mcs`-compiled debug assembly, to be replaced by
measurement:

| Transform | Code size | Dispatches |
|---|---|---|
| `nop` removal | −8 % | −8 % |
| prefix resolution | −1 % | −2 % |
| branch narrowing | −3 % | 0 |
| empty-ctor elision | −1 % | **−5 %** |
| `constrained.` resolution | −1 % | −2 %, plus allocations removed |
| superinstructions (16) | +0 % | **−15 % to −25 %** |

Everything above the superinstruction row is unconditional and cheap.
Superinstructions are where the real gain is and where the real risk is,
which is why they are gated and reversible.

---

## 11. Decisions and what remains open

**Resolved in this revision:**

### `.cctor` constant folding — **in scope, gated**

The mini-interpreter, its instruction subset, and two worked examples are in
§5.1. It is enabled per image by `CAP_CCTOR_FOLD` and verified by the
differential test, because a wrong constant is a program that is wrong before
it runs. Measure how often `.cctor` guards fire in Slice B; the fold is only
worth its verification cost if they fire on a hot path.

### User-defined instructions — **new encodings, never repurposed**

Superinstructions *are* the mechanism for adding instructions, and the
reserved `0xFE 0x80`–`0xFE 0xFF` range is where they live (§7.1). Two
sub-decisions:

- **Numbering is fixed in the tools**, not declared in the image. The header
  carries `CAP_SUPEROPS` plus a *set version*; an executor whose version
  differs refuses the image rather than mis-executing one instruction. A
  table-in-image form would allow a device with a smaller set to load a
  richer image at the cost of a dispatch indirection on the hottest path —
  keep it as the escape hatch for the day a second target profile exists,
  not as v1.
- **The set is capped at 16** and every member must appear in a measured
  top-20 dynamic profile (§7.1 rule 4, §7.3).

Repurposing standard opcodes is rejected outright: it buys nothing, since the
reserved range is empty, and it breaks every tool that reads the stream.

### `native int` is 32-bit, everywhere — **fixed**

`.vxb` images are **not** target-width specific. `native int`, `IntPtr` and
`UIntPtr` are 32 bits on the MCU and on the host, recorded as
`CAP_NATIVE_INT_32` and checked at load.

| Consequence | Detail |
|---|---|
| `conv.i` / `conv.u` folding | unconditional (§5) — no capability gate |
| `stack_slot_t.u.nint` | `int32_t`, not `intptr_t` ([14 §6.1](14_ObjectHandler.md)) |
| `sizeof(IntPtr)`, `IntPtr.Size` | 4, on every target |
| One image runs on both | which is the point: the host build is a rehearsal for the device, not a different program ([11 §4.6](11_Memory_Model.md)) |
| **A 64-bit host pointer must never enter a managed `native int`** | natives on the host hand out an `OBJECT_SPACE_NATIVE` descriptor index instead ([14 §3.1](14_ObjectHandler.md)) |

That last row is the whole cost of the decision, and it is a cost worth
paying: a design in which managed code can hold a raw host pointer does not
port to the device anyway.

### Devirtualisation flag bit — **top bit of the method index**

The `callvirt` operand is a token in ECMA IL and an index after relocation
(§9.1 step 5). It becomes a 16-bit field:

```
 bit 15      DEVIRT — operand is a direct method index; skip the vtable
 bits 14..0  method index (or vtable slot when DEVIRT is clear)
```

| Option | Verdict |
|---|---|
| Top bit of a 16-bit index, 32,767 methods | **chosen** — no size change, no new opcode, one test in the handler |
| A separate `callvirt.direct` superinstruction | rejected — it would gate a correctness-neutral fold behind `CAP_SUPEROPS` |
| Widen the operand to 32 bits | rejected — two bytes on every virtual call site to buy a method count no MCU image will reach |

The null check is unaffected: it is performed whether or not `DEVIRT` is set,
which is the entire reason the bit exists instead of a rewrite to `call`.
`cli_pack` fails the build if a reachable image exceeds 32,767 methods.
Encoding is recorded in [12_VXB_Format.md](12_VXB_Format.md).

**Still open:**

1. **Which superinstructions** — §7.2 is a candidate list, not an approved
   one. It stays open until the host build produces a dynamic instruction
   profile; that is a deliberate refusal to guess, not an oversight.
2. **Poll insertion for hard latency bounds** — §4.4 of
   [17_Threading_Model.md](17_Threading_Model.md) records a pack-time poll
   every *N* instructions as *available*. Whether any target needs it is
   unknown.
