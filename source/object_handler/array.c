#include "obj_handler.h"

#define ARRAY_HEADER    ((uint32_t)sizeof(array_header_t))

/* The header of a live array, heap or constant, or NULL once a bad id has gone to on_corrupt. */
static array_header_t * array_header_of(object_id_t id, const char * where, object_info_t * info)
{
    if (!objects_info(id, info, where))
        return NULL;
    if (info->kind != OBJECT_K_ARRAY) {
        objects_corrupt(where, id);
        return NULL;
    }
    return (array_header_t *)objects_ptr(id);
}

static uint8_t * array_at(array_header_t * hdr, uint32_t index)
{
    return (uint8_t *)hdr + ARRAY_HEADER + index * hdr->elem_size;
}

object_id_t array_create(uint16_t type, uint16_t elem_size, uint32_t count)
{
    array_header_t * hdr;
    object_id_t id;

    if (elem_size == 0) {
        objects_corrupt("array_create: element size 0", OBJECT_NULL);
        return OBJECT_NULL;
    }
#if OBJECT_DEBUG_REFS
    {
        uint32_t words = 0;

        if (obj_handler_host.type_refmap(type, &words) != NULL && words * 4u != elem_size) {
            objects_corrupt("array_create: element refmap does not cover elem_size", OBJECT_NULL);
            return OBJECT_NULL;
        }
    }
#endif
    /* Rejected before multiplying, so count * elem_size cannot wrap; never fits (A10). */
    if (count > 0xFFFFu || count > (HEAP_MAX_PAYLOAD - ARRAY_HEADER) / elem_size)
        return OBJECT_NULL;

    id = heap_alloc(OBJECT_SPACE_HEAP, type, OBJECT_K_ARRAY, ARRAY_HEADER + count * elem_size);
    if (id == OBJECT_NULL)
        return OBJECT_NULL;
    hdr = (array_header_t *)objects_ptr(id);
    hdr->count = (uint16_t)count;
    hdr->elem_size = elem_size;
    return id;
}

uint32_t array_length(object_id_t id)
{
    object_info_t info;
    const array_header_t * hdr = array_header_of(id, "array_length: not a live array", &info);

    return hdr != NULL ? hdr->count : 0;
}

void * array_range_ptr(object_id_t id, uint32_t index, uint32_t count)
{
    object_info_t info;
    array_header_t * hdr = array_header_of(id, "array_range_ptr: not a live array", &info);

    if (hdr == NULL)
        return NULL;
    /* Compared in elements, so no index or count can wrap past the check. */
    if (index > hdr->count || count > hdr->count - index)
        return NULL;
    return array_at(hdr, index);
}

object_byref_t array_element_byref(object_id_t id, uint32_t index)
{
    object_info_t info;
    const array_header_t * hdr = array_header_of(id, "array_element_byref: not a live array", &info);
    object_byref_t b;

    b.id = OBJECT_NULL;
    b.offset = 0;
    b._pad = 0;
    if (hdr != NULL && index < hdr->count) {
        b.id = id;
        b.offset = (uint16_t)(ARRAY_HEADER + index * hdr->elem_size);
    }
    return b;
}

void array_copy(object_id_t dst, uint32_t dst_index,
                object_id_t src, uint32_t src_index, uint32_t count)
{
    object_info_t di;
    object_info_t si;
    array_header_t * d = array_header_of(dst, "array_copy: destination is not a live array", &di);
    array_header_t * s = array_header_of(src, "array_copy: source is not a live array", &si);

    if (d == NULL || s == NULL)
        return;
    if (di.constant) {
        objects_corrupt("array_copy: destination is a constant", dst);
        return;
    }
    if (d->elem_size != s->elem_size) {
        objects_corrupt("array_copy: element sizes differ", dst);
        return;
    }
    if (dst_index > d->count || count > d->count - dst_index
        || src_index > s->count || count > s->count - src_index) {
        objects_corrupt("array_copy: range outside an array", dst);
        return;
    }

    if (d->elem_size == sizeof(object_id_t) && (di.flags & OBJECT_H_HASREFS)) {
        /* A reference array: every id goes through the barrier, in overlap-safe order. */
        object_id_t * to = (object_id_t *)(void *)array_at(d, dst_index);
        const object_id_t * from = (const object_id_t *)(void *)array_at(s, src_index);
        uint32_t i;

        if (to <= from)
            for (i = 0; i < count; i++)
                objects_write_ref(&to[i], from[i]);
        else
            for (i = count; i-- > 0; )
                objects_write_ref(&to[i], from[i]);
    } else {
        /* Struct elements move their ids by byte copy while the barrier is the identity (14 section 11). */
        memmove(array_at(d, dst_index), array_at(s, src_index), count * d->elem_size);
    }
}

void array_clear(object_id_t id, uint32_t index, uint32_t count)
{
    object_info_t info;
    array_header_t * hdr = array_header_of(id, "array_clear: not a live array", &info);

    if (hdr == NULL)
        return;
    if (info.constant) {
        objects_corrupt("array_clear: the array is a constant", id);
        return;
    }
    if (index > hdr->count || count > hdr->count - index) {
        objects_corrupt("array_clear: range outside the array", id);
        return;
    }
    /* Storing OBJECT_NULL needs no barrier. */
    memset(array_at(hdr, index), 0, count * hdr->elem_size);
}
