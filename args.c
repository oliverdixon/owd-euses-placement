/* ash-euses: argument-processor
 * Ashley Dixon. */

/* TODO: this file is currently freestanding from the main driver program; it is
 * not yet integrated. None of the following options are implemented in the
 * primary code of ash-euses. */

#include <stdint.h>

#define SET_BIT(val, n) ( val |= n )
#define CHK_BIT(val, n) ( val &  n )

/* The following command-line options are currently recognised:
 *
 *  - OPT_PRINT_REPO_NAMES: print the repository in which the match was found,
 *    in the form "::<repo>", e.g. <match> ::gentoo;
 *  - OPT_PRINT_REPO_LOCS: [implies OPT_PRINT_REPO_NAMES] print the location of
 *    the repository in which the match was found, in the form "::<repo> =>
 *    <path>";
 *  - OPT_SHOW_HELP: show help information and exit;
 *  - OPT_SHOW_VERSION: show version information and exit. */

enum opt_positions {
        OPT_PRINT_REPO_NAMES = 1,
        OPT_PRINT_REPO_LOCS  = 2,
        OPT_SHOW_HELP        = 4,
        OPT_SHOW_VERSION     = 8
};

enum argument_status_t {
        ARGSTAT_OK     =  0, /* everything is OK */
        ARGSTAT_DOUBLE = -1, /* an argument would doubly defined */
        ARGSTAT_UNKNWN = -2  /* the provided argument was unknown */
};

uint8_t options = 0;

/* provide_arg_error: returns a human-readable string representing the provided
 * error code in `status`, as enumerated in `argument_status_t`. */

const char * provide_arg_error ( enum argument_status_t status )
{
        switch ( status ) {
                case ARGSTAT_OK:     return "Everything is OK.";
                case ARGSTAT_DOUBLE: return "Argument was doubly defined.";
                case ARGSTAT_UNKNWN: return "Argument was unrecognised.";

                default:             return "Unknown error";
        }
}

