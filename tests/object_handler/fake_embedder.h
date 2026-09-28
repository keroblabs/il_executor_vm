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
    FAKE_T_COUNT
};

#define FAKE_MAX_HANDLES    65536u

extern obj_handler_config_t fake_cfg;
extern obj_handler_host_t   fake_host;

extern jmp_buf      fake_oom_jmp;
extern int          fake_oom_armed;
extern unsigned     fake_oom_count;

extern unsigned     fake_corrupt_count;
extern const char * fake_corrupt_where;
extern object_id_t  fake_corrupt_id;

void              fake_config(uint32_t payload_size, uint32_t handle_count);
void              fake_init(uint32_t payload_size, uint32_t handle_count);
void              fake_allow_corrupt(void);
uint8_t         * fake_payload(void);
object_handle_t * fake_handles(void);

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
