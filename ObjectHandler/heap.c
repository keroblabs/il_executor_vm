#include "heap.h"

#ifdef WANT_OBJECT_HANDLER
#include "objects.h"
#include "instance.h"
#endif

#ifdef TIMA_OS
#include "debug.h"
#endif

///////////////////////////////////////////////////////////////////////////

#define MIN_HEAP_SIZE         ( sizeof( heap_header_t ) + sizeof( void * ) )
#define HEAP_HEAD_SIZE        sizeof( heap_header_t )

#define HEAP_FREE_BLOCK       0xF460
#define HEAP_USED_BLOCK       0x8AC0
#define HEAP_USED_DATA        0x03E0
#define HEAP_USED_HEAP        0x2820
#define HEAP_CONST_DATA       0xF0F0

#define HEAP_FIELD_MASK		  0x001F

///////////////////////////////////////////////////////////////////////////

#ifdef WANT_OBJECT_HANDLER

#define HEAP_HEADER_STRUCT          uint16_t signature;				\
                                    uint16_t size;					\
                                    uint16_t flags;                 \
									uint16_t b_size;

#else

#define HEAP_HEADER_STRUCT          uint16_t signature;				\
                                    uint16_t size;

#endif

///////////////////////////////////////////////////////////////////////////

#ifdef WANT_OBJECT_HANDLER

#define P_HEAP_DATA		app_instance->heap_data

#else 

// minimal code for instance.h to use with heap
// when not using inside object handler
// this makes this module fully reusable

#define T_LINKED        12
#define P_HEAP_DATA		heap_data

typedef struct _heap_t
{
    uint8_t * memory;
    uint32_t size;
    uint32_t used;
    
} heap_t;

heap_t heap_data;

void instance_error_heap_corrupted( void )
{
}

void instance_error_invalid_ref( void )
{
}

#endif

//////////////////////////////////////////////////////////////////////////

typedef struct _heap_header_t
{
    HEAP_HEADER_STRUCT

} heap_header_t;

typedef struct _heap_header_t2
{
    HEAP_HEADER_STRUCT
    
    uint8_t mem;

} heap_header_t2;

typedef struct _heap_header_t3
{
    HEAP_HEADER_STRUCT
    
    uint8_t * data;

} heap_header_t3;

///////////////////////////////////////////////////////////////////////////

static heap_header_t * _locate_next( heap_header_t * p )
{
	uint8_t * p_obj;
   
	if( p == NULL )
	{
		return NULL;
	}

	p_obj = ( uint8_t * )p;
	p_obj += p->size + HEAP_HEAD_SIZE;

	p = ( heap_header_t * )p_obj;

	return( p );
}

static heap_header_t * _locate_previous( heap_header_t * p )
{
   heap_header_t * p_search;
   heap_header_t * p_prev;

   p_prev = NULL;
   p_search = ( heap_header_t * )&P_HEAP_DATA.memory[ 0 ];

   while( p_search < p )
   {
      p_prev = p_search;
      p_search = _locate_next( p_prev );
   }

   return( p_prev );
}

static INLINE uint8_t * _get_memory( heap_header_t * p )
{
   heap_header_t2 * p_mem;
   p_mem = ( heap_header_t2 * )p;
   return( &p_mem->mem );
}

static INLINE uint8_t * _get_memory_data( heap_header_t * p )
{
   heap_header_t2 * p_mem;
   uint8_t ** p_data;
   
   p_mem = ( heap_header_t2 * )p;
   p_data = ( uint8_t ** )( &p_mem->mem );
   
   return( *p_data );
}


static INLINE heap_header_t * _get_header( void * p_mem )
{
   //heap_header_t * p;
   uint64_t p_int = ( uint64_t )p_mem;
   //p = ( heap_header_t * )( ( uint8_t * )p_mem - HEAP_HEAD_SIZE );
   return( heap_header_t * )( p_int - HEAP_HEAD_SIZE );
}

static object_t _get_id( heap_header_t * p )
{
    uint8_t * p_obj;
    
    p_obj = ( uint8_t * )( ( uint64_t )p - ( uint64_t )&P_HEAP_DATA.memory[ 0 ] );
    
    return( ( object_t )( ( uint64_t )p_obj & 0x0FFFF ) ) + 1;
}

static heap_header_t * _locate_item( object_t id )
{
    return( heap_header_t * )( &P_HEAP_DATA.memory[ id - 1 ] );
}

