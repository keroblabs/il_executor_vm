#ifndef HEAP_H
#define HEAP_H

#include "objects_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HEAP_SENTINEL_BYTES     8u      /* payload bytes 0-7 are never allocated (doc 18 section 4) */
#if HEAP_DEBUG
#define HEAP_GUARD_BYTES        8u      /* guard header before every live block */
#else
#define HEAP_GUARD_BYTES        0u
#endif

typedef struct {
    uint32_t payload_bytes;
    uint32_t used_bytes;            /* live blocks, guard headers included */
    uint32_t free_bytes;            /* free list plus the untouched top */
    uint32_t largest_free;          /* with free_bytes, the fragmentation ratio (doc 14 section 5.4) */
    uint32_t free_blocks;           /* the untouched top counts as one */
    uint32_t live_objects;
    uint32_t free_handles;
    uint32_t peak_used_bytes;
    uint32_t histogram[HEAP_HISTOGRAM_BUCKETS];
} heap_stats_t;

object_id_t heap_alloc(obj_handler_t * oh, uint16_t space, uint16_t type, uint8_t kind,
                       uint32_t bytes);
void        heap_stats(obj_handler_t * oh, heap_stats_t * out);
uint32_t    heap_in_use(obj_handler_t * oh);
uint32_t    heap_largest_free(obj_handler_t * oh);
/* 1: consistent; 0: the first fault went to on_corrupt. */
int         heap_verify(obj_handler_t * oh);

/* Called by the sweep and by tests; an embedder never frees (doc 18 section 4). */
void        heap_release(obj_handler_t * oh, object_id_t id);

/* Library-internal: called by obj_handler_init and obj_handler_shutdown. */
void        heap_segment_init(obj_handler_t * oh, uint32_t seg,
                              uint8_t * payload, uint32_t payload_size,
                              object_handle_t * handles, uint32_t handle_count,
                              uint8_t gc_threshold_pct, uint8_t gc_handle_pct);
void        heap_shutdown(obj_handler_t * oh);

#ifdef __cplusplus
}
#endif

#endif /* HEAP_H */
