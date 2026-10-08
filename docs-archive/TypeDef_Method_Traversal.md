# Building Method Information from TypeDef

This document explains how to traverse ECMA-335 metadata tables to extract complete method information for a type, including field and method details with RVAs.

## Table Relationships Overview

```
TypeDef (0x02)
    │
    ├── FieldListIndex ──────► Field (0x04)
    │                              ├── Flags
    │                              ├── Name (→ #Strings)
    │                              └── Signature (→ #Blob)
    │
    └── MethodListIndex ─────► MethodDef (0x06)
                                   ├── RVA (code location)
                                   ├── ImplFlags
                                   ├── Flags
                                   ├── Name (→ #Strings)
                                   ├── Signature (→ #Blob)
                                   └── ParamListIndex ──► Param (0x08)
```

## Understanding List Indices

In ECMA-335 metadata, "list" columns (like `FieldList` and `MethodList`) use a **run-length encoding** scheme:

- The index points to the **first** item belonging to this type
- The **next** TypeDef's list index defines where the current type's items **end**
- For the **last** TypeDef, items run to the end of the target table

### Example: TypeDef to MethodDef Mapping

```
TypeDef Table:
┌──────┬─────────────────┬─────────────────┐
│ Row  │ TypeName        │ MethodListIndex │
├──────┼─────────────────┼─────────────────┤
│  1   │ <Module>        │       1         │  ← Methods 1 (none, since next starts at 1)
│  2   │ MyClass         │       1         │  ← Methods 1-3
│  3   │ OtherClass      │       4         │  ← Methods 4-5
│  4   │ LastClass       │       6         │  ← Methods 6 to end
└──────┴─────────────────┴─────────────────┘

MethodDef Table:
┌──────┬─────────────────┬────────────┐
│ Row  │ Name            │ RVA        │
├──────┼─────────────────┼────────────┤
│  1   │ .ctor           │ 0x00002050 │  ← MyClass
│  2   │ DoSomething     │ 0x00002080 │  ← MyClass
│  3   │ GetValue        │ 0x000020B0 │  ← MyClass
│  4   │ .ctor           │ 0x000020E0 │  ← OtherClass
│  5   │ Process         │ 0x00002110 │  ← OtherClass
│  6   │ .ctor           │ 0x00002140 │  ← LastClass
│  7   │ Execute         │ 0x00002170 │  ← LastClass
└──────┴─────────────────┴────────────┘
```

## Algorithm: Get Methods for a TypeDef

### Step 1: Get TypeDef Row

```c
// Get the decoded TypeDef row (1-indexed)
typedef_table_decoded_t* typedef_row = get_row_by_index(METADATA_TABLE_TypeDef, typedef_index);
```

### Step 2: Determine Method Range

```c
uint32_t method_start = typedef_row->MethodListIndex;
uint32_t method_end;

// Check if there's a next TypeDef row
typedef_table_decoded_t* next_typedef = get_row_by_index(METADATA_TABLE_TypeDef, typedef_index + 1);
if (next_typedef != NULL)
{
    method_end = next_typedef->MethodListIndex;
}
else
{
    // Last TypeDef: methods run to end of MethodDef table
    method_end = table_context->row_count_table[METADATA_TABLE_MethodDef] + 1;
}
```

### Step 3: Iterate Through Methods

```c
for (uint32_t method_idx = method_start; method_idx < method_end; method_idx++)
{
    methoddef_table_decoded_t* method = get_row_by_index(METADATA_TABLE_MethodDef, method_idx);

    // Access method information
    uint32_t rva = method->RVA;
    uint16_t impl_flags = method->ImplFlags;
    uint16_t flags = method->Flags;

    // Get method name from #Strings heap
    const char* method_name = get_string_from_heap(method->NameIndex);

    // Get method signature from #Blob heap
    uint32_t sig_length;
    uint8_t* signature = get_blob_from_heap(method->SignatureIndex, &sig_length);
}
```

