#include "obj_handler.h"

#if defined(__SANITIZE_ADDRESS__)
#define HEAP_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HEAP_ASAN 1
#endif
#endif

#if HEAP_POISON && defined(HEAP_ASAN)
#include <sanitizer/asan_interface.h>
#define HEAP_ASAN_POISON(p, n)      ASAN_POISON_MEMORY_REGION((p), (n))
#define HEAP_ASAN_UNPOISON(p, n)    ASAN_UNPOISON_MEMORY_REGION((p), (n))
#else
#define HEAP_ASAN_POISON(p, n)      ((void)(p), (void)(n))
#define HEAP_ASAN_UNPOISON(p, n)    ((void)(p), (void)(n))
#endif

#define HEAP_ALIGN(n)       (((n) + 7u) & ~7u)
#define HEAP_FREE_HEAD      8u          /* bytes of a free block that stay addressable */
#define HEAP_POISON_BYTE    0xDD
#define HEAP_GUARD_MAGIC    0xB10Cu

typedef struct {            /* written into the free block itself (doc 14 section 5.1) */
    uint16_t next;          /* offset of the next free block; 0: end */
    uint16_t size;
} heap_free_t;

#if HEAP_DEBUG
typedef struct {            /* precedes every live block */
    uint16_t magic;
    uint16_t handle;        /* back-reference */
    uint16_t size;          /* block bytes, this header included */
    uint16_t check;
} heap_guard_t;
#endif

