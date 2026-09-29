#include <string.h>
#include "harness.h"
#include "fake_embedder.h"

#define PAYLOAD 4096u
#define HANDLES 128u

static const object_handle_t * handle_of(object_id_t id)
{
    return &fake_handles()[OBJECT_INDEX(id)];
}

static object_id_t make_array(uint16_t type, uint16_t elem_size, uint32_t count)
{
    object_id_t id = array_create(&fake_oh, type, elem_size, count);

    CHECK(id != OBJECT_NULL);
    return id;
}

static void fill_ints(object_id_t a)
{
    int32_t * e = array_range_ptr(&fake_oh, a, 0, array_length(&fake_oh, a));
    uint32_t i;

    for (i = 0; i < array_length(&fake_oh, a); i++)
        e[i] = (int32_t)i;
}

static void expect_ints(object_id_t a, const int32_t * want, uint32_t n)
{
    const int32_t * e = array_range_ptr(&fake_oh, a, 0, n);
    uint32_t i;

    CHECK(e != NULL);
    for (i = 0; i < n; i++)
        CHECK_EQ(e[i], want[i]);
}

/* ---- arrays (doc 14 section 5.7) ------------------------------------- */

TEST_CASE(array_create_writes_the_header)
{
    const array_header_t * hdr;
    const uint8_t * p;
    object_id_t a;
    uint32_t i;

    fake_init(PAYLOAD, HANDLES);
    a = make_array(FAKE_T_DATA, 1, 10);
    hdr = objects_ptr(&fake_oh, a);
    CHECK_EQ(hdr->count, 10);
    CHECK_EQ(hdr->elem_size, 1);
    CHECK_EQ(array_length(&fake_oh, a), 10);
    CHECK_EQ(objects_kind_of(&fake_oh, a), OBJECT_K_ARRAY);
    CHECK_EQ(objects_size_of(&fake_oh, a), sizeof(array_header_t) + 10);
    CHECK(!(handle_of(a)->flags & OBJECT_H_HASREFS));      /* a byte[] is never scanned */
    p = array_range_ptr(&fake_oh, a, 0, 10);
    for (i = 0; i < 10; i++)
        CHECK_EQ(p[i], 0);
    CHECK(heap_verify(&fake_oh));
}

TEST_CASE(array_hasrefs_follows_the_element_refmap)
{
    fake_init(PAYLOAD, HANDLES);
    CHECK(handle_of(make_array(FAKE_T_NODE, 4, 3))->flags & OBJECT_H_HASREFS);
    CHECK(handle_of(make_array(FAKE_T_MIXED, 8, 3))->flags & OBJECT_H_HASREFS);
    CHECK(!(handle_of(make_array(FAKE_T_DATA, 4, 3))->flags & OBJECT_H_HASREFS));
}

