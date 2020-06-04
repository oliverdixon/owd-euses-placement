/* ash-euses: data and function signatures
 * Ashley Dixon. */

#ifndef _EUSES_H
#define _EUSES_H

#include <stdio.h>
#include <linux/limits.h>

struct repo_stack_t {
        struct repo_t * lead;
        unsigned long size;
};

struct repo_t {
        char location [ PATH_MAX ], name [ NAME_MAX + 1 ];
        FILE * ptr;
        struct repo_t * next;
};

struct repo_t * stack_peek ( struct repo_stack_t * );
struct repo_t * stack_pop ( struct repo_stack_t * );
void stack_push ( struct repo_stack_t *, struct repo_t * );
void stack_init ( struct repo_stack_t * );
void stack_cleanse ( struct repo_stack_t * );

#endif /* _EUSES_H */
