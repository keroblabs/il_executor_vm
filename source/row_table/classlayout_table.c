#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_classlayout(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    classlayout_table_decoded_t* out;

    out = malloc(sizeof(classlayout_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for classlayout table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;

    // Calculate TypeDef table index size
    uint32_t typedef_index_size = (table_context->row_count_table[METADATA_TABLE_TypeDef] < (1 << 16)) ? 2 : 4;

    // Read PackingSize (always 2 bytes)
    out->PackingSize = *(uint16_t*)(row_data + offset);
    offset += 2;

    // Read ClassSize (always 4 bytes)
    out->ClassSize = *(uint32_t*)(row_data + offset);
    offset += 4;

    // Read Parent (index into TypeDef table)
    if (typedef_index_size == 4)
    {
        out->ParentIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->ParentIndex = *(uint16_t*)(row_data + offset);
    }
    offset += typedef_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
