#include "method_parser.h"
#include "signature_parser.h"

//------------------------------------------------------------------------------
// Public: Decode a method signature (wrapper around signature_decode_method)
//------------------------------------------------------------------------------
bool_t method_parser_decode_signature(const uint8_t * signature, uint32_t signature_len,
                                      table_context_t * table_context, table_info_t * table_info,
                                      method_info_t * method_info)
{
    if (signature == NULL || signature_len == 0 || method_info == NULL)
    {
        return FALSE;
    }

    return signature_decode_method(signature, signature_len, &method_info->decoded_signature);
}

//------------------------------------------------------------------------------
// Public: Decode local variables from local_var_sig_tok
//------------------------------------------------------------------------------
bool_t method_parser_decode_locals(table_context_t * table_context, table_info_t * table_info,
                                   method_info_t * method_info)
{
    if (table_context == NULL || method_info == NULL)
    {
        return FALSE;
    }

    // Check if there's a local variable signature token
    if (method_info->local_var_sig_tok == 0)
    {
        method_info->local_count = 0;
        method_info->local_types = NULL;
        return TRUE;  // No locals, not an error
    }

    // Parse the metadata token: upper byte is table ID, lower 3 bytes are row index
    uint32_t token = method_info->local_var_sig_tok;
    uint8_t table_id = (token >> 24) & 0xFF;
    uint32_t row_index = token & 0x00FFFFFF;

    // Verify this is a StandAloneSig token (table 0x11)
    if (table_id != METADATA_TABLE_StandAloneSig)
    {
        printf("Error: local_var_sig_tok is not a StandAloneSig token (table 0x%02X)\n", table_id);
        return FALSE;
    }

    // Get the StandAloneSig row
    standalonesig_table_decoded_t * standalone_sig =
        (standalonesig_table_decoded_t *)metadata_table_get_row_data_by_index(
            METADATA_TABLE_StandAloneSig, table_context, row_index);

    if (standalone_sig == NULL)
    {
        printf("Error: Failed to get StandAloneSig row %u\n", row_index);
        return FALSE;
    }

    // Decode the LOCAL_SIG blob
    decoded_locals_t decoded_locals;
    bool_t ret =  signature_decode_locals(standalone_sig->Signature, standalone_sig->SignatureLength, &decoded_locals);

    if (ret)
    {
        method_info->local_count = decoded_locals.local_count;
        method_info->local_types = decoded_locals.local_types;
    }
    else
    {
        method_info->local_count = 0;
        method_info->local_types = NULL;
    }

    return ret;
}

//------------------------------------------------------------------------------
// Wrapper: Decode a single type (delegates to signature_parser)
//------------------------------------------------------------------------------
bool_t method_parser_decode_type(sig_decode_ctx_t * ctx, decoded_type_t ** out_type)
{
    return signature_decode_type(ctx, out_type);
}

//------------------------------------------------------------------------------
// Wrapper: Free functions (delegate to signature_parser)
//------------------------------------------------------------------------------
void method_parser_free_type(decoded_type_t * type)
{
    signature_free_type(type);
}

void method_parser_free_method_sig(decoded_method_sig_t * sig)
{
    signature_free_method_sig(sig);
}

void method_parser_free_locals(decoded_locals_t * locals)
{
    signature_free_locals(locals);
}

//------------------------------------------------------------------------------
// Wrapper: Get element type name
//------------------------------------------------------------------------------
const char * method_parser_element_type_name(uint8_t element_type)
{
    return signature_element_type_name(element_type);
}

//------------------------------------------------------------------------------
// Method-body section flags (ECMA-335 II.25.4.5)
//------------------------------------------------------------------------------
#define COR_ILMETHOD_SECT_EH_TABLE        0x01  // Section contains exception clauses
#define COR_ILMETHOD_SECT_OPTIL_TABLE     0x02  // Reserved / obsolete (skip contents)
#define COR_ILMETHOD_SECT_FAT_FORMAT      0x40  // Fat (uint32) clause fields vs small (uint8/16)
#define COR_ILMETHOD_SECT_MORE_SECTS      0x80  // Another section follows this one

//------------------------------------------------------------------------------
// Method-body flags (ECMA-335 II.25.4.4)
//------------------------------------------------------------------------------
#define COR_ILMETHOD_MORE_SECTS           0x08  // Fat header: EH / other sections follow IL

