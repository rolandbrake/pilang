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

#define SB_INIT_CAP 64

typedef struct
{
    char *buf;
    size_t len;
    size_t cap;
} sb_t;

/* Initialise with an optional string literal seed (pass "" for empty). */
static void sb_init(sb_t *sb, const char *seed)
{
    sb->len = strlen(seed);
    sb->cap = sb->len < SB_INIT_CAP ? SB_INIT_CAP : sb->len * 2;
    sb->buf = malloc(sb->cap);
    if (!sb->buf)
        error("[sb_init] Out of memory.");
    memcpy(sb->buf, seed, sb->len + 1); /* include NUL */
}

static void sb_append(sb_t *sb, const char *text)
{
    size_t tlen = strlen(text);
    size_t needed = sb->len + tlen + 1;

    if (needed > sb->cap)
    {
        /* Grow by at least 2× to keep amortised O(1) appends. */
        size_t new_cap = sb->cap * 2;
        if (new_cap < needed)
            new_cap = needed;
        char *resized = realloc(sb->buf, new_cap);
        if (!resized)
            error("[sb_append] Out of memory.");
        sb->buf = resized;
        sb->cap = new_cap;
    }

    memcpy(sb->buf + sb->len, text, tlen + 1);
    sb->len += tlen;
}

/* Transfer ownership of the internal buffer to the caller. */
static char *sb_finish(sb_t *sb)
{
    return sb->buf; /* caller is responsible for free() */
}

static char *format_number(double number)
{
    char *text = malloc(64);
    if (!text)
        error("Failed to allocate number string");

    if (isnan(number))
    {
        memcpy(text, "NAN", 4);
        return text;
    }
    if (isinf(number))
    {
        if (number < 0)
            memcpy(text, "-INF", 5);
        else
            memcpy(text, "INF", 4);
        return text;
    }
    if (number == 0.0)
    {
        memcpy(text, "0", 2);
        return text;
    }

    double abs_n = fabs(number);
    if (abs_n >= 1e-15 && abs_n < 1e18)
    {
        int n = snprintf(text, 64, "%.15g", number);
        /* %.15g sometimes chooses scientific notation for ordinary values. */
        if (n > 0 && (memchr(text, 'e', n) || memchr(text, 'E', n)))
        {
            n = snprintf(text, 64, "%.15f", number);
            /* Strip trailing zeros after the decimal point. */
            char *dot = memchr(text, '.', n);
            if (dot)
            {
                char *end = text + n - 1;
                while (end > dot && *end == '0')
                    *end-- = '\0';
                if (*end == '.')
                    *end = '\0';
            }
        }
    }
    else
    {
        snprintf(text, 64, "%.15g", number);
    }

    return text;
}

/* Maps the character after '\' to its replacement, 0 = unknown. */
static const char escape_table[256] = {
    ['n'] = '\n',
    ['t'] = '\t',
    ['r'] = '\r',
    ['\\'] = '\\',
    ['"'] = '"',
};

static char *unescape_string(const char *src)
{
    size_t len = strlen(src);
    char *dest = malloc(len + 1); /* worst case: same length */
    char *out = dest;

    for (const char *p = src; *p; ++p)
    {
        if (*p == '\\')
        {
            unsigned char next = (unsigned char)*++p;
            char mapped = escape_table[next];
            *out++ = mapped ? mapped : (char)next; /* unknown → raw char */
        }
        else
        {
            *out++ = *p;
        }
    }

    *out = '\0';
    return dest;
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
        if (!equals(*(Value *)list_getAt(left, i),
                    *(Value *)list_getAt(right, i)))
            return false;
    }
    return true;
}

