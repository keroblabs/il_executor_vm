#include "metadata_table.h"
#include "parser_table_types.h"

uint32_t metadata_table_decode_assembly(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    assembly_table_decoded_t* out;

    out = malloc(sizeof(assembly_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for assembly table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;
    uint32_t blob_index_size   = (table_context->tables_stream_header->HeapSizes & 0x04) ? 4 : 2;

    // Read HashAlgId (always 4 bytes)
    out->HashAlgId = *(uint32_t*)(row_data + offset);
    offset += 4;

    // Read MajorVersion (always 2 bytes)
    out->MajorVersion = *(uint16_t*)(row_data + offset);
    offset += 2;

    // Read MinorVersion (always 2 bytes)
    out->MinorVersion = *(uint16_t*)(row_data + offset);
    offset += 2;

    // Read BuildNumber (always 2 bytes)
    out->BuildNumber = *(uint16_t*)(row_data + offset);
    offset += 2;

    // Read RevisionNumber (always 2 bytes)
    out->RevisionNumber = *(uint16_t*)(row_data + offset);
    offset += 2;

    // Read Flags (always 4 bytes)
    out->Flags = *(uint32_t*)(row_data + offset);
    offset += 4;

    // Read PublicKey (BlobIndex)
    if (blob_index_size == 4)
    {
        out->PublicKeyIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->PublicKeyIndex = *(uint16_t*)(row_data + offset);
    }
    offset += blob_index_size;

    // Read Name (StringIndex)
    if (string_index_size == 4)
    {
        out->NameIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->NameIndex = *(uint16_t*)(row_data + offset);
    }
    offset += string_index_size;

    // Read Culture (StringIndex)
    if (string_index_size == 4)
    {
        out->CultureIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->CultureIndex = *(uint16_t*)(row_data + offset);
    }
    offset += string_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
