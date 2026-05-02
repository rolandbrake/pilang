#ifndef PI_SET_H
#define PI_SET_H

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
} set_t;

set_t *set_create(int (*hash)(void *), int (*equals)(void *, void *));
void set_add(set_t *set, void *data);
int set_contains(set_t *set, void *data);
bool set_has(set_t *set, void *data);
bool set_remove(set_t *set, void *data);
void set_free(set_t *set);

#endif // PI_SET_H