/**
 * @file
 * @brief The public argument-processing and -management API
 * @author Oliver Dixon
 * @date 25/09/2021
 */

#ifndef ARGS_H
#define ARGS_H

#include <stdint.h>
#include <stdbool.h>

/** The data-type to hold the argument-toggle bit string. This can be checked to
 * be sufficient with an assertion of the following form, where `ARG_TAIL` is
 * the maximum number of arguments to be considered in any single program
 * invocation: `assert ( sizeof ( args_t ) << 3 >= ARG_TAIL - 1 )`. */
typedef uint16_t args_t;

/** The argument index, storing each distinct argument that is recognised. */
enum arg_idx {
    /** Special argument flag: the unknown argument, used by the processors. */
    ARG_UNKNOWN,

    ARG_PRINT_REPO_NAMES,
        /**< Print the repository responsible for each match, in the form
         * "[repo name]::[match text]". */
    ARG_PRINT_REPO_PATHS,
        /**< Print the repository name, and its corresponding path, that is
         * responsible for each match in the form
         * "[repo path]::[repo name]::[match text]". */
    ARG_SHOW_HELP,
        /**< Display help information and exit. */
    ARG_SHOW_VERSION,
        /**< Display versioning information and exit. */
    ARG_LIST_REPOS,
        /**< List all searchable repositories and continue. */
    ARG_SEARCH_STRICT,
        /**< Search only the flag fields, identified by suffixing a hyphen, and
         * do not consider the entire buffer as a searchable space. */
    ARG_NO_COMPLAINING,
        /**< Do not print warnings regarding the presence of the legacy PORTDIR
         * directory hierarchy. */
    ARG_SEARCH_NO_CASE,
        /**< Perform case-insensitive searching. */
    ARG_ATTEMPT_PORTDIR,
        /**< Attempt to extract a PORTDIR value from a number of common
         * locations, likely the environment variables or legacy Portage
         * configuration files, before falling back to the newer repos.conf
         * directory hierarchy. */
    ARG_PRINT_NEEDLE,
        /**< Print the search needle responsible for each match. */
    ARG_NO_MIDBUF_WARN,
        /**< Suppress mid-buffer warnings, and do not clutter the output with
         * non-fatal error/warning `stderr` messages. See the QUIRKS section of
         * the manual for details regarding potential trans-buffer issues. */
    ARG_PKG_FILES_ONLY,
        /**< Only search files whose names contain ".local". In practice, this
         * restricts the search to files containing category-package pairs, and
         * excludes global USE-flag-description documents. */
    ARG_NO_COLOUR,
        /**< Do not colour the match output with ANSI escape sequences. */
    ARG_GLOBAL_ONLY,
        /**< Do not search files containing package-local flags. */

    /** Special argument flag: the tail-end of the argument vector. */
    ARG_TAIL
};

/** A status vector, the enumerators of which can be used to indicate the
 * transient state of the argument-processor. The processor implementation
 * should restrict its public reporting interface to the confines of these
 * codes, and should provide a human-readable message for each reportable
 * condition. */
enum arg_stat {
    ARG_STAT_OK,      /**< Stable state */
    ARG_STAT_DOUBLE,  /**< An argument has been doubly defined */
    ARG_STAT_UNKNOWN, /**< An unknown argument string was encountered */
    ARG_STAT_LACK,    /**< Insufficient arguments were provided */
    ARG_STAT_EMPTY,   /**< A given argument is meaningless or empty */
    ARG_STAT_UNABBR,  /**< The command-abbreviation list was erroneous */
    ARG_STAT_NOMORE,  /**< Further arguments should not be parsed as such */
    ARG_STAT_GLBPKG,  /**< The specified search space is contradictory */
};

/**
 * Checks whether an argument is enabled in the given bit string.
 * @param args the argument vector bit string
 * @param arg the argument to test
 * @return is the given argument set in the given bit string?
 */
inline bool arg_check ( args_t args, enum arg_idx arg );

/**
 * Provides a human-readable message for the given status code.
 * @param status the processor condition to decipher
 * @return the human-readable message
 */
const char * arg_strerror ( enum arg_stat status );

/**
 * Invokes the argument-processor on the given argument vector, populating the
 * bit string with flags corresponding to the fixed argument position index.
 * @param [in] argc the number of arguments provided in the argument vector
 * @param [in] argv the text argument vector, typically from the shell
 * @param [out] bitstring the argument-processor switch output
 * @return the status of the argument-processor
 */
enum arg_stat arg_parse ( int argc, char ** argv, args_t * bitstring );

#endif /* ARGS_H */

