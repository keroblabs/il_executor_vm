#include <math.h>
#include <string.h>
#include "harness.h"
#include "fake_embedder.h"

#define PAYLOAD 4096u
#define HANDLES 128u

static const object_handle_t * handle_of(object_id_t id)
{
    return &fake_handles()[OBJECT_INDEX(id)];
}

static void round_trip(const void * value, uint32_t bytes)
{
    uint8_t out[16];
    object_id_t box = value_box(&fake_oh, FAKE_T_DATA, value, bytes);

    CHECK(box != OBJECT_NULL);
    CHECK_EQ(objects_kind_of(&fake_oh, box), OBJECT_K_BOX);
    CHECK_EQ(objects_size_of(&fake_oh, box), bytes);
    CHECK(!(handle_of(box)->flags & OBJECT_H_HASREFS));
    memset(out, 0xEE, sizeof out);
    value_unbox(&fake_oh, box, out, bytes);
    CHECK(memcmp(out, value, bytes) == 0);
    CHECK_EQ(out[bytes], 0xEE);                 /* nothing past the value is written */
}

TEST_CASE(box_round_trips_every_primitive)
{
    const int8_t   i1 = -8;
    const uint8_t  u1 = 0xF0;
    const int16_t  i2 = -1600;
    const uint16_t c2 = 0xBEEF;                 /* char */
    const int32_t  i4 = -32000000;
    const uint32_t u4 = 0xDEADBEEFu;
    const int64_t  i8 = -6400000000000LL;
    const uint64_t u8 = 0xFEEDFACECAFEBEEFull;
    const float    r4 = -3.25f;
    const double   r8 = 6.02214076e23;
    const uint8_t  b1 = 1;                      /* bool */
    const int32_t  ni = 0x7FFFFFFF;             /* native int is 32 bits on every target */

    fake_init(PAYLOAD, HANDLES);
    round_trip(&i1, sizeof i1);
    round_trip(&u1, sizeof u1);
    round_trip(&i2, sizeof i2);
    round_trip(&c2, sizeof c2);
    round_trip(&i4, sizeof i4);
    round_trip(&u4, sizeof u4);
    round_trip(&i8, sizeof i8);
    round_trip(&u8, sizeof u8);
    round_trip(&r4, sizeof r4);
    round_trip(&r8, sizeof r8);
    round_trip(&b1, sizeof b1);
    round_trip(&ni, sizeof ni);
    CHECK(heap_verify(&fake_oh));
}

TEST_CASE(box_copies_the_value)
{
    int32_t v = 41;
    int32_t out = 0;
    object_id_t box;

    fake_init(PAYLOAD, HANDLES);
    box = value_box(&fake_oh, FAKE_T_DATA, &v, sizeof v);
    v = 42;
    value_unbox(&fake_oh, box, &out, sizeof out);
    CHECK_EQ(out, 41);
}

TEST_CASE(boxed_struct_with_an_id_is_scanned)
{
    const uint32_t pair[2] = { 5u, 7u };        /* word 0 holds an id */
    object_id_t box;

    fake_init(PAYLOAD, HANDLES);
    box = value_box(&fake_oh, FAKE_T_MIXED, pair, sizeof pair);
    CHECK(handle_of(box)->flags & OBJECT_H_HASREFS);
    CHECK_EQ(objects_type_of(&fake_oh, box), FAKE_T_MIXED);
}

