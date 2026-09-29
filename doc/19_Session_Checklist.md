# Session checklist — where the work stands and what comes next

Living document. **Read it at the start of every session, and update it at
the end.** It records where the work stands, the rules the work follows, and
the ordered list of what remains, so that any session can pick up where the
last one stopped without re-deriving the context from the whole document set.

It does not replace the contracts. [14_ObjectHandler.md](14_ObjectHandler.md)
says what ObjectHandler *is*; [18_ObjectHandler_Build_Plan.md](18_ObjectHandler_Build_Plan.md)
says in what order it is built and what proves each step. This document says
*where we are* on that path and *what to pick up next*. When it disagrees with
14 or 18, they win and this document is corrected.

State recorded here: **2026-09-29**, at commit `602cb7c`.

---

## 1. The project in one page

**Ruffus CLI** runs .NET programs — CIL, interpreted, no JIT — on
microcontrollers, with a host build that behaves identically. It succeeds
**Ruffus VM** (`ruffusvm/`, a 2016 embedded Java VM in C): it keeps that
project's architecture and rewrites its details
([13_Ruffus_VM_Reuse.md](13_Ruffus_VM_Reuse.md)).

| Piece | Role | State |
|---|---|---|
| `cli_pack` | host tool: reads .NET assemblies, resolves and folds everything it can, writes one `.vxb` image | parser front end done — `source/`, [Phase 0](99_phase0_work.md); the rest not started |
| `cli_exec` | runs a `.vxb` image, on the device or the host | not started |
| `cli_dump` | prints an image, for inspection and tests | not started |
| **ObjectHandler** | standalone C library for objects, the heap, per-thread stacks and the collector; knows nothing about CIL | contract [14](14_ObjectHandler.md), plan [18](18_ObjectHandler_Build_Plan.md); **OH-1, OH-2, OH-4, OH-5 and D7 built and tested**, all state in the application's instance, one stack per managed thread; OH-3 deferred by the owner |

**Current focus: ObjectHandler v1**, built in the order of doc 18 and tested
until it is the component nobody has to suspect. After it come the image
format (`12_VXB_Format.md`, not yet written) and the `cli_pack` / `cli_exec`
slices of [00 §4](00_Introduction.md).

**Where the code is.** `source/` holds the parser, which
[CMakeLists.txt](../CMakeLists.txt) builds into `cli_parser`. The new
ObjectHandler is written in `source/object_handler/`, one doc 18 slice at a
time (A1). The Ruffus copy that `126dfe2` put in `ObjectHandler/` is deleted;
the originals are in `ruffusvm/ObjectHandler/`, and this repository's changes
to them are in history up to `476cd6e`.

---

## 2. Parameters of the work

### 2.1 Process

1. **Contract before code, record after** ([00 §7](00_Introduction.md)). A
   design decision is written into the contract before code depends on it.
2. **An item marked *(open)* is never guessed at in code.** It is raised,
   decided and written down first.
3. **Decisions made while coding are folded back** into
   [14 §11](14_ObjectHandler.md) for the contract, or
   [18 §12](18_ObjectHandler_Build_Plan.md) for the plan — the definition of
   done in 18 §11.
4. **One logical change per commit.** A file rename is committed on its own,
   before its content changes, so that git keeps the file's history.
5. **The owner confirms** deletions, choices between alternatives the docs
   leave open, and anything hard to undo. A session proposes; it does not
   decide those alone.

### 2.2 Fixed technical parameters

Decided. Changing one means changing the contract first.

| Parameter | Value | Where |
|---|---|---|
| Object reference | `object_id_t`, 32 bits: 16-bit space tag and 16-bit index; a debug build uses 8-bit space, 8-bit generation, 16-bit index | 14 §2.1 |
| Heap ids | handle-table indices, never payload offsets | 14 §3.2, 18 D2 |
| Heap profile (v1) | one collectable segment; `HEAP_MAX_PAYLOAD` 65536; `handle_count = payload_size / 32` | 14 §5.5, §9.1, §9.2 |
| API widths | every byte and element count is `uint32_t`; type ids and `elem_size` stay 16-bit | 14 §5.2, §8 |
| Configuration | one configuration for MCU and host; only debug versus release differs | 18 §0.2, 11 §4.6 |
| Threads | one application — one CLI binary, one image — on one OS thread; its managed threads are green threads the VM schedules, preempted at safepoints; 20 ms slice; several applications at once is not a use case | 17 §1 |
| Instances | the application's `obj_handler_t`, taken first by every call and passed to every callback; no mutable globals | 14 §1, §8 |
| `native int` | 32 bits on every target | 15 §11 |
| Strings | UTF-8 payloads; `cli_pack` rejects non-ASCII literals unless `--allow-non-ascii` | 14 §5.6 |
| Arrays | one-dimensional, inline elements, fixed length; the type's refmap describes one element | 14 §5.7 |
| Stack | typed slots and overlapped frames in v1; frame scratch and the size-class recycler deferred | 14 §6.0 |
| Managed threads | one stack each, the one state a managed thread has for itself; its record is passed to every call and the VM supplies its arrays; overflow checked once per call | 14 §6.2 |
| Collector | tricolour mark-sweep, precise, non-moving in v1; a 75 % threshold sets a flag, and a failed allocation collects at once | 14 §7 |
| Init failures | `obj_handler_status_t`, from `obj_handler_init` only; after init, embedder bugs go to `on_corrupt`, which always returns | 14 §9.2 |
| Allocation failure | `OBJECT_NULL` only for a request that can never fit; otherwise collect, retry once, `on_no_memory` | 14 §5.2 |
| Pins | a count, 0–7, in the handle's flags | 14 §3.2 |
| Compiler baseline | C99, also built as C11; `-Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes` | 14 §1.1 |

