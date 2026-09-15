#include "pe_parser.h"
#include "ecma_335_types.h"
#include "ecma_335_parser.h"

#define FILENAME_CLR        "/home/wifi/projects/cli_parser/data/test_system.dll"
//#define FILENAME_CLR        "/home/wifi/projects/cli_parser/data/vectors.dll"         // exercises ExportedType (0x27), ManifestResource (0x28), GenericParamConstraint (0x2C)
//#define FILENAME_CLR        "/home/wifi/projects/cli_parser/data/mobiletap.dll"       // exercises EventMap (0x12) and Event (0x14)
//#define FILENAME_CLR        "/home/wifi/projects/cli_parser/data/psreadline.dll"      // exercises ImplMap (0x1C), FieldMarshal (0x0D), and 102 EH clauses in 61 methods
//#define FILENAME_CLR        "/home/wifi/projects/cli_parser/data/serial_sharp.dll"    // exercises FieldRVA (0x1D) with one row at RVA 0x6C58

int main(int argc, char * argv[])
{
    FILE * fp = fopen(FILENAME_CLR, "rb");
    if (!fp)
    {
        printf("Failed to open file: %s\n", FILENAME_CLR);
        return 1;
    }

    uint32_t md_offset = 0;
    uint32_t md_size = 0;
    uint32_t entry_point_token = 0;

    pe_data_t * pe_data = pe_parser(fp, &md_offset, &md_size, &entry_point_token);
    if (pe_data == NULL)
    {
        printf("Failed to parse PE file: %s\n", FILENAME_CLR);
        fclose(fp);
        return 1;
    }

    if (md_offset == 0 || md_size == 0)
    {
        printf("Invalid metadata offset or size in PE file: %s\n", FILENAME_CLR);
        fclose(fp);
        return 1;
    }

    ecma_data_t * ecma_data = ecma_335_parser(pe_data, md_offset, md_size, entry_point_token);
    if (ecma_data == NULL)
    {
        printf("Failed to parse ECMA-335 metadata from PE file: %s\n", FILENAME_CLR);
        fclose(fp);
        return 1;
    }

    fclose(fp);

    return 0;
}
