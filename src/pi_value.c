// clang-format off
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "pi_value.h"
#include "pi_object.h"
#include "pi_func.h"
#include "pi_module.h"
#include "pi_vm.h"

#if defined(__GNUC__)
#define PI_STRICT_FP __attribute__((optimize("no-fast-math")))
#else
#define PI_STRICT_FP
#endif

/* The VM deliberately uses NaN as a constant-pool sentinel.  These helpers
 * must retain IEEE NaN semantics even when the application is built -Ofast. */
static PI_STRICT_FP int _compare_numbers(double l, double r)
{
    if (fabs(l - r) < 1e-9)
        return 0;
    return (l > r) ? 1 : -1;
}

/* Do not use isnan() here: -ffast-math is permitted to assume it is always
 * false.  The constant pool uses NaN as a sentinel, so inspect IEEE-754 bits
 * directly before doing any floating-point equality arithmetic. */
static inline bool _number_isNaN(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000) &&
           (bits & UINT64_C(0x000fffffffffffff)) != 0;
}

static char *_dup_Cstring(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy)
        memcpy(copy, text, length);

    return copy;
}

static int _compare_ptrs(const void *l, const void *r)
{
    return ((uintptr_t)l > (uintptr_t)r) - 
           ((uintptr_t)l < (uintptr_t)r);
}

static bool _string_equals(const char *l, const char *r)
{
    return l == r || (l && r && strcmp(l, r) == 0);
}

static int _normalize_compare(int cmp)
{
    return (cmp > 0) - (cmp < 0);
}

static int _compare_strings(const char *l, const char *r)
{
    if (l == r)  return 0;
    if (!l)      return -1;
    if (!r)      return  1;
    return _normalize_compare(strcmp(l, r));
}

static int _compare_Cstrings(const void *l, const void *r)
{
    const char *const *a = (const char *const *)l;
    const char *const *b = (const char *const *)r;
    return strcmp(*a, *b);
}

#define SB_INIT_CAP 64

// String builder for formatting strings with dynamic length.
typedef struct
{
    char *buf;
    size_t len;
    size_t cap;
} StringBuilder;

/* Initialise with an optional string literal seed (pass "" for empty). */
static void _sb_init(StringBuilder *sb, const char *seed)
{
    sb->len = strlen(seed);
    sb->cap = sb->len < SB_INIT_CAP ? SB_INIT_CAP : sb->len * 2;
    sb->buf = malloc(sb->cap);
    if (!sb->buf)
        error("[sb_init] Out of memory.");
    memcpy(sb->buf, seed, sb->len + 1); /* include NUL */
}

static void _sb_append(StringBuilder *sb, const char *text)
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
static char *_sb_finish(StringBuilder *sb)
{
    return sb->buf; /* caller is responsible for free() */
}

static char *_format_number(double number)
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
        snprintf(text, 64, "%.15g", number);

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

static char *_unescape_string(const char *src)
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
            *out++ = *p;
    }

    *out = '\0';
    return dest;
}

/* Equality helpers. */
static bool _list_equals(List *l, List *r)
{
    if (l == r) return TRUE;
    if (!l || !r || LIST_SIZE(l) != LIST_SIZE(r)) return FALSE;
    for (size_t i = 0; i < LIST_SIZE(l); i++)
        if (!equals(*(Value *)_list_getAt(l, i), *(Value *)_list_getAt(r, i)))
            return FALSE;
    return TRUE;
}

static bool _tensor_equals(PiTensor *l, PiTensor *r)
{
    if (l->type != r->type || l->ndim != r->ndim || l->size != r->size)
        return FALSE;
    for (int i = 0; i < l->ndim; i++)
        if (l->shape[i] != r->shape[i]) return FALSE;
    for (int i = 0; i < l->size; i++)
        if (fabs(tensor_getFlat(l, i) - tensor_getFlat(r, i)) >= 1e-9) return FALSE;
    return TRUE;
}

static bool _map_equals(PiMap *l, PiMap *r)
{
    if (l == r) return TRUE;
    if (map_size(l) != map_size(r)) return FALSE;
    HT_Iterator it = _ht_iterator(l->table);
    while (_ht_next(&it))
    {
        Value *rv = _ht_get(r->table, it.key);
        if (!rv || !equals(*(Value *)it.value, *rv)) return FALSE;
    }
    return TRUE;
}

