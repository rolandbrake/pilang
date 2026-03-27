#include "matrix.h"
#include "../../list.h"
#include "../pi_builtin.h"



bool matrix_view(Value value, MatrixView *view)
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

double get_matrixView(MatrixView *view, int row, int col)
{
    if (view->dense)
        return matrix_get(view->as.matrix, row, col);

    Value *row_val = (Value *)list_getAt(view->as.list->items, row);
    list_t *row_list = as_list(*row_val);
    Value *cell = (Value *)list_getAt(row_list, col);
    return as_number(*cell);
}

PiMatrix *create_denseMatrix(int rows, int cols)
{
    return (PiMatrix *)new_matrix(rows, cols);
}

double matrix_viewSum(MatrixView *view)
{
    double total = 0.0;

    for (int row = 0; row < view->rows; row++)
        for (int col = 0; col < view->cols; col++)
            total += get_matrixView(view, row, col);

    return total;
}

Value matrix_sizeValue(int rows, int cols)
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

Value mat_size(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "Expected a matrix.");

    return matrix_sizeValue(view.rows, view.cols);
}

Value mat_zeros(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2 || !IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "Expected two numbers (rows, cols)");

    int rows = AS_NUM(argv[0]);
    int cols = AS_NUM(argv[1]);
    return NEW_OBJ(create_denseMatrix(rows, cols));
}

Value mat_ones(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2 || !IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "Expected two numbers (rows, cols)");

    int rows = AS_NUM(argv[0]);
    int cols = AS_NUM(argv[1]);
    PiMatrix *matrix = create_denseMatrix(rows, cols);

    int size = rows * cols;
    for (int i = 0; i < size; i++)
        matrix->data[i] = 1.0;

    return NEW_OBJ(matrix);
}

Value mat_eye(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2 || !IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "Expected two numbers (rows, cols)");

    int rows = AS_NUM(argv[0]);
    int cols = AS_NUM(argv[1]);
    PiMatrix *matrix = create_denseMatrix(rows, cols);

    int diagonal = rows < cols ? rows : cols;
    for (int i = 0; i < diagonal; i++)
        matrix_set(matrix, i, i, 1.0);

    return NEW_OBJ(matrix);
}

Value mat_rand(vm_t *vm, int argc, Value *argv)
{
    if (argc != 2 || !IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "Expected two numbers (rows, cols)");

    int rows = AS_NUM(argv[0]);
    int cols = AS_NUM(argv[1]);
    PiMatrix *matrix = create_denseMatrix(rows, cols);

    int size = rows * cols;
    for (int i = 0; i < size; i++)
        matrix->data[i] = rand() / (double)RAND_MAX;

    return NEW_OBJ(matrix);
}

Value mat_mult(vm_t *vm, int argc, Value *argv)
{
    MatrixView A;
    MatrixView B;

    if (argc != 2 || !matrix_view(argv[0], &A) || !matrix_view(argv[1], &B))
        vm_error(vm, "Expected two matrices.");

    if (A.cols != B.rows)
        vm_error(vm, "Matrix multiplication dimension mismatch.");

    PiMatrix *result = create_denseMatrix(A.rows, B.cols);

    for (int i = 0; i < A.rows; i++)
        for (int j = 0; j < B.cols; j++)
        {
            double sum = 0.0;
            for (int k = 0; k < A.cols; k++)
                sum += get_matrixView(&A, i, k) * get_matrixView(&B, k, j);
            matrix_set(result, i, j, sum);
        }

    return NEW_OBJ(result);
}

Value mat_transpose(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "transpose: Expected a matrix.");

    PiMatrix *result = create_denseMatrix(view.cols, view.rows);

    for (int row = 0; row < view.rows; row++)
        for (int col = 0; col < view.cols; col++)
            matrix_set(result, col, row, get_matrixView(&view, row, col));

    return NEW_OBJ(result);
}


Value mat_dot(vm_t *vm, int argc, Value *argv)
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

Value mat_cross(vm_t *vm, int argc, Value *argv)
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

Value mat_isMat(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    if (argc != 1)
        vm_error(vm, "Expected a matrix.");

    return NEW_BOOL(matrix_view(argv[0], &view));
}

static BuiltinFunc mat_funcs[] = {
    {"size", mat_size},
    {"zeros", mat_zeros},
    {"ones", mat_ones},
    {"eye", mat_eye},
    {"rand", mat_rand},
    {"mult", mat_mult},
    {"transpose", mat_transpose},
    {"dot", mat_dot},
    {"cross", mat_cross},
    {"is_mat", mat_isMat},
};

DEFINE_BUILTIN_MODULE(module_matrix, "matrix", mat_funcs, NULL);