## Algorithm: Get Fields for a TypeDef

Same pattern as methods:

```c
uint32_t field_start = typedef_row->FieldListIndex;
uint32_t field_end;

typedef_table_decoded_t* next_typedef = get_row_by_index(METADATA_TABLE_TypeDef, typedef_index + 1);
if (next_typedef != NULL)
{
    field_end = next_typedef->FieldListIndex;
}
else
{
    field_end = table_context->row_count_table[METADATA_TABLE_Field] + 1;
}

for (uint32_t field_idx = field_start; field_idx < field_end; field_idx++)
{
    field_table_decoded_t* field = get_row_by_index(METADATA_TABLE_Field, field_idx);

    const char* field_name = get_string_from_heap(field->NameIndex);
    uint32_t sig_length;
    uint8_t* signature = get_blob_from_heap(field->SignatureIndex, &sig_length);
}
```

## Algorithm: Get Parameters for a MethodDef

The Param table uses the same run-length pattern:

```c
uint32_t param_start = method->ParamListIndex;
uint32_t param_end;

methoddef_table_decoded_t* next_method = get_row_by_index(METADATA_TABLE_MethodDef, method_index + 1);
if (next_method != NULL)
{
    param_end = next_method->ParamListIndex;
}
else
{
    param_end = table_context->row_count_table[METADATA_TABLE_Param] + 1;
}

for (uint32_t param_idx = param_start; param_idx < param_end; param_idx++)
{
    param_table_decoded_t* param = get_row_by_index(METADATA_TABLE_Param, param_idx);

    uint16_t sequence = param->Sequence;  // 0 = return value, 1+ = parameters
    const char* param_name = get_string_from_heap(param->NameIndex);
}
```

## Complete Example: Building Type Information

