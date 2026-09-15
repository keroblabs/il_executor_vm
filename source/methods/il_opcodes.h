#ifndef IL_OPCODES_H
#define IL_OPCODES_H

/**
 * ECMA-335 CIL (Common Intermediate Language) Opcodes
 * Reference: ECMA-335 Partition III - CIL Instruction Set
 *
 * Single-byte opcodes: 0x00 - 0xFE
 * Two-byte opcodes: 0xFE 0xXX (represented as 0xFE00 + XX)
 */
enum // il_opcode_e
{
    // =========================================================================
    // Single-byte opcodes (0x00 - 0xFF)
    // =========================================================================

    IL_OP_NOP             = 0x00,  // No operation
    IL_OP_BREAK           = 0x01,  // Breakpoint instruction

    // Load argument onto stack
    IL_OP_LDARG_0         = 0x02,  // Load argument 0 onto stack
    IL_OP_LDARG_1         = 0x03,  // Load argument 1 onto stack
    IL_OP_LDARG_2         = 0x04,  // Load argument 2 onto stack
    IL_OP_LDARG_3         = 0x05,  // Load argument 3 onto stack

    // Load local variable onto stack
    IL_OP_LDLOC_0         = 0x06,  // Load local variable 0 onto stack
    IL_OP_LDLOC_1         = 0x07,  // Load local variable 1 onto stack
    IL_OP_LDLOC_2         = 0x08,  // Load local variable 2 onto stack
    IL_OP_LDLOC_3         = 0x09,  // Load local variable 3 onto stack

    // Store to local variable
    IL_OP_STLOC_0         = 0x0A,  // Pop value from stack to local variable 0
    IL_OP_STLOC_1         = 0x0B,  // Pop value from stack to local variable 1
    IL_OP_STLOC_2         = 0x0C,  // Pop value from stack to local variable 2
    IL_OP_STLOC_3         = 0x0D,  // Pop value from stack to local variable 3

    // Load/store argument (short form)
    IL_OP_LDARG_S         = 0x0E,  // Load argument (short form)
    IL_OP_LDARGA_S        = 0x0F,  // Load argument address (short form)
    IL_OP_STARG_S         = 0x10,  // Store to argument (short form)

    // Load/store local (short form)
    IL_OP_LDLOC_S         = 0x11,  // Load local variable (short form)
    IL_OP_LDLOCA_S        = 0x12,  // Load local variable address (short form)
    IL_OP_STLOC_S         = 0x13,  // Store to local variable (short form)

    // Null and constant loading
    IL_OP_LDNULL          = 0x14,  // Push null reference
    IL_OP_LDC_I4_M1       = 0x15,  // Push -1 as int32
    IL_OP_LDC_I4_0        = 0x16,  // Push 0 as int32
    IL_OP_LDC_I4_1        = 0x17,  // Push 1 as int32
    IL_OP_LDC_I4_2        = 0x18,  // Push 2 as int32
    IL_OP_LDC_I4_3        = 0x19,  // Push 3 as int32
    IL_OP_LDC_I4_4        = 0x1A,  // Push 4 as int32
    IL_OP_LDC_I4_5        = 0x1B,  // Push 5 as int32
    IL_OP_LDC_I4_6        = 0x1C,  // Push 6 as int32
    IL_OP_LDC_I4_7        = 0x1D,  // Push 7 as int32
    IL_OP_LDC_I4_8        = 0x1E,  // Push 8 as int32
    IL_OP_LDC_I4_S        = 0x1F,  // Push int8 as int32 (short form)
    IL_OP_LDC_I4          = 0x20,  // Push int32
    IL_OP_LDC_I8          = 0x21,  // Push int64
    IL_OP_LDC_R4          = 0x22,  // Push float32
    IL_OP_LDC_R8          = 0x23,  // Push float64

    // 0x24 is unused

    // Duplication and pop
    IL_OP_DUP             = 0x25,  // Duplicate top stack value
    IL_OP_POP             = 0x26,  // Pop top stack value and discard it

    // Control flow
    IL_OP_JMP             = 0x27,  // Jump to method
    IL_OP_CALL            = 0x28,  // Call method
    IL_OP_CALLI           = 0x29,  // Call method indirect
    IL_OP_RET             = 0x2A,  // Return from method