TEST_CASE(array_zero_length)
{
    object_id_t a;

    fake_init(PAYLOAD, HANDLES);
    a = make_array(FAKE_T_DATA, 4, 0);
    CHECK_EQ(array_length(&fake_oh, a), 0);
    CHECK_EQ(objects_size_of(&fake_oh, a), sizeof(array_header_t));
    CHECK(array_range_ptr(&fake_oh, a, 0, 0) != NULL);
    CHECK(array_element_ptr(&fake_oh, a, 0) == NULL);
    CHECK(array_element_byref(&fake_oh, a, 0).id == OBJECT_NULL);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(array_range_check_is_in_elements)
{
    object_id_t a;

    fake_init(PAYLOAD, HANDLES);
    a = make_array(FAKE_T_DATA, 4, 5);
    CHECK(array_range_ptr(&fake_oh, a, 0, 5) != NULL);
    CHECK(array_range_ptr(&fake_oh, a, 5, 0) != NULL);
    CHECK(array_range_ptr(&fake_oh, a, 4, 1) != NULL);
    CHECK(array_range_ptr(&fake_oh, a, 5, 1) == NULL);
    CHECK(array_range_ptr(&fake_oh, a, 0, 6) == NULL);
    CHECK(array_range_ptr(&fake_oh, a, 4, 2) == NULL);
    CHECK(array_range_ptr(&fake_oh, a, 0xFFFFFFFFu, 1) == NULL);
    CHECK(array_range_ptr(&fake_oh, a, 1, 0xFFFFFFFFu) == NULL);   /* index + count wraps */
    CHECK(array_element_ptr(&fake_oh, a, 5) == NULL);
    CHECK(array_element_ptr(&fake_oh, a, 0xFFFFFFFFu) == NULL);
    CHECK_EQ(fake_corrupt_count, 0);                        /* the program's error, not the embedder's */
}

TEST_CASE(array_create_rejects_what_never_fits)
{
    heap_stats_t st;

    fake_init(HEAP_MAX_PAYLOAD, HEAP_MAX_PAYLOAD / 32);
    CHECK(array_create(&fake_oh, FAKE_T_DATA, 1, 0x10000u) == OBJECT_NULL);
    CHECK(array_create(&fake_oh, FAKE_T_DATA, 4, 0x40000001u) == OBJECT_NULL); /* 4 * count wraps */
    CHECK(array_create(&fake_oh, FAKE_T_DATA, 8, 0xFFFFFFFFu) == OBJECT_NULL);
    CHECK(array_create(&fake_oh, FAKE_T_DATA, 0xFFFF, 2) == OBJECT_NULL);
    CHECK_EQ(fake_no_memory_count, 0);

    /* The largest byte[] fills the payload exactly. */
    make_array(FAKE_T_DATA, 1, HEAP_MAX_PAYLOAD - HEAP_SENTINEL_BYTES - HEAP_GUARD_BYTES
                               - (uint32_t)sizeof(array_header_t));
    heap_stats(&fake_oh, &st);
    CHECK_EQ(st.free_bytes, 0);
    CHECK(heap_verify(&fake_oh));
}

TEST_CASE(array_int64_and_double_elements_are_aligned)
{
    object_id_t a;
    object_id_t d;
    uint32_t i;

    fake_init(PAYLOAD, HANDLES);
    a = make_array(FAKE_T_DATA, 8, 7);
    d = make_array(FAKE_T_DATA, 8, 5);
    for (i = 0; i < 7; i++) {
        int64_t * e = array_element_ptr(&fake_oh, a, i);

        CHECK(((uintptr_t)e & 7u) == 0);
        *e = (int64_t)i * -1234567890123LL;
    }
    for (i = 0; i < 5; i++) {
        double * e = array_element_ptr(&fake_oh, d, i);

        CHECK(((uintptr_t)e & 7u) == 0);
        *e = i * 0.5 - 3.25;
    }
    for (i = 0; i < 7; i++)
        CHECK(*(int64_t *)array_element_ptr(&fake_oh, a, i) == (int64_t)i * -1234567890123LL);
    for (i = 0; i < 5; i++)
        CHECK(*(double *)array_element_ptr(&fake_oh, d, i) == i * 0.5 - 3.25);
}

TEST_CASE(array_byref_resolves_like_element_ptr)
{
    object_id_t a;
    uint32_t i;

    fake_init(PAYLOAD, HANDLES);
    a = make_array(FAKE_T_MIXED, 8, 6);
    for (i = 0; i < 6; i++) {
        object_byref_t b = array_element_byref(&fake_oh, a, i);

        CHECK(b.id == a);
        CHECK(objects_byref_ptr(&fake_oh, b) == array_element_ptr(&fake_oh, a, i));
    }
    CHECK(array_element_byref(&fake_oh, a, 6).id == OBJECT_NULL);
    CHECK(array_element_byref(&fake_oh, a, 0xFFFFFFFFu).id == OBJECT_NULL);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(array_copy_overlaps_in_both_directions)
{
    static const int32_t up[]   = { 0, 1, 0, 1, 2, 3, 4, 7, 8, 9 };
    static const int32_t down[] = { 3, 4, 5, 6, 7, 5, 6, 7, 8, 9 };
    object_id_t a;
    int k;

    fake_init(PAYLOAD, HANDLES);
    for (k = 0; k < 2; k++) {       /* int[] moves by memmove; object[] through the barrier */
        a = make_array(k == 0 ? FAKE_T_DATA : FAKE_T_NODE, 4, 10);
        fill_ints(a);
        array_copy(&fake_oh, a, 2, a, 0, 5);
        expect_ints(a, up, 10);
        fill_ints(a);
        array_copy(&fake_oh, a, 0, a, 3, 5);
        expect_ints(a, down, 10);
    }
    CHECK(heap_verify(&fake_oh));
}

TEST_CASE(array_copy_between_arrays)
{
    static const int32_t want[] = { 0, 0, 1, 2, 3, 0 };
    object_id_t s;
    object_id_t d;

    fake_init(PAYLOAD, HANDLES);
    s = make_array(FAKE_T_DATA, 4, 4);
    fill_ints(s);
    d = make_array(FAKE_T_DATA, 4, 6);
    array_copy(&fake_oh, d, 1, s, 0, 4);
    expect_ints(d, want, 6);
}

TEST_CASE(array_clear_zeroes_a_range)
{
    static const int32_t want[] = { 0, 1, 0, 0, 0, 5 };
    object_id_t a;

    fake_init(PAYLOAD, HANDLES);
    a = make_array(FAKE_T_DATA, 4, 6);
    fill_ints(a);
    array_clear(&fake_oh, a, 2, 3);
    expect_ints(a, want, 6);
}

TEST_CASE(array_misuse_is_corrupt)
{
    object_id_t a;
    object_id_t b;
    object_id_t o;

    fake_init(PAYLOAD, HANDLES);
    a = make_array(FAKE_T_DATA, 4, 4);
    b = make_array(FAKE_T_DATA, 8, 4);
    o = objects_create(&fake_oh, FAKE_T_DATA, 16);
    fake_allow_corrupt();
    CHECK(array_create(&fake_oh, FAKE_T_DATA, 0, 4) == OBJECT_NULL);
    CHECK_EQ(array_length(&fake_oh, o), 0);
    CHECK_EQ(array_length(&fake_oh, OBJECT_NULL), 0);
    CHECK(array_range_ptr(&fake_oh, o, 0, 1) == NULL);
    array_copy(&fake_oh, a, 0, b, 0, 1);    /* element sizes differ */
    array_copy(&fake_oh, a, 3, a, 0, 2);    /* destination range */
    array_copy(&fake_oh, a, 0, a, 3, 2);    /* source range */
    array_clear(&fake_oh, a, 2, 3);
    CHECK_EQ(fake_corrupt_count, 8);
}

TEST_CASE(array_refmap_must_cover_the_element)
{
#if OBJECT_DEBUG_REFS
    fake_init(PAYLOAD, HANDLES);
    fake_allow_corrupt();
    CHECK(array_create(&fake_oh, FAKE_T_NODE, 8, 2) == OBJECT_NULL); /* the refmap covers 4 bytes */
    CHECK_EQ(fake_corrupt_count, 1);
    CHECK(array_create(&fake_oh, FAKE_T_NODE, 4, 2) != OBJECT_NULL);
#else
    harness_skip("the element refmap check is a debug-build feature");
#endif
}

/* ---- strings (doc 14 section 5.6, D9) -------------------------------- */

static object_id_t make_string(const char * s)
{
    object_id_t id = obj_string_create(&fake_oh, s, (uint32_t)strlen(s));

    CHECK(id != OBJECT_NULL);
    return id;
}

TEST_CASE(string_create_ascii)
{
    object_id_t s;

    fake_init(PAYLOAD, HANDLES);
    s = make_string("hello");
    CHECK_EQ(obj_string_length(&fake_oh, s), 5);
    CHECK_EQ(obj_string_char_at(&fake_oh, s, 0), 'h');
    CHECK_EQ(obj_string_char_at(&fake_oh, s, 4), 'o');
    CHECK_EQ(obj_string_char_at(&fake_oh, s, 5), -1);
    CHECK_EQ(obj_string_char_at(&fake_oh, s, 0xFFFFFFFFu), -1);
    CHECK(strcmp(objects_ptr(&fake_oh, s), "hello") == 0);  /* NUL-terminated for C */
    CHECK_EQ(objects_size_of(&fake_oh, s), 6);
    CHECK_EQ(objects_type_of(&fake_oh, s), FAKE_T_STRING);
    CHECK_EQ(objects_kind_of(&fake_oh, s), OBJECT_K_STRING);
    CHECK(handle_of(s)->flags & OBJECT_H_ASCII);
    CHECK(!(handle_of(s)->flags & OBJECT_H_HASREFS));
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(string_type_comes_from_the_config)
{
    fake_config(PAYLOAD, HANDLES);
    fake_cfg.string_type = 77;
    CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, &fake_host), OBJ_HANDLER_OK);
    CHECK_EQ(objects_type_of(&fake_oh, make_string("x")), 77);
}

TEST_CASE(string_empty)
{
    object_id_t a;
    object_id_t b;

    fake_init(PAYLOAD, HANDLES);
    a = obj_string_create(&fake_oh, "", 0);
    b = obj_string_create(&fake_oh, NULL, 0);
    CHECK(a != OBJECT_NULL && b != OBJECT_NULL);
    CHECK_EQ(obj_string_length(&fake_oh, a), 0);
    CHECK_EQ(obj_string_length(&fake_oh, b), 0);
    CHECK_EQ(((const char *)objects_ptr(&fake_oh, a))[0], 0);
    CHECK_EQ(obj_string_char_at(&fake_oh, a, 0), -1);
}

typedef struct {
    const char * bytes;
    uint32_t     len;
    int          valid;
    uint32_t     units;         /* UTF-16 length, when valid */
    const char * what;
} utf8_row_t;

static const utf8_row_t utf8_rows[] = {
    { "",                   0, 1, 0, "empty" },
    { "abc",                3, 1, 3, "ASCII" },
    { "a\0b",               3, 1, 3, "an embedded NUL" },
    { "\xC3\xA9",           2, 1, 1, "U+00E9, two bytes" },
    { "\xE2\x82\xAC",       3, 1, 1, "U+20AC, three bytes" },
    { "\xEF\xBF\xBF",       3, 1, 1, "U+FFFF" },
    { "\xF0\x9F\x98\x80",   4, 1, 2, "U+1F600, a surrogate pair" },
    { "\xF4\x8F\xBF\xBF",   4, 1, 2, "U+10FFFF, the last code point" },
    { "\xC0\xAF",           2, 0, 0, "overlong, two bytes (C0)" },
    { "\xC1\xBF",           2, 0, 0, "overlong, two bytes (C1)" },
    { "\xE0\x80\xAF",       3, 0, 0, "overlong, three bytes" },
    { "\xF0\x80\x80\xAF",   4, 0, 0, "overlong, four bytes" },
    { "\xC3",               1, 0, 0, "truncated, two bytes" },
    { "\xE2\x82",           2, 0, 0, "truncated, three bytes" },
    { "\xF0\x9F\x98",       3, 0, 0, "truncated, four bytes" },
    { "\xED\xA0\x80",       3, 0, 0, "the surrogate U+D800" },
    { "\xED\xBF\xBF",       3, 0, 0, "the surrogate U+DFFF" },
    { "\xF4\x90\x80\x80",   4, 0, 0, "U+110000, above the range" },
    { "\xF5\x80\x80\x80",   4, 0, 0, "lead byte F5" },
    { "\x80",               1, 0, 0, "a lone continuation byte" },
    { "\xFF",               1, 0, 0, "byte FF" },
    { "a\xC3" "b",          3, 0, 0, "a continuation byte replaced by ASCII" },
};

static int all_ascii(const char * s, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++)
        if ((unsigned char)s[i] >= 0x80)
            return 0;
    return 1;
}

