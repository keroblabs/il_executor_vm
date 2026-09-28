#include <string.h>
#include "obj_handler.h"

obj_handler_host_t obj_handler_host;

static int obj_handler_ready;

int obj_handler_is_ready(void)
{
    return obj_handler_ready;
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

    /* Every check has passed; nothing is written before this point. */
    obj_handler_host = *host;
    memset(object_spaces, 0, sizeof object_spaces);
    for (i = 0; i < cfg->segments; i++)
        heap_segment_init(i, cfg->segment[i].payload, cfg->segment[i].payload_size,
                          cfg->segment[i].handles, cfg->segment[i].handle_count,
                          cfg->segment[i].gc_threshold_pct, cfg->segment[i].gc_handle_pct);
    obj_handler_ready = 1;
    return OBJ_HANDLER_OK;
}

void obj_handler_shutdown(void)
{
    heap_shutdown();
    memset(object_spaces, 0, sizeof object_spaces);
    memset(&obj_handler_host, 0, sizeof obj_handler_host);
    obj_handler_ready = 0;
}
