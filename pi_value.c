#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "pi_value.h"
#include "pi_object.h"
#include "pi_func.h"
#include "pi_module.h"
#include "pi_vm.h"

static int normalize_compare(int cmp)
{
    return (cmp > 0) - (cmp < 0);
}

static int compare_cstrings(const void *left, const void *right)
{
    const char *const *a = (const char *const *)left;
    const char *const *b = (const char *const *)right;
    return strcmp(*a, *b);
}

#if defined(__GNUC__)
#define PI_STRICT_FP __attribute__((optimize("no-fast-math")))
#else
#define PI_STRICT_FP
#endif

/* The VM deliberately uses NaN as a constant-pool sentinel.  These helpers
 * must retain IEEE NaN semantics even when the application is built -Ofast. */
static PI_STRICT_FP int compare_numbers(double left, double right)
{
    if (fabs(left - right) < 1e-9)
        return 0;
    return (left > right) ? 1 : -1;
}

/* Do not use isnan() here: -ffast-math is permitted to assume it is always
 * false.  The constant pool uses NaN as a sentinel, so inspect IEEE-754 bits
 * directly before doing any floating-point equality arithmetic. */
static inline bool number_isNaN(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000) &&
           (bits & UINT64_C(0x000fffffffffffff)) != 0;
}

static char *dup_cstring(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy == NULL)
        return NULL;

    memcpy(copy, text, length);
    return copy;
}

static char *format_number(double number)
{
    char *text = malloc(64);
    if (!text)
        error("Failed to allocate number string");

    if (isnan(number))
        snprintf(text, 64, "NAN");
    else if (isinf(number))
        snprintf(text, 64, "%s", number < 0 ? "-INF" : "INF");
    else if (number == 0.0)
        snprintf(text, 64, "0");
    else if (fabs(number) >= 1e-15 && fabs(number) < 1e18)
    {
        // %g gives concise, human-friendly decimals.  Only expand it when it
        // would otherwise choose scientific notation for an ordinary number.
        snprintf(text, 64, "%.15g", number);
        if (strchr(text, 'e') || strchr(text, 'E'))
        {
            snprintf(text, 64, "%.15f", number);
            char *end = text + strlen(text) - 1;
            while (end > text && *end == '0')
                *end-- = '\0';
            if (*end == '.')
                *end = '\0';
        }
    }
    else
        snprintf(text, 64, "%.15g", number);

    return text;
}

static int compare_ptrs(const void *left, const void *right)
{
    uintptr_t a = (uintptr_t)left;
    uintptr_t b = (uintptr_t)right;
    return (a > b) - (a < b);
}

static bool string_equals(const char *left, const char *right)
{
    if (left == right)
        return true;
    if (left == NULL || right == NULL)
        return false;
    return strcmp(left, right) == 0;
}

static int compare_strings(const char *left, const char *right)
{
    if (left == right)
        return 0;
    if (left == NULL)
        return -1;
    if (right == NULL)
        return 1;
    return normalize_compare(strcmp(left, right));
}

static bool native_funcEquals(native_func left, native_func right)
{
    return memcmp(&left, &right, sizeof(native_func)) == 0;
}

static int compare_nativeFuncs(native_func left, native_func right)
{
    return normalize_compare(memcmp(&left, &right, sizeof(native_func)));
}

static bool value_listEquals(list_t *left, list_t *right)
{
    if (left == right)
        return true;
    if (left == NULL || right == NULL)
        return false;
    if (LIST_SIZE(left) != LIST_SIZE(right))
        return false;

    for (size_t i = 0; i < LIST_SIZE(left); i++)
    {
        Value left_value = *(Value *)list_getAt(left, i);
        Value right_value = *(Value *)list_getAt(right, i);

        if (!equals(left_value, right_value))
            return false;
    }

    return true;
}

static int value_list_compare(list_t *left, list_t *right)
{
    if (left == right)
        return 0;
    if (left == NULL)
        return -1;
    if (right == NULL)
        return 1;

    size_t left_size = LIST_SIZE(left);
    size_t right_size = LIST_SIZE(right);
    size_t min_size = (left_size < right_size) ? left_size : right_size;

    for (size_t i = 0; i < min_size; i++)
    {
        Value left_value = *(Value *)list_getAt(left, i);
        Value right_value = *(Value *)list_getAt(right, i);
        int cmp = compare(left_value, right_value);
        if (cmp != 0)
            return cmp;
    }

    if (left_size == right_size)
        return 0;

    return (left_size > right_size) ? 1 : -1;
}

static bool list_equals(PiList *left, PiList *right)
{
    if (left == right)
        return true;
    if (left == NULL || right == NULL)
        return false;

    if (LIST_SIZE(left->items) != LIST_SIZE(right->items))
        return false;

    for (size_t i = 0; i < LIST_SIZE(left->items); i++)
    {
        Value item_left = *(Value *)list_getAt(left->items, i);
        Value item_right = *(Value *)list_getAt(right->items, i);

        if (!equals(item_left, item_right))
            return false;
    }

    return true;
}

static int list_compare(PiList *left, PiList *right)
{
    if (left == right)
        return 0;
    if (left == NULL)
        return -1;
    if (right == NULL)
        return 1;

    size_t left_size = LIST_SIZE(left->items);
    size_t right_size = LIST_SIZE(right->items);
    size_t min_size = (left_size < right_size) ? left_size : right_size;

    for (size_t i = 0; i < min_size; i++)
    {
        Value *left_item = list_getAt(left->items, i);
        Value *right_item = list_getAt(right->items, i);
        int cmp = compare(*left_item, *right_item);
        if (cmp != 0)
            return cmp;
    }

    if (left_size != right_size)
        return (left_size > right_size) ? 1 : -1;

    return 0;
}

