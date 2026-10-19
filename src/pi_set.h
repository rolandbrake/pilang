#ifndef PI_SET_H
#define PI_SET_H

#include <stdbool.h>

#define MAX_SET_SIZE 1024 // Define a maximum size for the set to prevent excessive memory usage

typedef struct SetNode
{
    void *data;
    struct SetNode *next;
} SetNode;

typedef struct
{
    SetNode **buckets;
    int size;

    int (*hash)(void *data);
    int (*equals)(void *a, void *b);
} Set;

Set *set_create(int (*hash)(void *), int (*equals)(void *, void *));
void set_add(Set *set, void *data);
int set_contains(Set *set, void *data);
bool set_has(Set *set, void *data);
bool set_remove(Set *set, void *data);
void set_free(Set *set);

#endif // PI_SET_H
