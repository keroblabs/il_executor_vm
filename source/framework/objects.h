#ifndef __OBJECTS_H__
#define __OBJECTS_H__

#include "types.h"

typedef struct object_header_t_
{
    uint32_t class_ref;
    uint32_t class_type;
    uint32_t field_count;
    uint32_t fields[];

} object_header_t;

typedef struct object_heap_t_
{
    uint32_t length;
    object_header_t * class_header;

} object_heap_t;

typedef struct string_heap_t_
{
    uint32_t length;
    char * buffer;

} string_heap_t;

typedef struct object_t_
{
    uint32_t element_type; // ELEMENT_TYPE_* with nested type flags (e.g., ELEMENT_TYPE_NESTED_PTR)

    union
    {
        object_heap_t heap;
        string_heap_t string;
        int64_t i_value;
        uint64_t u_value;
        float f_value;
        double d_value;
    } o;

} object_t;



#endif // __OBJECTS_H__
