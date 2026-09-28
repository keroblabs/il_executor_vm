#ifndef OBJ_HANDLER_H
#define OBJ_HANDLER_H

#include "objects_types.h"
#include "objects.h"
#include "heap.h"
#include "gc.h"
#include "array.h"
#include "obj_string.h"
#include "value.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    OBJ_HANDLER_E_HOST_CALLBACK     /* type_refmap, on_oom or on_corrupt is NULL */
} obj_handler_status_t;

typedef struct {
    /* Bit i set: word i of the payload is an object id. NULL: no ids.
       For an array type, the bitmap of one element (doc 14 section 5.7). */
    const uint8_t * (*type_refmap)(uint16_t type, uint32_t * words);

    void (*on_oom)(void);                                   /* must not return */
    void (*on_corrupt)(const char * where, object_id_t id); /* an embedder bug */
} obj_handler_host_t;

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

    uint8_t       * statics;        uint32_t statics_size;
    const uint8_t * statics_refmap;

    const uint8_t * const_base;     /* immutable image, may be flash */
    uint32_t        const_count;

    object_id_t   * literals;       uint32_t literal_count;
    uint16_t        string_type;    /* the embedder's type id for strings (D9) */

    uint8_t       * stack_mem;      uint32_t stack_size;

    uint16_t        worklist_len;   uint16_t * worklist;
} obj_handler_config_t;

obj_handler_status_t obj_handler_init(const obj_handler_config_t * cfg,
                                      const obj_handler_host_t   * host);
void                 obj_handler_shutdown(void);
int                  obj_handler_is_ready(void);

/* Library-internal: the host callbacks, valid while initialised. */
extern obj_handler_host_t obj_handler_host;

#ifdef __cplusplus
}
#endif

#endif /* OBJ_HANDLER_H */
