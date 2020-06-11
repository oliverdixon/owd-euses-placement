/* ash-euses: data and function signatures
 * Ashley Dixon. */

#ifndef _EUSES_H
#define _EUSES_H

#include <linux/limits.h>

#define PROGRAM_NAME         "ash-euses"
#define PROGRAM_AUTHOR       "Ashley Dixon"
#define PROGRAM_AUTHOR_EMAIL "ash@suugaku.co.uk"
#define PROGRAM_YEAR         "MMXX"
#define PROGRAM_VERSION      "alpha"
#define PROGRAM_LICENCE_NAME "WTF Public Licence"
#define PROGRAM_LICENCE_URL  "http://www.wtfpl.net/about/"

struct repo_stack_t {
        struct repo_t * lead;
        unsigned long size;
};

struct repo_t {
        char location [ PATH_MAX ], name [ NAME_MAX + 1 ];
        struct repo_t * next;
};

struct repo_t * stack_peek ( struct repo_stack_t * );
struct repo_t * stack_pop ( struct repo_stack_t * );
void stack_push ( struct repo_stack_t *, struct repo_t * );
void stack_init ( struct repo_stack_t * );
void stack_cleanse ( struct repo_stack_t * );
void stack_print ( struct repo_stack_t * );

/* see comments in euses.c 
 * TODO: integrate the argument-processor with the error_buffer system. */
/* extern char error_buffer [ PATH_MAX ]; */

#endif /* _EUSES_H */

