#include <stdio.h>
#include <string.h>
#include "harness.h"
#include "fake_embedder.h"

#define PAYLOAD 4096u
#define HANDLES 128u
#define G       HEAP_GUARD_BYTES
#define BLOCK   (32u + G)           /* the block of a 32-byte object */

static object_id_t alloc_kind(uint16_t type, uint8_t kind, uint32_t bytes)
{
    object_id_t id = heap_alloc(OBJECT_SPACE_HEAP, type, kind, bytes);

    CHECK(id != OBJECT_NULL);
    return id;
}

static object_id_t alloc(uint32_t bytes)
{
    return alloc_kind(FAKE_T_DATA, OBJECT_K_OBJECT, bytes);
}

static const object_handle_t * handle_of(object_id_t id)
{
    return &fake_handles()[OBJECT_INDEX(id)];
}

static uint32_t offset_of(object_id_t id)
{
    return handle_of(id)->offset;
}

static heap_stats_t stats(void)
{
    heap_stats_t st;

    heap_stats(&st);
    return st;
}

static void verify(void)
{
    CHECK(heap_verify());
}

/* A, B, C, D of 32 bytes each; D keeps C away from the untouched top. */
static void layout(object_id_t id[4])
{
    int i;

    fake_init(PAYLOAD, HANDLES);
    for (i = 0; i < 4; i++)
        id[i] = alloc(32);
}

/* ---- allocation ------------------------------------------------------ */

TEST_CASE(alloc_gives_zeroed_aligned_payload)
{
    object_id_t a;
    uint8_t * p;
    uint32_t i;

    fake_init(PAYLOAD, HANDLES);
    a = alloc(40);
    p = objects_ptr(a);
    CHECK(((uintptr_t)p & 7u) == 0);
    CHECK_EQ(offset_of(a), HEAP_SENTINEL_BYTES + G);
    CHECK_EQ(objects_size_of(a), 40);
    CHECK_EQ(objects_type_of(a), FAKE_T_DATA);
    CHECK_EQ(objects_kind_of(a), OBJECT_K_OBJECT);
    CHECK_EQ(handle_of(a)->flags, OBJECT_H_INUSE);
    for (i = 0; i < 40; i++)
        CHECK_EQ(p[i], 0);

    memset(p, 0xFF, 40);
    heap_release(a);
    a = alloc(40);
    CHECK_EQ(offset_of(a), HEAP_SENTINEL_BYTES + G);
    p = objects_ptr(a);
    for (i = 0; i < 40; i++)
        CHECK_EQ(p[i], 0);
    verify();
}

TEST_CASE(hasrefs_follows_refmap_and_kind)
{
    fake_init(PAYLOAD, HANDLES);
    CHECK(!(handle_of(alloc_kind(FAKE_T_DATA, OBJECT_K_OBJECT, 8))->flags & OBJECT_H_HASREFS));
    CHECK(handle_of(alloc_kind(FAKE_T_NODE, OBJECT_K_OBJECT, 8))->flags & OBJECT_H_HASREFS);
    CHECK(handle_of(alloc_kind(FAKE_T_PAIR, OBJECT_K_ARRAY, 24))->flags & OBJECT_H_HASREFS);
    CHECK(handle_of(alloc_kind(FAKE_T_NODE, OBJECT_K_BOX, 8))->flags & OBJECT_H_HASREFS);
    CHECK(!(handle_of(alloc_kind(FAKE_T_NODE, OBJECT_K_STRING, 8))->flags & OBJECT_H_HASREFS));
    CHECK(!(handle_of(alloc_kind(FAKE_T_NODE, OBJECT_K_RAW, 8))->flags & OBJECT_H_HASREFS));
    verify();
}

TEST_CASE(zero_byte_object_has_no_block)
{
    heap_stats_t before;
    object_id_t a;

    fake_init(PAYLOAD, HANDLES);
    before = stats();
    a = alloc(0);
    CHECK_EQ(offset_of(a), 0);
    CHECK_EQ(objects_size_of(a), 0);
    CHECK_EQ(stats().used_bytes, before.used_bytes);
    CHECK_EQ(stats().live_objects, before.live_objects + 1);
    verify();
    heap_release(a);
    verify();
    CHECK_EQ(stats().live_objects, 0);
}

