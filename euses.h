/* ash-euses: common data signatures
 * Ashley Dixon. */

#ifndef _EUSES_H
#define _EUSES_H

#include <linux/limits.h>

#define PROGRAM_NAME         "ash-euses"
#define PROGRAM_AUTHOR       "Ashley Dixon"
#define PROGRAM_AUTHOR_EMAIL "ash@suugaku.co.uk"
#define PROGRAM_YEAR         "MMXX"
#define PROGRAM_URL          "http://git.suugaku.co.uk/" PROGRAM_NAME "/"
#define PROGRAM_VERSION      "0.2"
#define PROGRAM_LICENCE_NAME "WTF Public Licence"
#define PROGRAM_LICENCE_URL  "http://www.wtfpl.net/about/"

struct repo_t {
        char location [ PATH_MAX ], name [ NAME_MAX + 1 ];
        struct repo_t * next;
};

#endif /* _EUSES_H */

