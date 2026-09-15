#include "fields_parser.h"
#include "signature_parser.h"

static void field_parser_decode_signature_for_field(ecma_file_t * ecma_file, field_table_decoded_t * field_data)
{
    if (field_data == NULL || field_data->Signature == NULL || field_data->SignatureLength == 0)
    {
        return; // No signature to decode
    }

    // Decode the field signature blob for this field and store the decoded type in the field info structure for the field
    signature_decode_field(field_data->Signature, field_data->SignatureLength, &field_data->field_info.decoded_type);
}

void field_parser_decode_signature(ecma_file_t * ecma_file)
{
    table_info_t * field_table_info = metadata_table_get_by_id(METADATA_TABLE_Field, &ecma_file->table_context);
    if (field_table_info == NULL)
    {
        printf("No Field table found in metadata\n");
        return;
    }

    uint32_t counter = 1;
    row_info_t * field_row_info = field_table_info->rows;
    while (field_row_info)
    {
        field_table_decoded_t * field_data = (field_table_decoded_t *)field_row_info->p_row;
        if (field_data != NULL)
        {
            // Decode the field signature blob for this field and store the decoded type in the field info structure for the field
            field_parser_decode_signature_for_field(ecma_file, field_data);

            printf("Decoded signature for field %d: %s", counter, field_data->Name);

            if (field_data->field_info.typedef_row_info)
            {
                typedef_table_decoded_t * typedef_data = (typedef_table_decoded_t *)field_data->field_info.typedef_row_info->p_row;
                if (typedef_data)
                {
                    printf(" ** in type %s.%s", typedef_data->TypeNamespace, typedef_data->TypeName);
                }
            }

            printf("\n");

            counter++;
        }

        field_row_info = field_row_info->p_next;
    }
}
