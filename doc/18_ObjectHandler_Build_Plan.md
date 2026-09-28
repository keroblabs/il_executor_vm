# ObjectHandler build plan

Build plan for [14_ObjectHandler.md](14_ObjectHandler.md). Doc 14 says *what*
the library is; this says *in what order it gets written, and what proves each
step*. It is a living document: each slice, once landed, is struck through
here and its consequences folded back into 14 §11.

Status: **in progress — D1–D4, OH-1 and OH-2 are built** (§0.1). Every
review item in §12 is closed. Progress, and the decisions each later slice
needs first, are tracked in [19_Session_Checklist.md](19_Session_Checklist.md).

**Scope rule.** This plan covers ObjectHandler alone — no `cli_exec`, no
`cli_pack`, no image. The embedder is a ~150-line fake in the test tree, which
is what makes every item below testable without the rest of the system.

---

## 0. How to read this

- **P0** items are decisions. They are in the first commit's headers, so
  changing them later is a rewrite rather than an edit. Everything else waits.
- **OH-n** items are code slices. Each names its files, the decisions it
  consumes, and *the tests that define it as done* — "compiles" is never done.
  `OH-n` is a label in this document, never an identifier in code.
- **Naming follows 14 §1.2**: no library-wide prefix, a name carries its
  owning module. `oh_` appears nowhere — the shared vocabulary header is
  `objects_types.h`, the lifecycle header is `obj_handler.h`, the profile
  header is `obj_handler_config.h`.
- **There is one configuration, not a host one and an MCU one** (§0.2).
- **Size** is relative scope (S/M/L), not a schedule.
- A slice is finished only when its tests pass in **three builds**: debug,
  release, and the standalone `-Werror -pedantic` build of 14 §1.1. Two of the
  three catch what the other hides.

### 0.1 Priority summary

| # | Item | P | Size | Depends on | Done when |
|---|---|---|---|---|---|
| ~~D1~~ | ~~Error and failure model~~ | **P0** | S | — | done — `e444cb9` |
| ~~D2~~ | ~~Debug object identity (generation)~~ | **P0** | S | — | done — `7ff3a8f` |
| ~~D3~~ | ~~Header isolation and file naming~~ | **P0** | S | — | done — `7ff3a8f` |
| ~~D4~~ | ~~Disposition of the old `framework/` draft~~ | **P0** | S | — | done — `745922d` |
| OH-T | Test system: harness, fake embedder, fault injection, replay, CI | **P1** | L | D3 | §2 acceptance list — L1, L2, L7 done; L3, L5, L6 in part |
| ~~OH-1~~ | ~~Foundation: types, config, spaces, resolution, build~~ | **P1** | M | D1–D4, OH-T L1–L2 | done — `7ff3a8f` |
| ~~OH-2~~ | ~~Allocator: `heap.c`, free list, `heap_release`, `heap_verify`, stats~~ | **P2** | L | OH-1 | done — `1fe5476` |
| OH-3 | Collector: `gc.c`, roots, scopes, triggers | **P3** | L | OH-2, D5–D7 | §5 tests |
| OH-4 | Stack: threads, slots, frames | **P4** | M | OH-3 | §6 tests |
| OH-5 | Typed constructors: `array.c`, `obj_string.c`, `value.c` | **P5** | M | OH-2, D9; interning with OH-3, `value_compare` with OH-4 | §7 tests — built in `92ff6ea` except the three pieces of §7 |
| OH-6 | Hardening: sanitizers, coverage, 32-bit release build, stats dump | **P6** | M | OH-2 onward | §8 tests |

D5, D6, D7 and D9 are decisions too, but they are **not** P0: each is consumed
by exactly one slice and is written when that slice is (§9).

### 0.2 One configuration, not two profiles

An earlier draft of this plan had `OH_PROFILE_HOST` and `OH_PROFILE_MCU`.
That was wrong, and doc 14 says why in three separate places
([11 §4.6](11_Memory_Model.md), 14 §5.6, 14 §7.3): the host build is a
**rehearsal for the device**, so a host build that sized, collected or encoded
differently would stop proving anything about the device. Two profiles would
have quietly destroyed the reason for testing on the host at all.

So `obj_handler_config.h` carries **one** set of values —
`OBJECT_SPACE_COUNT = 0x0014`, `HEAP_SEGMENTS = 1`, the worklist length, the
scope depth — and the only axis that varies is **debug versus release**:

| Switch | Debug | Release | Owner |
|---|---|---|---|
| `OBJECT_DEBUG_REFS` | on — bounds, `INUSE` and generation checks | off | `objects.c` |
| `HEAP_DEBUG` | on — guard header, signature, back-reference | off | `heap.c` |
| `HEAP_POISON` | on — `0xDD` fill and ASan poisoning of free blocks | off | `heap.c` |
| `HEAP_VERIFY_EVERY_SAFEPOINT` | on | off | `heap.c` |

A device debug build is therefore a legitimate and useful thing — same
configuration, instrumentation on — which is exactly what makes `heap_verify`
runnable inside a misbehaving Ruffus (§2, L3).

---

## 1. P0 — the four decisions in commit one

### D1 — Error and failure model

Every function had an unspecified failure path. Decision:

