#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "pi_methods.h"
#include "_pi_string.h"
#include "pi_col.h"
#include "pi_func.h"
#include "pi_tensor.h"
#include "pi_plot3d.h"
#include "pi_draw.h"
#include "pi_plot.h"

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
    {OBJ_STRING, "split", st_split},

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
    {OBJ_LIST, "join", cl_join},
    {OBJ_LIST, "reverse", cl_reverse},
    {OBJ_LIST, "flat", cl_flat},
    {OBJ_LIST, "sort", cl_sort},
    {OBJ_LIST, "shuffle", cl_shuffle},
    {OBJ_LIST, "map", _pi_map},

    {OBJ_TUPLE, "len", pi_len},
    {OBJ_TUPLE, "length", pi_len},
    {OBJ_TUPLE, "contains", cl_contains},
    {OBJ_TUPLE, "includes", cl_contains},
    {OBJ_TUPLE, "index", cl_indexOf},
    {OBJ_TUPLE, "indexOf", cl_indexOf},
    {OBJ_TUPLE, "count", cl_count},
    {OBJ_TUPLE, "concat", cl_concat},
    {OBJ_TUPLE, "repeat", cl_repeat},
    {OBJ_TUPLE, "join", cl_join},

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

    {OBJ_CONTEXT, "chart", pt3d_chart},
    {OBJ_CONTEXT, "clear", dw_clear},
    {OBJ_CONTEXT, "pixel", dw_pixel},
    {OBJ_CONTEXT, "line", dw_line},
    {OBJ_CONTEXT, "triangle", dw_triangle},
    {OBJ_CONTEXT, "rect", dw_rect},
    {OBJ_CONTEXT, "polygon", dw_polygon},
    {OBJ_CONTEXT, "circle", dw_circle},
    {OBJ_CONTEXT, "translate", dw_translate},
    {OBJ_CONTEXT, "scale", dw_scale},
    {OBJ_CONTEXT, "rotate", dw_rotate},
    {OBJ_CONTEXT, "alpha", dw_alpha},
    {OBJ_CONTEXT, "text", dw_text},
    {OBJ_CONTEXT, "image", dw_image},
    {OBJ_CONTEXT, "push", dw_push},
    {OBJ_CONTEXT, "pop", dw_pop},
    {OBJ_CONTEXT, "cursor", dw_cursor},
    {OBJ_CONTEXT, "present", dw_present},
    {OBJ_CONTEXT, "animate", dw_animate},
    {OBJ_CONTEXT, "on", dw_on},
    {OBJ_CONTEXT, "off", dw_off},
    {OBJ_CONTEXT, "close", dw_close},
    {OBJ_CONTEXT, "run", dw_run},

    {OBJ_CHART3D, "surface", pt3d_surface},
    {OBJ_CHART3D, "mesh", pt3d_mesh},
    {OBJ_CHART3D, "wireframe", pt3d_wireframe},
    {OBJ_CHART3D, "scatter", pt3d_scatter},
    {OBJ_CHART3D, "show", pt3d_show},
    {OBJ_CHART3D, "title", pt3d_title},
    {OBJ_CHART3D, "xlabel", pt3d_xlabel},
    {OBJ_CHART3D, "ylabel", pt3d_ylabel},
    {OBJ_CHART3D, "zlabel", pt3d_zlabel},
    {OBJ_CHART3D, "grid", pt3d_grid},
    {OBJ_CHART3D, "view", pt3d_view},
    {OBJ_CHART3D, "subplot", pt3d_subplot},

    {OBJ_CHART, "func", pt_func},
    {OBJ_CHART, "scatter", pt_scatter},
    {OBJ_CHART, "line", pt_line},
    {OBJ_CHART, "bar", pt_bar},
    {OBJ_CHART, "hist", pt_hist},
    {OBJ_CHART, "step", pt_step},
    {OBJ_CHART, "imshow", pt_imshow},
    {OBJ_CHART, "heatmap", pt_heatmap},
    {OBJ_CHART, "contour", pt_contour},
    {OBJ_CHART, "quiver", pt_quiver},
    {OBJ_CHART, "streamplot", pt_streamplot},
    {OBJ_CHART, "show", pt_show},
    {OBJ_CHART, "title", pt_title},
    {OBJ_CHART, "xlabel", pt_xlabel},
    {OBJ_CHART, "ylabel", pt_ylabel},
    {OBJ_CHART, "grid", pt_grid},
    {OBJ_CHART, "axes", pt_axes},
    {OBJ_CHART, "tick", pt_tick},
    {OBJ_CHART, "legend", pt_legend},
    {OBJ_CHART, "subplot", pt_subplot},
};

static int native_methodCount = sizeof(native_methods) / sizeof(NativeMethod);

NativeMethod *pi_nativeMethodFor(ObjectType type, const char *name)
{
    /* Methods are grouped by receiver type.  Start in the relevant group so
     * a hot `list.push()` lookup does not scan the string-method group (and
     * every other type) first. */
    int first = 0;
    int last = native_methodCount;
    switch (type)
    {
    case OBJ_STRING:
        first = 0;
        last = 18;
        break;
    case OBJ_LIST:
        first = 18;
        last = 40;
        break;
    case OBJ_TUPLE:
        first = 40;
        last = 50;
        break;
    case OBJ_SET:
        first = 50;
        last = 67;
        break;
    case OBJ_TENSOR:
        first = 67;
        last = 129;
        break;
    case OBJ_CONTEXT:
        first = 129;
        last = 152;
        break;
    case OBJ_CHART3D:
        first = 152;
        last = 165;
        break;
    case OBJ_CHART:
        first = 165;
        break;
    default:
        return NULL;
    }

    for (int i = first; i < last; i++)
    {
        NativeMethod *method = &native_methods[i];
        if (strcmp(method->name, name) == 0)
            return method;
    }

    return NULL;
}

void pi_nativeMethodNames(ObjectType type, char *buffer, size_t size)
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

        bool seen = FALSE;
        for (int j = 0; j < i; j++)
        {
            NativeMethod *previous = &native_methods[j];
            if (previous->type == type && strcmp(previous->name, method->name) == 0)
            {
                seen = TRUE;
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
