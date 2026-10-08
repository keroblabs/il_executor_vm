# VXB datasets and build plan

Planning document. Records what a `.vxb` image must supply to `cli_exec`,
what the existing parser supplies to `cli_pack`, and the blocks needed to
connect them. Introduced at the owner's request on **2026-10-07**.

This is a dataset inventory and implementation plan with **proposed block,
category and item headers**, added at the owner's request on **2026-10-08**.
It is not yet the binary-format contract. `12_VXB_Format.md`, still to be
written, will consolidate the agreed layout before implementation. The
structures below are development proposals, not a frozen format.

**Development policy:** before version 1, a layout change replaces the
previous layout end to end: packer, reader, executor, dump, fixtures and
images change together. Old draft headers are immediately deprecated;
there are no per-header versions, compatibility readers or migrations.
Regenerate old images. Begin formal header/format version and compatibility
control when **version 1** is frozen. Size fields below describe traversal
and bounds, not a draft revision; they cannot detect every stale image.

Read with [00_Introduction.md](00_Introduction.md),
[14_ObjectHandler.md](14_ObjectHandler.md),
[15_Pack_Time_IL_Transforms.md](15_Pack_Time_IL_Transforms.md),
[16_Native_Call_Protocol.md](16_Native_Call_Protocol.md) and
[17_Threading_Model.md](17_Threading_Model.md).
[19_Session_Checklist.md](19_Session_Checklist.md) records current progress.

**State:** VXB-1 through VXB-8 remain planned, not implemented. The
executor-only scalar, argument/local, branch/comparison and direct managed
call slices in sections 8-11 are built and tested. GC remains deferred;
this plan does not make it an ObjectHandler completion gate.

---

## 1. Boundary and scope

The image is a precomputed runtime representation, not a copy of the PE
file or its metadata heaps. The executor does not resolve ECMA tokens,
search assemblies, decode signatures or compute layouts.

```text
Parsed assemblies -> resolved runtime datasets -> VXB writer
                                                   |
                                                   v
Test fixtures -> runtime views <- VXB reader -> cli_dump
                     |
                     v
                 IL executor -> ObjectHandler
```

Tests can supply code and runtime descriptors directly before a file exists.
The eventual reader supplies equivalent information from image offsets.
An in-memory descriptor containing pointers is not an on-disk record.

The initial image supports a declared scalar subset. Other datasets can be
empty when unused, subject to the format's required-section rules. Required
features outside the supported profile cause a packing or loading failure,
not silent omission. Extend the profile as blocks land.

## 2. Constraints inherited from existing contracts

| Constraint | Consequence for packing |
|---|---|
| One application, one image, one OS thread; managed threads are green threads | Resolve the application and its required dependencies into one image; scheduling state is runtime-owned. |
| Managed calls never recurse through the C interpreter | Method records supply the information needed to push and pop ObjectHandler frames. |
| `native int` is 32-bit on host and MCU | Record/check the capability; never embed a host pointer in a managed integer. |
| ObjectHandler knows no CIL or general VXB container layout | The VM interprets descriptors and provides type refmaps, regions and callbacks through the existing API. |
| Object ids have different debug/release encodings | Store constant indices and fix-up descriptions, not prebuilt `object_id_t` values or heap handles. |
| Constants use `object_const_t` and immutable, id-free payloads | Preserve the existing 12-byte entry contract, reserved entry zero when present, and 8-byte payload alignment (14 section 3.5). |
| Statics occupy one flat space | Reserve bytes 0-7, pack initialization information and a per-word reference map (14 section 3.5). |
| Locals and arguments hold their declared storage form | Preserve storage types as well as stack kinds; loads widen and stores/calls narrow (14 section 6.2). |
| Reference stores use the barrier; pointers do not survive safepoints | Emit enough type information for correct stores and rooting; GC deferral does not remove these requirements. |

ObjectHandler's contract takes precedence over older memory-model summaries.
Open transform issues B7 and B9 in doc 19 must be settled before the affected
conversion/storage transforms are implemented; 32-bit native integers do
not make every `conv.i` or `conv.u` redundant.

## 3. Necessary categories and datasets

The proposed block has a named offset for each category. A category holds
sized items; an item contains its fixed header followed by its variable
data and padding. This is the starting physical organisation, not a frozen
layout. Type/field and other mixed categories distinguish item kinds.

### Block, category and item framing

```c
#include <stdint.h>

typedef struct {
  uint8_t magic[4];
  uint32_t header_size;
  uint32_t block_size;
  uint32_t required_capabilities;
  uint32_t native_abi_hash;
  uint32_t bytecode_offset;
  uint32_t constants_offset;
  uint32_t types_fields_offset;
  uint32_t methods_offset;
  uint32_t statics_offset;
  uint32_t dispatch_offset;
  uint32_t natives_offset;
  uint32_t gc_refs_offset;
  uint32_t exceptions_offset;
  uint32_t entry_point_offset;
} vxb_block_header_t;

typedef struct {
  uint32_t header_size;
  uint32_t category_size;
  uint32_t item_count;
  uint32_t items_offset;
  uint32_t lookup_count;
  uint32_t lookups_offset;
  uint16_t category_kind;
  uint16_t flags;
} vxb_category_header_t;

typedef struct {
  uint32_t item_size;
  uint16_t header_size;
  uint16_t item_kind;
} vxb_item_header_t;

typedef struct {
  vxb_item_header_t item;
  uint32_t index_count;
  uint32_t offsets_offset;
  uint16_t target_kind;
  uint16_t reserved;
} vxb_lookup_header_t;

typedef struct {
  uint16_t type_id;
  uint8_t element_type;
  uint8_t stack_kind;
  uint32_t storage_size;
  uint16_t slot_count;
  uint16_t flags;
} vxb_storage_type_t;
```

| Field or rule | Proposed meaning |
|---|---|
| `magic` | Four bytes `V`, `X`, `B`, NUL; not a version identifier. |
| `header_size` | Encoded fixed-header bytes, including the common prefix where present; validate against the current layout, not an old layout catalogue. |
| `block_size` | Entire block, including its header, categories and padding. The next block can start at `block_start + block_size`; the enclosing file decides whether another block exists. |
| Category offsets | Offsets from this block's start, not from the file's start. Zero means absent; moving a block inside a file changes none of them. |
| `category_size` | Entire category, including its header, lookups, items and padding. Categories need not be contiguous or occur in directory order. |
| `item_count`, `items_offset` | Number and first offset of top-level items. Each item's size gives the next top-level item; nested records have their own count/offset. |
| `item_size` | Entire item, including fixed header, owned variable data, nested records and trailing padding. `next_item_offset = current_item_offset + item_size`. |
| Lookups | One sized lookup record per required item kind. Its `uint32_t` offset array maps a dense ID/index directly to an item header; zero entries mean missing. Mixed categories have separate type and field lookups, for example. |
| Offsets within headers | Block-relative unless explicitly named/described as an IL offset, RAM offset, instance/value offset, or existing constant-descriptor offset. |
| Alignment and sizes | Propose 8-byte-aligned block/category/item starts, with padding included in the enclosing size. Counts and sizes exclude no hidden bytes; payload byte sizes exclude padding. |
| Encoding | Propose explicit little-endian field encoding, consistent with the existing constant table. No pointers, compiler padding, `size_t`, C bitfields or raw `fwrite(struct)` serialization. |
| Validation | Reject zero/undersized item spans, overflow, out-of-container offsets, inconsistent counts, overlapping top-level spans and invalid IDs/kinds. Nested data stays inside its owning item. |