static bool tensor_equals(PiTensor *left, PiTensor *right)
{
    if (left->type != right->type || left->ndim != right->ndim || left->size != right->size)
        return false;

    for (int i = 0; i < left->ndim; i++)
        if (left->shape[i] != right->shape[i])
            return false;

    for (int i = 0; i < left->size; i++)
        if (fabs(tensor_getFlat(left, i) - tensor_getFlat(right, i)) >= 1e-9)
            return false;

    return true;
}

static int tensor_compare(PiTensor *left, PiTensor *right)
{
    if (left->ndim != right->ndim)
        return (left->ndim > right->ndim) ? 1 : -1;

    for (int i = 0; i < left->ndim; i++)
        if (left->shape[i] != right->shape[i])
            return (left->shape[i] > right->shape[i]) ? 1 : -1;

    for (int i = 0; i < left->size; i++)
    {
        int cmp = compare_numbers(tensor_getFlat(left, i), tensor_getFlat(right, i));
        if (cmp != 0)
            return cmp;
    }

    return 0;
}

static bool map_equals(PiMap *left, PiMap *right)
{
    if (left == right)
        return true;

    if (map_size(left) != map_size(right))
        return false;

    if (left->is_instance != right->is_instance ||
        !string_equals(left->intrinsic_name, right->intrinsic_name))
        return false;

    if ((left->proto == NULL) != (right->proto == NULL))
        return false;

    if (left->proto != NULL && right->proto != NULL &&
        left->proto != right->proto &&
        !map_equals(left->proto, right->proto))
        return false;

    if (left->super_instance != right->super_instance)
        return false;

    // Iterate over left table entries
    ht_iter it = ht_iterator(left->table);
    while (ht_next(&it)) {
        char *key = it.key;
        Value *left_value = (Value*)it.value;
        Value *right_value = ht_get(right->table, key);

        if (right_value == NULL)
            return false;

        if (!equals(*left_value, *right_value))
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

    int instance_cmp = normalize_compare((int)left->is_instance - (int)right->is_instance);
    if (instance_cmp != 0)
        return instance_cmp;

    int name_cmp = compare_strings(left->intrinsic_name, right->intrinsic_name);
    if (name_cmp != 0)
        return name_cmp;

    int left_size = ht_length(left->table);
    int right_size = ht_length(right->table);

    char **left_sorted = malloc(sizeof(char *) * left_size);
    char **right_sorted = malloc(sizeof(char *) * right_size);
    if (!left_sorted || !right_sorted) {
        free(left_sorted);
        free(right_sorted);
        return 0; // fallback
    }

    // Collect keys using iterators
    int idx = 0;
    ht_iter it = ht_iterator(left->table);
    while (ht_next(&it))
        left_sorted[idx++] = it.key;

    idx = 0;
    it = ht_iterator(right->table);
    while (ht_next(&it))
        right_sorted[idx++] = it.key;

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
        int value_cmp = compare(*left_value, *right_value);
        if (value_cmp != 0)
        {
            free(left_sorted);
            free(right_sorted);
            return value_cmp;
        }
    }

    free(left_sorted);
    free(right_sorted);

    if (left->super_instance != right->super_instance)
        return (left->super_instance > right->super_instance) ? 1 : -1;

    if (left->proto == NULL && right->proto == NULL)
        return 0;
    if (left->proto == NULL)
        return -1;
    if (right->proto == NULL)
        return 1;

    return map_compare(left->proto, right->proto);
}

static bool range_equals(PiRange *left, PiRange *right)
{
    return compare_numbers(left->start, right->start) == 0 &&
           compare_numbers(left->end, right->end) == 0 &&
           compare_numbers(left->step, right->step) == 0;
}

static int range_compare(PiRange *left, PiRange *right)
{
    int cmp = compare_numbers(left->start, right->start);
    if (cmp != 0)
        return cmp;

    cmp = compare_numbers(left->end, right->end);
    if (cmp != 0)
        return cmp;

    return compare_numbers(left->step, right->step);
}

static bool code_equals(ObjCode *left, ObjCode *right)
{
    if (left == right)
        return true;

    if (left->hash != right->hash)
        return false;

    if ((left->data == NULL) != (right->data == NULL) ||
        (left->param_names == NULL) != (right->param_names == NULL))
        return false;

    if (left->data != NULL && !value_listEquals(left->data, right->data))
        return false;

    if (!value_listEquals(left->param_names, right->param_names))
        return false;

    return true;
}

static int code_compare(ObjCode *left, ObjCode *right)
{
    if (left->hash != right->hash)
        return (left->hash > right->hash) ? 1 : -1;

    if ((left->data == NULL) != (right->data == NULL))
        return left->data ? 1 : -1;

    if (left->data != NULL)
    {
        int cmp = value_list_compare(left->data, right->data);
        if (cmp != 0)
            return cmp;
    }

    if ((left->param_names == NULL) != (right->param_names == NULL))
        return left->param_names ? 1 : -1;

    if (left->param_names != NULL)
    {
        int cmp = value_list_compare(left->param_names, right->param_names);
        if (cmp != 0)
            return cmp;
    }

    return 0;
}

static bool function_equals(Function *left, Function *right)
{
    if (left == right)
        return true;

    if (!string_equals(left->name, right->name) ||
        left->is_native != right->is_native ||
        left->is_method != right->is_method ||
        left->need_args != right->need_args ||
        left->need_kwargs != right->need_kwargs ||
        left->upvalue_count != right->upvalue_count ||
        left->instance != right->instance ||
        left->owner != right->owner)
        return false;

    if (!native_funcEquals(left->native, right->native))
        return false;

    if (left->body != right->body)
    {
        if (left->body == NULL || right->body == NULL)
            return false;
        if (!code_equals(left->body, right->body))
            return false;
    }

    if (left->params != right->params)
    {
        if (!value_listEquals(left->params, right->params))
            return false;
    }

    if (left->param_names != right->param_names)
    {
        if (!value_listEquals(left->param_names, right->param_names))
            return false;
    }

    for (int i = 0; i < left->upvalue_count; i++)
    {
        UpValue *left_up = left->upvalues ? left->upvalues[i] : NULL;
        UpValue *right_up = right->upvalues ? right->upvalues[i] : NULL;

        if (left_up == right_up)
            continue;
        if (left_up == NULL || right_up == NULL)
            return false;
        if (left_up->index != right_up->index ||
            !equals(left_up->value, right_up->value))
            return false;
    }

    return true;
}

