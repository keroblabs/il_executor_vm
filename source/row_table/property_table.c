#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_property(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    property_table_decoded_t* out;

    out = malloc(sizeof(property_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for property table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;
    uint32_t blob_index_size   = (table_context->tables_stream_header->HeapSizes & 0x04) ? 4 : 2;

    // Read Flags (always 2 bytes)
    out->Flags = *(uint16_t*)(row_data + offset);
    offset += 2;

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

    // Read Type (BlobIndex - PropertySig)
    uint32_t TypeIndex;
    if (blob_index_size == 4)
    {
        TypeIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        TypeIndex = *(uint16_t*)(row_data + offset);
    }
    offset += blob_index_size;

    out->Name = ecma_335_get_string(table_context, NameIndex);
    out->Type = ecma_335_get_blob(table_context, TypeIndex, &out->TypeLength);

    metadata_table_add_row(table_info, out);

    return offset;
}
