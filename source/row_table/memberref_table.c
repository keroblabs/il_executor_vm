#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_memberref(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    memberref_table_decoded_t* out;

    out = malloc(sizeof(memberref_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for memberref table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;
    uint32_t blob_index_size   = (table_context->tables_stream_header->HeapSizes & 0x04) ? 4 : 2;

    // Calculate MemberRefParent coded index size (3-bit tag: TypeDef, TypeRef, ModuleRef, MethodDef, TypeSpec)
    uint32_t memberref_parent_max = table_context->row_count_table[METADATA_TABLE_TypeDef];
    if (table_context->row_count_table[METADATA_TABLE_TypeRef] > memberref_parent_max)
        memberref_parent_max = table_context->row_count_table[METADATA_TABLE_TypeRef];
    if (table_context->row_count_table[METADATA_TABLE_ModuleRef] > memberref_parent_max)
        memberref_parent_max = table_context->row_count_table[METADATA_TABLE_ModuleRef];
    if (table_context->row_count_table[METADATA_TABLE_MethodDef] > memberref_parent_max)
        memberref_parent_max = table_context->row_count_table[METADATA_TABLE_MethodDef];
    if (table_context->row_count_table[METADATA_TABLE_TypeSpec] > memberref_parent_max)
        memberref_parent_max = table_context->row_count_table[METADATA_TABLE_TypeSpec];
    uint32_t memberref_parent_index_size = (memberref_parent_max < (1 << 13)) ? 2 : 4;

    // Read Class (MemberRefParent coded index)
    if (memberref_parent_index_size == 4)
    {
        out->ClassIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->ClassIndex = *(uint16_t*)(row_data + offset);
    }
    offset += memberref_parent_index_size;

    // Read Name (StringIndex)
    uint32_t NameIndex;
    if (string_index_size == 4)
    {
        NameIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        NameIndex = *(uint16_t*)(row_data + offset);
    }
    offset += string_index_size;

    // Read Signature (BlobIndex)
    uint32_t SignatureIndex;
    if (blob_index_size == 4)
    {
        SignatureIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        SignatureIndex = *(uint16_t*)(row_data + offset);
    }
    offset += blob_index_size;

    out->Name = ecma_335_get_string(table_context, NameIndex);
    out->Signature = ecma_335_get_blob(table_context, SignatureIndex, &out->SignatureLength);

    metadata_table_add_row(table_info, out);

    return offset;
}