static int function_compare(Function *left, Function *right)
{
    int cmp = compare_strings(left->name, right->name);
    if (cmp != 0)
        return cmp;

    cmp = normalize_compare((int)left->is_native - (int)right->is_native);
    if (cmp != 0)
        return cmp;

    cmp = normalize_compare((int)left->is_method - (int)right->is_method);
    if (cmp != 0)
        return cmp;

    cmp = normalize_compare((int)left->need_args - (int)right->need_args);
    if (cmp != 0)
        return cmp;

    cmp = normalize_compare((int)left->need_kwargs - (int)right->need_kwargs);
    if (cmp != 0)
        return cmp;

    cmp = normalize_compare(left->upvalue_count - right->upvalue_count);
    if (cmp != 0)
        return cmp;

    cmp = compare_nativeFuncs(left->native, right->native);
    if (cmp != 0)
        return cmp;

    if ((left->body == NULL) != (right->body == NULL))
        return left->body ? 1 : -1;
    if (left->body != NULL)
    {
        cmp = code_compare(left->body, right->body);
        if (cmp != 0)
            return cmp;
    }

    if ((left->params == NULL) != (right->params == NULL))
        return left->params ? 1 : -1;
    if (left->params != NULL)
    {
        cmp = value_list_compare(left->params, right->params);
        if (cmp != 0)
            return cmp;
    }

    if ((left->param_names == NULL) != (right->param_names == NULL))
        return left->param_names ? 1 : -1;
    if (left->param_names != NULL)
    {
        cmp = value_list_compare(left->param_names, right->param_names);
        if (cmp != 0)
            return cmp;
    }

    for (int i = 0; i < left->upvalue_count; i++)
    {
        UpValue *left_up = left->upvalues ? left->upvalues[i] : NULL;
        UpValue *right_up = right->upvalues ? right->upvalues[i] : NULL;

        if (left_up == right_up)
            continue;
        if (left_up == NULL)
            return -1;
        if (right_up == NULL)
            return 1;

        cmp = normalize_compare(left_up->index - right_up->index);
        if (cmp != 0)
            return cmp;

        cmp = compare(left_up->value, right_up->value);
        if (cmp != 0)
            return cmp;
    }

    cmp = compare_ptrs(left->instance, right->instance);
    if (cmp != 0)
        return cmp;

    cmp = compare_ptrs(left->owner, right->owner);
    if (cmp != 0)
        return cmp;

    return 0;
}

static bool module_equals(ObjModule *left, ObjModule *right)
{
    if (left == right)
        return true;

    if (!string_equals(left->name, right->name) ||
        !string_equals(left->path, right->path) ||
        left->builtin != right->builtin ||
        left->is_main != right->is_main ||
        left->state != right->state)
        return false;

    if ((left->exports == NULL) != (right->exports == NULL))
        return false;
    if (left->exports != NULL && !map_equals(left->exports, right->exports))
        return false;

    if ((left->constants == NULL) != (right->constants == NULL) ||
        (left->names == NULL) != (right->names == NULL))
        return false;

    if (left->constants != NULL)
    {
        if (!value_listEquals(left->constants, right->constants))
            return false;
    }

    if (left->names != NULL)
    {
        if (!value_listEquals(left->names, right->names))
            return false;
    }

    return true;
}

static int module_compare(ObjModule *left, ObjModule *right)
{
    int cmp = compare_strings(left->name, right->name);
    if (cmp != 0)
        return cmp;

    cmp = compare_strings(left->path, right->path);
    if (cmp != 0)
        return cmp;

    cmp = normalize_compare((int)left->builtin - (int)right->builtin);
    if (cmp != 0)
        return cmp;

    cmp = normalize_compare((int)left->is_main - (int)right->is_main);
    if (cmp != 0)
        return cmp;

    cmp = normalize_compare((int)left->state - (int)right->state);
    if (cmp != 0)
        return cmp;

    if ((left->exports == NULL) != (right->exports == NULL))
        return left->exports ? 1 : -1;
    if (left->exports != NULL)
    {
        cmp = map_compare(left->exports, right->exports);
        if (cmp != 0)
            return cmp;
    }

    if ((left->constants == NULL) != (right->constants == NULL))
        return left->constants ? 1 : -1;
    if (left->constants != NULL)
    {
        cmp = value_list_compare(left->constants, right->constants);
        if (cmp != 0)
            return cmp;
    }

    if ((left->names == NULL) != (right->names == NULL))
        return left->names ? 1 : -1;
    if (left->names != NULL)
    {
        cmp = value_list_compare(left->names, right->names);
        if (cmp != 0)
            return cmp;
    }

    return 0;
}

static bool file_equals(ObjFile *left, ObjFile *right)
{
    return left == right ||
           (left->fp == right->fp &&
            left->closed == right->closed &&
            string_equals(left->mode, right->mode) &&
            string_equals(left->filename, right->filename));
}

static int file_compare(ObjFile *left, ObjFile *right)
{
    int cmp = compare_strings(left->filename, right->filename);
    if (cmp != 0)
        return cmp;

    cmp = compare_strings(left->mode, right->mode);
    if (cmp != 0)
        return cmp;

    cmp = normalize_compare((int)left->closed - (int)right->closed);
    if (cmp != 0)
        return cmp;

    cmp = compare_ptrs(left->fp, right->fp);
    if (cmp != 0)
        return cmp;

    return 0;
}

