/* ash-euses: Two-Way string-searching algorithm implementation.
 * Ashley Dixon */

/* This file implements the Two-Way string-matching algorithm for finding the
 * location of  a needle in a haystack. It essentially consists of a combination
 * between the forward-running Knuth-Morris-Pratt algorithm, and the
 * backward-running Boyer-Moore algorithm, hence its name. A detailed
 * description of the original algorithm was described by Maxime Corchemore and
 * Dominique Perrin in a 1991 paper in the A.C.M. Journal, 3rd Issue:
 * (https://dl.acm.org/doi/abs/10.1145/116825.116845). A general synopsis of the
 * algorithm is as follows:
 *
 *      - Perform Critical Factorisation of the needle, in which it is separated
 *        into two distinct parts: (u,v) and (v,u). This needs to be performed
 *        twice, using forward-going and backward-running methods. These
 *        methods, in addition to the maximum suffix, should also return the
 *        period `p` for the needle.
 *
 *      - Determine the appropriate Critical Factorisation. Given two maximum
 *        suffixes with their corresponding periods, determine which tuple is
 *        appropriate to provide to the string-matching driver function.
 *
 *      - Perform the search using a low-level memory-comparison function,
 *        such as memcmp(3). */

/* Useful Sources:
 *
 * Crochemore, M., Perrin, D. 1991. Two-Way String-Matching, Journal of the
 *      Association for Computing Machinery 38(3):651--675
 * Lecroq, T. 1997. Two-Way Algorithm.
 *      http://www-igm.univ-mlv.fr/~lecroq/string/node26.html */

#include <stddef.h>
#include <string.h>

/* HAYSTACK_OVERSHOOT: the amount (in terms of cachelines) which the searching
 * driver should add to the length of the needle when determining the size of
 * the haystack. This is for improved performance. */

#define HAYSTACK_OVERSHOOT ( 256 )

/* max_suffix_l: calculate the maximal suffix of `needle`, which is of length
 * `needle_len`, and place the period in `*period`. The _l variant computes the
 * maximal suffix as `cmp_a < cmp_b`. This function returns the maximal suffix,
 * and sets `*period` to the last-known period. */

static size_t max_suffix_l ( const unsigned char * needle,
                size_t needle_len, size_t * period )
{
        size_t max_suffix = -1, i = 0, j = 1;
        char cmp_a = '\0', cmp_b = '\0';

        *period = 1;

        while ( i + j < needle_len ) {
                cmp_a = needle [ i + j ];
                cmp_b = needle [ max_suffix + j ];

                if ( cmp_a < cmp_b ) {
                        i += j;
                        j = 1;
                        *period = i - max_suffix;
                } else if ( cmp_a == cmp_b ) {
                        if ( j != *period )
                                j++;
                        else {
                                i += *period;
                                j = 1;
                        }
                } else {
                        /* Alternate case: `cmp_a > cmp_b` */
                        max_suffix = i;
                        i = max_suffix + 1;
                        j = 1;
                        *period = 1;
                }
        }

        return max_suffix;
}

/* max_suffix_m: this function behaves identically to `max_suffix_l`, however
 * the _m variant computes the maximal suffix as `cmp_a < cmp_b`. Both of these
 * maximal suffixes need to be computed by the driver, which then determines
 * which period corresponds to a (strictly) greater maximal suffix. */

static size_t max_suffix_m ( const unsigned char * needle,
                size_t needle_len, size_t * period )
{
        size_t max_suffix = -1, i = 0, j = 1;
        char cmp_a = '\0', cmp_b = '\0';

        *period = 1;

        while ( i + j < needle_len ) {
                cmp_a = needle [ i + j ];
                cmp_b = needle [ max_suffix + j ];

                if ( cmp_a > cmp_b ) {
                        i += j;
                        j = 1;
                        *period = i - max_suffix;
                } else if ( cmp_a == cmp_b ) {
                        if ( j != *period )
                                j++;
                        else {
                                i += *period;
                                j = 1;
                        }
                } else {
                        /* Alternate case: `cmp_a < cmp_b` */
                        max_suffix = i;
                        i = max_suffix + 1;
                        j = 1;
                        *period = 1;
                }
        }

        return max_suffix;
}

/* twoway_search: TODO */

static char * twoway_search ( const unsigned char * haystack,
                const unsigned char * needle )
{
        /* no-op */
        return NULL;
}

/* _strstr: local driver for the `strstr` reimplementation. This function
 * expects unsigned character arrays, and returns the location of `needle` in
 * `haystack`. If the input is trivially erroneous---e.g., if the length of the
 * haystack is shorter than that of the needle---NULL is returned. If a `needle`
 * is not provided, `haystack` is immediately returned. */

static char * _strstr ( const unsigned char * haystack,
                const unsigned char * needle )
{
        size_t haystack_len = 0, needle_len = 0;

        if ( needle [ 0 ] == '\0' )
                /* If the `needle` has not been provided, return the haystack.
                 * I'm not sure whether this is the best course-of-action for
                 * erroneous input, however returning NULL would force
                 * extraneous behaviour from the already-misbehaving caller. */
                return ( char * ) haystack;

        /* Inspired by the glibc `strcasestr` implementation: reading a few
         * cachelines ahead of `needle_len` when determining the size of
         * `haystack` will create improved performance for a match occurring
         * early in a voluminous haystack.
         *
         * Aside: performing these (parenthesised) assignments in the `return`
         * statement leads to undefined sequence points. Shame, as that could
         * have been one hell of a line. */

        needle_len = strlen ( ( char * ) needle );
        haystack_len = strnlen ( ( char * ) haystack,
                        needle_len + HAYSTACK_OVERSHOOT );

        /* A `needle` which is shorter than a `haystack` cannot match
         * anything in the haystack. In this rare occurrence, returning
         * NULL is appropriate (c.f.\ glibc implementation). */

        return ( needle_len > haystack_len ) ?
                NULL : twoway_search ( haystack, needle );
}

