# Navigating Decoded Method Signatures and Locals

This document explains how to interpret the `decoded_type_t` structure when navigating method signatures and local variables.

## Overview

The `decoded_type_t` structure represents a single type in a signature. Types can be:
- **Simple**: Primitives like `int`, `string`, `object`
- **Nested**: Types that contain other types (pointers, arrays, generics)
- **Referenced**: Types that point to TypeDef/TypeRef/TypeSpec tables

## The decoded_type_t Structure

```c
struct decoded_type_t_
{
    struct decoded_type_t_ * p_next;    // For linked lists (params, locals)

    uint8_t element_type;               // The ELEMENT_TYPE_* value - start here!

    // TypeDefOrRef coded index - dual purpose based on element_type:
    //   - For CLASS, VALUETYPE: the referenced type (class, struct, enum)
    //   - For CMOD_REQD, CMOD_OPT: the modifier type (e.g., IsVolatile, IsConst)
    uint32_t type_ref_coded_index;
    uint8_t type_ref_table;             // 0=TypeDef, 1=TypeRef, 2=TypeSpec
    uint32_t type_ref_row;              // Row index (1-based)

    // For VAR, MVAR
    uint32_t generic_param_index;

    // For PTR, BYREF, SZARRAY, PINNED
    decoded_type_t * nested_type;

    // For GENERICINST
    decoded_type_t * generic_base_type;
    uint32_t generic_arg_count;
    decoded_type_t ** generic_args;

    // For ARRAY (multi-dimensional)
    uint32_t array_rank;
    uint32_t * array_sizes;
    uint32_t array_sizes_count;
    uint32_t * array_lower_bounds;
    uint32_t array_lower_bounds_count;

    // For FNPTR
    decoded_method_sig_t * fn_ptr_sig;

    // For CMOD_REQD, CMOD_OPT: the underlying type being modified
    // (the modifier itself is in type_ref_* fields above)
    decoded_type_t * modified_type;
};
```

---

## Type Categories

### 1. Simple/Primitive Types (No Additional Fields)

These types are self-contained - just check `element_type`:

| element_type | C# Type | .NET Type |
|--------------|---------|-----------|
| `ELEMENT_TYPE_VOID` (0x01) | `void` | System.Void |
| `ELEMENT_TYPE_BOOLEAN` (0x02) | `bool` | System.Boolean |
| `ELEMENT_TYPE_CHAR` (0x03) | `char` | System.Char |
| `ELEMENT_TYPE_I1` (0x04) | `sbyte` | System.SByte |
| `ELEMENT_TYPE_U1` (0x05) | `byte` | System.Byte |
| `ELEMENT_TYPE_I2` (0x06) | `short` | System.Int16 |
| `ELEMENT_TYPE_U2` (0x07) | `ushort` | System.UInt16 |
| `ELEMENT_TYPE_I4` (0x08) | `int` | System.Int32 |
| `ELEMENT_TYPE_U4` (0x09) | `uint` | System.UInt32 |
| `ELEMENT_TYPE_I8` (0x0A) | `long` | System.Int64 |
| `ELEMENT_TYPE_U8` (0x0B) | `ulong` | System.UInt64 |
| `ELEMENT_TYPE_R4` (0x0C) | `float` | System.Single |
| `ELEMENT_TYPE_R8` (0x0D) | `double` | System.Double |
| `ELEMENT_TYPE_STRING` (0x0E) | `string` | System.String |
| `ELEMENT_TYPE_OBJECT` (0x1C) | `object` | System.Object |
| `ELEMENT_TYPE_I` (0x18) | `IntPtr` | System.IntPtr |
| `ELEMENT_TYPE_U` (0x19) | `UIntPtr` | System.UIntPtr |
| `ELEMENT_TYPE_TYPEDBYREF` (0x16) | `TypedReference` | System.TypedReference |

**Example:**
```c
if (type->element_type == ELEMENT_TYPE_I4)
{
    printf("int");
}
```

---

### 2. Pointer Types - Use `nested_type`

| element_type | Meaning | Navigate to |
|--------------|---------|-------------|
| `ELEMENT_TYPE_PTR` (0x0F) | Pointer (`int*`) | `nested_type` |
| `ELEMENT_TYPE_BYREF` (0x10) | By-reference (`ref int`) | `nested_type` |

**Example: `int*` (pointer to int)**
```
decoded_type_t
├── element_type = ELEMENT_TYPE_PTR (0x0F)
└── nested_type ──► decoded_type_t
                    └── element_type = ELEMENT_TYPE_I4 (int)
```

