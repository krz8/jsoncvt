/* See one of the index files for license and other details. */
#ifndef jsoncvt_json_h
#define jsoncvt_json_h
#pragma once
#include <stdio.h>

/** The different types of values in our JSON parser. Unlike the
 *  standard, we discriminate between integer and real values. */
enum jtypes {
    jnull,      /**< The JSON "null" value. */
    jtrue,      /**< Just a simple "true" value. */
    jfalse,     /**< Just a simple "false" value. */
    jstring,    /**< Just your run of the mill string. */
    jnumber,    /**< A JSON number (still just a string). */
    jarray,     /**< A vector of values. */
    jobject,    /**< An assoc. array of names and arbitrary values. */
    jint,       /**< A JSON number parsed into a native integer. */
    jreal,      /**< A JSON number parsed into a long double. */
};

/** A jvalue represents the different values found in a parse of a
 *  JSON doc. A value can be terminal, like a string or a number, or
 *  it can nest, as with arrays and objects. The value of #d reflects
 *  which part of the union is value. */
typedef struct jvalue {

    /** Just your basic discriminator, describing which part of the
     *  union below is active. When this is jtrue, jfalse, or jnull,
     *  nothing in #u is valid (being unnecessary); all other values
     *  correspond to one of the #u members as described below. */
    enum jtypes d;

    /** Some values have a name associated with them; in a JSON
     *  object, for example, the value is assigned to a specific name.
     *  When #d is jobject, this string should point to the name of a
     *  member (whose value is in #u). For other values of #d, this
     *  member should be null. A previous implementation used a
     *  separate structure for these pairings, but placing the name
     *  inside each value only costs an extra 4 or 8 bytes yet
     *  simplifies the tree quite a bit for our client. */
    char *n;

    /** According to #d above, one or none of these are the active value. */
    union {
        /** When #d is jstring or jnumber, this string is active in
         *  the union. While obvious for jstring, why would this be
         *  used for jnumber? Because, often, there's no need to parse
         *  the number value into something native. While integers are
         *  exact, there's often an unavoidable loss of precision
         *  when converting real numbers. So, we defer it as long as
         *  we can. If the client application actually *wants* a
         *  parsed value, it can convert the string to a native value,
         *  cache it away in the #i or #r members, and change the
         *  discriminator to jint or jreal accordingly. This avoids
         *  unnecessary parsing work and loss of precision, but
         *  doesn't make it unduly hard for a client to deal with. See
         *  jupdate() as a function the client can call to do just
         *  that. */
        char *s;

        /** When the discriminator is jint, this integer is active. */
        long long i;

        /** When the discriminator is jreal, this long double is
         *  active. */
        long double r;

        /** When the discriminator is jarray or jobject, this
         *  zero-terminated vector of pointers to jvalue is active.
         *  You'll find the ptrvec routines make building these
         *  easy. */
        struct jvalue **v;
    } u;
} jvalue;

extern jvalue *jnew();
extern jvalue *jclear( jvalue * );
extern void jdel( jvalue * );
extern jvalue *jparse( FILE *fp );
extern jvalue *jupdate(  jvalue * );
extern int jdump( FILE *fp, const jvalue *j );

#endif

