#include "pe_parser.h"

////////////////////////////////////////////////////////////////////////////////////////

#define PE_HEADER_E_MAGIC                   0x5A4D // 'MZ'
#define PE_HEADER_ELNEW_OFFSET              0x3C
#define PE_PE32_MAGIC                       0x10B

#define IMAGE_FILE_EXECUTABLE_IMAGE         0x0002
#define IMAGE_FILE_LARGE_ADDRESS_AWARE      0x0020

////////////////////////////////////////////////////////////////////////////////////////

enum
{
    IMAGE_DIRECTORY_ENTRY_EXPORT          = 0,
    IMAGE_DIRECTORY_ENTRY_IMPORT          = 1,
    IMAGE_DIRECTORY_ENTRY_RESOURCE        = 2,
    IMAGE_DIRECTORY_ENTRY_EXCEPTION       = 3,
    IMAGE_DIRECTORY_ENTRY_SECURITY        = 4,
    IMAGE_DIRECTORY_ENTRY_BASERELOC       = 5,
    IMAGE_DIRECTORY_ENTRY_DEBUG           = 6,
    IMAGE_DIRECTORY_ENTRY_ARCHITECTURE    = 7,
    IMAGE_DIRECTORY_ENTRY_GLOBALPTR       = 8,
    IMAGE_DIRECTORY_ENTRY_TLS             = 9,
    IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG     = 10,
    IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT    = 11,
    IMAGE_DIRECTORY_ENTRY_IAT             = 12,
    IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT    = 13,
    IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR  = 14,
    IMAGE_DIRECTORY_ENTRY_RESERVED        = 15
};

////////////////////////////////////////////////////////////////////////////////////////

// PE (Portable Executable) file parser implementation
// at e_lfanew offset, there is a PE header, which contains the signature 'PE\0\0'
// and other information about the executable file. The PE header is followed by an
// optional header and then the section headers. This parser reads the PE header and
// retrieves the virtual address, size of raw data, and pointer to raw data for the first section.
typedef struct _pe_header_t
{
    uint32_t    signature;
    uint16_t    machine;
    uint16_t    number_of_sections;
    uint32_t    time_date_stamp;

    uint32_t    pointer_to_symbol_table;
    uint32_t    number_of_symbols;

    uint16_t    size_of_optional_header;
    uint16_t    characteristics;

} pe_header_t;

// this is the common part of the optional header for both PE32 and PE32+ formats, which contains
// information about the executable file such as the magic number, linker version, and size of code.
// The actual structure of the optional header differs between PE32 and PE32+ formats, so we define
// separate structures for each format.
typedef struct _pe_optional_header_prefix_t
{
    uint16_t    magic;
    uint8_t     major_linker_version;
    uint8_t     minor_linker_version;

} pe_optional_header_prefix_t;

// Optional header structure for PE32 (not PE32+) files, which contains information about the executable
// file such as the entry point, base of code, and base of data. It is located immediately
// after the PE header and is of variable size depending on the type of PE file (PE32 or PE32+).
typedef struct _pe32_optional_header_block_t
{
    // --- Standard fields (24 bytes) ---
    //uint16_t Magic;                 // 0x10B
    //uint8_t  MajorLinkerVersion;
    //uint8_t  MinorLinkerVersion;

    uint32_t    size_of_code;
    uint32_t    size_of_initialized_data;
    uint32_t    size_of_uninitialized_data;
    uint32_t    address_of_entry_point;
    uint32_t    base_of_code;
    uint32_t    base_of_data;

    uint32_t    ImageBase;            // +0x1C (4)
    uint32_t    SectionAlignment;     // +0x20 (4)
    uint32_t    FileAlignment;        // +0x24 (4)

    uint16_t    MajorOSVersion;       // +0x28 (2)
    uint16_t    MinorOSVersion;       // +0x2A (2)
    uint16_t    MajorImageVersion;    // +0x2C (2)
    uint16_t    MinorImageVersion;    // +0x2E (2)
    uint16_t    MajorSubsystemVersion;// +0x30 (2)
    uint16_t    MinorSubsystemVersion;// +0x32 (2)

    uint32_t    Win32VersionValue;    // +0x34 (4)

    uint32_t    SizeOfImage;          // +0x38 (4)
    uint32_t    SizeOfHeaders;        // +0x3C (4)
    uint32_t    CheckSum;             // +0x40 (4)

    uint16_t    Subsystem;            // +0x44 (2)
    uint16_t    DllCharacteristics;   // +0x46 (2)

    uint32_t    SizeOfStackReserve;   // +0x48 (4)
    uint32_t    SizeOfStackCommit;    // +0x4C (4)
    uint32_t    SizeOfHeapReserve;    // +0x50 (4)
    uint32_t    SizeOfHeapCommit;     // +0x54 (4)

    uint32_t    LoaderFlags;          // +0x58 (4)
    uint32_t    NumberOfRvaAndSizes;  // +0x5C (4)

} pe32_optional_header_block_t;