static heap_header_t * _internal_malloc( uint16_t size )
{
	heap_header_t * p;
	heap_header_t * p_new;
	uint8_t * p_mem;
    uint16_t signature;
	uint32_t i_size;

	p_mem = NULL;

	// 32bit allignment
	i_size = size;
	size = ( size + 3 )& ~3;

	// get first block
	p = ( heap_header_t * )&P_HEAP_DATA.memory[ 0 ];

	while( p != NULL )
	{
        signature = p->signature & ~HEAP_FIELD_MASK;
        
		if( ( signature != HEAP_FREE_BLOCK ) &&
			( signature != HEAP_USED_HEAP ) &&
			( signature != HEAP_USED_DATA ) &&
            ( signature != HEAP_USED_BLOCK ) )
		{
			instance_error_heap_corrupted();
			return NULL;
		}

		if( ( p->signature == HEAP_FREE_BLOCK ) && ( p->size >= size ) )
		{
			// block is free and suitable size
			p->signature = HEAP_USED_DATA;
			#ifdef WANT_OBJECT_HANDLER
			p->b_size = i_size;
			#endif
            p->flags = 0;
			p_mem = _get_memory( p );
            memset( p_mem, 0x00, p->b_size );

			// is there space for another block?
			if( ( p->size - size ) >= MIN_HEAP_SIZE )
			{
				// squeeze block
				uint32_t new_size = p->size - size - HEAP_HEAD_SIZE;
				p->size = size;

				// create another block
				p_new = _locate_next( p );
				p_new->signature = HEAP_FREE_BLOCK;
				p_new->size = new_size;
                p_new->flags = 0;
                
                P_HEAP_DATA.used += HEAP_HEAD_SIZE;
                
			}
            
            P_HEAP_DATA.used += p->size;

			break;
		}

		// get next block
		p = _locate_next( p );
	}

	if( p_mem == NULL )
	{
		// need more memory
		return NULL;
	}

	return( p );        
}

void _internal_free( void * p_mem )
{
	heap_header_t * p;
	heap_header_t * p_prev;
	heap_header_t * p_next;
    uint16_t signature;
    
	// get header block
	p = _get_header( p_mem );
	if( p == NULL ) return;

    signature = p->signature & ~HEAP_FIELD_MASK;

    if( ( signature != HEAP_FREE_BLOCK ) &&
		( signature != HEAP_USED_HEAP ) &&
		( signature != HEAP_USED_DATA ) &&
        ( signature != HEAP_USED_BLOCK ) )
    {
		instance_error_heap_corrupted();
        return;
    }    
    
	// release memory
	p->signature = HEAP_FREE_BLOCK;
    P_HEAP_DATA.used -= p->size;

	// locate next
	p_next = _locate_next( p );
	if( p_next != NULL )
	{
		if( p_next->signature == HEAP_FREE_BLOCK )
		{
			p->size = p->size + p_next->size + HEAP_HEAD_SIZE;
            P_HEAP_DATA.used -= HEAP_HEAD_SIZE;
		}
	}

	// locate previous
	p_prev = _locate_previous( p );
	if( p_prev != NULL )
	{
		if( p_prev->signature == HEAP_FREE_BLOCK )
		{
			p_prev->size = p_prev->size + p->size + HEAP_HEAD_SIZE;
            P_HEAP_DATA.used -= HEAP_HEAD_SIZE;
		}
	}
}

heap_header_t3 * _heap_get_object( object_t id )
{
    #ifdef WANT_OBJECT_HANDLER
    heap_header_t3 * p;
    uint16_t signature;
    
    p = ( heap_header_t3 * )_locate_item( id );
    if( p == NULL )
    {
		instance_error_heap_corrupted();
        return NULL;        
    }
    
    signature = p->signature & ~HEAP_FIELD_MASK;

    if( ( signature != HEAP_FREE_BLOCK ) &&
		( signature != HEAP_USED_HEAP ) &&
		( signature != HEAP_USED_DATA ) &&
        ( signature != HEAP_USED_BLOCK ) )
    {
		instance_error_heap_corrupted();
        return NULL;
    }       
    
    return p;
    #else
    return NULL;
    #endif
}

///////////////////////////////////////////////////////////////////////////

void heap_init( uint8_t * buffer, uint32_t size )
{
	heap_header_t * p;
    
    P_HEAP_DATA.memory = buffer;
    P_HEAP_DATA.size = size;
    P_HEAP_DATA.used = 0;

	p = ( heap_header_t * )P_HEAP_DATA.memory;

	p->signature = HEAP_FREE_BLOCK;
	p->size = size - ( uint32_t )HEAP_HEAD_SIZE;
    
    P_HEAP_DATA.used += ( uint32_t )HEAP_HEAD_SIZE;
}