| Case | Behaviour |
|---|---|
| Allocation cannot be satisfied | `OBJECT_NULL` only for a request that can never fit; otherwise collect, retry once, then `on_oom` — A10, 14 §5.2. `obj_string_create` also returns `OBJECT_NULL` for invalid UTF-8 (D9) |
| `on_oom` | **must not return** — it aborts, `longjmp`s, or resets the board. The library has no way to continue and must not pretend it has |
| `obj_handler_init` | returns `obj_handler_status_t` — one code per thing the embedder has to fix, listed in 14 §9.2 (not a bare `int`) |
| Detected corruption | `on_corrupt(where, id)`, then return a safe value and keep going, in every build. Aborting is the embedder's choice inside `on_corrupt` (A13), which is what lets §2.1's recording fake run negative tests in a debug build |
| Bad argument from the embedder (null id, out-of-range index) | `on_corrupt` — it is a bug in the embedder, not a run-time condition |

**The boundary:** until `obj_handler_init` returns `OBJ_HANDLER_OK`, errors
are returned; after that, an embedder bug goes to `on_corrupt`. Init cannot
report through `on_corrupt`, because a NULL `on_corrupt` is one of the things
it rejects. The same rule makes `objects_space_register` `void` (14 §8).

`obj_handler_status_t` lives in `obj_handler.h` because 14 §1.2 gives the
lifecycle module the init/shutdown/config/host vocabulary. It also gives the
test suite something to assert on other than a crash, which is the practical
reason to have it.

### D2 — Debug object identity

**Why the index is a handle index, not an offset.** Worth stating first,
because the Ruffus model is the opposite one and the instinct is to reach for
it. Heap ids are **handle-table indices from the first commit** (14 §3.1
catalogue, §3.2, §3.3) — not payload offsets — and there is no later
migration to "real" indices. Offsets survive only in the *flat* spaces
(statics, frame scratch, flat constants), which is precisely why those regions
are immovable (14 §2.3.1).

Offset-based ids would not be a simpler v1; they would be a more expensive one
that also forecloses compaction:

| | Handle index (this design) | Payload offset |
|---|---|---|
| Where `type`, `size`, `kind`, `flags` and the mark bit live | the handle, outside the payload | a per-object header *inside* the payload |
| Overhead per 16-byte object | **8 B** (14 §5.1) | 8 B header, plus the payload fragmentation it causes |
| Adding compaction later | rewrite one field per live object | rewrite **every stored reference in the system** — fields, statics, array elements |
| A stale id resolves to | a recycled handle — a different live object | whatever now occupies that offset — a different live object |

The last row is the point of D2: **the failure mode is identical either way**,
so the detection has to exist either way. A handle index is reused after a
sweep, so a stale `object_id_t` resolves to a different, valid, live object
and corrupts silently. This is the one bug class that costs days later and
costs nothing now.

Proposal — two mechanisms, both debug-only.

#### Mechanism 1 — a generation counter carried in the id

The idea in one sentence: **give every handle a counter that increments each
time it is freed, put a copy of that counter inside the id, and have
`objects_ptr` refuse an id whose copy is stale.**

The id is 32 bits in both builds. Only the meaning of the top half changes:

```
release  — OBJECT_DEBUG_REFS off

  bit 31                 16 15                  0
      +-------------------+--------------------+
      |     space tag     |       index        |
      +-------------------+--------------------+
            16 bits              16 bits


debug    — OBJECT_DEBUG_REFS on

  bit 31       24 23       16 15                  0
      +----------+----------+--------------------+
      |  space   |   gen    |       index        |
      +----------+----------+--------------------+
         8 bits     8 bits         16 bits
```

The index keeps all 16 bits, so the handle table is addressed identically in
both builds. The 8 generation bits are taken from the space tag, which in v1
uses tags `0x0000`–`0x0013` — 20 of the 65 536 available (14 §3.1) — so the
tag has bits to spare.

**Worked example.** A dangling reference, in both builds:

| Step | What happens | Debug id | Release id |
|---|---|---|---|
| 1 | `objects_create` takes handle 7, whose generation is 3 | `0x0003_0007` | `0x0000_0007` |
| 2 | the object dies; `heap_release`, called by the sweep, frees handle 7 and bumps its generation to 4 (A8) | — | — |
| 3 | a later allocation is handed handle 7 again | `0x0004_0007` | `0x0000_0007` |
| 4 | buggy code still holds the id from step 1 and dereferences it | handle 7's generation is 4, the id says 3 → **`on_corrupt` fires at the first use** | byte-identical to step 3's id → **silent corruption, discovered much later and somewhere else** |

Step 4 is the entire argument. In release the two ids are the same 32-bit
value and nothing can tell them apart; in debug they differ in one byte.

**What it costs:**

| | Release | Debug |
|---|---|---|
| `object_id_t` width | 32 bits | 32 bits — *unchanged* |
| Spaces addressable | 65 536 | 256 (v1 uses 20, and the reserved range ends at `0x00FF`) |
| Objects per segment | 65 535 | 65 535 — unchanged |
| `object_handle_t` | 8 bytes | 9 bytes, padded to 10 |
| Cost per resolution | none | one byte compare |

**What it constrains**, and this is the whole of the "two id formats" worry:

1. An embedder-defined space above `0x00FF` (14 §3.1) cannot be used in a
   debug build. There are none in v1, and none planned.
