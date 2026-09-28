#ifndef OBJ_HANDLER_H
#define OBJ_HANDLER_H

#include "arrays_type.h"
#include "heap.h"
#include "instance.h"
#include "objects.h"
#include "stack.h"
#include "obj_string.h"
#include "value_type.h"

/* obj_handler_init result, checked in this order; append only (doc 14 section 9.2) */
typedef enum {
    OBJ_HANDLER_OK = 0,
    OBJ_HANDLER_E_ARG_NULL,         /* cfg or host is NULL */
    OBJ_HANDLER_E_ALREADY_INIT,     /* already initialised; shut down first */
    OBJ_HANDLER_E_SEGMENTS,         /* segments == 0 or > HEAP_SEGMENTS */
    OBJ_HANDLER_E_PAYLOAD_BASE,     /* payload NULL or not 8-byte aligned */
    OBJ_HANDLER_E_PAYLOAD_SIZE,     /* < 16, > HEAP_MAX_PAYLOAD, or not a multiple of 8 */
    OBJ_HANDLER_E_HANDLES,          /* handles NULL, or handle_count < 2 or > 65536 */
    OBJ_HANDLER_E_WORKLIST,         /* worklist NULL or worklist_len == 0 */
    OBJ_HANDLER_E_HOST_CALLBACK,    /* type_refmap, on_oom or on_corrupt is NULL */
} obj_handler_status_t;

#endif /* OBJ_HANDLER_H */
