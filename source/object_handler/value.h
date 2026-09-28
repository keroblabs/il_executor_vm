#ifndef VALUE_H
#define VALUE_H

#include "objects_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CIL box and unbox.any: a heap copy of a value, tagged with its value type. */
object_id_t value_box(obj_handler_t * oh, uint16_t type, const void * src, uint32_t bytes);
void        value_unbox(obj_handler_t * oh, object_id_t id, void * dst, uint32_t bytes);

#ifdef __cplusplus
}
#endif

#endif /* VALUE_H */
