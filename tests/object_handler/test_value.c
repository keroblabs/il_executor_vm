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

static const harness_case_t cases[] = {
    HARNESS_CASE(box_round_trips_every_primitive),
    HARNESS_CASE(box_copies_the_value),
    HARNESS_CASE(boxed_struct_with_an_id_is_scanned),
    HARNESS_CASE(box_of_an_empty_struct),
    HARNESS_CASE(box_that_can_never_fit_returns_null),
    HARNESS_CASE(unbox_misuse_is_corrupt),
};

HARNESS_MAIN(cases)
