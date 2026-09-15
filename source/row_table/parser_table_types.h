#ifndef __TABLE_TYPES_H__
#define __TABLE_TYPES_H__

#include "types.h"

// Table 0x00 - Module
typedef struct module_table_decoded_t_
{
    uint16_t Generation;
    uint32_t NameIndex;       // Index into #Strings heap
    uint32_t MvidIndex;       // Index into #GUID heap
    uint32_t EncIdIndex;      // Index into #GUID heap
    uint32_t EncBaseIdIndex;  // Index into #GUID heap
} module_table_decoded_t;

// Table 0x01 - TypeRef
typedef struct typeref_table_decoded_t_
{
    const char * TypeName;          // Resolved type name from #Strings heap
    const char * TypeNamespace;     // Resolved type namespace from #Strings heap

    uint32_t ResolutionScopeIndex;  // Coded index: ResolutionScope
} typeref_table_decoded_t;

// Table 0x02 - TypeDef
typedef struct typedef_table_decoded_t_
{
    const char * TypeName;          // Resolved type name from #Strings heap
    const char * TypeNamespace;     // Resolved type namespace from #Strings heap

    uint32_t Flags;            // TypeAttributes flags
    uint32_t ExtendsIndex;     // Coded index: TypeDefOrRef
    uint32_t FieldListIndex;   // Index into Field table
    uint32_t MethodListIndex;  // Index into MethodDef table
    uint32_t FieldListCount;   // Count of fields in the Field table
    uint32_t MethodListCount;  // Count of methods in the MethodDef table
} typedef_table_decoded_t;

// Table 0x04 - Field
typedef struct field_table_decoded_t_
{
    const char * Name;        // Resolved field name from #Strings heap
    const uint8_t * Signature; // Resolved field signature from #Blob heap
    uint32_t SignatureLength;  // Length of the field signature blob

    uint16_t Flags;            // FieldAttributes flags

    // Parsed field information based on the field signature, which will be populated
    // later after we decode the Field table rows and read the field signature blob to
    // determine the field type and attributes for parsing the field information correctly.
    field_info_t field_info;
} field_table_decoded_t;

// Table 0x06 - MethodDef
typedef struct methoddef_table_decoded_t_
{
    const char * Name;            // Resolved method name from #Strings heap
    const uint8_t * Signature;    // Resolved method signature from #Blob heap
    uint32_t SignatureLength;     // Length of the method signature blob

    uint32_t RVA;             // Relative Virtual Address
    uint16_t ImplFlags;       // MethodImplAttributes
    uint16_t Flags;           // MethodAttributes

    uint32_t ParamListIndex;  // Index into Param table
    uint32_t ParamCount;      // Count of parameters in the Param table for this method

    // This will be populated later with the method body and parameter information after we
    // decode the MethodDef table and Param table rows and read the IL code for the method
    // based on the RVA and method signature to determine the parameter types and sizes for
    // parsing the method body and parameters correctly.
    method_info_t method_info;
} methoddef_table_decoded_t;

// Table 0x08 - Param
typedef struct param_table_decoded_t_
{
    const char * Name;        // Resolved parameter name from #Strings heap

    uint16_t Flags;           // ParamAttributes flags
    uint16_t Sequence;        // Sequence number
} param_table_decoded_t;

// Table 0x09 - InterfaceImpl
typedef struct interfaceimpl_table_decoded_t_
{
    uint32_t ClassIndex;      // Index into TypeDef table
    uint32_t InterfaceIndex;  // Coded index: TypeDefOrRef
} interfaceimpl_table_decoded_t;

// Table 0x0A - MemberRef
typedef struct memberref_table_decoded_t_
{
    const char * Name;        // Resolved member name from #Strings heap
    const uint8_t * Signature;   // Resolved member signature from #Blob heap
    uint32_t SignatureLength;  // Length of the member signature blob

    const char * ClassName;       // Resolved parent type name from #Strings heap (for TypeRef parent) or module name (for ModuleRef parent)
    const char * ClassNamespace;  // Resolved parent type namespace from #Strings heap (for TypeRef parent) or NULL (for ModuleRef parent)

    // Parsed member reference information based on the member signature,
    // which will be populated later after we decode the MemberRef table
    // rows and read the member signature blob to determine the member
    // type and attributes for parsing the member reference information correctly.
    decoded_member_ref_sig_t * decoded_member_ref_sig;

    uint32_t ClassIndex;      // Coded index: MemberRefParent
} memberref_table_decoded_t;

// Table 0x0B - Constant
typedef struct constant_table_decoded_t_
{
    uint8_t  Type;            // Element type (ELEMENT_TYPE_*)
    uint8_t  PaddingZero;     // Padding, always 0
    uint32_t ParentIndex;     // Coded index: HasConstant
    uint32_t ValueIndex;      // Index into #Blob heap
} constant_table_decoded_t;

