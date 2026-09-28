#include <stdio.h>
#include <string.h>
#include "fake_embedder.h"

obj_handler_config_t fake_cfg;

jmp_buf      fake_oom_jmp;
int          fake_oom_armed;
unsigned     fake_oom_count;

unsigned     fake_corrupt_count;
const char * fake_corrupt_where;
object_id_t  fake_corrupt_id;

static int fake_corrupt_allowed;

static union { uint64_t align; uint8_t bytes[HEAP_MAX_PAYLOAD]; } fake_payload_mem;
static object_handle_t fake_handle_mem[FAKE_MAX_HANDLES];
static uint16_t        fake_worklist_mem[64];

static union { uint64_t align; uint8_t bytes[FAKE_STATICS_BYTES]; } fake_statics_mem;
static const uint8_t fake_statics_map[FAKE_STATICS_BYTES / 32] = { 0x0C };

static union { uint64_t align; uint8_t bytes[FAKE_CONST_BYTES]; } fake_const_mem;
static uint32_t fake_const_count;
static uint32_t fake_const_used;

static const uint8_t fake_map_node[] = { 0x01 };
static const uint8_t fake_map_pair[] = { 0x03 };

static const uint8_t * fake_type_refmap(uint16_t type, uint32_t * words)
{
    switch (type) {
    case FAKE_T_NODE:
        *words = 1;
        return fake_map_node;
    case FAKE_T_PAIR:
        *words = 2;
        return fake_map_pair;
    case FAKE_T_MIXED:
        *words = 2;
        return fake_map_node;
    default:
        *words = 0;
        return NULL;
    }
}

static void fake_on_oom(void)
{
    fake_oom_count++;
    if (fake_oom_armed) {
        fake_oom_armed = 0;
        longjmp(fake_oom_jmp, 1);
    }
    harness_fail(__FILE__, __LINE__, "unexpected on_oom");
}

static void fake_on_corrupt(const char * where, object_id_t id)
{
    fake_corrupt_count++;
    fake_corrupt_where = where;
    fake_corrupt_id = id;
    if (!fake_corrupt_allowed) {
        fprintf(stderr, "on_corrupt: %s (id 0x%08lx)\n", where, (unsigned long)id);
        harness_fail(__FILE__, __LINE__, "unexpected on_corrupt");
    }
}

obj_handler_host_t fake_host = { fake_type_refmap, fake_on_oom, fake_on_corrupt };

void fake_dump(void)
{
    heap_stats_t st;
    uint32_t i;
    uint32_t shown = 0;

    heap_stats(&st);
    fprintf(stderr, "---- heap: used %lu, free %lu in %lu blocks, largest %lu, live %lu ----\n",
            (unsigned long)st.used_bytes, (unsigned long)st.free_bytes,
            (unsigned long)st.free_blocks, (unsigned long)st.largest_free,
            (unsigned long)st.live_objects);
    fprintf(stderr, "---- handles in use ----\n");
    for (i = 1; i < fake_cfg.segment[0].handle_count && i < FAKE_MAX_HANDLES; i++) {
        const object_handle_t * h = &fake_handle_mem[i];

        if (!(h->flags & OBJECT_H_INUSE))
            continue;
        fprintf(stderr, "  [%5lu] offset %5u size %5u type %3u kind %u flags 0x%02x\n",
                (unsigned long)i, h->offset, h->size, h->type, h->kind, h->flags);
        if (++shown == 64) {
            fprintf(stderr, "  ...\n");
            break;
        }
    }
}

