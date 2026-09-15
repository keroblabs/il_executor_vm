#ifndef __METADATA_TABLE_TYPES_H__
#define __METADATA_TABLE_TYPES_H__

#include "types.h"

enum
{
    METADATA_TABLE_Module                 = 0x00,
    METADATA_TABLE_TypeRef                = 0x01,
    METADATA_TABLE_TypeDef                = 0x02,
    METADATA_TABLE_FieldPtr               = 0x03,
    METADATA_TABLE_Field                  = 0x04,
    METADATA_TABLE_MethodPtr              = 0x05,
    METADATA_TABLE_MethodDef              = 0x06,
    METADATA_TABLE_ParamPtr               = 0x07,
    METADATA_TABLE_Param                  = 0x08,
    METADATA_TABLE_InterfaceImpl          = 0x09,
    METADATA_TABLE_MemberRef              = 0x0A,
    METADATA_TABLE_Constant               = 0x0B,
    METADATA_TABLE_CustomAttribute        = 0x0C,
    METADATA_TABLE_FieldMarshal           = 0x0D,
    METADATA_TABLE_DeclSecurity           = 0x0E,
    METADATA_TABLE_ClassLayout            = 0x0F,
    METADATA_TABLE_FieldLayout            = 0x10,
    METADATA_TABLE_StandAloneSig          = 0x11,
    METADATA_TABLE_EventMap               = 0x12,
    METADATA_TABLE_EventPtr               = 0x13,
    METADATA_TABLE_Event                  = 0x14,
    METADATA_TABLE_PropertyMap            = 0x15,
    METADATA_TABLE_PropertyPtr            = 0x16,
    METADATA_TABLE_Property               = 0x17,
    METADATA_TABLE_MethodSemantics        = 0x18,
    METADATA_TABLE_MethodImpl             = 0x19,
    METADATA_TABLE_ModuleRef              = 0x1A,
    METADATA_TABLE_TypeSpec               = 0x1B,
    METADATA_TABLE_ImplMap                = 0x1C,
    METADATA_TABLE_FieldRVA               = 0x1D,

    // ----- Edit-and-Continue (rare but defined) -----
    METADATA_TABLE_ENCLog                 = 0x1E,
    METADATA_TABLE_ENCMap                 = 0x1F,

    // ----- Assembly metadata -----
    METADATA_TABLE_Assembly               = 0x20,
    METADATA_TABLE_AssemblyProcessor      = 0x21,
    METADATA_TABLE_AssemblyOS             = 0x22,
    METADATA_TABLE_AssemblyRef            = 0x23,
    METADATA_TABLE_AssemblyRefProcessor   = 0x24,
    METADATA_TABLE_AssemblyRefOS          = 0x25,
    METADATA_TABLE_File                   = 0x26,
    METADATA_TABLE_ExportedType           = 0x27,
    METADATA_TABLE_ManifestResource       = 0x28,
    METADATA_TABLE_NestedClass            = 0x29,
    METADATA_TABLE_GenericParam           = 0x2A,
    METADATA_TABLE_MethodSpec             = 0x2B,
    METADATA_TABLE_GenericParamConstraint = 0x2C,

    // ----- Reserved / unused -----
    METADATA_TABLE_MAX                    = 0x40  // 64 tables
};

enum
{
    METADATA_FIELD_U16,
    METADATA_FIELD_U32,
    METADATA_FIELD_STRING,
    METADATA_FIELD_BLOB,
    METADATA_FIELD_GUID,
    METADATA_FIELD_TABLE,
    METADATA_FIELD_CODED
};

typedef struct row_info_t_
{
    struct row_info_t_ * p_next;

    // point to the table info entry in which the row was allocated,
    // we can get the table ID and other information from the parent table info entry
    void * p_parent;

    // the ID of the table to which this row belongs, this is used to determine the structure of the
    // row data and how to parse it. This is the index of the table in the metadata tables stream header,
    // which is stored in the ecma_file structure. This allows us to quickly access the table info for a
    // given table ID without having to search through the tables each time.
    uint32_t table_id;

    // point to the heap data in which the row was allocated, this will be cast to the
    // appropriate row structure based on the table ID and used to access the row data.
    void * p_row;

} row_info_t;

typedef struct table_info_t_
{
    struct table_info_t_ * p_next;

    // table ID, which is the index of the table in the metadata tables stream header.
    // This allows us to quickly access the table info for a given table ID without
    // having to search through the tables each time.
    uint32_t id;

    // number of rows in the table
    uint32_t count;

    // linked list of row info for each row in the table, indexed by the row index.
    // This allows us to quickly access the row info for a given row index without
    // having to search through the rows each time.
    row_info_t * rows;

    // pointer to the last row info entry in the linked list of row info for the table,
    // this allows us to quickly add new rows to the end of the linked list without having
    // to traverse the entire list each time.
    row_info_t * last_row;

} table_info_t;

#endif // __METADATA_TABLE_TYPES_H__
