# Adding New ECMA-335 Metadata Table Decoders

This document describes the process of implementing a new metadata table decoder for the CLI parser project. This follows the ECMA-335 specification for .NET metadata tables.

## Overview

Each metadata table in a .NET assembly has a specific structure defined by ECMA-335. The decoder reads raw bytes from the table row buffer and populates a structured type with the parsed values.

## File Structure

```
source/
├── metadata_table_types.h      # Table ID enum definitions (METADATA_TABLE_*)
├── metadata_table.h            # table_context_t and table_info_t definitions
├── metadata_table.c            # metadata_table_add_row() implementation
└── row_table/
    ├── parser_table_types.h    # Decoded struct type definitions (*_table_decoded_t)
    ├── parser_table_data.h     # Function declarations and table_map array
    └── *_table.c               # Individual decoder implementations
```

## Steps to Add a New Table Decoder

### Step 1: Identify the Table

Find the table ID in `source/metadata_table_types.h`:

```c
enum
{
    METADATA_TABLE_Module                 = 0x00,
    METADATA_TABLE_TypeRef                = 0x01,
    // ... etc
    METADATA_TABLE_NestedClass            = 0x29,
    // ...
};
```

### Step 2: Define the Decoded Structure Type

Add the structure definition to `source/row_table/parser_table_types.h`.

**Naming Convention**: `{tablename}_table_decoded_t`

**Example** (FieldRVA table 0x1D):
```c
// Table 0x1D - FieldRVA
typedef struct fieldrva_table_decoded_t_
{
    uint32_t RVA;         // Relative Virtual Address
    uint32_t FieldIndex;  // Index into Field table
} fieldrva_table_decoded_t;
```

### Step 3: Create the Decoder Source File

Create `source/row_table/{tablename}_table.c`

**Template**:
```c
#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_{tablename}(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    {tablename}_table_decoded_t* out;

    out = malloc(sizeof({tablename}_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for {tablename} table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;

    // Calculate index sizes based on HeapSizes and row counts
    // ... (see Field Type Reference below)

    // Read each field
    // ... (see Field Reading Patterns below)

    metadata_table_add_row(table_info, out);

    return offset;  // Return total bytes consumed
}
```

### Step 4: Add Function Declaration

In `source/row_table/parser_table_data.h`, add the function declaration in the declarations section:

```c
uint32_t metadata_table_decode_{tablename}(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
```

### Step 5: Update the table_map Array

In `source/row_table/parser_table_data.h`, update the `table_map[]` array:

```c
static const table_map_t table_map[] =
{
    // ... existing entries
    { METADATA_TABLE_{TableName}, metadata_table_decode_{tablename} },
    // ...
};
```

**Replace any existing NULL entry** with the function pointer.

### Step 6: Add Source to CMakeLists.txt

In `CMakeLists.txt`, add the new source file to `SOURCE_FILES`:

```cmake
set(SOURCE_FILES        ...
                        ./source/row_table/{tablename}_table.c
                        )
```

---

## Field Type Reference

### Heap Index Sizes (from HeapSizes byte)

```c
// #Strings heap index size
uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;

// #GUID heap index size
uint32_t guid_index_size = (table_context->tables_stream_header->HeapSizes & 0x02) ? 4 : 2;

// #Blob heap index size
uint32_t blob_index_size = (table_context->tables_stream_header->HeapSizes & 0x04) ? 4 : 2;
```

### Simple Table Index Size

For a simple index into another table:

```c
// Index into TypeDef table
uint32_t typedef_index_size = (table_context->row_count_table[METADATA_TABLE_TypeDef] < (1 << 16)) ? 2 : 4;
```

### Coded Index Sizes

Coded indices encode both table type and row index. The tag bits determine which table the index refers to.

**Common Coded Index Patterns:**

