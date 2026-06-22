#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "pi_table.h"
#include "pi_value.h"
#include "pi_string.h"




/* Tombstone marker – a unique address that cannot be a real key */
static char tombstone_key = 0;
#define TOMBSTONE ((char *)&tombstone_key)

/**
 * FNV-1a hash function
 *
 * The FNV-1a hash is based on an algorithm originally developed by Landon Curt Noll.
 * See http://www.isthe.com/chongo/tech/comp/fnv/ for more information.
 *
 * @param key The string to hash
 * @return The hash value
 */
static inline uint64_t fnv_1a(const char *key)
{
    uint64_t hash = FNV_OFFSET;
    for (const char *p = key; *p; p++)
    {
        hash ^= (uint64_t)(unsigned char)(*p);
        hash *= FNV_PRIME;
    }
    return hash;
}


table_t *ht_create(size_t i_size)
{
    table_t *table = malloc(sizeof(table_t));
    if (!table)
        return NULL;

    table->size = 0;
    table->capacity = INIT_CAP;
    table->i_size = i_size;
    table->items = calloc(table->capacity, sizeof(ht_item));
    if (!table->items)
    {
        free(table);
        return NULL;
    }
    return table;
}

void *ht_get(table_t *table, const char *key)
{
    uint64_t hash = fnv_1a(key);
    int mask = table->capacity - 1;
    int index = (int)(hash & mask);

    while (table->items[index].key != NULL)
    {
        if (table->items[index].key != TOMBSTONE &&
            table->items[index].hash == hash &&
            strcmp(table->items[index].key, key) == 0)
        {
            return table->items[index].value;
        }
        index = (index + 1) & mask;
    }
    return NULL;
}

bool ht_has(table_t *table, const char *key)
{
    return ht_get(table, key) != NULL;
}


bool ht_set(table_t *table, const char *key, const void *value)
{
    uint64_t hash = fnv_1a(key);
    int mask = table->capacity - 1;
    int index = (int)(hash & mask);

    while (table->items[index].key != NULL)
    {
        if (table->items[index].key != TOMBSTONE &&
            table->items[index].hash == hash &&
            strcmp(table->items[index].key, key) == 0)
        {
            memcpy(table->items[index].value, value, table->i_size);
            return true;
        }
        index = (index + 1) & mask;
    }
    return false;
}


bool ht_put(table_t *table, const char *key, const void *value)
{
    // Expand if load factor > 0.75
    if ((table->size + 1) * 4 > table->capacity * 3)
    {
        if (!ht_expand(table))
            return false;
    }

    uint64_t hash = fnv_1a(key);
    int mask = table->capacity - 1;
    int index = (int)(hash & mask);
    int tombstone_idx = -1;

    // Search for existing key, remember first tombstone
    while (table->items[index].key != NULL)
    {
        if (table->items[index].key == TOMBSTONE)
        {
            if (tombstone_idx == -1)
                tombstone_idx = index;
        }
        else if (table->items[index].hash == hash &&
                 strcmp(table->items[index].key, key) == 0)
        {
            // Update existing entry
            memcpy(table->items[index].value, value, table->i_size);
            return true;
        }
        index = (index + 1) & mask;
    }

    // Not found – insert into first tombstone or at empty slot
    int insert_idx = (tombstone_idx != -1) ? tombstone_idx : index;

    char *new_key = strdup(key);
    if (!new_key)
        return false;
    void *new_value = malloc(table->i_size);
    if (!new_value)
    {
        free(new_key);
        return false;
    }
    memcpy(new_value, value, table->i_size);

    table->items[insert_idx].key = new_key;
    table->items[insert_idx].value = new_value;
    table->items[insert_idx].hash = hash;
    table->size++;
    return true;
}


bool ht_delete(table_t *table, const char *key)
{
    if (!table || !key)
        return false;

    uint64_t hash = fnv_1a(key);
    int mask = table->capacity - 1;
    int index = (int)(hash & mask);

    while (table->items[index].key != NULL)
    {
        if (table->items[index].key != TOMBSTONE &&
            table->items[index].hash == hash &&
            strcmp(table->items[index].key, key) == 0)
        {
            // Found – mark as tombstone, free resources
            free(table->items[index].key);
            free(table->items[index].value);
            table->items[index].key = TOMBSTONE;
            table->items[index].value = NULL; // optional
            table->size--;
            return true;
        }
        index = (index + 1) & mask;
    }
    return false;
}


bool ht_expand(table_t *table)
{
    int new_cap = table->capacity * 2;
    ht_item *new_items = calloc(new_cap, sizeof(ht_item));
    if (!new_items)
        return false;

    int new_mask = new_cap - 1;
    // Rehash all live entries (skip NULL and tombstone)
    for (int i = 0; i < table->capacity; i++)
    {
        ht_item item = table->items[i];
        if (item.key == NULL || item.key == TOMBSTONE)
            continue;

        int idx = (int)(item.hash & new_mask);
        while (new_items[idx].key != NULL)
            idx = (idx + 1) & new_mask;
        new_items[idx] = item; // copy the whole item
    }

    free(table->items);
    table->items = new_items;
    table->capacity = new_cap;
    return true;
}


void ht_free(table_t *table)
{
    if (!table)
        return;

    for (int i = 0; i < table->capacity; i++)
    {
        if (table->items[i].key != NULL && table->items[i].key != TOMBSTONE)
        {
            free(table->items[i].key);
            free(table->items[i].value);
        }
    }
    free(table->items);
    free(table);
}


int ht_length(table_t *table)
{
    return table->size;
}


ht_iter ht_iterator(table_t *table)
{
    ht_iter it = {._table = table, ._index = 0};
    return it;
}

bool ht_next(ht_iter *it)
{
    table_t *table = it->_table;
    while (it->_index < table->capacity)
    {
        ht_item *item = &table->items[it->_index++];
        if (item->key != NULL && item->key != TOMBSTONE)
        {
            it->key = item->key;
            it->value = item->value;
            return true;
        }
    }
    return false;
}

bool ht_hasNext(ht_iter *it)
{
    table_t *table = it->_table;
    for (int i = it->_index; i < table->capacity; i++)
    {
        if (table->items[i].key != NULL && table->items[i].key != TOMBSTONE)
            return true;
    }
    return false;
}

void ht_reset(ht_iter *it)
{
    it->_index = 0;
}