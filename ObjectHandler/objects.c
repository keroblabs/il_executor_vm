#include "objects.h"
#include "heap.h"
#include "arrays_type.h"
//#include "vm_debug.h"

///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////

//static object_t obj_searched[ 20 ];
//static uint16_t obj_search_index = 0;

///////////////////////////////////////////////////////////////////////////

#ifndef DEBUG_GC
#define DEBUG_GC(...)
#endif

///////////////////////////////////////////////////////////////////////////

object_t _object_create_internal( uint32_t cref, uint8_t mode, uint32_t size )
{
	object_t object;
	object_data_t * ptr_obj;

	object = ( object_t )heap_object_malloc( size, mode );
	if( object == 0 )
	{
		// need some memory
		object_garbage_collection();

		// try again
		object = ( object_t )heap_object_malloc( size, mode );

		// out of memory
		if( object == 0 )
		{
			instance_error_memory_overflow();
			// instance_er
			return 0;
		}
	}

	if( mode == T_CLASS )
	{
		// set information
		ptr_obj = ( object_data_t * )heap_object_locate( object, NULL, NULL );
		ptr_obj->cref = cref;
	}

	// all created objects are white
	//ptr_obj->colour_code = COLOUR_WHITE;

	return object;
}

void _object_resize_internal( object_t object, uint32_t new_size )
{
	if( heap_object_resize( ( uint16_t )object, new_size ) == FALSE )
	{
		// need some memory
		object_garbage_collection();

		// try again
		if( heap_object_resize( ( uint16_t )object, new_size ) == FALSE )
		{
			instance_error_memory_overflow();
		}
	}
}

///////////////////////////////////////////////////////////////////////////

void object_delete( object_t object )
{
	heap_object_free( object );
}

object_t object_const_create( uint32_t index )
{
	return( ( index + CONST_OBJECT_ID ) & 0x0FFFF );
}

///////////////////////////////////////////////////////////////////////////

object_t object_create( uint32_t cref, uint8_t mode, uint32_t total_fields )
{
	uint32_t size = ( ( total_fields - 1 ) * sizeof( object_t ) ) + sizeof( object_data_t );
	return _object_create_internal( cref, mode, size );
}

void object_resize( object_t object, uint32_t new_total_fields )
{
	uint32_t new_size;
    
	new_size = ( ( new_total_fields - 1 ) * sizeof( object_t ) ) + sizeof( object_data_t );
	_object_resize_internal( object, new_size );
}

void object_create_extension( object_t object, uint32_t size )
{
	uint32_t total_fields;
	object_t * fields;
	object_t extension;
    
	// add one more field
	total_fields = object_get_total_fields( object ) + 1;
	object_resize( object, total_fields );

	// create extension
	extension = _object_create_internal( 0, T_DATA, size );

	// get extended field
	total_fields = object_get_total_fields( object );
	fields = object_get_fields_ptr( object );
	fields[ total_fields - 1 ] = extension;

	DEBUG_INFO("Created extension %04x for %04x\n", extension, object );
}

void * object_get_extension( object_t object )
{
	uint32_t field_index;
	object_t * fields;
	object_t extension;
    
	// add one more field
	field_index = object_get_total_fields( object );

	if( field_index == 0 )
	{
		instance_error_invalid_ref();
		return NULL;
	}

	// get extended field
	fields = object_get_fields_ptr( object );
	extension = fields[ field_index - 1 ];

	// return address of the object
	return object_get_ptr( extension );
}

uint32_t object_get_total_fields( object_t object )
{
	uint32_t size = 0;
	uint8_t field_ref = 0;
	uint32_t fields;
    
	heap_object_locate( object, &size, &field_ref );

	if( field_ref == T_CLASS )
	{
		fields = size - sizeof( object_data_t ) + sizeof( object_t );
		return fields / sizeof( object_t );
	}

	// it's not a class
	instance_error_invalid_ref();
	return 0;
}

object_t * object_get_fields_ptr( object_t object )
{
	uint32_t size = 0;
	uint8_t field_ref = 0;
	object_t * fields_ptr;
	object_data_t * obj_data;
    
	obj_data = ( object_data_t * )heap_object_locate( object, &size, &field_ref );

	if( field_ref == T_CLASS )
	{
		fields_ptr = &obj_data->fields;
		return fields_ptr;
	}

	// it's not a class
	instance_error_invalid_ref();
	return NULL;
}

