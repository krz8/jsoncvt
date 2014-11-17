/* See one of the index files for license and other details. */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "sanity.h"
#include "json.h"
#include "ksh.h"

/** Given a nesting depth (zero being the outermost element), emit
 *  some number of spaces that are appropriate for that depth. */
static void
indent( FILE *fp, unsigned depth )
{
    while( depth-- )
        fputs( "  ", fp );
}

/** Write the string, in ksh C-string format, to the output stream. A
 * trailing newline is automatically appended. */
static void
emit( FILE *out, const char *s )
{
    --s;
    fputs( "$'", out );
    while( *++s )
        if( *s == 0x07 )        fputs( "\\a", out );
        else if( *s == 0x08 )   fputs( "\\b", out );
        else if( *s == 0x09 )   fputs( "\\t", out );
        else if( *s == 0x0a )   fputs( "\\n", out );
        else if( *s == 0x0b )   fputs( "\\v", out );
        else if( *s == 0x0c )   fputs( "\\f", out );
        else if( *s == 0x0d )   fputs( "\\r", out );
        else if( *s == 0x1b )   fputs( "\\E", out );
        else if( *s == '\'' )   fputs( "\\'", out );
        else if( *s == '\\' )   fputs( "\\\\", out );
        else if( *s < 32 )      fprintf( out, "\\x%02x", *s );
        else if( *s < 127 )     fputc( *s, out );
        else                    fprintf( out, "\\x%02x", *s );
    fputs( "'\n", out );
}

/** Write the string, in plain text, to the output stream. Safely
 *  replace all problem characters with underscore. This is primarily
 *  meant for non-C-strings, like variable or member names. */
static void
safe( FILE *out, const char *s )
{
    if(( *s >= 'A' && *s <= 'Z' ) || ( *s >= 'a' && *s <= 'z' ))
        fputc( *s, out );
    else
        fputc( '_', out );

    while( *++s )
        if(( *s >= 'A' && *s <= 'Z' ) || ( *s >= 'a' && *s <= 'z' )
           || ( *s >= '0' && *s <= '9' ))
            fputc( *s, out );
        else
            fputc( '_', out );
}

/** Returns true if \a j is a jarray and all of its jvalue.u.v members
 *  are the same type. This is useful for providing more specific type
 *  information to ksh when declaring arrays. The array must have at
 *  least one member in it for this to return true. */
static bool
sameval( const jvalue *j )
{
    if( !j || j->d != jarray || !j->u.v || !j->u.v[0] )
        return false;

    enum jtypes jd = j->u.v[0]->d;

    if( jd == jtrue || jd == jfalse ) {
        for( jvalue **jj = &j->u.v[1]; *jj; ++jj )
            if( (*jj)->d != jtrue && (*jj)->d != jfalse )
                return false;
    } else if( jd == jint || jd == jreal || jd == jnumber ) {
        for( jvalue **jj = &j->u.v[1]; *jj; ++jj )
            if( (*jj)->d != jint && (*jj)->d != jreal && (*jj)->d != jnumber )
                return false;
    } else
        for( jvalue **jj = &j->u.v[1]; *jj; ++jj )
            if( jd != (*jj)->d )
                return false;

    return true;
}

/** Returns true if \a j is a jarray wherein all of its are integers,
 *  either as jnumber strings without a decimal point or as jint
 *  values. */
static bool
allints( const jvalue *j )
{
    if( !j->u.v )
        return false;
    for( jvalue **jj = j->u.v; *jj; ++jj )
        switch( (*jj)->d ) {
        case jnumber:
            if( strchr( (*jj)->u.s, '.' ))
                return false;
            break;
        case jint:
            break;
        default:
            return false;
        }
    return true;
}

/** Write out a typeset string for an array of the given type. Since
 *  we're only called from ktypeset(), we can make safe assumptions
 *  about \a j. */
void
ktypesetarray( FILE *fp, const jvalue *j )
{
    if( allints( j ))
        fputs( "integer -a ", fp );
    else
        switch( sameval( j ) ? j->u.v[0]->d : jnull ) {
        case jtrue: case jfalse:
            fputs( "bool -a ", fp );
            break;
        case jnumber: case jreal:
            fputs( "float -a ", fp );
            break;
        case jobject:
            fputs( "compound -a ", fp );
            break;
        default:
            fputs( "typeset -a ", fp );
            break;
    }
}

/** Write out a typeset string that introduces the next word to be
 *  printed as a variable or compound member of the right type.
 *  Nothing is printed for strings and the like. */
void
ktypeset( FILE *fp, const jvalue *j )
{
    switch( j ? j->d : jnull ) {
    case jtrue: case jfalse:
        fputs( "bool ", fp );
        break;
    case jint:
        fputs( "integer ", fp );
        break;
    case jreal:
        fputs( "float ", fp );
        break;
    case jnumber:
        fputs(( j->u.s && strchr( j->u.s, '.' )) ? "float " : "integer ", fp );
        break;
    case jobject:
        fputs( "compound ", fp );
        break;
    case jarray:
        ktypesetarray( fp, j );
        break;
    default:
        break;
    }
}

/** Write the name of a jvalue out, with any necessary typeset
 *  information preceding it. An '=' is printed at the end. If \a j
 *  does not have a name, a fake name is generated on the fly. */
void
kname( FILE *fp, const jvalue *j )
{
    ktypeset( fp, j );

    if( !j || !j->n )
        fputs( "foobar=", fp );
    else {
        safe( fp, j->n );
        fputc( '=', fp );
    }
}

/** Writes the JSON value out to the supplied file descriptor. When \a
 *  nested is true and we encounter a jarray, we understand that we
 *  don't need to print a leading typeset or name, and skip right to
 *  the value; along those lines, when we encounter a jarray, we know
 *  to set nested true for the recursion, and set it false on jobject
 *  recursion. All of these are */
bool
kvalue( FILE *fp, const jvalue *j, bool nested, unsigned depth )
{
    indent( fp, depth );

    if( !nested )
        kname( fp, j );

    switch( j->d ) {
    case jnull:
        fputc( '\n', fp );
        break;
    case jtrue:
        fputs( "true\n", fp );
        break;
    case jfalse:
        fputs( "false\n", fp );
        break;
    case jstring:
        emit( fp, j->u.s );
        break;
    case jnumber:
        fputs( j->u.s, fp );
        fputc( '\n', fp );
        break;
    case jint:
        fprintf( fp, "%llu\n", j->u.i );
        break;
    case jreal:
        fprintf( fp, "%Lg\n", j->u.r );
        break;
    case jobject:
        fputs( "(\n", fp );
        for( jvalue **jj = j->u.v; *jj; ++jj )
            kvalue( fp, *jj, false, depth+1 );
        indent( fp, depth );
        fputs( ")\n", fp );
        break;
    case jarray:
        fputs( "(\n", fp );
        for( jvalue **jj = j->u.v; *jj; ++jj )
            kvalue( fp, *jj, true, depth+1 );
        indent( fp, depth );
        fputs( ")\n", fp );
        break;
    }

    return true;
}

bool
writeksh( FILE *fp, const jvalue *j )
{
    return kvalue( fp, j, false, 0 );
}