static int value_listCompare(list_t *left, list_t *right)
{
    if (left == right)
        return 0;
    if (left == NULL)
        return -1;
    if (right == NULL)
        return 1;

    size_t ls = LIST_SIZE(left), rs = LIST_SIZE(right);
    size_t min = ls < rs ? ls : rs;

    for (size_t i = 0; i < min; i++)
    {
        int cmp = compare(*(Value *)list_getAt(left, i),
                          *(Value *)list_getAt(right, i));
        if (cmp != 0)
            return cmp;
    }
    return ls == rs ? 0 : (ls > rs ? 1 : -1);
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
        if (!equals(*(Value *)list_getAt(left->items, i),
                    *(Value *)list_getAt(right->items, i)))
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

    size_t ls = LIST_SIZE(left->items), rs = LIST_SIZE(right->items);
    size_t min = ls < rs ? ls : rs;

    for (size_t i = 0; i < min; i++)
    {
        int cmp = compare(*(Value *)list_getAt(left->items, i),
                          *(Value *)list_getAt(right->items, i));
        if (cmp != 0)
            return cmp;
    }
    return ls == rs ? 0 : (ls > rs ? 1 : -1);
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
        return left->ndim > right->ndim ? 1 : -1;
    for (int i = 0; i < left->ndim; i++)
        if (left->shape[i] != right->shape[i])
            return left->shape[i] > right->shape[i] ? 1 : -1;
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
    if (left->proto != NULL && left->proto != right->proto &&
        !map_equals(left->proto, right->proto))
        return false;
    if (left->super_instance != right->super_instance)
        return false;

    ht_iter it = ht_iterator(left->table);
    while (ht_next(&it))
    {
        Value *rv = ht_get(right->table, it.key);
        if (rv == NULL || !equals(*(Value *)it.value, *rv))
            return false;
    }
    return true;
}

static int map_compare(PiMap *left, PiMap *right)
{
    if (left == right)
        return 0;

    int cmp = normalize_compare(map_size(left) - map_size(right));
    if (cmp != 0)
        return cmp;

    cmp = normalize_compare((int)left->is_instance - (int)right->is_instance);
    if (cmp != 0)
        return cmp;

    cmp = compare_strings(left->intrinsic_name, right->intrinsic_name);
    if (cmp != 0)
        return cmp;

    int sz = ht_length(left->table);
    char **lk = malloc(sizeof(char *) * sz);
    char **rk = malloc(sizeof(char *) * sz);
    if (!lk || !rk)
    {
        free(lk);
        free(rk);
        return 0;
    }

    int idx = 0;
    ht_iter it = ht_iterator(left->table);
    while (ht_next(&it))
        lk[idx++] = it.key;

    idx = 0;
    it = ht_iterator(right->table);
    while (ht_next(&it))
        rk[idx++] = it.key;

    qsort(lk, sz, sizeof(char *), compare_cstrings);
    qsort(rk, sz, sizeof(char *), compare_cstrings);

    for (int i = 0; i < sz; i++)
    {
        cmp = strcmp(lk[i], rk[i]);
        if (cmp != 0)
        {
            free(lk);
            free(rk);
            return normalize_compare(cmp);
        }

        cmp = compare(*(Value *)ht_get(left->table, lk[i]),
                      *(Value *)ht_get(right->table, rk[i]));
        if (cmp != 0)
        {
            free(lk);
            free(rk);
            return cmp;
        }
    }

    free(lk);
    free(rk);

    if (left->super_instance != right->super_instance)
        return left->super_instance > right->super_instance ? 1 : -1;
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
    if (left->data && !value_listEquals(left->data, right->data))
        return false;
    if (left->param_names && !value_listEquals(left->param_names, right->param_names))
        return false;
    return true;
}

static int code_compare(ObjCode *left, ObjCode *right)
{
    if (left->hash != right->hash)
        return left->hash > right->hash ? 1 : -1;
    if ((left->data == NULL) != (right->data == NULL))
        return left->data ? 1 : -1;
    if (left->data)
    {
        int cmp = value_listCompare(left->data, right->data);
        if (cmp != 0)
            return cmp;
    }
    if ((left->param_names == NULL) != (right->param_names == NULL))
        return left->param_names ? 1 : -1;
    if (left->param_names)
    {
        int cmp = value_listCompare(left->param_names, right->param_names);
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
        left->owner != right->owner ||
        !native_funcEquals(left->native, right->native))
        return false;

    if (left->body != right->body)
    {
        if (!left->body || !right->body || !code_equals(left->body, right->body))
            return false;
    }
    if (left->params != right->params && !value_listEquals(left->params, right->params))
        return false;
    if (left->param_names != right->param_names && !value_listEquals(left->param_names, right->param_names))
        return false;

    for (int i = 0; i < left->upvalue_count; i++)
    {
        UpValue *lu = left->upvalues ? left->upvalues[i] : NULL;
        UpValue *ru = right->upvalues ? right->upvalues[i] : NULL;
        if (lu == ru)
            continue;
        if (!lu || !ru || lu->index != ru->index || !equals(lu->value, ru->value))
            return false;
    }
    return true;
}

