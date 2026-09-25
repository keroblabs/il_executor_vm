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
object_t array_create_data( uint32_t length, uint32_t item_size );

uint32_t array_get_capacity( object_t object );
uint32_t array_get_type( object_t object );

void array_insert_data( object_t object, uint32_t position, uint32_t item_size, void * data );

uint32_t array_get_element_size( uint8_t mode );

void array_destroy( object_t object );

void array_copy( object_t object, uint32_t position, void * new_array, uint32_t new_size );
void array_get( object_t object, uint32_t position, uint32_t len, void * output );
void array_insert( object_t object, uint32_t position, void * input, uint32_t start, uint32_t len );
void array_delete( object_t object, uint32_t position, uint32_t len );

///////////////////////////////////////////////////////////////////////////

#ifdef	__cplusplus
}
#endif

#endif	/* ARRAYS_TYPE_H */

