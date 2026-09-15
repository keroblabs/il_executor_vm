#ifndef __PARSER_TABLE_H__
#define __PARSER_TABLE_H__

#include "types.h"
#include "parser_table_types.h"
#include "metadata_table.h"

// Function declarations for table decoders
uint32_t metadata_table_decode_module(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_typeref(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);

void metadata_table_init_typedef(table_info_t * table_info);
void metadata_table_update_typedef(table_info_t * table_info, table_context_t * table_context);
void metadata_table_finish_typedef(table_info_t * table_info, table_context_t * table_context);
uint32_t metadata_table_decode_typedef(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);

uint32_t metadata_table_decode_field(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);

void metadata_table_init_methoddef(table_info_t * table_info);
void metadata_table_update_methoddef(table_info_t * table_info, table_context_t * table_context);
void metadata_table_finish_methoddef(table_info_t * table_info, table_context_t * table_context);
uint32_t metadata_table_decode_methoddef(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);

uint32_t metadata_table_decode_param(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_interfaceimpl(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_memberref(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_constant(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_customattribute(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_classlayout(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_fieldlayout(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_standalonesig(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_eventmap(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_event(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_propertymap(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_property(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_methodsemantics(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_methodimpl(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_moduleref(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_typespec(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_implmap(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_fieldrva(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
void metadata_table_finish_fieldrva(table_info_t * table_info, table_context_t * table_context);
uint32_t metadata_table_decode_fieldmarshal(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_declsecurity(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_assembly(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_assemblyref(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_file(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_exportedtype(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_manifestresource(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_nestedclass(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_genericparam(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_methodspec(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
uint32_t metadata_table_decode_genericparamconstraint(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);

typedef uint32_t (*decode_function_t)(table_context_t * table_context, table_info_t * table_info, void * row_buffer, uint32_t buffer_index);
typedef void (*init_function_t)(table_info_t * table_info);
typedef void (*update_function_t)(table_info_t * table_info, table_context_t * table_context);
typedef void (*finish_function_t)(table_info_t * table_info, table_context_t * table_context);

typedef struct table_map_t_
{
    uint32_t table_id;
    init_function_t init_function;
    update_function_t update_function;
    finish_function_t finish_function;
    decode_function_t decode_function;

} table_map_t;

static const table_map_t table_map[] =
{
    { METADATA_TABLE_Module, NULL, NULL, NULL, metadata_table_decode_module },
    { METADATA_TABLE_TypeRef, NULL, NULL, NULL, metadata_table_decode_typeref },
    { METADATA_TABLE_TypeDef, metadata_table_init_typedef, metadata_table_update_typedef, metadata_table_finish_typedef, metadata_table_decode_typedef },
    { METADATA_TABLE_Field, NULL, NULL, NULL, metadata_table_decode_field },
    { METADATA_TABLE_MethodDef, metadata_table_init_methoddef, metadata_table_update_methoddef, metadata_table_finish_methoddef, metadata_table_decode_methoddef },
    { METADATA_TABLE_Param, NULL, NULL, NULL, metadata_table_decode_param },
    { METADATA_TABLE_InterfaceImpl, NULL, NULL, NULL, metadata_table_decode_interfaceimpl },
    { METADATA_TABLE_MemberRef, NULL, NULL, NULL, metadata_table_decode_memberref },
    { METADATA_TABLE_Constant, NULL, NULL, NULL, metadata_table_decode_constant },
    { METADATA_TABLE_CustomAttribute,NULL, NULL, NULL,  metadata_table_decode_customattribute },
    { METADATA_TABLE_FieldMarshal, NULL, NULL, NULL, metadata_table_decode_fieldmarshal },
    { METADATA_TABLE_DeclSecurity, NULL, NULL, NULL, metadata_table_decode_declsecurity },
    { METADATA_TABLE_ClassLayout, NULL, NULL, NULL, metadata_table_decode_classlayout },
    { METADATA_TABLE_FieldLayout, NULL, NULL, NULL, metadata_table_decode_fieldlayout },
    { METADATA_TABLE_StandAloneSig, NULL, NULL, NULL, metadata_table_decode_standalonesig },
    { METADATA_TABLE_EventMap, NULL, NULL, NULL, metadata_table_decode_eventmap },
    { METADATA_TABLE_Event, NULL, NULL, NULL, metadata_table_decode_event },
    { METADATA_TABLE_PropertyMap, NULL, NULL, NULL, metadata_table_decode_propertymap },
    { METADATA_TABLE_Property, NULL, NULL, NULL, metadata_table_decode_property },
    { METADATA_TABLE_MethodSemantics, NULL, NULL, NULL, metadata_table_decode_methodsemantics },
    { METADATA_TABLE_MethodImpl, NULL, NULL, NULL, metadata_table_decode_methodimpl },
    { METADATA_TABLE_ModuleRef, NULL, NULL, NULL, metadata_table_decode_moduleref },
    { METADATA_TABLE_TypeSpec, NULL, NULL, NULL, metadata_table_decode_typespec },
    { METADATA_TABLE_ImplMap, NULL, NULL, NULL, metadata_table_decode_implmap },
    { METADATA_TABLE_FieldRVA, NULL, NULL, metadata_table_finish_fieldrva, metadata_table_decode_fieldrva },

    { METADATA_TABLE_Assembly, NULL, NULL, NULL, metadata_table_decode_assembly },
    { METADATA_TABLE_AssemblyRef, NULL, NULL, NULL, metadata_table_decode_assemblyref },
    { METADATA_TABLE_File, NULL, NULL, NULL, metadata_table_decode_file },
    { METADATA_TABLE_ExportedType, NULL, NULL, NULL, metadata_table_decode_exportedtype },
    { METADATA_TABLE_ManifestResource, NULL, NULL, NULL, metadata_table_decode_manifestresource },
    { METADATA_TABLE_NestedClass, NULL, NULL, NULL, metadata_table_decode_nestedclass },
    { METADATA_TABLE_GenericParam, NULL, NULL, NULL, metadata_table_decode_genericparam },
    { METADATA_TABLE_MethodSpec, NULL, NULL, NULL, metadata_table_decode_methodspec },
    { METADATA_TABLE_GenericParamConstraint, NULL, NULL, NULL, metadata_table_decode_genericparamconstraint }
};

#endif // __PARSER_TABLE_H__
