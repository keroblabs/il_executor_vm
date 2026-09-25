#include "obj_string.h"
#include "heap.h"
#include "objects.h"
#include "arrays_type.h"

///////////////////////////////////////////////////////////////////////////

float strings_atof( const char * str )
{
	return ( float )atof(str);
}

int32_t strings_atoi( const char * str, int radix )
{
	return ( int32_t )strings_atol( str, radix );
}

int64_t strings_atol( const char * str, int radix )
{
	uint32_t len;
	uint32_t cnt;
	int64_t ret;

	ret = 0;
	len = ( uint32_t )strlen( str );

	if( ( radix != 2 ) && ( radix != 8 ) && ( radix != 10 ) && ( radix != 16 ) ) return 0;

	for( cnt = 0; cnt < len; cnt++ )
	{
		ret *= radix;

		if( ( str[ cnt ] >= 'A' ) && ( str[ cnt ] <= 'F' ) )
		{
			ret += ( str[ cnt ] - (int32_t)'A' + 10 );
		}
		else if( ( str[ cnt ] >= 'a' ) && ( str[ cnt ] <= 'f' ) )
		{
			ret += ( str[ cnt ] - (int32_t)'a' + 10 );
		}
		else if( ( str[ cnt ] >= '0' ) && ( str[ cnt ] <= '9' ) )
		{
			ret += ( str[ cnt ] - (int32_t)'0' );
		}
		else
		{
			return 0;
		}
	}

	return ret;
}

void strings_itoa(char *str, int32_t val, uint32_t radix)
{
	strings_ltoa(str, (int64_t)val, radix);
}

void strings_ltoa(char *str, int64_t val, uint32_t radix) 
{
	int64_t m;
	int64_t div;
	bool_t printed = FALSE;
    
    if( ( radix != 10 ) && ( val < 0 ) )
    {
        val *= -1;
    }
  
	switch( radix )
	{
		case 16: div = (int64_t)0x1000000000000000LL; break;
		case 8: div =  (int64_t)0x8000000000000000LL; break;
		case 10: div = 100000000000000000LL; break;
		case 2: div =  100000000000000000LL; break;
		default: str[0] = 0; return;
	}

	if(val < 0) 
	{
		*str++ = '-';
		val = 0-val;
	}
  
	while(div > 0) 
	{
		m = val / div;
		if( ( m != 0 ) || ( printed == TRUE ) )
		{
			val = val - ( m * div );
			if( m <= 9 ) *str++ = '0' + ( char )m;
			else *str++ = 'A' + ( char )m - 10;		

			printed = TRUE;
		}
    
		div /= radix;
	}
  
	*str++ = 0;
}

void strings_ftoa(char *str, float val) 
{
	int32_t i;
	char * sep;

	if (val<0)
	{
		*str++='-';
		val=-val;
	}

	i = ( int32_t )val;
	strings_itoa(str, i, 10);
  
	while (*str) str++; // to string end
  
	sep = str;
  
	val+=1-i;
	i= (uint32_t)val*1000000;
	strings_itoa(str, i, 10);
	sep[0] ='.';
  
	while (*str) str++; // to string end
	while (*(--str)=='0') *str=0;
  
	if (*str=='.') *str=0;
}

object_t strings_split( object_t object, const char * delimit )
{
    char * str_temp;
    char * last_token;
    char * out;
    char * str_ptr;
    int size;
    int counter;
    object_t array_obj;
    object_t element;
    
    counter = 0;
    
    str_temp = strings_get_text_ptr( object );
    
    // first need to count
	while (strings_tokenizer(str_temp, ( const char ** )&out, &size, delimit, &last_token)) 
	{
        counter++;
		str_temp = NULL;
	}       
    
    // create array
    array_obj = array_create( counter, T_CLASS );
    
    str_temp = strings_get_text_ptr( object );
    counter = 0;
    
    // first need to count
	while (strings_tokenizer(str_temp, ( const char ** )&out, &size, delimit, &last_token)) 
	{
        element = strings_create_capacity( size + 1 );
        str_ptr = strings_get_text_ptr( element );
        strncpy( str_ptr, out, size );
        str_ptr[size] = 0;
        
        array_copy( array_obj, counter, &element, 1 );
        counter++;
		str_temp = NULL;
	}       
    
    return array_obj;
}

