#include <stdio.h>
#include <string.h>
#include "harness.h"
#include "fake_embedder.h"

/* A second application beside fake_oh, with regions of its own (doc 14 section 1). */

#define PAYLOAD     4096u
#define HANDLES     128u
#define B_PAYLOAD   1024u
#define B_HANDLES   16u

static union { uint64_t align; uint8_t bytes[B_PAYLOAD]; } b_payload;
static object_handle_t b_handles[B_HANDLES];
static uint16_t        b_worklist[8];
static obj_handler_t   b_oh;

static void b_config(obj_handler_config_t * cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->segment[0].payload      = b_payload.bytes;
    cfg->segment[0].payload_size = B_PAYLOAD;
    cfg->segment[0].handles      = b_handles;
    cfg->segment[0].handle_count = B_HANDLES;
    cfg->segments                = 1;
    cfg->worklist                = b_worklist;
    cfg->worklist_len            = sizeof b_worklist / sizeof b_worklist[0];
    cfg->string_type             = FAKE_T_STRING;
}

static void b_init(void)
{
    obj_handler_config_t cfg;

    b_config(&cfg);
    CHECK_EQ(obj_handler_init(&b_oh, &cfg, &fake_host), OBJ_HANDLER_OK);
}

static int inside(const void * p, const uint8_t * base, uint32_t size)
{
    return (const uint8_t *)p >= base && (const uint8_t *)p < base + size;
}

TEST_CASE(instances_share_nothing)
{
    heap_stats_t st;
    object_id_t a;
    object_id_t b;

    fake_init(PAYLOAD, HANDLES);
    b_init();
    a = objects_create(&fake_oh, FAKE_T_DATA, 16);
    b = objects_create(&b_oh, FAKE_T_DATA, 16);
    CHECK(a == b);                              /* one id, a different object in each */
    CHECK(inside(objects_ptr(&fake_oh, a), fake_payload(), PAYLOAD));
    CHECK(inside(objects_ptr(&b_oh, b), b_payload.bytes, B_PAYLOAD));
    memset(objects_ptr(&fake_oh, a), 0xAA, 16);
    memset(objects_ptr(&b_oh, b), 0xBB, 16);
    CHECK_EQ(((const uint8_t *)objects_ptr(&fake_oh, a))[15], 0xAA);
    CHECK_EQ(((const uint8_t *)objects_ptr(&b_oh, b))[15], 0xBB);

    heap_release(&fake_oh, a);
    CHECK_EQ(objects_size_of(&b_oh, b), 16);
    heap_stats(&b_oh, &st);
    CHECK_EQ(st.payload_bytes, B_PAYLOAD);
    CHECK_EQ(st.live_objects, 1);
    heap_stats(&fake_oh, &st);
    CHECK_EQ(st.payload_bytes, PAYLOAD);
    CHECK_EQ(st.live_objects, 0);
    CHECK(heap_verify(&fake_oh));
    CHECK(heap_verify(&b_oh));
}

TEST_CASE(callbacks_receive_their_instance)
{
    uint32_t i;

    fake_init(PAYLOAD, HANDLES);
    b_init();
    objects_create(&b_oh, FAKE_T_NODE, 8);
    CHECK(fake_refmap_oh == &b_oh);
    objects_create(&fake_oh, FAKE_T_NODE, 8);
    CHECK(fake_refmap_oh == &fake_oh);

    fake_allow_corrupt();
    heap_release(&b_oh, OBJECT_NULL);
    CHECK(fake_corrupt_oh == &b_oh);
    heap_release(&fake_oh, OBJECT_NULL);
    CHECK(fake_corrupt_oh == &fake_oh);

    for (i = 0; i < B_HANDLES - 2; i++)         /* handle 0 is reserved and one is in use */
        objects_create(&b_oh, FAKE_T_DATA, 8);
    FAKE_EXPECT_OOM(objects_create(&b_oh, FAKE_T_DATA, 8));
    CHECK(fake_oom_oh == &b_oh);
    CHECK(objects_create(&fake_oh, FAKE_T_DATA, 8) != OBJECT_NULL);
}