// not supported at the moment, but we define the structure for PE32+ optional
// header for completeness. The main difference
typedef struct _pe32_plus_optional_header_block_t
{
    // --- Standard fields (24 bytes) ---
    //uint16_t Magic;                 // 0x20B
    //uint8_t  MajorLinkerVersion;
    //uint8_t  MinorLinkerVersion;

    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    // (no BaseOfData in PE32+)

    // --- Windows-specific fields (88 bytes) ---
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
} pe32_plus_optional_header_block_t;

typedef struct _image_directory_entry_t
{
    uint32_t VirtualAddress;  // RVA
    uint32_t Size;
} image_directory_entry_t;

typedef struct _pe32_optional_header_t
{
    pe_optional_header_prefix_t prefix;
    pe32_optional_header_block_t pe32;

    image_directory_entry_t data_directory[];

} pe32_optional_header_t;

typedef struct _image_section_header_entry_
{
    char        name[8];
    uint32_t    virtual_size;
    uint32_t    virtual_address;
    uint32_t    size_of_raw_data;
    uint32_t    pointer_to_raw_data;
    uint32_t    pointer_to_relocations;
    uint32_t    pointer_to_linenumbers;
    uint16_t    number_of_relocations;
    uint16_t    number_of_linenumbers;
    uint32_t    characteristics;
} image_section_header_entry_t;

typedef struct _image_cor20_header_t
{
    uint32_t cb;                    // Size of this header (usually 0x48)
    uint16_t MajorRuntimeVersion;
    uint16_t MinorRuntimeVersion;

    image_directory_entry_t MetaData;  // <-- VERY IMPORTANT

    uint32_t Flags;
    uint32_t EntryPointToken;       // or RVA (depends on Flags)

    image_directory_entry_t Resources;
    image_directory_entry_t StrongNameSignature;
    image_directory_entry_t CodeManagerTable;
    image_directory_entry_t VTableFixups;
    image_directory_entry_t ExportAddressTableJumps;
    image_directory_entry_t ManagedNativeHeader;
} image_cor20_header_t;

typedef struct pe_file_t_
{
    FILE * fp;
    uint32_t e_lfanew;
    pe_header_t pe_header;
    uint8_t * optional_header_buffer;
    pe32_optional_header_t * optional_header;
    image_section_header_entry_t * section_headers;
    image_cor20_header_t cli_header;

    // Full PE image loaded into memory once at parse time. Required so that
    // RVAs (FieldRVA rows, MethodDef body pointers, string/blob pool refs
    // outside the metadata streams) can be resolved to a stable pointer that
    // consumers may memcpy from without touching the file handle again.
    uint8_t * image_buffer;
    uint32_t  image_size;

} pe_file_t;

////////////////////////////////////////////////////////////////////////////////////////

