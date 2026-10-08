# Signature Decoding in ECMA-335

This document describes how to decode method and field signatures from the #Blob heap in .NET metadata.

## Overview

Signatures in ECMA-335 are stored as byte arrays in the #Blob heap. The signature format varies depending on the type:

| Signature Type | First Byte | Description |
|----------------|-----------|-------------|
| Field          | `0x06`    | Single type |
| Method (static)| `0x00`    | Return + params |
| Method (instance)| `0x20`  | HASTHIS + return + params |
| Method (generic)| `0x10`   | GenParamCount + return + params |
| LocalVar       | `0x07`    | Local variable signature |
| Property       | `0x08`    | Property signature |

## Inputs

```c
uint8_t * signature;    // Pointer to signature blob data
uint32_t length;        // Length of signature blob
```

## Dependencies

- `metadata_decompress_integer()` from `metadata_table.h`
- Element type constants from `methods/element_types.h`

---

## Suggested Implementation

### Header File: `source/signature_decoder.h`

```c
#ifndef __SIGNATURE_DECODER_H__
#define __SIGNATURE_DECODER_H__

#include "types.h"
#include "methods/element_types.h"
#include "metadata_table.h"

// Forward declarations
typedef struct decoded_type_t_ decoded_type_t;
typedef struct decoded_method_sig_t_ decoded_method_sig_t;
typedef struct decoded_field_sig_t_ decoded_field_sig_t;

/**
 * Represents a decoded type from a signature.
 * Types can be recursive (e.g., SZARRAY of CLASS).
 */
struct decoded_type_t_
{
    uint8_t element_type;           // ELEMENT_TYPE_* value

    // For CLASS, VALUETYPE: TypeDefOrRef coded index
    uint32_t type_ref_coded_index;
    uint8_t type_ref_table;         // 0=TypeDef, 1=TypeRef, 2=TypeSpec
    uint32_t type_ref_row;          // Row index (1-based)

    // For VAR, MVAR: generic parameter index
    uint32_t generic_param_index;

    // For SZARRAY, PTR, BYREF: nested type
    decoded_type_t * nested_type;

    // For GENERICINST: base type + type arguments
    decoded_type_t * generic_base_type;
    uint32_t generic_arg_count;
    decoded_type_t ** generic_args;

    // For ARRAY: element type + shape info
    uint32_t array_rank;
    uint32_t * array_sizes;
    uint32_t array_sizes_count;
    uint32_t * array_lower_bounds;
    uint32_t array_lower_bounds_count;

    // For FNPTR: method signature
    decoded_method_sig_t * fn_ptr_sig;

    // For CMOD_REQD, CMOD_OPT: modifier type + underlying type
    decoded_type_t * modified_type;
};

/**
 * Represents a decoded method signature.
 */
struct decoded_method_sig_t_
{
    uint8_t calling_convention;     // Raw calling convention byte
    bool_t has_this;                // Instance method
    bool_t explicit_this;           // Explicit this parameter
    bool_t is_generic;              // Generic method
    bool_t is_vararg;               // Vararg method

    uint32_t generic_param_count;   // Number of generic type parameters
    uint32_t param_count;           // Number of parameters

    decoded_type_t * return_type;   // Return type
    decoded_type_t ** param_types;  // Array of parameter types

    // For vararg: sentinel position and extra args
    uint32_t sentinel_position;     // Index where SENTINEL appears (-1 if none)
};

/**
 * Represents a decoded field signature.
 */
struct decoded_field_sig_t_
{
    decoded_type_t * field_type;    // The field's type
};

/**
 * Decode context - tracks position in signature blob.
 */
typedef struct sig_decode_ctx_t_
{
    const uint8_t * data;           // Signature blob data
    uint32_t length;                // Total length
    uint32_t offset;                // Current position
} sig_decode_ctx_t;

// Main decoding functions
bool_t signature_decode_method(const uint8_t * signature, uint32_t length,
                               decoded_method_sig_t ** out_sig);
bool_t signature_decode_field(const uint8_t * signature, uint32_t length,
                              decoded_field_sig_t ** out_sig);

// Type decoding (recursive)
bool_t signature_decode_type(sig_decode_ctx_t * ctx, decoded_type_t ** out_type);

// Cleanup
void signature_free_type(decoded_type_t * type);
void signature_free_method_sig(decoded_method_sig_t * sig);
void signature_free_field_sig(decoded_field_sig_t * sig);

// Utility: get element type name for debugging
const char * signature_element_type_name(uint8_t element_type);

#endif // __SIGNATURE_DECODER_H__
```