**Code:**
```c
if (type->element_type == ELEMENT_TYPE_PTR)
{
    printf("pointer to: ");
    print_type(type->nested_type);  // Recursively print the pointed-to type
}
```

**Example: `ref string` (by-reference)**
```
decoded_type_t
├── element_type = ELEMENT_TYPE_BYREF (0x10)
└── nested_type ──► decoded_type_t
                    └── element_type = ELEMENT_TYPE_STRING
```

---

### 3. Array Types - Use `nested_type`

| element_type | Meaning | Navigate to |
|--------------|---------|-------------|
| `ELEMENT_TYPE_SZARRAY` (0x1D) | Single-dim array (`int[]`) | `nested_type` |
| `ELEMENT_TYPE_ARRAY` (0x14) | Multi-dim array (`int[,]`) | `nested_type` + shape fields |

**Example: `string[]`**
```
decoded_type_t
├── element_type = ELEMENT_TYPE_SZARRAY (0x1D)
└── nested_type ──► decoded_type_t
                    └── element_type = ELEMENT_TYPE_STRING
```

**Code:**
```c
if (type->element_type == ELEMENT_TYPE_SZARRAY)
{
    print_type(type->nested_type);
    printf("[]");
}
```

**Example: `int[,]` (2D array)**
```
decoded_type_t
├── element_type = ELEMENT_TYPE_ARRAY (0x14)
├── array_rank = 2
├── array_sizes_count = 0 (unspecified sizes)
├── array_lower_bounds_count = 0
└── nested_type ──► decoded_type_t
                    └── element_type = ELEMENT_TYPE_I4
```

---

### 4. Class/ValueType - Use `type_ref_*` Fields

| element_type | Meaning |
|--------------|---------|
| `ELEMENT_TYPE_CLASS` (0x12) | Reference type (class, interface, delegate) |
| `ELEMENT_TYPE_VALUETYPE` (0x11) | Value type (struct, enum) |

Both use these fields:
- `type_ref_table`: Which table (0=TypeDef, 1=TypeRef, 2=TypeSpec)
- `type_ref_row`: Row index (1-based)

**Example: `System.Exception`**
```
decoded_type_t
├── element_type = ELEMENT_TYPE_CLASS (0x12)
├── type_ref_table = 1 (TypeRef)
└── type_ref_row = 5
```

**Code to resolve:**
```c
if (type->element_type == ELEMENT_TYPE_CLASS || type->element_type == ELEMENT_TYPE_VALUETYPE)
{
    switch (type->type_ref_table)
    {
        case 0:  // TypeDef
        {
            typedef_table_decoded_t * td = metadata_table_get_row_data_by_index(
                METADATA_TABLE_TypeDef, table_context, type->type_ref_row);
            printf("%s.%s", td->TypeNamespace, td->TypeName);
            break;
        }
        case 1:  // TypeRef
        {
            typeref_table_decoded_t * tr = metadata_table_get_row_data_by_index(
                METADATA_TABLE_TypeRef, table_context, type->type_ref_row);
            printf("%s.%s", tr->TypeNamespace, tr->TypeName);
            break;
        }
        case 2:  // TypeSpec
        {
            // TypeSpec contains another signature - rarely needed for simple cases
            break;
        }
    }
}
```

---

### 5. Generic Instantiation - Use `generic_*` Fields

`ELEMENT_TYPE_GENERICINST` (0x15) represents a closed generic type like `List<string>`.

Fields:
- `generic_base_type`: The open generic type (e.g., `List'1`)
- `generic_arg_count`: Number of type arguments
- `generic_args`: Array of type arguments

**Example: `List<string>`**
```
decoded_type_t
├── element_type = ELEMENT_TYPE_GENERICINST (0x15)
├── generic_arg_count = 1
├── generic_args[0] ──► decoded_type_t { element_type = ELEMENT_TYPE_STRING }
└── generic_base_type ──► decoded_type_t
                          ├── element_type = ELEMENT_TYPE_CLASS
                          ├── type_ref_table = 1 (TypeRef)
                          └── type_ref_row = X (→ "System.Collections.Generic.List`1")
