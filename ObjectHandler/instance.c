#include "instance.h"
#include "objects.h"
#include "heap.h"

#ifdef TIMA_OS
#include "system.h"
#endif

///////////////////////////////////////////////////////////////////////////

app_instance_t	* app_instance;

///////////////////////////////////////////////////////////////////////////

#if ( defined TIMA_OS )
static app_instance_t local_app_instance;

static uint8_t heap_memory[ TIMA_HEAP_SIZE ];
#endif

///////////////////////////////////////////////////////////////////////////

void instance_app_create_new( void )
{
#if ( defined TIMA_OS )
	// when using object handler standalone, call this method to create new instance
	memset( &local_app_instance, 0x00, sizeof( local_app_instance ) );
	instance_set_app( &local_app_instance );
    
    heap_init( heap_memory, TIMA_HEAP_SIZE );
#endif
}

void instance_set( instance_t * _instance )
{
	if( _instance != NULL ) app_instance->curr_instance = _instance;
}

app_instance_t * instance_set_app( app_instance_t * _app_instance )
{
    app_instance_t * prev_instance = app_instance;
	if( _app_instance != NULL )
    {
        app_instance = _app_instance;
        
        #if ( defined TIMA_OS )
        thread_set_data_object( ( void ** )&app_instance );
        #endif        
    }
    return prev_instance;
}

void instance_set_const( uint32_t * const_ptr )
{
	app_instance->const_ptr = const_ptr;
}

void instance_delete_statics( void )
{
	heap_free( app_instance->statics_ptr );
    app_instance->statics_ptr = NULL;
    app_instance->total_statics = 0;
}

void instance_set_statics( uint16_t total_statics )
{
    app_instance->base_native_statics = 0xFFFF;
	app_instance->statics_ptr = ( object_t * )heap_malloc( sizeof( object_t ) * total_statics );
	app_instance->total_statics = total_statics;
	memset( app_instance->statics_ptr, 0x00, sizeof( object_t ) * total_statics );
}

uint16_t instance_resize_statics( uint16_t add_statics )
{
    object_t * new_static;
    uint16_t new_size;
    
    // define new size
    new_size = app_instance->total_statics + add_statics;
    if( new_size <= app_instance->total_statics ) return app_instance->total_statics;
    
    // mark native statics
    if( app_instance->base_native_statics == 0xFFFF )
    {
        app_instance->base_native_statics = app_instance->total_statics;
    }
    
    // create new block of memory
    new_static = ( object_t * )heap_malloc( sizeof( object_t ) * new_size );
    memset( new_static, 0x00, sizeof( object_t ) * new_size );
    if( app_instance->statics_ptr != NULL )
    {
        memcpy( new_static, app_instance->statics_ptr, sizeof( object_t ) * app_instance->total_statics );
    }
    
    // set new block
	app_instance->statics_ptr = new_static;
	app_instance->total_statics = new_size;
    
    return new_size-1;
}

object_t instance_locate_native_static( uint32_t cref )
{
    uint16_t cnt;

    cnt = app_instance->base_native_statics;
    
    while( cnt < app_instance->total_statics )
    {
        if( ( app_instance->statics_ptr[cnt] != 0 ) && 
            ( object_get_class_ref( app_instance->statics_ptr[cnt] ) == cref ) )
        {
            return app_instance->statics_ptr[cnt];
        }
        
        cnt++;
    }
    
    return 0;
}

object_t * instance_get_statics( void )
{
	return app_instance->statics_ptr;
}

instance_t * instance_get_current( void )
{
	return app_instance->curr_instance;
}

instance_t * instance_get_last( void )
{
	return app_instance->last_instance;
}

instance_t * instance_get( instance_t * start )
{
	if( start == NULL ) return app_instance->first_instance;
	
	return start->next_instance;
}

void instance_destroy( instance_t * _instance )
{
	instance_t * next_thread;
	instance_t * curr_thread;

	// get current thread
	curr_thread = app_instance->curr_instance;
	next_thread = FALSE;

	if( ( curr_thread->next_instance == NULL ) &&
		( curr_thread->prev_instance == NULL ) )
	{
		// no more threads
		app_instance->first_instance = NULL;
		app_instance->last_instance = NULL;
	}

	// remove from the list
	if( curr_thread->prev_instance != NULL )
	{
		curr_thread->prev_instance->next_instance = curr_thread->next_instance;
	}

	if( curr_thread->next_instance != NULL )
	{
		curr_thread->next_instance->prev_instance = curr_thread->prev_instance;
	}
	else
	{
		app_instance->last_instance = curr_thread->prev_instance;
	}

	heap_free( _instance );
}

instance_t * instance_create( void )
{
	instance_t * ret_instance;

	// create instance
	ret_instance = ( instance_t * )heap_malloc( sizeof( instance_t ) );
	if( ret_instance == NULL )
	{
		// out of memory, try garbage collection
		object_garbage_collection();

		ret_instance = ( instance_t * )heap_malloc( sizeof( instance_t ) );
		if( ret_instance == NULL )
		{
			return NULL;
		}
	}

	memset( ret_instance, 0x00, sizeof( instance_t ) );

	// add the thread
	if( app_instance->first_instance == NULL )
	{
		app_instance->first_instance = ret_instance;
	}
	else
	{
		app_instance->last_instance->next_instance = ret_instance;
		ret_instance->prev_instance = app_instance->last_instance;
	}

	app_instance->last_instance = ret_instance;

	return ret_instance;
}

///////////////////////////////////////////////////////////////////////////

#if ( defined TIMA_OS )
DECLARE_POWER_UP_SECTION( instance_app_create_new );
#endif