static bool _range_equals(PiRange *l, PiRange *r)
{
    return _compare_numbers(l->start, r->start) == 0 &&
           _compare_numbers(l->end, r->end) == 0 &&
           _compare_numbers(l->step, r->step) == 0;
}

static bool _code_equals(ObjCode *l, ObjCode *r)
{
    if (l == r) return TRUE;
    if (l->hash != r->hash) return FALSE;
    if ((l->data        == NULL) != (r->data        == NULL)) return FALSE;
    if ((l->param_names == NULL) != (r->param_names == NULL)) return FALSE;
    if (l->data        && !_list_equals(l->data,        r->data))        return FALSE;
    if (l->param_names && !_list_equals(l->param_names, r->param_names)) return FALSE;
    return TRUE;
}

static bool _function_equals(Function *l, Function *r)
{
    if (l == r)   return TRUE;
    if (!l || !r) return FALSE;

    if (!_string_equals(l->name, r->name))             return FALSE;

    if (l->flags != r->flags ||
        l->upvalue_count != r->upvalue_count)         return FALSE;
        
    if (l->instance != r->instance ||
        l->owner    != r->owner)                      return FALSE;

    if (memcmp(&l->native, &r->native, sizeof(NativeFunc)) != 0) return FALSE;

    if (l->body != r->body &&
        (!l->body || !r->body || !_code_equals(l->body, r->body))) return FALSE;
        
    if (!_list_equals(l->params,       r->params))      return FALSE;
    if (!_list_equals(l->param_names,  r->param_names)) return FALSE;
    
    for (int i = 0; i < l->upvalue_count; i++)
    {
        UpValue *lu = l->upvalues ? l->upvalues[i] : NULL;
        UpValue *ru = r->upvalues ? r->upvalues[i] : NULL;

        if (lu == ru) continue;
        if (!lu || !ru) return FALSE;

        if (lu->index != ru->index || !equals(lu->value, ru->value)) return FALSE;
    }
    return TRUE;
}

static bool _module_equals(ObjModule *l, ObjModule *r)
{
    if (l == r) return TRUE;
    return _string_equals(l->name, r->name) AND
           _string_equals(l->path, r->path) AND
           l->builtin == r->builtin AND
           l->is_main == r->is_main AND
           l->state   == r->state   AND
           (l->exports   == r->exports   OR (l->exports   AND r->exports   AND _map_equals(l->exports,    r->exports)))   AND
           (l->constants == r->constants OR (l->constants AND r->constants AND _list_equals(l->constants, r->constants))) AND
           (l->names     == r->names     OR (l->names     AND r->names     AND _list_equals(l->names,     r->names)));
}

static bool _file_equals(ObjFile *l, ObjFile *r)
{
    return l == r                           OR
           (l->fp == r->fp                  AND
            l->closed == r->closed          AND
            _string_equals(l->mode, r->mode) AND
            _string_equals(l->filename, r->filename));
}

static bool _event_equals(PiEvent *l, PiEvent *r)
{
    return l == r                           OR
           (_string_equals(l->type, r->type) AND
            l->event_type == r->event_type  AND
            l->x  == r->x                   AND
            l->y  == r->y                   AND
            l->dx == r->dx                  AND
            l->dy == r->dy                  AND
            _string_equals(l->key, r->key)   AND
            l->button  == r->button         AND
            l->pressed == r->pressed        AND
            l->width   == r->width          AND
            l->height  == r->height);
}

/* Ordering helpers. */
static int _list_compare(List *l, List *r)
{
    if (l == r)
        return 0;
    if (l == NULL)
        return -1;
    if (r == NULL)
        return 1;

    size_t ls = LIST_SIZE(l), rs = LIST_SIZE(r);
    size_t min = ls < rs ? ls : rs;

    for (size_t i = 0; i < min; i++)
    {
        int cmp = compare(*(Value *)_list_getAt(l, i),
                          *(Value *)_list_getAt(r, i));
        if (cmp != 0)
            return cmp;
    }
    return ls == rs ? 0 : (ls > rs ? 1 : -1);
}