static int function_compare(Function *left, Function *right)
{
    int cmp;
#define FCMP(expr)      \
    do                  \
    {                   \
        cmp = (expr);   \
        if (cmp != 0)   \
            return cmp; \
    } while (0)
    FCMP(compare_strings(left->name, right->name));
    FCMP(normalize_compare((int)left->is_native - (int)right->is_native));
    FCMP(normalize_compare((int)left->is_method - (int)right->is_method));
    FCMP(normalize_compare((int)left->need_args - (int)right->need_args));
    FCMP(normalize_compare((int)left->need_kwargs - (int)right->need_kwargs));
    FCMP(normalize_compare(left->upvalue_count - right->upvalue_count));
    FCMP(compare_nativeFuncs(left->native, right->native));

    if ((left->body == NULL) != (right->body == NULL))
        return left->body ? 1 : -1;
    if (left->body)
        FCMP(code_compare(left->body, right->body));

    if ((left->params == NULL) != (right->params == NULL))
        return left->params ? 1 : -1;
    if (left->params)
        FCMP(value_listCompare(left->params, right->params));

    if ((left->param_names == NULL) != (right->param_names == NULL))
        return left->param_names ? 1 : -1;
    if (left->param_names)
        FCMP(value_listCompare(left->param_names, right->param_names));

    for (int i = 0; i < left->upvalue_count; i++)
    {
        UpValue *lu = left->upvalues ? left->upvalues[i] : NULL;
        UpValue *ru = right->upvalues ? right->upvalues[i] : NULL;
        if (lu == ru)
            continue;
        if (!lu)
            return -1;
        if (!ru)
            return 1;
        FCMP(normalize_compare(lu->index - ru->index));
        FCMP(compare(lu->value, ru->value));
    }

    FCMP(compare_ptrs(left->instance, right->instance));
    FCMP(compare_ptrs(left->owner, right->owner));
#undef FCMP
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
    if ((left->constants == NULL) != (right->constants == NULL))
        return false;
    if ((left->names == NULL) != (right->names == NULL))
        return false;
    if (left->exports && !map_equals(left->exports, right->exports))
        return false;
    if (left->constants && !value_listEquals(left->constants, right->constants))
        return false;
    if (left->names && !value_listEquals(left->names, right->names))
        return false;
    return true;
}

