#include "reduce.h"

#include "reduce.h"
#include "../../list.h"
#include "../pi_builtin.h"
#include <math.h>

// Reduction Engine

typedef double (*ReduceFunc)(double accum, double val, int count);

static Value matrix_reduce(vm_t *vm, MatrixView *view, int axis, ReduceFunc fn, double init)
{
    if (axis == -1)
    {
        double accum = init;
        int count = 0;
        for (int r = 0; r < view->rows; r++)
            for (int c = 0; c < view->cols; c++)
                accum = fn(accum, get_matrixView(view, r, c), ++count);
        return NEW_NUM(accum);
    }

    if (axis == 0) // reduce along rows -> 1 x cols
    {
        PiMatrix *result = create_denseMatrix(1, view->cols);
        for (int c = 0; c < view->cols; c++)
        {
            double accum = init;
            int count = 0;
            for (int r = 0; r < view->rows; r++)
                accum = fn(accum, get_matrixView(view, r, c), ++count);
            matrix_set(result, 0, c, accum);
        }
        return NEW_OBJ(result);
    }

    if (axis == 1) // reduce along cols -> rows x 1
    {
        PiMatrix *result = create_denseMatrix(view->rows, 1);
        for (int r = 0; r < view->rows; r++)
        {
            double accum = init;
            int count = 0;
            for (int c = 0; c < view->cols; c++)
                accum = fn(accum, get_matrixView(view, r, c), ++count);
            matrix_set(result, r, 0, accum);
        }
        return NEW_OBJ(result);
    }

    vm_error(vm, "reduce: axis must be -1, 0, or 1.");
    return NEW_NUM(0); // unreachable
}

// Reduce Functions

static double reduce_sum(double accum, double val, int count)
{
    (void)count;
    return accum + val;
}

static double reduce_mean(double accum, double val, int count)
{
    // Welford's online update — avoids catastrophic cancellation on large sets
    return accum + (val - accum) / count;
}

static double reduce_min(double accum, double val, int count)
{
    (void)count;
    return val < accum ? val : accum;
}

static double reduce_max(double accum, double val, int count)
{
    (void)count;
    return val > accum ? val : accum;
}

static double reduce_prod(double accum, double val, int count)
{
    (void)count;
    return accum * val;
}

// Arg Reduction Engine

static Value matrix_argreduce(vm_t *vm, MatrixView *view, int axis, bool want_max)
{
    // guard: empty matrix
    if (view->rows == 0 || view->cols == 0)
        vm_error(vm, "argmax/argmin: matrix is empty.");

    if (axis == -1) // flat index into row-major layout
    {
        int best_idx = 0;
        double best_val = get_matrixView(view, 0, 0);

        for (int r = 0; r < view->rows; r++)
            for (int c = 0; c < view->cols; c++)
            {
                double val = get_matrixView(view, r, c);
                if ((want_max && val > best_val) || (!want_max && val < best_val))
                {
                    best_val = val;
                    best_idx = r * view->cols + c;
                }
            }
        return NEW_NUM(best_idx);
    }

    if (axis == 0) // best row index per column -> 1 x cols
    {
        PiMatrix *result = create_denseMatrix(1, view->cols);
        for (int c = 0; c < view->cols; c++)
        {
            int best = 0;
            double best_val = get_matrixView(view, 0, c);
            for (int r = 1; r < view->rows; r++)
            {
                double val = get_matrixView(view, r, c);
                if ((want_max && val > best_val) || (!want_max && val < best_val))
                {
                    best_val = val;
                    best = r;
                }
            }
            matrix_set(result, 0, c, (double)best);
        }
        return NEW_OBJ(result);
    }

    if (axis == 1) // best col index per row -> rows x 1
    {
        PiMatrix *result = create_denseMatrix(view->rows, 1);
        for (int r = 0; r < view->rows; r++)
        {
            int best = 0;
            double best_val = get_matrixView(view, r, 0);
            for (int c = 1; c < view->cols; c++)
            {
                double val = get_matrixView(view, r, c);
                if ((want_max && val > best_val) || (!want_max && val < best_val))
                {
                    best_val = val;
                    best = c;
                }
            }
            matrix_set(result, r, 0, (double)best);
        }
        return NEW_OBJ(result);
    }

    vm_error(vm, "argmax/argmin: axis must be -1, 0, or 1.");
    return NEW_NUM(0); // unreachable
}

// Argument Parsing Helper

// Parses (matrix) or (matrix, axis) — axis defaults to -1
static bool matrix_parseAxis(vm_t *vm, int argc, Value *argv,
                              MatrixView *view, int *axis, const char *fname)
{
    if (argc < 1 || argc > 2 || !matrix_view(argv[0], view))
    {
        vm_errorf(vm, "%s: Expected a matrix and optional axis (0, 1, or -1).", fname);
        return false;
    }

    *axis = -1;
    if (argc == 2)
    {
        if (!IS_NUM(argv[1]))
        {
            vm_errorf(vm, "%s: axis must be a number.", fname);
            return false;
        }
        *axis = (int)AS_NUM(argv[1]);
    }

    return true;
}

// Reductions