bool_t strings_tokenizer(const char *s1, const char ** exit, int * size, const char *delimit, char ** lastToken)
{
    const char *tmp;

    /* Skip leading delimiters if new string. */
    if ( s1 == NULL ) 
	{
        s1 = *lastToken;
        if (s1 == NULL)         /* End of story? */
            return FALSE;
    } 
	else 
	{
        s1 += strspn(s1, delimit);
    }

    /* Find end of segment */
    tmp = strpbrk(s1, delimit);
    if (tmp) 
	{
        /* Found another delimiter, split string and save state. */
		*exit = s1;
		*size = ( int )( ( uint8_t * )tmp - ( uint8_t * )s1 );
        *lastToken = ( char * )tmp + 1;
    } 
	else 
	{
        /* Last segment, remember that. */
		*exit = s1;
		*size = ( int )strlen( *exit );
        *lastToken = NULL;
    }

	if( s1 != NULL ) return TRUE;
    return FALSE;
}

void strings_destroy( object_t object )
{
	object_delete( object );
}

object_t strings_create( const char * string )
{
	return strings_create_index( string, 0, NEG_U32 );
}

object_t strings_create_index( const char * string, uint32_t start, uint32_t len )
{
	uint32_t length = 0;
	object_t object;
	char * ptr_string;

	if( len == NEG_U32 ) len = ( uint32_t )strlen( string );

	// setup size
	if( string != NULL )
	{
		length = ( uint32_t )strlen( string );

		// for exception
		if( ( start+len ) > length )
		{
			instance_error_string_out_of_bounds();
			return 0;
		}
	}

	// add class ref to length
	length++;

	// create object
	object = object_data_create( T_STRING, length );
	ptr_string = ( char * )object_get_ptr( object );
	memset( ptr_string, 0x00, length );

	// copy source string, if exist
	if( string != NULL )
	{
		strncpy( &ptr_string[start], string, len );
	}

	return object;
}

int32_t strings_index_of( object_t object, const char * search )
{
    char * obj_string = strings_get_text_ptr( object );
    char * ret = strstr( obj_string, search );
    
    if( ret == NULL ) return -1;
    
    return ( int32_t )( ( uint8_t * )ret - ( uint8_t * )obj_string );
}

object_t strings_create_capacity( uint32_t length )
{
	object_t object;
	char * ptr_string;

	// add class ref to strings
	length++;

	// setup object
	object = object_data_create( T_STRING, length );
	ptr_string = ( char * )object_get_ptr( object );
	memset( ptr_string, 0x00, length );

	return object;
}

uint32_t strings_get_capacity( object_t object )
{
	uint32_t length;
	char * ptr_string;

	ptr_string = ( char * )object_get_ptr( object );
	length = ( uint32_t )strlen( ptr_string ) + 1;

	return length;
}

void strings_copy( object_t object, const char * new_string )
{
	uint32_t new_size = 0;
	char * text;

	// get everything to determine new size
	text = ( char * )object_get_ptr( object );
	new_size = sizeof( char ) + ( uint32_t )strlen( new_string );

	if( new_size > strlen( text ) )
	{
		// resize object
		object_data_resize( object, new_size );
	}

	// get buffer again - it was resized
	text = ( char * )object_get_ptr( object );

	// and add string
	strcpy( text, new_string );
}

void strings_concat( object_t object, const char * add_text )
{
	uint32_t new_size = 0;
	char * text;

	// get everything to determine new size
	text = ( char * )object_get_ptr( object );
	new_size = sizeof( char ) + ( uint32_t )strlen( text ) + ( uint32_t )strlen( add_text );

	// resize object
	object_data_resize( object, new_size );

	// get buffer again - it was resized
	text = ( char * )object_get_ptr( object );

	// and add string
	strcat( text, add_text );
}

int strings_compare( object_t object, const char * string )
{
	char * text;

	// get everything to determine new size
	text = ( char * )object_get_ptr( object );
	
    return strcmp( text, string );
}

char strings_char_at( object_t object, uint32_t index )
{
	uint32_t size = 0;
	char * text;

	// get everything to determine new size
	text = ( char * )object_get_ptr( object );
	size = ( uint32_t )strlen( text );

    if( index == NEG_U32 )
    {
        index = size - 1;
    }
    
    return text[ index ];
}