    // Branch instructions (short form)
    IL_OP_BR_S            = 0x2B,  // Branch unconditional (short form)
    IL_OP_BRFALSE_S       = 0x2C,  // Branch if false/null/zero (short form)
    IL_OP_BRTRUE_S        = 0x2D,  // Branch if true/non-null/non-zero (short form)
    IL_OP_BEQ_S           = 0x2E,  // Branch if equal (short form)
    IL_OP_BGE_S           = 0x2F,  // Branch if greater or equal (short form)
    IL_OP_BGT_S           = 0x30,  // Branch if greater (short form)
    IL_OP_BLE_S           = 0x31,  // Branch if less or equal (short form)
    IL_OP_BLT_S           = 0x32,  // Branch if less (short form)
    IL_OP_BNE_UN_S        = 0x33,  // Branch if not equal (unsigned/unordered) (short form)
    IL_OP_BGE_UN_S        = 0x34,  // Branch if >= (unsigned/unordered) (short form)
    IL_OP_BGT_UN_S        = 0x35,  // Branch if > (unsigned/unordered) (short form)
    IL_OP_BLE_UN_S        = 0x36,  // Branch if <= (unsigned/unordered) (short form)
    IL_OP_BLT_UN_S        = 0x37,  // Branch if < (unsigned/unordered) (short form)

    // Branch instructions (long form)
    IL_OP_BR              = 0x38,  // Branch unconditional
    IL_OP_BRFALSE         = 0x39,  // Branch if false/null/zero
    IL_OP_BRTRUE          = 0x3A,  // Branch if true/non-null/non-zero
    IL_OP_BEQ             = 0x3B,  // Branch if equal
    IL_OP_BGE             = 0x3C,  // Branch if greater or equal
    IL_OP_BGT             = 0x3D,  // Branch if greater
    IL_OP_BLE             = 0x3E,  // Branch if less or equal
    IL_OP_BLT             = 0x3F,  // Branch if less
    IL_OP_BNE_UN          = 0x40,  // Branch if not equal (unsigned/unordered)
    IL_OP_BGE_UN          = 0x41,  // Branch if >= (unsigned/unordered)
    IL_OP_BGT_UN          = 0x42,  // Branch if > (unsigned/unordered)
    IL_OP_BLE_UN          = 0x43,  // Branch if <= (unsigned/unordered)
    IL_OP_BLT_UN          = 0x44,  // Branch if < (unsigned/unordered)

    // Switch
    IL_OP_SWITCH          = 0x45,  // Jump table

    // Indirect load, pop the address from stack and load the value at that address onto stack
    IL_OP_LDIND_I1        = 0x46,  // Load int8 indirect
    IL_OP_LDIND_U1        = 0x47,  // Load unsigned int8 indirect
    IL_OP_LDIND_I2        = 0x48,  // Load int16 indirect
    IL_OP_LDIND_U2        = 0x49,  // Load unsigned int16 indirect
    IL_OP_LDIND_I4        = 0x4A,  // Load int32 indirect
    IL_OP_LDIND_U4        = 0x4B,  // Load unsigned int32 indirect
    IL_OP_LDIND_I8        = 0x4C,  // Load int64 indirect
    IL_OP_LDIND_I         = 0x4D,  // Load native int indirect
    IL_OP_LDIND_R4        = 0x4E,  // Load float32 indirect
    IL_OP_LDIND_R8        = 0x4F,  // Load float64 indirect
    IL_OP_LDIND_REF       = 0x50,  // Load object reference indirect

    // Indirect store, pop the address and value from stack and store the value at the address
    IL_OP_STIND_REF       = 0x51,  // Store object reference indirect
    IL_OP_STIND_I1        = 0x52,  // Store int8 indirect
    IL_OP_STIND_I2        = 0x53,  // Store int16 indirect
    IL_OP_STIND_I4        = 0x54,  // Store int32 indirect
    IL_OP_STIND_I8        = 0x55,  // Store int64 indirect
    IL_OP_STIND_R4        = 0x56,  // Store float32 indirect
    IL_OP_STIND_R8        = 0x57,  // Store float64 indirect