`item_kind`, category flags, storage flags and capability values need a
shared enumeration before implementation. Method/type/field IDs are
proposed as nonzero, with zero for absent optional links. Native binding
indices remain zero-based; constant index zero remains reserved. Existing
16-bit type IDs, frame limits and doc 15's method-index limit still apply;
the packer rejects values that do not fit.

`vxb_storage_type_t` records declared storage, not only evaluation-stack
kind. `element_type` comes from the decoded ECMA signature, `type_id` names
its resolved concrete type when needed, and `storage_size`/`slot_count` are
computed by the packer. A primitive can use type ID zero with its explicit
element type. Byref/array/value-type descriptions refer to concrete runtime
types; unsupported signature shapes are rejected, not guessed.

Each **Header** below uses the same item prefix. Ordinary categories use
`vxb_category_header_t`; constants and natives extend it to preserve their
existing directly indexed runtime tables. Fixed table entries and storage
descriptors are payload elements, not independently traversed VXB items.

### 3.1 IL bytecode

**Source:** each method's `il_code` and `code_size`, plus its decoded signature
and body information in
[methods_type.h](../source/methods/methods_type.h).

**Header:** one item per managed method body.

```c
typedef struct {
  vxb_item_header_t item;
  uint16_t method_id;
  uint16_t code_flags;
  uint32_t code_offset;
  uint32_t code_size;
} vxb_bytecode_header_t;
```

`code_offset` points to the first opcode; `code_size` excludes the VXB
header and padding. The item owns those bytes. `method_id` links to the
method record; `code_flags` describes required packed-code features, not
tiny/fat PE header format. Frame and signature information belongs in 3.4.

**Pack:** a code region and an offset/length for each managed method.
Arithmetic, stack operations, argument/local indices and branch operands
retain CIL semantics. Numeric immediates such as `ldc.i4 123` remain inline;
they are not automatically constant-pool entries.

Resolve metadata-token operands to image-wide identifiers or another
explicitly agreed precomputed form. Operand widths and prefix resolution
must agree with doc 15 and the executor. If an instruction changes size,
remap branches, `switch` targets and exception boundaries. Validate targets
as instruction boundaries. Optional fusion/optimisation is not needed to
bring up the first image.

### 3.2 Constants and read-only data

**Source:** reachable `ldstr` operands and the `#US` heap, metadata `Constant`
values, and `FieldRVA` blobs where required.

**Header:** a category extension, one sized item per constant, and the
existing fixed descriptor shape used directly by ObjectHandler.

```c
typedef struct {
  vxb_category_header_t category;
  uint32_t constant_table_offset;
  uint32_t constant_count;
  uint32_t constant_space_size;
} vxb_constants_category_header_t;

typedef struct {
  vxb_item_header_t item;
  uint32_t constant_index;
  uint32_t descriptor_offset;
} vxb_constant_header_t;

typedef struct {
  uint32_t offset;
  uint32_t size;
  uint16_t type;
  uint8_t kind;
  uint8_t flags;
} vxb_constant_descriptor_t;
```

`vxb_constant_descriptor_t` reproduces `object_const_t`'s 12-byte layout,
not a replacement ABI. Store that descriptor array contiguously before the
sized constant items; `constant_count` includes reserved entry zero,
whereas `category.item_count` counts actual constants. Each item owns its
payload and points to its descriptor; the descriptor supplies payload size,
type, kind and flags without duplicating them in the item header.

The VM gives ObjectHandler `const_base = block_start + constant_table_offset`
and `const_size = constant_space_size`. A descriptor's `offset` alone is
relative to that `const_base`, as required by doc 14, not to the block.
The constant region covers the table and subsequent items/payloads, stays
inside the category, and preserves 8-byte table/payload alignment. Validate
`descriptor_offset` against the index's table entry. Interleaving VXB item
headers into the 12-byte descriptor array would break direct indexing.

**Pack:** immutable object descriptors and payloads for strings, constant
arrays and other supported constant objects; retain raw data blobs where
the program must copy them into writable storage. Convert user strings
from UTF-16 to the agreed UTF-8 representation, with NUL termination and
correct ASCII flags. Apply the existing non-ASCII opt-in policy.

`ldstr` names a constant index; ObjectHandler constructs the runtime id.
Metadata `Constant` rows describe literal/default values, not arbitrary
mutable static initialization. Copy `FieldRVA` bytes only after deriving and
checking their size. A constant array must have a whole payload, including
its header, of at most 65536 bytes (doc 19 B11); test the producer boundary.

### 3.3 Runtime types and fields

**Source:** `TypeDef`, field signatures, inheritance, `ClassLayout`,
`FieldLayout`, interfaces and relevant type specifications in
[parser_table_types.h](../source/row_table/parser_table_types.h).

**Header:** distinct type and field items in the same category, each with
its own kind and ID lookup.

```c
typedef struct {
  vxb_item_header_t item;
  uint16_t type_id;
  uint16_t base_type_id;
  uint32_t type_flags;
  uint32_t instance_size;
  uint32_t alignment;
  uint32_t field_count;
  uint32_t field_offsets_offset;
  uint32_t method_count;
  uint32_t method_ids_offset;
  uint32_t dispatch_offset;
  uint32_t gc_refs_offset;
  uint16_t cctor_method_id;
  uint16_t element_type_id;
  uint16_t element_size;
  uint8_t type_kind;
  uint8_t storage_kind;
} vxb_type_header_t;

typedef struct {
  vxb_item_header_t item;
  uint32_t field_id;
  uint16_t owner_type_id;
  uint16_t field_flags;
  vxb_storage_type_t storage_type;
  uint32_t value_offset;
} vxb_field_header_t;
```

Type names and source `Extends` indices become resolved IDs and flags.
`instance_size`, alignment and element information are computed layouts;
`type_kind` distinguishes class/value/enum/interface/array descriptions.
An enum's element information identifies its underlying storage type.
`field_offsets_offset` points to an owned array of field-header offsets;
`method_ids_offset` points to an owned array of `uint16_t` method IDs. The
referenced field/method headers are separate items, not hidden C pointers.

