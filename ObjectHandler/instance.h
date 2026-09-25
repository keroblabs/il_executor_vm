#ifndef _OBJ_INSTANCE_H
#define	_OBJ_INSTANCE_H

/////////////////////////////////////////////////////////////////////////////

#include "types.h"

#ifdef TIMA_OS
#include "debug.h"
#endif
    
/////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////

enum
{
	RADIX_BINARY = 2,
	RADIX_OCTAL = 8,
	RADIX_DECIMAL = 10,
	RADIX_HEXADECIMAL = 16
};

/////////////////////////////////////////////////////////////////////////////

enum
{
	T_DATA     = 3,
	T_BOOLEAN  = 4,
	T_CHAR 	      ,
	T_FLOAT       ,
	T_DOUBLE      ,
	T_BYTE 	      ,
	T_SHORT       ,
	T_INT 	      ,
	T_LONG 	      ,

	T_LINKED      ,

	T_FIELD		  ,
	T_CLASS       ,

	T_ARRAY = 0x10,

	T_STRING = ( T_ARRAY + T_CHAR ),

	T_MAX_TYPE
};

/////////////////////////////////////////////////////////////////////////////

#ifndef DEBUG_INFO
#define DEBUG_INFO(...)
#endif

#ifndef DEBUG_HEAP
#define DEBUG_HEAP(...)
#endif

#define T_ARRAY_STRING      T_CLASS

/////////////////////////////////////////////////////////////////////////////

typedef uint16_t object_t;
#define CONST_OBJECT_ID 0x00008000

/////////////////////////////////////////////////////////////////////////////

// typedef uint16_t object_t;

typedef void ( *error_handler_t )( void );
typedef void ( *on_object_t )( object_t object, uint8_t * data );  

typedef struct _instance_t
{
	object_t *stack;                // base of the physical stack [STACKSIZE]
	object_t *sp;					// the current stack pointer
	object_t *stack_base;			// where the stack is supposed to be when return
	object_t *locals;				// stack point for locals

	object_t sync_object;		// object this thread owns the access
	object_t wait_object;		// object this thread is wait to notify
	object_t task_object;		// object this task runs periodicaly
	object_t req_object;		// object this thread wants to own
    object_t prop_object;       // object with the value of a property
    object_t return_object;     // object returned by a thread
    
	void * data;				// embedded data for application

	struct _instance_t * prev_instance;
	struct _instance_t * next_instance;

} instance_t;

typedef struct _heap_t
{
    uint8_t * memory;
    uint32_t size;
    uint32_t used;
    
} heap_t;

typedef struct _app_instance_t
{
	heap_t heap_data;

	object_t * root_fields;
	uint16_t root_size;

	uint32_t * const_ptr;
	object_t * statics_ptr;
	uint16_t  total_statics; 
    uint16_t base_native_statics;

	instance_t * first_instance;
	instance_t * last_instance;
	instance_t * curr_instance;
    
	error_handler_t error_invalid_ref;
	error_handler_t error_out_of_bounds;
	error_handler_t error_string_out_of_bounds;
	error_handler_t error_array_out_of_bounds;
	error_handler_t error_division_by_zero;
	error_handler_t error_negative_array;
	error_handler_t error_heap_corrupted;
	error_handler_t error_memory_overflow;

} app_instance_t;

/////////////////////////////////////////////////////////////////////////////

extern app_instance_t * app_instance;

/////////////////////////////////////////////////////////////////////////////

void instance_app_create_new( void );

void instance_set( instance_t * _instance );

app_instance_t * instance_set_app( app_instance_t * _app_instance );

void instance_set_const( uint32_t * const_ptr );
void instance_set_statics( uint16_t total_statics );

void instance_destroy( instance_t * _instance );
void instance_delete_statics( void );

instance_t * instance_get( instance_t * start );
instance_t * instance_get_current( void );
instance_t * instance_get_last( void );
instance_t * instance_create( void );

object_t * instance_get_statics( void );

object_t instance_locate_native_static( uint32_t cref );
uint16_t instance_resize_statics( uint16_t add_statics );

/////////////////////////////////////////////////////////////////////////////

#define CREATE_INSTANCE_ERROR(a)	static void INLINE instance_##a( void )				\
									{													\
										if( app_instance->a != NULL )					\
											app_instance->a();							\
									}

/////////////////////////////////////////////////////////////////////////////

CREATE_INSTANCE_ERROR( error_invalid_ref )
CREATE_INSTANCE_ERROR( error_out_of_bounds )
CREATE_INSTANCE_ERROR( error_string_out_of_bounds )
CREATE_INSTANCE_ERROR( error_array_out_of_bounds )
CREATE_INSTANCE_ERROR( error_negative_array )
CREATE_INSTANCE_ERROR( error_division_by_zero )
CREATE_INSTANCE_ERROR( error_heap_corrupted )
CREATE_INSTANCE_ERROR( error_memory_overflow )

/////////////////////////////////////////////////////////////////////////////

#endif	/* _OBJ_INSTANCE_H */

