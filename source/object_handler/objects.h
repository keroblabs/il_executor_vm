#ifndef OBJECTS_H
#define OBJECTS_H

#include "objects_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void *      objects_resolve_indirect(obj_handler_t * oh, object_id_t id);
#if OBJECT_DEBUG_REFS
void *      objects_ptr_checked(obj_handler_t * oh, object_id_t id);
#endif

/* Valid until deletion or the next safepoint (doc 14 sections 2.1.1 and 8). */
static inline void * objects_ptr(obj_handler_t * oh, object_id_t id)
{
#if OBJECT_DEBUG_REFS
    return objects_ptr_checked(oh, id);
#else
    const object_space_t * sp = &oh->spaces[OBJECT_SPACE(id)];

    if (OBJECT_LIKELY(sp->handles != NULL))
        return sp->base + sp->handles[OBJECT_INDEX(id)].offset;
    if (OBJECT_LIKELY(sp->base != NULL))
        return sp->base + OBJECT_INDEX(id);
    return objects_resolve_indirect(oh, id);
#endif
}

static inline void * objects_byref_ptr(obj_handler_t * oh, object_byref_t b)
{
    uint8_t * base = (uint8_t *)objects_ptr(oh, b.id);

    return base != NULL ? base + b.offset : NULL;
}

uint16_t    objects_type_of(obj_handler_t * oh, object_id_t id);
uint32_t    objects_size_of(obj_handler_t * oh, object_id_t id);
uint8_t     objects_kind_of(obj_handler_t * oh, object_id_t id);

/* May collect: every live id must be reachable from a root first (doc 14 section 7.3). */
object_id_t objects_create(obj_handler_t * oh, uint16_t type, uint32_t bytes);
object_id_t objects_clone(obj_handler_t * oh, object_id_t id);
void        objects_delete(obj_handler_t * oh, object_id_t id);
object_id_t objects_promote(obj_handler_t * oh, object_id_t id);

/* Identity in v1; tier 2 turns this pair into the escape barrier (doc 14 section 6.0). */
static inline object_id_t objects_escape(obj_handler_t * oh, object_id_t id)
{
    (void)oh;
    return id;
}

static inline void objects_write_ref(obj_handler_t * oh, object_id_t * slot, object_id_t value)
{
    (void)oh;
    *slot = value;
}

void *      objects_pin(obj_handler_t * oh, object_id_t id);
void        objects_unpin(obj_handler_t * oh, object_id_t id);
void        objects_space_register(obj_handler_t * oh, uint16_t tag, const object_space_t * desc);

/* Library-internal. */
object_handle_t * objects_handle_of(obj_handler_t * oh, object_id_t id, const char * where);
void        objects_corrupt(obj_handler_t * oh, const char * where, object_id_t id);

/* What a live heap handle or constant entry says; a constant's flags hold only OBJECT_H_ASCII. */
typedef struct {
    uint32_t size;
    uint16_t type;
    uint8_t  kind;
    uint8_t  flags;
    uint8_t  constant;          /* immutable and never freed (D7) */
} object_info_t;

int         objects_info(obj_handler_t * oh, object_id_t id, object_info_t * out,
                         const char * where);
void        objects_init(obj_handler_t * oh, uint8_t * statics, uint32_t statics_size,
                         const uint8_t * const_base, uint32_t const_count);

#ifdef __cplusplus
}
#endif

#endif /* OBJECTS_H */