//------------------------------------------------------------------------------
// Parse the EH sections that follow the IL code of a fat method body.
// Sections start 4-byte aligned relative to the method body, may be small or
// fat, and may chain via COR_ILMETHOD_SECT_MORE_SECTS.
//------------------------------------------------------------------------------
static bool_t parse_eh_sections(FILE * fp, uint32_t method_body_offset,
                                uint32_t sections_start_offset,
                                method_info_t * method_info)
{
    method_info->eh_clause_count = 0;
    method_info->eh_clauses = NULL;

    // Align the first section relative to the method body start.
    uint32_t rel = sections_start_offset - method_body_offset;
    rel = (rel + 3) & ~3u;
    uint32_t pos = method_body_offset + rel;

    uint8_t more_sects;
    do
    {
        uint8_t kind;
        if (fseek(fp, pos, SEEK_SET) != 0) return FALSE;
        if (fread(&kind, 1, 1, fp) != 1)   return FALSE;

        bool_t is_fat      = (kind & COR_ILMETHOD_SECT_FAT_FORMAT) != 0;
        bool_t is_eh_table = (kind & COR_ILMETHOD_SECT_EH_TABLE)   != 0;
        more_sects         = (kind & COR_ILMETHOD_SECT_MORE_SECTS);

        // Read the section's total byte count (header + clauses).
        uint32_t section_size;
        if (is_fat)
        {
            // Fat section header: kind(1) + data_size(3 LE bytes) = 4 bytes total.
            uint8_t sz_bytes[3];
            if (fread(sz_bytes, 1, 3, fp) != 3) return FALSE;
            section_size = (uint32_t)sz_bytes[0]
                         | ((uint32_t)sz_bytes[1] << 8)
                         | ((uint32_t)sz_bytes[2] << 16);
        }
        else
        {
            // Small section header: kind(1) + data_size(1) + padding(2) = 4 bytes total.
            uint8_t data_size;
            uint16_t padding;
            if (fread(&data_size, 1, 1, fp) != 1) return FALSE;
            if (fread(&padding,   2, 1, fp) != 1) return FALSE;
            (void)padding;
            section_size = data_size;
        }

        // Only EH tables produce clauses. OptIL and unknown-kind sections are skipped
        // but still consume their declared size so the section chain stays aligned.
        if (is_eh_table && section_size > 4)
        {
            uint32_t clause_bytes = section_size - 4;
            uint32_t clause_size  = is_fat ? 24u : 12u;
            uint32_t num_clauses  = clause_bytes / clause_size;

            if (num_clauses > 0)
            {
                uint32_t new_count = method_info->eh_clause_count + num_clauses;
                eh_clause_t * grown = (eh_clause_t *)realloc(method_info->eh_clauses,
                                                             new_count * sizeof(eh_clause_t));
                if (grown == NULL) return FALSE;
                method_info->eh_clauses = grown;

                for (uint32_t i = 0; i < num_clauses; i++)
                {
                    eh_clause_t * c = &method_info->eh_clauses[method_info->eh_clause_count + i];

                    if (is_fat)
                    {
                        // Fat clause: 6 * uint32
                        uint32_t buf[6];
                        if (fread(buf, sizeof(uint32_t), 6, fp) != 6) return FALSE;
                        c->flags          = buf[0];
                        c->try_offset     = buf[1];
                        c->try_length     = buf[2];
                        c->handler_offset = buf[3];
                        c->handler_length = buf[4];
                        c->class_token    = buf[5];
                    }
                    else
                    {
                        // Small clause (12 bytes):
                        //   Flags(u16) TryOffset(u16) TryLength(u8)
                        //   HandlerOffset(u16) HandlerLength(u8) ClassTokenOrFilter(u32)
                        uint16_t flags16, try_off16, handler_off16;
                        uint8_t  try_len8, handler_len8;
                        uint32_t class_token;
                        if (fread(&flags16,      2, 1, fp) != 1) return FALSE;
                        if (fread(&try_off16,    2, 1, fp) != 1) return FALSE;
                        if (fread(&try_len8,     1, 1, fp) != 1) return FALSE;
                        if (fread(&handler_off16,2, 1, fp) != 1) return FALSE;
                        if (fread(&handler_len8, 1, 1, fp) != 1) return FALSE;
                        if (fread(&class_token,  4, 1, fp) != 1) return FALSE;

                        c->flags          = flags16;
                        c->try_offset     = try_off16;
                        c->try_length     = try_len8;
                        c->handler_offset = handler_off16;
                        c->handler_length = handler_len8;
                        c->class_token    = class_token;
                    }
                }
                method_info->eh_clause_count = new_count;
            }
        }

        // Advance to the next section, 4-byte aligned relative to the method body.
        pos += section_size;
        rel = pos - method_body_offset;
        rel = (rel + 3) & ~3u;
        pos = method_body_offset + rel;

    } while (more_sects);

    return TRUE;
}

