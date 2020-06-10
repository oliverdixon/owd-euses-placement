/* ash-euses: argument-processor header
 * Ashley Dixon. */

#ifndef _ARGS_H
#define _ARGS_H

#include <stdint.h>

#define CHK_BIT(val, n) ( val &  n )

/* The following command-line options are currently recognised:
 *
 *  - ARG_PRINT_REPO_NAMES: print the repository in which the match was found,
 *    in the form "::<repo>", e.g. <match> ::gentoo;
 *  - ARG_PRINT_REPO_PATHS: [implies ARG_PRINT_REPO_NAMES] print the location of
 *    the repository in which the match was found, in the form "::<repo> =>
 *    <path>";
 *  - ARG_SHOW_HELP: show help information and exit;
 *  - ARG_SHOW_VERSION: show version information and exit;
 *  - ARG_LIST_REPOS: list repositories to be searched. */

enum arg_positions_t {
        ARG_UNKNOWN          =  0,
        ARG_PRINT_REPO_NAMES =  1,
        ARG_PRINT_REPO_PATHS =  2,
        ARG_SHOW_HELP        =  4,
        ARG_SHOW_VERSION     =  8,
        ARG_LIST_REPOS       = 16
};

extern uint8_t options;
int process_args ( int, char **, int * );

#endif /* _ARGS_H */

