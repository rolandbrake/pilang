#include "transform.h"
#include "../pi_builtin.h"
#include <math.h>
#include <stdlib.h>

// Internal Helpers

// Determine if a value is a scalar number
static bool is_scalar(Value v) { return IS_NUM(v); }

// Apply a C function (double -> double) element-wise, return new matrix
static Value elementwise_unary(vm_t *vm, MatrixView *view,
                               double (*fn)(double), const char *fname)
{
    (void)vm;
    (void)fname;
    PiMatrix *result = create_denseMatrix(view->rows, view->cols);
    for (int r = 0; r < view->rows; r++)
        for (int c = 0; c < view->cols; c++)
            matrix_set(result, r, c, fn(get_matrixView(view, r, c)));
    return NEW_OBJ(result);
}

// Broadcasting Binary Engine
//
// Supports four cases:
//   matrix OP matrix  (same shape)
//   matrix OP scalar
//   scalar OP matrix
//   scalar OP scalar  (just returns a number)

typedef double (*BinaryOp)(double a, double b);

static Value binary_broadcast(vm_t *vm, Value lhs, Value rhs,
                              BinaryOp op, const char *fname)
{
    bool lhs_scalar = is_scalar(lhs);
    bool rhs_scalar = is_scalar(rhs);

    // scalar OP scalar
    if (lhs_scalar && rhs_scalar)
        return NEW_NUM(op(AS_NUM(lhs), AS_NUM(rhs)));

    MatrixView A, B;
    bool lhs_mat = !lhs_scalar && matrix_view(lhs, &A);
    bool rhs_mat = !rhs_scalar && matrix_view(rhs, &B);

    if (!lhs_mat && !lhs_scalar)
        vm_errorf(vm, "%s: Left operand must be a matrix or scalar.", fname);
    if (!rhs_mat && !rhs_scalar)
        vm_errorf(vm, "%s: Right operand must be a matrix or scalar.", fname);

    // matrix OP scalar
    if (lhs_mat && rhs_scalar)
    {
        double s = AS_NUM(rhs);
        PiMatrix *result = create_denseMatrix(A.rows, A.cols);
        for (int r = 0; r < A.rows; r++)
            for (int c = 0; c < A.cols; c++)
                matrix_set(result, r, c, op(get_matrixView(&A, r, c), s));
        return NEW_OBJ(result);
    }

    // scalar OP matrix
    if (lhs_scalar && rhs_mat)
    {
        double s = AS_NUM(lhs);
        PiMatrix *result = create_denseMatrix(B.rows, B.cols);
        for (int r = 0; r < B.rows; r++)
            for (int c = 0; c < B.cols; c++)
                matrix_set(result, r, c, op(s, get_matrixView(&B, r, c)));
        return NEW_OBJ(result);
    }

    // matrix OP matrix — check for shape compatibility
    // Full match: same shape
    if (A.rows == B.rows && A.cols == B.cols)
    {
        PiMatrix *result = create_denseMatrix(A.rows, A.cols);
        for (int r = 0; r < A.rows; r++)
            for (int c = 0; c < A.cols; c++)
                matrix_set(result, r, c,
                           op(get_matrixView(&A, r, c),
                              get_matrixView(&B, r, c)));
        return NEW_OBJ(result);
    }

    // Row broadcast: B is 1 × cols (broadcast across rows of A)
    if (B.rows == 1 && A.cols == B.cols)
    {
        PiMatrix *result = create_denseMatrix(A.rows, A.cols);
        for (int r = 0; r < A.rows; r++)
            for (int c = 0; c < A.cols; c++)
                matrix_set(result, r, c,
                           op(get_matrixView(&A, r, c),
                              get_matrixView(&B, 0, c)));
        return NEW_OBJ(result);
    }

    // Col broadcast: B is rows × 1 (broadcast across cols of A)
    if (B.cols == 1 && A.rows == B.rows)
    {
        PiMatrix *result = create_denseMatrix(A.rows, A.cols);
        for (int r = 0; r < A.rows; r++)
            for (int c = 0; c < A.cols; c++)
                matrix_set(result, r, c,
                           op(get_matrixView(&A, r, c),
                              get_matrixView(&B, r, 0)));
        return NEW_OBJ(result);
    }

    // Row broadcast: A is 1 × cols (broadcast across rows of B)
    if (A.rows == 1 && A.cols == B.cols)
    {
        PiMatrix *result = create_denseMatrix(B.rows, B.cols);
        for (int r = 0; r < B.rows; r++)
            for (int c = 0; c < B.cols; c++)
                matrix_set(result, r, c,
                           op(get_matrixView(&A, 0, c),
                              get_matrixView(&B, r, c)));
        return NEW_OBJ(result);
    }

    // Col broadcast: A is rows × 1 (broadcast across cols of B)
    if (A.cols == 1 && A.rows == B.rows)
    {
        PiMatrix *result = create_denseMatrix(B.rows, B.cols);
        for (int r = 0; r < B.rows; r++)
            for (int c = 0; c < B.cols; c++)
                matrix_set(result, r, c,
                           op(get_matrixView(&A, r, 0),
                              get_matrixView(&B, r, c)));
        return NEW_OBJ(result);
    }

    vm_errorf(vm, "%s: Incompatible shapes (%dx%d) and (%dx%d).",
             fname, A.rows, A.cols, B.rows, B.cols);
    return NEW_NUM(0); // unreachable
}

