#include <string.h>

#include "pi_methods.h"
#include "_pi_string.h"
#include "pi_col.h"

static NativeMethod native_methods[] = {
    {OBJ_STRING, "trim", pi_trim},
    {OBJ_STRING, "upper", pi_upper},
    {OBJ_STRING, "lower", pi_lower},
    {OBJ_STRING, "append", cl_append},
    {OBJ_STRING, "push", pi_push},
    {OBJ_STRING, "pop", pi_pop},
    {OBJ_STRING, "peek", pi_peek},
    {OBJ_STRING, "len", pi_len},
    {OBJ_STRING, "contains", cl_contains},
    {OBJ_STRING, "index", cl_indexOf},
    {OBJ_STRING, "count", cl_count},
    {OBJ_STRING, "repeat", cl_repeat},
    {OBJ_STRING, "copy", cl_copy},
    {OBJ_STRING, "reverse", cl_reverse},

    {OBJ_LIST, "append", cl_append},
    {OBJ_LIST, "push", pi_push},
    {OBJ_LIST, "pop", pi_pop},
    {OBJ_LIST, "peek", pi_peek},
    {OBJ_LIST, "insert", pi_insert},
    {OBJ_LIST, "remove", pi_remove},
    {OBJ_LIST, "len", pi_len},
    {OBJ_LIST, "contains", cl_contains},
    {OBJ_LIST, "index", cl_indexOf},
    {OBJ_LIST, "count", cl_count},
    {OBJ_LIST, "concat", cl_concat},
    {OBJ_LIST, "repeat", cl_repeat},
    {OBJ_LIST, "copy", cl_copy},
    {OBJ_LIST, "reverse", cl_reverse},
    {OBJ_LIST, "sort", cl_sort},
    {OBJ_LIST, "shuffle", cl_shuffle},

    {OBJ_TUPLE, "len", pi_len},
    {OBJ_TUPLE, "contains", cl_contains},
    {OBJ_TUPLE, "index", cl_indexOf},
    {OBJ_TUPLE, "count", cl_count},
    {OBJ_TUPLE, "concat", cl_concat},
    {OBJ_TUPLE, "repeat", cl_repeat},
};

static int native_methodCount = sizeof(native_methods) / sizeof(NativeMethod);

NativeMethod *pi_nativeMethodFor(o_type type, const char *name)
{
    for (int i = 0; i < native_methodCount; i++)
    {
        NativeMethod *method = &native_methods[i];
        if (method->type == type && strcmp(method->name, name) == 0)
            return method;
    }

    return NULL;
}