static bool event_equals(PiEvent *left, PiEvent *right)
{
    return left == right ||
           (string_equals(left->type, right->type) &&
            left->event_type == right->event_type &&
            left->x == right->x &&
            left->y == right->y &&
            left->dx == right->dx &&
            left->dy == right->dy &&
            string_equals(left->key, right->key) &&
            left->button == right->button &&
            left->pressed == right->pressed &&
            left->width == right->width &&
            left->height == right->height);
}

static int event_compare(PiEvent *left, PiEvent *right)
{
    int cmp = compare_strings(left->type, right->type);
    if (cmp != 0)
        return cmp;

    cmp = normalize_compare((int)left->event_type - (int)right->event_type);
    if (cmp != 0)
        return cmp;

    cmp = normalize_compare(left->x - right->x);
    if (cmp != 0)
        return cmp;
    cmp = normalize_compare(left->y - right->y);
    if (cmp != 0)
        return cmp;
    cmp = normalize_compare(left->dx - right->dx);
    if (cmp != 0)
        return cmp;
    cmp = normalize_compare(left->dy - right->dy);
    if (cmp != 0)
        return cmp;

    cmp = compare_strings(left->key, right->key);
    if (cmp != 0)
        return cmp;

    cmp = normalize_compare(left->button - right->button);
    if (cmp != 0)
        return cmp;
    cmp = normalize_compare((int)left->pressed - (int)right->pressed);
    if (cmp != 0)
        return cmp;
    cmp = normalize_compare(left->width - right->width);
    if (cmp != 0)
        return cmp;

    return normalize_compare(left->height - right->height);
}

/**
 * Checks if two values are equal.
 *
 * The comparison for numbers is a tolerance check, as exact floating-point
 * comparisons are not always reliable.
 *
 * The comparison for booleans is a direct comparison.
 *
 * The comparison for NIL is a special case, as all NIL values are considered
 * equal.
 *
 * The comparison for objects is a recursive comparison of their contents.
 *
 * @param left The first value to compare.
 * @param right The second value to compare.
 * @return true if the values are equal, false if they are not.
 */

PI_STRICT_FP bool equals(Value left, Value right)
{
    // If the types are different, they can't be equal.
    if (left.type != right.type)
        return false;

    switch (left.type)
    {
    case VAL_NUM:
        // Use a tolerance for floating-point comparisons.
        if (number_isNaN(left.data.number) || number_isNaN(right.data.number))
            return false;
        return fabs(left.data.number - right.data.number) < 1e-9;

    case VAL_BOOL:
        // Direct comparison for booleans.
        return left.data.boolean == right.data.boolean;

    case VAL_NIL:
        // All NIL values are considered equal.
        return true;

    case VAL_OBJ:
        if (left.data.object->type != right.data.object->type)
            return false;

        switch (left.data.object->type)
        {
        case OBJ_STRING:
        {
            PiString *a = (PiString *)left.data.object;
            PiString *b = (PiString *)right.data.object;
            if (a->length != b->length)
                return false;
            return strcmp(a->chars, b->chars) == 0;
        }

        case OBJ_LIST:
            return list_equals(AS_LIST(left), AS_LIST(right));

        case OBJ_TUPLE:
            return value_listEquals(AS_TUPLE(left)->items, AS_TUPLE(right)->items);

        case OBJ_TENSOR:
            return tensor_equals(AS_TENSOR(left), AS_TENSOR(right));

        case OBJ_MAP:
            return map_equals(AS_MAP(left), AS_MAP(right));

        case OBJ_MODULE:
            return module_equals(AS_MODULE(left), AS_MODULE(right));

        case OBJ_RANGE:
            return range_equals(AS_RANGE(left), AS_RANGE(right));

        case OBJ_FUN:
            return function_equals(AS_FUN(left), AS_FUN(right));

        case OBJ_CODE:
            return code_equals(AS_CODE(left), AS_CODE(right));

        case OBJ_FILE:
            return file_equals(AS_FILE(left), AS_FILE(right));

        case OBJ_EVENT:
            return event_equals(AS_EVENT(left), AS_EVENT(right));

        default:
            // For unsupported object types, fall back to pointer comparison.
            return left.data.object == right.data.object;
        }

    default:
        // Handle unexpected or unsupported types.
        return false;
    }
}

/**
 * Compares two values.
 *
 * @param left the first value to compare
 * @param right the second value to compare
 *
 * @return a negative value if left is less than right,
 *         zero if left is equal to right,
 *         a positive value if left is greater than right
 */

int compare(Value left, Value right)
{
    if (left.type != right.type)
    {
        if (is_numeric(left) && is_numeric(right))
            return compare_numbers(as_number(left), as_number(right));

        return ERROR_COMPARE;
    }

    // If types match, compare normally
    switch (left.type)
    {
    case VAL_NUM:
        return compare_numbers(left.data.number, right.data.number);

    case VAL_BOOL:
        return (int)left.data.boolean - (int)right.data.boolean;

    case VAL_NIL:
        return 0;

    case VAL_OBJ:
        if (OBJ_TYPE(left) == OBJ_STRING)
        {
            PiString *l_str = AS_STRING(left);
            PiString *r_str = AS_STRING(right);
            return strcmp(l_str->chars, r_str->chars);
        }
        else if (OBJ_TYPE(left) == OBJ_LIST)
            return list_compare(AS_LIST(left), AS_LIST(right));
        else if (OBJ_TYPE(left) == OBJ_TUPLE)
            return value_list_compare(AS_TUPLE(left)->items, AS_TUPLE(right)->items);
        else if (OBJ_TYPE(left) == OBJ_TENSOR)
            return tensor_compare(AS_TENSOR(left), AS_TENSOR(right));
        else if (OBJ_TYPE(left) == OBJ_MAP)
            return map_compare(AS_MAP(left), AS_MAP(right));
        else if (OBJ_TYPE(left) == OBJ_RANGE)
            return range_compare(AS_RANGE(left), AS_RANGE(right));
        else if (OBJ_TYPE(left) == OBJ_FUN)
            return function_compare(AS_FUN(left), AS_FUN(right));
        else if (OBJ_TYPE(left) == OBJ_CODE)
            return code_compare(AS_CODE(left), AS_CODE(right));
        else if (OBJ_TYPE(left) == OBJ_MODULE)
            return module_compare(AS_MODULE(left), AS_MODULE(right));
        else if (OBJ_TYPE(left) == OBJ_FILE)
            return file_compare(AS_FILE(left), AS_FILE(right));
        else if (OBJ_TYPE(left) == OBJ_EVENT)
            return event_compare(AS_EVENT(left), AS_EVENT(right));
        else
            return ERROR_COMPARE;

    default:
        return ERROR_COMPARE;
    }
}

