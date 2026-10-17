#include "pi_class.h"

#define CREATE_OBJ(obj, type) (obj *)alloc_object(sizeof(obj), type)

static uint64_t class_epoch = 1;

static bool _table_getValue(HT_Table *table, const char *name, Value *out)
{
    if (!table)
        return FALSE;

    Value *value = (Value *)_ht_get(table, name);
    if (!value)
        return FALSE;

    if (out)
        *out = *value;
    return TRUE;
}

static bool _table_getValueHash(HT_Table *table, const char *name, uint64_t hash, Value *out)
{
    if (!table)
        return FALSE;

    Value *value = (Value *)_ht_getHash(table, name, hash);
    if (!value)
        return FALSE;

    if (out)
        *out = *value;
    return TRUE;
}

Object *new_class(const char *name, PiClass *super, HT_Table *members)
{
    PiClass *_class = CREATE_OBJ(PiClass, OBJ_CLASS);
    _class->name = name ? strdup(name) : NULL;
    _class->super = super;
    _class->members = members ? members : ht_create(sizeof(Value));
    _class->field_names = ht_create(sizeof(uint16_t));
    _class->slot_count = super ? super->slot_count : 0;
    if (super && super->field_names)
    {
        HT_Iterator fields = _ht_iterator(super->field_names);
        while (_ht_next(&fields))
            ht_put(_class->field_names, fields.key, fields.value);
    }
    _class->version = 1;
    memset(_class->bound_cache, 0, sizeof(_class->bound_cache));
    _class->bound_cache_next = 0;
    _class->it = _ht_iterator(_class->members);
    return (Object *)_class;
}

Object *new_instance(PiClass *_class)
{
    PiInstance *instance = CREATE_OBJ(PiInstance, OBJ_INSTANCE);
    instance->_class = _class;
    instance->fields = NULL;
    instance->slots = _class->slot_count
                          ? calloc((size_t)_class->slot_count, sizeof(Value))
                          : NULL;
    for (uint16_t i = 0; i < _class->slot_count; i++)
        instance->slots[i] = NEW_NIL();
    instance->owns_storage = TRUE;
    memset(instance->bound_cache, 0, sizeof(instance->bound_cache));
    instance->bound_cache_next = 0;
    instance->it = _ht_iterator(instance->fields);
    return (Object *)instance;
}

bool class_getMember(PiClass *_class, const char *name, Value *out)
{
    for (PiClass *current = _class; current != NULL; current = current->super)
    {
        if (_table_getValue(current->members, name, out))
            return TRUE;
    }
    return FALSE;
}

bool class_getMemberHash(PiClass *_class, const char *name, uint64_t hash, Value *out)
{
    for (PiClass *current = _class; current != NULL; current = current->super)
    {
        if (_table_getValueHash(current->members, name, hash, out))
            return TRUE;
    }
    return FALSE;
}

void class_setMember(PiClass *_class, const char *name, Value value)
{
    if (!_class || !_class->members)
        return;
    if (!ht_set(_class->members, name, &value))
        ht_put(_class->members, name, &value);
    _class->version++;
    class_epoch++;
}

bool class_deleteMember(PiClass *_class, const char *name)
{
    if (!_class || !_class->members)
        return FALSE;

    bool removed = ht_delete(_class->members, name);
    if (removed)
    {
        _class->version++;
        class_epoch++;
    }
    return removed;
}

uint64_t class_mutationVersion(void)
{
    return class_epoch;
}

bool instance_getMember(PiInstance *instance, const char *name, Value *out)
{
    if (!instance)
        return FALSE;

    uint16_t slot;
    if (class_getFieldSlot(instance->_class, name, &slot))
    {
        if (out)
            *out = instance->slots[slot];
        return TRUE;
    }
    if (_table_getValue(instance->fields, name, out))
        return TRUE;
    return class_getMember(instance->_class, name, out);
}

bool instance_getMemberHash(PiInstance *instance, const char *name, uint64_t hash, Value *out)
{
    if (!instance)
        return FALSE;

    uint16_t slot;
    if (class_getFieldSlotHash(instance->_class, name, hash, &slot))
    {
        if (out)
            *out = instance->slots[slot];
        return TRUE;
    }
    if (_table_getValueHash(instance->fields, name, hash, out))
        return TRUE;
    return class_getMemberHash(instance->_class, name, hash, out);
}

void instance_setMember(PiInstance *instance, const char *name, Value value)
{
    if (!instance)
        return;
    uint16_t slot;
    if (class_getFieldSlot(instance->_class, name, &slot))
    {
        instance->slots[slot] = value;
        return;
    }
    if (!instance->fields)
    {
        instance->fields = ht_create(sizeof(Value));
        instance->it = _ht_iterator(instance->fields);
    }
    if (!ht_set(instance->fields, name, &value))
        ht_put(instance->fields, name, &value);
}

bool class_getFieldSlot(PiClass *_class, const char *name, uint16_t *slot)
{
    if (!_class || !_class->field_names)
        return FALSE;
    uint16_t *found = (uint16_t *)_ht_get(_class->field_names, name);
    if (!found)
        return FALSE;
    if (slot)
        *slot = *found;
    return TRUE;
}

bool class_getFieldSlotHash(PiClass *_class, const char *name, uint64_t hash, uint16_t *slot)
{
    if (!_class || !_class->field_names)
        return FALSE;
    uint16_t *found = (uint16_t *)_ht_getHash(_class->field_names, name, hash);
    if (!found)
        return FALSE;
    if (slot)
        *slot = *found;
    return TRUE;
}