`value_offset` is a byte offset within an instance/value payload or, for a
static field, within the application's RAM slab; it is not a file offset.
Field flags distinguish those cases and literal fields without storage.
The declaring-type field list, computed inherited layout and dispatch
records must agree. `gc_refs_offset` and `dispatch_offset` link to the
corresponding category items; `cctor_method_id` is a method link, not live
initialization state.

**Pack:** resolved type identities and kinds, base types, instance/value
sizes, alignment, field storage types and offsets, and required flags.
Include concrete enum and array descriptions where used, not only classes.
Array descriptions supply element type, size and reference information.

The packer computes layouts, including inherited fields and references
embedded inside value types. The executor consumes those layouts. A
`newobj` operand identifies a constructor; its declaring type identifies
the allocation layout. Whether a field operand is a descriptor index or a
specialised offset is an encoding decision, not a reason to lose its type.

### 3.4 Methods, signatures and frame requirements

**Source:** `MethodDef` flags/implementation flags, method signatures, tiny
or fat bodies, local signatures and `max_stack`, decoded by
[method_parser.c](../source/methods/method_parser.c).

**Header:** one method item with its signature and local descriptions.

```c
typedef struct {
  vxb_item_header_t item;
  uint16_t method_id;
  uint16_t owner_type_id;
  uint16_t method_flags;
  uint16_t implementation_flags;
  uint16_t body_flags;
  uint16_t max_stack;
  uint16_t argument_slots;
  uint16_t local_slots;
  uint16_t return_slots;
  uint8_t calling_convention;
  uint8_t method_kind;
  uint32_t parameter_count;
  uint32_t argument_count;
  uint32_t local_count;
  uint32_t arguments_offset;
  uint32_t locals_offset;
  uint32_t code_offset;
  uint32_t exceptions_offset;
  uint32_t native_offset;
  vxb_storage_type_t return_type;
} vxb_method_header_t;
```

`arguments_offset` and `locals_offset` point to owned arrays of
`vxb_storage_type_t`; argument descriptions include `this`, whereas
`parameter_count` counts only declared parameters. Slot counts are the
validated inputs to frame creation, not unchecked copies of source counts.
`calling_convention`, return type and storage descriptions come from the
decoded signatures; `max_stack` and relevant body flags come from the body.
Discard the PE tiny/fat encoding bits, but retain `InitLocals` semantics.

`code_offset` points to a bytecode item header, not its first opcode.
`exceptions_offset` points to this method's EH item; `native_offset` links
to a native-binding item where applicable. `method_kind` distinguishes
managed, native and abstract methods and constrains which links may exist.
A void return has zero return slots and an explicit void storage descriptor.

**Pack:** declaring type, managed/native/abstract classification, code
offset/length where applicable, parameter and return types/conventions,
argument/local slot counts, declared storage types, local stack kinds,
maximum evaluation-stack depth and required method/body flags. Arguments
include `this` where required. Keep method attributes distinct from body
flags such as `InitLocals`.

Counts alone are insufficient: `int8` and `int32` may use the same stack
kind but different storage behaviour. Restrict unsupported signatures,
including value-type calling conventions not yet implemented, explicitly.

### 3.5 Static storage and initialization

**Source:** static field declarations, literal/default metadata, applicable
initial data and type initializers (`.cctor`).

**Header:** one item per stored static field; constant-reference fix-ups
are sized child records when needed.

```c
typedef struct {
  vxb_item_header_t item;
  uint32_t field_id;
  uint32_t ram_offset;
  uint32_t storage_size;
  uint32_t initial_data_offset;
  uint32_t initial_data_size;
  uint32_t fixup_count;
  uint32_t fixups_offset;
} vxb_static_header_t;

typedef struct {
  vxb_item_header_t item;
  uint32_t ram_offset;
  uint32_t constant_index;
} vxb_static_fixup_header_t;
```

`field_id` provides declaring type, flags and declared storage through 3.3.
RAM offsets are absolute within the one statics slab, beginning after its
reserved eight bytes. Initial bytes are owned payload, never serialized
runtime object IDs; a zero data size means zero-filled storage. Validate
their size and field containment. Each child fix-up names a reference word
inside this field and a constant index; advance children by their own sizes.
The owning type provides its `.cctor` link. Total slab size and its GC-map
link are in the startup item (3.10), not repeated on every field.

**Pack:** total RAM size, per-field offsets/storage types, initial bytes or
zero-fill descriptions, constant-reference fix-ups and a statics reference
map. A fix-up records where a runtime constant id must be constructed and
stored; that id is not serialized in the image.

Associate each type with its `.cctor` or its proved pack-time-initialized
state. Initialization state is mutable runtime data. Do not run every
`.cctor` at startup without preserving the specified initialization policy.
Constant folding of initializers remains gated by doc 15, not assumed here.

### 3.6 Virtual and interface dispatch

**Source:** inheritance, virtual/new-slot method attributes, `InterfaceImpl`
and `MethodImpl` relationships.

**Header:** one dispatch item per concrete type with sized interface
dispatch children.

```c
typedef struct {
  vxb_item_header_t item;
  uint16_t type_id;
  uint16_t flags;
  uint32_t vtable_count;
  uint32_t vtable_offset;
  uint32_t interface_count;
  uint32_t interfaces_offset;
  uint32_t interface_index_count;
  uint32_t interface_index_offset;
} vxb_dispatch_header_t;

typedef struct {
  vxb_item_header_t item;
  uint16_t interface_type_id;
  uint16_t flags;
  uint32_t slot_count;
  uint32_t targets_offset;
} vxb_interface_dispatch_header_t;
```

The item owns its vtable, interface children and interface-ID lookup array.
`interfaces_offset` locates the first child for traversal;
`interface_index_offset` maps a type ID directly to the appropriate child
offset, with zero for an interface not implemented. Children own their
slot-target arrays. Preserve inherited/replaced/new-slot ordering from the
computed dispatch model. The proposed target arrays use `uint16_t` entries;
their method-versus-native binding bit convention follows doc 16 and must
be agreed before VXB-7. Array counts must fit the instruction's slot limits.

**Pack:** vtable slots, per-type implementations, interface membership and
interface-to-implementation mappings. The executor does no method-name
search. Direct and virtual operands must have distinct agreed meanings.
Preserve doc 15's `DEVIRT` encoding and the null check on `callvirt`, even
when dispatch becomes direct.

### 3.7 Native bindings

**Source:** supported native assembly declarations, method signatures and
supported import/internal-call metadata. An `ImplMap` row alone does not
make arbitrary P/Invoke supported.

**Header:** a category extension and one sized item per binding, alongside
the fixed binding table already specified by doc 16.

