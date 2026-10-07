#include "obj_handler.h"

void objects_corrupt(obj_handler_t * oh, const char * where, object_id_t id)
{
    if (oh->host.on_corrupt != NULL)
        oh->host.on_corrupt(oh, where, id);
}

void objects_init(obj_handler_t * oh, uint8_t * statics, uint32_t statics_size,
                  const uint8_t * const_base, uint32_t const_count)
{
    memset(oh->spaces, 0, sizeof oh->spaces);
    if (statics_size != 0) {
        oh->spaces[OBJECT_SPACE_STATIC].base  = statics;
        oh->spaces[OBJECT_SPACE_STATIC].limit = statics_size;
        oh->spaces[OBJECT_SPACE_STATIC].flags = OBJECT_SP_WRITABLE | OBJECT_SP_ROOT;
    }
    oh->const_base = const_count != 0 ? const_base : NULL;
    oh->consts = (const object_const_t *)(const void *)oh->const_base;
    oh->spaces[OBJECT_SPACE_CONST].limit = const_count;        /* base NULL: indirect */
}

/* The table entry of a valid constant id, or NULL (doc 14 section 3.5). */
static const object_const_t * objects_const_of(const obj_handler_t * oh, object_id_t id)
{
    const uint16_t index = OBJECT_INDEX(id);

    if (OBJECT_SPACE(id) != OBJECT_SPACE_CONST || OBJECT_GEN(id) != 0 || oh->consts == NULL
        || index == 0 || index >= oh->spaces[OBJECT_SPACE_CONST].limit)
        return NULL;
    return &oh->consts[index];
}

void * objects_resolve_indirect(obj_handler_t * oh, object_id_t id)
{
    const object_const_t * c = objects_const_of(oh, id);

    if (c == NULL) {
        objects_corrupt(oh, "objects_ptr: not a resolvable indirect id", id);
        return NULL;
    }
    return (void *)(oh->const_base + c->offset);
}

object_handle_t * objects_handle_of(obj_handler_t * oh, object_id_t id, const char * where)
{
    const uint16_t space = OBJECT_SPACE(id);
    const uint16_t index = OBJECT_INDEX(id);
    object_handle_t * h;

    if (space >= OBJECT_SPACE_COUNT || oh->spaces[space].handles == NULL
        || index == 0 || index >= oh->spaces[space].limit) {
        objects_corrupt(oh, where, id);
        return NULL;
    }
    h = &oh->spaces[space].handles[index];
    if (!(h->flags & OBJECT_H_INUSE) || OBJECT_HANDLE_GEN(h) != OBJECT_GEN(id)) {
        objects_corrupt(oh, where, id);
        return NULL;
    }
    return h;
}

#if OBJECT_DEBUG_REFS
void * objects_ptr_checked(obj_handler_t * oh, object_id_t id)
{
    const uint16_t space = OBJECT_SPACE(id);
    const uint16_t index = OBJECT_INDEX(id);
    const object_space_t * sp;
    const object_handle_t * h;

    if (id == OBJECT_NULL) {
        objects_corrupt(oh, "objects_ptr: null id", id);
        return NULL;
    }
    if (space >= OBJECT_SPACE_COUNT || oh->spaces[space].limit == 0) {
        objects_corrupt(oh, "objects_ptr: unregistered space", id);
        return NULL;
    }
    sp = &oh->spaces[space];
    if (index == 0 || index >= sp->limit) {
        objects_corrupt(oh, "objects_ptr: index out of range", id);
        return NULL;
    }
    if (sp->handles == NULL)
        return sp->base != NULL ? sp->base + index : objects_resolve_indirect(oh, id);

    h = &sp->handles[index];
    if (!(h->flags & OBJECT_H_INUSE)) {
        objects_corrupt(oh, "objects_ptr: handle not in use", id);
        return NULL;
    }
    if (h->gen != OBJECT_GEN(id)) {
        objects_corrupt(oh, "objects_ptr: stale id", id);
        return NULL;
    }
    return sp->base + h->offset;
}
#endif

int objects_info(obj_handler_t * oh, object_id_t id, object_info_t * out, const char * where)
{
    const object_const_t * c = objects_const_of(oh, id);
    const object_handle_t * h;

    if (c != NULL) {
        out->size     = c->size;
        out->type     = c->type;
        out->kind     = c->kind;
        out->flags    = c->flags;
        out->constant = 1;
        return 1;
    }
    h = objects_handle_of(oh, id, where);
    if (h == NULL)
        return 0;
    out->size     = h->size;
    out->type     = h->type;
    out->kind     = h->kind;
    out->flags    = h->flags;
    out->constant = 0;
    return 1;
}

