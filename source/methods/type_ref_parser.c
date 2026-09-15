#include "type_ref_parser.h"

void type_ref_parser_link(ecma_file_t * ecma_file)
{
    // For the TypeRef table, we can link the TypeRef entries to their parent TypeDef entries based on the ResolutionScope
    // coded index of the TypeRef entries, which can reference a TypeDef entry. This allows us to easily navigate from a
    // TypeRef entry to its parent TypeDef entry and access the type information for the referenced type.

    table_info_t * typeref_table_info = metadata_table_get_by_id(METADATA_TABLE_TypeRef, &ecma_file->table_context);
    if (typeref_table_info == NULL)
    {
        printf("No TypeRef table found in metadata\n");
        return;
    }

    uint32_t counter = 1;

    row_info_t * typeref_row_info = typeref_table_info->rows;
    while (typeref_row_info)
    {
        typeref_table_decoded_t * typeref_data = (typeref_table_decoded_t *)typeref_row_info->p_row;
        if (typeref_data != NULL)
        {
            printf("TypeRef 0x%03x: %s.%s, ResolutionScopeIndex: %u\n", counter, typeref_data->TypeNamespace, typeref_data->TypeName, typeref_data->ResolutionScopeIndex);
        }

        typeref_row_info = typeref_row_info->p_next;
        counter++;
    }
}