static int _tensor_compare(PiTensor *l, PiTensor *r)
{
    if (l->ndim != r->ndim)
        return l->ndim > r->ndim ? 1 : -1;

    for (int i = 0; i < l->ndim; i++)
    {
        if (l->shape[i] != r->shape[i])
            return l->shape[i] > r->shape[i] ? 1 : -1;
    }

    for (int i = 0; i < l->size; i++)
    {
        int cmp = _compare_numbers(tensor_getFlat(l, i), tensor_getFlat(r, i));
        if (cmp != 0)
            return cmp;
    }
    return 0;
}

static int _map_compare(PiMap *l, PiMap *r)
{
    if (l == r)
        return 0;

    int cmp = _normalize_compare(map_size(l) - map_size(r));
    if (cmp != 0)
        return cmp;

    int sz = ht_length(l->table);
    const char **lk = malloc(sizeof(char *) * sz);
    const char **rk = malloc(sizeof(char *) * sz);
    if (!lk || !rk)
    {
        free(lk);
        free(rk);
        return 0;
    }

    int idx = 0;
    HT_Iterator it = _ht_iterator(l->table);
    while (_ht_next(&it))
        lk[idx++] = it.key;

    idx = 0;
    it = _ht_iterator(r->table);
    while (_ht_next(&it))
        rk[idx++] = it.key;

    qsort(lk, sz, sizeof(char *), _compare_Cstrings);
    qsort(rk, sz, sizeof(char *), _compare_Cstrings);

    for (int i = 0; i < sz; i++)
    {
        cmp = strcmp(lk[i], rk[i]);
        if (cmp != 0)
        {
            free(lk);
            free(rk);
            return _normalize_compare(cmp);
        }

        cmp = compare(*(Value *)_ht_get(l->table, lk[i]),
                      *(Value *)_ht_get(r->table, rk[i]));
        if (cmp != 0)
        {
            free(lk);
            free(rk);
            return cmp;
        }
    }

    free(lk);
    free(rk);
    return 0;
}

static int _range_compare(PiRange *l, PiRange *r)
{
    int cmp;
    if ((cmp = _compare_numbers(l->start, r->start))) return cmp;
    if ((cmp = _compare_numbers(l->end,   r->end)))   return cmp;
    return _compare_numbers(l->step, r->step);
}

static int _code_compare(ObjCode *l, ObjCode *r)
{
    if (l->hash != r->hash)
        return l->hash > r->hash ? 1 : -1;
    int cmp;
    if ((l->data == NULL) != (r->data == NULL)) return l->data ? 1 : -1;
    if (l->data && (cmp = _list_compare(l->data, r->data))) return cmp;
    if ((l->param_names == NULL) != (r->param_names == NULL)) return l->param_names ? 1 : -1;
    if (l->param_names) return _list_compare(l->param_names, r->param_names);
    return 0;
}

/* Macro: compare expression, early-return if non-zero. */
#define CMP_RET(expr)    \
    do                   \
    {                    \
        int _c = (expr); \
        if (_c)          \
            return _c;   \
    } while (0)

static int _function_compare(Function *l, Function *r)
{
    CMP_RET(_compare_strings(l->name, r->name));
    CMP_RET(_normalize_compare(FUNC_HAS_FLAG(l, FUNC_NATIVE)    - FUNC_HAS_FLAG(r, FUNC_NATIVE)));
    CMP_RET(_normalize_compare(FUNC_HAS_FLAG(l, FUNC_METHOD)    - FUNC_HAS_FLAG(r, FUNC_METHOD)));
    CMP_RET(_normalize_compare(FUNC_HAS_FLAG(l, FUNC_NEED_ARGS) - FUNC_HAS_FLAG(r, FUNC_NEED_ARGS)));
    CMP_RET(_normalize_compare(FUNC_HAS_FLAG(l, FUNC_NEED_KWARGS) - FUNC_HAS_FLAG(r, FUNC_NEED_KWARGS)));
    CMP_RET(_normalize_compare(l->upvalue_count - r->upvalue_count));
    CMP_RET(_normalize_compare(memcmp(&l->native, &r->native, sizeof(NativeFunc))));

    if ((l->body == NULL) != (r->body == NULL)) return l->body ? 1 : -1;
    if (l->body) CMP_RET(_code_compare(l->body, r->body));

    if ((l->params == NULL) != (r->params == NULL)) return l->params ? 1 : -1;
    if (l->params) CMP_RET(_list_compare(l->params, r->params));

    if ((l->param_names == NULL) != (r->param_names == NULL)) return l->param_names ? 1 : -1;
    if (l->param_names) CMP_RET(_list_compare(l->param_names, r->param_names));

    for (int i = 0; i < l->upvalue_count; i++)
    {
        UpValue *lu = l->upvalues ? l->upvalues[i] : NULL;
        UpValue *ru = r->upvalues ? r->upvalues[i] : NULL;

        if (lu == ru) continue;
        if (!lu) return -1;
        if (!ru) return  1;

        CMP_RET(_normalize_compare(lu->index - ru->index));
        CMP_RET(compare(lu->value, ru->value));
    }

    CMP_RET(_compare_ptrs(l->instance, r->instance));
    return _compare_ptrs(l->owner, r->owner);
}

