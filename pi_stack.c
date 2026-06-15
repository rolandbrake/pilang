#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pi_stack.h"
#include "common.h"

stack_t *stack_create(int i_size)
{
    stack_t *stack = (stack_t *)malloc(sizeof(stack_t));
    stack->i_size = i_size;
    stack->top = -1;
    stack->capacity = INIT_CAP;
    stack->data = malloc(i_size * stack->capacity);

    return stack;
}

void stack_expand(stack_t *stack, int new_cap)
{
    stack->capacity = new_cap;

    stack->data = realloc(stack->data, stack->i_size * stack->capacity);
    if (stack->data == NULL)
    {
        fprintf(stderr, "Failed to expand stack\n");
        exit(EXIT_FAILURE);
    }
}

void push(stack_t *stack, void *item)
{
    if (is_full(stack))
        stack_expand(stack, stack->capacity * 2);

    stack->top++;
    memcpy((byte *)stack->data + (stack->top * stack->i_size), item, stack->i_size);
}

void *pop(stack_t *stack)
{
    if (is_empty(stack))
    {
        printf("Stack underflow\n");
        return NULL;
    }

    // pop() returns a heap copy because the internal slot becomes invalid after top moves.
    void *item = malloc(stack->i_size);
    memcpy(item, (byte *)stack->data + (stack->top * stack->i_size), stack->i_size);
    stack->top--;
    return item;
}

void *top(stack_t *stack)
{
    if (is_empty(stack))
        return NULL;

    return (byte *)stack->data + (stack->top * stack->i_size);
}

int is_full(stack_t *stack)
{
    return stack->top == stack->capacity - 1;
}

int is_empty(stack_t *stack)
{
    return stack->top == -1;
}

void *stack_getAt(stack_t *stack, int index)
{
    if (index < 0 || index > stack->top)
        return NULL;

    return (byte *)stack->data + (index * stack->i_size);
}

int stack_size(stack_t *stack)
{
    return stack->top + 1;
}

void stack_free(stack_t *stack)
{
    free(stack->data);
    free(stack);
}

void stack_print(stack_t *stack, void (*print_item)(void *))
{
    for (int i = 0; i <= stack->top; i++)
        print_item(stack_getAt(stack, i));
}
