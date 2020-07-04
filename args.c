/* ash-euses: argument-processor
 * Ashley Dixon. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "converse.h"
#include "args.h"

#define SET_ARG(val, n) ( val |= n )
#define ERROR_PREFIX "Inadequate command-line arguments were provided."

enum argument_status_t {
        ARGSTAT_OK     =  0, /* everything is OK */
        ARGSTAT_DOUBLE = -1, /* an argument would doubly defined */
        ARGSTAT_UNKNWN = -2, /* the provided argument was unknown */
        ARGSTAT_LACK   = -3, /* not enough arguments were provided */
        ARGSTAT_EMPTY  = -4, /* the provided argument was meaningless/empty */
        ARGSTAT_UNABBR = -5, /* the command-abbreviation list was erroneous */
        ARGSTAT_NOMORE = -6, /* further arguments should not be considered */
        ARGSTAT_NOMREE = -7  /* ARGSTAT_NOMORE, but it was explicitly defined */
};

opts_t options = 0;

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
                case ARGSTAT_UNABBR: return "One of the abbreviated arguments" \
                                        " was unrecognised.";

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
                "repo-names", "repo-paths", "help", "version", "list-repos",
                "strict", "quiet", "no-case", "portdir", "print-needles",
                "no-interrupt", "package", "colour"
        }, arg_abv [ ] = {
                /* For a long-form argument which does not have an abbreviated
                 * form, the corresponding entry in arg_abv should be a NULL-
                 * terminator. */
                'n', 'p', 'h', 'v', 'r', 's', 'q', 'c', 'd', 'e', 'i', 'k', 'o'
        };

        /* `fargc`: full argument count. This should be more than or equal to
         * the count of non-NULL abbreviated arguments. */
        static const int fargc = sizeof ( arg_full ) / sizeof ( *arg_full );

        /* if the corresponding arg_abv value is NULL, there is no
         * shortened counterpart against which to check */
        for ( int i = 0; i < fargc && arg [ 0 ] == '-'; i++ )
                if ( ( arg [ 1 ] == '-' && strcmp ( & ( arg [ 2 ] ),
                                                arg_full [ i ] ) == 0 )
                                || ( arg_abv [ i ] != '\0' &&
                                        arg [ 1 ] == arg_abv [ i ] &&
                                        arg [ 2 ] == '\0' ) ) {
                        *apos = 1 << i;
                        break;
                }

        /* unrecognised argument ? */
        return ( *apos == ARG_UNKNOWN ) ? -1 : 0;
}

/* match_abbr_arg: given an abbreviated string beginning with '-', this function
 * sets the appropriate arguments for every character in the string. Should a
 * character be unrecognised or doubly defined, ARGSTAT_UNABBR or ARGSTAT_DOUBLE
 * is returned respectively. On success, ARGSTAT_OK is returned. */

static enum argument_status_t match_abbr_arg ( const char * str )
{
        static const char * abbr_list = "nphvrsqcdeiko";
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
                        return ARGSTAT_UNABBR;
        }

        return ARGSTAT_OK;
}

/* argument_subprocessor: checks a single string as an argument; if this is an
 * abbreviated string, the `options` flag-list may be changed multiple times. If
 * the given string is empty or meaningless, ARGSTAT_EMPTY is returned, and
 * ARGSTAT_DOUBLE is returned if an argument has been doubly defined. On
 * success, ARGSTAT_OK is returned, and in the event of an abbreviated multi-
 * argument string, the return value is dictated by `match_abbr_arg`. */

static enum argument_status_t argument_subprocessor ( char * arg )
{
        enum argument_status_t argstat = ARGSTAT_OK;
        enum arg_positions_t apos = ARG_UNKNOWN;

        if ( arg [ 0 ] != '-' )
                return ARGSTAT_NOMORE;

        /* This statement is safe, as `arg` is null-terminated. */
        if ( arg [ 0 ] == '-' && arg [ 1 ] == '-' && arg [ 2 ] == '\0' )
                return ARGSTAT_NOMREE;

        if ( arg [ 1 ] == '\0' ) {
                /* check for an empty argument ("-<null>") */
                populate_info_buffer ( arg );
                return ARGSTAT_EMPTY;
        }

        if ( match_arg ( arg, &apos ) == 0 ) {
                if ( CHK_ARG ( options, apos ) != 0 ) {
                        /* full or shortened individual arguments */
                        populate_info_buffer ( arg );
                        return ARGSTAT_DOUBLE;
                }

                SET_ARG ( options, apos );
        } else {
                if ( ( argstat = match_abbr_arg ( arg ) ) != ARGSTAT_OK ) {
                        /* combined arguments */
                        populate_info_buffer ( arg );
                        return argstat;
                }
        }

        return ARGSTAT_OK;
}

/* [exposed function] process_args: process the argument list in `argv` and
 * populate the `options` global variable accordingly. This function, due to its
 * notability, produces its own error functions directly to the appropriate
 * output buffer (via print_fatal), and returns zero to indicate success, and -1
 * to indicate failure. The caller should probably quit the program with an
 * unsuccessful status code in the event of the latter, as `options` may be
 * incomplete and produce strange results in later execution. On success,
 * `advanced_idx` is set to the index of the next, non-argument option in
 * `argv`. The new `argc` could be calculated by the caller with `argc -
 * advanced_idx`.
 *
 * If "--", or an argument not beginning with "-" is found, success is returned
 * and further arguments are not considered. Thus, all options to be considered
 * by the argument-processor must precede other arguments. */

int process_args ( int argc, char ** argv, int * advanced_idx )
{
        enum argument_status_t argstat = ARGSTAT_OK;
        int i = 1;

        if ( argc < 2 ) {
                print_fatal ( ERROR_PREFIX, ARGSTAT_LACK, &provide_arg_error );
                return -1;
        }

        for ( ; i < argc; i++ ) {
                if ( ( argstat = argument_subprocessor ( argv [ i ] ) )
                                != ARGSTAT_OK ) {
                        if ( argstat == ARGSTAT_NOMORE )
                                /* do not consider further arguments */
                                break;

                        if ( argstat == ARGSTAT_NOMREE ) {
                                /* skip past the explicit argument-terminator */
                                i++;
                                break;
                        }

                        print_fatal ( ERROR_PREFIX, argstat,
                                        &provide_arg_error );
                        return -1;
                }
        }

        *advanced_idx = i;
        return 0;
}

