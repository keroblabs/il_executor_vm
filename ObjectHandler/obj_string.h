#ifndef OBJ_STRING_H
#define	OBJ_STRING_H

#ifdef	__cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////

#include "instance.h"
	
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////

void	strings_itoa(char *str, int32_t val, uint32_t radix);
void	strings_ltoa(char *str, int64_t val, uint32_t radix);
void	strings_ftoa(char *str, float val);

int32_t    strings_atoi( const char * str, int radix );
int64_t    strings_atol( const char * str, int radix );
float    strings_atof( const char * str );

bool_t   strings_tokenizer(const char *s1, const char ** exit, int * size, const char *delimit, char ** lastToken);
object_t strings_split( object_t object, const char * delimit );

char strings_char_at( object_t object, uint32_t index );

int strings_compare( object_t object, const char * string );

int32_t  strings_index_of( object_t object, const char * search );

object_t strings_create( const char * string );
object_t strings_create_index( const char * string, uint32_t start, uint32_t len );
object_t strings_create_capacity( uint32_t length );
object_t strings_duplicate( object_t object );
object_t strings_get_substring( object_t object, uint32_t position, uint32_t size );

uint32_t strings_get_capacity( object_t object );

void strings_destroy( object_t object );

void strings_copy( object_t object, const char * new_string );
void strings_concat( object_t object, const char * add_text );
void strings_tolower( object_t object );
void strings_toupper( object_t object );
void strings_insert( object_t object, uint32_t position, const char * input, uint32_t start, uint32_t len );
void strings_delete( object_t object, uint32_t position, uint32_t size );
void strings_replace_char( object_t object, char replace_char, char new_char );

char * strings_get_text_ptr( object_t object );

///////////////////////////////////////////////////////////////////////////

#ifdef	__cplusplus
}
#endif

#endif	/* OBJ_STRING_H */

