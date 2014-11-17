/* See one of the index files for license and other details. */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdbool.h>
#include "sanity.h"
#include "json.h"
#include "xml.h"

static void indent( FILE *fp, unsigned depth );
static bool xstr( FILE *fp, const char *s );
static bool xvalue( FILE *fp, const jvalue *j, unsigned depth );

/** Writes the parsed JSON value tree out to the supplied file
 *  descriptor in XML, using the grammar described in the man page for
 *  jsoncvt. */
bool
writexml( FILE *fp, const jvalue *j )
{
    fputs( "<?xml version='1.0' encoding='utf-8' ?>\n", fp );
    fputs( "<!DOCTYPE jsoncvt PUBLIC '-//KRZ//DTD jsoncvt 1.0.5//EN' 'http://www.cis.rit.edu/~krz/hacks/jsoncvt/jsoncvt.dtd'>\n", fp );
    fputs( "<jsoncvt>\n", fp );
    int r = xvalue( fp, j, 1 );
    fputs( "</jsoncvt>\n", fp );
    return r;
}

/** For a given jvalue, print its opening element. It may optionally
 *  contain a name attribute. */
static void
xopen( FILE *fp, const jvalue *j, unsigned depth )
{
    indent( fp, depth );

    switch( j->d ) {
    case jnull:
        fputs( "<null", fp );
        break;
    case jtrue:
        fputs( "<true", fp );
        break;
    case jfalse:
        fputs( "<false", fp );
        break;
    case jstring:
        fputs( "<string", fp );
        break;
    case jnumber: case jint: case jreal:
        fputs( "<number", fp );
        break;
    case jarray:
        fputs( "<array", fp );
        break;
    case jobject:
        fputs( "<object", fp );
        break;
    }

    if( j->n ) {
        fputs( " name='", fp );
        xstr( fp, j->n );
        fputc( '\'', fp );
    }

    switch( j->d ) {
    case jnull: case jtrue: case jfalse:
        fputs( " />", fp );
        break;
    case jarray: case jobject:
        fputs( ">\n", fp );
        break;
    default:
        fputc( '>', fp );
        break;
    }
}

/** For a given jvalue, print its closing element. */
static void
xclose( FILE *fp, const jvalue *j, unsigned depth )
{
    switch( j->d ) {
    case jnull: case jtrue: case jfalse:
        break;
    case jarray:
        indent( fp, depth );
        fputs( "</array>", fp );
        break;
    case jobject:
        indent( fp, depth );
        fputs( "</object>", fp );
        break;
    case jstring:
        fputs( "</string>", fp );
        break;
    case jnumber: case jint: case jreal:
        fputs( "</number>", fp );
        break;
    }
    fputc( '\n', fp );
}

/** Given a JSON value, write its value to the supplied output stream. */
static bool
xvalue( FILE *fp, const jvalue *j, unsigned depth )
{
    xopen( fp, j, depth );

    switch( j->d ) {
    case jnull: case jtrue: case jfalse:
        break;
    case jstring:
        xstr( fp, j->u.s );
        break;
    case jnumber:
        fputs( j->u.s, fp );
        break;
    case jint:
        fprintf( fp, "%llu", j->u.i );
        break;
    case jreal:
        fprintf( fp, "%Lg", j->u.r );
        break;
    case jarray: case jobject:
        for( jvalue **jj = j->u.v; *jj; ++jj )
            xvalue( fp, *jj, depth+1 );
        break;
    }

    xclose( fp, j, depth );

    return true;
}

/** Write the supplied string onto the outfile file stream, escaping
 *  the main five standard entities along the way. Because we know
 *  that the JSON parser went out of its way to store text as UTF-8,
 *  we don't actually have to do anything special here. Returns <0 if
 *  there's an error. */
static bool
xstr( FILE *fp, const char *s )
{
    while( *s ) {
        if( *s == '<' )
            fputs( "&lt;", fp );
        else if( *s == '>' )
            fputs( "&gt;", fp );
        else if( *s == '&' )
            fputs( "&amp;", fp );
        else if( *s == '\'' )
            fputs( "&apos;", fp );
        else if( *s == '"' )
            fputs( "&quot;", fp );
        else
            fputc( *s, fp );
        ++s;
    }
    return true;
}

/** Given a nesting depth (zero being the outermost element), emit
 *  some number of spaces that are appropriate for that depth. */
static void
indent( FILE *fp, unsigned depth )
{
    while( depth-- )
        fputs( "  ", fp );
}