// Element-wise Math

static double op_add(double a, double b) { return a + b; }
static double op_sub(double a, double b) { return a - b; }
static double op_mul(double a, double b) { return a * b; }
static double op_div(double a, double b) { return b == 0.0 ? NAN : a / b; }

Value mat_add(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2)
        vm_error(vm, "add: Expected two arguments.");
    return binary_broadcast(vm, argv[0], argv[1], op_add, "add");
}

Value mat_sub(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2)
        vm_error(vm, "sub: Expected two arguments.");
    return binary_broadcast(vm, argv[0], argv[1], op_sub, "sub");
}

Value mat_mul(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2)
        vm_error(vm, "mul: Expected two arguments.");
    return binary_broadcast(vm, argv[0], argv[1], op_mul, "mul");
}

Value mat_div(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2)
        vm_error(vm, "div: Expected two arguments.");
    return binary_broadcast(vm, argv[0], argv[1], op_div, "div");
}

// mat_apply — call a language-level function on each element

// Value mat_apply(vm_t *vm, int argc, Value *argv)
// {
//     // Signature: apply(matrix, fn)
//     // fn is a user-defined function in the language that takes one number
//     // and returns one number.
//     if (argc != 2)
//         vm_error(vm, "apply: Expected (matrix, fn).");

//     MatrixView view;
//     if (!matrix_view(argv[0], &view))
//         vm_error(vm, "apply: First argument must be a matrix.");

//     if (!IS_FUN(argv[1]) && !IS_CLOSURE(argv[1]))
//         vm_error(vm, "apply: Second argument must be a function.");

//     Value fn = argv[1];
//     PiMatrix *result = create_denseMatrix(view.rows, view.cols);

//     for (int r = 0; r < view.rows; r++)
//         for (int c = 0; c < view.cols; c++)
//         {
//             Value elem = NEW_NUM(get_matrixView(&view, r, c));
//             // vm_call dispatches into the language runtime
//             Value out = vm_call(vm, fn, 1, &elem);
//             if (!IS_NUM(out))
//                 vm_error(vm, "apply: Function must return a number.");
//             matrix_set(result, r, c, AS_NUM(out));
//         }

//     return NEW_OBJ(result);
// }

// Activation Functions

// Safe log: returns -inf for x <= 0 rather than NaN
static double safe_log(double x) { return x <= 0.0 ? -INFINITY : log(x); }
static double safe_sqrt(double x) { return x < 0.0 ? NAN : sqrt(x); }
static double scalar_abs(double x) { return fabs(x); }
static double scalar_sign(double x)
{
    if (x > 0.0)
        return 1.0;
    if (x < 0.0)
        return -1.0;
    return 0.0;
}

Value mat_exp(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "exp: Expected a matrix.");
    return elementwise_unary(vm, &view, exp, "exp");
}

Value mat_log(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "log: Expected a matrix.");
    return elementwise_unary(vm, &view, safe_log, "log");
}

Value mat_sqrt(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "sqrt: Expected a matrix.");
    return elementwise_unary(vm, &view, safe_sqrt, "sqrt");
}

Value mat_abs(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "abs: Expected a matrix.");
    return elementwise_unary(vm, &view, scalar_abs, "abs");
}

Value mat_sign(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "sign: Expected a matrix.");
    return elementwise_unary(vm, &view, scalar_sign, "sign");
}

Value mat_clip(vm_t *vm, int argc, Value *argv)
{
    // clip(matrix, lo, hi) — clamp each element to [lo, hi]
    MatrixView view;
    if (argc != 3 || !matrix_view(argv[0], &view) || !IS_NUM(argv[1]) || !IS_NUM(argv[2]))
        vm_error(vm, "clip: Expected (matrix, lo, hi).");

    double lo = AS_NUM(argv[1]);
    double hi = AS_NUM(argv[2]);

    if (lo > hi)
        vm_error(vm, "clip: lo must be <= hi.");

    PiMatrix *result = create_denseMatrix(view.rows, view.cols);
    for (int r = 0; r < view.rows; r++)
        for (int c = 0; c < view.cols; c++)
        {
            double v = get_matrixView(&view, r, c);
            if (v < lo)
                v = lo;
            else if (v > hi)
                v = hi;
            matrix_set(result, r, c, v);
        }
    return NEW_OBJ(result);
}

// Shaping

