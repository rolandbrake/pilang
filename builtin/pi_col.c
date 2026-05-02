#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <string.h>

#include "pi_col.h"
#include "../list.h"
#include "pi_builtin.h"

/**
 * @brief Compares two values and returns a negative, zero, or positive value.
 *
 * This function compares two values and returns a negative value if the first
 * value is less than the second, zero if they are equal, and a positive value if
 * the first value is greater than the second.
 *
 * @param a The first value to compare.
 * @param b The second value to compare.
 * @return A negative value if the first value is less than the second, zero if
 *         they are equal, and a positive value if the first value is greater
 *         than the second.
 */
static int _compare(const void *a, const void *b)
{
    const Value *va = (const Value *)a;
    const Value *vb = (const Value *)b;

    // Compare two numbers
    if (IS_NUM(*va) && IS_NUM(*vb))
    {
        double diff = AS_NUM(*va) - AS_NUM(*vb);
        return (diff < 0) ? -1 : (diff > 0);
    }
    // Compare two strings
    else if (IS_STRING(*va) && IS_STRING(*vb))
        return strcmp(AS_CSTRING(*va), AS_CSTRING(*vb));

    // Should not reach here due to earlier type check
    return 0;
}

/**
 * @brief Removes the last element from a list or character from a string and returns it.
 *
 * This function takes a list or string as input and removes the last element/character.
 * If the input is a list, the last element is removed and returned.
 * If the input is a string, the last character is removed and returned as a new string.
 * If the input is neither, an error is raised.
 *
 * @param vm The virtual machine instance.
 * @param argc The number of arguments passed to the function.
 * @param argv The arguments provided to the function.
 * @return The last element or character from the list or string.
 */
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

        // Return the last character as a new string
        char ch[2] = {str->chars[len - 1], '\0'};

        // Resize the original string in place (if desired), or just return the popped character
        str->length -= 1;
        str->chars[len - 1] = '\0';

        return NEW_OBJ(new_pistring(strdup(ch)));
    }
    else
        vm_error(vm, "[pop] Argument must be a list or a string.");

    return NEW_NIL();
}

/**
 * @brief Adds elements to the end of a list or characters to the end of a string.
 *
 * This function takes a list or string as the first argument and appends additional
 * elements/characters to it. If the first argument is a list, all subsequent arguments
 * are appended as elements. If it's a string, each argument must be a string of length 1,
 * which will be appended as characters. If the first argument is neither, an error is raised.
 *
 * @param vm The virtual machine instance.
 * @param argc The number of arguments passed to the function.
 * @param argv The arguments provided to the function.
 * @return The new length of the list or string after pushing.
 */
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

            // Append the character
            char ch = _arg->chars[0];
            str->chars = realloc(str->chars, str->length + 2); // +1 for new char, +1 for '\0'
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

/**
 * @brief Checks if a list, string, or map is empty.
 *
 * This function takes one argument and returns true if the list, string, or map is empty,
 * false otherwise. If the input is not a list, string, or map, an error is raised.
 *
 * @param vm The virtual machine instance.
 * @param argc The number of arguments passed to the function.
 * @param argv The arguments provided to the function.
 * @return true if the input is empty, false otherwise.
 */
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
        return NEW_BOOL(set->table->size == 0);
    }
    else
        vm_error(vm, "[empty] Argument must be a list, string, map, or set.");

    return NEW_NIL();
}

/**
 * @brief Inserts a value into a list or string at a specified index.
 *
 * For lists, the value is inserted directly.
 * For strings, only single-character strings can be inserted.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments passed.
 * @param argv Arguments (collection, index, value).
 * @return The modified collection (same reference).
 */
Value pi_insert(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3)
        vm_error(vm, "[insert] expects 3 arguments at least: collection, index, value.");

    Value collection = argv[0];
    Value _index = argv[1];
    Value value = argv[2];

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

        // Allocate new space for +1 character
        int new_len = str->length + strlen(_str);
        char *new_chars = malloc(new_len + 1); // +1 for null terminator

        // Copy before index
        memcpy(new_chars, str->chars, index);
        // Insert new char
        for (int i = 0; i < strlen(_str); i++)
            new_chars[index + i] = _str[i];

        // Copy after index
        memcpy(new_chars + index + strlen(_str), str->chars + index, str->length - index);
        new_chars[new_len] = '\0';

        // Replace original string content
        free(str->chars);
        str->chars = new_chars;
        str->length = new_len;
        free(_str);

        return collection;
    }

    vm_error(vm, "[insert] First argument must be a list or string.");
    return NEW_NIL(); // unreachable
}