// Table 0x0C - CustomAttribute
typedef struct customattribute_table_decoded_t_
{
    uint32_t ParentIndex;     // Coded index: HasCustomAttribute
    uint32_t TypeIndex;       // Coded index: CustomAttributeType
    uint32_t ValueIndex;      // Index into #Blob heap
} customattribute_table_decoded_t;

// Table 0x0D - FieldMarshal
typedef struct fieldmarshal_table_decoded_t_
{
    uint32_t ParentIndex;           // Coded index: HasFieldMarshal (Field=0 or Param=1)
    const uint8_t * NativeType;     // Resolved marshaling descriptor (MarshalSpec) from #Blob heap
    uint32_t NativeTypeLength;      // Length of the NativeType blob
} fieldmarshal_table_decoded_t;

// Table 0x0E - DeclSecurity
typedef struct declsecurity_table_decoded_t_
{
    uint16_t Action;                // SecurityAction enum value
    uint32_t ParentIndex;           // Coded index: HasDeclSecurity (TypeDef=0, MethodDef=1, Assembly=2)
    const uint8_t * PermissionSet;  // Resolved permission set descriptor from #Blob heap
    uint32_t PermissionSetLength;   // Length of the PermissionSet blob
} declsecurity_table_decoded_t;

// Table 0x0F - ClassLayout
typedef struct classlayout_table_decoded_t_
{
    uint16_t PackingSize;     // Packing size (1, 2, 4, 8, 16, 32, 64, or 128)
    uint32_t ClassSize;       // Class size in bytes
    uint32_t ParentIndex;     // Index into TypeDef table
} classlayout_table_decoded_t;

// Table 0x10 - FieldLayout
typedef struct fieldlayout_table_decoded_t_
{
    uint32_t Offset;          // Offset in bytes
    uint32_t FieldIndex;      // Index into Field table
} fieldlayout_table_decoded_t;

// Table 0x11 - StandAloneSig
typedef struct standalonesig_table_decoded_t_
{
    const uint8_t * Signature;   // Resolved signature from #Blob heap
    uint32_t SignatureLength;  // Length of the signature blob
} standalonesig_table_decoded_t;

// Table 0x12 - EventMap
typedef struct eventmap_table_decoded_t_
{
    uint32_t ParentIndex;        // Index into TypeDef table
    uint32_t EventListIndex;     // Index into Event table (run-length encoded)
} eventmap_table_decoded_t;

// Table 0x14 - Event
typedef struct event_table_decoded_t_
{
    uint16_t EventFlags;         // EventAttributes flags
    const char * Name;           // Resolved event name from #Strings heap
    uint32_t EventTypeIndex;     // Coded index: TypeDefOrRef (delegate type of the event)
} event_table_decoded_t;

// Table 0x15 - PropertyMap
typedef struct propertymap_table_decoded_t_
{
    uint32_t ParentIndex;        // Index into TypeDef table
    uint32_t PropertyListIndex;  // Index into Property table
} propertymap_table_decoded_t;

// Table 0x17 - Property
typedef struct property_table_decoded_t_
{
    uint16_t Flags;           // PropertyAttributes flags
    const char * Name;        // Resolved property name from #Strings heap
    const uint8_t * Type;     // Resolved property type from #Blob heap (PropertySig)
    uint32_t TypeLength;      // Length of the property type blob
} property_table_decoded_t;

// Table 0x18 - MethodSemantics
typedef struct methodsemantics_table_decoded_t_
{
    uint16_t Semantics;       // MethodSemanticsAttributes
    uint32_t MethodIndex;     // Index into MethodDef table
    uint32_t AssociationIndex; // Coded index: HasSemantics
} methodsemantics_table_decoded_t;

// Table 0x19 - MethodImpl
typedef struct methodimpl_table_decoded_t_
{
    uint32_t ClassIndex;           // Index into TypeDef table
    uint32_t MethodBodyIndex;      // Coded index: MethodDefOrRef
    uint32_t MethodDeclarationIndex; // Coded index: MethodDefOrRef
} methodimpl_table_decoded_t;

// Table 0x1A - ModuleRef
typedef struct moduleref_table_decoded_t_
{
    const char * Name;        // Resolved module name from #Strings heap
} moduleref_table_decoded_t;

// Table 0x1B - TypeSpec
typedef struct typespec_table_decoded_t_
{
    uint32_t SignatureIndex;  // Index into #Blob heap
} typespec_table_decoded_t;

