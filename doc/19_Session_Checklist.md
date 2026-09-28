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

State recorded here: **2026-09-28**, at commit `b3dd1bf`.

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
| **ObjectHandler** | standalone C library for objects, the heap, per-thread stacks and the collector; knows nothing about CIL | contract [14](14_ObjectHandler.md), plan [18](18_ObjectHandler_Build_Plan.md); implementation starting |

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
| Threads | one OS thread; green threads preempted at safepoints; 20 ms slice | 17 |
| `native int` | 32 bits on every target | 15 §11 |
| Strings | UTF-8 payloads; `cli_pack` rejects non-ASCII literals unless `--allow-non-ascii` | 14 §5.6 |
| Arrays | one-dimensional, inline elements, fixed length; the type's refmap describes one element | 14 §5.7 |
| Stack | typed slots and overlapped frames in v1; frame scratch and the size-class recycler deferred | 14 §6.0 |
| Collector | tricolour mark-sweep, precise, non-moving in v1; a 75 % threshold sets a flag, and a failed allocation collects at once | 14 §7 |
| Init failures | `obj_handler_status_t`, from `obj_handler_init` only; after init, embedder bugs go to `on_corrupt` | 14 §9.2 |

### 2.3 Hard rules

- The interpreter never recurses into the C stack for a managed call
  ([17 §2](17_Threading_Model.md)).
- Every reference store goes through `objects_write_ref`
  ([14 §6.0](14_ObjectHandler.md)).
- No raw pointer survives a safepoint: resolve once per opcode
  ([14 §2.1.1](14_ObjectHandler.md), [11 §4.3](11_Memory_Model.md)).
- ObjectHandler contains no CIL, ECMA-335 or `.vxb` knowledge, calls neither
  `malloc` nor `printf`, and its headers compile against `<stdint.h>` and
  `<string.h>` only ([14 §1](14_ObjectHandler.md)).
- `on_oom` never returns ([18 D1](18_ObjectHandler_Build_Plan.md)).
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
- **Line endings** are kept per file: most Ruffus headers are CRLF, most `.c`
  files LF.

### 2.5 Environment

| Item | Value |
|---|---|
| Repository | `/home/wifi/projects/cli_parser`; `/home/wifi/projects/ruffusvm` is reference only |
| Host compiler | gcc 13, Linux |
| Build | CMake 3.28.3; `cmake --build build` builds `cli_parser` |
| Device compiler | `/home/wifi/gcc_toolchain/bin/arm-none-eabi-gcc` 14.2.1 with newlib, e.g. `-mcpu=cortex-m4 -mthumb` |

### 2.6 How every change is verified

1. **Compile every file of `source/object_handler/` on its own**, with the
   include path set to that directory alone and
   `-Wall -Wextra -Werror -pedantic`, under C99 and C11 until A5 picks one.
   The baseline is zero diagnostics.
2. **Prove a behaviour change with a harness** that fails before the change
   and passes after it, one process per case, built with
   `-fsanitize=address,undefined`.
3. **Step 1 runs on the ARM compiler too**: all of the library is device
   code.
4. **Once OH-T exists,** `ctest` in the debug, release and standalone builds
   replaces steps 1 and 2 (18 §0).

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

### 3.2 Code, file by file

`source/object_handler/`, the new library (A1):

| File | State | Grows in |
|---|---|---|
| `obj_handler.h` | include guard and `obj_handler_status_t` (D1); compiles on its own, host and ARM, C99 and C11 | OH-1: init, config and host structs, and an include for each module header as it lands |

Nothing else carried over from the Ruffus copy. Every other file was built on
its model — 16-bit `object_t`, the `app_instance` global, `types.h`, the `T_*`
mode byte — and none would build against doc 14 without a rewrite. What each
slice takes from Ruffus, as design rather than code, is in 13 §§4 and 6.

### 3.3 Documents

| Doc | State |
|---|---|
| [00](00_Introduction.md) | project overview and roadmap; current |
| [10](10_Native_Assembly_Layout.md) | `Native.*` assemblies; §§4–8 deferred |
| [11](11_Memory_Model.md) | memory policy; **contradicts 14 in six places** — B1 |
| `12_VXB_Format.md` | **not written**; the next contract after ObjectHandler v1 |
| [13](13_Ruffus_VM_Reuse.md) | Ruffus VM heritage; current |
| [14](14_ObjectHandler.md) | ObjectHandler contract, normative; one false claim in §1 — B2 |
| [15](15_Pack_Time_IL_Transforms.md) | pack-time IL transforms; current |
| [16](16_Native_Call_Protocol.md) | native call protocol; four items open |
| [17](17_Threading_Model.md) | threading model; two items open |
| [18](18_ObjectHandler_Build_Plan.md) | build plan; §12's "Nothing remains open" is stale — B4 |
| 19 | this checklist |
| [99](99_phase0_work.md) | Phase 0 record; history |

### 3.4 Build plan progress

| Doc 18 item | State |
|---|---|
| D1 error and failure model | **done** — `obj_handler_status_t`, 14 §9.2 |
| D2 debug object identity | decided; lands in `objects_types.h` with OH-1 |
| D3 header isolation and naming | names and location decided (`objects_types.h`, `obj_string`, `source/object_handler/`); the isolated build waits for C1 |
| D4 old `source/framework/` draft | waits for C2 |
| OH-T to OH-6 | not started |

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
  *Recorded in:* 14 §9.
