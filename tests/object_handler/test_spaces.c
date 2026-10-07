#include <string.h>
#include "harness.h"
#include "fake_embedder.h"

#define PAYLOAD 4096u
#define HANDLES 128u

static heap_stats_t stats(void)
{
    heap_stats_t st;

    heap_stats(&fake_oh, &st);
    return st;
}

static void expect_init(obj_handler_status_t expected)
{
    CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, &fake_host), expected);
    CHECK(!obj_handler_is_ready(&fake_oh));
}

/* ---- the statics slab (doc 14 section 3.5) --------------------------- */

TEST_CASE(statics_resolve_as_byte_offsets)
{
    object_byref_t b;

    fake_init(PAYLOAD, HANDLES);
    CHECK(objects_ptr(&fake_oh, OBJECT_ID(OBJECT_SPACE_STATIC, 8)) == fake_statics() + 8);
    CHECK(objects_ptr(&fake_oh, OBJECT_ID(OBJECT_SPACE_STATIC, FAKE_STATICS_BYTES - 1))
          == fake_statics() + FAKE_STATICS_BYTES - 1);
    b.id = OBJECT_ID(OBJECT_SPACE_STATIC, 16);
    b.offset = 4;
    b._pad = 0;
    CHECK(objects_byref_ptr(&fake_oh, b) == fake_statics() + 20);
    CHECK_EQ(fake_oh.spaces[OBJECT_SPACE_STATIC].flags, OBJECT_SP_WRITABLE | OBJECT_SP_ROOT);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(statics_reserved_bytes_and_bounds_in_debug)
{
#if OBJECT_DEBUG_REFS
    fake_init(PAYLOAD, HANDLES);
    fake_allow_corrupt();
    CHECK(objects_ptr(&fake_oh, OBJECT_ID(OBJECT_SPACE_STATIC, 0)) == NULL);
    CHECK(objects_ptr(&fake_oh, OBJECT_ID(OBJECT_SPACE_STATIC, FAKE_STATICS_BYTES)) == NULL);
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
    CHECK_EQ(fake_oh.spaces[OBJECT_SPACE_STATIC].limit, 0);
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

TEST_CASE(constant_string_check_accepts_only_terminated_payloads)
{
    static const uint8_t empty[] = { 0 };
    static const uint8_t text[] = { 'o', 'k', 0 };
    static const uint8_t embedded[] = { 'a', 0, 'b', 0 };
    static const uint8_t unterminated[] = { 'o', 'k' };
    static const uint8_t trusted[] = { 0xF0, 0 };

    CHECK(!obj_handler_is_ready(&fake_oh));
    CHECK(obj_string_check_const(&fake_oh, empty, sizeof empty));
    CHECK(obj_string_check_const(&fake_oh, text, sizeof text));
    CHECK(obj_string_check_const(&fake_oh, embedded, sizeof embedded));
    CHECK(obj_string_check_const(&fake_oh, trusted, sizeof trusted));
    CHECK(!obj_string_check_const(&fake_oh, NULL, 0));
    CHECK(!obj_string_check_const(&fake_oh, NULL, 1));
    CHECK(!obj_string_check_const(&fake_oh, NULL, UINT32_MAX));
    CHECK(!obj_string_check_const(&fake_oh, text, 0));
    CHECK(!obj_string_check_const(&fake_oh, text, sizeof text - 1u));
    CHECK(!obj_string_check_const(&fake_oh, unterminated, sizeof unterminated));
    CHECK(!obj_handler_is_ready(&fake_oh));
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(constant_string_init_preserves_trusted_content)
{
    static const uint8_t trusted[] = { 0xF0, 0 };
    object_id_t empty;
    object_id_t embedded;
    object_id_t opaque;
    object_info_t info;

    fake_config(PAYLOAD, HANDLES);
    fake_consts_begin();
    empty = OBJECT_ID(OBJECT_SPACE_CONST, fake_const_string("", 0));
    embedded = OBJECT_ID(OBJECT_SPACE_CONST, fake_const_string("a\0b", 3));
    opaque = OBJECT_ID(OBJECT_SPACE_CONST,
                      fake_const_add(FAKE_T_STRING, OBJECT_K_STRING, OBJECT_H_ASCII,
                                     trusted, sizeof trusted));
    fake_consts_install();
    fake_start();

    CHECK_EQ(objects_size_of(&fake_oh, empty), 1);
    CHECK_EQ(obj_string_length(&fake_oh, empty), 0);
    CHECK_EQ(obj_string_char_at(&fake_oh, empty, 0), -1);
    CHECK_EQ(obj_string_length(&fake_oh, embedded), 3);
    CHECK_EQ(obj_string_char_at(&fake_oh, embedded, 0), 'a');
    CHECK_EQ(obj_string_char_at(&fake_oh, embedded, 1), 0);
    CHECK_EQ(obj_string_char_at(&fake_oh, embedded, 2), 'b');
    CHECK_EQ(obj_string_char_at(&fake_oh, embedded, 3), -1);
    CHECK(objects_info(&fake_oh, opaque, &info, "constant_string_init"));
    CHECK_EQ(info.flags, OBJECT_H_ASCII);
    CHECK(memcmp(objects_ptr(&fake_oh, opaque), trusted, sizeof trusted) == 0);
    CHECK_EQ(heap_in_use(&fake_oh), 0);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(literal_is_read_in_place)
{
    object_id_t id;
    uint16_t i;

    fake_config(PAYLOAD, HANDLES);
    fake_consts_begin();
    i = fake_const_string("hello", 5);
    fake_consts_install();
    fake_start();

    id = obj_string_intern(&fake_oh, i);
    CHECK(id == OBJECT_ID(OBJECT_SPACE_CONST, i));
    CHECK(objects_ptr(&fake_oh, id)
          == fake_const_bytes() + fake_const_entry(i)->offset);  /* no copy */
    CHECK(strcmp(objects_ptr(&fake_oh, id), "hello") == 0);
    CHECK_EQ(obj_string_length(&fake_oh, id), 5);
    CHECK_EQ(obj_string_char_at(&fake_oh, id, 1), 'e');
    CHECK_EQ(objects_type_of(&fake_oh, id), FAKE_T_STRING);
    CHECK_EQ(objects_kind_of(&fake_oh, id), OBJECT_K_STRING);
    CHECK_EQ(objects_size_of(&fake_oh, id), 6);
    CHECK_EQ(stats().live_objects, 0);                          /* nothing was allocated */
    CHECK_EQ(heap_in_use(&fake_oh), 0);
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
    CHECK_EQ(obj_string_length(&fake_oh, id), 3);
    CHECK_EQ(obj_string_char_at(&fake_oh, id, 0), 0xE9);
    CHECK_EQ(obj_string_char_at(&fake_oh, id, 1), 't');
    CHECK_EQ(obj_string_char_at(&fake_oh, id, 3), -1);
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
    CHECK(obj_string_intern(&fake_oh, 0) == OBJECT_NULL);       /* the reserved entry */
    CHECK(obj_string_intern(&fake_oh, beyond) == OBJECT_NULL);  /* past const_count */
    CHECK(obj_string_intern(&fake_oh, raw) == OBJECT_NULL);     /* not a string */
    CHECK_EQ(fake_corrupt_count, 3);
}

TEST_CASE(constants_are_optional)
{
    fake_init(PAYLOAD, HANDLES);
    CHECK_EQ(fake_oh.spaces[OBJECT_SPACE_CONST].limit, 0);
    fake_allow_corrupt();
    CHECK(obj_string_intern(&fake_oh, 1) == OBJECT_NULL);
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

    CHECK_EQ(array_length(&fake_oh, c), 5);
    CHECK_EQ(*(const int32_t *)array_element_ptr(&fake_oh, c, 2), 30);
    CHECK(array_element_ptr(&fake_oh, c, 5) == NULL);
    b = array_element_byref(&fake_oh, c, 4);
    CHECK(b.id == c);
    CHECK(objects_byref_ptr(&fake_oh, b) == array_element_ptr(&fake_oh, c, 4));

    h = array_create(&fake_oh, FAKE_T_DATA, 4, 5);
    array_copy(&fake_oh, h, 0, c, 0, 5);                        /* out of the image */
    copied = array_range_ptr(&fake_oh, h, 0, 5);
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
    h = array_create(&fake_oh, FAKE_T_DATA, 4, 2);
    fake_allow_corrupt();
    array_copy(&fake_oh, c, 0, h, 0, 1);
    array_clear(&fake_oh, c, 0, 1);
    heap_release(&fake_oh, c);
    heap_release(&fake_oh, s);
    CHECK_EQ(fake_corrupt_count, 4);
    CHECK_EQ(*(const int32_t *)array_element_ptr(&fake_oh, c, 0), 7);  /* untouched */
}

TEST_CASE(delete_rejects_constants_and_flat_spaces)
{
    void (*const delete_functions[])(obj_handler_t *, object_id_t) = {
        objects_delete, obj_string_delete, array_delete, value_delete
    };
    const int32_t number = 42;
    object_id_t ids[4];
    size_t operation;

    fake_config(PAYLOAD, HANDLES);
    fake_consts_begin();
    ids[0] = OBJECT_ID(OBJECT_SPACE_CONST,
                       fake_const_add(FAKE_T_DATA, OBJECT_K_OBJECT, 0,
                                      &number, sizeof number));
    ids[1] = OBJECT_ID(OBJECT_SPACE_CONST, fake_const_string("text", 4));
    ids[2] = OBJECT_ID(OBJECT_SPACE_CONST, fake_const_array(FAKE_T_DATA, 4, 1, &number));
    ids[3] = OBJECT_ID(OBJECT_SPACE_CONST,
                       fake_const_add(FAKE_T_DATA, OBJECT_K_BOX, 0,
                                      &number, sizeof number));
    fake_consts_install();
    fake_start();
    fake_allow_corrupt();
    for (operation = 0; operation < 4; operation++) {
        delete_functions[operation](&fake_oh, ids[operation]);
        delete_functions[operation](&fake_oh, OBJECT_ID(OBJECT_SPACE_STATIC, 8));
    }
    CHECK_EQ(fake_corrupt_count, 8);
    CHECK_EQ(obj_string_length(&fake_oh, ids[1]), 4);
    CHECK_EQ(array_length(&fake_oh, ids[2]), 1);
    CHECK_EQ(objects_size_of(&fake_oh, ids[0]), sizeof number);
    CHECK_EQ(objects_size_of(&fake_oh, ids[3]), sizeof number);
    CHECK_EQ(stats().live_objects, 0);
    CHECK(heap_verify(&fake_oh));
}

TEST_CASE(pinning_a_constant_does_nothing)
{
    object_id_t s;

    fake_config(PAYLOAD, HANDLES);
    fake_consts_begin();
    s = OBJECT_ID(OBJECT_SPACE_CONST, fake_const_string("pin", 3));
    fake_consts_install();
    fake_start();
    CHECK(objects_pin(&fake_oh, s) == objects_ptr(&fake_oh, s));
    objects_unpin(&fake_oh, s);
    objects_unpin(&fake_oh, s);
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

    c = obj_string_concat(&fake_oh, s, obj_string_create(&fake_oh, "bar", 3));
    CHECK_EQ(OBJECT_SPACE(c), OBJECT_SPACE_HEAP);
    CHECK(strcmp(objects_ptr(&fake_oh, c), "foobar") == 0);
    CHECK(fake_handles()[OBJECT_INDEX(c)].flags & OBJECT_H_ASCII);

    k = objects_clone(&fake_oh, s);
    CHECK_EQ(OBJECT_SPACE(k), OBJECT_SPACE_HEAP);
    CHECK(strcmp(objects_ptr(&fake_oh, k), "foo") == 0);
    CHECK(fake_handles()[OBJECT_INDEX(k)].flags & OBJECT_H_ASCII);

    k = objects_clone(&fake_oh, a);
    CHECK_EQ(OBJECT_SPACE(k), OBJECT_SPACE_HEAP);
    CHECK_EQ(array_length(&fake_oh, k), 3);
    CHECK_EQ(*(int32_t *)array_element_ptr(&fake_oh, k, 2), 3);
    CHECK(heap_verify(&fake_oh));
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
    value_unbox(&fake_oh, b, &out, sizeof out);
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
    CHECK_EQ(objects_type_of(&fake_oh, OBJECT_ID_GEN(OBJECT_SPACE_CONST, 1, i)), 0);
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
    HARNESS_CASE(constant_string_check_accepts_only_terminated_payloads),
    HARNESS_CASE(constant_string_init_preserves_trusted_content),
    HARNESS_CASE(literal_is_read_in_place),
    HARNESS_CASE(non_ascii_literal_decodes),
    HARNESS_CASE(intern_misuse_is_corrupt),
    HARNESS_CASE(constants_are_optional),
    HARNESS_CASE(constant_array_reads_like_a_heap_array),
    HARNESS_CASE(writing_into_a_constant_is_corrupt),
    HARNESS_CASE(delete_rejects_constants_and_flat_spaces),
    HARNESS_CASE(pinning_a_constant_does_nothing),
    HARNESS_CASE(concat_and_clone_copy_constants_into_the_heap),
    HARNESS_CASE(constant_box_unboxes),
    HARNESS_CASE(constant_id_with_a_generation_is_rejected_in_debug),
    HARNESS_CASE(init_rejects_a_malformed_constant_table),
    HARNESS_CASE(init_checks_statics_before_constants),
};

HARNESS_MAIN(cases)
