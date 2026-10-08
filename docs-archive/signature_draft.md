Below is a concise but spec-accurate answer based on **ECMA‑335 (CLI specification)** and how metadata signatures are actually encoded.

---

## ✅ Short answers

1. **No** — the signature format is **not the same** for Fields and MethodDefs, though they share common encoding rules.
2. Signatures are decoded by walking a **byte-stream defined by ECMA‑335 §II.23.2**, starting with a **calling-convention byte** and then recursively decoding element types, parameters, and modifiers.

---

## #### 1. Field vs. MethodDef signature formats

Although both signatures live in the **Blob heap** and use the same low‑level encoding rules, their **top‑level structure differs**.

### **Field signature**

A field signature is very simple:

```
FIELD_SIG ::= FIELD Type
```

Binary layout:
```
+-------------------+
| 0x06              |  // FIELD calling convention
+-------------------+
| Type              |  // one element type
+-------------------+
```

Key points:
- First byte is **0x06** (`IMAGE_CEE_CS_CALLCONV_FIELD`)
- Contains **only one Type**
- No parameter count
- No return type distinction (the field *is* the type)

---

### **MethodDef signature**

A method signature is more complex:

```
METHOD_SIG ::= CallingConvention [GenParamCount] ParamCount RetType Param*
```

Binary layout (common case):
```
+-------------------+
| CallConv flags    |  // e.g. 0x00, 0x20 (HASTHIS), 0x10 (GENERIC)
+-------------------+
| ParamCount (u32)  |
+-------------------+
| RetType           |
+-------------------+
| Param[0]          |
| Param[1]          |
| ...               |
```

Key points:
- Calling convention is **not 0x06**
- May include:
  - `HASTHIS` (instance methods)
  - `GENERIC` + generic parameter count
- Has:
  - Parameter count
  - Return type
  - N parameter types

---

### **Comparison table**

| Aspect                  | Field Signature | MethodDef Signature |
|-------------------------|-----------------|---------------------|
| Blob heap               | ✅               | ✅                   |
| First byte              | `0x06`          | flags (0x00–0x30)   |
| Parameter count         | ❌               | ✅                   |
| Return type             | ❌               | ✅                   |
| Uses `Type` grammar     | ✅               | ✅                   |
| Uses `Param` grammar    | ❌               | ✅                   |

---

## #### 2. How to decode a signature (step by step)

All decoding follows **ECMA‑335 §II.23.2 – Signatures**.

---

### **Step 1: Read the calling convention byte**

```c
uint8_t callConv = blob[0];
```

Common values:
- `0x06` → Field
- `0x00` → Static method
- `0x20` → Instance (`HASTHIS`)
- `0x10` → Generic method (next is GenParamCount)

---

### **Step 2: Decode compressed integers**

Metadata uses **compressed integers** (ECMA‑335 §II.23.2):

| Encoding | Size | Bits |
|--------|------|------|
| `0xxx xxxx` | 1 byte | 7 bits |
| `10xx xxxx xxxx xxxx` | 2 bytes | 14 bits |
| `110x xxxx xxxx xxxx xxxx xxxx xxxx` | 4 bytes | 29 bits |

You **must** implement this before decoding signatures.

Used for:
- Parameter counts
- Generic parameter counts
- Array ranks
- `VAR` / `MVAR` indices

---

### **Step 3: Decode `Type` (core task)**

Both field and method signatures ultimately decode a **Type**.

The next byte defines the element type:

| Byte | Meaning |
|----|--------|
| `0x02` | BOOLEAN |
| `0x03` | CHAR |
| `0x08` | I4 |
| `0x0E` | STRING |
| `0x12` | CLASS |
| `0x15` | VALUETYPE |
| `0x1D` | SZARRAY |
| `0x13` | VAR (generic type param) |
| `0x1E` | MVAR (generic method param) |
| `0x10` | BYREF |
| `0x0F` | PTR |

Some types are **recursive**:

Example: `int32[]`
```
0x1D        // SZARRAY
  0x08      // I4
```

Example: `class MyType`
```
0x12        // CLASS
  TypeDefOrRefEncoded
```

---

### **TypeDefOrRefEncoded**

This is a **coded index** into:
- TypeDef
- TypeRef
- TypeSpec

Decoded as:
```
value >> 2  → row index
value & 0x3 → table selector
```

---

### **Step 4: Method-specific decoding**

For MethodDef signatures:

1. Read callconv byte
2. If GENERIC → read GenParamCount
3. Read ParamCount
4. Decode ReturnType
5. Decode ParamCount parameter types

Each parameter uses:
```
Param ::= [BYREF] Type
```

---

### **What you are NOT decoding yet**

You mentioned:
> Exception not extracted yet

Correct: **Exception handling clauses are not part of the signature**
They live in the **method body**, after IL, in:
- Fat exception sections
- Small exception sections

Signature decoding does **not** involve EH.

---

## ✅ Practical advice

Since you already store:
- `uint8_t* blob`
- `blob_length`

You should:
1. Pass a pointer cursor by reference
2. Decode incrementally
3. Build a recursive descent decoder for `Type`

This is exactly how ILDasm / CoreCLR does it.

---

## ✅ Summary

- **Field and MethodDef signatures are different at the top level**
- **They share the same Type encoding rules**
- **Decoding requires**
  - Calling convention byte
  - Compressed integer decoding
  - Recursive `Type` parsing
- **Exception clauses are unrelated to signatures**

---

<span id='relatedprompt' style='display:none'>
  <span id='relQ1'>How is TypeSpec (generic instantiation) encoded in ECMA-335 signatures?</span>
  <span id='relQ2'>How are method exception handling sections encoded in IL method bodies?</span>
</span>
