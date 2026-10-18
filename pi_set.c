#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "pi_set.h"

static int _get_index(Set *set, const void *data)
{
    int hash = set->hash(data);
    if (hash < 0)
        hash = -hash;

    return hash % set->size;
}

Set *set_create(int (*hash)(void *), int (*equals)(void *, void *))
{
    Set *set = malloc(sizeof(Set));
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

void set_add(Set *set, void *data)
{
    int index = _get_index(set, data);
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

int set_contains(Set *set, void *data)
{
    int index = _get_index(set, data);
    SetNode *node = set->buckets[index];

    while (node != NULL)
    {
        if (set->equals(node->data, data))
            return 1;

        node = node->next;
    }

    return 0;
}

bool set_has(Set *set, void *data)
{
    return set_contains(set, data) == 1;
}

bool set_remove(Set *set, void *data)
{
    int index = _get_index(set, data);
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
            return TRUE;
        }

        prev = node;
        node = node->next;
    }

    return FALSE;
}

void set_free(Set *set)
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