2. An id must never be written to a file, an image, or anything that outlives
   the process, because its layout differs between builds. It never is — ids
   are run-time only, and the image stores literal-pool *indices*, not ids.
3. Nothing may open-code `>> 16`. Every id manipulation goes through
   `OBJECT_ID` / `OBJECT_SPACE` / `OBJECT_INDEX`, in the library **and** in
   the tests, from the first line written. That is why D2 is P0: it is a rule
   about all code, not a feature in one file.

**Why not just widen the id to 64 bits in debug?** It reads more cleanly, and
it is worse: every object field, array element, stack slot and refmap word
would change size, so the debug build's memory layout would stop matching the
release build's — and a debug run would no longer be a rehearsal of the thing
that ships, which is the same mistake §0.2 rejects. Keeping 32 bits and
reinterpreting the top half leaves every structure byte-identical.

#### Mechanism 2 — handle quarantine

Freed handles go to the **tail** of the handle free list rather than the head,
so a handle index is reused as late as possible. On its own this detects
nothing — it only widens the window in which mechanism 1 can fire, and delays
the moment a stale id starts aliasing a live object. It costs one extra
pointer in the free list and is worth having either way.

### D3 — Header isolation and file naming

[CMakeLists.txt](../CMakeLists.txt) already puts `./source` on the include
path, and [source/types.h](../source/types.h) lives there — so 14 §1.1's
`source/object_handler/types.h` is ambiguous the day both are on a path. The
same argument applies to `objects.h`, which already exists twice.

**Decision: the header keeping the shared vocabulary is `objects_types.h`**,
named for the module that owns `object_id_t` per 14 §1.2, not for the library.
The full set:

| File | Holds | Prefix it exports |
|---|---|---|
| `objects_types.h` | `object_id_t`, id macros, `object_handle_t`, `object_space_t`, `object_byref_t`, `OBJECT_H_*`, `OBJECT_K_*`, `array_header_t` (read by `gc.c` — 14 §5.7) | `OBJECT_` / `objects_` |
| `obj_handler_config.h` | the single configuration (§0.2) and the debug switches | `OBJECT_` / `HEAP_` / `STACK_` per owner |
| `obj_handler.h` | umbrella; `obj_handler_init/shutdown`, `obj_handler_config_t`, `obj_handler_host_t`, `obj_handler_status_t` | `obj_handler_` |
| `heap.h`, `objects.h`, `gc.h`, `stack.h`, `array.h`, `obj_string.h`, `value.h` | one module each | their own |

The `object_handler` target's include path contains **only**
`source/object_handler/`, so `source/types.h` is unreachable from inside the
library by construction rather than by
discipline. 14 §1.1's file list is corrected to match.

Isolation keeps `source/` out of the library; it cannot keep the library's
headers from shadowing *system* headers on the same `-I` path. `strings.h`
did exactly that — glibc's and newlib's `<string.h>` include `<strings.h>` in
their default GNU modes, and the copied Ruffus directory failed with 260
errors under `-I`. **Decision: the string module is `obj_string`** — file,
`obj_string_` prefix and `OBJ_STRING_` macros — which also moves the prefix
off `str` + lowercase, reserved for `<string.h>` by C11 §7.31.13. Every other
name in the table was checked against the standard and common POSIX headers,
on host gcc and `arm-none-eabi-gcc`, and is clean.

**Location (2026-09-28): `source/object_handler/`, written new.** The
library is a new implementation, not a run of patches to the Ruffus copy that
`126dfe2` put in `ObjectHandler/`: the Ruffus model — 16-bit ids, no handle
table, a three-list collector, boxed stack slots — differs from doc 14 at the
root, so each patch bought less than the one before. Only `obj_handler.h`
carried over. The rest of the copy was deleted the same day, because no file
in it would build against doc 14 without a rewrite; the originals are in
`ruffusvm/ObjectHandler/`, and this repository's changes to them are in
history up to `476cd6e`. The directory is snake_case like
`source/row_table/`, and the rest follows it: `tests/object_handler/`, CMake
target `object_handler`.

### D4 — The old draft in `source/framework/`

`framework/objects.c/.h` and `framework/stack.c/.h` were the pre-contract
draft that 14 §6.6 rejects — `object_t` with a union payload, boxed slots,
empty stubs — and were still on the `cli_parser` include path.

**Done (2026-09-28): all four deleted.** Nothing included them and CMake built
neither, so `cli_parser` was unaffected. Leaving a second `objects.h`
reachable is how the wrong one gets included six weeks from now.

---

## 2. OH-T — the test system

**Promoted to a slice of its own, at P1**, because it is what the whole
project rests on: a well-tested ObjectHandler is the component you get to stop
suspecting, and that status has to be *earned by machinery*, not asserted.

The important correction to the earlier draft: the assertion framework is the
**smallest** part of this. What makes a test system strong enough to take a
component off the suspect list is layers 3 to 7 below — the oracle, the fault
injection, the replay. A framework only prints the result.

### 2.1 The eight layers