```

**Example: `Dictionary<string, int>`**
```
decoded_type_t
├── element_type = ELEMENT_TYPE_GENERICINST (0x15)
├── generic_arg_count = 2
├── generic_args[0] ──► decoded_type_t { element_type = ELEMENT_TYPE_STRING }
├── generic_args[1] ──► decoded_type_t { element_type = ELEMENT_TYPE_I4 }
└── generic_base_type ──► decoded_type_t
                          ├── element_type = ELEMENT_TYPE_CLASS
                          └── (points to Dictionary`2)
```

**Code:**
```c
if (type->element_type == ELEMENT_TYPE_GENERICINST)
{
    // Print base type name (without `N suffix)
    print_type(type->generic_base_type);

    printf("<");
    for (uint32_t i = 0; i < type->generic_arg_count; i++)
    {
        if (i > 0) printf(", ");
        print_type(type->generic_args[i]);
    }
    printf(">");
}
```

---

### 6. Generic Parameters - Use `generic_param_index`

| element_type | Meaning |
|--------------|---------|
| `ELEMENT_TYPE_VAR` (0x13) | Type's generic parameter (`T` in `class Foo<T>`) |
| `ELEMENT_TYPE_MVAR` (0x1E) | Method's generic parameter (`T` in `void Bar<T>()`) |

**Example: `T` (first type generic param)**
```
decoded_type_t
├── element_type = ELEMENT_TYPE_VAR (0x13)
└── generic_param_index = 0
```

**Code:**
```c
if (type->element_type == ELEMENT_TYPE_VAR)
{
    printf("!%u", type->generic_param_index);  // ILDasm style: !0, !1, etc.
}
else if (type->element_type == ELEMENT_TYPE_MVAR)
{
    printf("!!%u", type->generic_param_index);  // ILDasm style: !!0, !!1, etc.
}
```

---

### 7. Pinned Types - Use `nested_type`

`ELEMENT_TYPE_PINNED` (0x45) marks a local variable as pinned (GC won't move it).

**Example: `pinned int&` (fixed int reference)**
```
decoded_type_t
├── element_type = ELEMENT_TYPE_PINNED (0x45)
└── nested_type ──► decoded_type_t
                    ├── element_type = ELEMENT_TYPE_BYREF
                    └── nested_type ──► decoded_type_t
                                        └── element_type = ELEMENT_TYPE_I4
```

---

### 8. Custom Modifiers - Use `type_ref_*` + `modified_type`

| element_type | Meaning |
|--------------|---------|
| `ELEMENT_TYPE_CMOD_REQD` (0x1F) | Required modifier (must be understood) |
| `ELEMENT_TYPE_CMOD_OPT` (0x20) | Optional modifier (can be ignored) |

Custom modifiers add metadata to a type (e.g., `volatile`, `const`). They use **two** parts of `decoded_type_t`:

- **`type_ref_*` fields**: The modifier type itself (e.g., `System.Runtime.CompilerServices.IsVolatile`)
- **`modified_type`**: The underlying type being modified (e.g., `int`)

**Note:** The `type_ref_*` fields have dual purpose:
- For `CLASS`/`VALUETYPE`: the referenced type
- For `CMOD_REQD`/`CMOD_OPT`: the modifier type

**Example: `volatile int` field**
```
decoded_type_t
├── element_type = ELEMENT_TYPE_CMOD_REQD (0x1F)
├── type_ref_table = 1 (TypeRef)
├── type_ref_row = X (→ "System.Runtime.CompilerServices.IsVolatile")
└── modified_type ──► decoded_type_t
                      └── element_type = ELEMENT_TYPE_I4 (int)
```

**Code:**
```c
if (type->element_type == ELEMENT_TYPE_CMOD_REQD || type->element_type == ELEMENT_TYPE_CMOD_OPT)
{
    // Get the modifier type name (same resolution as CLASS/VALUETYPE)
    const char * modifier_name = NULL;
    if (type->type_ref_table == 1)  // TypeRef
    {
        typeref_table_decoded_t * tr = metadata_table_get_row_data_by_index(
            METADATA_TABLE_TypeRef, ctx, type->type_ref_row);
        if (tr) modifier_name = tr->TypeName;  // e.g., "IsVolatile"
    }

    // Print modifier if it's a known one
    if (modifier_name && strcmp(modifier_name, "IsVolatile") == 0)
    {
        printf("volatile ");
    }

    // Then print the underlying type
    print_type(type->modified_type, ctx);
}
```

**Common modifiers:**
| Modifier Type | Meaning |
|---------------|---------|
| `IsVolatile` | C# `volatile` keyword |
| `IsConst` | C++ `const` (rare in C#) |
| `IsLong` | C++ `long` modifier |
| `IsSignUnspecifiedByte` | Unspecified sign |

---

### 9. Function Pointers - Use `fn_ptr_sig`

`ELEMENT_TYPE_FNPTR` (0x1B) represents a function pointer type. The `fn_ptr_sig` field contains the full method signature.

**Example: `delegate*<int, string>` (function pointer: int -> string)**
```
decoded_type_t
├── element_type = ELEMENT_TYPE_FNPTR (0x1B)
└── fn_ptr_sig ──► decoded_method_sig_t
                   ├── param_count = 1
                   ├── return_type ──► decoded_type_t { element_type = ELEMENT_TYPE_STRING }
                   └── param_types ──► decoded_type_t { element_type = ELEMENT_TYPE_I4 }
