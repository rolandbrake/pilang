#ifndef linalgs_H
#define linalgs_H

#include "../../pi_value.h"
#include "../../pi_vm.h"
#include "matrix.h"

// Solvers
Value mat_solve(vm_t *vm, int argc, Value *argv);
Value mat_inv(vm_t *vm, int argc, Value *argv);
Value mat_det(vm_t *vm, int argc, Value *argv);

// Decompositions
Value mat_lu(vm_t *vm, int argc, Value *argv);
Value mat_cholesky(vm_t *vm, int argc, Value *argv);
Value mat_qr(vm_t *vm, int argc, Value *argv);
Value mat_svd(vm_t *vm, int argc, Value *argv);
Value mat_eig(vm_t *vm, int argc, Value *argv);

// Utilities
Value mat_norm(vm_t *vm, int argc, Value *argv);
Value mat_rank(vm_t *vm, int argc, Value *argv);
Value mat_trace(vm_t *vm, int argc, Value *argv);
Value mat_pinv(vm_t *vm, int argc, Value *argv);

#endif // linalgs_H