#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_standalonesig(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    standalonesig_table_decoded_t* out;

    out = malloc(sizeof(standalonesig_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for standalonesig table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t blob_index_size = (table_context->tables_stream_header->HeapSizes & 0x04) ? 4 : 2;

    // Read Signature (BlobIndex)
    uint32_t SignatureIndex;
    if (blob_index_size == 4)
    {
        SignatureIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        SignatureIndex = *(uint16_t*)(row_data + offset);
    }
    offset += blob_index_size;

    out->Signature = ecma_335_get_blob(table_context, SignatureIndex, &out->SignatureLength);

    metadata_table_add_row(table_info, out);

    return offset;
}