| L | Layer | What it is | Built in |
|---|---|---|---|
| 1 | Assertions, registration, reporting | **`harness.h` + CTest** — assertion macros carrying file, line and expression, a `{name, fn}` table, one OS process per test case. No vendored framework (§2.2) | OH-T |
| 2 | Fake embedder | static type table (type id → refmap, instance size, element size), `on_oom` that `longjmp`s so exhaustion is assertable, `on_corrupt` that *records* so negative tests can run to completion | OH-T |
| 3 | **Invariant checkers** | `heap_verify()`, `gc_verify()`, `stack_verify()` — **library code under the debug switches, not test code** (§2.3) | OH-2, OH-3, OH-4 |
| 4 | **Oracle / shadow model** | an independent object graph and reachability answer kept by the harness; after every collection the live set must equal it *exactly*, so a leak and a premature free both fail | OH-3 |
| 5 | **Fault injection** | fail the *n*th allocation; exhaust handles with payload free and vice versa; force worklist overflow with a one-entry worklist; corrupt a handle deliberately and require `on_corrupt`; return NULL from `type_refmap`. Every failure path in the library is reachable only from here | OH-T, extended per slice |
| 6 | **Generation and replay** | seeded operation streams; a failing run writes seed + op log; a failing seed becomes a checked-in regression verbatim. A stress failure that cannot be replayed is not a test, it is a rumour | OH-T |
| 7 | **Isolation and diagnostics** | one process per case (§2.2), so a deliberately corrupted heap takes down only its own case; on failure, dump the heap block map, the handle table, the free list and the stats block | OH-T |
| 8 | **CI matrix** | debug × release × standalone, plus ASan/UBSan, plus gcov/lcov coverage, plus a 32-bit `-Os` build (§8) | OH-6 |

### 2.2 Layer 1 — no framework, because CTest already is one

This reverses the previous revision, and this time on a criterion rather than
a preference: **what does layer 1 need that CTest does not already give?**

| Need | Provided by |
|---|---|
| Discovery, filtering, parallel running | `ctest`, `ctest -R`, `ctest -j` |
| Pass/fail reporting and a summary | `ctest` |
| **Per-test process isolation** | one `add_test` per *case* — stronger than any in-process framework offers, and free |
| Assertion macros with file, line, expression | ~50 lines of `harness.h` |
| Dump the heap map, handle table, free list and stats on failure | must be custom — no framework knows what a handle table is |

That is the entire list, and the bottom two rows are custom work whatever sits
above them. A vendored framework would add the repository's only dependency,
duplicate the top three rows, and contribute nothing to the two that matter.

So: `harness.h` carries the assertion macros and a `{name, fn}` table; `main`
runs the case named on its command line; CMake registers one `add_test` per
case. A test that deliberately corrupts the heap and aborts kills its own
process and nothing else — which is layer 7, obtained for free rather than
with `fork()`.

**On the two reversals:** the first draft said "no framework" for the wrong
reason (build simplicity), the second said Unity for a wrong premise — that a
serious test system means a big framework. It does not. The mass is in layers
3–7, and layer 1 is fifty lines.

### 2.3 The one thing that makes attribution possible

**Layer 3 is library code, compiled under `HEAP_DEBUG` / `OBJECT_DEBUG_REFS`,
not test code.** That is the whole answer to *"is it ObjectHandler's fault?"*:
because `heap_verify()` ships inside a debug build of the library, a
misbehaving Ruffus can be run with it enabled at every safepoint, and the
question is settled by output rather than by argument. A verifier that only
exists in `tests/` answers the question for the tests and for nothing else.

The corollary is a coverage requirement rather than a coverage aspiration:
`heap.c` and `gc.c` need close to full line **and branch** coverage, because
they are precisely the files whose bugs are unattributable once the
interpreter is on top of them. Layer 5 is how the failure branches get
covered; there is no other way to reach them.

### 2.4 Tree

```
source/object_handler/         the library — no test code, ever
tests/object_handler/
    harness.h                  L1 — asserts, {name, fn} table, dump-on-failure
    fake_embedder.c/.h         L2
    faults.c/.h                L5
    oracle.c/.h                L4 — shadow graph and reachability
    replay.c/.h                L6 — seed, op log, record and replay
    test_objects.c             OH-1
    test_heap.c                OH-2
    test_gc.c                  OH-3
    test_stack.c               OH-4
    test_array_string.c        OH-5
    test_value.c               OH-5 — boxing
    torture.c                  OH-2 and OH-3, driven by oracle + replay
    regressions/               failing seeds, one file each, never deleted
```

No `vendor/` directory: the repository keeps zero dependencies, test-side
included.

### 2.5 OH-T is done when

- `ctest` runs every case as its own process, in debug, release and the
  standalone build, and `ctest -R` selects a single one;
- a deliberately broken commit (an off-by-one in the free-list insert) is
  caught by L3 and reported with a heap dump, not a segfault;
- a case that aborts deliberately fails alone and the rest of the suite still
  reports;
- an injected allocation failure at operation *n* is reproducible from the
  seed alone, on a different machine;
- the fake embedder contains no CIL, no ECMA and no image vocabulary — the
  standing check that the 14 §1 boundary is real.

---

## 3. OH-1 — Foundation

**Goal:** an id can be made, resolved, and bounds-checked; the library
initialises from a caller-supplied config and refuses a malformed one.

**Files:** `objects_types.h`, `obj_handler_config.h`, `obj_handler.h`,
`obj_handler.c`, `objects.c/.h`, CMake targets.

