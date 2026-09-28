#ifndef OBJECTS_TYPES_H
#define OBJECTS_TYPES_H

#include <stdint.h>
#include <string.h>
#include "obj_handler_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define OBJECT_LIKELY(x)        __builtin_expect(!!(x), 1)
#define OBJECT_UNLIKELY(x)      __builtin_expect(!!(x), 0)
#else
#define OBJECT_LIKELY(x)        (x)
#define OBJECT_UNLIKELY(x)      (x)
#endif

/* C99 has no _Static_assert: a negative array size stops the build. */
#define OBJECT_STATIC_ASSERT(cond, name) \
        typedef char object_static_assert_##name[(cond) ? 1 : -1]

/* ---- object id (doc 14 section 2.1) --------------------------------- */

typedef uint32_t object_id_t;

#define OBJECT_NULL             ((object_id_t)0)

#if OBJECT_DEBUG_REFS
/* 8-bit space | 8-bit generation | 16-bit index */
#define OBJECT_SPACE(id)        ((uint16_t)((id) >> 24))
#define OBJECT_GEN(id)          ((uint8_t)(((id) >> 16) & 0xFFu))
#define OBJECT_ID(space, idx)   (((object_id_t)(space) << 24) | (uint16_t)(idx))
#define OBJECT_ID_GEN(space, gen, idx) \
        (((object_id_t)(space) << 24) | ((object_id_t)(gen) << 16) | (uint16_t)(idx))
#else
#define OBJECT_SPACE(id)        ((uint16_t)((id) >> 16))
#define OBJECT_GEN(id)          ((uint8_t)0)
#define OBJECT_ID(space, idx)   (((object_id_t)(space) << 16) | (uint16_t)(idx))
#define OBJECT_ID_GEN(space, gen, idx)  OBJECT_ID((space), (idx))
#endif
#define OBJECT_INDEX(id)        ((uint16_t)((id) & 0xFFFFu))

/* ---- spaces (doc 14 section 3.1) ------------------------------------ */

enum {
    OBJECT_SPACE_HEAP   = 0x0000,       /* segment n is OBJECT_SPACE_HEAP + n */
    OBJECT_SPACE_CONST  = 0x0010,
    OBJECT_SPACE_STATIC = 0x0011,
    OBJECT_SPACE_FRAME  = 0x0012,       /* reserved, unused in v1 */
    OBJECT_SPACE_NATIVE = 0x0013
};

enum {
    OBJECT_SP_COLLECTED = 0x0001,
    OBJECT_SP_WRITABLE  = 0x0002,
    OBJECT_SP_ROOT      = 0x0004
};

/* ---- handle slot (doc 14 section 3.2) ------------------------------- */

typedef struct {
    uint16_t offset;    /* payload offset; for a free handle, the next free index */
    uint16_t size;      /* payload bytes */
    uint16_t type;      /* embedder type id */
    uint8_t  flags;     /* OBJECT_H_* */
    uint8_t  kind;      /* OBJECT_K_* */
#if OBJECT_DEBUG_REFS
    uint8_t  gen;       /* 1-255, bumped by heap_release */
#endif
} object_handle_t;

#if OBJECT_DEBUG_REFS
#define OBJECT_HANDLE_GEN(h)    ((h)->gen)
#else
#define OBJECT_HANDLE_GEN(h)    0u
#endif

enum {
    OBJECT_H_INUSE   = 0x01,
    OBJECT_H_ASCII   = 0x02,            /* strings only: every byte below 0x80 (D9) */
    OBJECT_H_MARK    = 0x04,
    OBJECT_H_SCANNED = 0x08,
    OBJECT_H_HASREFS = 0x10,
    OBJECT_H_PINNED  = 0xE0             /* pin count 0-7 (A4); nonzero: must not move */
};
#define OBJECT_H_PIN_ONE        0x20u

enum {
    OBJECT_K_FREE = 0,
    OBJECT_K_OBJECT,
    OBJECT_K_ARRAY,
    OBJECT_K_STRING,
    OBJECT_K_BOX,
    OBJECT_K_RAW,
    OBJECT_K_COUNT
};

typedef struct {
    uint8_t         * base;     /* NULL: indirect space */
    object_handle_t * handles;  /* non-NULL: handled space */
    uint32_t          limit;    /* highest valid index + 1; 0: not registered */
    uint16_t          flags;    /* OBJECT_SP_* */
} object_space_t;

/* ---- interior pointer (doc 14 section 4) ---------------------------- */

typedef struct {
    object_id_t id;
    uint16_t    offset;
    uint16_t    _pad;
} object_byref_t;

/* ---- array payload header, read by gc.c (doc 14 section 5.7) -------- */

typedef struct {
    uint16_t count;
    uint16_t elem_size;
    uint32_t _reserved;         /* keeps elements 8-byte aligned */
} array_header_t;

#if !OBJECT_DEBUG_REFS
OBJECT_STATIC_ASSERT(sizeof(object_handle_t) == 8, handle_is_8_bytes);
#else
OBJECT_STATIC_ASSERT(OBJECT_SPACE_COUNT <= 0x100u, debug_ids_carry_8_bit_spaces);
#endif
OBJECT_STATIC_ASSERT(sizeof(object_byref_t) == 8, byref_is_8_bytes);
OBJECT_STATIC_ASSERT(sizeof(array_header_t) == 8, array_header_is_8_bytes);
OBJECT_STATIC_ASSERT(HEAP_SEGMENTS >= 1 && HEAP_SEGMENTS <= 16, heap_segments_fit_the_tags);

#ifdef __cplusplus
}
#endif

#endif /* OBJECTS_TYPES_H */