### 2.3 Hard rules

- The interpreter never recurses into the C stack for a managed call
  ([17 §2](17_Threading_Model.md)).
- Every reference store goes through `objects_write_ref`
  ([14 §6.0](14_ObjectHandler.md)).
- No raw pointer survives a safepoint: resolve once per opcode
  ([14 §2.1.1](14_ObjectHandler.md), [11 §4.3](11_Memory_Model.md)).
- ObjectHandler contains no CIL, ECMA-335 or `.vxb` knowledge beyond the
  primitive values a stack slot holds (14 §6.7), calls neither `malloc` nor
  `printf`, and its headers compile against `<stdint.h>` and `<string.h>`
  only ([14 §1](14_ObjectHandler.md)).
- `on_no_memory` never returns ([18 D1](18_ObjectHandler_Build_Plan.md)).
- ObjectHandler keeps no mutable globals: its state is the `obj_handler_t`
  every call takes ([14 §1](14_ObjectHandler.md)).
- Nothing is copied from `ruffusvm/RuffusVM/RuffusVMTool/`, which is
  GPL/NanoVM code ([13](13_Ruffus_VM_Reuse.md)).

### 2.4 Naming and conventions

- **A name carries the module that owns it:** `objects_`/`OBJECT_`,
  `heap_`/`HEAP_`, `stack_`/`STACK_`, `obj_string_`/`OBJ_STRING_`,
  `value_`/`VALUE_`, `array_`/`ARRAY_`, `gc_`/`GC_`,
  `obj_handler_`/`OBJ_HANDLER_`. There is no library-wide prefix; `oh_` and
  `vee_` are retired ([14 §1.2](14_ObjectHandler.md)).
- **No file named like a system header, and no identifier C reserves.**
  `strings.h` shadowed `<strings.h>`, and `strings_*` began with `str` and a
  lowercase letter ([18 D3](18_ObjectHandler_Build_Plan.md)). Leading
  underscores at file scope are reserved as well.
- **Docs:** British spelling, decision-dense, tables, cross-references written
  as "14 §5.7".
- **Code comments** state only what the code cannot show, in one short line.
- **Line endings:** LF in `source/object_handler/` and `tests/object_handler/`
  (A5); elsewhere kept per file.

### 2.5 Environment

| Item | Value |
|---|---|
| Repository | `/home/wifi/projects/cli_parser`; `/home/wifi/projects/ruffusvm` is reference only |
| Host compiler | gcc 13, Linux |
| Build | CMake 3.28.3; `cmake --build build` builds `cli_parser`, ObjectHandler and its tests; the three test trees are in §5.3 |
| Device compiler | `/home/wifi/gcc_toolchain/bin/arm-none-eabi-gcc` 14.2.1 with newlib, e.g. `-mcpu=cortex-m4 -mthumb` |

### 2.6 How every change is verified

1. **`ctest` in three builds** (18 §0): debug with ASan and UBSan, release,
   and standalone — the library configured on its own, which proves it
   reaches nothing in `source/`. Every case runs in its own process; a case
   that means something in one build only exits 77 and is reported as
   skipped. Baseline: 129 cases pass, with zero warnings.
2. **Every library file on its own, through the ARM compiler** and host gcc,
   C99 and C11, debug and release switches. Baseline: zero diagnostics.
3. **A behaviour change is proved by a case** that fails before the change
   and passes after it.

The commands are in §5.3.

---

## 3. Where we are

### 3.1 Commits

| Commit | Change |
|---|---|
| `126dfe2` | Ruffus VM ObjectHandler copied into `ObjectHandler/` |
| `5042cea` | documentation updated |
| `a18881e` | `strings.c/.h` renamed to `obj_string.c/.h` |
| `2bb3152` | prefix `strings_` → `obj_string_`; docs synced |
| `cf433a6` | the heap allocator takes `uint32_t` and rejects oversized requests |
| `e444cb9` | `obj_handler_status_t` (D1); 14 §9.2 |
| `476cd6e` | arrays: contract in 14 §5.7; `arrays_type` cut down and fixed |
| `ddd6dde` | this checklist |
| `360c37a` | A1: the new library goes in `source/object_handler/`; docs 13, 14, 18, 19 synced |
| `0cf74cf` | `obj_handler.h` moved to `source/object_handler/` |
| `358f7c2` | `obj_handler.h`: Ruffus includes dropped |
| `5d63f74` | checklist: session record |
| `b3dd1bf` | the rest of the Ruffus copy deleted; docs 18, 19 synced |
| `2fdecdf` | checklist: the deletion recorded |
| `b999151` | A2–A13 decided; docs 11, 14, 17, 18, 19 |
| `745922d` | D4: the `source/framework/` objects and stack draft deleted |
| `7ff3a8f` | OH-1: ids, config, spaces, resolution, init; harness, fake embedder, CMake/CTest |
| `1fe5476` | OH-2: allocator, free list, `heap_release`, `heap_verify`, stats; `objects_create`/`clone`/`pin` |
| `dcded70` | OH-1/OH-2 decisions folded into docs 14 and 18; checklist |
| `a1faa16` | D9 decided; OH-5 moved before OH-3 and OH-4 |
| `92ff6ea` | OH-5: arrays, UTF-8 strings, boxing; interning and `value_compare` wait |
| `c6600df` | OH-5 decisions folded into docs 14 and 18; checklist |
| `6d72cea` | D7 decided: the statics slab and the constant table; B3 settled, A6 superseded |
| `e06439e` | D7 built: both spaces configured at init, constants readable everywhere, `obj_string_intern` |
| `1c56a11` | D7 coding decisions folded into docs 14 and 18; checklist |
| `ba2afed` | checklist: OH-4 deferred; the several-applications question recorded |
| `bed86cf` | instances decided (I1–I6): one `obj_handler_t` per application; docs 11, 14, 17, 18 |
| `d99a6ec` | instances built: every global moved into `obj_handler_t`; `test_instances.c` |
| `4ec7b8d` | checklist: the instances session |
| `3f33e07` | application model aligned: one CLI binary, one image, one OS thread; docs 11, 14, 17, 18 |
| `ce58272` | comments and test names follow the one-application model |
| `b81ec73` | checklist: the application-model alignment |
| `d682790` | the out-of-memory callback `on_oom` renamed `on_no_memory`; tests and docs follow |
| `f16aa94` | checklist: the `on_no_memory` rename |
| `c03c1f6` | doc 18: interning came with D7, so two OH-5 pieces wait, not three |
| `928c029` | B6 decided: the library normalises, compares and converts slot values; docs 14, 18, 19 |
| `6127ce6` | `value_compare` and `value_convert` built; `stack_slot_t` in `objects_types.h`; `float-cast-overflow` in the sanitized build |
| `1840093` | value normalisation: coding decisions folded into docs 14 and 18; checklist and B7 |
| `a2e2761` | OH-4 decided: one stack per managed thread, passed to every call; `stack_mem` leaves the config; docs 14, 17, 18, 19; B8 and the RPL plan |
| `602cb7c` | OH-4 built: `stack.c/.h`, the scanner registered with `gc.c`, `stack_verify`; `test_stack.c` |

