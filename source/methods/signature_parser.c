#include "signature_parser.h"
#include <stdio.h>
#include <stdlib.h>

//------------------------------------------------------------------------------
// Helper: Read a byte from context
//------------------------------------------------------------------------------
bool_t sig_ctx_read_byte(sig_decode_ctx_t * ctx, uint8_t * out)
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
bool_t sig_ctx_read_compressed_uint(sig_decode_ctx_t * ctx, uint32_t * out)
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
decoded_type_t * signature_alloc_type(void)
{
    decoded_type_t * type = (decoded_type_t *)calloc(1, sizeof(decoded_type_t));
    return type;
}

//------------------------------------------------------------------------------
// Decode TypeDefOrRefEncoded (compressed coded index)
//------------------------------------------------------------------------------
bool_t signature_decode_type_def_or_ref(sig_decode_ctx_t * ctx, decoded_type_t * type)
{
    uint32_t coded_index;
    if (!sig_ctx_read_compressed_uint(ctx, &coded_index))
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
// Forward declaration for method signature decoding (used by FNPTR)
//------------------------------------------------------------------------------
static bool_t decode_method_sig_internal(sig_decode_ctx_t * ctx, decoded_method_sig_t ** out_sig);

//------------------------------------------------------------------------------
// Decode ARRAY type (multi-dimensional)
// Format: ELEMENT_TYPE_ARRAY Type ArrayShape
// ArrayShape: Rank NumSizes Size* NumLoBounds LoBound*
//------------------------------------------------------------------------------
static bool_t decode_array_shape(sig_decode_ctx_t * ctx, decoded_type_t * type)
{
    // Read rank
    if (!sig_ctx_read_compressed_uint(ctx, &type->array_rank))
    {
        return FALSE;
    }

    // Read number of sizes
    uint32_t num_sizes;
    if (!sig_ctx_read_compressed_uint(ctx, &num_sizes))
    {
        return FALSE;
    }

    type->array_sizes_count = num_sizes;
    if (num_sizes > 0)
    {
        type->array_sizes = (uint32_t *)calloc(num_sizes, sizeof(uint32_t));
        if (type->array_sizes == NULL)
        {
            return FALSE;
        }
        for (uint32_t i = 0; i < num_sizes; i++)
        {
            if (!sig_ctx_read_compressed_uint(ctx, &type->array_sizes[i]))
            {
                return FALSE;
            }
        }
    }

    // Read number of lower bounds
    uint32_t num_lobounds;
    if (!sig_ctx_read_compressed_uint(ctx, &num_lobounds))
    {
        return FALSE;
    }

    type->array_lower_bounds_count = num_lobounds;
    if (num_lobounds > 0)
    {
        type->array_lower_bounds = (uint32_t *)calloc(num_lobounds, sizeof(uint32_t));
        if (type->array_lower_bounds == NULL)
        {
            return FALSE;
        }
        for (uint32_t i = 0; i < num_lobounds; i++)
        {
            // Note: lower bounds are signed compressed integers
            if (!sig_ctx_read_compressed_uint(ctx, &type->array_lower_bounds[i]))
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
    type->generic_base_type = signature_alloc_type();
    if (type->generic_base_type == NULL)
    {
        return FALSE;
    }

    uint8_t base_element_type;
    if (!sig_ctx_read_byte(ctx, &base_element_type))
    {
        return FALSE;
    }

    if (base_element_type != ELEMENT_TYPE_CLASS && base_element_type != ELEMENT_TYPE_VALUETYPE)
    {
        printf("Error: GENERICINST base must be CLASS or VALUETYPE, got 0x%02X\n", base_element_type);
        return FALSE;
    }

    type->generic_base_type->element_type = base_element_type;
    if (!signature_decode_type_def_or_ref(ctx, type->generic_base_type))
    {
        return FALSE;
    }

    // Read generic argument count
    if (!sig_ctx_read_compressed_uint(ctx, &type->generic_arg_count))
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
            type->generic_args[i] = signature_alloc_type();
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
    if (!sig_ctx_read_byte(ctx, &element_type))
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
            if (!signature_decode_type_def_or_ref(ctx, type))
            {
                return FALSE;
            }
            break;

        // PTR and BYREF: followed by Type
        case ELEMENT_TYPE_PTR:
        case ELEMENT_TYPE_BYREF:
            type->nested_type = signature_alloc_type();
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
            type->nested_type = signature_alloc_type();
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
            type->nested_type = signature_alloc_type();
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
            if (!sig_ctx_read_compressed_uint(ctx, &type->generic_param_index))
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
            if (!decode_method_sig_internal(ctx, &type->fn_ptr_sig))
            {
                return FALSE;
            }
            break;

        // CMOD_REQD and CMOD_OPT: custom modifiers
        case ELEMENT_TYPE_CMOD_REQD:
        case ELEMENT_TYPE_CMOD_OPT:
            // Read modifier type reference
            if (!signature_decode_type_def_or_ref(ctx, type))
            {
                return FALSE;
            }
            // Then read the underlying type
            type->modified_type = signature_alloc_type();
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
            type->nested_type = signature_alloc_type();
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
    *out_type = signature_alloc_type();
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
// Internal: Decode a method signature from context (used by FNPTR and public API)
//------------------------------------------------------------------------------
static bool_t decode_method_sig_internal(sig_decode_ctx_t * ctx, decoded_method_sig_t ** out_sig)
{
    if (ctx == NULL || out_sig == NULL)
    {
        return FALSE;
    }

    // Allocate output structure
    decoded_method_sig_t * sig = (decoded_method_sig_t *)calloc(1, sizeof(decoded_method_sig_t));
    if (sig == NULL)
    {
        return FALSE;
    }
    sig->sentinel_position = (uint32_t)-1;  // No sentinel by default

    // Read calling convention byte
    uint8_t call_conv;
    if (!sig_ctx_read_byte(ctx, &call_conv))
    {
        free(sig);
        return FALSE;
    }

    sig->calling_convention = call_conv;
    sig->has_this = (call_conv & CALLING_CONV_HASTHIS) != 0;
    sig->explicit_this = (call_conv & CALLING_CONV_EXPLICITTHIS) != 0;
    sig->is_generic = (call_conv & CALLING_CONV_GENERIC) != 0;
    sig->is_vararg = (call_conv & 0x0F) == CALLING_CONV_VARARG;

    // Check for field signature (should not be passed to method decoder)
    if ((call_conv & 0x0F) == CALLING_CONV_FIELD)
    {
        printf("Error: Field signature passed to method decoder\n");
        free(sig);
        return FALSE;
    }

    // If generic, read generic parameter count
    if (sig->is_generic)
    {
        if (!sig_ctx_read_compressed_uint(ctx, &sig->generic_param_count))
        {
            free(sig);
            return FALSE;
        }
    }

    // Read parameter count
    if (!sig_ctx_read_compressed_uint(ctx, &sig->param_count))
    {
        free(sig);
        return FALSE;
    }

    // Decode return type
    if (!signature_decode_type(ctx, &sig->return_type))
    {
        free(sig);
        return FALSE;
    }

    // Decode parameter types as linked list
    sig->param_types = NULL;
    if (sig->param_count > 0)
    {
        decoded_type_t * param_list_tail = NULL;

        // Decode each parameter type
        for (uint32_t i = 0; i < sig->param_count; i++)
        {
            // Check for SENTINEL (vararg boundary)
            if (ctx->offset < ctx->length && ctx->data[ctx->offset] == ELEMENT_TYPE_SENTINEL)
            {
                sig->sentinel_position = i;
                ctx->offset++;  // Skip sentinel
            }

            decoded_type_t * param_type = NULL;
            if (!signature_decode_type(ctx, &param_type))
            {
                // Cleanup on error
                signature_free_method_sig(sig);
                return FALSE;
            }

            // Append to linked list
            if (sig->param_types == NULL)
            {
                sig->param_types = param_type;
            }
            else
            {
                param_list_tail->p_next = param_type;
            }
            param_list_tail = param_type;
        }
    }

    *out_sig = sig;
    return TRUE;
}

//------------------------------------------------------------------------------
// Public: Decode a method signature blob
//------------------------------------------------------------------------------
bool_t signature_decode_method(const uint8_t * signature, uint32_t signature_len,
                               decoded_method_sig_t ** out_sig)
{
    if (signature == NULL || signature_len == 0 || out_sig == NULL)
    {
        return FALSE;
    }

    sig_decode_ctx_t ctx = { .data = signature, .length = signature_len, .offset = 0 };

    return decode_method_sig_internal(&ctx, out_sig);
}

//------------------------------------------------------------------------------
// Public: Decode a field signature blob
// FieldSig ::= FIELD CustomMod* Type
//------------------------------------------------------------------------------
bool_t signature_decode_field(const uint8_t * signature, uint32_t signature_len,
                              decoded_type_t ** out_type)
{
    if (signature == NULL || signature_len == 0 || out_type == NULL)
    {
        return FALSE;
    }

    sig_decode_ctx_t ctx = { .data = signature, .length = signature_len, .offset = 0 };

    // Read calling convention byte - must be FIELD (0x06)
    uint8_t call_conv;
    if (!sig_ctx_read_byte(&ctx, &call_conv))
    {
        printf("Error: Failed to read FIELD signature calling convention\n");
        return FALSE;
    }

    if ((call_conv & 0x0F) != CALLING_CONV_FIELD)
    {
        printf("Error: Expected FIELD signature (0x06), got 0x%02X\n", call_conv);
        return FALSE;
    }

    // Decode the field type (may include CMOD_* prefixes)
    return signature_decode_type(&ctx, out_type);
}

//------------------------------------------------------------------------------
// Public: Decode a LOCAL_SIG blob (local variables)
//------------------------------------------------------------------------------
bool_t signature_decode_locals(const uint8_t * signature, uint32_t signature_len,
                               decoded_locals_t * locals)
{
    if (signature == NULL || signature_len == 0 || locals == NULL)
    {
        return FALSE;
    }

    sig_decode_ctx_t ctx = { .data = signature, .length = signature_len, .offset = 0 };

    // Read calling convention byte - must be LOCAL_SIG (0x07)
    uint8_t call_conv;
    if (!sig_ctx_read_byte(&ctx, &call_conv))
    {
        printf("Error: Failed to read LOCAL_SIG calling convention\n");
        return FALSE;
    }

    if (call_conv != CALLING_CONV_LOCAL_SIG)
    {
        printf("Error: Expected LOCAL_SIG (0x07), got 0x%02X\n", call_conv);
        return FALSE;
    }

    // Read local count
    if (!sig_ctx_read_compressed_uint(&ctx, &locals->local_count))
    {
        free(locals);
        return FALSE;
    }

    // Decode each local variable type as linked list
    locals->local_types = NULL;
    decoded_type_t * local_list_tail = NULL;

    for (uint32_t i = 0; i < locals->local_count; i++)
    {
        decoded_type_t * local_type = NULL;
        if (!signature_decode_type(&ctx, &local_type))
        {
            signature_free_locals(locals);
            return FALSE;
        }

        // Append to linked list
        if (locals->local_types == NULL)
        {
            locals->local_types = local_type;
        }
        else
        {
            local_list_tail->p_next = local_type;
        }
        local_list_tail = local_type;
    }

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

    // Free param_types linked list
    decoded_type_t * param = sig->param_types;
    while (param != NULL)
    {
        decoded_type_t * next = param->p_next;
        signature_free_type(param);
        param = next;
    }

    free(sig);
}

//------------------------------------------------------------------------------
// Cleanup: Free decoded locals
//------------------------------------------------------------------------------
void signature_free_locals(decoded_locals_t * locals)
{
    if (locals == NULL)
    {
        return;
    }

    // Free local_types linked list
    decoded_type_t * local = locals->local_types;
    while (local != NULL)
    {
        decoded_type_t * next = local->p_next;
        signature_free_type(local);
        local = next;
    }
}

//------------------------------------------------------------------------------
// Public: Decode a MemberRef signature blob
// MemberRefSig can be either a MethodRefSig or FieldSig, determined by first byte
//------------------------------------------------------------------------------
bool_t signature_decode_member_ref(const uint8_t * signature, uint32_t signature_len,
                                   decoded_member_ref_sig_t ** out_sig)
{
    if (signature == NULL || signature_len == 0 || out_sig == NULL)
    {
        return FALSE;
    }

    // Peek at the first byte to determine signature type
    uint8_t call_conv = signature[0];

    // Allocate output structure
    decoded_member_ref_sig_t * sig = (decoded_member_ref_sig_t *)calloc(1, sizeof(decoded_member_ref_sig_t));
    if (sig == NULL)
    {
        return FALSE;
    }

    // Check if this is a field signature (CALLING_CONV_FIELD = 0x06)
    if ((call_conv & 0x0F) == CALLING_CONV_FIELD)
    {
        // This is a field signature
        sig->is_field = TRUE;

        if (!signature_decode_field(signature, signature_len, &sig->field_type))
        {
            free(sig);
            return FALSE;
        }
    }
    else
    {
        // This is a method signature (MethodRefSig)
        sig->is_field = FALSE;

        if (!signature_decode_method(signature, signature_len, &sig->method_sig))
        {
            free(sig);
            return FALSE;
        }
    }

    *out_sig = sig;
    return TRUE;
}

//------------------------------------------------------------------------------
// Cleanup: Free a decoded MemberRef signature
//------------------------------------------------------------------------------
void signature_free_member_ref_sig(decoded_member_ref_sig_t * sig)
{
    if (sig == NULL)
    {
        return;
    }

    if (sig->is_field)
    {
        if (sig->field_type != NULL)
        {
            signature_free_type(sig->field_type);
        }
    }
    else
    {
        if (sig->method_sig != NULL)
        {
            signature_free_method_sig(sig->method_sig);
        }
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