/**
 * @brief Removes an element from a list or a character from a string at the given index.
 *
 * For lists: returns the removed element.
 * For strings: returns the removed character as a new string.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments passed (must be 2).
 * @param argv Arguments: collection, index.
 * @return The removed element or character.
 */
Value pi_remove(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[remove] expects two arguments at least: collection and index.");

    Value collection = argv[0];
    Value _index = argv[1];

    int index = as_number(_index);

    // Handle list removal
    if (IS_LIST(collection))
    {
        list_t *list = AS_CLIST(collection);
        return *(Value *)list_remove(list, index); // Assumes list_removeAt returns a pointer to Value
    }

    // Handle string character removal
    else if (IS_STRING(collection))
    {
        PiString *str = AS_STRING(collection);

        index = get_index(index, str->length);

        // Get the character being removed
        char removed = str->chars[index];

        // Create a new string with the character
        char ch[2] = {removed, '\0'};
        Value removed_val = NEW_OBJ(new_pistring(strdup(ch)));

        // Shift string content left to remove character
        memmove(&str->chars[index], &str->chars[index + 1], str->length - index);
        str->length--;
        str->chars[str->length] = '\0'; // Null-terminate

        return removed_val;
    }
    else if (IS_SET(collection))
    {
        PiSet *set = AS_SET(collection);
        char *key = as_string(argv[1]);
        ht_delete(set->table, key);
        free(key);
        return collection;
    }

    vm_error(vm, "[remove] First argument must be a list or string or set.");

    return NEW_NIL();
}

Value pi_slice(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3)
        vm_error(vm, "[slice] expects 3 arguments at least: collection, start, end.");

    Value collection = argv[0];
    Value start = argv[1];
    Value end = argv[2];

    if (!IS_LIST(collection) && !IS_STRING(collection) && !IS_TUPLE(collection))
        vm_error(vm, "[slice] first argument must be a list, tuple, or a string.");

    if (!IS_NUM(start) || !IS_NUM(end))
        vm_error(vm, "[slice] second and third arguments must be numbers.");

    int len = COL_LENGTH(collection);
    int start_index = get_index(as_number(start), len);
    int end_index = get_index(as_number(end), len);

    if (start_index > end_index)
        vm_error(vm, "[slice] start index must be less than or equal to end index.");

    if (IS_LIST(collection))
    {
        PiList *list = AS_LIST(collection);

        list_t *sliced_items = list_create(sizeof(Value));
        for (int i = start_index; i <= end_index; i++)
        {
            Value *item = (Value *)list_getAt(list->items, i);
            list_add(sliced_items, item);
        }

        PiList *result = (PiList *)new_list(sliced_items);
        result->is_numeric = list->is_numeric;
        result->is_matrix = list->is_matrix;

        return NEW_OBJ(result);
    }
    else if (IS_STRING(collection))
    {
        PiString *str = AS_STRING(collection);

        char *sliced_chars = malloc(end_index - start_index + 1);
        for (int i = start_index; i <= end_index; i++)
            sliced_chars[i - start_index] = str->chars[i];

        sliced_chars[end_index - start_index + 1] = '\0';

        return NEW_OBJ(new_pistring(sliced_chars));
    }
    else if (IS_TUPLE(collection))
    {
        PiTuple *tuple = AS_TUPLE(collection);
        list_t *sliced_items = list_create(sizeof(Value));
        for (int i = start_index; i <= end_index; i++)
        {
            Value *item = (Value *)list_getAt(tuple->items, i);
            list_add(sliced_items, item);
        }
        return NEW_OBJ(new_tuple(sliced_items));
    }

    vm_error(vm, "[slice] only works with lists, tuples, or strings.");

    return NEW_NIL(); // unreachable
}

