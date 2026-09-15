#ifndef ELEMENT_TYPES_H
#define ELEMENT_TYPES_H

/**
 * ECMA-335 ELEMENT_TYPE values
 * Used in type signatures (LOCAL_SIG, method signatures, field signatures, etc.)
 * Reference: ECMA-335 II.23.1.16
 */
enum // element_type_e
{
    ELEMENT_TYPE_END         = 0x00,  // End of list marker
    ELEMENT_TYPE_VOID        = 0x01,  // void
    ELEMENT_TYPE_BOOLEAN     = 0x02,  // bool
    ELEMENT_TYPE_CHAR        = 0x03,  // char (2-byte Unicode)
    ELEMENT_TYPE_I1          = 0x04,  // sbyte
    ELEMENT_TYPE_U1          = 0x05,  // byte
    ELEMENT_TYPE_I2          = 0x06,  // short
    ELEMENT_TYPE_U2          = 0x07,  // ushort
    ELEMENT_TYPE_I4          = 0x08,  // int
    ELEMENT_TYPE_U4          = 0x09,  // uint
    ELEMENT_TYPE_I8          = 0x0A,  // long
    ELEMENT_TYPE_U8          = 0x0B,  // ulong
    ELEMENT_TYPE_R4          = 0x0C,  // float
    ELEMENT_TYPE_R8          = 0x0D,  // double
    ELEMENT_TYPE_STRING      = 0x0E,  // System.String

    ELEMENT_TYPE_PTR         = 0x0F,  // pointer type (followed by type)
    ELEMENT_TYPE_BYREF       = 0x10,  // by-reference (followed by type)
    ELEMENT_TYPE_VALUETYPE   = 0x11,  // value type (followed by TypeDefOrRef token)
    ELEMENT_TYPE_CLASS       = 0x12,  // class type (followed by TypeDefOrRef token)
    ELEMENT_TYPE_VAR         = 0x13,  // generic type parameter (followed by index)

    ELEMENT_TYPE_ARRAY       = 0x14,  // multi-dimensional array
    ELEMENT_TYPE_GENERICINST = 0x15,  // generic instantiation
    ELEMENT_TYPE_TYPEDBYREF  = 0x16,  // System.TypedReference
    ELEMENT_TYPE_I           = 0x18,  // native int (IntPtr)
    ELEMENT_TYPE_U           = 0x19,  // native uint (UIntPtr)
    ELEMENT_TYPE_FNPTR       = 0x1B,  // function pointer
    ELEMENT_TYPE_OBJECT      = 0x1C,  // System.Object

    ELEMENT_TYPE_SZARRAY     = 0x1D,  // single-dim array (followed by element type)
    ELEMENT_TYPE_MVAR        = 0x1E,  // generic method parameter (followed by index)

    ELEMENT_TYPE_CMOD_REQD   = 0x1F,  // required modifier
    ELEMENT_TYPE_CMOD_OPT    = 0x20,  // optional modifier
    ELEMENT_TYPE_INTERNAL    = 0x21,  // internal (CLR internal use)
    ELEMENT_TYPE_MODIFIER    = 0x40,  // modifier flag (OR'd with type)
    ELEMENT_TYPE_SENTINEL    = 0x41,  // sentinel for varargs
    ELEMENT_TYPE_PINNED      = 0x45,  // pinned local variable

    // nested types (used in custom modifiers and function pointers)

    ELEMENT_TYPE_NESTED_PTR         = 0x0F00,  // pointer type (followed by type)
    ELEMENT_TYPE_NESTED_BYREF       = 0x1000,  // by-reference (followed by type)
    ELEMENT_TYPE_NESTED_VALUETYPE   = 0x1100,  // value type (followed by TypeDefOrRef token)
    ELEMENT_TYPE_NESTED_CLASS       = 0x1200,  // class type (followed by TypeDefOrRef token)
    ELEMENT_TYPE_NESTED_VAR         = 0x1300,  // generic type parameter (followed by index)

    ELEMENT_TYPE_NESTED_ARRAY       = 0x1400,  // multi-dimensional array (followed by element type and shapes)

    ELEMENT_TYPE_NESTED_SZARRAY     = 0x1D00,  // single-dim array (followed by element type)
    ELEMENT_TYPE_NESTED_MVAR        = 0x1E00,  // generic method parameter (followed by index)
};

/**
 * Signature calling conventions (first byte of signature blob)
 * Reference: ECMA-335 II.23.2.1
 */
enum // calling_convention_e
{
    CALLING_CONV_DEFAULT     = 0x00,  // Default calling convention
    CALLING_CONV_VARARG      = 0x05,  // Vararg calling convention
    CALLING_CONV_FIELD       = 0x06,  // Field signature
    CALLING_CONV_LOCAL_SIG   = 0x07,  // Local variable signature
    CALLING_CONV_PROPERTY    = 0x08,  // Property signature
    CALLING_CONV_GENERIC     = 0x10,  // Generic method (OR'd with other flags)
    CALLING_CONV_HASTHIS     = 0x20,  // Instance method (has 'this' pointer)
    CALLING_CONV_EXPLICITTHIS = 0x40, // Explicit 'this' parameter

};

#endif // ELEMENT_TYPES_H
