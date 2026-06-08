#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pi_obj.h"

static int normalize_compare(int cmp)
{
    return (cmp > 0) - (cmp < 0);
}

static bool is_object_map(vm_t *vm, PiMap *map)
{
    while (map != NULL)
    {
        if (map == vm->object_proto)
            return true;
        map = map->proto;
    }

    return false;
}

static int compare_cstrings(const void *left, const void *right)
{
    const char *const *a = (const char *const *)left;
    const char *const *b = (const char *const *)right;
    return strcmp(*a, *b);
}

static bool map_equals(PiMap *left, PiMap *right);
static int map_compare(PiMap *left, PiMap *right);

static bool value_equals(Value left, Value right)
{
    if (IS_MAP(left) && IS_MAP(right))
        return map_equals(AS_MAP(left), AS_MAP(right));

    return equals(left, right);
}

static int value_compare(Value left, Value right)
{
    if (IS_MAP(left) && IS_MAP(right))
        return map_compare(AS_MAP(left), AS_MAP(right));

    int cmp = compare(left, right);
    if (cmp != ERROR_COMPARE)
        return normalize_compare(cmp);

    char *l_type = type_name(left);
    char *r_type = type_name(right);
    int type_cmp = strcmp(l_type, r_type);
    if (type_cmp != 0)
        return normalize_compare(type_cmp);

    if (IS_OBJ(left) && IS_OBJ(right))
        return normalize_compare((AS_OBJ(left) > AS_OBJ(right)) - (AS_OBJ(left) < AS_OBJ(right)));

    return 0;
}

static bool map_equals(PiMap *left, PiMap *right)
{
    if (left == right)
        return true;

    if (map_size(left) != map_size(right))
        return false;

    if ((left->proto == NULL) != (right->proto == NULL))
        return false;

    if (left->proto != NULL && right->proto != NULL &&
        left->proto != right->proto &&
        !map_equals(left->proto, right->proto))
        return false;

    int size = ht_length(left->table);
    char **left_keys = ht_keys(left->table);

    for (int i = 0; i < size; i++)
    {
        Value *left_value = ht_get(left->table, left_keys[i]);
        Value *right_value = ht_get(right->table, left_keys[i]);

        if (left_value == NULL || right_value == NULL)
            return false;

        if (!value_equals(*left_value, *right_value))
            return false;
    }

    return true;
}

static int map_compare(PiMap *left, PiMap *right)
{
    if (left == right)
        return 0;

    int size_cmp = normalize_compare(map_size(left) - map_size(right));
    if (size_cmp != 0)
        return size_cmp;

    int left_size = ht_length(left->table);
    int right_size = ht_length(right->table);

    char **left_keys = ht_keys(left->table);
    char **right_keys = ht_keys(right->table);

    char **left_sorted = malloc(sizeof(char *) * left_size);
    char **right_sorted = malloc(sizeof(char *) * right_size);

    for (int i = 0; i < left_size; i++)
        left_sorted[i] = left_keys[i];
    for (int i = 0; i < right_size; i++)
        right_sorted[i] = right_keys[i];

    qsort(left_sorted, left_size, sizeof(char *), compare_cstrings);
    qsort(right_sorted, right_size, sizeof(char *), compare_cstrings);

    for (int i = 0; i < left_size; i++)
    {
        int key_cmp = strcmp(left_sorted[i], right_sorted[i]);
        if (key_cmp != 0)
        {
            free(left_sorted);
            free(right_sorted);
            return normalize_compare(key_cmp);
        }

        Value *left_value = ht_get(left->table, left_sorted[i]);
        Value *right_value = ht_get(right->table, right_sorted[i]);
        int value_cmp = value_compare(*left_value, *right_value);
        if (value_cmp != 0)
        {
            free(left_sorted);
            free(right_sorted);
            return value_cmp;
        }
    }

    free(left_sorted);
    free(right_sorted);

    if (left->proto == NULL && right->proto == NULL)
        return 0;
    if (left->proto == NULL)
        return -1;
    if (right->proto == NULL)
        return 1;

    return map_compare(left->proto, right->proto);
}

