#ifndef PI_TABLE_H
#define PI_TABLE_H

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "pi_list.h"
#include "common.h"

#define FNV_OFFSET 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL
#define LOAD_FACTOR 0.75f

typedef struct
{
    char *key;
    void *value;
    uint64_t hash; // Precomputed hash
} ht_item;

typedef struct
{
    int size;      // Number of items in the table
    int capacity;  // Total capacity of the table
    size_t i_size; // Size of each value type (item)
    ht_item *items;
    list_t *keys; // Keys in insertion order
} table_t;

// Create a table for values of size `i_size`
table_t *ht_create(size_t i_size);
bool ht_has(table_t *table, const char *key);

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

static inline void *ht_get(table_t *table, const char *key)
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

bool ht_set(table_t *table, const char *key, const void *value);
bool ht_put(table_t *table, const char *key, const void *value);
bool ht_delete(table_t *table, const char *key);

bool ht_expand(table_t *table);
int ht_length(table_t *table);
void ht_free(table_t *table);

typedef struct
{
    char *key;       // Current key
    void *value;     // Current value
    table_t *_table; // Reference to the table
    size_t _index;   // Current index
} ht_iter;

ht_iter ht_iterator(table_t *table);
bool ht_next(ht_iter *it);
bool ht_hasNext(ht_iter *it);
void ht_reset(ht_iter *it);

#endif // PI_TABLE_H
