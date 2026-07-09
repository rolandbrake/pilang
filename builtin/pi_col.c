#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <string.h>

#include "pi_col.h"
#include "../pi_list.h"
#include "pi_builtin.h"

static int _compare(const void *a, const void *b)
{
    const Value *va = (const Value *)a;
    const Value *vb = (const Value *)b;

    if (IS_NUM(*va) && IS_NUM(*vb))
    {
        double diff = AS_NUM(*va) - AS_NUM(*vb);
        return (diff < 0) ? -1 : (diff > 0);
    }

    else if (IS_STRING(*va) && IS_STRING(*vb))
        return strcmp(AS_CSTRING(*va), AS_CSTRING(*vb));

    return 0;
}

Value pi_pop(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[pop] expects at least one argument.");

    Value arg = argv[0];

    if (IS_LIST(arg))
    {
        list_t *list = AS_CLIST(arg);
        if (list->size == 0)
            vm_error(vm, "[pop] Cannot pop from an empty list.");
        return *(Value *)list_pop(list);
    }
    else if (IS_STRING(arg))
    {

        PiString *str = ((PiString *)AS_OBJ(arg));
        int len = str->length;
        if (len == 0)
            vm_error(vm, "[pop] Cannot pop from an empty string.");

        char ch[2] = {str->chars[len - 1], '\0'};

        // Mutates the original string and returns the removed character.
        str->length -= 1;
        str->chars[len - 1] = '\0';

        return NEW_OBJ(new_pistring(strdup(ch)));
    }
    else
        vm_error(vm, "[pop] Argument must be a list or a string.");

    return NEW_NIL();
}

Value pi_push(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[push] expects at least two arguments.");

    Value target = argv[0];

    if (IS_LIST(target))
    {
        list_t *list = AS_CLIST(target);
        for (int i = 1; i < argc; i++)
            list_add(list, &argv[i]);

        return NEW_NUM(list->size);
    }
    else if (IS_STRING(target))
    {
        PiString *str = (PiString *)AS_OBJ(target);

        for (int i = 1; i < argc; i++)
        {
            if (!IS_STRING(argv[i]))
                vm_error(vm, "[push] When pushing to a string, all values must be strings.");

            PiString *_arg = (PiString *)AS_OBJ(argv[i]);
            if (_arg->length != 1)
                vm_error(vm, "[push] Only single-character strings can be pushed to a string.");

            char ch = _arg->chars[0];
            str->chars = realloc(str->chars, str->length + 2);
            str->chars[str->length] = ch;
            str->length += 1;
            str->chars[str->length] = '\0';
        }

        return NEW_NUM(str->length);
    }
    else
        vm_error(vm, "[push] First argument must be a list or a string.");

    return NEW_NIL();
}

Value pi_empty(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[empty] expects at least one argument.");

    Value arg = argv[0];

    if (IS_LIST(arg))
    {
        list_t *list = AS_CLIST(arg);
        return NEW_BOOL(list->size == 0);
    }
    else if (IS_STRING(arg))
    {
        PiString *str = (PiString *)AS_OBJ(arg);
        return NEW_BOOL(str->length == 0);
    }
    else if (IS_MAP(arg))
    {
        PiMap *map = AS_MAP(arg);
        return NEW_BOOL(map->table->size == 0);
    }
    else if (IS_SET(arg))
    {
        PiSet *set = AS_SET(arg);
        return NEW_BOOL(set_size(set) == 0);
    }
    else
        vm_error(vm, "[empty] Argument must be a list, string, map, or set.");

    return NEW_NIL();
}

Value pi_insert(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3)
        vm_error(vm, "[insert] expects 3 arguments at least: collection, index, value.");

    Value collection = argv[0];
    Value _index = argv[1];
    Value value = argv[2];

    if (!IS_NUM(_index))
        vm_error(vm, "[insert] index must be a number.");

    int index = as_number(_index);

    if (IS_LIST(collection))
    {
        list_t *list = AS_CLIST(collection);
        if (index < 0 || index > list->size)
            vm_error(vm, "[insert] Index out of bounds for list.");

        list_addAt(list, index, &value);
        return collection;
    }
    else if (IS_STRING(collection))
    {
        PiString *str = AS_STRING(collection);

        char *_str = as_string(value);

        index = get_index(index, str->length);

        int new_len = str->length + strlen(_str);
        char *new_chars = malloc(new_len + 1);

        memcpy(new_chars, str->chars, index);

        for (int i = 0; i < strlen(_str); i++)
            new_chars[index + i] = _str[i];

        memcpy(new_chars + index + strlen(_str), str->chars + index, str->length - index);
        new_chars[new_len] = '\0';

        free(str->chars);
        str->chars = new_chars;
        str->length = new_len;
        free(_str);

        return collection;
    }

    vm_error(vm, "[insert] First argument must be a list or string.");
    return NEW_NIL();
}