TEST_CASE(request_that_can_never_fit_returns_null)
{
    const uint32_t largest = PAYLOAD - HEAP_SENTINEL_BYTES - G;
    object_id_t a;

    fake_init(PAYLOAD, HANDLES);
    CHECK(heap_alloc(OBJECT_SPACE_HEAP, FAKE_T_DATA, OBJECT_K_RAW, largest + 1) == OBJECT_NULL);
    CHECK(heap_alloc(OBJECT_SPACE_HEAP, FAKE_T_DATA, OBJECT_K_RAW, PAYLOAD) == OBJECT_NULL);
    CHECK(heap_alloc(OBJECT_SPACE_HEAP, FAKE_T_DATA, OBJECT_K_RAW, 0xFFFFFFFFu) == OBJECT_NULL);
    CHECK_EQ(fake_oom_count, 0);

    a = alloc_kind(FAKE_T_DATA, OBJECT_K_RAW, largest);
    CHECK_EQ(stats().free_bytes, 0);
    verify();
    heap_release(a);
    verify();
}

TEST_CASE(max_payload_boundaries)
{
    const uint32_t largest = HEAP_MAX_PAYLOAD - HEAP_SENTINEL_BYTES - G;
    object_id_t a;
    object_id_t b;

    fake_init(HEAP_MAX_PAYLOAD, HEAP_MAX_PAYLOAD / 32);
    a = alloc_kind(FAKE_T_DATA, OBJECT_K_RAW, largest);
    CHECK_EQ(offset_of(a) + largest, HEAP_MAX_PAYLOAD);
    CHECK_EQ(stats().free_bytes, 0);
    verify();
    heap_release(a);
    verify();
    CHECK_EQ(stats().largest_free, HEAP_MAX_PAYLOAD - HEAP_SENTINEL_BYTES);

    /* The last 8 bytes of the payload are the highest offset a handle can hold. */
    a = alloc_kind(FAKE_T_DATA, OBJECT_K_RAW, largest - 8 - G);
    b = alloc_kind(FAKE_T_DATA, OBJECT_K_RAW, 8);
    CHECK_EQ(offset_of(b), HEAP_MAX_PAYLOAD - 8);
    CHECK_EQ(stats().free_bytes, 0);
    verify();
    heap_release(a);
    verify();
    heap_release(b);
    verify();
    CHECK_EQ(stats().free_blocks, 1);
}

TEST_CASE(stats_track_usage)
{
    heap_stats_t st;
    object_id_t a;

    fake_init(PAYLOAD, HANDLES);
    a = alloc(100);
    alloc(8);
    st = stats();
    CHECK_EQ(st.payload_bytes, PAYLOAD);
    CHECK_EQ(st.used_bytes, 104 + 8 + 2 * G);
    CHECK_EQ(st.live_objects, 2);
    CHECK_EQ(st.free_handles, HANDLES - 1 - 2);
    CHECK_EQ(st.histogram[0], 1);               /* 8 bytes */
    CHECK_EQ(st.histogram[4], 1);               /* 104 bytes: 64 < 104 <= 128 */
    CHECK_EQ(heap_in_use(), st.used_bytes);
    heap_release(a);
    CHECK_EQ(stats().peak_used_bytes, 104 + 8 + 2 * G);
    CHECK_EQ(heap_largest_free(), stats().largest_free);
}

/* ---- release and coalescing (doc 18 section 4) ----------------------- */

TEST_CASE(coalesce_with_neither)
{
    object_id_t id[4];
    uint32_t b;

    layout(id);
    b = offset_of(id[1]);
    heap_release(id[1]);
    verify();
    CHECK_EQ(stats().free_blocks, 2);           /* the hole and the top */
    CHECK_EQ(offset_of(alloc(32)), b);          /* exact fit */
    CHECK_EQ(stats().free_blocks, 1);
    verify();
}

TEST_CASE(coalesce_with_previous_only)
{
    object_id_t id[4];
    uint32_t a;

    layout(id);
    a = offset_of(id[0]);
    heap_release(id[0]);
    heap_release(id[1]);
    verify();
    CHECK_EQ(stats().free_blocks, 2);
    CHECK_EQ(offset_of(alloc(2 * BLOCK - G)), a);
    verify();
}

TEST_CASE(coalesce_with_next_only)
{
    object_id_t id[4];
    uint32_t a;

    layout(id);
    a = offset_of(id[0]);
    heap_release(id[1]);
    heap_release(id[0]);
    verify();
    CHECK_EQ(stats().free_blocks, 2);
    CHECK_EQ(offset_of(alloc(2 * BLOCK - G)), a);
    verify();
}

