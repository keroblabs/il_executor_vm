#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_file(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    file_table_decoded_t* out;

    out = malloc(sizeof(file_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for file table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;
    uint32_t blob_index_size   = (table_context->tables_stream_header->HeapSizes & 0x04) ? 4 : 2;

    // Read Flags (always 4 bytes)
    out->Flags = *(uint32_t*)(row_data + offset);
    offset += 4;

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

    // Read HashValue (BlobIndex)
    if (blob_index_size == 4)
    {
        out->HashValueIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->HashValueIndex = *(uint16_t*)(row_data + offset);
    }
    offset += blob_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
