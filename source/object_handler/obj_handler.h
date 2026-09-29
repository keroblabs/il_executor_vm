#ifndef OBJ_HANDLER_H
#define OBJ_HANDLER_H

#include "objects_types.h"
#include "objects.h"
#include "heap.h"
#include "gc.h"
#include "stack.h"
#include "array.h"
#include "obj_string.h"
#include "value.h"

#ifdef __cplusplus
extern "C" {
#endif

/* obj_handler_init result, checked in this order; append only (doc 14 section 9.2) */
typedef enum {
    OBJ_HANDLER_OK = 0,
    OBJ_HANDLER_E_ARG_NULL,         /* oh, cfg or host is NULL */
    OBJ_HANDLER_E_ALREADY_INIT,     /* this instance is initialised; shut it down first */
    OBJ_HANDLER_E_SEGMENTS,         /* segments == 0 or > HEAP_SEGMENTS */
    OBJ_HANDLER_E_PAYLOAD_BASE,     /* payload NULL or not 8-byte aligned */
    OBJ_HANDLER_E_PAYLOAD_SIZE,     /* < 16, > HEAP_MAX_PAYLOAD, or not a multiple of 8 */
    OBJ_HANDLER_E_HANDLES,          /* handles NULL, or handle_count < 2 or > 65536 */
    OBJ_HANDLER_E_WORKLIST,         /* worklist NULL or worklist_len == 0 */
    OBJ_HANDLER_E_HOST_CALLBACK,    /* type_refmap, on_no_memory or on_corrupt is NULL */
    OBJ_HANDLER_E_STATICS,          /* statics malformed, or its refmap marks bytes 0-7 */
    OBJ_HANDLER_E_CONST             /* the constant table, or one of its entries */
} obj_handler_status_t;

/* Every region is caller-supplied (doc 14 section 9). */
typedef struct {
    struct {
        uint8_t         * payload;      uint32_t payload_size;
        object_handle_t * handles;      uint32_t handle_count;
        uint8_t           gc_threshold_pct;
        uint8_t           gc_handle_pct;
    } segment[HEAP_SEGMENTS];
    uint8_t         segments;

    uint8_t         gc_recover_pct, gc_damp_step_pct, gc_damp_cap_pct;

    uint8_t       * statics;        uint32_t statics_size;  /* 0: no statics */
    const uint8_t * statics_refmap;                         /* one bit per word; NULL: no ids */

    const uint8_t * const_base;     /* the constant table, may be flash (D7) */
    uint32_t        const_count;    /* entries, the reserved entry 0 included; 0: none */
    uint32_t        const_size;     /* table and payloads, in bytes */

    uint16_t        string_type;    /* the embedder's type id for strings (D9) */

    uint16_t        worklist_len;   uint16_t * worklist;
} obj_handler_config_t;

/* The application's instance, used only from the application's thread (doc 14 section 1). */
obj_handler_status_t obj_handler_init(obj_handler_t * oh, const obj_handler_config_t * cfg,
                                      const obj_handler_host_t * host);
void                 obj_handler_shutdown(obj_handler_t * oh);  /* zeroes the instance */
int                  obj_handler_is_ready(obj_handler_t * oh);

#ifdef __cplusplus
}
#endif

#endif /* OBJ_HANDLER_H */
