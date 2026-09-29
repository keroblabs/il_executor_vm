#include <string.h>
#include "harness.h"
#include "fake_embedder.h"

#define PAYLOAD 4096u
#define HANDLES 128u

static void expect_init(obj_handler_status_t expected)
{
    CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, &fake_host), expected);
    CHECK(!obj_handler_is_ready(&fake_oh));
}

static void expect_valid_init(void)
{
    fake_config(PAYLOAD, HANDLES);
    CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, &fake_host), OBJ_HANDLER_OK);
    CHECK(obj_handler_is_ready(&fake_oh));
}

/* ---- ids ------------------------------------------------------------- */

TEST_CASE(id_round_trip_every_space)
{
    static const uint16_t indices[] = { 0, 1, 0x7FFF, 0x8000, 0xFFFF };
    uint32_t space;
    size_t k;

    for (space = 0; space < OBJECT_SPACE_COUNT; space++) {
        for (k = 0; k < sizeof indices / sizeof indices[0]; k++) {
            object_id_t id = OBJECT_ID(space, indices[k]);

            CHECK_EQ(OBJECT_SPACE(id), space);
            CHECK_EQ(OBJECT_INDEX(id), indices[k]);
            CHECK_EQ(OBJECT_GEN(id), 0);
        }
    }
    CHECK(OBJECT_ID(OBJECT_SPACE_HEAP, 0) == OBJECT_NULL);
}

TEST_CASE(id_generation_split)
{
    object_id_t id = OBJECT_ID_GEN(OBJECT_SPACE_STATIC, 0xA5, 0x1234);

    CHECK_EQ(OBJECT_SPACE(id), OBJECT_SPACE_STATIC);
    CHECK_EQ(OBJECT_INDEX(id), 0x1234);
#if OBJECT_DEBUG_REFS
    CHECK_EQ(OBJECT_GEN(id), 0xA5);
    CHECK_EQ(id, 0x11A51234u);
#else
    CHECK_EQ(OBJECT_GEN(id), 0);
    CHECK_EQ(id, 0x00111234u);
#endif
}

/* ---- obj_handler_init: one case per status code (doc 14 section 9.2) - */

TEST_CASE(init_ok)
{
    expect_valid_init();
    CHECK(fake_oh.spaces[OBJECT_SPACE_HEAP].handles == fake_handles());
    CHECK(fake_oh.spaces[OBJECT_SPACE_HEAP].base == fake_payload());
    CHECK_EQ(fake_oh.spaces[OBJECT_SPACE_HEAP].limit, HANDLES);
    CHECK(fake_oh.spaces[OBJECT_SPACE_STATIC].base == fake_statics());
    CHECK_EQ(fake_oh.spaces[OBJECT_SPACE_STATIC].limit, FAKE_STATICS_BYTES);
    CHECK_EQ(fake_oh.spaces[OBJECT_SPACE_CONST].limit, 0);  /* the default fake has no constants */
}

TEST_CASE(init_arg_null)
{
    fake_config(PAYLOAD, HANDLES);
    CHECK_EQ(obj_handler_init(NULL, &fake_cfg, &fake_host), OBJ_HANDLER_E_ARG_NULL);
    CHECK_EQ(obj_handler_init(&fake_oh, NULL, &fake_host), OBJ_HANDLER_E_ARG_NULL);
    CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, NULL), OBJ_HANDLER_E_ARG_NULL);
    expect_valid_init();
}

TEST_CASE(init_already_initialised)
{
    expect_valid_init();
    CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, &fake_host), OBJ_HANDLER_E_ALREADY_INIT);
    CHECK(obj_handler_is_ready(&fake_oh));
    obj_handler_shutdown(&fake_oh);
    CHECK(!obj_handler_is_ready(&fake_oh));
    expect_valid_init();
}

TEST_CASE(init_segments)
{
    fake_config(PAYLOAD, HANDLES);
    fake_cfg.segments = 0;
    expect_init(OBJ_HANDLER_E_SEGMENTS);
    fake_cfg.segments = HEAP_SEGMENTS + 1;
    expect_init(OBJ_HANDLER_E_SEGMENTS);
    expect_valid_init();
}

TEST_CASE(init_payload_base)
{
    fake_config(PAYLOAD, HANDLES);
    fake_cfg.segment[0].payload = NULL;
    expect_init(OBJ_HANDLER_E_PAYLOAD_BASE);
    fake_cfg.segment[0].payload = fake_payload() + 4;
    expect_init(OBJ_HANDLER_E_PAYLOAD_BASE);
    expect_valid_init();
}

