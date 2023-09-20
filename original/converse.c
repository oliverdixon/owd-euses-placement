/* owd-euses: direct reporting functions; see converse.h.
 * Oliver Dixon. */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "euses.h"
#include "converse.h"

/* Globally accessible message buffer for better error-reporting; it should only
 * be written to using the populate_info_buffer function, as it provides
 * overflow-protection and pretty truncation. */
char info_buffer [ ERROR_MAX ];

/* [exposed function] print_fatal: format a `status` code, prefixed with
 * `prefix`, and send it to stderr. This function relies on the global error
 * buffer, `info_buffer`, and uses the function pointer `get_detail` to
 * retrieve the error detail; this should take a single integer and return a
 * `const char *`. This function is intended to be used for fatal errors, in
 * which the caller immediately requests the program be exited with a non-zero
 * status code.
 *
 * It is assumed that a `status` of one indicates a system error. */

void print_fatal ( const char * prefix, int status,
        const char * ( * get_detail ) ( int ) )
{
    fprintf ( stderr, PROGRAM_NAME " caught a fatal error and cannot " \
            "continue.\nRe-run with \"--help --version\" or "          \
            "\"-hv\" for help.\n\nSummary: \"%s\"\nOffending Article"  \
            ": \"%s\"\nError Detail: \"%s\"\nStatus Code: %d\n",

            prefix, ( info_buffer [ 0 ] != '\0' ) ? info_buffer : "N/A",
            get_detail ( status ), ( status == 1 ) ? errno : status );
}

/* [exposed function] print_warning: format the contents of the info buffer on
 * one line. This provides a succinct message designed for warnings which do not
 * generally change the flow of execution.  This function uses the `get_detail`
 * function to attain a summary regarding the `status`, which should be
 * compatible with the enumerable type expected by the `get_detail` function
 * dereference.
 *
 * The same assumptions made by `print_fatal` are made in this function's
 * implementation; it is rather safe, nonetheless. */

void print_warning ( int status, const char * ( * get_detail ) ( int ) )
{
    fputs ( PROGRAM_NAME ": warning", stderr );

    if ( info_buffer [ 0 ] != '\0' )
        fprintf ( stderr, " (\"%s\")", info_buffer );

    fprintf ( stderr, ": %d(%c): %s\n",
            ( status == 1 ) ? errno : status, ( status == 1 ) ? 'S'
            : 'I', get_detail ( status ) );
}

/* [exposed function] populate_info_buffer: copy the `message` into the global
 * `error_buffer`, truncating with " [...]" if necessary. This function assumes
 * that error_buffer is of the size ERROR_MAX. If the passed `message` is NULL,
 * then the buffer is cleared. */

void populate_info_buffer ( const char * message )
{
    size_t msg_len = 0;

    info_buffer [ 0 ] = '\0';
    if ( message == NULL )
        return;

#if defined(__GNUC__) && !defined(__clang__) && !defined(__INTEL_COMPILER)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
    /* If strncpy truncates the NULL-terminator from the source, it is
     * re-added in the following truncation `if` block. */
    strncpy ( info_buffer, message, ERROR_MAX - 1 );
#pragma GCC diagnostic pop
#else
    strncpy ( info_buffer, message, ERROR_MAX - 1 );
#endif

    if ( ( msg_len = strlen ( message ) ) >= ERROR_MAX - 1 ) {
        /* indicate that the message in the error buffer has been
         * truncated */
        info_buffer [ ERROR_MAX - 7 ] = ' ';
        info_buffer [ ERROR_MAX - 6 ] = '[';
        info_buffer [ ERROR_MAX - 5 ] = '.';
        info_buffer [ ERROR_MAX - 4 ] = '.';
        info_buffer [ ERROR_MAX - 3 ] = '.';
        info_buffer [ ERROR_MAX - 2 ] = ']';
        info_buffer [ ERROR_MAX - 1 ] = '\0';
    }
}

