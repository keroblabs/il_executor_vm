#include "obj_handler.h"

object_space_t object_spaces[OBJECT_SPACE_COUNT];

static const uint8_t *        objects_const_base;
static const object_const_t * objects_consts;

void objects_corrupt(const char * where, object_id_t id)
{
    if (obj_handler_host.on_corrupt != NULL)
        obj_handler_host.on_corrupt(where, id);
}

void objects_init(uint8_t * statics, uint32_t statics_size,
                  const uint8_t * const_base, uint32_t const_count)
{
    memset(object_spaces, 0, sizeof object_spaces);
    if (statics_size != 0) {
        object_spaces[OBJECT_SPACE_STATIC].base  = statics;
        object_spaces[OBJECT_SPACE_STATIC].limit = statics_size;
        object_spaces[OBJECT_SPACE_STATIC].flags = OBJECT_SP_WRITABLE | OBJECT_SP_ROOT;
    }
    objects_const_base = const_count != 0 ? const_base : NULL;
    objects_consts = (const object_const_t *)(const void *)objects_const_base;
    object_spaces[OBJECT_SPACE_CONST].limit = const_count;     /* base NULL: indirect */
}

void objects_shutdown(void)
{
    memset(object_spaces, 0, sizeof object_spaces);
    objects_const_base = NULL;
    objects_consts = NULL;
}

/* The table entry of a valid constant id, or NULL (doc 14 section 3.5). */
static const object_const_t * objects_const_of(object_id_t id)
{
    const uint16_t index = OBJECT_INDEX(id);

    if (OBJECT_SPACE(id) != OBJECT_SPACE_CONST || OBJECT_GEN(id) != 0 || objects_consts == NULL
        || index == 0 || index >= object_spaces[OBJECT_SPACE_CONST].limit)
        return NULL;
    return &objects_consts[index];
}

void * objects_resolve_indirect(object_id_t id)
{
    const object_const_t * c = objects_const_of(id);

    if (c == NULL) {
        objects_corrupt("objects_ptr: not a resolvable indirect id", id);
        return NULL;
    }
    return (void *)(objects_const_base + c->offset);
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

int objects_info(object_id_t id, object_info_t * out, const char * where)
{
    const object_const_t * c = objects_const_of(id);
    const object_handle_t * h;

    if (c != NULL) {
        out->size     = c->size;
        out->type     = c->type;
        out->kind     = c->kind;
        out->flags    = c->flags;
        out->constant = 1;
        return 1;
    }
    h = objects_handle_of(id, where);
    if (h == NULL)
        return 0;
    out->size     = h->size;
    out->type     = h->type;
    out->kind     = h->kind;
    out->flags    = h->flags;
    out->constant = 0;
    return 1;
}

uint16_t objects_type_of(object_id_t id)
{
    object_info_t info;

    return objects_info(id, &info, "objects_type_of: not a live object") ? info.type : 0;
}

uint32_t objects_size_of(object_id_t id)
{
    object_info_t info;

    return objects_info(id, &info, "objects_size_of: not a live object") ? info.size : 0;
}

uint8_t objects_kind_of(object_id_t id)
{
    object_info_t info;

    return objects_info(id, &info, "objects_kind_of: not a live object")
           ? info.kind : (uint8_t)OBJECT_K_FREE;
}

object_id_t objects_create(uint16_t type, uint32_t bytes)
{
    return heap_alloc(OBJECT_SPACE_HEAP, type, OBJECT_K_OBJECT, bytes);
}

/* A constant is copied into the heap; the copy keeps the ASCII bit. */
object_id_t objects_clone(object_id_t id)
{
    object_info_t info;
    object_id_t copy;

    if (!objects_info(id, &info, "objects_clone: not a live object"))
        return OBJECT_NULL;
    copy = heap_alloc(info.constant ? (uint16_t)OBJECT_SPACE_HEAP : OBJECT_SPACE(id),
                      info.type, info.kind, info.size);
    if (copy == OBJECT_NULL)
        return OBJECT_NULL;
    object_spaces[OBJECT_SPACE(copy)].handles[OBJECT_INDEX(copy)].flags |=
        (uint8_t)(info.flags & OBJECT_H_ASCII);
    if (info.size != 0)
        memcpy(objects_ptr(copy), objects_ptr(id), info.size);
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

/* Statics and constants never move, so only heap objects carry a pin count (A4). */
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
    else if (tag < OBJECT_SPACE_NATIVE)
        error = "objects_space_register: heap, constant, statics and frame spaces are the library's";
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
