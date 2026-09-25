#ifndef OBJECTS_H
#define	OBJECTS_H

#ifdef	__cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////

#include "instance.h"

///////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////

#define _OBJECTS_INCLUDE

#define COLOUR_WHITE		0
#define COLOUR_GREY			1
#define COLOUR_BLACK		2

//////////////////////////////////////////////////////////////////////////

typedef struct
{
	uint32_t cref;
	//object_t properties;
	//uint8_t colour_code;
	//uint8_t reserved;
	object_t fields;

} object_data_t;

typedef struct
{
	uint32_t cref;
	uint8_t data;

} object_base_t;

///////////////////////////////////////////////////////////////////////////

void	 object_garbage_collection( void );
void	 object_delete( object_t object );

object_t object_const_create( uint32_t index );

void     object_create_extension( object_t object, uint32_t size );
void *   object_get_extension( object_t object );
void     object_update_class_ref( object_t object, uint16_t id );

//void	 object_set_colour_code( object_t object, uint8_t colour );
//uint8_t  object_get_colour_code( object_t object );

object_t  object_create( uint32_t cref, uint8_t mode, uint32_t total_fields );
void	  object_resize( object_t object, uint32_t new_total_fields );
uint32_t  object_get_total_fields( object_t object );
object_t * object_get_fields_ptr( object_t object );
uint32_t  object_get_class_ref( object_t object );

//bool_t	  object_search_entry( object_t object, object_t entry );
//bool_t    object_stack_search( object_t entry, instance_t * thread );

object_t  object_data_create( uint8_t mode, uint32_t size );
void      object_data_resize( object_t object, uint32_t new_size );
uint32_t  object_get_size( object_t object );
uint8_t * object_get_ptr( object_t object );
uint8_t   object_get_type( object_t object );

object_t  object_clone( object_t object );

void * object_get_constant( uint32_t index, uint32_t * size, uint8_t * field_ref );

//////////////////////////////////////////////////////////////////////////
    
#ifdef	__cplusplus
}
#endif

#endif	/* OBJECTS_H */

