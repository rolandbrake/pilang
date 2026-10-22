#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "pi_obj.h"
#include "../src/pi_class.h"

static int _normalize_compare(int cmp)
{
    return (cmp > 0) - (cmp < 0);
}

static bool _is_classType(Value value)
{
    return IS_CLASS(value) || IS_INSTANCE(value);
}

static bool _get_classType(Value target, Value key, Value *out)
{
    if (!_is_classType(target) || !IS_STRING(key))
        return FALSE;
    const char *name = AS_CSTRING(key);
    return IS_CLASS(target)
               ? class_getMember(AS_CLASS(target), name, out)
               : instance_getMember(AS_INSTANCE(target), name, out);
}

static bool _has_classType(Value target, Value key)
{
    Value ignored;
    return _get_classType(target, key, &ignored);
}

static void _set_classType(Value target, Value key, Value value)
{
    if (!_is_classType(target) || !IS_STRING(key))
        return;
    if (IS_CLASS(target))
        class_setMember(AS_CLASS(target), AS_CSTRING(key), value);
    else
        instance_setMember(AS_INSTANCE(target), AS_CSTRING(key), value);
}

static bool _delete_classType(Value target, Value key)
{
    if (!_is_classType(target) || !IS_STRING(key))
        return FALSE;
    if (IS_CLASS(target))
        return class_deleteMember(AS_CLASS(target), AS_CSTRING(key));
    PiInstance *instance = AS_INSTANCE(target);
    if (class_getFieldSlot(instance->_class, AS_CSTRING(key), NULL))
        return FALSE;
    return instance->fields && ht_delete(instance->fields, AS_CSTRING(key));
}

static int _compare_cstrings(const void *left, const void *right)
{
    const char *const *a = (const char *const *)left;
    const char *const *b = (const char *const *)right;
    return strcmp(*a, *b);
}

static bool _map_equals(PiMap *left, PiMap *right);
static int _map_compare(PiMap *left, PiMap *right);

static bool _value_equals(Value left, Value right)
{
    if (IS_MAP(left) && IS_MAP(right))
        return _map_equals(AS_MAP(left), AS_MAP(right));

    return equals(left, right);
}

static int _value_compare(Value left, Value right)
{
    if (IS_MAP(left) && IS_MAP(right))
        return _map_compare(AS_MAP(left), AS_MAP(right));

    int cmp = compare(left, right);
    if (cmp != ERROR_COMPARE)
        return _normalize_compare(cmp);

    char *l_type = type_name(left);
    char *r_type = type_name(right);
    int type_cmp = strcmp(l_type, r_type);
    if (type_cmp != 0)
        return _normalize_compare(type_cmp);

    if (IS_OBJ(left) && IS_OBJ(right))
        return _normalize_compare((AS_OBJ(left) > AS_OBJ(right)) - (AS_OBJ(left) < AS_OBJ(right)));

    return 0;
}

static bool _map_equals(PiMap *left, PiMap *right)
{
    if (left == right)
        return TRUE;

    if (map_size(left) != map_size(right))
        return FALSE;

    // Use iterator over left map
    HT_Iterator it = _ht_iterator(left->table);
    while (_ht_next(&it))
    {
        const char *key = it.key;
        Value *left_value = (Value *)it.value;
        Value *right_value = _ht_get(right->table, key);

        if (right_value == NULL)
            return FALSE;

        if (!_value_equals(*left_value, *right_value))
            return FALSE;
    }

    return TRUE;
}

static int _map_compare(PiMap *left, PiMap *right)
{
    if (left == right)
        return 0;

    int size_cmp = _normalize_compare(map_size(left) - map_size(right));
    if (size_cmp != 0)
        return size_cmp;

    int left_size = ht_length(left->table);
    int right_size = ht_length(right->table);

    // Collect keys using iterators
    const char **left_keys = malloc(sizeof(char *) * left_size);
    const char **right_keys = malloc(sizeof(char *) * right_size);
    if (!left_keys || !right_keys)
    {
        free(left_keys);
        free(right_keys);
        return 0; // fallback (shouldn't happen)
    }

    int idx = 0;
    HT_Iterator it = _ht_iterator(left->table);
    while (_ht_next(&it))
        left_keys[idx++] = it.key;

    idx = 0;
    it = _ht_iterator(right->table);
    while (_ht_next(&it))
        right_keys[idx++] = it.key;

    qsort(left_keys, left_size, sizeof(char *), _compare_cstrings);
    qsort(right_keys, right_size, sizeof(char *), _compare_cstrings);

    for (int i = 0; i < left_size; i++)
    {
        int key_cmp = strcmp(left_keys[i], right_keys[i]);
        if (key_cmp != 0)
        {
            free(left_keys);
            free(right_keys);
            return _normalize_compare(key_cmp);
        }

        Value *left_value = _ht_get(left->table, left_keys[i]);
        Value *right_value = _ht_get(right->table, right_keys[i]);
        int value_cmp = _value_compare(*left_value, *right_value);
        if (value_cmp != 0)
        {
            free(left_keys);
            free(right_keys);
            return value_cmp;
        }
    }

    free(left_keys);
    free(right_keys);
    return 0;
}

