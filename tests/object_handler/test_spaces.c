#include <string.h>
#include "harness.h"
#include "fake_embedder.h"

#define PAYLOAD 4096u
#define HANDLES 128u

static heap_stats_t stats(void)
{
    heap_stats_t st;

    heap_stats(&st);
    return st;
}

static void expect_init(obj_handler_status_t expected)
{
    CHECK_EQ(obj_handler_init(&fake_cfg, &fake_host), expected);
    CHECK(!obj_handler_is_ready());
}

/* ---- the statics slab (doc 14 section 3.5) --------------------------- */

TEST_CASE(statics_resolve_as_byte_offsets)
{
    object_byref_t b;

    fake_init(PAYLOAD, HANDLES);
    CHECK(objects_ptr(OBJECT_ID(OBJECT_SPACE_STATIC, 8)) == fake_statics() + 8);
    CHECK(objects_ptr(OBJECT_ID(OBJECT_SPACE_STATIC, FAKE_STATICS_BYTES - 1))
          == fake_statics() + FAKE_STATICS_BYTES - 1);
    b.id = OBJECT_ID(OBJECT_SPACE_STATIC, 16);
    b.offset = 4;
    b._pad = 0;
    CHECK(objects_byref_ptr(b) == fake_statics() + 20);
    CHECK_EQ(object_spaces[OBJECT_SPACE_STATIC].flags, OBJECT_SP_WRITABLE | OBJECT_SP_ROOT);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(statics_reserved_bytes_and_bounds_in_debug)
{
#if OBJECT_DEBUG_REFS
    fake_init(PAYLOAD, HANDLES);
    fake_allow_corrupt();
    CHECK(objects_ptr(OBJECT_ID(OBJECT_SPACE_STATIC, 0)) == NULL);
    CHECK(objects_ptr(OBJECT_ID(OBJECT_SPACE_STATIC, FAKE_STATICS_BYTES)) == NULL);
    CHECK_EQ(fake_corrupt_count, 2);
#else
    harness_skip("checked resolution is a debug-build feature");
#endif
}

TEST_CASE(statics_are_optional)
{
    fake_config(PAYLOAD, HANDLES);
    fake_cfg.statics = NULL;
    fake_cfg.statics_size = 0;
    fake_cfg.statics_refmap = NULL;
    fake_start();
    CHECK_EQ(object_spaces[OBJECT_SPACE_STATIC].limit, 0);
}

TEST_CASE(init_rejects_malformed_statics)
{
    static const uint8_t marks_word_0[8] = { 0x01 };
    static const uint8_t marks_word_1[8] = { 0x02 };

    fake_config(PAYLOAD, HANDLES);
    fake_cfg.statics = NULL;
    expect_init(OBJ_HANDLER_E_STATICS);
    fake_config(PAYLOAD, HANDLES);
    fake_cfg.statics = fake_statics() + 4;
    expect_init(OBJ_HANDLER_E_STATICS);
    fake_config(PAYLOAD, HANDLES);
    fake_cfg.statics_size = 8;
    expect_init(OBJ_HANDLER_E_STATICS);
    fake_config(PAYLOAD, HANDLES);
    fake_cfg.statics_size = 65544;
    expect_init(OBJ_HANDLER_E_STATICS);
    fake_config(PAYLOAD, HANDLES);
    fake_cfg.statics_size = 20;
    expect_init(OBJ_HANDLER_E_STATICS);
    fake_config(PAYLOAD, HANDLES);
    fake_cfg.statics_refmap = marks_word_0;
    expect_init(OBJ_HANDLER_E_STATICS);
    fake_config(PAYLOAD, HANDLES);
    fake_cfg.statics_refmap = marks_word_1;
    expect_init(OBJ_HANDLER_E_STATICS);

    fake_config(PAYLOAD, HANDLES);
    fake_cfg.statics_size = 16;
    fake_start();
}

/* ---- the constant table (doc 14 section 3.5) ------------------------- */

TEST_CASE(literal_is_read_in_place)
{
    object_id_t id;
    uint16_t i;

    fake_config(PAYLOAD, HANDLES);
    fake_consts_begin();
    i = fake_const_string("hello", 5);
    fake_consts_install();
    fake_start();

    id = obj_string_intern(i);
    CHECK(id == OBJECT_ID(OBJECT_SPACE_CONST, i));
    CHECK(objects_ptr(id) == fake_const_bytes() + fake_const_entry(i)->offset);   /* no copy */
    CHECK(strcmp(objects_ptr(id), "hello") == 0);
    CHECK_EQ(obj_string_length(id), 5);
    CHECK_EQ(obj_string_char_at(id, 1), 'e');
    CHECK_EQ(objects_type_of(id), FAKE_T_STRING);
    CHECK_EQ(objects_kind_of(id), OBJECT_K_STRING);
    CHECK_EQ(objects_size_of(id), 6);
    CHECK_EQ(stats().live_objects, 0);                          /* nothing was allocated */
    CHECK_EQ(heap_in_use(), 0);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(non_ascii_literal_decodes)
{
    object_id_t id;

    fake_config(PAYLOAD, HANDLES);
    fake_consts_begin();
    id = OBJECT_ID(OBJECT_SPACE_CONST, fake_const_string("\xC3\xA9t\xC3\xA9", 5));
    fake_consts_install();
    fake_start();
    CHECK_EQ(obj_string_length(id), 3);
    CHECK_EQ(obj_string_char_at(id, 0), 0xE9);
    CHECK_EQ(obj_string_char_at(id, 1), 't');
    CHECK_EQ(obj_string_char_at(id, 3), -1);
}

TEST_CASE(intern_misuse_is_corrupt)
{
    static const uint8_t blob[4] = { 1, 2, 3, 4 };
    uint16_t raw;
    uint16_t beyond;

    fake_config(PAYLOAD, HANDLES);
    fake_consts_begin();
    fake_const_string("s", 1);
    raw = fake_const_add(FAKE_T_DATA, OBJECT_K_RAW, 0, blob, sizeof blob);
    beyond = fake_const_string("t", 1);
    fake_consts_install();
    fake_cfg.const_count = beyond;              /* a real string entry sits just past the table */
    fake_start();
    fake_allow_corrupt();
    CHECK(obj_string_intern(0) == OBJECT_NULL);                 /* the reserved entry */
    CHECK(obj_string_intern(beyond) == OBJECT_NULL);            /* past const_count */
    CHECK(obj_string_intern(raw) == OBJECT_NULL);               /* not a string */
    CHECK_EQ(fake_corrupt_count, 3);
}

TEST_CASE(constants_are_optional)
{
    fake_init(PAYLOAD, HANDLES);
    CHECK_EQ(object_spaces[OBJECT_SPACE_CONST].limit, 0);
    fake_allow_corrupt();
    CHECK(obj_string_intern(1) == OBJECT_NULL);
    CHECK_EQ(fake_corrupt_count, 1);
}

TEST_CASE(constant_array_reads_like_a_heap_array)
{
    static const int32_t elements[5] = { 10, 20, 30, 40, 50 };
    object_byref_t b;
    object_id_t c;
    object_id_t h;
    const int32_t * copied;
    uint32_t k;

    fake_config(PAYLOAD, HANDLES);
    fake_consts_begin();
    c = OBJECT_ID(OBJECT_SPACE_CONST, fake_const_array(FAKE_T_DATA, 4, 5, elements));
    fake_consts_install();
    fake_start();

    CHECK_EQ(array_length(c), 5);
    CHECK_EQ(*(const int32_t *)array_element_ptr(c, 2), 30);
    CHECK(array_element_ptr(c, 5) == NULL);
    b = array_element_byref(c, 4);
    CHECK(b.id == c);
    CHECK(objects_byref_ptr(b) == array_element_ptr(c, 4));

    h = array_create(FAKE_T_DATA, 4, 5);
    array_copy(h, 0, c, 0, 5);                                  /* out of the image */
    copied = array_range_ptr(h, 0, 5);
    for (k = 0; k < 5; k++)
        CHECK_EQ(copied[k], elements[k]);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(writing_into_a_constant_is_corrupt)
{
    static const int32_t elements[2] = { 7, 8 };
    object_id_t c;
    object_id_t s;
    object_id_t h;

    fake_config(PAYLOAD, HANDLES);
    fake_consts_begin();
    c = OBJECT_ID(OBJECT_SPACE_CONST, fake_const_array(FAKE_T_DATA, 4, 2, elements));
    s = OBJECT_ID(OBJECT_SPACE_CONST, fake_const_string("s", 1));
    fake_consts_install();
    fake_start();
    h = array_create(FAKE_T_DATA, 4, 2);
    fake_allow_corrupt();
    array_copy(c, 0, h, 0, 1);
    array_clear(c, 0, 1);
    heap_release(c);
    heap_release(s);
    CHECK_EQ(fake_corrupt_count, 4);
    CHECK_EQ(*(const int32_t *)array_element_ptr(c, 0), 7);    /* untouched */
}

TEST_CASE(pinning_a_constant_does_nothing)
{
    object_id_t s;

    fake_config(PAYLOAD, HANDLES);
    fake_consts_begin();
    s = OBJECT_ID(OBJECT_SPACE_CONST, fake_const_string("pin", 3));
    fake_consts_install();
    fake_start();
    CHECK(objects_pin(s) == objects_ptr(s));
    objects_unpin(s);
    objects_unpin(s);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(concat_and_clone_copy_constants_into_the_heap)
{
    static const int32_t elements[3] = { 1, 2, 3 };
    object_id_t s;
    object_id_t c;
    object_id_t k;
    object_id_t a;

    fake_config(PAYLOAD, HANDLES);
    fake_consts_begin();
    s = OBJECT_ID(OBJECT_SPACE_CONST, fake_const_string("foo", 3));
    a = OBJECT_ID(OBJECT_SPACE_CONST, fake_const_array(FAKE_T_DATA, 4, 3, elements));
    fake_consts_install();
    fake_start();

    c = obj_string_concat(s, obj_string_create("bar", 3));
    CHECK_EQ(OBJECT_SPACE(c), OBJECT_SPACE_HEAP);
    CHECK(strcmp(objects_ptr(c), "foobar") == 0);
    CHECK(fake_handles()[OBJECT_INDEX(c)].flags & OBJECT_H_ASCII);

    k = objects_clone(s);
    CHECK_EQ(OBJECT_SPACE(k), OBJECT_SPACE_HEAP);
    CHECK(strcmp(objects_ptr(k), "foo") == 0);
    CHECK(fake_handles()[OBJECT_INDEX(k)].flags & OBJECT_H_ASCII);

    k = objects_clone(a);
    CHECK_EQ(OBJECT_SPACE(k), OBJECT_SPACE_HEAP);
    CHECK_EQ(array_length(k), 3);
    CHECK_EQ(*(int32_t *)array_element_ptr(k, 2), 3);
    CHECK(heap_verify());
}

TEST_CASE(constant_box_unboxes)
{
    const int32_t v = 7;
    int32_t out = 0;
    object_id_t b;

    fake_config(PAYLOAD, HANDLES);
    fake_consts_begin();
    b = OBJECT_ID(OBJECT_SPACE_CONST, fake_const_add(FAKE_T_DATA, OBJECT_K_BOX, 0, &v, sizeof v));
    fake_consts_install();
    fake_start();
    value_unbox(b, &out, sizeof out);
    CHECK_EQ(out, 7);
}

TEST_CASE(constant_id_with_a_generation_is_rejected_in_debug)
{
#if OBJECT_DEBUG_REFS
    uint16_t i;

    fake_config(PAYLOAD, HANDLES);
    fake_consts_begin();
    i = fake_const_string("g", 1);
    fake_consts_install();
    fake_start();
    fake_allow_corrupt();
    CHECK_EQ(objects_type_of(OBJECT_ID_GEN(OBJECT_SPACE_CONST, 1, i)), 0);
    CHECK_EQ(fake_corrupt_count, 1);
#else
    harness_skip("release ids carry no generation");
#endif
}

/* ---- init checks every constant entry -------------------------------- */

static uint16_t valid_string;
static uint16_t valid_array;

static void build_valid_table(void)
{
    static const int32_t elements[2] = { 1, 2 };

    fake_config(PAYLOAD, HANDLES);
    fake_consts_begin();
    valid_string = fake_const_string("ok", 2);
    valid_array = fake_const_array(FAKE_T_DATA, 4, 2, elements);
    fake_consts_install();
}

static array_header_t * valid_header(void)
{
    return (array_header_t *)(void *)(fake_const_bytes() + fake_const_entry(valid_array)->offset);
}

TEST_CASE(init_rejects_a_malformed_constant_table)
{
    build_valid_table();
    fake_cfg.const_base = NULL;
    expect_init(OBJ_HANDLER_E_CONST);
    build_valid_table();
    fake_cfg.const_base = fake_const_bytes() + 4;
    expect_init(OBJ_HANDLER_E_CONST);
    build_valid_table();
    fake_cfg.const_count = 65537;
    expect_init(OBJ_HANDLER_E_CONST);
    build_valid_table();
    fake_cfg.const_size = 8;
    expect_init(OBJ_HANDLER_E_CONST);

    build_valid_table();
    fake_const_entry(valid_string)->kind = OBJECT_K_FREE;
    expect_init(OBJ_HANDLER_E_CONST);
    build_valid_table();
    fake_const_entry(valid_string)->kind = OBJECT_K_COUNT;
    expect_init(OBJ_HANDLER_E_CONST);
    build_valid_table();
    fake_const_entry(valid_string)->flags = OBJECT_H_MARK;
    expect_init(OBJ_HANDLER_E_CONST);
    build_valid_table();
    fake_const_entry(valid_array)->flags = OBJECT_H_ASCII;     /* ASCII is for strings */
    expect_init(OBJ_HANDLER_E_CONST);

    build_valid_table();
    fake_const_entry(valid_string)->offset += 4;
    expect_init(OBJ_HANDLER_E_CONST);
    build_valid_table();
    fake_const_entry(valid_string)->offset = 0;                /* inside the table */
    expect_init(OBJ_HANDLER_E_CONST);
    build_valid_table();
    fake_const_entry(valid_array)->size = fake_cfg.const_size; /* runs past the end */
    expect_init(OBJ_HANDLER_E_CONST);

    build_valid_table();
    fake_const_bytes()[fake_const_entry(valid_string)->offset + 2] = 'x';  /* no NUL */
    expect_init(OBJ_HANDLER_E_CONST);
    build_valid_table();
    fake_const_entry(valid_string)->size = 0;
    expect_init(OBJ_HANDLER_E_CONST);

    build_valid_table();
    valid_header()->count = 3;                                  /* 8 + 3 * 4 != 16 */
    expect_init(OBJ_HANDLER_E_CONST);
    build_valid_table();
    valid_header()->elem_size = 0;
    expect_init(OBJ_HANDLER_E_CONST);

    build_valid_table();
    fake_const_entry(valid_array)->type = FAKE_T_NODE;         /* a constant holding an id */
    expect_init(OBJ_HANDLER_E_CONST);
    build_valid_table();
    fake_const_entry(valid_string)->kind = OBJECT_K_OBJECT;
    fake_const_entry(valid_string)->flags = 0;
    fake_const_entry(valid_string)->type = FAKE_T_PAIR;
    expect_init(OBJ_HANDLER_E_CONST);

    build_valid_table();
    fake_start();
}

TEST_CASE(init_checks_statics_before_constants)
{
    build_valid_table();
    fake_cfg.statics_size = 20;
    fake_const_entry(valid_string)->kind = OBJECT_K_FREE;
    expect_init(OBJ_HANDLER_E_STATICS);
}

static const harness_case_t cases[] = {
    HARNESS_CASE(statics_resolve_as_byte_offsets),
    HARNESS_CASE(statics_reserved_bytes_and_bounds_in_debug),
    HARNESS_CASE(statics_are_optional),
    HARNESS_CASE(init_rejects_malformed_statics),
    HARNESS_CASE(literal_is_read_in_place),
    HARNESS_CASE(non_ascii_literal_decodes),
    HARNESS_CASE(intern_misuse_is_corrupt),
    HARNESS_CASE(constants_are_optional),
    HARNESS_CASE(constant_array_reads_like_a_heap_array),
    HARNESS_CASE(writing_into_a_constant_is_corrupt),
    HARNESS_CASE(pinning_a_constant_does_nothing),
    HARNESS_CASE(concat_and_clone_copy_constants_into_the_heap),
    HARNESS_CASE(constant_box_unboxes),
    HARNESS_CASE(constant_id_with_a_generation_is_rejected_in_debug),
    HARNESS_CASE(init_rejects_a_malformed_constant_table),
    HARNESS_CASE(init_checks_statics_before_constants),
};

HARNESS_MAIN(cases)
