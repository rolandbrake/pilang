#ifndef PI_KWARGS_H
#define PI_KWARGS_H

#include "../pi_vm.h"

static inline bool kw_has(vm_t *vm, const char *name)
{
    return vm_hasKwarg(vm, name);
}

static inline bool kw_get(vm_t *vm, const char *name, Value *out)
{
    return vm_getKwarg(vm, name, out);
}

static inline Value kw_getOr(vm_t *vm, const char *name, Value fallback)
{
    return vm_getKwargOr(vm, name, fallback);
}

static inline bool kw_getNum(vm_t *vm, const char *name, double *out)
{
    Value value;
    if (!vm_getKwarg(vm, name, &value))
        return false;
    if (!IS_NUM(value))
        return false;
    *out = AS_NUM(value);
    return true;
}

static inline bool kw_getBool(vm_t *vm, const char *name, bool *out)
{
    Value value;
    if (!vm_getKwarg(vm, name, &value))
        return false;
    if (!IS_BOOL(value))
        return false;
    *out = AS_BOOL(value);
    return true;
}

static inline bool kw_getString(vm_t *vm, const char *name, const char **out)
{
    Value value;
    if (!vm_getKwarg(vm, name, &value))
        return false;
    if (!IS_STRING(value))
        return false;
    *out = AS_CSTRING(value);
    return true;
}

#endif // PI_KWARGS_H
