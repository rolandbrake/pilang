#include "pi_list.h"
#include "common.h"
#include "pi_value.h"
#include "pi_object.h"

static list_t *_list_alloc(int i_size, int capacity)
{
    list_t *list = malloc(sizeof(list_t));
    if (!list)
        return NULL;

    list->data = malloc((size_t)i_size * capacity);
    if (!list->data)
    {
        free(list);
        return NULL;
    }

    list->i_size = i_size;
    list->size = 0;
    list->capacity = capacity;
    return list;
}

list_t *list_create(int i_size)
{
    list_t *list = _list_alloc(i_size, INIT_CAP);
    if (!list)
        error("[list_create] Out of memory.");
    return list;
}

/* Pre-sized variant - use when the final size is known (avoids grow cycles). */
list_t *list_createCap(int i_size, int capacity)
{
    if (capacity < INIT_CAP)
        capacity = INIT_CAP;
    list_t *list = _list_alloc(i_size, capacity);
    if (!list)
        error("[list_createCap] Out of memory.");
    return list;
}

void _list_expand(list_t *list, int new_cap)
{
    void *data = realloc(list->data, (size_t)new_cap * list->i_size);
    if (!data)
        error("[_list_expand] Out of memory.");
    list->data = data;
    list->capacity = new_cap;
}

list_t *list_copy(const list_t *list)
{
    /* Allocate exactly as many slots as there are live elements.
       Use INIT_CAP as minimum so small copies can still grow cheaply. */
    int cap = list->size > INIT_CAP ? list->size : INIT_CAP;
    list_t *copy = list_createCap(list->i_size, cap);

    if (list->size > 0)
        memcpy(copy->data, list->data, (size_t)list->size * list->i_size);

    copy->size = list->size;
    return copy;
}

list_t *list_addAll(list_t *list, const list_t *items)
{
    if (!items || items->size == 0)
        return list;

    int new_size = list->size + items->size;
    if (new_size > list->capacity)
    {
        int cap = new_size > list->capacity * 2 ? new_size : list->capacity * 2;
        _list_expand(list, cap);
    }

    memcpy((char *)list->data + list->size * list->i_size,
           items->data,
           (size_t)items->size * items->i_size);

    list->size = new_size;
    return list;
}

void list_addAt(list_t *list, int index, const void *item)
{
    if (list->size == list->capacity)
        _list_grow(list);

    int _index = get_index(index, list->size);

    char *target = (char *)list->data + _index * list->i_size;
    /* memmove handles the overlap between source and destination */
    memmove(target + list->i_size, target, (size_t)(list->size - _index) * list->i_size);
    memcpy(target, item, list->i_size);
    list->size++;
}

void list_addFirst(list_t *list, const void *item)
{
    if (list->size == list->capacity)
        _list_grow(list);

    /* Shift everything right by one slot */
    memmove((char *)list->data + list->i_size,
            list->data,
            (size_t)list->size * list->i_size);

    memcpy(list->data, item, list->i_size);
    list->size++;
}

void *list_pop(list_t *list)
{
    if (list->size == 0)
        error("[list_pop] List is empty.");

    list->size--;
    /* The slot at size is no longer "live" but the bytes are intact */
    return (char *)list->data + list->size * list->i_size;
}

void *list_remove(list_t *list, int index)
{
    index = get_index(index, list->size);

    char *target = (char *)list->data + index * list->i_size;
    char *next = target + list->i_size;

    memmove(target, next, (size_t)(list->size - index - 1) * list->i_size);
    list->size--;

    /* Return pointer to what is now the first element of the shifted tail.
       Valid until next mutation. Caller should treat as read-only. */
    return target;
}

list_t *list_map(list_t *list, Value *(*func)(Value *))
{
    if (!list || !func)
        error("[list_map] Invalid arguments.");

    list_t *result = list_createCap(list->i_size, list->size > INIT_CAP ? list->size : INIT_CAP);

    for (int i = 0; i < list->size; i++)
    {
        Value *item = (Value *)list_getAt(list, i);
        Value *mapped = func(item);
        list_add(result, mapped);
    }

    return result;
}

bool list_isEmpty(list_t *list)
{
    return list->size == 0;
}

void list_clear(list_t *list)
{
    if (!list)
        return;
    list->size = 0;
}

void list_print(list_t *list)
{
    for (int i = 0; i < list->size; i++)
    {
        Value item = *(Value *)list_getAt(list, i);
        printf("[%d] %s\n", i, as_string(item));
    }
}

void list_free(list_t *list)
{
    if (!list)
        return;
    free(list->data);
    free(list);
}