**Consumes:** D1, D2, D3, D4, and layers 1–2 of OH-T.

**Work:**

1. `objects_types.h` — `object_id_t`, `OBJECT_NULL`, the id macros in **both**
   splits behind `OBJECT_DEBUG_REFS`, `object_handle_t`, `object_byref_t`,
   `OBJECT_H_*`, `OBJECT_K_*`, `object_space_t`, and `array_header_t`
   (14 §5.7).
2. `obj_handler_config.h` — the single configuration of §0.2:
   `OBJECT_SPACE_COUNT`, `HEAP_SEGMENTS`, `OBJ_STRING_ENCODING`, worklist
   length, scope depth, and the four debug switches with their debug/release
   defaults.
3. `objects.c` — the `object_spaces[]` table, `objects_space_register`, the
   three resolution paths (handled / flat / indirect→`on_corrupt` in v1),
   `objects_type_of` / `size_of` / `kind_of`, and `objects_write_ref` +
   `objects_escape` as the identity pair required by 14 §6.0.
4. `obj_handler_init` validation, one `obj_handler_status_t` per branch, in
   enum order (14 §9.2): `E_ARG_NULL`, `E_ALREADY_INIT`, `E_SEGMENTS`,
   `E_PAYLOAD_BASE`, `E_PAYLOAD_SIZE`, `E_HANDLES`, `E_WORKLIST`,
   `E_HOST_CALLBACK`. Nothing is written until every check has passed.
5. CMake: `object_handler` static library with an isolated include path (D3);
   the 14 §1.1 standalone job; CTest wiring for the three builds.

**Done when:**

- every space tag round-trips through the id macros, in both splits;
- an out-of-range index, an unregistered space and a not-`INUSE` handle each
  call `on_corrupt` exactly once;
- every `obj_handler_init` precondition has a test that trips it and gets its
  own status code back, after which a valid init still succeeds — the proof
  that a failed init wrote nothing;
- the standalone `-Wall -Wextra -Werror -pedantic` build passes with no
  include path into `source/`.

**Risk:** the two id splits diverging. Mitigation is D2's rule — macros only,
enforced by a grep in the CI job for `>> 16` outside `objects_types.h`.

---

## 4. OH-2 — Allocator

**Goal:** `heap.c` allocates and releases payload blocks, and `heap_verify()`
can prove the heap self-consistent at any instant.

**Files:** `heap.c/.h`.

**Consumes:** nothing new. This is the slice with no pending decisions, which
is why it is early.

**Work:**

1. Segment record: payload base and size, bump top, free-list head, `used`,
   `collect_at`, handle free-list head and tail (D2 quarantine), stats block.
2. `heap_alloc` per 14 §5.2 — round to 8, reject oversize, first fit with
   split, bump, handle acquisition, zero the payload, set `OBJECT_H_HASREFS`
   from the embedder's refmap, test the threshold and call `gc_request()`
   (a stub until OH-3).
3. **`heap_release(object_id_t)`** — declared in `heap.h`, not hidden behind
   the test build. Its callers are the sweep (OH-3) and the test suite, and it
   exists in this slice so that the allocator is completely exercisable before
   a collector exists. It is reachable through `obj_handler.h`, which
   includes every module header (14 §8), but it is not part of the
   embedder's API: an embedder never frees, because lifetime is the
   collector's job (14 §1).
   Address-ordered free-list insert, coalescing the previous and following
   blocks, handle returned to the *tail* of the free list (D2 quarantine).
4. `heap_verify()` — the invariant checker, and the most valuable single
   function in the slice:
   - free blocks plus live blocks cover the payload exactly once, no gaps and
     no overlaps;
   - the free list is address-ordered and contains no two adjacent blocks
     (i.e. coalescing actually happened);
   - every in-use handle points inside the payload, is 8-aligned, and its
     `size` matches the block;
   - the handle free list terminates, has no cycle, and contains no `INUSE`
     entry.
5. `heap_stats_t`: used, free, largest free, free-block count, live objects,
   peak live bytes, allocation-size histogram — i.e. exactly the three numbers
   14 §5.4 says decide the allocator's successor.
6. `HEAP_DEBUG` guard header (signature + back-reference) and `HEAP_POISON`
   (fill freed blocks with `0xDD`, plus `ASAN_POISON_MEMORY_REGION` when built
   with ASan).

**The free-list terminator — decided, not deferred.** `heap_free_t.next == 0`
means "end of list", but offset 0 is also a legal block, so the list would be
truncated the moment the first block is freed. **The first 8 bytes of every
payload are a reserved sentinel and are never allocated.** That makes offset 0
impossible for a live block, `next == 0` unambiguous, and
`object_handle_t.offset == 0` a usable "no payload" marker for a free or
zero-length handle. It costs 8 bytes per segment and it is exactly the trick
14 §2.1 already plays with index 0 — the same idea applied one level down.
The alternative, `0xFFFF` as terminator, costs nothing but has to be respelled
every time the payload width changes and gives no second benefit.

**Done when:**

- split/coalesce unit tests cover: coalesce with previous only, next only,
  both, neither; exact fit; a remainder too small to split — which cannot
  occur, because sizes and the minimum block are both 8;
- exhaustion: payload full and handles full each end in `on_oom` — the fake's
  `longjmp` — after the collect, a no-op until OH-3, and leave
  `heap_verify()` clean (A10);
