#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "pi_set.h"

static int get_index(set_t *set, const void *data)
{
    int hash = set->hash(data);
    if (hash < 0)
        hash = -hash;

    return hash % set->size;
}

set_t *set_create(int (*hash)(void *), int (*equals)(void *, void *))
{
    set_t *set = malloc(sizeof(set_t));
    if (!set)
        return NULL;

    set->size = MAX_SET_SIZE;
    set->buckets = calloc(set->size, sizeof(SetNode *));

    if (!set->buckets)
    {
        free(set);
        return NULL;
    }

    set->hash = hash;
    set->equals = equals;
    return set;
}

void set_add(set_t *set, void *data)
{
    int index = get_index(set, data);
    SetNode *node = set->buckets[index];

    while (node != NULL)
    {
        if (set->equals(node->data, data))
            return;

        node = node->next;
    }

    SetNode *new_node = malloc(sizeof(SetNode));
    if (!new_node)
        return;

    new_node->data = data;
    new_node->next = set->buckets[index];
    set->buckets[index] = new_node;
}

int set_contains(set_t *set, void *data)
{
    int index = get_index(set, data);
    SetNode *node = set->buckets[index];

    while (node != NULL)
    {
        if (set->equals(node->data, data))
            return 1;

        node = node->next;
    }

    return 0;
}

bool set_has(set_t *set, void *data)
{
    return set_contains(set, data) == 1;
}

bool set_remove(set_t *set, void *data)
{
    int index = get_index(set, data);
    SetNode *node = set->buckets[index];
    SetNode *prev = NULL;

    while (node != NULL)
    {
        if (set->equals(node->data, data))
        {
            if (prev == NULL)
                set->buckets[index] = node->next;
            else
                prev->next = node->next;

            free(node);
            return true;
        }

        prev = node;
        node = node->next;
    }

    return false;
}

void set_free(set_t *set)
{
    if (!set)
        return;

    // Only the wrapper nodes are freed; element ownership stays with the caller.
    for (int i = 0; i < set->size; i++)
    {
        SetNode *node = set->buckets[i];
        while (node != NULL)
        {
            SetNode *temp = node;
            node = node->next;
            free(temp);
        }
    }

    free(set->buckets);
    free(set);
}
