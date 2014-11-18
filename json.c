/* See one of the index files for license and other details. */
#define _POSIX_C_SOURCE 200112L
#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "sanity.h"
#include "twine.h"
#include "ptrvec.h"
#include "json.h"

/** This just makes it easier for us to track a line counter along
 *  with an input stream, so when we report errors, we can say
 *  something useful about where the error appeared. getch() will bump
 *  #line when a newline appears on the file stream. Initialize this
 *  with a file stream opened for reading, and set #line to 1. */
typedef struct ifile {
    FILE *fp;                   /**< Input file stream */
    size_t line;                /**< Line number */
} ifile;

static jvalue *readvalue( ifile * );

/* When we're running single threaded, use the faster getc_unlocked(3)
 * that doesn't enforce thread safety; otherwise, use the regular
 * getc(3) which works everywhere. This is a bit of overkill, since
 * once a file stream is passed to the JSON parser, _even if_ we were
 * multithreaded, this is the only code that should be reading from
 * the stream. But, still, let's try to play by the rules. */
inline static int
jgetc( FILE *f )
{
#ifdef _REENTRANT
    return getc( f );
#else
    return getc_unlocked( f );
#endif
}

/** Returns the next character in the open input stream under \a f,
 *  and bump the line counter in \a f when appropriate. Errors can
 *  always be reported using line. */
static int
getch( ifile *f )
{
    int c = jgetc( f->fp );
    if( c == '\n' )
        ++f->line;
    return c;
}

/** Return a character back to the file stream under \a f, like
 *  ungetc() would, but also manage its line counter. Though many
 *  stdio's can handle it, there is actually no guarantee that more
 *  than a single character can ever be pushed back onto the file
 *  stream. */
static int
ungetch( ifile *f, char c )
{
    if( c == '\n' )
        --f->line;
    return ungetc( c, f->fp );
}

/** A wrapper around getch() that skips any leading whitespace before
 *  the character eventually returned, or EOF. Rather than a
 *  traditional parsing of whitespace, we limit ourselves to only the
 *  ws characters defined in JSON. */
static int
getchskip( ifile *f )
{
    int c;
    do {
        c = getch( f );
    } while( c == ' ' || c == '\t' || c == '\n' || c == '\r' );
    return c;
}

/** Skip ahead over any leading whitespace, leaving the next
 *  non-whitespace character in the stream ready for reading. The
 *  character returned is effectively a "peek" ahead at the next
 *  character that will be obtained from getch(). */
static int
skipws( ifile *f )
{
    int c = getchskip( f );
    if( c != EOF )
        ungetch( f, c );
    return c;
}

/** Create and return a new jvalue, initialized to be a jnull. Does
 *  not return if a new jvalue could not be allocated. */
jvalue *
jnew()
{
    jvalue *j = emalloc( sizeof( *j ));
    *j = (jvalue){ 0 };
    j->d = jnull;
    return j;
}

/** Walk a tree of jvalue, or even just a single jvalue, and free
 *  everything it contains, leaving \j intact but set to jnull.
 *  Normally, we'd set the various freed pointers to null explicitly,
 *  but at the end of the function, we zero the entire structure. */
jvalue *
jclear( jvalue *j )
{
    if( j ) {
        free( j->n );

        switch( j->d ) {
        case jarray:
        case jobject:
            if( j->u.v )
                for( jvalue **jv = j->u.v; *jv; ++jv )
                    jdel( *jv );
            free( j->u.v );
            break;

        case jstring:
        case jnumber:
            free( j->u.s );
            break;

        default:
            break;
        }

        *j = (jvalue){ 0 };
        j->d = jnull;
    }

    return j;
}

/** Walk a tree of jvalue, or even just a single value, and free
 *  everything it contains. Everything, even \a j itself, is freed.
 *  When this is complete, \a j is <em>no longer valid.</em> */
void
jdel( jvalue *j )
{
    free( jclear( j ));
}