- a stale id (freed then reused handle) is caught in debug and is *not* caught
  in release — the same test compiled twice, asserting different outcomes,
  which is what proves the debug machinery is debug-only (moved from OH-1,
  A9);
- seeded torture: random alloc/release, `heap_verify()` after every operation
  for the first 10 000 and periodically after; the run ends by releasing
  everything and asserting the payload is one free block of full size;
- a synthetic interleaved pattern produces a fragmentation ratio that is
  reported and plausible — the §5.4 instrument works before it is needed.

**Risk:** 16-bit internal arithmetic overflowing at exactly 64 KB. Every
test runs once with a payload of the maximum size.

---

## 5. OH-3 — Collector

**Goal:** tricolour mark-sweep over the handle table, with roots.

**Files:** `gc.c/.h`, plus root plumbing in `objects.c`. The thread stacks are
reached through a scan function that `stack.c` registers (A7); until OH-4
exists, the tests register a fake one.

**Consumes** (decide at the start of this slice, §9): D5 rooting-scope API,
D6 `gc_poll` and the safepoint contract; D7 is decided (14 §3.5).

**Work:**

1. Rooting scopes: `gc_scope_push/pop`, `gc_root_add(object_id_t *)`, a
   fixed-depth stack sized from the config. Without this nothing outside a
   stack slot can survive an allocation — it is the first thing written.
2. Mark: bounded worklist of handle indices; scan object payloads by refmap,
   and array payloads by the element refmap `count` times (14 §5.7); the
   overflow flag and its rescan loop (14 §7.2).
3. Sweep: linear handle walk, release unmarked blocks through
   `heap_release`, clear marks, bump the generation (D2), refill stats.
4. Triggers (14 §7.3): threshold → `gc_request()` flag; `gc_poll()` performs
   the collection; allocation failure collects synchronously and retries once;
   the damping rule.
5. `gc_stats_t`: collections, objects marked and swept, bytes recovered,
   worklist high-water, overflow count.

**Done when:**

- **Model-based stress is the headline test.** The harness maintains its own
  object graph and its own reachability answer; after every collection, the
  set of live handles must equal the harness's reachable set — *exactly*, so
  both a premature free and a leak fail. The seed makes any failure a one-line
  repro.
- a tiny worklist plus a deep graph forces the overflow path and must produce
  the identical live set to a large worklist;
- an unreferenced cycle is fully collected;
- arrays built directly with `heap_alloc` and an `array_header_t`, before
  `array.c` exists: a reference array and a struct array whose elements hold
  ids keep their referents alive, and a `byte[]` is never scanned;
- an object reachable only through a scope survives; after the scope pops, it
  is collected;
- the threshold sets the flag and **no collection happens until `gc_poll`**;
  allocation failure collects without waiting for one;
- `heap_verify()` is clean after every collection in the stress run.

**Risk:** the caller holding an unrooted id across an allocation that
collects. The stress test must deliberately contain that pattern so the
failure is ours, in a test, rather than Ruffus's, in six months.

---

## 6. OH-4 — Stack

**Goal:** tiers 0 and 1 of 14 §6 — typed slots and overlapped frames.

**Files:** `stack.c/.h`; the thread ring that 14 §7.4 assumes.

**Work:**

1. `stack_thread_t` and the thread ring (length one in v1, but a ring — 14
   §7.4 and [17 §4](17_Threading_Model.md) both depend on the enumeration).
2. Region carve-up: value stack of `stack_slot_t`, control stack of
   `stack_frame_t`, from one caller-supplied block.
3. `static inline` push/pop/peek per slot kind; overflow checked on push.
4. `stack_frame_push`/`pop` with the callee's argument window overlapping the
   caller's evaluation stack (14 §6.2), and the return-value move in `pop`.
5. Root scanning of slots by `kind`, including `STACK_K_BYREF` bases.

**Done when:**

- frame-overlap arithmetic: after a push, `callee->args` aliases the caller's
  top *n* slots; after a pop, the caller's stack pointer is exactly
  `args + (returns ? 1 : 0)`;
- recursion to the configured depth succeeds and one frame deeper fails
  cleanly, with no write past the region;
- **GC precision**: a slot holding an `int32` whose bit pattern is a *valid
  live object id* is not traced, and the object it resembles is collected.
  This is the test that proves tagged slots do what 14 §6.1 claims;
- a byref slot keeps its base object alive.

**Risk:** conflating `STACK_K_NINT` and `STACK_K_I4`. They are distinct kinds
even though both are 32-bit (14 §6.1) — one test asserts they do not compare
equal through `value_compare` in OH-5.

---

## 7. OH-5 — Typed constructors

**Files:** `array.c/.h`, `obj_string.c/.h`, `value.c/.h`.

**Consumes:** the allocator (OH-2) and D9, decided 2026-09-28 in 14 §5.6.
The array layout is **decided** in 14 §5.7.

**Order (2026-09-28): built before OH-3 and OH-4**, at the owner's request.
Three pieces wait for the slice they depend on: `obj_string_intern` for D7
(OH-3), `value_compare` for `stack_slot_t` and B6's rules (OH-4), and the
test that the collector traces an `array_create` array (OH-3).

**Work:**