// Clones a PiMap object, preserving its prototype chain and key-value pairs.
Value pi_clone(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_MAP(argv[0]))
        vm_error(vm, "[clone] expects a map as the first argument.");

    PiMap *original = AS_MAP(argv[0]);

    table_t *new_table = ht_create(sizeof(Value));
    Object *obj = new_map(new_table, original->is_instance);
    PiMap *map = (PiMap *)obj;

    map->proto = original->proto;
    map->super_instance = original->super_instance;
    map->locked = original->locked;
    map->bracket_access = original->bracket_access;
    if (original->intrinsic_name)
        map->intrinsic_name = strdup(original->intrinsic_name);

    char **keys = ht_keys(original->table);
    int size = ht_length(original->table);

    for (int i = 0; i < size; i++)
    {
        char *key = keys[i];
        Value *value = (Value *)ht_get(original->table, key);
        if (value)
            ht_put(map->table, key, value);
    }

    return NEW_OBJ(add_obj(vm, obj));
}

Value pi_values(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_MAP(argv[0]))
        vm_error(vm, "[values] expects a map as the first argument.");

    PiMap *map = AS_MAP(argv[0]);
    char **keys = ht_keys(map->table);
    int size = ht_length(map->table);

    list_t *list = list_create(sizeof(Value));

    for (int i = 0; i < size; i++)
    {
        // char *key = string_get(keys, i);
        char *key = keys[i];
        Value *val = ht_get(map->table, key);
        if (val)
            list_add(list, val); // Copy value to the list
    }

    return NEW_OBJ(new_list(list));
}

Value pi_keys(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_MAP(argv[0]))
        vm_error(vm, "[keys] expects a map as the first argument.");

    PiMap *map = AS_MAP(argv[0]);

    char **keys = ht_keys(map->table);
    int size = ht_length(map->table);

    list_t *list = list_create(sizeof(Value));

    for (int i = 0; i < size; i++)
    {
        // char *key = string_get(keys, i);
        char *key = keys[i];
        list_add(list, &NEW_OBJ(new_pistring(key)));
    }

    return NEW_OBJ(new_list(list));
}

Value pi_toString(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !(IS_MAP(argv[0]) &&
                      AS_MAP(argv[0])->is_instance))
        vm_error(vm, "[format] expects an object as the first argument.");

    char *text = as_string(argv[0]);
    return NEW_OBJ(add_obj(vm, new_pistring(text)));
}

Value pi_valueOf(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !(IS_MAP(argv[0]) &&
                      AS_MAP(argv[0])->is_instance))
        vm_error(vm, "[format] expects an object as the first argument.");

    return argv[0];
}

Value pi_hashCode(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_MAP(argv[0]))
        vm_error(vm, "[hash] expects a map as the first argument.");

    uintptr_t ptr = (uintptr_t)AS_OBJ(argv[0]);
    return NEW_NUM((double)(ptr & 0x1FFFFFFFFFFFFFull));
}

Value pi_extends(vm_t *vm, int argc, Value *argv)
{
    PiMap *parent = NULL;
    PiMap *child = NULL;

    if (argc >= 3 && IS_MAP(argv[1]) && IS_MAP(argv[2]))
    {
        parent = AS_MAP(argv[1]);
        child = AS_MAP(argv[2]);
    }
    else if (argc >= 2 && IS_MAP(argv[0]) && IS_MAP(argv[1]))
    {
        if (AS_MAP(argv[0]) == vm->object_proto)
        {
            parent = AS_MAP(argv[0]);
            child = AS_MAP(argv[1]);
        }
        else
        {
            child = AS_MAP(argv[0]);
            parent = AS_MAP(argv[1]);
        }
    }
    else
        vm_error(vm, "[extends] expects either child.extends(parent) or Object.extends(parent, child).");

    if (parent->is_instance)
        vm_error(vm, "[extends] parent must be a prototype map, not an instance.");

    if (child->is_instance)
        vm_error(vm, "[extends] child must be a map literal or prototype, not an instance.");

    child->proto = parent;
    child->has_compute = child->has_compute || parent->has_compute;
    child->has_rcompute = child->has_rcompute || parent->has_rcompute;
    return NEW_OBJ((Object *)child);
}

Value pi_equals(vm_t *vm, int argc, Value *argv)
{
    if (argc == 2)
        return NEW_BOOL(value_equals(argv[0], argv[1]));

    if (argc >= 3)
        return NEW_BOOL(value_equals(argv[1], argv[2]));

    vm_error(vm, "[equals] expects either obj.equals(other) or Object.equals(left, right).");
    return NEW_NIL();
}

Value pi_ident(vm_t *vm, int argc, Value *argv)
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
        return NEW_BOOL(false);

    return NEW_BOOL(AS_OBJ(left) == AS_OBJ(right));
}

