# Threading model

Contract document. Specifies how `cli_exec` runs more than one managed thread
and why that is cheaper here than in a conventional runtime.

Resolves the threading question deferred by
[11_Memory_Model.md §10](11_Memory_Model.md) and
[13_Ruffus_VM_Reuse.md §10](13_Ruffus_VM_Reuse.md).

Read with [14_ObjectHandler.md](14_ObjectHandler.md) (which owns thread
stacks) and [16_Native_Call_Protocol.md §5](16_Native_Call_Protocol.md)
(which classifies which natives can block).

---

## 1. The claim, stated precisely

> Threading is cheap because a managed thread's entire state lives in a VM
> record, not on the C stack. The executor performs the context switch by
> changing one pointer.

That is correct, and it is worth being exact about what it means, because the
property it depends on is fragile and easy to destroy.

| Conventional runtime | Here |
|---|---|
| Thread state is the C stack: registers, return addresses, spilled locals | Thread state is `exec_thread_t`: `ip`, `sp`, frame chain, stack region |
| Switching requires `setjmp`/`longjmp`, `ucontext`, or an RTOS task switch | Switching is `current = next` |
| Each thread needs a full C stack — kilobytes, over-provisioned | Each thread needs a slot region sized from the image |
| Preemption can land anywhere, so the GC needs stack maps | Preemption lands between opcodes, so the stack is self-describing |

Ruffus VM already works this way: `thread_t` carries `pc` and `ptr_method`,
and `thread_vm_yield()` is a handful of assignments. It has no assembly, no
`ucontext`, and no per-thread C stack.

The threading model is therefore **green threads on a single OS thread,
preemptively scheduled at safepoints**. From the managed program's point of
view it is preemptive — no `yield` call is required and a tight loop will be
interrupted. From the VM's point of view it is cooperative — the switch
happens at a point the executor chose.

---

## 2. The rule everything depends on

> **A managed call must never use the C stack.** `call`, `callvirt`,
> `newobj` and `calli` push a `stack_frame_t` and continue the dispatch loop.
> The interpreter never calls itself.

If a single opcode handler ever calls `interpret()` recursively, that
thread's state is partly in C stack frames, and switching away from it
becomes impossible without capturing a C context. The whole model collapses,
and it collapses silently — the code still works until the first preemption
at that depth.

Consequences that follow, and must be honoured:

- `ret` pops a frame and continues the loop; it does not return to C.
- `.cctor` invocation on first use pushes a frame with a "resume the
  triggering instruction" marker; it does not recurse.
- Exception unwinding walks the frame chain iteratively.
- An intrinsic may **not** call back into managed code in v1
  ([16 §10](16_Native_Call_Protocol.md) open question 5). That is the one
  place where the rule would be broken, and it is forbidden rather than
  worked around.

This rule is worth a comment at the top of the dispatch loop and an item on
every code review of it.

---

## 3. Thread record

The record is **split along the library boundary**, because half of it is
memory and half of it is .NET. ObjectHandler owns the stacks; `cli_exec` owns
the scheduling and the managed semantics, and knows nothing about the other's
internals.

```c
/* ---- stack.h, ObjectHandler: the execution context ------------------ */
typedef struct stack_thread_t {
    struct stack_thread_t * next;   /* all-threads ring, walked by gc.c     */
    uint16_t        id;

    /* execution point */
    const uint8_t * ip;
    stack_frame_t * frame;

    /* stacks — carved from the shared region, see §8 */
    stack_slot_t  * stack_base;
    stack_slot_t  * sp;
    stack_slot_t  * stack_limit;
    stack_frame_t * frame_base;
    stack_frame_t * frame_limit;
    uint8_t       * scratch_base;   /* OBJECT_SPACE_FRAME arena — 14 §6.3   */
    uint16_t        scratch_top;
} stack_thread_t;

/* ---- cli_exec: the managed thread ----------------------------------- */
typedef struct exec_thread_t {
    stack_thread_t  ctx;            /* what the collector walks             */

    uint8_t     state;              /* EXEC_T_* below                       */
    uint8_t     priority;           /* ThreadPriority, 0..4 — §6.1          */

    /* scheduling */
    uint32_t    wake_ms;            /* for EXEC_T_SLEEPING                  */
    object_id_t blocker;            /* monitor or wait target               */
    object_id_t managed;            /* the System.Threading.Thread object   */

    /* synchronisation */
    object_id_t held_monitor;
    uint16_t    held_count;
    uint8_t     interrupted;
} exec_thread_t;
```

