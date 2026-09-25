#ifndef OBJ_STRING_H
#define	OBJ_STRING_H

#ifdef	__cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////

#include "instance.h"
	
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////

void	obj_string_itoa(char *str, int32_t val, uint32_t radix);
void	obj_string_ltoa(char *str, int64_t val, uint32_t radix);
void	obj_string_ftoa(char *str, float val);

int32_t    obj_string_atoi( const char * str, int radix );
int64_t    obj_string_atol( const char * str, int radix );
float    obj_string_atof( const char * str );

bool_t   obj_string_tokenizer(const char *s1, const char ** exit, int * size, const char *delimit, char ** lastToken);
object_t obj_string_split( object_t object, const char * delimit );

char obj_string_char_at( object_t object, uint32_t index );

int obj_string_compare( object_t object, const char * string );

int32_t  obj_string_index_of( object_t object, const char * search );

object_t obj_string_create( const char * string );
object_t obj_string_create_index( const char * string, uint32_t start, uint32_t len );
object_t obj_string_create_capacity( uint32_t length );
object_t obj_string_duplicate( object_t object );
object_t obj_string_get_substring( object_t object, uint32_t position, uint32_t size );

uint32_t obj_string_get_capacity( object_t object );

void obj_string_destroy( object_t object );

void obj_string_copy( object_t object, const char * new_string );
void obj_string_concat( object_t object, const char * add_text );
void obj_string_tolower( object_t object );
void obj_string_toupper( object_t object );
void obj_string_insert( object_t object, uint32_t position, const char * input, uint32_t start, uint32_t len );
void obj_string_delete( object_t object, uint32_t position, uint32_t size );
void obj_string_replace_char( object_t object, char replace_char, char new_char );

char * obj_string_get_text_ptr( object_t object );

///////////////////////////////////////////////////////////////////////////

#ifdef	__cplusplus
}
#endif

#endif	/* OBJ_STRING_H */

