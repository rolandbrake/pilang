#ifndef PI_LIST_H
#define PI_LIST_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

typedef struct Value Value;

#define PI_MAX_LIST_SIZE 10000000

#define LIST_SIZE(l) ((l)->size)
#define LIST_AT(l, i) (*(Value *)_list_getAt((l), (i)))

typedef struct
{
    void *data;   // flat contiguous element buffer
    int i_size;   // size of each element in bytes
    int size;     // number of live elements
    int capacity; // allocated slots (always a power of two)
} List;

void _list_expand(List *list, int new_cap);

static inline void _list_grow(List *list)
{
    int new_cap = list->capacity < 1024
                      ? list->capacity * 2
                      : list->capacity + list->capacity / 4 + 256;
    _list_expand(list, new_cap);
}

static inline void *_list_getAt(List *list, int index)
{
    if (index < 0)
        index += list->size;
    if ((unsigned)index >= (unsigned)list->size)
    {
        fprintf(stderr, "[pi_list] index %d out of range (size=%d)\n",
                index, list->size);
        exit(EXIT_FAILURE);
    }
    return (char *)list->data + index * list->i_size;
}

static inline int _list_getSize(List *list) { return list->size; }

static inline void _list_set(List *list, int index, const void *item)
{
    if (index < 0)
        index += list->size;
    if ((unsigned)index >= (unsigned)list->size)
    {
        fprintf(stderr, "[pi_list] set index %d out of range (size=%d)\n",
                index, list->size);
        exit(EXIT_FAILURE);
    }
    memcpy((char *)list->data + index * list->i_size, item, list->i_size);
}

static inline void _list_add(List *list, const void *item)
{
    if (list->size == list->capacity)
        _list_grow(list);
    memcpy((char *)list->data + list->size * list->i_size, item, list->i_size);
    list->size++;
}

static inline void *_list_peek(List *list)
{
    return (char *)list->data + (list->size - 1) * list->i_size;
}

List *list_create(int i_size);
List *list_createCap(int i_size, int capacity); /* pre-sized allocation   */

void list_addAt(List *list, int index, const void *item);
void list_addFirst(List *list, const void *item);
List *list_addAll(List *list, const List *items);
List *list_copy(const List *list);

void *list_pop(List *list);

void *list_remove(List *list, int index);

List *list_map(List *list, Value *(*func)(Value *));

bool list_isEmpty(List *list);

void list_clear(List *list);

void list_print(List *list);
void list_free(List *list);

#endif /* PI_LIST_H */
