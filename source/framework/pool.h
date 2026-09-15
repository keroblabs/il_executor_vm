#ifndef __POOL_DATA_H__
#define __POOL_DATA_H__

////////////////////////////////////////////////////////////////////////////////////////

#include "types.h"

////////////////////////////////////////////////////////////////////////////////////////

#define POOL_ELEMENT_SIZEOF(t)      (sizeof(t) + sizeof(pool_element_t))

#define DECLARE_POOL(t,c)           static pool_data_t _##t##_pool_data;                                \
                                    static uint8_t     _##t##_pool_buffer[POOL_ELEMENT_SIZEOF(t) * c]

#define DECLARE_POOL_ATTR(t,c,a)    static pool_data_t _##t##_pool_data;                                \
                                    a static uint8_t     _##t##_pool_buffer[POOL_ELEMENT_SIZEOF(t) * c]

#define CREATE_POOL(t)              pool_create(&_##t##_pool_data, sizeof(t), _##t##_pool_buffer, sizeof(_##t##_pool_buffer));

#define GET_POOL_DATA(t)            &_##t##_pool_data

#define POOL_CALLBACK(cb)           ((pool_search_hdl)cb)

////////////////////////////////////////////////////////////////////////////////////////

typedef struct pool_element_t_
{
    struct pool_element_t_ * p_next;
    uint8_t data[];

} pool_element_t;

typedef struct
{
    pool_element_t * queue;
    pool_element_t * tail;

} queue_data_t;

typedef struct
{
    uint32_t element_size;

    pool_element_t * free_pool;
    pool_element_t * last_free;
    queue_data_t     active;

} pool_data_t;

typedef bool_t (*pool_search_hdl)(void * element, void * p_data);

////////////////////////////////////////////////////////////////////////////////////////

void   pool_create(pool_data_t * pool_data, uint32_t element_size, void * buffer, uint32_t buffer_size);

void * pool_malloc_element(pool_data_t * pool_data);
void   pool_add_to_queue(pool_data_t * pool_data, void * p_element, queue_data_t * queue, pool_search_hdl add_hdl);
void   pool_remove_from_queue(pool_data_t * pool_data, queue_data_t * queue, void * p_element);
void   pool_free_element(pool_data_t * pool_data, void ** p_element);

void * pool_peek_head(pool_data_t * pool_data, queue_data_t * queue);
void * pool_get_from_head(pool_data_t * pool_data, queue_data_t * queue);
void * pool_locate_element(pool_data_t * pool_data, queue_data_t * queue, void * p_element, pool_search_hdl search_hdl);

void * pool_get_chain_element(pool_data_t * pool_data, queue_data_t * queue, void * p_element);

void pool_push_to_tail(pool_data_t * pool_data, void * p_element, queue_data_t * queue);

void * pool_pop_from_tail(pool_data_t * pool_data, queue_data_t * queue);
void * pool_get_element_at(pool_data_t * pool_data, queue_data_t * queue, uint32_t index);

////////////////////////////////////////////////////////////////////////////////////////

#endif // __POOL_DATA_H__