Value mat_sum(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    int axis;
    matrix_parseAxis(vm, argc, argv, &view, &axis, "sum");
    return matrix_reduce(vm, &view, axis, reduce_sum, 0.0);
}

Value mat_mean(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    int axis;
    matrix_parseAxis(vm, argc, argv, &view, &axis, "mean");

    // mean needs an empty-check before dividing
    if (view.rows == 0 || view.cols == 0)
        return NEW_NUM(NAN);

    return matrix_reduce(vm, &view, axis, reduce_mean, 0.0);
}

Value mat_min(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    int axis;
    matrix_parseAxis(vm, argc, argv, &view, &axis, "min");

    if (view.rows == 0 || view.cols == 0)
        vm_error(vm, "min: matrix is empty.");

    return matrix_reduce(vm, &view, axis, reduce_min, INFINITY);
}

Value mat_max(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    int axis;
    matrix_parseAxis(vm, argc, argv, &view, &axis, "max");

    if (view.rows == 0 || view.cols == 0)
        vm_error(vm, "max: matrix is empty.");

    return matrix_reduce(vm, &view, axis, reduce_max, -INFINITY);
}

Value mat_prod(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    int axis;
    matrix_parseAxis(vm, argc, argv, &view, &axis, "prod");
    return matrix_reduce(vm, &view, axis, reduce_prod, 1.0);
}

// Arg Operations

Value mat_argmax(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    int axis;
    matrix_parseAxis(vm, argc, argv, &view, &axis, "argmax");
    return matrix_argreduce(vm, &view, axis, true);
}

Value mat_argmin(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    int axis;
    matrix_parseAxis(vm, argc, argv, &view, &axis, "argmin");
    return matrix_argreduce(vm, &view, axis, false);
}

// Boolean Reductions

Value mat_any(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    int axis;
    matrix_parseAxis(vm, argc, argv, &view, &axis, "any");

    if (axis == -1)
    {
        for (int r = 0; r < view.rows; r++)
            for (int c = 0; c < view.cols; c++)
                if (get_matrixView(&view, r, c) != 0.0)
                    return NEW_BOOL(true);
        return NEW_BOOL(false);
    }

    if (axis == 0) // any nonzero in each column -> 1 x cols of bools
    {
        PiMatrix *result = create_denseMatrix(1, view.cols);
        for (int c = 0; c < view.cols; c++)
        {
            bool found = false;
            for (int r = 0; r < view.rows; r++)
                if (get_matrixView(&view, r, c) != 0.0)
                {
                    found = true;
                    break;
                }
            matrix_set(result, 0, c, found ? 1.0 : 0.0);
        }
        return NEW_OBJ(result);
    }

    if (axis == 1) // any nonzero in each row -> rows x 1 of bools
    {
        PiMatrix *result = create_denseMatrix(view.rows, 1);
        for (int r = 0; r < view.rows; r++)
        {
            bool found = false;
            for (int c = 0; c < view.cols; c++)
                if (get_matrixView(&view, r, c) != 0.0)
                {
                    found = true;
                    break;
                }
            matrix_set(result, r, 0, found ? 1.0 : 0.0);
        }
        return NEW_OBJ(result);
    }

    vm_error(vm, "any: axis must be -1, 0, or 1.");
    return NEW_BOOL(false); // unreachable
}

Value mat_all(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    int axis;
    matrix_parseAxis(vm, argc, argv, &view, &axis, "all");

    if (axis == -1)
    {
        for (int r = 0; r < view.rows; r++)
            for (int c = 0; c < view.cols; c++)
                if (get_matrixView(&view, r, c) == 0.0)
                    return NEW_BOOL(false);
        return NEW_BOOL(true);
    }

    if (axis == 0) // all nonzero in each column -> 1 x cols
    {
        PiMatrix *result = create_denseMatrix(1, view.cols);
        for (int c = 0; c < view.cols; c++)
        {
            bool all_nz = true;
            for (int r = 0; r < view.rows; r++)
                if (get_matrixView(&view, r, c) == 0.0)
                {
                    all_nz = false;
                    break;
                }
            matrix_set(result, 0, c, all_nz ? 1.0 : 0.0);
        }
        return NEW_OBJ(result);
    }

    if (axis == 1) // all nonzero in each row -> rows x 1
    {
        PiMatrix *result = create_denseMatrix(view.rows, 1);
        for (int r = 0; r < view.rows; r++)
        {
            bool all_nz = true;
            for (int c = 0; c < view.cols; c++)
                if (get_matrixView(&view, r, c) == 0.0)
                {
                    all_nz = false;
                    break;
                }
            matrix_set(result, r, 0, all_nz ? 1.0 : 0.0);
        }
        return NEW_OBJ(result);
    }

    vm_error(vm, "all: axis must be -1, 0, or 1.");
    return NEW_BOOL(false); // unreachable
}

// Module Registration
static BuiltinFunc reduce_funcs[] = {
    {"sum", mat_sum},
    {"mean", mat_mean},
    {"min", mat_min},
    {"max", mat_max},
    {"prod", mat_prod},
    {"argmax", mat_argmax},
    {"argmin", mat_argmin},
    {"any", mat_any},
    {"all", mat_all},
};

DEFINE_BUILTIN_MODULE(module_matReduce, "matrix.reduce", reduce_funcs, NULL);

