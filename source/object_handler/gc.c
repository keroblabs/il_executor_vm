#include "gc.h"

/* OH-2 stub: nothing can be marked until OH-3 brings the roots (doc 18 section 4). */

static int gc_requested;

void gc_collect(void)
{
    gc_requested = 0;
}

void gc_request(void)
{
    gc_requested = 1;
}