Value pi_remove(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[remove] expects two arguments at least: collection and index.");

    Value collection = argv[0];
    Value _index = argv[1];

    if ((IS_LIST(collection) || IS_STRING(collection)) && !IS_NUM(_index))
        vm_error(vm, "[remove] index must be a number.");

    if (IS_LIST(collection))
    {
        int index = as_number(_index);
        list_t *list = AS_CLIST(collection);
        return *(Value *)list_remove(list, index);
    }

    else if (IS_STRING(collection))
    {
        int index = as_number(_index);
        PiString *str = AS_STRING(collection);

        index = get_index(index, str->length);

        char removed = str->chars[index];

        char ch[2] = {removed, '\0'};
        Value removed_val = NEW_OBJ(new_pistring(strdup(ch)));

        // Move the terminator too, so the buffer stays null-terminated.
        memmove(&str->chars[index], &str->chars[index + 1], str->length - index);
        str->length--;
        str->chars[str->length] = '\0';

        return removed_val;
    }
    else if (IS_MAP(collection))
    {
        PiMap *map = AS_MAP(collection);
        char *key = as_string(argv[1]);
        if (ht_delete(map->table, key))
            map_dirty(map);
        free(key);
        return collection;
    }
    else if (IS_SET(collection))
    {
        PiSet *set = AS_SET(collection);
        set_remove(set, argv[1]);
        return collection;
    }

    vm_error(vm, "[remove] First argument must be a list or string or set.");

    return NEW_NIL();
}

Value pi_slice(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[slice] expects at least 2 arguments: start, end [, step].");

    Value start = argv[0];
    Value end = argv[1];
    Value step = argc >= 3 ? argv[2] : NEW_NUM(1.0);

    if (!IS_NUM(start) || !IS_NUM(end))
        vm_error(vm, "[slice] start and end must be numbers.");

    if (!IS_NUM(step))
        vm_error(vm, "[slice] step must be a number.");

    if (as_number(step) == 0.0)
        vm_error(vm, "[slice] step cannot be zero.");

    return NEW_OBJ(add_obj(vm, new_slice(as_number(start),
                                         as_number(end),
                                         as_number(step))));
}

Value pi_len(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[len] expects at least one argument.");

    Value arg = argv[0];
    if (!IS_OBJ(arg))
        vm_error(vm, "[len] argument must be a list, string, map, set, tuple, or tensor.");

    switch (OBJ_TYPE(arg))
    {
    case OBJ_LIST:
        return NEW_NUM(AS_CLIST(arg)->size);
    case OBJ_STRING:
        return NEW_NUM(AS_STRING(arg)->length);
    case OBJ_MAP:
        return NEW_NUM(AS_CMAP(arg)->size);
    case OBJ_SET:
        return NEW_NUM(set_size(AS_SET(arg)));
    case OBJ_TUPLE:
        return NEW_NUM(AS_TUPLE(arg)->items->size);
    case OBJ_TENSOR:
        return NEW_NUM(AS_TENSOR(arg)->ndim == 0 ? 0 : AS_TENSOR(arg)->shape[0]);
    default:
        return NEW_NIL();
    }
}

Value pi_range(vm_t *vm, int argc, Value *argv)
{
    double start = 0;
    double end = 0;
    double step = 1;

    if (argc == 1)
    {

        if (!IS_NUM(argv[0]))
            vm_error(vm, "[range] Expected a number as the end value.");
        end = AS_NUM(argv[0]);
    }
    else if (argc == 2)
    {

        if (!IS_NUM(argv[0]) || !IS_NUM(argv[1]))
            vm_error(vm, "[range] Expected numbers for start and end values.");

        start = AS_NUM(argv[0]);
        end = AS_NUM(argv[1]);
    }
    else if (argc >= 3)
    {

        if (!IS_NUM(argv[0]) || !IS_NUM(argv[1]) || !IS_NUM(argv[2]))
            vm_error(vm, "[range] Expected numbers for start, end, and step values.");

        start = AS_NUM(argv[0]);
        end = AS_NUM(argv[1]);
        step = AS_NUM(argv[2]);
        if (step == 0)
            vm_error(vm, "[range] Step cannot be zero.");
    }
    else
        vm_error(vm, "[range] Expected 1 to 3 arguments.");

    Object *range_obj = new_range(start, end, step);
    return NEW_OBJ(range_obj);
}

Value pi_peek(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[peek] expects at least one argument.");

    Value arg = argv[0];

    if (IS_LIST(arg))
    {
        list_t *list = AS_CLIST(arg);
        if (list->size == 0)
            vm_error(vm, "[peek] Cannot peek from an empty list.");
        return *(Value *)list_getAt(list, list->size - 1);
    }
    else if (IS_STRING(arg))
    {
        PiString *str = (PiString *)AS_OBJ(arg);
        int len = str->length;
        if (len == 0)
            vm_error(vm, "[peek] Cannot peek from an empty string.");

        char ch[2] = {str->chars[len - 1], '\0'};
        return NEW_OBJ(new_pistring(strdup(ch)));
    }

    vm_error(vm, "[peek] Argument must be a list or a string.");
    return NEW_NIL();
}

Value pi_union(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[union] expects at least two sets.");

    for (int i = 0; i < argc; i++)
    {
        if (!IS_SET(argv[i]))
            vm_error(vm, "[union] all arguments must be sets.");
    }

    PiSet *result = (PiSet *)new_set();

    for (int i = 0; i < argc; i++)
    {
        PiSet *set = AS_SET(argv[i]);
        for (int j = 0; j < set_size(set); j++)
            set_add(result, set_get(set, j));
    }

    return NEW_OBJ((Object *)result);
}

