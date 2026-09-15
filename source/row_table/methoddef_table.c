#include "metadata_table.h"
#include "parser_table_types.h"
#include "pe_parser.h"
#include "method_parser.h"

static row_info_t * prev_row_info = NULL;

void metadata_table_init_methoddef(table_info_t * table_info)
{
    prev_row_info = NULL;
}

void metadata_table_update_methoddef(table_info_t * table_info, table_context_t * table_context)
{
    methoddef_table_decoded_t * current_data = (methoddef_table_decoded_t *)table_info->last_row->p_row;

    if (prev_row_info != NULL)
    {
        methoddef_table_decoded_t * prev_data = (methoddef_table_decoded_t *)prev_row_info->p_row;

        // update the ParamCount for the previous MethodDef entry based on the
        // ParamListIndex of the current MethodDef entry.
        prev_data->ParamCount = (current_data->ParamListIndex > prev_data->ParamListIndex) ?
            (current_data->ParamListIndex - prev_data->ParamListIndex) : 0;

        if (prev_data->ParamCount != 0)
        {
            // update the parameters from the Param table for the previous MethodDef entry based on the ParamListIndex
            // and ParamCount of the previous MethodDef entry. The parameters for each MethodDef entry are stored in
            // the Param table in sequential order based on the ParamListIndex and ParamCount for each MethodDef entry,
            // so we can calculate the range of parameters for the previous MethodDef entry based on the ParamListIndex
            // and ParamCount of the previous MethodDef entry and the ParamListIndex of the current MethodDef entry.

        }
    }

    prev_row_info = table_info->last_row;

    if (current_data->RVA != 0)
    {
        method_parser_get_info(table_context, current_data, &current_data->method_info);
    }
}

void metadata_table_finish_methoddef(table_info_t * table_info, table_context_t * table_context)
{
    if (prev_row_info != NULL)
    {
        methoddef_table_decoded_t * prev_data = (methoddef_table_decoded_t *)prev_row_info->p_row;

        // update last MethodDef entry's ParamCount based on the total number of
        // parameters in the metadata, which is determined by the row count of the Param table
        // in the tables stream header. If the last MethodDef entry has no parameters,
        // we set the ParamCount to zero. Otherwise, we set the ParamCount to
        // the total number of parameters minus the ParamListIndex to indicate that the last
        // MethodDef entry has parameters up to the end of the Param table.
        prev_data->ParamCount = (table_context->row_count_table[METADATA_TABLE_Param] >= prev_data->ParamListIndex) ?
            (table_context->row_count_table[METADATA_TABLE_Param] - prev_data->ParamListIndex + 1) : 0;
    }
}

uint32_t metadata_table_decode_methoddef(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index)
{
    methoddef_table_decoded_t* out;

    out = malloc(sizeof(methoddef_table_decoded_t));
    if (out == NULL)
    {
        printf("Failed to allocate memory for methoddef table decoded\n");
        return 0;
    }

    uint8_t * row_data = (uint8_t *)row_buffer + buffer_index;
    uint32_t offset = 0;
    uint32_t string_index_size = (table_context->tables_stream_header->HeapSizes & 0x01) ? 4 : 2;
    uint32_t blob_index_size   = (table_context->tables_stream_header->HeapSizes & 0x04) ? 4 : 2;

    // Calculate Param table index size
    uint32_t param_index_size = (table_context->row_count_table[METADATA_TABLE_Param] < (1 << 16)) ? 2 : 4;

    // Read RVA (always 4 bytes)
    out->RVA = *(uint32_t*)(row_data + offset);
    offset += 4;

    // Read ImplFlags (always 2 bytes)
    out->ImplFlags = *(uint16_t*)(row_data + offset);
    offset += 2;

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
    out->Name = ecma_335_get_string(table_context, NameIndex);
    offset += string_index_size;

    // Read Signature (BlobIndex)
    uint32_t SignatureIndex;
    if (blob_index_size == 4)
    {
        SignatureIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        SignatureIndex = *(uint16_t*)(row_data + offset);
    }
    out->Signature = ecma_335_get_blob(table_context, SignatureIndex, &out->SignatureLength);
    offset += blob_index_size;

    // Read ParamList (index into Param table)
    if (param_index_size == 4)
    {
        out->ParamListIndex = *(uint32_t*)(row_data + offset);
    }
    else
    {
        out->ParamListIndex = *(uint16_t*)(row_data + offset);
    }

    // This will be updated later when we decode subsequent MethodDef table rows and
    // know the range of parameters for this method
    out->ParamCount = 0;

    offset += param_index_size;

    metadata_table_add_row(table_info, out);

    return offset;
}
