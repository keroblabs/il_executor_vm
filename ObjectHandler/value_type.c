#include "value_type.h"
#include "heap.h"
#include "obj_string.h"
#include "objects.h"

///////////////////////////////////////////////////////////////////////////

#if 0

void _string_itoa(char *str, int32_t val) 
{
	int32_t m;
	int32_t div = 1000000000L;  // max num to be output 
	bool_t printed = FALSE;
  
	if(val < 0) 
	{
		*str++ = '-';
		val = 0-val;
	}
  
	while(div > 0) 
	{
		m = val / div;
		if(m) printed = TRUE;
    
		if((printed)||(div == 1)) 
			*str++ = '0' + m;
    
		val = val % div;
		div /= 10;
	}
  
	*str++ = 0;
}

void _string_ltoa(char *str, int64_t val) 
{
	int64_t m;
	int64_t div = ( int64_t )100000000000000LL;  // max num to be output 
	bool_t printed = FALSE;
  
	if(val < 0) 
	{
		*str++ = '-';
		val = 0-val;
	}
  
	while(div > 0) 
	{
		m = val / div;
		if(m) printed = TRUE;
    
		if((printed)||(div == 1)) 
			*str++ = '0' + ( char )m;
    
		val = val % div;
		div /= 10;
	}
  
	*str++ = 0;
}

void _string_ftoa(char *str, float val) 
{
	int32_t i;
	char * sep;

	if (val<0)
	{
		*str++='-';
		val=-val;
	}

	i = ( int32_t )val;
	_string_itoa(str, i);
  
	while (*str) str++; // to string end
  
	sep = str;
  
	val+=1-i;
	i= (uint32_t)val*1000000;
	_string_itoa(str, i);
	sep[0] ='.';
  
	while (*str) str++; // to string end
	while (*(--str)=='0') *str=0;
  
	if (*str=='.') *str=0;
}

#endif

int32_t value_get_object_size( uint8_t type )
{
	switch( type )
	{
		case T_BOOLEAN:
		case T_BYTE: return (int32_t)1;
		case T_SHORT: return (int32_t)2;
		case T_INT: 
		case T_FLOAT: return (int32_t)4;
		case T_DOUBLE: 
		case T_LONG: return (int32_t)8;
	}

	return 0;
}


object_t value_create( void * value_ptr, uint8_t type )
{
	object_t object;
	int64_t * ptr_value_64;
	int32_t * ptr_value_32;

	if( ( type == T_LONG ) || ( type == T_DOUBLE ) )
	{
		object = object_data_create( type, sizeof( int64_t ) );
		ptr_value_64 = ( int64_t * )object_get_ptr( object );

		if( value_ptr == NULL )
		{
			memset( ptr_value_64, 0x00, sizeof( int64_t ) );
		}
		else
		{
			*ptr_value_64 = *((int64_t *)(value_ptr));
		}
	}
	else
	{
		object = object_data_create( type, sizeof( int32_t ) );
		ptr_value_32 = ( int32_t * )object_get_ptr( object );

		if( value_ptr == NULL )
		{
			memset( ptr_value_32, 0x00, sizeof( int32_t ) );
		}
		else
		{
            *ptr_value_32 = 0;
            memcpy( ptr_value_32, value_ptr, value_get_object_size(type) );
			// *ptr_value_32 = *( (int32_t *)(value_ptr) );
		}

		if( ( type == T_BOOLEAN ) && ( *ptr_value_32 != 0 ) ) *ptr_value_32 = 1;
		else if( ( type == T_CHAR ) && ( ( *ptr_value_32 < 0x20 ) || ( *ptr_value_32 > 128 ) ) ) *ptr_value_32 = ( uint32_t )('?');
	}

	return object;
}

int32_t value_compare( object_t obj1, object_t obj2 )
{
	void * obj1_ptr;
	void * obj2_ptr;

	uint8_t mode1 = 0;
	uint8_t mode2 = 0;

	int32_t result;

	// get object 1
	if( obj1 >= CONST_OBJECT_ID )
	{
		obj1_ptr = object_get_constant( obj1 - CONST_OBJECT_ID, NULL, &mode1 );
	}
	else
	{
		obj1_ptr = heap_object_locate( obj1, NULL, &mode1 );
	}

	// get object 2
	if( obj2 >= CONST_OBJECT_ID )
	{
		obj2_ptr = object_get_constant( obj2 - CONST_OBJECT_ID, NULL, &mode2 );
	}
	else
	{
		obj2_ptr = heap_object_locate( obj2, NULL, &mode2 );
	}

	// type mismatch
	if( mode1 != mode2 )
	{
		instance_error_invalid_ref();
		return 0;
	}

	result = 0;

	if( mode1 == T_DOUBLE )
	{
		if( *( ( double * )obj1_ptr ) > *( ( double * )obj2_ptr ) ) result = 1;
		else if( *( ( double * )obj1_ptr ) < *( ( double * )obj2_ptr ) ) result = -1;
	}
	else if( mode1 == T_FLOAT )
	{
		if( *( ( float * )obj1_ptr ) > *( ( float * )obj2_ptr ) ) result = 1;
		else if( *( ( float * )obj1_ptr ) < *( ( float * )obj2_ptr ) ) result = -1;
	}
	else if( mode1 == T_LONG )
	{
		if( *( ( int64_t * )obj1_ptr ) > *( ( int64_t * )obj2_ptr ) ) result = 1;
		else if( *( ( int64_t * )obj1_ptr ) < *( ( int64_t * )obj2_ptr ) ) result = -1;
	}
	else
	{
		if( *( ( int32_t * )obj1_ptr ) > *( ( int32_t * )obj2_ptr ) ) result = 1;
		else if( *( ( int32_t * )obj1_ptr ) < *( ( int32_t * )obj2_ptr ) ) result = -1;
	}

	return result;
}

