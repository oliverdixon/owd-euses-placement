/* ash-euses: stack function and data signatures */

#ifndef _STACK_H
#define _STACK_H

#include "euses.h"

struct repo_stack_t {
        struct repo_t * lead;
        unsigned long size;
};

struct repo_t * stack_peek ( struct repo_stack_t * );
struct repo_t * stack_pop ( struct repo_stack_t * );
void stack_push ( struct repo_stack_t *, struct repo_t * );
void stack_init ( struct repo_stack_t * );
void stack_cleanse ( struct repo_stack_t * );

#endif /* _STACK_H */

