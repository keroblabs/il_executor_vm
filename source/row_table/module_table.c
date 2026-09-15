#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_module(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    module_table_decoded_t* out;

    out = malloc(sizeof(module_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for module table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;
    uint32_t guid_index_size   = (table_context->tables_stream_header->HeapSizes & 0x02) ? 4 : 2;

    // Read Generation (always 2 bytes, little-endian)
    out->Generation = *(uint16_t*)(row_data + offset);
    offset += 2;

    // Read Name (StringIndex)
    if (string_index_size == 4)
    {
        out->NameIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->NameIndex = *(uint16_t*)(row_data + offset);
    }
    offset += string_index_size;

    // Read Mvid (GuidIndex)
    if (guid_index_size == 4)
    {
        out->MvidIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->MvidIndex = *(uint16_t*)(row_data + offset);
    }
    offset += guid_index_size;

    // Read EncId (GuidIndex)
    if (guid_index_size == 4)
    {
        out->EncIdIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->EncIdIndex = *(uint16_t*)(row_data + offset);
    }
    offset += guid_index_size;

    // Read EncBaseId (GuidIndex)
    if (guid_index_size == 4)
    {
        out->EncBaseIdIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->EncBaseIdIndex = *(uint16_t*)(row_data + offset);
    }

    offset += guid_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
