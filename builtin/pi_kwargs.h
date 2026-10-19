#ifndef PI_KWARGS_H
#define PI_KWARGS_H

#include "../src/pi_vm.h"

static inline bool _kw_has(VM *vm, const char *name)
{
    return vm_hasKwarg(vm, name);
}

static inline bool _kw_get(VM *vm, const char *name, Value *out)
{
    return vm_getKwarg(vm, name, out);
}

static inline Value _kw_getOr(VM *vm, const char *name, Value fallback)
{
    return vm_getKwargOr(vm, name, fallback);
}

static inline bool _kw_getNum(VM *vm, const char *name, double *out)
{
    Value value;
    if (!vm_getKwarg(vm, name, &value))
        return FALSE;
    if (!IS_NUM(value))
        return FALSE;
    *out = AS_NUM(value);
    return TRUE;
}

static inline bool _kw_getBool(VM *vm, const char *name, bool *out)
{
    Value value;
    if (!vm_getKwarg(vm, name, &value))
        return FALSE;
    if (!IS_BOOL(value))
        return FALSE;
    *out = AS_BOOL(value);
    return TRUE;
}

static inline bool _kw_getString(VM *vm, const char *name, const char **out)
{
    Value value;
    if (!vm_getKwarg(vm, name, &value))
        return FALSE;
    if (!IS_STRING(value))
        return FALSE;
    *out = AS_CSTRING(value);
    return TRUE;
}

#endif // PI_KWARGS_H