uint16_t objects_type_of(obj_handler_t * oh, object_id_t id)
{
    object_info_t info;

    return objects_info(oh, id, &info, "objects_type_of: not a live object") ? info.type : 0;
}

uint32_t objects_size_of(obj_handler_t * oh, object_id_t id)
{
    object_info_t info;

    return objects_info(oh, id, &info, "objects_size_of: not a live object") ? info.size : 0;
}

uint8_t objects_kind_of(obj_handler_t * oh, object_id_t id)
{
    object_info_t info;

    return objects_info(oh, id, &info, "objects_kind_of: not a live object")
           ? info.kind : (uint8_t)OBJECT_K_FREE;
}

object_id_t objects_create(obj_handler_t * oh, uint16_t type, uint32_t bytes)
{
    return heap_alloc(oh, OBJECT_SPACE_HEAP, type, OBJECT_K_OBJECT, bytes);
}

/* A constant is copied into the heap; the copy keeps the ASCII bit. */
object_id_t objects_clone(obj_handler_t * oh, object_id_t id)
{
    object_info_t info;
    object_id_t copy;

    if (!objects_info(oh, id, &info, "objects_clone: not a live object"))
        return OBJECT_NULL;
    copy = heap_alloc(oh, info.constant ? (uint16_t)OBJECT_SPACE_HEAP : OBJECT_SPACE(id),
                      info.type, info.kind, info.size);
    if (copy == OBJECT_NULL)
        return OBJECT_NULL;
    oh->spaces[OBJECT_SPACE(copy)].handles[OBJECT_INDEX(copy)].flags |=
        (uint8_t)(info.flags & OBJECT_H_ASCII);
    if (info.size != 0)
        memcpy(objects_ptr(oh, copy), objects_ptr(oh, id), info.size);
    return copy;
}

void objects_delete(obj_handler_t * oh, object_id_t id)
{
    heap_release(oh, id);
}

object_id_t objects_promote(obj_handler_t * oh, object_id_t id)
{
    /* Nothing lives in OBJECT_SPACE_FRAME until tier 2 (doc 14 section 6.5). */
    if (OBJECT_SPACE(id) == OBJECT_SPACE_FRAME) {
        objects_corrupt(oh, "objects_promote: no frame scratch in v1", id);
        return OBJECT_NULL;
    }
    return id;
}

/* Statics and constants never move, so only heap objects carry a pin count (A4). */
void * objects_pin(obj_handler_t * oh, object_id_t id)
{
    object_handle_t * h;

    if (OBJECT_SPACE(id) < OBJECT_SPACE_CONST) {
        h = objects_handle_of(oh, id, "objects_pin: not a live heap object");
        if (h == NULL)
            return NULL;
        if ((h->flags & OBJECT_H_PINNED) == OBJECT_H_PINNED)
            objects_corrupt(oh, "objects_pin: more than 7 nested pins", id);
        else
            h->flags = (uint8_t)(h->flags + OBJECT_H_PIN_ONE);
    }
    return objects_ptr(oh, id);
}

void objects_unpin(obj_handler_t * oh, object_id_t id)
{
    object_handle_t * h;

    if (OBJECT_SPACE(id) >= OBJECT_SPACE_CONST)
        return;
    h = objects_handle_of(oh, id, "objects_unpin: not a live heap object");
    if (h == NULL)
        return;
    if ((h->flags & OBJECT_H_PINNED) == 0) {
        objects_corrupt(oh, "objects_unpin: not pinned", id);
        return;
    }
    h->flags = (uint8_t)(h->flags - OBJECT_H_PIN_ONE);
}

void objects_space_register(obj_handler_t * oh, uint16_t tag, const object_space_t * desc)
{
    const char * error = NULL;

    if (!obj_handler_is_ready(oh))
        error = "objects_space_register: library not initialised";
    else if (tag >= OBJECT_SPACE_COUNT)
        error = "objects_space_register: tag beyond OBJECT_SPACE_COUNT";
    else if (tag != OBJECT_SPACE_NATIVE)
        error = "objects_space_register: every space but the native one is the library's";
    else if (desc == NULL)
        error = "objects_space_register: desc is NULL";
    else if (desc->handles != NULL)
        error = "objects_space_register: only heap.c creates handled spaces";
    else if (desc->limit == 0 || desc->limit > 65536u)
        error = "objects_space_register: limit outside 1-65536";
    else if (oh->spaces[tag].limit != 0)
        error = "objects_space_register: space already registered";

    if (error != NULL) {
        objects_corrupt(oh, error, OBJECT_NULL);
        return;
    }
    oh->spaces[tag] = *desc;
}
