/**
 * @file
 * @brief The stack ADT public interface, supporting typical FIFO operations.
 * @author Oliver Dixon
 * @date 20/09/2021
 */

#ifndef STACK_H
#define STACK_H

#include <stddef.h>

/**
 * @struct stack
 * @brief The base stack data-type
 */
struct stack;

/**
 * Initialises an empty stack with the given initial capacity.
 * @param capacity the initial capacity
 * @return the newly created stack, or NULL on error
 */
struct stack * stack_init ( size_t capacity );

/**
 * Destructs the stack, freeing all memory allocated by the initialisation and
 * subsequent resizes.
 * @param self the stack to destruct
 */
void stack_free ( struct stack * self );

/**
 * Pushes a reference to the node at the given address to the stack.
 * @param self the stack to which the node must be pushed
 * @param node the node to push
 * @return the newly pushed node address, or NULL if the stack capacity is
 *  insufficient and can not be resized.
 */
void * stack_push ( struct stack * self, void * node );

/**
 * Peeks at the node at the top of the stack.
 * @param self the stack to peek
 * @return the node atop the stack, or NULL if the stack is empty
 */
void * stack_peek ( struct stack * self );

/**
 * Pops the node at the top of the stack.
 * @param self the stack to pop
 * @return the popped node
 */
void * stack_pop ( struct stack * self );

/**
 * Prints the stack contents to stdout.
 * @param self the stack to print
 */
void stack_print ( struct stack * self );

#endif /* STACK_H */

