# Virtual Method Tables (vtables)

This document explains how virtual method dispatch works in the CLR, how vtables are structured, and how to build them during metadata parsing.

## Overview

Virtual method tables (vtables) enable **polymorphic method dispatch** - calling the correct method implementation based on the runtime type of an object, not its compile-time type.

## The Problem vtables Solve

Consider this inheritance hierarchy:

```csharp
class base_class {
    public virtual void M() { Console.WriteLine("base"); }
}

class derived_class1 : base_class {
    public override void M() { Console.WriteLine("derived1"); }
}

class derived_class2 : base_class {
    // Does NOT override M - inherits base implementation
}
```

When executing:
```csharp
void CallIt(base_class obj) {
    obj.M();  // IL: callvirt instance void base_class::M()
}

CallIt(new derived_class1());  // Should print "derived1"
CallIt(new derived_class2());  // Should print "base"
```

The IL instruction references `base_class::M`, but the runtime must call the correct implementation based on the actual object type.

---

## vtable Structure

### One vtable Per Class

Each class has exactly **one vtable** - an array of method pointers for all virtual methods.

```
┌─────────────────────────────┐
│      base_class vtable      │
│  [slot 0]: 0x1000  ─────────────────────────────┐
└─────────────────────────────┘                   │
                                                  ▼
┌─────────────────────────────┐           ┌──────────────────┐
│   derived_class1 vtable     │           │  base_class::M   │
│  [slot 0]: 0x2000  ─────────────┐       │  code at 0x1000  │
└─────────────────────────────┘   │       └──────────────────┘
                                  │
┌─────────────────────────────┐   │       ┌──────────────────┐
│   derived_class2 vtable     │   └──────▶│ derived_class1   │
│  [slot 0]: 0x1000  ─────────────────────│ ::M code at 0x2000│
└─────────────────────────────┘   │       └──────────────────┘
                                  │
                                  └───────▶ (same 0x1000 as base!)
```

**Key observations:**
- Three separate vtable arrays (one per class)
- `derived_class2.vtable[0]` points to the **same address** as `base_class.vtable[0]` because it doesn't override `M`
- `derived_class1.vtable[0]` points to its own implementation

### Instances Share Their Class's vtable

