# Native call protocol — what `cli_pack` packs for the executor

Contract document. Answers: *should the packer pack native calls for the
executor, and how?*

**Yes.** A native call is resolved, shaped, classified and given its own
opcode on the host. At run time it costs one table index and one indirect
call, with no signature inspection, no string comparison, and no argument
copying.

This is the pack-time half of the native bridge. The C-side registration API
— `native_bcl.h`, type substitution, the Tier 1 `System.*` floor — belongs to
the planned `09_Native_BCL_Bridge.md`. The two meet at the manifest (§6) and
the ABI hash (§7).

Read with [00_Introduction.md §6](00_Introduction.md),
[10_Native_Assembly_Layout.md](10_Native_Assembly_Layout.md) and
[13_Ruffus_VM_Reuse.md §5.5](13_Ruffus_VM_Reuse.md).

---

## 1. What Ruffus does, and what it leaves on the table

Ruffus already packs the *identity* of a native call. `NativeMapper` parses
`.native` declaration files, assigns class ids from `NATIVE_CLASS_BASE = 8192`,
packs `(class_id << shift) | method_id`, and the C side dispatches through a
generated switch. No name resolution happens on the device. That part is
right and is reused.

What it does not pack is the **shape** of the call:

```c
void native_method_lang_math_abs_double(void)
{
    object_t a = stack_pop();
    double v = value_get_double(a);
    if (v < 0) v *= -1;
    stack_push_double(v);
}
```

Zero C parameters. The function reaches into the current thread's stack
through a hidden global, pops what it believes is there, and pushes what it
believes it should. Three costs follow:

1. **The contract is invisible.** Nothing checks that this function pops
   exactly one slot. A mismatch corrupts the stack of whatever runs next.
2. **The executor cannot reason about the call.** It does not know whether
   the intrinsic allocates, blocks, or throws, so it must assume the worst
   before and after every one.
3. **It cannot be tested in isolation** — there is no argument to pass.

All three are fixed by moving the shape to pack time.

---

## 2. Decision

`cli_pack` emits a `bindings[]` section. For each *distinct native method*
reachable from the entry point it records the target, the shape, and the
behavioural flags. For each *call site* it rewrites the instruction to a
dedicated opcode carrying the binding index.

```
  call 0x0A000012           ──cli_pack──▶   calln 0x0007
  (MemberRef token)                         (index into bindings[])
```

The executor's handler is:

```c
case IL_OP_CALLN: {
    const vxb_binding_t * b = &bindings[read_u16(ip)];
    stack_slot_t * argv = t->sp - b->arg_slots;
    native_thunk[b->shape](t, argv, &argv[0]);   /* return lands in argv[0] */
    t->sp = argv + b->ret_slots;
    break;
}
```

No flag test on the method record, no branch between managed and native
dispatch, no signature walk. A separate opcode is what buys that: the
distinction between managed and native is a *static* property of the call
site, so it belongs in the opcode, not in a run-time test.

Three opcodes are needed, all in the superinstruction range
([15_Pack_Time_IL_Transforms.md §7.1](15_Pack_Time_IL_Transforms.md)) but
**not** optional — they are part of the base capability set:

| Opcode | Replaces | Note |
|---|---|---|
| `calln <binding>` | `call`/`callvirt` to a native method | includes devirtualised `callvirt` |
| `newobjn <binding>` | `newobj` of a native-implemented type | allocation is the intrinsic's job |
| `callnv <slot>` | `callvirt` to a native method that is genuinely virtual | vtable slot holds a binding index with the native bit set |

---

## 3. The binding record

```c
typedef struct {
    uint16_t fn_index;      /* index into the C dispatch table               */
    uint8_t  shape;         /* thunk selector — §4                           */
    uint8_t  arg_slots;     /* evaluation-stack slots consumed (incl. this)  */
    uint32_t arg_kinds;     /* 4 bits per argument, up to 8 arguments        */
    uint8_t  ret_kind;      /* STACK_K_* of the result; STACK_K_EMPTY = void */
    uint8_t  ret_slots;     /* 0 or 1                                        */
    uint8_t  flags;         /* NAT_* — §5                                    */
    uint8_t  reserved;
} vxb_binding_t;            /* 12 bytes */
```

