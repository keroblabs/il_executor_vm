#include "pool.h"
#include "exception.h"
#include "arch.h"
#include "critical_section.h"

void pool_create(pool_data_t * pool_data, uint32_t element_size, void * buffer, uint32_t buffer_size)
{
    uint8_t * temp_buffer = (uint8_t *)buffer;
    uint32_t temp_index = 0;

    pool_data->element_size = element_size;
    pool_data->free_pool = NULL;
    pool_data->last_free = NULL;
    pool_data->active.queue = NULL;

    while (temp_index < buffer_size)
    {
        pool_element_t * element = (pool_element_t *)&temp_buffer[temp_index];

        element->p_next = pool_data->free_pool;
        pool_data->free_pool = element;

        if (pool_data->last_free == NULL)
        {
            pool_data->last_free = element;
        }

        temp_index += (element_size + sizeof(pool_element_t));
    }
}

void * pool_malloc_element(pool_data_t * pool_data)
{
    void * ret = NULL;

    critical_section_start();

    if (pool_data->free_pool != NULL)
    {
        pool_element_t * element = pool_data->free_pool;
        pool_data->free_pool = element->p_next;

        if (pool_data->free_pool == NULL)
        {
            pool_data->last_free = NULL;
        }

        element->p_next = element;
        ret = element->data;
    }

    critical_section_end();

    DO_ASSERT(ret != NULL);

    return ret;
}

void * pool_peek_head(pool_data_t * pool_data, queue_data_t * queue)
{
    critical_section_start();
    queue_data_t * this_queue = queue ? queue : &pool_data->active;
    uint8_t * ret = this_queue->queue ? this_queue->queue->data : NULL;
    critical_section_end();
    return ret;
}

void * pool_get_from_head(pool_data_t * pool_data, queue_data_t * queue)
{
    critical_section_start();
    void * ret = pool_peek_head(pool_data, queue);

    if (ret != NULL)
    {
        pool_remove_from_queue(pool_data, queue, ret);
    }
    critical_section_end();

    return ret;
}

void pool_push_to_tail(pool_data_t * pool_data, void * p_element, queue_data_t * queue)
{
    critical_section_start();

    queue_data_t   * this_queue     = queue ? queue : &pool_data->active;
    pool_element_t * this_element   = (pool_element_t *)((void *)((n_ptr_t)p_element - sizeof(pool_element_t)));

    if (this_element->p_next != this_element)
    {
        pool_remove_from_queue(pool_data, this_queue, p_element);
    }

    if (this_queue->tail != NULL)
    {
        this_queue->tail->p_next = this_element;
    }
    else
    {
        this_queue->queue = this_element;
    }

    this_queue->tail = this_element;
    this_element->p_next = NULL;

    critical_section_end();
}

void * pool_pop_from_tail(pool_data_t * pool_data, queue_data_t * queue)
{
    critical_section_start();

    queue_data_t   * this_queue     = queue ? queue : &pool_data->active;
    pool_element_t * search_element = this_queue->queue;
    pool_element_t * prev_element   = NULL;
    void * ret = NULL;

    if (search_element != NULL)
    {
        while (search_element->p_next != NULL)
        {
            prev_element = search_element;
            search_element = search_element->p_next;
        }

        ret = search_element->data;

        if (prev_element != NULL)
        {
            prev_element->p_next = NULL;
            this_queue->tail = prev_element;
        }
        else
        {
            this_queue->queue = NULL;
            this_queue->tail = NULL;
        }
    }

    critical_section_end();

    return ret;
}

void * pool_get_element_at(pool_data_t * pool_data, queue_data_t * queue, uint32_t index)
{
    critical_section_start();

    queue_data_t   * this_queue     = queue ? queue : &pool_data->active;
    pool_element_t * search_element = this_queue->queue;
    void * ret = NULL;

    while (search_element && index > 0)
    {
        search_element = search_element->p_next;
        index--;
    }

    if (search_element)
    {
        ret = search_element->data;
    }

    critical_section_end();

    return ret;
}

