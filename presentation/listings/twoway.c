/**
 * Implements a high-level driver for the Two-Way searching algorithm.
 * [... Documentation snipped for presentation]
 *
 * @param haystack the search space
 * @param haystack_len the length of the search space
 * @param needle the match string
 * @param needle_len the length of the match string
 * @return the matched text
 */

static char * twoway_search ( const char * haystack, size_t haystack_len,
        const char * needle, size_t needle_len )
{
    /* Use a reverse lexicographic search by default. */
    char * ( *searcher ) ( size_t, size_t, size_t, const char *, size_t,
        const char *, size_t, size_t ) = &rev_lexi_search;
    size_t primary, secondary, ell, period;
    char * match = NULL;

    primary = compute_suffixes ( needle, needle_len, &period, &secondary );
    ell = primary;

    if ( memcmp ( needle, needle + period, ell + 1 ) == 0 )
        /* Attempt a forward-running search where suffixes are optimal. */
        searcher = &fwd_lexi_search;

    return searcher ( primary, secondary, ell, needle, needle_len, haystack,
        haystack_len, period );
}

