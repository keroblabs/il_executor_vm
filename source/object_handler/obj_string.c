#include "obj_handler.h"

#if OBJ_STRING_ENCODING != OBJ_STRING_ENCODING_UTF8
#error "only UTF-8 string payloads are implemented (doc 14 section 5.6)"
#endif

static uint16_t obj_string_type;

void obj_string_init(uint16_t string_type)
{
    obj_string_type = string_type;
}

/* Bytes in the well-formed sequence at s, or 0 (Unicode 15, table 3-7). */
static uint32_t obj_string_sequence(const uint8_t * s, uint32_t avail)
{
    uint8_t lo = 0x80;
    uint8_t hi = 0xBF;
    uint32_t len;
    uint32_t i;

    if (s[0] < 0x80)
        return 1;
    if (s[0] < 0xC2)                    /* a continuation byte, or an overlong lead */
        return 0;
    if (s[0] < 0xE0) {
        len = 2;
    } else if (s[0] < 0xF0) {
        len = 3;
        if (s[0] == 0xE0)
            lo = 0xA0;                  /* overlong */
        else if (s[0] == 0xED)
            hi = 0x9F;                  /* a surrogate */
    } else if (s[0] < 0xF5) {
        len = 4;
        if (s[0] == 0xF0)
            lo = 0x90;                  /* overlong */
        else if (s[0] == 0xF4)
            hi = 0x8F;                  /* above U+10FFFF */
    } else {
        return 0;
    }
    if (len > avail || s[1] < lo || s[1] > hi)
        return 0;
    for (i = 2; i < len; i++)
        if (s[i] < 0x80 || s[i] > 0xBF)
            return 0;
    return len;
}

static int obj_string_valid(const uint8_t * s, uint32_t n, int * ascii)
{
    uint32_t i = 0;

    *ascii = 1;
    while (i < n) {
        uint32_t len = obj_string_sequence(s + i, n - i);

        if (len == 0)
            return 0;
        if (len > 1)
            *ascii = 0;
        i += len;
    }
    return 1;
}

/* Sequence width from the lead byte of a string already validated. */
static uint32_t obj_string_width(uint8_t lead)
{
    return lead < 0x80 ? 1u : lead < 0xE0 ? 2u : lead < 0xF0 ? 3u : 4u;
}

static uint32_t obj_string_decode(const uint8_t * s, uint32_t width)
{
    switch (width) {
    case 1:
        return s[0];
    case 2:
        return ((uint32_t)(s[0] & 0x1Fu) << 6) | (s[1] & 0x3Fu);
    case 3:
        return ((uint32_t)(s[0] & 0x0Fu) << 12) | ((uint32_t)(s[1] & 0x3Fu) << 6)
             | (s[2] & 0x3Fu);
    default:
        return ((uint32_t)(s[0] & 0x07u) << 18) | ((uint32_t)(s[1] & 0x3Fu) << 12)
             | ((uint32_t)(s[2] & 0x3Fu) << 6) | (s[3] & 0x3Fu);
    }
}

static const object_handle_t * obj_string_handle(object_id_t id, const char * where)
{
    const object_handle_t * h = objects_handle_of(id, where);

    if (h == NULL)
        return NULL;
    if (h->kind != OBJECT_K_STRING || h->size == 0) {
        objects_corrupt(where, id);
        return NULL;
    }
    return h;
}

/* A string of `bytes` UTF-8 bytes; its NUL is already there, since heap_alloc zeroes. */
static object_id_t obj_string_alloc(uint32_t bytes, int ascii)
{
    object_id_t id;

    if (bytes >= HEAP_MAX_PAYLOAD)
        return OBJECT_NULL;
    id = heap_alloc(OBJECT_SPACE_HEAP, obj_string_type, OBJECT_K_STRING, bytes + 1u);
    if (id != OBJECT_NULL && ascii)
        object_spaces[OBJECT_SPACE(id)].handles[OBJECT_INDEX(id)].flags |= OBJECT_H_ASCII;
    return id;
}

object_id_t obj_string_create(const char * utf8, uint32_t len)
{
    object_id_t id;
    int ascii = 0;

    if (utf8 == NULL && len != 0) {
        objects_corrupt("obj_string_create: NULL bytes", OBJECT_NULL);
        return OBJECT_NULL;
    }
    if (len >= HEAP_MAX_PAYLOAD || !obj_string_valid((const uint8_t *)utf8, len, &ascii))
        return OBJECT_NULL;
    id = obj_string_alloc(len, ascii);
    if (id != OBJECT_NULL && len != 0)
        memcpy(objects_ptr(id), utf8, len);
    return id;
}

object_id_t obj_string_concat(object_id_t a, object_id_t b)
{
    const object_handle_t * ha = obj_string_handle(a, "obj_string_concat: not a live string");
    const object_handle_t * hb = obj_string_handle(b, "obj_string_concat: not a live string");
    uint32_t la;
    uint32_t lb;
    uint8_t * p;
    object_id_t id;

    if (ha == NULL || hb == NULL)
        return OBJECT_NULL;
    la = ha->size - 1u;
    lb = hb->size - 1u;
    id = obj_string_alloc(la + lb, (ha->flags & hb->flags & OBJECT_H_ASCII) != 0);
    if (id == OBJECT_NULL)
        return OBJECT_NULL;
    /* Resolved after the allocation, which may collect: a and b must be rooted. */
    p = (uint8_t *)objects_ptr(id);
    memcpy(p, objects_ptr(a), la);
    memcpy(p + la, objects_ptr(b), lb);
    return id;
}

uint32_t obj_string_length(object_id_t id)
{
    const object_handle_t * h = obj_string_handle(id, "obj_string_length: not a live string");
    const uint8_t * s;
    uint32_t n;
    uint32_t i;
    uint32_t units = 0;

    if (h == NULL)
        return 0;
    n = h->size - 1u;
    if (h->flags & OBJECT_H_ASCII)
        return n;
    s = (const uint8_t *)objects_ptr(id);
    for (i = 0; i < n; i += obj_string_width(s[i]))
        units += s[i] >= 0xF0 ? 2u : 1u;        /* a supplementary character is a surrogate pair */
    return units;
}

int32_t obj_string_char_at(object_id_t id, uint32_t index)
{
    const object_handle_t * h = obj_string_handle(id, "obj_string_char_at: not a live string");
    const uint8_t * s;
    uint32_t n;
    uint32_t i;
    uint32_t unit = 0;

    if (h == NULL)
        return -1;
    n = h->size - 1u;
    s = (const uint8_t *)objects_ptr(id);
    if (h->flags & OBJECT_H_ASCII)
        return index < n ? (int32_t)s[index] : -1;

    for (i = 0; i < n; ) {
        const uint32_t width = obj_string_width(s[i]);
        const uint32_t cp = obj_string_decode(s + i, width);

        if (cp < 0x10000u) {
            if (unit == index)
                return (int32_t)cp;
            unit++;
        } else {
            if (unit == index)
                return (int32_t)(0xD800u + ((cp - 0x10000u) >> 10));
            if (unit + 1u == index)
                return (int32_t)(0xDC00u + ((cp - 0x10000u) & 0x3FFu));
            unit += 2u;
        }
        i += width;
    }
    return -1;
}