TEST_CASE(init_payload_size)
{
    static const uint32_t bad[] = { 0, 8, 15, 4100, HEAP_MAX_PAYLOAD + 8 };
    static const uint32_t good[] = { 16, HEAP_MAX_PAYLOAD };
    size_t k;

    for (k = 0; k < sizeof bad / sizeof bad[0]; k++) {
        fake_config(bad[k], HANDLES);
        expect_init(OBJ_HANDLER_E_PAYLOAD_SIZE);
    }
    for (k = 0; k < sizeof good / sizeof good[0]; k++) {
        fake_config(good[k], HANDLES);
        CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, &fake_host), OBJ_HANDLER_OK);
        obj_handler_shutdown(&fake_oh);
    }
}

TEST_CASE(init_handles)
{
    static const uint32_t bad[] = { 0, 1, 65537 };
    static const uint32_t good[] = { 2, 65536 };
    size_t k;

    fake_config(PAYLOAD, HANDLES);
    fake_cfg.segment[0].handles = NULL;
    expect_init(OBJ_HANDLER_E_HANDLES);
    for (k = 0; k < sizeof bad / sizeof bad[0]; k++) {
        fake_config(PAYLOAD, bad[k]);
        expect_init(OBJ_HANDLER_E_HANDLES);
    }
    for (k = 0; k < sizeof good / sizeof good[0]; k++) {
        fake_config(PAYLOAD, good[k]);
        CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, &fake_host), OBJ_HANDLER_OK);
        obj_handler_shutdown(&fake_oh);
    }
}

TEST_CASE(init_worklist)
{
    fake_config(PAYLOAD, HANDLES);
    fake_cfg.worklist = NULL;
    expect_init(OBJ_HANDLER_E_WORKLIST);
    fake_config(PAYLOAD, HANDLES);
    fake_cfg.worklist_len = 0;
    expect_init(OBJ_HANDLER_E_WORKLIST);
    expect_valid_init();
}

TEST_CASE(init_host_callback)
{
    obj_handler_host_t host;

    fake_config(PAYLOAD, HANDLES);
    host = fake_host;
    host.type_refmap = NULL;
    CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, &host), OBJ_HANDLER_E_HOST_CALLBACK);
    host = fake_host;
    host.on_no_memory = NULL;
    CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, &host), OBJ_HANDLER_E_HOST_CALLBACK);
    host = fake_host;
    host.on_corrupt = NULL;
    CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, &host), OBJ_HANDLER_E_HOST_CALLBACK);
    CHECK(!obj_handler_is_ready(&fake_oh));
    expect_valid_init();
}

TEST_CASE(init_checks_in_enum_order)
{
    obj_handler_host_t host = fake_host;

    host.on_no_memory = NULL;
    fake_config(PAYLOAD, HANDLES);
    fake_cfg.segments = 0;
    fake_cfg.worklist = NULL;
    CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, &host), OBJ_HANDLER_E_SEGMENTS);

    fake_config(PAYLOAD, HANDLES);
    fake_cfg.segment[0].payload = NULL;
    fake_cfg.segment[0].handles = NULL;
    CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, &host), OBJ_HANDLER_E_PAYLOAD_BASE);

    fake_config(PAYLOAD, HANDLES);
    fake_cfg.segment[0].handles = NULL;
    fake_cfg.worklist = NULL;
    CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, &host), OBJ_HANDLER_E_HANDLES);
}

TEST_CASE(init_failure_writes_nothing)
{
    obj_handler_host_t host = fake_host;
    const uint8_t * handle_bytes = (const uint8_t *)fake_handles();
    uint32_t i;

    fake_config(PAYLOAD, HANDLES);
    memset(fake_payload(), 0xA5, PAYLOAD);
    memset(fake_handles(), 0xA5, HANDLES * sizeof(object_handle_t));
    host.on_corrupt = NULL;             /* the last check, so every other one ran */
    CHECK_EQ(obj_handler_init(&fake_oh, &fake_cfg, &host), OBJ_HANDLER_E_HOST_CALLBACK);

    for (i = 0; i < PAYLOAD; i++)
        CHECK_EQ(fake_payload()[i], 0xA5);
    for (i = 0; i < HANDLES * sizeof(object_handle_t); i++)
        CHECK_EQ(handle_bytes[i], 0xA5);
    CHECK_EQ(fake_oh.spaces[OBJECT_SPACE_HEAP].limit, 0);
    CHECK(fake_oh.host.on_corrupt == NULL);
    expect_valid_init();
}

/* ---- resolution (doc 14 sections 3.3 and 3.4) ------------------------ */

static object_id_t hand_built_handle(uint16_t index, uint16_t offset, uint16_t size)
{
    object_handle_t * h = &fake_handles()[index];

    h->offset = offset;
    h->size   = size;
    h->type   = FAKE_T_DATA;
    h->kind   = OBJECT_K_OBJECT;
    h->flags  = OBJECT_H_INUSE;
#if OBJECT_DEBUG_REFS
    h->gen    = 3;
#endif
    return OBJECT_ID_GEN(OBJECT_SPACE_HEAP, 3, index);
}

