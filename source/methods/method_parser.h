#ifndef __METHOD_PARSER_H__
#define __METHOD_PARSER_H__

#include "methods_type.h"
#include "metadata_table.h"
#include "parser_table_types.h"
#include "pe_parser.h"
#include "element_types.h"
#include "signature_parser.h"


// Parse method body info (IL code, etc.)
void method_parser_get_info(table_context_t * table_context, methoddef_table_decoded_t * methoddef_data, method_info_t * method_info);

// Decode a method signature blob
bool_t method_parser_decode_signature(const uint8_t * signature, uint32_t signature_len,
                                      table_context_t * table_context, table_info_t * table_info,
                                      method_info_t * method_info);

// Decode local variables from local_var_sig_tok (StandAloneSig token)
bool_t method_parser_decode_locals(table_context_t * table_context, table_info_t * table_info,
                                   method_info_t * method_info);

// Decode a single type from signature (recursive)
bool_t method_parser_decode_type(sig_decode_ctx_t * ctx, decoded_type_t ** out_type);

// Cleanup functions
void method_parser_free_type(decoded_type_t * type);
void method_parser_free_method_sig(decoded_method_sig_t * sig);
void method_parser_free_locals(decoded_locals_t * locals);

// Utility: get element type name for debugging
const char * method_parser_element_type_name(uint8_t element_type);

#endif // __METHOD_PARSER_H__
