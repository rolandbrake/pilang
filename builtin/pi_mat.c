#include "pi_mat.h"
#include "../list.h"
#include "pi_builtin.h"

typedef struct
{
    bool dense;
    int rows;
    int cols;
    union
    {
        PiMatrix *matrix;
        PiList *list;
    } as;
} MatrixView;

static bool matrix_view(Value value, MatrixView *view)
{
    if (IS_MATRIX(value))
    {
        PiMatrix *matrix = AS_MATRIX(value);
        view->dense = true;
        view->rows = matrix->rows;
        view->cols = matrix->cols;
        view->as.matrix = matrix;
        return true;
    }

    if (IS_LIST(value))
    {
        PiList *list = AS_LIST(value);
        if (!list->is_matrix || !list->is_numeric || list->rows < 0 || list->cols < 0)
            return false;

        view->dense = false;
        view->rows = list->rows;
        view->cols = list->cols;
        view->as.list = list;
        return true;
    }

    return false;
}

static double matrix_view_get(MatrixView *view, int row, int col)
{
    if (view->dense)
        return matrix_get(view->as.matrix, row, col);

    Value *row_val = (Value *)list_getAt(view->as.list->items, row);
    list_t *row_list = as_list(*row_val);
    Value *cell = (Value *)list_getAt(row_list, col);
    return as_number(*cell);
}

static PiMatrix *create_dense_matrix(int rows, int cols)
{
    return (PiMatrix *)new_matrix(rows, cols);
}

static double matrix_view_sum(MatrixView *view)
{
    double total = 0.0;

    for (int row = 0; row < view->rows; row++)
        for (int col = 0; col < view->cols; col++)
            total += matrix_view_get(view, row, col);

    return total;
}

static Value matrix_size_value(int rows, int cols)
{
    list_t *items = list_create(sizeof(Value));
    Value row_val = NEW_NUM(rows);
    Value col_val = NEW_NUM(cols);
    list_add(items, &row_val);
    list_add(items, &col_val);

    PiList *result = (PiList *)new_list(items);
    result->is_numeric = true;
    result->is_matrix = false;
    result->rows = 1;
    result->cols = 2;
    return NEW_OBJ(result);
}

Value pi_size(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "Expected a matrix.");

    return matrix_size_value(view.rows, view.cols);
}

Value pi_zeros(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2 || !IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "Expected two numbers (rows, cols)");

    int rows = AS_NUM(argv[0]);
    int cols = AS_NUM(argv[1]);
    return NEW_OBJ(create_dense_matrix(rows, cols));
}

Value pi_ones(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2 || !IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "Expected two numbers (rows, cols)");

    int rows = AS_NUM(argv[0]);
    int cols = AS_NUM(argv[1]);
    PiMatrix *matrix = create_dense_matrix(rows, cols);

    int size = rows * cols;
    for (int i = 0; i < size; i++)
        matrix->data[i] = 1.0;

    return NEW_OBJ(matrix);
}

Value pi_eye(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2 || !IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "Expected two numbers (rows, cols)");

    int rows = AS_NUM(argv[0]);
    int cols = AS_NUM(argv[1]);
    PiMatrix *matrix = create_dense_matrix(rows, cols);

    int diagonal = rows < cols ? rows : cols;
    for (int i = 0; i < diagonal; i++)
        matrix_set(matrix, i, i, 1.0);

    return NEW_OBJ(matrix);
}

Value pi_matRand(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2 || !IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "Expected two numbers (rows, cols)");

    int rows = AS_NUM(argv[0]);
    int cols = AS_NUM(argv[1]);
    PiMatrix *matrix = create_dense_matrix(rows, cols);

    int size = rows * cols;
    for (int i = 0; i < size; i++)
        matrix->data[i] = rand() / (double)RAND_MAX;

    return NEW_OBJ(matrix);
}

Value pi_mult(vm_t *vm, int argc, Value *argv)
{
    MatrixView A;
    MatrixView B;

    if (argc != 2 || !matrix_view(argv[0], &A) || !matrix_view(argv[1], &B))
        vm_error(vm, "Expected two matrices.");

    if (A.cols != B.rows)
        vm_error(vm, "Matrix multiplication dimension mismatch.");

    PiMatrix *result = create_dense_matrix(A.rows, B.cols);

    for (int i = 0; i < A.rows; i++)
        for (int j = 0; j < B.cols; j++)
        {
            double sum = 0.0;
            for (int k = 0; k < A.cols; k++)
                sum += matrix_view_get(&A, i, k) * matrix_view_get(&B, k, j);
            matrix_set(result, i, j, sum);
        }

    return NEW_OBJ(result);
}

