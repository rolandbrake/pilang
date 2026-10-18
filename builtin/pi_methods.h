#ifndef PI_METHODS_H
#define PI_METHODS_H

#include <stddef.h>
#include <stdbool.h>

#include "../pi_func.h"

typedef struct
{
    ObjectType type;
    const char *name;
    NativeFunc func;
    Value cached_bound;
    bool has_cached_bound;
} NativeMethod;

NativeMethod *pi_nativeMethodFor(ObjectType type, const char *name);
void pi_nativeMethodNames(ObjectType type, char *buffer, size_t size);

#endif // PI_METHODS_H