uint32_t heap_in_use( void )
{
	return P_HEAP_DATA.used;
}

uint32_t heap_size( void )
{
	return P_HEAP_DATA.used;
}

bool_t heap_object_resize( object_t id, uint32_t new_size )
{
	#ifdef WANT_OBJECT_HANDLER
    heap_header_t3 * p;
    heap_header_t2 * p_new;
	heap_header_t * p_old;
    
    uint32_t copy_size;
    
    // locate item
    p = ( heap_header_t3 * )_locate_item( id );
    if( p == NULL )
    {
		instance_error_heap_corrupted();
        return FALSE;        
    }
    
    if( ( p->signature == HEAP_USED_BLOCK ) ||
		( p->signature == HEAP_USED_HEAP ) ||
		( p->signature == HEAP_FREE_BLOCK ) )
    {
		instance_error_invalid_ref();
        return FALSE;
    }

	p_old = _get_header( p->data );

    // need to resize?
	if( ( new_size >= p_old->b_size ) && ( new_size <= p_old->size ) )
	{
		p_old->b_size = new_size;
		return TRUE;
	}
    
    // create new data
    p_new = ( heap_header_t2 * )_internal_malloc( new_size );
    if( p_new == NULL )
    {
        // need some memory?
        return FALSE;        
    }    
    
	// copy data
	copy_size = new_size;
	if( new_size > p_old->size ) copy_size = p_old->size;
	memcpy( &p_new->mem, p->data, copy_size );   
	p_new->signature |= p->signature & HEAP_FIELD_MASK;
    
    // delete old one
    _internal_free( p->data );
    
    // point to the memory
    p->data = &p_new->mem;
    
	#endif

	return TRUE;
}

void * heap_object_locate( object_t id, uint32_t * size, uint8_t * field_ref )
{
	#ifdef WANT_OBJECT_HANDLER
    heap_header_t3 * p;
    heap_header_t * p2;
    uint8_t * p_mem;
    
    // locate item
    p = ( heap_header_t3 * )_locate_item( id );
    if( p == NULL )
    {
        // object not found, dont return error
        return NULL;        
    }
    
    if( size != NULL ) *size = p->b_size;
    if( field_ref != NULL ) *field_ref = p->signature & HEAP_FIELD_MASK;

	if( ( p->signature & HEAP_FIELD_MASK ) <= T_LINKED )
	{
        heap_header_t2 * p_out = ( heap_header_t2 * )p;
		return &p_out->mem;
	}
    
    p_mem = p->data;

	p2 = _get_header( p_mem );

    if( size != NULL ) *size = p2->b_size;

    return p_mem;
	#else
	return NULL;
	#endif
}

object_t heap_object_malloc( uint32_t size, uint8_t field_ref )
{
    #ifdef WANT_OBJECT_HANDLER
	heap_header_t3 * p;
	heap_header_t2 * p_data;

	if( field_ref <= T_LINKED )
	{
		// create header not linked
		p_data = ( heap_header_t2 * )_internal_malloc( size );
		if( p_data == NULL ) 
		{
			// need more memory
			return 0;
		}	
    
		p_data->signature |= ( field_ref & HEAP_FIELD_MASK );
		return _get_id( ( heap_header_t * )p_data );
	}
   
    // create header
    p = ( heap_header_t3 * )_internal_malloc( sizeof( uint8_t * ) );
    if( p == NULL ) 
    {
        // need more memory, dont generate error
        return 0;
    }
    
    // update header
    p->signature = HEAP_USED_BLOCK | ( field_ref & HEAP_FIELD_MASK );
    
    // now create the data block
    p_data = ( heap_header_t2 * )_internal_malloc( size );
    if( p_data == NULL )
    {
        // need more memory
		_internal_free( &p->data );
        return 0;
    }
    
    // point to the memory
	p->data = &(p_data->mem);
    p_data->signature |= ( field_ref & HEAP_FIELD_MASK );

    // return index to the memory
    return _get_id( ( heap_header_t * )p );
	#else
	return 0;
	#endif    
}

void * heap_malloc( uint32_t size )
{
	heap_header_t * p_data;
    uint8_t * p_mem;
    
    // create header
    p_data = ( heap_header_t * )_internal_malloc( size );
    if( p_data == NULL ) 
    {
        // need more memory
        return NULL;
    }	
    
	p_data->signature = HEAP_USED_HEAP;
    p_mem = _get_memory( p_data );
    return p_mem;
}