    // Arithmetic operations
    IL_OP_ADD             = 0x58,  // Add
    IL_OP_SUB             = 0x59,  // Subtract
    IL_OP_MUL             = 0x5A,  // Multiply
    IL_OP_DIV             = 0x5B,  // Divide
    IL_OP_DIV_UN          = 0x5C,  // Divide unsigned
    IL_OP_REM             = 0x5D,  // Remainder
    IL_OP_REM_UN          = 0x5E,  // Remainder unsigned

    // Bitwise operations
    IL_OP_AND             = 0x5F,  // Bitwise AND
    IL_OP_OR              = 0x60,  // Bitwise OR
    IL_OP_XOR             = 0x61,  // Bitwise XOR
    IL_OP_SHL             = 0x62,  // Shift left
    IL_OP_SHR             = 0x63,  // Shift right
    IL_OP_SHR_UN          = 0x64,  // Shift right unsigned
    IL_OP_NEG             = 0x65,  // Negate
    IL_OP_NOT             = 0x66,  // Bitwise NOT

    // Conversion operations
    IL_OP_CONV_I1         = 0x67,  // Convert to int8
    IL_OP_CONV_I2         = 0x68,  // Convert to int16
    IL_OP_CONV_I4         = 0x69,  // Convert to int32
    IL_OP_CONV_I8         = 0x6A,  // Convert to int64
    IL_OP_CONV_R4         = 0x6B,  // Convert to float32
    IL_OP_CONV_R8         = 0x6C,  // Convert to float64
    IL_OP_CONV_U4         = 0x6D,  // Convert to unsigned int32
    IL_OP_CONV_U8         = 0x6E,  // Convert to unsigned int64

    // Object/method operations
    IL_OP_CALLVIRT        = 0x6F,  // Call virtual method
    IL_OP_CPOBJ           = 0x70,  // Copy value type
    IL_OP_LDOBJ           = 0x71,  // Load value type
    IL_OP_LDSTR           = 0x72,  // Load string literal
    IL_OP_NEWOBJ          = 0x73,  // Create new object
    IL_OP_CASTCLASS       = 0x74,  // Cast to class
    IL_OP_ISINST          = 0x75,  // Test if instance of class
    IL_OP_CONV_R_UN       = 0x76,  // Convert unsigned to float

    // 0x77 - 0x78 unused

    IL_OP_UNBOX           = 0x79,  // Unbox value type
    IL_OP_THROW           = 0x7A,  // Throw exception
    IL_OP_LDFLD           = 0x7B,  // Load field
    IL_OP_LDFLDA          = 0x7C,  // Load field address
    IL_OP_STFLD           = 0x7D,  // Store to field
    IL_OP_LDSFLD          = 0x7E,  // Load static field
    IL_OP_LDSFLDA         = 0x7F,  // Load static field address
    IL_OP_STSFLD          = 0x80,  // Store to static field
    IL_OP_STOBJ           = 0x81,  // Store value type
    IL_OP_CONV_OVF_I1_UN  = 0x82,  // Convert unsigned to int8 with overflow check
    IL_OP_CONV_OVF_I2_UN  = 0x83,  // Convert unsigned to int16 with overflow check
    IL_OP_CONV_OVF_I4_UN  = 0x84,  // Convert unsigned to int32 with overflow check
    IL_OP_CONV_OVF_I8_UN  = 0x85,  // Convert unsigned to int64 with overflow check
    IL_OP_CONV_OVF_U1_UN  = 0x86,  // Convert unsigned to uint8 with overflow check
    IL_OP_CONV_OVF_U2_UN  = 0x87,  // Convert unsigned to uint16 with overflow check
    IL_OP_CONV_OVF_U4_UN  = 0x88,  // Convert unsigned to uint32 with overflow check
    IL_OP_CONV_OVF_U8_UN  = 0x89,  // Convert unsigned to uint64 with overflow check
    IL_OP_CONV_OVF_I_UN   = 0x8A,  // Convert unsigned to native int with overflow check
    IL_OP_CONV_OVF_U_UN   = 0x8B,  // Convert unsigned to native uint with overflow check
    IL_OP_BOX             = 0x8C,  // Box value type
    IL_OP_NEWARR          = 0x8D,  // Create zero-based one-dimensional array
    IL_OP_LDLEN           = 0x8E,  // Load array length
    IL_OP_LDELEMA         = 0x8F,  // Load array element address
    IL_OP_LDELEM_I1       = 0x90,  // Load int8 array element
    IL_OP_LDELEM_U1       = 0x91,  // Load unsigned int8 array element
    IL_OP_LDELEM_I2       = 0x92,  // Load int16 array element
    IL_OP_LDELEM_U2       = 0x93,  // Load unsigned int16 array element
    IL_OP_LDELEM_I4       = 0x94,  // Load int32 array element
    IL_OP_LDELEM_U4       = 0x95,  // Load unsigned int32 array element
    IL_OP_LDELEM_I8       = 0x96,  // Load int64 array element
    IL_OP_LDELEM_I        = 0x97,  // Load native int array element
    IL_OP_LDELEM_R4       = 0x98,  // Load float32 array element
    IL_OP_LDELEM_R8       = 0x99,  // Load float64 array element
    IL_OP_LDELEM_REF      = 0x9A,  // Load object reference array element
    IL_OP_STELEM_I        = 0x9B,  // Store native int array element
    IL_OP_STELEM_I1       = 0x9C,  // Store int8 array element
    IL_OP_STELEM_I2       = 0x9D,  // Store int16 array element
    IL_OP_STELEM_I4       = 0x9E,  // Store int32 array element
    IL_OP_STELEM_I8       = 0x9F,  // Store int64 array element
    IL_OP_STELEM_R4       = 0xA0,  // Store float32 array element
    IL_OP_STELEM_R8       = 0xA1,  // Store float64 array element
    IL_OP_STELEM_REF      = 0xA2,  // Store object reference array element
    IL_OP_LDELEM          = 0xA3,  // Load array element (generic)
    IL_OP_STELEM          = 0xA4,  // Store array element (generic)
    IL_OP_UNBOX_ANY       = 0xA5,  // Unbox to any type