TEST_CASE(shutdown_of_one_leaves_the_other)
{
    obj_handler_config_t cfg;
    object_id_t s;

    fake_init(PAYLOAD, HANDLES);
    b_init();
    s = obj_string_create(&b_oh, "kept", 4);
    b_config(&cfg);
    CHECK_EQ(obj_handler_init(&b_oh, &cfg, &fake_host), OBJ_HANDLER_E_ALREADY_INIT);

    obj_handler_shutdown(&fake_oh);
    CHECK(!obj_handler_is_ready(&fake_oh));
    CHECK(obj_handler_is_ready(&b_oh));
    CHECK(strcmp(objects_ptr(&b_oh, s), "kept") == 0);
    CHECK(heap_verify(&b_oh));

    fake_start();
    CHECK(strcmp(objects_ptr(&b_oh, s), "kept") == 0);
    CHECK(heap_verify(&fake_oh));
}

/* ---- two applications, interleaved one operation at a time ---------- */

#define MIX_OPS     20000u
#define MIX_LIVE    64u

typedef struct {
    obj_handler_t * oh;
    object_id_t     id[MIX_LIVE];
    uint8_t         fill[MIX_LIVE];
    uint32_t        live;
    uint32_t        ooms;
} mix_app_t;

static mix_app_t mix[2];
static uint32_t  mix_state;
static uint32_t  mix_op;

static uint32_t mix_rand(void)
{
    mix_state = mix_state * 1103515245u + 12345u;
    return mix_state >> 8;
}

static void mix_dump(void)
{
    fprintf(stderr, "interleaving: op %lu, live %lu and %lu\n", (unsigned long)mix_op,
            (unsigned long)mix[0].live, (unsigned long)mix[1].live);
    fake_dump();
}

/* Each object holds one byte value throughout: a write into the other instance shows here. */
static void mix_check(const mix_app_t * app, uint32_t k)
{
    const uint8_t * p = objects_ptr(app->oh, app->id[k]);
    uint32_t n = objects_size_of(app->oh, app->id[k]);
    uint32_t i;

    for (i = 0; i < n; i++)
        CHECK_EQ(p[i], app->fill[k]);
}

static void mix_step(mix_app_t * app)
{
    if (app->live == 0 || (app->live < MIX_LIVE && mix_rand() % 2 == 0)) {
        uint32_t bytes = mix_rand() % 48;
        object_id_t id;

        fake_oom_armed = 1;
        if (setjmp(fake_oom_jmp) != 0) {
            app->ooms++;
            return;
        }
        id = objects_create(app->oh, FAKE_T_DATA, bytes);
        fake_oom_armed = 0;
        CHECK(id != OBJECT_NULL);
        app->id[app->live] = id;
        app->fill[app->live] = (uint8_t)(mix_rand() | 1u);
        memset(objects_ptr(app->oh, id), app->fill[app->live], bytes);
        app->live++;
    } else {
        uint32_t k = mix_rand() % app->live;

        mix_check(app, k);
        heap_release(app->oh, app->id[k]);
        app->live--;
        app->id[k] = app->id[app->live];
        app->fill[k] = app->fill[app->live];
    }
}

TEST_CASE(interleaved_instances_stay_consistent)
{
    uint32_t a;
    uint32_t k;

    fake_init(PAYLOAD, HANDLES);
    b_init();
    harness_set_dump(mix_dump);
    mix[0].oh = &fake_oh;
    mix[1].oh = &b_oh;
    mix_state = 20260928u;
    for (mix_op = 0; mix_op < MIX_OPS; mix_op++) {
        mix_app_t * app = &mix[mix_rand() % 2];

        mix_step(app);
        CHECK(heap_verify(app->oh));
        if (mix_op % 97 == 0)
            for (a = 0; a < 2; a++)
                for (k = 0; k < mix[a].live; k++)
                    mix_check(&mix[a], k);
    }
    for (a = 0; a < 2; a++) {
        heap_stats_t st;

        while (mix[a].live > 0) {
            mix_check(&mix[a], mix[a].live - 1);
            heap_release(mix[a].oh, mix[a].id[--mix[a].live]);
        }
        CHECK(heap_verify(mix[a].oh));
        heap_stats(mix[a].oh, &st);
        CHECK_EQ(st.live_objects, 0);
        CHECK_EQ(st.used_bytes, 0);
    }
    printf("interleaving: allocations ending in on_oom: %lu and %lu\n",
           (unsigned long)mix[0].ooms, (unsigned long)mix[1].ooms);
    CHECK(mix[1].ooms > 0);                     /* one application ran full beside the other */
}

static const harness_case_t cases[] = {
    HARNESS_CASE(instances_share_nothing),
    HARNESS_CASE(callbacks_receive_their_instance),
    HARNESS_CASE(shutdown_of_one_leaves_the_other),
    HARNESS_CASE(interleaved_instances_stay_consistent),
};

HARNESS_MAIN(cases)