//------------------------------------------------------------------------------
// Parse method body info from RVA (IL code, etc.)
//------------------------------------------------------------------------------
void method_parser_get_info(table_context_t * table_context, methoddef_table_decoded_t * methoddef_data, method_info_t * method_info)
{
    ecma_file_t * ecma_file = (ecma_file_t *)table_context->ecma_data;
    pe_data_t * pe_data = ecma_file->pe_data;

    uint32_t file_offset = pe_rva_to_file_offset(methoddef_data->RVA, table_context->pe_data);
    FILE * fp = pe_get_file_handle(pe_data);

    // Read the first byte to determine the method body format (tiny or fat)
    uint8_t header_byte;
    fseek(fp, file_offset, SEEK_SET);
    fread(&header_byte, 1, 1, fp);

    if ((header_byte & 0x03) == 0x02)
    {
        // Tiny format (1-byte header)
        method_info->flags              = 0x02;                 // Tiny format flag
        method_info->max_stack          = 8;                    // Fixed at 8 for tiny
        method_info->code_size          = header_byte >> 2;     // Code size in upper 6 bits
        method_info->local_var_sig_tok  = 0;                    // No locals in tiny format
        method_info->eh_clause_count    = 0;                    // No EH in tiny format
        method_info->eh_clauses         = NULL;
        method_info->il_code            = (uint8_t *)malloc(method_info->code_size);
        if (method_info->il_code == NULL)
        {
            printf("Failed to allocate memory for IL code\n");
            method_info->code_size = 0;
            return;
        }

        fread(method_info->il_code, 1, method_info->code_size, fp);
    }
    else if ((header_byte & 0x03) == 0x03)
    {
        // Fat format (12-byte header)
        fseek(fp, file_offset, SEEK_SET);
        uint16_t flags_and_size = 0;
        fread(&flags_and_size, sizeof(flags_and_size), 1, fp);  // Read the full 2-byte header

        method_info->flags      = flags_and_size & 0x0FFF;      // Lower 12 bits are flags
        uint8_t header_size     = (flags_and_size >> 12) * 4;   // Header size in bytes

        method_info->max_stack = 0; // *(uint16_t*)(ptr + 2);
        fread(&method_info->max_stack, sizeof(method_info->max_stack), 1, fp);
        fread(&method_info->code_size, sizeof(method_info->code_size), 1, fp);
        fread(&method_info->local_var_sig_tok, sizeof(method_info->local_var_sig_tok), 1, fp);

        method_info->eh_clause_count = 0;
        method_info->eh_clauses      = NULL;

        method_info->il_code = (uint8_t *)malloc(method_info->code_size);
        if (method_info->il_code == NULL)
        {
            printf("Failed to allocate memory for IL code\n");
            method_info->code_size = 0;
            return;
        }
        fseek(fp, file_offset + header_size, SEEK_SET); // Move to the start of IL code after the header
        fread(method_info->il_code, 1, method_info->code_size, fp);

        // Parse trailing EH sections when the header advertises them.
        if (method_info->flags & COR_ILMETHOD_MORE_SECTS)
        {
            uint32_t sections_start = file_offset + header_size + method_info->code_size;
            if (!parse_eh_sections(fp, file_offset, sections_start, method_info))
            {
                printf("Failed to parse EH sections for method at RVA 0x%X\n", methoddef_data->RVA);
                // Non-fatal: leave eh_clause_count/eh_clauses as populated so far.
            }
        }
    }
    else
    {
        printf("Unknown method body format for RVA 0x%X\n", methoddef_data->RVA);
        method_info->code_size = 0;
        return;
    }
}