```c
typedef struct {
  vxb_category_header_t category;
  uint32_t binding_table_offset;
  uint32_t binding_count;
  uint32_t manifest_offset;
  uint32_t manifest_size;
} vxb_natives_category_header_t;

typedef struct {
  vxb_item_header_t item;
  uint16_t binding_index;
  uint16_t method_id;
  uint32_t binding_offset;
  uint32_t overflow_kinds_count;
  uint32_t overflow_kinds_offset;
} vxb_native_header_t;

typedef struct {
  uint16_t fn_index;
  uint8_t shape;
  uint8_t arg_slots;
  uint32_t arg_kinds;
  uint8_t ret_kind;
  uint8_t ret_slots;
  uint8_t flags;
  uint8_t reserved;
} vxb_binding_t;
```

`binding_table_offset` locates contiguous 12-byte `vxb_binding_t` records,
so `calln` retains direct binding-index lookup. The sized item links the
binding index to a method signature and owns any overflow argument-kind
bytes needed by doc 16's generic shape. Its `binding_offset` must identify
the corresponding fixed-table entry, not a C function address. Normalized
argument/result kinds and counts are computed from signatures; native
behaviour flags and `fn_index` come from the binding manifest/registry.

The category manifest is shared metadata, outside individual item spans.
The block's `native_abi_hash` is the four-byte identity required by doc 16;
compare it when native bindings are required, even during development.
It checks the native interface, not a header-format revision.

**Pack:** binding identifiers, call shapes, behaviour flags, the required
binding manifest and native ABI identity from doc 16. The host/device
registry maps those identifiers to C implementations; no C function pointer
or host address is written to the file. An unavailable binding fails
explicitly. Resolve doc 16's remaining choices before implementing them.

### 3.8 GC reference maps

**Source:** computed type and static layouts, including nested value types.

**Header:** one item per required reference map.

```c
typedef struct {
  vxb_item_header_t item;
  uint16_t type_id;
  uint16_t reserved;
  uint32_t word_count;
  uint32_t bitmap_offset;
  uint32_t bitmap_size;
} vxb_gc_refmap_header_t;
```

The item owns the bitmap bytes; `bitmap_size` is `(word_count + 7) / 8`
with checked arithmetic. Each bit describes a four-byte storage word.
`item_kind` distinguishes instance/value/array-element/static maps; the
one static map has type ID zero and a startup link instead of an owning
type. Type records link their instance/value/element map directly. A type
without references can have no map link, letting the callback return NULL.
For a reference-bearing array element, the map must cover exactly its
element storage size, as required by ObjectHandler's array API.

**Pack:** per-word reference bitmaps for instances, boxed value payloads,
array elements and statics, with the sizes/word counts needed by
ObjectHandler's `type_refmap` callback. Array maps describe one element.

The existing typed slots and root scanner identify stack roots; this plan
does not add a duplicate per-instruction stack map. Map production can be
tested before collection exists. Runtime collection remains a separate
ObjectHandler block.

### 3.9 Exception information

**Source:** each method's normalized `eh_clauses` and `eh_clause_count`;
the parser reads small/fat and chained EH sections.

**Header:** one method EH item with sized clause children.

```c
typedef struct {
  vxb_item_header_t item;
  uint16_t method_id;
  uint16_t reserved;
  uint32_t clause_count;
  uint32_t clauses_offset;
} vxb_exceptions_header_t;

typedef struct {
  vxb_item_header_t item;
  uint32_t clause_flags;
  uint32_t try_il_offset;
  uint32_t try_il_size;
  uint32_t handler_il_offset;
  uint32_t handler_il_size;
  uint16_t catch_type_id;
  uint16_t reserved;
  uint32_t filter_il_offset;
} vxb_eh_clause_header_t;
```

The parent owns its clauses, in the required EH ordering; each clause has
its own size even when it has no additional payload. IL offsets are
relative to this method's first opcode, not to the block, and all refer to
the rewritten code. Catch clauses carry a resolved type ID; filter clauses
carry a filter offset, which can legitimately be zero. Clause flags select
which is meaningful; zero the unused fields. Finally/fault clauses use
neither. Bound both ends of each region within the corresponding code item.

**Pack:** protected ranges, handler ranges, clause kinds, resolved catch
type identifiers and filter offsets. Remap both ends of a range after code
rewriting, then derive its new length. Validate ranges and targets against
the rewritten method.

Unwinding is runtime work. Unsupported catch/filter/finally/fault behaviour
is a declared profile limitation; never drop the corresponding clauses and
claim the method is supported.

### 3.10 Image and startup descriptor

**Source:** the application entry-point token, resolved dataset counts,
feature requirements and the selected execution profile.

**Header:** the outer `vxb_block_header_t` is proposed above; this category
contains one startup item for the application block.

```c
typedef struct {
  vxb_item_header_t item;
  uint16_t entry_method_id;
  uint16_t string_type_id;
  uint16_t entry_kind;
  uint16_t startup_flags;
  uint32_t statics_size;
  uint32_t statics_gc_refs_offset;
} vxb_entry_point_header_t;
```

Resolve the parser's entry token to `entry_method_id`. `entry_kind` records
the agreed argument/return convention, checked against its method record;
`string_type_id` configures ObjectHandler when strings are used.
`statics_size` is the computed slab size, zero when absent;
`statics_gc_refs_offset` links its map item, zero when it has no references.
Startup flags describe required setup, not live `.cctor` state. The block
header supplies category offsets, capability requirements and native ABI
identity. Heap and thread capacities remain an explicit policy decision;
they cannot be extracted reliably from one method's `max_stack`.

**Pack:** magic, block size and category offset directory, capability
information, entry-method identifier and native ABI identity when used.
During development, there is no header-format version field or old-layout
reader. Document the current record sizing, byte order, alignment and
absent/empty rules before serialization; introduce version/compatibility
control at the version-1 freeze, not on every draft edit.

Validate structural bounds and compatibility before execution. Resource
budgets such as heap, stack/frame capacity and scheduling settings need an
explicit image-versus-embedder policy; their placement remains open. A
method's `max_stack` is not a bound on total recursive call depth.

## 4. What remains between the parser and these datasets

The parser exposes source tables, signatures, IL bytes and EH clauses. It
does not yet produce the resolved runtime representation. In particular,
[type_ref_parser.c](../source/methods/type_ref_parser.c) and
[member_ref_parser.c](../source/methods/member_ref_parser.c) expose reference
information but do not complete binding to runtime targets.

```text
1. Parse the application and required dependencies.
2. Resolve references and determine the reachable closure.
3. Assign image-wide type, method, field and constant identifiers.
4. Compute layouts, statics, dispatch and reference maps as required.
5. Build constants and rewrite instruction operands/prefixes.
6. Finalise code positions; fix branches, switch tables and EH ranges.
7. Validate the datasets, emit the image, and read/dump it independently.
```

Reference resolution and reachability interact: follow direct/virtual calls,
constructors, initializers, fields and types until the supported closure is
complete. Source tokens are scoped by assembly/module; equal token numbers
from different inputs are not equal runtime identities. IDs and output
ordering must be deterministic. Reject unresolved required references.