TEST_CASE(box_of_an_empty_struct)
{
    object_id_t box;

    fake_init(PAYLOAD, HANDLES);
    box = value_box(&fake_oh, FAKE_T_DATA, NULL, 0);
    CHECK(box != OBJECT_NULL);
    CHECK_EQ(objects_size_of(&fake_oh, box), 0);
    value_unbox(&fake_oh, box, NULL, 0);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(box_that_can_never_fit_returns_null)
{
    static uint8_t big[PAYLOAD];

    fake_init(PAYLOAD, HANDLES);
    CHECK(value_box(&fake_oh, FAKE_T_DATA, big, PAYLOAD) == OBJECT_NULL);
    CHECK_EQ(fake_no_memory_count, 0);
}

TEST_CASE(unbox_misuse_is_corrupt)
{
    int32_t v = 1;
    int32_t out = 0;
    object_id_t box;
    object_id_t arr;

    fake_init(PAYLOAD, HANDLES);
    box = value_box(&fake_oh, FAKE_T_DATA, &v, sizeof v);
    arr = array_create(&fake_oh, FAKE_T_DATA, 4, 1);
    fake_allow_corrupt();
    value_unbox(&fake_oh, box, &out, 8);        /* the wrong size */
    value_unbox(&fake_oh, arr, &out, 4);        /* not a box */
    value_unbox(&fake_oh, OBJECT_NULL, &out, 4);
    value_unbox(&fake_oh, box, NULL, 4);
    CHECK(value_box(&fake_oh, FAKE_T_DATA, NULL, 4) == OBJECT_NULL);
    CHECK_EQ(fake_corrupt_count, 5);
    CHECK_EQ(out, 0);
}

/* ---- normalisation (doc 14 section 6.7) ------------------------------ */

static stack_slot_t slot_of(uint8_t kind)
{
    stack_slot_t s;

    memset(&s, 0, sizeof s);
    s.kind = kind;
    return s;
}

static stack_slot_t slot_i4(int32_t v)
{
    stack_slot_t s = slot_of(STACK_K_I4);

    s.u.i4 = v;
    return s;
}

static stack_slot_t slot_nint(int32_t v)
{
    stack_slot_t s = slot_of(STACK_K_NINT);

    s.u.nint = v;
    return s;
}

static stack_slot_t slot_i8(int64_t v)
{
    stack_slot_t s = slot_of(STACK_K_I8);

    s.u.i8 = v;
    return s;
}

static stack_slot_t slot_r8(double v)
{
    stack_slot_t s = slot_of(STACK_K_R8);

    s.u.r8 = v;
    return s;
}

static stack_slot_t slot_ref(object_id_t id)
{
    stack_slot_t s = slot_of(STACK_K_REF);

    s.u.id = id;
    return s;
}

static stack_slot_t slot_byref(object_byref_t b)
{
    stack_slot_t s = slot_of(STACK_K_BYREF);

    s.u.byref_base = b.id;
    s.aux = b.offset;
    return s;
}

/* A byref into the statics slab, whose ids are byte offsets (doc 14 section 3.5). */
static stack_slot_t slot_static_byref(uint16_t field, uint16_t offset)
{
    object_byref_t b;

    b.id = OBJECT_ID(OBJECT_SPACE_STATIC, field);
    b.offset = offset;
    b._pad = 0;
    return slot_byref(b);
}

/* A byref to a thread's stack slot, which has no address without the thread (doc 14 section 4.1). */
static stack_slot_t slot_stack_byref(uint16_t slot, uint16_t offset)
{
    object_byref_t b;

    b.id = OBJECT_ID(OBJECT_SPACE_STACK, slot);
    b.offset = offset;
    b._pad = 0;
    return slot_byref(b);
}

/* A failure reads as the digits: kind a, kind b, flags, result + 1. */
static void expect_order(stack_slot_t a, stack_slot_t b, uint32_t flags, int32_t want)
{
    const long tag = ((a.kind * 10L + b.kind) * 10L + (long)flags) * 10L + 1L;

    CHECK_EQ(tag + value_compare(&fake_oh, &a, &b, flags), tag + want);
}

TEST_CASE(compare_matrix_over_every_kind_pair)
{
    stack_slot_t low[6];
    stack_slot_t high[6];
    object_id_t a;
    uint32_t flags;
    int i;
    int j;

    fake_init(PAYLOAD, HANDLES);
    a = array_create(&fake_oh, FAKE_T_DATA, 4, 2);
    /* I4, I8, R8, NINT, REF, BYREF; every number is 4 low and 5 high. */
    low[0] = slot_i4(4);
    high[0] = slot_i4(5);
    low[1] = slot_i8(4);
    high[1] = slot_i8(5);
    low[2] = slot_r8(4.0);
    high[2] = slot_r8(5.0);
    low[3] = slot_nint(4);
    high[3] = slot_nint(5);
    low[4] = slot_ref(OBJECT_NULL);
    high[4] = slot_ref(a);
    low[5] = slot_byref(array_element_byref(&fake_oh, a, 0));
    high[5] = slot_byref(array_element_byref(&fake_oh, a, 1));

    for (flags = 0; flags <= VALUE_UNSIGNED; flags++)
        for (i = 0; i < 6; i++)
            for (j = 0; j < 6; j++) {
                const int common = (i < 4 && j < 4) || i == j;

                expect_order(low[i], high[j], flags, common ? VALUE_LESS : VALUE_UNORDERED);
                expect_order(high[i], low[j], flags, common ? VALUE_GREATER : VALUE_UNORDERED);
                expect_order(high[i], high[j], flags, common ? VALUE_EQUAL : VALUE_UNORDERED);
            }
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(compare_i4_and_nint_by_value)
{
    stack_slot_t n;

    fake_init(PAYLOAD, HANDLES);
    expect_order(slot_i4(5), slot_nint(5), 0, VALUE_EQUAL);
    expect_order(slot_nint(5), slot_i4(5), VALUE_UNSIGNED, VALUE_EQUAL);
    expect_order(slot_i4(-1), slot_nint(0), 0, VALUE_LESS);
    expect_order(slot_i4(-1), slot_nint(0), VALUE_UNSIGNED, VALUE_GREATER);

    n = slot_i4(5);                         /* the values compare; the kinds stay apart */
    CHECK(value_convert(&fake_oh, &n, VALUE_T_NATIVE_INT, 0, &n));
    CHECK_EQ(n.kind, STACK_K_NINT);
    CHECK_EQ(n.u.nint, 5);
    CHECK(value_convert(&fake_oh, &n, VALUE_T_INT32, 0, &n));
    CHECK_EQ(n.kind, STACK_K_I4);
    CHECK_EQ(n.u.i4, 5);
}

TEST_CASE(compare_unsigned_reads_each_width)
{
    fake_init(PAYLOAD, HANDLES);
    expect_order(slot_i4(-1), slot_i4(1), 0, VALUE_LESS);
    expect_order(slot_i4(-1), slot_i4(1), VALUE_UNSIGNED, VALUE_GREATER);
    expect_order(slot_i4(INT32_MIN), slot_i4(INT32_MAX), 0, VALUE_LESS);
    expect_order(slot_i4(INT32_MIN), slot_i4(INT32_MAX), VALUE_UNSIGNED, VALUE_GREATER);
    expect_order(slot_i8(INT64_MIN), slot_i8(INT64_MAX), 0, VALUE_LESS);
    expect_order(slot_i8(INT64_MIN), slot_i8(INT64_MAX), VALUE_UNSIGNED, VALUE_GREATER);
    expect_order(slot_i8(-1), slot_i8(1), VALUE_UNSIGNED, VALUE_GREATER);
    /* Each operand is zero-extended from its own width. */
    expect_order(slot_i4(-1), slot_i8(0xFFFFFFFFLL), 0, VALUE_LESS);
    expect_order(slot_i4(-1), slot_i8(0xFFFFFFFFLL), VALUE_UNSIGNED, VALUE_EQUAL);
    expect_order(slot_i8(-1), slot_nint(-1), 0, VALUE_EQUAL);
    expect_order(slot_i8(-1), slot_nint(-1), VALUE_UNSIGNED, VALUE_GREATER);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(compare_reals_and_nan)
{
    fake_init(PAYLOAD, HANDLES);
    expect_order(slot_r8(NAN), slot_r8(NAN), 0, VALUE_UNORDERED);
    expect_order(slot_r8(NAN), slot_i4(0), 0, VALUE_UNORDERED);
    expect_order(slot_i8(0), slot_r8(NAN), VALUE_UNSIGNED, VALUE_UNORDERED);
    expect_order(slot_r8(-0.0), slot_r8(0.0), 0, VALUE_EQUAL);
    expect_order(slot_r8(INFINITY), slot_i8(INT64_MAX), 0, VALUE_GREATER);
    expect_order(slot_r8(-INFINITY), slot_i8(INT64_MIN), 0, VALUE_LESS);
    expect_order(slot_r8(2.5), slot_i4(2), 0, VALUE_GREATER);
    expect_order(slot_r8(-2.5), slot_nint(-2), 0, VALUE_LESS);
    /* The integer meets the double through the conversion, so 2^53 + 1 is 2^53. */
    expect_order(slot_i8(9007199254740993LL), slot_r8(9007199254740992.0), 0, VALUE_EQUAL);
    expect_order(slot_i4(-1), slot_r8(4294967295.0), 0, VALUE_LESS);
    expect_order(slot_i4(-1), slot_r8(4294967295.0), VALUE_UNSIGNED, VALUE_EQUAL);
    expect_order(slot_i8(-1), slot_r8(18446744073709551616.0), VALUE_UNSIGNED, VALUE_EQUAL);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(compare_references_by_identity)
{
    stack_slot_t p;
    stack_slot_t q;

    fake_init(PAYLOAD, HANDLES);
    p = slot_ref(objects_create(&fake_oh, FAKE_T_DATA, 8));
    q = slot_ref(objects_create(&fake_oh, FAKE_T_DATA, 8));
    expect_order(p, p, 0, VALUE_EQUAL);
    expect_order(p, slot_ref(OBJECT_NULL), VALUE_UNSIGNED, VALUE_GREATER);   /* the null test */
    expect_order(slot_ref(OBJECT_NULL), slot_ref(OBJECT_NULL), 0, VALUE_EQUAL);
    CHECK(value_compare(&fake_oh, &p, &q, 0) != VALUE_EQUAL);
    CHECK_EQ(value_compare(&fake_oh, &p, &q, 0), -value_compare(&fake_oh, &q, &p, 0));
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(compare_byrefs_by_address)
{
    object_id_t a;

    fake_init(PAYLOAD, HANDLES);
    a = array_create(&fake_oh, FAKE_T_DATA, 4, 4);
    expect_order(slot_byref(array_element_byref(&fake_oh, a, 1)),
                 slot_byref(array_element_byref(&fake_oh, a, 2)), 0, VALUE_LESS);
    expect_order(slot_byref(array_element_byref(&fake_oh, a, 2)),
                 slot_byref(array_element_byref(&fake_oh, a, 2)), 0, VALUE_EQUAL);
    /* One static byte spelt two ways: a byref is where it points. */
    expect_order(slot_static_byref(16, 4), slot_static_byref(20, 0), 0, VALUE_EQUAL);
    expect_order(slot_static_byref(16, 4), slot_static_byref(20, 1), 0, VALUE_LESS);
    expect_order(slot_static_byref(16, 4), slot_nint(0), 0, VALUE_UNORDERED);   /* none is null */
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(compare_stack_byrefs_by_slot)
{
    stack_slot_t heap;

    fake_init(PAYLOAD, HANDLES);
    heap = slot_byref(array_element_byref(&fake_oh, array_create(&fake_oh, FAKE_T_DATA, 4, 2), 0));
    expect_order(slot_stack_byref(3, 0), slot_stack_byref(3, 0), 0, VALUE_EQUAL);
    expect_order(slot_stack_byref(3, 0), slot_stack_byref(3, 4), 0, VALUE_LESS);
    expect_order(slot_stack_byref(3, 4), slot_stack_byref(4, 0), 0, VALUE_LESS);
    expect_order(slot_stack_byref(4, 0), slot_stack_byref(3, 7), VALUE_UNSIGNED, VALUE_GREATER);
    /* Never the same byte as a heap or static byref, and no address to order them by. */
    expect_order(slot_stack_byref(3, 0), heap, 0, VALUE_UNORDERED);
    expect_order(slot_static_byref(16, 0), slot_stack_byref(3, 0), 0, VALUE_UNORDERED);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(compare_empty_or_unknown_kind_is_corrupt)
{
    fake_init(PAYLOAD, HANDLES);
    fake_allow_corrupt();
    expect_order(slot_of(STACK_K_EMPTY), slot_i4(0), 0, VALUE_UNORDERED);
    expect_order(slot_i4(0), slot_of(STACK_K_EMPTY), 0, VALUE_UNORDERED);
    expect_order(slot_of(STACK_K_BYREF + 1), slot_of(STACK_K_BYREF + 1), 0, VALUE_UNORDERED);
    CHECK_EQ(fake_corrupt_count, 3);
}

#define I4(v)       { STACK_K_I4, 0, 0, { .i4 = (v) } }
#define NI(v)       { STACK_K_NINT, 0, 0, { .nint = (v) } }
#define I8(v)       { STACK_K_I8, 0, 0, { .i8 = (v) } }
#define R8(v)       { STACK_K_R8, 0, 0, { .r8 = (v) } }
#define REFUSED     { STACK_K_EMPTY, 0, 0, { .i8 = 0 } }
#define CHK         VALUE_CHECKED
#define UNS         VALUE_UNSIGNED

typedef struct {
    stack_slot_t in;
    uint8_t      to;
    uint8_t      flags;
    stack_slot_t out;           /* REFUSED: returns 0, dst untouched */
    const char * what;
} convert_row_t;

static const convert_row_t convert_rows[] = {
    { I4(200),   VALUE_T_INT8, 0,   I4(-56),  "int32 200 to int8 wraps" },
    { I4(300),   VALUE_T_INT8, 0,   I4(44),   "int32 300 to int8 keeps the low byte" },
    { I4(127),   VALUE_T_INT8, CHK, I4(127),  "int8 max, checked" },
    { I4(128),   VALUE_T_INT8, CHK, REFUSED,  "int8 max + 1, checked" },
    { I4(-128),  VALUE_T_INT8, CHK, I4(-128), "int8 min, checked" },
    { I4(-129),  VALUE_T_INT8, CHK, REFUSED,  "int8 min - 1, checked" },
    { R8(127.9),    VALUE_T_INT8, 0,   I4(127),  "127.9 truncates to int8 max" },
    { R8(-128.9),   VALUE_T_INT8, CHK, I4(-128), "-128.9 truncates to int8 min, checked" },
    { R8(-129.0),   VALUE_T_INT8, CHK, REFUSED,  "-129.0 to int8, checked" },
    { R8(-129.0),   VALUE_T_INT8, 0,   I4(-128), "-129.0 saturates at int8 min" },
    { R8(INFINITY), VALUE_T_INT8, 0,   I4(127),  "+inf saturates at int8 max" },
    { R8(NAN),      VALUE_T_INT8, 0,   I4(0),    "NaN to int8 is 0" },

    { I4(300),   VALUE_T_UINT8, 0,         I4(44),  "int32 300 to uint8 keeps the low byte" },
    { I4(-1),    VALUE_T_UINT8, 0,         I4(255), "int32 -1 to uint8" },
    { I4(255),   VALUE_T_UINT8, CHK,       I4(255), "uint8 max, checked" },
    { I4(256),   VALUE_T_UINT8, CHK,       REFUSED, "uint8 max + 1, checked" },
    { I4(-1),    VALUE_T_UINT8, CHK,       REFUSED, "int32 -1 to uint8, checked" },
    { I4(-1),    VALUE_T_UINT8, CHK | UNS, REFUSED, "uint32 max to uint8, checked" },
    { R8(255.9),     VALUE_T_UINT8, CHK, I4(255), "255.9 to uint8, checked" },
    { R8(256.0),     VALUE_T_UINT8, CHK, REFUSED, "256.0 to uint8, checked" },
    { R8(300.0),     VALUE_T_UINT8, 0,   I4(255), "300.0 saturates at uint8 max, not 44" },
    { R8(-INFINITY), VALUE_T_UINT8, 0,   I4(0),   "-inf saturates at uint8 min" },

    { I4(65535),  VALUE_T_INT16, 0,   I4(-1),     "int32 65535 to int16" },
    { I4(32767),  VALUE_T_INT16, CHK, I4(32767),  "int16 max, checked" },
    { I4(32768),  VALUE_T_INT16, CHK, REFUSED,    "int16 max + 1, checked" },
    { I4(-32768), VALUE_T_INT16, CHK, I4(-32768), "int16 min, checked" },
    { I4(-32769), VALUE_T_INT16, CHK, REFUSED,    "int16 min - 1, checked" },
    { R8(65578.0),  VALUE_T_INT16, 0, I4(32767),  "65578.0 saturates at int16 max, not 42" },
    { R8(-65578.0), VALUE_T_INT16, 0, I4(-32768), "-65578.0 saturates at int16 min" },

    { I4(-1),     VALUE_T_UINT16, 0,   I4(65535), "int32 -1 to uint16" },
    { I4(65535),  VALUE_T_UINT16, CHK, I4(65535), "uint16 max, checked" },
    { I4(65536),  VALUE_T_UINT16, CHK, REFUSED,   "uint16 max + 1, checked" },
    { R8(-42.0),  VALUE_T_UINT16, 0,   I4(0),     "-42.0 saturates at uint16 min, not 65494" },

    { I8(0x100000005LL), VALUE_T_INT32, 0,         I4(5),         "int64 to int32: the low word" },
    { I8(-2147483649LL), VALUE_T_INT32, 0,         I4(INT32_MAX), "-2^31 - 1 to int32 wraps" },
    { I8(2147483647LL),  VALUE_T_INT32, CHK,       I4(INT32_MAX), "int32 max, checked" },
    { I8(2147483648LL),  VALUE_T_INT32, CHK,       REFUSED,       "int32 max + 1, checked" },
    { I8(-2147483648LL), VALUE_T_INT32, CHK,       I4(INT32_MIN), "int32 min, checked" },
    { I8(-2147483649LL), VALUE_T_INT32, CHK,       REFUSED,       "int32 min - 1, checked" },
    { I4(-1),            VALUE_T_INT32, CHK | UNS, REFUSED,       "uint32 max to int32, checked" },
    { R8(-1.5),          VALUE_T_INT32, 0,   I4(-1),        "-1.5 truncates toward zero" },
    { R8(2147483647.9),  VALUE_T_INT32, 0,   I4(INT32_MAX), "just below 2^31" },
    { R8(2147483648.0),  VALUE_T_INT32, 0,   I4(INT32_MAX), "2^31 saturates at int32 max" },
    { R8(-2147483648.9), VALUE_T_INT32, 0,   I4(INT32_MIN), "just above -2^31 - 1" },
    { R8(-2147483649.0), VALUE_T_INT32, 0,   I4(INT32_MIN), "-2^31 - 1 saturates at int32 min" },
    { R8(INFINITY),      VALUE_T_INT32, 0,   I4(INT32_MAX), "+inf to int32" },
    { R8(-INFINITY),     VALUE_T_INT32, 0,   I4(INT32_MIN), "-inf to int32" },
    { R8(NAN),           VALUE_T_INT32, 0,   I4(0),         "NaN to int32 is 0" },
    { R8(2147483647.9),  VALUE_T_INT32, CHK, I4(INT32_MAX), "just below 2^31, checked" },
    { R8(2147483648.0),  VALUE_T_INT32, CHK, REFUSED,       "2^31, checked" },
    { R8(-2147483648.9), VALUE_T_INT32, CHK, I4(INT32_MIN), "just above -2^31 - 1, checked" },
    { R8(-2147483649.0), VALUE_T_INT32, CHK, REFUSED,       "-2^31 - 1, checked" },
    { R8(NAN),           VALUE_T_INT32, CHK, REFUSED,       "NaN to int32, checked" },

    { I4(-1),            VALUE_T_UINT32, 0,         I4(-1),  "int32 -1 to uint32 keeps the bits" },
    { I8(0x100000007LL), VALUE_T_UINT32, 0,         I4(7),   "int64 to uint32 keeps the low word" },
    { I4(-1),            VALUE_T_UINT32, CHK,       REFUSED, "int32 -1 to uint32, checked" },
    { I4(-1),            VALUE_T_UINT32, CHK | UNS, I4(-1),  "uint32 max, checked" },
    { I8(4294967295LL),  VALUE_T_UINT32, CHK,       I4(-1),  "int64 uint32 max, checked" },
    { I8(4294967296LL),  VALUE_T_UINT32, CHK,       REFUSED, "uint32 max + 1, checked" },
    { R8(4294967295.5),  VALUE_T_UINT32, 0,         I4(-1),  "just below 2^32" },
    { R8(1e10),          VALUE_T_UINT32, 0,         I4(-1),  "1e10 saturates at uint32 max" },
    { R8(-0.5),          VALUE_T_UINT32, CHK,       I4(0),   "-0.5 truncates to 0, checked" },
    { R8(-1.0),          VALUE_T_UINT32, CHK,       REFUSED, "-1.0 to uint32, checked" },
    { R8(-1.0),          VALUE_T_UINT32, CHK | UNS, REFUSED, "unsigned has no effect on a float" },

    { I4(-1),  VALUE_T_INT64, 0,         I8(-1),           "int32 to int64 sign-extends" },
    { NI(-1),  VALUE_T_INT64, 0,         I8(-1),           "native int to int64 sign-extends" },
    { I4(-1),  VALUE_T_INT64, UNS,       I8(4294967295LL), "unsigned int32 to int64" },
    { I4(-1),  VALUE_T_INT64, CHK | UNS, I8(4294967295LL), "unsigned int32 to int64, checked" },
    { I8(-1),  VALUE_T_INT64, CHK | UNS, REFUSED,          "uint64 max to int64, checked" },
    { R8(1e300),  VALUE_T_INT64, 0, I8(INT64_MAX), "1e300 saturates at int64 max" },
    { R8(-1e300), VALUE_T_INT64, 0, I8(INT64_MIN), "-1e300 saturates at int64 min" },
    { R8(9223372036854775808.0),  VALUE_T_INT64, 0,   I8(INT64_MAX), "2^63 saturates" },
    { R8(9223372036854775808.0),  VALUE_T_INT64, CHK, REFUSED,       "2^63, checked" },
    { R8(9223372036854774784.0),  VALUE_T_INT64, CHK, I8(9223372036854774784LL),
      "the double below 2^63, checked" },
    { R8(-9223372036854775808.0), VALUE_T_INT64, CHK, I8(INT64_MIN), "-2^63, checked" },
    { R8(NAN),                    VALUE_T_INT64, CHK, REFUSED,       "NaN to int64, checked" },

    { I4(-1),  VALUE_T_UINT64, 0,         I8(4294967295LL), "int32 to uint64 zero-extends" },
    { NI(-1),  VALUE_T_UINT64, 0,         I8(4294967295LL), "native int to uint64 zero-extends" },
    { I8(-1),  VALUE_T_UINT64, 0,         I8(-1),           "int64 to uint64 keeps the bits" },
    { I4(-1),  VALUE_T_UINT64, CHK,       REFUSED,          "int32 -1 to uint64, checked" },
    { I8(-1),  VALUE_T_UINT64, CHK,       REFUSED,          "int64 -1 to uint64, checked" },
    { I8(-1),  VALUE_T_UINT64, CHK | UNS, I8(-1),           "uint64 max, checked" },
    { R8(18446744073709549568.0), VALUE_T_UINT64, CHK, I8(-2048), "2^64 - 2048, checked" },
    { R8(18446744073709551616.0), VALUE_T_UINT64, 0,   I8(-1),    "2^64 saturates at uint64 max" },
    { R8(18446744073709551616.0), VALUE_T_UINT64, CHK, REFUSED,   "2^64, checked" },
    { R8(-1.0),                   VALUE_T_UINT64, 0,   I8(0),     "-1.0 saturates at uint64 min" },

    { I8(0x1000001000000001LL), VALUE_T_FLOAT32, 0, R8(1152921642045800448.0),
      "int64 to float32 rounds once, not through a double" },
    { NI(16777217), VALUE_T_FLOAT32, 0,   R8(16777216.0),   "2^24 + 1 rounds to even" },
    { I4(-1),       VALUE_T_FLOAT32, UNS, R8(4294967296.0), "uint32 max rounds up to 2^32" },
    { R8(0.1),      VALUE_T_FLOAT32, 0,   R8((double)0.1f), "0.1 to float precision" },
    { R8(1e39),     VALUE_T_FLOAT32, 0,   R8(INFINITY),     "beyond float range is an infinity" },
    { R8(1e39),     VALUE_T_FLOAT32, CHK, R8(INFINITY),     "a float target always fits" },
    { R8(-0.0),     VALUE_T_FLOAT32, 0,   R8(-0.0),         "-0.0 keeps its sign" },

    { I4(-1), VALUE_T_FLOAT64, 0,   R8(-1.0),                    "int32 to float64" },
    { I4(-1), VALUE_T_FLOAT64, UNS, R8(4294967295.0),            "unsigned int32 to float64" },
    { I8(-1), VALUE_T_FLOAT64, UNS, R8(18446744073709551616.0),  "uint64 max to float64" },
    { I8(9007199254740993LL), VALUE_T_FLOAT64, 0, R8(9007199254740992.0),
      "2^53 + 1 rounds to even" },
    { I4(3),   VALUE_T_FLOAT64, CHK, R8(3.0), "checked has no effect on a float target" },
    { R8(NAN), VALUE_T_FLOAT64, 0,   R8(NAN), "NaN stays NaN" },

    { I4(7),             VALUE_T_NATIVE_INT, 0,   NI(7),         "int32 to native int" },
    { I8(0x100000005LL), VALUE_T_NATIVE_INT, 0,   NI(5),         "int64 to native int truncates" },
    { I8(-2147483648LL), VALUE_T_NATIVE_INT, CHK, NI(INT32_MIN), "native int min, checked" },
    { I8(2147483648LL),  VALUE_T_NATIVE_INT, CHK, REFUSED,       "native int max + 1, checked" },
    { R8(3.9),           VALUE_T_NATIVE_INT, 0,   NI(3),         "3.9 to native int" },

    { I8(0x1FFFFFFFFLL), VALUE_T_NATIVE_UINT, 0,         NI(-1),  "int64 to native uint wraps" },
    { NI(-1),            VALUE_T_NATIVE_UINT, CHK,       REFUSED, "-1 to native uint, checked" },
    { NI(-1),            VALUE_T_NATIVE_UINT, CHK | UNS, NI(-1),  "native uint max, checked" },
    { R8(-3.9),          VALUE_T_NATIVE_UINT, 0,         NI(0),   "-3.9 saturates at 0" },
};

/* Bit for bit, so -0.0 is not 0.0; a NaN matches any NaN. */
static int same_slot(const stack_slot_t * a, const stack_slot_t * b)
{
    if (a->kind != b->kind || a->flags != b->flags || a->aux != b->aux)
        return 0;
    switch (a->kind) {
    case STACK_K_I4:
        return a->u.i4 == b->u.i4;
    case STACK_K_NINT:
        return a->u.nint == b->u.nint;
    case STACK_K_I8:
        return a->u.i8 == b->u.i8;
    default:
        return (a->u.r8 != a->u.r8 && b->u.r8 != b->u.r8)
            || memcmp(&a->u.r8, &b->u.r8, sizeof a->u.r8) == 0;
    }
}

TEST_CASE(convert_table)
{
    size_t k;

    fake_init(PAYLOAD, HANDLES);
    for (k = 0; k < sizeof convert_rows / sizeof convert_rows[0]; k++) {
        const convert_row_t * r = &convert_rows[k];
        const stack_slot_t before = slot_i8(0x5A5A5A5A5A5A5A5ALL);
        const int refused = r->out.kind == STACK_K_EMPTY;
        stack_slot_t got = before;
        const int ok = value_convert(&fake_oh, &r->in, r->to, r->flags, &got);

        if (ok == refused || !same_slot(&got, refused ? &before : &r->out))
            harness_fail(__FILE__, __LINE__, r->what);
    }
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(convert_in_place)
{
    stack_slot_t s;

    fake_init(PAYLOAD, HANDLES);
    s = slot_i4(300);
    CHECK(value_convert(&fake_oh, &s, VALUE_T_UINT8, 0, &s));
    CHECK_EQ(s.kind, STACK_K_I4);
    CHECK_EQ(s.u.i4, 44);
    s = slot_r8(-2.75);
    CHECK(value_convert(&fake_oh, &s, VALUE_T_INT64, 0, &s));
    CHECK_EQ(s.kind, STACK_K_I8);
    CHECK_EQ(s.u.i8, -2);
    s = slot_i8(-1);
    CHECK(!value_convert(&fake_oh, &s, VALUE_T_UINT64, VALUE_CHECKED, &s));
    CHECK_EQ(s.kind, STACK_K_I8);           /* refused, so untouched */
    CHECK_EQ(s.u.i8, -1);
}

TEST_CASE(convert_misuse_is_corrupt)
{
    stack_slot_t from;
    stack_slot_t got;

    fake_init(PAYLOAD, HANDLES);
    fake_allow_corrupt();
    got = slot_i4(77);
    from = slot_ref(OBJECT_NULL);
    CHECK(!value_convert(&fake_oh, &from, VALUE_T_INT32, 0, &got));
    from = slot_static_byref(16, 0);
    CHECK(!value_convert(&fake_oh, &from, VALUE_T_NATIVE_UINT, 0, &got));
    from = slot_of(STACK_K_EMPTY);
    CHECK(!value_convert(&fake_oh, &from, VALUE_T_INT64, 0, &got));
    from = slot_i4(1);
    CHECK(!value_convert(&fake_oh, &from, 0, 0, &got));
    CHECK(!value_convert(&fake_oh, &from, VALUE_T_COUNT, 0, &got));
    CHECK_EQ(fake_corrupt_count, 5);
    CHECK_EQ(got.kind, STACK_K_I4);
    CHECK_EQ(got.u.i4, 77);
}

static const harness_case_t cases[] = {
    HARNESS_CASE(box_round_trips_every_primitive),
    HARNESS_CASE(box_copies_the_value),
    HARNESS_CASE(boxed_struct_with_an_id_is_scanned),
    HARNESS_CASE(box_of_an_empty_struct),
    HARNESS_CASE(box_that_can_never_fit_returns_null),
    HARNESS_CASE(unbox_misuse_is_corrupt),
    HARNESS_CASE(compare_matrix_over_every_kind_pair),
    HARNESS_CASE(compare_i4_and_nint_by_value),
    HARNESS_CASE(compare_unsigned_reads_each_width),
    HARNESS_CASE(compare_reals_and_nan),
    HARNESS_CASE(compare_references_by_identity),
    HARNESS_CASE(compare_byrefs_by_address),
    HARNESS_CASE(compare_stack_byrefs_by_slot),
    HARNESS_CASE(compare_empty_or_unknown_kind_is_corrupt),
    HARNESS_CASE(convert_table),
    HARNESS_CASE(convert_in_place),
    HARNESS_CASE(convert_misuse_is_corrupt),
};

HARNESS_MAIN(cases)