Value pi_len(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[len] expects at least one argument.");

    Value arg = argv[0];
    switch (OBJ_TYPE(arg))
    {
    case OBJ_LIST:
        return NEW_NUM(AS_CLIST(arg)->size);
    case OBJ_STRING:
        return NEW_NUM(AS_STRING(arg)->length);
    case OBJ_MAP:
        return NEW_NUM(AS_CMAP(arg)->size);
    case OBJ_SET:
        return NEW_NUM(AS_SET(arg)->table->size);
    case OBJ_TUPLE:
        return NEW_NUM(AS_TUPLE(arg)->items->size);
    case OBJ_MATRIX:
        return NEW_NUM(AS_MATRIX(arg)->rows);
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
        // range(end)
        if (!IS_NUM(argv[0]))
            vm_error(vm, "[range] Expected a number as the end value.");
        end = AS_NUM(argv[0]);
    }
    else if (argc == 2)
    {
        // range(start, end)
        if (!IS_NUM(argv[0]) || !IS_NUM(argv[1]))
            vm_error(vm, "[range] Expected numbers for start and end values.");

        start = AS_NUM(argv[0]);
        end = AS_NUM(argv[1]);
    }
    else if (argc == 3)
    {
        // range(start, end, step)
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

/**
 * Returns the union of sets.
 */
Value pi_union(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[union] expects at least two sets.");

    for (int i = 0; i < argc; i++)
    {
        if (!IS_SET(argv[i]))
            vm_error(vm, "[union] all arguments must be sets.");
    }

    table_t *table = ht_create(sizeof(Value));

    for (int i = 0; i < argc; i++)
    {
        PiSet *set = AS_SET(argv[i]);
        ht_iter it = ht_iterator(set->table);
        while (ht_next(&it))
        {
            Value *val = (Value *)it.value;
            ht_set(table, it.key, val);
        }
    }

    return NEW_OBJ(new_set(table));
}

/**
 * Returns the intersection of sets.
 */
Value pi_intersection(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[intersection] expects at least two sets.");

    for (int i = 0; i < argc; i++)
    {
        if (!IS_SET(argv[i]))
            vm_error(vm, "[intersection] all arguments must be sets.");
    }

    // Start with copy of first set
    PiSet *first = AS_SET(argv[0]);
    table_t *table = ht_create(sizeof(Value));
    ht_iter it = ht_iterator(first->table);
    while (ht_next(&it))
    {
        Value *val = (Value *)it.value;
        ht_set(table, it.key, val);
    }

    // Intersect with others
    for (int i = 1; i < argc; i++)
    {
        PiSet *set = AS_SET(argv[i]);
        ht_iter it2 = ht_iterator(table);
        while (ht_next(&it2))
        {
            if (ht_get(set->table, it2.key) == NULL)
            {
                ht_delete(table, it2.key);
            }
        }
    }

    return NEW_OBJ(new_set(table));
}

/**
 * Returns the difference of two sets.
 */
Value pi_difference(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2)
        vm_error(vm, "[difference] expects two sets.");

    if (!IS_SET(argv[0]) || !IS_SET(argv[1]))
        vm_error(vm, "[difference] both arguments must be sets.");

    PiSet *s1 = AS_SET(argv[0]);
    PiSet *s2 = AS_SET(argv[1]);

    table_t *table = ht_create(sizeof(Value));
    ht_iter it = ht_iterator(s1->table);
    while (ht_next(&it))
    {
        if (ht_get(s2->table, it.key) == NULL)
        {
            Value *val = (Value *)it.value;
            ht_set(table, it.key, val);
        }
    }

    return NEW_OBJ(new_set(table));
}

/**
 * Returns the symmetric difference of two sets.
 */
Value pi_symmetricDiff(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2)
        vm_error(vm, "[s_diff] expects two sets.");

    if (!IS_SET(argv[0]) || !IS_SET(argv[1]))
        vm_error(vm, "[s_diff] both arguments must be sets.");

    PiSet *s1 = AS_SET(argv[0]);
    PiSet *s2 = AS_SET(argv[1]);

    table_t *table = ht_create(sizeof(Value));

    // Elements in s1 but not s2
    ht_iter it = ht_iterator(s1->table);
    while (ht_next(&it))
    {
        if (ht_get(s2->table, it.key) == NULL)
        {
            Value *val = (Value *)it.value;
            ht_set(table, it.key, val);
        }
    }

    // Elements in s2 but not s1
    it = ht_iterator(s2->table);
    while (ht_next(&it))
    {
        if (ht_get(s1->table, it.key) == NULL)
        {
            Value *val = (Value *)it.value;
            ht_set(table, it.key, val);
        }
    }

    return NEW_OBJ(new_set(table));
}

