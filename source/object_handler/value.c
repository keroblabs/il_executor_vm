#include "obj_handler.h"

object_id_t value_box(uint16_t type, const void * src, uint32_t bytes)
{
    object_id_t id;

    if (src == NULL && bytes != 0) {
        objects_corrupt("value_box: NULL source", OBJECT_NULL);
        return OBJECT_NULL;
    }
    /* The box is scanned by the value type's refmap (A3). */
    id = heap_alloc(OBJECT_SPACE_HEAP, type, OBJECT_K_BOX, bytes);
    if (id != OBJECT_NULL && bytes != 0)
        memcpy(objects_ptr(id), src, bytes);
    return id;
}

void value_unbox(object_id_t id, void * dst, uint32_t bytes)
{
    const object_handle_t * h = objects_handle_of(id, "value_unbox: not a live object");

    if (h == NULL)
        return;
    if (h->kind != OBJECT_K_BOX || h->size != bytes || (dst == NULL && bytes != 0)) {
        objects_corrupt("value_unbox: not a box of that size", id);
        return;
    }
    if (bytes != 0)
        memcpy(dst, objects_ptr(id), bytes);
}