## 5. Build blocks and proof

Implement one block at a time. Extend the format contract only as far as
that block needs. The order below produces a scalar image before introducing
object allocation or a dependency on GC.

- [ ] **VXB-1 - Logical records and first profile.** Agree the initial
  instruction/signature subset, runtime method/code views, entry convention
  and unsupported-feature policy. Review the proposed block/category/item
  headers in section 3 and record the relevant current contract, not C
  struct serialization. **Proof:** describe a no-argument method returning
  `int32` with `ldc.i4.2; ldc.i4.3; add; ret`, zero locals and `max_stack` 2;
  every value needed to decode it and create its frame has a named source.
- [ ] **VXB-2 - Resolution, reachability and IDs.** Depends on VXB-1. Bind
  the supported input references and assign deterministic identifiers;
  include additional roots as each later profile extension requires.
  **Proof:** local and cross-assembly calls resolve to the intended method,
  colliding source token numbers stay distinct, unresolved references fail,
  and repeated runs produce the same reachable tables.
- [ ] **VXB-3 - Scalar methods and code.** Depends on VXB-1 and VXB-2.
  Pack signatures, typed arguments/locals, maximum stack and scalar code;
  rewrite supported operands and fix branch targets. **Proof:** decoded
  fixtures cover constants, arithmetic, locals, a loop and a nested managed
  call, with correct operand types, code boundaries and frame requirements.
- [ ] **VXB-4 - First writer, shared reader and dump.** Depends on VXB-3
  and the corresponding doc 12 layouts. Emit the smallest scalar image;
  share the reader between `cli_dump` and the future executor. **Proof:**
  parse/pack/read/dump reproduces expected datasets, repeated images match,
  and truncated records, bad bounds/IDs, unsupported capabilities and invalid
  entry points fail. Verify category offsets and item-size traversal when
  the block is embedded at a nonzero file offset. Unused datasets follow
  the empty-category rules, not ad hoc special cases. Draft changes rebuild
  and regenerate all consumers/images together; version compatibility tests
  begin at the version-1 freeze.
- [ ] **VXB-5 - Types, fields and reference maps.** Depends on VXB-2 and
  VXB-4. Add the supported class/value/enum/array layouts, field descriptions
  and type reference maps. **Proof:** inherited fields, supported packing
  and explicit offsets, nested references and array element layouts match
  expected sizes/offsets/maps; unsupported layouts fail. Exercise the
  ObjectHandler type callback without requiring collection.
- [ ] **VXB-6 - Constants, statics and initialization.** Depends on VXB-5.
  Add strings/data, constant descriptors, static allocation, fix-ups,
  static refmaps and `.cctor` associations. **Proof:** UTF-8/ASCII policy,
  constant identity, bounded `FieldRVA` copies, narrow static storage and
  fix-ups are correct; constant-array payloads of 65536 bytes are accepted
  and oversized payloads rejected without arithmetic overflow. Verify
  runtime-id construction under both ObjectHandler configurations.
- [ ] **VXB-7 - Dispatch and native integration.** Depends on VXB-5 and
  the applicable contracts in docs 15 and 16. Add virtual/interface maps
  and supported binding records. **Proof:** overrides/new slots/explicit
  interface implementations select the intended method; devirtualized
  calls retain their null check; absent bindings and ABI mismatches fail.
  Execution-specific checks run when those executor handlers exist.
- [ ] **VXB-8 - Exception tables.** Depends on VXB-3 through VXB-5 and
  the selected EH profile. Add resolved clauses and remapped ranges.
  **Proof:** small/fat source clauses yield equivalent logical records,
  size-changing rewrites preserve protected regions and filter targets,
  and malformed or unsupported clauses fail. Unwinding correctness is a
  separate executor check, not proved by a successful dump.

For each block, test the producer's calculations and the reader's view
against known source fixtures, including failure/boundary cases. Reuse the
existing harness/CTest conventions. Run scoped sanitizer and release tests
for new code; verify portable reader/executor code with host and ARM
compilers. Preserve ObjectHandler's standalone build.

When the corresponding executor slice exists, run the same datasets both
from direct fixtures and from packed images, checking results, side effects
and managed faults. Only claim execution support after that check. Folded
and unfolded execution comparisons become mandatory when transforms are
enabled, as specified by doc 15.

## 6. Optional datasets and data not packed

Optional: diagnostic type/method names, original-IL-to-packed-offset maps,
source/debug information and application resources when used. Required
resources cannot be omitted merely because resource support is postponed.

Reflection and generic support require an explicit profile. The existing
architecture plans concrete generic instantiations at pack time, not a
runtime generic resolver. Reject unsupported required instantiations in
early images. Do not copy all ECMA metadata just because it was decoded.

Not packed: live heap objects, handles, active frames, managed thread
records, monitor state, native addresses or mutable initialization state.
The runtime creates these. Constant-object descriptions are image data;
they are not a serialized live heap.

## 7. Decisions still open

| Decision | Needed before |
|---|---|
| Image entry convention and category/item/flag values; executor-only scalar scope is settled in section 8 | VXB-1 |
| Packer/reader test homes and remaining image runtime views; executor placement/API is settled in section 8 | Relevant VXB block before code |
| Operand widths/forms not already settled by doc 15; typed-storage transforms (B9) and correct native-int folding (B7) | Affected VXB-3/VXB-5 handlers and rewrites |
| Review section 3's proposed block/category/item headers, little-endian encoding, alignment, ID lookups, sentinels and flags/kinds | Relevant block before serialization |
| Freeze version 1 and introduce header/format version and compatibility control; no legacy draft support | Version-1 release, not development edits |
| Layout/field operand choices within the supported type profile | VXB-5 |
| Image versus embedder ownership of heap/stack/frame/scheduling budgets | VXB-4 startup interface, or the extension first using each budget |
| Remaining native protocol choices and supported EH clause kinds | VXB-7 and VXB-8 respectively |

Do not guess these in code. Existing settled contracts are not reopened
merely because their binary records have not yet been written.

## 8. Executor-first slice

**Owner-directed next step, 2026-10-08:** return to the IL executor before
implementing the VXB writer/reader. The owner accepted the scalar slice
below. The header proposals remain the image design; the VXB build blocks
remain unchecked. File loading and GC implementation are not prerequisites
for this executor slice.

### 8.1 Accepted scope and interface

Production code goes in `source/il_executor/`; tests go in
`tests/il_executor/`, reusing the existing harness and fake embedder while
linking the real ObjectHandler. The executor contains no PE parser, VXB
loader, scheduler, native dispatch, mutable globals or heap allocation.

The public API is [executor.h](../source/il_executor/executor.h);
[executor.c](../source/il_executor/executor.c) implements the dispatcher.