Value pi_transpose(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "transpose: Expected a matrix.");

    PiMatrix *result = create_dense_matrix(view.cols, view.rows);

    for (int row = 0; row < view.rows; row++)
        for (int col = 0; col < view.cols; col++)
            matrix_set(result, col, row, matrix_view_get(&view, row, col));

    return NEW_OBJ(result);
}

Value pi_matSum(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "sum: Expected a matrix.");

    return NEW_NUM(matrix_view_sum(&view));
}

Value pi_matMean(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    int count;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "mean: Expected a matrix.");

    count = view.rows * view.cols;
    if (count == 0)
        return NEW_NUM(NAN);

    return NEW_NUM(matrix_view_sum(&view) / count);
}

Value pi_dot(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2 || !IS_LIST(argv[0]) || !IS_LIST(argv[1]))
        vm_error(vm, "dot: Expected two numeric vectors (lists)");

    PiList *A = AS_LIST(argv[0]);
    PiList *B = AS_LIST(argv[1]);

    if (!A->is_numeric || !B->is_numeric)
        vm_error(vm, "dot: Vectors must be numeric");

    if (A->items->size != B->items->size)
        vm_error(vm, "dot: Vectors must be of same length");

    double sum = 0;
    for (int i = 0; i < A->items->size; i++)
    {
        double a = AS_NUM(*(Value *)list_getAt(A->items, i));
        double b = AS_NUM(*(Value *)list_getAt(B->items, i));
        sum += a * b;
    }

    return NEW_NUM(sum);
}

Value pi_cross(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2 || !IS_LIST(argv[0]) || !IS_LIST(argv[1]))
        vm_error(vm, "cross: Expected two 3D numeric vectors");

    PiList *A = AS_LIST(argv[0]);
    PiList *B = AS_LIST(argv[1]);

    if (!A->is_numeric || !B->is_numeric)
        vm_error(vm, "cross: Vectors must be numeric");

    if (A->items->size != 3 || B->items->size != 3)
        vm_error(vm, "cross: Only 3D vectors supported");

    double a1 = AS_NUM(*(Value *)list_getAt(A->items, 0));
    double a2 = AS_NUM(*(Value *)list_getAt(A->items, 1));
    double a3 = AS_NUM(*(Value *)list_getAt(A->items, 2));

    double b1 = AS_NUM(*(Value *)list_getAt(B->items, 0));
    double b2 = AS_NUM(*(Value *)list_getAt(B->items, 1));
    double b3 = AS_NUM(*(Value *)list_getAt(B->items, 2));

    list_t *items = list_create(sizeof(Value));

    Value x = NEW_NUM(a2 * b3 - a3 * b2);
    Value y = NEW_NUM(a3 * b1 - a1 * b3);
    Value z = NEW_NUM(a1 * b2 - a2 * b1);

    list_add(items, &x);
    list_add(items, &y);
    list_add(items, &z);

    PiList *result = (PiList *)new_list(items);
    result->is_numeric = true;
    result->is_matrix = false;
    result->rows = 1;
    result->cols = 3;

    return NEW_OBJ(result);
}

Value pi_isMat(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    if (argc != 1)
        vm_error(vm, "Expected a matrix.");

    return NEW_BOOL(matrix_view(argv[0], &view));
}

static BuiltinFunc mat_funcs[] = {
    {"size", pi_size},
    {"zeros", pi_zeros},
    {"ones", pi_ones},
    {"eye", pi_eye},
    {"rand", pi_matRand},
    {"mult", pi_mult},
    {"transpose", pi_transpose},
    {"sum", pi_matSum},
    {"mean", pi_matMean},
    {"dot", pi_dot},
    {"cross", pi_cross},
    {"is_mat", pi_isMat},
};

DEFINE_BUILTIN_MODULE(mat_module, "matrix", mat_funcs, NULL);
