/**
 * @file
 * @brief The stack ADT implementation
 * @author Oliver Dixon
 * @date 20/09/2021
 */

#include <stdlib.h>
#include <stdio.h>

#include "stack.h"

struct stack {
    void ** data;    /**< The stack storage array */
    size_t capacity; /**< The maximum number of items storable by the stack */
    size_t size;     /**< The current number of items stored on the stack */
};

struct stack * stack_init ( size_t capacity )
{
    const size_t DEFAULT_CAPACITY = 8;
    struct stack * self = NULL;

    if ( !capacity )
        capacity = DEFAULT_CAPACITY;

    if ( ( self = malloc ( sizeof ( struct stack ) ) ) )
        if ( ( self->data = malloc ( sizeof ( void * ) * capacity ) ) ) {
            self->size = 0;
            self->capacity = capacity;
        } else {
            free ( self );
            self = NULL;
        }

    return self;
}

void stack_free ( struct stack * self )
{
    free ( self->data );
    free ( self );
}

void * stack_push ( struct stack * self, void * node )
{
    void ** new_data;

    if ( self->capacity <= self->size )
        /* If there is insufficient capacity on the stack, then we attempt to
         * double the data allowance and continue with the push. */
        if ( ( new_data = realloc ( self->data,
                sizeof ( self->capacity << 1 ) ) ) == NULL ) {
            self->data = new_data;
            self->capacity <<= 1;
        } else
            /* The stack capacity was insufficient and could not be resized. */
            return NULL;

    self->data [ self->size++ ] = node;
    return node;
}

void * stack_peek ( struct stack * self )
{
    return self->data [ self->size - 1 ];
}

void * stack_pop ( struct stack * self )
{
    return ( self->size ) ? self->data [ self->size-- ] : NULL;
}

void stack_print ( struct stack * self )
{
    const size_t size = self->size;

    for ( size_t i = 0; i < size; i++ )
        printf ( "#%zu: %p\n", i, self->data [ i ] );
}