TEST_CASE(string_utf8_validation_table)
{
    size_t k;

    fake_init(PAYLOAD, HANDLES);
    for (k = 0; k < sizeof utf8_rows / sizeof utf8_rows[0]; k++) {
        const utf8_row_t * r = &utf8_rows[k];
        object_id_t id = obj_string_create(&fake_oh, r->bytes, r->len);

        if (!r->valid) {
            if (id != OBJECT_NULL)
                harness_fail(__FILE__, __LINE__, r->what);
            continue;
        }
        if (id == OBJECT_NULL)
            harness_fail(__FILE__, __LINE__, r->what);
        CHECK_EQ(obj_string_length(&fake_oh, id), r->units);
        CHECK_EQ(objects_size_of(&fake_oh, id), r->len + 1);
        CHECK_EQ((handle_of(id)->flags & OBJECT_H_ASCII) != 0, all_ascii(r->bytes, r->len));
    }
    CHECK_EQ(fake_corrupt_count, 0);
    CHECK_EQ(fake_no_memory_count, 0);
}

TEST_CASE(string_char_at_counts_utf16_units)
{
    /* a, U+00E9, U+20AC, U+1F600, b */
    static const char text[] = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80" "b";
    static const int32_t units[] = { 0x61, 0xE9, 0x20AC, 0xD83D, 0xDE00, 0x62 };
    object_id_t s;
    uint32_t i;

    fake_init(PAYLOAD, HANDLES);
    s = obj_string_create(&fake_oh, text, sizeof text - 1);
    CHECK(s != OBJECT_NULL);
    CHECK(!(handle_of(s)->flags & OBJECT_H_ASCII));
    CHECK_EQ(obj_string_length(&fake_oh, s), 6);
    for (i = 0; i < 6; i++)
        CHECK_EQ(obj_string_char_at(&fake_oh, s, i), units[i]);
    CHECK_EQ(obj_string_char_at(&fake_oh, s, 6), -1);
}

