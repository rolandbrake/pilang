#include "pi_stack.h"
#include "common.h"

stack_t *stack_create(int i_size)
{
    return stack_createCap(i_size, INIT_CAP);
}

// allocate with a known initial capacity.
// Use when the maximum depth is predictable (e.g. the VM call stack).
// Avoids all grow cycles when the depth stays within the initial cap.
stack_t *stack_createCap(int i_size, int capacity)
{
    if (capacity < INIT_CAP)
        capacity = INIT_CAP;

    stack_t *stack = malloc(sizeof(stack_t));
    if (!stack)
        error("[stack_create] Out of memory allocating stack header.");

    stack->data = malloc((size_t)i_size * capacity);
    if (!stack->data)
    {
        free(stack);
        error("[stack_create] Out of memory allocating stack buffer.");
    }

    stack->i_size = i_size;
    stack->top = -1;
    stack->capacity = capacity;
    return stack;
}

void stack_expand(stack_t *stack)
{
    int new_cap = stack->capacity < 1024
                      ? stack->capacity * 2
                      : stack->capacity + stack->capacity / 4 + 256;

    void *new_data = realloc(stack->data, (size_t)new_cap * stack->i_size);
    if (!new_data)
        error("[stack_expand] Out of memory resizing stack.");

    /* Update data BEFORE capacity so the struct is never in a corrupt state */
    stack->data = new_data;
    stack->capacity = new_cap;
}

void *stack_getAt(const stack_t *stack, int index)
{
    if (index < 0 || index > stack->top)
        return NULL;
    return (byte *)stack->data + index * stack->i_size;
}

void stack_free(stack_t *stack)
{
    if (!stack)
        return;
    free(stack->data);
    free(stack);
}

void stack_print(stack_t *stack, void (*print_item)(void *))
{
    if (!stack || !print_item)
        return;
    for (int i = 0; i <= stack->top; i++)
        print_item(stack_getAt(stack, i));
}