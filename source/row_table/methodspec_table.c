#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_methodspec(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    methodspec_table_decoded_t* out;

    out = malloc(sizeof(methodspec_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for methodspec table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t blob_index_size = (table_context->tables_stream_header->HeapSizes & 0x04) ? 4 : 2;

    // Calculate MethodDefOrRef coded index size (1-bit tag: MethodDef, MemberRef)
    uint32_t method_def_or_ref_max = table_context->row_count_table[METADATA_TABLE_MethodDef];
    if (table_context->row_count_table[METADATA_TABLE_MemberRef] > method_def_or_ref_max)
        method_def_or_ref_max = table_context->row_count_table[METADATA_TABLE_MemberRef];
    uint32_t method_def_or_ref_index_size = (method_def_or_ref_max < (1 << 15)) ? 2 : 4;

    // Read Method (MethodDefOrRef coded index)
    if (method_def_or_ref_index_size == 4)
    {
        out->MethodIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->MethodIndex = *(uint16_t*)(row_data + offset);
    }
    offset += method_def_or_ref_index_size;

    // Read Instantiation (BlobIndex)
    if (blob_index_size == 4)
    {
        out->InstantiationIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->InstantiationIndex = *(uint16_t*)(row_data + offset);
    }
    offset += blob_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
