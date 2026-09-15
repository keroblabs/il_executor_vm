#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_declsecurity(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    declsecurity_table_decoded_t* out;

    out = malloc(sizeof(declsecurity_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for declsecurity table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t blob_index_size = (table_context->tables_stream_header->HeapSizes & 0x04) ? 4 : 2;

    // Calculate HasDeclSecurity coded index size (2-bit tag: TypeDef=0, MethodDef=1, Assembly=2)
    uint32_t has_decl_security_max = table_context->row_count_table[METADATA_TABLE_TypeDef];
    if (table_context->row_count_table[METADATA_TABLE_MethodDef] > has_decl_security_max)
        has_decl_security_max = table_context->row_count_table[METADATA_TABLE_MethodDef];
    if (table_context->row_count_table[METADATA_TABLE_Assembly] > has_decl_security_max)
        has_decl_security_max = table_context->row_count_table[METADATA_TABLE_Assembly];
    uint32_t has_decl_security_index_size = (has_decl_security_max < (1 << 14)) ? 2 : 4;

    // Read Action (always 2 bytes - SecurityAction enum)
    out->Action = *(uint16_t*)(row_data + offset);
    offset += 2;

    // Read Parent (HasDeclSecurity coded index)
    if (has_decl_security_index_size == 4)
    {
        out->ParentIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->ParentIndex = *(uint16_t*)(row_data + offset);
    }
    offset += has_decl_security_index_size;

    // Read PermissionSet (BlobIndex) and resolve into the #Blob heap so the
    // security consumer receives a pointer + length ready to be parsed as an
    // XML permission set (2.0+) or a serialized attribute blob (4.0+).
    uint32_t PermissionSetIndex;
    if (blob_index_size == 4)
    {
        PermissionSetIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        PermissionSetIndex = *(uint16_t*)(row_data + offset);
    }
    out->PermissionSet = ecma_335_get_blob(table_context, PermissionSetIndex, &out->PermissionSetLength);
    offset += blob_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