```c
typedef struct type_info_t_
{
    const char* type_name;
    const char* type_namespace;
    uint32_t type_flags;

    uint32_t field_count;
    field_info_t* fields;

    uint32_t method_count;
    method_info_t* methods;

} type_info_t;

typedef struct method_body_t_
{
    uint16_t flags;           // Method body flags (bits 0-1: format, bit 3: more sects, bit 4: init locals)
    uint16_t max_stack;       // Maximum stack depth (8 for tiny format)
    uint32_t code_size;       // Size of IL code in bytes
    uint32_t local_var_sig_tok; // StandAloneSig token for locals (0 if none / tiny format)
    uint8_t* il_code;         // Pointer to IL bytecode (includes exception handlers for fat format)

} method_body_t;

typedef struct method_info_t_
{
    const char* name;
    uint32_t rva;             // RVA from MethodDef table
    uint16_t flags;           // MethodDef flags
    uint16_t impl_flags;      // MethodDef implementation flags
    uint8_t* signature;       // Method signature blob
    uint32_t signature_length; // Length of signature blob

    method_body_t body;       // Parsed method body

    uint32_t param_count;
    param_info_t* params;

} method_info_t;

// Parse method body at given file offset
void parse_method_body(uint8_t* file_data, uint32_t file_offset, method_body_t* body)
{
    uint8_t* ptr = file_data + file_offset;
    uint8_t header_byte = *ptr;

    if ((header_byte & 0x03) == 0x02)
    {
        // Tiny format (1-byte header)
        body->flags = 0x02;                      // Tiny format flag
        body->max_stack = 8;                     // Fixed at 8 for tiny
        body->code_size = header_byte >> 2;      // Code size in upper 6 bits
        body->local_var_sig_tok = 0;             // No locals in tiny format
        body->il_code = ptr + 1;                 // IL starts after 1-byte header
    }
    else if ((header_byte & 0x03) == 0x03)
    {
        // Fat format (12-byte header)
        uint16_t flags_and_size = *(uint16_t*)ptr;
        body->flags = flags_and_size & 0x0FFF;   // Lower 12 bits are flags
        uint8_t header_size = (flags_and_size >> 12) * 4;  // Header size in bytes

        body->max_stack = *(uint16_t*)(ptr + 2);
        body->code_size = *(uint32_t*)(ptr + 4);
        body->local_var_sig_tok = *(uint32_t*)(ptr + 8);
        body->il_code = ptr + header_size;       // IL starts after header

        // Note: Exception handlers follow IL code if (flags & 0x08)
        // They are included in the memory region after il_code + code_size
    }
}

type_info_t* build_type_info(table_context_t* ctx, pe_file_t* pe, uint8_t* file_data, uint32_t typedef_index)
{
    type_info_t* info = malloc(sizeof(type_info_t));

    // Get TypeDef row
    typedef_table_decoded_t* typedef_row = get_row(ctx, METADATA_TABLE_TypeDef, typedef_index);

    info->type_name = get_string(ctx, typedef_row->TypeNameIndex);
    info->type_namespace = get_string(ctx, typedef_row->TypeNamespaceIndex);
    info->type_flags = typedef_row->Flags;

    // Calculate method range
    uint32_t method_start = typedef_row->MethodListIndex;
    uint32_t method_end = get_next_list_index(ctx, METADATA_TABLE_TypeDef, typedef_index,
                                               METADATA_TABLE_MethodDef, offsetof(typedef_table_decoded_t, MethodListIndex));

    info->method_count = method_end - method_start;
    info->methods = malloc(sizeof(method_info_t) * info->method_count);

    // Build method info
    for (uint32_t i = 0; i < info->method_count; i++)
    {
        uint32_t method_idx = method_start + i;
        methoddef_table_decoded_t* method = get_row(ctx, METADATA_TABLE_MethodDef, method_idx);

        info->methods[i].name = get_string(ctx, method->NameIndex);
        info->methods[i].rva = method->RVA;
        info->methods[i].flags = method->Flags;
        info->methods[i].impl_flags = method->ImplFlags;
        info->methods[i].signature = get_blob(ctx, method->SignatureIndex, &info->methods[i].signature_length);

        // Parse method body if RVA is non-zero (not abstract/extern)
        if (method->RVA != 0)
        {
            uint32_t file_offset = rva_to_file_offset(pe, method->RVA);
            parse_method_body(file_data, file_offset, &info->methods[i].body);
        }
        else
        {
            // Abstract or external method - no body
            memset(&info->methods[i].body, 0, sizeof(method_body_t));
        }

        // Build parameter info (same pattern)
        uint32_t param_start = method->ParamListIndex;
        uint32_t param_end = get_next_list_index(ctx, METADATA_TABLE_MethodDef, method_idx,
                                                  METADATA_TABLE_Param, offsetof(methoddef_table_decoded_t, ParamListIndex));

        info->methods[i].param_count = param_end - param_start;
        // ... build params
    }

    // Similar for fields...

    return info;
}
```

## RVA to File Offset Conversion

The `RVA` (Relative Virtual Address) in MethodDef points to the method's IL code in memory. To read the actual code from the file:

```c
uint32_t rva_to_file_offset(pe_file_t* pe, uint32_t rva)
{
    // Find which section contains this RVA
    for (int i = 0; i < pe->section_count; i++)
    {
        section_header_t* section = &pe->sections[i];

        uint32_t section_start = section->VirtualAddress;
        uint32_t section_end = section_start + section->VirtualSize;

        if (rva >= section_start && rva < section_end)
        {
            // Calculate file offset
            uint32_t offset_in_section = rva - section->VirtualAddress;
            return section->PointerToRawData + offset_in_section;
        }
    }

    return 0; // RVA not found
}
```

## Method Body Structure (at RVA)

Once you have the file offset from RVA, the method body has this structure:

### Tiny Format (1-byte header)
If `(header_byte & 0x03) == 0x02`:
```
┌──────────────────────────────────────────┐
│ Header (1 byte)                          │
│   bits 0-1: 0x02 (tiny format)           │
│   bits 2-7: code size in bytes           │
├──────────────────────────────────────────┤
│ IL Code (code_size bytes)                │
└──────────────────────────────────────────┘
```

