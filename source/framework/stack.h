#ifndef _STACK_H_
#define _STACK_H_

///////////////////////////////////////////////////////////////////////////

#include "pool.h"

///////////////////////////////////////////////////////////////////////////

typedef struct stack_node_t_
{
    uint8_t type[2];
    uint32_t object[4];

} stack_node_t;

typedef struct
{
    pool_data_t * pool_data;
    queue_data_t * queue;

    uint8_t  * stack_buffer;
    uint32_t   stack_size;

} stack_data_t;

///////////////////////////////////////////////////////////////////////////

void stack_init(stack_data_t * stack_data, uint8_t * buffer, uint32_t buffer_size);
void stack_push(stack_data_t * stack_data, stack_node_t * data);

stack_node_t * stack_pop(stack_data_t * stack_data);
stack_node_t * stack_peek(stack_data_t * stack_data);
stack_node_t * stack_peek_at(stack_data_t * stack_data, uint32_t index);

//////////////////////////////////////////////////////////////////////////

#endif // _STACK_H_
