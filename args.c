/* ash-euses: argument-processor
 * Ashley Dixon. */

/* TODO: this file is currently freestanding from the main driver program; it is
 * not yet integrated. None of the following options are implemented in the
 * primary code of ash-euses. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SET_BIT(val, n) ( val |= n )
#define CHK_BIT(val, n) ( val &  n )

/* The following command-line options are currently recognised:
 *
 *  - ARG_PRINT_REPO_NAMES: print the repository in which the match was found,
 *    in the form "::<repo>", e.g. <match> ::gentoo;
 *  - ARG_PRINT_REPO_PATHS: [implies ARG_PRINT_REPO_NAMES] print the location of
 *    the repository in which the match was found, in the form "::<repo> =>
 *    <path>";
 *  - ARG_SHOW_HELP: show help information and exit;
 *  - ARG_SHOW_VERSION: show version information and exit. */

enum arg_positions_t {
        ARG_UNKNOWN          = 0,
        ARG_PRINT_REPO_NAMES = 1,
        ARG_PRINT_REPO_PATHS = 2,
        ARG_SHOW_HELP        = 4,
        ARG_SHOW_VERSION     = 8
};

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

/* process_args: process the argument list in `argv` and populate the `options`
 * global variable accordingly. This function, due to its notability, produces
 * its own error functions directly to the appropriate output buffer, and
 * returns zero to indicate success, and -1 to indicate failure. The caller
 * should probably quit the program with an unsuccessful status code in the
 * event of the latter, as `options` may be incomplete and produce strange
 * results in later execution. */

int process_args ( int argc, char ** argv )
{
        static const char * arg_str_full [ ] = {
                "--repo-names", "--repo-paths", "--help", "--version"
        }, * arg_str_short [ ] = {
                "-n", "-p", "-h", "-v"
        };

        static const int full_arg_count = sizeof ( arg_str_full ) /
                sizeof ( *arg_str_full );
        enum argument_status_t status = ARGSTAT_OK;
        enum arg_positions_t apos = ARG_UNKNOWN;

        for ( int i = 1; i < argc; i++ ) {
                apos = ARG_UNKNOWN;

                /* if the corresponding arg_str_short value is NULL, there is no
                 * shortened counterpart against which to check */
                for ( int j = 0; j < full_arg_count; j++ )
                        if ( strcmp ( argv [ i ], arg_str_full [ j ] ) == 0 ||
                                        ( ( arg_str_short [ j ] ) ?
                                          ( strcmp ( argv [ i ],
                                                     arg_str_short [ j ] )
                                                        == 0 ) : 0 ) ) {
                                apos = 1 << j;
                                printf ( "\"%s\": %d\n", argv [ i ], apos );
                                break;
                        }

                if ( apos == ARG_UNKNOWN )
                        return -1; /* unrecognised argument */
        }

        return 0;
}

/* CURRENTLY FREESTANDING FROM ASH-EUSES: TEST DRIVER MAIN */
int main ( int argc, char ** argv )
{
        if ( argc < 2 )
                puts ( "nothing to do" );
        else
                printf ( "process_args returned %d\n",
                                process_args ( argc, argv ) );

        return 0;
}

