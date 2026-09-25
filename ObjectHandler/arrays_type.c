#include "arrays_type.h"
#include "objects.h"
#include "heap.h"
#include "value_type.h"
#include "objects.h"

///////////////////////////////////////////////////////////////////////////

uint32_t _array_get_object_type( uint8_t mode );

///////////////////////////////////////////////////////////////////////////

uint32_t array_get_element_size( uint8_t mode )
{
	if( mode > T_ARRAY ) mode -= T_ARRAY;

	switch( mode )
	{
		case T_BOOLEAN:
		case T_BYTE:
		case T_CHAR:
			return sizeof( uint8_t );

		case T_SHORT:
			return sizeof( uint16_t );

		case T_INT:
		case T_FLOAT:
			return sizeof( uint32_t );

		case T_CLASS:
		case T_DATA:
			return sizeof( object_t );

		case T_LONG:
		case T_DOUBLE:
			return sizeof( uint64_t );
	}

	return 0;
}

object_t _array_create_internal( uint32_t length, uint32_t item_size, uint8_t mode )
{
	object_t object;
	void * ptr_array;

	if( item_size == 0 )
	{
		instance_error_invalid_ref();
		return 0;
	}

	// get real_size
	item_size *= length;

	// setup object
	object = object_data_create( T_ARRAY + mode, item_size );
	ptr_array = ( char * )object_get_ptr( object );
	memset( ptr_array, 0x00, item_size );

	return object;
}

object_t array_create( uint32_t length, uint8_t mode )
{
	return _array_create_internal( length, array_get_element_size( mode ), mode );
}

object_t array_create_data( uint32_t length, uint32_t item_size )
{
	return _array_create_internal( length, item_size, T_DATA );
}

void array_destroy( object_t object )
{
#if 0
	uint32_t size = 0;
	uint8_t mode = 0;
    object_t * _array;
    
    if( heap_is_disposed( object ) == TRUE )
    {
        object_delete( object );
        return;
    }

	// get everything to determine new size
	_array = ( object_t * )heap_object_locate( object, &size, &mode );

	if( mode < T_ARRAY )
	{
		instance_error_invalid_ref();
		return;
	}

	mode -= T_ARRAY;
    
    if( mode == T_CLASS )
    {
        // dispose elements
        while( *_array )
        {
            object_delete( *_array );
            _array++;
            
            size -= sizeof( object_t );
            if( size == 0 ) break;
        }
    }
#endif
	object_delete( object );
}

uint32_t array_get_capacity( object_t object )
{
	uint32_t size;
	uint8_t mode = 0;
    
	heap_object_locate( object, &size, &mode );

	return size / array_get_element_size(mode);
}

uint32_t array_get_type( object_t object )
{
	uint8_t mode = 0;

	mode = object_get_type( object );

	if( mode < T_ARRAY )
	{
		instance_error_invalid_ref();
		return 0;
	}

	return mode - T_ARRAY;
}

void array_copy( object_t object, uint32_t position, void * new_array, uint32_t new_size )
{
	uint32_t size = 0;
	uint8_t mode = 0;
	uint8_t * _array;
    
	// get everything to determine new size
	_array = ( uint8_t * )heap_object_locate( object, &size, &mode );

	if( mode < T_ARRAY )
	{
		instance_error_invalid_ref();
		return;
	}

	mode -= T_ARRAY;

	position *= array_get_element_size(mode);
	new_size *= array_get_element_size(mode);

	// for exception
	if( ( position+new_size ) > size )
	{
		instance_error_array_out_of_bounds();
		return;
	}

	// and add string
	memcpy( &_array[position], new_array, new_size );
}

void * array_get_data( object_t object, uint32_t position, uint32_t item_size )
{
	uint32_t size = 0;
	uint8_t * _array;
	uint8_t mode = 0;
    
	// get everything to determine new size
	_array = ( uint8_t * )heap_object_locate( object, &size, &mode );

	if( mode < T_ARRAY )
	{
		instance_error_invalid_ref();
		return NULL;
	}

	mode -= T_ARRAY;

	position *= item_size;
	return &_array[position];
}