// Clones a PiMap object, preserving its prototype chain and key-value pairs.
Value pi_clone(VM *vm, int argc, Value *argv)
{
    if (argc >= 1 && IS_CLASS(argv[0]))
    {
        PiClass *original = AS_CLASS(argv[0]);
        HT_Table *members = ht_create(sizeof(Value));
        HT_Iterator it = _ht_iterator(original->members);
        while (_ht_next(&it))
            ht_put(members, it.key, it.value);
        PiClass *copy = (PiClass *)new_class(original->name, original->super, members);
        ht_free(copy->field_names);
        copy->field_names = ht_create(sizeof(uint16_t));
        HT_Iterator fields = _ht_iterator(original->field_names);
        while (_ht_next(&fields))
            ht_put(copy->field_names, fields.key, fields.value);
        copy->slot_count = original->slot_count;
        return NEW_OBJ(add_obj(vm, (Object *)copy));
    }
    if (argc >= 1 && IS_INSTANCE(argv[0]))
    {
        PiInstance *original = AS_INSTANCE(argv[0]);
        PiInstance *copy = (PiInstance *)new_instance(original->_class);
        if (original->_class->slot_count)
            memcpy(copy->slots, original->slots,
                   (size_t)original->_class->slot_count * sizeof(Value));
        if (original->fields)
        {
            copy->fields = ht_create(sizeof(Value));
            HT_Iterator it = _ht_iterator(original->fields);
            while (_ht_next(&it))
                ht_put(copy->fields, it.key, it.value);
            copy->it = _ht_iterator(copy->fields);
        }
        return NEW_OBJ(add_obj(vm, (Object *)copy));
    }

    if (argc < 1 || !IS_MAP(argv[0]))
        vm_error(vm, "[clone] expects a map as the first argument.");

    PiMap *original = AS_MAP(argv[0]);

    HT_Table *new_table = ht_create(sizeof(Value));
    Object *obj = new_map(new_table);
    PiMap *map = (PiMap *)obj;

    // Use iterator to copy all entries
    HT_Iterator it = _ht_iterator(original->table);
    while (_ht_next(&it))
    {
        const char *key = it.key;
        Value *value = (Value *)it.value;
        if (value)
            ht_put(map->table, key, value);
    }

    return NEW_OBJ(add_obj(vm, obj));
}

Value pi_values(VM *vm, int argc, Value *argv)
{
    if (argc >= 1 && IS_CLASS(argv[0]))
    {
        List *list = list_create(sizeof(Value));
        HT_Table *seen = ht_create(sizeof(Value));
        for (PiClass *current = AS_CLASS(argv[0]); current; current = current->super)
        {
            HT_Iterator it = _ht_iterator(current->members);
            while (_ht_next(&it))
            {
                if (ht_has(seen, it.key))
                    continue;
                Value value = *(Value *)it.value;
                ht_put(seen, it.key, &value);
                _list_add(list, &value);
            }
        }
        ht_free(seen);
        return NEW_OBJ(new_list(list));
    }
    if (argc >= 1 && IS_INSTANCE(argv[0]))
    {
        List *list = list_create(sizeof(Value));
        PiInstance *instance = AS_INSTANCE(argv[0]);
        HT_Iterator names = _ht_iterator(instance->_class->field_names);
        while (_ht_next(&names))
        {
            uint16_t slot = *(uint16_t *)names.value;
            _list_add(list, &instance->slots[slot]);
        }
        if (instance->fields)
        {
            HT_Iterator it = _ht_iterator(instance->fields);
            while (_ht_next(&it))
                _list_add(list, (Value *)it.value);
        }
        return NEW_OBJ(new_list(list));
    }

    if (argc < 1 || !IS_MAP(argv[0]))
        vm_error(vm, "[values] expects a map as the first argument.");

    PiMap *map = AS_MAP(argv[0]);
    List *list = list_create(sizeof(Value));

    HT_Iterator it = _ht_iterator(map->table);
    while (_ht_next(&it))
    {
        Value *val = (Value *)it.value;
        if (val)
            _list_add(list, val);
    }

    return NEW_OBJ(new_list(list));
}