/**
 * Checks if one set is a subset of another.
 */
Value pi_issubset(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2)
        vm_error(vm, "[issubset] expects two sets.");

    if (!IS_SET(argv[0]) || !IS_SET(argv[1]))
        vm_error(vm, "[issubset] both arguments must be sets.");

    PiSet *s1 = AS_SET(argv[0]);
    PiSet *s2 = AS_SET(argv[1]);

    ht_iter it = ht_iterator(s1->table);
    while (ht_next(&it))
    {
        if (ht_get(s2->table, it.key) == NULL)
            return NEW_BOOL(false);
    }

    return NEW_BOOL(true);
}

/**
 * Checks if one set is a superset of another.
 */
Value pi_issuperset(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2)
        vm_error(vm, "[issuperset] expects two sets.");

    if (!IS_SET(argv[0]) || !IS_SET(argv[1]))
        vm_error(vm, "[issuperset] both arguments must be sets.");

    PiSet *s1 = AS_SET(argv[0]);
    PiSet *s2 = AS_SET(argv[1]);

    ht_iter it = ht_iterator(s2->table);
    while (ht_next(&it))
    {
        if (ht_get(s1->table, it.key) == NULL)
            return NEW_BOOL(false);
    }

    return NEW_BOOL(true);
}

/**
 * Checks if two sets are disjoint.
 */
Value pi_isdisjoint(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2)
        vm_error(vm, "[isdisjoint] expects two sets.");

    if (!IS_SET(argv[0]) || !IS_SET(argv[1]))
        vm_error(vm, "[isdisjoint] both arguments must be sets.");

    PiSet *s1 = AS_SET(argv[0]);
    PiSet *s2 = AS_SET(argv[1]);

    ht_iter it = ht_iterator(s1->table);
    while (ht_next(&it))
    {
        if (ht_get(s2->table, it.key) != NULL)
            return NEW_BOOL(false);
    }

    return NEW_BOOL(true);
}

/**
 * Creates a new set from an iterable, removing duplicates.
 */
Value _pi_set(vm_t *vm, int argc, Value *argv)
{
    if (argc >= 1 && !IS_COLLECTION(argv[0]))
        vm_error(vm, "[set] expects one argument: an iterable.");

    table_t *table = ht_create(sizeof(Value));

    if (argc >= 1)
    {
        Value iterable = argv[0];
        if (IS_LIST(iterable))
        {
            PiList *list = AS_LIST(iterable);
            for (int i = 0; i < list->items->size; i++)
            {
                Value *item = (Value *)list_getAt(list->items, i);
                char *key = as_string(*item);
                ht_put(table, key, item);
                free(key);
            }
        }
        else if (IS_SET(iterable))
        {
            PiSet *set = AS_SET(iterable);
            ht_iter it = ht_iterator(set->table);
            while (ht_next(&it))
            {
                char *key = strdup(it.key);
                Value *value = (Value *)it.value;
                ht_put(table, key, value);
                free(key);
            }
        }
        else if (IS_STRING(iterable))
        {
            PiString *str = AS_STRING(iterable);
            for (int i = 0; i < str->length; i++)
            {
                char ch[2] = {str->chars[i], '\0'};
                char *key = strdup(ch);
                Value value = NEW_OBJ(new_pistring(strdup(ch)));
                ht_put(table, key, &value);
                free(key);
            }
        }
        // else if (IS_TUPLE(iterable))
        // {
        //     PiTuple *tuple = AS_TUPLE(iterable);
        //     for (int i = 0; i < tuple->items->size; i++)
        //     {
        //         Value *item = (Value *)list_getAt(tuple->items, i);
        //         char *key = as_string(*item);
        //         ht_set(table, key, &NIL_VAL());
        //         free(key);
        //     }
        // }
        else
            vm_error(vm, "[set] argument must be a list or set.");
    }

    return NEW_OBJ(new_set(table));
}