/* [exposed function] print_version_info: uses the various PROGRAM_* definitions
 * to print program information to stdout. */

void print_version_info ( )
{
    puts ( "This is " PROGRAM_NAME ", v. " PROGRAM_VERSION " by "  \
            PROGRAM_AUTHOR " (" PROGRAM_YEAR ").\nFor support, "   \
            "send e-mail to " PROGRAM_AUTHOR " <"                  \
            PROGRAM_AUTHOR_EMAIL ">.\n\nThe source code "          \
            "repository and tarballs are available on-line "       \
            "at\n" PROGRAM_URL ". The code is licensed under"      \
            "\nthe " PROGRAM_LICENCE_NAME ", the details of "      \
            "which can be found at\n" PROGRAM_LICENCE_URL ".\n" );
}

/* [exposed function] print_help_info: pretty-print information regarding the
 * possible command- line arguments, with their abbreviated form and a brief
 * description.
 *
 * Use this format-specifier for adding argument documentation:
 * "--%-13s -%-3c\t%s\n" with "<long name>", "<shortname>", "<description>". */

void print_help_info ( const char * invocation )
{
    printf ( PROGRAM_NAME " command-line argument summary.\n" \
            "Syntax: %s [options] substrings\n" \
            "\n--%-13s -%-3c\t%s\n--%-13s -%-3c\t%s\n" \
            "--%-13s -%-3c\t%s\n--%-13s -%-3c\t%s\n" \
            "--%-13s -%-3c\t%s\n--%-13s -%-3c\t%s\n" \
            "--%-13s -%-3c\t%s\n--%-13s -%-3c\t%s\n" \
            "--%-13s -%-3c\t%s\n--%-13s -%-3c\t%s\n" \
            "--%-13s -%-3c\t%s\n--%-13s -%-3c\t%s\n" \
            "--%-13s -%-3c\t%s\n--%-13s -%-3c\t%s\n" \
            "--%-13s -%-3c\t%s\n", invocation,

            "list-repos", 'r', "Prepend a list of located " \
                "repositories (repos.conf/ only).",
            "repo-names", 'n', "Print repository names for " \
                "each match.",
            "repo-paths", 'p', "Print repository details for " \
                "each match (implies repo-names).",
            "help", 'h', "Print this help information and exit.",
            "version", 'v', "Prepend version and license " \
                "information to the output.",
            "strict", 's', "Search only in the flag field, " \
                "excluding the description.",
            "portdir", 'd', "Attempt to use the PORTDIR value.",
            "quiet", 'q', "Do not complain about PORTDIR.",
            "no-case", 'c', "Perform a case-insensitive search " \
                "across the files.",
            "print-needles", 'e', "Prepend each match with the " \
                "relevant needle substring.",
            "no-interrupt", 'i', "Do not interrupt the " \
                "search results with warnings.",
            "package", 'k', "Restrict the search to category-" \
                "package description files.",
            "colour", 'o', "Print the package, flag, and" \
                " description in distinct colours.",
            "global", 'g', "Exclude all sources describing "
                "package-local flags.",
            "", '\b', "Consider all further arguments as " \
                "substrings/queries." );
}

/* [exposed function] list_repos: pretty-print a list of repositories, in which
 * `stack` is the full stack of repository details, and `base` is the
 * repo-configuration base directory (e.g., "/etc/portage/repos.conf"). */

void list_repos ( struct repo_stack_t * stack, char * base )
{
    struct repo_t * repo = stack_peek ( stack );

    fputs ( "Configuration directory: ", stdout );
    puts ( base );
    putchar ( '\n' );

    if ( repo == NULL )
        return; /* A lack of repositories has already been caught. */

    do
        printf ( "Name: %-10s\tLocation: %-16s\n", repo->name,
                repo->location );
    while ( ( repo = repo->next ) != NULL );

    putchar ( '\n' );
}

