/* ash-euses: argument-processor
 * Ashley Dixon. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "args.h"

#define SET_BIT(val, n) ( val |= n )

/* Would this be better with a single call to printf, perhaps in an inline
 * function ? */
#define PREFIX_PRINT(prefix, status) \
        fputs ( prefix, stderr ); \
        fputs ( ": ", stderr ); \
        fputs ( provide_arg_error ( status ), stderr ); \
        putc ( '\n', stderr )

enum argument_status_t {
        ARGSTAT_OK     =  0, /* everything is OK */
        ARGSTAT_DOUBLE = -1, /* an argument would doubly defined */
        ARGSTAT_UNKNWN = -2  /* the provided argument was unknown */
};

uint8_t options = 0;

/* provide_arg_error: returns a human-readable string representing the provided
 * error code in `status`, as enumerated in `argument_status_t`. */

static const char * provide_arg_error ( enum argument_status_t status )
{
        switch ( status ) {
                case ARGSTAT_OK:     return "Everything is OK.";
                case ARGSTAT_DOUBLE: return "Argument was doubly defined.";
                case ARGSTAT_UNKNWN: return "Argument was unrecognised.";

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
                "--repo-names", "--repo-paths", "--help", "--version"
        }, * arg_abv [ ] = {
                "-n", "-p", "-h", "-v"
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
 * its own error functions directly to the appropriate output buffer, and
 * returns zero to indicate success, and -1 to indicate failure. The caller
 * should probably quit the program with an unsuccessful status code in the
 * event of the latter, as `options` may be incomplete and produce strange
 * results in later execution.
 *
 * This function only considers arguments which begin with '-'. If that is not
 * the case, the argv entry is skipped. */

int process_args ( int argc, char ** argv )
{
        enum arg_positions_t apos = ARG_UNKNOWN;

        for ( int i = 1; i < argc; i++ ) {
                apos = ARG_UNKNOWN;

                if ( argv [ i ] [ 0 ] != '-' )
                        continue; /* not an argument */

                if ( match_arg ( argv [ i ], &apos ) == 0 ) {
                        if ( CHK_BIT ( options, apos ) != 0 ) {
                                PREFIX_PRINT ( argv [ i ], ARGSTAT_DOUBLE );
                                return -1;
                        }

                        printf ( "\"%s\": %d\n", argv [ i ], apos );
                        SET_BIT ( options, apos );
                } else {
                        PREFIX_PRINT ( argv [ i ], ARGSTAT_UNKNWN );
                        return -1;
                }
        }

        return 0;
}