TEST_CASE(resolve_heap_handle)
{
    object_id_t id;

    fake_init(PAYLOAD, HANDLES);
    id = hand_built_handle(5, 64, 16);
    CHECK(objects_ptr(&fake_oh, id) == fake_payload() + 64);
    CHECK_EQ(objects_type_of(&fake_oh, id), FAKE_T_DATA);
    CHECK_EQ(objects_size_of(&fake_oh, id), 16);
    CHECK_EQ(objects_kind_of(&fake_oh, id), OBJECT_K_OBJECT);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(resolve_flat_space_and_byref)
{
    object_byref_t b;

    fake_init(PAYLOAD, HANDLES);
    CHECK(objects_ptr(&fake_oh, OBJECT_ID(OBJECT_SPACE_STATIC, 40)) == fake_statics() + 40);

    b.id = OBJECT_ID(OBJECT_SPACE_STATIC, 40);
    b.offset = 8;
    b._pad = 0;
    CHECK(objects_byref_ptr(&fake_oh, b) == fake_statics() + 48);
    CHECK_EQ(fake_corrupt_count, 0);
}

TEST_CASE(indirect_space_is_corrupt)
{
    object_space_t desc = { NULL, NULL, 16, 0 };

    fake_init(PAYLOAD, HANDLES);
    objects_space_register(&fake_oh, OBJECT_SPACE_NATIVE, &desc);
    fake_allow_corrupt();
    CHECK(objects_ptr(&fake_oh, OBJECT_ID(OBJECT_SPACE_NATIVE, 3)) == NULL);
    CHECK_EQ(fake_corrupt_count, 1);
}

/* Checked resolution exists in debug builds only; release resolves blind (doc 14 section 3.4). */
#if OBJECT_DEBUG_REFS
static void expect_rejected(object_id_t id)
{
    unsigned before = fake_corrupt_count;

    CHECK(objects_ptr(&fake_oh, id) == NULL);
    CHECK_EQ(fake_corrupt_count, before + 1);
}
#define DEBUG_ONLY_BEGIN    fake_init(PAYLOAD, HANDLES); fake_allow_corrupt();
#define DEBUG_ONLY_END
#else
#define DEBUG_ONLY_BEGIN    harness_skip("checked resolution is a debug-build feature"); if (0) {
#define DEBUG_ONLY_END      }
#define expect_rejected(id) ((void)(id))
#endif

TEST_CASE(debug_rejects_null_id)
{
    DEBUG_ONLY_BEGIN
    expect_rejected(OBJECT_NULL);
    DEBUG_ONLY_END
}

TEST_CASE(debug_rejects_index_out_of_range)
{
    DEBUG_ONLY_BEGIN
    expect_rejected(OBJECT_ID_GEN(OBJECT_SPACE_HEAP, 1, HANDLES));
    expect_rejected(OBJECT_ID_GEN(OBJECT_SPACE_HEAP, 1, 0xFFFF));
    DEBUG_ONLY_END
}

TEST_CASE(debug_rejects_unregistered_space)
{
    DEBUG_ONLY_BEGIN
    expect_rejected(OBJECT_ID(OBJECT_SPACE_CONST, 8));
    expect_rejected(OBJECT_ID(OBJECT_SPACE_COUNT, 8));
    expect_rejected(OBJECT_ID(0xFF, 8));
    DEBUG_ONLY_END
}

TEST_CASE(debug_rejects_handle_not_in_use)
{
    DEBUG_ONLY_BEGIN
    expect_rejected(OBJECT_ID_GEN(OBJECT_SPACE_HEAP, 1, 7));
    DEBUG_ONLY_END
}

TEST_CASE(debug_rejects_wrong_generation)
{
    DEBUG_ONLY_BEGIN
    object_id_t id = hand_built_handle(9, 128, 8);

    CHECK(objects_ptr(&fake_oh, id) == fake_payload() + 128);
    expect_rejected(OBJECT_ID_GEN(OBJECT_SPACE_HEAP, 2, 9));
    expect_rejected(OBJECT_ID(OBJECT_SPACE_HEAP, 9));
    DEBUG_ONLY_END
}

/* ---- accessors and registration ------------------------------------- */

TEST_CASE(accessors_reject_non_objects)
{
    object_id_t flat = OBJECT_ID(OBJECT_SPACE_STATIC, 8);  /* a static field, not an object */

    fake_init(PAYLOAD, HANDLES);
    fake_allow_corrupt();
    CHECK_EQ(objects_type_of(&fake_oh, flat), 0);
    CHECK_EQ(objects_size_of(&fake_oh, flat), 0);
    CHECK_EQ(objects_kind_of(&fake_oh, flat), OBJECT_K_FREE);
    CHECK_EQ(fake_corrupt_count, 3);
    CHECK_EQ(objects_type_of(&fake_oh, OBJECT_NULL), 0);
    CHECK_EQ(objects_type_of(&fake_oh, OBJECT_ID_GEN(OBJECT_SPACE_HEAP, 1, 7)), 0);
    CHECK_EQ(fake_corrupt_count, 5);
}

TEST_CASE(space_register_rejects_misuse)
{
    static uint8_t slab[64];
    static object_handle_t table[4];
    object_space_t ok      = { slab, NULL, sizeof slab, 0 };
    object_space_t handled = { slab, table, 4, 0 };
    object_space_t empty   = { slab, NULL, 0, 0 };
    object_space_t huge    = { slab, NULL, 65537, 0 };

    fake_init(PAYLOAD, HANDLES);
    fake_allow_corrupt();
    objects_space_register(&fake_oh, OBJECT_SPACE_COUNT, &ok);
    objects_space_register(&fake_oh, OBJECT_SPACE_HEAP, &ok);
    /* init configures CONST and STATIC */
    objects_space_register(&fake_oh, OBJECT_SPACE_CONST, &ok);
    objects_space_register(&fake_oh, OBJECT_SPACE_STATIC, &ok);
    objects_space_register(&fake_oh, OBJECT_SPACE_FRAME, &ok);
    objects_space_register(&fake_oh, OBJECT_SPACE_NATIVE, NULL);
    objects_space_register(&fake_oh, OBJECT_SPACE_NATIVE, &handled);
    objects_space_register(&fake_oh, OBJECT_SPACE_NATIVE, &empty);
    objects_space_register(&fake_oh, OBJECT_SPACE_NATIVE, &huge);
    CHECK_EQ(fake_corrupt_count, 9);
    CHECK_EQ(fake_oh.spaces[OBJECT_SPACE_NATIVE].limit, 0);

    objects_space_register(&fake_oh, OBJECT_SPACE_NATIVE, &ok);
    CHECK_EQ(fake_corrupt_count, 9);
    CHECK_EQ(fake_oh.spaces[OBJECT_SPACE_NATIVE].limit, sizeof slab);
    objects_space_register(&fake_oh, OBJECT_SPACE_NATIVE, &ok);
    CHECK_EQ(fake_corrupt_count, 10);
}

TEST_CASE(space_register_before_init_is_ignored)
{
    static uint8_t slab[64];
    object_space_t ok = { slab, NULL, sizeof slab, 0 };

    objects_space_register(&fake_oh, OBJECT_SPACE_NATIVE, &ok);
    CHECK_EQ(fake_oh.spaces[OBJECT_SPACE_NATIVE].limit, 0);
    fake_init(PAYLOAD, HANDLES);
    CHECK_EQ(fake_oh.spaces[OBJECT_SPACE_NATIVE].limit, 0);
}

TEST_CASE(write_ref_and_escape_are_identity)
{
    object_id_t slot = OBJECT_NULL;
    object_id_t id = OBJECT_ID_GEN(OBJECT_SPACE_HEAP, 1, 42);

    objects_write_ref(&fake_oh, &slot, id);
    CHECK(slot == id);
    CHECK(objects_escape(&fake_oh, id) == id);
}

static const harness_case_t cases[] = {
    HARNESS_CASE(id_round_trip_every_space),
    HARNESS_CASE(id_generation_split),
    HARNESS_CASE(init_ok),
    HARNESS_CASE(init_arg_null),
    HARNESS_CASE(init_already_initialised),
    HARNESS_CASE(init_segments),
    HARNESS_CASE(init_payload_base),
    HARNESS_CASE(init_payload_size),
    HARNESS_CASE(init_handles),
    HARNESS_CASE(init_worklist),
    HARNESS_CASE(init_host_callback),
    HARNESS_CASE(init_checks_in_enum_order),
    HARNESS_CASE(init_failure_writes_nothing),
    HARNESS_CASE(resolve_heap_handle),
    HARNESS_CASE(resolve_flat_space_and_byref),
    HARNESS_CASE(indirect_space_is_corrupt),
    HARNESS_CASE(debug_rejects_null_id),
    HARNESS_CASE(debug_rejects_index_out_of_range),
    HARNESS_CASE(debug_rejects_unregistered_space),
    HARNESS_CASE(debug_rejects_handle_not_in_use),
    HARNESS_CASE(debug_rejects_wrong_generation),
    HARNESS_CASE(accessors_reject_non_objects),
    HARNESS_CASE(space_register_rejects_misuse),
    HARNESS_CASE(space_register_before_init_is_ignored),
    HARNESS_CASE(write_ref_and_escape_are_identity),
};

HARNESS_MAIN(cases)