    // 0xA6 - 0xB2 unused

    // Conversion with overflow check
    IL_OP_CONV_OVF_I1     = 0xB3,  // Convert to int8 with overflow check
    IL_OP_CONV_OVF_U1     = 0xB4,  // Convert to uint8 with overflow check
    IL_OP_CONV_OVF_I2     = 0xB5,  // Convert to int16 with overflow check
    IL_OP_CONV_OVF_U2     = 0xB6,  // Convert to uint16 with overflow check
    IL_OP_CONV_OVF_I4     = 0xB7,  // Convert to int32 with overflow check
    IL_OP_CONV_OVF_U4     = 0xB8,  // Convert to uint32 with overflow check
    IL_OP_CONV_OVF_I8     = 0xB9,  // Convert to int64 with overflow check
    IL_OP_CONV_OVF_U8     = 0xBA,  // Convert to uint64 with overflow check

    // 0xBB - 0xC1 unused

    IL_OP_REFANYVAL       = 0xC2,  // Load address from typed reference
    IL_OP_CKFINITE        = 0xC3,  // Check for finite float

    // 0xC4 - 0xC5 unused

    IL_OP_MKREFANY        = 0xC6,  // Create typed reference

    // 0xC7 - 0xCF unused

    IL_OP_LDTOKEN         = 0xD0,  // Load metadata token
    IL_OP_CONV_U2         = 0xD1,  // Convert to uint16
    IL_OP_CONV_U1         = 0xD2,  // Convert to uint8
    IL_OP_CONV_I          = 0xD3,  // Convert to native int
    IL_OP_CONV_OVF_I      = 0xD4,  // Convert to native int with overflow check
    IL_OP_CONV_OVF_U      = 0xD5,  // Convert to native uint with overflow check
    IL_OP_ADD_OVF         = 0xD6,  // Add with overflow check
    IL_OP_ADD_OVF_UN      = 0xD7,  // Add unsigned with overflow check
    IL_OP_MUL_OVF         = 0xD8,  // Multiply with overflow check
    IL_OP_MUL_OVF_UN      = 0xD9,  // Multiply unsigned with overflow check
    IL_OP_SUB_OVF         = 0xDA,  // Subtract with overflow check
    IL_OP_SUB_OVF_UN      = 0xDB,  // Subtract unsigned with overflow check
    IL_OP_ENDFINALLY      = 0xDC,  // End finally/fault clause
    IL_OP_LEAVE           = 0xDD,  // Exit protected region
    IL_OP_LEAVE_S         = 0xDE,  // Exit protected region (short form)
    IL_OP_STIND_I         = 0xDF,  // Store native int indirect
    IL_OP_CONV_U          = 0xE0,  // Convert to native uint