typedef struct {
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

static heap_segment_t heap_segments[HEAP_SEGMENTS];
static uint32_t       heap_segment_count;

static heap_free_t * heap_free_at(const heap_segment_t * s, uint32_t off)
{
    return (heap_free_t *)(void *)(s->payload + off);
}

#if HEAP_DEBUG
static heap_guard_t * heap_guard_at(const heap_segment_t * s, uint32_t off)
{
    return (heap_guard_t *)(void *)(s->payload + off);
}

static uint16_t heap_guard_check(const heap_guard_t * g)
{
    return (uint16_t)~(g->magic ^ g->handle ^ g->size);
}
#endif

static uint32_t heap_bucket(uint32_t size)
{
    uint32_t k = 0;

    while (k < HEAP_HISTOGRAM_BUCKETS - 1u && size > (8u << k))
        k++;
    return k;
}

void heap_segment_init(uint32_t seg, uint8_t * payload, uint32_t payload_size,
                       object_handle_t * handles, uint32_t handle_count,
                       uint8_t gc_threshold_pct, uint8_t gc_handle_pct)
{
    heap_segment_t * s = &heap_segments[seg];
    object_space_t * sp = &object_spaces[OBJECT_SPACE_HEAP + seg];
    uint32_t i;

    memset(s, 0, sizeof *s);
    s->payload           = payload;
    s->payload_size      = payload_size;
    s->handles           = handles;
    s->handle_count      = handle_count;
    s->top               = HEAP_SENTINEL_BYTES;
    s->collect_at        = payload_size * gc_threshold_pct / 100u;
    s->handle_collect_at = (handle_count - 1u) * gc_handle_pct / 100u;

    /* Handle 0 is reserved; the rest form the free list, in index order. */
    memset(handles, 0, handle_count * sizeof *handles);
    for (i = 1; i < handle_count; i++) {
        handles[i].offset = (uint16_t)(i + 1u < handle_count ? i + 1u : 0u);
#if OBJECT_DEBUG_REFS
        handles[i].gen = 1;
#endif
    }
    s->handle_free_head = 1;
    s->handle_free_tail = (uint16_t)(handle_count - 1u);

    memset(payload, 0, HEAP_SENTINEL_BYTES);
#if HEAP_POISON
    memset(payload + HEAP_SENTINEL_BYTES, HEAP_POISON_BYTE, payload_size - HEAP_SENTINEL_BYTES);
#endif
    HEAP_ASAN_POISON(payload + HEAP_SENTINEL_BYTES, payload_size - HEAP_SENTINEL_BYTES);

    sp->base    = payload;
    sp->handles = handles;
    sp->limit   = handle_count;
    sp->flags   = OBJECT_SP_COLLECTED | OBJECT_SP_WRITABLE;
    heap_segment_count = seg + 1u;
}

void heap_shutdown(void)
{
    uint32_t seg;

    for (seg = 0; seg < heap_segment_count; seg++)
        HEAP_ASAN_UNPOISON(heap_segments[seg].payload, heap_segments[seg].payload_size);
    memset(heap_segments, 0, sizeof heap_segments);
    heap_segment_count = 0;
}

/* First fit over the address-ordered free list, then the untouched top. 0: no room. */
static uint32_t heap_carve(heap_segment_t * s, uint32_t need)
{
    uint16_t * link = &s->free_head;

    while (*link != 0) {
        uint32_t off = *link;
        heap_free_t * f = heap_free_at(s, off);

        if (f->size >= need) {
            if (f->size > need) {       /* sizes are multiples of 8, so the rest is a block */
                uint32_t rest = off + need;
                heap_free_t * r;

                HEAP_ASAN_UNPOISON(s->payload + rest, HEAP_FREE_HEAD);
                r = heap_free_at(s, rest);
                r->next = f->next;
                r->size = (uint16_t)(f->size - need);
                *link = (uint16_t)rest;
            } else {
                *link = f->next;
            }
            return off;
        }
        link = &f->next;
    }
    if (s->payload_size - s->top >= need) {
        uint32_t off = s->top;

        s->top += need;
        return off;
    }
    return 0;
}

/* Address-ordered insert, coalescing with both neighbours and with the untouched top. */
static void heap_insert_free(heap_segment_t * s, uint32_t start, uint32_t size)
{
    uint16_t * link = &s->free_head;
    uint16_t * prev_link = NULL;
    uint16_t * block_link;
    uint32_t prev = 0;
    uint32_t next;
    uint32_t block;
    uint32_t block_size;

#if HEAP_POISON
    memset(s->payload + start, HEAP_POISON_BYTE, size);
#endif
    while (*link != 0 && *link < start) {
        prev_link = link;
        prev = *link;
        link = &heap_free_at(s, prev)->next;
    }
    next = *link;
    if (next != 0 && start + size == next) {
        const heap_free_t * n = heap_free_at(s, next);

        size += n->size;
        next = n->next;
    }
    if (prev != 0 && prev + heap_free_at(s, prev)->size == start) {
        heap_free_t * p = heap_free_at(s, prev);

        p->size = (uint16_t)(p->size + size);
        p->next = (uint16_t)next;
        block = prev;
        block_size = p->size;
        block_link = prev_link;
    } else {
        heap_free_t * f = heap_free_at(s, start);

        f->next = (uint16_t)next;
        f->size = (uint16_t)size;
        *link = (uint16_t)start;
        block = start;
        block_size = size;
        block_link = link;
    }
    if (block + block_size == s->top) {
        *block_link = 0;                /* nothing free lies above it, so it is the last */
        s->top = block;
        HEAP_ASAN_POISON(s->payload + block, block_size);
    } else {
        HEAP_ASAN_POISON(s->payload + block + HEAP_FREE_HEAD, block_size - HEAP_FREE_HEAD);
    }
}

object_id_t heap_alloc(uint16_t space, uint16_t type, uint8_t kind, uint32_t bytes)
{
    heap_segment_t * s;
    object_handle_t * h;
    uint32_t need;
    uint32_t off = 0;
    uint32_t words = 0;
    uint16_t index;

    if ((uint32_t)space - OBJECT_SPACE_HEAP >= heap_segment_count) {
        objects_corrupt("heap_alloc: not a configured heap segment", OBJECT_NULL);
        return OBJECT_NULL;
    }
    if (kind == OBJECT_K_FREE || kind >= OBJECT_K_COUNT) {
        objects_corrupt("heap_alloc: invalid kind", OBJECT_NULL);
        return OBJECT_NULL;
    }
    s = &heap_segments[space - OBJECT_SPACE_HEAP];

    /* The only OBJECT_NULL return: a request that can never fit (A10). */
    if (bytes > s->payload_size
        || HEAP_ALIGN(bytes) + HEAP_GUARD_BYTES > s->payload_size - HEAP_SENTINEL_BYTES)
        return OBJECT_NULL;
    need = bytes == 0 ? 0u : HEAP_ALIGN(bytes) + HEAP_GUARD_BYTES;

    if ((s->collect_at != 0 && s->used + need > s->collect_at)
        || (s->handle_collect_at != 0 && s->handles_used + 1u > s->handle_collect_at))
        gc_request();

    /* The handle is secured first, so an on_oom that longjmps strands no block. */
    if (s->handle_free_head == 0) {
        gc_collect();
        if (s->handle_free_head == 0) {
            obj_handler_host.on_oom();
            return OBJECT_NULL;
        }
    }
    if (need != 0) {
        off = heap_carve(s, need);
        if (off == 0) {
            gc_collect();
            off = heap_carve(s, need);
            if (off == 0) {
                obj_handler_host.on_oom();
                return OBJECT_NULL;
            }
        }
    }

    index = s->handle_free_head;
    h = &s->handles[index];
    s->handle_free_head = h->offset;
    if (s->handle_free_head == 0)
        s->handle_free_tail = 0;

    if (need != 0) {
        HEAP_ASAN_UNPOISON(s->payload + off, need);
#if HEAP_DEBUG
        {
            heap_guard_t * g = heap_guard_at(s, off);

            g->magic  = HEAP_GUARD_MAGIC;
            g->handle = index;
            g->size   = (uint16_t)need;
            g->check  = heap_guard_check(g);
        }
#endif
        memset(s->payload + off + HEAP_GUARD_BYTES, 0, need - HEAP_GUARD_BYTES);
        h->offset = (uint16_t)(off + HEAP_GUARD_BYTES);
    } else {
        h->offset = 0;
    }
    h->size  = (uint16_t)bytes;
    h->type  = type;
    h->kind  = kind;
    h->flags = OBJECT_H_INUSE;
    if (kind != OBJECT_K_STRING && kind != OBJECT_K_RAW          /* never scanned (A3) */
        && obj_handler_host.type_refmap(type, &words) != NULL)
        h->flags |= OBJECT_H_HASREFS;

    s->used += need;
    if (s->used > s->peak_used)
        s->peak_used = s->used;
    s->handles_used++;
    s->histogram[heap_bucket(HEAP_ALIGN(bytes))]++;
    return OBJECT_ID_GEN(space, OBJECT_HANDLE_GEN(h), index);
}

void heap_release(object_id_t id)
{
    object_handle_t * h = objects_handle_of(id, "heap_release: not a live heap object");
    heap_segment_t * s;
    uint16_t index = OBJECT_INDEX(id);

    if (h == NULL)
        return;
    if (h->flags & OBJECT_H_PINNED) {
        objects_corrupt("heap_release: object is pinned", id);
        return;
    }
    s = &heap_segments[OBJECT_SPACE(id) - OBJECT_SPACE_HEAP];

    if (h->offset != 0) {
        uint32_t need  = HEAP_ALIGN(h->size) + HEAP_GUARD_BYTES;
        uint32_t start = (uint32_t)h->offset - HEAP_GUARD_BYTES;

#if HEAP_DEBUG
        {
            const heap_guard_t * g = heap_guard_at(s, start);

            if (g->magic != HEAP_GUARD_MAGIC || g->check != heap_guard_check(g)
                || g->handle != index || g->size != need) {
                objects_corrupt("heap_release: guard header damaged", id);
                return;
            }
        }
#endif
        heap_insert_free(s, start, need);
        s->used -= need;
    }

    h->offset = 0;
    h->size   = 0;
    h->type   = 0;
    h->kind   = OBJECT_K_FREE;
    h->flags  = 0;
#if OBJECT_DEBUG_REFS
    h->gen = (uint8_t)(h->gen == 255u ? 1u : h->gen + 1u);    /* never 0 (doc 14 section 2.1) */
#endif

    /* Quarantine: a freed handle is reused as late as possible (doc 18 D2). */
    if (s->handle_free_tail != 0)
        s->handles[s->handle_free_tail].offset = index;
    else
        s->handle_free_head = index;
    s->handle_free_tail = index;
    s->handles_used--;
}

void heap_stats(heap_stats_t * out)
{
    uint32_t seg;
    uint32_t k;

    memset(out, 0, sizeof *out);
    for (seg = 0; seg < heap_segment_count; seg++) {
        const heap_segment_t * s = &heap_segments[seg];
        uint32_t tail = s->payload_size - s->top;
        uint32_t off;

        out->payload_bytes   += s->payload_size;
        out->used_bytes      += s->used;
        out->live_objects    += s->handles_used;
        out->free_handles    += s->handle_count - 1u - s->handles_used;
        out->peak_used_bytes += s->peak_used;
        for (off = s->free_head; off != 0; off = heap_free_at(s, off)->next) {
            uint32_t size = heap_free_at(s, off)->size;

            out->free_bytes += size;
            out->free_blocks++;
            if (size > out->largest_free)
                out->largest_free = size;
        }
        if (tail != 0) {
            out->free_bytes += tail;
            out->free_blocks++;
            if (tail > out->largest_free)
                out->largest_free = tail;
        }
        for (k = 0; k < HEAP_HISTOGRAM_BUCKETS; k++)
            out->histogram[k] += s->histogram[k];
    }
}

uint32_t heap_in_use(void)
{
    uint32_t seg;
    uint32_t used = 0;

    for (seg = 0; seg < heap_segment_count; seg++)
        used += heap_segments[seg].used;
    return used;
}

uint32_t heap_largest_free(void)
{
    heap_stats_t st;

    heap_stats(&st);
    return st.largest_free;
}

/* ---- heap_verify: the invariant checker (doc 18 section 4, item 4) --- */

static int heap_fail(const char * why, object_id_t id)
{
    objects_corrupt(why, id);
    return 0;
}

static int heap_verify_segment(uint32_t seg)
{
    const heap_segment_t * s = &heap_segments[seg];
    const uint16_t space = (uint16_t)(OBJECT_SPACE_HEAP + seg);
    uint32_t prev_end = 0;
    uint32_t free_bytes = 0;
    uint32_t live_bytes = 0;
    uint32_t live = 0;
    uint32_t live_blocks = 0;
    uint32_t free_handles = 0;
    uint32_t off;
    uint32_t i;
    uint16_t hi;
    uint16_t last = 0;

    if (s->top < HEAP_SENTINEL_BYTES || s->top > s->payload_size || (s->top & 7u) != 0)
        return heap_fail("heap_verify: top out of range", OBJECT_NULL);

    for (off = s->free_head; off != 0; off = heap_free_at(s, off)->next) {
        const heap_free_t * f;

        if (off < HEAP_SENTINEL_BYTES || off >= s->top || (off & 7u) != 0)
            return heap_fail("heap_verify: free block out of range", OBJECT_NULL);
        if (prev_end != 0 && off < prev_end)
            return heap_fail("heap_verify: free list not address-ordered", OBJECT_NULL);
        if (prev_end != 0 && off == prev_end)
            return heap_fail("heap_verify: adjacent free blocks not coalesced", OBJECT_NULL);
        f = heap_free_at(s, off);
        if (f->size < 8u || (f->size & 7u) != 0 || off + f->size > s->top)
            return heap_fail("heap_verify: free block size", OBJECT_NULL);
        prev_end = off + f->size;
        free_bytes += f->size;
    }
    if (prev_end == s->top)
        return heap_fail("heap_verify: last free block not merged into the top", OBJECT_NULL);

    for (i = 1; i < s->handle_count; i++) {
        const object_handle_t * h = &s->handles[i];
        const object_id_t id = OBJECT_ID_GEN(space, OBJECT_HANDLE_GEN(h), i);
        uint32_t start;

#if OBJECT_DEBUG_REFS
        if (h->gen == 0)
            return heap_fail("heap_verify: handle generation 0", id);
#endif
        if (!(h->flags & OBJECT_H_INUSE)) {
            if (h->flags != 0 || h->kind != OBJECT_K_FREE)
                return heap_fail("heap_verify: free handle keeps flags or kind", id);
            continue;
        }
        if (h->kind == OBJECT_K_FREE || h->kind >= OBJECT_K_COUNT)
            return heap_fail("heap_verify: live handle has an invalid kind", id);
        live++;
        if (h->offset == 0) {
            if (h->size != 0)
                return heap_fail("heap_verify: sized object without a block", id);
            continue;
        }
        start = (uint32_t)h->offset - HEAP_GUARD_BYTES;
        if (h->size == 0 || (h->offset & 7u) != 0
            || h->offset < HEAP_SENTINEL_BYTES + HEAP_GUARD_BYTES
            || start + HEAP_ALIGN(h->size) + HEAP_GUARD_BYTES > s->top)
            return heap_fail("heap_verify: live block out of range", id);
        live_bytes += HEAP_ALIGN(h->size) + HEAP_GUARD_BYTES;
        live_blocks++;
    }

    for (hi = s->handle_free_head; hi != 0; hi = s->handles[hi].offset) {
        if (hi >= s->handle_count || (s->handles[hi].flags & OBJECT_H_INUSE))
            return heap_fail("heap_verify: handle free list corrupt", OBJECT_NULL);
        if (++free_handles > s->handle_count - 1u)
            return heap_fail("heap_verify: handle free list cycles", OBJECT_NULL);
        last = hi;
    }
    if (last != s->handle_free_tail)
        return heap_fail("heap_verify: handle free-list tail", OBJECT_NULL);
    if (live != s->handles_used || live + free_handles != s->handle_count - 1u)
        return heap_fail("heap_verify: handle accounting", OBJECT_NULL);
    if (live_bytes != s->used)
        return heap_fail("heap_verify: used bytes", OBJECT_NULL);
    if (live_bytes + free_bytes != s->top - HEAP_SENTINEL_BYTES)
        return heap_fail("heap_verify: blocks do not cover the payload", OBJECT_NULL);

#if HEAP_DEBUG
    /* Guard headers make an exact address walk possible: no gap, no overlap. */
    {
        uint32_t p = HEAP_SENTINEL_BYTES;
        uint32_t next_free = s->free_head;
        uint32_t blocks = 0;

        while (p < s->top) {
            const heap_guard_t * g;
            const object_handle_t * h;

            if (p == next_free) {
                const heap_free_t * f = heap_free_at(s, p);

                next_free = f->next;
                p += f->size;
                continue;
            }
            g = heap_guard_at(s, p);
            if (g->magic != HEAP_GUARD_MAGIC || g->check != heap_guard_check(g)
                || g->handle == 0 || g->handle >= s->handle_count)
                return heap_fail("heap_verify: guard header damaged", OBJECT_NULL);
            h = &s->handles[g->handle];
            if (!(h->flags & OBJECT_H_INUSE) || h->offset != p + HEAP_GUARD_BYTES
                || g->size != HEAP_ALIGN(h->size) + HEAP_GUARD_BYTES)
                return heap_fail("heap_verify: guard does not match its handle",
                                 OBJECT_ID_GEN(space, OBJECT_HANDLE_GEN(h), g->handle));
            p += g->size;
            blocks++;
        }
        if (p != s->top || next_free != 0 || blocks != live_blocks)
            return heap_fail("heap_verify: address walk does not end at the top", OBJECT_NULL);
    }
#else
    (void)live_blocks;
#endif
    return 1;
}

int heap_verify(void)
{
    uint32_t seg;

    for (seg = 0; seg < heap_segment_count; seg++)
        if (!heap_verify_segment(seg))
            return 0;
    return 1;
}
