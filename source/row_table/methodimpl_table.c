#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_methodimpl(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    methodimpl_table_decoded_t* out;

    out = malloc(sizeof(methodimpl_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for methodimpl table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;

    // Calculate TypeDef table index size
    uint32_t typedef_index_size = (table_context->row_count_table[METADATA_TABLE_TypeDef] < (1 << 16)) ? 2 : 4;

    // Calculate MethodDefOrRef coded index size (1-bit tag: MethodDef, MemberRef)
    uint32_t method_def_or_ref_max = table_context->row_count_table[METADATA_TABLE_MethodDef];
    if (table_context->row_count_table[METADATA_TABLE_MemberRef] > method_def_or_ref_max)
        method_def_or_ref_max = table_context->row_count_table[METADATA_TABLE_MemberRef];
    uint32_t method_def_or_ref_index_size = (method_def_or_ref_max < (1 << 15)) ? 2 : 4;

    // Read Class (index into TypeDef table)
    if (typedef_index_size == 4)
    {
        out->ClassIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->ClassIndex = *(uint16_t*)(row_data + offset);
    }
    offset += typedef_index_size;

    // Read MethodBody (MethodDefOrRef coded index)
    if (method_def_or_ref_index_size == 4)
    {
        out->MethodBodyIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->MethodBodyIndex = *(uint16_t*)(row_data + offset);
    }
    offset += method_def_or_ref_index_size;

    // Read MethodDeclaration (MethodDefOrRef coded index)
    if (method_def_or_ref_index_size == 4)
    {
        out->MethodDeclarationIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->MethodDeclarationIndex = *(uint16_t*)(row_data + offset);
    }
    offset += method_def_or_ref_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
