#include "typedef_parser.h"

static void link_field_to_typedef(row_info_t * typedef_row_info, table_info_t * field_table_info)
{
    typedef_table_decoded_t * typedef_data = (typedef_table_decoded_t *)typedef_row_info->p_row;

    if (typedef_data == NULL || field_table_info == NULL)
    {
        return;
    }

    // Link the Field entries for the TypeDef entry based on the FieldList index and FieldList count
    // of the TypeDef entry. The Field entries for each TypeDef entry are stored in the Field table
    // in sequential order based on the FieldList index and FieldList count for each TypeDef entry, so we can
    // calculate the range of Field entries for the TypeDef entry based on the FieldList index and FieldList
    // count of the TypeDef entry.

    uint32_t field_list_index = typedef_data->FieldListIndex;
    uint32_t field_list_count = typedef_data->FieldListCount;

    if (field_list_count == 0)
    {
        return; // No fields to link
    }

    row_info_t * field_row_info = field_table_info->rows;
    uint32_t current_field_index = 1; // Row indices are 1-based

    while (field_row_info && current_field_index < field_list_index)
    {
        field_row_info = field_row_info->p_next;
        current_field_index++;
    }

    for (uint32_t i = 0; i < field_list_count && field_row_info; i++)
    {
        field_table_decoded_t * field_data = (field_table_decoded_t *)field_row_info->p_row;
        field_data->field_info.typedef_row_info = typedef_row_info; // Link Field to its parent TypeDef

        field_row_info = field_row_info->p_next;
    }
}

static void link_methoddef_to_typedef(row_info_t * typedef_row_info, table_info_t * methoddef_table_info)
{
    typedef_table_decoded_t * typedef_data = (typedef_table_decoded_t *)typedef_row_info->p_row;

    if (typedef_data == NULL || methoddef_table_info == NULL)
    {
        return;
    }

    // Link the MethodDef entries for the TypeDef entry based on the MethodList index and MethodList count
    // of the TypeDef entry. The MethodDef entries for each TypeDef entry are stored in the MethodDef table
    // in sequential order based on the MethodList index and MethodList count for each TypeDef entry, so we can
    // calculate the range of MethodDef entries for the TypeDef entry based on the MethodList index and MethodList
    // count of the TypeDef entry.

    uint32_t method_list_index = typedef_data->MethodListIndex;
    uint32_t method_list_count = typedef_data->MethodListCount;

    if (method_list_count == 0)
    {
        return; // No methods to link
    }

    row_info_t * methoddef_row_info = methoddef_table_info->rows;
    uint32_t current_method_index = 1; // Row indices are 1-based

    while (methoddef_row_info && current_method_index < method_list_index)
    {
        methoddef_row_info = methoddef_row_info->p_next;
        current_method_index++;
    }

    for (uint32_t i = 0; i < method_list_count && methoddef_row_info; i++)
    {
        methoddef_table_decoded_t * methoddef_data = (methoddef_table_decoded_t *)methoddef_row_info->p_row;
        methoddef_data->method_info.typedef_row_info = typedef_row_info; // Link MethodDef to its parent TypeDef

        methoddef_row_info = methoddef_row_info->p_next;
    }
}

void type_class_parser_link(ecma_file_t * ecma_file)
{
    table_info_t * typedef_table_info = metadata_table_get_by_id(METADATA_TABLE_TypeDef, &ecma_file->table_context);
    table_info_t * methoddef_table_info = metadata_table_get_by_id(METADATA_TABLE_MethodDef, &ecma_file->table_context);
    table_info_t * field_table_info = metadata_table_get_by_id(METADATA_TABLE_Field, &ecma_file->table_context);

    if (typedef_table_info == NULL || methoddef_table_info == NULL || field_table_info == NULL)
    {
        printf("No TypeDef, MethodDef, or Field table found in metadata\n");
        return;
    }

    uint32_t counter = 1;

    row_info_t * typedef_row_info = typedef_table_info->rows;
    while (typedef_row_info)
    {
        link_methoddef_to_typedef(typedef_row_info, methoddef_table_info);
        link_field_to_typedef(typedef_row_info, field_table_info);

        printf("Linked TypeDef 0x%03x %s.%s to its MethodDef and Field entries", counter, ((typedef_table_decoded_t *)typedef_row_info->p_row)->TypeNamespace, ((typedef_table_decoded_t *)typedef_row_info->p_row)->TypeName);
        printf(" Parent class: 0x%08x\n", ((typedef_table_decoded_t *)typedef_row_info->p_row)->ExtendsIndex );

        typedef_row_info = typedef_row_info->p_next;
        counter++;
    }
}
