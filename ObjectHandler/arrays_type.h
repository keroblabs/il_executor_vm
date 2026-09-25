#ifndef ARRAYS_TYPE_H
#define	ARRAYS_TYPE_H

#ifdef	__cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////

#include "instance.h"

///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////

object_t array_create( uint32_t length, uint8_t mode );
uint32_t array_length( object_t object );
void *   array_range_ptr( object_t object, uint32_t index, uint32_t count );   /* NULL if out of range, already reported */

static INLINE void * array_element_ptr( object_t object, uint32_t index )
{
	return array_range_ptr( object, index, 1 );
}

///////////////////////////////////////////////////////////////////////////

#ifdef	__cplusplus
}
#endif

#endif	/* ARRAYS_TYPE_H */

