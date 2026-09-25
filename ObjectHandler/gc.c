#include "gc.h"
#include "objects.h"
#include "heap.h"
#include "arrays_type.h"

///////////////////////////////////////////////////////////////////////////

#define TC_GC_LIST_SIZE		20

///////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////

typedef struct _tc_list_t_
{
	object_t * list;
	uint32_t size;
	uint32_t index;

} tc_list_t;

///////////////////////////////////////////////////////////////////////////

void _object_tc_gc_remove( tc_list_t * list, object_t object )
{
	uint32_t counter;
	bool_t is_copy;

	is_copy = FALSE;

	for( counter = 0; counter < list->index; counter++ )
	{
		if( is_copy == FALSE )
		{
			if( object == list->list[ counter ] ) is_copy = TRUE;
		}
		else
		{
			list->list[ counter - 1 ] = list->list[ counter ];
		}
	}

	if( is_copy == TRUE )
	{
		list->list[ list->index - 1 ] = 0;
		list->index--;
	}
}

bool_t _object_tc_gc_search( tc_list_t * list, object_t object )
{
	uint32_t counter;

	if( object == 0 ) return TRUE;
	if( object >= CONST_OBJECT_ID ) return TRUE;

	for( counter = 0; counter < list->index; counter++ )
	{
		if( object == list->list[ counter ] ) return TRUE;
	}

	return FALSE;
}

void _object_tc_gc_delete_list( tc_list_t * list )
{
	MFREE( list->list );
	MFREE( list );
}

tc_list_t * _object_tc_gc_create_list( uint32_t size )
{
	tc_list_t * list;

	list = ( tc_list_t * )MMALLOC( sizeof( tc_list_t ) );
	if( list == NULL ) return NULL;

	memset( list, 0x00, sizeof( tc_list_t ) );

	list->list = ( object_t * )MMALLOC( sizeof( object_t ) * size );
	if( list->list == NULL )
	{
		heap_free( list );
		return NULL;
	}

	memset( list->list, 0x00, sizeof( object_t ) * size );
	list->size = size;

	return list;
}

void _object_tc_gc_add_to_list( tc_list_t * list, object_t object )
{
	object_t * new_list;
	uint32_t new_size;

	if( object == 0 ) return;
	if( object >= CONST_OBJECT_ID ) return;
	if( _object_tc_gc_search( list, object ) == TRUE ) return;

	list->list[ list->index ] = object;
	list->index++;
	if( list->index >= list->size )
	{
		// need resize
		new_size = list->size + TC_GC_LIST_SIZE;

		new_list = ( object_t * )MMALLOC( new_size * sizeof( object_t ) );
		memset( new_list, 0x00, new_size * sizeof( object_t ) );
		memcpy( new_list, list->list, list->size * sizeof( object_t ) );
		MFREE( list->list );

		list->size = new_size;
		list->list = new_list;
	}
}

void object_garbage_collection( void )
{
	tc_list_t * white_list;
	tc_list_t * grey_list;
	tc_list_t * black_list;

	object_t object;

	instance_t * instance;

	object_t * fields_ptr;
	object_t * sp;

	object_data_t * obj_data;

	uint32_t counter;
	uint32_t size = 0;
	uint32_t fields;

	uint8_t field_ref = 0;

	// initialize the list with a good size 
	white_list = _object_tc_gc_create_list( TC_GC_LIST_SIZE );
	grey_list = _object_tc_gc_create_list( TC_GC_LIST_SIZE );
	black_list = _object_tc_gc_create_list( TC_GC_LIST_SIZE );

	// all static objects are grey
	for( counter = 0; counter < app_instance->total_statics; counter++ )
    {
		object = instance_get_statics()[ counter ];

		if( object != 0 )
		{
			_object_tc_gc_add_to_list( grey_list, object );
		}
    }

	// and all objects in stack are grey
	instance = app_instance->first_instance;

	while( instance != NULL )
	{
        if( instance->stack != NULL )
		{
			// add objects referenced in the thread struct
			_object_tc_gc_add_to_list( grey_list, instance->req_object );
			_object_tc_gc_add_to_list( grey_list, instance->task_object );

			// search in the stack
			for( sp = instance->stack; sp <= instance->sp; sp++ )
			{
				// null reference, skip
				if( ( *sp ) == 0 ) continue;
				if( ( *sp ) >= CONST_OBJECT_ID ) continue; 

				_object_tc_gc_add_to_list( grey_list, ( *sp ) );
			}
		}

		instance = instance->next_instance;
	}

	// everything else is white
	object = heap_get_chain( 0 );

	while( object != 0 )
	{
        // skip constants, greyed, null and native objects
		if( ( object < CONST_OBJECT_ID ) && 
		    ( object != 0 ) &&
		    ( _object_tc_gc_search( grey_list, object ) == FALSE ) )
		{
			_object_tc_gc_add_to_list( white_list, object );
		}

		object = heap_get_chain( object );
	}

	// apply tricolor process: Blacken a greyed object by greying all the white objects it
	// references directly until there are no more grey objects. All the remaining white
	// objects have been demonstrated not to be reachable hence can be deleted.
	while( grey_list->index )
	{
		// for classes and array of classes, greyen all referenced objects.
		// but if it's not class and not array of class - this is a greyed object, therefore
		// it's static or referenced by a blacked object, so we can blacken it straight
		object = grey_list->list[0];

		_object_tc_gc_remove( grey_list, object );
		_object_tc_gc_add_to_list( black_list, object );

		obj_data = ( object_data_t * )heap_object_locate( object, &size, &field_ref );
		if( obj_data != NULL )
		{
			if( ( field_ref == T_CLASS ) || ( field_ref == ( T_ARRAY + T_CLASS ) ) )
			{
				// only valid for arrays of classes and classes
				if( field_ref == ( T_ARRAY + T_CLASS ) )
				{
					// point to the the first element of the array
					fields_ptr = ( object_t * )obj_data;
					fields = size / sizeof( object_t );
				}
				else
				{
					// point to fields
					fields_ptr = &obj_data->fields;
					fields = ( size - sizeof( object_data_t ) + sizeof( object_t ) ) / sizeof( object_t );
				}

				// go through fields/array elements
				for( counter = 0; counter < fields; counter++ )
				{
					// skip null object
					if( fields_ptr[ counter ] == 0 ) continue;

					// skip consts
					if( fields_ptr[ counter ] >= CONST_OBJECT_ID ) continue;

					// shouldnt happen, but
					if( fields_ptr[ counter ] == object ) continue;

					// greyen a white object (i.e. not black and not grey)
					if( ( _object_tc_gc_search( black_list, fields_ptr[ counter ] ) == FALSE ) &&
						( _object_tc_gc_search( grey_list, fields_ptr[ counter ] ) == FALSE ) )
					{
						// remove from white list - if it's not there, no worries
						_object_tc_gc_remove( white_list, fields_ptr[ counter ] );

						// add to grey list
						_object_tc_gc_add_to_list( grey_list, fields_ptr[ counter ] );
					}
				}
			}
		}
	}

	// at this point remaining white objects can be deleted
	for( counter = 0; counter < white_list->index; counter++ )
	{
		object_delete( white_list->list[counter] );
	}

	// delete all lists
	_object_tc_gc_delete_list( white_list );
	_object_tc_gc_delete_list( grey_list );
	_object_tc_gc_delete_list( black_list );
}