The split is not cosmetic: [14 §1](14_ObjectHandler.md) forbids ObjectHandler
from knowing what a `System.Threading.Thread` is, and the earlier single
record broke that rule. The collector walks `stack_thread_t`; the scheduler
walks `exec_thread_t`; `&t->ctx` converts between them for free.

States, taken from Ruffus's `run_mode` bitmask but made exclusive, because a
bitmask that can express "sleeping and locked" invites bugs:

| State | Runnable | Left by |
|---|---|---|
| `EXEC_T_READY` | yes | scheduled |
| `EXEC_T_RUNNING` | — | preempted, blocked, or ended |
| `EXEC_T_SLEEPING` | no | `wake_ms` reached |
| `EXEC_T_MONITOR` | no | owner releases `blocker` |
| `EXEC_T_WAITING` | no | `Pulse`/`PulseAll` on `blocker`, or timeout |
| `EXEC_T_NATIVE` | no | a `NAT_BLOCK` intrinsic completes — §7 |
| `EXEC_T_DEAD` | no | — |

---

## 4. Preemption

### 4.1 What Ruffus does, and why not to copy it

`vm_process()` executes exactly one instruction per call and, before each
one, evaluates `thread_is_expired()`, which calls `ms_timer_get_ms()` and
compares against a per-thread deadline. A timer read and a comparison on
**every instruction** is a large fraction of the cost of the cheap
instructions, which are most of them.

### 4.2 Decision — a polled flag at bounded-latency points

Two changes.

**A single flag, set from outside.**

```c
extern volatile uint8_t exec_preempt;   /* set by the tick source */
```

The platform millisecond tick — an ISR on hardware, a timer or a counter on
the host — sets `exec_preempt` when the current thread's slice expires. The
interpreter never reads the clock.

**Polled only where a loop must pass.**

```c
#define EXEC_POLL(t)  do { if (UNLIKELY(exec_preempt)) sched_at_safepoint(t); } while (0)
```

`EXEC_POLL` is placed at exactly three kinds of instruction:

| Site | Why |
|---|---|
| backward branches (`br*` with a negative displacement) | every loop contains one |
| `ret` | every recursion contains one |
| `call`/`callvirt`/`newobj`/`calln` with `NAT_ALLOC` or heavier | bounds latency in call-heavy code and coincides with an existing slow path |

Any IL sequence that runs unboundedly must contain a backward branch or a
call, so latency is bounded, while straight-line code pays nothing. This is
the standard safepoint-poll placement and it is strictly better than a
per-instruction check.

`switch` counts as a backward branch when any of its targets is backward.
`endfinally` and `leave` count as `ret`.

### 4.3 Slice length

**20 ms, configurable.** It is a scheduling decision, not an architectural
one: the value lives in the image header as `slice_ms`, and the board
manifest may override it. 20 ms is Ruffus's figure and is the right starting
point — long enough that a switch costs nothing measurable, short enough that
a 50 Hz interaction feels immediate.

Nothing in the design depends on the number. A thread that yields, sleeps or
blocks before its slice expires is the common case anyway.

### 4.4 Latency

A poll is missed only for the duration of one straight-line run. The
pathological case is a long `switch` arm or a huge unrolled block, which is
bounded by method size. If a hard bound is ever needed, add a poll every *N*
instructions in the packer's output — a pack-time decision, not a run-time
cost. Record that as available rather than implementing it.

