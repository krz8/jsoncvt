/* See one of the index files for license and other details. */
#define _POSIX_C_SOURCE 200112L
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sanity.h"
#include "twine.h"

enum {
    /** The initial size allocation for a twine. Twines start off
     * empty (zero bytes in size), and when they grow, this is their
     * first size. It's arbitrary, really, what you start with; we're
     * going with 16 because it's the cache line size on modern x86
     * hardware; anything smaller would be pointless. */
    tw_initial_size = 16
};

/** Create a new empty twine and return a pointer to it. This
 *  function never returns if it cannot allocate the requested
 *  memory. */
twine *
twnew()
{
    twine *t = emalloc( sizeof( *t ));
    *t = (twine){ 0 };
    return t;
}

/** Release a twine obtained via strnew() and all of its memory. Once
 *  you've called this, \a t is no longer valid. */
void
twdel( twine *t )
{
    twclear( t );
    free( t );
}

/** Zero a twine, returning its storage back to the system, but
 *  leaving the twine structure still valid (though zeroed). Useful
 *  for twine structure defined on the stack, for example. Also, it's
 *  a severe way to clear a twine of current data, but useful if
 *  you've got to return memory. */
twine *
twclear( twine *t )
{
    if( t->p )
        free( t->p );
    *t = (twine){ 0 };
    return t;
}

/** Return a pure C string that is a copy of the string we've been
 *  building in our twine. Unlike our string, this one will be
 *  allocated from the heap and contains just enough space to hold it. */
char *
twdup( const twine *t )
{
    char *p = emalloc( t->len + 1 );
    return strcpy( p, t->p ? t->p : "" );
}

/** A wrapper for the common case at the end of working with twine.
 *  Return a C string ready for storage somewhere, and kill our own
 *  storage so that the next thing to come along can use our memory. */
char *
twfinal( twine *t )
{
    char *p = twdup( t );
    twclear( t );
    return p;
}

/** Given a target size, resize the underlying buffer to be just large
 *  enough to handle it. This function doesn't pad space like the
 *  twadd*() functions will, and it might even shrink the buffer
 *  depending on how this system's realloc(3) is set up. This is
 *  because if you've added once, you're likely to add again, but if
 *  you have a size in mind in advance, you probably don't need to
 *  grow it soon. */
twine *
twsize( twine *t, size_t nb )
{
    if( !nb )
        return twclear( t );

    t->p = erealloc( t->p, t->sz = nb );
    if( t->len >= t->sz ) {
        t->len = t->sz - 1;
        t->p[ t->len ] = 0;
    }
    return t;
}

/** Copy into one of our twines a null-terminated C twine. Does not
 *  return if we cannot allocate enough memory. This operation doesn't
 *  pad the size of the twine with any reserve space, because most of
 *  the time, ssetz() is called on static twines that aren't going
 *  to be modified. */
twine *
twsetz( twine *t, const char *z )
{
    size_t zlen = strlen( z );

    /* Life is easy if we know that the source isn't overlapping with
       our twine. Given that the source and the destination are two
       different types, this should never happen, anyway. But if
       something is screwed up, we'll try to dodge the imminent core
       dump and do this in a slower, more wasteful fashion. */
    char *src = ( !t->p || (( z >= t->p + t->sz ) && ( z + zlen < t->p )))
        ? (char*)z
        : estrdup( z );

    if( zlen + 1 < tw_initial_size ) {
        twsize( t, tw_initial_size );
        t->len = zlen;
    } else
        twsize( t, ( t->len = zlen ) + 1 );
    strcpy( t->p, src );

    if( src != z )
        free( src );
    return t;
}

/** Copy into one of our twines some number of characters. */
twine *
twset( twine *t, const char *z, size_t nb )
{
    /* Life is easy if we know that the source isn't overlapping with
       our twine. Given that the source and the destination are two
       different types, this should never happen, anyway. But if
       something is screwed up, we'll try to dodge the imminent core
       dump and do this in a slower, more wasteful fashion. */
    char *src = ( !t->p || (( z >= t->p + t->sz ) && ( z + nb < t->p )))
        ? (char*)z
        : estrdup( z );

    if( nb + 1 < tw_initial_size ) {
        twsize( t, tw_initial_size );
        t->len = nb;
    } else
        twsize( t, ( t->len = nb ) + 1 );

    strncpy( t->p, src, nb );
    t->p[nb] = 0;

    if( src != z )
        free( src );
    return t;
}

