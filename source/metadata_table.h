#ifndef __METADATA_TABLE_H__
#define __METADATA_TABLE_H__

#include "ecma_335_types.h"
#include "ecma_335_parser.h"
#include "metadata_table_types.h"

void metadata_table_add_row(table_info_t * table_info, void * row_data);
bool_t metadata_table_decode(table_context_t * table_context);
bool_t metadata_parse_tables(ecma_file_t * ecma_file);

table_info_t * metadata_table_get_by_id(uint32_t table_id, table_context_t * table_context);
void * metadata_table_get_row_data_by_index(uint32_t table_id, table_context_t * table_context, uint32_t row_index);

uint32_t metadata_decompress_integer(const uint8_t * buffer, uint32_t * value_out);
void metadata_parse_signature_for_methods(ecma_file_t * ecma_file);

#endif // __METADATA_TABLE_H__