/**
 * Adds elements to a set.
 */
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
            // Single element
            char *key_str = as_string(elem);
            ht_put(set->table, key_str, &elem);
            free(key_str);
        }
        else
        {
            // Bulk from iterable
            Object *iterable = AS_OBJ(elem);
            iter_reset(iterable);
            while (iter_hasNext(iterable))
            {
                Value it_elem = iter_next(iterable);
                char *key_str = as_string(it_elem);
                ht_put(set->table, key_str, &it_elem);
                free(key_str);
            }
        }
    }

    return argv[0];
}

/**
 * Removes all elements from a set.
 */
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
        ht_free(set->table);
        set->table = ht_create(sizeof(Value));
    }
    else
        vm_error(vm, "[clear] Argument must be a list, string, or set.");

    return argv[0];
}

/**
 * @brief Retrieves the last element from a list or character from a string without removing it.
 *
 * This function takes a list or string as input and returns the last element/character
 * without modifying the input. If the input is a list, the last element is returned.
 * If the input is a string, the last character is returned as a one-character string.
 * If the input is neither, or is empty, an error is raised.
 *
 * @param vm The virtual machine instance.
 * @param argc The number of arguments passed to the function.
 * @param argv The arguments provided to the function.
 * @return The last element or character.
 */
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

        // Return the last character as a one-character string
        char ch[2] = {str->chars[len - 1], '\0'};
        return NEW_OBJ(new_pistring(strdup(ch)));
    }
    else
        vm_error(vm, "[peek] Argument must be a list or a string.");

    return NEW_NIL();
}

/**
 * @brief Sorts a list in-place in ascending order.
 *
 * This function takes one argument: a list. It sorts the list in-place using the default
 * comparison for supported types (numbers and strings). All elements must be of the same
 * type and either all numbers or all strings. Mixed types or unsupported types will raise an error.
 *
 * @param vm The virtual machine instance.
 * @param argc The number of arguments passed to the function.
 * @param argv The arguments provided to the function.
 * @return nil
 */
Value cl_sort(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[sort] expects one argument.");

    Value arg = argv[0];

    if (!IS_LIST(arg))
        vm_error(vm, "[sort] Argument must be a list.");

    list_t *list = AS_CLIST(arg);

    if (list->size <= 1)
        return NEW_NIL(); // Nothing to sort

    Value first = (*(Value *)list_getAt(list, 0));

    if (!IS_STRING(first) && !IS_NUM(first))
        vm_error(vm, "[sort] List elements must all be numbers or strings.");

    for (int i = 1; i < list->size; i++)
    {
        Value item = (*(Value *)list_getAt(list, i));
        if (item.type != first.type)
            vm_error(vm, "[sort] List elements must all be of the same type.");
    }

    // Comparator for qsort

    qsort(list->data, list->size, sizeof(Value), _compare);

    return NEW_NIL();
}

/**
 * @brief Prepends one or more values to the beginning of a collection.
 *
 * Supports both lists and strings. For lists, any type of value is allowed.
 * For strings, all values must be strings or characters.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments.
 * @param argv Arguments: collection followed by values to prepend.
 * @return The new size of the collection.
 */
Value cl_unshift(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[unshift] expects at least two arguments: collection and values.");

    Value target = argv[0];

    if (IS_LIST(target))
    {
        list_t *list = AS_CLIST(target);

        // Shift items right and insert in reverse order to maintain input order
        for (int i = 1; i < argc; i++)
            list_addFirst(list, &argv[i]); // Prepend each item at index 0

        return NEW_NUM(list->size);
    }
    else if (IS_STRING(target))
    {
        PiString *str = AS_STRING(target);

        // Calculate total new length
        int total_len = str->length;
        for (int i = argc - 1; i >= 1; i--)
        {
            if (!IS_STRING(argv[i]))
                vm_error(vm, "[unshift] All values must be strings when prepending to a string.");
            total_len += AS_STRING(argv[i])->length;
        }

        // Allocate new string
        char *new_chars = malloc(total_len + 1);
        int offset = 0;

        // Copy new items first
        for (int i = argc - 1; i >= 1; i--)
        {
            PiString *s = AS_STRING(argv[i]);
            memcpy(new_chars + offset, s->chars, s->length);
            offset += s->length;
        }

        // Copy old string content
        memcpy(new_chars + offset, str->chars, str->length);
        new_chars[total_len] = '\0';

        // Replace original string content
        free(str->chars);
        str->chars = new_chars;
        str->length = total_len;

        return NEW_NUM(str->length);
    }
    else
        vm_error(vm, "[unshift] First argument must be a list or a string.");

    return NEW_NIL(); // Unreachable
}