- [x] **A7 — Collector ↔ stack seam.** *Decided:* `stack.c` registers one
  root-scan function with `gc.c`, which never includes `stack.h`, so OH-3 is
  testable before OH-4. *Recorded in:* 14 §7.4, 18 §5.
- [x] **A8 — Who bumps a handle's generation.** *Decided:* `heap_release`,
  which the sweep calls. *Recorded in:* 18 D2, 14 §3.4.
- [x] **A9 — OH-1's stale-id test.** *Decided:* it moves to OH-2, where
  `heap_alloc` and `heap_release` exist. *Recorded in:* 18 §3, §4.
- [x] **A10 — What `heap_alloc` does when memory runs out.** *Decided:*
  `OBJECT_NULL` only for a request that can never fit; otherwise collect,
  retry once, then `on_oom`. *Recorded in:* 14 §5.2, 18 D1.
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
- [ ] **B2 — 14 §1** claims `heap.c` compiles without `WANT_OBJECT_HANDLER`.
  It does not (`heap_t` is defined twice; `flags` and `b_size` are missing).
- [ ] **B3 — 14 §3.1 against §3.3** on what an `OBJECT_SPACE_CONST` index
  means — a literal-pool index or a byte offset. Settled by D7.
- [ ] **B4 — 18 §12's "Nothing remains open"** stays untrue until A is
  closed; reword it to point here.
- [ ] **B5 — Docs 00, 13, 15 and 17**: check them for the same stale points
  as B1 (strings open, tier 2 shipping).
- [ ] **B6 — 14 §6.6 and 18 OH-5** tell `value.c` to reimplement Ruffus's
  "mixed-type promotion table" for `value_compare`. Ruffus has none: its
  `value_compare` rejects operands of different types. The rules have to come
  from ECMA-335 Partition III §1.5 instead; settle them with OH-5.

### C. Build and code chores

- [ ] **C1 — CMake.** An `object_handler` static library whose include path
  is its own directory only; the standalone `-Wall -Wextra -Werror -pedantic`
  build; `enable_testing()` with one test per case; debug and release builds
  (18 D3, OH-1 item 5, OH-T). Depends on A5; A1 is done.
- [ ] **C2 — D4.** Delete `source/framework/objects.c/.h` and
  `stack.c/.h`: CMake builds only `pool.c` and `critical_section.c` from that
  directory. Needs the owner's confirmation.
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
- [ ] **C7 — Test harnesses.** The heap and array harnesses of 2026-09-25
  lived only in `/tmp`; OH-T recreates them in `tests/object_handler/`.

### D. Build plan slices — doc 18

- [x] **D1** — error and failure model: `obj_handler_status_t` (`e444cb9`).
- [ ] **D2** — debug object identity, written into `objects_types.h`.
- [ ] **D3** — header isolation: names and location done; the isolated build
  needs C1.
- [ ] **D4** — the old `source/framework/` draft: C2.
- [ ] **OH-T** — test system: `harness.h`, fake embedder, fault injection,
  replay, CTest (18 §2).
- [ ] **OH-1** — foundation: types, configuration, spaces, resolution,
  `obj_handler_init` with the checks of 14 §9.2 (18 §3).
- [ ] **OH-2** — allocator: new `heap.c`, free list, `heap_release`,
  `heap_verify`, statistics (18 §4).
- [ ] **OH-3** — collector (18 §5). Decide first: D5 rooting-scope API, D6
  `gc_poll` and the safepoint contract, D7 statics slab and literal pool
  (includes B3).
- [ ] **OH-4** — stack: threads, slots, frames (18 §6).
- [ ] **OH-5** — typed constructors: `array.c`, `obj_string.c`, `value.c`
  (18 §7). Decide first: D9 string layout and interning. `String.Split` stays
  out of `obj_string.c`: it needs the embedder's `string[]` type id (C6).
- [ ] **OH-6** — hardening: sanitizers, coverage, 32-bit `-Os` build,
  statistics dump (18 §8).

### E. Later, not blocking

- Finalizers and weak references (14 §11, item 1).
- The tuning constants, which need host measurements (14 §11, item 2).
- Struct copies and the write barrier, decided with tier 2 (14 §11, item 3).
- Generational collection (11 §7.7).
- The four open items in doc 16 and `Thread.Interrupt` semantics (17 §11).
- `12_VXB_Format.md`, the next contract after ObjectHandler v1
  (00 §7.1), and `09_Native_BCL_Bridge.md` before the first intrinsic. Number
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

# 1 and 3. Every library file on its own: host and device, C99 and C11 (A5).
for cc in gcc "/home/wifi/gcc_toolchain/bin/arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb"; do
  for std in c99 c11; do
    for f in source/object_handler/*.[ch]; do
      $cc -std=$std -fsyntax-only -Wall -Wextra -Werror -pedantic \
        -I source/object_handler -x c "$f" || echo "FAIL: $cc -std=$std $f"
    done
  done
done

# 2. A harness against the library, one case per process.
gcc -std=c11 -g -O0 -fsanitize=address,undefined -I source/object_handler \
  test.c source/object_handler/*.c -o test
```

---

## 6. Session log

Newest first. One entry per session: what changed, and the commits.

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