An in-memory method view supplies immutable code bytes and their length,
a nonzero method ID, `max_stack` and a return kind. The first profile is a
static managed method with no arguments or locals, returning either
`STACK_K_I4` or `STACK_K_EMPTY` (void). A later VXB reader constructs this
view from validated image records; tests supply it directly. The executor
does not manufacture declaring types or resolve an `owner_type_id`.

The caller owns an initialized `stack_thread_t`, its slot/frame arrays, the
method's code and a zeroed executor record. `executor_start` requires an
empty, frameless thread and pushes one ObjectHandler frame. Invalid start
arguments or insufficient frame/slot capacity return an explicit status
without changing the thread or an existing execution. Keep borrowed
thread/code storage alive while the executor record is used.

`executor_step` executes at most one instruction. `executor_run` executes
at most its instruction budget; `EXECUTOR_CONTINUE` means the caller may
resume, including after a zero budget. The record publishes the byte offset
and the thread's IP after each successful non-return instruction. There is
no managed recursion through the C stack.

On successful `ret`, the frame is popped. An `int32` result remains as one
typed slot on the thread; a void method leaves no slots. The thread's IP is
cleared and the executor reports `EXECUTOR_RETURNED`. The caller consumes
the result before starting another method, or ends the thread through
ObjectHandler. Repeating step/run after completion returns the same status.

### 8.2 Supported instructions and failures

| Instruction family | First-slice behaviour |
|---|---|
| `nop` | Advance one byte without changing the stack. |
| `ldc.i4.m1`, `ldc.i4.0-8`, `ldc.i4.s`, `ldc.i4` | Push `STACK_K_I4`; sign-extend the short literal and decode the full literal little-endian. |
| `dup`, `pop` | Duplicate/drop a whole typed slot; preserve its kind and value. |
| `add`, `sub`, `mul`, `neg` | Accept only `STACK_K_I4`; use 32-bit wrapping arithmetic without C signed-overflow undefined behaviour. `sub` consumes right then left and computes left minus right. |
| `ret` | Require exactly the declared return depth and kind, then pop the frame with zero or one return slot. |

Check code length, stack depth/room and operand kinds in every build, not
only when ObjectHandler's debug checks are enabled. A failing instruction
does not advance its IP or change its operands. Failures are terminal for
that execution: unsupported opcode, truncated supported instruction,
falling off the code without `ret`, stack underflow/overflow, wrong operand
kind, wrong return shape or invalid execution state. Preserve the frame
and live slots for inspection; the caller ends the thread to discard them.
These are executor statuses, not a managed exception-unwinding subsystem.

Arguments, locals, conversions, other numeric kinds, branches and managed
calls follow as separately contracted and tested slices. The storage-form
and conversion questions B7/B9 still apply when those slices arrive. No
unsupported opcode is silently treated as `nop`.

### 8.3 Proof and next action

First prove `ldc.i4.2; ldc.i4.3; add; ret` returns `5` using the real stack
and frame APIs. Add focused cases for step/budget resumption, literal forms,
operand order, wrapping boundaries, duplicate/drop behaviour, truncation,
unsupported opcodes, stack/frame limits and incorrect return types/depths.
Fault cases must preserve the failing offset and stack and must not invoke
ObjectHandler's corruption callback. Separate contexts must not interfere.

Run the executor cases in sanitized debug and release builds; compile the
module and its public header for host and Cortex-M4. Keep ObjectHandler's
standalone build independent. A passing executor test is not a VXB
pack/read round trip; do not mark VXB-1 through VXB-8 built on that basis.

### 8.4 Implementation record (2026-10-08)

The `il_executor` CMake target and
[test_executor.c](../tests/il_executor/test_executor.c) are built. All 23
executor cases pass in sanitized Debug (ASan/UBSan) and Release with all
ObjectHandler debug switches disabled. Sixteen isolated source/header
checks pass: host and Cortex-M4, C99 and C11, Debug and Release ABIs.
ObjectHandler's standalone configuration succeeds; its code is unchanged.
The active build was restored to its original Debug, sanitizers-off settings.

The first fixture returns `5`. Literal forms, wrapping arithmetic, typed
stack operations, step/budget resumption, terminal fault preservation,
stack/frame capacity, return shape and independent contexts are covered.
Table-driven fixtures initialize the fake ObjectHandler once and recreate
their threads between runs; repeated `fake_init` is not a reset operation.
CTest's existing missing-`DartConfiguration.tcl` warnings remain; both
focused suite runs returned success. The full ObjectHandler suite was not
rerun for this executor-only change.

This first slice is extended by the argument/local implementation in
section 9. Branches, managed calls, VXB loading and GC remain separate work.
No image-format block is claimed complete.

## 9. Arguments and locals

**Owner-directed extension, 2026-10-08:** implement the argument/local slice
on the existing executor. It uses per-method type tables as recorded by
doc 15 section 3.1 (B9), not new typed opcode encodings. VXB loading, GC,
branches, managed calls and indirect load/store instructions remain deferred.

### 9.1 Method view and entry

Extend `executor_method_t` with 16-bit argument/local counts and immutable
`uint8_t` declared-type tables, one `EXECUTOR_TYPE_*` per location. Zero
counts need no table. Support signed/unsigned 8/16/32/64-bit integers,
32-bit native integers, `float32`/`float64`, object references and byrefs.
`bool`/`char` map to unsigned 8/16-bit storage. Concrete class assignability,
value-type aggregates and unsupported signature shapes are outside this
slice; the return profile remains `int32` or void.

Start requires a frameless initialized thread whose stack contains exactly
the declared number of arguments, in evaluation-stack form and declaration
order. Validate all metadata, argument kinds and capacity before changing
the thread. Push the real ObjectHandler frame with overlapping arguments,
normalise each argument into storage form and initialize every local to a
typed zero before publishing successful entry. Rejected starts preserve
the caller's arguments and execution record.

The existing no-argument entry still works with zero counts. Borrowed type
tables, like code, remain alive and unchanged while the execution is active.
Each argument/local occupies one slot in this primitive/reference profile.

### 9.2 Instructions and storage

Support `ldarg.0-3`, `ldloc.0-3`, `stloc.0-3`, all `.s` forms and the
`0xFE`-prefixed 16-bit-index forms of `ldarg`, `ldloc`, `starg`, `stloc`,
`ldarga` and `ldloca`. Decode indices little-endian, check the entire operand
before reading it, and reject indices outside the relevant table with
`EXECUTOR_BAD_INDEX`. Missing prefix/index bytes are truncated instructions,
not unsupported instructions. Retain each successful instruction's actual
encoded length for step/budget resumption.

Loads widen signed/unsigned narrow storage and reconstruct `float32` as
an evaluation-stack `R8`. Stores require the declared evaluation-stack kind,
then narrow/round into the location's storage form. Reuse ObjectHandler's
numeric conversion helpers after validation. Ref/byref stores preserve their
representation and use the reference barrier for the referenced base/id.
Their slots remain visible to the existing root scanner.