static bool_t is_valid_pe_header(FILE * fp, uint32_t * e_lfanew)
{
    uint32_t e_magic;
    fseek(fp, 0L, SEEK_SET);
    fread(&e_magic, sizeof(uint16_t), 1, fp);

    if (e_magic != PE_HEADER_E_MAGIC)
    {
        return FALSE;
    }

    fseek(fp, PE_HEADER_ELNEW_OFFSET, SEEK_SET);
    fread(e_lfanew, sizeof(uint32_t), 1, fp);
    return TRUE;
}

static bool_t read_pe_header(FILE * fp, uint32_t e_lfanew, pe_header_t * header)
{
    fseek(fp, e_lfanew, SEEK_SET);
    if (fread(header, sizeof(pe_header_t), 1, fp) != 1)
    {
        return FALSE;
    }
    return TRUE;
}

FILE * pe_get_file_handle(pe_data_t * pe_data)
{
    pe_file_t * file = (pe_file_t *)pe_data;
    return file->fp;
}

uint32_t pe_rva_to_file_offset(uint32_t rva, pe_data_t * pe_data)
{
    pe_file_t * file = (pe_file_t *)pe_data;

    image_section_header_entry_t * section_headers = file->section_headers;
    uint16_t number_of_sections = file->pe_header.number_of_sections;

    for (int i = 0; i < number_of_sections; i++)
    {
        if (rva >= section_headers[i].virtual_address &&
            rva < section_headers[i].virtual_address + section_headers[i].virtual_size)
        {
            return section_headers[i].pointer_to_raw_data + (rva - section_headers[i].virtual_address);
        }
    }
    return 0; // not found
}

const uint8_t * pe_rva_to_pointer(pe_data_t * pe_data, uint32_t rva)
{
    pe_file_t * file = (pe_file_t *)pe_data;

    if (file->image_buffer == NULL)
    {
        return NULL;
    }

    uint32_t file_offset = pe_rva_to_file_offset(rva, pe_data);
    if (file_offset == 0 || file_offset >= file->image_size)
    {
        return NULL;
    }

    return file->image_buffer + file_offset;
}