TEST_CASE(coalesce_with_both)
{
    object_id_t id[4];
    uint32_t a;

    layout(id);
    a = offset_of(id[0]);
    heap_release(id[0]);
    heap_release(id[2]);
    heap_release(id[1]);
    verify();
    CHECK_EQ(stats().free_blocks, 2);
    CHECK_EQ(offset_of(alloc(3 * BLOCK - G)), a);
    verify();
}

TEST_CASE(split_leaves_the_remainder_free)
{
    object_id_t id[4];
    uint32_t b;

    layout(id);
    b = offset_of(id[1]);
    heap_release(id[1]);
    CHECK_EQ(offset_of(alloc(8)), b);
    CHECK_EQ(stats().free_blocks, 2);           /* the remainder and the top */
    verify();
}

TEST_CASE(release_at_the_top_lowers_it)
{
    object_id_t a;
    object_id_t b;

    fake_init(PAYLOAD, HANDLES);
    a = alloc(32);
    b = alloc(32);
    heap_release(b);
    verify();
    CHECK_EQ(stats().free_blocks, 1);
    CHECK_EQ(stats().free_bytes, PAYLOAD - HEAP_SENTINEL_BYTES - BLOCK);
    heap_release(a);
    verify();
    CHECK_EQ(stats().largest_free, PAYLOAD - HEAP_SENTINEL_BYTES);
    CHECK_EQ(stats().used_bytes, 0);
}

TEST_CASE(hole_rejoins_the_top_with_its_upper_neighbour)
{
    object_id_t a;
    object_id_t b;
    object_id_t c;

    fake_init(PAYLOAD, HANDLES);
    a = alloc(32);
    b = alloc(32);
    c = alloc(32);
    heap_release(b);
    heap_release(c);
    verify();
    CHECK_EQ(stats().free_blocks, 1);
    CHECK_EQ(offset_of(alloc(32)), offset_of(a) + BLOCK);
    verify();
}

/* ---- exhaustion ends in on_oom (A10) --------------------------------- */

TEST_CASE(payload_exhaustion_ends_in_on_oom)
{
    object_id_t ids[64];
    heap_stats_t before;
    uint32_t n = 0;

    fake_init(PAYLOAD, HANDLES);
    while (stats().free_bytes >= 256 + G)
        ids[n++] = alloc(256);
    before = stats();
    FAKE_EXPECT_OOM(heap_alloc(OBJECT_SPACE_HEAP, FAKE_T_DATA, OBJECT_K_OBJECT, 256));
    CHECK_EQ(fake_oom_count, 1);
    CHECK_EQ(stats().live_objects, before.live_objects);
    CHECK_EQ(stats().free_handles, before.free_handles);
    verify();

    heap_release(ids[0]);
    CHECK(heap_alloc(OBJECT_SPACE_HEAP, FAKE_T_DATA, OBJECT_K_OBJECT, 256) != OBJECT_NULL);
    verify();
}

TEST_CASE(handle_exhaustion_ends_in_on_oom)
{
    uint32_t i;

    fake_init(PAYLOAD, 8);
    for (i = 0; i < 7; i++)
        alloc(8);
    FAKE_EXPECT_OOM(heap_alloc(OBJECT_SPACE_HEAP, FAKE_T_DATA, OBJECT_K_OBJECT, 8));
    CHECK_EQ(fake_oom_count, 1);
    CHECK_EQ(stats().used_bytes, 7 * (8 + G));  /* the failed request carved nothing */
    verify();
}

/* ---- identity (doc 18 D2; the stale-id test moved here by A9) -------- */

TEST_CASE(stale_id_caught_in_debug_only)
{
    object_id_t a;
    object_id_t d;

    fake_init(PAYLOAD, 4);                      /* handles 1-3 */
    a = alloc(16);
    heap_release(a);                            /* handle 1 rejoins at the tail */
    alloc(16);
    alloc(16);
    d = alloc(16);
    CHECK_EQ(OBJECT_INDEX(d), OBJECT_INDEX(a));
#if OBJECT_DEBUG_REFS
    CHECK(d != a);
    fake_allow_corrupt();
    CHECK(objects_ptr(a) == NULL);
    CHECK_EQ(fake_corrupt_count, 1);
#else
    CHECK(d == a);                              /* byte-identical: nothing can tell them apart */
    CHECK(objects_ptr(a) == objects_ptr(d));
    CHECK_EQ(fake_corrupt_count, 0);
#endif
}