1. `array.c` — the API of 14 §8 over the layout of 14 §5.7, replacing
   Ruffus's `arrays_type.c`, whose cut-down version at `476cd6e` already has
   the element-wise range check and the overflow guard:
   - `array_create` rejects a `count × elem_size` overflow before allocating,
     sets `OBJECT_H_HASREFS` when the type's refmap is non-NULL, and in debug
     checks that the refmap covers exactly `elem_size` bytes;
   - `array_range_ptr` is the one range check, done in elements;
     `array_element_ptr` is a range of one, and `array_element_byref` gives
     `ldelema` the same check as `{id, offset}`;
   - `array_copy` is overlap-safe (`memmove`), same `elem_size` only, and every
     copied id goes through `objects_write_ref`;
   - `array_clear` zeroes a range; storing `OBJECT_NULL` needs no barrier.

   The 8-byte header lives in `objects_types.h` from OH-1, so the collector
   reads it in OH-3. **This deletes `array_shape` from the host struct**
   (14 §7.5), leaving three callbacks and one source of truth for an array's
   shape.
2. `obj_string.c` — UTF-8 bodies per 14 §5.6; `obj_string_create`
   *validates* its input and rejects overlong encodings, truncated sequences
   and surrogates; the intern table comes from the config. `obj_string_length`
   and `obj_string_char_at` count UTF-16 units (D9).
3. `value.c` — `value_box`/`value_unbox` with a size check; `value_compare`
   over slots, reimplementing Ruffus's mixed-type promotion table with no
   allocation (14 §6.6).

**Done when:** range tests, including index `0xFFFFFFFF`, a wrapping
`index + count` and a `count × elem_size` overflow at creation; zero-length
arrays; an overlapping `array_copy` in both directions; an element byref that
resolves to the same address as `array_element_ptr`, and has an `OBJECT_NULL`
id for an index out of range; an array made by `array_create` traced exactly
as OH-3's hand-built ones, and a `byte[]` created with `OBJECT_H_HASREFS`
clear; **an `int64[]` and a `double[]` round-trip with an alignment assertion
on every element address**; a UTF-8 validation table (valid / overlong /
truncated / surrogate / empty) with an expected verdict per row; box→unbox
round-trip for every primitive kind; a `value_compare` matrix over every
slot-kind pair.

**Built (2026-09-28, `92ff6ea`):** everything above except the tracing test
(OH-3), the `value_compare` matrix (OH-4) and interning (OH-3, D7). Two
mutants — an overlong three-byte form accepted, an off-by-one range check —
were each caught by the suite.

---

## 8. OH-6 — Hardening and instrumentation

Runs alongside OH-2 onward rather than after it. Layers 1–7 of the test system
are OH-T; this is layer 8 plus the two instruments doc 14 asks for.

1. **Sanitizers.** ASan/UBSan CI job — *with* the `HEAP_POISON` poisoning from
   OH-2, because a custom allocator inside one caller-supplied array is
   otherwise invisible to ASan and the job would prove nothing.
2. **Coverage.** gcov/lcov in CI, with `heap.c` and `gc.c` held to near-full
   line and branch coverage (§2.3). Their failure branches are reachable only
   through OH-T layer 5.
3. **32-bit `-Os` release build.** Not a second profile (§0.2) — the same
   configuration, release switches, a 32-bit target — to catch pointer-width
   assumptions and to record `.text`/`.bss`. A size regression is a design
   regression on this project.
4. **`heap_verify()` at every safepoint** under `HEAP_VERIFY_EVERY_SAFEPOINT`,
   so a corruption is attributed to the operation that caused it rather than
   the one that tripped over it. This is the switch a debug Ruffus build turns
   on (§2.3).
5. **Stats dump** helper producing the 14 §5.4 triple, so the deferred
   decisions in 14 §11 can actually be closed by measurement.

---

## 9. Decisions deferred to the slice that consumes them

Not P0: each is local to one module, and deciding it early means deciding it
without the code in front of you.

| # | Decision | Slice | Note |
|---|---|---|---|
| D5 | Rooting-scope API shape | OH-3 | fixed-depth array vs linked scopes |
| D6 | `gc_poll` and the safepoint contract | OH-3 | must agree with [17 §4.4](17_Threading_Model.md) |
| ~~D7~~ | ~~Statics slab and literal pool layout~~ | OH-3 | **decided 2026-09-28** — 14 §3.5; B3 settled: a `CONST` index selects a constant-table entry |
| ~~D9~~ | ~~String layout and interning~~ | OH-5 | **decided 2026-09-28** — 14 §5.6; interning goes with D7 |

D8, the array payload header, was on this list and is now resolved in 14 §5.7:
an 8-byte header, chosen in review.

---

## 10. Explicitly not in this plan

Tier 2 (frame scratch, promotion), the size-class recycler, compaction,
finalizers and weak references, the native descriptor space, and `cli_exec`
bring-up. Each is recorded in 14 §11 with the condition that would reopen it.
The v1 hooks that keep them cheap — `OBJECT_SPACE_FRAME` reserved,
`objects_write_ref` at every store, `OBJECT_H_PINNED` honoured — are in OH-1
and OH-4, not deferred.

---

## 11. Definition of done, per slice

