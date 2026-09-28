#include "obj_handler.h"

object_space_t object_spaces[OBJECT_SPACE_COUNT];

void objects_corrupt(const char * where, object_id_t id)
{
    if (obj_handler_host.on_corrupt != NULL)
        obj_handler_host.on_corrupt(where, id);
}

void * objects_resolve_indirect(object_id_t id)
{
    objects_corrupt("objects_ptr: no space resolves indirectly in v1", id);
    return NULL;
}

object_handle_t * objects_handle_of(object_id_t id, const char * where)
{
    const uint16_t space = OBJECT_SPACE(id);
    const uint16_t index = OBJECT_INDEX(id);
    object_handle_t * h;

    if (space >= OBJECT_SPACE_COUNT || object_spaces[space].handles == NULL
        || index == 0 || index >= object_spaces[space].limit) {
        objects_corrupt(where, id);
        return NULL;
    }
    h = &object_spaces[space].handles[index];
    if (!(h->flags & OBJECT_H_INUSE) || OBJECT_HANDLE_GEN(h) != OBJECT_GEN(id)) {
        objects_corrupt(where, id);
        return NULL;
    }
    return h;
}

#if OBJECT_DEBUG_REFS
void * objects_ptr_checked(object_id_t id)
{
    const uint16_t space = OBJECT_SPACE(id);
    const uint16_t index = OBJECT_INDEX(id);
    const object_space_t * sp;
    const object_handle_t * h;

    if (id == OBJECT_NULL) {
        objects_corrupt("objects_ptr: null id", id);
        return NULL;
    }
    if (space >= OBJECT_SPACE_COUNT || object_spaces[space].limit == 0) {
        objects_corrupt("objects_ptr: unregistered space", id);
        return NULL;
    }
    sp = &object_spaces[space];
    if (index == 0 || index >= sp->limit) {
        objects_corrupt("objects_ptr: index out of range", id);
        return NULL;
    }
    if (sp->handles == NULL)
        return sp->base != NULL ? sp->base + index : objects_resolve_indirect(id);

    h = &sp->handles[index];
    if (!(h->flags & OBJECT_H_INUSE)) {
        objects_corrupt("objects_ptr: handle not in use", id);
        return NULL;
    }
    if (h->gen != OBJECT_GEN(id)) {
        objects_corrupt("objects_ptr: stale id", id);
        return NULL;
    }
    return sp->base + h->offset;
}
#endif

uint16_t objects_type_of(object_id_t id)
{
    const object_handle_t * h = objects_handle_of(id, "objects_type_of: not a live heap object");

    return h != NULL ? h->type : 0;
}

uint32_t objects_size_of(object_id_t id)
{
    const object_handle_t * h = objects_handle_of(id, "objects_size_of: not a live heap object");

    return h != NULL ? h->size : 0;
}

uint8_t objects_kind_of(object_id_t id)
{
    const object_handle_t * h = objects_handle_of(id, "objects_kind_of: not a live heap object");

    return h != NULL ? h->kind : (uint8_t)OBJECT_K_FREE;
}

object_id_t objects_create(uint16_t type, uint32_t bytes)
{
    return heap_alloc(OBJECT_SPACE_HEAP, type, OBJECT_K_OBJECT, bytes);
}

object_id_t objects_clone(object_id_t id)
{
    const object_handle_t * h = objects_handle_of(id, "objects_clone: not a live heap object");
    object_id_t copy;
    uint32_t size;
    uint8_t kept;

    if (h == NULL)
        return OBJECT_NULL;
    size = h->size;
    kept = (uint8_t)(h->flags & OBJECT_H_ASCII);
    copy = heap_alloc(OBJECT_SPACE(id), h->type, h->kind, size);
    if (copy == OBJECT_NULL)
        return OBJECT_NULL;
    object_spaces[OBJECT_SPACE(copy)].handles[OBJECT_INDEX(copy)].flags |= kept;
    if (size != 0)
        memcpy(objects_ptr(copy), objects_ptr(id), size);
    return copy;
}

object_id_t objects_promote(object_id_t id)
{
    /* Nothing lives in OBJECT_SPACE_FRAME until tier 2 (doc 14 section 6.5). */
    if (OBJECT_SPACE(id) == OBJECT_SPACE_FRAME) {
        objects_corrupt("objects_promote: no frame scratch in v1", id);
        return OBJECT_NULL;
    }
    return id;
}

/* Flat spaces never move, so only heap objects carry a pin count (A4). */
void * objects_pin(object_id_t id)
{
    object_handle_t * h;

    if (OBJECT_SPACE(id) < OBJECT_SPACE_CONST) {
        h = objects_handle_of(id, "objects_pin: not a live heap object");
        if (h == NULL)
            return NULL;
        if ((h->flags & OBJECT_H_PINNED) == OBJECT_H_PINNED)
            objects_corrupt("objects_pin: more than 7 nested pins", id);
        else
            h->flags = (uint8_t)(h->flags + OBJECT_H_PIN_ONE);
    }
    return objects_ptr(id);
}

void objects_unpin(object_id_t id)
{
    object_handle_t * h;

    if (OBJECT_SPACE(id) >= OBJECT_SPACE_CONST)
        return;
    h = objects_handle_of(id, "objects_unpin: not a live heap object");
    if (h == NULL)
        return;
    if ((h->flags & OBJECT_H_PINNED) == 0) {
        objects_corrupt("objects_unpin: not pinned", id);
        return;
    }
    h->flags = (uint8_t)(h->flags - OBJECT_H_PIN_ONE);
}

void objects_space_register(uint16_t tag, const object_space_t * desc)
{
    const char * error = NULL;

    if (!obj_handler_is_ready())
        error = "objects_space_register: library not initialised";
    else if (tag >= OBJECT_SPACE_COUNT)
        error = "objects_space_register: tag beyond OBJECT_SPACE_COUNT";
    else if (tag < OBJECT_SPACE_CONST)
        error = "objects_space_register: heap spaces belong to heap.c";
    else if (tag == OBJECT_SPACE_FRAME)
        error = "objects_space_register: OBJECT_SPACE_FRAME is reserved in v1";
    else if (desc == NULL)
        error = "objects_space_register: desc is NULL";
    else if (desc->handles != NULL)
        error = "objects_space_register: only heap.c creates handled spaces";
    else if (desc->limit == 0 || desc->limit > 65536u)
        error = "objects_space_register: limit outside 1-65536";
    else if (object_spaces[tag].limit != 0)
        error = "objects_space_register: space already registered";

    if (error != NULL) {
        objects_corrupt(error, OBJECT_NULL);
        return;
    }
    object_spaces[tag] = *desc;
}