pe_data_t * pe_parser(FILE * fp, uint32_t *md_offset, uint32_t *md_size, uint32_t * entry_point_token)
{
    pe_file_t * file = (pe_file_t *)malloc(sizeof(pe_file_t));
    if (!file)
    {
        printf("Failed to allocate memory for PE file structure\n");
        return NULL;
    }
    memset(file, 0, sizeof(pe_file_t));

    // Load the whole PE image into memory so that later stages can resolve
    // RVAs to stable pointers (see pe_rva_to_pointer). The FILE handle is
    // kept open and existing fseek/fread paths continue to work — the
    // handle is repositioned to the start below.
    fseek(fp, 0L, SEEK_END);
    long file_size = ftell(fp);
    if (file_size <= 0)
    {
        printf("Invalid PE file size\n");
        free(file);
        return NULL;
    }
    file->image_size = (uint32_t)file_size;
    file->image_buffer = (uint8_t *)malloc(file->image_size);
    if (!file->image_buffer)
    {
        printf("Failed to allocate memory for PE image buffer\n");
        free(file);
        return NULL;
    }
    fseek(fp, 0L, SEEK_SET);
    if (fread(file->image_buffer, 1, file->image_size, fp) != file->image_size)
    {
        printf("Failed to read PE image into memory\n");
        free(file->image_buffer);
        free(file);
        return NULL;
    }
    fseek(fp, 0L, SEEK_SET);

    // read DOS header and validate PE signature
    if (!is_valid_pe_header(fp, &file->e_lfanew))
    {
        printf("Invalid PE file\n");
        free(file->image_buffer);
        free(file);
        return NULL;
    }

    // read PE header
    if (!read_pe_header(fp, file->e_lfanew, &file->pe_header))
    {
        printf("Failed to read PE header\n");
        free(file->image_buffer);
        free(file);
        return NULL;
    }

    // read the optional header to get the size of code and other information. Note that the optional header is
    // located immediately after the PE header and is of variable size depending on the type of PE file (PE32 or PE32+).
    file->optional_header_buffer = (uint8_t *)malloc(file->pe_header.size_of_optional_header);
    if (!file->optional_header_buffer)
    {
        printf("Failed to allocate memory for PE optional header\n");
        free(file->image_buffer);
        free(file);
        return NULL;
    }

    if (fread(file->optional_header_buffer, 1, file->pe_header.size_of_optional_header, fp) != file->pe_header.size_of_optional_header)
    {
        printf("Failed to read PE optional header\n");
        free(file->optional_header_buffer);
        free(file->image_buffer);
        free(file);
        return NULL;
    }

    // at the moment, we only support PE32 format, so we read the optional header
    // as PE32 and ignore the possibility of PE32+ format. In a complete implementation,
    // we would check the magic
    if (((pe_optional_header_prefix_t *)file->optional_header_buffer)->magic != PE_PE32_MAGIC)
    {
        printf("Unsupported PE optional header magic\n");
        free(file->optional_header_buffer);
        free(file->image_buffer);
        free(file);
        return NULL;
    }

    // now we need to read the image section headers to get the virtual address, size of raw data,
    // and pointer to raw data for the first section. The section headers are located immediately
    // after the optional header and there are pe_header.number_of_sections of them.
    uint8_t * section_headers_buffer = (uint8_t *)malloc(sizeof(image_section_header_entry_t) * file->pe_header.number_of_sections);
    if (!section_headers_buffer)
    {
        printf("Failed to allocate memory for PE section headers\n");
        free(file->optional_header_buffer);
        free(file->image_buffer);
        free(file);
        return NULL;
    }

    fread(section_headers_buffer, sizeof(image_section_header_entry_t), file->pe_header.number_of_sections, fp);

    file->optional_header = (pe32_optional_header_t *)file->optional_header_buffer;
    file->section_headers = (image_section_header_entry_t *)section_headers_buffer;

    // get the CLR file offset from the data directory in the optional header. The CLR file offset is located
    // in the data directory entry at index IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR (14).
    uint32_t clr_rva = file->optional_header->data_directory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress;
    uint32_t clr_size = file->optional_header->data_directory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].Size;

    uint32_t clr_file_offset = pe_rva_to_file_offset(clr_rva, (pe_data_t *)file);
    if (clr_file_offset == 0)
    {
        printf("Failed to find CLR file offset in PE sections\n");
        free(file->optional_header_buffer);
        free(file->section_headers);
        free(file->image_buffer);
        free(file);
        return NULL;
    }

    // read the CLR header to get the virtual address, size of raw data, and pointer to raw data for the CLR section.
    // The CLR header is located at the CLR file offset and is of size clr_size.
    fseek(fp, clr_file_offset, SEEK_SET);
    if (fread(&file->cli_header, 1, sizeof(image_cor20_header_t), fp) != sizeof(image_cor20_header_t))
    {
        printf("Failed to read CLR header\n");
        free(file->optional_header_buffer);
        free(file->section_headers);
        free(file->image_buffer);
        free(file);
        return NULL;
    }

    if (file->cli_header.cb > clr_size)
    {
        printf("Invalid CLR header size\n");
        free(file->optional_header_buffer);
        free(file->section_headers);
        free(file->image_buffer);
        free(file);
        return NULL;
    }

    uint32_t md_rva  = file->cli_header.MetaData.VirtualAddress;
    uint32_t md_file_offset = pe_rva_to_file_offset(md_rva, (pe_data_t *)file);
    if (md_file_offset == 0)
    {
        printf("Failed to find metadata file offset in PE sections\n");
        free(file->optional_header_buffer);
        free(file->section_headers);
        free(file->image_buffer);
        free(file);
        return NULL;
    }

    *md_offset = md_file_offset;
    *md_size = file->cli_header.MetaData.Size;
    *entry_point_token = file->cli_header.EntryPointToken;
    file->fp = fp;

    printf("Successfully parsed PE file\n");
    printf("Metadata size: %u bytes\n", file->cli_header.MetaData.Size);

    return (pe_data_t *)file;
}