Value pi_intersection(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[intersection] expects at least two sets.");

    for (int i = 0; i < argc; i++)
    {
        if (!IS_SET(argv[i]))
            vm_error(vm, "[intersection] all arguments must be sets.");
    }

    PiSet *first = AS_SET(argv[0]);
    PiSet *result = (PiSet *)new_set();

    for (int i = 0; i < set_size(first); i++)
    {
        Value value = set_get(first, i);
        bool present = true;
        for (int i = 1; i < argc; i++)
        {
            if (!set_has(AS_SET(argv[i]), value))
            {
                present = false;
                break;
            }
        }
        if (present)
            set_add(result, value);
    }

    return NEW_OBJ((Object *)result);
}

Value pi_difference(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[difference] expects two sets.");

    if (!IS_SET(argv[0]) || !IS_SET(argv[1]))
        vm_error(vm, "[difference] both arguments must be sets.");

    PiSet *s1 = AS_SET(argv[0]);
    PiSet *s2 = AS_SET(argv[1]);

    PiSet *result = (PiSet *)new_set();
    for (int i = 0; i < set_size(s1); i++)
    {
        Value value = set_get(s1, i);
        if (!set_has(s2, value))
            set_add(result, value);
    }

    return NEW_OBJ((Object *)result);
}

Value pi_symmetricDiff(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[s_diff] expects two sets.");

    if (!IS_SET(argv[0]) || !IS_SET(argv[1]))
        vm_error(vm, "[s_diff] both arguments must be sets.");

    PiSet *s1 = AS_SET(argv[0]);
    PiSet *s2 = AS_SET(argv[1]);

    PiSet *result = (PiSet *)new_set();

    // Elements in s1 but not s2
    for (int i = 0; i < set_size(s1); i++)
    {
        Value value = set_get(s1, i);
        if (!set_has(s2, value))
            set_add(result, value);
    }

    // Elements in s2 but not s1
    for (int i = 0; i < set_size(s2); i++)
    {
        Value value = set_get(s2, i);
        if (!set_has(s1, value))
            set_add(result, value);
    }

    return NEW_OBJ((Object *)result);
}

Value pi_issubset(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[issubset] expects two sets.");

    if (!IS_SET(argv[0]) || !IS_SET(argv[1]))
        vm_error(vm, "[issubset] both arguments must be sets.");

    PiSet *s1 = AS_SET(argv[0]);
    PiSet *s2 = AS_SET(argv[1]);

    for (int i = 0; i < set_size(s1); i++)
    {
        if (!set_has(s2, set_get(s1, i)))
            return NEW_BOOL(false);
    }

    return NEW_BOOL(true);
}

Value pi_issuperset(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[issuperset] expects two sets.");

    if (!IS_SET(argv[0]) || !IS_SET(argv[1]))
        vm_error(vm, "[issuperset] both arguments must be sets.");

    PiSet *s1 = AS_SET(argv[0]);
    PiSet *s2 = AS_SET(argv[1]);

    for (int i = 0; i < set_size(s2); i++)
    {
        if (!set_has(s1, set_get(s2, i)))
            return NEW_BOOL(false);
    }

    return NEW_BOOL(true);
}

Value pi_isdisjoint(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[isdisjoint] expects two sets.");

    if (!IS_SET(argv[0]) || !IS_SET(argv[1]))
        vm_error(vm, "[isdisjoint] both arguments must be sets.");

    PiSet *s1 = AS_SET(argv[0]);
    PiSet *s2 = AS_SET(argv[1]);

    for (int i = 0; i < set_size(s1); i++)
    {
        if (set_has(s2, set_get(s1, i)))
            return NEW_BOOL(false);
    }

    return NEW_BOOL(true);
}

Value _pi_set(vm_t *vm, int argc, Value *argv)
{
    if (argc >= 1 && !IS_COLLECTION(argv[0]))
        vm_error(vm, "[set] expects one argument: an iterable.");

    PiSet *result = (PiSet *)new_set();

    if (argc >= 1)
    {
        Value iterable = argv[0];
        if (IS_LIST(iterable))
        {
            PiList *list = AS_LIST(iterable);
            for (int i = 0; i < list->items->size; i++)
            {
                Value *item = (Value *)list_getAt(list->items, i);
                set_add(result, *item);
            }
        }
        else if (IS_SET(iterable))
        {
            PiSet *set = AS_SET(iterable);
            for (int i = 0; i < set_size(set); i++)
                set_add(result, set_get(set, i));
        }
        else if (IS_STRING(iterable))
        {
            PiString *str = AS_STRING(iterable);
            for (int i = 0; i < str->length; i++)
            {
                char ch[2] = {str->chars[i], '\0'};
                Value value = NEW_OBJ(add_obj(vm, new_pistring(strdup(ch))));
                set_add(result, value);
            }
        }
        else if (IS_TUPLE(iterable))
        {
            PiTuple *tuple = AS_TUPLE(iterable);
            for (int i = 0; i < tuple->items->size; i++)
            {
                Value *item = (Value *)list_getAt(tuple->items, i);
                set_add(result, *item);
            }
        }
        else
            vm_error(vm, "[set] argument must be a list, set, string, or tuple.");
    }

    return NEW_OBJ((Object *)result);
}

