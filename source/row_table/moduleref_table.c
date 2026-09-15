#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_moduleref(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    moduleref_table_decoded_t* out;

    out = malloc(sizeof(moduleref_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for moduleref table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;

    // Read Name (StringIndex)
    uint32_t NameIndex;
    if (string_index_size == 4)
    {
        NameIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        NameIndex = *(uint16_t*)(row_data + offset);
    }
    offset += string_index_size;

    out->Name = ecma_335_get_string(table_context, NameIndex);
    metadata_table_add_row(table_info, out);

    return offset;
}