Value mat_flatten(vm_t *vm, int argc, Value *argv)
{
    // flatten(matrix) -> 1 × (rows*cols) matrix, row-major order
    MatrixView view;
    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "flatten: Expected a matrix.");

    int total = view.rows * view.cols;
    PiMatrix *result = create_denseMatrix(1, total);

    for (int r = 0; r < view.rows; r++)
        for (int c = 0; c < view.cols; c++)
            matrix_set(result, 0, r * view.cols + c,
                       get_matrixView(&view, r, c));

    return NEW_OBJ(result);
}

Value mat_expand_dims(vm_t *vm, int argc, Value *argv)
{
    // expand_dims(matrix, axis)
    // axis=0: (rows, cols) -> (1, rows, cols) approximated as (1*rows, cols)
    //                        i.e. no-op on a 2D matrix along axis 0
    // axis=1: (rows, cols) -> (rows, 1, cols) approximated as (rows, cols)
    //                        wraps each row into its own "batch"
    //
    // In a 2D-only matrix system the meaningful expansions are:
    //   axis=0: (n,)   -> (1, n)   — turn row vector into matrix
    //   axis=1: (n,)   -> (n, 1)   — turn row vector into column vector
    //
    // For a matrix input we add a size-1 dimension on the chosen axis:
    //   axis=0: (r, c) -> (1, r*c) — prepend batch dim, flatten to single row
    //   axis=1: (r, c) -> (r, c)   — no change (already 2D), kept for compat

    MatrixView view;
    if (argc != 2 || !matrix_view(argv[0], &view) || !IS_NUM(argv[1]))
        vm_error(vm, "expand_dims: Expected (matrix, axis).");

    int axis = (int)AS_NUM(argv[1]);

    if (axis == 0)
    {
        // Add a leading dimension: treat the whole matrix as one "sample"
        // Result: 1 × (rows * cols)
        int total = view.rows * view.cols;
        PiMatrix *result = create_denseMatrix(1, total);
        for (int r = 0; r < view.rows; r++)
            for (int c = 0; c < view.cols; c++)
                matrix_set(result, 0, r * view.cols + c,
                           get_matrixView(&view, r, c));
        return NEW_OBJ(result);
    }

    if (axis == 1)
    {
        // Add a trailing dimension to each row: (rows, cols) -> (rows*cols, 1)
        int total = view.rows * view.cols;
        PiMatrix *result = create_denseMatrix(total, 1);
        for (int r = 0; r < view.rows; r++)
            for (int c = 0; c < view.cols; c++)
                matrix_set(result, r * view.cols + c, 0,
                           get_matrixView(&view, r, c));
        return NEW_OBJ(result);
    }

    vm_error(vm, "expand_dims: axis must be 0 or 1.");
    return NEW_NUM(0); // unreachable
}

Value mat_squeeze(vm_t *vm, int argc, Value *argv)
{
    // squeeze(matrix) — remove size-1 dimensions
    //   (1, n) -> (1, n) stored as row vector    [cols = n, rows = 1]
    //   (n, 1) -> (1, n) stored as row vector    [flatten the column]
    //   (1, 1) -> scalar number
    //   (m, n) -> unchanged (nothing to squeeze)
    MatrixView view;
    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "squeeze: Expected a matrix.");

    // (1, 1) -> scalar
    if (view.rows == 1 && view.cols == 1)
        return NEW_NUM(get_matrixView(&view, 0, 0));

    // (n, 1) -> (1, n) row vector
    if (view.cols == 1)
    {
        PiMatrix *result = create_denseMatrix(1, view.rows);
        for (int r = 0; r < view.rows; r++)
            matrix_set(result, 0, r, get_matrixView(&view, r, 0));
        return NEW_OBJ(result);
    }

    // (1, n) -> already a row vector, return a fresh copy
    if (view.rows == 1)
    {
        PiMatrix *result = create_denseMatrix(1, view.cols);
        for (int c = 0; c < view.cols; c++)
            matrix_set(result, 0, c, get_matrixView(&view, 0, c));
        return NEW_OBJ(result);
    }

    // (m, n) — nothing to squeeze, return copy
    PiMatrix *result = create_denseMatrix(view.rows, view.cols);
    for (int r = 0; r < view.rows; r++)
        for (int c = 0; c < view.cols; c++)
            matrix_set(result, r, c, get_matrixView(&view, r, c));
    return NEW_OBJ(result);
}

// Module Registration 
static BuiltinFunc transform_funcs[] = {
    // {"apply", mat_apply},
    {"add", mat_add},
    {"sub", mat_sub},
    {"mul", mat_mul},
    {"div", mat_div},
    {"exp", mat_exp},
    {"log", mat_log},
    {"sqrt", mat_sqrt},
    {"abs", mat_abs},
    {"clip", mat_clip},
    {"sign", mat_sign},
    {"flatten", mat_flatten},
    {"expand_dims", mat_expand_dims},
    {"squeeze", mat_squeeze},
};

DEFINE_BUILTIN_MODULE(module_matTransform, "matrix.transform", transform_funcs, NULL);