object_t heap_get_chain( object_t id )
{
	heap_header_t * p;
	uint16_t signature;
	uint8_t mode;

	if( id == 0 )
	{
		p = ( heap_header_t * )&P_HEAP_DATA.memory[ 0 ];
	}
	else
	{
		p = _locate_next( _locate_item( id ) );
	}

	while( p != NULL )
	{
        signature = p->signature & ~HEAP_FIELD_MASK;
		mode = p->signature & HEAP_FIELD_MASK;
        
		// check signature
		if( ( signature != HEAP_FREE_BLOCK ) &&
			( signature != HEAP_USED_HEAP ) &&
			( signature != HEAP_USED_BLOCK ) &&
            ( signature != HEAP_USED_DATA ) )
		{
			instance_error_heap_corrupted();
			return 0;
		}

		// return a valid object
		if( signature == HEAP_USED_BLOCK )
		{
			return _get_id( p );
		}

		if( ( signature == HEAP_USED_DATA ) && ( mode <= T_LINKED ) )
		{
			return _get_id( p );
		}

		// get next one
		p = _locate_next( p );
	}

	return 0;
}

void heap_check( void )
{
	heap_header_t * p;
	heap_header_t * p_prev;
    uint16_t signature;

	// get first block
	p = ( heap_header_t * )&P_HEAP_DATA.memory[ 0 ];

	while( p != NULL )
	{
        signature = p->signature & ~HEAP_FIELD_MASK;
        
		// check signature
		if( ( signature != HEAP_FREE_BLOCK ) &&
			( signature != HEAP_USED_HEAP ) &&
			( signature != HEAP_USED_BLOCK ) &&
            ( signature != HEAP_USED_DATA ) )
		{
			instance_error_heap_corrupted();
			return;
		}

		// get next block
		p_prev = p;
		p = _locate_next( p );
	}

	// if came to here means heap is fine
}

void heap_object_free( object_t object )
{
    #ifdef WANT_OBJECT_HANDLER
    heap_header_t3 * p;

    p = ( heap_header_t3 * )_locate_item( object );
    if( p == NULL )
    {
		instance_error_heap_corrupted();
        return;
    }   
    
    if( ( p->signature & ~HEAP_FIELD_MASK ) == HEAP_USED_BLOCK )
    {
        _internal_free( p->data );
    }
    
	_internal_free( &p->data );
    #endif
}

void heap_free( void * p_mem )
{
    heap_header_t * p;
    uint8_t * p_data;

    // get header block
    p = _get_header( p_mem );
    if( p == NULL ) return;  
    
    if( ( p->signature & ~HEAP_FIELD_MASK ) == HEAP_USED_BLOCK )
    {
        p_data = _get_memory_data( p );
        _internal_free( p_data );
    }
    
    _internal_free( p_mem );
}

