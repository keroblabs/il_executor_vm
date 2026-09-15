#ifndef __PE_PARSER_H__
#define __PE_PARSER_H__

#include "types.h"

typedef void * pe_data_t;

pe_data_t * pe_parser(FILE * fp, uint32_t *md_offset, uint32_t *md_size, uint32_t * entry_point_token);
uint32_t pe_rva_to_file_offset(uint32_t rva, pe_data_t * pe_data);
const uint8_t * pe_rva_to_pointer(pe_data_t * pe_data, uint32_t rva);
FILE * pe_get_file_handle(pe_data_t * pe_data);

#endif // __PE_PARSER_H__
