#include <stdlib.h>
#include <string.h>

#include "pi_class.h"
#include "gc.h"

static bool table_getValue(table_t *table, const char *name, Value *out)
{
    if (!table || !name)
        return false;

    Value *value = (Value *)ht_get(table, name);
    if (!value)
        return false;

    if (out)
        *out = *value;
    return true;
}

static void table_setValue(table_t *table, const char *name, Value value)
{
    if (!ht_set(table, name, &value))
        ht_put(table, name, &value);
}

static bool instance_ensureCapacity(PiInstance *instance, int capacity)
{
    if (capacity <= instance->slot_capacity)
        return true;

    int old_capacity = instance->slot_capacity;
    int new_capacity = old_capacity < 8 ? 8 : old_capacity;
    while (new_capacity < capacity)
        new_capacity *= 2;

    Value *slots = realloc(instance->slots, sizeof(Value) * (size_t)new_capacity);
    if (!slots)
        return false;

    bool *slot_used = realloc(instance->slot_used, sizeof(bool) * (size_t)new_capacity);
    if (!slot_used)
    {
        instance->slots = slots;
        return false;
    }

    instance->slots = slots;
    instance->slot_used = slot_used;
    for (int i = old_capacity; i < new_capacity; i++)
    {
        instance->slots[i] = NEW_NIL();
        instance->slot_used[i] = false;
    }
    instance->slot_capacity = new_capacity;
    return true;
}

Object *new_class(const char *name, PiClass *super)
{
    PiClass *_class = (PiClass *)alloc_object(sizeof(PiClass), OBJ_CLASS);
    _class->name = name ? strdup(name) : NULL;
    _class->methods = ht_create(sizeof(Value));
    _class->static_fields = ht_create(sizeof(Value));
    _class->slots = ht_create(sizeof(int));
    _class->slot_count = super ? super->slot_count : 0;
    _class->super = super;

    if (!_class->methods || !_class->static_fields || !_class->slots)
        error("[new_class] Memory allocation failed.");

    if (super && super->slots)
    {
        ht_iter it = ht_iterator(super->slots);
        while (ht_next(&it))
            ht_put(_class->slots, it.key, it.value);
    }

    return (Object *)_class;
}

Object *new_instance(PiClass *_class)
{
    if (!_class)
        error("[new_instance] Class cannot be NULL.");

    PiInstance *instance = (PiInstance *)alloc_object(sizeof(PiInstance), OBJ_INSTANCE);
    instance->_class = _class;
    instance->slot_capacity = _class->slot_count;
    instance->slots = NULL;
    instance->slot_used = NULL;
    instance->fields = ht_create(sizeof(Value));
    if (!instance->fields)
        error("[new_instance] Memory allocation failed.");

    if (instance->slot_capacity > 0)
    {
        instance->slots = malloc(sizeof(Value) * (size_t)instance->slot_capacity);
        instance->slot_used = calloc((size_t)instance->slot_capacity, sizeof(bool));
        if (!instance->slots || !instance->slot_used)
            error("[new_instance] Memory allocation failed.");

        for (int i = 0; i < instance->slot_capacity; i++)
            instance->slots[i] = NEW_NIL();
    }

    return (Object *)instance;
}

void class_setMethod(PiClass *_class, const char *name, Value method)
{
    if (!_class || !name)
        return;
    table_setValue(_class->methods, name, method);
}

bool class_getMethod(PiClass *_class, const char *name, Value *out)
{
    for (PiClass *current = _class; current; current = current->super)
        if (table_getValue(current->methods, name, out))
            return true;
    return false;
}

void class_setStatic(PiClass *_class, const char *name, Value value)
{
    if (!_class || !name)
        return;
    table_setValue(_class->static_fields, name, value);
}

bool class_getStatic(PiClass *_class, const char *name, Value *out)
{
    for (PiClass *current = _class; current; current = current->super)
        if (table_getValue(current->static_fields, name, out))
            return true;
    return false;
}

int class_getSlot(PiClass *_class, const char *name)
{
    if (!_class || !name)
        return -1;

    for (PiClass *current = _class; current; current = current->super)
    {
        int *slot = (int *)ht_get(current->slots, name);
        if (slot)
            return *slot;
    }
    return -1;
}

int class_ensureSlot(PiClass *_class, const char *name)
{
    if (!_class || !name)
        return -1;

    int existing = class_getSlot(_class, name);
    if (existing >= 0)
        return existing;

    if (_class->super && _class->slot_count < _class->super->slot_count)
        _class->slot_count = _class->super->slot_count;

    int slot = _class->slot_count++;
    if (!ht_put(_class->slots, name, &slot))
        return -1;

    return slot;
}

bool instance_getSlot(PiInstance *instance, const char *name, Value *out)
{
    if (!instance || !name)
        return false;

    int slot = class_getSlot(instance->_class, name);
    if (slot < 0 || slot >= instance->slot_capacity || !instance->slot_used[slot])
        return false;

    if (out)
        *out = instance->slots[slot];
    return true;
}

bool instance_setSlot(PiInstance *instance, const char *name, Value value)
{
    if (!instance || !name)
        return false;

    int slot = class_ensureSlot(instance->_class, name);
    if (slot < 0)
        return false;

    if (!instance_ensureCapacity(instance, slot + 1))
        return false;

    instance->slots[slot] = value;
    instance->slot_used[slot] = true;
    return true;
}

bool instance_getField(PiInstance *instance, const char *name, Value *out)
{
    if (!instance || !name)
        return false;

    if (instance_getSlot(instance, name, out))
        return true;

    return table_getValue(instance->fields, name, out);
}

void instance_setField(PiInstance *instance, const char *name, Value value)
{
    if (!instance || !name)
        return;

    if (class_getSlot(instance->_class, name) >= 0)
    {
        instance_setSlot(instance, name, value);
        return;
    }

    table_setValue(instance->fields, name, value);
}

bool instance_deleteField(PiInstance *instance, const char *name)
{
    if (!instance || !name)
        return false;

    int slot = class_getSlot(instance->_class, name);
    if (slot >= 0 && slot < instance->slot_capacity && instance->slot_used[slot])
    {
        instance->slots[slot] = NEW_NIL();
        instance->slot_used[slot] = false;
        return true;
    }

    return ht_delete(instance->fields, name);
}

static void mark_valueTable(table_t *table)
{
    if (!table)
        return;

    ht_iter it = ht_iterator(table);
    while (ht_next(&it))
        mark_value(*(Value *)it.value);
}

void mark_class(PiClass *_class)
{
    if (!_class)
        return;

    if (_class->super)
        mark_object((Object *)_class->super);

    mark_valueTable(_class->methods);
    mark_valueTable(_class->static_fields);
}

void mark_instance(PiInstance *instance)
{
    if (!instance)
        return;

    if (instance->_class)
        mark_object((Object *)instance->_class);

    for (int i = 0; i < instance->slot_capacity; i++)
        if (instance->slot_used[i])
            mark_value(instance->slots[i]);

    mark_valueTable(instance->fields);
}

void free_class(PiClass *_class)
{
    if (!_class)
        return;

    free(_class->name);
    ht_free(_class->methods);
    ht_free(_class->static_fields);
    ht_free(_class->slots);
}

void free_instance(PiInstance *instance)
{
    if (!instance)
        return;

    free(instance->slots);
    free(instance->slot_used);
    ht_free(instance->fields);
}
