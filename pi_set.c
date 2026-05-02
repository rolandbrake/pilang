#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "pi_set.h"

/**
 * Computes a hash value for a given piece of data
 * using a hash function.
 *
 * The hash value is computed by applying the hash function
 * to the data and taking the absolute value of the result.
 * The hash value is then reduced modulo the size of the set.
 *
 * @param set The set to compute the hash value for.
 * @param data The piece of data to compute the hash value for.
 * @return The hash value of the data.
 */
static int get_index(set_t *set, const void *data)
{
    int hash = set->hash(data);
    if (hash < 0)
        hash = -hash;
    return hash % set->size;
}

/**
 * Creates a new set with the given hash and equality functions.
 * The hash function takes a piece of data and returns a hash value.
 * The equality function takes two pieces of data and returns a boolean
 * indicating whether they are equal.
 * @param hash The hash function to use for the set.
 * @param equals The equality function to use for the set.
 * @return A pointer to the newly created set.
 */
set_t *set_create(int (*hash)(void *), int (*equals)(void *, void *))
{
    set_t *set = malloc(sizeof(set_t));

    // If memory allocation fails, return NULL
    if (!set)
        return NULL;
    set->size = MAX_SET_SIZE;
    set->buckets = calloc(set->size, sizeof(SetNode *));

    if (!set->buckets)
    {
        free(set); // Free the set structure if bucket allocation fails
        return NULL;
    }

    set->hash = hash;     // Set the hash function
    set->equals = equals; // Set the equality function
    return set;
}

/**
 * Adds a new element to the set.
 * The element is added to the front of the bucket
 * corresponding to the hash of the element.
 * If the element already exists in the set, it is not added again.
 * @param set The set to add the element to.
 * @param data The element to add.
 */
void set_add(set_t *set, void *data)
{
    int index = get_index(set, data);    // Compute the bucket index using the hash function
    SetNode *node = set->buckets[index]; // Get the head of the bucket

    // Check if the data already exists in the bucket
    while (node != NULL)
    {
        if (set->equals(node->data, data)) // Use the equals function to compare data
            return;                        // Data already exists, do not add again
        node = node->next;                 // Move to the next node in the bucket
    }

    // If data does not exist, create a new node and add it to the front of the bucket
    SetNode *new_node = malloc(sizeof(SetNode));
    new_node->data = data;
    new_node->next = set->buckets[index];
    set->buckets[index] = new_node; // Update the head of the bucket to the new node
}

/**
 * Checks if a given element exists in the set.
 * The element is looked up by its hash value in the corresponding bucket.
 * The equals function is used to compare elements.
 * @param set The set to search for the element.
 * @param data The element to search for.
 * @return 1 if the element is found, 0 otherwise.
 */
int set_contains(set_t *set, void *data)
{
    int index = get_index(set, data);    // Compute the bucket index using the hash function
    SetNode *node = set->buckets[index]; // Get the head of the bucket

    // Traverse the bucket to find the data
    while (node != NULL)
    {
        if (set->equals(node->data, data)) // Use the equals function to compare data
            return 1;                      // Data found in the set
        node = node->next;                 // Move to the next node in the bucket
    }
    return 0; // Data not found in the set
}

/**
 * Checks if a given element exists in the set.
 *
 * This function checks if the given data exists in the set by
 * traversing the bucket associated with the data's hash value.
 * The equals function is used to compare elements.
 *
 * @param set The set to search for the element.
 * @param data The element to search for.
 * @return true if the element is found, false otherwise.
 */
bool set_has(set_t *set, void *data)
{
    return set_contains(set, data) == 1; // Return true if data is found, false otherwise
}

/**
 * Removes a given element from the set.
 * The element is looked up by its hash value in the corresponding bucket.
 * The equals function is used to compare elements.
 * @param set The set to remove the element from.
 * @param data The element to remove.
 * @return true if the element is successfully removed, false otherwise.
 */
bool set_remove(set_t *set, void *data)
{
    int index = get_index(set, data);    // Compute the bucket index using the hash function
    SetNode *node = set->buckets[index]; // Get the head of the bucket
    SetNode *prev = NULL;                // Keep track of the previous node

    // Traverse the bucket to find the data
    while (node != NULL)
    {
        if (set->equals(node->data, data)) // Use the equals function to compare data
        {
            if (prev == NULL)
                set->buckets[index] = node->next; // Update head of bucket if first node is removed
            else
                prev->next = node->next; // Bypass the removed node in the bucket
            free(node);                  // Free the memory of the removed node
            return true;                 // Data successfully removed
        }
        prev = node;       // Update previous node
        node = node->next; // Move to the next node in the bucket
    }
    return false; // Data not found in the set, nothing removed
}

/**
 * Frees the memory allocated for a set.
 *
 * This function traverses each bucket of the set and frees all
 * associated nodes. It then frees the buckets array and the set
 * structure itself.
 *
 * @param set The set to free.
 */
void set_free(set_t *set)
{
    // Free all nodes in each bucket
    for (int i = 0; i < set->size; i++)
    {
        SetNode *node = set->buckets[i];
        while (node != NULL)
        {
            SetNode *temp = node;
            node = node->next;
            free(temp); // Free each node
        }
    }
    free(set->buckets); // Free the buckets array
    free(set);          // Free the set structure itself
}
