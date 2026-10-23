#ifndef PI_KWARGS_H
#define PI_KWARGS_H

#include "../src/pi_vm.h"

/* Positional calls leave this nil, so they avoid all keyword lookups. */
static inline PiMap *_kw_map(VM *vm)
{
    return vm && IS_MAP(vm->_kw_args) ? AS_MAP(vm->_kw_args) : NULL;
}

static inline bool _kw_get(PiMap *kwargs, const char *name, Value *out)
{
    if (!kwargs || !name || !out)
        return FALSE;
    Value *value = _ht_get(kwargs->table, name);
    if (!value)
        return FALSE;
    *out = *value;
    return TRUE;
}

#endif // PI_KWARGS_H