### 4.5 `sched_at_safepoint`

```c
static void sched_at_safepoint(exec_thread_t * t)
{
    exec_preempt = 0;
    t->ctx.ip = ip;  t->ctx.sp = sp;      /* publish the cached registers */
    if (gc_requested) gc_collect();       /* §5 */
    wake_sleepers(now_ms());
    if (t->state == EXEC_T_RUNNING) t->state = EXEC_T_READY;
    current = sched_pick();
    /* reload ip, sp from current */
}
```

The only real cost is publishing and reloading the interpreter's cached `ip`
and `sp`, which is two stores and two loads, and only on the tick.

---

## 5. Garbage collection becomes easier, not harder

This is the part worth emphasising, because the usual expectation is the
opposite.

[11_Memory_Model.md §7.3](11_Memory_Model.md) requires that "with threads,
every thread must reach a safepoint before marking begins". With green
threads that requirement is satisfied **by construction**:

- Only one managed thread is executing at any instant.
- Every other thread is parked at a safepoint — that is the only place a
  thread stops.
- The collector runs from `sched_at_safepoint`, so the running thread is at
  one too.

There is no stop-the-world protocol, no thread suspension, no signal, no
spinning on a poll page. Marking simply walks the thread ring:

```c
for (stack_thread_t * t = ring; t; t = t->next) {
    scan_slots(t->stack_base, t->sp);        /* self-describing by kind */
    scan_scratch(t);                         /* OBJECT_SPACE_FRAME arena */
}
```

[14_ObjectHandler.md §7.4](14_ObjectHandler.md) already lists roots this way,
and it walks `stack_thread_t` — the half of the record that contains no .NET
at all (§3). The only obligation this document adds is that a thread's `sp`
must be published before it parks — which §4.5 does unconditionally.

**Corollary, recorded for the day it matters:** all of this holds because
there is exactly one OS thread. Real parallelism would reintroduce
suspension, atomics, a write barrier with teeth, and `volatile.`
([15 §6](15_Pack_Time_IL_Transforms.md)). The image records
`CAP_SINGLE_OS_THREAD` so an image packed under these assumptions cannot be
loaded by an executor that broke them.

---

## 6. Scheduling policy and synchronisation

Because only one thread runs at a time and switches occur only at safepoints,
every synchronisation primitive is a plain data structure. No atomics, no
memory barriers, no lock-free algorithms.

### 6.1 Priorities follow the managed thread

.NET puts priority on the thread object — `Thread.Priority` of type
`ThreadPriority`, five levels: `Lowest`, `BelowNormal`, `Normal`,
`AboveNormal`, `Highest`. There is no reason to invent anything: the
property setter is a native that writes `exec_thread_t.priority`, and the
thread record is the one place the value lives.

**Scheduler: highest ready priority wins; round-robin within a level.**
`sched_pick()` is a scan of the ring for the maximum `priority` among
`EXEC_T_READY` threads, resuming after the last one picked at that level so
ties rotate. On a ring of a handful of threads that is a few comparisons at
the tick, and nothing at all between ticks.

Two consequences, both matching .NET rather than fighting it:

- **A busy high-priority thread starves the ones below it.** That is what
  strict priority means, it is what .NET does on a single core, and the
  remedy is the program's: sleep, wait, or lower the priority. The VM does
  not age priorities.
- **Priority is a hint about intent, not a real-time guarantee.** Preemption
  still only happens at safepoints (§4.2), so a high-priority thread waits
  for the running one to reach one.

Default is `Normal`. A program that never touches the property gets exactly
Ruffus's plain round-robin, which is the behaviour that was already known to
be sufficient.

### 6.2 Monitors

`lock`, `Monitor.Enter/Exit` — an object needs an owner, a recursion count,
and a wait queue. Storing three fields in every object header is unacceptable
on a 64 KB heap, so use a **side table**, allocated only for objects that are
actually locked, keyed by object id and searched linearly:

```c
typedef struct {
    object_id_t     obj;
    exec_thread_t * owner;
    uint16_t        count;      /* recursion depth */
    uint16_t        queue;      /* head of the blocked list */
} monitor_entry_t;

static monitor_entry_t monitors[MONITOR_TABLE_SIZE];   /* fixed */
```

**Decision: the table is a fixed array, sized from the image header, and
exhaustion throws.** It is never grown from the heap. Three reasons, in
order:

1. A device must not have a failure mode that depends on heap fragmentation
   at the moment a `lock` is entered.
2. The number is knowable at pack time: `cli_pack` counts distinct `lock`
   sites and `Monitor.Enter` call sites in the reachable closure, and emits
   that count plus headroom. Real programs lock a handful of objects.
3. Entries are freed on the final `Monitor.Exit`, so the table sizes to
   *concurrently held* monitors, not to lock sites over time — always the
   smaller number.

The cost is real and is accepted: a program that exceeds the cap is legal
.NET and fails here, with `OutOfMemoryException` and a diagnostic naming the
limit. That is the same trade as every other fixed region in this design
([11 §4.5](11_Memory_Model.md)), and the alternative — a program that runs
until the heap happens to be fragmented — is worse on a device.

Ruffus stores `sync_object`, `wait_object` and `req_object` on the thread
rather than the object, which is the same trade made from the other side and
also works.

`Monitor.Enter` on a contended monitor sets `EXEC_T_MONITOR` and yields;
`Monitor.Exit` moves the head of the queue to `EXEC_T_READY`. `Wait`/`Pulse`
follow directly.

### 6.3 The rest

**`Interlocked.*`** — implementable as ordinary reads and writes inside a
`NAT_LEAF` intrinsic, because a leaf native cannot be preempted (§4.2 places
no poll inside one). This is one of the clean wins of the model: the atomics
are free and obviously correct.

**`volatile.`** — a no-op, discarded by the packer
([15 §6](15_Pack_Time_IL_Transforms.md)).

---

## 7. Natives that block

The one place the model strains. A native that blocks — a UART read, a file
read, a socket accept — blocks the *OS* thread, and therefore every managed
thread.

`NAT_BLOCK` ([16 §5](16_Native_Call_Protocol.md)) marks these. Three
permitted implementations, in order of preference:

1. **Split into start + poll.** The intrinsic starts the operation and
   returns immediately; the thread enters `EXEC_T_NATIVE` with a completion
   predicate the scheduler evaluates. This is the natural shape for DMA and
   interrupt-driven peripherals and is the recommended form on hardware.
2. **Non-blocking with retry.** The intrinsic attempts the operation; on
   `EWOULDBLOCK` the thread yields and the instruction is retried. Requires
   the instruction to be restartable, so the intrinsic must not have consumed
   its arguments — which the fixed stack window makes easy, since `sp` is not
   adjusted until the call returns.
3. **Genuinely blocking, declared.** Permitted only when the platform layer
   can guarantee the wait is short, or when there is exactly one runnable
   thread. `cli_exec` logs it in debug builds.

**An intrinsic must never block while holding a pin** — a pin prevents
compaction, and a long pin fragments the heap in the way the design exists to
avoid ([11_Memory_Model.md §8.4](11_Memory_Model.md)).

---

## 8. Memory

Thread stacks come out of the single `stack_mem` region ObjectHandler is
given ([14 §9](14_ObjectHandler.md)). Each thread gets three contiguous
areas:

```
 [ stack_slot_t value stack ][ stack_frame_t control stack ][ scratch arena ]
```

Sizing:

| Quantity | Source |
|---|---|
| default per-thread slot count | image header, computed by `cli_pack` from the deepest reachable call chain × (`MaxStack` + locals + args) |
| default frame count | image header |
| scratch bytes | image header, computed per method — [14 §6.4](14_ObjectHandler.md) |
| maximum thread count | image header or board manifest |
| slice length | image header, default 20 ms — §4.3 |
| monitor table size | image header, from the reachable `lock` sites — §6.2 |