```

**Example: `delegate*<int, bool, void>` (function pointer: (int, bool) -> void)**
```
decoded_type_t
├── element_type = ELEMENT_TYPE_FNPTR (0x1B)
└── fn_ptr_sig ──► decoded_method_sig_t
                   ├── param_count = 2
                   ├── return_type ──► decoded_type_t { element_type = ELEMENT_TYPE_VOID }
                   └── param_types ──► decoded_type_t { element_type = ELEMENT_TYPE_I4 }
                                       └── p_next ──► decoded_type_t { element_type = ELEMENT_TYPE_BOOLEAN }
```

**Code:**
```c
if (type->element_type == ELEMENT_TYPE_FNPTR)
{
    decoded_method_sig_t * fn_sig = type->fn_ptr_sig;

    printf("delegate*<");

    // Print parameter types
    decoded_type_t * param = fn_sig->param_types;
    for (uint32_t i = 0; i < fn_sig->param_count; i++)
    {
        if (i > 0) printf(", ");
        print_type(param, ctx);
        param = param->p_next;
    }

    // Print return type (last in the angle brackets for C# delegate* syntax)
    if (fn_sig->param_count > 0) printf(", ");
    print_type(fn_sig->return_type, ctx);

    printf(">");
}
```

**Use cases:**
- Parameters that are function pointers: `void Foo(delegate*<int, bool> callback)`
- Return values that are function pointers: `delegate*<int, bool> GetCallback()`
- Fields that are function pointers: `private delegate*<string, void> _handler;`

---

## Navigating Method Signatures

`decoded_method_sig_t` contains:
- `calling_convention`: Raw byte (check flags)
- `has_this`: Instance method
- `is_generic`: Generic method
- `param_count`: Number of parameters
- `return_type`: `decoded_type_t *` for return type
- `param_types`: Linked list of `decoded_type_t *` for parameters

**Example: `string Foo(int x, bool y)`**
```
decoded_method_sig_t
├── has_this = true (instance method)
├── param_count = 2
├── return_type ──► decoded_type_t { element_type = ELEMENT_TYPE_STRING }
└── param_types ──► decoded_type_t { element_type = ELEMENT_TYPE_I4 }
                    └── p_next ──► decoded_type_t { element_type = ELEMENT_TYPE_BOOLEAN }
                                   └── p_next = NULL
```

**Code to iterate parameters:**
```c
decoded_method_sig_t * sig = method_info->decoded_signature;

printf("Return: ");
print_type(sig->return_type);

printf("\nParameters:\n");
decoded_type_t * param = sig->param_types;
uint32_t i = 0;
while (param != NULL)
{
    printf("  [%u] ", i);
    print_type(param);
    printf("\n");
    param = param->p_next;
    i++;
}
```

---

## Navigating Local Variables

`decoded_locals_t` contains:
- `local_count`: Number of local variables
- `local_types`: Linked list of `decoded_type_t *`

**Example: `int x; string y;`**
```
decoded_locals_t
├── local_count = 2
└── local_types ──► decoded_type_t { element_type = ELEMENT_TYPE_I4 }
                    └── p_next ──► decoded_type_t { element_type = ELEMENT_TYPE_STRING }
                                   └── p_next = NULL
