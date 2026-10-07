#ifndef OBJ_HANDLER_CONFIG_H
#define OBJ_HANDLER_CONFIG_H

/* One configuration for MCU and host; only the debug switches differ (doc 18 section 0.2). */

#include <obj_handler_build_config.h>

#define OBJECT_SPACE_COUNT          0x0015u
#define HEAP_SEGMENTS               1u
#define HEAP_MAX_PAYLOAD            65536u

#define OBJ_STRING_ENCODING_UTF8    1
#define OBJ_STRING_ENCODING         OBJ_STRING_ENCODING_UTF8

#ifndef OBJECT_DEBUG_REFS
#define OBJECT_DEBUG_REFS           OBJ_HANDLER_BUILD_OBJECT_DEBUG_REFS
#elif OBJECT_DEBUG_REFS != OBJ_HANDLER_BUILD_OBJECT_DEBUG_REFS
#error "OBJECT_DEBUG_REFS conflicts with the ObjectHandler library configuration"
#endif
#ifndef HEAP_DEBUG
#define HEAP_DEBUG                  OBJ_HANDLER_BUILD_HEAP_DEBUG
#elif HEAP_DEBUG != OBJ_HANDLER_BUILD_HEAP_DEBUG
#error "HEAP_DEBUG conflicts with the ObjectHandler library configuration"
#endif
#ifndef HEAP_POISON
#define HEAP_POISON                 OBJ_HANDLER_BUILD_HEAP_POISON
#elif HEAP_POISON != OBJ_HANDLER_BUILD_HEAP_POISON
#error "HEAP_POISON conflicts with the ObjectHandler library configuration"
#endif
#ifndef HEAP_VERIFY_EVERY_SAFEPOINT
#define HEAP_VERIFY_EVERY_SAFEPOINT OBJ_HANDLER_BUILD_HEAP_VERIFY_EVERY_SAFEPOINT
#elif HEAP_VERIFY_EVERY_SAFEPOINT != OBJ_HANDLER_BUILD_HEAP_VERIFY_EVERY_SAFEPOINT
#error "HEAP_VERIFY_EVERY_SAFEPOINT conflicts with the ObjectHandler library configuration"
#endif
#ifndef STACK_DEBUG
#define STACK_DEBUG                 OBJ_HANDLER_BUILD_STACK_DEBUG
#elif STACK_DEBUG != OBJ_HANDLER_BUILD_STACK_DEBUG
#error "STACK_DEBUG conflicts with the ObjectHandler library configuration"
#endif

#endif /* OBJ_HANDLER_CONFIG_H */
