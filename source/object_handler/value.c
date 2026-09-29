#include "obj_handler.h"

object_id_t value_box(obj_handler_t * oh, uint16_t type, const void * src, uint32_t bytes)
{
    object_id_t id;

    if (src == NULL && bytes != 0) {
        objects_corrupt(oh, "value_box: NULL source", OBJECT_NULL);
        return OBJECT_NULL;
    }
    /* The box is scanned by the value type's refmap (A3). */
    id = heap_alloc(oh, OBJECT_SPACE_HEAP, type, OBJECT_K_BOX, bytes);
    if (id != OBJECT_NULL && bytes != 0)
        memcpy(objects_ptr(oh, id), src, bytes);
    return id;
}

void value_unbox(obj_handler_t * oh, object_id_t id, void * dst, uint32_t bytes)
{
    object_info_t info;

    if (!objects_info(oh, id, &info, "value_unbox: not a live object"))
        return;
    if (info.kind != OBJECT_K_BOX || info.size != bytes || (dst == NULL && bytes != 0)) {
        objects_corrupt(oh, "value_unbox: not a box of that size", id);
        return;
    }
    if (bytes != 0)
        memcpy(dst, objects_ptr(oh, id), bytes);
}

/* ---- normalisation (doc 14 section 6.7) ----------------------------- */

#define VALUE_SIGN      ((uint64_t)1 << 63)

typedef struct {
    uint8_t kind;               /* the result's slot kind */
    uint8_t bits;
    uint8_t is_signed;          /* integer targets */
} value_target_t;

static const value_target_t value_targets[VALUE_T_COUNT] = {
    [VALUE_T_INT8]        = { STACK_K_I4,    8, 1 },
    [VALUE_T_UINT8]       = { STACK_K_I4,    8, 0 },
    [VALUE_T_INT16]       = { STACK_K_I4,   16, 1 },
    [VALUE_T_UINT16]      = { STACK_K_I4,   16, 0 },
    [VALUE_T_INT32]       = { STACK_K_I4,   32, 1 },
    [VALUE_T_UINT32]      = { STACK_K_I4,   32, 0 },
    [VALUE_T_INT64]       = { STACK_K_I8,   64, 1 },
    [VALUE_T_UINT64]      = { STACK_K_I8,   64, 0 },
    [VALUE_T_FLOAT32]     = { STACK_K_R8,   32, 0 },
    [VALUE_T_FLOAT64]     = { STACK_K_R8,   64, 0 },
    [VALUE_T_NATIVE_INT]  = { STACK_K_NINT, 32, 1 },
    [VALUE_T_NATIVE_UINT] = { STACK_K_NINT, 32, 0 },
};

static int value_is_number(uint8_t kind)
{
    return kind >= STACK_K_I4 && kind <= STACK_K_NINT;
}

/* An integer slot in 64 bits: sign-extended, or zero-extended from its own width when unsigned. */
static uint64_t value_bits(const stack_slot_t * s, int is_unsigned)
{
    int32_t v;

    if (s->kind == STACK_K_I8)
        return (uint64_t)s->u.i8;
    v = s->kind == STACK_K_NINT ? s->u.nint : s->u.i4;
    return is_unsigned ? (uint64_t)(uint32_t)v : (uint64_t)(int64_t)v;
}

/* The value of 64 two's-complement bits, with no implementation-defined conversion. */
static int64_t value_signed(uint64_t x)
{
    return x <= (uint64_t)INT64_MAX ? (int64_t)x
                                    : (int64_t)(x - (uint64_t)INT64_MAX - 1u) + INT64_MIN;
}

/* What value_convert to VALUE_T_FLOAT64 makes of a number. */
static double value_real(const stack_slot_t * s, int is_unsigned)
{
    uint64_t x;

    if (s->kind == STACK_K_R8)
        return s->u.r8;
    x = value_bits(s, is_unsigned);
    return is_unsigned ? (double)x : (double)value_signed(x);
}

static int32_t value_order(uint64_t a, uint64_t b)
{
    return a < b ? VALUE_LESS : a > b ? VALUE_GREATER : VALUE_EQUAL;
}

static int32_t value_order_real(double a, double b)
{
    if (a < b)
        return VALUE_LESS;
    if (a > b)
        return VALUE_GREATER;
    return a == b ? VALUE_EQUAL : VALUE_UNORDERED;
}

static uintptr_t value_address(obj_handler_t * oh, const stack_slot_t * s)
{
    object_byref_t b;

    b.id = s->u.byref_base;
    b.offset = s->aux;
    b._pad = 0;
    return (uintptr_t)objects_byref_ptr(oh, b);
}

