#include "obj_handler.h"

object_id_t value_box(obj_handler_t * oh, uint16_t type, const void * src, uint32_t bytes)
{
    object_id_t id;

    if (src == NULL && bytes != 0) {
        objects_corrupt(oh, "value_box: NULL source", OBJECT_NULL);
        return OBJECT_NULL;
    }
    /* The box is scanned by the value type's refmap (A3). */
    id = heap_alloc(oh, OBJECT_SPACE_HEAP, type, OBJECT_K_BOX, bytes);
    if (id != OBJECT_NULL && bytes != 0)
        memcpy(objects_ptr(oh, id), src, bytes);
    return id;
}

void value_unbox(obj_handler_t * oh, object_id_t id, void * dst, uint32_t bytes)
{
    object_info_t info;

    if (!objects_info(oh, id, &info, "value_unbox: not a live object"))
        return;
    if (info.kind != OBJECT_K_BOX || info.size != bytes || (dst == NULL && bytes != 0)) {
        objects_corrupt(oh, "value_unbox: not a box of that size", id);
        return;
    }
    if (bytes != 0)
        memcpy(dst, objects_ptr(oh, id), bytes);
}