Value pi_copy(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "[copy] expects one argument.");

    Value value = argv[0];

    if (!IS_OBJ(value))
        return value;

    switch (OBJ_TYPE(value))
    {
    case OBJ_STRING:
        return NEW_OBJ(add_obj(vm, new_pistring(strdup(AS_STRING(value)->chars))));

    case OBJ_LIST:
    {
        PiList *original = AS_LIST(value);
        list_t *items = list_create(sizeof(Value));
        for (int i = 0; i < LIST_SIZE(original->items); i++)
        {
            Value item = *(Value *)list_getAt(original->items, i);
            Value copied = pi_copy(vm, 1, &item);
            list_add(items, &copied);
        }

        PiList *copy = (PiList *)add_obj(vm, new_list(items));
        copy->is_numeric = original->is_numeric;
        copy->is_matrix = original->is_matrix;
        copy->rows = original->rows;
        copy->cols = original->cols;
        return NEW_OBJ((Object *)copy);
    }

    case OBJ_TUPLE:
    {
        PiTuple *original = AS_TUPLE(value);
        list_t *items = list_create(sizeof(Value));
        for (int i = 0; i < LIST_SIZE(original->items); i++)
        {
            Value item = *(Value *)list_getAt(original->items, i);
            Value copied = pi_copy(vm, 1, &item);
            list_add(items, &copied);
        }

        return NEW_OBJ(add_obj(vm, new_tuple(items)));
    }

    case OBJ_MAP:
    {
        PiMap *original = AS_MAP(value);
        table_t *table = ht_create(sizeof(Value));
        Object *obj = add_obj(vm, new_map(table, original->is_instance));
        PiMap *copy = (PiMap *)obj;

        copy->proto = original->proto;

        copy->super_instance = original->super_instance;
        copy->locked = original->locked;
        copy->bracket_access = original->bracket_access;

        copy->has_compute = original->has_compute;
        copy->has_rcompute = original->has_rcompute;

        if (original->intrinsic_name)
            copy->intrinsic_name = strdup(original->intrinsic_name);

        ht_iter it = ht_iterator(original->table);
        while (ht_next(&it))
        {
            Value *item = it.value;
            if (!item)
                continue;

            Value copied = pi_copy(vm, 1, item);
            ht_put(copy->table, it.key, &copied);
        }

        return NEW_OBJ(obj);
    }

    case OBJ_SET:
    {
        PiSet *result = (PiSet *)new_set();
        PiSet *set = AS_SET(value);

        for (int i = 0; i < set_size(set); i++)
        {
            Value item = set_get(set, i);
            Value copied = pi_copy(vm, 1, &item);
            set_add(result, copied);
        }

        return NEW_OBJ(add_obj(vm, (Object *)result));
    }

    case OBJ_TENSOR:
    {
        PiTensor *original = AS_TENSOR(value);
        PiTensor *copy = (PiTensor *)add_obj(vm, new_tensor(original->ndim, original->shape, original->type));
        for (int i = 0; i < original->size; i++)
            tensor_setFlat(copy, i, tensor_getFlat(original, i));
        return NEW_OBJ((Object *)copy);
    }

    case OBJ_RANGE:
    {
        PiRange *range = AS_RANGE(value);
        return NEW_OBJ(add_obj(vm, new_range(range->start, range->end, range->step)));
    }

    case OBJ_SLICE:
    {
        PiSlice *slice = AS_SLICE(value);
        return NEW_OBJ(add_obj(vm, new_slice(slice->start, slice->stop, slice->step)));
    }

    default:
        return value;
    }
}

Value cl_add(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[add] expects at least two arguments: set and elements.");

    if (!IS_SET(argv[0]))
        vm_error(vm, "[add] first argument must be a set.");

    PiSet *set = AS_SET(argv[0]);

    for (int i = 1; i < argc; i++)
    {
        Value elem = argv[i];
        if (!IS_COLLECTION(elem))
        {

            set_add(set, elem);
        }
        else
        {

            Object *iterable = AS_OBJ(elem);
            iter_reset(iterable);
            while (iter_hasNext(iterable))
            {
                Value it_elem = iter_next(iterable);
                set_add(set, it_elem);
            }
        }
    }

    return argv[0];
}

Value cl_clear(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "[clear] expects at least one argument");

    if (IS_LIST(argv[0]))
    {
        list_t *list = AS_CLIST(argv[0]);
        list_clear(list);
    }
    else if (IS_STRING(argv[0]))
    {
        PiString *str = AS_STRING(argv[0]);
        str->length = 0;
        str->chars[0] = '\0';
    }
    else if (IS_SET(argv[0]))
    {

        PiSet *set = AS_SET(argv[0]);
        set_clear(set);
    }
    else
        vm_error(vm, "[clear] Argument must be a list, string, or set.");

    return argv[0];
}

