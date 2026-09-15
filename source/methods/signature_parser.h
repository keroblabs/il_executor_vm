#ifndef __SIGNATURE_PARSER_H__
#define __SIGNATURE_PARSER_H__

#include "methods_type.h"
#include "metadata_table.h"
#include "parser_table_types.h"
#include "element_types.h"

//------------------------------------------------------------------------------
// Context helpers
//------------------------------------------------------------------------------

// Read a single byte from decode context
bool_t sig_ctx_read_byte(sig_decode_ctx_t * ctx, uint8_t * out);

// Read a compressed unsigned integer from decode context
bool_t sig_ctx_read_compressed_uint(sig_decode_ctx_t * ctx, uint32_t * out);

//------------------------------------------------------------------------------
// Type decoding
//------------------------------------------------------------------------------

// Decode a single type from signature context
bool_t signature_decode_type(sig_decode_ctx_t * ctx, decoded_type_t ** out_type);

// Decode TypeDefOrRef coded index into a type
bool_t signature_decode_type_def_or_ref(sig_decode_ctx_t * ctx, decoded_type_t * type);

//------------------------------------------------------------------------------
// Signature decoding
//------------------------------------------------------------------------------

// Decode a method signature blob
bool_t signature_decode_method(const uint8_t * signature, uint32_t signature_len,
                               decoded_method_sig_t ** out_sig);

// Decode a field signature blob
// FieldSig ::= FIELD CustomMod* Type
bool_t signature_decode_field(const uint8_t * signature, uint32_t signature_len,
                              decoded_type_t ** out_type);

// Decode a LOCAL_SIG blob (local variables)
bool_t signature_decode_locals(const uint8_t * signature, uint32_t signature_len,
                               decoded_locals_t * out_locals);

// Decode a MemberRef signature blob (can be either method or field)
bool_t signature_decode_member_ref(const uint8_t * signature, uint32_t signature_len,
                                   decoded_member_ref_sig_t ** out_sig);

//------------------------------------------------------------------------------
// Cleanup functions
//------------------------------------------------------------------------------

// Free a decoded type (recursive, handles linked list via p_next)
void signature_free_type(decoded_type_t * type);

// Free a decoded method signature
void signature_free_method_sig(decoded_method_sig_t * sig);

// Free decoded locals
void signature_free_locals(decoded_locals_t * locals);

// Free a decoded MemberRef signature
void signature_free_member_ref_sig(decoded_member_ref_sig_t * sig);

//------------------------------------------------------------------------------
// Utility
//------------------------------------------------------------------------------

// Get element type name for debugging
const char * signature_element_type_name(uint8_t element_type);

// Allocate a zeroed decoded_type_t
decoded_type_t * signature_alloc_type(void);

#endif // __SIGNATURE_PARSER_H__