static int _module_compare(ObjModule *l, ObjModule *r)
{
    CMP_RET(_compare_strings(l->name, r->name));
    CMP_RET(_compare_strings(l->path, r->path));
    CMP_RET(_normalize_compare((int)l->builtin - (int)r->builtin));
    CMP_RET(_normalize_compare((int)l->is_main - (int)r->is_main));
    CMP_RET(_normalize_compare((int)l->state   - (int)r->state));

    if ((l->exports == NULL) != (r->exports == NULL)) return l->exports ? 1 : -1;
    if (l->exports) CMP_RET(_map_compare(l->exports, r->exports));

    if ((l->constants == NULL) != (r->constants == NULL)) return l->constants ? 1 : -1;
    if (l->constants) CMP_RET(_list_compare(l->constants, r->constants));

    if ((l->names == NULL) != (r->names == NULL)) return l->names ? 1 : -1;
    if (l->names) return _list_compare(l->names, r->names);
    return 0;
}

static int _file_compare(ObjFile *l, ObjFile *r)
{
    CMP_RET(_compare_strings(l->filename, r->filename));
    CMP_RET(_compare_strings(l->mode, r->mode));
    CMP_RET(_normalize_compare((int)l->closed - (int)r->closed));

    return _compare_ptrs(l->fp, r->fp);
}

static int _event_compare(PiEvent *l, PiEvent *r)
{
    CMP_RET(_compare_strings(l->type, r->type));
    CMP_RET(_normalize_compare((int)l->event_type - (int)r->event_type));
    CMP_RET(_normalize_compare(l->x - r->x));
    CMP_RET(_normalize_compare(l->y - r->y));
    CMP_RET(_normalize_compare(l->dx - r->dx));
    CMP_RET(_normalize_compare(l->dy - r->dy));
    CMP_RET(_compare_strings(l->key, r->key));
    CMP_RET(_normalize_compare(l->button - r->button));
    CMP_RET(_normalize_compare((int)l->pressed - (int)r->pressed));
    CMP_RET(_normalize_compare(l->width - r->width));

    return _normalize_compare(l->height - r->height);
}

#undef CMP_RET

PI_STRICT_FP bool equals(Value l, Value r)
{
    if (l.type != r.type)
        return FALSE;

    switch (l.type)
    {
    case VAL_NUM:
        return !_number_isNaN(l.data.number) &&
               !_number_isNaN(r.data.number) &&
               fabs(l.data.number - r.data.number) < 1e-9;
    case VAL_BOOL:
        return l.data.boolean == r.data.boolean;
    case VAL_NIL:
        return TRUE;
    case VAL_OBJ:
    {
        if (l.data.object->type != r.data.object->type)
            return FALSE;
        switch (l.data.object->type)
        {
        case OBJ_STRING:
        {
            PiString *a = (PiString *)l.data.object;
            PiString *b = (PiString *)r.data.object;
            return a->length == b->length && strcmp(a->chars, b->chars) == 0;
        }
        case OBJ_LIST:    return _list_equals(AS_LIST(l)->items,   AS_LIST(r)->items);
        case OBJ_TUPLE:   return _list_equals(AS_TUPLE(l)->items,  AS_TUPLE(r)->items);
        case OBJ_TENSOR:  return _tensor_equals(AS_TENSOR(l),      AS_TENSOR(r));
        case OBJ_MAP:     return _map_equals(AS_MAP(l),            AS_MAP(r));
        case OBJ_MODULE:  return _module_equals(AS_MODULE(l),      AS_MODULE(r));
        case OBJ_RANGE:   return _range_equals(AS_RANGE(l),        AS_RANGE(r));
        case OBJ_FUN:     return _function_equals(AS_FUN(l),       AS_FUN(r));
        case OBJ_CODE:    return _code_equals(AS_CODE(l),          AS_CODE(r));
        case OBJ_FILE:    return _file_equals(AS_FILE(l),          AS_FILE(r));
        case OBJ_EVENT:   return _event_equals(AS_EVENT(l),        AS_EVENT(r));
        default:          return l.data.object == r.data.object;
        }
    }
    default:
        return FALSE;
    }
}

