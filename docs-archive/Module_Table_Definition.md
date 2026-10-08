# ECMA-335 Module Table (0x00) Definition

## Overview

The **Module** table is defined in **ECMA-335 Section II.22.30**. It is always the first metadata table (table ID `0x00`) and contains exactly **one row** per assembly. The Module table stores information that identifies the current module.

## Table Structure

| Column Index | Column Name | Type | Description |
|--------------|-------------|------|-------------|
| 0 | **Generation** | `uint16` (2 bytes) | Reserved, shall be zero in conforming implementations |
| 1 | **Name** | `StringIndex` (2 or 4 bytes) | Index into the `#Strings` heap; the module name (with extension, e.g., "MyModule.dll") |
| 2 | **Mvid** | `GuidIndex` (2 or 4 bytes) | Index into the `#GUID` heap; a unique identifier (Module Version ID) |
| 3 | **EncId** | `GuidIndex` (2 or 4 bytes) | Index into the `#GUID` heap; reserved, shall be zero |
| 4 | **EncBaseId** | `GuidIndex` (2 or 4 bytes) | Index into the `#GUID` heap; reserved, shall be zero |

## Column Details

### Generation (Offset +0x00)
- **Size**: Always 2 bytes (`uint16`)
- **Value**: Must be 0 for conforming implementations
- **Purpose**: Reserved for future use (Edit-and-Continue scenarios)

### Name (String Index)
- **Size**: 2 or 4 bytes, determined by `HeapSizes` bit 0
- **Points to**: `#Strings` heap
- **Value**: The filename of the module with extension (e.g., "MyAssembly.dll")

### Mvid (GUID Index)
- **Size**: 2 or 4 bytes, determined by `HeapSizes` bit 1
- **Points to**: `#GUID` heap
- **Value**: A GUID that uniquely identifies this specific build of the module
- **Note**: Re-compiling the same source typically generates a new Mvid

### EncId (GUID Index)
- **Size**: 2 or 4 bytes, determined by `HeapSizes` bit 1
- **Points to**: `#GUID` heap
- **Value**: Reserved, shall be zero (used in Edit-and-Continue)

### EncBaseId (GUID Index)
- **Size**: 2 or 4 bytes, determined by `HeapSizes` bit 1
- **Points to**: `#GUID` heap
- **Value**: Reserved, shall be zero (used in Edit-and-Continue)

## HeapSizes Field

The `HeapSizes` field is located in the `#~` stream header (`metadata_table_root_t.HeapSizes`). It determines the size of heap indices:

| Bit | Meaning | If Set (1) | If Clear (0) |
|-----|---------|------------|--------------|
| 0 | `#Strings` heap size | 4-byte indices | 2-byte indices |
| 1 | `#GUID` heap size | 4-byte indices | 2-byte indices |
| 2 | `#Blob` heap size | 4-byte indices | 2-byte indices |
| 3-7 | Reserved | — | — |

## Row Size Calculation

The Module table row size varies depending on heap sizes:

```c
uint32_t calculate_module_row_size(uint8_t HeapSizes)
{
    uint32_t string_index_size = (HeapSizes & 0x01) ? 4 : 2;
    uint32_t guid_index_size   = (HeapSizes & 0x02) ? 4 : 2;

    // Generation (2) + Name (string) + Mvid (guid) + EncId (guid) + EncBaseId (guid)
    uint32_t row_size = 2                       // Generation: always 2 bytes
                      + string_index_size       // Name: 2 or 4 bytes
                      + guid_index_size         // Mvid: 2 or 4 bytes
                      + guid_index_size         // EncId: 2 or 4 bytes
                      + guid_index_size;        // EncBaseId: 2 or 4 bytes

    return row_size;
}
```

### Possible Row Sizes

| HeapSizes & 0x03 | String Index | GUID Index | Total Row Size |
|------------------|--------------|------------|----------------|
| `0x00` | 2 bytes | 2 bytes | 2 + 2 + 2 + 2 + 2 = **10 bytes** |
| `0x01` | 4 bytes | 2 bytes | 2 + 4 + 2 + 2 + 2 = **12 bytes** |
| `0x02` | 2 bytes | 4 bytes | 2 + 2 + 4 + 4 + 4 = **16 bytes** |
| `0x03` | 4 bytes | 4 bytes | 2 + 4 + 4 + 4 + 4 = **18 bytes** |