### 3.2 Code, file by file

`source/object_handler/` — the library; C99, LF:

| File | Holds | Slice |
|---|---|---|
| `obj_handler_config.h` | the one configuration: space count, segments, `HEAP_MAX_PAYLOAD`, string encoding; the five debug switches, on unless `NDEBUG` | OH-1, OH-4 |
| `objects_types.h` | `object_id_t` and its macros in both splits; spaces, `object_handle_t`, flags with the pin count, kinds, byref, `array_header_t`, the stack slot `stack_slot_t` and its `STACK_K_*` kinds, the constant entry `object_const_t`; the collector's visitor and scan types; the instance `obj_handler_t`, with the host callbacks, heap segments, thread ring and registered scan it holds; `OBJECT_LIKELY`; layout asserts | OH-1, D7, I1, B6, OH-4 |
| `objects.c/.h` | the space table, `objects_ptr` (checked in debug) and the constant resolver, `objects_info`, registration of `NATIVE`, accessors, `objects_create`/`clone`/`promote`/`pin`/`unpin`, the identity barrier | OH-1, OH-2, D7 |
| `obj_handler.c/.h` | the umbrella: status codes, the config struct, `obj_handler_init` on the caller's instance with its ten checks, `obj_handler_shutdown`, which zeroes it | OH-1, D7, I1 |
| `heap.c/.h` | first fit with coalescing, bump top, handle free list with quarantine, generations, guard headers and poisoning in debug, `heap_verify`, `heap_stats` | OH-2 |
| `gc.c/.h` | stub: `gc_request` and `gc_collect` do nothing until OH-3; `gc_register_root_scan` keeps the stack's scanner (A7) | OH-2, OH-4 |
| `stack.c/.h` | `stack_thread_t` and `stack_frame_t`; `stack_thread_init` on the VM's record and arrays, `stack_thread_end`; inline push, pop and peek per slot kind and whole slot, `dup`, `drop`, `depth`, `room`, checked under `STACK_DEBUG`; `stack_frame_push`/`pop` with overlapped arguments and one overflow check per call; `stack_scan_roots`; `stack_verify` | OH-4 |
| `array.c/.h` | `array_create`, length, the one range check, element pointer and byref, overlap-safe copy through the barrier, clear | OH-5 |
| `obj_string.c/.h` | strict UTF-8 validation, create, concat, `Length` and `char_at` in UTF-16 units, `OBJECT_H_ASCII`; `obj_string_intern` returns a constant's id | OH-5, D7 |
| `value.c/.h` | `value_box`, `value_unbox`; `value_compare` over every pair of slot kinds and `value_convert` to twelve primitive types, checked or not (14 §6.7) | OH-5, B6 |
| `CMakeLists.txt` | target `object_handler`; configured on its own, it is the standalone build and adds the tests | OH-1 |

`tests/object_handler/`: `harness.c/.h` (L1), `fake_embedder.c/.h` (L2),
`test_objects.c` (25 cases), `test_heap.c` (29, among them two seeded 64 KB
tortures and deliberate-damage cases that `heap_verify` must catch),
`test_array_string.c` (21, among them a 22-row UTF-8 table),
`test_value.c` (16, among them the kind-pair matrix and a 108-row conversion
table), `test_spaces.c` (16: statics, constants, and one
case per malformed slab or table entry), `test_instances.c` (5: two
instances side by side, among them a seeded interleaving in which one runs
out of memory beside the other) and `test_stack.c` (17: frames, recursion to
both limits, a look-alike `int32` the scanner must skip, two threads
switched by passing the record, and damage `stack_verify` must catch).

Release build for Cortex-M4 at `-Os`: 9,749 bytes of `.text` and no `.data`
or `.bss`; one instance is 452 bytes, and the embedder allocates it, as the
VM does each thread's 40-byte record and its arrays.
`value.c` calls libgcc's soft-float helpers, which the interpreter needs for
CIL's floating point anyway.
Nothing carried over from the Ruffus copy except `obj_handler.h`'s status
enum; what each slice takes from Ruffus, as design, is in 13 §§4 and 6.

### 3.3 Documents