int compare(Value l, Value r)
{
    if (l.type != r.type)
    {
        if (is_numeric(l) && is_numeric(r))
            return _compare_numbers(as_number(l), as_number(r));
        return ERROR_COMPARE;
    }

    switch (l.type)
    {
    case VAL_NUM:   return _compare_numbers(l.data.number, r.data.number);
    case VAL_BOOL:  return (int)l.data.boolean - (int)r.data.boolean;
    case VAL_NIL:   return 0;
    case VAL_OBJ:
        switch (OBJ_TYPE(l))
        {
        case OBJ_STRING: return strcmp(AS_STRING(l)->chars, AS_STRING(r)->chars);
        case OBJ_LIST:   return _list_compare(AS_LIST(l)->items,  AS_LIST(r)->items);
        case OBJ_TUPLE:  return _list_compare(AS_TUPLE(l)->items, AS_TUPLE(r)->items);
        case OBJ_TENSOR: return _tensor_compare(AS_TENSOR(l),     AS_TENSOR(r));
        case OBJ_MAP:    return _map_compare(AS_MAP(l),           AS_MAP(r));
        case OBJ_RANGE:  return _range_compare(AS_RANGE(l),       AS_RANGE(r));
        case OBJ_FUN:    return _function_compare(AS_FUN(l),      AS_FUN(r));
        case OBJ_CODE:   return _code_compare(AS_CODE(l),         AS_CODE(r));
        case OBJ_MODULE: return _module_compare(AS_MODULE(l),     AS_MODULE(r));
        case OBJ_FILE:   return _file_compare(AS_FILE(l),         AS_FILE(r));
        case OBJ_EVENT:  return _event_compare(AS_EVENT(l),       AS_EVENT(r));
        default:         return ERROR_COMPARE;
        }
    default:
        return ERROR_COMPARE;
    }
}

char *string_fromToken(Token token)
{
    char *raw = tk_string(token);
    if (token.type != TK_STR)
        return raw;
    char *text = _unescape_string(raw);
    free(raw);
    return text;
}

Value new_value(Token token)
{
    Value val;

    switch (token.type)
    {
    case TK_NUM:
        val.type = VAL_NUM;
        val.data.number = tk_double(token);
        break;

    case TK_STR:
    case TK_ID:
        val = NEW_OBJ(new_pistring(string_fromToken(token)));
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
    case VAL_NUM:   return val.data.number;
    case VAL_BOOL:  return val.data.boolean ? 1.0 : 0.0;
    case VAL_NIL:   return 0.0;
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
    case VAL_BOOL:  return val.data.boolean;
    case VAL_NUM:   return val.data.number != 0.0;
    case VAL_NIL:   return FALSE;
    case VAL_OBJ:
        switch (AS_OBJ(val)->type)
        {
        case OBJ_STRING:  return AS_STRING(val)->length > 0;
        case OBJ_LIST:    return LIST_SIZE(AS_LIST(val)->items) > 0;
        case OBJ_TENSOR:  return AS_TENSOR(val)->size > 0;
        case OBJ_MAP:     return ht_length(AS_MAP(val)->table) > 0;
        case OBJ_MODULE:  return AS_MODULE(val)->exports &&
                                 ht_length(AS_MODULE(val)->exports->table) > 0;
        case OBJ_RANGE:   return AS_RANGE(val)->start != AS_RANGE(val)->end;
        default:          return TRUE;
        }
    default:
        error("Expected a boolean, but got %s", type_name(val));
    }
}

static bool _tensor_shouldSummarizeDim(PiTensor *tensor, int dim)
{
    return tensor->shape[dim] > 10;
}

static bool _tensor_shouldPrintIndex(PiTensor *tensor, int dim, int index)
{
    if (!_tensor_shouldSummarizeDim(tensor, dim))
        return TRUE;
    return index < 3 || index >= tensor->shape[dim] - 3;
}

