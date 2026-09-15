#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_methodsemantics(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    methodsemantics_table_decoded_t* out;

    out = malloc(sizeof(methodsemantics_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for methodsemantics table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;

    // Calculate MethodDef table index size
    uint32_t methoddef_index_size = (table_context->row_count_table[METADATA_TABLE_MethodDef] < (1 << 16)) ? 2 : 4;

    // Calculate HasSemantics coded index size (1-bit tag: Event, Property)
    uint32_t has_semantics_max = table_context->row_count_table[METADATA_TABLE_Event];
    if (table_context->row_count_table[METADATA_TABLE_Property] > has_semantics_max)
        has_semantics_max = table_context->row_count_table[METADATA_TABLE_Property];
    uint32_t has_semantics_index_size = (has_semantics_max < (1 << 15)) ? 2 : 4;

    // Read Semantics (always 2 bytes)
    out->Semantics = *(uint16_t*)(row_data + offset);
    offset += 2;

    // Read Method (index into MethodDef table)
    if (methoddef_index_size == 4)
    {
        out->MethodIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->MethodIndex = *(uint16_t*)(row_data + offset);
    }
    offset += methoddef_index_size;

    // Read Association (HasSemantics coded index)
    if (has_semantics_index_size == 4)
    {
        out->AssociationIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->AssociationIndex = *(uint16_t*)(row_data + offset);
    }
    offset += has_semantics_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
