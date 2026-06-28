#ifndef PI_STACK_H
#define PI_STACK_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef unsigned char byte;

typedef struct
{
    void *data;   // flat element buffer
    int i_size;   // size of each element in bytes
    int top;      // index of top element; -1 when empty
    int capacity; // allocated slot count
} pistack_t;

static inline int stack_isEmpty(const pistack_t *stack)
{
    return stack->top == -1;
}

static inline int stack_isFull(const pistack_t *stack)
{
    return stack->top == stack->capacity - 1;
}

static inline int stack_size(const pistack_t *stack)
{
    return stack->top + 1;
}

static inline void *stack_peek(const pistack_t *stack)
{
    if (stack->top < 0)
        return NULL;
    return (byte *)stack->data + stack->top * stack->i_size;
}

void stack_expand(pistack_t *stack);

static inline void stack_push(pistack_t *stack, const void *item)
{
    if (stack_isFull(stack))
        stack_expand(stack);
    stack->top++;
    memcpy((byte *)stack->data + stack->top * stack->i_size, item, stack->i_size);
}

static inline void *stack_pop(pistack_t *stack)
{
    if (stack->top < 0)
        return NULL;
    void *slot = (byte *)stack->data + stack->top * stack->i_size;
    stack->top--;
    return slot;
}

// Convenience macros for int-typed stacks.
// Safe now that stack_pop returns an interior pointer (no leak).
#define PUSH_INT(stack, value)      \
    do                              \
    {                               \
        int _tmp = (value);         \
        stack_push((stack), &_tmp); \
    } while (0)

#define POP_INT(stack) (*(int *)stack_pop(stack))

pistack_t *stack_create(int i_size);
pistack_t *stack_createCap(int i_size, int capacity);

void stack_push(pistack_t *stack, const void *item);
void *stack_pop(pistack_t *stack);

void *stack_peek(const pistack_t *stack);

void *stack_getAt(const pistack_t *stack, int index);

void stack_free(pistack_t *stack);
void stack_print(pistack_t *stack, void (*print_item)(void *));

#endif /* PI_STACK_H */
