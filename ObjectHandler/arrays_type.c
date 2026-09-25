#include "arrays_type.h"
#include "objects.h"
#include "heap.h"

///////////////////////////////////////////////////////////////////////////

static uint32_t array_get_element_size( uint8_t mode )
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

object_t array_create( uint32_t length, uint8_t mode )
{
	uint32_t item_size = array_get_element_size( mode );
	object_t object;

	if( item_size == 0 )
	{
		instance_error_invalid_ref();
		return 0;
	}

	if( length > NEG_U32 / item_size )
	{
		instance_error_memory_overflow();
		return 0;
	}

	object = object_data_create( T_ARRAY + mode, length * item_size );
	if( object == 0 )
	{
		// object_data_create has already reported the failure
		return 0;
	}

	memset( object_get_ptr( object ), 0x00, length * item_size );

	return object;
}

// NULL when object is not an array; the failure has already been reported
static uint8_t * array_locate( object_t object, uint32_t * length, uint32_t * item_size )
{
	uint32_t size = 0;
	uint8_t mode = 0;
	uint8_t * base;

	base = ( uint8_t * )heap_object_locate( object, &size, &mode );

	*item_size = array_get_element_size( mode );
	if( ( mode < T_ARRAY ) || ( *item_size == 0 ) )
	{
		instance_error_invalid_ref();
		return NULL;
	}

	*length = size / *item_size;
	return base;
}

uint32_t array_length( object_t object )
{
	uint32_t length = 0;
	uint32_t item_size;

	if( array_locate( object, &length, &item_size ) == NULL )
	{
		return 0;
	}

	return length;
}

void * array_range_ptr( object_t object, uint32_t index, uint32_t count )
{
	uint32_t length;
	uint32_t item_size;
	uint8_t * base;

	base = array_locate( object, &length, &item_size );
	if( base == NULL )
	{
		return NULL;
	}

	// compare in elements, so a huge index or count cannot wrap past the check
	if( ( index > length ) || ( count > length - index ) )
	{
		instance_error_array_out_of_bounds();
		return NULL;
	}

	return &base[ index * item_size ];
}
