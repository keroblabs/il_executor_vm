#include <string.h>
#include "obj_handler.h"

obj_handler_host_t obj_handler_host;

static int obj_handler_ready;

int obj_handler_is_ready(void)
{
    return obj_handler_ready;
}

static int obj_handler_statics_ok(const obj_handler_config_t * cfg)
{
    if (cfg->statics_size == 0)
        return 1;
    if (cfg->statics == NULL || ((uintptr_t)cfg->statics & 7u) != 0 || cfg->statics_size < 16u
        || cfg->statics_size > 65536u || (cfg->statics_size & 7u) != 0)
        return 0;
    /* Bytes 0-7 are reserved, so words 0 and 1 never hold an id. */
    return cfg->statics_refmap == NULL || (cfg->statics_refmap[0] & 0x03u) == 0;
}

static int obj_handler_const_ok(const obj_handler_config_t * cfg, const obj_handler_host_t * host,
                                const object_const_t * c, uint32_t table_bytes)
{
    const uint8_t * p;
    uint32_t words = 0;

    if (c->kind == OBJECT_K_FREE || c->kind >= OBJECT_K_COUNT
        || (c->flags & ~OBJECT_H_ASCII) != 0
        || ((c->flags & OBJECT_H_ASCII) && c->kind != OBJECT_K_STRING))
        return 0;
    if ((c->offset & 7u) != 0 || c->offset < table_bytes || c->offset > cfg->const_size
        || c->size > cfg->const_size - c->offset)
        return 0;
    p = cfg->const_base + c->offset;
    if (c->kind == OBJECT_K_STRING)
        return c->size != 0 && p[c->size - 1u] == 0;
    if (c->kind == OBJECT_K_RAW)
        return 1;
    if (c->kind == OBJECT_K_ARRAY) {
        const array_header_t * a = (const array_header_t *)(const void *)p;

        if (c->size < sizeof *a || a->elem_size == 0
            || (uint32_t)sizeof *a + (uint32_t)a->count * a->elem_size != c->size)
            return 0;
    }
    /* A constant never holds an id (doc 14 section 3.5). */
    return host->type_refmap(c->type, &words) == NULL;
}

static int obj_handler_consts_ok(const obj_handler_config_t * cfg, const obj_handler_host_t * host)
{
    const object_const_t * table = (const object_const_t *)(const void *)cfg->const_base;
    uint32_t table_bytes;
    uint32_t i;

    if (cfg->const_count == 0)
        return 1;
    if (cfg->const_base == NULL || ((uintptr_t)cfg->const_base & 7u) != 0
        || cfg->const_count > 65536u)
        return 0;
    table_bytes = cfg->const_count * (uint32_t)sizeof *table;
    if (cfg->const_size < table_bytes)
        return 0;
    for (i = 1; i < cfg->const_count; i++)
        if (!obj_handler_const_ok(cfg, host, &table[i], table_bytes))
            return 0;
    return 1;
}

obj_handler_status_t obj_handler_init(const obj_handler_config_t * cfg,
                                      const obj_handler_host_t   * host)
{
    uint32_t i;

    if (cfg == NULL || host == NULL)
        return OBJ_HANDLER_E_ARG_NULL;
    if (obj_handler_ready)
        return OBJ_HANDLER_E_ALREADY_INIT;
    if (cfg->segments == 0 || cfg->segments > HEAP_SEGMENTS)
        return OBJ_HANDLER_E_SEGMENTS;
    for (i = 0; i < cfg->segments; i++)
        if (cfg->segment[i].payload == NULL || ((uintptr_t)cfg->segment[i].payload & 7u) != 0)
            return OBJ_HANDLER_E_PAYLOAD_BASE;
    for (i = 0; i < cfg->segments; i++)
        if (cfg->segment[i].payload_size < 16 || cfg->segment[i].payload_size > HEAP_MAX_PAYLOAD
            || (cfg->segment[i].payload_size & 7u) != 0)
            return OBJ_HANDLER_E_PAYLOAD_SIZE;
    for (i = 0; i < cfg->segments; i++)
        if (cfg->segment[i].handles == NULL || cfg->segment[i].handle_count < 2
            || cfg->segment[i].handle_count > 65536u)
            return OBJ_HANDLER_E_HANDLES;
    if (cfg->worklist == NULL || cfg->worklist_len == 0)
        return OBJ_HANDLER_E_WORKLIST;
    if (host->type_refmap == NULL || host->on_oom == NULL || host->on_corrupt == NULL)
        return OBJ_HANDLER_E_HOST_CALLBACK;
    if (!obj_handler_statics_ok(cfg))
        return OBJ_HANDLER_E_STATICS;
    if (!obj_handler_consts_ok(cfg, host))
        return OBJ_HANDLER_E_CONST;

    /* Every check has passed; nothing is written before this point. */
    obj_handler_host = *host;
    objects_init(cfg->statics, cfg->statics_size, cfg->const_base, cfg->const_count);
    for (i = 0; i < cfg->segments; i++)
        heap_segment_init(i, cfg->segment[i].payload, cfg->segment[i].payload_size,
                          cfg->segment[i].handles, cfg->segment[i].handle_count,
                          cfg->segment[i].gc_threshold_pct, cfg->segment[i].gc_handle_pct);
    obj_string_init(cfg->string_type);
    obj_handler_ready = 1;
    return OBJ_HANDLER_OK;
}

void obj_handler_shutdown(void)
{
    heap_shutdown();
    obj_string_init(0);
    objects_shutdown();
    memset(&obj_handler_host, 0, sizeof obj_handler_host);
    obj_handler_ready = 0;
}
