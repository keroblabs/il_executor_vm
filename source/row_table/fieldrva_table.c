#include "metadata_table.h"
#include "parser_table_types.h"
#include "pe_parser.h"

uint32_t metadata_table_decode_fieldrva(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    fieldrva_table_decoded_t* out;

    out = malloc(sizeof(fieldrva_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for fieldrva table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;

    // Calculate Field table index size
    uint32_t field_index_size = (table_context->row_count_table[METADATA_TABLE_Field] < (1 << 16)) ? 2 : 4;

    // Read RVA (always 4 bytes)
    out->RVA = *(uint32_t*)(row_data + offset);
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

    out->Data = NULL; // Resolved in metadata_table_finish_fieldrva once all rows are decoded.

    metadata_table_add_row(table_info, out);

    return offset;
}

void metadata_table_finish_fieldrva(table_info_t * table_info, table_context_t * table_context)
{
    row_info_t * row = table_info->rows;

    while (row != NULL)
    {
        fieldrva_table_decoded_t * decoded = (fieldrva_table_decoded_t *)row->p_row;

        decoded->Data = pe_rva_to_pointer(table_context->pe_data, decoded->RVA);
        if (decoded->Data == NULL)
        {
            printf("FieldRVA row (Field=%u): failed to resolve RVA 0x%08x to a pointer inside the PE image\n",
                   decoded->FieldIndex, decoded->RVA);
        }

        row = row->p_next;
    }
}
