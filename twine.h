/* See one of the index files for license and other details. */
#ifndef jsoncvt_twine_h
#define jsoncvt_twine_h
#pragma once
#include <stddef.h>
#include <stdint.h>

/** twine is like a string, it manages memory for a string, and can
 *  be used to create new C strings as necessary. It's called twine
 *  because it's like a string, but a little thicker; there's usually a
 *  spool of it handy, like the memory management it comes with; and
 *  it's easy to extract a string from it when you're done.
 *
 *  It serves primarily as a C-string builder with memory management.
 *  With it, you can build strings incrementally from a variety of
 *  sources, without the overhead of length calculations and
 *  realloc(3) calls every time. A trailing null is included even in
 *  the in-progress string, so that the internal string can be used
 *  directly when necessary.
 *
 *  Expected usage is something like
 *
 *  1. Obtain new twine via twnew(), or initialize one to all zeroes.
 *
 *  2. Use the twadd*() and twset*() functions to build up its contents.
 *  You can access the in-progress string via the p member at any
 *  time. Under the hood, p points to a null-terminated region bigger
 *  than necessary to reduce the number of reallocs necessary.
 *
 *  3. Use twdup() to create a new C string that is exactly the size
 *  needed for the resulting string, or use twfinal() below.
 *
 *  4. Free up any current space space via twclear(), resetting things
 *  to "empty" again. Set a hard size via twsize().
 *
 *  5. if you called twnew() earlier, call twdel() to free it. If you
 *  just want to free up the memory it uses but not the twine itself,
 *  call twclear(). twfinal() combines both twdup() and twclear().
 */
typedef struct twine {
    char *p;               /**< null terminated C string data */
    size_t len;            /**< size of the string, not counting null */
    size_t sz;             /**< size of the underlying buffer */
} twine;

extern twine *twnew();
extern twine *twclear();
extern char *twfinal( twine * );
extern void twdel( twine * );

extern char *twdup( const twine * );

extern twine *twsize( twine *, size_t );

extern twine *twset( twine *, const char *, size_t );
extern twine *twsetz( twine *, const char * );

extern twine *twadd( twine *, const twine * );
extern twine *twaddc( twine *, char );
extern twine *twaddu( twine *, uint32_t );
extern twine *twaddz( twine *, const char * );

#endif