Value pi_keys(VM *vm, int argc, Value *argv)
{
    if (argc >= 1 && IS_CLASS(argv[0]))
    {
        List *list = list_create(sizeof(Value));
        HT_Table *seen = ht_create(sizeof(Value));
        for (PiClass *current = AS_CLASS(argv[0]); current; current = current->super)
        {
            HT_Iterator it = _ht_iterator(current->members);
            while (_ht_next(&it))
            {
                if (ht_has(seen, it.key))
                    continue;
                Value marker = NEW_NIL();
                ht_put(seen, it.key, &marker);
                Value key = NEW_OBJ(new_pistring(strdup(it.key)));
                _list_add(list, &key);
            }
        }
        ht_free(seen);
        return NEW_OBJ(new_list(list));
    }
    if (argc >= 1 && IS_INSTANCE(argv[0]))
    {
        List *list = list_create(sizeof(Value));
        PiInstance *instance = AS_INSTANCE(argv[0]);
        HT_Iterator names = _ht_iterator(instance->_class->field_names);
        while (_ht_next(&names))
        {
            Value key = NEW_OBJ(new_pistring(strdup(names.key)));
            _list_add(list, &key);
        }
        if (instance->fields)
        {
            HT_Iterator it = _ht_iterator(instance->fields);
            while (_ht_next(&it))
            {
                Value key = NEW_OBJ(new_pistring(strdup(it.key)));
                _list_add(list, &key);
            }
        }
        return NEW_OBJ(new_list(list));
    }

    if (argc < 1 || !IS_MAP(argv[0]))
        vm_error(vm, "[keys] expects a map as the first argument.");

    PiMap *map = AS_MAP(argv[0]);
    List *list = list_create(sizeof(Value));

    HT_Iterator it = _ht_iterator(map->table);
    while (_ht_next(&it))
    {
        const char *key = it.key;
        _list_add(list, &NEW_OBJ(new_pistring(strdup(key))));
    }

    return NEW_OBJ(new_list(list));
}

Value pi_toString(VM *vm, int argc, Value *argv)
{
    if (argc < 1 || !_is_classType(argv[0]))
        vm_error(vm, "[format] expects an object as the first argument.");

    char *text = as_string(argv[0]);
    return NEW_OBJ(add_obj(vm, new_pistring(text)));
}

Value pi_valueOf(VM *vm, int argc, Value *argv)
{
    if (argc < 1 || !_is_classType(argv[0]))
        vm_error(vm, "[format] expects an object as the first argument.");

    return argv[0];
}

Value pi_hashCode(VM *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_OBJ(argv[0]))
        vm_error(vm, "[hash] expects an object as the first argument.");
    Value target = (argc >= 2 && IS_OBJ(argv[1])) ? argv[1] : argv[0];
    return NEW_NUM((double)AS_OBJ(target)->id);
}

Value pi_extends(VM *vm, int argc, Value *argv)
{
    if (argc >= 3 && IS_CLASS(argv[1]) && IS_CLASS(argv[2]))
    {
        AS_CLASS(argv[2])->super = AS_CLASS(argv[1]);
        return argv[2];
    }
    if (argc >= 2 && IS_CLASS(argv[0]) && IS_CLASS(argv[1]))
    {
        AS_CLASS(argv[1])->super = AS_CLASS(argv[0]);
        return argv[1];
    }

    vm_error(vm, "[extends] map inheritance was removed; use class inheritance.");
    return NEW_NIL();
}

Value pi_equals(VM *vm, int argc, Value *argv)
{
    if (argc == 2)
        return NEW_BOOL(_value_equals(argv[0], argv[1]));

    if (argc >= 3)
        return NEW_BOOL(_value_equals(argv[1], argv[2]));

    vm_error(vm, "[equals] expects either obj.equals(other) or Object.equals(left, right).");
    return NEW_NIL();
}

Value pi_id(VM *vm, int argc, Value *argv)
{
    if (argc != 1 || !IS_OBJ(argv[0]))
    {
        vm_error(vm, "[id] expects exactly one heap object; numbers, booleans, and nil have no object identity.");
        return NEW_NIL();
    }

    char buffer[21]; // Maximum uint64_t decimal digits plus the terminator.
    snprintf(buffer, sizeof(buffer), "%" PRIu64, AS_OBJ(argv[0])->id);
    return NEW_OBJ(add_obj(vm, new_pistring(strdup(buffer))));
}

