#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_constant(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    constant_table_decoded_t* out;

    out = malloc(sizeof(constant_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for constant table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t blob_index_size = (table_context->tables_stream_header->HeapSizes & 0x04) ? 4 : 2;

    // Calculate HasConstant coded index size (2-bit tag: Field, Param, Property)
    uint32_t has_constant_max = table_context->row_count_table[METADATA_TABLE_Field];
    if (table_context->row_count_table[METADATA_TABLE_Param] > has_constant_max)
        has_constant_max = table_context->row_count_table[METADATA_TABLE_Param];
    if (table_context->row_count_table[METADATA_TABLE_Property] > has_constant_max)
        has_constant_max = table_context->row_count_table[METADATA_TABLE_Property];
    uint32_t has_constant_index_size = (has_constant_max < (1 << 14)) ? 2 : 4;

    // Read Type (1 byte element type)
    out->Type = *(uint8_t*)(row_data + offset);
    offset += 1;

    // Read PaddingZero (1 byte padding, always 0)
    out->PaddingZero = *(uint8_t*)(row_data + offset);
    offset += 1;

    // Read Parent (HasConstant coded index)
    if (has_constant_index_size == 4)
    {
        out->ParentIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->ParentIndex = *(uint16_t*)(row_data + offset);
    }
    offset += has_constant_index_size;

    // Read Value (BlobIndex)
    if (blob_index_size == 4)
    {
        out->ValueIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->ValueIndex = *(uint16_t*)(row_data + offset);
    }
    offset += blob_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