| Doc | State |
|---|---|
| [00](00_Introduction.md) | project overview and roadmap; current |
| [10](10_Native_Assembly_Layout.md) | `Native.*` assemblies; §§4–8 deferred |
| [11](11_Memory_Model.md) | memory policy; **contradicts 14 in seven places** — B1 |
| `12_VXB_Format.md` | **not written**; the next contract after ObjectHandler v1 |
| [13](13_Ruffus_VM_Reuse.md) | Ruffus VM heritage; current |
| [14](14_ObjectHandler.md) | ObjectHandler contract, normative; one false claim in §1 — B2 |
| [15](15_Pack_Time_IL_Transforms.md) | pack-time IL transforms; its `conv.i`/`conv.u` fold is too broad — B7 |
| [16](16_Native_Call_Protocol.md) | native call protocol; four items open |
| [17](17_Threading_Model.md) | threading model; one item open |
| [18](18_ObjectHandler_Build_Plan.md) | build plan; D1–D4, OH-1 and OH-2 struck through |
| 19 | this checklist |
| [99](99_phase0_work.md) | Phase 0 record; history |

### 3.4 Build plan progress

| Doc 18 item | State |
|---|---|
| D1 error and failure model | **done** — `obj_handler_status_t`, 14 §9.2 |
| D2 debug object identity | **done** — `objects_types.h`, `7ff3a8f`; its stale-id test is in OH-2 |
| D3 header isolation and naming | **done** — the standalone build passes, `7ff3a8f` |
| D4 old `source/framework/` draft | **done** — deleted, `745922d` |
| OH-T | L1, L2 and L7 done; L3, L5 and L6 in part (§4 D) |
| OH-1 | **done** — `7ff3a8f` |
| OH-2 | **done** — `1fe5476` |
| OH-5 | **built** — `92ff6ea`, interning `e06439e`, `value_compare` and `value_convert` `6127ce6`; the tracing test (OH-3) waits |
| OH-4 | **built** — decided `a2e2761`, built `602cb7c`; the collect-and-keep halves of two tests (OH-3) wait |
| Instances (A14) | **done** — decided `bed86cf`, built `d99a6ec`, model aligned `3f33e07` |
| OH-3, OH-6 | not started |

---

## 4. Checklist

In order: **A** before any OH-1 code, then **B** and **C**, then the slices
in **D**. Each item says what to decide or do, what is recommended, and where
the answer is written down.

### A. Decide first

These either land in the first commit's headers or block OH-1.

- [x] **A1 — Strategy and code location.** *Decided 2026-09-28:* new files
  in `source/object_handler/` — snake_case, not `objecthandler`. Only
  `obj_handler.h` carried over; the rest of the Ruffus copy is deleted
  (§3.2). Tests go in `tests/object_handler/`; the CMake target is
  `object_handler`. *Recorded in:* 18 D3 and §12, 14 §1.1, 13 §4.
- [x] **A2 — Concurrency contract.** State that ObjectHandler is not
  re-entrant and is called from one thread only; doc 17 guarantees one OS
  thread, but nothing tells a third-party embedder. *Recorded in:* 14 §1.
- [x] **A3 — Scan rule for the remaining kinds.** How the collector treats
  `OBJECT_K_STRING`, `OBJECT_K_BOX` and `OBJECT_K_RAW`; objects and arrays
  are done (14 §7.5, §5.7). *Decided:* strings and raw blocks never scanned,
  a box scanned with its value type's refmap. *Recorded in:* 14 §7.5.
- [x] **A4 — Pin depth.** One `OBJECT_H_PINNED` bit cannot express two
  overlapping pins: the first unpin clears a pin still held. *Decided:* a
  count, 0–7, in the three spare flag bits; past 7 or below 0 goes to
  `on_corrupt`. *Recorded in:* 14 §3.2.
- [x] **A5 — Compiler baseline.** *Decided:* C99, also built as C11; the
  14 §1.1 flags plus `-Wshadow -Wstrict-prototypes -Wmissing-prototypes`;
  `OBJECT_LIKELY`/`OBJECT_UNLIKELY`; `extern "C"` in headers; LF line
  endings, `obj_handler.h` converted. *Recorded in:* 14 §1.1.
- [x] **A6 — Reserve the literal-table fields.** The interned-literal table
  is a root (14 §7.4) with no field in `obj_handler_config_t` (14 §9).
  *Decided:* `object_id_t * literals` and `uint32_t literal_count`.
  *Recorded in:* 14 §9. *Superseded by D7* (14 §3.5): literals are
  constants, so the fields and the root are gone.
- [x] **A7 — Collector ↔ stack seam.** *Decided:* `stack.c` registers one
  root-scan function with `gc.c`, which never includes `stack.h`, so OH-3 is
  testable before OH-4. *Recorded in:* 14 §7.4, 18 §5.
- [x] **A8 — Who bumps a handle's generation.** *Decided:* `heap_release`,
  which the sweep calls. *Recorded in:* 18 D2, 14 §3.4.
- [x] **A9 — OH-1's stale-id test.** *Decided:* it moves to OH-2, where
  `heap_alloc` and `heap_release` exist. *Recorded in:* 18 §3, §4.
- [x] **A10 — What `heap_alloc` does when memory runs out.** *Decided:*
  `OBJECT_NULL` only for a request that can never fit; otherwise collect,
  retry once, then `on_no_memory`. *Recorded in:* 14 §5.2, 18 D1.
- [x] **A11 — Id alignment in every payload.** *Decided:* every
  `object_id_t` in any payload is 4-byte aligned, an embedder layout rule.
  *Recorded in:* 14 §7.5, 11 §9.