Value pi_ident(VM *vm, int argc, Value *argv)
{
    Value left;
    Value right;

    if (argc == 2)
    {
        left = argv[0];
        right = argv[1];
    }
    else if (argc >= 3)
    {
        left = argv[1];
        right = argv[2];
    }
    else
        vm_error(vm, "[ident] expects either obj.ident(other) or Object.ident(left, right).");

    if (!IS_OBJ(left) || !IS_OBJ(right))
        return NEW_BOOL(FALSE);

    return NEW_BOOL(AS_OBJ(left) == AS_OBJ(right));
}

Value pi_compare(VM *vm, int argc, Value *argv)
{
    Value left;
    Value right;

    if (argc == 2)
    {
        left = argv[0];
        right = argv[1];
    }
    else if (argc >= 3)
    {
        left = argv[1];
        right = argv[2];
    }
    else
        vm_error(vm, "[compare] expects either obj.compare(other) or Object.compare(left, right).");

    return NEW_NUM(_value_compare(left, right));
}

Value pi_type(VM *vm, int argc, Value *argv)
{
    Value target;

    if (argc == 1)
        target = argv[0];
    else if (argc >= 2)
        target = argv[1];
    else
        vm_error(vm, "[type] expects either obj.type() or Object.type(value).");

    if (IS_MAP(target))
    {
        return NEW_OBJ(add_obj(vm, new_pistring(strdup("map"))));
    }

    return NEW_OBJ(add_obj(vm, new_pistring(strdup(type_name(target)))));
}

// TODO: checkout later
Value pi_name(VM *vm, int argc, Value *argv)
{
    Value target = NEW_NIL();

    if (argc >= 2)
        target = argv[1];
    else if (argc >= 1)
        target = argv[0];

    if (IS_OBJ(target))
    {
        switch (OBJ_TYPE(target))
        {
        case OBJ_CLASS:
            return NEW_OBJ(add_obj(
                vm,
                new_pistring(strdup(AS_CLASS(target)->name))));

        case OBJ_INSTANCE:
            if (AS_INSTANCE(target)->_class)
            {
                return NEW_OBJ(add_obj(
                    vm,
                    new_pistring(strdup(AS_INSTANCE(target)->_class->name))));
            }
            break;

        default:
            break;
        }
    }

    if (IS_MAP(target))
        return NEW_NIL();

    vm_error(vm, "[name] expects obj.name() or Object.name(obj).");
    return NEW_NIL();
}

Value pi_setName(VM *vm, int argc, Value *argv)
{
    if (argc >= 3 && IS_CLASS(argv[1]) && IS_STRING(argv[2]))
    {
        free(AS_CLASS(argv[1])->name);
        AS_CLASS(argv[1])->name = strdup(AS_CSTRING(argv[2]));
        return argv[1];
    }
    if (argc >= 2 && IS_CLASS(argv[0]) && IS_STRING(argv[1]))
    {
        free(AS_CLASS(argv[0])->name);
        AS_CLASS(argv[0])->name = strdup(AS_CSTRING(argv[1]));
        return argv[0];
    }

    vm_error(vm, "[setName] is only supported for classes.");
    return NEW_NIL();
}

Value pi_lock(VM *vm, int argc, Value *argv)
{
    if (argc >= 1 && _is_classType(argv[0]))
        return argv[0];
    if (argc >= 2 && _is_classType(argv[1]))
        return argv[1];

    if (argc >= 1 && IS_MAP(argv[0]))
        return argv[0];
    if (argc >= 2 && IS_MAP(argv[1]))
        return argv[1];
    vm_error(vm, "[lock] expects an object.");
    return NEW_NIL();
}

Value pi_bracketAccess(VM *vm, int argc, Value *argv)
{
    if (argc >= 1 && _is_classType(argv[0]))
        return argv[0];
    if (argc >= 2 && _is_classType(argv[1]))
        return argv[1];

    if (argc >= 1 && IS_MAP(argv[0]))
        return argv[0];
    if (argc >= 2 && IS_MAP(argv[1]))
        return argv[1];
    vm_error(vm, "[bracketAccess] expects an object.");
    return NEW_NIL();
}