---

### Implementation File: `source/signature_decoder.c`

```c
#include "signature_decoder.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

//------------------------------------------------------------------------------
// Helper: Read a byte from context
//------------------------------------------------------------------------------
static bool_t ctx_read_byte(sig_decode_ctx_t * ctx, uint8_t * out)
{
    if (ctx->offset >= ctx->length)
    {
        return FALSE;
    }
    *out = ctx->data[ctx->offset];
    ctx->offset++;
    return TRUE;
}

//------------------------------------------------------------------------------
// Helper: Read a compressed integer from context
//------------------------------------------------------------------------------
static bool_t ctx_read_compressed_uint(sig_decode_ctx_t * ctx, uint32_t * out)
{
    if (ctx->offset >= ctx->length)
    {
        return FALSE;
    }

    uint32_t bytes_read = metadata_decompress_integer(ctx->data + ctx->offset, out);
    if (bytes_read == 0)
    {
        return FALSE;
    }

    ctx->offset += bytes_read;
    return TRUE;
}

//------------------------------------------------------------------------------
// Helper: Allocate and initialize a decoded_type_t
//------------------------------------------------------------------------------
static decoded_type_t * alloc_type(void)
{
    decoded_type_t * type = (decoded_type_t *)calloc(1, sizeof(decoded_type_t));
    return type;
}

//------------------------------------------------------------------------------
// Decode TypeDefOrRefEncoded (compressed coded index)
//------------------------------------------------------------------------------
static bool_t decode_type_def_or_ref(sig_decode_ctx_t * ctx, decoded_type_t * type)
{
    uint32_t coded_index;
    if (!ctx_read_compressed_uint(ctx, &coded_index))
    {
        return FALSE;
    }

    type->type_ref_coded_index = coded_index;
    type->type_ref_table = coded_index & 0x03;  // 2-bit tag: 0=TypeDef, 1=TypeRef, 2=TypeSpec
    type->type_ref_row = coded_index >> 2;       // Row index (1-based)

    return TRUE;
}

//------------------------------------------------------------------------------
// Forward declaration for recursive type decoding
//------------------------------------------------------------------------------
static bool_t decode_type_internal(sig_decode_ctx_t * ctx, decoded_type_t * type);

//------------------------------------------------------------------------------
// Decode ARRAY type (multi-dimensional)
// Format: ELEMENT_TYPE_ARRAY Type ArrayShape
// ArrayShape: Rank NumSizes Size* NumLoBounds LoBound*
//------------------------------------------------------------------------------
static bool_t decode_array_shape(sig_decode_ctx_t * ctx, decoded_type_t * type)
{
    // Read rank
    if (!ctx_read_compressed_uint(ctx, &type->array_rank))
    {
        return FALSE;
    }

    // Read number of sizes
    uint32_t num_sizes;
    if (!ctx_read_compressed_uint(ctx, &num_sizes))
    {
        return FALSE;
    }

    type->array_sizes_count = num_sizes;
    if (num_sizes > 0)
    {
        type->array_sizes = (uint32_t *)calloc(num_sizes, sizeof(uint32_t));
        for (uint32_t i = 0; i < num_sizes; i++)
        {
            if (!ctx_read_compressed_uint(ctx, &type->array_sizes[i]))
            {
                return FALSE;
            }
        }
    }

    // Read number of lower bounds
    uint32_t num_lobounds;
    if (!ctx_read_compressed_uint(ctx, &num_lobounds))
    {
        return FALSE;
    }

    type->array_lower_bounds_count = num_lobounds;
    if (num_lobounds > 0)
    {
        type->array_lower_bounds = (uint32_t *)calloc(num_lobounds, sizeof(uint32_t));
        for (uint32_t i = 0; i < num_lobounds; i++)
        {
            // Note: lower bounds are signed compressed integers
            if (!ctx_read_compressed_uint(ctx, &type->array_lower_bounds[i]))
            {
                return FALSE;
            }
        }
    }

    return TRUE;
}

//------------------------------------------------------------------------------
// Decode GENERICINST type
// Format: ELEMENT_TYPE_GENERICINST (CLASS|VALUETYPE) TypeDefOrRefEncoded GenArgCount Type*
//------------------------------------------------------------------------------
static bool_t decode_generic_inst(sig_decode_ctx_t * ctx, decoded_type_t * type)
{
    // Read base type (CLASS or VALUETYPE)
    type->generic_base_type = alloc_type();
    if (type->generic_base_type == NULL)
    {
        return FALSE;
    }

    uint8_t base_element_type;
    if (!ctx_read_byte(ctx, &base_element_type))
    {
        return FALSE;
    }

    if (base_element_type != ELEMENT_TYPE_CLASS && base_element_type != ELEMENT_TYPE_VALUETYPE)
    {
        printf("Error: GENERICINST base must be CLASS or VALUETYPE, got 0x%02X\n", base_element_type);
        return FALSE;
    }

    type->generic_base_type->element_type = base_element_type;
    if (!decode_type_def_or_ref(ctx, type->generic_base_type))
    {
        return FALSE;
    }

    // Read generic argument count
    if (!ctx_read_compressed_uint(ctx, &type->generic_arg_count))
    {
        return FALSE;
    }

    // Allocate and decode generic arguments
    if (type->generic_arg_count > 0)
    {
        type->generic_args = (decoded_type_t **)calloc(type->generic_arg_count, sizeof(decoded_type_t *));
        if (type->generic_args == NULL)
        {
            return FALSE;
        }

        for (uint32_t i = 0; i < type->generic_arg_count; i++)
        {
            type->generic_args[i] = alloc_type();
            if (type->generic_args[i] == NULL)
            {
                return FALSE;
            }
            if (!decode_type_internal(ctx, type->generic_args[i]))
            {
                return FALSE;
            }
        }
    }

    return TRUE;
}

//------------------------------------------------------------------------------
// Decode a single Type (recursive)
//------------------------------------------------------------------------------
static bool_t decode_type_internal(sig_decode_ctx_t * ctx, decoded_type_t * type)
{
    uint8_t element_type;
    if (!ctx_read_byte(ctx, &element_type))
    {
        return FALSE;
    }

    type->element_type = element_type;

    switch (element_type)
    {
        // Primitive types - no additional data
        case ELEMENT_TYPE_VOID:
        case ELEMENT_TYPE_BOOLEAN:
        case ELEMENT_TYPE_CHAR:
        case ELEMENT_TYPE_I1:
        case ELEMENT_TYPE_U1:
        case ELEMENT_TYPE_I2:
        case ELEMENT_TYPE_U2:
        case ELEMENT_TYPE_I4:
        case ELEMENT_TYPE_U4:
        case ELEMENT_TYPE_I8:
        case ELEMENT_TYPE_U8:
        case ELEMENT_TYPE_R4:
        case ELEMENT_TYPE_R8:
        case ELEMENT_TYPE_STRING:
        case ELEMENT_TYPE_OBJECT:
        case ELEMENT_TYPE_I:
        case ELEMENT_TYPE_U:
        case ELEMENT_TYPE_TYPEDBYREF:
            // No additional data needed
            break;

        // CLASS and VALUETYPE: followed by TypeDefOrRefEncoded
        case ELEMENT_TYPE_CLASS:
        case ELEMENT_TYPE_VALUETYPE:
            if (!decode_type_def_or_ref(ctx, type))
            {
                return FALSE;
            }
            break;

        // PTR and BYREF: followed by Type
        case ELEMENT_TYPE_PTR:
        case ELEMENT_TYPE_BYREF:
            type->nested_type = alloc_type();
            if (type->nested_type == NULL)
            {
                return FALSE;
            }
            if (!decode_type_internal(ctx, type->nested_type))
            {
                return FALSE;
            }
            break;

        // SZARRAY: followed by Type (single-dimensional zero-based array)
        case ELEMENT_TYPE_SZARRAY:
            type->nested_type = alloc_type();
            if (type->nested_type == NULL)
            {
                return FALSE;
            }
            if (!decode_type_internal(ctx, type->nested_type))
            {
                return FALSE;
            }
            break;

        // ARRAY: followed by Type and ArrayShape
        case ELEMENT_TYPE_ARRAY:
            type->nested_type = alloc_type();
            if (type->nested_type == NULL)
            {
                return FALSE;
            }
            if (!decode_type_internal(ctx, type->nested_type))
            {
                return FALSE;
            }
            if (!decode_array_shape(ctx, type))
            {
                return FALSE;
            }
            break;

        // VAR and MVAR: followed by compressed integer (generic param index)
        case ELEMENT_TYPE_VAR:
        case ELEMENT_TYPE_MVAR:
            if (!ctx_read_compressed_uint(ctx, &type->generic_param_index))
            {
                return FALSE;
            }
            break;

        // GENERICINST: generic instantiation
        case ELEMENT_TYPE_GENERICINST:
            if (!decode_generic_inst(ctx, type))
            {
                return FALSE;
            }
            break;

        // FNPTR: followed by full method signature
        case ELEMENT_TYPE_FNPTR:
            // Decode inline method signature
            type->fn_ptr_sig = (decoded_method_sig_t *)calloc(1, sizeof(decoded_method_sig_t));
            if (type->fn_ptr_sig == NULL)
            {
                return FALSE;
            }
            // Note: Need to decode method signature inline (same format as MethodDefSig)
            // This requires calling the method signature decoder
            printf("Warning: FNPTR signature decoding not fully implemented\n");
            return FALSE;

        // CMOD_REQD and CMOD_OPT: custom modifiers
        case ELEMENT_TYPE_CMOD_REQD:
        case ELEMENT_TYPE_CMOD_OPT:
            // Read modifier type reference
            if (!decode_type_def_or_ref(ctx, type))
            {
                return FALSE;
            }
            // Then read the underlying type
            type->modified_type = alloc_type();
            if (type->modified_type == NULL)
            {
                return FALSE;
            }
            if (!decode_type_internal(ctx, type->modified_type))
            {
                return FALSE;
            }
            break;

        // PINNED: used in local variable signatures
        case ELEMENT_TYPE_PINNED:
            type->nested_type = alloc_type();
            if (type->nested_type == NULL)
            {
                return FALSE;
            }
            if (!decode_type_internal(ctx, type->nested_type))
            {
                return FALSE;
            }
            break;

        default:
            printf("Error: Unknown element type 0x%02X at offset %u\n", element_type, ctx->offset - 1);
            return FALSE;
    }

    return TRUE;
}

//------------------------------------------------------------------------------
// Public: Decode a Type
//------------------------------------------------------------------------------
bool_t signature_decode_type(sig_decode_ctx_t * ctx, decoded_type_t ** out_type)
{
    *out_type = alloc_type();
    if (*out_type == NULL)
    {
        return FALSE;
    }

    if (!decode_type_internal(ctx, *out_type))
    {
        signature_free_type(*out_type);
        *out_type = NULL;
        return FALSE;
    }

    return TRUE;
}

//------------------------------------------------------------------------------
// Public: Decode a method signature
//------------------------------------------------------------------------------
bool_t signature_decode_method(const uint8_t * signature, uint32_t length,
                               decoded_method_sig_t ** out_sig)
{
    if (signature == NULL || length == 0 || out_sig == NULL)
    {
        return FALSE;
    }

    sig_decode_ctx_t ctx = { .data = signature, .length = length, .offset = 0 };

    // Allocate output structure
    decoded_method_sig_t * sig = (decoded_method_sig_t *)calloc(1, sizeof(decoded_method_sig_t));
    if (sig == NULL)
    {
        return FALSE;
    }
    sig->sentinel_position = (uint32_t)-1;  // No sentinel by default

    // Read calling convention byte
    uint8_t call_conv;
    if (!ctx_read_byte(&ctx, &call_conv))
    {
        free(sig);
        return FALSE;
    }

    sig->calling_convention = call_conv;
    sig->has_this = (call_conv & CALLING_CONV_HASTHIS) != 0;
    sig->explicit_this = (call_conv & CALLING_CONV_EXPLICITTHIS) != 0;
    sig->is_generic = (call_conv & CALLING_CONV_GENERIC) != 0;
    sig->is_vararg = (call_conv & 0x0F) == CALLING_CONV_VARARG;

    // Check for field signature (should use signature_decode_field instead)
    if ((call_conv & 0x0F) == CALLING_CONV_FIELD)
    {
        printf("Error: Field signature passed to method decoder\n");
        free(sig);
        return FALSE;
    }

    // If generic, read generic parameter count
    if (sig->is_generic)
    {
        if (!ctx_read_compressed_uint(&ctx, &sig->generic_param_count))
        {
            free(sig);
            return FALSE;
        }
    }

    // Read parameter count
    if (!ctx_read_compressed_uint(&ctx, &sig->param_count))
    {
        free(sig);
        return FALSE;
    }

    // Decode return type
    if (!signature_decode_type(&ctx, &sig->return_type))
    {
        free(sig);
        return FALSE;
    }

    // Allocate parameter types array
    if (sig->param_count > 0)
    {
        sig->param_types = (decoded_type_t **)calloc(sig->param_count, sizeof(decoded_type_t *));
        if (sig->param_types == NULL)
        {
            signature_free_type(sig->return_type);
            free(sig);
            return FALSE;
        }

        // Decode each parameter type
        for (uint32_t i = 0; i < sig->param_count; i++)
        {
            // Check for SENTINEL (vararg boundary)
            if (ctx.offset < ctx.length && ctx.data[ctx.offset] == ELEMENT_TYPE_SENTINEL)
            {
                sig->sentinel_position = i;
                ctx.offset++;  // Skip sentinel
            }

            if (!signature_decode_type(&ctx, &sig->param_types[i]))
            {
                // Cleanup on error
                signature_free_method_sig(sig);
                return FALSE;
            }
        }
    }

    *out_sig = sig;
    return TRUE;
}

//------------------------------------------------------------------------------
// Public: Decode a field signature
//------------------------------------------------------------------------------
bool_t signature_decode_field(const uint8_t * signature, uint32_t length,
                              decoded_field_sig_t ** out_sig)
{
    if (signature == NULL || length == 0 || out_sig == NULL)
    {
        return FALSE;
    }

    sig_decode_ctx_t ctx = { .data = signature, .length = length, .offset = 0 };

    // Read calling convention byte
    uint8_t call_conv;
    if (!ctx_read_byte(&ctx, &call_conv))
    {
        return FALSE;
    }

    // Verify this is a field signature
    if (call_conv != CALLING_CONV_FIELD)
    {
        printf("Error: Not a field signature (calling convention 0x%02X, expected 0x06)\n", call_conv);
        return FALSE;
    }

    // Allocate output structure
    decoded_field_sig_t * sig = (decoded_field_sig_t *)calloc(1, sizeof(decoded_field_sig_t));
    if (sig == NULL)
    {
        return FALSE;
    }

    // Decode the field type
    if (!signature_decode_type(&ctx, &sig->field_type))
    {
        free(sig);
        return FALSE;
    }

    *out_sig = sig;
    return TRUE;
}

//------------------------------------------------------------------------------
// Cleanup: Free a decoded type (recursive)
//------------------------------------------------------------------------------
void signature_free_type(decoded_type_t * type)
{
    if (type == NULL)
    {
        return;
    }

    // Free nested type
    if (type->nested_type != NULL)
    {
        signature_free_type(type->nested_type);
    }

    // Free generic base type
    if (type->generic_base_type != NULL)
    {
        signature_free_type(type->generic_base_type);
    }

    // Free generic arguments
    if (type->generic_args != NULL)
    {
        for (uint32_t i = 0; i < type->generic_arg_count; i++)
        {
            signature_free_type(type->generic_args[i]);
        }
        free(type->generic_args);
    }

    // Free array shape info
    if (type->array_sizes != NULL)
    {
        free(type->array_sizes);
    }
    if (type->array_lower_bounds != NULL)
    {
        free(type->array_lower_bounds);
    }

    // Free function pointer signature
    if (type->fn_ptr_sig != NULL)
    {
        signature_free_method_sig(type->fn_ptr_sig);
    }

    // Free modified type
    if (type->modified_type != NULL)
    {
        signature_free_type(type->modified_type);
    }

    free(type);
}

//------------------------------------------------------------------------------
// Cleanup: Free a decoded method signature
//------------------------------------------------------------------------------
void signature_free_method_sig(decoded_method_sig_t * sig)
{
    if (sig == NULL)
    {
        return;
    }

    if (sig->return_type != NULL)
    {
        signature_free_type(sig->return_type);
    }

    if (sig->param_types != NULL)
    {
        for (uint32_t i = 0; i < sig->param_count; i++)
        {
            signature_free_type(sig->param_types[i]);
        }
        free(sig->param_types);
    }

    free(sig);
}

//------------------------------------------------------------------------------
// Cleanup: Free a decoded field signature
//------------------------------------------------------------------------------
void signature_free_field_sig(decoded_field_sig_t * sig)
{
    if (sig == NULL)
    {
        return;
    }

    if (sig->field_type != NULL)
    {
        signature_free_type(sig->field_type);
    }

    free(sig);
}

//------------------------------------------------------------------------------
// Utility: Get element type name for debugging
//------------------------------------------------------------------------------
const char * signature_element_type_name(uint8_t element_type)
{
    switch (element_type)
    {
        case ELEMENT_TYPE_END:        return "END";
        case ELEMENT_TYPE_VOID:       return "void";
        case ELEMENT_TYPE_BOOLEAN:    return "bool";
        case ELEMENT_TYPE_CHAR:       return "char";
        case ELEMENT_TYPE_I1:         return "sbyte";
        case ELEMENT_TYPE_U1:         return "byte";
        case ELEMENT_TYPE_I2:         return "short";
        case ELEMENT_TYPE_U2:         return "ushort";
        case ELEMENT_TYPE_I4:         return "int";
        case ELEMENT_TYPE_U4:         return "uint";
        case ELEMENT_TYPE_I8:         return "long";
        case ELEMENT_TYPE_U8:         return "ulong";
        case ELEMENT_TYPE_R4:         return "float";
        case ELEMENT_TYPE_R8:         return "double";
        case ELEMENT_TYPE_STRING:     return "string";
        case ELEMENT_TYPE_PTR:        return "PTR";
        case ELEMENT_TYPE_BYREF:      return "BYREF";
        case ELEMENT_TYPE_VALUETYPE:  return "VALUETYPE";
        case ELEMENT_TYPE_CLASS:      return "CLASS";
        case ELEMENT_TYPE_VAR:        return "VAR";
        case ELEMENT_TYPE_ARRAY:      return "ARRAY";
        case ELEMENT_TYPE_GENERICINST:return "GENERICINST";
        case ELEMENT_TYPE_TYPEDBYREF: return "TypedReference";
        case ELEMENT_TYPE_I:          return "IntPtr";
        case ELEMENT_TYPE_U:          return "UIntPtr";
        case ELEMENT_TYPE_FNPTR:      return "FNPTR";
        case ELEMENT_TYPE_OBJECT:     return "object";
        case ELEMENT_TYPE_SZARRAY:    return "SZARRAY";
        case ELEMENT_TYPE_MVAR:       return "MVAR";
        case ELEMENT_TYPE_CMOD_REQD:  return "CMOD_REQD";
        case ELEMENT_TYPE_CMOD_OPT:   return "CMOD_OPT";
        case ELEMENT_TYPE_SENTINEL:   return "SENTINEL";
        case ELEMENT_TYPE_PINNED:     return "PINNED";
        default:                      return "UNKNOWN";
    }
}
```