- [x] **A12 — `[ThreadStatic]`, yes or no.** *Decided:* no in v1; `cli_pack`
  rejects it and names the field. *Recorded in:* 17 §11, 14 §7.4.
- [x] **A13 — `on_corrupt` always returns.** 18 D1 had the library abort
  after `on_corrupt` in debug, which stops the recording fake of 18 §2.1
  from running a negative test to completion. *Decided:* a safe value is
  returned in every build; aborting is the embedder's choice inside
  `on_corrupt`. *Recorded in:* 18 D1, 14 §9.2.
- [x] **A14 — Application, threads and the instance.** *Decided:* an
  application is one CLI binary, packed into one image, and runs on one OS
  thread; its managed threads are green threads the VM schedules (17 §1).
  Running several applications at once is not a use case. All library state
  is in one `obj_handler_t` (I1), which every call takes first and every
  callback receives (I2), for the no-hidden-state reason of 14 §8; A2 holds
  for it (I3); built before OH-3 and OH-4 (I6). *Withdrawn the same day:* I4
  (ids never cross applications) and I5 (locks for peripherals applications
  share), which assumed several applications. *Recorded in:* 14 §§1, 7.5, 8,
  9 and §11; 17 §§1, 5; 11 §7.3; 18 §12.

### B. Documentation cleanup

- [ ] **B1 — Doc 11 against doc 14.** Six contradictions:
  - §4.4 declares `heap_alloc` and `stack_scratch_alloc` with `uint16_t bytes`;
    14 says `uint32_t`.
  - §6.2 says to re-size `stack_node_t`, a type from the rejected draft.
  - §4.4, §6.3, §7.2 and the Slice C row of §11 describe frame scratch and
    the size-class recycler as shipping; both are deferred (14 §6.0).
  - §7.2 says "exactly five" root sources; v1 has four.
  - §7.4's heading says "not allocation failure"; both triggers apply
    (14 §7.3).
  - §10 still lists the string representation as open; UTF-8 is decided
    (14 §5.6).
  - §6.1 checks overflow on every push against the region limit; since OH-4
    it is checked once per call, and each push only in debug (14 §6.2).
- [ ] **B2 — 14 §1** claims `heap.c` compiles without `WANT_OBJECT_HANDLER`.
  It does not (`heap_t` is defined twice; `flags` and `b_size` are missing).
- [x] **B3 — 14 §3.1 against §3.3** on what an `OBJECT_SPACE_CONST` index
  means — a literal-pool index or a byte offset. *Settled by D7:* it selects a
  constant-table entry (14 §3.5).
- [x] **B4 — 18 §12's "Nothing remains open"** stays untrue until A is
  closed; reword it to point here. *Done:* A is closed, and §12 now says
  "nothing remains open before OH-3".
- [ ] **B5 — Docs 00, 13, 15 and 17**: check them for the same stale points
  as B1 (strings open, tier 2 shipping). Doc 00 still says `ldstr` takes a
  literal-pool index; since D7 it is a constant index (14 §3.5).
- [x] **B6 — 14 §6.6 and 18 OH-5** told `value.c` to reimplement Ruffus's
  "mixed-type promotion table" for `value_compare`. Ruffus has none: its
  `value_compare` rejects operands of different types. *Decided
  2026-09-29:* the library normalises — `value_compare` orders any two
  slots, `value_convert` converts any number to any primitive type — and the
  interpreter chooses the operation and checks §III.1.5 validity; `I4 5`
  equals `NINT 5`. *Recorded in:* 14 §§1, 5.7, 6.1, 6.6, 6.7, 8 and 11;
  18 §§0.1, 6, 7 and 12.
- [ ] **B7 — 15 §5 and §11** remove `conv.i` and `conv.u` "always", because
  `native int` is 32 bits. That holds only when the stack top is an `int32`
  or a `native int`: from an `int64` they truncate, and from `F` they convert
  (14 §6.7). The condition should be the stack type, as it is for `conv.i4`
  in the same table.
- [ ] **B8 — A byref to a local or an argument** *(open)*. 11 §5 and 14 §4
  say a byref can designate a frame slot, but v1 has no id for one:
  `OBJECT_SPACE_FRAME` is tier 2's scratch arena, and a slot belongs to one
  thread's stack, which the space table cannot tell apart. Decide before
  `cli_exec` implements `ldloca` and `ldarga`. OH-4 does not wait: its
  scanner reports every byref base, and skipping those that are not heap
  ids is the collector's job (14 §7.4).

### C. Build and code chores

- [x] **C1 — CMake.** An `object_handler` static library whose include path
  is its own directory only; the standalone `-Wall -Wextra -Werror -pedantic`
  build; `enable_testing()` with one test per case; debug and release builds
  (18 D3, OH-1 item 5, OH-T). *Done* (`7ff3a8f`), with the options
  `OBJECT_HANDLER_C_STANDARD` and `OBJECT_HANDLER_SANITIZE`.
- [x] **C2 — D4.** Delete `source/framework/objects.c/.h` and
  `stack.c/.h`: CMake builds only `pool.c` and `critical_section.c` from that
  directory. *Done:* confirmed by the owner and deleted.
- [x] **C3 — Ruffus `types.h` on ARM.** Every Ruffus file fails on
  `arm-none-eabi-gcc`, because `types.h` redefines `uint32_t` and `int32_t`
  differently from newlib. *Moot:* the copy is deleted, and the new headers
  use `<stdint.h>`.
- [x] **C4 — `stack.c`** includes `vm_debug.h`, which was not copied over.
  *Moot:* the copy is deleted.
