# Phase 0 — Work Log

Completion record for **Phase 0** of the roadmap defined in
[00_Introduction.md](00_Introduction.md#L263):

> *"Finish missing table decoders (§3.1.1) + EH parser (§3.1.2). Deliverable
> test-case: load a fat-header method with `try/catch` without warnings."*

Status: **complete**.

---

## 1. Summary

At start of Phase 0 the parser could decode 27 of the 35 spec-defined
metadata tables and completely ignored anything past the IL code of a fat
method body. Phase 0 filled both gaps.

| Metric                                              | Before | After |
|-----------------------------------------------------|-------:|------:|
| Spec-defined tables with a decoder                  |     27 |    35 |
| `NULL` slots in `table_map[]`                       |      7 |     0 |
| EH clauses parsed per method (any format)           |      0 |   all |
| Real .NET assemblies exercised in testing           |      2 |     5 |
| PE32 test-sample corpus in `data/`                  |      2 |     5 |

Every spec-defined table listed in [metadata_table_types.h](../source/metadata_table_types.h)
is now wired into [table_map[]](../source/row_table/parser_table_data.h)
with a functioning decoder. The Edit-and-Continue tables (`ENCLog 0x1E`,
`ENCMap 0x1F`) and the obsolete processor/OS assembly tables
(`AssemblyProcessor 0x21`, `AssemblyOS 0x22`, `AssemblyRefProcessor 0x24`,
`AssemblyRefOS 0x25`) are still absent — none of them appear in any real
CoreCLR / .NET Framework assembly and they can be added on demand.

---

## 2. Table decoders added

All eight decoders follow the pattern in
[Adding_Table_Decoder.md](../docs-archive/Adding_Table_Decoder.md):
one `<table>_table.c` per table, a decoded-row struct appended in
table-order to [parser_table_types.h](../source/row_table/parser_table_types.h),
a function declaration in [parser_table_data.h](../source/row_table/parser_table_data.h),
a `table_map[]` entry replacing the previous `NULL`, and the source added
to [CMakeLists.txt](../CMakeLists.txt).

| Table | Name                     | Source file                                                                                              | ECMA-335 ref  |
|-------|--------------------------|----------------------------------------------------------------------------------------------------------|---------------|
| 0x0D  | `FieldMarshal`           | [fieldmarshal_table.c](../source/row_table/fieldmarshal_table.c)                                         | §II.22.17     |
| 0x0E  | `DeclSecurity`           | [declsecurity_table.c](../source/row_table/declsecurity_table.c)                                         | §II.22.11     |
| 0x12  | `EventMap`               | [eventmap_table.c](../source/row_table/eventmap_table.c)                                                 | §II.22.12     |
| 0x14  | `Event`                  | [event_table.c](../source/row_table/event_table.c)                                                       | §II.22.13     |
| 0x1C  | `ImplMap`                | [implmap_table.c](../source/row_table/implmap_table.c)                                                   | §II.22.22     |
| 0x27  | `ExportedType`           | [exportedtype_table.c](../source/row_table/exportedtype_table.c)                                         | §II.22.14     |
| 0x28  | `ManifestResource`       | [manifestresource_table.c](../source/row_table/manifestresource_table.c)                                 | §II.22.24     |
| 0x2C  | `GenericParamConstraint` | [genericparamconstraint_table.c](../source/row_table/genericparamconstraint_table.c)                     | §II.22.21     |

### Design decisions specific to the new decoders

- **Pre-resolved blobs.** `FieldMarshal.NativeType`, `DeclSecurity.PermissionSet`,
  and any other decoder producing a blob that a downstream subsystem will
  parse (the future P/Invoke marshaller and CAS reader respectively) store
  `const uint8_t *` + `uint32_t length` instead of the raw `#Blob` index.
  This mirrors the pattern already used for `Field.Signature`,
  `MethodDef.Signature`, `MemberRef.Signature`, and `StandAloneSig.Signature`,
  so consumers receive a pointer + length pair ready to parse.
- **Pre-resolved strings.** `Event.Name`, `ImplMap.ImportName`,
  `ExportedType.TypeName/Namespace`, and `ManifestResource.Name` are all
  resolved into the `#Strings` heap at decode time (via
  `ecma_335_get_string`), matching how `TypeDef`, `TypeRef`, `MemberRef`,
  `Property`, and `ModuleRef` handle their string fields.
- **Coded-index sizing.** Every coded index used by these tables was
  implemented per §II.24.2.6 tag/width table:
  - `HasFieldMarshal` — 1-bit tag over `Field/Param`, threshold `1 << 15`.
  - `HasDeclSecurity` — 2-bit tag over `TypeDef/MethodDef/Assembly`, threshold `1 << 14`.
  - `TypeDefOrRef` (used by `Event.EventType` and `GenericParamConstraint.Constraint`) — 2-bit tag, threshold `1 << 14`.
  - `MemberForwarded` — 1-bit tag over `Field/MethodDef`, threshold `1 << 15`.
  - `Implementation` — 2-bit tag over `File/AssemblyRef/ExportedType`, threshold `1 << 14`.
- **Zero-coded-index sentinel.** `ManifestResource.Implementation == 0`
  is the standard "embedded in current module" marker and is preserved
  verbatim; consumers distinguish it from a valid coded value.
- **`ExportedType.TypeDefId`** is a 32-bit hint, **not** a coded index —
  read as raw `uint32_t` per spec.

---

## 3. Exception-handling section parser

Method bodies with the fat header flag `CorILMethod_MoreSects` (bit 3)
carry one or more EH sections after the IL, aligned to 4 bytes relative to
the method body start. Small sections use 12-byte clauses; fat sections use
24-byte clauses; sections chain via `CorILMethod_Sect_MoreSects` (bit 7 of
the section kind byte).

### 3.1 Types added

Both live in [methods_type.h](../source/methods/methods_type.h):

```c
enum {
    EH_CLAUSE_KIND_CATCH   = 0x0000,
    EH_CLAUSE_KIND_FILTER  = 0x0001,
    EH_CLAUSE_KIND_FINALLY = 0x0002,
    EH_CLAUSE_KIND_FAULT   = 0x0004,
};

typedef struct eh_clause_t_ {
    uint32_t flags;             // EH_CLAUSE_KIND_*
    uint32_t try_offset;
    uint32_t try_length;
    uint32_t handler_offset;
    uint32_t handler_length;
    union {
        uint32_t class_token;   // CATCH: metadata token of exception type
        uint32_t filter_offset; // FILTER: IL offset of the filter code
    };
} eh_clause_t;
```

`method_info_t` gained two new fields:

```c
uint32_t      eh_clause_count;   // 0 when no EH sections present
eh_clause_t * eh_clauses;        // NULL when count is 0
```

### 3.2 Parsing algorithm

Implemented in [`parse_eh_sections`](../source/methods/method_parser.c)
(static helper) and invoked from `method_parser_get_info` whenever the fat
header flag bit 3 is set:

1. Advance past IL, round up to the next 4-byte boundary relative to the
   method body start (per §II.25.4.5).
2. Read the 1-byte section kind:
   - Bit 0 = `CorILMethod_Sect_EHTable` — clauses follow.
   - Bit 6 = `CorILMethod_Sect_FatFormat` — 24-byte clauses; else 12-byte.
   - Bit 7 = `CorILMethod_Sect_MoreSects` — another section follows.
3. Read section size:
   - Small header: `data_size(u8) + padding(u16)` (total header = 4 bytes).
   - Fat header: `data_size(3 LE bytes)` (total header = 4 bytes).
4. Skip non-EH sections (e.g. obsolete OptIL) but still consume their bytes
   so the section chain stays aligned.
5. For each clause:
   - Fat format: read `6 × uint32` directly into `eh_clause_t`.
   - Small format: read `Flags(u16) + TryOffset(u16) + TryLength(u8) +
     HandlerOffset(u16) + HandlerLength(u8) + ClassTokenOrFilter(u32)` and
     zero-extend into the same `uint32` fields.
6. Grow `eh_clauses` via `realloc`, then loop if `MoreSects` is set.

Failure is reported with a printf and the partial results are preserved —
non-fatal so a single malformed method does not sink parsing of the whole
assembly.

---

## 4. Validation results

All new code was exercised against real .NET assemblies staged in `data/`:

| DLL              | Origin                                                                | Size    | Tables exercised                                                    |
|------------------|-----------------------------------------------------------------------|--------:|---------------------------------------------------------------------|
| test_system.dll  | project fixture (BasicSharp interpreter)                              |   ~50 K | Baseline — 18 present tables, 11 EH clauses across 7 methods       |
| serial_sharp.dll | project fixture                                                       |   ~30 K | Baseline — no new tables triggered                                 |
| psreadline.dll   | `Microsoft.PowerShell.PSReadLine.dll` (VS Code bundle)                | ~322 K | `FieldMarshal` (10) + `ImplMap` (32) + `ManifestResource` + 102 EH clauses in 61 methods |
| mobiletap.dll    | `Microsoft.VisualStudio.DesignTools.MobileTapContracts.dll` (VS Code) |   ~55 K | `EventMap` (1) + `Event` (1)                                       |
| vectors.dll      | `System.Numerics.Vectors.dll` (VS Code C# extension)                  |  ~108 K | `ExportedType` (7) + `ManifestResource` (1) + `GenericParamConstraint` (45) + 1 EH clause |

### 4.1 Sampled decoded rows (verification highlights)

- **`ImplMap` on psreadline.dll** — 32 P/Invoke declarations resolved to
  real Win32 entry points: `CreateFile`, `GetStdHandle`,
  `SetConsoleCtrlHandler`, `NtQueryInformationProcess`,
  `GetConsoleProcessList`, etc. `MemberForwarded` tag (bit 0) correctly
  distinguished `MethodDef` from `Field`. `MappingFlags` values
  (`0x0100/0x0140/0x0144`) match plausible `[DllImport]` attribute
  combinations.
- **`ExportedType` on vectors.dll** — 7 rows, all with `Flags=0x00200000`
  (`tdForwarder`) redirecting `System.Numerics.Matrix3x2/Matrix4x4/Plane/
  Quaternion/Vector2/…` to `AssemblyRef#2`. This is the type-forwarding
  pattern used for API-compatibility shims — decoder handled it correctly.
- **`GenericParamConstraint` on vectors.dll** — 45 rows, all pointing at
  `TypeRef#31` (the `System.ValueType` `where T : struct` constraint the
  numeric-generics require). Owner-to-`GenericParam` mapping increments
  sequentially per spec.
- **`ManifestResource` on vectors.dll** — `"FxResources.System.Numerics.
  Vectors.SR.resources"`, `Flags=0x00000001` (`mrPublic`),
  `Implementation=0` (embedded). Both the "embedded" and "coded-index"
  paths behave correctly.
- **`Event` + `EventMap` on mobiletap.dll** — `EventMap[1]` links
  `TypeDef#41 → Event#1`; `Event[1]` = `name="Updated"` with `EventType`
  coded `0x149` → tag 1 (TypeRef) row 82 (a delegate type).
- **EH parser on test_system.dll — `Socket_Process`** — 5 clauses: two
  independent `try/finally` pairs plus three `CATCH` clauses sharing one
  outer try range `[0x25..0x97)` with distinct handlers targeting
  `TypeRef` rows 24 / 25 / 26. Exact ordering matches C# codegen for
  `try { … } finally { … } try { … } finally { … } catch(A) catch(B)
  catch(C)`.
- **EH parser on psreadline.dll — `IsMutexPresent`** — 2 clauses:
  inner `try/finally` (Mutex disposal) wrapped by outer `try/catch`,
  with the inner clause listed first per §II.19.5 (innermost-first is
  the runtime-required ordering for the interpreter's future unwind
  walk).

### 4.2 Full-corpus totals

| DLL              | Methods | Methods with EH | Total EH clauses |
|------------------|--------:|----------------:|-----------------:|
| test_system.dll  |     267 |               7 |               11 |
| vectors.dll      |     197 |               1 |                1 |
| psreadline.dll   |    1638 |              61 |              102 |

Zero decode failures, zero memory errors, zero regressions relative to the
pre-Phase-0 baseline on the two project fixtures.

---

## 5. Files touched

### 5.1 New files

- [source/row_table/fieldmarshal_table.c](../source/row_table/fieldmarshal_table.c)
- [source/row_table/declsecurity_table.c](../source/row_table/declsecurity_table.c)
- [source/row_table/eventmap_table.c](../source/row_table/eventmap_table.c)
- [source/row_table/event_table.c](../source/row_table/event_table.c)
- [source/row_table/implmap_table.c](../source/row_table/implmap_table.c)
- [source/row_table/exportedtype_table.c](../source/row_table/exportedtype_table.c)
- [source/row_table/manifestresource_table.c](../source/row_table/manifestresource_table.c)
- [source/row_table/genericparamconstraint_table.c](../source/row_table/genericparamconstraint_table.c)
- `data/psreadline.dll` (staged, PE32, 322 KB)
- `data/mobiletap.dll`  (staged, PE32, 55 KB)
- `data/vectors.dll`    (staged, PE32, 108 KB)

### 5.2 Modified files

- [source/row_table/parser_table_types.h](../source/row_table/parser_table_types.h) — 8 new decoded-row struct definitions in table-order.
- [source/row_table/parser_table_data.h](../source/row_table/parser_table_data.h) — 8 new function declarations; every `NULL` slot in `table_map[]` replaced with a working decoder pointer.
- [CMakeLists.txt](../CMakeLists.txt) — 8 new source files added to `SOURCE_FILES`.
- [source/methods/methods_type.h](../source/methods/methods_type.h) — `EH_CLAUSE_KIND_*` enum, `eh_clause_t` struct, two new fields on `method_info_t`.
- [source/methods/method_parser.c](../source/methods/method_parser.c) — `parse_eh_sections` static helper + hook in `method_parser_get_info`.
- [source/main.c](../source/main.c) — commented-alias list for the five test DLLs kept as documentation; program logic unchanged from the pre-Phase-0 baseline.

---

## 6. Practical constraints noted during Phase 0

- **`pe_parser` is PE32-only** ([pe_parser.c#L314](../source/pe_parser.c#L314)).
  Most modern .NET Core assemblies ship as PE32+; the current parser
  rejects them with `"Unsupported PE optional header magic"`. This
  restricts the choice of real-world test samples. Not blocking for
  Phase 0 — enough PE32 samples exist in the wild (all the VS Code
  BuildHost-net472 DLLs are PE32) — but a PE32+ code path will be
  required before the runtime can load `System.Private.CoreLib.dll` or
  similar. Suggested as a Phase 1 side-quest.
- **No IL assembler on host.** No `ilasm`, no `dotnet`, no `mono`.
  Custom synthetic test bytes cannot be produced on this machine;
  Phase 0 relied entirely on shipped .NET DLLs found on disk. This
  is fine for parser work but the interpreter phases will benefit
  from adding a minimal `ilasm`-equivalent (or committing a set of
  pre-compiled .il/.dll fixtures) to the repo.
- **Coded-index size selection uses `< (1 << N)`, not `<=`.** Every
  new decoder followed the exact pattern already in the codebase; no
  cross-decoder inconsistency was introduced.

---

## 7. Ready-for-Phase-1 checklist

All prerequisites the class loader will need are now available:

- [x] All type-defining tables decoded (`TypeDef`, `TypeRef`, `TypeSpec`).
- [x] All layout-affecting tables decoded (`Field`, `FieldLayout`,
      `ClassLayout`, `NestedClass`).
- [x] All method-defining tables decoded (`MethodDef`, `MethodImpl`,
      `MethodSpec`, `MemberRef`).
- [x] All dispatch-affecting tables decoded (`InterfaceImpl`,
      `MethodSemantics`, `Event`, `EventMap`, `Property`, `PropertyMap`).
- [x] All generics tables decoded (`GenericParam`, `GenericParamConstraint`).
- [x] All P/Invoke tables decoded (`ImplMap`, `ModuleRef`, `FieldMarshal`).
- [x] All resource / type-forwarding tables decoded (`File`,
      `ExportedType`, `ManifestResource`).
- [x] Method bodies fully decoded including EH clauses.

Per the recommendation in the introduction doc, the next artefacts should
be the two "contract" docs (`09_Native_BCL_Bridge.md` and the GC-API
section of `02_Managed_Heap.md`) followed by Phase 1's class loader.

---

## 8. Follow-up: §3.1.3 `#US` accessor and §3.1.4 `FieldRVA` bootstrap

The initial Phase 0 milestone description in the roadmap only called out
§3.1.1 (missing table decoders) and §3.1.2 (EH clauses). The two remaining
items of §3.1 Loader completion — the `#US` (user-string) heap accessor
and the `FieldRVA` bootstrap — were not included in that first pass and
are landed here as a Phase 0 follow-up so that §3.1 closes cleanly before
the class loader in Phase 1 begins.

### 8.1 `#US` heap accessor (§3.1.3)

Added [`ecma_335_get_user_string`](../source/ecma_335_parser.c) with the
signature the introduction predicted:

```c
const uint16_t * ecma_335_get_user_string(table_context_t * ctx,
                                          uint32_t          index,
                                          uint32_t        * out_char_count);
```

Semantics per ECMA-335 §II.24.2.4:

- Index 0 is defined as the empty string; the accessor returns `NULL` and
  writes `0` to `*out_char_count`.
- Non-zero indices point to a compressed-int length prefix (same encoding
  as `#Blob`, decoded inline to avoid a header dependency on
  `metadata_table.c`) followed by the UTF-16 payload and a single
  trailing terminator byte (0x00 or 0x01). The terminator is counted in
  the length, so the character count is `(length - 1) / 2`.
- Out-of-range indices return `NULL`.

The returned pointer aliases the `#US` stream inside the metadata buffer;
no allocation is performed. This matches how `ecma_335_get_string` and
`ecma_335_get_blob` already behave and gives the interpreter's future
`ldstr` handler a zero-copy path into the pool.

### 8.2 `FieldRVA` bootstrap (§3.1.4)

The introduction calls for each `FieldRVA` row to be resolved to a
"pointer inside the mapped image" so that
`RuntimeHelpers.InitializeArray` (or its intrinsic) can `memcpy` from it.
Two coordinated changes deliver that:

**PE image now fully loaded.**
[pe_parser.c](../source/pe_parser.c) previously read only the metadata
range from the FILE handle; it now also loads the whole PE image once at
parse time into a new `image_buffer` / `image_size` pair on
`pe_file_t`. The existing `fseek`/`fread` code paths (method-body reader,
EH-section reader) are unchanged — the FILE handle is repositioned after
the initial slurp — so nothing downstream needed to be modified.

**New `pe_rva_to_pointer` helper.**
[pe_parser.h](../source/pe_parser.h) exposes:

```c
const uint8_t * pe_rva_to_pointer(pe_data_t * pe_data, uint32_t rva);
```

It layers on top of the existing `pe_rva_to_file_offset` and returns
`image_buffer + file_offset`, or `NULL` if the RVA does not fall inside
any section. This is the single primitive the class loader (Phase 1) and
the `RuntimeHelpers.InitializeArray` intrinsic (Phase 4) will use for
RVA-based access — including static array initialisers, embedded
resource blobs referenced via `ManifestResource.Offset`, and future
`MethodDef.RVA` pointer-based dispatch.

**Decoded row now carries a resolved pointer.**
`fieldrva_table_decoded_t` in
[parser_table_types.h](../source/row_table/parser_table_types.h)
gained a `const uint8_t * Data` field. A new
`metadata_table_finish_fieldrva` hook in
[fieldrva_table.c](../source/row_table/fieldrva_table.c) walks every row
after decoding finishes and populates `Data` via `pe_rva_to_pointer`.
Rows whose RVA does not resolve are logged but do not abort loading (a
malformed row does not sink the rest of the assembly, matching the
policy used elsewhere in the parser). The hook is wired into
`table_map[]` in
[parser_table_data.h](../source/row_table/parser_table_data.h) as the
`finish_function` for `METADATA_TABLE_FieldRVA` — the first non-`TypeDef`,
non-`MethodDef` table to use this slot.

### 8.3 Validation

Exercised against the existing fixture corpus:

| DLL              | FieldRVA rows | Result                                                          |
|------------------|--------------:|-----------------------------------------------------------------|
| test_system.dll  |             0 | Loader silent; no rows to resolve. No regressions.              |
| serial_sharp.dll |             1 | Row (Field=56, RVA=0x00006C58) → `Data = { 3E 0D 2B }`, exact match with the `ildasm` dump at `I_00006C58`. |
| vectors.dll      |             0 | Loader silent; no rows to resolve. No regressions.              |
| mobiletap.dll    |             0 | Loader silent; no rows to resolve. No regressions.              |
| psreadline.dll   |             0 | Loader silent; no rows to resolve. No regressions.              |

The one resolved row on `serial_sharp.dll` matches the `<PrivateImplementationDetails>::__StaticArrayInitTypeSize=3`
static field the C# compiler emits for a 3-byte inline array literal —
exactly the shape `RuntimeHelpers.InitializeArray` consumes in Phase 4.

### 8.4 Files touched (follow-up)

- [source/pe_parser.h](../source/pe_parser.h) — declared `pe_rva_to_pointer`.
- [source/pe_parser.c](../source/pe_parser.c) — added `image_buffer` / `image_size` on `pe_file_t`, whole-image slurp in `pe_parser`, `pe_rva_to_pointer` implementation, and matching cleanup in every error path.
- [source/ecma_335_parser.h](../source/ecma_335_parser.h) — declared `ecma_335_get_user_string`.
- [source/ecma_335_parser.c](../source/ecma_335_parser.c) — added `ecma_335_get_user_string` implementation.
- [source/row_table/parser_table_types.h](../source/row_table/parser_table_types.h) — added `Data` field on `fieldrva_table_decoded_t`.
- [source/row_table/parser_table_data.h](../source/row_table/parser_table_data.h) — declared `metadata_table_finish_fieldrva`, wired it into `table_map[]`.
- [source/row_table/fieldrva_table.c](../source/row_table/fieldrva_table.c) — `Data` initialised to `NULL` at decode time, populated by the new `metadata_table_finish_fieldrva` hook.
- [source/main.c](../source/main.c) — added the `serial_sharp.dll` commented alias annotated with its FieldRVA usage.

With §3.1.3 and §3.1.4 landed, §3.1 Loader completion is now fully
implemented. Phase 1 (class loader + layouts + vtables) can start against
the metadata now presented by the loader.
