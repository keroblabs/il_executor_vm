#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_fieldmarshal(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    fieldmarshal_table_decoded_t* out;

    out = malloc(sizeof(fieldmarshal_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for fieldmarshal table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t blob_index_size = (table_context->tables_stream_header->HeapSizes & 0x04) ? 4 : 2;

    // Calculate HasFieldMarshal coded index size (1-bit tag: Field=0, Param=1)
    uint32_t has_field_marshal_max = table_context->row_count_table[METADATA_TABLE_Field];
    if (table_context->row_count_table[METADATA_TABLE_Param] > has_field_marshal_max)
        has_field_marshal_max = table_context->row_count_table[METADATA_TABLE_Param];
    uint32_t has_field_marshal_index_size = (has_field_marshal_max < (1 << 15)) ? 2 : 4;

    // Read Parent (HasFieldMarshal coded index)
    if (has_field_marshal_index_size == 4)
    {
        out->ParentIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->ParentIndex = *(uint16_t*)(row_data + offset);
    }
    offset += has_field_marshal_index_size;

    // Read NativeType (BlobIndex) and resolve into the #Blob heap so the marshaller
    // consumer receives a pointer + length ready to be parsed as a MarshalSpec (II.23.4).
    uint32_t NativeTypeIndex;
    if (blob_index_size == 4)
    {
        NativeTypeIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        NativeTypeIndex = *(uint16_t*)(row_data + offset);
    }
    out->NativeType = ecma_335_get_blob(table_context, NativeTypeIndex, &out->NativeTypeLength);
    offset += blob_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
