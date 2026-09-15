#ifndef ___METHODS_TYPE_H__
#define ___METHODS_TYPE_H__

#include "types.h"
#include "element_types.h"
#include "metadata_table_types.h"

// Forward declarations
typedef struct decoded_type_t_ decoded_type_t;
typedef struct decoded_method_sig_t_ decoded_method_sig_t;

/**
 * Represents a decoded type from a signature.
 * Types can be recursive (e.g., SZARRAY of CLASS).
 */
struct decoded_type_t_
{
    struct decoded_type_t_ * p_next;

    uint8_t element_type;           // ELEMENT_TYPE_* value

    // TypeDefOrRef coded index - dual purpose based on element_type:
    //   - For CLASS, VALUETYPE: the referenced type (class, struct, enum)
    //   - For CMOD_REQD, CMOD_OPT: the modifier type (e.g., IsVolatile, IsConst)
    uint32_t type_ref_coded_index;
    uint8_t type_ref_table;         // 0=TypeDef, 1=TypeRef, 2=TypeSpec
    uint32_t type_ref_row;          // Row index (1-based)

    // For VAR, MVAR: generic parameter index
    uint32_t generic_param_index;

    // For SZARRAY, PTR, BYREF, PINNED: nested type
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

    // For CMOD_REQD, CMOD_OPT: the underlying type being modified
    // (the modifier itself is in type_ref_* fields above)
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
    decoded_type_t * param_types;  // Array of parameter types

    // For vararg: sentinel position (-1 if none)
    uint32_t sentinel_position;
};

/**
 * Represents decoded local variable types from a LOCAL_SIG.
 */
typedef struct decoded_locals_t_
{
    uint32_t local_count;           // Number of local variables
    decoded_type_t * local_types;   // Linked list of local variable types
} decoded_locals_t;

/**
 * Represents a decoded MemberRef signature.
 * MemberRef can reference either a method or a field.
 */
typedef struct decoded_member_ref_sig_t_
{
    bool_t is_field;                // TRUE if this is a field signature, FALSE if method
    union
    {
        decoded_method_sig_t * method_sig;  // Method signature (when is_field == FALSE)
        decoded_type_t * field_type;        // Field type (when is_field == TRUE)
    };
} decoded_member_ref_sig_t;

/**
 * Decode context - tracks position in signature blob.
 */
typedef struct sig_decode_ctx_t_
{
    const uint8_t * data;           // Signature blob data
    uint32_t length;                // Total length
    uint32_t offset;                // Current position
} sig_decode_ctx_t;

typedef struct param_info_t_
{
    struct param_info_t_ * p_next;

    const char* name;
    uint16_t flags;           // ParamAttributes flags
    uint16_t sequence;        // Sequence number
    uint8_t* signature;       // Parameter type signature blob

} param_info_t;

typedef struct field_info_t_
{
    const char* name;
    uint16_t flags;           // FieldAttributes flags
    uint8_t* signature;       // Field type signature blob
    uint32_t signalture_length; // Length of the field signature blob

    decoded_type_t * decoded_type; // Decoded field type from signature blob

    // Pointer to the TypeDef row info for the parent type of this field, used for resolving type references in
    // signatures and for linking Field to its parent TypeDef after we decode the TypeDef and Field tables and
    // determine the field ranges for each type based on the FieldList indices in the TypeDef entries. The field
    // ranges for each type are determined based on the FieldList index and FieldList count in each TypeDef entry,
    // which specify the range of Field entries in the Field table that belong to each TypeDef entry. By linking
    // the Field entries to their parent TypeDef entry, we can easily access the parent type information when
    // decoding field signatures and resolving type references in field signatures.
    row_info_t * typedef_row_info;

} field_info_t;

// Exception-handling clause flags (ECMA-335 II.25.4.6, COR_ILEXCEPTION_CLAUSE_*)
enum
{
    EH_CLAUSE_KIND_CATCH   = 0x0000,  // Typed catch clause (ClassTokenOrFilter is a TypeDef/TypeRef/TypeSpec token)
    EH_CLAUSE_KIND_FILTER  = 0x0001,  // Exception filter clause (ClassTokenOrFilter is an IL offset)
    EH_CLAUSE_KIND_FINALLY = 0x0002,  // Finally clause (runs on both normal and exceptional exit)
    EH_CLAUSE_KIND_FAULT   = 0x0004,  // Fault clause (runs only on exceptional exit)
};

// Single exception-handling clause decoded from a fat method body's EH section.
// Small (12-byte) and fat (24-byte) on-disk formats are both normalized into this shape.
typedef struct eh_clause_t_
{
    uint32_t flags;             // EH_CLAUSE_KIND_*
    uint32_t try_offset;        // Byte offset into IL where the protected (try) region starts
    uint32_t try_length;        // Byte length of the try region
    uint32_t handler_offset;    // Byte offset into IL where the handler starts
    uint32_t handler_length;    // Byte length of the handler
    union
    {
        uint32_t class_token;   // For CATCH: metadata token of the exception type
        uint32_t filter_offset; // For FILTER: byte offset into IL where filter code starts
    };
} eh_clause_t;

typedef struct method_info_t_
{
    uint16_t flags;             // Method body flags (bits 0-1: format, bit 3: more sects, bit 4: init locals)
    uint16_t max_stack;         // Maximum stack depth (8 for tiny format)
    uint32_t code_size;         // Size of IL code in bytes
    uint32_t local_var_sig_tok; // StandAloneSig token for locals (0 if none / tiny format)
    uint8_t* il_code;           // Pointer to IL bytecode (includes exception handlers for fat format)

    decoded_method_sig_t * decoded_signature; // Decoded method signature from the method signature blob

    uint32_t local_count;           // Number of local variables
    decoded_type_t * local_types;   // Linked list of local variable types

    // Exception-handling clauses parsed from the method body sections following the IL code
    // when the fat header has CorILMethod_MoreSects set (flags & 0x08). NULL/0 otherwise.
    uint32_t eh_clause_count;
    eh_clause_t * eh_clauses;

    // Pointer to the TypeDef row info for the parent type of this method, used for resolving type references in
    // signatures and for linking MethodDef to its parent TypeDef after we decode the TypeDef and MethodDef tables
    // and determine the method ranges for each type based on the MethodList indices in the TypeDef entries.
    row_info_t * typedef_row_info;

} method_info_t;

typedef struct type_info_t_
{
    struct type_info_t_ * p_next;

    const char* type_name;
    const char* type_namespace;
    uint32_t type_flags;

    uint32_t field_count;
    field_info_t* fields;

    uint32_t method_count;
    method_info_t* methods;

} type_info_t;


#endif // ___METHODS_TYPE_H__