`ldarga`/`ldloca` use `stack_slot_byref`, exposing the location's declared
storage bytes. Byref-to-byref address locations are outside this profile
and fail with a type mismatch; ordinary byref values can still be copied
between arguments/locals. No managed pointer is returned to its own frame.

Every build checks bounds, depth, room and kinds. A failing load/store/address
instruction leaves its IP, evaluation stack and argument/local storage
unchanged. Stores consume their operand only after successful validation
and storage. A return still discards the frame's args/locals and leaves only
the declared result on the thread.

### 9.3 Proof

Add cases to the existing executor test file for compact/short/wide forms,
indices above 255, typed zero locals, entry argument validation/narrowing,
signed versus unsigned loads, truncating stores, `float32` storage/rounding,
reference/byref copies and root visibility, and address resolution. Include
bad indices, every truncation boundary, stack/frame limits and terminal
fault preservation. Run the scalar regression cases as well as the new
cases in sanitized Debug and Release; repeat host/Cortex-M4 C99/C11 checks.

### 9.4 Implementation record (2026-10-08)

The method view now includes declared argument/local tables. Entry validates
every type and argument before pushing the frame, narrows arguments to
storage form and initializes typed locals. Standard compact/short/wide
load/store/address instructions are implemented; wide decoding uses the
shared opcode table's second-byte `IL_OP_FE_*` constants.

All **50 executor cases** pass in sanitized Debug (ASan/UBSan) and Release
with all five ObjectHandler debug switches disabled: 23 scalar regressions
and 27 argument/local cases. Coverage includes indices above 255, signed
narrow storage, actual `float32` bytes and rounding, ref/byref root scanning,
storage addresses, metadata/capacity validation and atomic failures. Sixteen
isolated host/Cortex-M4 C99/C11 source/header checks pass with Debug and
Release ABIs. The original Debug, sanitizers-off build settings are restored.

No ObjectHandler implementation or standalone-build dependency changed.
The full ObjectHandler suite was not rerun. Existing CTest
missing-`DartConfiguration.tcl` warnings remain; both focused suite runs
returned success. B9's contract is recorded in doc 15; packer emission and
managed-call boundary handlers remain future work.

This slice is followed by the branch/comparison implementation in section
10. Managed calls, indirect access, additional arithmetic kinds, VXB loading
and GC are not included in this implementation record.

## 10. Branches and comparisons

**Owner-directed extension, 2026-10-08:** add control flow and comparison
instructions to the existing executor. Managed calls, exception handling,
VXB loading, GC and a scheduler remain separate work.

### 10.1 Instructions and operand types

Support short/long `br`, `brfalse`, `brtrue`, `beq`, `bne.un`, `bge`, `bgt`,
`ble`, `blt` and the unsigned/unordered relational branch variants; `switch`;
and `0xFE`-prefixed `ceq`, `cgt`, `cgt.un`, `clt`, `clt.un`. Comparison
opcodes consume left then right in stack order and push `STACK_K_I4` 0/1.
Binary branches consume two operands; truth branches and `switch` consume
one. Unconditional branches consume none. `leave` and `jmp` remain
unsupported because their exception/call semantics are not ordinary branches.

Reuse ObjectHandler's `value_compare` after CIL profile validation: allow
I4/I4, I8/I8, native/native, mixed I4/native and R8/R8 pairs. Do not promote
otherwise invalid mixed types merely because the helper could compare them.
Signed forms read integers signed; `.un` forms read them unsigned and treat
floating NaN as unordered. Ordered relational forms return false on NaN;
unsigned/unordered relational forms and `bne.un` return true. `ceq`/`beq`
return false on NaN; equal positive/negative floating zero compares equal.

Ref/ref identity is supported for `ceq`, `beq` and `bne.un`. `cgt.un` also
supports reference/null pairs, including two nulls, as a non-null test;
ordering two non-null references is outside this profile. Other reference
ordering, byref comparisons and integer/ref or float/integer mixes fail with
`EXECUTOR_TYPE_MISMATCH` without resolving pointers or invoking corruption
callbacks. Truth branches accept I4, I8, native integers, refs and byrefs,
using zero/null versus nonzero; they reject floating operands. `switch`
requires I4 and treats a negative/out-of-range index as fall-through.

### 10.2 Decoding and target validation

Decode signed 8-bit or little-endian signed 32-bit displacements. A branch
target is relative to the first byte after the complete branch instruction.
`switch` holds a uint32 count and that many signed 32-bit displacements;
every switch displacement is relative to the byte after the entire table,
not to its own entry. Check table size by division before multiplication.

Compute destination offsets in a wider integer domain, then require
`0 <= target < code_size` and an instruction boundary. Do not create an
out-of-range pointer to perform validation. A target in a literal/index,
switch table or the second byte of an opcode prefix is invalid. All encoded
targets are checked, even on an untaken branch or unselected switch case.
Malformed operands report truncation; malformed destinations report
`EXECUTOR_BAD_BRANCH_TARGET`. Only after decoding, target/type/depth
validation succeeds may operands be consumed and the new IP published.
On failure, the IP, frame, live slots and local/argument data stay unchanged.

The first implementation derives boundaries using the supported instruction
decoder, with no allocation or new caller-supplied map. Bytes traversed to
establish a target must decode in the current profile; unsupported/truncated
instructions in that path cannot establish a valid boundary. This is a
linear validation cost, not a claim that the final hot path is optimised.
A future validated image can supply precomputed boundary data as a separate
change. This check is not a full CIL verifier or EH-region validator.

### 10.3 Bounded execution and proof

Each branch, comparison or switch costs one instruction from `executor_run`'s
budget, regardless of encoded length or whether a branch is taken. Backward
branches can loop indefinitely, but a finite budget always returns control
with the actual next offset/IP available for resumption. No scheduler or
GC safepoint implementation is claimed by this control-flow slice.

Test forward/backward short/long branches, every relational outcome and
operand order, signed/unsigned boundaries, I8/native/mixed I4-native pairs,
NaN/zero/infinity, reference identity/null tests and truth operand kinds.
Include switch selection/default/negative indices and whole-table-relative
targets, self-loop budget/resumption, every truncation boundary, invalid
targets/overflow, underflow/type mismatches and fault preservation. Run all
scalar and argument/local regressions in sanitized Debug and Release and
repeat the host/Cortex-M4 C99/C11 source/header checks.

### 10.4 Implementation record (2026-10-08)

Short/long truth, equality and relational branches, `switch`, and all five
comparison opcodes are implemented. A shared supported-instruction decoder
drives execution lengths and target-boundary checks. Signed displacements
use the complete instruction/table end as their base, and destinations are
validated as offsets before constructing an IP. Untaken branches and all
switch entries are checked before their operands are consumed.

