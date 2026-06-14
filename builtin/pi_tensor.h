#ifndef PI_TENSOR_H
#define PI_TENSOR_H

#include "../pi_value.h"
#include "../pi_vm.h"

// Creation
Value tn_zeros(vm_t *vm, int argc, Value *argv);
Value tn_ones(vm_t *vm, int argc, Value *argv);
Value tn_eye(vm_t *vm, int argc, Value *argv);
Value tn_rand(vm_t *vm, int argc, Value *argv);
Value tn_randn(vm_t *vm, int argc, Value *argv);
Value tn_randint(vm_t *vm, int argc, Value *argv);
Value tn_from(vm_t *vm, int argc, Value *argv);
Value tn_fill(vm_t *vm, int argc, Value *argv);

// Structure
Value tn_shape(vm_t *vm, int argc, Value *argv);
Value tn_ndim(vm_t *vm, int argc, Value *argv);
Value tn_size(vm_t *vm, int argc, Value *argv);

Value tn_reshape(vm_t *vm, int argc, Value *argv);
Value tn_slice(vm_t *vm, int argc, Value *argv);
Value tn_concat(vm_t *vm, int argc, Value *argv);


Value tn_flatten(vm_t *vm, int argc, Value *argv);
Value tn_transpose(vm_t *vm, int argc, Value *argv);
Value tn_expandDims(vm_t *vm, int argc, Value *argv);
Value tn_squeeze(vm_t *vm, int argc, Value *argv);

// Check
Value tn_isTensor(vm_t *vm, int argc, Value *argv);
Value tn_isScalar(vm_t *vm, int argc, Value *argv);
Value tn_isVector(vm_t *vm, int argc, Value *argv);
Value tn_isMatrix(vm_t *vm, int argc, Value *argv);

// Mathematical Operations (Element-wise)
Value tn_add(vm_t *vm, int argc, Value *argv);
Value tn_sub(vm_t *vm, int argc, Value *argv);
Value tn_mult(vm_t *vm, int argc, Value *argv);
Value tn_div(vm_t *vm, int argc, Value *argv);
Value tn_exp(vm_t *vm, int argc, Value *argv);
Value tn_log(vm_t *vm, int argc, Value *argv);
Value tn_sqrt(vm_t *vm, int argc, Value *argv);
Value tn_abs(vm_t *vm, int argc, Value *argv);
Value tn_clip(vm_t *vm, int argc, Value *argv);
Value tn_sign(vm_t *vm, int argc, Value *argv);
Value tn_apply(vm_t *vm, int argc, Value *argv);

// Algebraic Operations
Value tn_matmult(vm_t *vm, int argc, Value *argv);
Value tn_dot(vm_t *vm, int argc, Value *argv);
Value tn_cross(vm_t *vm, int argc, Value *argv);


// Linear Algebra
Value tn_solve(vm_t *vm, int argc, Value *argv);
Value tn_inv(vm_t *vm, int argc, Value *argv);
Value tn_det(vm_t *vm, int argc, Value *argv);
Value tn_lu(vm_t *vm, int argc, Value *argv);
Value tn_qr(vm_t *vm, int argc, Value *argv);
Value tn_svd(vm_t *vm, int argc, Value *argv);
Value tn_eig(vm_t *vm, int argc, Value *argv);
Value tn_norm(vm_t *vm, int argc, Value *argv);
Value tn_rank(vm_t *vm, int argc, Value *argv);
Value tn_trace(vm_t *vm, int argc, Value *argv);
Value tn_pinv(vm_t *vm, int argc, Value *argv);

// Reductions
Value tn_sum(vm_t *vm, int argc, Value *argv);
Value tn_mean(vm_t *vm, int argc, Value *argv);
Value tn_min(vm_t *vm, int argc, Value *argv);
Value tn_max(vm_t *vm, int argc, Value *argv);
Value tn_prod(vm_t *vm, int argc, Value *argv);
Value tn_argmax(vm_t *vm, int argc, Value *argv);
Value tn_argmin(vm_t *vm, int argc, Value *argv);
Value tn_any(vm_t *vm, int argc, Value *argv);
Value tn_all(vm_t *vm, int argc, Value *argv);
Value tn_reduce(vm_t *vm, int argc, Value *argv);

// Statistics
Value tn_var(vm_t *vm, int argc, Value *argv);
Value tn_std(vm_t *vm, int argc, Value *argv);
Value tn_median(vm_t *vm, int argc, Value *argv);
Value tn_percentile(vm_t *vm, int argc, Value *argv);
Value tn_mode(vm_t *vm, int argc, Value *argv);
Value tn_covariance(vm_t *vm, int argc, Value *argv);
Value tn_correlation(vm_t *vm, int argc, Value *argv);
Value tn_zscore(vm_t *vm, int argc, Value *argv);

// Randomness
Value tn_shuffle(vm_t *vm, int argc, Value *argv);

#endif