Value cl_peek(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[peek] expects at least one argument.");

    Value arg = argv[0];

    if (IS_LIST(arg))
    {
        list_t *list = AS_CLIST(arg);
        if (list->size == 0)
            vm_error(vm, "[peek] Cannot peek from an empty list.");
        return *(Value *)list_getAt(list, list->size - 1);
    }
    else if (IS_STRING(arg))
    {
        PiString *str = (PiString *)AS_OBJ(arg);
        int len = str->length;
        if (len == 0)
            vm_error(vm, "[peek] Cannot peek from an empty string.");

        char ch[2] = {str->chars[len - 1], '\0'};
        return NEW_OBJ(new_pistring(strdup(ch)));
    }
    else
        vm_error(vm, "[peek] Argument must be a list or a string.");

    return NEW_NIL();
}

Value cl_sort(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[sort] expects one argument.");

    Value arg = argv[0];

    if (!IS_LIST(arg))
        vm_error(vm, "[sort] Argument must be a list.");

    list_t *list = AS_CLIST(arg);

    if (list->size <= 1)
        return NEW_NIL();

    Value first = (*(Value *)list_getAt(list, 0));

    if (!IS_STRING(first) && !IS_NUM(first))
        vm_error(vm, "[sort] List elements must all be numbers or strings.");

    for (int i = 1; i < list->size; i++)
    {
        Value item = (*(Value *)list_getAt(list, i));
        if (item.type != first.type)
            vm_error(vm, "[sort] List elements must all be of the same type.");
    }

    qsort(list->data, list->size, sizeof(Value), _compare);

    return NEW_NIL();
}

Value cl_unshift(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[unshift] expects at least two arguments: collection and values.");

    Value target = argv[0];

    if (IS_LIST(target))
    {
        list_t *list = AS_CLIST(target);

        for (int i = 1; i < argc; i++)
            list_addFirst(list, &argv[i]);

        return NEW_NUM(list->size);
    }
    else if (IS_STRING(target))
    {
        PiString *str = AS_STRING(target);

        int total_len = str->length;
        for (int i = argc - 1; i >= 1; i--)
        {
            if (!IS_STRING(argv[i]))
                vm_error(vm, "[unshift] All values must be strings when prepending to a string.");
            total_len += AS_STRING(argv[i])->length;
        }

        char *new_chars = malloc(total_len + 1);
        int offset = 0;

        for (int i = argc - 1; i >= 1; i--)
        {
            PiString *s = AS_STRING(argv[i]);
            memcpy(new_chars + offset, s->chars, s->length);
            offset += s->length;
        }

        memcpy(new_chars + offset, str->chars, str->length);
        new_chars[total_len] = '\0';

        free(str->chars);
        str->chars = new_chars;
        str->length = total_len;

        return NEW_NUM(str->length);
    }
    else
        vm_error(vm, "[unshift] First argument must be a list or a string.");

    return NEW_NIL(); // Unreachable
}

Value cl_append(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[append] expects at least two arguments: collection and values.");

    Value target = argv[0];

    if (IS_LIST(target))
    {
        list_t *list = AS_CLIST(target);

        for (int i = 1; i < argc; i++)
            list_add(list, &argv[i]);

        return NEW_NUM(list->size);
    }
    else if (IS_STRING(target))
    {
        PiString *str = AS_STRING(target);

        int total_len = str->length;
        for (int i = 1; i < argc; i++)
        {
            if (!IS_STRING(argv[i]))
                vm_error(vm, "[append] All values must be strings when appending to a string.");
            total_len += AS_STRING(argv[i])->length;
        }

        char *new_chars = malloc(total_len + 1);
        memcpy(new_chars, str->chars, str->length);

        int offset = str->length;
        for (int i = 1; i < argc; i++)
        {
            PiString *s = AS_STRING(argv[i]);
            memcpy(new_chars + offset, s->chars, s->length);
            offset += s->length;
        }

        new_chars[total_len] = '\0';

        free(str->chars);
        str->chars = new_chars;
        str->length = total_len;

        return NEW_NUM(str->length);
    }
    else
        vm_error(vm, "[append] First argument must be a list or a string.");

    return NEW_NIL(); // Unreachable
}

Value cl_contains(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[contains] expects two arguments at least: a collection and a value.");

    Value collection = argv[0];
    Value target = argv[1];

    if (IS_LIST(collection))
    {
        PiList *list = AS_LIST(collection);
        for (int i = 0; i < list->items->size; i++)
        {
            Value item = *(Value *)list_getAt(list->items, i);
            if (equals(item, target))
                return NEW_BOOL(true);
        }
    }
    else if (IS_STRING(collection))
    {
        if (!IS_STRING(target))
            vm_error(vm, "[contains] When searching a string, the value must also be a string.");

        PiString *str = AS_STRING(collection);
        PiString *substr = AS_STRING(target);

        if (substr->length == 0 || substr->length > str->length)
            return NEW_BOOL(false);

        for (int i = 0; i <= str->length - substr->length; i++)
        {
            if (strncmp(&str->chars[i], substr->chars, substr->length) == 0)
                return NEW_BOOL(true);
        }
    }
    else if (IS_TUPLE(collection))
    {
        PiTuple *tuple = AS_TUPLE(collection);
        for (int i = 0; i < tuple->items->size; i++)
        {
            Value item = *(Value *)list_getAt(tuple->items, i);
            if (equals(item, target))
                return NEW_BOOL(true);
        }
    }
    else if (IS_MAP(collection))
    {
        PiMap *map = AS_MAP(collection);
        return NEW_BOOL(map_has(map, target));
    }
    else if (IS_SET(collection))
    {
        PiSet *set = AS_SET(collection);
        bool found = set_has(set, target);
        return NEW_BOOL(found);
    }
    else
        vm_error(vm, "[contains] First argument must be a list, tuple, string, map, or set.");

    return NEW_BOOL(false);
}

