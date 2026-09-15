#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_event(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    event_table_decoded_t* out;

    out = malloc(sizeof(event_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for event table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;

    // Calculate TypeDefOrRef coded index size (2-bit tag: TypeDef=0, TypeRef=1, TypeSpec=2)
    uint32_t typedef_or_ref_max = table_context->row_count_table[METADATA_TABLE_TypeDef];
    if (table_context->row_count_table[METADATA_TABLE_TypeRef] > typedef_or_ref_max)
        typedef_or_ref_max = table_context->row_count_table[METADATA_TABLE_TypeRef];
    if (table_context->row_count_table[METADATA_TABLE_TypeSpec] > typedef_or_ref_max)
        typedef_or_ref_max = table_context->row_count_table[METADATA_TABLE_TypeSpec];
    uint32_t typedef_or_ref_index_size = (typedef_or_ref_max < (1 << 14)) ? 2 : 4;

    // Read EventFlags (always 2 bytes - EventAttributes)
    out->EventFlags = *(uint16_t*)(row_data + offset);
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
    out->Name = ecma_335_get_string(table_context, NameIndex);
    offset += string_index_size;

    // Read EventType (TypeDefOrRef coded index - delegate type of the event)
    if (typedef_or_ref_index_size == 4)
    {
        out->EventTypeIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->EventTypeIndex = *(uint16_t*)(row_data + offset);
    }
    offset += typedef_or_ref_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
