#include "obj_handler.h"

#if OBJ_STRING_ENCODING != OBJ_STRING_ENCODING_UTF8
#error "only UTF-8 string payloads are implemented (doc 14 section 5.6)"
#endif

/* ldstr: a literal is a constant, read in place (doc 14 section 3.5). */
object_id_t obj_string_intern(obj_handler_t * oh, uint16_t literal_index)
{
    const object_id_t id = OBJECT_ID(OBJECT_SPACE_CONST, literal_index);
    object_info_t info;

    if (!objects_info(oh, id, &info, "obj_string_intern: not a constant"))
        return OBJECT_NULL;
    if (info.kind != OBJECT_K_STRING) {
        objects_corrupt(oh, "obj_string_intern: the constant is not a string", id);
        return OBJECT_NULL;
    }
    return id;
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

/* A live string, heap or constant (doc 14 section 3.5). */
static int obj_string_info(obj_handler_t * oh, object_id_t id, object_info_t * info,
                           const char * where)
{
    if (!objects_info(oh, id, info, where))
        return 0;
    if (info->kind != OBJECT_K_STRING || info->size == 0) {
        objects_corrupt(oh, where, id);
        return 0;
    }
    return 1;
}

/* A string of `bytes` UTF-8 bytes; its NUL is already there, since heap_alloc zeroes. */
static object_id_t obj_string_alloc(obj_handler_t * oh, uint32_t bytes, int ascii)
{
    object_id_t id;

    if (bytes >= HEAP_MAX_PAYLOAD)
        return OBJECT_NULL;
    id = heap_alloc(oh, OBJECT_SPACE_HEAP, oh->string_type, OBJECT_K_STRING, bytes + 1u);
    if (id != OBJECT_NULL && ascii)
        oh->spaces[OBJECT_SPACE(id)].handles[OBJECT_INDEX(id)].flags |= OBJECT_H_ASCII;
    return id;
}

object_id_t obj_string_create(obj_handler_t * oh, const char * utf8, uint32_t len)
{
    object_id_t id;
    int ascii = 0;

    if (utf8 == NULL && len != 0) {
        objects_corrupt(oh, "obj_string_create: NULL bytes", OBJECT_NULL);
        return OBJECT_NULL;
    }
    if (len >= HEAP_MAX_PAYLOAD || !obj_string_valid((const uint8_t *)utf8, len, &ascii))
        return OBJECT_NULL;
    id = obj_string_alloc(oh, len, ascii);
    if (id != OBJECT_NULL && len != 0)
        memcpy(objects_ptr(oh, id), utf8, len);
    return id;
}

object_id_t obj_string_concat(obj_handler_t * oh, object_id_t a, object_id_t b)
{
    object_info_t ia;
    object_info_t ib;
    const int ok_a = obj_string_info(oh, a, &ia, "obj_string_concat: not a live string");
    const int ok_b = obj_string_info(oh, b, &ib, "obj_string_concat: not a live string");
    uint32_t la;
    uint32_t lb;
    uint8_t * p;
    object_id_t id;

    if (!ok_a || !ok_b)
        return OBJECT_NULL;
    la = ia.size - 1u;
    lb = ib.size - 1u;
    id = obj_string_alloc(oh, la + lb, (ia.flags & ib.flags & OBJECT_H_ASCII) != 0);
    if (id == OBJECT_NULL)
        return OBJECT_NULL;
    /* Resolved after the allocation, which may collect: a and b must be rooted. */
    p = (uint8_t *)objects_ptr(oh, id);
    memcpy(p, objects_ptr(oh, a), la);
    memcpy(p + la, objects_ptr(oh, b), lb);
    return id;
}

uint32_t obj_string_length(obj_handler_t * oh, object_id_t id)
{
    object_info_t info;
    const uint8_t * s;
    uint32_t n;
    uint32_t i;
    uint32_t units = 0;

    if (!obj_string_info(oh, id, &info, "obj_string_length: not a live string"))
        return 0;
    n = info.size - 1u;
    if (info.flags & OBJECT_H_ASCII)
        return n;
    s = (const uint8_t *)objects_ptr(oh, id);
    for (i = 0; i < n; i += obj_string_width(s[i]))
        units += s[i] >= 0xF0 ? 2u : 1u;        /* a supplementary character is a surrogate pair */
    return units;
}

int32_t obj_string_char_at(obj_handler_t * oh, object_id_t id, uint32_t index)
{
    object_info_t info;
    const uint8_t * s;
    uint32_t n;
    uint32_t i;
    uint32_t unit = 0;

    if (!obj_string_info(oh, id, &info, "obj_string_char_at: not a live string"))
        return -1;
    n = info.size - 1u;
    s = (const uint8_t *)objects_ptr(oh, id);
    if (info.flags & OBJECT_H_ASCII)
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
