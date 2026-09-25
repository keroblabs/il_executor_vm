#ifndef _HEAP_MMU_H_
#define _HEAP_MMU_H_

///////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "instance.h"

///////////////////////////////////////////////////////////////////////////
    
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
    
void     heap_init( uint8_t * buffer, uint32_t size );
bool_t   heap_object_resize( object_t id, uint32_t new_size );
void *   heap_object_locate( object_t id, uint32_t * size, uint8_t * field_ref );
object_t heap_object_malloc( uint32_t size, uint8_t field_ref );
void *   heap_malloc( uint32_t size );
void     heap_check( void );
void     heap_free( void * p_mem );
void	 heap_object_free( object_t object );

void	 heap_dump( void );
void	 heap_dump_object( object_t object );

object_t heap_get_chain( object_t id );

uint32_t heap_in_use( void );
uint32_t heap_size( void );

//////////////////////////////////////////////////////////////////////////

#endif // _HEAP_MMU_H_