static void _tensor_appendToSb(StringBuilder *sb, PiTensor *tensor, int dim, int *indices)
{
    if (dim == tensor->ndim)
    {
        char *item = _format_number(tensor_get(tensor, indices));
        _sb_append(sb, item);
        free(item);
        return;
    }

    _sb_append(sb, "[");
    bool wrote = FALSE;
    bool ellipsis = FALSE;

    for (int i = 0; i < tensor->shape[dim]; i++)
    {
        if (!_tensor_shouldPrintIndex(tensor, dim, i))
        {
            if (!ellipsis)
            {
                if (wrote)
                    _sb_append(sb, ", ");
                _sb_append(sb, "...");
                wrote = TRUE;
                ellipsis = TRUE;
            }
            continue;
        }
        if (wrote)
            _sb_append(sb, ", ");
        indices[dim] = i;
        _tensor_appendToSb(sb, tensor, dim + 1, indices);
        wrote = TRUE;
    }
    _sb_append(sb, "]");
}

/* Helper: malloc a small formatted string. */
static char *_fmt_alloc(size_t n, const char *fmt, ...)
{
    char *buf = malloc(n);
    if (!buf) return NULL;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return buf;
}

char *as_stringWithFormat(VM *vm, Value val)
{
    if (vm && IS_INSTANCE(val))
    {
        Value formatted = vm_callMethodNoArgs(vm, val, "format");
        if (!(IS_OBJ(formatted) && AS_OBJ(formatted) == AS_OBJ(val)))
            return as_stringWithFormat(vm, formatted);
    }

    switch (val.type)
    {
    case VAL_NUM:  return _format_number(val.data.number);
    case VAL_BOOL: return _dup_Cstring(val.data.boolean ? "true" : "false");
    case VAL_NIL:  return _dup_Cstring("nil");
    case VAL_OBJ:
        switch (AS_OBJ(val)->type)
        {
        case OBJ_STRING:
            return _dup_Cstring(AS_STRING(val)->chars);

        case OBJ_LIST:
        {
            List *list = as_list(val);
            StringBuilder sb; _sb_init(&sb, "[");
            for (size_t i = 0; i < (size_t)list->size; i++)
            {
                if (i) _sb_append(&sb, ", ");
                char *item = as_stringWithFormat(vm, *(Value *)_list_getAt(list, i));
                _sb_append(&sb, item); free(item);
            }
            _sb_append(&sb, "]");
            return _sb_finish(&sb);
        }

        case OBJ_MAP:
        {
            PiMap *map = AS_MAP(val);
            StringBuilder sb; _sb_init(&sb, "{");
            bool first = TRUE;
            HT_Iterator it = _ht_iterator(map->table);
            while (_ht_next(&it))
            {
                if (!first) _sb_append(&sb, ", ");
                first = FALSE;
                _sb_append(&sb, it.key);
                _sb_append(&sb, ": ");
                char *vstr = as_stringWithFormat(vm, *(Value *)it.value);
                _sb_append(&sb, vstr); free(vstr);
            }
            _sb_append(&sb, "}");
            return _sb_finish(&sb);
        }

        case OBJ_CLASS:
        {
            PiClass *c = AS_CLASS(val);
            return _fmt_alloc(256, "<class %s>", c->name ? c->name : "<anonymous>");
        }

        case OBJ_INSTANCE:
        {
            PiInstance *inst = AS_INSTANCE(val);
            const char *name = inst->_class && inst->_class->name
                                   ? inst->_class->name : "<anonymous>";
            return _fmt_alloc(256, "<instance %s>", name);
        }

        case OBJ_SET:
        {
            PiSet *set = AS_SET(val);
            int size   = set_size(set);
            if (size == 0) return _dup_Cstring("{}");
            StringBuilder sb; _sb_init(&sb, "{");
            for (int i = 0; i < size; i++)
            {
                if (i) _sb_append(&sb, ", ");
                char *item = as_stringWithFormat(vm, set_get(set, i));
                _sb_append(&sb, item); free(item);
            }
            _sb_append(&sb, "}");
            return _sb_finish(&sb);
        }

        case OBJ_TUPLE:
        {
            PiTuple *tuple = AS_TUPLE(val);
            int size       = LIST_SIZE(tuple->items);
            StringBuilder sb; _sb_init(&sb, "(");
            for (int i = 0; i < size; i++)
            {
                if (i) _sb_append(&sb, ", ");
                char *item = as_stringWithFormat(vm, *(Value *)_list_getAt(tuple->items, i));
                _sb_append(&sb, item); free(item);
            }
            if (size == 1) _sb_append(&sb, ",");
            _sb_append(&sb, ")");
            return _sb_finish(&sb);
        }

        case OBJ_TENSOR:
        {
            StringBuilder sb; _sb_init(&sb, "");
            int indices[MAX_TENSOR_DIMS] = {0};
            _tensor_appendToSb(&sb, AS_TENSOR(val), 0, indices);
            return _sb_finish(&sb);
        }

        case OBJ_RANGE:
        {
            PiRange *r = AS_RANGE(val);
            char *start = _format_number(r->start);
            char *end   = _format_number(r->end);
            StringBuilder sb; _sb_init(&sb, start);
            _sb_append(&sb, ".."); _sb_append(&sb, end);
            free(start); free(end);
            if (r->step != 1.0)
            {
                char *step = _format_number(r->step);
                _sb_append(&sb, ":"); _sb_append(&sb, step);
                free(step);
            }
            return _sb_finish(&sb);
        }

        case OBJ_FUN:
            return _fmt_alloc(128, "<FUN: %s>", AS_FUN(val)->name);

        case OBJ_MODULE:
        {
            ObjModule *m = AS_MODULE(val);
            return _fmt_alloc(256, "<module %s>", m->name ? m->name : "<anonymous>");
        }

        case OBJ_FILE:
        {
            ObjFile *f = AS_FILE(val);
            return _fmt_alloc(256, "<file %s>", f->filename ? f->filename : "<anonymous>");
        }

        case OBJ_SOCKET:
            return _dup_Cstring("<socket>");

#ifndef __EMSCRIPTEN__
        case OBJ_IMAGE:
        {
            ObjImage *img = AS_IMAGE(val);
            return _fmt_alloc(128, "<image %dx%d>", img->surface->w, img->surface->h);
        }

        case OBJ_AUDIO:
        {
            ObjAudio *audio = AS_AUDIO(val);
            return _fmt_alloc(128, "<audio %d Hz, %u channel%s>", audio->spec.freq,
                             audio->spec.channels, audio->spec.channels == 1 ? "" : "s");
        }
#endif

        default:
            return NULL;
        }
    default:
        return NULL;
    }
}

