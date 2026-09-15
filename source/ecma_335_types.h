#ifndef __ECMA_335_TYPES_H__
#define __ECMA_335_TYPES_H__

#include "types.h"
#include "metadata_table_types.h"
#include "methods_type.h"

////////////////////////////////////////////////////////////////////////////////////////

#define METADATA_SIGNATURE                  0x424A5342 // "BSJB"

////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////

enum
{
    // Tables
    METADATA_STREAM_TABLES,       // "#~"
    METADATA_STREAM_TABLES_UNOPT, // "#-" (unoptimized tables)

    // Heaps
    METADATA_STREAM_STRINGS,      // "#Strings"
    METADATA_STREAM_BLOB,         // "#Blob"
    METADATA_STREAM_GUID,         // "#GUID"
    METADATA_STREAM_US,           // "#US"

    // Rare / optional
    METADATA_STREAM_PDB,          // "#Pdb" / "#PDB" (portable PDB)
    METADATA_STREAM_ENC,          // "#ENC"
    METADATA_STREAM_ENC_LOG,       // "#ENCLog"

    METADATA_STREAM_MAX,          // maximum valid stream kind value
    METADATA_STREAM_UNKNOWN = 255,
};

typedef struct metadata_table_root_t_
{
    uint32_t  Reserved;
    uint8_t   MajorVersion;
    uint8_t   MinorVersion;
    uint8_t   HeapSizes;
    uint8_t   Reserved2;
    uint64_t  ValidMask;
    uint64_t  SortedMask;
    uint32_t  RowCount[]; // variable size array of row counts for each metadata table, indexed by the table index enum value.

} metadata_table_root_t;

typedef struct metadata_root_header_t_
{
    uint32_t Signature;         // "BSJB"
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint32_t Reserved;
    uint32_t VersionStringLength;
    // followed by Version string (variable)
    char stringVersion[];          // null-terminated string

} metadata_root_header_t;

typedef struct metadata_stream_header_t_
{
    uint32_t Offset;            // offset from the start of the metadata root
    uint32_t Size;              // size of the stream
    char Name[];                // null-terminated string

} metadata_stream_header_t;

typedef struct metadata_stream_t_
{
    uint16_t flags;
    uint16_t number_of_streams;
    uint8_t stream_headers_buffer[]; // variable number of stream headers

} metadata_stream_t;

typedef struct table_context_t_
{
    // pointer to the tables stream header, which contains the number of rows for each metadata table. We can use this information
    // to quickly access the metadata tables and their rows. The tables stream header is located at the offset specified in the
    // stream header for the #~ stream, which is stored in the ecma_file structure.
    // The tables stream header is of size sizeof(metadata_table_root_t) + (number_of_tables * sizeof(uint32_t)).
    metadata_table_root_t * tables_stream_header;

    // array of row counts for each metadata table, indexed by the table index enum value. This allows us to quickly access
    // the row count for a given metadata table without having to search through the tables stream header each time.
    uint32_t row_count_table[METADATA_TABLE_MAX];

    // linked list of table info for each metadata table, indexed by the table index enum value. This allows us to quickly access
    // the table info for a given metadata table without having to search through the tables each time.
    table_info_t * table_list;

    // buffer containing the raw row data for all tables, this is used by the decoder functions to read the row data for each table.
    // The row data for each table is located at the offset specified in the tables stream header for the #~ stream, which is stored
    // in the ecma_file structure. The row data for each table is of size (number_of_rows * size_of_row), where the size of each row
    // is determined by the table ID and the heap sizes specified in the tables stream header.
    uint8_t * row_buffer;

    // pointer to the row info for the entry point method, this is set during the decoding of the MethodDef table and used to identify
    // the entry point method when decoding the method signatures and bodies. The entry point method is identified by the entry point
    // token, which is obtained from the PE header and stored in the ecma_file structure. During the decoding of the MethodDef table,
    // if a MethodDef entry has a token that matches the entry point token, we set the entry_point_method pointer to the row info for
    // that MethodDef entry. This allows us to easily identify the entry point method when decoding the method signatures and bodies,
    // as we can compare the current MethodDef entry being decoded with the entry_point_method pointer to determine if it is the entry
    // point method.
    row_info_t * entry_point_method;

    // pointer back to the parent ecma_file structure, this allows us to access the metadata buffer and stream headers from the table
    // context when decoding rows. The table context is passed to each decoder function for each table, so having a pointer back to the
    // parent ecma_file structure allows us to access the metadata buffer and stream headers from within the decoder functions without
    // having to pass additional parameters for the metadata buffer and stream headers.
    void * ecma_data;

    // pointer to the PE data structure, this is used to access the file handle for reading the metadata buffer from the PE file.
    // The PE data structure is defined in the pe_parser module and contains information about the PE file, such as the file handle,
    // the section headers, and the data directories. The PE data structure is initialized in the pe_parser function and passed to
    // the ecma_335_parser function for reading the metadata buffer from the PE file. The PE data structure is also stored in the
    // table context for access from the decoder functions when needed.
    void * pe_data;

} table_context_t;

typedef struct ecma_file_t_
{
    // pointer to the PE data structure, this is used to access the file handle for reading the metadata buffer from the PE file.
    // The PE data structure is defined in the pe_parser module and contains information about the PE file, such as the file handle,
    // the section headers, and the data directories. The PE data structure is initialized in the pe_parser function and passed to
    // the ecma_335_parser function for reading the metadata buffer from the PE file.
    void * pe_data;

    uint8_t * metadata_buffer;
    uint32_t metadata_size;
    uint32_t entry_point_token;

    metadata_root_header_t * metadata_root;
    metadata_stream_t * metadata_stream;

    // pointers to the stream headers for each stream kind, indexed by the stream kind enum value.
    // This allows us to quickly access the stream header for a given stream kind without having to
    // search through the stream headers each time.
    metadata_stream_header_t * stream_headers[METADATA_STREAM_MAX];

    // context for decoding metadata tables, this is used by the decoder functions to access the
    // tables stream header and the row buffer for decoding the rows of each table. The table
    // context is initialized in the ecma_335_parse_tables function and passed to each decoder function
    // for each table.
    table_context_t table_context;

    // linked list of all types defined in the metadata, this is populated during the decoding of the TypeDef
    // table and used for resolving type references during decoding of other tables and for displaying type information.
    // The type_info_t structure contains the type name, namespace, flags, fields, and methods for each type defined in
    // the metadata. The type list is indexed by the TypeDef table index, which is the row index of the TypeDef table for
    // each type. This allows us to quickly access the type information for a given TypeDef row index without having to
    // search through the type list each time.
    type_info_t * type_list;

} ecma_file_t;

#endif // __ECMA_335_TYPES_H__
