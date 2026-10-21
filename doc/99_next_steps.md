Updated todo list

I’ll compare the project’s plans with the executor code and tests, then summarize what works, what remains incomplete, and the next priorities.


Read 

Read , lines 1 to 250

Read , lines 1 to 260

Read memory

The checklist describes an executor-first prototype, not an end-to-end VM. I’ll check that against the current code, especially call handling, stack frames, and test coverage.


Searched for files matching `**/{AGENTS.md,copilot-instructions.md,*.instructions.md}`, no matches

Read , lines 250 to 844

Read 

Read 

The executor has a useful core: integer calculations, locals and arguments, control flow, and iterative managed calls. Its `call` operands are already pack-time rewritten method IDs, so I’ll also check how much of the assembly-to-runtime pipeline exists to feed it.


Read 

Read 

Read 

Read , lines 1 to 340

Read 

The parser and executor are separate components today; there is no implemented packer or image loader connecting them. I’ll check the test matrix and recent commits next, then run host checks to verify this checkout’s state.




Ran terminal command:  git -C /home/wifi/projects_dev/il_executor_vm status --short --branch

Ran terminal command:  git -C /home/wifi/projects_dev/il_executor_vm --no-pager log -8 --format='%h %ad %s' --date=short

Searched for regex `^TEST_CASE\(|^##|^###|\[ \]` (`**/{test_executor.c,20_VXB_Build_Plan.md}`), 160 results

Read 

The executor tests cover more than happy paths: they check malformed instructions, failed-call state preservation, managed recursion limits, and root visibility. I’ll try the configured CMake build now, then compare those results with the documented verification record.


Build with the CMake Tools extension
Completed with input: {}
Tool execution failed: Build failed: Unable to configure the project

VS Code’s CMake integration could not configure the project, so I don’t yet have a fresh test result. I’ll check its diagnostics while reading the remaining roadmap and runtime integration code.


Read , lines 669 to 1208

Read , lines 260 to 560

Read 

Read , lines 1 to 280

Get CMake diagnostics
Completed with input: {}

The code and records agree on the main boundary: this is a tested interpreter core fed by in-memory fixtures, not yet a runnable .NET application pipeline. Typed storage and comparisons support several numeric kinds, but arithmetic and returns are still limited to `int32`; GC is explicitly a stub.


Read , lines 1 to 320

Read 

Read 

Read , lines 1 to 155

List tests with the CMake Tools extension
Completed with input: {}

Created 2 todos

A small compiled program packed into an image and run by the existing scalar executor looks like the most useful next milestone, and it would not require GC. I’m making a final check of reference decoding and frame handling for issues that could block that path.


Searched for regex `executor_start_program|executor_start\(|vxb|gc_poll|METADATA_CODEDINDEX_MemberRefParent|MemberRefParent` (`**/source/**`), 8 results

Read 

Read , lines 270 to 355

Read 

I found one concrete parser issue to fix before token resolution: `MemberRefParent` is read as a three-bit tagged index, but the helper interprets it with a two-bit mask and shift. I’ll include that alongside the broader integration gaps and distinguish recorded test results from checks I could run today.


Searched for regex `static int executor_boundary|static int executor_method_valid|static executor_status_t executor_call|static executor_status_t executor_return|case IL_OP_ADD:|executor_status_t executor_run|Unsupported PE optional header|ClassIndex &|ClassIndex >>|All \*\*113|## 5\. Build blocks|void gc_collect|#define FILENAME_CLR|optional_header_magic|0x10[Bb]|0x20[Bb]` (`**/{executor.c,pe_parser.c,member_ref_parser.c,main.c,gc.c,20_VXB_Build_Plan.md}`), 21 results

## Project Stage

**This is an early runtime prototype: beyond a metadata parser, but not yet an end-to-end .NET VM.** The foundations are substantial; the main missing piece is connecting parsed assemblies to executable images.

| Component | Current State |
|---|---|
| Parser | Broad metadata, signature, method-body and exception-table decoding implemented. PE32-only; the executable still uses a hardcoded input path. |
| ObjectHandler | Handles, heap allocation, typed stacks, frames, arrays, strings, boxing and explicit deletion implemented. Automatic GC remains a stub. |
| IL executor | Working scalar interpreter with arguments, locals, control flow and direct managed calls. |
| Application pipeline | Resolution, reachability, packing, VXB serialization/loading and the finished `cli_exec`/`cli_dump` tools remain unimplemented. |

## IL Executor

**It is a well-structured, correctness-first core, but deliberately narrow.**

Implemented capabilities include:
- `int32` constants, wrapping `add/sub/mul/neg`, and stack operations.
- Typed argument/local storage, including narrow integers, floating-point storage, references and byrefs.
- Conditional/unconditional branches, comparisons, switches and loops.
- Direct managed calls, nested calls and recursion without recursive C interpreter invocations.
- Single stepping, bounded execution and resumable state.

The defensive behavior is a strength: instructions check bounds, operand kinds and capacity before changing state. Failure tests check preservation of the failing instruction, operands and frames.

Important limits:
- Arithmetic and return values remain **`int32`/void**, despite broader storage and comparison support.
- No indirect loads/stores, object/field/array instructions, virtual/native dispatch or managed exception unwinding.
- Instruction budgets provide resumability, not a scheduler or completed safepoint system.
- Branch-boundary validation rescans code. That is reasonable for bring-up, but should eventually move into image validation/precomputed data.

The latest verification record reports **113 executor cases passing in sanitized Debug and Release**, plus host/Cortex-M4 compilation checks.

## Recommended Next Steps

**Make the next project milestone a real compiled scalar program running through the complete pipeline.** The existing executor already supports enough for a small program containing locals, a loop and nested calls.

1. **Define the minimal image contract and supported profile.** Start with the existing executor capabilities and explicitly reject unsupported features.
2. **Implement resolution, reachability and deterministic method IDs.** One concrete prerequisite: the MemberRef parent helper incorrectly uses a two-bit mask/shift for a three-bit tagged index. Fix it and add regression coverage.
3. **Build the scalar packing path.** Produce method/type tables, rewrite metadata-token calls to packed IDs, and relocate branches/switch targets when instruction sizes change.
4. **Implement the shared writer/reader, dump tool and executor launcher.** Prove a compiled assembly produces a deterministic image and the expected execution result. These are the planned VXB-1 through VXB-4 blocks.
5. **Expand runtime capabilities in tested slices.** For executor-only continuation, typed `ldind.*`/`stind.*` is a natural next family because byrefs already exist. Then widen scalar operations/returns, followed by objects, fields, statics and arrays. GC becomes essential before claiming sustained allocation-heavy execution.

Also make the build and fixtures reproducible through presets/CI. I could not independently rerun the tests today: VS Code’s CMake integration failed to configure the workspace and discovered no tests. The passing counts above are recorded results, not a fresh run.

Starting: *Identify next steps for IL executor project* (2/2)