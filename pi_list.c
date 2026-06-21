#include "pi_list.h"
#include "common.h"
#include "pi_value.h"
#include "pi_object.h"

static list_t *_list_create(int item_size, int capacity)
{
    list_t *list = (list_t *)malloc(sizeof(list_t));
    if (!list)
    {
        perror("Failed to allocate memory for list");
        exit(EXIT_FAILURE);
    }

    list->data = malloc(item_size * capacity);
    if (!list->data)
    {
        free(list);
        perror("Failed to allocate memory for list data");
        exit(EXIT_FAILURE);
    }

    list->i_size = item_size;
    list->size = 0;
    list->capacity = capacity;

    return list;
}

list_t *list_create(int i_size)
{
    return _list_create(i_size, INIT_CAP);
}


void list_addAt(list_t *list, int index, const void *item)
{
    int _index = get_index(index, list->size);

    void *target = (byte *)list->data + _index * list->i_size;

    memmove(target + list->i_size, target, (list->size - _index) * list->i_size);

    memcpy(target, item, list->i_size);

    list->size++;
}

// Performs a shallow copy; referenced objects are shared.
list_t *list_copy(list_t *list)
{
    list_t *copy = _list_create(list->i_size, list->capacity);

    for (size_t i = 0; i < list->size; i++)
    {
        void *item = list_getAt(list, (int)i);
        list_add(copy, item);
    }

    return copy;
}

list_t *list_addAll(list_t *list, list_t *items)
{

    // Calculate the required capacity after adding the new elements
    int size = list->size + items->size;

    if (size > list->capacity)
    {
        // Expand the list to accommodate the new elements
        int capacity = (size > list->capacity * 2) ? size : list->capacity * 2;
        list_expand(list, capacity);
    }

    // Copy the elements from 'items' to 'list'
    void *dest = (char *)list->data + list->size * list->i_size;
    memcpy(dest, items->data, items->size * items->i_size);

    list->size = size;

    return list;
}

// Removes an element and returns a heap-allocated copy slot for the removed value.
void *list_remove(list_t *list, int index)
{
    index = get_index(index, list->size);

    void *target = (byte *)list->data + index * list->i_size;

    // Allocate temporary buffer to hold the removed item
    void *removed_item = malloc(list->i_size);
    if (!removed_item)
        error("[list_remove] Memory allocation failed.");
    memcpy(removed_item, target, list->i_size);

    void *next = (byte *)list->data + (index + 1) * list->i_size;
    memmove(target, next, (list->size - index - 1) * list->i_size);

    list->size--;

    return removed_item; // Caller is responsible for freeing this
}

void *list_pop(list_t *list)
{
    if (list->size == 0)
        error("[pop] PiList is empty.");

    list->size--;
    void *item = (byte *)list->data + list->size * list->i_size;
    return item;
}

void list_addFirst(list_t *list, const void *item)
{
    if (list->size == list->capacity)
        // If the list is at capacity, expand it to double its size or increase by 25%
        list_expand(list, list->capacity < 1024 ? list->capacity * 2 : list->capacity + list->capacity / 4 + 256);

    // Move all existing items one slot forward
    void *dest = (byte *)list->data + list->i_size;
    void *src = list->data;
    memmove(dest, src, list->size * list->i_size);

    // Insert the new item at the beginning
    memcpy(list->data, item, list->i_size);
    list->size++;
}

void list_expand(list_t *list, int new_cap)
{
    void *_value = realloc(list->data, new_cap * list->i_size);
    if (_value == NULL)
    {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    list->data = _value;
    list->capacity = new_cap;
}
list_t *list_map(list_t *list, Value *(*func)(Value *))
{
    if (!list || !func)
        error("Invalid arguments to list_map.");

    list_t *_list = list_create(list->i_size);
    for (int i = 0; i < list->size; i++)
    {
        // Get the current item, apply the _transformation, and add to the new list
        Value *item = (Value *)list_getAt(list, i);
        Value *mapped = func(item);
        list_add(_list, mapped);
    }
    return _list;
}

bool list_isEmpty(list_t *list)
{
    return list->size == 0;
}

// Clears logical contents without releasing allocated capacity.
void list_clear(list_t *list)
{
    if (!list || !list->data)
        return; // Exit if the list or its data is NULL

    // Note: If elements are pointers and need to be freed, uncomment and implement as needed:
    // for (int i = 0; i < list->size; i++) {
    //     free(((void **)list->data)[i]); // Free each element if dynamically allocated
    // }

    list->size = 0; // Reset the size of the list to zero

    // Optional: Zero out the memory for debugging/clean state
    memset(list->data, 0, list->capacity * list->i_size);
}

void list_print(list_t *list)
{
    for (int i = 0; i < list->size; i++)
    {
        Value item = *(Value *)list_getAt(list, i);
        printf("%s\n", as_string(item));
    }
}
void list_free(list_t *list)
{

    if (!list)
        return; // Avoid freeing NULL

    // Free the memory allocated for the data
    if (list->data)
    {
        free(list->data);  // Free the memory allocated for the list
        list->data = NULL; // Set the data pointer to NULL
    }

    list->size = 0;     // Set the size to 0
    list->capacity = 0; // Set the capacity to 0
    list->i_size = 0;   // Set the item size to 0

    free(list); // Free the memory allocated for the list
}