/** Report an early EOF; that is, that the input stream ended before a
 *  value being read was finished. Bad syntax, truncated files, all
 *  the usual errors like that will trigger this. There's no point in
 *  reporting the line number, since this is an EOF. This is a
 *  function of its own, since it happens so often. */
static void
earlyeof()
{
    err( "premature EOF in JSON data" );
}

/** Almost all of our errors end with a line number and a message that
 *  the user should be looking in the JSON data. Sure, it's a little
 *  clunky, but factoring it out into a common routine shortens a lot
 *  of our subsequent code. The standard for variable arguments,
 *  though, doesn't allow for the obvious simple implementation of
 *  this function, so we have to do it the annoying way with temporary
 *  buffers and such. Also, we could use vasprintf, instead of
 *  vsnprintf; we don't, because most implementations of *asprintf are
 *  naive about initial buffer allocations and do all sorts of malloc
 *  and reallocs under the hood. This approach sucks up a page on the
 *  stack, but lets go of the space immediately and doesn't fragment
 *  our heap further. */
static void
ierr( const ifile *f, const char *msg, ... )
{
    va_list ap;
    char buf[ 512 ];

    va_start( ap, msg );
    vsnprintf( buf, sizeof( buf ), msg, ap );
    va_end( ap );
    err( "%s on line %zu in JSON data", buf, f->line );
}

/** The next character read from \a f must be a double quote. */
bool
expectdq( ifile *f )
{
    int c = getch( f );

    if( c == EOF ) {
        earlyeof();
        return false;
    } else if( c != '"' ) {
        ierr( f, "missing quote from string" );
        return false;
    } else
        return true;
}

/** Reads a JSON string that is wrapped with quotes from \a f, parsing
 *  all the various string escapes therein. Returns a C string (sans
 *  quotes) freshly allocated from the heap, or a null on error. When
 *  null is returned, a diagnostic will have been sent to the standard
 *  error stream. */
static char *
readstring( ifile *f )
{
    if( !expectdq( f ))
        return 0;

    bool esc = false;           /* the next character is escaped */
    bool oops = false;          /* a bad string was seen? */
    unsigned int hex = 0;       /* read this many chars as a hex
                                 * Unicode code point */
    unsigned int x = 0;
    twine tw = (twine){ 0 };

    while( !oops ) {
        int c = getch( f );

        if( c == EOF ) {
            earlyeof();
            oops = true;

        } else if( hex ) {
            if( isxdigit( c )) {
	        if( isdigit( c ))
		    x = 16 * x + c - '0';
		else if( isupper( c ))
		    x = 16 * x + c - 'A' + 10;
		else
		    x = 16 * x + c - 'a' + 10;
                if( !--hex )
                    twaddu( &tw, x );
            } else {
                ierr( f, "expected hex digit" );
                oops = true;
            }

        } else if( esc ) {
            switch( c ) {
            case '"':  twaddc( &tw, '"' ); break;
            case '/':  twaddc( &tw, '/' ); break;
            case '\\': twaddc( &tw, '\\' ); break;
            case 'b':  twaddc( &tw, '\b' ); break;
            case 'f':  twaddc( &tw, '\f' ); break;
            case 'n':  twaddc( &tw, '\n' ); break;
            case 'r':  twaddc( &tw, '\r' ); break;
            case 't':  twaddc( &tw, '\t' ); break;
            case 'u':
                x = 0;
                hex = 4;
                break;
            default:
                ierr( f, "unknown escape code '\\%c'", (char)c );
                oops = true;
            }
            esc = 0;
        
        } else if( c == '"' )    /* done parsing the string! bye! */
            return twfinal( &tw );

        else if( c == '\\' )
            esc = 1;

        else if( c >= ' ' )
            twaddc( &tw, c );

        else if( isspace( c )) {
            ierr( f, "unescaped whitespace" );
            oops = true;

        } else {
            ierr( f, "unknown byte (0x%02x)", c );
            oops = true;
        }
    }

    twclear( &tw );             /* oops. bad string. give up and go
                                 * home. */
    return 0;
}

