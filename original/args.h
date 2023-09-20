/* owd-euses: argument-processor header
 * Oliver Dixon. */

#ifndef ARGS_H
#define ARGS_H

#include <stdint.h>

#define CHK_ARG(val, n) ( val & n )

/* The following command-line options are currently recognised:
 *
 *  - ARG_PRINT_REPO_NAMES: print the repository in which the match was found,
 *    in the form "<repo name>::<match>";
 *  - ARG_PRINT_REPO_PATHS: [implies ARG_PRINT_REPO_NAMES] print the location of
 *    the repository in which the match was found, in the form "<file>::<repo
 *    name>::<match>";
 *  - ARG_SHOW_HELP: show help information and exit;
 *  - ARG_SHOW_VERSION: show version information and exit;
 *  - ARG_LIST_REPOS: list repositories to be searched;
 *  - ARG_SEARCH_STRICT: only search the flag field (identified by suffixing the
 *    query with " - "), and not the entire buffer;
 *  - ARG_NO_COMPLAINING: suppress the PORTDIR warning message;
 *  - ARG_SEARCH_NO_CASE: perform case-insensitive searching;
 *  - ARG_ATTEMPT_PORTDIR: before attempting to use repos.conf/, try extracting
 *    PORTDIR from the environment variable string or make.conf;
 *  - ARG_PRINT_NEEDLE: prepend the relevant search needle to every match;
 *  - ARG_NO_MIDBUF_WARN: suppress mid-buffer warnings; do not interrupt the
 *    search results with non-fatal errors/warnings;
 *  - ARG_PKG_FILES_ONLY: only search files whose names contain ".local". In
 *    practice, this restricts the search to files only containing category-
 *    package pairs, and exclude global USE-flag-description files;
 *  - ARG_NO_COLOUR: disabled coloured output;
 *  - ARG_GLOBAL_ONLY: [conflicts with ARG_PKG_FILES_ONLY] do not search files
 *    containing package-local flags. */

enum arg_positions_t {
    ARG_UNKNOWN          =    0,
    ARG_PRINT_REPO_NAMES =    1,
    ARG_PRINT_REPO_PATHS =    2,
    ARG_SHOW_HELP        =    4,
    ARG_SHOW_VERSION     =    8,
    ARG_LIST_REPOS       =   16,
    ARG_SEARCH_STRICT    =   32,
    ARG_NO_COMPLAINING   =   64,
    ARG_SEARCH_NO_CASE   =  128,
    ARG_ATTEMPT_PORTDIR  =  256,
    ARG_PRINT_NEEDLE     =  512,
    ARG_NO_MIDBUF_WARN   = 1024,
    ARG_PKG_FILES_ONLY   = 2048,
    ARG_NO_COLOUR        = 4096,
    ARG_GLOBAL_ONLY      = 8192
};

/* uintN_t, such that log2(<highest `arg_position_t`>) <= N */
typedef uint16_t opts_t;

extern opts_t  options;
int process_args ( int, char **, int * );

#endif /* ARGS_H */