    // 0xE1 - 0xFD unused

    // Two-byte opcode prefix
    IL_OP_PREFIX1         = 0xFE,  // Two-byte opcode prefix
    IL_OP_PREFIXREF       = 0xFF,  // Reserved
};

enum
{
    // =========================================================================
    // Two-byte opcodes (0xFE 0xXX) - represented as 0x00 + XX
    // =========================================================================

    IL_OP_FE_ARGLIST         = 0x00,  // Get argument list handle
    IL_OP_FE_CEQ             = 0x01,  // Compare equal
    IL_OP_FE_CGT             = 0x02,  // Compare greater than
    IL_OP_FE_CGT_UN          = 0x03,  // Compare greater than (unsigned/unordered)
    IL_OP_FE_CLT             = 0x04,  // Compare less than
    IL_OP_FE_CLT_UN          = 0x05,  // Compare less than (unsigned/unordered)
    IL_OP_FE_LDFTN           = 0x06,  // Load method pointer
    IL_OP_FE_LDVIRTFTN       = 0x07,  // Load virtual method pointer

    // 0x08 unused

    IL_OP_FE_LDARG           = 0x09,  // Load argument
    IL_OP_FE_LDARGA          = 0x0A,  // Load argument address
    IL_OP_FE_STARG           = 0x0B,  // Store to argument
    IL_OP_FE_LDLOC           = 0x0C,  // Load local variable
    IL_OP_FE_LDLOCA          = 0x0D,  // Load local variable address
    IL_OP_FE_STLOC           = 0x0E,  // Store to local variable
    IL_OP_FE_LOCALLOC        = 0x0F,  // Allocate local memory

    // 0x10 unused

    IL_OP_FE_ENDFILTER       = 0x11,  // End filter clause
    IL_OP_FE_UNALIGNED_      = 0x12,  // Unaligned pointer prefix
    IL_OP_FE_VOLATILE_       = 0x13,  // Volatile pointer prefix
    IL_OP_FE_TAIL_           = 0x14,  // Tail call prefix
    IL_OP_FE_INITOBJ         = 0x15,  // Initialize value type
    IL_OP_FE_CONSTRAINED_    = 0x16,  // Constrained call prefix
    IL_OP_FE_CPBLK           = 0x17,  // Copy memory block
    IL_OP_FE_INITBLK         = 0x18,  // Initialize memory block
    IL_OP_FE_NO_             = 0x19,  // No prefix (fault check suppression)
    IL_OP_FE_RETHROW         = 0x1A,  // Rethrow exception

    // 0x1B unused

    IL_OP_FE_SIZEOF          = 0x1C,  // Get size of value type
    IL_OP_FE_REFANYTYPE      = 0x1D,  // Get type from typed reference
    IL_OP_FE_READONLY_       = 0x1E,  // Readonly prefix (for ldelema)

    // 0x1F - 0xFF unused/reserved
};

// Alias names for compatibility
#define IL_OP_BRNULL_S      IL_OP_BRFALSE_S   // Branch if null (alias)
#define IL_OP_BRZERO_S      IL_OP_BRFALSE_S   // Branch if zero (alias)
#define IL_OP_BRINST_S      IL_OP_BRTRUE_S    // Branch if instance (alias)
#define IL_OP_BRNULL        IL_OP_BRFALSE     // Branch if null (alias)
#define IL_OP_BRZERO        IL_OP_BRFALSE     // Branch if zero (alias)
#define IL_OP_BRINST        IL_OP_BRTRUE      // Branch if instance (alias)
#define IL_OP_ENDFAULT      IL_OP_ENDFINALLY  // End fault clause (alias)
#define IL_OP_LDELEM_U8     IL_OP_LDELEM_I8   // Load uint64 array element (same as I8)

/**
 * Check if opcode is a two-byte opcode (has 0xFE prefix)
 */
#define IL_OP_IS_TWOBYTE(op) ((op) >= 0xFE00)

/**
 * Get the second byte of a two-byte opcode
 */
#define IL_OP_TWOBYTE_SECOND(op) ((op) & 0xFF)

#endif // IL_OPCODES_H
