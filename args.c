/* ash-euses: argument-processor [freestanding from ash-euses]
 * Ashley Dixon. */

/* TODO: this file is currently freestanding from the main driver program; it is
 * not yet integrated. None of the following options are implemented in the
 * primary code of ash-euses. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SET_BIT(val, n) ( val |= n )
#define CHK_BIT(val, n) ( val &  n )

/* Would this be better with a single call to printf, perhaps in an inline
 * function ? */
#define PREFIX_PRINT(prefix, status) \
        fputs ( prefix, stderr ); \
        fputs ( ": ", stderr ); \
        fputs ( provide_arg_error ( status ), stderr ); \
        putc ( '\n', stderr )

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

/* match_arg: overcomplicated[ly] elegant argument-matcher, supporting both long
 * and short argument forms, assuming that the arg_positions_t enum increments
 * in powers of two. This function returns zero on success, or -1 on failure
 * (unrecognised argument), populating the apos variable appropriately for the
 * caller. */

int match_arg ( const char * arg, enum arg_positions_t * apos )
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
        for ( int j = 0; j < fargc; j++ )
                if ( strcmp ( arg, arg_full [ j ] ) == 0 ||
                                ( ( arg_abv [ j ] ) ?
                                  ( strcmp ( arg, arg_abv [ j ] ) == 0 )
                                  : 0 ) ) {
                        *apos = 1 << j;
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
 * results in later execution. */

int process_args ( int argc, char ** argv )
{
        /* enum argument_status_t status = ARGSTAT_OK; */
        enum arg_positions_t apos = ARG_UNKNOWN;

        for ( int i = 1; i < argc; i++ ) {
                apos = ARG_UNKNOWN;

                if ( match_arg ( argv [ i ], &apos ) == 0 )
                        printf ( "\"%s\": %d\n", argv [ i ], apos );
                else {
                        PREFIX_PRINT ( argv [ i ], ARGSTAT_UNKNWN );
                        return -1;
                }
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

