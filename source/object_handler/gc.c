#include "gc.h"

/* OH-2 stub: nothing can be marked until OH-3 brings the roots (doc 18 section 4). */

void gc_collect(obj_handler_t * oh)
{
    oh->gc_requested = 0;
}

void gc_request(obj_handler_t * oh)
{
    oh->gc_requested = 1;
}
