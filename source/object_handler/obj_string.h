#ifndef OBJ_STRING_H
#define OBJ_STRING_H

#include "objects_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Immutable UTF-8 plus a NUL (doc 14 section 5.6). OBJECT_NULL: invalid UTF-8, or it can never fit. */
object_id_t obj_string_create(const char * utf8, uint32_t len);
object_id_t obj_string_concat(object_id_t a, object_id_t b);
object_id_t obj_string_intern(uint16_t literal_index);     /* the constant's id: no allocation */

/* In UTF-16 code units, as ECMA-335 counts them. */
uint32_t    obj_string_length(object_id_t id);
int32_t     obj_string_char_at(object_id_t id, uint32_t index);    /* -1: out of range */

/* Library-internal: called by obj_handler_init and obj_handler_shutdown. */
void        obj_string_init(uint16_t string_type);

#ifdef __cplusplus
}
#endif

#endif /* OBJ_STRING_H */
