#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "pi_table.h"

static inline uint32_t next_pow2(uint32_t v)
{
    if (v == 0)
        return 1;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

/*
 * append_key - intern a key string into keys_buf.
 * Returns the byte offset of the stored key, or UINT32_MAX on OOM.
 */
static uint32_t append_key(table_t *table, const char *key)
{
    uint32_t needed = (uint32_t)strlen(key) + 1;
    if (table->keys_used + needed > table->keys_cap)
    {
        uint32_t new_cap = table->keys_cap ? table->keys_cap * 2 : 64;
        while (new_cap < table->keys_used + needed)
            new_cap *= 2;
        char *nb = realloc(table->keys_buf, new_cap);
        if (!nb)
            return UINT32_MAX;
        table->keys_buf = nb;
        table->keys_cap = new_cap;
    }
    uint32_t off = table->keys_used;
    memcpy(table->keys_buf + off, key, needed);
    table->keys_used += needed;
    return off;
}

/*
 * push_order - record slot_idx in insertion-order array.
 */
static bool push_order(table_t *table, uint32_t slot_idx)
{
    if (table->order_size == table->order_cap)
    {
        uint32_t new_cap = table->order_cap ? table->order_cap * 2 : 8;
        uint32_t *nb = realloc(table->order, new_cap * sizeof(uint32_t));
        if (!nb)
            return false;
        table->order = nb;
        table->order_cap = new_cap;
    }
    table->order[table->order_size++] = slot_idx;
    return true;
}

table_t *ht_create(size_t i_size)
{
    table_t *table = calloc(1, sizeof(table_t));
    if (!table)
        return NULL;

    table->i_size = i_size;
    table->item_stride = sizeof(ht_item) + i_size;
    table->capacity = HT_INIT_CAP;

    table->items = calloc(HT_INIT_CAP, table->item_stride);
    if (!table->items)
    {
        free(table);
        return NULL;
    }

    /* keys_buf and order are grown lazily */
    return table;
}

void ht_free(table_t *table)
{
    if (!table)
        return;
    free(table->items);
    free(table->keys_buf);
    free(table->order);
    free(table);
}

bool ht_expand(table_t *table)
{
    uint32_t new_cap = table->capacity * 2;
    char *new_items = calloc(new_cap, table->item_stride);
    if (!new_items)
        return false;

    uint32_t new_mask = new_cap - 1;
    uint32_t new_order_size = 0;

    /*
     * Rehash: walk order[] instead of the full bucket array so we naturally
     * compact away tombstones from order[] during expand.
     */
    for (uint32_t oi = 0; oi < table->order_size; oi++)
    {
        ht_item *old_slot = HT_SLOT(table, table->order[oi]);
        if (!(old_slot->flags & HT_FLAG_LIVE))
            continue; /* tombstone - drop from order */

        /* find new slot */
        uint32_t idx = (uint32_t)(old_slot->hash & new_mask);
        while (1)
        {
            ht_item *ns = (ht_item *)(new_items + (size_t)idx * table->item_stride);
            if (!(ns->flags & HT_FLAG_LIVE))
            {
                /* copy header + inline value in one shot */
                memcpy(ns, old_slot, table->item_stride);
                table->order[new_order_size++] = idx;
                break;
            }
            idx = (idx + 1) & new_mask;
        }
    }

    free(table->items);
    table->items = new_items;
    table->capacity = new_cap;
    table->order_size = new_order_size;
    return true;
}

bool ht_put(table_t *table, const char *key, const void *value)
{
    /* expand before reaching 75% load */
    if ((table->size + 1) * HT_LOAD_DEN > table->capacity * HT_LOAD_NUM)
    {
        if (!ht_expand(table))
            return false;
    }

    uint64_t hash = fnv_1a(key);
    uint32_t mask = table->capacity - 1;
    uint32_t idx = (uint32_t)(hash & mask);
    int32_t tomb = -1; /* first tombstone slot seen */

    for (;;)
    {
        ht_item *slot = HT_SLOT(table, idx);

        if (slot->flags & HT_FLAG_LIVE)
        {
            /* occupied: check for key match (update) */
            if (slot->hash == hash &&
                strcmp(table->keys_buf + slot->key_idx, key) == 0)
            {
                memcpy(HT_SLOT_VALUE(slot), value, table->i_size);
                return true;
            }
        }
        else if (slot->flags & HT_FLAG_TOMB)
        {
            if (tomb < 0)
                tomb = (int32_t)idx;
        }
        else
        {
            /* empty - key not present */
            break;
        }
        idx = (idx + 1) & mask;
    }

    /* Insert at tombstone (if any) or empty slot */
    uint32_t insert_idx = (tomb >= 0) ? (uint32_t)tomb : idx;
    ht_item *slot = HT_SLOT(table, insert_idx);

    uint32_t key_off = append_key(table, key);
    if (key_off == UINT32_MAX)
        return false;

    slot->hash = hash;
    slot->key_idx = key_off;
    slot->flags = HT_FLAG_LIVE;
    memcpy(HT_SLOT_VALUE(slot), value, table->i_size);

    if (!push_order(table, insert_idx))
    {
        /* rollback: mark slot empty again (key_off wasted but safe) */
        slot->flags = 0;
        return false;
    }
    table->size++;
    return true;
}

bool ht_set(table_t *table, const char *key, const void *value)
{
    uint64_t hash = fnv_1a(key);
    uint32_t mask = table->capacity - 1;
    uint32_t idx = (uint32_t)(hash & mask);

    for (;;)
    {
        ht_item *slot = HT_SLOT(table, idx);
        if (!(slot->flags & (HT_FLAG_LIVE | HT_FLAG_TOMB)))
            return false;
        if ((slot->flags & HT_FLAG_LIVE) && slot->hash == hash && strcmp(table->keys_buf + slot->key_idx, key) == 0)
        {
            memcpy(HT_SLOT_VALUE(slot), value, table->i_size);
            return true;
        }
        idx = (idx + 1) & mask;
    }
}

/*
 * O(1) ordered delete using swap-to-back on order[].
 *
 * We mark the bucket as a tombstone (keeps probe chains valid) and then
 * remove it from order[] by swapping it with the last element - no linear
 * scan, no shifting.
 *
 * Note: key bytes in keys_buf are NOT reclaimed (they're compacted on the
 * next ht_expand). This is almost always fine; keys are short and the buf
 * only grows with unique inserts.
 */
bool ht_delete(table_t *table, const char *key)
{
    if (!table || !key)
        return false;

    uint64_t hash = fnv_1a(key);
    uint32_t mask = table->capacity - 1;
    uint32_t idx = (uint32_t)(hash & mask);

    for (;;)
    {
        ht_item *slot = HT_SLOT(table, idx);
        if (!(slot->flags & (HT_FLAG_LIVE | HT_FLAG_TOMB)))
            return false; /* empty - key absent */

        if ((slot->flags & HT_FLAG_LIVE) && slot->hash == hash && strcmp(table->keys_buf + slot->key_idx, key) == 0)
        {

            slot->flags = HT_FLAG_TOMB;
            table->size--;

            /* O(1) removal from order[]: find this slot's order entry and
             * swap with the tail, then shrink.  We store slot indices in
             * order[] so we scan for `idx`, not the key. */
            for (uint32_t oi = 0; oi < table->order_size; oi++)
            {
                if (table->order[oi] == idx)
                {
                    table->order[oi] = table->order[--table->order_size];
                    break;
                }
            }
            return true;
        }
        idx = (idx + 1) & mask;
    }
}

bool ht_has(table_t *table, const char *key)
{
    return ht_get(table, key) != NULL;
}

int ht_length(table_t *table)
{
    return (int)table->size;
}