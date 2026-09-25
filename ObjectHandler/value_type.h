
#ifndef VALUE_TYPE_H
#define	VALUE_TYPE_H

#ifdef	__cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////

#include "instance.h"
#include "objects.h"

///////////////////////////////////////////////////////////////////////////
	
object_t value_create( void * value_ptr, uint8_t type );

int32_t value_compare( object_t obj1, object_t obj2 );

uint8_t value_get_as_byte( object_t object );
int16_t value_get_as_short( object_t object );
int32_t value_get_as_integer( object_t object );
int64_t value_get_as_long( object_t object );
double  value_get_as_double( object_t object );
float   value_get_as_float( object_t object );

char * value_to_string( object_t object );

///////////////////////////////////////////////////////////////////////////

static INLINE bool_t value_get_boolean( object_t object )
{
	return *( ( bool_t * )object_get_ptr( object ) );
}

static INLINE uint8_t  value_get_byte( object_t object )
{
	return *( ( uint8_t * )object_get_ptr( object ) );
}

static INLINE char value_get_character( object_t object )
{
	return *( ( char * )object_get_ptr( object ) );
}

static INLINE int16_t  value_get_short( object_t object )
{
	return *( ( int16_t * )object_get_ptr( object ) );
}

static INLINE int32_t value_get_decimal( object_t object )
{
	return *( ( int32_t * )object_get_ptr( object ) );
}

static INLINE int32_t value_get_int( object_t object )
{
	return *( ( int32_t * )object_get_ptr( object ) );
}

static INLINE int64_t value_get_long( object_t object )
{
	return *( ( int64_t * )object_get_ptr( object ) );
}

static INLINE float value_get_float( object_t object )
{
	return *( ( float * )object_get_ptr( object ) );
}

static INLINE double value_get_double( object_t object )
{
	return *( ( double * )object_get_ptr( object ) );
}

//////////////////////////////////////////////////////////////////////////

static INLINE void value_set_boolean( object_t object, bool_t value )
{
	*( ( bool_t * )object_get_ptr( object ) ) = value;
}

static INLINE void  value_set_byte( object_t object, uint8_t value )
{
	*( ( uint8_t * )object_get_ptr( object ) ) = value;
}

static INLINE void value_set_character( object_t object, char value )
{
	*( ( char * )object_get_ptr( object ) ) = value;
}

static INLINE void  value_set_short( object_t object, int16_t value )
{
	*( ( int16_t * )object_get_ptr( object ) ) = value;
}

static INLINE void value_set_decimal( object_t object, int32_t value )
{
	*( ( int32_t * )object_get_ptr( object ) ) = value;
}

static INLINE void value_set_int( object_t object, int32_t value )
{
	*( ( int32_t * )object_get_ptr( object ) ) = value;
}

static INLINE void value_set_long( object_t object, int64_t value )
{
	*( ( int64_t * )object_get_ptr( object ) ) = value;
}

static INLINE void value_set_float( object_t object, float value )
{
	*( ( float * )object_get_ptr( object ) ) = value;
}

static INLINE void value_set_double( object_t object, double value )
{
	*( ( double * )object_get_ptr( object ) ) = value;
}

//////////////////////////////////////////////////////////////////////////

static INLINE object_t value_create_boolean( bool_t value )
{
	return value_create( &value, T_BOOLEAN );
}

static INLINE object_t value_create_byte( uint8_t value )
{
	return value_create( &value, T_BYTE );
}

static INLINE object_t value_create_character( char value )
{
	return value_create( &value, T_CHAR );
}

static INLINE object_t value_create_short( int16_t value )
{
	return value_create( &value, T_SHORT );
}

static INLINE object_t value_create_decimal( int32_t value )
{
	return value_create( &value, T_INT );
}

static INLINE object_t value_create_int( int32_t value )
{
	return value_create( &value, T_INT );
}

static INLINE object_t value_create_long( int64_t value )
{
	return value_create( &value, T_LONG );
}

static INLINE object_t value_create_float( float value )
{
	return value_create( &value, T_FLOAT );
}

static INLINE object_t value_create_double( double value )
{
	return value_create( &value, T_DOUBLE );
}

//////////////////////////////////////////////////////////////////////////

#ifdef	__cplusplus
}
#endif

#endif	/* VALUE_TYPE_H */