- [x] **C5 — Ruffus `heap.c` bugs.** A failed allocation walks off the end
  of the heap and reports "corrupted" instead of out-of-memory; `heap_init`
  truncates a buffer over 64 KB; blocks are 4-byte aligned but hold 8-byte
  pointers on a 64-bit host. *Moot:* the copy is deleted. The new code covers
  the same ground: OH-2's exhaustion tests and alignment check (18 §4), and
  `E_PAYLOAD_SIZE` (14 §9.2).
- [x] **C6 — `obj_string_split`** moves to the embedder as a native: under
  14 §5.7 it needs the embedder's type id for `string[]`. *Moot:* the copy is
  deleted; the rule moves to OH-5 below.
- [x] **C7 — Test harnesses.** The heap and array harnesses of 2026-09-25
  lived only in `/tmp`; OH-T recreates them in `tests/object_handler/`.
  *Done* for the heap (`1fe5476`); the array cases come with OH-5.

### D. Build plan slices — doc 18

- [x] **D1** — error and failure model: `obj_handler_status_t` (`e444cb9`).
- [x] **D2** — debug object identity, written into `objects_types.h`
  (`7ff3a8f`).
- [x] **D3** — header isolation: the standalone build passes (`7ff3a8f`).
- [x] **D4** — the old `source/framework/` draft: C2 (`745922d`).
- [ ] **OH-T** — test system (18 §2). Done: `harness.h` and CTest (L1), the
  fake embedder (L2), one process per case and a dump on failure (L7). In
  part: `heap_verify` and `stack_verify` (L3 — `gc_verify` comes with OH-3),
  fault injection for `on_no_memory` and corruption (L5), seeded tortures
  that report seed and operation (L6). Still to do: the oracle (L4, with
  OH-3), the operation log and `regressions/` (L6), and CI (L8, OH-6).
- [x] **OH-1** — foundation: types, configuration, spaces, resolution,
  `obj_handler_init` with the checks of 14 §9.2 (18 §3) — `7ff3a8f`.
- [x] **OH-2** — allocator: new `heap.c`, free list, `heap_release`,
  `heap_verify`, statistics (18 §4) — `1fe5476`.
- [ ] **OH-3** — collector (18 §5), deferred by the owner. Decide first: D5
  rooting-scope API and D6 `gc_poll` and the safepoint contract; D7 is
  decided (14 §3.5).
- [ ] **OH-4** — stack: threads, slots, frames (18 §6). Deferred by the owner
  (2026-09-28), then brought ahead of OH-3 (2026-09-29). These are the
  managed evaluation stacks and frames, not OS thread stacks: no IL runs
  without them, and they are the collector's roots. The heap is the
  application's; the stack is the one state each managed thread has for
  itself. *Decided 2026-09-29, all eight as recommended* (14 §11, 18 §12):
  the record keeps the name `stack_thread_t` and is passed to every
  per-thread call, so a thread switch passes another record and nothing is
  global; the VM supplies each thread's slot and frame arrays to
  `stack_thread_init` and ends it with `stack_thread_end`, and `stack_mem`
  leaves the config; overflow is checked once per call, and per push and pop
  in debug; Ruffus's push/pop/peek shape comes over per slot kind, with
  `dup`, `drop` and `depth`, but not `stack_scanf`, `stack_push_string` or
  `stack_dump`; no tier-2 hooks, 16-bit frame counts, `stack_frame_pop`
  takes `ret_slots`; `stack_scan_roots` is registered with `gc.c`, the ring
  headed in `obj_handler_t`. *Built* (`602cb7c`): everything in 18 §6 but
  the collect-and-keep halves of the precision and byref tests, which wait
  for OH-3; their scanner halves pass through a recording visitor.
- [ ] **OH-5** — typed constructors: `array.c`, `obj_string.c`, `value.c`
  (18 §7), built before OH-3 and OH-4 (`92ff6ea`). D9 decided (14 §5.6);
  `obj_string_intern` came with D7 (`e06439e`), and `value_compare` and
  `value_convert` with B6 (14 §6.7, `6127ce6`). Still open: the tracing test
  (OH-3). `String.Split` stays out of `obj_string.c`: it needs the
  embedder's `string[]` type id (C6).
- [ ] **OH-6** — hardening: sanitizers, coverage, 32-bit `-Os` build,
  statistics dump (18 §8).

### E. Later, not blocking

- Finalizers and weak references (14 §11, item 1).
- **An RPL layer** *(plan, not a requirement; owner, 2026-09-29)* — an
  HP 48-style Reverse Polish Lisp front end on ObjectHandler, integrating
  `value`, `obj_string` and `array` with the stack: stack words (`SWAP`,
  `OVER`, `ROT`, `PICK`, `ROLL`) as pure slot moves, and operators that
  take their operands from the stack and push the result — `value_compare`
  and `value_convert` over the top slots, `obj_string_concat` for `+` on
  strings, `GET`/`PUT` over `array_*`, lists as arrays of ids with numbers
  boxed. It is a separate module above those four, never inside them, and
  a second embedder, proving the library knows no CIL (14 §1). The stack
  suits it: an operator peeks its operands, allocates while they are still
  rooted on the stack, then pops, so it needs no rooting scope (14 §2.1.1).
  OH-4 keeps it possible — a stack can be used with no frame, and
  `stack_room` stands in for `MaxStack` — and arithmetic normalised like B6
  would be its next need.
- The tuning constants, which need host measurements (14 §11, item 2).
- Struct copies and the write barrier, decided with tier 2 (14 §11, item 3).
- Generational collection (11 §7.7).
- The four open items in doc 16 and `Thread.Interrupt` semantics (17 §11).
- `12_VXB_Format.md`, the next contract after ObjectHandler v1
  (00 §7.1): it must carry the constant table of 14 §3.5 and the statics
  fix-up list of 15 §5.1. `09_Native_BCL_Bridge.md` comes before the first
  intrinsic. Number
  formatting and parsing are natives there, written new: Ruffus's `strings.c`
  prints `0` as an empty string and `3.25` as `3`, and parses `"-5"` as 0.