`arg_kinds` holds the *promoted* stack kind of each argument (§III.1.5:
`bool`/`int8`/`int16`/`char` arrive as `STACK_K_I4`, `float32` as
`STACK_K_R8`), so an intrinsic never sees a narrow type and never has to ask.

Methods with more than eight arguments fall back to shape 0 (§4) and carry
their kinds in an overflow table. Measure before optimising for them; on an
embedded API surface they barely exist.

**Twelve bytes per distinct native method**, not per call site. A 200-method
native surface is 2.4 KB in flash.

---

## 4. Shapes and thunks

A *shape* is a pre-written C function that unpacks a fixed argument pattern
from the stack window into real C parameters and packs the result back.
The packer chooses the shape; the executor indexes a table.

```c
typedef void (*native_thunk_fn)(exec_thread_t * t, stack_slot_t * argv,
                                stack_slot_t * ret);

extern const native_thunk_fn native_thunk[NAT_SHAPE_COUNT];
```

Catalogue, chosen to cover an embedded API surface:

| Shape | Signature | Example |
|---|---|---|
| `0` | generic `(t, argv, argc, ret)` — **fallback, always present** | anything |
| `1` | `void (void)` | `Gc.Collect` |
| `2` | `i4 (void)` | `Timer.Millis` |
| `3` | `void (i4)` | `Thread.Sleep` |
| `4` | `i4 (i4)` | `Math.Abs` |
| `5` | `void (i4, i4)` | `Gpio.Write` |
| `6` | `i4 (i4, i4)` | `Adc.Read` |
| `7` | `r8 (r8)` | `Math.Sqrt` |
| `8` | `r8 (r8, r8)` | `Math.Pow` |
| `9` | `void (ref)` | `Console.WriteLine(string)` |
| `10` | `i4 (ref)` | `String.get_Length` |
| `11` | `i4 (ref, i4)` | `String.get_Chars` |
| `12` | `void (ref, i4, i4)` | `Uart.Write(buf, off, len)` |
| `13` | `i4 (ref, ref)` | `String.Equals` |
| `14` | `i8 (void)` | `Timer.Ticks` |
| `15` | `ref (ref, ref)` | `String.Concat` |

**Shape 0 must always work.** It is the escape hatch for anything the
catalogue does not cover, and it is what the first implementation of every
intrinsic uses before a shape is assigned. Shapes are an optimisation layered
onto a working generic path, never a prerequisite.

The measured gain for short calls in the sketch in
[00_Introduction.md §6.8](00_Introduction.md) was around 30 %. The change
here is that the thunk is **selected by the packer from a fixed catalogue**
rather than hand-written per intrinsic, so the maintenance cost is one table,
not one thunk per method.

### 4.1 Arguments are not copied

Because a thread has one contiguous value stack with frames overlaid
([14_ObjectHandler.md §6.2](14_ObjectHandler.md)), the arguments of a native
call are already laid out contiguously at `sp - arg_slots`. The thunk gets a
pointer to that window. There is no marshalling buffer, no `memcpy`, and no
per-argument loop for the shaped path.

This also makes the GC contract trivial: the window is part of the thread's
stack, which is a root, so **arguments are rooted for the whole call with no
registration** ([11_Memory_Model.md §8.1](11_Memory_Model.md)).

A byref argument — an `out` or `ref` parameter — may point into a slot of
the calling thread's stack, so the thunk resolves it with
`stack_byref_ptr(&t->ctx, b)`, never with `objects_byref_ptr`
([14 §4.1](14_ObjectHandler.md)).

### 4.2 Return values

The result is written to `argv[0]` — the first argument slot — and `sp` is
set to `argv + ret_slots`. One convention for every shape, so the executor
needs no branch on whether the call returns a value.

---

## 5. Behaviour flags

The flags are the reason this is worth doing at all. They let the executor
skip work it would otherwise have to do defensively around every native call.