## Memory Layout Examples

### Minimum Size (10 bytes): HeapSizes = 0x00
```
Offset  Size    Field
+0x00   2       Generation (uint16)
+0x02   2       Name (StringIndex, 2-byte)
+0x04   2       Mvid (GuidIndex, 2-byte)
+0x06   2       EncId (GuidIndex, 2-byte)
+0x08   2       EncBaseId (GuidIndex, 2-byte)
─────────────────────────────────────────────
Total:  10 bytes
```

### Maximum Size (18 bytes): HeapSizes = 0x03
```
Offset  Size    Field
+0x00   2       Generation (uint16)
+0x02   4       Name (StringIndex, 4-byte)
+0x06   4       Mvid (GuidIndex, 4-byte)
+0x0A   4       EncId (GuidIndex, 4-byte)
+0x0E   4       EncBaseId (GuidIndex, 4-byte)
─────────────────────────────────────────────
Total:  18 bytes
```

## Data Structure Definition

```c
// Variable-size structure - compute offsets dynamically
typedef struct module_table_row_t_
{
    uint16_t Generation;     // Always at offset 0, always 2 bytes
    // Following fields have variable offsets based on HeapSizes:
    // Name:      StringIndex (2 or 4 bytes)
    // Mvid:      GuidIndex   (2 or 4 bytes)
    // EncId:     GuidIndex   (2 or 4 bytes)
    // EncBaseId: GuidIndex   (2 or 4 bytes)
} module_table_row_t;

// Helper structure for decoded values
typedef struct module_table_decoded_t_
{
    uint16_t Generation;
    uint32_t NameIndex;       // Index into #Strings heap
    uint32_t MvidIndex;       // Index into #GUID heap
    uint32_t EncIdIndex;      // Index into #GUID heap
    uint32_t EncBaseIdIndex;  // Index into #GUID heap
} module_table_decoded_t;
```

## Decoding Example

```c
bool_t decode_module_table(uint8_t* row_data, uint8_t HeapSizes, module_table_decoded_t* out)
{
    uint32_t offset = 0;
    uint32_t string_index_size = (HeapSizes & 0x01) ? 4 : 2;
    uint32_t guid_index_size   = (HeapSizes & 0x02) ? 4 : 2;

    // Read Generation (always 2 bytes, little-endian)
    out->Generation = *(uint16_t*)(row_data + offset);
    offset += 2;

    // Read Name (StringIndex)
    if (string_index_size == 4) {
        out->NameIndex = *(uint32_t*)(row_data + offset);
    } else {
        out->NameIndex = *(uint16_t*)(row_data + offset);
    }
    offset += string_index_size;

    // Read Mvid (GuidIndex)
    if (guid_index_size == 4) {
        out->MvidIndex = *(uint32_t*)(row_data + offset);
    } else {
        out->MvidIndex = *(uint16_t*)(row_data + offset);
    }
    offset += guid_index_size;

    // Read EncId (GuidIndex)
    if (guid_index_size == 4) {
        out->EncIdIndex = *(uint32_t*)(row_data + offset);
    } else {
        out->EncIdIndex = *(uint16_t*)(row_data + offset);
    }
    offset += guid_index_size;

    // Read EncBaseId (GuidIndex)
    if (guid_index_size == 4) {
        out->EncBaseIdIndex = *(uint32_t*)(row_data + offset);
    } else {
        out->EncBaseIdIndex = *(uint16_t*)(row_data + offset);
    }

    return TRUE;
}
```

## Validation Rules (ECMA-335 II.22.30)

1. The Module table **shall** contain exactly one row
2. `Generation` **shall** be zero
3. `Name` **shall** index a non-empty string in the `#Strings` heap
4. `Mvid` **shall** index a non-null GUID in the `#GUID` heap
5. `EncId` **should** be zero (Edit-and-Continue, not used in normal assemblies)
6. `EncBaseId` **should** be zero (Edit-and-Continue, not used in normal assemblies)

## References

- ECMA-335 6th Edition, June 2012
  - Section II.22.30: Module (Table 0x00)
  - Section II.24.2.6: #~ stream (HeapSizes definition)
  - Section II.24.2.3: #Strings heap
  - Section II.24.2.5: #GUID heap