int32_t value_compare(obj_handler_t * oh, const stack_slot_t * a, const stack_slot_t * b,
                      uint32_t flags)
{
    const int is_unsigned = (flags & VALUE_UNSIGNED) != 0;

    if (a->kind == STACK_K_EMPTY || a->kind > STACK_K_BYREF
        || b->kind == STACK_K_EMPTY || b->kind > STACK_K_BYREF) {
        objects_corrupt(oh, "value_compare: an empty or unknown slot kind", OBJECT_NULL);
        return VALUE_UNORDERED;
    }
    if (value_is_number(a->kind) && value_is_number(b->kind)) {
        if (a->kind == STACK_K_R8 || b->kind == STACK_K_R8)
            return value_order_real(value_real(a, is_unsigned), value_real(b, is_unsigned));
        if (is_unsigned)
            return value_order(value_bits(a, 1), value_bits(b, 1));
        /* Flipping the sign bit orders two's complement as unsigned. */
        return value_order(value_bits(a, 0) ^ VALUE_SIGN, value_bits(b, 0) ^ VALUE_SIGN);
    }
    if (a->kind != b->kind)
        return VALUE_UNORDERED;
    if (a->kind == STACK_K_REF)
        return value_order(a->u.id, b->u.id);
    return value_order(value_address(oh, a), value_address(oh, b));
}

/* The low `bits` of x, extended by the signedness. */
static uint64_t value_fit(uint64_t x, unsigned bits, int is_signed)
{
    const uint64_t mask = bits < 64 ? ((uint64_t)1 << bits) - 1u : ~(uint64_t)0;

    x &= mask;
    if (is_signed && (x >> (bits - 1)) != 0)
        x |= ~mask;
    return x;
}

/* The largest value of an integer target; the smallest is ~max when signed, else 0. */
static uint64_t value_max(const value_target_t * t)
{
    if (t->bits == 64)
        return t->is_signed ? (uint64_t)INT64_MAX : ~(uint64_t)0;
    return ((uint64_t)1 << (t->bits - t->is_signed)) - 1u;
}

/* Whether 64 bits, read signed or unsigned, hold a value inside the target. */
static int value_fits(uint64_t x, int is_unsigned, const value_target_t * t)
{
    const uint64_t max = value_max(t);

    if (is_unsigned || !(x & VALUE_SIGN))
        return x <= max;
    return t->is_signed && x >= ~max;
}

/* A double truncated into an integer target, saturated; 0 if it does not fit and checked. */
static int value_from_real(double d, const value_target_t * t, int checked, uint64_t * out)
{
    const uint64_t max = value_max(t);
    const double above = (double)max + 1.0;
    const double least = t->is_signed ? -above : 0.0;
    /* (least - 1, above) truncates into range; for 64 bits least - 1 rounds to least itself. */
    const int in_range = (d > least - 1.0 || d == least) && d < above;

    if (in_range) {
        *out = d < 0 ? (uint64_t)(int64_t)d : (uint64_t)d;
        return 1;
    }
    if (checked)
        return 0;
    *out = d != d ? 0 : d > 0 ? max : t->is_signed ? ~max : 0;
    return 1;
}

int value_convert(obj_handler_t * oh, const stack_slot_t * src, uint8_t to, uint32_t flags,
                  stack_slot_t * dst)
{
    const int checked = (flags & VALUE_CHECKED) != 0;
    const value_target_t * t;
    stack_slot_t out;
    uint64_t x;
    int is_unsigned;

    if (to == 0 || to >= VALUE_T_COUNT) {
        objects_corrupt(oh, "value_convert: an unknown target", OBJECT_NULL);
        return 0;
    }
    if (!value_is_number(src->kind)) {
        objects_corrupt(oh, "value_convert: the source is not a number", OBJECT_NULL);
        return 0;
    }
    t = &value_targets[to];
    /* An unchecked conversion to an unsigned integer zero-extends (ECMA-335 Table III.8). */
    is_unsigned = (flags & VALUE_UNSIGNED) != 0
               || (!checked && t->kind != STACK_K_R8 && !t->is_signed);
    memset(&out, 0, sizeof out);
    out.kind = t->kind;

    if (t->kind == STACK_K_R8) {
        if (src->kind == STACK_K_R8) {
            out.u.r8 = t->bits == 32 ? (double)(float)src->u.r8 : src->u.r8;
        } else {
            x = value_bits(src, is_unsigned);
            /* Rounded once, straight to the target's precision. */
            if (t->bits == 32)
                out.u.r8 = is_unsigned ? (double)(float)x : (double)(float)value_signed(x);
            else
                out.u.r8 = is_unsigned ? (double)x : (double)value_signed(x);
        }
    } else {
        if (src->kind == STACK_K_R8) {
            if (!value_from_real(src->u.r8, t, checked, &x))
                return 0;
        } else {
            x = value_bits(src, is_unsigned);
            if (checked && !value_fits(x, is_unsigned, t))
                return 0;
        }
        x = value_fit(x, t->bits, t->is_signed);
        if (t->kind == STACK_K_I8)
            out.u.i8 = value_signed(x);
        else if (t->kind == STACK_K_NINT)
            out.u.nint = (int32_t)value_signed(value_fit(x, 32, 1));
        else
            out.u.i4 = (int32_t)value_signed(value_fit(x, 32, 1));
    }
    *dst = out;
    return 1;
}
