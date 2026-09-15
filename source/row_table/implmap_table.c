#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_implmap(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    implmap_table_decoded_t* out;

    out = malloc(sizeof(implmap_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for implmap table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;

    // Calculate MemberForwarded coded index size (1-bit tag: Field=0, MethodDef=1)
    // In practice only MethodDef is used, but the spec allows both.
    uint32_t member_forwarded_max = table_context->row_count_table[METADATA_TABLE_Field];
    if (table_context->row_count_table[METADATA_TABLE_MethodDef] > member_forwarded_max)
        member_forwarded_max = table_context->row_count_table[METADATA_TABLE_MethodDef];
    uint32_t member_forwarded_index_size = (member_forwarded_max < (1 << 15)) ? 2 : 4;

    // Calculate ModuleRef table index size
    uint32_t moduleref_index_size = (table_context->row_count_table[METADATA_TABLE_ModuleRef] < (1 << 16)) ? 2 : 4;

    // Read MappingFlags (always 2 bytes - PInvokeAttributes)
    out->MappingFlags = *(uint16_t*)(row_data + offset);
    offset += 2;

    // Read MemberForwarded (MemberForwarded coded index - the managed method being forwarded)
    if (member_forwarded_index_size == 4)
    {
        out->MemberForwardedIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->MemberForwardedIndex = *(uint16_t*)(row_data + offset);
    }
    offset += member_forwarded_index_size;

    // Read ImportName (StringIndex - the native function name)
    uint32_t ImportNameIndex;
    if (string_index_size == 4)
    {
        ImportNameIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        ImportNameIndex = *(uint16_t*)(row_data + offset);
    }
    out->ImportName = ecma_335_get_string(table_context, ImportNameIndex);
    offset += string_index_size;

    // Read ImportScope (index into ModuleRef table - the native library)
    if (moduleref_index_size == 4)
    {
        out->ImportScopeIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->ImportScopeIndex = *(uint16_t*)(row_data + offset);
    }
    offset += moduleref_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
