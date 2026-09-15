#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_genericparam(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    genericparam_table_decoded_t* out;

    out = malloc(sizeof(genericparam_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for genericparam table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;

    // Calculate TypeOrMethodDef coded index size (1-bit tag: TypeDef, MethodDef)
    uint32_t type_or_method_def_max = table_context->row_count_table[METADATA_TABLE_TypeDef];
    if (table_context->row_count_table[METADATA_TABLE_MethodDef] > type_or_method_def_max)
        type_or_method_def_max = table_context->row_count_table[METADATA_TABLE_MethodDef];
    uint32_t type_or_method_def_index_size = (type_or_method_def_max < (1 << 15)) ? 2 : 4;

    // Read Number (always 2 bytes)
    out->Number = *(uint16_t*)(row_data + offset);
    offset += 2;

    // Read Flags (always 2 bytes)
    out->Flags = *(uint16_t*)(row_data + offset);
    offset += 2;

    // Read Owner (TypeOrMethodDef coded index)
    if (type_or_method_def_index_size == 4)
    {
        out->OwnerIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->OwnerIndex = *(uint16_t*)(row_data + offset);
    }
    offset += type_or_method_def_index_size;

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

    metadata_table_add_row(table_info, out);

    return offset;
}
