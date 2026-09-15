#ifndef __ECMA_335_PARSER_H__
#define __ECMA_335_PARSER_H__

#include "types.h"
#include "pe_parser.h"

typedef void * ecma_data_t;

ecma_data_t * ecma_335_parser(pe_data_t * pe_data, uint32_t md_offset, uint32_t md_size, uint32_t entry_point_token);
bool_t ecma_335_parse_tables(ecma_data_t * ecma_data);

const char* ecma_335_get_string(table_context_t * ctx, uint32_t string_index);
uint8_t* ecma_335_get_blob(table_context_t * ctx, uint32_t blob_index, uint32_t* out_length);
const uint16_t* ecma_335_get_user_string(table_context_t * ctx, uint32_t index, uint32_t * out_char_count);

#endif // __ECMA_335_PARSER_H__