TEST_CASE(freed_handles_are_reused_last)
{
    object_id_t a;

    fake_init(PAYLOAD, HANDLES);
    a = alloc(16);
    heap_release(a);
    CHECK(OBJECT_INDEX(alloc(16)) != OBJECT_INDEX(a));
    verify();
}

TEST_CASE(release_misuse_is_corrupt)
{
    object_id_t a;

    fake_init(PAYLOAD, HANDLES);
    a = alloc(16);
    heap_release(a);
    fake_allow_corrupt();
    heap_release(a);
    CHECK_EQ(fake_corrupt_count, 1);
    heap_release(OBJECT_NULL);
    CHECK_EQ(fake_corrupt_count, 2);
    heap_release(OBJECT_ID(OBJECT_SPACE_STATIC, 8));
    CHECK_EQ(fake_corrupt_count, 3);
    verify();
}

TEST_CASE(alloc_misuse_is_corrupt)
{
    fake_init(PAYLOAD, HANDLES);
    fake_allow_corrupt();
    CHECK(heap_alloc(OBJECT_SPACE_CONST, FAKE_T_DATA, OBJECT_K_OBJECT, 8) == OBJECT_NULL);
    CHECK(heap_alloc(OBJECT_SPACE_HEAP + 1, FAKE_T_DATA, OBJECT_K_OBJECT, 8) == OBJECT_NULL);
    CHECK(heap_alloc(OBJECT_SPACE_HEAP, FAKE_T_DATA, OBJECT_K_FREE, 8) == OBJECT_NULL);
    CHECK(heap_alloc(OBJECT_SPACE_HEAP, FAKE_T_DATA, OBJECT_K_COUNT, 8) == OBJECT_NULL);
    CHECK_EQ(fake_corrupt_count, 4);
    CHECK(objects_promote(OBJECT_ID(OBJECT_SPACE_FRAME, 8)) == OBJECT_NULL);
    CHECK_EQ(fake_corrupt_count, 5);
    verify();
}

/* ---- objects_* on top of the heap ------------------------------------ */

TEST_CASE(create_makes_a_plain_object)
{
    object_id_t a;

    fake_init(PAYLOAD, HANDLES);
    a = objects_create(FAKE_T_DATA, 40);
    CHECK_EQ(objects_kind_of(a), OBJECT_K_OBJECT);
    CHECK_EQ(objects_size_of(a), 40);
    CHECK(objects_promote(a) == a);
    verify();
}

TEST_CASE(clone_copies_the_payload)
{
    object_id_t a;
    object_id_t b;
    uint8_t * p;
    uint32_t i;

    fake_init(PAYLOAD, HANDLES);
    a = alloc_kind(FAKE_T_NODE, OBJECT_K_OBJECT, 24);
    p = objects_ptr(a);
    for (i = 0; i < 24; i++)
        p[i] = (uint8_t)(i * 7u + 1u);
    b = objects_clone(a);
    CHECK(b != OBJECT_NULL && b != a);
    CHECK_EQ(objects_type_of(b), FAKE_T_NODE);
    CHECK_EQ(objects_size_of(b), 24);
    CHECK(handle_of(b)->flags & OBJECT_H_HASREFS);
    CHECK(memcmp(objects_ptr(a), objects_ptr(b), 24) == 0);
    verify();
}

TEST_CASE(pins_nest_up_to_seven)
{
    object_id_t a;
    int i;

    fake_init(PAYLOAD, HANDLES);
    a = alloc(16);
    for (i = 0; i < 7; i++)
        CHECK(objects_pin(a) == objects_ptr(a));
    CHECK_EQ(handle_of(a)->flags & OBJECT_H_PINNED, OBJECT_H_PINNED);

    fake_allow_corrupt();
    CHECK(objects_pin(a) == objects_ptr(a));    /* refused, but the pointer stays valid */
    CHECK_EQ(fake_corrupt_count, 1);
    heap_release(a);                            /* a pinned object is never freed */
    CHECK_EQ(fake_corrupt_count, 2);
    CHECK_EQ(objects_size_of(a), 16);

    for (i = 0; i < 7; i++)
        objects_unpin(a);
    CHECK_EQ(handle_of(a)->flags & OBJECT_H_PINNED, 0);
    objects_unpin(a);
    CHECK_EQ(fake_corrupt_count, 3);
    heap_release(a);
    CHECK_EQ(fake_corrupt_count, 3);
    verify();
}