### Fat Format (12-byte header)
If `(header_byte & 0x03) == 0x03`:
```
┌──────────────────────────────────────────┐
│ Flags + Size (2 bytes)                   │
│   bits 0-1: 0x03 (fat format)            │
│   bits 2-3: more flags                   │
│   bits 4-7: header size / 4              │
├──────────────────────────────────────────┤
│ MaxStack (2 bytes)                       │
├──────────────────────────────────────────┤
│ CodeSize (4 bytes)                       │
├──────────────────────────────────────────┤
│ LocalVarSigTok (4 bytes)                 │
├──────────────────────────────────────────┤
│ IL Code (CodeSize bytes)                 │
├──────────────────────────────────────────┤
│ Exception Handlers (if flags indicate)   │
└──────────────────────────────────────────┘
```

## LocalVarSigTok and LOCAL_SIG Parsing

### What is LocalVarSigTok?

`LocalVarSigTok` is a **metadata token** (4 bytes) in the Fat method header that points to the method's local variable signature in the **StandAloneSig table (0x11)**.

**Token structure:**
```
┌─────────────────────────────────────┐
│ bits 24-31: Table ID (0x11)         │
│ bits 0-23:  Row index (1-based)     │
└─────────────────────────────────────┘
```

**Example:** `0x11000003` means StandAloneSig table, row 3.

If `LocalVarSigTok == 0`, the method has no local variables.

### Resolving LocalVarSigTok

```c
uint8_t* get_local_var_signature(table_context_t* ctx, uint32_t local_var_sig_tok, uint32_t* out_length)
{
    if (local_var_sig_tok == 0)
    {
        if (out_length) *out_length = 0;
        return NULL;  // No local variables
    }

    // Extract table ID and row index
    uint8_t table_id = (local_var_sig_tok >> 24) & 0xFF;
    uint32_t row_index = local_var_sig_tok & 0x00FFFFFF;

    // Verify it's a StandAloneSig token
    if (table_id != METADATA_TABLE_StandAloneSig)
    {
        if (out_length) *out_length = 0;
        return NULL;  // Invalid token
    }

    // Get the StandAloneSig row
    standalonesig_table_decoded_t* sig_row = get_row(ctx, METADATA_TABLE_StandAloneSig, row_index);

    // Get the signature blob from #Blob heap
    return get_blob(ctx, sig_row->SignatureIndex, out_length);
}
```

### LOCAL_SIG Blob Format (ECMA-335 II.23.2.6)

The signature blob has this structure:

```
LOCAL_SIG ::= 0x07 Count LocalVar*

┌─────────────────────────────────────────────────────────────┐
│ Calling Convention (1 byte): 0x07 = LOCAL_SIG               │
├─────────────────────────────────────────────────────────────┤
│ Count (compressed uint): number of local variables          │
├─────────────────────────────────────────────────────────────┤
│ LocalVar[0]: Type signature of first local                  │
├─────────────────────────────────────────────────────────────┤
│ LocalVar[1]: Type signature of second local                 │
├─────────────────────────────────────────────────────────────┤
│ ...                                                         │
├─────────────────────────────────────────────────────────────┤
│ LocalVar[Count-1]: Type signature of last local             │
└─────────────────────────────────────────────────────────────┘
```

### Compressed Unsigned Integer Format

ECMA-335 uses a variable-length encoding for unsigned integers:

| First Byte | Encoding | Range |
|------------|----------|-------|
| `0x00-0x7F` | 1 byte: value as-is | 0 - 127 |
| `0x80-0xBF` | 2 bytes: `((b0 & 0x3F) << 8) \| b1` | 128 - 16383 |
| `0xC0-0xDF` | 4 bytes: `((b0 & 0x1F) << 24) \| (b1 << 16) \| (b2 << 8) \| b3` | 16384 - 536870911 |

