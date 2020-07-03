/* ash-euses: Two-Way string-searching algorithm implementation.
 * Ashley Dixon */

/* NOTE: I'm going to keep this file separate from the primary euses codebase
 * for a while, as it does not yet perform at an acceptable level. glibc
 * performs some very interesting optimisations for needles of a short length
 * (typical with USE-flags), so I will implement some of them in the relatively
 * immediate future. */

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
 *        appropriate to provide to the string-matching driver function. The
 *        other one must still be preserved for the reversal searching.
 *
 *      - Perform the search using a low-level memory-comparison function,
 *        such as memcmp(3).
 *
 * To perform crude testing of the searcher on the command-line, define _DEFMAIN
 * to link a simple main() routine. SYNTAX: `./prog haystack needle`. Results
 * are printed to stdout, and if no matches are found, a note is sent to stderr;
 * stdout remains clean. For machines which place a great tax on branching,
 * _SLOW_BRANCHING can be defined to avoid branching in the MAX macro; this is
 * seldom required or even optimal. */

/* Useful Sources:
 *
 * Crochemore, M., Perrin, D. 1991. Two-Way String-Matching, Journal of the
 *      Association for Computing Machinery 38(3):651--675
 * Lecroq, T. 1997. Two-Way Algorithm.
 *      http://www-igm.univ-mlv.fr/~lecroq/string/node26.html */

#ifdef _DEFMAIN
#include <stdio.h>
#endif

#include <sys/types.h>

/* sys/types.h provides `ssize_t`, which is required for almost all integer
 * variables in this searcher. Providing this implementation remains consistent
 * with the glibc convention of using `size_t` variants wherever possible,
 * `ssize_t` is required as the searcher compares the various values against
 * values which may be negative. */

#include <string.h> /* strlen(3) and memcmp(3) */

#ifdef _SLOW_BRANCHING
/* https://graphics.stanford.edu/~seander/bithacks.html#IntegerMinOrMax */
#define MAX(x,y) ( x ^ ( ( x ^ y ) & - ( x < y ) ) )
#else
#define MAX(x,y) ( x > y ) ? x : y
#endif

/* max_suffix_l: calculate the maximal suffix of `needle`, which is of length
 * `needle_len`, and place the period in `*period`. The _l variant computes the
 * maximal suffix as `cmp_a < cmp_b`. This function returns the maximal suffix,
 * and sets `*period` to the last-known period. */