/* ---- heap_verify catches damage: layer 3 (doc 18 section 2.3) -------- */

TEST_CASE(verify_catches_a_damaged_free_block)
{
    object_id_t id[4];
    uint32_t start;

    layout(id);
    start = offset_of(id[1]) - G;
    heap_release(id[1]);
    fake_allow_corrupt();
    /* heap_free_t is { next, size } at the start of the free block (doc 14 section 5.1) */
    ((uint16_t *)(void *)(fake_payload() + start))[1] = 8;
    CHECK(!heap_verify());
    CHECK_EQ(fake_corrupt_count, 1);
}

TEST_CASE(verify_catches_a_misaligned_handle)
{
    object_id_t id[4];

    layout(id);
    fake_allow_corrupt();
    fake_handles()[OBJECT_INDEX(id[1])].offset += 4;
    CHECK(!heap_verify());
    CHECK_EQ(fake_corrupt_count, 1);
}

TEST_CASE(verify_catches_overlap_in_debug)
{
#if HEAP_DEBUG
    object_id_t id[4];

    layout(id);
    fake_allow_corrupt();
    fake_handles()[OBJECT_INDEX(id[1])].offset += 8;    /* B now overlaps C */
    CHECK(!heap_verify());
    CHECK_EQ(fake_corrupt_count, 1);
#else
    harness_skip("the exact address walk needs HEAP_DEBUG guard headers");
#endif
}

TEST_CASE(verify_catches_a_damaged_guard)
{
#if HEAP_DEBUG
    object_id_t id[4];

    layout(id);
    fake_allow_corrupt();
    fake_payload()[offset_of(id[2]) - G] ^= 0xFF;          /* B overran into C's guard */
    CHECK(!heap_verify());
    CHECK_EQ(fake_corrupt_count, 1);
    heap_release(id[2]);                                    /* refused: the guard is wrong */
    CHECK_EQ(fake_corrupt_count, 2);
#else
    harness_skip("guard headers are a debug-build feature");
#endif
}

/* ---- fragmentation instrument (doc 14 section 5.4) ------------------- */

TEST_CASE(fragmentation_ratio_is_reported)
{
    static object_id_t ids[512];
    heap_stats_t st;
    uint32_t n = 0;
    uint32_t i;

    fake_init(PAYLOAD, 512);
    while (stats().free_bytes >= BLOCK && n < 512)
        ids[n++] = alloc(32);
    for (i = 1; i + 1 < n; i += 2)
        heap_release(ids[i]);
    verify();
    st = stats();
    printf("fragmentation: largest free %lu of %lu free bytes, ratio %.3f\n",
           (unsigned long)st.largest_free, (unsigned long)st.free_bytes,
           (double)st.largest_free / (double)st.free_bytes);
    CHECK_EQ(st.largest_free, BLOCK);
    CHECK(st.free_bytes > 10 * BLOCK);
}

/* ---- seeded torture at the maximum payload (doc 18 section 4) -------- */

#define TORTURE_OPS     50000u
#define TORTURE_FULL    10000u          /* heap_verify after every one of the first ops */
#define TORTURE_LIVE    2047u

static struct {
    object_id_t id[TORTURE_LIVE];
    uint8_t     fill[TORTURE_LIVE];
    uint32_t    live;
    uint32_t    seed;
    uint32_t    state;
    uint32_t    op;
} torture;

static uint32_t torture_rand(void)
{
    torture.state = torture.state * 1103515245u + 12345u;
    return torture.state >> 8;
}

static void torture_dump(void)
{
    fprintf(stderr, "torture: seed %lu, op %lu, live %lu\n", (unsigned long)torture.seed,
            (unsigned long)torture.op, (unsigned long)torture.live);
    fake_dump();
}

/* Each object holds one byte value throughout: an overlap between two blocks shows here. */
static void torture_check(uint32_t k)
{
    const uint8_t * p = objects_ptr(torture.id[k]);
    uint32_t n = objects_size_of(torture.id[k]);
    uint32_t i;

    for (i = 0; i < n; i++)
        CHECK_EQ(p[i], torture.fill[k]);
}

