#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_typeref(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    typeref_table_decoded_t* out;

    out = malloc(sizeof(typeref_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for typeref table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;

    // Calculate ResolutionScope coded index size (2-bit tag: Module, ModuleRef, AssemblyRef, TypeRef)
    uint32_t resolution_scope_max = table_context->row_count_table[METADATA_TABLE_Module];
    if (table_context->row_count_table[METADATA_TABLE_ModuleRef] > resolution_scope_max)
        resolution_scope_max = table_context->row_count_table[METADATA_TABLE_ModuleRef];
    if (table_context->row_count_table[METADATA_TABLE_AssemblyRef] > resolution_scope_max)
        resolution_scope_max = table_context->row_count_table[METADATA_TABLE_AssemblyRef];
    if (table_context->row_count_table[METADATA_TABLE_TypeRef] > resolution_scope_max)
        resolution_scope_max = table_context->row_count_table[METADATA_TABLE_TypeRef];
    uint32_t resolution_scope_index_size = (resolution_scope_max < (1 << 14)) ? 2 : 4;

    // Read ResolutionScope (coded index)
    if (resolution_scope_index_size == 4)
    {
        out->ResolutionScopeIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->ResolutionScopeIndex = *(uint16_t*)(row_data + offset);
    }
    offset += resolution_scope_index_size;

    // Read TypeName (StringIndex)
    uint32_t TypeNameIndex;
    if (string_index_size == 4)
    {
        TypeNameIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        TypeNameIndex = *(uint16_t*)(row_data + offset);
    }
    offset += string_index_size;

    // Read TypeNamespace (StringIndex)
    uint32_t TypeNamespaceIndex;
    if (string_index_size == 4)
    {
        TypeNamespaceIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        TypeNamespaceIndex = *(uint16_t*)(row_data + offset);
    }
    offset += string_index_size;

    out->TypeName = ecma_335_get_string(table_context, TypeNameIndex);
    out->TypeNamespace = ecma_335_get_string(table_context, TypeNamespaceIndex);

    metadata_table_add_row(table_info, out);

    return offset;
}
