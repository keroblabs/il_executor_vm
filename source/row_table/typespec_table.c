#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_typespec(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    typespec_table_decoded_t* out;

    out = malloc(sizeof(typespec_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for typespec table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t blob_index_size = (table_context->tables_stream_header->HeapSizes & 0x04) ? 4 : 2;

    // Read Signature (BlobIndex)
    if (blob_index_size == 4)
    {
        out->SignatureIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->SignatureIndex = *(uint16_t*)(row_data + offset);
    }
    offset += blob_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
