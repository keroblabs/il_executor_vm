#ifndef OBJ_STRING_H
#define OBJ_STRING_H

#include "objects_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int         obj_string_check_const(obj_handler_t * oh, const uint8_t * payload, uint32_t size);

/* Immutable UTF-8 plus a NUL (doc 14 section 5.6). OBJECT_NULL: invalid UTF-8, or it can never fit. */
object_id_t obj_string_create(obj_handler_t * oh, const char * utf8, uint32_t len);
void        obj_string_delete(obj_handler_t * oh, object_id_t id);
object_id_t obj_string_concat(obj_handler_t * oh, object_id_t a, object_id_t b);
object_id_t obj_string_intern(obj_handler_t * oh, uint16_t literal_index);  /* no allocation */

/* In UTF-16 code units, as ECMA-335 counts them; -1: out of range. */
uint32_t    obj_string_length(obj_handler_t * oh, object_id_t id);
int32_t     obj_string_char_at(obj_handler_t * oh, object_id_t id, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* OBJ_STRING_H */
