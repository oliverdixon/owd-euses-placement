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
        ARGSTAT_LACK   = -3  /* not enough arguments were provided */
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
                "--list-repos", "--strict", "--quiet"
        }, * arg_abv [ ] = {
                "-n", "-p", "-h", "-v", "-r", "-s", "-q"
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
                                        "provided";
        enum arg_positions_t apos = ARG_UNKNOWN;
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

                if ( match_arg ( argv [ i ], &apos ) == 0 ) {
                        if ( CHK_ARG ( options, apos ) != 0 ) {
                                populate_error_buffer ( argv [ i ] );
                                print_error ( error_prefix, ARGSTAT_DOUBLE,
                                                &provide_arg_error );
                                return -1;
                        }

                        SET_ARG ( options, apos );
                } else {
                        populate_error_buffer ( argv [ i ] );
                        print_error ( error_prefix, ARGSTAT_UNKNWN,
                                        &provide_arg_error );
                        return -1;
                }
        }

        *advanced_idx = i;
        return 0;
}