Value cl_indexOf(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[index_of] expects at least two arguments: a collection and a value.");

    Value collection = argv[0];
    Value target = argv[1];

    if (IS_LIST(collection))
    {
        PiList *list = AS_LIST(collection);
        for (int i = 0; i < list->items->size; i++)
        {
            Value item = *(Value *)list_getAt(list->items, i);
            if (equals(item, target))
                return NEW_NUM(i);
        }
    }
    else if (IS_STRING(collection))
    {
        if (!IS_STRING(target))
            vm_error(vm, "[index_of] When searching a string, the target must also be a string.");

        PiString *str = AS_STRING(collection);
        PiString *substr = AS_STRING(target);

        if (substr->length == 0 || substr->length > str->length)
            return NEW_NUM(-1);

        for (int i = 0; i <= str->length - substr->length; i++)
            if (strncmp(&str->chars[i], substr->chars, substr->length) == 0)
                return NEW_NUM(i);
    }
    else if (IS_TUPLE(collection))
    {
        PiTuple *tuple = AS_TUPLE(collection);
        for (int i = 0; i < tuple->items->size; i++)
        {
            Value item = *(Value *)list_getAt(tuple->items, i);
            if (equals(item, target))
                return NEW_NUM(i);
        }
    }
    else
        vm_error(vm, "[index_of] First argument must be a list, tuple, or string.");

    return NEW_NUM(-1);
}

Value cl_count(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[count] expects at least two arguments: a collection and a value.");

    Value collection = argv[0];
    Value target = argv[1];
    int count = 0;

    if (IS_LIST(collection))
    {
        PiList *list = AS_LIST(collection);
        for (int i = 0; i < list->items->size; i++)
        {
            Value item = *(Value *)list_getAt(list->items, i);
            if (equals(item, target))
                count++;
        }
    }
    else if (IS_TUPLE(collection))
    {
        PiTuple *tuple = AS_TUPLE(collection);
        for (int i = 0; i < tuple->items->size; i++)
        {
            Value item = *(Value *)list_getAt(tuple->items, i);
            if (equals(item, target))
                count++;
        }
    }
    else if (IS_STRING(collection))
    {
        if (!IS_STRING(target))
            vm_error(vm, "[count] When counting in a string, the target must also be a string.");

        PiString *str = AS_STRING(collection);
        PiString *substr = AS_STRING(target);

        if (substr->length == 0)
            return NEW_NUM(0);

        for (int i = 0; i <= str->length - substr->length; i++)
            if (strncmp(&str->chars[i], substr->chars, substr->length) == 0)
                count++;
    }
    else if (IS_SET(collection))
    {
        Value args[2] = {collection, target};
        Value contains = cl_contains(vm, 2, args);
        return NEW_NUM(as_bool(contains) ? 1 : 0);
    }
    else
        vm_error(vm, "[count] First argument must be a list, tuple, string, or set.");

    return NEW_NUM(count);
}

Value cl_concat(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[concat] expects at least two arguments.");

    Value left = argv[0];
    Value right = argv[1];

    if (IS_LIST(left) && IS_LIST(right))
    {
        PiList *l_list = AS_LIST(left);
        PiList *r_list = AS_LIST(right);
        list_t *result = list_copy(l_list->items);
        list_addAll(result, r_list->items);
        return NEW_OBJ(add_obj(vm, new_list(result)));
    }
    else if (IS_TUPLE(left) && IS_TUPLE(right))
    {
        PiTuple *l_tuple = AS_TUPLE(left);
        PiTuple *r_tuple = AS_TUPLE(right);
        list_t *result = list_copy(l_tuple->items);
        list_addAll(result, r_tuple->items);
        return NEW_OBJ(add_obj(vm, new_tuple(result)));
    }
    else if (IS_STRING(left) && IS_STRING(right))
    {
        const char *l_str = AS_STRING(left)->chars;
        const char *r_str = AS_STRING(right)->chars;
        size_t l_len = AS_STRING(left)->length;
        size_t r_len = AS_STRING(right)->length;
        char *res = malloc(l_len + r_len + 1);
        if (!res)
            vm_error(vm, "Memory allocation failed.");
        memcpy(res, l_str, l_len);
        memcpy(res + l_len, r_str, r_len + 1);
        return NEW_OBJ(add_obj(vm, new_pistring(res)));
    }

    vm_error(vm, "[concat] expects both arguments to be lists, tuples, or strings.");
    return NEW_NIL();
}