/**
 * @brief Escapes special characters in a string.
 *
 * Takes a string with special characters escaped using C-style escape
 * sequences and returns a new string with the escape sequences converted
 * into their corresponding special characters.
 *
 * @param src The string to unescape.
 * @return A new string with the escape sequences converted.
 */
static char *unescape_string(const char *src)
{
    size_t len = strlen(src);
    char *dest = malloc(len + 1); // worst case: same length
    char *out = dest;

    for (const char *p = src; *p; ++p)
    {
        if (*p == '\\')
        {
            p++;
            switch (*p)
            {
            case 'n':
                // Newline character
                *out++ = '\n';
                break;
            case 't':
                // Tab character
                *out++ = '\t';
                break;
            case '\\':
                // Backslash character
                *out++ = '\\';
                break;
            case '"':
                // Double quote character
                *out++ = '"';
                break;
            case 'r':
                // Carriage return character
                *out++ = '\r';
                break;
            default:
                // Unknown escape sequence, treat as raw character
                *out++ = *p;
                break;
            }
        }
        else
            *out++ = *p;
    }

    *out = '\0';
    return dest;
}

/**
 * @brief Creates a new Value from a given token.
 *
 * This function converts a token into a Pi Value based on its type.
 * It supports various token types such as numbers, strings,
 * identifiers, booleans, and nil.
 *
 * @param token The token to convert.
 * @return A Value representing the token.
 */
Value new_value(token_t token)
{
    Value val; // The value to be returned

    switch (token.type)
    {
    case TK_NUM:
        // Convert numeric token to a number value
        val.type = VAL_NUM;
        val.data.number = tk_double(token);
        break;

    case TK_STR:
    {
        // Convert string token to a string object
        const char *raw = tk_string(token);
        char *unescaped = unescape_string(raw); // Function to unescape special characters
        val = NEW_OBJ(new_pistring(dup_cstring(unescaped)));
        free(unescaped); // Free the temporary unescaped string
        break;
    }

    case TK_ID:
        // Convert identifier token to a string object
        val = NEW_OBJ(new_pistring(tk_string(token)));
        break;

    case TK_TRUE:
    case TK_FALSE:
        // Convert boolean token to a boolean value
        val.type = VAL_BOOL;
        val.data.boolean = tk_bool(token);
        break;

    case TK_NIL:
        // Convert nil token to a nil value
        val.type = VAL_NIL;
        break;

    default:
        // Handle unexpected token types
        error("Unexpected token value: %s", tk_string(token));
    }

    return val;
}

/**
 * @brief Converts a value to a number
 *
 * This function attempts to convert a given Pi value to a number.
 * Conversion is based on the type of the value.
 *
 * @param val The value to convert.
 * @return A number representation of the value.
 */
double as_number(Value val)
{
    switch (val.type)
    {
    case VAL_NUM:
        // Numbers are already numbers
        return val.data.number;
    case VAL_BOOL:
        // Boolean values can be converted to 0 or 1
        return val.data.boolean ? 1.0 : 0.0;
    case VAL_NIL:
        // Nil values are equivalent to 0
        return 0.0;
    case VAL_OBJ:
        if (AS_OBJ(val)->type == OBJ_STRING)
        {
            // Attempt to parse the string as a number
            char *endptr;
            PiString *str = AS_STRING(val);
            double result = strtod(str->chars, &endptr);

            // Check if the entire string was successfully converted
            if (endptr == str->chars)
                error("Error: String '%s' cannot be converted to a number.", str->chars);

            return result;
        }
        // Fall through to default if object type is unsupported
        break;
    default:
        error("Cannot convert %s to a number", type_name(val));
    }

    return 0.0;
}

/**
 * @brief Converts a value to a boolean
 *
 * This function attempts to convert a given Pi value to a boolean.
 * Conversion is based on the type of the value.
 *
 * @param val The value to convert.
 * @return A boolean representation of the value.
 */
bool as_bool(Value val)
{
    switch (val.type)
    {
    case VAL_BOOL:
        // Directly return the boolean value
        return val.data.boolean;
    case VAL_NUM:
        // Numbers are true if non-zero
        return val.data.number != 0.0;
    case VAL_NIL:
        // Nil values are false
        return false;
    case VAL_OBJ:
        switch (AS_OBJ(val)->type)
        {
        case OBJ_STRING:
            // Strings are true if non-empty
            return AS_STRING(val)->length > 0;
        case OBJ_LIST:
            // Lists are true if they have items
            return LIST_SIZE(AS_LIST(val)->items) > 0;
        case OBJ_TENSOR:
            return AS_TENSOR(val)->size > 0;
        case OBJ_MAP:
            // Maps are true if they have key-value pairs
            return ht_length(AS_MAP(val)->table) > 0;
        case OBJ_MODULE:
            return AS_MODULE(val)->exports && ht_length(AS_MODULE(val)->exports->table) > 0;
        case OBJ_RANGE:
            // Ranges are true if start and end are different
            return AS_RANGE(val)->start != AS_RANGE(val)->end;
        default:
            // Other object types default to true
            return true;
        }
    default:
        // Error if value cannot be converted
        error("Expected a boolean, but got %s", type_name(val));
    }
}

