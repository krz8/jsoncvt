/* See one of the index files for license and other details. */
#ifndef jsoncvt_sanity_h
#define jsoncvt_sanity_h
#pragma once
#include <stddef.h>

/* While this code is original, it is certainly inspired by the
 * excellent book, "The Practice of Programming" by Brian W. Kernighan
 * and Rob Pike.
 * 
 * Quoting from the book: "You may use this code for any purpose, as
 * long as you leave the copyright notice and book citation attached.
 * Copyright © 1999 Lucent Technologies. All rights reserved. Mon Mar
 * 19 13:59:27 EST 2001" */

extern void *emalloc( size_t );
extern void *erealloc( void *, size_t );
extern char *estrdup( const char * );
extern void err( const char *msg, ... );
extern void die( int xit, const char *msg, ... );

#endif
