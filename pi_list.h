#ifndef PI_LIST_H
#define PI_LIST_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

typedef struct Value Value;

#define LIST_SIZE(l) ((l)->size)
#define LIST_AT(l, i) (*(Value *)list_getAt((l), (i)))

typedef struct
{
    void *data;   // flat contiguous element buffer
    int i_size;   // size of each element in bytes
    int size;     // number of live elements
    int capacity; // allocated slots (always a power of two)
} list_t;

void _list_expand(list_t *list, int new_cap);

static inline void _list_grow(list_t *list)
{
    int new_cap = list->capacity < 1024
                      ? list->capacity * 2
                      : list->capacity + list->capacity / 4 + 256;
    _list_expand(list, new_cap);
}

static inline void *list_getAt(list_t *list, int index)
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

static inline int list_size(list_t *list) { return list->size; }

static inline void list_set(list_t *list, int index, const void *item)
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

static inline void list_add(list_t *list, const void *item)
{
    if (list->size == list->capacity)
        _list_grow(list);
    memcpy((char *)list->data + list->size * list->i_size, item, list->i_size);
    list->size++;
}

static inline void *list_peek(list_t *list)
{
    return (char *)list->data + (list->size - 1) * list->i_size;
}

list_t *list_create(int i_size);
list_t *list_create_cap(int i_size, int capacity); /* pre-sized allocation   */

void list_addAt(list_t *list, int index, const void *item);
void list_addFirst(list_t *list, const void *item);
list_t *list_addAll(list_t *list, const list_t *items);
list_t *list_copy(const list_t *list);

void *list_pop(list_t *list);

void *list_remove(list_t *list, int index);

list_t *list_map(list_t *list, Value *(*func)(Value *));

bool list_isEmpty(list_t *list);

void list_clear(list_t *list);

void list_print(list_t *list);
void list_free(list_t *list);

#endif /* PI_LIST_H */