/**
 * @brief Appends one or more values to the end of a collection.
 *
 * Supports both lists and strings. For lists, any type of value is allowed.
 * For strings, all values must be strings or characters.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments.
 * @param argv Arguments: collection followed by values to append.
 * @return The new size of the collection.
 */
Value cl_append(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[append] expects at least two arguments: collection and values.");

    Value target = argv[0];

    if (IS_LIST(target))
    {
        list_t *list = AS_CLIST(target);

        for (int i = 1; i < argc; i++)
            list_add(list, &argv[i]); // Append each value to the end

        return NEW_NUM(list->size);
    }
    else if (IS_STRING(target))
    {
        PiString *str = AS_STRING(target);

        // Calculate new total length
        int total_len = str->length;
        for (int i = 1; i < argc; i++)
        {
            if (!IS_STRING(argv[i]))
                vm_error(vm, "[append] All values must be strings when appending to a string.");
            total_len += AS_STRING(argv[i])->length;
        }

        // Allocate new buffer
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

        // Replace old string
        free(str->chars);
        str->chars = new_chars;
        str->length = total_len;

        return NEW_NUM(str->length);
    }
    else
        vm_error(vm, "[append] First argument must be a list or a string.");

    return NEW_NIL(); // Unreachable
}

/**
 * @brief Checks whether a collection contains a given value or key.
 *
 * For lists, checks if the value is present.
 * For strings, checks if the value is a substring.
 * For maps, checks if the value is a key.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments.
 * @param argv Arguments: [collection, value]
 * @return A boolean indicating whether the collection contains the value.
 */
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
        char *key_str = as_string(target);
        bool found = ht_get(set->table, key_str) != NULL;
        free(key_str);
        return NEW_BOOL(found);
    }
    else
        vm_error(vm, "[contains] First argument must be a list, tuple, string, map, or set.");

    return NEW_BOOL(false);
}

/**
 * @brief Returns the index of the first occurrence of a value in a collection.
 *
 * Works for both lists and strings. Returns -1 if the value is not found.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments.
 * @param argv Arguments: [collection, value]
 * @return The index of the value in the collection, or -1 if not found.
 */
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

    return NEW_NUM(-1); // Not found
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

/**
 * @param argc Number of arguments (should be 1).
 * @param argv Arguments: [collection]
 * @return The reversed collection.
 */
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

        return NEW_OBJ(new_list(copy)); // reversed in-place
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

/**
 * @brief Shuffles a list in-place using Fisher–Yates algorithm.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Arguments: [list]
 * @return The shuffled list.
 */
Value cl_shuffle(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "[shuffle] expects one argument at least: a list.");

    if (!IS_LIST(argv[0]))
        vm_error(vm, "[shuffle] argument must be a list.");

    PiList *list = AS_LIST(argv[0]);
    int size = list->items->size;

    // Seed RNG once
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

    return argv[0]; // shuffled in-place
}

/**
 * @brief Returns a deep copy of a list or a string.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 1).
 * @param argv Arguments: [collection]
 * @return A new copy of the collection.
 */
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
            list_add(copied_items, item); // shallow copy of elements
        }

        PiList *result = (PiList *)new_list(copied_items);
        result->is_numeric = orig->is_numeric;
        result->is_matrix = orig->is_matrix;

        return NEW_OBJ(result);
    }
    else if (IS_SET(input))
    {
        PiSet *orig = AS_SET(argv[0]);
        table_t *table = ht_create(sizeof(Value));

        ht_iter it = ht_iterator(orig->table);
        while (ht_next(&it))
        {
            Value *val = (Value *)it.value;
            ht_set(table, it.key, val);
        }

        return NEW_OBJ(new_set(table));
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

    // Check if all iterables are of the same length
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

    // Create a new list to hold the zipped values
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

Value cl_isIterable(vm_t *vm, int argc, Value *argv)
{
    return is_iterable(AS_OBJ(argv[0])) ? NEW_BOOL(true) : NEW_BOOL(false);
}

// Module definition
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
    {"is_iterable", cl_isIterable},
    {"add", cl_add},
    {"clear", cl_clear},
};

DEFINE_BUILTIN_MODULE(module_col, "col", col_functions, NULL);