char *as_string(Value val) { return as_stringWithFormat(NULL, val); }

static uint64_t _hash_mix(uint64_t hash, uint64_t value)
{
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

uint64_t value_hash(Value val)
{
    switch (val.type)
    {
    case VAL_NUM:
    {
        if (isnan(val.data.number))
            return 0x6e616eull;
        double n = val.data.number == 0.0 ? 0.0 : val.data.number;
        uint64_t bits = 0;
        memcpy(&bits, &n, sizeof(bits));
        return _hash_mix(0x6e3aull, bits);
    }
    case VAL_BOOL:
        return val.data.boolean ? 0x623a31ull : 0x623a30ull;
    case VAL_NIL:
        return 0x7a3a6e696cull;
    case VAL_OBJ:
    {
        Object *obj = AS_OBJ(val);
        if (obj->type == OBJ_STRING)
            return _hash_mix(0x733aull, AS_STRING(val)->hash);
        return _hash_mix(0x6f3aull, obj->id);
    }
    }
    return 0;
}

bool value_keyEquals(Value l, Value r)
{
    if (l.type != r.type) return FALSE;
    switch (l.type)
    {
    case VAL_NUM:
        return (isnan(l.data.number) && isnan(r.data.number)) ||
               l.data.number == r.data.number;
    case VAL_BOOL: return l.data.boolean == r.data.boolean;
    case VAL_NIL:  return TRUE;
    case VAL_OBJ:
    {
        Object *lo = AS_OBJ(l), *ro = AS_OBJ(r);
        if (lo->type != ro->type) return FALSE;
        if (lo->type == OBJ_STRING)
        {
            PiString *a = AS_STRING(l), *b = AS_STRING(r);
            return a->length == b->length && memcmp(a->chars, b->chars, a->length) == 0;
        }
        return lo->id == ro->id;
    }
    }
    return FALSE;
}

List *as_list(Value val)
{
    if (val.type == VAL_OBJ && OBJ_TYPE(val) == OBJ_LIST)
        return AS_LIST(val)->items;
    error("Expected a list, but got %s", type_name(val));
}

bool is_numeric(Value val)
{
    switch (val.type)
    {
    case VAL_NUM:
    case VAL_BOOL:
    case VAL_NIL:
        return TRUE;

    case VAL_OBJ:
        if (OBJ_TYPE(val) != OBJ_STRING)
            return FALSE;

        const char *text = AS_STRING(val)->chars;
        if (*text == '\0')
            return FALSE;

        char *end;
        strtod(text, &end);
        return *end == '\0';

    default:
        return FALSE;
    }
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
            PiString *original = (PiString *)obj;
            PiString *string = malloc(sizeof(PiString));

            string->object.type = OBJ_STRING;
            string->length = original->length;
            string->chars = malloc(string->length + 1);

            memcpy(string->chars, original->chars, string->length + 1);

            copy.data.object = (Object *)string;
            break;
        }
        case OBJ_LIST:
        {
            PiList *original = (PiList *)obj;
            PiList *list = malloc(sizeof(PiList));

            list->object.type = OBJ_LIST;
            list->items = list_create(sizeof(Value));

            list->current = 0;
            for (size_t i = 0; i < LIST_SIZE(original->items); i++)
            {
                Value item = copy_value(*(Value *)_list_getAt(original->items, i));
                _list_add(list->items, &item);
            }
            copy.data.object = (Object *)list;
            break;
        }
        case OBJ_TENSOR:
        {
            PiTensor *original = (PiTensor *)obj;
            PiTensor *tensor = (PiTensor *)new_tensor(original->ndim,
                                                      original->shape, original->type);
            for (int i = 0; i < original->size; i++)
                tensor_setFlat(tensor, i, tensor_getFlat(original, i));

            copy.data.object = (Object *)tensor;
            break;
        }
        case OBJ_MAP:
        {
            // TODO: double check in the future
            PiMap *original = (PiMap *)obj;
            PiMap *map = malloc(sizeof(PiMap));

            map->object.type = OBJ_MAP;
            map->table = ht_create(sizeof(Value));
            map->it = _ht_iterator(map->table);

            HT_Iterator it = _ht_iterator(original->table);
            while (_ht_next(&it))
            {
                Value value = copy_value(*(Value *)it.value);
                ht_put(map->table, it.key, &value);
            }

            copy.data.object = (Object *)map;
            break;
        }
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
        char *text = _format_number(val.data.number);
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
        case OBJ_AUDIO:
            printf("<audio %d Hz, %u channel%s>", AS_AUDIO(val)->spec.freq,
                   AS_AUDIO(val)->spec.channels,
                   AS_AUDIO(val)->spec.channels == 1 ? "" : "s");
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
    case VAL_NUM:   return "number";
    case VAL_BOOL:  return "boolean";
    case VAL_NIL:   return "nil";
    case VAL_OBJ:
        switch (AS_OBJ(val)->type)
        {
        case OBJ_STRING:   return "string";
        case OBJ_LIST:     return "list";
        case OBJ_TENSOR:   return "tensor";
        case OBJ_MAP:      return "map";
        case OBJ_CLASS:    return "class";
        case OBJ_INSTANCE:
        {
            PiInstance *inst = AS_INSTANCE(val);
            return inst->_class && inst->_class->name ? inst->_class->name : "instance";
        }
        case OBJ_SET:      return "set";
        case OBJ_TUPLE:    return "tuple";
        case OBJ_MODULE:   return "module";
        case OBJ_RANGE:    return "range";
        case OBJ_SLICE:    return "slice";
        case OBJ_FUN:      return "function";
        case OBJ_CODE:     return "code";
        case OBJ_FILE:     return "file";
        case OBJ_SOCKET:   return "socket";
        case OBJ_MODEL3D:  return "model3d";
        case OBJ_IMAGE:    return "image";
        case OBJ_AUDIO:    return "audio";
        case OBJ_CONTEXT:  return "context";
        case OBJ_CHART:    return "chart";
        case OBJ_CHART3D:  return "chart3d";
        case OBJ_EVENT:    return "event";
        default:           return "undefined";
        }
    }
    return NULL;
}