```

**Code:**
```c
decoded_locals_t * locals = method_info->decoded_locals;
if (locals != NULL)
{
    printf("Locals:\n");
    decoded_type_t * local = locals->local_types;
    uint32_t i = 0;
    while (local != NULL)
    {
        printf("  [%u] ", i);
        print_type(local);
        printf("\n");
        local = local->p_next;
        i++;
    }
}
```

---

## Complete Navigation Example

Here's a complete function to print a type:

```c
void print_type(decoded_type_t * type, table_context_t * ctx)
{
    if (type == NULL)
    {
        printf("(null)");
        return;
    }

    switch (type->element_type)
    {
        // Primitives
        case ELEMENT_TYPE_VOID:     printf("void"); break;
        case ELEMENT_TYPE_BOOLEAN:  printf("bool"); break;
        case ELEMENT_TYPE_CHAR:     printf("char"); break;
        case ELEMENT_TYPE_I1:       printf("sbyte"); break;
        case ELEMENT_TYPE_U1:       printf("byte"); break;
        case ELEMENT_TYPE_I2:       printf("short"); break;
        case ELEMENT_TYPE_U2:       printf("ushort"); break;
        case ELEMENT_TYPE_I4:       printf("int"); break;
        case ELEMENT_TYPE_U4:       printf("uint"); break;
        case ELEMENT_TYPE_I8:       printf("long"); break;
        case ELEMENT_TYPE_U8:       printf("ulong"); break;
        case ELEMENT_TYPE_R4:       printf("float"); break;
        case ELEMENT_TYPE_R8:       printf("double"); break;
        case ELEMENT_TYPE_STRING:   printf("string"); break;
        case ELEMENT_TYPE_OBJECT:   printf("object"); break;
        case ELEMENT_TYPE_I:        printf("IntPtr"); break;
        case ELEMENT_TYPE_U:        printf("UIntPtr"); break;

        // Pointer/Reference
        case ELEMENT_TYPE_PTR:
            print_type(type->nested_type, ctx);
            printf("*");
            break;

        case ELEMENT_TYPE_BYREF:
            printf("ref ");
            print_type(type->nested_type, ctx);
            break;

        // Arrays
        case ELEMENT_TYPE_SZARRAY:
            print_type(type->nested_type, ctx);
            printf("[]");
            break;

        case ELEMENT_TYPE_ARRAY:
            print_type(type->nested_type, ctx);
            printf("[");
            for (uint32_t i = 1; i < type->array_rank; i++) printf(",");
            printf("]");
            break;

        // Class/Struct
        case ELEMENT_TYPE_CLASS:
        case ELEMENT_TYPE_VALUETYPE:
            if (type->type_ref_table == 0)
            {
                typedef_table_decoded_t * td = metadata_table_get_row_data_by_index(
                    METADATA_TABLE_TypeDef, ctx, type->type_ref_row);
                if (td) printf("%s.%s", td->TypeNamespace, td->TypeName);
            }
            else if (type->type_ref_table == 1)
            {
                typeref_table_decoded_t * tr = metadata_table_get_row_data_by_index(
                    METADATA_TABLE_TypeRef, ctx, type->type_ref_row);
                if (tr) printf("%s.%s", tr->TypeNamespace, tr->TypeName);
            }
            break;

        // Generics
        case ELEMENT_TYPE_GENERICINST:
            print_type(type->generic_base_type, ctx);
            printf("<");
            for (uint32_t i = 0; i < type->generic_arg_count; i++)
            {
                if (i > 0) printf(", ");
                print_type(type->generic_args[i], ctx);
            }
            printf(">");
            break;

        case ELEMENT_TYPE_VAR:
            printf("!%u", type->generic_param_index);
            break;

        case ELEMENT_TYPE_MVAR:
            printf("!!%u", type->generic_param_index);
            break;

        // Pinned
        case ELEMENT_TYPE_PINNED:
            printf("pinned ");
            print_type(type->nested_type, ctx);
            break;

        // Function pointer
        case ELEMENT_TYPE_FNPTR:
            {
                decoded_method_sig_t * fn_sig = type->fn_ptr_sig;
                printf("delegate*<");
                decoded_type_t * param = fn_sig->param_types;
                for (uint32_t i = 0; i < fn_sig->param_count; i++)
                {
                    if (i > 0) printf(", ");
                    print_type(param, ctx);
                    param = param->p_next;
                }
                if (fn_sig->param_count > 0) printf(", ");
                print_type(fn_sig->return_type, ctx);
                printf(">");
            }
            break;

        default:
            printf("UNKNOWN(0x%02X)", type->element_type);
            break;
    }
}
```

---

## Quick Reference

| element_type | What to look at |
|--------------|-----------------|
| Primitives (VOID, I4, STRING, etc.) | Nothing - type is complete |
| PTR, BYREF, SZARRAY, PINNED | `nested_type` |
| ARRAY | `nested_type` + `array_rank` + size/bounds arrays |
| CLASS, VALUETYPE | `type_ref_table` + `type_ref_row` |
| GENERICINST | `generic_base_type` + `generic_args[]` |
| VAR, MVAR | `generic_param_index` |
| CMOD_REQD, CMOD_OPT | `type_ref_*` (modifier) + `modified_type` |
| FNPTR | `fn_ptr_sig` |
