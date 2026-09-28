#ifndef FAKE_EMBEDDER_H
#define FAKE_EMBEDDER_H

#include <setjmp.h>
#include "harness.h"
#include "obj_handler.h"

/* The fake type table: no CIL, ECMA or image vocabulary (doc 18 section 2.5). */
enum {
    FAKE_T_DATA = 1,        /* no ids */
    FAKE_T_NODE,            /* word 0 is an id */
    FAKE_T_PAIR,            /* words 0 and 1 are ids */
    FAKE_T_MIXED,           /* word 0 is an id, word 1 is not */
    FAKE_T_STRING,          /* config.string_type */
    FAKE_T_COUNT
};

#define FAKE_MAX_HANDLES    65536u
#define FAKE_STATICS_BYTES  256u        /* words 2 and 3 hold ids */
#define FAKE_CONST_MAX      16u         /* table entries, the reserved one included */
#define FAKE_CONST_BYTES    4096u

extern obj_handler_config_t fake_cfg;
extern obj_handler_host_t   fake_host;
extern obj_handler_t        fake_oh;        /* the instance fake_start initialises */

extern jmp_buf         fake_oom_jmp;
extern int             fake_oom_armed;
extern unsigned        fake_oom_count;

extern unsigned        fake_corrupt_count;
extern const char *    fake_corrupt_where;
extern object_id_t     fake_corrupt_id;

/* The instance that made the last call to each callback. */
extern obj_handler_t * fake_oom_oh;
extern obj_handler_t * fake_corrupt_oh;
extern obj_handler_t * fake_refmap_oh;

void              fake_config(uint32_t payload_size, uint32_t handle_count);
void              fake_start(void);         /* obj_handler_init on fake_cfg, which must pass */
void              fake_init(uint32_t payload_size, uint32_t handle_count);
void              fake_allow_corrupt(void);
void              fake_dump(void);
uint8_t         * fake_payload(void);
object_handle_t * fake_handles(void);
uint8_t         * fake_statics(void);

/* A constant table laid out the way cli_pack lays it out (doc 14 section 3.5). */
void              fake_consts_begin(void);
uint16_t          fake_const_add(uint16_t type, uint8_t kind, uint8_t flags,
                                 const void * payload, uint32_t size);
uint16_t          fake_const_string(const char * utf8, uint32_t len);
uint16_t          fake_const_array(uint16_t type, uint16_t elem_size, uint16_t count,
                                   const void * elements);
void              fake_consts_install(void);  /* into fake_cfg, after fake_config */
object_const_t  * fake_const_entry(uint16_t index);
uint8_t         * fake_const_bytes(void);

/* Requires stmt to end in on_oom. Locals that stmt writes are indeterminate afterwards. */
#define FAKE_EXPECT_OOM(stmt) \
    do { \
        fake_oom_armed = 1; \
        if (setjmp(fake_oom_jmp) == 0) { \
            stmt; \
            fake_oom_armed = 0; \
            harness_fail(__FILE__, __LINE__, "expected on_oom: " #stmt); \
        } \
    } while (0)

#endif /* FAKE_EMBEDDER_H */