void object_update_class_ref( object_t object, uint16_t id )
{
	uint32_t size = 0;
	uint8_t field_ref = 0;
	object_data_t * obj_data;
    
	obj_data = ( object_data_t * )heap_object_locate( object, &size, &field_ref );

	if( field_ref == T_CLASS )
	{
		obj_data->cref = ( obj_data->cref & 0xFFFF0000 ) | id;
        return;
	}

	// it's not a class
	instance_error_invalid_ref();    
}

uint32_t object_get_class_ref( object_t object )
{
	uint32_t size = 0;
	uint8_t field_ref = 0;
	object_data_t * obj_data;
    
	obj_data = ( object_data_t * )heap_object_locate( object, &size, &field_ref );

	if( field_ref == T_CLASS )
	{
		return obj_data->cref;
	}

	// it's not a class
	instance_error_invalid_ref();
	return 0;
}

///////////////////////////////////////////////////////////////////////////

object_t object_data_create( uint8_t mode, uint32_t size )
{
	return _object_create_internal( 0, mode, size );
}

void object_data_resize( object_t object, uint32_t new_size )
{
	if( ( uint16_t )object >= CONST_OBJECT_ID )
	{
		// invalid for const objects
		instance_error_invalid_ref();
		return;
	}

	_object_resize_internal( object, new_size );
}

uint32_t object_get_size( object_t object )
{
	uint32_t size = 0;
	uint8_t field_ref = 0;

	if( object >= CONST_OBJECT_ID )
	{
		object_get_constant(object - CONST_OBJECT_ID, &size, &field_ref );
	}
	else
	{
		heap_object_locate( object, &size, &field_ref );
	}

	if( field_ref != T_CLASS )
	{
		return size;
	}

	// it's not a class
	instance_error_invalid_ref();
	return 0;
}

uint8_t object_get_type( object_t object )
{
	uint8_t field_ref = 0;

	if( object >= CONST_OBJECT_ID )
	{
		object_get_constant(object - CONST_OBJECT_ID, NULL, &field_ref );
	}
	else
	{
		heap_object_locate( object, NULL, &field_ref );
	}

	return field_ref;
}

uint8_t * object_get_ptr( object_t object )
{
	uint32_t size = 0;
	uint8_t field_ref = 0;
	uint8_t * obj_data;

	if( object >= CONST_OBJECT_ID )
	{
		obj_data = ( uint8_t * )object_get_constant(object - CONST_OBJECT_ID, &size, &field_ref );
	}
	else
	{
		obj_data = ( uint8_t * )heap_object_locate( object, &size, &field_ref );
	}

	if( field_ref != T_CLASS )
	{
		return obj_data;
	}

	// it's not a class
	instance_error_invalid_ref();
	return NULL;
}

object_t object_clone( object_t object )
{
	object_t obj_clone;
	void * obj_ptr;
	void * obj_ptr_clone;
	uint32_t size = 0;
	uint8_t mode;

	obj_ptr = heap_object_locate( object, &size, &mode );

	obj_clone = ( object_t )heap_object_malloc( size, mode );
	if( obj_clone == 0 )
	{
		// need some memory
		object_garbage_collection();

		// try again
		obj_clone = ( object_t )heap_object_malloc( size, mode );

		// out of memory
		if( obj_clone == 0 )
		{
			instance_error_memory_overflow();
			return 0;
		}
	}

	obj_ptr_clone = heap_object_locate( object, NULL, NULL );

	memcpy( obj_ptr_clone, obj_ptr, size );

	return obj_clone;
}

void * object_get_constant( uint32_t index, uint32_t * size, uint8_t * field_ref )
{
	uint32_t offset = 0;
	uint8_t type = 0;
	uint8_t * obj_data;
	uint32_t * const_info;

	const_info = &(app_instance->const_ptr[index]);

	offset = ( *const_info ) >> 16;
	type = ( *const_info ) & 0x0FFFF;

	obj_data = ( uint8_t * )app_instance->const_ptr + offset;

	if( field_ref != NULL ) *field_ref = type;

	if( size != NULL )
	{
		if( ( type == T_INT ) || ( type == T_FLOAT ) ) *size = sizeof( uint32_t );
		else if( ( type == T_DOUBLE ) || ( type == T_LONG ) ) *size = sizeof( uint64_t );
		else if( type == T_STRING ) *size = (uint32_t)strlen( ( char * )obj_data );
	}

	return obj_data;
}