/**
 * @brief Converts a value to a string
 *
 * The function converts a given Pi value to a string.
 * For numerical values, it converts them to a string using the `%g` format specifier.
 * For boolean values, it returns the string "true" or "false".
 * For nil values, it returns the string "nil".
 * For list and map values, it recursively converts the elements to strings and concatenates them.
 * For functions, it returns a string in the format `<function name: pointer>`.
 * For range values, it returns a string in the format `<start>..=<end>`.
 * For code values, it returns an empty string.
 *
 * @param val The Pi value to be converted
 * @return A string representation of the value
 */
static void append_text(char **result, size_t *buffer_size, size_t *capacity, const char *text)
{
    size_t len = strlen(text);
    size_t required = *buffer_size + len + 1;

    if (required > *capacity)
    {
        size_t new_capacity = *capacity ? *capacity : 16;
        while (new_capacity < required)
        {
            size_t grown = new_capacity < 1024
                               ? new_capacity * 2
                               : new_capacity + new_capacity / 4 + 256;
            new_capacity = grown > new_capacity ? grown : required;
        }

        char *resized = realloc(*result, new_capacity);
        if (!resized)
            error("[as_string] Memory allocation failed.");
        *result = resized;
        *capacity = new_capacity;
    }

    memcpy(*result + *buffer_size, text, len + 1);
    *buffer_size += len;
}

static bool tensor_shouldSummarizeDim(PiTensor *tensor, int dim)
{
    return tensor->shape[dim] > 10;
}

static bool tensor_shouldPrintIndex(PiTensor *tensor, int dim, int index)
{
    if (!tensor_shouldSummarizeDim(tensor, dim))
        return true;
    return index < 3 || index >= tensor->shape[dim] - 3;
}

static void tensor_appendString(char **result, size_t *buffer_size, size_t *capacity, PiTensor *tensor, int dim, int *indices)
{
    if (dim == tensor->ndim)
    {
        Value cell = NEW_NUM(tensor_get(tensor, indices));
        char *item = as_string(cell);
        append_text(result, buffer_size, capacity, item);
        free(item);
        return;
    }

    append_text(result, buffer_size, capacity, "[");

    bool wrote_item = false;
    bool wrote_ellipsis = false;
    for (int i = 0; i < tensor->shape[dim]; i++)
    {
        if (!tensor_shouldPrintIndex(tensor, dim, i))
        {
            if (!wrote_ellipsis)
            {
                if (wrote_item)
                    append_text(result, buffer_size, capacity, ", ");
                append_text(result, buffer_size, capacity, "...");
                wrote_item = true;
                wrote_ellipsis = true;
            }
            continue;
        }

        if (wrote_item)
            append_text(result, buffer_size, capacity, ", ");

        indices[dim] = i;
        tensor_appendString(result, buffer_size, capacity, tensor, dim + 1, indices);
        wrote_item = true;
    }

    append_text(result, buffer_size, capacity, "]");
}

char *as_stringWithFormat(vm_t *vm, Value val)
{
    if (vm != NULL && IS_MAP(val) && AS_MAP(val)->is_instance)
    {
        Value formatted = vm_callMethodNoArgs(vm, val, "format");
        if (!(IS_MAP(formatted) && AS_MAP(formatted) == AS_MAP(val)))
            return as_stringWithFormat(vm, formatted);
    }

    switch (val.type)
    {
    case VAL_NUM:
    {
        return format_number(val.data.number);
    }
    case VAL_BOOL:
        return val.data.boolean ? dup_cstring("true") : dup_cstring("false");
    case VAL_NIL:
        return dup_cstring("nil");
    case VAL_OBJ:
    {
        switch (AS_OBJ(val)->type)
        {
        case OBJ_STRING:
        {
            char *str = AS_STRING(val)->chars;
            return dup_cstring(str); // Create a copy
        }
        case OBJ_LIST:
        {
            list_t *list = as_list(val);
            size_t buffer_size = 1;
            size_t capacity = 2;
            char *result = dup_cstring("[");

            int size = list->size;
            for (size_t i = 0; i < size; i++)
            {
                if (i > 0)
                    append_text(&result, &buffer_size, &capacity, ", ");

                char *item = as_stringWithFormat(vm, *(Value *)list_getAt(list, i));
                append_text(&result, &buffer_size, &capacity, item);
                free(item);
            }

            append_text(&result, &buffer_size, &capacity, "]");

            return result;
        }

        case OBJ_MAP:
        {
            PiMap *map = AS_MAP(val);
            size_t buffer_size = 1;
            size_t capacity = 2;
            char *result = dup_cstring("{");
            bool first = true;

            ht_iter it = ht_iterator(map->table);
            while (ht_next(&it)) {
                char *key = it.key;
                Value *value = (Value*)it.value;
                char *value_str = as_stringWithFormat(vm, *value);

                if (!first)
                    append_text(&result, &buffer_size, &capacity, ", ");
                first = false;

                append_text(&result, &buffer_size, &capacity, key);
                append_text(&result, &buffer_size, &capacity, ": ");
                append_text(&result, &buffer_size, &capacity, value_str);

                free(value_str);
            }

            append_text(&result, &buffer_size, &capacity, "}");
            return result;
        }

        case OBJ_SET:
        {
            PiSet *set = AS_SET(val);
            size_t buffer_size = 1;
            size_t capacity = 2;
            char *result = dup_cstring("{");
            int size = set_size(set);

            if (size == 0)
                return dup_cstring("{}");

            for (int i = 0; i < size; i++)
            {
                char *item = as_stringWithFormat(vm, set_get(set, i));
                if (i > 0)
                    append_text(&result, &buffer_size, &capacity, ", ");
                append_text(&result, &buffer_size, &capacity, item);
                free(item);
            }

            append_text(&result, &buffer_size, &capacity, "}");

            return result;
        }
        case OBJ_TUPLE:
        {
            PiTuple *tuple = AS_TUPLE(val);
            size_t buffer_size = 1;
            size_t capacity = 2;
            char *result = dup_cstring("(");
            int size = LIST_SIZE(tuple->items);

            for (int i = 0; i < size; i++)
            {
                if (i > 0)
                    append_text(&result, &buffer_size, &capacity, ", ");

                Value item = *(Value *)list_getAt(tuple->items, i);
                char *str = as_stringWithFormat(vm, item);
                append_text(&result, &buffer_size, &capacity, str);
                free(str);
            }

            if (size == 1)
                append_text(&result, &buffer_size, &capacity, ",");

            append_text(&result, &buffer_size, &capacity, ")");
            return result;
        }
        case OBJ_TENSOR:
        {
            PiTensor *tensor = AS_TENSOR(val);
            char *result = dup_cstring("");
            size_t buffer_size = 0;
            size_t capacity = 1;
            int indices[MAX_TENSOR_DIMS] = {0};
            tensor_appendString(&result, &buffer_size, &capacity, tensor, 0, indices);
            return result;
        }
        case OBJ_RANGE:
        {
            PiRange *range = AS_RANGE(val);
            char *start = format_number(range->start);
            char *end = format_number(range->end);
            char *step = format_number(range->step);
            size_t buffer_size = strlen(start);
            size_t capacity = buffer_size + 1;
            char *result = dup_cstring(start);

            append_text(&result, &buffer_size, &capacity, "..");
            append_text(&result, &buffer_size, &capacity, end);
            if (range->step != 1.0)
            {
                append_text(&result, &buffer_size, &capacity, ":");
                append_text(&result, &buffer_size, &capacity, step);
            }

            free(start);
            free(end);
            free(step);
            return result;
        }

        case OBJ_FUN:
        {
            Function *fun = AS_FUN(val);

            char *result = (char *)malloc(128);
            sprintf(result, "<FUN: %s>", fun->name);
            return result;
        }
        case OBJ_MODULE:
        {
            ObjModule *module = AS_MODULE(val);
            char *result = (char *)malloc(256);
            snprintf(result, 256, "<module %s>", module->name ? module->name : "<anonymous>");
            return result;
        }
        case OBJ_FILE:
        {
            ObjFile *file = AS_FILE(val);
            char *result = (char *)malloc(256);
            snprintf(result, 256, "<file %s>", file->filename ? file->filename : "<anonymous>");
            return result;
        }
#ifndef __EMSCRIPTEN__
        case OBJ_IMAGE:
        {
            ObjImage *img = AS_IMAGE(val);
            char *result = (char *)malloc(128);
            snprintf(result, 128, "<image %dx%d>", img->surface->w, img->surface->h);
            return result;
        }
#endif
        case OBJ_CODE:
            break;
        }
    }
    default:
        return NULL;
    }

    return NULL;
}