| Flag | Meaning | Executor may then |
|---|---|---|
| `NAT_LEAF` | does not allocate, block, throw, or call back into managed code | skip the GC scope, skip the safepoint poll, keep resolved pointers across the call |
| `NAT_ALLOC` | may allocate | must establish a GC scope; must re-resolve any raw pointer after the call |
| `NAT_THROW` | may raise a managed exception | must be prepared to unwind from the call site |
| `NAT_BLOCK` | may block for an unbounded time | must park the managed thread and schedule another — [17_Threading_Model.md §7](17_Threading_Model.md) |
| `NAT_CALLBACK` | may re-enter the interpreter (delegates, comparators) | must treat the call as a full safepoint |
| `NAT_PIN` | takes a reference argument straight to hardware or a syscall | must pin the argument for the call and unpin after |

`NAT_LEAF` is the common case on an embedded surface — `Gpio.Write`,
`Math.Abs`, `Timer.Millis` are all leaves — and it is where the speed comes
from: a leaf call is an indirect call and nothing else.

The flags are **declared in the manifest by the C author and copied into the
image by the packer.** They are not inferred. A wrong declaration is a bug in
the intrinsic, which is why `NAT_ALLOC` is the default when the manifest is
silent.

---

## 6. The manifest

Ruffus's `.native` files are the right mechanism with the wrong syntax — they
carry Java descriptors and no behavioural information. The successor is one
file per native assembly, and it is the **single source of truth**:
`nativegen` generates the C dispatch table, the C prototypes, and the C# ref
assembly from it ([10_Native_Assembly_Layout.md §5.2](10_Native_Assembly_Layout.md)).

```
assembly Native.Gpio abi 3

type Native.Gpio.Pin
    method void  WriteDigital(int32 pin, bool value)  fn nat_gpio_write  leaf
    method bool  ReadDigital(int32 pin)               fn nat_gpio_read   leaf
    method void  SetMode(int32 pin, int32 mode)       fn nat_gpio_mode   leaf throw

type Native.Gpio.Port
    field  int32 Index 0
    method void  WriteAll(uint8[] data, int32 off, int32 len)
                                                      fn nat_gpio_wall   leaf pin
```

Rules:

1. **Signatures are .NET types**, so the packer can match them against the
   decoded `MemberRef`/`MethodDef` signature directly, with no descriptor
   translation.
2. **Overloads are disambiguated by the full parameter list**, not by an
   ordinal. Ruffus's `abs:(D)D` / `abs:(I)I` scheme works, but ordinals in
   declaration order are a silent-breakage hazard the moment a method is
   inserted.
3. **`fn` names the C symbol.** `nativegen` emits the prototype, so a
   mismatch is a compile error rather than a run-time jump into nothing.
4. **Flags are explicit**; absence of `leaf` means `alloc`.
5. **`abi N`** is bumped by hand whenever a signature changes
   incompatibly.

---

## 7. Binding the two binaries together

The packer resolves names; the executor resolves nothing. That means the
image and the executor must be built from the same manifest, and nothing in
the format can detect a mismatch by inspection — `fn_index 7` is `fn_index 7`
whatever it points at.

**Decision: the image header carries a `native_abi_hash`.**

```
native_abi_hash = FNV-1a over, for each binding in index order:
                  assembly, type, method name, each parameter type,
                  return type, shape, flags
```

`cli_exec` computes the same hash over its registered table at startup and
**refuses to load the image if they differ.** The cost is one pass over the
binding table at startup and four bytes in the header. The alternative —
discovering the mismatch when `Gpio.Write` calls `Uart.Read` — is
unbounded.

The hash covers shapes and flags, not just names, because changing a shape or
clearing `NAT_ALLOC` is just as incompatible as renaming a method.

Additionally: `cli_pack` fails the build if a reachable method is marked
`internalcall` and has no manifest entry. An unresolved native is never
deferred to run time.

---

## 8. Worked example

```csharp
Native.Gpio.Pin.WriteDigital(13, true);
```

**IL as compiled:**

```
ldc.i4.s   13
ldc.i4.1
call       void Native.Gpio.Pin::WriteDigital(int32, bool)   // 0x0A000009
```

**Pack time:**

