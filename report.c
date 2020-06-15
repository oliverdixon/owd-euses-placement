/* ash-euses: direct reporting functions, c.f.\ report.h
 * Ashley Dixon. */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "euses.h"
#include "report.h"

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
 * status code. */

void print_fatal ( const char * prefix, int status,
                const char * ( * get_detail ) ( int ) )
{
        fprintf ( stderr, PROGRAM_NAME " caught a fatal error and cannot " \
                        "continue.\nRe-run with \"--help\" or \"-h\" for help" \
                        ".\n\n\tSummary: \"%s\"\n\tOffending Article:" \
                        " \"%s\"\n\tError Detail: \"%s\"\n\tStatus Code: %d%s" \
                        "\n\tProgram Exit Code: %d\n",

                        prefix, ( info_buffer [ 0 ] != '\0' ) ? info_buffer :
                        "N/A", get_detail ( status ), ( status == STATUS_ERRNO )
                        ? errno : status, ( status == STATUS_ERRNO ) ?
                        " (system errno)" : " (internal)", EXIT_FAILURE );
}

/* [exposed function] print_warning: format the provided summary with the
 * contents of the info buffer on one line. This provides a succinct message
 * designed for warnings which do not generally change the flow of execution. */

void print_warning ( const char * summary )
{
        fprintf ( stderr, PROGRAM_NAME ": warning (\"%s\"): %s\n",
                        ( info_buffer [ 0 ] != '\0' ) ? info_buffer : "N/A",
                        summary );
}

/* [exposed function] populate_info_buffer: copy the `message` into the global
 * `error_buffer`, truncating with " [...]" if necessary. This function assumes
 * that error_buffer is of the size ERROR_MAX. */

void populate_info_buffer ( const char * message )
{
        size_t msg_len = 0;

        info_buffer [ 0 ] = '\0'; /* previous call did not result in a fatal */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
        /* If strncpy truncates the NULL-terminator from the source, it is
         * re-added in the following truncation `if` block. */
        strncpy ( info_buffer, message, ERROR_MAX - 1 );
#pragma GCC diagnostic pop

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