Value pi_tuple(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
    {
        list_t *items = list_create(sizeof(Value));
        return NEW_OBJ(add_obj(vm, new_tuple(items)));
    }

    if (argc == 1)
    {
        Value arg = argv[0];
        if (IS_LIST(arg))
        {
            PiList *list = AS_LIST(arg);
            return NEW_OBJ(add_obj(vm, new_tuple(list_copy(list->items))));
        }
        else if (IS_TUPLE(arg))
        {
            PiTuple *tuple = AS_TUPLE(arg);
            return NEW_OBJ(add_obj(vm, new_tuple(list_copy(tuple->items))));
        }
        else if (IS_STRING(arg))
        {
            PiString *str = AS_STRING(arg);
            list_t *items = list_create(sizeof(Value));
            for (int i = 0; i < str->length; i++)
            {
                char *ch = malloc(2);
                ch[0] = str->chars[i];
                ch[1] = '\0';
                Value value = NEW_OBJ(add_obj(vm, new_pistring(ch)));
                list_add(items, &value);
            }
            return NEW_OBJ(add_obj(vm, new_tuple(items)));
        }
    }

    list_t *items = list_create(sizeof(Value));
    for (int i = 0; i < argc; i++)
        list_add(items, &argv[i]);

    return NEW_OBJ(add_obj(vm, new_tuple(items)));
}

Value cl_repeat(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[repeat] expects two arguments: a collection and a repeat count.");

    Value collection = argv[0];
    if (!IS_NUM(argv[1]))
        vm_error(vm, "[repeat] repeat count must be a number.");

    int times = (int)as_number(argv[1]);
    if (times < 0)
        times = 0;

    if (IS_LIST(collection))
    {
        PiList *list = AS_LIST(collection);
        list_t *result = list_create(sizeof(Value));
        for (int i = 0; i < times; i++)
            list_addAll(result, list->items);

        Object *o = add_obj(vm, new_list(result));
        return NEW_OBJ(o);
    }
    else if (IS_TUPLE(collection))
    {
        PiTuple *tuple = AS_TUPLE(collection);
        list_t *result = list_create(sizeof(Value));
        for (int i = 0; i < times; i++)
            list_addAll(result, tuple->items);
        Object *o = add_obj(vm, new_tuple(result));
        return NEW_OBJ(o);
    }
    else if (IS_STRING(collection))
    {
        const char *str = AS_STRING(collection)->chars;
        size_t len = AS_STRING(collection)->length;
        size_t total = len * (size_t)times;
        char *res = malloc(total + 1);
        if (!res)
            vm_error(vm, "Memory allocation failed.");
        for (int i = 0; i < times; i++)
            memcpy(res + i * len, str, len);
        res[total] = '\0';
        return NEW_OBJ(add_obj(vm, new_pistring(res)));
    }

    vm_error(vm, "[repeat] expects a list, tuple, or string as the first argument.");
    return NEW_NIL();
}

Value cl_reverse(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "[reverse] expects one argument at least: a list or a string.");

    Value input = argv[0];

    if (IS_LIST(input))
    {
        list_t *list = AS_CLIST(input);
        int size = list->size;

        list_t *copy = list_copy(list);
        for (int i = 0; i < size / 2; i++)
        {
            Value *a = (Value *)list_getAt(copy, i);
            Value *b = (Value *)list_getAt(copy, size - i - 1);
            Value tmp = *a;
            *a = *b;
            *b = tmp;
        }

        return NEW_OBJ(new_list(copy));
    }
    else if (IS_STRING(input))
    {
        PiString *str = AS_STRING(input);
        int len = str->length;
        char *reversed = malloc(len + 1);

        for (int i = 0; i < len; i++)
            reversed[i] = str->chars[len - i - 1];

        reversed[len] = '\0';
        return NEW_OBJ(new_pistring(reversed));
    }
    else
    {
        vm_error(vm, "[reverse] argument must be a list or a string.");
    }

    return NEW_NIL();
}

Value cl_flat(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "[flat] expects one argument: a list.");

    if (!IS_LIST(argv[0]))
        vm_error(vm, "[flat] argument must be a list.");

    PiList *input = AS_LIST(argv[0]);
    list_t *result = list_create(sizeof(Value));

    for (int i = 0; i < input->items->size; i++)
    {
        Value item = *(Value *)list_getAt(input->items, i);
        if (IS_LIST(item))
        {
            PiList *nested = AS_LIST(item);
            for (int j = 0; j < nested->items->size; j++)
                list_add(result, list_getAt(nested->items, j));
        }
        else
            list_add(result, &item);
    }

    return NEW_OBJ(add_obj(vm, new_list(result)));
}

Value cl_shuffle(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "[shuffle] expects one argument at least: a list.");

    if (!IS_LIST(argv[0]))
        vm_error(vm, "[shuffle] argument must be a list.");

    PiList *list = AS_LIST(argv[0]);
    int size = list->items->size;

    // Seed RNG once
    // Seed once so repeated shuffle calls do not reset randomness.
    static bool seeded = false;
    if (!seeded)
    {
        srand((unsigned int)time(NULL));
        seeded = true;
    }

    for (int i = size - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        Value *a = (Value *)list_getAt(list->items, i);
        Value *b = (Value *)list_getAt(list->items, j);
        Value tmp = *a;
        *a = *b;
        *b = tmp;
    }

    return argv[0];
}