1. Resolve `0x0A000009` → `Native.Gpio` / `Pin` / `WriteDigital(int32, bool)`.
2. Match the manifest → `fn nat_gpio_write`, `leaf`.
3. Allocate binding index 4; record
   `{ fn_index = 12, shape = 5, arg_slots = 2, arg_kinds = I4,I4,
      ret_kind = EMPTY, ret_slots = 0, flags = NAT_LEAF }`.
   Note `bool` promoted to `I4`, per §III.1.5.
4. Rewrite the instruction to `calln 4`.

**Run time:** push two slots, read `bindings[4]`, call
`native_thunk[5](t, sp - 2, sp - 2)` which is

```c
static void thunk_v_i4_i4(exec_thread_t * t, stack_slot_t * a, stack_slot_t * r)
{
    (void)t; (void)r;
    ((void (*)(int32_t, int32_t))native_fn[/* b->fn_index */])(a[0].u.i4, a[1].u.i4);
}
```

and set `sp -= 2`. No allocation, no scope, no poll, no string, no signature.

---

## 9. Compared with Ruffus

| | Ruffus | Here |
|---|---|---|
| Name resolution | pack time | pack time — **same** |
| Declaration file | `.native`, Java descriptors | manifest, .NET signatures |
| Dispatch | generated `switch` | `bindings[]` + thunk table |
| C prototype | `void (void)`, implicit stack | explicit window + typed thunk |
| Argument transfer | pop one at a time, unboxing each | pointer to the existing stack window |
| Call-site encoding | ordinary `invoke*` + flag test | dedicated `calln` opcode |
| Behaviour known to the executor | nothing | six flags |
| Mismatch detection | none | `native_abi_hash` |

---

## 10. Open questions

1. **Shape catalogue size** *(open)* — 16 shapes is a guess. Generate the
   real distribution from the first complete `Native.*` surface and cut the
   tail to shape 0.
2. **`newobjn` allocation ownership** — does the intrinsic allocate the
   object, or does the executor allocate and pass the reference in? The
   second is simpler and keeps allocation in one place; the first is needed
   if the native type has a variable-size payload. *(open)* — decide with
   `System.String`, which is the awkward case.
3. **P/Invoke** — `ImplMap`-based calls on the host target are a different
   mechanism (dynamic symbol lookup, `FieldMarshal`) and are deliberately not
   covered here. They should reuse `vxb_binding_t` with a flag, rather than
   growing a second path. *(open)*.
4. **Where the C dispatch table lives** — one flat `native_fn[]` shared by
   all assemblies, or per-assembly tables with the assembly index in the
   binding. Flat is simpler; per-assembly allows an assembly to be omitted
   from a build without renumbering. *(open)*.
5. **`NAT_CALLBACK` re-entrancy — forbidden in v1, and here is what it is.**
   It means exactly what it sounds like: C code, called *from* managed code,
   calling *back into* managed code before it returns. The canonical cases
   are a native `Array.Sort` invoking a managed `IComparer.Compare`, a native
   `String.Format` invoking a managed `IFormattable.ToString`, and any
   intrinsic that invokes a delegate.

   Why it conflicts with the rest of the design: the intrinsic is running on
   the **C stack**, inside an opcode handler, inside the dispatch loop. To
   run managed code it would need a *nested* interpreter activation — a
   second dispatch loop above a C frame — and from that moment the thread's
   state is partly in C frames that cannot be captured. The scheduler could
   not switch away from the thread, which is precisely the rule
   [17 §2](17_Threading_Model.md) exists to protect, and the failure is
   silent: everything works until the first preemption at that depth.

   **Decision: forbidden in v1.** The flag is defined so the packer can
   *reject* a manifest that declares it, not so the executor can honour it.
   The workaround is not a workaround: anything needing a callback is written
   in managed code, where a `callvirt` is a frame push and costs nothing
   extra. `Array.Sort(T[], IComparer<T>)` belongs in the class library, not
   in C.

   If it is ever needed — delegates marshalled to a native event source are
   the realistic driver — the shape is a **trampoline, not a nested loop**:
   the intrinsic returns a "call this method, then resume me at state *k*"
   continuation record, the interpreter pushes the managed frame normally,
   and the intrinsic is re-entered with the result. The intrinsic becomes a
   small state machine and the C stack stays out of it. Revisit with
   delegates; do not let it arrive by accident.