void fake_config(uint32_t payload_size, uint32_t handle_count)
{
    memset(&fake_cfg, 0, sizeof fake_cfg);
    fake_cfg.segment[0].payload          = fake_payload_mem.bytes;
    fake_cfg.segment[0].payload_size     = payload_size;
    fake_cfg.segment[0].handles          = fake_handle_mem;
    fake_cfg.segment[0].handle_count     = handle_count;
    fake_cfg.segment[0].gc_threshold_pct = 75;
    fake_cfg.segment[0].gc_handle_pct    = 75;
    fake_cfg.segments                    = 1;
    fake_cfg.gc_recover_pct              = 25;
    fake_cfg.gc_damp_step_pct            = 10;
    fake_cfg.gc_damp_cap_pct             = 90;
    fake_cfg.worklist                    = fake_worklist_mem;
    fake_cfg.worklist_len                = sizeof fake_worklist_mem / sizeof fake_worklist_mem[0];
    fake_cfg.statics                     = fake_statics_mem.bytes;
    fake_cfg.statics_size                = FAKE_STATICS_BYTES;
    fake_cfg.statics_refmap              = fake_statics_map;
    fake_cfg.string_type                 = FAKE_T_STRING;
}

void fake_start(void)
{
    CHECK_EQ(obj_handler_init(&fake_cfg, &fake_host), OBJ_HANDLER_OK);
    harness_set_dump(fake_dump);
}

void fake_init(uint32_t payload_size, uint32_t handle_count)
{
    fake_config(payload_size, handle_count);
    fake_start();
}

void fake_consts_begin(void)
{
    memset(&fake_const_mem, 0, sizeof fake_const_mem);
    fake_const_count = 1;
    fake_const_used = FAKE_CONST_MAX * (uint32_t)sizeof(object_const_t);
}

uint16_t fake_const_add(uint16_t type, uint8_t kind, uint8_t flags,
                        const void * payload, uint32_t size)
{
    object_const_t * c;

    CHECK(fake_const_count < FAKE_CONST_MAX && fake_const_used + size <= FAKE_CONST_BYTES);
    c = fake_const_entry((uint16_t)fake_const_count);
    c->offset = fake_const_used;
    c->size   = size;
    c->type   = type;
    c->kind   = kind;
    c->flags  = flags;
    if (size != 0)
        memcpy(fake_const_mem.bytes + fake_const_used, payload, size);
    fake_const_used = (fake_const_used + size + 7u) & ~7u;
    return (uint16_t)fake_const_count++;
}

uint16_t fake_const_string(const char * utf8, uint32_t len)
{
    char buf[256];
    uint8_t flags = OBJECT_H_ASCII;
    uint32_t i;

    CHECK(len < sizeof buf);
    for (i = 0; i < len; i++)
        if ((unsigned char)utf8[i] >= 0x80)
            flags = 0;
    memcpy(buf, utf8, len);
    buf[len] = '\0';
    return fake_const_add(FAKE_T_STRING, OBJECT_K_STRING, flags, buf, len + 1u);
}

uint16_t fake_const_array(uint16_t type, uint16_t elem_size, uint16_t count,
                          const void * elements)
{
    uint8_t buf[512];
    array_header_t hdr;
    const uint32_t bytes = (uint32_t)count * elem_size;

    CHECK(sizeof hdr + bytes <= sizeof buf);
    memset(&hdr, 0, sizeof hdr);
    hdr.count = count;
    hdr.elem_size = elem_size;
    memcpy(buf, &hdr, sizeof hdr);
    if (bytes != 0)
        memcpy(buf + sizeof hdr, elements, bytes);
    return fake_const_add(type, OBJECT_K_ARRAY, 0, buf, (uint32_t)sizeof hdr + bytes);
}

void fake_consts_install(void)
{
    fake_cfg.const_base  = fake_const_mem.bytes;
    fake_cfg.const_count = fake_const_count;
    fake_cfg.const_size  = fake_const_used;
}

object_const_t * fake_const_entry(uint16_t index)
{
    return (object_const_t *)(void *)fake_const_mem.bytes + index;
}

uint8_t * fake_const_bytes(void)
{
    return fake_const_mem.bytes;
}

uint8_t * fake_statics(void)
{
    return fake_statics_mem.bytes;
}

void fake_allow_corrupt(void)
{
    fake_corrupt_allowed = 1;
}

uint8_t * fake_payload(void)
{
    return fake_payload_mem.bytes;
}

object_handle_t * fake_handles(void)
{
    return fake_handle_mem;
}
