#ifndef VALUE_H
#define VALUE_H

#include "objects_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CIL box and unbox.any: a heap copy of a value, tagged with its value type. */
object_id_t value_box(obj_handler_t * oh, uint16_t type, const void * src, uint32_t bytes);
void        value_unbox(obj_handler_t * oh, object_id_t id, void * dst, uint32_t bytes);

/* Normalisation (doc 14 section 6.7): which operation, and whether it is valid, is the caller's. */
enum {                                  /* value_compare's result */
    VALUE_LESS      = -1,
    VALUE_EQUAL     = 0,
    VALUE_GREATER   = 1,
    VALUE_UNORDERED = 2                 /* a NaN, or no common form */
};

enum {                                  /* flags */
    VALUE_UNSIGNED = 0x01,              /* integers are read unsigned */
    VALUE_CHECKED  = 0x02               /* value_convert: a value that does not fit is refused */
};

enum {                                  /* value_convert's targets */
    VALUE_T_INT8 = 1,
    VALUE_T_UINT8,
    VALUE_T_INT16,
    VALUE_T_UINT16,
    VALUE_T_INT32,
    VALUE_T_UINT32,
    VALUE_T_INT64,
    VALUE_T_UINT64,
    VALUE_T_FLOAT32,
    VALUE_T_FLOAT64,
    VALUE_T_NATIVE_INT,
    VALUE_T_NATIVE_UINT,
    VALUE_T_COUNT
};

int32_t     value_compare(obj_handler_t * oh, const stack_slot_t * a, const stack_slot_t * b,
                          uint32_t flags);

/* dst may be src. 0: not converted and dst untouched, since it does not fit, or on_corrupt. */
int         value_convert(obj_handler_t * oh, const stack_slot_t * src, uint8_t to,
                          uint32_t flags, stack_slot_t * dst);

#ifdef __cplusplus
}
#endif

#endif /* VALUE_H */