uint8_t value_get_as_byte( object_t object )
{
	switch( object_get_type( object ) )
	{
		case T_BOOLEAN: return (uint8_t)value_get_boolean( object );
		case T_BYTE: return (uint8_t)value_get_byte( object );
		case T_SHORT: return (uint8_t)value_get_short( object );
		case T_INT: return (uint8_t)value_get_int( object );
		case T_LONG: return (uint8_t)value_get_long( object );
		case T_DOUBLE: return (uint8_t)value_get_double( object );
		case T_FLOAT: return (uint8_t)value_get_float( object );
	}

	return 0;
}

int16_t value_get_as_short( object_t object )
{
	switch( object_get_type( object ) )
	{
		case T_BOOLEAN: return (int16_t)value_get_boolean( object );
		case T_BYTE: return (int16_t)value_get_byte( object );
		case T_SHORT: return (int16_t)value_get_short( object );
		case T_INT: return (int16_t)value_get_int( object );
		case T_LONG: return (int16_t)value_get_long( object );
		case T_DOUBLE: return (int16_t)value_get_double( object );
		case T_FLOAT: return (int16_t)value_get_float( object );
	}

	return 0;
}

int32_t value_get_as_integer( object_t object )
{
	switch( object_get_type( object ) )
	{
		case T_BOOLEAN: return (int32_t)value_get_boolean( object );
		case T_BYTE: return (int32_t)value_get_byte( object );
		case T_SHORT: return (int32_t)value_get_short( object );
		case T_INT: return (int32_t)value_get_int( object );
		case T_LONG: return (int32_t)value_get_long( object );
		case T_DOUBLE: return (int32_t)value_get_double( object );
		case T_FLOAT: return (int32_t)value_get_float( object );
	}

	return 0;
}

int64_t value_get_as_long( object_t object )
{
	switch( object_get_type( object ) )
	{
		case T_BOOLEAN: return (int64_t)value_get_boolean( object );
		case T_BYTE: return (int64_t)value_get_byte( object );
		case T_SHORT: return (int64_t)value_get_short( object );
		case T_INT: return (int64_t)value_get_int( object );
		case T_LONG: return (int64_t)value_get_long( object );
		case T_DOUBLE: return (int64_t)value_get_double( object );
		case T_FLOAT: return (int64_t)value_get_float( object );
	}

	return 0L;
}

double value_get_as_double( object_t object )
{
	switch( object_get_type( object ) )
	{
		case T_BOOLEAN: return (double)value_get_boolean( object );
		case T_BYTE: return (double)value_get_byte( object );
		case T_SHORT: return (double)value_get_short( object );
		case T_INT: return (double)value_get_int( object );
		case T_LONG: return (double)value_get_long( object );
		case T_DOUBLE: return (double)value_get_double( object );
		case T_FLOAT: return (double)value_get_float( object );
	}

	return 0.0;
}

float value_get_as_float( object_t object )
{
	switch( object_get_type( object ) )
	{
		case T_BOOLEAN: return (float)value_get_boolean( object );
		case T_BYTE: return (float)value_get_byte( object );
		case T_SHORT: return (float)value_get_short( object );
		case T_INT: return (float)value_get_int( object );
		case T_LONG: return (float)value_get_long( object );
		case T_DOUBLE: return (float)value_get_double( object );
		case T_FLOAT: return (float)value_get_float( object );
	}

	return (float)0.0;
}

char * value_to_string( object_t object )
{
	void * obj_ptr;
	static char _to_string_temp[20];
	uint32_t size = 0;
	uint8_t mode = 0;
	//uint32_t cref;

	// get object
	if( object >= CONST_OBJECT_ID )
	{
		obj_ptr = object_get_constant(object - CONST_OBJECT_ID, &size, &mode );
	}
	else
	{
		obj_ptr = heap_object_locate( object, &size, &mode );
	}

	_to_string_temp[0] = 0;

	// decode object
	switch( mode )
	{
		case T_CHAR:
			_to_string_temp[0] = ( *( ( int32_t * )obj_ptr ) ) & 0x0FF;
			_to_string_temp[1] = 0;
			break;

		case T_BOOLEAN:
			if( *( ( int32_t * )obj_ptr ) != 0 ) strcpy( _to_string_temp, "true" );
			else strcpy( _to_string_temp, "false" );
			break;

		case T_BYTE:
		case T_SHORT:
		case T_INT:
			obj_string_itoa( _to_string_temp, *( ( int32_t * )obj_ptr ), RADIX_DECIMAL );
			break;

		case T_LONG:
			obj_string_ltoa( _to_string_temp, *( ( int64_t * )obj_ptr ), RADIX_DECIMAL );
			break;

		case T_FLOAT:
			obj_string_ftoa( _to_string_temp, *( ( float * )obj_ptr ) );
			break;

		case T_DOUBLE:
			obj_string_ftoa( _to_string_temp, ( float )( *( ( double * )obj_ptr ) ) );
			break;

		case (T_ARRAY + T_CHAR):
			return obj_string_get_text_ptr( object );
	}

	return _to_string_temp;
}

//////////////////////////////////////////////////////////////////////