// Table 0x1C - ImplMap
typedef struct implmap_table_decoded_t_
{
    uint16_t MappingFlags;              // PInvokeAttributes flags
    uint32_t MemberForwardedIndex;      // Coded index: MemberForwarded (Field=0 or MethodDef=1)
    const char * ImportName;            // Resolved native function name from #Strings heap
    uint32_t ImportScopeIndex;          // Index into ModuleRef table (the native library)
} implmap_table_decoded_t;

// Table 0x1D - FieldRVA
typedef struct fieldrva_table_decoded_t_
{
    uint32_t RVA;         // Relative Virtual Address
    uint32_t FieldIndex;  // Index into Field table
    const uint8_t * Data; // Resolved pointer into the mapped PE image (populated by the finish hook)
} fieldrva_table_decoded_t;

// Table 0x20 - Assembly
typedef struct assembly_table_decoded_t_
{
    uint32_t HashAlgId;       // Assembly hash algorithm
    uint16_t MajorVersion;    // Major version number
    uint16_t MinorVersion;    // Minor version number
    uint16_t BuildNumber;     // Build number
    uint16_t RevisionNumber;  // Revision number
    uint32_t Flags;           // AssemblyFlags
    uint32_t PublicKeyIndex;  // Index into #Blob heap
    uint32_t NameIndex;       // Index into #Strings heap
    uint32_t CultureIndex;    // Index into #Strings heap
} assembly_table_decoded_t;

// Table 0x23 - AssemblyRef
typedef struct assemblyref_table_decoded_t_
{
    uint16_t MajorVersion;         // Major version number
    uint16_t MinorVersion;         // Minor version number
    uint16_t BuildNumber;          // Build number
    uint16_t RevisionNumber;       // Revision number
    uint32_t Flags;                // AssemblyFlags
    uint32_t PublicKeyOrTokenIndex; // Index into #Blob heap
    uint32_t NameIndex;            // Index into #Strings heap
    uint32_t CultureIndex;         // Index into #Strings heap
    uint32_t HashValueIndex;       // Index into #Blob heap
} assemblyref_table_decoded_t;

// Table 0x26 - File
typedef struct file_table_decoded_t_
{
    uint32_t Flags;           // FileAttributes flags
    uint32_t NameIndex;       // Index into #Strings heap
    uint32_t HashValueIndex;  // Index into #Blob heap
} file_table_decoded_t;

// Table 0x27 - ExportedType
typedef struct exportedtype_table_decoded_t_
{
    uint32_t Flags;                 // TypeAttributes flags
    uint32_t TypeDefId;             // 4-byte hint into the exporting assembly's TypeDef table (not a coded index)
    const char * TypeName;          // Resolved type name from #Strings heap
    const char * TypeNamespace;     // Resolved type namespace from #Strings heap
    uint32_t ImplementationIndex;   // Coded index: Implementation (File=0, AssemblyRef=1, ExportedType=2)
} exportedtype_table_decoded_t;

// Table 0x28 - ManifestResource
typedef struct manifestresource_table_decoded_t_
{
    uint32_t Offset;                // Byte offset within the containing resource stream / file
    uint32_t Flags;                 // ManifestResourceAttributes
    const char * Name;              // Resolved resource name from #Strings heap
    uint32_t ImplementationIndex;   // Coded index: Implementation (File=0, AssemblyRef=1, 0=embedded in current module)
} manifestresource_table_decoded_t;

// Table 0x29 - NestedClass
typedef struct nestedclass_table_decoded_t_
{
    uint32_t NestedClassIndex;    // Index into TypeDef table
    uint32_t EnclosingClassIndex; // Index into TypeDef table
} nestedclass_table_decoded_t;

// Table 0x2A - GenericParam
typedef struct genericparam_table_decoded_t_
{
    uint16_t Number;          // Generic parameter index
    uint16_t Flags;           // GenericParamAttributes
    uint32_t OwnerIndex;      // Coded index: TypeOrMethodDef
    uint32_t NameIndex;       // Index into #Strings heap
} genericparam_table_decoded_t;

// Table 0x2B - MethodSpec
typedef struct methodspec_table_decoded_t_
{
    uint32_t MethodIndex;         // Coded index: MethodDefOrRef
    uint32_t InstantiationIndex;  // Index into #Blob heap
} methodspec_table_decoded_t;

// Table 0x2C - GenericParamConstraint
typedef struct genericparamconstraint_table_decoded_t_
{
    uint32_t OwnerIndex;          // Index into GenericParam table (the type parameter being constrained)
    uint32_t ConstraintIndex;     // Coded index: TypeDefOrRef (the interface or base class required)
} genericparamconstraint_table_decoded_t;

#endif // __TABLE_TYPES_H__