/** Grow a twine as much as necessary to satisfy some number of bytes.
 *  This only concerns itself with size, not the logical length, so be
 *  sure to add the null in yourself to \a sz. The 1.5X growth factor
 *  is meant to balance between calling realloc too often, but not
 *  wasting memory like mad like some libraries do. */
static twine *
twensure( twine *t, size_t sz )
{
    size_t newsz;

    if( !sz || sz <= t->sz )
        return t;
    if( sz <= tw_initial_size )
        return twsize( t, tw_initial_size );

    newsz = t->sz * 3 / 2;
    if( newsz < sz )
        newsz = sz * 3 / 2;
    if( newsz < t->sz )
        die( 1, "twine overflow" );

    return twsize( t, newsz );
}

/** Add a single plain character to our twine. This will grow the
 *  twine with padding if necessary; see twensure(). As with the other
 *  functions, if we can't get the memory we need, we just error and
 *  die. In this application, there's no point in trying to recover
 *  from an out of memory error. */
twine *
twaddc( twine *t, char c )
{
    twensure( t, t->len + 2 );
    char *p = t->p + t->len;
    *p++ = c;
    *p++ = 0;
    ++t->len;
    return t;
}

/** Like twaddc(), but this adds a null terminated C string. */
twine *
twaddz( twine *t, const char *z )
{
    size_t zlen = strlen( z );
    twensure( t, t->len + zlen + 1 );
    strcpy( t->p + t->len, z );
    t->len += zlen;
    return t;
}

/** Like twaddz(), but this adds another twine to us. */
twine *
twadd( twine *dst, const twine *src )
{
    twensure( dst, dst->len + src->len + 1 );
    strcpy( dst->p + dst->len, src->p );
    dst->len += src->len;
    return dst;
}

/** Adds a Unicode code point into the twine in UTF-8 format. Handles
 *  the full range of code points, up to 0x7ffffff. This is
 *  simplistic, and I think it's not quite comforming (apparently
 *  there's UTF-8 and there'S CESU and one has intentional omissions
 *  the other doesn't? */
twine *
twaddu( twine *t, uint32_t c )
{
    if( c <= 0x007f )
        twaddc( t, c );
    else if( c <= 0x07ff ) {
        twaddc( t, 0xc0 | ( c >> 6 ));
        twaddc( t, 0x80 | ( c & 0x3f ));
    } else if( c <= 0xffff ) {
        twaddc( t, 0xe0 | ( c >> 12 ));
        twaddc( t, 0x80 | ( c >> 6 & 0x3f ));
        twaddc( t, 0x80 | ( c & 0x3f ));
    } else if( c <= 0x1fffff ) {
        twaddc( t, 0xf0 | ( c >> 18 ));
        twaddc( t, 0x80 | ( c >> 12 & 0x3f ));
        twaddc( t, 0x80 | ( c >> 6 & 0x3f ));
        twaddc( t, 0x80 | ( c & 0x3f ));
    } else if( c <= 0x3ffffff ) {
        twaddc( t, 0xf8 | ( c >> 24 ));
        twaddc( t, 0x80 | ( c >> 18 & 0x3f ));
        twaddc( t, 0x80 | ( c >> 12 & 0x3f ));
        twaddc( t, 0x80 | ( c >> 6 & 0x3f ));
        twaddc( t, 0x80 | ( c & 0x3f ));
    } else if( c <= 0x7ffffff ) {
        twaddc( t, 0xfc | ( c >> 30 ));
        twaddc( t, 0x80 | ( c >> 24 & 0x3f ));
        twaddc( t, 0x80 | ( c >> 18 & 0x3f ));
        twaddc( t, 0x80 | ( c >> 12 & 0x3f ));
        twaddc( t, 0x80 | ( c >> 6 & 0x3f ));
        twaddc( t, 0x80 | ( c & 0x3f ));
    } else
        err( "unicode code point cannot be >0x7ffffff" );
    return t;
}