char *as_string(Value val)
{
    return as_stringWithFormat(NULL, val);
}

static uint64_t hash_mix(uint64_t hash, uint64_t value)
{
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    return hash;
}

uint64_t value_hash(Value val)
{
    switch (val.type)
    {
    case VAL_NUM:
    {
        if (isnan(val.data.number))
            return 0x6e616eULL;
        double number = val.data.number == 0.0 ? 0.0 : val.data.number;
        uint64_t bits = 0;
        memcpy(&bits, &number, sizeof(bits));
        return hash_mix(0x6e3aULL, bits);
    }

    case VAL_BOOL:
        return val.data.boolean ? 0x623a31ULL : 0x623a30ULL;

    case VAL_NIL:
        return 0x7a3a6e696cULL;

    case VAL_OBJ:
    {
        Object *obj = AS_OBJ(val);
        if (obj->type == OBJ_STRING)
            return hash_mix(0x733aULL, AS_STRING(val)->hash);

        return hash_mix(0x6f3aULL, obj->id);
    }
    }

    return 0;
}

bool value_keyEquals(Value left, Value right)
{
    if (left.type != right.type)
        return false;

    switch (left.type)
    {
    case VAL_NUM:
        return (isnan(left.data.number) && isnan(right.data.number)) ||
               left.data.number == right.data.number;

    case VAL_BOOL:
        return left.data.boolean == right.data.boolean;

    case VAL_NIL:
        return true;

    case VAL_OBJ:
    {
        Object *left_obj = AS_OBJ(left);
        Object *right_obj = AS_OBJ(right);
        if (left_obj->type != right_obj->type)
            return false;
        if (left_obj->type == OBJ_STRING)
        {
            PiString *a = AS_STRING(left);
            PiString *b = AS_STRING(right);
            return a->length == b->length &&
                   memcmp(a->chars, b->chars, a->length) == 0;
        }
        return left_obj->id == right_obj->id;
    }
    }

    return false;
}

/**
 * @brief Converts a Value to a list_t pointer if the Value is a list.
 * @param val The Value to convert
 * @return A pointer to the list_t structure if the Value is a list, else NULL
 */
list_t *as_list(Value val)
{
    if (val.type == VAL_OBJ && OBJ_TYPE(val) == OBJ_LIST)
        return AS_LIST(val)->items;

    error("Expected a list, but got %s", type_name(val));
}
/**
 * @brief Checks if a Value is numeric.
 *
 * This function determines if the given Value represents a numeric type.
 * It considers numbers, booleans, and nil values as numeric. For string
 * objects, it attempts to parse the string as a number.
 *
 * @param val The Value to check for numeric type.
 * @return True if the Value is numeric, otherwise false.
 */
bool is_numeric(Value val)
{
    // Directly numeric types
    if (val.type == VAL_NUM || val.type == VAL_BOOL || val.type == VAL_NIL)
        return true;

    // Check if the Value is a string object
    if (val.type == VAL_OBJ && OBJ_TYPE(val) == OBJ_STRING)
    {
        char *str_value = AS_STRING(val)->chars;
        if (*str_value == '\0')
            return false;
        char *end_ptr;
        // Attempt to convert the string to a double
        strtod(str_value, &end_ptr);

        // Check if the conversion consumed the entire string
        return *end_ptr == '\0';
    }

    // Non-numeric for all other types
    return false;
}

