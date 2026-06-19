#ifndef PI_METHODS_H
#define PI_METHODS_H

#include <stddef.h>
#include <stdbool.h>

#include "../pi_func.h"

typedef struct
{
    o_type type;
    const char *name;
    native_func func;
    Value cached_bound;
    bool has_cached_bound;
} NativeMethod;

NativeMethod *pi_nativeMethodFor(o_type type, const char *name);
void pi_nativeMethodNames(o_type type, char *buffer, size_t size);

#endif // PI_METHODS_H
