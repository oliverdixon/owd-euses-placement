/* ash-euses: argument-processor
 * Ashley Dixon. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "error.h"
#include "args.h"

#define SET_ARG(val, n) ( val |= n )

enum argument_status_t {
        ARGSTAT_OK     =  0, /* everything is OK */
        ARGSTAT_DOUBLE = -1, /* an argument would doubly defined */
        ARGSTAT_UNKNWN = -2, /* the provided argument was unknown */
        ARGSTAT_LACK   = -3, /* not enough arguments were provided */
        ARGSTAT_EMPTY  = -4  /* the provided argument was meaningless/empty */
};

uint8_t options = 0;

/* provide_arg_error: returns a human-readable string representing the provided
 * error code in `status`, as enumerated in `argument_status_t`. */

static const char * provide_arg_error ( int status )
{
        switch ( status ) {
                case ARGSTAT_OK:     return "Everything is OK.";
                case ARGSTAT_DOUBLE: return "Argument was doubly defined.";
                case ARGSTAT_UNKNWN: return "Argument was unrecognised.";
                case ARGSTAT_LACK:   return "Not enough arguments were " \
                                        "provided.";
                case ARGSTAT_EMPTY:  return "Argument was empty.";

                default:             return "Unknown error";
        }
}

/* match_arg: overcomplicated(ly) elegant argument-matcher, supporting both long
 * and short argument forms, assuming that the arg_positions_t enum increments
 * in powers of two. This function returns zero on success, or -1 on failure
 * (unrecognised argument), populating the apos variable appropriately for the
 * caller. */

static int match_arg ( const char * arg, enum arg_positions_t * apos )
{
        static const char * arg_full [ ] = {
                "--repo-names", "--repo-paths", "--help", "--version",
                "--list-repos", "--strict", "--quiet", "--no-case"
        }, * arg_abv [ ] = {
                "-n", "-p", "-h", "-v", "-r", "-s", "-q", "-c"
        };

        /* `fargc`: full argument count */
        static const int fargc = sizeof ( arg_full ) / sizeof ( *arg_full );

        /* if the corresponding arg_abv value is NULL, there is no
         * shortened counterpart against which to check */
        for ( int i = 0; i < fargc; i++ )
                if ( strcmp ( arg, arg_full [ i ] ) == 0 ||
                                ( ( arg_abv [ i ] ) ?
                                  ( strcmp ( arg, arg_abv [ i ] ) == 0 )
                                  : 0 ) ) {
                        *apos = 1 << i;
                        break;
                }

        /* unrecognised argument ? */
        return ( *apos == ARG_UNKNOWN ) ? -1 : 0;
}

/* match_abbr_arg: given an abbreviated string beginning with '-', this function
 * sets the appropriate arguments for every character in the string. Should a
 * character be unrecognised or doubly defined, ARGSTAT_UNKNWN or ARGSTAT_DOUBLE
 * is returned respectively. On success, ARGSTAT_OK is returned. */

static enum argument_status_t match_abbr_arg ( const char * str )
{
        static const char * abbr_list = "nphvrsqc";
        const int abbr_sz = strlen ( abbr_list );
        size_t len = strlen ( str );
        int found = 0;

        for ( size_t i = 1; i < len; i++ ) {
                found = 0;

                for ( int j = 0; j < abbr_sz; j++ ) {
                        if ( str [ i ] == abbr_list [ j ] ) {
                                if ( CHK_ARG ( options, 1 << j ) != 0 )
                                        return ARGSTAT_DOUBLE;
                                SET_ARG ( options, 1 << j );
                                found = 1;

                                if ( str [ i + 1 ] == '\0' )
                                        return ARGSTAT_OK;

                                break;
                        }
                }

                if ( found == 0 )
                        return ARGSTAT_UNKNWN;
        }

        return ARGSTAT_OK;
}

/* process_args: process the argument list in `argv` and populate the `options`
 * global variable accordingly. This function, due to its notability, produces
 * its own error functions directly to the appropriate output buffer (via
 * print_error), and returns zero to indicate success, and -1 to indicate
 * failure. The caller should probably quit the program with an unsuccessful
 * status code in the event of the latter, as `options` may be incomplete and
 * produce strange results in later execution. On success, `advanced_idx` is set
 * to the index of the next, non-argument option in `argv`. The new `argc` could
 * be calculated by the caller with `argc - advanced_idx`.
 *
 * If "--", or an argument not beginning with "-" is found, success is returned
 * and further arguments are not considered. Thus, all options to be considered
 * by the argument-processor must precede other arguments. */

int process_args ( int argc, char ** argv, int * advanced_idx )
{
        const char * error_prefix = "Inadequate command-line arguments were " \
                                        "provided.";
        enum arg_positions_t apos = ARG_UNKNOWN;
        enum argument_status_t argstat = ARGSTAT_OK;
        int i = 1;

        if ( argc < 2 ) {
                print_error ( error_prefix, ARGSTAT_LACK, &provide_arg_error );
                return -1;
        }

        for ( ; i < argc; i++ ) {
                apos = ARG_UNKNOWN;

                if ( argv [ i ] [ 0 ] != '-' ||
                                strcmp ( argv [ i ], "--" ) == 0 )
                        break; /* do not consider further arguments */

                if ( argv [ i ] [ 1 ] == '\0' ) {
                        /* check for an empty argument ("-<null>") */
                        populate_error_buffer ( argv [ i ] );
                        print_error ( error_prefix, ARGSTAT_EMPTY,
                                        &provide_arg_error );
                        return -1;
                }

                if ( match_arg ( argv [ i ], &apos ) == 0 ) {
                        /* full or shortened individual arguments */
                        if ( CHK_ARG ( options, apos ) != 0 ) {
                                populate_error_buffer ( argv [ i ] );
                                print_error ( error_prefix, ARGSTAT_DOUBLE,
                                                &provide_arg_error );
                                return -1;
                        }

                        SET_ARG ( options, apos );
                } else {
                        if ( ( argstat = match_abbr_arg ( argv [ i ] ) )
                                        != ARGSTAT_OK ) {
                                /* combined arguments */
                                populate_error_buffer ( argv [ i ] );
                                print_error ( error_prefix, argstat,
                                                &provide_arg_error );
                                return -1;
                        }
                }
        }

        *advanced_idx = i;
        return 0;
}

