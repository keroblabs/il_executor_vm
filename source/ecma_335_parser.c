#include "ecma_335_types.h"
#include "metadata_table.h"
#include "ecma_335_parser.h"
#include "pe_parser.h"
#include "typedef_parser.h"
#include "fields_parser.h"
#include "type_ref_parser.h"
#include "member_ref_parser.h"

typedef struct metadata_name_map_t_
{
    const char* name;
    uint32_t kind;
} metadata_name_map_t;

////////////////////////////////////////////////////////////////////////////////////////

static const metadata_name_map_t metadata_stream_name_map[] =
{
    { "#~",        METADATA_STREAM_TABLES },
    { "#-",        METADATA_STREAM_TABLES_UNOPT },

    { "#Strings",  METADATA_STREAM_STRINGS },
    { "#Blob",     METADATA_STREAM_BLOB },
    { "#GUID",     METADATA_STREAM_GUID },
    { "#US",       METADATA_STREAM_US },

    { "#Pdb",      METADATA_STREAM_PDB },
    { "#PDB",      METADATA_STREAM_PDB },

    { "#ENC",      METADATA_STREAM_ENC },
    { "#ENCLog",   METADATA_STREAM_ENC_LOG },

    { NULL,        METADATA_STREAM_UNKNOWN }
};

////////////////////////////////////////////////////////////////////////////////////////

uint32_t get_stream_kind_from_name(const char * name)
{
    for (int i = 0; metadata_stream_name_map[i].name != NULL; i++)
    {
        if (strcmp(name, metadata_stream_name_map[i].name) == 0)
        {
            return metadata_stream_name_map[i].kind;
        }
    }

    return METADATA_STREAM_UNKNOWN;
}

const char* ecma_335_get_string(table_context_t * ctx, uint32_t string_index)
{
    ecma_file_t * file = (ecma_file_t *)ctx->ecma_data;

    // strings_heap points to start of #Strings stream
    return (const char*)(file->metadata_buffer + file->stream_headers[METADATA_STREAM_STRINGS]->Offset + string_index);
}