void strings_tolower( object_t object )
{
	uint32_t size = 0;
	uint32_t i;
	char * text;

	// get everything to determine new size
	text = ( char * )object_get_ptr( object );
	size = ( uint32_t )strlen( text );

	// process it
	for( i = 0; i < size; i++ )
	{
		if( ( text[i] >= 0x41 ) && ( text[i] <= 0x5A ) ) text[i] = text[i] | 0x20;
	}
}

void strings_toupper( object_t object )
{
	uint32_t size = 0;
	uint32_t i;
	char * text;

	// get everything to determine new size
	text = ( char * )object_get_ptr( object );
	size = ( uint32_t )strlen( text );

	// process it
	for( i = 0; i < size; i++ )
	{
		if( ( text[i] >= 0x61 ) && ( text[i] <= 0x7A ) ) text[i] = text[i] & ~0x20;
	}
}

void strings_replace_char( object_t object, char replace_char, char new_char )
{
	uint32_t size = 0;
	uint32_t i;
	char * text;

	// get everything to determine new size
	text = ( char * )object_get_ptr( object );
	size = ( uint32_t )strlen( text );

	// process it
	for( i = 0; i < size; i++ )
	{
		if( text[i] == replace_char ) text[i] = new_char;
	}    
}

void strings_insert( object_t object, uint32_t position, const char * input, uint32_t start, uint32_t len )
{
	uint32_t input_size = 0;
	uint32_t new_size = 0;
	uint32_t size = 0;
	char * text;
	char * new_text;

	// adjust values
	input_size = ( uint32_t )strlen( input );
	if( len == NEG_U32 ) len = input_size;

	// get everything to determine new size
	text = ( char * )object_get_ptr( object );
	size = ( uint32_t )strlen( text );
	new_size = size + len + 1;
	if( position == NEG_U32 ) position = size;

	// for exception
	if( ( position >= size ) ||
		( start >= input_size ) ||
		( ( start+len ) > input_size ) )
	{
		instance_error_string_out_of_bounds();
		return;
	}

	// create temp string
	new_text = ( char * )heap_malloc( new_size );

	// build up into new string
	strncpy( new_text, text, position );
	strncat( new_text, &input[start], len );
	strcat( new_text, &text[position] );

	// resize object
	object_data_resize( object, new_size );

	// get buffer again - it was resized
	text = ( char * )object_get_ptr( object );

	// and add string
	strcpy( text, new_text );

	// delete temp string
	heap_free( new_text );
}

object_t strings_get_substring( object_t object, uint32_t position, uint32_t len )
{
	uint32_t size = 0;
	char * text;
	char * input;
	object_t new_object;

	// get everything to determine new size
	text = ( char * )object_get_ptr( object );
	size = ( uint32_t )strlen( text );

	if( len == NEG_U32 ) len = size - position;

	// for exception
	if( ( position >= size ) ||
		( ( position+len ) > size ) )
	{
		instance_error_string_out_of_bounds();
		return 0;
	}

	new_object = strings_create_capacity( len + 1 );
	input = strings_get_text_ptr( new_object );

	// copy string
	strncpy( input, &text[position], len );

	return new_object;
}

void strings_delete( object_t object, uint32_t position, uint32_t len )
{
	uint32_t size = 0;
	char * text;

	// get everything to determine new size
	text = ( char * )object_get_ptr( object );
	size = ( uint32_t )strlen( text );
    
    if( position == NEG_U32 ) position = size - 1;
    if( len == NEG_U32 ) len = size - position;

	// for exception
	if( ( position >= size ) ||
		( ( position+len ) > size ) )
	{
		instance_error_string_out_of_bounds();
		return;
	}

	// copy string
	strcpy( &text[position], &text[position+len] );

	// resize
	size = ( uint32_t )strlen( text ) + 1;
	object_data_resize( object, size );
}

object_t strings_duplicate( object_t object )
{
	char * text;
	object_t new_string;

	// get everything to determine new size
	text = ( char * )object_get_ptr( object );

	// create new string
	new_string = strings_create( text );

	return new_string;
}

char * strings_get_text_ptr( object_t object )
{
	return ( char * )object_get_ptr( object );
}
