#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_fieldlayout(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    fieldlayout_table_decoded_t* out;

    out = malloc(sizeof(fieldlayout_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for fieldlayout table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;

    // Calculate Field table index size
    uint32_t field_index_size = (table_context->row_count_table[METADATA_TABLE_Field] < (1 << 16)) ? 2 : 4;

    // Read Offset (always 4 bytes)
    out->Offset = *(uint32_t*)(row_data + offset);
    offset += 4;

    // Read Field (index into Field table)
    if (field_index_size == 4)
    {
        out->FieldIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->FieldIndex = *(uint16_t*)(row_data + offset);
    }
    offset += field_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