uint8_t* ecma_335_get_blob(table_context_t * ctx, uint32_t blob_index, uint32_t* out_length)
{
    ecma_file_t * file = (ecma_file_t *)ctx->ecma_data;

    uint8_t* ptr = file->metadata_buffer + file->stream_headers[METADATA_STREAM_BLOB]->Offset + blob_index;

    // Read compressed length (same format as compressed uint)
    uint32_t length;
    uint8_t* data;

    if ((ptr[0] & 0x80) == 0)
    {
        // 1-byte length
        length = ptr[0];
        data = ptr + 1;
    }
    else if ((ptr[0] & 0xC0) == 0x80)
    {
        // 2-byte length
        length = ((ptr[0] & 0x3F) << 8) | ptr[1];
        data = ptr + 2;
    }
    else
    {
        // 4-byte length
        length = ((ptr[0] & 0x1F) << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
        data = ptr + 4;
    }

    if (out_length)
        *out_length = length;

    return data;
}

const uint16_t* ecma_335_get_user_string(table_context_t * ctx, uint32_t index, uint32_t * out_char_count)
{
    ecma_file_t * file = (ecma_file_t *)ctx->ecma_data;

    if (out_char_count)
        *out_char_count = 0;

    metadata_stream_header_t * us_header = file->stream_headers[METADATA_STREAM_US];
    if (us_header == NULL || index >= us_header->Size)
    {
        return NULL;
    }

    // Per ECMA-335 II.24.2.4: index 0 is the empty string. Every non-empty
    // entry is a compressed-int length prefix followed by UTF-16 payload
    // and a single trailing terminator byte (0x00 or 0x01) that is counted
    // in the length.
    if (index == 0)
    {
        return NULL;
    }

    uint8_t * ptr = file->metadata_buffer + us_header->Offset + index;

    uint32_t length;
    uint8_t * data;

    if ((ptr[0] & 0x80) == 0)
    {
        length = ptr[0];
        data = ptr + 1;
    }
    else if ((ptr[0] & 0xC0) == 0x80)
    {
        length = ((ptr[0] & 0x3F) << 8) | ptr[1];
        data = ptr + 2;
    }
    else
    {
        length = ((ptr[0] & 0x1F) << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
        data = ptr + 4;
    }

    if (length == 0)
    {
        return NULL;
    }

    if (out_char_count)
        *out_char_count = (length - 1) / 2;

    return (const uint16_t *)data;
}

/* query:
I have a PE file with ECMA-335 data parsed, with all streams (#~, #Strings, #Blocb, #GUID and #US) and a linked list of tables, each with a linked list of rows.
MethodDef has method header (fat/tiny) and IL.
*/
ecma_data_t * ecma_335_parser(pe_data_t * pe_data, uint32_t md_offset, uint32_t md_size, uint32_t entry_point_token)
{
    FILE * fp = pe_get_file_handle(pe_data);
    ecma_file_t * ecma_file = (ecma_file_t *)malloc(sizeof(ecma_file_t));
    if (!ecma_file)
    {
        printf("Failed to allocate memory for ECMA file structure\n");
        return NULL;
    }

    memset(ecma_file, 0, sizeof(ecma_file_t));
    ecma_file->pe_data = pe_data;

    ecma_file->metadata_buffer = (uint8_t *)malloc(md_size);
    if (!ecma_file->metadata_buffer)
    {
        printf("Failed to allocate memory for ECMA metadata buffer\n");
        free(ecma_file);
        return NULL;
    }

    fseek(fp, md_offset, SEEK_SET);
    uint32_t bytes_read = fread(ecma_file->metadata_buffer, 1, md_size, fp);
    if (bytes_read != md_size)
    {
        printf("Failed to read ECMA metadata buffer\n");
        free(ecma_file->metadata_buffer);
        free(ecma_file);
        return NULL;
    }

    // Ge the metadada root header from the metadata buffer. The metadata root header is located at the beginning
    // of the metadata buffer and is of size sizeof(metadata_root_header_t).
    ecma_file->metadata_root = (metadata_root_header_t *)ecma_file->metadata_buffer;
    if (ecma_file->metadata_root->Signature != METADATA_SIGNATURE)
    {
        printf("Invalid metadata signature\n");
        free(ecma_file->metadata_buffer);
        free(ecma_file);
        return NULL;
    }

    // Store the entry point token in the ecma_file structure for access from the decoder functions when needed.
    // The entry point token is obtained from the PE header and is passed to the ecma_335_parser function as a parameter.
    // Storing the entry point token in the ecma_file structure allows us to access it from the decoder functions when
    // needed, such as when decoding the MethodDef table to identify the entry point method.
    ecma_file->entry_point_token = entry_point_token;

    // Get the metadata stream header from the metadata buffer. The metadata stream header is located immediately
    // after the metadata root header and is of size sizeof(metadata_stream_t) + (number_of_streams * sizeof(metadata_stream_header_t)).
    ecma_file->metadata_stream = (metadata_stream_t *)(ecma_file->metadata_buffer + sizeof(metadata_root_header_t) + ecma_file->metadata_root->VersionStringLength);

    // this is the base of the first stream header, which is located immediately after the metadata stream header and is of size
    // sizeof(metadata_stream_header_t) * number_of_streams. We can use this pointer to read the stream headers and get the
    // offset and size of each stream.
    uint8_t * stream_headers_buffer = (uint8_t *)ecma_file->metadata_stream->stream_headers_buffer;

    // read each stream header and store the pointer to the stream header for each stream kind in the ecma_file structure. This
    // allows us to quickly access the stream header for a given stream kind without having to search through the stream headers each time.
    uint32_t count = 0;
    while (count < ecma_file->metadata_stream->number_of_streams)
    {
        metadata_stream_header_t * stream_header_entry = (metadata_stream_header_t *)stream_headers_buffer;
        printf("Stream header: %s\n", stream_header_entry->Name);

        uint32_t stream_kind = get_stream_kind_from_name(stream_header_entry->Name);
        if (stream_kind != METADATA_STREAM_UNKNOWN && stream_kind < METADATA_STREAM_MAX)
        {
            ecma_file->stream_headers[stream_kind] = stream_header_entry;
        }

        stream_headers_buffer += sizeof(metadata_stream_header_t) + ((strlen(stream_header_entry->Name) + 1 + 3) & ~3); // align to 4 bytes
        count++;
    }

    if (ecma_file->stream_headers[METADATA_STREAM_TABLES] == NULL)
    {
        printf("Failed to find tables stream header\n");
        free(ecma_file->metadata_buffer);
        free(ecma_file);
        return NULL;
    }

    metadata_parse_tables(ecma_file);
    metadata_table_decode(&ecma_file->table_context);
    type_class_parser_link(ecma_file);
    metadata_parse_signature_for_methods(ecma_file);
    field_parser_decode_signature(ecma_file);
    type_ref_parser_link(ecma_file);
    member_ref_parser_decode_row(ecma_file);

    return (ecma_data_t *)ecma_file;
}
