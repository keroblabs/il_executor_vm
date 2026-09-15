#include "stack.h"

void stack_init(stack_data_t * stack_data, uint8_t * buffer, uint32_t buffer_size)
{
    stack_data->stack_buffer = buffer;
    stack_data->stack_size = buffer_size;

    pool_create(stack_data->pool_data, sizeof(stack_node_t), stack_data->stack_buffer, stack_data->stack_size);
}

void stack_push(stack_data_t * stack_data, stack_node_t * data)
{

}

stack_node_t * stack_pop(stack_data_t * stack_data)
{

}

stack_node_t * stack_peek_at(stack_data_t * stack_data, uint32_t index)
{

}