static int module_compare(ObjModule *left, ObjModule *right)
{
    int cmp;
#define MCMP(expr)      \
    do                  \
    {                   \
        cmp = (expr);   \
        if (cmp != 0)   \
            return cmp; \
    } while (0)
    MCMP(compare_strings(left->name, right->name));
    MCMP(compare_strings(left->path, right->path));
    MCMP(normalize_compare((int)left->builtin - (int)right->builtin));
    MCMP(normalize_compare((int)left->is_main - (int)right->is_main));
    MCMP(normalize_compare((int)left->state - (int)right->state));

    if ((left->exports == NULL) != (right->exports == NULL))
        return left->exports ? 1 : -1;
    if (left->exports)
        MCMP(map_compare(left->exports, right->exports));

    if ((left->constants == NULL) != (right->constants == NULL))
        return left->constants ? 1 : -1;
    if (left->constants)
        MCMP(value_listCompare(left->constants, right->constants));

    if ((left->names == NULL) != (right->names == NULL))
        return left->names ? 1 : -1;
    if (left->names)
        MCMP(value_listCompare(left->names, right->names));
#undef MCMP
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
    int cmp;
    if ((cmp = compare_strings(left->filename, right->filename)) != 0)
        return cmp;
    if ((cmp = compare_strings(left->mode, right->mode)) != 0)
        return cmp;
    if ((cmp = normalize_compare((int)left->closed - (int)right->closed)) != 0)
        return cmp;
    return compare_ptrs(left->fp, right->fp);
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
    int cmp;
    if ((cmp = compare_strings(left->type, right->type)) != 0)
        return cmp;
    if ((cmp = normalize_compare((int)left->event_type - (int)right->event_type)) != 0)
        return cmp;
    if ((cmp = normalize_compare(left->x - right->x)) != 0)
        return cmp;
    if ((cmp = normalize_compare(left->y - right->y)) != 0)
        return cmp;
    if ((cmp = normalize_compare(left->dx - right->dx)) != 0)
        return cmp;
    if ((cmp = normalize_compare(left->dy - right->dy)) != 0)
        return cmp;
    if ((cmp = compare_strings(left->key, right->key)) != 0)
        return cmp;
    if ((cmp = normalize_compare(left->button - right->button)) != 0)
        return cmp;
    if ((cmp = normalize_compare((int)left->pressed - (int)right->pressed)) != 0)
        return cmp;
    if ((cmp = normalize_compare(left->width - right->width)) != 0)
        return cmp;
    return normalize_compare(left->height - right->height);
}