void heap_dump_object( object_t object )
{
#if defined WANT_OBJECT_HANDLER
	heap_header_t * p = NULL;
    uint16_t signature;
	uint8_t mode;
	uint8_t mode2;
	uint32_t counter;
	uint32_t size;
	uint8_t *buffer;
	bool_t is_constant = FALSE;

	if( object >= CONST_OBJECT_ID )
	{
		is_constant = TRUE;
		signature = HEAP_CONST_DATA;
		buffer = ( uint8_t * )object_get_constant( object - CONST_OBJECT_ID, &size, &mode );
	}
	else
	{
		p = _locate_item( object );
		signature = p->signature & ~HEAP_FIELD_MASK;
		mode = p->signature & HEAP_FIELD_MASK;
		size = p->size;
	}
        
	// check signature
	if( ( signature != HEAP_FREE_BLOCK ) &&
		( signature != HEAP_USED_BLOCK ) &&
		( signature != HEAP_USED_HEAP ) &&
		( signature != HEAP_CONST_DATA ) &&
		( signature != HEAP_USED_DATA ) )
	{
		instance_error_heap_corrupted();
		return;
	}

	if( object < CONST_OBJECT_ID )
		DEBUG_PRINTK( "0x%08X: (0x%04x) ", ( uint32_t )p, object );
	else
	    DEBUG_PRINTK( "DATA_CONST: (0x%04x) ", object );

	switch( signature )
	{
		case HEAP_FREE_BLOCK: DEBUG_PRINTK( "HEAP_FREE_BLOCK - " ); break;
		case HEAP_USED_BLOCK: DEBUG_PRINTK( "HEAP_USED_BLOCK - " ); break;
		case HEAP_USED_DATA:  DEBUG_PRINTK( "HEAP_USED_DATA  - " ); break;
		case HEAP_USED_HEAP:  DEBUG_PRINTK( "HEAP_USED_HEAP  - " ); break;
		case HEAP_CONST_DATA: DEBUG_PRINTK( "HEAP_CONST_DATA - " ); break;
	}

	if( mode >= T_ARRAY )
	{
		DEBUG_PRINTK( "[]" );
		mode2 = mode - T_ARRAY;
	}
	else
	{
		DEBUG_PRINTK( "  " );
		mode2 = mode;
	}

	switch( mode2 )
	{
		case T_BOOLEAN:		DEBUG_PRINTK( "T_BOOL -   " ); break;

		case T_CHAR:		DEBUG_PRINTK( "T_CHAR -   " ); break;
		case T_FLOAT:		DEBUG_PRINTK( "T_FLOAT -  " ); break;
		case T_DOUBLE:		DEBUG_PRINTK( "T_DOUBLE - " ); break;
		case T_BYTE:		DEBUG_PRINTK( "T_BYTE -   " ); break;
		case T_SHORT:		DEBUG_PRINTK( "T_SHORT -  " ); break;

		case T_INT:			DEBUG_PRINTK( "T_INT   -  " ); break;
		case T_LONG:		DEBUG_PRINTK( "T_LONG  -  " ); break;
		case T_CLASS:		DEBUG_PRINTK( "T_CLASS -  " ); break;
		case T_DATA:		DEBUG_PRINTK( "T_DATA  -  " ); break;
		default:            DEBUG_PRINTK( "           " ); break;
	}

	if( ( signature == HEAP_USED_DATA ) ||
		( signature == HEAP_USED_HEAP ) )
	{
		buffer = &( ( heap_header_t2 * )p )->mem;
		DEBUG_PRINTK( "%04d: ", p->size );

		if( mode == T_CLASS )
		{
			DEBUG_PRINTK( "cref = 0x%04x, ", *( ( uint32_t * )buffer ) >> 16 );

			for( counter = 4; counter < p->size; counter += sizeof( object_t ) )
			{
				DEBUG_PRINTK( "%04x ", *( ( object_t * )&buffer[counter] ) );
				if( counter >= 7 ) break;
			}
		}
		else if( mode2 == T_CLASS )
		{
			for( counter = 0; counter < p->size; counter += sizeof( object_t ) )
			{
				DEBUG_PRINTK( "%04x ", *( ( object_t * )( &buffer[ counter ] ) ) );
				if( counter > 8 ) break;
			}
		}
		else
		{
			for( counter = 0; counter < p->size; counter++ )
			{
				DEBUG_PRINTK( "%02x ", buffer[ counter ] );
				if( counter >= 7 ) break;
			}
		}

		DEBUG_PRINTK( "\n" );
	}
	else if( signature == HEAP_CONST_DATA )
	{
		DEBUG_PRINTK( "%04d: ", size );

		switch( mode )
		{
			case T_STRING:
				DEBUG_PRINTK( "%s", ( char * )buffer );
				break;

			case T_INT:
			case T_CHAR:
			case T_SHORT:
			case T_BOOLEAN:
				DEBUG_PRINTK( "%d", ( int32_t * )buffer );
				break;

			default:
				for( counter = 0; counter < size; counter++ )
				{
					DEBUG_PRINTK( "%02x ", buffer[ counter ] );
					if( counter >= 7 ) break;
				}
				break;
		}

		DEBUG_PRINTK( "\n" );
	}
	else if( signature == HEAP_USED_BLOCK )
	{
		DEBUG_PRINTK( "%04d: 0x%04x\n", p->size, ( uint32_t )( ( heap_header_t3 * )p )->data - HEAP_HEAD_SIZE - ( uint32_t )P_HEAP_DATA.memory + 1 );
	}
	else
	{
		DEBUG_PRINTK( "%04d\n", p->size );
	}
#endif
}

void heap_dump( void )
{
#ifdef WANT_OBJECT_HANDLER

	heap_header_t * p;
	object_t object;

	// get first block
	p = ( heap_header_t * )&P_HEAP_DATA.memory[ 0 ];
	DEBUG_PRINTK( "used %d\n", P_HEAP_DATA.used );

	while( p != NULL )
	{
		object = _get_id( p );
		heap_dump_object( object );

		// get next block
		p = _locate_next( p );
	}

	// if came to here means heap is fine
#endif
}