Value cl_copy(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "[copy] expects one argument at least!.");

    Value input = argv[0];

    if (IS_STRING(input))
    {
        PiString *str = AS_STRING(input);
        return NEW_OBJ(new_pistring(strdup(str->chars)));
    }
    else if (IS_LIST(input))
    {
        PiList *orig = AS_LIST(input);
        list_t *copied_items = list_create(sizeof(Value));

        for (int i = 0; i < orig->items->size; i++)
        {
            Value *item = (Value *)list_getAt(orig->items, i);
            // Collection method copy is shallow; pi_copy() above is the deep-copy builtin.
            list_add(copied_items, item);
        }

        PiList *result = (PiList *)new_list(copied_items);
        result->is_numeric = orig->is_numeric;
        result->is_matrix = orig->is_matrix;

        return NEW_OBJ(result);
    }
    else if (IS_SET(input))
    {
        PiSet *orig = AS_SET(argv[0]);
        PiSet *result = (PiSet *)new_set();

        for (int i = 0; i < set_size(orig); i++)
            set_add(result, set_get(orig, i));

        return NEW_OBJ((Object *)result);
    }

    vm_error(vm, "[copy] only works with lists or strings or sets.");
    return NEW_NIL();
}

Value cl_zip(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[zip] expects at least two arguments: iterables.");

    Value *iterables = argv;
    int numIterables = argc;

    int minLength = INT_MAX;
    for (int i = 0; i < numIterables; i++)
    {
        if (!IS_LIST(iterables[i]) && !IS_STRING(iterables[i]))
            vm_error(vm, "[zip] all iterables must be lists, strings, or maps.");

        int length = 0;
        if (IS_LIST(iterables[i]))
            length = PILIST_SIZE(iterables[i]);

        else if (IS_STRING(iterables[i]))
            length = PISTR_SIZE(iterables[i]);

        if (minLength > length)
            minLength = length;
    }

    list_t *zippedItems = list_create(sizeof(Value));
    for (int i = 0; i < minLength; i++)
    {
        list_t *zippedItemsIndex = list_create(sizeof(Value));

        for (int j = 0; j < numIterables; j++)
        {
            Value value = NEW_NIL();

            if (IS_LIST(iterables[j]))
            {
                Value *item = (Value *)list_getAt(AS_CLIST(iterables[j]), i);
                value = *item;
            }
            else if (IS_STRING(iterables[j]))
            {
                PiString *str = AS_STRING(iterables[j]);
                char ch[2] = {str->chars[i], '\0'};
                value = NEW_OBJ(new_pistring(strdup(ch)));
            }

            list_add(zippedItemsIndex, &value);
        }

        Value listVal = NEW_OBJ(new_list(zippedItemsIndex));
        list_add(zippedItems, &listVal);
    }

    return NEW_OBJ(new_list(zippedItems));
}

Value cl_join(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || argc > 2)
        vm_error(vm, "[join] expects a list, tuple, or string, and an optional separator.");

    Value collection = argv[0];
    const char *separator = "";
    if (argc == 2)
    {
        if (!IS_STRING(argv[1]))
            vm_error(vm, "[join] separator must be a string.");
        separator = AS_CSTRING(argv[1]);
    }

    if (IS_STRING(collection))
        return collection;

    if (!IS_LIST(collection) && !IS_TUPLE(collection))
        vm_error(vm, "[join] first argument must be a list, tuple, or string.");

    list_t *items = IS_LIST(collection) ? AS_LIST(collection)->items : AS_TUPLE(collection)->items;
    int size = LIST_SIZE(items);
    size_t sep_len = strlen(separator);
    size_t result_len = 0;
    char **parts = NULL;

    if (size > 0)
    {
        parts = malloc(sizeof(char *) * size);
        if (!parts)
            vm_error(vm, "Memory allocation failed.");
    }

    for (int i = 0; i < size; i++)
    {
        Value item = *(Value *)list_getAt(items, i);
        parts[i] = as_string(item);
        result_len += strlen(parts[i]);
        if (i > 0)
            result_len += sep_len;
    }

    char *result = malloc(result_len + 1);
    if (!result)
        vm_error(vm, "Memory allocation failed.");

    char *cursor = result;
    for (int i = 0; i < size; i++)
    {
        if (i > 0 && sep_len > 0)
        {
            memcpy(cursor, separator, sep_len);
            cursor += sep_len;
        }

        size_t part_len = strlen(parts[i]);
        memcpy(cursor, parts[i], part_len);
        cursor += part_len;
        free(parts[i]);
    }

    free(parts);
    *cursor = '\0';

    return NEW_OBJ(add_obj(vm, new_pistring(result)));
}

Value pi_join(vm_t *vm, int argc, Value *argv)
{
    return cl_join(vm, argc, argv);
}

Value cl_isIterable(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "[is_iterable] expects at least one argument.");

    return NEW_BOOL(IS_OBJ(argv[0]) && is_iterable(AS_OBJ(argv[0])));
}

// Functions exported by the col module.
static BuiltinFunc col_functions[] = {
    {"peek", cl_peek},
    {"sort", cl_sort},
    {"unshift", cl_unshift},
    {"append", cl_append},
    {"contains", cl_contains},
    {"indexOf", cl_indexOf},
    {"reverse", cl_reverse},
    {"shuffle", cl_shuffle},
    {"copy", cl_copy},
    {"zip", cl_zip},
    {"join", cl_join},
    {"is_iterable", cl_isIterable},
    {"add", cl_add},
    {"clear", cl_clear},
};

DEFINE_BUILTIN_MODULE(module_col, "col", col_functions, NULL);