| Coded Index | Tag Bits | Tables | Size Calculation |
|-------------|----------|--------|------------------|
| TypeDefOrRef | 2 | TypeDef, TypeRef, TypeSpec | max < (1 << 14) ? 2 : 4 |
| HasConstant | 2 | Field, Param, Property | max < (1 << 14) ? 2 : 4 |
| HasCustomAttribute | 5 | Many tables | max < (1 << 11) ? 2 : 4 |
| HasSemantics | 1 | Event, Property | max < (1 << 15) ? 2 : 4 |
| MemberRefParent | 3 | TypeDef, TypeRef, ModuleRef, MethodDef, TypeSpec | max < (1 << 13) ? 2 : 4 |
| MethodDefOrRef | 1 | MethodDef, MemberRef | max < (1 << 15) ? 2 : 4 |
| ResolutionScope | 2 | Module, ModuleRef, AssemblyRef, TypeRef | max < (1 << 14) ? 2 : 4 |
| CustomAttributeType | 3 | MethodDef, MemberRef (only 2 used) | max < (1 << 13) ? 2 : 4 |

**Example** (TypeDefOrRef):
```c
uint32_t typedef_or_ref_max = table_context->row_count_table[METADATA_TABLE_TypeDef];
if (table_context->row_count_table[METADATA_TABLE_TypeRef] > typedef_or_ref_max)
    typedef_or_ref_max = table_context->row_count_table[METADATA_TABLE_TypeRef];
if (table_context->row_count_table[METADATA_TABLE_TypeSpec] > typedef_or_ref_max)
    typedef_or_ref_max = table_context->row_count_table[METADATA_TABLE_TypeSpec];
uint32_t typedef_or_ref_index_size = (typedef_or_ref_max < (1 << 14)) ? 2 : 4;
```

---

## Field Reading Patterns

### Fixed-Size Fields

```c
// 2-byte field (uint16)
out->Flags = *(uint16_t*)(row_data + offset);
offset += 2;

// 4-byte field (uint32)
out->RVA = *(uint32_t*)(row_data + offset);
offset += 4;

// 1-byte field (uint8)
out->Type = *(uint8_t*)(row_data + offset);
offset += 1;
```

### Variable-Size Index Fields

```c
// String heap index
if (string_index_size == 4)
{
    out->NameIndex = *(uint32_t*)(row_data + offset);
}
else
{
    out->NameIndex = *(uint16_t*)(row_data + offset);
}
offset += string_index_size;

// Table index (same pattern)
if (typedef_index_size == 4)
{
    out->ParentIndex = *(uint32_t*)(row_data + offset);
}
else
{
    out->ParentIndex = *(uint16_t*)(row_data + offset);
}
offset += typedef_index_size;
```

---

## Complete Example: FieldRVA Table (0x1D)

### Structure Definition (parser_table_types.h)
```c
// Table 0x1D - FieldRVA
typedef struct fieldrva_table_decoded_t_
{
    uint32_t RVA;         // Relative Virtual Address
    uint32_t FieldIndex;  // Index into Field table
} fieldrva_table_decoded_t;
```

### Decoder Implementation (fieldrva_table.c)
```c
#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_fieldrva(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    fieldrva_table_decoded_t* out;

    out = malloc(sizeof(fieldrva_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for fieldrva table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;

    // Calculate Field table index size
    uint32_t field_index_size = (table_context->row_count_table[METADATA_TABLE_Field] < (1 << 16)) ? 2 : 4;

    // Read RVA (always 4 bytes)
    out->RVA = *(uint32_t*)(row_data + offset);
    offset += 4;

    // Read Field (index into Field table)
    if (field_index_size == 4)
    {
        out->FieldIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->FieldIndex = *(uint16_t*)(row_data + offset);
    }
    offset += field_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
```

---

## Tables Still TODO

The following tables in `table_map[]` have `NULL` decoders and need implementation:

| Table ID | Name | Notes |
|----------|------|-------|
| 0x0D | FieldMarshal | HasFieldMarshal coded index + Blob |
| 0x0E | DeclSecurity | HasDeclSecurity coded index + Action + Blob |
| 0x12 | EventMap | TypeDef + Event index |
| 0x27 | ExportedType | Flags + TypeDefId + Strings (2x) + Implementation coded |
| 0x28 | ManifestResource | Offset + Flags + String + Implementation coded |
| 0x2C | GenericParamConstraint | GenericParam index + TypeDefOrRef coded |

---

## Reference

- ECMA-335 Section II.22 defines all metadata table structures
- See `docs/ECMA-335_6th_edition_june_2012.pdf` for the full specification
- See `docs/Module_Table_Definition.md` for a detailed example of table structure analysis