void array_insert_data( object_t object, uint32_t position, uint32_t item_size, void * data )
{
	uint32_t new_size = 0;
	uint32_t size = 0;
	uint8_t mode = 0;
	uint8_t * _array;
    
	// get everything to determine new size
	_array = ( uint8_t * )heap_object_locate( object, &size, &mode );

	if( mode < T_ARRAY )
	{
		instance_error_invalid_ref();
		return;
	}

	mode -= T_ARRAY;

	position *= item_size;

	// insert new item
	if( position == NEG_U32 )
	{
		position = size;
		new_size = size + item_size;

		// resize object
		object_data_resize( object, new_size );

		// get buffer again - it was resized
		_array = ( uint8_t * )object_get_ptr( object );
	}
	else if( position >= size )
	{
		// for exception
		instance_error_array_out_of_bounds();
		return;
	}
		
	// build up into new string
	if( data != NULL ) memcpy( &_array[position], data, item_size );
	else memset( &_array[position], 0x00, item_size );

}

void array_get( object_t object, uint32_t position, uint32_t len, void * output )
{
	uint32_t size = 0;
	uint8_t * _array;
	uint8_t mode = 0;
    
	// get everything to determine new size
	_array = ( uint8_t * )heap_object_locate( object, &size, &mode );

	if( mode < T_ARRAY )
	{
		instance_error_invalid_ref();
		return;
	}

	mode -= T_ARRAY;

	position *= array_get_element_size(mode);

	if( len == NEG_U32 ) len = size;
	else len *= array_get_element_size(mode);

	// for exception
	if( ( position >= size ) ||
		( ( position+len ) > size ) )
	{
		instance_error_array_out_of_bounds();
		return;
	}

	// copy string
	memcpy( output, &_array[position], len );
}

void array_insert( object_t object, uint32_t position, void * input, uint32_t start, uint32_t len )
{
	uint32_t new_size = 0;
	uint32_t size = 0;
	uint8_t mode = 0;
	uint8_t * _array;
	uint8_t * new_array;
	uint8_t * array_input = ( uint8_t * )input;
    
	// get everything to determine new size
	_array = ( uint8_t * )heap_object_locate( object, &size, &mode );

	if( mode < T_ARRAY )
	{
		instance_error_invalid_ref();
		return;
	}

	mode -= T_ARRAY;

	position *= array_get_element_size(mode);
	start *= array_get_element_size(mode);
	len *= array_get_element_size(mode);

	if( position == NEG_U32 ) position = size;
	new_size = size + len;

	// for exception
	if( ( position >= size ) ||
		( ( position+len ) > size ) )
	{
		instance_error_array_out_of_bounds();
		return;
	}

	// create temp string
	new_array = ( uint8_t * )heap_malloc( new_size );

	// build up into new string
	memcpy( new_array, _array, position );
	if( array_input != NULL ) memcpy( &new_array[position], &array_input[start], len );
	else memset( &new_array[position], 0x00, len );
	memcpy( &new_array[position+len], &_array[position], size-position-len );

	// resize object
	object_data_resize( object, new_size );

	// get buffer again - it was resized
	_array = ( uint8_t * )object_get_ptr( object );

	// and add string
	memcpy( _array, new_array, new_size );

	// delete temp string
	heap_free( new_array );
}

void array_delete( object_t object, uint32_t position, uint32_t len )
{
	uint32_t size = 0;
	uint32_t new_size;
	uint8_t * _array;
	uint8_t mode = 0;
    
	// get everything to determine new size
	_array = ( uint8_t * )heap_object_locate( object, &size, &mode );

	if( mode < T_ARRAY )
	{
		instance_error_invalid_ref();
		return;
	}

	mode -= T_ARRAY;

	position *= array_get_element_size(mode);
	len *= array_get_element_size(mode);

	if( len == NEG_U32 ) len = size-position;

	// for exception
	if( ( position >= size ) ||
		( ( position+len ) > size ) )
	{
		instance_error_array_out_of_bounds();
		return;
	}

	// copy string
	new_size = size-position-len;

	if( new_size != 0 ) memcpy( &_array[position], &_array[position+len], size-position-len );

	// resize
	size -= len;
	object_data_resize( object, size );
}