Instances of the **same class** share **one vtable** (they don't each get a copy):

```
┌─────────────────┐
│ instance1       │───┐
│ (derived_class1)│   │
└─────────────────┘   │
                      ├──▶  ONE vtable for derived_class1
┌─────────────────┐   │
│ instance2       │───┘
│ (derived_class1)│
└─────────────────┘
```

**NOT** like this (wasteful):
```
instance1 ──▶ vtable copy #1
instance2 ──▶ vtable copy #2  ← Duplicate data!
```

### Summary Table

| What              | Unique Per  | Contains                      |
|-------------------|-------------|-------------------------------|
| vtable            | Class       | Array of method pointers      |
| vtable pointer    | Instance    | Address of class's vtable     |
| Method code       | Implementation | The actual instructions    |

---

## Object Layout

Each object instance stores a pointer to its class's method table (which contains the vtable):

```c
// Each object instance:
struct Object {
    MethodTable* type;   // Pointer to the class's method table
    // ... instance fields follow
};

// Each class has ONE method table (created at parse/load time):
struct MethodTable {
    MethodTable* parent;          // For type checking
    uint32_t num_virtual_methods;
    void* vtable[];               // Array of function pointers
};
```

---

## How `callvirt` Executes

The `callvirt` instruction performs an O(1) vtable lookup:

```c
void execute_callvirt(uint32_t method_token) {
    // 1. Get the vtable slot from metadata (compile-time constant)
    int slot = get_vtable_slot(method_token);

    // 2. Pop the object reference from the stack
    Object* obj = stack_pop();

    // 3. Get the object's runtime type (its method table)
    MethodTable* mt = obj->type;

    // 4. Look up the method in the vtable (direct array index - O(1))
    void* method_ptr = mt->vtable[slot];

    // 5. Call the method with 'this' = obj
    call(method_ptr, obj);
}
```

**Critical insight:** The slot number is fixed at compile time. Only the vtable consulted varies based on the object's runtime type.

### `callvirt` vs `call`

| Instruction | Behavior |
|-------------|----------|
| `call`      | Calls the exact method specified (no vtable lookup) |
| `callvirt`  | Looks up the method in the runtime type's vtable |

---

## Building vtables at Parse Time

### When to Build

vtables are built when classes are loaded/parsed - **not** at `callvirt` execution time. This makes method dispatch O(1).

### Algorithm

**Step 1: Parse base_class (no parent or parent is System.Object)**

```c
// base_class has methods [1, 2, 3], where method 3 is virtual
base_class.vtable = allocate_vtable(1);  // 1 virtual method
base_class.vtable[0] = &method3;
```

**Step 2: Parse derived_class1 (extends base_class, overrides method)**

```c
// derived_class1 has methods [4, 5, 6], where method 6 overrides method 3

// Start with a copy of parent's vtable
derived_class1.vtable = copy(base_class.vtable);  // [slot0: &method3]

// Find and replace overrides
for (each method in derived_class1) {
    if (method.is_virtual_override) {
        int slot = find_matching_slot(base_class.vtable, method);
        derived_class1.vtable[slot] = &method;  // Replace!
    }
}

// Result: [slot0: &method6]
```

**Step 3: Parse derived_class2 (extends base_class, NO override)**

```c
// derived_class2 has methods [7, 8] - no override of method 3

// Start with a copy of parent's vtable
derived_class2.vtable = copy(base_class.vtable);  // [slot0: &method3]

// No overrides found, nothing to replace

// Result: [slot0: &method3]  ← Still points to base_class method!
```

### Parsing Order

Classes must be parsed in **inheritance order** (parent before child). If you encounter a class before its parent:
- Do a topological sort first, OR
- Use two-pass: collect all classes, then build vtables

---

## Override Matching

To determine if a derived method overrides a base method, three conditions must match:

1. **Method name** (string comparison)
2. **Signature length** (integer comparison)
3. **Signature bytes** (byte comparison)

### Implementation

```c
bool is_override(methoddef_table_decoded_t* base,
                 methoddef_table_decoded_t* derived)
{
    // 1. Names must match
    if (strcmp(base->Name, derived->Name) != 0)
        return false;

    // 2. Signature sizes must match
    if (base->SignatureLength != derived->SignatureLength)
        return false;

    // 3. Signature bytes must match
    if (memcmp(base->Signature, derived->Signature,
               base->SignatureLength) != 0)
        return false;

    return true;
}
```

### Why All Three?

```csharp
virtual void Foo(int x);   // Name: "Foo", Sig: (int)->void
virtual void Bar(int x);   // Name: "Bar", Sig: (int)->void  ← Same signature!
```

These are **different** virtual methods requiring **different** vtable slots.

### Caveat: Cross-Assembly Inheritance

The `memcmp` approach works when base and derived are in the **same assembly**. If the base class is in a different assembly (e.g., `System.Object` from mscorlib), signature blobs might encode types differently (TypeRef vs TypeDef tokens). In that case, semantic comparison of decoded signatures is required.

---

## Complete Example

### Metadata

```
TypeDef Table:
  [1] base_class      extends [TypeRef to System.Object]  MethodList: 1
  [2] derived_class1  extends [1] base_class              MethodList: 4
  [3] derived_class2  extends [1] base_class              MethodList: 7

MethodDef Table:
  [1] .ctor           (base_class)          - not virtual
  [2] Foo             (base_class)          - not virtual
  [3] VirtualMethod   (base_class)          - VIRTUAL (Flags & 0x0040)
  [4] .ctor           (derived_class1)      - not virtual
  [5] Bar             (derived_class1)      - not virtual
  [6] VirtualMethod   (derived_class1)      - OVERRIDE (Flags & 0x0040, matches [3])
  [7] .ctor           (derived_class2)      - not virtual
  [8] Baz             (derived_class2)      - not virtual
```

### Built vtables

```
base_class.vtable:
  [0] → MethodDef[3] (VirtualMethod)

derived_class1.vtable:
  [0] → MethodDef[6] (VirtualMethod override)

derived_class2.vtable:
  [0] → MethodDef[3] (inherited from base_class)
```

### Runtime Execution

```
// newobj derived_class1
instance1->type = &derived_class1_method_table;

// newobj derived_class2
instance2->type = &derived_class2_method_table;

// callvirt base_class::VirtualMethod on instance1
slot = 0;  // From metadata
method = instance1->type->vtable[0];  // → MethodDef[6]
call(method);  // Calls derived_class1::VirtualMethod

// callvirt base_class::VirtualMethod on instance2
slot = 0;  // Same slot
method = instance2->type->vtable[0];  // → MethodDef[3]
call(method);  // Calls base_class::VirtualMethod
```

---

## Method Flags Reference

From ECMA-335, MethodDef.Flags:

| Flag        | Value  | Meaning |
|-------------|--------|---------|
| Virtual     | 0x0040 | Method is virtual |
| Final       | 0x0020 | Method cannot be overridden (sealed) |
| NewSlot     | 0x0100 | Method always gets a new vtable slot |
| Abstract    | 0x0400 | Method has no implementation |

### Virtual Method Detection

```c
#define METHOD_FLAG_VIRTUAL  0x0040
#define METHOD_FLAG_NEWSLOT  0x0100

bool is_virtual(uint16_t flags) {
    return (flags & METHOD_FLAG_VIRTUAL) != 0;
}

bool is_new_virtual_slot(uint16_t flags) {
    return (flags & (METHOD_FLAG_VIRTUAL | METHOD_FLAG_NEWSLOT)) ==
           (METHOD_FLAG_VIRTUAL | METHOD_FLAG_NEWSLOT);
}

bool is_override(uint16_t flags) {
    // Virtual but NOT NewSlot = override
    return (flags & METHOD_FLAG_VIRTUAL) != 0 &&
           (flags & METHOD_FLAG_NEWSLOT) == 0;
}
```

---

## Data Structures for Implementation

```c
typedef struct vtable_entry {
    methoddef_table_decoded_t* method;  // Pointer to the method
    uint32_t slot;                       // Slot index
} vtable_entry_t;

typedef struct class_vtable {
    uint32_t num_slots;
    vtable_entry_t* entries;            // Array of vtable entries
} class_vtable_t;

typedef struct method_table {
    typedef_table_decoded_t* type_def;  // Back-reference to TypeDef
    struct method_table* parent;         // Parent class method table
    class_vtable_t vtable;               // The vtable
    // ... other runtime type info
} method_table_t;
```

---

## Summary

1. **vtables are per-class**, created at parse/load time
2. **Instances point to** their class's vtable (no copy)
3. **Building vtables**: copy parent's vtable, replace slots for overrides
4. **Override matching**: `strcmp(name) && length == length && memcmp(signature)`
5. **callvirt**: O(1) array lookup using object's runtime type
6. **Parse order**: parent classes must be processed before children
