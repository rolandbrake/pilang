#include <stdint.h>
#include <string.h>

#include "pi_obj.h"

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

Value pi_tostring(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_MAP(argv[0]))
        vm_error(vm, "[tostring] expects a map as the first argument.");

    char *text = as_string(argv[0]);
    return NEW_OBJ(add_obj(vm, new_pistring(text)));
}

Value pi_valueof(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_MAP(argv[0]))
        vm_error(vm, "[valueof] expects a map as the first argument.");

    return argv[0];
}

Value pi_hashCode(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_MAP(argv[0]))
        vm_error(vm, "[hash] expects a map as the first argument.");

    uintptr_t ptr = (uintptr_t)AS_OBJ(argv[0]);
    return NEW_NUM((double)(ptr & 0x1FFFFFFFFFFFFFull));
}
