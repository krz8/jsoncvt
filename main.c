/* See one of the index files for license and other details. */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdbool.h>
#include <getopt.h>
#include "sanity.h"
#include "json.h"
#include "xml.h"
#include "ksh.h"

const char usage[]="usage: jsoncvt [-kx] [label]\n"
    "example: jsoncvt -x mydata <foo.json >foo.xml\n";

int
main( int argc, char *argv[] )
{
    /* output is our driver, pointing to the routine indicated by the
     * command line option for different output languages. XML and
     * ksh93 are supported at present. */

    bool (*output)( FILE *, const jvalue * ) = writexml;
    int opt;

    while(( opt = getopt( argc, argv, "kx" )) != EOF )
        switch( opt ) {
        case 'k':
            output = writeksh;
            break;
        case 'x':
            output = writexml;
            break;
        default:
            fputs( usage, stderr );
            return 2;
        }

    argc -= optind;
    argv += optind;
    if( argc > 1 ) {
        err( "too many arguments" );
        return 2;
    }

    /* Okay, now that we know which output driver to use, pull in the
     * JSON data into a parse tree. If the parse was successful, label
     * it by setting its top value name to something from the command
     * line. Print it out, and go home. */

    jvalue *j = jparse( stdin );
    if( !j )
        return 1;

    j->n = estrdup( argc > 0 ? argv[0] : "foobar" );
    (*output)( stdout, j );
    jdel( j );

    return 0;
}