static ssize_t max_suffix_l ( const unsigned char * needle,
                ssize_t needle_len, ssize_t * period )
{
        ssize_t max_suffix = -1, i = 0, j = 0;
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

static ssize_t max_suffix_m ( const unsigned char * needle,
                ssize_t needle_len, ssize_t * period )
{
        ssize_t max_suffix = -1, i = 0, j = 1;
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

/* calculate_suffixes: calculate the maximal suffixes for the primary and
 * secondary (forward and reverse) lexicographic search (as is usually required
 * by the Two-Way algorithm). The primary suffix is returned, and the secondary
 * is placed in `secondary`. The primary's corresponding period is also placed
 * into `period`. */

static ssize_t calculate_suffixes ( const unsigned char * needle,
                ssize_t needle_len, ssize_t * period, ssize_t * secondary )
{
        ssize_t i = 0, j = 0, * primary = NULL, p = 0, q = 0;

        i = max_suffix_l ( needle, needle_len, &p );
        j = max_suffix_m ( needle, needle_len, &q );

        if ( i > j ) {
                primary = &i;
                *secondary = j;
                *period = p;
        } else {
                primary = &j;
                *secondary = i;
                *period = q;
        }

        return *primary;
}

/* fwd_lexi_search: perform a forward lexicographic search, in which `prm`
 * (`ell`) and `sec` are the primary and secondary maximal suffixes
 * respectively, and `period` is the corresponding period of the primary maximal
 * suffix. If the `needle` is found using this particular function, the address
 * of the first character of the match inside `hs` is returned. NULL is returned
 * if the match remains missing. */

static unsigned char * fwd_lexi_search ( ssize_t prm, ssize_t sec,
                ssize_t ell, const unsigned char * ne, ssize_t ne_len,
                const unsigned char * hs, ssize_t hs_len,
                ssize_t period )
{
        /* Use a temporary space to store the address of the first suggestion of
         * a match. */
        ssize_t memory = -1; 
        sec = 0;

        while ( sec <= hs_len - ne_len ) {
                prm = MAX ( ell, memory ) + 1;

                while ( prm < ne_len && ne [ prm ] == hs [ prm + sec ] )
                        prm++;

                if ( prm >= ne_len ) {
                        prm = ell;
                        while ( prm > memory && ne [ prm ] == hs [ prm + sec ] )
                                prm--;

                        if ( prm <= memory )
                                /* Found a match. */
                                return ( unsigned char * ) & ( hs [ sec ] );

                        sec += period;
                        memory = ne_len - period - 1;
                } else {
                        sec += ( prm - ell );
                        memory = -1;
                }
        }

        return NULL;
}

/* rev_lexi_search: similar to `fwd_lexi_search`, however this function performs
 * a reverse lexicographic search. For further documentation, c.f.\ the
 * aforementioned variant, as the external A.P.I.\ is almost identical. */

static unsigned char * rev_lexi_search ( ssize_t prm, ssize_t sec, ssize_t ell,
                const unsigned char * ne, ssize_t ne_len,
                const unsigned char * hs, ssize_t hs_len )
{
        /* As we only calculated and retained the period of the primary maximal
         * suffix, it can be trivially calculated here. */
        ssize_t period = MAX ( ( ell + 1 ), ( ne_len - ell - 1 ) ) + 1;
        sec = 0;

        while ( sec <= hs_len - ne_len ) {
                prm = ell + 1;

                while ( prm < ne_len && ne [ prm ] == hs [ prm + sec ] )
                        prm++;

                if ( prm >= ne_len ) {
                        prm = ell;
                        while ( prm >= 0 && ne [ prm ] == hs [ prm + sec ] )
                                prm--;
                        if ( prm < 0 )
                                /* Found a match. */
                                return ( unsigned char * ) & ( hs [ sec ] );

                        sec += period;
                } else
                        sec += ( prm - ell );
        }

        return NULL;
}

/* twoway_search: perform either a forward- or backward-running search on
 * `haystack` for `needle`, with the caller providing their respective lengths.
 * If the match is found, the address of the first according character in
 * `haystack` is returned. Otherwise, NULL is returned. */

static unsigned char * twoway_search ( const unsigned char * haystack,
                ssize_t haystack_len, const unsigned char * needle,
                ssize_t needle_len )
{
        ssize_t primary, secondary, ell, period;
        unsigned char * match = NULL;

        primary = calculate_suffixes ( needle, needle_len, &period,
                        &secondary );
        ell = primary;

        if ( memcmp ( needle, needle + period, ell + 1 ) == 0 ) {
                /* Attempt a forward lexicographic search if the start of the
                 * `needle` coincides with `ell + 1` bytes of the `period`
                 * address. */
                if ( ( match = fwd_lexi_search ( primary, secondary, ell,
                                                needle, needle_len, haystack,
                                                haystack_len, period ) )
                                != NULL )
                        return match;
        } else
                /* Otherwise, use a reverse lexicographic search. */
                return rev_lexi_search ( primary, secondary, ell, needle,
                                needle_len, haystack, haystack_len );

        /* The entire string has been traversed (the reverse lexicographic
         * search was deemed unsuitable/suboptimal); the Knuth-Morris-Pratt
         * variant found no match. */
        return NULL;
}

/* _strstr: local driver for the `strstr` reimplementation. This function
 * expects unsigned character arrays, and returns the location of `needle` in
 * `haystack`. If the input is trivially erroneous---e.g., if the length of the
 * haystack is shorter than that of the needle---NULL is returned. If a `needle`
 * is not provided, `haystack` is immediately returned. */

unsigned char * _strstr ( const unsigned char * haystack,
                const unsigned char * needle )
{
        ssize_t haystack_len = 0, needle_len = 0;

        if ( needle [ 0 ] == '\0' )
                /* If the `needle` has not been provided, return the haystack.
                 * I'm not sure whether this is the best course-of-action for
                 * erroneous input, however returning NULL would force
                 * extraneous behaviour from the already-misbehaving caller. */
                return ( unsigned char * ) haystack;

         /* Aside: performing these (parenthesised) assignments in the `return`
          * statement leads to undefined sequence points. It's a tremendous
          * shame, as that could have been one hell of a line. */

        needle_len = strlen ( ( char * ) needle );
        haystack_len = strlen ( ( char * ) haystack );

        /* A `needle` which is shorter than a `haystack` cannot match
         * anything in the haystack. In this rare occurrence, returning
         * NULL is appropriate (c.f.\ glibc implementation). */

        return ( needle_len > haystack_len ) ? NULL :
                twoway_search ( haystack, haystack_len, needle, needle_len );
}

#ifdef _DEFMAIN

/* main: crude testing routine for the searcher. This will probably be removed
 * (or moved elsewhere) in the near future. SYNTAX: `./prog haystack needle`. */

int main ( int argc, char ** argv )
{
        char * pos = NULL;

        if ( argc < 3 ) {
                fputs ( "Syntax: haystack needle\n", stderr );
                return -1;
        }

        if ( ( pos = ( char * ) _strstr ( ( unsigned char * ) argv [ 1 ],
                                        ( unsigned char * ) argv [ 2 ] ) ) )
                puts ( pos );
        else
                fputs ( "No match found.\n", stderr );

        return 0;
}

#endif /* _DEFMAIN */

