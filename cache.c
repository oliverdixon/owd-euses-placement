/* ash-euses: repository-description caching
 * Ashley Dixon. */

#include <stdio.h>

#include "euses.h"

/* TODO: allowing changing CACHE_FILE at the command-line */
#define CACHE_FILE "~/.cache/" PROGRAM_NAME

/* TODO: implement:
 *      - load_cache: attempt to load the cache into the repository stack;
 *      - write_cache: if loaded with an invalidated cache, write the newfound
 *        repository-description information to the cache.
 *      - validate_cache: if the cache is more than a few days old, invalidate
 *        it and write out a new cache.
 *      - Various options in the argument-processor, allowing fine control of
 *        the cache. */

int load_cache ( )
{
        FILE * cache_fp = fopen ( CACHE_FILE, "r" );
        char buffer [ BUFFER_SZ ];

        fclose ( cache_fp );
        return 0;
}