---

## 5. Session protocol

### 5.1 Starting a session

1. Read §3 and §4 of this document.
2. Run `git log --oneline -10` and `git status`, and compare them with §3.1
   and the last entry in §6.
3. Take the first open item in §4 — or the one the owner names — and read the
   doc sections it cites before changing anything.

### 5.2 Finishing a session

1. Tick what is done; add every new finding to §4 under A–E, with a pointer to
   the doc section it concerns.
2. Fold decisions into 14 §11 or 18 §12 (§2.1, rule 3).
3. Update §3 where the state changed, and the "state recorded here" line at
   the top.
4. Add an entry to §6.
5. Commit the docs together with the code they describe.

### 5.3 Verification recipes

```bash
cd /home/wifi/projects/cli_parser

# 1. The three test builds (18 section 0), under build/.
cmake -S . -B build/oh-debug -DCMAKE_BUILD_TYPE=Debug -DOBJECT_HANDLER_SANITIZE=ON
cmake -S . -B build/oh-release -DCMAKE_BUILD_TYPE=Release
cmake -S source/object_handler -B build/oh-standalone
for b in debug release standalone; do
  cmake --build build/oh-$b -j && ctest --test-dir build/oh-$b -j8 --output-on-failure
done
# One case by name: build/oh-debug/tests/object_handler/test_heap torture_seed_1

# 2. Every library file on its own: host and ARM, C99 and C11, debug and release.
#    The wrapper line keeps a macro-only header from being an empty unit under -pedantic.
for cc in gcc "/home/wifi/gcc_toolchain/bin/arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb"; do
  for std in c99 c11; do
    for rel in "" -DNDEBUG; do
      for f in source/object_handler/*.[ch]; do
        printf '#include "%s"\ntypedef int not_empty;\n' "$(basename "$f")" |
          $cc -std=$std $rel -fsyntax-only -Wall -Wextra -Werror -pedantic -Wshadow \
            -Wstrict-prototypes -Wmissing-prototypes -I source/object_handler -x c - ||
          echo "FAIL: ${cc%% *} $std $rel $f"
      done
    done
  done
done

# 3. Device code size, release switches.
root=$PWD; d=$(mktemp -d)
(cd "$d" && /home/wifi/gcc_toolchain/bin/arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb \
   -std=c99 -Os -DNDEBUG -c -I "$root/source/object_handler" "$root"/source/object_handler/*.c &&
 /home/wifi/gcc_toolchain/bin/arm-none-eabi-size *.o)
rm -rf "$d"
```

---

## 6. Session log

Newest first. One entry per session: what changed, and the commits.

- **2026-09-29, third part** — The owner asked what OH-4 lacked, then set
  the model the stack follows. The application — its heap, statics and
  constants — runs on one OS thread; the stack is the one state each managed
  thread has for itself, and it must keep no globals. The deferral of
  2026-09-28 had read "thread stacks" as OS stacks. Who uses the stack: in
  Ruffus, only `gc.c` inside ObjectHandler, while `stack.h` itself called
  `value_type` and `strings` because every push boxed; outside it, the
  interpreter and about 2,500 lines of natives. In the new library only the
  collector reads stacks, and `value.c` reads single slots. Eight decisions
  accepted as recommended and recorded in docs 14, 17, 18 and 19
  (`a2e2761`). **OH-4 built** (`602cb7c`): the record passed to every call,
  the VM's arrays per thread, frames overlaid with one overflow check per
  call, Ruffus's push/pop/peek shape over typed slots, the scanner
  registered with `gc.c`, `stack_verify`. 129 cases pass in the three
  builds; every file compiles clean for host and ARM, C99 and C11; fifteen
  mutants were caught, one only after its test was strengthened. 9,749 bytes
  for Cortex-M4 with no `.bss`; the instance is 452 bytes and a thread
  record 40. New findings: B8 (a byref to a local has no id) and a seventh B1
  point. The RPL layer the owner described is recorded in E as a plan.

- **2026-09-29, second part** — The owner settled B6 and the I4-versus-NINT
  question in one round: the library normalises, and the interpreter
  chooses. `value_compare` orders any two slots — integers in 64 bits,
  signed or unsigned; an integer meets a double through the conversion;
  references by id, byrefs by address; any other mix `VALUE_UNORDERED` — so
  `I4 5` equals `NINT 5`. `value_convert` converts any number to twelve
  primitive types, checked or not, per Table III.8; a float out of range
  saturates, NaN to 0, as CoreCLR does. Doc 18's stale interning text was
  fixed first (`c03c1f6`), the decision went into 14 §§1 and 6.7 and
  18 §§6–7 (`928c029`), and the code followed (`6127ce6`), with
  `stack_slot_t` in `objects_types.h` so that it does not wait for OH-4.
  111 cases pass in the three builds, every file compiles clean for host and
  ARM, C99 and C11, and eight mutants were caught. GCC's `undefined`
  sanitizer misses an out-of-range float cast, so the sanitized build adds
  `float-cast-overflow`. 8,463 bytes for Cortex-M4, from 7,128. New finding:
  B7.

- **2026-09-29** — At the owner's request the out-of-memory callback
  `on_oom` is now `on_no_memory`, and every test name built on it follows
  (`fake_no_memory_count`, `FAKE_EXPECT_NO_MEMORY`, the two
  `*_ends_in_on_no_memory` cases); 14 §7.5 now says what it is (`d682790`).
  101 cases pass in the three builds; every file compiles clean for host and
  ARM, C99 and C11.

