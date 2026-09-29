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
    OBJECT_SPACE_NATIVE = 0x0013,
    OBJECT_SPACE_STACK  = 0x0014        /* thread-relative: resolved with the thread (doc 14 section 4.1) */
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

/* ---- stack slot, read by value.c (doc 14 section 6.1) --------------- */

enum {
    STACK_K_EMPTY = 0,
    STACK_K_I4,
    STACK_K_I8,
    STACK_K_R8,
    STACK_K_NINT,
    STACK_K_REF,                /* u.id */
    STACK_K_BYREF               /* u.byref_base, with the offset in aux */
};

typedef struct {
    uint8_t  kind;              /* STACK_K_* */
    uint8_t  flags;             /* reserved */
    uint16_t aux;               /* byref offset */
    union {
        int32_t     i4;
        int64_t     i8;
        double      r8;
        int32_t     nint;       /* native int is 32 bits on every target */
        object_id_t id;
        object_id_t byref_base;
    } u;
} stack_slot_t;

/* ---- array payload header, read by gc.c (doc 14 section 5.7) -------- */

typedef struct {
    uint16_t count;
    uint16_t elem_size;
    uint32_t _reserved;         /* keeps elements 8-byte aligned */
} array_header_t;

/* ---- constant table entry, an image format (doc 14 section 3.5) ----- */

typedef struct {
    uint32_t offset;            /* payload offset from const_base; 8-aligned, past the table */
    uint32_t size;              /* payload bytes */
    uint16_t type;
    uint8_t  kind;              /* OBJECT_K_*, never FREE */
    uint8_t  flags;             /* OBJECT_H_ASCII for an ASCII string, else 0 */
} object_const_t;

/* ---- the application's instance (doc 14 section 1) ------------------ */

typedef struct obj_handler obj_handler_t;
struct stack_thread_t;

/* The seam between the stacks and the collector (doc 14 section 7.4, A7). */
typedef void (*gc_visit_fn)(obj_handler_t * oh, object_id_t id);
typedef void (*gc_scan_fn)(obj_handler_t * oh, gc_visit_fn visit);

/* All three required (doc 14 section 7.5); each receives the instance that calls it. */
typedef struct {
    /* Bit i set: word i of the payload is an object id. NULL: no ids.
       For an array type, the bitmap of one element (doc 14 section 5.7). */
    const uint8_t * (*type_refmap)(obj_handler_t * oh, uint16_t type, uint32_t * words);

    /* on_no_memory must not return; on_corrupt reports an embedder bug. */
    void (*on_no_memory)(obj_handler_t * oh);
    void (*on_corrupt)(obj_handler_t * oh, const char * where, object_id_t id);
} obj_handler_host_t;

#define HEAP_HISTOGRAM_BUCKETS  12u     /* bucket k: rounded size <= 8 << k; the last: larger */

typedef struct {                        /* one heap segment, owned by heap.c */
    uint8_t         * payload;
    object_handle_t * handles;
    uint32_t          payload_size;
    uint32_t          handle_count;
    uint32_t          top;              /* [top, payload_size) has never been carved */
    uint32_t          used;
    uint32_t          peak_used;
    uint32_t          collect_at;       /* 0: no threshold */
    uint32_t          handles_used;
    uint32_t          handle_collect_at;
    uint16_t          free_head;
    uint16_t          handle_free_head; /* 0: none */
    uint16_t          handle_free_tail;
    uint32_t          histogram[HEAP_HISTOGRAM_BUCKETS];
} heap_segment_t;

/* The embedder provides it zeroed, as static storage is; every field is the library's. */
struct obj_handler {
    object_space_t         spaces[OBJECT_SPACE_COUNT];
    heap_segment_t         heap[HEAP_SEGMENTS];
    uint32_t               heap_count;
    const uint8_t        * const_base;
    const object_const_t * consts;
    obj_handler_host_t     host;
    struct stack_thread_t * threads;    /* every managed thread's stack, owned by stack.c */
    gc_scan_fn             root_scan;   /* registered by stack.c */
    uint16_t               string_type;
    uint8_t                gc_requested;
    uint8_t                ready;
};

#if !OBJECT_DEBUG_REFS
OBJECT_STATIC_ASSERT(sizeof(object_handle_t) == 8, handle_is_8_bytes);
#else
OBJECT_STATIC_ASSERT(OBJECT_SPACE_COUNT <= 0x100u, debug_ids_carry_8_bit_spaces);
#endif
OBJECT_STATIC_ASSERT(sizeof(object_byref_t) == 8, byref_is_8_bytes);
OBJECT_STATIC_ASSERT(sizeof(((stack_slot_t *)0)->u) == 8, slot_value_is_8_bytes);
OBJECT_STATIC_ASSERT(sizeof(array_header_t) == 8, array_header_is_8_bytes);
OBJECT_STATIC_ASSERT(sizeof(object_const_t) == 12, const_entry_is_12_bytes);
OBJECT_STATIC_ASSERT(HEAP_SEGMENTS >= 1 && HEAP_SEGMENTS <= 16, heap_segments_fit_the_tags);

#ifdef __cplusplus
}
#endif

#endif /* OBJECTS_TYPES_H */