TEST_CASE(string_concat)
{
    object_id_t a;
    object_id_t c;

    fake_init(PAYLOAD, HANDLES);
    a = make_string("foo");
    c = obj_string_concat(&fake_oh, a, make_string("bar"));
    CHECK(c != OBJECT_NULL);
    CHECK(strcmp(objects_ptr(&fake_oh, c), "foobar") == 0);
    CHECK_EQ(obj_string_length(&fake_oh, c), 6);
    CHECK(handle_of(c)->flags & OBJECT_H_ASCII);

    c = obj_string_concat(&fake_oh, a, obj_string_create(&fake_oh, "\xC3\xA9", 2));
    CHECK_EQ(obj_string_length(&fake_oh, c), 4);
    CHECK(!(handle_of(c)->flags & OBJECT_H_ASCII));
    CHECK_EQ(obj_string_char_at(&fake_oh, c, 3), 0xE9);

    c = obj_string_concat(&fake_oh, make_string(""), a);
    CHECK(strcmp(objects_ptr(&fake_oh, c), "foo") == 0);
    CHECK(strcmp(objects_ptr(&fake_oh, a), "foo") == 0);    /* the operands are unchanged */
    CHECK(heap_verify(&fake_oh));
}

TEST_CASE(string_that_can_never_fit_returns_null)
{
    static char big[40000];
    object_id_t s;

    fake_init(HEAP_MAX_PAYLOAD, HEAP_MAX_PAYLOAD / 32);
    memset(big, 'a', sizeof big);
    CHECK(obj_string_create(&fake_oh, big, HEAP_MAX_PAYLOAD) == OBJECT_NULL);  /* not even read */
    CHECK(obj_string_create(&fake_oh, big, 0xFFFFFFFFu) == OBJECT_NULL);
    s = obj_string_create(&fake_oh, big, sizeof big);
    CHECK(s != OBJECT_NULL);
    CHECK(obj_string_concat(&fake_oh, s, s) == OBJECT_NULL);  /* 80,000 bytes */
    CHECK_EQ(fake_no_memory_count, 0);
    CHECK_EQ(obj_string_length(&fake_oh, s), sizeof big);
}

