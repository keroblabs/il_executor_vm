#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_customattribute(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    customattribute_table_decoded_t* out;

    out = malloc(sizeof(customattribute_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for customattribute table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t blob_index_size = (table_context->tables_stream_header->HeapSizes & 0x04) ? 4 : 2;

    // Calculate HasCustomAttribute coded index size (5-bit tag: many tables)
    // Tables: MethodDef, Field, TypeRef, TypeDef, Param, InterfaceImpl, MemberRef, Module,
    // DeclSecurity, Property, Event, StandAloneSig, ModuleRef, TypeSpec, Assembly,
    // AssemblyRef, File, ExportedType, ManifestResource, GenericParam, GenericParamConstraint, MethodSpec
    uint32_t has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_MethodDef];
    if (table_context->row_count_table[METADATA_TABLE_Field] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_Field];
    if (table_context->row_count_table[METADATA_TABLE_TypeRef] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_TypeRef];
    if (table_context->row_count_table[METADATA_TABLE_TypeDef] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_TypeDef];
    if (table_context->row_count_table[METADATA_TABLE_Param] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_Param];
    if (table_context->row_count_table[METADATA_TABLE_InterfaceImpl] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_InterfaceImpl];
    if (table_context->row_count_table[METADATA_TABLE_MemberRef] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_MemberRef];
    if (table_context->row_count_table[METADATA_TABLE_Module] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_Module];
    if (table_context->row_count_table[METADATA_TABLE_DeclSecurity] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_DeclSecurity];
    if (table_context->row_count_table[METADATA_TABLE_Property] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_Property];
    if (table_context->row_count_table[METADATA_TABLE_Event] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_Event];
    if (table_context->row_count_table[METADATA_TABLE_StandAloneSig] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_StandAloneSig];
    if (table_context->row_count_table[METADATA_TABLE_ModuleRef] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_ModuleRef];
    if (table_context->row_count_table[METADATA_TABLE_TypeSpec] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_TypeSpec];
    if (table_context->row_count_table[METADATA_TABLE_Assembly] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_Assembly];
    if (table_context->row_count_table[METADATA_TABLE_AssemblyRef] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_AssemblyRef];
    if (table_context->row_count_table[METADATA_TABLE_File] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_File];
    if (table_context->row_count_table[METADATA_TABLE_ExportedType] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_ExportedType];
    if (table_context->row_count_table[METADATA_TABLE_ManifestResource] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_ManifestResource];
    if (table_context->row_count_table[METADATA_TABLE_GenericParam] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_GenericParam];
    if (table_context->row_count_table[METADATA_TABLE_GenericParamConstraint] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_GenericParamConstraint];
    if (table_context->row_count_table[METADATA_TABLE_MethodSpec] > has_custom_attr_max)
        has_custom_attr_max = table_context->row_count_table[METADATA_TABLE_MethodSpec];
    uint32_t has_custom_attr_index_size = (has_custom_attr_max < (1 << 11)) ? 2 : 4;

    // Calculate CustomAttributeType coded index size (3-bit tag: MethodDef=2, MemberRef=3)
    uint32_t custom_attr_type_max = table_context->row_count_table[METADATA_TABLE_MethodDef];
    if (table_context->row_count_table[METADATA_TABLE_MemberRef] > custom_attr_type_max)
        custom_attr_type_max = table_context->row_count_table[METADATA_TABLE_MemberRef];
    uint32_t custom_attr_type_index_size = (custom_attr_type_max < (1 << 13)) ? 2 : 4;

    // Read Parent (HasCustomAttribute coded index)
    if (has_custom_attr_index_size == 4)
    {
        out->ParentIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->ParentIndex = *(uint16_t*)(row_data + offset);
    }
    offset += has_custom_attr_index_size;

    // Read Type (CustomAttributeType coded index)
    if (custom_attr_type_index_size == 4)
    {
        out->TypeIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->TypeIndex = *(uint16_t*)(row_data + offset);
    }
    offset += custom_attr_type_index_size;

    // Read Value (BlobIndex)
    if (blob_index_size == 4)
    {
        out->ValueIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->ValueIndex = *(uint16_t*)(row_data + offset);
    }
    offset += blob_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
