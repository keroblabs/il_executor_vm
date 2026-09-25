
#ifndef STACK_H
#define	STACK_H

#ifdef	__cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////

#include "instance.h"
#include "value_type.h"
#include "obj_string.h"
		
///////////////////////////////////////////////////////////////////////////
    
void stack_dump( void );
void stack_init( uint32_t size );
void stack_scanf( const char * fmt, ... );

///////////////////////////////////////////////////////////////////////////

static INLINE void stack_add_sp(int16_t offset) 
{
	instance_get_current()->sp += offset;
}

static INLINE void stack_remove( uint16_t offset )
{
	instance_get_current()->sp -= offset;
}

static INLINE object_t * stack_get_sp(void)
{
	return instance_get_current()->sp;
}

static INLINE bool_t stack_is_empty(void) 
{
	return(instance_get_current()->sp == &instance_get_current()->stack[0]);
}

///////////////////////////////////////////////////////////////////////////

static INLINE object_t stack_peek(u08_t index) 
{
	return instance_get_current()->sp[-index];
}

static INLINE void * stack_peek_addr(u08_t index) 
{
	return object_get_ptr(instance_get_current()->sp[-index]);
}

static INLINE bool_t stack_peek_boolean(u08_t index) 
{
	return value_get_boolean( instance_get_current()->sp[-index] );
}

static INLINE uint8_t stack_peek_character(u08_t index) 
{
	return value_get_character( instance_get_current()->sp[-index] );
}

static INLINE uint8_t stack_peek_byte(u08_t index) 
{
	return value_get_byte( instance_get_current()->sp[-index] );
}

static INLINE int16_t stack_peek_short(u08_t index) 
{
	return value_get_short( instance_get_current()->sp[-index] );
}

static INLINE int32_t stack_peek_integer(u08_t index) 
{
	return value_get_decimal( instance_get_current()->sp[-index] );
}

static INLINE int64_t stack_peek_long(u08_t index) 
{
	return value_get_long( instance_get_current()->sp[-index] );
}

static INLINE float stack_peek_float(u08_t index)
{
	return value_get_float( instance_get_current()->sp[-index] );
}

static INLINE double stack_peek_double(u08_t index)
{
	return value_get_double( instance_get_current()->sp[-index] );
}

///////////////////////////////////////////////////////////////////////////

static INLINE object_t stack_pop(void) 
{
	return *(instance_get_current()->sp--);
}

static INLINE void * stack_pop_addr(void) 
{
	return object_get_ptr(*(instance_get_current()->sp--));
}

static INLINE bool_t stack_pop_boolean(void)
{
	return value_get_boolean( *(instance_get_current()->sp--) );
}

static INLINE uint8_t stack_pop_byte(void)
{
	return value_get_byte( *(instance_get_current()->sp--) );
}

static INLINE int16_t stack_pop_short(void)
{
	return value_get_short( *(instance_get_current()->sp--) );
}

static INLINE char stack_pop_character(void)
{
	return value_get_character( *(instance_get_current()->sp--) );
}

static INLINE int32_t stack_pop_integer(void)
{
	return value_get_decimal( *(instance_get_current()->sp--) );
}

static INLINE int64_t stack_pop_long(void)
{
	return value_get_long( *(instance_get_current()->sp--) );
}

static INLINE float stack_pop_float(void)
{
	return value_get_float( *(instance_get_current()->sp--) );
}

static INLINE double stack_pop_double(void)
{
	return value_get_double( *(instance_get_current()->sp--) );
}

///////////////////////////////////////////////////////////////////////////

static INLINE void stack_push( object_t object ) 
{
	*(++instance_get_current()->sp) = object;
}

static INLINE void stack_push_string( char * value )
{
	stack_push( obj_string_create( value ) );
}

static INLINE void stack_push_boolean( bool_t value )
{
	stack_push( value_create_boolean( value ) );
}

static INLINE void stack_push_byte( uint8_t value )
{
	stack_push( value_create_byte( value ) );
}

static INLINE void stack_push_character( char value )
{
	stack_push( value_create_character( value ) );
}

static INLINE void stack_push_short( int16_t value )
{
	stack_push( value_create_short( value ) );
}

static INLINE void stack_push_integer( int32_t value )
{
	stack_push( value_create_decimal( value ) );
}

static INLINE void stack_push_long( int64_t value )
{
	stack_push( value_create_long( value ) );
}

static INLINE void stack_push_float( float value )
{
	stack_push( value_create_float( value ) );
}

static INLINE void stack_push_double( double value )
{
	stack_push( value_create_double( value ) );
}

///////////////////////////////////////////////////////////////////////////

#ifdef	__cplusplus
}
#endif

#endif	/* STACK_H */

