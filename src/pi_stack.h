#ifndef PI_STACK_H
#define PI_STACK_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef unsigned char Byte;

typedef struct
{
    void *data;   // flat element buffer
    int i_size;   // size of each element in bytes
    int top;      // index of top element; -1 when empty
    int capacity; // allocated slot count
} PiStack;

static inline int _stack_isEmpty(const PiStack *stack)
{
    return stack->top == -1;
}

static inline int _stack_isFull(const PiStack *stack)
{
    return stack->top == stack->capacity - 1;
}

static inline int _stack_size(const PiStack *stack)
{
    return stack->top + 1;
}

static inline void *_stack_peek(const PiStack *stack)
{
    if (stack->top < 0)
        return NULL;
    return (Byte *)stack->data + stack->top * stack->i_size;
}

void stack_expand(PiStack *stack);

static inline void _stack_push(PiStack *stack, const void *item)
{
    if (_stack_isFull(stack))
        stack_expand(stack);
    stack->top++;
    memcpy((Byte *)stack->data + stack->top * stack->i_size, item, stack->i_size);
}

static inline void *_stack_pop(PiStack *stack)
{
    if (stack->top < 0)
        return NULL;
    void *slot = (Byte *)stack->data + stack->top * stack->i_size;
    stack->top--;
    return slot;
}

// Convenience macros for int-typed stacks.
// Safe now that stack_pop returns an interior pointer (no leak).
#define PUSH_INT(stack, value)      \
    do                              \
    {                               \
        int _tmp = (value);         \
        _stack_push((stack), &_tmp); \
    } while (0)

#define POP_INT(stack) (*(int *)_stack_pop(stack))

PiStack *stack_create(int i_size);
PiStack *stack_createCap(int i_size, int capacity);

void _stack_push(PiStack *stack, const void *item);
void *_stack_pop(PiStack *stack);

void *_stack_peek(const PiStack *stack);

void *stack_getAt(const PiStack *stack, int index);

void stack_free(PiStack *stack);
void stack_print(PiStack *stack, void (*print_item)(void *));

#endif /* PI_STACK_H */