TEST_CASE(string_misuse_is_corrupt)
{
    object_id_t s;
    object_id_t o;

    fake_init(PAYLOAD, HANDLES);
    s = make_string("x");
    o = objects_create(&fake_oh, FAKE_T_DATA, 8);
    fake_allow_corrupt();
    CHECK_EQ(obj_string_length(&fake_oh, o), 0);
    CHECK_EQ(obj_string_char_at(&fake_oh, o, 0), -1);
    CHECK(obj_string_concat(&fake_oh, o, s) == OBJECT_NULL);
    CHECK(obj_string_concat(&fake_oh, s, OBJECT_NULL) == OBJECT_NULL);
    CHECK(obj_string_create(&fake_oh, NULL, 3) == OBJECT_NULL);
    CHECK_EQ(fake_corrupt_count, 5);
}

TEST_CASE(string_clone_keeps_ascii)
{
    object_id_t s;
    object_id_t c;

    fake_init(PAYLOAD, HANDLES);
    s = make_string("clone me");
    c = objects_clone(&fake_oh, s);
    CHECK(c != OBJECT_NULL && c != s);
    CHECK(handle_of(c)->flags & OBJECT_H_ASCII);
    CHECK(strcmp(objects_ptr(&fake_oh, c), "clone me") == 0);
    CHECK_EQ(obj_string_length(&fake_oh, c), 8);
}

static const harness_case_t cases[] = {
    HARNESS_CASE(array_create_writes_the_header),
    HARNESS_CASE(array_hasrefs_follows_the_element_refmap),
    HARNESS_CASE(array_zero_length),
    HARNESS_CASE(array_range_check_is_in_elements),
    HARNESS_CASE(array_create_rejects_what_never_fits),
    HARNESS_CASE(array_int64_and_double_elements_are_aligned),
    HARNESS_CASE(array_byref_resolves_like_element_ptr),
    HARNESS_CASE(array_copy_overlaps_in_both_directions),
    HARNESS_CASE(array_copy_between_arrays),
    HARNESS_CASE(array_clear_zeroes_a_range),
    HARNESS_CASE(array_misuse_is_corrupt),
    HARNESS_CASE(array_refmap_must_cover_the_element),
    HARNESS_CASE(string_create_ascii),
    HARNESS_CASE(string_type_comes_from_the_config),
    HARNESS_CASE(string_empty),
    HARNESS_CASE(string_utf8_validation_table),
    HARNESS_CASE(string_char_at_counts_utf16_units),
    HARNESS_CASE(string_concat),
    HARNESS_CASE(string_that_can_never_fit_returns_null),
    HARNESS_CASE(string_misuse_is_corrupt),
    HARNESS_CASE(string_clone_keeps_ascii),
};

HARNESS_MAIN(cases)