```c
uint32_t read_compressed_uint(uint8_t** ptr)
{
    uint8_t* p = *ptr;
    uint32_t value;

    if ((p[0] & 0x80) == 0)
    {
        // 1-byte encoding
        value = p[0];
        *ptr = p + 1;
    }
    else if ((p[0] & 0xC0) == 0x80)
    {
        // 2-byte encoding
        value = ((p[0] & 0x3F) << 8) | p[1];
        *ptr = p + 2;
    }
    else
    {
        // 4-byte encoding
        value = ((p[0] & 0x1F) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
        *ptr = p + 4;
    }

    return value;
}
```

### LocalVar Type Format

Each `LocalVar` in the signature can be:

```
LocalVar ::= [PINNED] [ByRef] Type
           | TYPEDBYREF

Where:
- PINNED    = 0x45 (optional, for fixed/pinned locals)
- ByRef     = 0x10 (optional, for ref locals)
- TYPEDBYREF = 0x16
```

### Common ELEMENT_TYPE Values

| Value | Name | Description |
|-------|------|-------------|
| 0x01 | ELEMENT_TYPE_VOID | void |
| 0x02 | ELEMENT_TYPE_BOOLEAN | bool |
| 0x03 | ELEMENT_TYPE_CHAR | char (2-byte Unicode) |
| 0x04 | ELEMENT_TYPE_I1 | sbyte |
| 0x05 | ELEMENT_TYPE_U1 | byte |
| 0x06 | ELEMENT_TYPE_I2 | short |
| 0x07 | ELEMENT_TYPE_U2 | ushort |
| 0x08 | ELEMENT_TYPE_I4 | int |
| 0x09 | ELEMENT_TYPE_U4 | uint |
| 0x0A | ELEMENT_TYPE_I8 | long |
| 0x0B | ELEMENT_TYPE_U8 | ulong |
| 0x0C | ELEMENT_TYPE_R4 | float |
| 0x0D | ELEMENT_TYPE_R8 | double |
| 0x0E | ELEMENT_TYPE_STRING | System.String |
| 0x0F | ELEMENT_TYPE_PTR | pointer type (followed by type) |
| 0x10 | ELEMENT_TYPE_BYREF | by-reference (followed by type) |
| 0x11 | ELEMENT_TYPE_VALUETYPE | value type (followed by TypeDefOrRef token) |
| 0x12 | ELEMENT_TYPE_CLASS | class type (followed by TypeDefOrRef token) |
| 0x13 | ELEMENT_TYPE_VAR | generic type parameter |
| 0x14 | ELEMENT_TYPE_ARRAY | multi-dim array |
| 0x15 | ELEMENT_TYPE_GENERICINST | generic instantiation |
| 0x18 | ELEMENT_TYPE_I | native int (IntPtr) |
| 0x19 | ELEMENT_TYPE_U | native uint (UIntPtr) |
| 0x1C | ELEMENT_TYPE_OBJECT | System.Object |
| 0x1D | ELEMENT_TYPE_SZARRAY | single-dim array (followed by element type) |
| 0x1E | ELEMENT_TYPE_MVAR | generic method parameter |
| 0x45 | ELEMENT_TYPE_PINNED | pinned local |

### Complete LOCAL_SIG Parser

