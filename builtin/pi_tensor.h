#ifndef PI_TENSOR_H
#define PI_TENSOR_H

#include "../pi_value.h"
#include "../pi_vm.h"

// Creation
Value tn_zeros(VM *vm, int argc, Value *argv);
Value tn_ones(VM *vm, int argc, Value *argv);
Value tn_eye(VM *vm, int argc, Value *argv);
Value tn_rand(VM *vm, int argc, Value *argv);
Value tn_randn(VM *vm, int argc, Value *argv);
Value tn_randint(VM *vm, int argc, Value *argv);
Value tn_from(VM *vm, int argc, Value *argv);
Value tn_fill(VM *vm, int argc, Value *argv);
Value tn_linspace(VM *vm, int argc, Value *argv);
Value tn_range(VM *vm, int argc, Value *argv);

// Structure
Value tn_shape(VM *vm, int argc, Value *argv);
Value tn_ndim(VM *vm, int argc, Value *argv);
Value tn_size(VM *vm, int argc, Value *argv);

Value tn_reshape(VM *vm, int argc, Value *argv);
Value tn_slice(VM *vm, int argc, Value *argv);
Value tn_concat(VM *vm, int argc, Value *argv);


Value tn_flatten(VM *vm, int argc, Value *argv);
Value tn_transpose(VM *vm, int argc, Value *argv);
Value tn_expandDims(VM *vm, int argc, Value *argv);
Value tn_squeeze(VM *vm, int argc, Value *argv);

// Check
Value tn_isTensor(VM *vm, int argc, Value *argv);
Value tn_isScalar(VM *vm, int argc, Value *argv);
Value tn_isVector(VM *vm, int argc, Value *argv);
Value tn_isMatrix(VM *vm, int argc, Value *argv);

// Mathematical Operations (Element-wise)
Value tn_add(VM *vm, int argc, Value *argv);
Value tn_sub(VM *vm, int argc, Value *argv);
Value tn_mult(VM *vm, int argc, Value *argv);
Value tn_div(VM *vm, int argc, Value *argv);
Value tn_exp(VM *vm, int argc, Value *argv);
Value tn_log(VM *vm, int argc, Value *argv);
Value tn_sqrt(VM *vm, int argc, Value *argv);
Value tn_abs(VM *vm, int argc, Value *argv);
Value tn_clip(VM *vm, int argc, Value *argv);
Value tn_sign(VM *vm, int argc, Value *argv);
Value tn_apply(VM *vm, int argc, Value *argv);


Value tn_conv2d(VM *vm, int argc, Value *argv);
Value tn_avgpool2d(VM *vm, int argc, Value *argv);
Value tn_dense(VM *vm, int argc, Value *argv);
Value tn_trainClassifierHead(VM *vm, int argc, Value *argv);

// Algebraic Operations
Value tn_matmult(VM *vm, int argc, Value *argv);
Value tn_dot(VM *vm, int argc, Value *argv);
Value tn_cross(VM *vm, int argc, Value *argv);


// Linear Algebra
Value tn_solve(VM *vm, int argc, Value *argv);
Value tn_inv(VM *vm, int argc, Value *argv);
Value tn_det(VM *vm, int argc, Value *argv);
Value tn_lu(VM *vm, int argc, Value *argv);
Value tn_qr(VM *vm, int argc, Value *argv);
Value tn_svd(VM *vm, int argc, Value *argv);
Value tn_eig(VM *vm, int argc, Value *argv);
Value tn_norm(VM *vm, int argc, Value *argv);
Value tn_rank(VM *vm, int argc, Value *argv);
Value tn_trace(VM *vm, int argc, Value *argv);
Value tn_pinv(VM *vm, int argc, Value *argv);

// Reductions
Value tn_sum(VM *vm, int argc, Value *argv);
Value tn_mean(VM *vm, int argc, Value *argv);
Value tn_min(VM *vm, int argc, Value *argv);
Value tn_max(VM *vm, int argc, Value *argv);
Value tn_prod(VM *vm, int argc, Value *argv);
Value tn_argmax(VM *vm, int argc, Value *argv);
Value tn_argmin(VM *vm, int argc, Value *argv);
Value tn_any(VM *vm, int argc, Value *argv);
Value tn_all(VM *vm, int argc, Value *argv);
Value tn_reduce(VM *vm, int argc, Value *argv);

// Statistics
Value tn_var(VM *vm, int argc, Value *argv);
Value tn_std(VM *vm, int argc, Value *argv);
Value tn_median(VM *vm, int argc, Value *argv);
Value tn_percentile(VM *vm, int argc, Value *argv);
Value tn_mode(VM *vm, int argc, Value *argv);
Value tn_covariance(VM *vm, int argc, Value *argv);
Value tn_correlation(VM *vm, int argc, Value *argv);
Value tn_zscore(VM *vm, int argc, Value *argv);

// Randomness
Value tn_shuffle(VM *vm, int argc, Value *argv);

#endif
