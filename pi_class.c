#include "pi_object.h"

#define CREATE_OBJ(obj, type) (obj *)alloc_object(sizeof(obj), type)

static bool table_getValue(table_t *table, const char *name, Value *out)
{
    if (!table)
        return false;

    Value *value = (Value *)ht_get(table, name);
    if (!value)
        return false;

    if (out)
        *out = *value;
    return true;
}

Object *new_class(const char *name, PiClass *super, table_t *members)
{
    PiClass *_class = CREATE_OBJ(PiClass, OBJ_CLASS);
    _class->name = name ? strdup(name) : NULL;
    _class->super = super;
    _class->members = members ? members : ht_create(sizeof(Value));
    _class->it = ht_iterator(_class->members);
    return (Object *)_class;
}

Object *new_instance(PiClass *_class)
{
    PiInstance *instance = CREATE_OBJ(PiInstance, OBJ_INSTANCE);
    instance->_class = _class;
    instance->fields = ht_create(sizeof(Value));
    instance->it = ht_iterator(instance->fields);
    return (Object *)instance;
}

bool class_getMember(PiClass *_class, const char *name, Value *out)
{
    for (PiClass *current = _class; current != NULL; current = current->super)
    {
        if (table_getValue(current->members, name, out))
            return true;
    }
    return false;
}

void class_setMember(PiClass *_class, const char *name, Value value)
{
    if (!_class || !_class->members)
        return;
    if (!ht_set(_class->members, name, &value))
        ht_put(_class->members, name, &value);
}

bool instance_getMember(PiInstance *instance, const char *name, Value *out)
{
    if (!instance)
        return false;

    if (table_getValue(instance->fields, name, out))
        return true;
    return class_getMember(instance->_class, name, out);
}

void instance_setMember(PiInstance *instance, const char *name, Value value)
{
    if (!instance || !instance->fields)
        return;
    if (!ht_set(instance->fields, name, &value))
        ht_put(instance->fields, name, &value);
}