/** Used by readnumber(), this is passed the twine under construction
 *  and returns the value we should return to the readnumber() caller.
 *  It ensures that any storage accumulated in supplied twine is
 *  returned to the system, and returns 0, which readnumber() should
 *  chain return back to its caller, indicating a failure. Because
 *  this is only used by readnumber(), we'll mark it static. */
static char *
readnumberfail( twine *tw )
{
    twclear( tw );
    return 0;
}

/** We just peeked ahead and saw something that introduces a number.
 *  Gather it up into a string. The client can opt to convert this
 *  into a real number (integer or real) via jupdate() if they choose.
 *  Now, we could simply collect characters from a set [-+.0-9eE] and
 *  that would suffice, but instead, we'll do this the long way so
 *  that we can catch errors in bogus numeric fields (e.g.,
 *  "123.456.789"). */
static char *
readnumber( ifile *f )
{
    int c;
    twine tw = (twine){ 0 };

    if(( c = getch( f )) == '-' )		/* sign bit */
        twaddc( &tw, c );
    else
        ungetch( f, c );

    if(( c = getch( f )) == '0' ) {		/* integer */
        twaddc( &tw, c );
	c = getch( f );
    } else if( isdigit( c ) && c != '0' ) {
        do {
	    twaddc( &tw, c );
	    c = getch( f );
	} while( isdigit( c ));
    } else {
        ierr( f, "unexpected '%c'", c );
	return readnumberfail( &tw );
    }

    if( c == '.' )				/* fraction */
	do {
	    twaddc( &tw, c );
	    c = getch( f );
	} while( isdigit( c ));

    if( c == 'e' || c == 'E' ) {		/* exponent */
	twaddc( &tw, c );
	c = getch( f );
	if( c == '+' || c == '-' ) {
	    twaddc( &tw, c );
	    c = getch( f );
	}
	while( isdigit( c )) {
	    twaddc( &tw, c );
	    c = getch( f );
	}
    }

    if( c == ',' || c == ']' || c == '}' )	/* acceptable term */
	ungetch( f, c );
    else if( c != EOF && !isspace( c )) {	/* unacceptable */
        ierr( f, "unexpected '%c'", c );
	return readnumberfail( &tw );
    }

    return twfinal( &tw );
}

/** The next characters in the file stream \a f must match the ones
 *  we've been given in the string \a s. */
static bool
must( ifile *f, const char *s )
{
    int c;
    const char *p = s;

    while( *p && (( c = getch( f )) != EOF ))
        if( *p++ != c )
            break;

    if( !*p )
        return true;

    if( c == EOF )
        earlyeof();
    else
        ierr( f, "expected %s", s );

    return false;
}

/** With the stream pointing to a JSON string, read the object element
 *  at this point. Returns a new jvalue, or null when there is an
 *  error. */
static jvalue *
readobjel( ifile *f )
{
    char *n;

    if( !( n = readstring( f )))
        return 0;

    if( getchskip( f ) != ':' ) {
        ierr( f, "expected colon in object element" );
        free( n );
        return 0;
    }

    jvalue *j = readvalue( f );
    if( !j )
        free( n );
    else
        j->n = n;
    return j;
}

/** Reads a series of values from the JSON input stream at \a f,
 *  storing it in the #u.v member of \a j. We're passed a
 *  discriminator so we know whether we're parsing a simple array or
 *  an object; an object is just an array with names and colons before
 *  it. Processing of the actual elements is pretty simple, actually.
 *  Returns false when a parsing error is detected (which is
 *  reported). */
