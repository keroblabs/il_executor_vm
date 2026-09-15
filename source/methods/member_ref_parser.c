#include "member_ref_parser.h"
#include "signature_parser.h"

static void member_ref_parser_decode_signature_for_member_ref(memberref_table_decoded_t * memberref_data)
{
    if (memberref_data == NULL || memberref_data->Signature == NULL || memberref_data->SignatureLength == 0)
    {
        return; // No signature to decode
    }

    // Decode the MemberRef signature blob for this MemberRef and store the decoded method signature in the MemberRef info structure for this MemberRef entry
    signature_decode_member_ref(memberref_data->Signature, memberref_data->SignatureLength, &memberref_data->decoded_member_ref_sig);
}

static void get_member_ref_parent_string(table_context_t * table_context, memberref_table_decoded_t * memberref_data)
{
    if (memberref_data->ClassIndex == 0)
    {
        //snprintf(buffer, buffer_size, "<Module>");
        memberref_data->ClassName = NULL;
        memberref_data->ClassNamespace = NULL;
        return;
    }

    // The ClassIndex is a coded index that can reference TypeDef, TypeRef, ModuleRef, MethodDef, or TypeSpec
    uint32_t table_id = memberref_data->ClassIndex & 0x03; // 2-bit tag
    uint32_t row_index = memberref_data->ClassIndex >> 2;   // Row index

    switch (table_id)
    {
        case 0: // TypeDef
            {
                typedef_table_decoded_t * typedef_data = (typedef_table_decoded_t *)metadata_table_get_row_data_by_index(METADATA_TABLE_TypeDef, table_context, row_index);
                if (typedef_data)
                {
                    memberref_data->ClassName = typedef_data->TypeName;
                    memberref_data->ClassNamespace = typedef_data->TypeNamespace;
                }
                else
                {
                    memberref_data->ClassName = NULL;
                    memberref_data->ClassNamespace = NULL;
                }
            }
            break;
        case 1: // TypeRef
            {
                typeref_table_decoded_t * typeref_data = (typeref_table_decoded_t *)metadata_table_get_row_data_by_index(METADATA_TABLE_TypeRef, table_context, row_index);
                if (typeref_data)
                {
                    memberref_data->ClassName = typeref_data->TypeName;
                    memberref_data->ClassNamespace = typeref_data->TypeNamespace;
                }
                else
                {
                    memberref_data->ClassName = NULL;
                    memberref_data->ClassNamespace = NULL;
                }
            }
            break;
        case 2: // ModuleRef
            {
                moduleref_table_decoded_t * moduleref_data = (moduleref_table_decoded_t *)metadata_table_get_row_data_by_index(METADATA_TABLE_ModuleRef, table_context, row_index);
                if (moduleref_data)
                {
                    memberref_data->ClassName = moduleref_data->Name;
                    memberref_data->ClassNamespace = NULL;
                }
                else
                {
                    memberref_data->ClassName = NULL;
                    memberref_data->ClassNamespace = NULL;
                }
            }
            break;
        case 3: // MethodDef
            {
                methoddef_table_decoded_t * methoddef_data = (methoddef_table_decoded_t *)metadata_table_get_row_data_by_index(METADATA_TABLE_MethodDef, table_context, row_index);
                if (methoddef_data)
                {
                    memberref_data->ClassName = methoddef_data->Name;
                    memberref_data->ClassNamespace = NULL;
                }
                else
                {
                    memberref_data->ClassName = NULL;
                    memberref_data->ClassNamespace = NULL;
                }
            }
            break;
        default:
            memberref_data->ClassName = NULL;
            memberref_data->ClassNamespace = NULL;
            break;
    }
}

void member_ref_parser_decode_row(ecma_file_t * ecma_file)
{
    // For the MemberRef table, we can decode the signature blob for each MemberRef entry and store the decoded method signature in the MemberRef info structure for each MemberRef entry. This allows us to easily access the method signature information for each MemberRef entry when needed, such as when displaying method information or resolving method references.

    table_info_t * memberref_table_info = metadata_table_get_by_id(METADATA_TABLE_MemberRef, &ecma_file->table_context);
    if (memberref_table_info == NULL)
    {
        printf("No MemberRef table found in metadata\n");
        return;
    }

    uint32_t counter = 1;

    row_info_t * memberref_row_info = memberref_table_info->rows;
    while (memberref_row_info)
    {
        memberref_table_decoded_t * memberref_data = (memberref_table_decoded_t *)memberref_row_info->p_row;
        if (memberref_data != NULL)
        {
            member_ref_parser_decode_signature_for_member_ref(memberref_data);

            get_member_ref_parent_string(&ecma_file->table_context, memberref_data);
            printf("MemberRef %d: %s, Class: %s .%s\n", counter, memberref_data->Name, memberref_data->ClassNamespace ? memberref_data->ClassNamespace : "<Unknown>",
                                                                                        memberref_data->ClassName ? memberref_data->ClassName : "<Unknown>");

            // Decode the method signature blob for this MemberRef and store the decoded method signature in the MemberRef info structure for this MemberRef entry
            //method_parser_decode_signature(memberref_data->Signature, memberref_data->SignatureLength, &ecma_file->table_context, &memberref_table_info->table_info, &memberref_data->member_ref_info);
        }

        memberref_row_info = memberref_row_info->p_next;
        counter++;
    }
}