Value copy_value(Value val)
{
    Value copy;

    switch (val.type)
    {
    case VAL_NUM:
    case VAL_BOOL:
    case VAL_NIL:
        copy = val;
        break;

    case VAL_OBJ:
    {
        Object *obj = AS_OBJ(val);
        copy.type = VAL_OBJ;
        switch (obj->type)
        {
        case OBJ_STRING:
        {
            // Deep copy string
            PiString *original = (PiString *)obj;
            PiString *str = malloc(sizeof(PiString));

            str->object.type = OBJ_STRING;
            str->length = original->length;

            str->chars = malloc(str->length + 1);
            strcpy(str->chars, original->chars);
            copy.data.object = (Object *)str;
            break;
        }

        case OBJ_LIST:
        {
            // Deep copy list
            PiList *original = (PiList *)obj;
            PiList *list = malloc(sizeof(PiList));

            list->object.type = OBJ_LIST;
            list->items = list_create(sizeof(Value)); // PiList contains Value pointers
            list->current = 0;

            for (size_t i = 0; i < LIST_SIZE(original->items); i++)
            {
                // original item
                Value o_item = *(Value *)list_getAt(original->items, i);
                // copied item
                Value c_item = copy_value(o_item);

                list_add(list->items, &c_item);
            }

            copy.data.object = (Object *)list;
            break;
        }

        case OBJ_TENSOR:
        {
            PiTensor *original = (PiTensor *)obj;
            PiTensor *tensor = (PiTensor *)new_tensor(original->ndim, original->shape, original->type);
            for (int i = 0; i < original->size; i++)
                tensor_setFlat(tensor, i, tensor_getFlat(original, i));
            copy.data.object = (Object *)tensor;
            break;
        }

        case OBJ_MAP:
            // PiMap copying not implemented (matches original behavior)
            break;

        default:
            error("Unsupported object type for copy");
        }
        break;
    }

    default:
        error("Unsupported object type for copy");
    }

    return copy;
}
void print_value(Value val, bool is_root)
{
    switch (val.type)
    {
    case VAL_NUM:
    {
        char *text = format_number(val.data.number);
        printf("%s", text);
        free(text);
        break;
    }
    case VAL_BOOL:
        printf("%s", val.data.boolean ? "true" : "false");
        break;
    case VAL_NIL:
        printf("nil");
        break;
    case VAL_OBJ:
        switch (AS_OBJ(val)->type)
        {
        case OBJ_STRING:
            printf("\'%s\'", AS_STRING(val)->chars);
            break;
        case OBJ_LIST:
        {
            int print_limit = 10000; // Set a reasonable limit
            list_t *items = AS_LIST(val)->items;
            int size = items->size;
            printf("[");
            for (int i = 0; i < size; i++)
            {
                print_value(*(Value *)list_getAt(items, i), false);
                if (i < size - 1)
                    printf(", ");
                if (i >= print_limit)
                {
                    printf("... and %d more", size - print_limit);
                    break;
                }
            }
            printf("]");
            break;
        }
        case OBJ_TENSOR:
        {
            char *text = as_string(val);
            printf("%s", text);
            free(text);
            break;
        }
        case OBJ_RANGE:
        {
            PiRange *r = AS_RANGE(val);
            printf("[%f..%f:%f]", r->start, r->end, r->step);
            break;
        }
        case OBJ_SLICE:
        {
            PiSlice *s = AS_SLICE(val);
            printf("[%f:%f:%f]", s->start, s->stop, s->step);
            break;
        }
        case OBJ_FUN:
        {
            Function *fn = AS_FUN(val);
            printf("<%s: %p>", fn->name, (void *)fn);
            break;
        }
        case OBJ_MODULE:
        {
            ObjModule *module = AS_MODULE(val);
            printf("<module %s>", module->name ? module->name : "<anonymous>");
            break;
        }
#ifndef __EMSCRIPTEN__
        case OBJ_IMAGE:
        {
            ObjImage *img = AS_IMAGE(val);
            printf("<image %dx%d>", img->surface->w, img->surface->h);
            break;
        }
#endif
        case OBJ_MAP:
        case OBJ_CODE:
            break;
        }
        break;
    default:
        error("Unknown value type: %s", type_name(val));
    }
    if (is_root)
        printf("\n");
    else
        printf(" ");
}

char *type_name(Value val)
{
    switch (val.type)
    {
    case VAL_NUM:
        return "number";
    case VAL_BOOL:
        return "boolean";
    case VAL_NIL:
        return "nil";
    case VAL_OBJ:
        switch (AS_OBJ(val)->type)
        {
        case OBJ_STRING:
            return "string";
        case OBJ_LIST:
            return "list";
        case OBJ_TENSOR:
            return "tensor";
        case OBJ_MAP:
        {
            PiMap *map = AS_MAP(val);
            if (map->proto != NULL)
            {
                if (map->intrinsic_name != NULL)
                {
                    return map->intrinsic_name;
                }
                else
                    return "object";
            }
            return "map";
        }
        case OBJ_SET:
            return "set";
        case OBJ_TUPLE:
            return "tuple";
        case OBJ_MODULE:
            return "module";
        case OBJ_RANGE:
            return "range";
        case OBJ_SLICE:
            return "slice";
        case OBJ_FUN:
            return "function";
        case OBJ_CODE:
            return "code";
        case OBJ_FILE:
            return "file";
        case OBJ_MODEL3D:
            return "model3d";
        case OBJ_IMAGE:
            return "image";
        case OBJ_CONTEXT:
            return "context";
        case OBJ_CHART:
            return "chart";
        case OBJ_CHART3D:
            return "chart3d";
        case OBJ_EVENT:
            return "event";
        default:
            return "undefined";
        }
    }

    return NULL;
}