void pool_add_to_queue(pool_data_t * pool_data, void * p_element, queue_data_t * queue, pool_search_hdl add_hdl)
{
    critical_section_start();

    queue_data_t   * this_queue     = queue ? queue : &pool_data->active;
    pool_element_t * this_element   = (pool_element_t *)((void *)((n_ptr_t)p_element - sizeof(pool_element_t)));
    pool_element_t * search_element = this_queue->queue;
    pool_element_t * prev_element   = NULL;
    bool_t is_added = FALSE;

    if (this_element->p_next != this_element)
    {
        pool_remove_from_queue(pool_data, this_queue, p_element);
    }

    this_element->p_next = NULL;

    while (search_element)
    {
        if (add_hdl && add_hdl(search_element->data, p_element))
        {
            if (prev_element != NULL)
            {
                this_element->p_next = prev_element->p_next;
                prev_element->p_next = this_element;
            }
            else
            {
                this_element->p_next = this_queue->queue;
                this_queue->queue = this_element;
            }

            is_added = TRUE;
            break;
        }
        prev_element = search_element;
        search_element = search_element->p_next;
    }

    if (is_added == FALSE)
    {
        if (prev_element != NULL)
        {
            prev_element->p_next = this_element;
        }
        else
        {
            this_queue->queue = this_element;
        }
    }

    critical_section_end();
}

void * pool_get_chain_element(pool_data_t * pool_data, queue_data_t * queue, void * p_element)
{
    pool_element_t * this_element = NULL;
    void * ret = NULL;

    critical_section_start();

    if (p_element == NULL)
    {
        ret = pool_peek_head(pool_data, queue);
    }
    else
    {
        this_element = (pool_element_t *)((void *)((n_ptr_t)p_element - sizeof(pool_element_t)));

        if (this_element->p_next != this_element)
        {
            ret = this_element->p_next->data;
        }
    }

    critical_section_end();

    return ret;
}

void * pool_locate_element(pool_data_t * pool_data, queue_data_t * queue, void * p_element, pool_search_hdl search_hdl)
{
    critical_section_start();

    queue_data_t   * this_queue     = queue ? queue : &pool_data->active;
    pool_element_t * search_element = this_queue->queue;
    uint8_t * ret_data = NULL;

    while (search_element && search_hdl)
    {
        if (search_hdl(search_element->data, p_element))
        {
            ret_data = search_element->data;
            break;
        }

        search_element = search_element->p_next;
    }

    critical_section_end();

    return ret_data;
}

void pool_remove_from_queue(pool_data_t * pool_data, queue_data_t * queue, void * p_element)
{
    critical_section_start();

    queue_data_t   * this_queue     = queue ? queue : &pool_data->active;
    pool_element_t * this_element   = (pool_element_t *)((void *)((n_ptr_t)p_element - sizeof(pool_element_t)));
    pool_element_t * search_element = this_queue->queue;
    pool_element_t * prev_element   = NULL;

    if (this_element->p_next == this_element)
    {
        critical_section_end();
        return;
    }

    while (search_element)
    {
        if (search_element->data == p_element)
        {
            if (prev_element != NULL)
            {
                prev_element->p_next = search_element->p_next;
            }
            else
            {
                this_queue->queue = this_element->p_next;
            }

            this_element->p_next = this_element;

            break;
        }
        prev_element = search_element;
        search_element = search_element->p_next;
    }

    critical_section_end();
}

void pool_free_element(pool_data_t * pool_data, void ** p_element)
{
    critical_section_start();

    pool_element_t * this_element   = (pool_element_t *)((void *)((n_ptr_t)(*p_element) - sizeof(pool_element_t)));
    pool_element_t * prev_element   = NULL;

    DO_ASSERT(this_element->p_next == this_element);

    if (pool_data->last_free != NULL)
    {
        pool_data->last_free->p_next = this_element;
    }
    else
    {
        pool_data->free_pool = this_element;
    }

    pool_data->last_free = this_element;

    *p_element = NULL;

    critical_section_end();
}
