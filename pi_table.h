#ifndef PI_TABLE_H
#define PI_TABLE_H

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "common.h"

#define HT_INIT_CAP 8 /* must be power of two                    */
#define HT_LOAD_NUM 3 /* load-factor numerator   (3/4 = 0.75)    */
#define HT_LOAD_DEN 4 /* load-factor denominator                 */

#define FNV_OFFSET 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

static inline uint64_t fnv_1a(const char *key)
{
    uint64_t h = FNV_OFFSET;
    for (const unsigned char *p = (const unsigned char *)key; *p; p++)
        h = (h ^ *p) * FNV_PRIME;
    return h;
}

/*
 * ht_item - one bucket in the open-addressed array.
 *
 * Layout (assuming i_size == sizeof(pi_value) == 16):
 *   hash      8 bytes   checked first → early-out on mismatch, no strcmp
 *   value     i_size    INLINE - no separate heap allocation
 *   key_idx   4 bytes   index into table->keys_buf (UINT32_MAX = empty/tomb)
 *   _flags    1 byte    HT_LIVE | HT_TOMB
 *   _pad      3 bytes   (compiler will add anyway; be explicit)
 *
 * Keeping hash and value contiguous means a typical successful lookup
 * reads exactly one cache line.
 */
#define HT_FLAG_LIVE 0x01
#define HT_FLAG_TOMB 0x02

/* We can't use a flexible array member inside ht_item, so we store the
 * value as a zero-length char array and access it with the accessor macros
 * below.  The actual storage is in a flat `char *arena` on the table. */

typedef struct
{
    uint64_t hash;
    uint32_t key_idx; /* UINT32_MAX → empty or tombstone */
    uint8_t flags;
    uint8_t _pad[3];
    /* value bytes follow - see HT_SLOT_VALUE() */
} ht_item;

/*
 * table_t - the hash table.
 *
 * items[]     open-addressed bucket array  (size = capacity * item_stride)
 * keys_buf    flat byte buffer of NUL-terminated keys packed end-to-end
 * order[]     uint32_t array of *slot indices* in insertion order
 *             (used by the iterator; swap-to-back on delete → O(1))
 *
 * item_stride = sizeof(ht_item) + i_size  (precomputed at create time)
 */
typedef struct
{
    char *items;         /* flat arena: capacity * item_stride bytes     */
    char *keys_buf;      /* packed key strings                           */
    uint32_t *order;     /* insertion-order slot indices                 */
    uint32_t capacity;   /* bucket count, always power of two            */
    uint32_t size;       /* live entries                                 */
    uint32_t order_size; /* entries in order[] (may include tombs)       */
    uint32_t keys_used;  /* bytes consumed in keys_buf                   */
    uint32_t keys_cap;   /* allocated bytes in keys_buf                  */
    uint32_t order_cap;  /* allocated slots in order[]                   */
    size_t i_size;       /* sizeof the value type                        */
    size_t item_stride;  /* sizeof(ht_item) + i_size                     */
    uint64_t version;    /* changes whenever the table contents change */
} table_t;

#define HT_SLOT(t, i) ((ht_item *)((t)->items + (size_t)(i) * (t)->item_stride))
#define HT_SLOT_VALUE(s) ((void *)((char *)(s) + sizeof(ht_item)))

table_t *ht_create(size_t i_size);
void ht_free(table_t *table);

bool ht_put(table_t *table, const char *key, const void *value);
bool ht_set(table_t *table, const char *key, const void *value);
bool ht_delete(table_t *table, const char *key);
bool ht_has(table_t *table, const char *key);
int ht_length(table_t *table);

bool ht_expand(table_t *table);

/*
 * Returns a pointer directly into the bucket's inline value storage.
 * No extra dereference the value lives right after the ht_item header.
 *
 * The hash is checked before strcmp, so keys that merely collide on their
 * bucket index (not their full hash) skip the strcmp entirely.
 */
static inline void *ht_getHash(table_t *table, const char *key, uint64_t hash)
{
    uint32_t mask = table->capacity - 1;
    uint32_t idx = (uint32_t)(hash & mask);

    for (;;)
    {
        ht_item *slot = HT_SLOT(table, idx);

        if (!(slot->flags & (HT_FLAG_LIVE | HT_FLAG_TOMB)))
            return NULL; /* empty bucket → miss */

        if ((slot->flags & HT_FLAG_LIVE) && slot->hash == hash && strcmp(table->keys_buf + slot->key_idx, key) == 0)
            return HT_SLOT_VALUE(slot);

        idx = (idx + 1) & mask;
    }
}

static inline void *ht_get(table_t *table, const char *key)
{
    return ht_getHash(table, key, fnv_1a(key));
}

/*
 * The iterator walks order[] (insertion-order slot indices) and reads the
 * slot directly - no ht_get, no hash recomputation.
 */
typedef struct
{
    const char *key; /* current key (points into keys_buf)            */
    void *value;     /* current value (points into inline slot)       */
    table_t *_table;
    uint32_t _order_i; /* position in order[]                           */
} ht_iter;

static inline ht_iter ht_iterator(table_t *table)
{
    ht_iter it = {.key = NULL, .value = NULL, ._table = table, ._order_i = 0};
    return it;
}

static inline bool ht_next(ht_iter *it)
{
    table_t *t = it->_table;
    while (it->_order_i < t->order_size)
    {
        uint32_t slot_i = t->order[it->_order_i++];
        ht_item *slot = HT_SLOT(t, slot_i);
        if (slot->flags & HT_FLAG_LIVE)
        {
            it->key = t->keys_buf + slot->key_idx;
            it->value = HT_SLOT_VALUE(slot);
            return true;
        }
        /* tombstone in order[] - skip (compacted lazily on expand) */
    }
    return false;
}

static inline void ht_reset(ht_iter *it) { it->_order_i = 0; }

/* hasNext is O(1) amortised: just check if any remaining order entry is live */
static inline bool ht_hasNext(ht_iter *it)
{
    table_t *t = it->_table;
    for (uint32_t i = it->_order_i; i < t->order_size; i++)
    {
        ht_item *slot = HT_SLOT(t, t->order[i]);
        if (slot->flags & HT_FLAG_LIVE)
            return true;
    }
    return false;
}

#endif /* PI_TABLE_H */