```c
typedef struct local_var_t_
{
    uint8_t  is_pinned;    // 1 if PINNED modifier present
    uint8_t  is_byref;     // 1 if ByRef modifier present
    uint8_t  element_type; // ELEMENT_TYPE_* value
    uint32_t type_token;   // For CLASS/VALUETYPE: TypeDefOrRef coded index
    // Additional fields for arrays, generics, etc.
} local_var_t;

typedef struct local_sig_t_
{
    uint32_t     count;
    local_var_t* locals;
} local_sig_t;

local_sig_t* parse_local_sig(uint8_t* blob)
{
    uint8_t* ptr = blob;

    // Read blob length (compressed uint at start of blob)
    uint32_t blob_length = read_compressed_uint(&ptr);

    // Verify calling convention is LOCAL_SIG (0x07)
    uint8_t calling_conv = *ptr++;
    if (calling_conv != 0x07)
        return NULL;  // Not a LOCAL_SIG

    // Read local variable count
    uint32_t count = read_compressed_uint(&ptr);

    local_sig_t* sig = malloc(sizeof(local_sig_t));
    sig->count = count;
    sig->locals = malloc(sizeof(local_var_t) * count);

    // Parse each local variable type
    for (uint32_t i = 0; i < count; i++)
    {
        sig->locals[i].is_pinned = 0;
        sig->locals[i].is_byref = 0;

        // Check for PINNED modifier
        if (*ptr == 0x45)
        {
            sig->locals[i].is_pinned = 1;
            ptr++;
        }

        // Check for ByRef modifier
        if (*ptr == 0x10)
        {
            sig->locals[i].is_byref = 1;
            ptr++;
        }

        // Read element type
        uint8_t elem_type = *ptr++;
        sig->locals[i].element_type = elem_type;

        // Handle types that have additional data
        switch (elem_type)
        {
            case 0x11:  // ELEMENT_TYPE_VALUETYPE
            case 0x12:  // ELEMENT_TYPE_CLASS
                // Read TypeDefOrRef coded index (compressed)
                sig->locals[i].type_token = read_compressed_uint(&ptr);
                break;

            case 0x1D:  // ELEMENT_TYPE_SZARRAY
                // Next byte is the element type of the array
                // Recursively parse element type...
                break;

            case 0x13:  // ELEMENT_TYPE_VAR (generic type param)
            case 0x1E:  // ELEMENT_TYPE_MVAR (generic method param)
                // Read parameter index
                sig->locals[i].type_token = read_compressed_uint(&ptr);
                break;

            case 0x15:  // ELEMENT_TYPE_GENERICINST
                // Complex: GenericClass TypeArgCount Type*
                // Requires recursive parsing
                break;

            default:
                // Primitive types have no additional data
                sig->locals[i].type_token = 0;
                break;
        }
    }

    return sig;
}
```

### Example 1: Simple Types

Given a method with locals: `int x, string s, bool[] flags`

Blob bytes:
```
0x07        ; LOCAL_SIG calling convention
0x03        ; Count = 3 locals
0x08        ; Local 0: ELEMENT_TYPE_I4 (int)
0x0E        ; Local 1: ELEMENT_TYPE_STRING
0x1D 0x02   ; Local 2: ELEMENT_TYPE_SZARRAY + ELEMENT_TYPE_BOOLEAN (bool[])
```

Parsing result:
```
locals[0]: element_type = 0x08 (I4/int)
locals[1]: element_type = 0x0E (STRING)
locals[2]: element_type = 0x1D (SZARRAY), element_of = 0x02 (BOOLEAN)
```

### Example 2: Class and ValueType References

Given a method with locals: `int count, MyClass obj, DateTime timestamp`

Where:
- `MyClass` is at TypeDef row 5
- `DateTime` is at TypeRef row 12

Blob bytes:
```
0x07        ; LOCAL_SIG calling convention
0x03        ; Count = 3 locals
0x08        ; Local 0: ELEMENT_TYPE_I4 (int)
0x12 0x14   ; Local 1: ELEMENT_TYPE_CLASS + TypeDefOrRef coded index
0x11 0x31   ; Local 2: ELEMENT_TYPE_VALUETYPE + TypeDefOrRef coded index
```

**Understanding the TypeDefOrRef coded index:**

TypeDefOrRef is a coded index with a 2-bit tag:
| Tag | Table |
|-----|-------|
| 0 | TypeDef |
| 1 | TypeRef |
| 2 | TypeSpec |

Encoding: `(row_index << 2) | tag`