- **2026-09-28, sixth part** — The owner aligned the model: an application
  is one CLI binary, packed into one image, on one OS thread; managed threads
  are green threads the VM schedules; several applications at once is not a
  use case. Docs 11, 14, 17 and 18 now say so (`3f33e07`), and the code's
  comments and test names follow (`ce58272`). A14 restated: I4 and I5
  withdrawn; the instance stays, for 14 §8's no-hidden-state rule, and
  `test_instances.c` now guards that rule rather than a second application.
  No behaviour changed; 101 cases pass in the three builds.

- **2026-09-28, fifth part** — The owner deferred OH-4 and set the threading
  model: one application is one thread, and several applications are several
  threads that share nothing. Ruffus isolates applications through one
  `app_instance` pointer switched per slice; the new library had nine
  file-scope globals instead, so two applications would have shared a heap.
  **Instances decided and built** (A14; `bed86cf`, `d99a6ec`): every global
  moved into `obj_handler_t`, which every call takes first and every host
  callback receives; `objects_shutdown` and `obj_string_init` are gone.
  Measured on Cortex-M4, resolving an id takes 9 instructions with the
  instance passed, 10 with globals and 11 with a switched pointer.
  `test_instances.c` adds 4 cases, and 101 pass in the three builds; every
  file compiles clean for host and ARM, C99 and C11. A heap pointer kept
  inside `heap.c` fails three of the new cases. 7.1 KB of code for
  Cortex-M4, no `.bss`, and 444 bytes per instance.

- **2026-09-28, fourth part** — The owner deferred the collector (OH-3) and
  asked for D7 on its own. D7 decided (`6d72cea`): the statics slab is a flat
  space the embedder fills; constants are an indirect space over a 12-byte-
  entry table in the image, immutable and id-free; a literal is a constant,
  so `ldstr` allocates nothing. That settled B3 and superseded A6. Built
  (`e06439e`): `E_STATICS` and `E_CONST`, constants readable by every
  module through `objects_info`, `obj_string_intern`. 97 cases pass in the
  three builds; three mutants were caught, one only after a test was
  strengthened; 7.0 KB for Cortex-M4. Still ahead: OH-3 (D5, D6), OH-4,
  `value_compare`, and B5, which now includes doc 00's "literal-pool index".

- **2026-09-28, third part** — OH-5 moved before OH-3 and OH-4 at the
  owner's request, and D9 decided: a NUL-terminated UTF-8 payload,
  `OBJECT_H_ASCII`, `Length` and `obj_string_char_at` in UTF-16 units,
  `OBJECT_NULL` for invalid UTF-8, `string_type` in the config (`a1faa16`).
  **OH-5 built** (`92ff6ea`): arrays per 14 §5.7, strings per D9, boxing.
  Nothing was ported from Ruffus: its strings are mutable NUL-terminated
  ASCII and its boxing is the model 14 §6.6 rejects; the array range check
  and overflow guard of `476cd6e` carried over as logic. 81 cases pass in the
  three builds; the library is 6.3 KB for Cortex-M4. Two mutants were caught.
  Waiting: interning (OH-3, D7), `value_compare` (OH-4, B6), the tracing test
  (OH-3).

- **2026-09-28, second part** — A2–A13 decided in one round, as recommended,
  and recorded (`b999151`); D4 done (`745922d`). **OH-1 built:** ids in both
  splits, spaces, checked resolution, the eight init checks, with the
  harness, the fake embedder and CMake/CTest (`7ff3a8f`). **OH-2 built:**
  first fit with coalescing, bump top, handle quarantine and generations,
  guard headers and poisoning in debug, `heap_verify`, `heap_stats`
  (`1fe5476`). 54 cases pass in the debug (ASan, UBSan), release and
  standalone builds; every file compiles for Cortex-M4 under C99 and C11; the
  release library is 4 KB of code. A coalescing bug injected into a copy of
  `heap.c` was caught by `heap_verify` with a heap dump, and by the torture
  with its seed. Folded into 14 §11 and 18 §12: the debug handle is 10 bytes,
  not 12; non-inline accessors check ids in every build; a pinned object is
  never released; a remainder too small to split cannot occur.

- **2026-09-28** — A1 decided: the new ObjectHandler is written in
  `source/object_handler/` — snake_case, not `objecthandler`; docs 13, 14, 18
  and 19 synced (`360c37a`). `obj_handler.h` moved there (`0cf74cf`) and cut
  to its guard and `obj_handler_status_t` (`358f7c2`); it compiles on its own
  with `-Werror -pedantic`, C99 and C11, host and ARM. The verification
  recipes now target the new directory. Every other Ruffus file was reviewed
  against doc 14 and none is reusable, so the owner had `ObjectHandler/`
  deleted (`b3dd1bf`), closing C3–C6. New findings: B6 (Ruffus has no
  promotion table for `value_compare`), Ruffus's broken number formatting
  (E, doc 09), and the new directory's line endings (A5).

- **2026-09-25** — Reviewed docs 00–18 for readiness. Heap allocator widened
  to `uint32_t` (`cf433a6`). `strings` renamed to `obj_string`, files and
  prefix, after finding it shadowed `<strings.h>` on host and ARM compilers
  (`a18881e`, `2bb3152`). D1 settled: `obj_handler_status_t`, 14 §9.2
  (`e444cb9`). Array model settled in 14 §5.7; `arrays_type` cut from eleven
  public functions to four, with its overflow and out-of-memory bugs fixed
  (`476cd6e`). Found that no Ruffus file compiles for ARM (C3). This checklist
  created.
