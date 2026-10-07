#ifndef ARRAY_H
#define ARRAY_H

#include "objects_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One-dimensional, zero-based, fixed length, elements inline (doc 14 section 5.7). */
object_id_t    array_create(obj_handler_t * oh, uint16_t type, uint16_t elem_size, uint32_t count);
void           array_delete(obj_handler_t * oh, object_id_t id);
uint32_t       array_length(obj_handler_t * oh, object_id_t id);

/* The one range check: NULL, or an OBJECT_NULL id, is the program's IndexOutOfRangeException. */
void *         array_range_ptr(obj_handler_t * oh, object_id_t id, uint32_t index, uint32_t count);
object_byref_t array_element_byref(obj_handler_t * oh, object_id_t id, uint32_t index);

static inline void * array_element_ptr(obj_handler_t * oh, object_id_t id, uint32_t index)
{
    return array_range_ptr(oh, id, index, 1);
}

void           array_copy(obj_handler_t * oh, object_id_t dst, uint32_t dst_index,
                          object_id_t src, uint32_t src_index, uint32_t count);
void           array_clear(obj_handler_t * oh, object_id_t id, uint32_t index, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* ARRAY_H */
