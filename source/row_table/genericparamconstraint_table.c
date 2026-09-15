#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_genericparamconstraint(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    genericparamconstraint_table_decoded_t* out;

    out = malloc(sizeof(genericparamconstraint_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for genericparamconstraint table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;

    // Calculate GenericParam table index size
    uint32_t genericparam_index_size = (table_context->row_count_table[METADATA_TABLE_GenericParam] < (1 << 16)) ? 2 : 4;

    // Calculate TypeDefOrRef coded index size (2-bit tag: TypeDef=0, TypeRef=1, TypeSpec=2)
    uint32_t typedef_or_ref_max = table_context->row_count_table[METADATA_TABLE_TypeDef];
    if (table_context->row_count_table[METADATA_TABLE_TypeRef] > typedef_or_ref_max)
        typedef_or_ref_max = table_context->row_count_table[METADATA_TABLE_TypeRef];
    if (table_context->row_count_table[METADATA_TABLE_TypeSpec] > typedef_or_ref_max)
        typedef_or_ref_max = table_context->row_count_table[METADATA_TABLE_TypeSpec];
    uint32_t typedef_or_ref_index_size = (typedef_or_ref_max < (1 << 14)) ? 2 : 4;

    // Read Owner (index into GenericParam table - the type parameter being constrained)
    if (genericparam_index_size == 4)
    {
        out->OwnerIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->OwnerIndex = *(uint16_t*)(row_data + offset);
    }
    offset += genericparam_index_size;

    // Read Constraint (TypeDefOrRef coded index - the interface or base class the parameter must satisfy)
    if (typedef_or_ref_index_size == 4)
    {
        out->ConstraintIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->ConstraintIndex = *(uint16_t*)(row_data + offset);
    }
    offset += typedef_or_ref_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
