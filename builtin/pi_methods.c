#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "pi_methods.h"
#include "_pi_string.h"
#include "pi_col.h"
#include "pi_tensor.h"

static NativeMethod native_methods[] = {
    {OBJ_STRING, "trim", pi_trim},
    {OBJ_STRING, "upper", pi_upper},
    {OBJ_STRING, "lower", pi_lower},
    {OBJ_STRING, "append", cl_append},
    {OBJ_STRING, "push", pi_push},
    {OBJ_STRING, "pop", pi_pop},
    {OBJ_STRING, "peek", pi_peek},
    {OBJ_STRING, "len", pi_len},
    {OBJ_STRING, "length", pi_len},
    {OBJ_STRING, "contains", cl_contains},
    {OBJ_STRING, "includes", cl_contains},
    {OBJ_STRING, "index", cl_indexOf},
    {OBJ_STRING, "indexOf", cl_indexOf},
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
    {OBJ_LIST, "length", pi_len},
    {OBJ_LIST, "contains", cl_contains},
    {OBJ_LIST, "includes", cl_contains},
    {OBJ_LIST, "index", cl_indexOf},
    {OBJ_LIST, "indexOf", cl_indexOf},
    {OBJ_LIST, "count", cl_count},
    {OBJ_LIST, "concat", cl_concat},
    {OBJ_LIST, "repeat", cl_repeat},
    {OBJ_LIST, "copy", cl_copy},
    {OBJ_LIST, "reverse", cl_reverse},
    {OBJ_LIST, "sort", cl_sort},
    {OBJ_LIST, "shuffle", cl_shuffle},

    {OBJ_TUPLE, "len", pi_len},
    {OBJ_TUPLE, "length", pi_len},
    {OBJ_TUPLE, "contains", cl_contains},
    {OBJ_TUPLE, "includes", cl_contains},
    {OBJ_TUPLE, "index", cl_indexOf},
    {OBJ_TUPLE, "indexOf", cl_indexOf},
    {OBJ_TUPLE, "count", cl_count},
    {OBJ_TUPLE, "concat", cl_concat},
    {OBJ_TUPLE, "repeat", cl_repeat},

    {OBJ_SET, "add", cl_add},
    {OBJ_SET, "clear", cl_clear},
    {OBJ_SET, "len", pi_len},
    {OBJ_SET, "length", pi_len},
    {OBJ_SET, "contains", cl_contains},
    {OBJ_SET, "includes", cl_contains},
    {OBJ_SET, "union", pi_union},
    {OBJ_SET, "intersection", pi_intersection},
    {OBJ_SET, "difference", pi_difference},
    {OBJ_SET, "symmetric_diff", pi_symmetricDiff},
    {OBJ_SET, "symmetricDiff", pi_symmetricDiff},
    {OBJ_SET, "issubset", pi_issubset},
    {OBJ_SET, "isSubset", pi_issubset},
    {OBJ_SET, "issuperset", pi_issuperset},
    {OBJ_SET, "isSuperset", pi_issuperset},
    {OBJ_SET, "isdisjoint", pi_isdisjoint},
    {OBJ_SET, "isDisjoint", pi_isdisjoint},

    {OBJ_TENSOR, "shape", tn_shape},
    {OBJ_TENSOR, "ndim", tn_ndim},
    {OBJ_TENSOR, "size", tn_size},
    {OBJ_TENSOR, "reshape", tn_reshape},
    {OBJ_TENSOR, "slice", tn_slice},
    {OBJ_TENSOR, "concat", tn_concat},
    {OBJ_TENSOR, "transpose", tn_transpose},
    {OBJ_TENSOR, "flatten", tn_flatten},
    {OBJ_TENSOR, "expand_dims", tn_expandDims},
    {OBJ_TENSOR, "expandDims", tn_expandDims},
    {OBJ_TENSOR, "squeeze", tn_squeeze},
    {OBJ_TENSOR, "is_tensor", tn_isTensor},
    {OBJ_TENSOR, "isTensor", tn_isTensor},
    {OBJ_TENSOR, "is_matrix", tn_isMatrix},
    {OBJ_TENSOR, "isMatrix", tn_isMatrix},
    {OBJ_TENSOR, "is_vector", tn_isVector},
    {OBJ_TENSOR, "isVector", tn_isVector},
    {OBJ_TENSOR, "is_scalar", tn_isScalar},
    {OBJ_TENSOR, "isScalar", tn_isScalar},
    {OBJ_TENSOR, "add", tn_add},
    {OBJ_TENSOR, "sub", tn_sub},
    {OBJ_TENSOR, "mult", tn_mult},
    {OBJ_TENSOR, "div", tn_div},
    {OBJ_TENSOR, "exp", tn_exp},
    {OBJ_TENSOR, "log", tn_log},
    {OBJ_TENSOR, "sqrt", tn_sqrt},
    {OBJ_TENSOR, "abs", tn_abs},
    {OBJ_TENSOR, "clip", tn_clip},
    {OBJ_TENSOR, "sign", tn_sign},
    {OBJ_TENSOR, "apply", tn_apply},
    {OBJ_TENSOR, "matmult", tn_matmult},
    {OBJ_TENSOR, "matmul", tn_matmult},
    {OBJ_TENSOR, "dot", tn_dot},
    {OBJ_TENSOR, "cross", tn_cross},
    {OBJ_TENSOR, "solve", tn_solve},
    {OBJ_TENSOR, "inv", tn_inv},
    {OBJ_TENSOR, "det", tn_det},
    {OBJ_TENSOR, "svd", tn_svd},
    {OBJ_TENSOR, "eig", tn_eig},
    {OBJ_TENSOR, "norm", tn_norm},
    {OBJ_TENSOR, "rank", tn_rank},
    {OBJ_TENSOR, "trace", tn_trace},
    {OBJ_TENSOR, "pinv", tn_pinv},
    {OBJ_TENSOR, "sum", tn_sum},
    {OBJ_TENSOR, "mean", tn_mean},
    {OBJ_TENSOR, "min", tn_min},
    {OBJ_TENSOR, "max", tn_max},
    {OBJ_TENSOR, "prod", tn_prod},
    {OBJ_TENSOR, "argmax", tn_argmax},
    {OBJ_TENSOR, "argmin", tn_argmin},
    {OBJ_TENSOR, "any", tn_any},
    {OBJ_TENSOR, "all", tn_all},
    {OBJ_TENSOR, "reduce", tn_reduce},
    {OBJ_TENSOR, "var", tn_var},
    {OBJ_TENSOR, "std", tn_std},
    {OBJ_TENSOR, "median", tn_median},
    {OBJ_TENSOR, "percentile", tn_percentile},
    {OBJ_TENSOR, "mode", tn_mode},
    {OBJ_TENSOR, "covariance", tn_covariance},
    {OBJ_TENSOR, "correlation", tn_correlation},
    {OBJ_TENSOR, "zscore", tn_zscore},
    {OBJ_TENSOR, "shuffle", tn_shuffle},
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

void pi_nativeMethodNames(o_type type, char *buffer, size_t size)
{
    if (!buffer || size == 0)
        return;

    buffer[0] = '\0';
    size_t used = 0;

    for (int i = 0; i < native_methodCount; i++)
    {
        NativeMethod *method = &native_methods[i];
        if (method->type != type)
            continue;

        bool seen = false;
        for (int j = 0; j < i; j++)
        {
            NativeMethod *previous = &native_methods[j];
            if (previous->type == type && strcmp(previous->name, method->name) == 0)
            {
                seen = true;
                break;
            }
        }

        if (seen)
            continue;

        int written = snprintf(buffer + used, size - used, "%s%s",
                               used > 0 ? ", " : "", method->name);
        if (written < 0)
            return;

        if ((size_t)written >= size - used)
        {
            buffer[size - 1] = '\0';
            return;
        }

        used += (size_t)written;
    }
}