static void torture_run(uint32_t seed)
{
    heap_stats_t st;

    fake_init(HEAP_MAX_PAYLOAD, HEAP_MAX_PAYLOAD / 32);
    harness_set_dump(torture_dump);
    torture.seed = torture.state = seed;
    torture.live = 0;
    for (torture.op = 0; torture.op < TORTURE_OPS; torture.op++) {
        if (torture.live == 0 || (torture.live < TORTURE_LIVE && torture_rand() % 8 < 5)) {
            fake_oom_armed = 1;
            if (setjmp(fake_oom_jmp) == 0) {
                uint32_t bytes = torture_rand() % 300;
                object_id_t id = heap_alloc(OBJECT_SPACE_HEAP, FAKE_T_DATA, OBJECT_K_OBJECT, bytes);

                fake_oom_armed = 0;
                CHECK(id != OBJECT_NULL);
                torture.id[torture.live] = id;
                torture.fill[torture.live] = (uint8_t)(torture_rand() | 1u);
                memset(objects_ptr(id), torture.fill[torture.live], bytes);
                torture.live++;
            }
        } else {
            uint32_t k = torture_rand() % torture.live;

            torture_check(k);
            heap_release(torture.id[k]);
            torture.live--;
            torture.id[k] = torture.id[torture.live];
            torture.fill[k] = torture.fill[torture.live];
        }
        if (torture.op < TORTURE_FULL || torture.op % 97 == 0)
            CHECK(heap_verify());
    }
    while (torture.live > 0) {
        torture_check(torture.live - 1);
        heap_release(torture.id[--torture.live]);
    }
    CHECK(heap_verify());
    st = stats();
    printf("torture seed %lu: %lu allocations ended in on_oom\n",
           (unsigned long)seed, (unsigned long)fake_oom_count);
    CHECK(fake_oom_count > 0);                  /* the run did reach a full heap */
    CHECK_EQ(st.live_objects, 0);
    CHECK_EQ(st.used_bytes, 0);
    CHECK_EQ(st.free_blocks, 1);
    CHECK_EQ(st.largest_free, HEAP_MAX_PAYLOAD - HEAP_SENTINEL_BYTES);
}

TEST_CASE(torture_seed_1)
{
    torture_run(1);
}

TEST_CASE(torture_seed_20260928)
{
    torture_run(20260928u);
}

static const harness_case_t cases[] = {
    HARNESS_CASE(alloc_gives_zeroed_aligned_payload),
    HARNESS_CASE(hasrefs_follows_refmap_and_kind),
    HARNESS_CASE(zero_byte_object_has_no_block),
    HARNESS_CASE(request_that_can_never_fit_returns_null),
    HARNESS_CASE(max_payload_boundaries),
    HARNESS_CASE(stats_track_usage),
    HARNESS_CASE(coalesce_with_neither),
    HARNESS_CASE(coalesce_with_previous_only),
    HARNESS_CASE(coalesce_with_next_only),
    HARNESS_CASE(coalesce_with_both),
    HARNESS_CASE(split_leaves_the_remainder_free),
    HARNESS_CASE(release_at_the_top_lowers_it),
    HARNESS_CASE(hole_rejoins_the_top_with_its_upper_neighbour),
    HARNESS_CASE(payload_exhaustion_ends_in_on_oom),
    HARNESS_CASE(handle_exhaustion_ends_in_on_oom),
    HARNESS_CASE(stale_id_caught_in_debug_only),
    HARNESS_CASE(freed_handles_are_reused_last),
    HARNESS_CASE(release_misuse_is_corrupt),
    HARNESS_CASE(alloc_misuse_is_corrupt),
    HARNESS_CASE(create_makes_a_plain_object),
    HARNESS_CASE(clone_copies_the_payload),
    HARNESS_CASE(pins_nest_up_to_seven),
    HARNESS_CASE(verify_catches_a_damaged_free_block),
    HARNESS_CASE(verify_catches_a_misaligned_handle),
    HARNESS_CASE(verify_catches_overlap_in_debug),
    HARNESS_CASE(verify_catches_a_damaged_guard),
    HARNESS_CASE(fragmentation_ratio_is_reported),
    HARNESS_CASE(torture_seed_1),
    HARNESS_CASE(torture_seed_20260928),
};

HARNESS_MAIN(cases)
