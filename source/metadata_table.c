#include "metadata_table.h"
#include "parser_table_types.h"
#include "parser_table_data.h"
#include "method_parser.h"

static const table_map_t * get_row_handler(uint32_t id)
{
    for (size_t i = 0; i < sizeof(table_map) / sizeof(table_map[0]); i++)
    {
        if (table_map[i].table_id == id)
        {
            return &table_map[i];
        }
    }

    return NULL; // No decoder found for the given table ID
}

uint32_t metadata_decompress_integer(const uint8_t * buffer, uint32_t * value_out)
{
    uint32_t offset = 0;
    uint8_t first_byte = buffer[0];

    if ((first_byte & 0x80) == 0)
    {
        // 1-byte integer (0xxxxxxx)
        *value_out = first_byte;
        offset = 1;
    }
    else if ((first_byte & 0xC0) == 0x80)
    {
        // 2-byte integer (10xxxxxx xxxxxxxx)
        *value_out = ((first_byte & 0x3F) << 8) | buffer[1];
        offset = 2;
    }
    else if ((first_byte & 0xE0) == 0xC0)
    {
        // 4-byte integer (110xxxxx xxxxxxxx xxxxxxxx xxxxxxxx)
        *value_out = ((first_byte & 0x1F) << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
        offset = 4;
    }
    else
    {
        // Invalid compressed integer format
        *value_out = 0;
        offset = 0;
    }

    return offset;
}

table_info_t * metadata_table_get_by_id(uint32_t table_id, table_context_t * table_context)
{
    table_info_t * table_info = table_context->table_list;

    while (table_info)
    {
        if (table_info->id == table_id)
        {
            return table_info;
        }

        table_info = table_info->p_next;
    }

    return NULL;
}

void * metadata_table_get_row_data_by_index(uint32_t table_id, table_context_t * table_context, uint32_t row_index)
{
    table_info_t * table_info = metadata_table_get_by_id(table_id, table_context);
    if (table_info == NULL)
    {
        printf("No table info found for table ID 0x%02x\n", table_id);
        return NULL;
    }

    row_info_t * row_info = table_info->rows;
    uint32_t current_index = 1; // Row indices are 1-based

    while (row_info)
    {
        if (current_index == row_index)
        {
            return row_info->p_row;
        }

        row_info = row_info->p_next;
        current_index++;
    }

    return NULL; // No row found for the given index
}

void metadata_table_add_row(table_info_t * table_info, void * row_data)
{
    row_info_t * row_info = malloc(sizeof(row_info_t));
    if (row_info == NULL)
    {
        printf("Failed to allocate memory for row info\n");
        return;
    }

    row_info->p_parent = table_info;
    row_info->table_id = table_info->id;
    row_info->p_row = row_data;
    row_info->p_next = NULL; // table_info->rows;

    if (table_info->last_row)
    {
        table_info->last_row->p_next = row_info;
    }
    else
    {
        table_info->rows = row_info;
    }

    table_info->last_row = row_info;
}

bool_t metadata_table_decode(table_context_t * table_context)
{
    table_info_t * current_table_info = table_context->table_list;
    row_info_t * prev_row_info = NULL;
    uint32_t table_buffer_offset = 0;

    while (current_table_info != NULL)
    {
        const table_map_t * row_handler = get_row_handler(current_table_info->id);
        if (row_handler == NULL)
        {
            printf("No map found for table ID 0x%02x\n", current_table_info->id);
            return FALSE; // Failure
        }

        if (row_handler->decode_function == NULL)
        {
            printf("No decoder function found for table ID 0x%02x\n", current_table_info->id);
            return FALSE; // Failure
        }

        if (row_handler->init_function)
        {
            row_handler->init_function(current_table_info);
        }

        for (uint32_t i = 0; i < current_table_info->count; i++)
        {
            uint32_t bytes_decoded = row_handler->decode_function(table_context, current_table_info, table_context->row_buffer, table_buffer_offset);
            if (bytes_decoded == 0)
            {
                printf("Failed to decode row %u of table ID %u\n", i + 1, current_table_info->id);
                return FALSE; // Failure
            }

            if (row_handler->update_function)
            {
                row_handler->update_function(current_table_info, table_context);
            }

            table_buffer_offset += bytes_decoded;
        }

        if (row_handler->finish_function)
        {
            row_handler->finish_function(current_table_info, table_context);
        }

        printf("Decoded table ID 0x%02x with %u rows\n", current_table_info->id, current_table_info->count);

        current_table_info = current_table_info->p_next;
    }

    return TRUE; // Success
}

void metadata_parse_signature_for_methods(ecma_file_t * ecma_file)
{
    table_info_t * methoddef_table_info = metadata_table_get_by_id(METADATA_TABLE_MethodDef, &ecma_file->table_context);
    if (methoddef_table_info == NULL)
    {
        printf("No MethodDef table found in metadata\n");
        return;
    }

    uint32_t counter = 0;

    row_info_t * methoddef_row_info = methoddef_table_info->rows;
    while (methoddef_row_info)
    {
        methoddef_table_decoded_t * methoddef_data = (methoddef_table_decoded_t *)methoddef_row_info->p_row;
        if (methoddef_data != NULL)
        {
            method_parser_decode_signature(methoddef_data->Signature, methoddef_data->SignatureLength, &ecma_file->table_context, methoddef_table_info, &methoddef_data->method_info);
            method_parser_decode_locals(&ecma_file->table_context, methoddef_table_info, &methoddef_data->method_info);
            counter++;
            typedef_table_decoded_t * typedef_data = methoddef_data->method_info.typedef_row_info ? (typedef_table_decoded_t *)methoddef_data->method_info.typedef_row_info->p_row : NULL;
            printf("Decoded signature for method %u: %s", counter, methoddef_data->Name);

            if (typedef_data)
            {
                printf(" ** in type %s.%s", typedef_data->TypeNamespace, typedef_data->TypeName);
            }

            if (ecma_file->entry_point_token >> 24 == METADATA_TABLE_MethodDef && (ecma_file->entry_point_token & 0xFFFFFF) == counter)
            {
                printf(" ** entry point");
                ecma_file->table_context.entry_point_method = methoddef_row_info;
            }

            printf(" ** local variables: %u", methoddef_data->method_info.local_count);

            printf(" ** parameters: %u", methoddef_data->method_info.decoded_signature ? methoddef_data->method_info.decoded_signature->param_count : 0);

            printf("\n");
        }

        methoddef_row_info = methoddef_row_info->p_next;
    }
}

bool_t metadata_parse_tables(ecma_file_t * ecma_file)
{
    // Initialize the table context for decoding metadata tables, this is used by the decoder functions to access the tables stream
    // header and the row buffer for decoding the rows of each table. The table context is initialized in the ecma_335_parse_tables
    // function and passed to each decoder function for each table. The table context contains a pointer to the tables stream header,
    // an array of row counts for each metadata table, a linked list of table info for each metadata table, a buffer containing the
    // raw row data for all tables, and a pointer back to the parent ecma_file structure. The table context is used by the decoder
    // functions to access the tables stream header and the row buffer for decoding the rows of each table.
    ecma_file->table_context.ecma_data = ecma_file;

    // Point to the tables stream header, which contains the number of rows for each metadata table. We can use this information
    // to quickly access the metadata tables and their rows. The tables stream header is located at the offset specified in the
    // stream header for the #~ stream, which is stored in the ecma_file structure. The tables stream header is of
    // size sizeof(metadata_table_root_t) + (number_of_tables * sizeof(uint32_t)).
    ecma_file->table_context.tables_stream_header = (metadata_table_root_t *)(ecma_file->metadata_buffer + ecma_file->stream_headers[METADATA_STREAM_TABLES]->Offset);

    uint32_t table_data_offset = 0;
    uint32_t row_index = 0;
    table_info_t * last_table_data = NULL;

    for (table_data_offset = 0; table_data_offset < METADATA_TABLE_MAX; table_data_offset++)
    {
        if (ecma_file->table_context.tables_stream_header->ValidMask & ((uint64_t)1 << table_data_offset))
        {
            table_info_t * table_data = malloc(sizeof(table_info_t));
            if (table_data == NULL)
            {
                printf("Failed to allocate memory for table data\n");
                free(ecma_file->metadata_buffer);
                free(ecma_file);
                return FALSE;
            }

            table_data->id = table_data_offset;
            table_data->count = ecma_file->table_context.tables_stream_header->RowCount[row_index];
            table_data->p_next = NULL;
            table_data->rows = NULL;
            table_data->last_row = NULL;

            ecma_file->table_context.row_count_table[table_data_offset] = table_data->count;

            if (last_table_data == NULL)
            {
                ecma_file->table_context.table_list = table_data;
            }
            else
            {
                last_table_data->p_next = table_data;
            }

            last_table_data = table_data;

            printf("Number of rows in table 0x%02x: %u\n", table_data_offset, ecma_file->table_context.row_count_table[table_data_offset]);
            row_index++;
        }
    }

    // the base data for the tables is located immediately after the tables stream header and the row counts for each table.
    // The size of the tables stream header is sizeof(metadata_table_root_t) + (number_of_tables * sizeof(uint32_t)).
    ecma_file->table_context.row_buffer = (uint8_t *)&ecma_file->table_context.tables_stream_header->RowCount[row_index];
    ecma_file->table_context.pe_data = ecma_file->pe_data;

    return TRUE;
}