Value pi_compare(vm_t *vm, int argc, Value *argv)
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

    return NEW_NUM(value_compare(left, right));
}

Value pi_type(vm_t *vm, int argc, Value *argv)
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
        const char *kind = is_object_map(vm, AS_MAP(target)) ? "Object" : "map";
        return NEW_OBJ(add_obj(vm, new_pistring(strdup(kind))));
    }

    return NEW_OBJ(add_obj(vm, new_pistring(strdup(type_name(target)))));
}

Value pi_name(vm_t *vm, int argc, Value *argv)
{
    PiMap *map;

    if (argc >= 2 && IS_MAP(argv[1]))
    {
        map = AS_MAP(argv[1]);
        if (map->intrinsic_name == NULL)
            return NEW_NIL();
        return NEW_OBJ(add_obj(vm, new_pistring(strdup(map->intrinsic_name))));
    }
    else if (argc >= 1 && IS_MAP(argv[0]))
    {
        map = AS_MAP(argv[0]);
        if (map->intrinsic_name == NULL)
            return NEW_NIL();
        return NEW_OBJ(add_obj(vm, new_pistring(strdup(map->intrinsic_name))));
    }

    vm_error(vm, "[name] expects obj.name() or Object.name(obj).");
    return NEW_NIL();
}

Value pi_setName(vm_t *vm, int argc, Value *argv)
{
    PiMap *map;
    const char *name;

    if (argc >= 2 && IS_MAP(argv[0]) && IS_STRING(argv[1]))
    {
        map = AS_MAP(argv[0]);
        name = AS_CSTRING(argv[1]);
    }
    else if (argc >= 3 && IS_MAP(argv[1]) && IS_STRING(argv[2]))
    {
        map = AS_MAP(argv[1]);
        name = AS_CSTRING(argv[2]);
    }
    else
    {
        vm_error(vm, "[setName] expects obj.setName(value) or Object.setName(obj, value).");
        return NEW_NIL();
    }

    if (map->intrinsic_name)
        free(map->intrinsic_name);
    map->intrinsic_name = strdup(name);

    if (IS_MAP(argv[0]))
        return argv[0];

    return argv[1];
}

Value pi_lock(vm_t *vm, int argc, Value *argv)
{
    PiMap *map;
    bool locked = true;

    if (argc >= 3 && IS_MAP(argv[1]))
    {
        map = AS_MAP(argv[1]);
        locked = as_bool(argv[2]);
    }
    else if (argc >= 1 && IS_MAP(argv[0]))
    {
        map = AS_MAP(argv[0]);
        if (argc >= 2)
            locked = as_bool(argv[1]);
    }
    else
        vm_error(vm, "[lock] expects obj.lock(value) or Object.lock(obj, value).");

    map->locked = locked;
    return NEW_OBJ((Object *)map);
}

Value pi_bracketAccess(vm_t *vm, int argc, Value *argv)
{
    PiMap *map;
    bool enabled = true;

    if (argc >= 3 && IS_MAP(argv[1]))
    {
        map = AS_MAP(argv[1]);
        enabled = as_bool(argv[2]);
    }
    else if (argc >= 1 && IS_MAP(argv[0]))
    {
        map = AS_MAP(argv[0]);
        if (argc >= 2)
            enabled = as_bool(argv[1]);
    }
    else
        vm_error(vm, "[bracketAccess] expects obj.bracketAccess(value) or Object.bracketAccess(obj, value).");

    map->bracket_access = enabled;
    return NEW_OBJ((Object *)map);
}

Value pi_get(vm_t *vm, int argc, Value *argv)
{
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

Value pi_set(vm_t *vm, int argc, Value *argv)
{
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

    PiMap *owner = map_owner(map, key);
    if (map->locked && owner == NULL)
        vm_error(vm, "[set] cannot add a new key to a locked object.");

    map_set(map, key, value);
    return NEW_OBJ((Object *)map);
}

Value pi_has(vm_t *vm, int argc, Value *argv)
{
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

Value pi_delete(vm_t *vm, int argc, Value *argv)
{
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

    PiMap *owner = map_owner(map, key);
    if (owner && owner->locked)
        vm_error(vm, "[delete] cannot delete from a locked object.");

    return NEW_BOOL(map_delete(map, key));
}

Value pi_iterator(vm_t *vm, int argc, Value *argv)
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

Value pi_next(vm_t *vm, int argc, Value *argv)
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