All **80 executor cases** pass in sanitized Debug (ASan/UBSan) and Release
with all five ObjectHandler debug switches disabled: the 50 prior cases
and 30 control-flow/comparison cases. The relational matrices exercise both
forms, signed/unsigned limits, I8/native and mixed I4/native, NaN, zero and
infinity, reference identity/null tests, switch selection/defaults and
instruction-budget resumption. A loop using arguments and locals returns
the expected sum. Fault matrices preserve the stack and failing IP while
rejecting truncation, bad bounds/boundaries and incompatible operand kinds.

Sixteen isolated host/Cortex-M4 C99/C11 source/header checks pass with Debug
and Release ABIs. The original Debug, sanitizers-off build configuration is
restored. ObjectHandler, GC and VXB code are unchanged; the full
ObjectHandler suite was not rerun. Existing CTest
missing-`DartConfiguration.tcl` warnings remain; both focused suite runs
returned success. The allocation-free boundary scan remains a documented
linear validation cost, not a completed loader/verifier or speed claim.

The direct managed-call implementation follows in section 11, using the
existing frame stack and argument normalisation. Native/virtual dispatch, exception
unwinding, indirect memory access, VXB loading and GC remain separate work.

## 11. Direct managed calls

**Owner-directed extension, 2026-10-08:** add direct managed `call` and
iterative return to the existing executor. No interpreter recursion, VXB
loading, native/virtual dispatch, constructors, exceptions or GC is added.
The supported return profile remains `STACK_K_I4` or void; parameter/local
types retain the primitive/reference/byref profile from section 9.

### 11.1 Program view and instruction

`executor_program_t` borrows an immutable dense method table and its entry
count. Index zero is reserved; a live entry's `method_id` equals its table
index. Nonzero IDs are at most 32767, matching the existing packed method
index contract. The descriptor, table, code and type tables stay alive and
unchanged while their executor is used. Unused table entries may be empty;
validate selected entry/target metadata before using it.

`executor_start_program` selects the root by ID and uses the existing
argument-normalising entry path. The original `executor_start` still runs
standalone methods without a registry; executing `call` in that mode fails
with `EXECUTOR_BAD_METHOD`. The executor allocates no registry or frames.

The instruction is **`call <uint16 method_id>`**, opcode plus two
little-endian operand bytes. It consumes a pre-resolved packed identifier,
not a four-byte ECMA metadata token. The shared decoder records its
three-byte length so branch destinations cannot enter its operand. Zero,
out-of-range, mismatched, empty or unsupported target descriptors fail as
bad methods; missing operand bytes are truncated instructions.

### 11.2 Call preflight and transition

Arguments occupy the top of the caller's evaluation stack in declaration
order, including an explicit receiver when the method view describes one.
Lower caller values are retained. Validate target metadata, argument depth
and kinds, frame/slot capacity, and the caller's eventual result depth
before changing arguments, frame state or IP. A no-argument value-returning
call must still have space to put its result in the caller's stack.

Push an ObjectHandler frame that overlaps those argument slots. Reuse entry
normalisation to narrow the parameters and establish typed-zero locals.
The suspended caller frame's VM-owned `ip` stores the byte after `call`,
and its `method` supplies the table ID for restoring its view. Publish the
callee as the active method/frame with byte offset zero, then continue the
same outer dispatch loop. Direct `call` adds no implicit receiver null
check; concrete assignability and virtual semantics remain separate work.

A failing call leaves its IP, frame chain, evaluation operands, arguments
and locals unchanged and latches the explicit failure status. No library
corruption callback substitutes for an executor validation failure.

### 11.3 Return and bounded execution

Require exactly the callee's declared return depth and kind, as before.
For a nested return, validate the suspended caller's method ID, saved IP
and result capacity before popping. The saved IP must be the boundary after
a `call`, including the method end when the call was its final instruction;
merely pointing inside the caller's code is insufficient. The current I4 return is already in
full-width evaluation form; void returns move no slot. ObjectHandler moves
the result over the consumed arguments, restores the previous frame, and
the executor resumes the caller's view/IP without returning to a recursive
C interpreter invocation. Returning from the root clears the thread IP and
reports `EXECUTOR_RETURNED` as before.

One `call` and one `ret` each consume one instruction-budget unit. Pausing
inside a callee preserves the entire managed frame chain. Recursion is
limited by supplied frame/value arrays, not by C stack depth; a capacity
failure preserves the current callee and instruction for inspection.
The existing root scanner sees caller/callee reference slots throughout.

### 11.4 Proof

Test a direct addition call, preserved lower caller values, nested calls,
void/zero-argument calls, and pauses on call/return transitions. Verify
narrow integer and `float32` argument normalisation, callee typed locals,
reference/byref root visibility and repeated calls. Cover malformed IDs,
metadata, truncation, argument/return kinds and depths, exhausted frames
and slots, caller result overflow and invalid saved continuations. Run all
prior cases plus the new call cases in sanitized Debug and Release, and
repeat host/Cortex-M4 C99/C11 source/header checks. VXB blocks remain
unchecked; a direct-fixture call is not a packed-image round trip.

### 11.5 Implementation record (2026-10-08)

`executor_start_program` and the immutable borrowed method registry are
built. Three-byte `call` operands select a validated method by direct ID.
Entry and call share metadata checking and frame-entry normalisation. Calls
preflight argument shape and caller result capacity before pushing; returns
validate caller ID, continuation boundary and capacity before popping.
Suspended state lives in the existing ObjectHandler frames, not a parallel
return stack or recursive C interpreter invocation.

All **113 executor cases** pass in sanitized Debug (ASan/UBSan) and Release
with all five ObjectHandler debug switches disabled: the 80 prior cases
and 33 direct managed-call cases. Tests cover caller value preservation,
budgeted transitions, repeated/nested/recursive calls, narrow/float32
parameters, callee typed locals, reference roots and caller-local byrefs.
Failure cases preserve the frame chain and operands on invalid metadata,
truncation, underflow/type errors, result overflow, exhausted frames/slots
and malformed callee returns or saved continuations. Separate executors
retain their own method registries and active methods.

Sixteen isolated host/Cortex-M4 C99/C11 source/header checks pass with Debug
and Release ABIs. The original Debug, sanitizers-off build configuration is
restored. ObjectHandler implementation/ABI, native dispatch, VXB and GC
are unchanged; the full ObjectHandler suite was not rerun. Existing CTest
missing-`DartConfiguration.tcl` warnings remain; both focused suite runs
returned success. Borrowed-table lifetimes and the I4/void return restriction
remain part of this slice's contract.

Next: select and contract the next instruction family before implementing
it. Remaining numeric operations and object/field access are candidates;
virtual/native dispatch, constructors, exceptions and VXB loading require
their corresponding metadata and protocols. No such extension or image
format block is claimed implemented by the direct-call tests.
