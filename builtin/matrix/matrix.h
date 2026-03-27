#ifndef MATRIX_H
#define MATRIX_H


#include <math.h>
#include "../../pi_value.h"
#include "../../pi_vm.h"

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

// Utility
bool matrix_view(Value value, MatrixView *view);
double get_matrixView(MatrixView *view, int row, int col);
PiMatrix *create_denseMatrix(int rows, int cols);
double matrix_viewSum(MatrixView *view);
Value matrix_sizeValue(int rows, int cols);

// Construction
Value mat_zeros(vm_t *vm, int argc, Value *argv);
Value mat_ones(vm_t *vm, int argc, Value *argv);
Value mat_eye(vm_t *vm, int argc, Value *argv);
Value mat_rand(vm_t *vm, int argc, Value *argv);

// Structure
Value mat_size(vm_t *vm, int argc, Value *argv);
Value mat_reshape(vm_t *vm, int argc, Value *argv);
Value mat_slice(vm_t *vm, int argc, Value *argv);
Value mat_concat(vm_t *vm, int argc, Value *argv);
Value mat_transpose(vm_t *vm, int argc, Value *argv);

Value mat_mult(vm_t *vm, int argc, Value *argv);
Value mat_dot(vm_t *vm, int argc, Value *argv);
Value mat_cross(vm_t *vm, int argc, Value *argv);

// check if the giving list is a matrix
Value mat_isMat(vm_t *vm, int argc, Value *argv);

#endif // MATRIX_H

