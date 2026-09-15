#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_exportedtype(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    exportedtype_table_decoded_t* out;

    out = malloc(sizeof(exportedtype_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for exportedtype table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;

    // Calculate Implementation coded index size (2-bit tag: File=0, AssemblyRef=1, ExportedType=2)
    uint32_t implementation_max = table_context->row_count_table[METADATA_TABLE_File];
    if (table_context->row_count_table[METADATA_TABLE_AssemblyRef] > implementation_max)
        implementation_max = table_context->row_count_table[METADATA_TABLE_AssemblyRef];
    if (table_context->row_count_table[METADATA_TABLE_ExportedType] > implementation_max)
        implementation_max = table_context->row_count_table[METADATA_TABLE_ExportedType];
    uint32_t implementation_index_size = (implementation_max < (1 << 14)) ? 2 : 4;

    // Read Flags (always 4 bytes - TypeAttributes)
    out->Flags = *(uint32_t*)(row_data + offset);
    offset += 4;

    // Read TypeDefId (always 4 bytes - hint index into the exporting assembly's TypeDef table;
    // not a coded index and not authoritative - Implementation + name is what resolves the type)
    out->TypeDefId = *(uint32_t*)(row_data + offset);
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

    // Read Implementation (Implementation coded index - File, AssemblyRef, or ExportedType)
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