On an MCU the region is one static array, partitioned at startup; threads
beyond the configured maximum throw at `Thread.Start`. There is no growth and
no guard page: overflow is detected by an explicit check on push and raises
`StackOverflowException` in the offending thread only, which the model makes
survivable — the other threads are untouched.

**Recursion depth is now a visible, budgeted resource** rather than a
property of the host's C stack. That is a real improvement over any runtime
that uses the C stack for managed calls, and it is worth stating in the
capability matrix.

---

## 9. Interrupts and peripheral callbacks

An ISR runs outside the model entirely. It may not allocate, may not touch
the heap, and may not call managed code
([11_Memory_Model.md §7.3](11_Memory_Model.md)).

What it may do:

- set `exec_preempt`;
- push a small record onto a fixed-size, single-producer queue;
- set a completion flag an `EXEC_T_NATIVE` thread is waiting on.

Managed-side event delivery is then a dedicated managed thread that drains
the queue and invokes handlers — normal managed code on a normal thread, at
normal safepoints. This is how the timer and peripheral callbacks in
[10_Native_Assembly_Layout.md §4](10_Native_Assembly_Layout.md) are
delivered, and it keeps the ISR path free of every rule above.

**This is also the whole of the periodic-task story.** Ruffus has a
`THREAD_MODE_TIMERTASK` thread that restarts itself on completion; there is
no .NET equivalent and none is added (§11). A periodic job is a managed
thread with a loop and a `Thread.Sleep`, or a `System.Threading.Timer`-shaped
native that posts to this queue. Inventing a VM-level thread mode would put a
construct in the runtime that no C# program can name.

---

## 10. Slice mapping

Threading is **not** part of the [§4.1 definition of done](00_Introduction.md).
It is a post-milestone slice. What must be true *before* then, so that adding
it is not a rewrite:

| Must hold from | Requirement |
|---|---|
| Slice A | the dispatch loop never recurses into C for a managed call (§2) |
| Slice A | interpreter state is read from and written to a thread record, even when there is one thread |
| Slice A | `EXEC_POLL` exists at the three sites, with an empty body |
| Slice C | the collector enumerates roots by walking a thread ring of length one |
| Slice C | intrinsics carry `NAT_*` flags, even though nothing acts on `NAT_BLOCK` yet |

Each of these costs nothing in a single-threaded build and each is expensive
to retrofit. That is the whole reason they are written down now.

---

## 11. Decisions and what remains open

**Resolved in this revision:**

| Was open | Now |
|---|---|
| Priorities | **Follow the managed thread.** `Thread.Priority`'s five `ThreadPriority` levels land in `exec_thread_t.priority`; the scheduler is highest-ready-first with round-robin within a level, default `Normal` — §6.1 |
| Slice length | **20 ms, configurable** through `slice_ms` in the image header — §4.3 |
| Monitor table sizing | **Fixed array**, sized by `cli_pack` from the reachable `lock` sites, never grown from the heap, `OutOfMemoryException` on exhaustion — §6.2 |
| Timer-task threads | **Dropped.** Ruffus's `THREAD_MODE_TIMERTASK` has no .NET equivalent and is not added; periodic work is a managed loop with `Thread.Sleep`, or the ISR queue plus a dispatcher thread — §9 |

**Still open:**

1. **`Thread.Abort` / interruption semantics** — Ruffus has
   `THREAD_MODE_INTERRUPT`. .NET deprecated `Abort`; `Interrupt` is enough
   and is simple here (set a flag, wake the thread, throw at its next
   safepoint). Confirm the exception model with
   [05_Exception_Handling.md](00_Introduction.md) when it is written.
2. **Thread-local storage** — `[ThreadStatic]` and `ThreadLocal<T>` need a
   per-thread slice of the statics slab rather than one shared slab
   ([11 §3.1](11_Memory_Model.md)). Cheap to add, but it changes a root
   enumeration, so decide before the collector is written if it is wanted at
   all.