static bool
readseries( jvalue *j, ifile *f, enum jtypes t )
{
    char term;
    jvalue *(*reader)( ifile* ) = 0; /* reads an element */

    switch( t ) {
    case jarray:
        term = ']';
        reader = readvalue;
        break;
    case jobject:
        term = '}';
        reader = readobjel;
        break;
    default:
        ierr( f, "internal error jtype %d in readseries", (int)t );
        return false;
    }

    /* Peeking ahead in the stream saw [ or { which is how we got
       called. So go ahead and throw it away. */
    getch( f );

    ptrvec pv = (ptrvec){ 0 };
    for( bool oops = false; !oops; ) {
        jvalue *x = 0;
        int c = skipws( f );
        
        if( c == EOF ) {
            earlyeof();
            oops = true;

        } else if( c == ',' ) {
            if( pv.len == 0 ) {
                ierr( f, "missing value before comma" );
                oops = true;
            }
            getch( f );             /* consume the , */

        } else if( c == term ) {     /* we're done! */
            getch( f );             /* consume the } */
            j->u.v = (jvalue**)pvfinal( &pv );
            return true;

        } else if( !( x = reader( f )))
            oops = true;

        else
            pvadd( &pv, x );
    }

    pvclear( &pv );
    return false;
}

/** Get the next value out of the file stream \a f, storing it in a
 *  jvalue newly allocated from the heap. Returns 0 on a parsing
 *  failure (with diagnostic(s) sent to the standard error stream).
 *  This function is recursive; if the value in \a f is an array or an
 *  object, a properly nested jvalue tree is returned. Any leading
 *  whitespace is skipped. */
static jvalue *
readvalue( ifile *f )
{
    int c;
    jvalue *j = jnew();

    switch(( c = skipws( f ))) {
    case EOF:
        earlyeof();
        break;
    case 'f':
        if( must( f, "false" )) {
            j->d = jfalse;
            return j;
        }
        break;
    case 'n':
        if( must( f, "null" )) {
            j->d = jnull;
            return j;
        }
        break;
    case 't':
        if( must( f, "true" )) {
            j->d = jtrue;
            return j;
        }
        break;
    case '{':
        if( readseries( j, f, j->d = jobject ))
            return j;
        break;
    case '[':
        if( readseries( j, f, j->d = jarray ))
            return j;
        break;
    case '"':
        if(( j->u.s = readstring( f ))) {
            j->d = jstring;
            return j;
        }
        break;
    case '-': case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        if(( j->u.s = readnumber( f ))) {
            j->d = jnumber;
            return j;
        }
        break;
    default:
        ierr( f, "unexpected '%c'", (char)c );
    }

    jdel( j );
    return 0;
}

/** Parse the opened for reading file stream \a into a new jvalue,
 *  which is returned. That jvalue is actually a parse tree, but for
 *  some limited cases, a single top level value might be returned.
 *  The function returns 0 on a failed parse (and diagnostics will be
 *  printed to stderr). This is slightly more liberal than the JSON
 *  standard, in that the outermost value of the stream at \a fp
 *  isn't limited to being just an array or object, but can also be
 *  any other JSON value. */
jvalue *
jparse( FILE *fp )
{
    if( !fp )
        return 0;

    ifile f = (ifile){ .fp = fp, .line = 1 };
    return readvalue( &f );
}

/** This is only used by jupdate, so we hide it static to this file.
 *  It returns true if the supplied string appears to just be an
 *  integer number.  Specifically, this means it does not contain an
 *  exponent nor a decimal point, as those typically introduce reals.
 *  It's arguable that an exponent, itself, doesn't necessarily mean
 *  a value is a real.  But, because it's so easy to exceed the range
 *  of an integer with a really huge exponent, nothing short of
 *  parsing the number itself makes it a safe call to deal with
 *  exponential integers.  So, exponents mean "not integer".  Also,
 #  because we assume that we're parsing numbers gathered by
 *  readnumber() via jparse(), we don't bother testing on isdigit()
 #  like a well behaved function would; otherwise, failing isdigit()
 #  in the while loop would also be an immediate return false. */
