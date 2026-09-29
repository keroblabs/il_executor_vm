#ifndef GC_H
#define GC_H

#include "objects_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void gc_collect(obj_handler_t * oh);
void gc_request(obj_handler_t * oh);        /* collect at the next safepoint (doc 14 section 7.3) */
/* stack.c registers the scan of every managed thread's slots (doc 14 section 7.4). */
void gc_register_root_scan(obj_handler_t * oh, gc_scan_fn scan);

#ifdef __cplusplus
}
#endif

#endif /* GC_H */
