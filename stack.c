/* ash-euses: stack implementation
 * Ashley Dixon. */

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