static bool
integerp( const char *str )
{
    if( *str == '-' || *str == '+' )
        ++str;
    while( *str )
        if( *str == '.' || *str == 'e' || *str == 'E' )
            return false;
	else
	    ++str;
    return true;
}

/** Given a jvalue, "update" it or its children. "Update" means
 *  several things, but it basically finishes the work started by
 *  jparse(). jparse() implements a quick parse of a JSON stream, but
 *  does things like leaving numbers as strings, in the event that the
 *  caller doesn't need lossy conversions introduced by atof().
 *  Calling jupdate() effectively "finishes" the parse, converting
 *  everything into native formats. */
jvalue *
jupdate( jvalue *j )
{
    if( j )
        switch( j->d ) {
        case jnumber:
	    if( integerp( j->u.s )) {
                j->u.i = strtoll( j->u.s, 0, 10 );
                j->d = jint;
            } else {
                j->u.r = strtold( j->u.s, 0 );
                j->d = jreal;
            }
            break;
        case jarray:
	case jobject:
            for( jvalue **jv = j->u.v; *jv; ++jv )
                jupdate( *jv );
            break;
        default:
            break;
        }

    return j;
}

/** Emit some number of spaces for each level of indentation we're at. */
static void
indent( FILE *fp, unsigned int n )
{
    while( n-- > 0 )
        fputs( "    ", fp );
}

/** Dump out a jvalue in some structured form. This is not a
 *  converter, this is just a debug activity. */
static int
jdumpval( FILE *fp, const jvalue *j, unsigned int depth )
{
    indent( fp, depth );

    switch( j->d ) {
    case jnull:
        fputs( "null\n", fp );
        break;
    case jtrue:
        fputs( "true\n", fp );
        break;
    case jfalse:
        fputs( "false\n", fp );
        break;
    case jstring:
        if( j->u.s ) {
            fputs( "string \"", fp );
            for( char *c = &j->u.s[0]; *c; ++c )
                if( *c == 8 )                   fputs( "\\t", fp );
                else if( *c == 10 )             fputs( "\\n", fp );
                else if( *c == 13 )             fputs( "\\r", fp );
                else if( *c == 34 )             fputs( "\\\"", fp );
                else if( *c == 92 )             fputs( "\\\\", fp );
                else if( 32 <= *c && *c < 127 ) fputc( *c, fp );
                else
                    fprintf( fp, "\\x%02x", (unsigned) *c );
            fputs( "\"\n", fp );
        } else
            fputs( "NULL string (oops)\n", fp );
        break;
    case jnumber:
        if( j->u.s )
            fprintf( fp, "number %s\n", j->u.s );
        else
            fputs( "NULL number (oops)\n", fp );
        break;
    case jint:
        fprintf( fp, "integer %lld\n", j->u.i );
        break;
    case jreal:
        fprintf( fp, "real %Lg\n", j->u.r );
        break;
    case jarray:
        fputs( "array\n", fp );
        for( jvalue **jv = j->u.v; *jv; ++jv )
            jdumpval( fp, *jv, depth+1 );
        break;
    case jobject:
        fputs( "object\n", fp );
        for( jvalue **jv = j->u.v; *jv; ++jv ) {
            indent( fp, depth+1 );
            if( (*jv)->n )
                fputs( (*jv)->n, fp );
            else
                fputs( "NULL name (oops)", fp );
            fputc( '\n', fp );
            jdumpval( fp, *jv, depth+2 );
        }
        break;
    default:
        fprintf( fp, "unknown %d (oops)\n", j->d );
        break;
    }
    return 0;
}

/** Dump out a JSON parse tree onto \a fp. This is really the entry
 *  point for the jdumpval recursion. */
int
jdump( FILE *fp, const jvalue *j )
{
    return jdumpval( fp, j, 0 );
}