PI_STRICT_FP bool equals(Value left, Value right)
{
    if (left.type != right.type)
        return false;

    switch (left.type)
    {
    case VAL_NUM:
        if (number_isNaN(left.data.number) || number_isNaN(right.data.number))
            return false;
        return fabs(left.data.number - right.data.number) < 1e-9;

    case VAL_BOOL:
        return left.data.boolean == right.data.boolean;

    case VAL_NIL:
        return true;

    case VAL_OBJ:
    {
        if (left.data.object->type != right.data.object->type)
            return false;
        switch (left.data.object->type)
        {
        case OBJ_STRING:
        {
            PiString *a = (PiString *)left.data.object;
            PiString *b = (PiString *)right.data.object;
            return a->length == b->length && strcmp(a->chars, b->chars) == 0;
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
            return left.data.object == right.data.object;
        }
    }

    default:
        return false;
    }
}

int compare(Value left, Value right)
{
    if (left.type != right.type)
    {
        if (is_numeric(left) && is_numeric(right))
            return compare_numbers(as_number(left), as_number(right));
        return ERROR_COMPARE;
    }

    switch (left.type)
    {
    case VAL_NUM:
        return compare_numbers(left.data.number, right.data.number);
    case VAL_BOOL:
        return (int)left.data.boolean - (int)right.data.boolean;
    case VAL_NIL:
        return 0;
    case VAL_OBJ:
        switch (OBJ_TYPE(left))
        {
        case OBJ_STRING:
            return strcmp(AS_STRING(left)->chars,
                          AS_STRING(right)->chars);
        case OBJ_LIST:
            return list_compare(AS_LIST(left), AS_LIST(right));
        case OBJ_TUPLE:
            return value_listCompare(AS_TUPLE(left)->items,
                                     AS_TUPLE(right)->items);
        case OBJ_TENSOR:
            return tensor_compare(AS_TENSOR(left),
                                  AS_TENSOR(right));
        case OBJ_MAP:
            return map_compare(AS_MAP(left), AS_MAP(right));
        case OBJ_RANGE:
            return range_compare(AS_RANGE(left), AS_RANGE(right));
        case OBJ_FUN:
            return function_compare(AS_FUN(left), AS_FUN(right));
        case OBJ_CODE:
            return code_compare(AS_CODE(left), AS_CODE(right));
        case OBJ_MODULE:
            return module_compare(AS_MODULE(left), AS_MODULE(right));
        case OBJ_FILE:
            return file_compare(AS_FILE(left), AS_FILE(right));
        case OBJ_EVENT:
            return event_compare(AS_EVENT(left), AS_EVENT(right));
        default:
            return ERROR_COMPARE;
        }
    default:
        return ERROR_COMPARE;
    }
}

Value new_value(token_t token)
{
    Value val;

    switch (token.type)
    {
    case TK_NUM:
        val.type = VAL_NUM;
        val.data.number = tk_double(token);
        break;

    case TK_STR:
    {
        const char *raw = tk_string(token);
        char *unescaped = unescape_string(raw);
        val = NEW_OBJ(new_pistring(dup_cstring(unescaped)));
        free(unescaped);
        break;
    }

    case TK_ID:
        val = NEW_OBJ(new_pistring(tk_string(token)));
        break;

    case TK_TRUE:
    case TK_FALSE:
        val.type = VAL_BOOL;
        val.data.boolean = tk_bool(token);
        break;

    case TK_NIL:
        val.type = VAL_NIL;
        break;

    default:
        error("Unexpected token value: %s", tk_string(token));
    }

    return val;
}

double as_number(Value val)
{
    switch (val.type)
    {
    case VAL_NUM:
        return val.data.number;
    case VAL_BOOL:
        return val.data.boolean ? 1.0 : 0.0;
    case VAL_NIL:
        return 0.0;
    case VAL_OBJ:
        if (AS_OBJ(val)->type == OBJ_STRING)
        {
            PiString *str = AS_STRING(val);
            char *end;
            double result = strtod(str->chars, &end);
            if (end == str->chars)
                error("Error: String '%s' cannot be converted to a number.", str->chars);
            return result;
        }
        /* fall through */
    default:
        error("Cannot convert %s to a number", type_name(val));
    }
    return 0.0;
}

bool as_bool(Value val)
{
    switch (val.type)
    {
    case VAL_BOOL:
        return val.data.boolean;
    case VAL_NUM:
        return val.data.number != 0.0;
    case VAL_NIL:
        return false;
    case VAL_OBJ:
        switch (AS_OBJ(val)->type)
        {
        case OBJ_STRING:
            return AS_STRING(val)->length > 0;
        case OBJ_LIST:
            return LIST_SIZE(AS_LIST(val)->items) > 0;
        case OBJ_TENSOR:
            return AS_TENSOR(val)->size > 0;
        case OBJ_MAP:
            return ht_length(AS_MAP(val)->table) > 0;
        case OBJ_MODULE:
            return AS_MODULE(val)->exports &&
                   ht_length(AS_MODULE(val)->exports->table) > 0;
        case OBJ_RANGE:
            return AS_RANGE(val)->start != AS_RANGE(val)->end;
        default:
            return true;
        }
    default:
        error("Expected a boolean, but got %s", type_name(val));
    }
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

static void tensor_appendToSb(sb_t *sb, PiTensor *tensor, int dim, int *indices)
{
    if (dim == tensor->ndim)
    {
        char *item = format_number(tensor_get(tensor, indices));
        sb_append(sb, item);
        free(item);
        return;
    }

    sb_append(sb, "[");
    bool wrote = false;
    bool ellipsis = false;

    for (int i = 0; i < tensor->shape[dim]; i++)
    {
        if (!tensor_shouldPrintIndex(tensor, dim, i))
        {
            if (!ellipsis)
            {
                if (wrote)
                    sb_append(sb, ", ");
                sb_append(sb, "...");
                wrote = true;
                ellipsis = true;
            }
            continue;
        }
        if (wrote)
            sb_append(sb, ", ");
        indices[dim] = i;
        tensor_appendToSb(sb, tensor, dim + 1, indices);
        wrote = true;
    }
    sb_append(sb, "]");
}

char *as_stringWithFormat(vm_t *vm, Value val)
{
    /* Let instances override via .format() method. */
    if (vm != NULL && IS_MAP(val) && AS_MAP(val)->is_instance)
    {
        Value formatted = vm_callMethodNoArgs(vm, val, "format");
        if (!(IS_MAP(formatted) && AS_MAP(formatted) == AS_MAP(val)))
            return as_stringWithFormat(vm, formatted);
    }

    switch (val.type)
    {
    case VAL_NUM:
        return format_number(val.data.number);

    case VAL_BOOL:
        return dup_cstring(val.data.boolean ? "true" : "false");

    case VAL_NIL:
        return dup_cstring("nil");

    case VAL_OBJ:
        switch (AS_OBJ(val)->type)
        {

        case OBJ_STRING:
            return dup_cstring(AS_STRING(val)->chars);

        case OBJ_LIST:
        {
            list_t *list = as_list(val);
            sb_t sb;
            sb_init(&sb, "[");
            for (size_t i = 0; i < (size_t)list->size; i++)
            {
                if (i > 0)
                    sb_append(&sb, ", ");
                char *item = as_stringWithFormat(vm, *(Value *)list_getAt(list, i));
                sb_append(&sb, item);
                free(item);
            }
            sb_append(&sb, "]");
            return sb_finish(&sb);
        }

        case OBJ_MAP:
        {
            PiMap *map = AS_MAP(val);
            sb_t sb;
            sb_init(&sb, "{");
            bool first = true;
            ht_iter it = ht_iterator(map->table);
            while (ht_next(&it))
            {
                char *vstr = as_stringWithFormat(vm, *(Value *)it.value);
                if (!first)
                    sb_append(&sb, ", ");
                first = false;
                sb_append(&sb, it.key);
                sb_append(&sb, ": ");
                sb_append(&sb, vstr);
                free(vstr);
            }
            sb_append(&sb, "}");
            return sb_finish(&sb);
        }

        case OBJ_SET:
        {
            PiSet *set = AS_SET(val);
            int size = set_size(set);
            if (size == 0)
                return dup_cstring("{}");
            sb_t sb;
            sb_init(&sb, "{");
            for (int i = 0; i < size; i++)
            {
                if (i > 0)
                    sb_append(&sb, ", ");
                char *item = as_stringWithFormat(vm, set_get(set, i));
                sb_append(&sb, item);
                free(item);
            }
            sb_append(&sb, "}");
            return sb_finish(&sb);
        }

        case OBJ_TUPLE:
        {
            PiTuple *tuple = AS_TUPLE(val);
            int size = LIST_SIZE(tuple->items);
            sb_t sb;
            sb_init(&sb, "(");
            for (int i = 0; i < size; i++)
            {
                if (i > 0)
                    sb_append(&sb, ", ");
                char *item = as_stringWithFormat(vm, *(Value *)list_getAt(tuple->items, i));
                sb_append(&sb, item);
                free(item);
            }
            if (size == 1)
                sb_append(&sb, ",");
            sb_append(&sb, ")");
            return sb_finish(&sb);
        }

        case OBJ_TENSOR:
        {
            PiTensor *tensor = AS_TENSOR(val);
            sb_t sb;
            sb_init(&sb, "");
            int indices[MAX_TENSOR_DIMS] = {0};
            tensor_appendToSb(&sb, tensor, 0, indices);
            return sb_finish(&sb);
        }

        case OBJ_RANGE:
        {
            PiRange *range = AS_RANGE(val);
            char *start = format_number(range->start);
            char *end = format_number(range->end);
            sb_t sb;
            sb_init(&sb, start);
            sb_append(&sb, "..");
            sb_append(&sb, end);
            free(start);
            free(end);
            if (range->step != 1.0)
            {
                char *step = format_number(range->step);
                sb_append(&sb, ":");
                sb_append(&sb, step);
                free(step);
            }
            return sb_finish(&sb);
        }

        case OBJ_FUN:
        {
            Function *fun = AS_FUN(val);
            char *result = malloc(128);
            snprintf(result, 128, "<FUN: %s>", fun->name);
            return result;
        }

        case OBJ_MODULE:
        {
            ObjModule *m = AS_MODULE(val);
            char *result = malloc(256);
            snprintf(result, 256, "<module %s>", m->name ? m->name : "<anonymous>");
            return result;
        }

        case OBJ_FILE:
        {
            ObjFile *f = AS_FILE(val);
            char *result = malloc(256);
            snprintf(result, 256, "<file %s>", f->filename ? f->filename : "<anonymous>");
            return result;
        }

#ifndef __EMSCRIPTEN__
        case OBJ_IMAGE:
        {
            ObjImage *img = AS_IMAGE(val);
            char *result = malloc(128);
            snprintf(result, 128, "<image %dx%d>", img->surface->w, img->surface->h);
            return result;
        }
#endif

        case OBJ_CODE:
            return NULL;

        default:
            return NULL;
        }

    default:
        return NULL;
    }
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
        double n = val.data.number == 0.0 ? 0.0 : val.data.number;
        uint64_t bits = 0;
        memcpy(&bits, &n, sizeof(bits));
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
        Object *lo = AS_OBJ(left), *ro = AS_OBJ(right);
        if (lo->type != ro->type)
            return false;
        if (lo->type == OBJ_STRING)
        {
            PiString *a = AS_STRING(left), *b = AS_STRING(right);
            return a->length == b->length && memcmp(a->chars, b->chars, a->length) == 0;
        }
        return lo->id == ro->id;
    }
    }
    return false;
}

