/* ash-euses: stack implementation
 * Ashley Dixon. */

#include <stdlib.h>
#include <stdio.h>

#include "euses.h"

/* stack_peek: peeks at the leading node of the stack. */

struct repo_t * stack_peek ( struct repo_stack_t * stack )
{
        return stack->lead;
}

/* stack_pop: removes the leading node from the stack and returns it. NULL is
 * returned in the event of an underflow. */

struct repo_t * stack_pop ( struct repo_stack_t * stack )
{
        struct repo_t * node = NULL;

        if ( stack->size == 0 )
                return NULL; /* underflow */

        node = stack->lead;
        stack->lead = node->next;
        stack->size--;

        return node;
}

/* stack_push: pushes the node to the stack. */

void stack_push ( struct repo_stack_t * stack, struct repo_t * node )
{
        node->next = stack->lead;
        stack->lead = node;
        stack->size++;
}

/* stack_init: initialises a stack. */

void stack_init ( struct repo_stack_t * stack )
{
        stack->lead = NULL;
        stack->size = 0;
}

/* stack_cleanse: recursively free all nodes on a stack, making the safe
 * presumption that they reside on the heap. */

void stack_cleanse ( struct repo_stack_t * stack )
{
        unsigned int size = stack->size;

        for ( unsigned int i = 0; i < size; i++ )
                free ( stack_pop ( stack ) );
}

/* stack_print: recursively print all the names and locations of the
 * repositories on the given stack. If an underflow occurs, a message is
 * printed to stdout. */

void stack_print ( struct repo_stack_t * stack )
{
        struct repo_t * repo = stack_peek ( stack );

        if ( repo == NULL ) {
                puts ( "The stack is empty." );
                return;
        }

        do {
                printf ( "Name: %-10s\tLocation: %-16s\n", repo->name,
                                repo->location );
        } while ( ( repo = repo->next ) != NULL );
}