---

## Usage Example

```c
#include "signature_decoder.h"

void decode_and_print_method_signature(uint8_t * signature, uint32_t length)
{
    decoded_method_sig_t * sig = NULL;

    if (!signature_decode_method(signature, length, &sig))
    {
        printf("Failed to decode method signature\n");
        return;
    }

    printf("Method signature:\n");
    printf("  Calling convention: 0x%02X\n", sig->calling_convention);
    printf("  Has this: %s\n", sig->has_this ? "yes" : "no");
    printf("  Is generic: %s\n", sig->is_generic ? "yes" : "no");
    if (sig->is_generic)
    {
        printf("  Generic param count: %u\n", sig->generic_param_count);
    }
    printf("  Param count: %u\n", sig->param_count);
    printf("  Return type: %s\n", signature_element_type_name(sig->return_type->element_type));

    for (uint32_t i = 0; i < sig->param_count; i++)
    {
        printf("  Param[%u]: %s\n", i, signature_element_type_name(sig->param_types[i]->element_type));
    }

    signature_free_method_sig(sig);
}
```

---

## TypeDefOrRef Coded Index

When decoding `CLASS`, `VALUETYPE`, or `GENERICINST` types, the TypeDefOrRef coded index encodes:

| Bits | Meaning |
|------|---------|
| `value & 0x03` | Table selector: 0=TypeDef, 1=TypeRef, 2=TypeSpec |
| `value >> 2` | Row index (1-based) |

Example: If coded index is `0x49` (binary `01001001`):
- Table = `0x49 & 0x03` = `1` → TypeRef
- Row = `0x49 >> 2` = `18` → Row 18 in TypeRef table

---

## Common Signature Examples

### Static method: `static int Add(int a, int b)`
```
00          ; DEFAULT calling convention
02          ; ParamCount = 2
08          ; RetType = I4 (int)
08          ; Param[0] = I4 (int)
08          ; Param[1] = I4 (int)
```

### Instance method: `string ToString()`
```
20          ; HASTHIS calling convention
00          ; ParamCount = 0
0E          ; RetType = STRING
```

### Generic method: `T GetValue<T>()`
```
30          ; HASTHIS | GENERIC
01          ; GenParamCount = 1
00          ; ParamCount = 0
13 00       ; RetType = VAR 0 (T)
```

### Field: `int[] numbers`
```
06          ; FIELD calling convention
1D          ; SZARRAY
08          ; element type = I4 (int)
```

### Class type: `List<string>`
```
15          ; GENERICINST
12          ; CLASS
xx xx       ; TypeRef for List`1 (coded index)
01          ; GenArgCount = 1
0E          ; GenArg[0] = STRING
```