list_t *as_list(Value val)
{
    if (val.type == VAL_OBJ && OBJ_TYPE(val) == OBJ_LIST)
        return AS_LIST(val)->items;
    error("Expected a list, but got %s", type_name(val));
}

bool is_numeric(Value val)
{
    if (val.type == VAL_NUM || val.type == VAL_BOOL || val.type == VAL_NIL)
        return true;
    if (val.type == VAL_OBJ && OBJ_TYPE(val) == OBJ_STRING)
    {
        const char *s = AS_STRING(val)->chars;
        if (!*s)
            return false;
        char *end;
        strtod(s, &end);
        return *end == '\0';
    }
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
        return val;

    case VAL_OBJ:
    {
        Object *obj = AS_OBJ(val);
        copy.type = VAL_OBJ;
        switch (obj->type)
        {
        case OBJ_STRING:
        {
            PiString *orig = (PiString *)obj;
            PiString *str = malloc(sizeof(PiString));
            str->object.type = OBJ_STRING;
            str->length = orig->length;
            str->chars = malloc(str->length + 1);
            memcpy(str->chars, orig->chars, str->length + 1);
            copy.data.object = (Object *)str;
            break;
        }
        case OBJ_LIST:
        {
            PiList *orig = (PiList *)obj;
            PiList *list = malloc(sizeof(PiList));
            list->object.type = OBJ_LIST;
            list->items = list_create(sizeof(Value));
            list->current = 0;
            for (size_t i = 0; i < LIST_SIZE(orig->items); i++)
            {
                Value item = copy_value(*(Value *)list_getAt(orig->items, i));
                list_add(list->items, &item);
            }
            copy.data.object = (Object *)list;
            break;
        }
        case OBJ_TENSOR:
        {
            PiTensor *orig = (PiTensor *)obj;
            PiTensor *tensor = (PiTensor *)new_tensor(orig->ndim, orig->shape, orig->type);
            for (int i = 0; i < orig->size; i++)
                tensor_setFlat(tensor, i, tensor_getFlat(orig, i));
            copy.data.object = (Object *)tensor;
            break;
        }
        case OBJ_MAP:
            /* PiMap deep-copy not implemented. */
            break;
        default:
            error("Unsupported object type for copy");
        }
        break;
    }

    default:
        error("Unsupported value type for copy");
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
            printf("'%s'", AS_STRING(val)->chars);
            break;
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
            printf("<%s: %p>", AS_FUN(val)->name, (void *)AS_FUN(val));
            break;
        case OBJ_MODULE:
            printf("<module %s>",
                   AS_MODULE(val)->name ? AS_MODULE(val)->name : "<anonymous>");
            break;
#ifndef __EMSCRIPTEN__
        case OBJ_IMAGE:
            printf("<image %dx%d>",
                   AS_IMAGE(val)->surface->w, AS_IMAGE(val)->surface->h);
            break;
#endif
        /* Containers: reuse as_string so format lives in one place. */
        default:
        {
            char *text = as_string(val);
            if (text)
            {
                printf("%s", text);
                free(text);
            }
            break;
        }
        }
        break;
    default:
        error("Unknown value type: %s", type_name(val));
    }

    printf(is_root ? "\n" : " ");
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
                return map->intrinsic_name ? map->intrinsic_name : "object";
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
