
#ifndef PI_LIST_H
#define PI_LIST_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>


typedef struct Value Value;

#define MAX_SIZE 20000
#define LIST_SIZE(l) ((l)->size)

#define LIST_AT(l, i) (*(Value *)list_getAt((l), (i)))

// Define the PiList structure
typedef struct
{
    void *data;   // Pointer to the array of items
    int i_size;   // Size of each item
    int size;     // Current number of items
    int capacity; // Maximum number of items before resizing
} list_t;

// create a new PiList
list_t *list_create(int i_size);

// get an item from the PiList by index
static inline void *list_getAt(list_t *list, int index)
{
    if (index < 0)
        index += list->size;
    if (index < 0 || index >= list->size)
    {
        fprintf(stderr, "List index out of range.\n");
        exit(EXIT_FAILURE);
    }
    return (char *)list->data + index * list->i_size;
}

static inline int list_size(list_t *list)
{
    return list->size;
}

// set an item in the PiList by index
static inline void list_set(list_t *list, int index, void *item)
{
    if (index < 0)
        index += list->size;
    if (index < 0 || index >= list->size)
    {
        fprintf(stderr, "List index out of range.\n");
        exit(EXIT_FAILURE);
    }
    memcpy((char *)list->data + index * list->i_size, item, list->i_size);
}

// resize the PiList to a new capacity
void list_expand(list_t *list, int new_cap);

// add an item to the PiList
static inline void list_add(list_t *list, const void *item)
{
    if (list->size == list->capacity)
        list_expand(list, list->capacity < 1024
                              ? list->capacity * 2
                              : list->capacity + list->capacity / 4 + 256);

    memcpy((char *)list->data + list->size * list->i_size, item, list->i_size);
    list->size++;
}

void list_addAt(list_t *list, int index, const void *item);

list_t *list_copy(list_t *list);

list_t *list_addAll(list_t *list, list_t *items);

void *list_pop(list_t *list);
void list_addFirst(list_t *list, const void *item);

// remove an item from the PiList by index
void *list_remove(list_t *list, int index);

list_t *list_map(list_t *list, Value *(*func)(Value *));

bool list_isEmpty(list_t *list);

void list_clear(list_t *list);
void list_print(list_t *list);

// free the memory used by the PiList
void list_free(list_t *list);

#endif // PI_LIST_H
