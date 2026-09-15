#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_manifestresource(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    manifestresource_table_decoded_t* out;

    out = malloc(sizeof(manifestresource_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for manifestresource table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;

    // Calculate Implementation coded index size (2-bit tag: File=0, AssemblyRef=1, ExportedType=2)
    // For ManifestResource, ExportedType is not used in practice, but the encoding is shared.
    // A zero coded index means the resource is embedded in the current module.
    uint32_t implementation_max = table_context->row_count_table[METADATA_TABLE_File];
    if (table_context->row_count_table[METADATA_TABLE_AssemblyRef] > implementation_max)
        implementation_max = table_context->row_count_table[METADATA_TABLE_AssemblyRef];
    if (table_context->row_count_table[METADATA_TABLE_ExportedType] > implementation_max)
        implementation_max = table_context->row_count_table[METADATA_TABLE_ExportedType];
    uint32_t implementation_index_size = (implementation_max < (1 << 14)) ? 2 : 4;

    // Read Offset (always 4 bytes - byte offset within the manifest resource stream when
    // Implementation is null; otherwise a byte offset within the referenced file/assembly)
    out->Offset = *(uint32_t*)(row_data + offset);
    offset += 4;

    // Read Flags (always 4 bytes - ManifestResourceAttributes)
    out->Flags = *(uint32_t*)(row_data + offset);
    offset += 4;

    // Read Name (StringIndex - resource name, e.g. "MyApp.Resources.strings.resources")
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

    // Read Implementation (Implementation coded index - File, AssemblyRef, or 0 for embedded)
    if (implementation_index_size == 4)
    {
        out->ImplementationIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->ImplementationIndex = *(uint16_t*)(row_data + offset);
    }
    offset += implementation_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
