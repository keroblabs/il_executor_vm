#ifndef OBJ_HANDLER_CONFIG_H
#define OBJ_HANDLER_CONFIG_H

/* One configuration for MCU and host; only the debug switches differ (doc 18 section 0.2). */

#define OBJECT_SPACE_COUNT          0x0014u
#define HEAP_SEGMENTS               1u
#define HEAP_MAX_PAYLOAD            65536u

#define OBJ_STRING_ENCODING_UTF8    1
#define OBJ_STRING_ENCODING         OBJ_STRING_ENCODING_UTF8

#ifdef NDEBUG
#define OBJ_HANDLER_DEBUG_DEFAULT   0
#else
#define OBJ_HANDLER_DEBUG_DEFAULT   1
#endif

#ifndef OBJECT_DEBUG_REFS
#define OBJECT_DEBUG_REFS           OBJ_HANDLER_DEBUG_DEFAULT
#endif
#ifndef HEAP_DEBUG
#define HEAP_DEBUG                  OBJ_HANDLER_DEBUG_DEFAULT
#endif
#ifndef HEAP_POISON
#define HEAP_POISON                 OBJ_HANDLER_DEBUG_DEFAULT
#endif
#ifndef HEAP_VERIFY_EVERY_SAFEPOINT
#define HEAP_VERIFY_EVERY_SAFEPOINT OBJ_HANDLER_DEBUG_DEFAULT
#endif
#ifndef STACK_DEBUG
#define STACK_DEBUG                 OBJ_HANDLER_DEBUG_DEFAULT
#endif

#endif /* OBJ_HANDLER_CONFIG_H */
