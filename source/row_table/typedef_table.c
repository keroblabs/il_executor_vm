#include "metadata_table.h"
#include "parser_table_types.h"

static row_info_t * prev_row_info = NULL;

void metadata_table_init_typedef(table_info_t * table_info)
{
    prev_row_info = NULL;
}

void metadata_table_update_typedef(table_info_t * table_info, table_context_t * table_context)
{
    if (prev_row_info != NULL)
    {
        typedef_table_decoded_t * prev_data = (typedef_table_decoded_t *)prev_row_info->p_row;
        typedef_table_decoded_t * current_data = (typedef_table_decoded_t *)table_info->last_row->p_row;

        // update the FieldList count and MethodList count for the previous TypeDef entry based on the
        // FieldList index and MethodList index of the current TypeDef entry.
        prev_data->FieldListCount = (current_data->FieldListIndex > prev_data->FieldListIndex) ?
            (current_data->FieldListIndex - prev_data->FieldListIndex) : 0;
        prev_data->MethodListCount = (current_data->MethodListIndex > prev_data->MethodListIndex) ?
            (current_data->MethodListIndex - prev_data->MethodListIndex) : 0;
    }

    prev_row_info = table_info->last_row;
}

void metadata_table_finish_typedef(table_info_t * table_info, table_context_t * table_context)
{
    if (prev_row_info != NULL)
    {
        typedef_table_decoded_t * prev_data = (typedef_table_decoded_t *)prev_row_info->p_row;

        // update last TypeDef entry's FieldList count and MethodList count based on the total number of
        // fields and methods in the metadata, which is determined by the row count of the Field table and
        // MethodDef table in the tables stream header. If the last TypeDef entry has no fields or methods,
        // we set the Field ListIndex or MethodListIndex to zero to indicate that there are no fields or
        // methods for the last TypeDef entry. Otherwise, we set the FieldListLast or MethodListLast to
        // the total number of fields or methods in the metadata to indicate that the last TypeDef entry
        // has fields or methods up to the end of the Field table or MethodDef table.
        prev_data->FieldListCount = (table_context->row_count_table[METADATA_TABLE_Field] > prev_data->FieldListIndex) ?
            (table_context->row_count_table[METADATA_TABLE_Field] - prev_data->FieldListIndex) : 0;
        prev_data->MethodListCount = (table_context->row_count_table[METADATA_TABLE_MethodDef] > prev_data->MethodListIndex) ?
            (table_context->row_count_table[METADATA_TABLE_MethodDef] - prev_data->MethodListIndex) : 0;
    }
}

uint32_t metadata_table_decode_typedef(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    typedef_table_decoded_t* out;

    out = malloc(sizeof(typedef_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for typedef table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;

    // Calculate TypeDefOrRef coded index size (2-bit tag: TypeDef, TypeRef, TypeSpec)
    uint32_t typedef_or_ref_max = table_context->row_count_table[METADATA_TABLE_TypeDef];
    if (table_context->row_count_table[METADATA_TABLE_TypeRef] > typedef_or_ref_max)
        typedef_or_ref_max = table_context->row_count_table[METADATA_TABLE_TypeRef];
    if (table_context->row_count_table[METADATA_TABLE_TypeSpec] > typedef_or_ref_max)
        typedef_or_ref_max = table_context->row_count_table[METADATA_TABLE_TypeSpec];
    uint32_t typedef_or_ref_index_size = (typedef_or_ref_max < (1 << 14)) ? 2 : 4;

    // Calculate Field table index size
    uint32_t field_index_size = (table_context->row_count_table[METADATA_TABLE_Field] < (1 << 16)) ? 2 : 4;

    // Calculate MethodDef table index size
    uint32_t methoddef_index_size = (table_context->row_count_table[METADATA_TABLE_MethodDef] < (1 << 16)) ? 2 : 4;

    // Read Flags (always 4 bytes)
    out->Flags = *(uint32_t*)(row_data + offset);
    offset += 4;

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
    out->TypeName = ecma_335_get_string(table_context, TypeNameIndex);
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
    out->TypeNamespace = ecma_335_get_string(table_context, TypeNamespaceIndex);
    offset += string_index_size;

    // Read Extends (TypeDefOrRef coded index)
    uint32_t extends_index_value;
    if (typedef_or_ref_index_size == 4)
    {
        extends_index_value = *(uint32_t*)(row_data + offset);
    }
    else
    {
        extends_index_value = *(uint16_t*)(row_data + offset);
    }

    out->ExtendsIndex = extends_index_value >> 2; // Remove the 2-bit tag to get the actual index

    if ((extends_index_value & 0x3) == 0) // TypeDef
    {
        out->ExtendsIndex |= (METADATA_TABLE_TypeDef << 24); // Set the high 2 bits to indicate TypeDef
    }
    else if ((extends_index_value & 0x3) == 1) // TypeRef
    {
        out->ExtendsIndex |= (METADATA_TABLE_TypeRef << 24); // Set the high 2 bits to indicate TypeRef
    }
    else if ((extends_index_value & 0x3) == 2) // TypeSpec
    {
        out->ExtendsIndex |= (METADATA_TABLE_TypeSpec << 24); // Set the high 2 bits to indicate TypeSpec
    }

    offset += typedef_or_ref_index_size;

    // Read FieldList (index into Field table)
    if (field_index_size == 4)
    {
        out->FieldListIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->FieldListIndex = *(uint16_t*)(row_data + offset);
    }

    // This will be updated later when we decode the Field table rows and
    // know the last index for the Field table
    out->FieldListCount = 0;

    offset += field_index_size;

    // Read MethodList (index into MethodDef table)
    if (methoddef_index_size == 4)
    {
        out->MethodListIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->MethodListIndex = *(uint16_t*)(row_data + offset);
    }

    // This will be updated later when we decode the MethodDef table rows and
    // know the last index for the MethodDef table
    out->MethodListCount = 0;

    offset += methoddef_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
