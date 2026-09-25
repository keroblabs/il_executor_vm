#include "stack.h"
#include "heap.h"
#include "vm_debug.h"
#include <stdarg.h>

///////////////////////////////////////////////////////////////////////////

void stack_dump( void )
{
#if	SET_LEVEL_STACK != 0
	uint32_t i = 0;
	object_t * dump_stack;

	DEBUG_STACK( "Stack:\n" );
	dump_stack = &instance_get_current()->stack[1];

	while( dump_stack <= instance_get_current()->sp )
	{
		DEBUG_STACK( "%3d: ", i++ );
		if( *dump_stack == 0 )
		{
			DEBUG_STACK( "NULL_OBJECT (0x0000)\n" );
		}
		else
		{
			heap_dump_object( *dump_stack );
		}
		dump_stack++;
	}

#endif

}

/***********************/

void stack_init( uint32_t size )
{
    instance_get_current()->stack = ( object_t * )heap_malloc( size * sizeof( object_t ) );
	memset( instance_get_current()->stack, 0, size * sizeof( object_t ) );
	instance_get_current()->sp = &instance_get_current()->stack[0];
}

/***********************/

void stack_scanf( const char * fmt, ... )
{
	va_list args;

	float * _float;
	int32_t * _int32_t;
	int64_t * _int64_t;
	uint32_t ** addr;

	bool_t is_long;
	char * fp;
	uint32_t index;

	is_long = FALSE;
	index = 0;

	va_start(args, fmt);
	
	for( fp = ( char * )fmt; *fp != '\0'; fp++ )
	{
		while ( *fp != '%' ) fp++;

		if( *fp == '%' )
		{
			switch( *++fp )
			{
				case 'i':	
				case 'd':	
				case 'x':	
					if( is_long == FALSE )
					{
						_int32_t = va_arg( args, int32_t * );
						*_int32_t = value_get_decimal( *instance_get_current()->sp );
					}
					else
					{
						_int64_t = va_arg( args, int64_t * );
						*_int64_t = value_get_long( *instance_get_current()->sp );
						is_long = FALSE;
					}
					instance_get_current()->sp--;
					break;

				case 'f':	
					_float = va_arg( args, float * );
					*_float = value_get_float( *instance_get_current()->sp );
					instance_get_current()->sp--;
					break;

				case 'l':	
					is_long = TRUE;
					break;

				case 's':	
					addr = va_arg( args, uint32_t ** );
					*addr = ( uint32_t * )object_get_ptr( *instance_get_current()->sp );
					instance_get_current()->sp--;
					break;
			}
		}
	}

	va_end(args);
}