Value pi_get(VM *vm, int argc, Value *argv)
{
    if (argc >= 3 && _is_classType(argv[1]))
    {
        Value result;
        if (!_get_classType(argv[1], argv[2], &result))
            return NEW_NIL();
        return result;
    }
    if (argc >= 2 && _is_classType(argv[0]))
    {
        Value result;
        if (!_get_classType(argv[0], argv[1], &result))
            return NEW_NIL();
        return result;
    }

    PiMap *map;
    Value key;

    if (argc >= 2 && IS_MAP(argv[0]))
    {
        map = AS_MAP(argv[0]);
        key = argv[1];
    }
    else if (argc >= 3 && IS_MAP(argv[1]))
    {
        map = AS_MAP(argv[1]);
        key = argv[2];
    }
    else
        vm_error(vm, "[get] expects either obj.get(key) or Object.get(obj, key).");

    return map_get(map, key);
}

Value pi_set(VM *vm, int argc, Value *argv)
{
    if (argc >= 4 && _is_classType(argv[1]))
    {
        _set_classType(argv[1], argv[2], argv[3]);
        return argv[1];
    }
    if (argc >= 3 && _is_classType(argv[0]))
    {
        _set_classType(argv[0], argv[1], argv[2]);
        return argv[0];
    }

    PiMap *map;
    Value key;
    Value value;

    if (argc >= 3 && IS_MAP(argv[0]))
    {
        map = AS_MAP(argv[0]);
        key = argv[1];
        value = argv[2];
    }
    else if (argc >= 4 && IS_MAP(argv[1]))
    {
        map = AS_MAP(argv[1]);
        key = argv[2];
        value = argv[3];
    }
    else
        vm_error(vm, "[set] expects either obj.set(key, value) or Object.set(obj, key, value).");

    map_set(map, key, value);
    return NEW_OBJ((Object *)map);
}

Value pi_has(VM *vm, int argc, Value *argv)
{
    if (argc >= 3 && _is_classType(argv[1]))
        return NEW_BOOL(_has_classType(argv[1], argv[2]));

    if (argc >= 2 && _is_classType(argv[0]))
        return NEW_BOOL(_has_classType(argv[0], argv[1]));

    PiMap *map;
    Value key;

    if (argc >= 2 && IS_MAP(argv[0]))
    {
        map = AS_MAP(argv[0]);
        key = argv[1];
    }
    else if (argc >= 3 && IS_MAP(argv[1]))
    {
        map = AS_MAP(argv[1]);
        key = argv[2];
    }
    else
        vm_error(vm, "[has] expects either obj.has(key) or Object.has(obj, key).");

    return NEW_BOOL(map_has(map, key));
}

Value pi_delete(VM *vm, int argc, Value *argv)
{
    if (argc >= 3 && _is_classType(argv[1]))
        return NEW_BOOL(_delete_classType(argv[1], argv[2]));

    if (argc >= 2 && _is_classType(argv[0]))
        return NEW_BOOL(_delete_classType(argv[0], argv[1]));

    PiMap *map;
    Value key;

    if (argc >= 2 && IS_MAP(argv[0]))
    {
        map = AS_MAP(argv[0]);
        key = argv[1];
    }
    else if (argc >= 3 && IS_MAP(argv[1]))
    {
        map = AS_MAP(argv[1]);
        key = argv[2];
    }
    else
        vm_error(vm, "[delete] expects either obj.delete(key) or Object.delete(obj, key).");

    return NEW_BOOL(map_delete(map, key));
}

Value pi_iterator(VM *vm, int argc, Value *argv)
{
    Value target;

    if (argc == 1)
        target = argv[0];
    else if (argc >= 2)
        target = argv[1];
    else
        vm_error(vm, "[iterator] expects either obj.iterator() or Object.iterator(value).");

    if (!IS_OBJ(target) || !is_iterable(AS_OBJ(target)))
        vm_error(vm, "[iterator] target is not iterable.");

    iter_reset(AS_OBJ(target));
    return target;
}

Value pi_next(VM *vm, int argc, Value *argv)
{
    Value target;

    if (argc == 1)
        target = argv[0];
    else if (argc >= 2)
        target = argv[1];
    else
        vm_error(vm, "[next] expects either obj.next() or Object.next(value).");

    if (!IS_OBJ(target) || !is_iterable(AS_OBJ(target)))
        vm_error(vm, "[next] target is not iterable.");

    if (!iter_hasNext(AS_OBJ(target)))
        return NEW_NIL();

    return iter_next(AS_OBJ(target));
}