For `MyClass` (TypeDef row 5):
```
coded_index = (5 << 2) | 0 = 20 = 0x14
```

For `DateTime` (TypeRef row 12):
```
coded_index = (12 << 2) | 1 = 49 = 0x31
```

Parsing result:
```
locals[0]: element_type = 0x08 (I4/int)
locals[1]: element_type = 0x12 (CLASS), type_token = 0x14
           → Decode: tag = 0x14 & 0x03 = 0 (TypeDef)
                     row = 0x14 >> 2 = 5
           → TypeDef row 5 = MyClass
locals[2]: element_type = 0x11 (VALUETYPE), type_token = 0x31
           → Decode: tag = 0x31 & 0x03 = 1 (TypeRef)
                     row = 0x31 >> 2 = 12
           → TypeRef row 12 = System.DateTime
```

### Example 3: Generic Type Instance

Given a method with local: `List<int> numbers`

Where `List<T>` is at TypeRef row 8.

Blob bytes:
```
0x07              ; LOCAL_SIG calling convention
0x01              ; Count = 1 local
0x15              ; ELEMENT_TYPE_GENERICINST
  0x12            ; ELEMENT_TYPE_CLASS (List is a class)
  0x21            ; TypeDefOrRef: (8 << 2) | 1 = TypeRef row 8
  0x01            ; GenArgCount = 1 type argument
  0x08            ; Type arg 0: ELEMENT_TYPE_I4 (int)
```

Parsing result:
```
locals[0]: element_type = 0x15 (GENERICINST)
           generic_type = CLASS, TypeRef row 8 (List<>)
           type_arg_count = 1
           type_args[0] = 0x08 (I4/int)
           → List<int>
```

### Decoding TypeDefOrRef Helper

```c
typedef struct type_ref_t_
{
    uint8_t  table;      // 0=TypeDef, 1=TypeRef, 2=TypeSpec
    uint32_t row_index;  // 1-based row index
} type_ref_t;

type_ref_t decode_typedef_or_ref(uint32_t coded_index)
{
    type_ref_t ref;
    ref.table = coded_index & 0x03;      // Low 2 bits = tag
    ref.row_index = coded_index >> 2;    // Remaining bits = row
    return ref;
}

const char* get_type_name(table_context_t* ctx, uint32_t coded_index)
{
    type_ref_t ref = decode_typedef_or_ref(coded_index);

    switch (ref.table)
    {
        case 0:  // TypeDef
        {
            typedef_table_decoded_t* row = get_row(ctx, METADATA_TABLE_TypeDef, ref.row_index);
            return get_string(ctx, row->TypeNameIndex);
        }
        case 1:  // TypeRef
        {
            typeref_table_decoded_t* row = get_row(ctx, METADATA_TABLE_TypeRef, ref.row_index);
            return get_string(ctx, row->TypeNameIndex);
        }
        case 2:  // TypeSpec
            return "<TypeSpec>";  // Requires further signature parsing
    }
    return NULL;
}
```

## Summary: Table Traversal Pattern

1. **TypeDef** → contains `FieldListIndex` and `MethodListIndex`
2. **FieldListIndex** → first Field row for this type
3. **MethodListIndex** → first MethodDef row for this type
4. **MethodDef.ParamListIndex** → first Param row for this method
5. **MethodDef.RVA** → file location of IL code

The "next row's index minus current row's index" pattern is fundamental to navigating these relationships.

## Related Tables

| Table | Relationship |
|-------|-------------|
| TypeDef (0x02) | Defines types with field/method lists |
| Field (0x04) | Field definitions, referenced by TypeDef.FieldList |
| MethodDef (0x06) | Method definitions with RVA, referenced by TypeDef.MethodList |
| Param (0x08) | Parameter definitions, referenced by MethodDef.ParamList |
| MemberRef (0x0A) | References to methods/fields in other assemblies |
| MethodSemantics (0x18) | Links properties/events to getter/setter methods |
| MethodImpl (0x19) | Interface method implementations |
