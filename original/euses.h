/* owd-euses: common data signatures
 * Oliver Dixon. */

#ifndef EUSES_H
#define EUSES_H

#include <limits.h>

#define PROGRAM_NAME     "owd-euses-placemewnt"
#define PROGRAM_AUTHOR       "Oliver Dixon"
#define PROGRAM_AUTHOR_EMAIL "od641@york.ac.uk"
#define PROGRAM_YEAR     "MMXX & MMXXIII"
#define PROGRAM_URL      "https://github.com/oliverdixon/" PROGRAM_NAME
#define PROGRAM_VERSION      "placement"
#define PROGRAM_LICENCE_NAME "MIT Licence"
#define PROGRAM_LICENCE_URL  "https://mit-license.org/"

struct repo_t {
    char location [ PATH_MAX ], name [ NAME_MAX + 1 ];
    struct repo_t * next;
};

int construct_path ( char *, const char *, const char * );

#endif /* EUSES_H */