1. Tests pass in debug, release **and** the standalone build.
2. `heap_verify()` clean where applicable.
3. Anything the code decided is folded back into
   [14_ObjectHandler.md](14_ObjectHandler.md) §11 — the contract stays the
   description of the code, not of an earlier intention.
4. The slice's row here is struck through with the commit that closed it.

---

## 12. Review record

**Settled in review (2026-09-18):**

| Point | Outcome |
|---|---|
| Header naming | No `oh_*` anywhere. `objects_types.h` for the shared vocabulary, `obj_handler_config.h` and `obj_handler.h` for the lifecycle, module headers keep their own names except `obj_string.h` (2026-09-25), because `strings.h` shadows `<strings.h>` — D3, and 14 §1.1 corrected |
| Profiles | **One configuration**, not a host one and an MCU one. The only axis is debug versus release — §0.2. This is what keeps the host build a rehearsal for the device |
| Test system | A full test system, its own P1 slice, layers 3–7 carrying the weight — §2 |
| Layer-1 framework | **None.** CTest supplies discovery, reporting, filtering and per-case process isolation; `harness.h` supplies the assertion macros and the failure dump. No vendored dependency — §2.2 |
| Free-list terminator | **Reserved 8-byte sentinel at payload offset 0**; `next == 0` becomes unambiguous and `offset == 0` becomes a usable "no payload" marker — §4 |
| `heap_release` | Kept, declared in `heap.h`, **called by the GC sweep** in production and directly by tests; absent from the umbrella header because an embedder never frees — §4 |
| OH-2 before OH-3 | **Settled**, no longer open: the allocator is fully exercisable without a collector, and `heap_release` is a real API with a real caller rather than a test hook |
| Array header | **Kept, and widened to 8 bytes** so `int64`/`double` elements stay naturally aligned; deletes `array_shape` from the host struct — now defined in 14 §5.7 |
| Heap ids | Confirmed: **handle-table indices from v1**, never payload offsets, with no later migration — D2 and 14 §3.2 |
| Debug object identity | **Approved: 8-bit space / 8-bit generation / 16-bit index in debug builds**, release unchanged at 16/16. Both mechanisms adopted — the generation check and the free-handle quarantine. Folded into the contract at 14 §2.1, §3.2, §3.4 and §11 |
| Init status (2026-09-25) | **`obj_handler_status_t`, returned by `obj_handler_init` only**: eight codes in OH-1, one per thing the embedder has to fix, appended per slice. Errors are returned before init and go to `on_corrupt` after it, so `objects_space_register` is `void`. `HEAP_MAX_PAYLOAD` is 65536 — D1, 14 §9.2 |
| Array model (2026-09-25) | **Kept, and cut back to what a CIL array is**: one-dimensional, inline elements, fixed length. The type's refmap describes one element; `array_range_ptr` is the only range check; the header moves to `objects_types.h`. Ruffus's list operations are dropped — 14 §5.7 |
| Location (2026-09-28) | **`source/object_handler/`, a new implementation**, not patches to the Ruffus copy. `obj_handler.h` moved there; the rest of `ObjectHandler/` was deleted, the originals staying in `ruffusvm/`. Tests in `tests/object_handler/`, CMake target `object_handler` — D3 |
| Pre-OH-1 decisions (2026-09-28) | [19](19_Session_Checklist.md) A2–A13 and D4, accepted as recommended. Recorded in 14 §§1, 1.1, 3.2, 3.4, 5.2, 7.4, 7.5, 9 and 9.2, and here in D1, D2, §3, §4 and §5 |
| Coding OH-1 and OH-2 (2026-09-28) | Debug switches default from `NDEBUG`, each overridable with `-D`. `heap_verify` returns 1 or 0 and reports the first fault: release checks order, ranges, accounting and coverage by sums, and `HEAP_DEBUG` adds an exact address walk through the guard headers. A free block that reaches the untouched top rejoins it. The standalone build is `source/object_handler/` configured on its own, which also adds the tests. The per-file recipe compiles each header through a one-line wrapper, since a macro-only header is an empty unit under `-pedantic` |
| OH-5 first, D9 (2026-09-28) | OH-5 is built before OH-3 and OH-4; interning, `value_compare` and the tracing test wait for their slices (§7). D9: a NUL-terminated UTF-8 payload, `OBJECT_H_ASCII`, `Length` and `obj_string_char_at` in UTF-16 units, `OBJECT_NULL` for invalid UTF-8, `string_type` in the config — 14 §5.6 |
| Statics and constants (D7, 2026-09-28) | Decided ahead of OH-3, at the owner's request, and built at once: both spaces configured by `obj_handler_init` with `E_STATICS` and `E_CONST`, constants accepted by every reader, `obj_string_intern` returning constant ids. Supersedes A6 — 14 §3.5 |
| Coding OH-5 (2026-09-28) | The element-refmap check in `array_create` sits under `OBJECT_DEBUG_REFS`. Reference arrays copy through the barrier element by element; everything else by `memmove`. Boxing tests live in `test_value.c`, which the §2.4 tree gains |

**Nothing remains open before OH-3:** A1–A13 are settled
([19 §4](19_Session_Checklist.md)). D5, D6, D7 and D9 (§9) are scheduled decisions, not
unresolved ones: each is taken at the start of the slice that consumes it,
with that slice's code in front of us.
