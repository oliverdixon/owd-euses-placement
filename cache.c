/* ash-euses: repository-description caching
 * Ashley Dixon. */

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "euses.h"
#include "report.h"

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

enum cache_warning_t {
        CACHEWARN_ERRNO =  1,
        CACHEWARN_FLONG = -1  /* the cache file exceeds BUFFER_SZ - 1 */
};

static const char * provide_cache_warning ( int status )
{
        switch ( status ) {
                case CACHEWARN_ERRNO: return strerror ( errno );
                case CACHEWARN_FLONG: return "The file cache file is " \
                                          "exceedingly voluminous.";

                default: return "Unknown warning.";
        }
}

static int parse_cache ( )
{
        /* TODO implement */
        return 0;
}

/* get_cache_len: returns the length of the file, fp. The file should be rewound
 * with rewind(3) by the caller after this function, if further non-closing
 * operations are desired. */

static inline long get_cache_len ( FILE * fp )
{
        long size = -1;

        return ( fseek ( fp, 0, SEEK_END ) == -1
                        || ( size = ftell ( fp ) ) == -1 ) ? -1 : size;
}

int load_cache ( struct repo_stack_t * stack )
{
        FILE * fp = fopen ( CACHE_FILE, "r" );
        char buffer [ BUFFER_SZ ];

        if ( fp == NULL ) {
                populate_info_buffer ( CACHE_FILE );
                print_warning ( CACHEWARN_ERRNO, &provide_cache_warning );
                return -1;
        }

        if ( get_cache_len ( fp ) >= BUFFER_SZ ) {
                populate_info_buffer ( CACHE_FILE );
                print_warning ( CACHEWARN_FLONG, &provide_cache_warning );
                fclose ( fp );
                return -1;
        }

        if ( fread ( buffer, sizeof ( char ), BUFFER_SZ - 1, fp )
                        < BUFFER_SZ - 1 && ! feof ( fp ) ) {
                populate_info_buffer ( CACHE_FILE );
                print_warning ( CACHEWARN_ERRNO, &provide_cache_warning );
                fclose ( fp );
                return -1;
        }

        fclose ( fp );
        return 0;
}

