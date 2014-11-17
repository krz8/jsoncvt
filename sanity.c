/* See one of the index files for license and other details. */
#define _POSIX_C_SOURCE 200112L
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sanity.h"

/** Allocate some number of bytes from the system and return a pointer
 *  to them, or exit. */
void *
emalloc( size_t nb )
{
    void *p = malloc( nb );
    if( !p )
        die( 1, "unable to allocate %zu bytes", nb );
    return p;
}

/** Change an allocated buffer to another size, returning a pointer to
 *  the new buffer. If the buffer could not be grown, an error is
 *  displayed and the process exits (this function does not return). */
void *
erealloc( void *ptr, size_t nb )
{
    void *p = realloc( ptr, nb );
    if( !p )
        die( 1, "unable to reallocate %zu bytes", nb );
    return p;
}

/** Just like strdup(3), but exits on failure instead of returning
 *  crap. */
char *
estrdup( const char *s )
{
    return s ? strcpy( emalloc( strlen( s ) + 1 ), s ) : 0;
}

/** Wraps the common part of our formatting for err() and die() below. */
static void
va_err( const char *extra, const char *msg, va_list ap )
{
    fputs( "jsoncvt: ", stderr );
    if( extra )
        fputs( extra, stderr );
    vfprintf( stderr, msg, ap );
    fputc( '\n', stderr );
}

/** Inform the user of some printf(3) style error. */
void
err( const char *msg, ... )
{
    va_list ap;
    va_start( ap, msg );
    va_err( 0, msg, ap );
    va_end( ap );
}

/** Display a printf(3) style error message, and then exit directly
 *  with status code \a x. This function never returns. */
void
die( int x, const char *msg, ... )
{
    va_list ap;
    va_start( ap, msg );
    va_err( "fatal: ", msg, ap );
    va_end( ap );
    exit( x );
}
