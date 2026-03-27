#ifndef REDUCE_H
#define REDUCE_H

#include "../../pi_value.h"
#include "../../pi_vm.h"
#include "matrix.h"

// Reductions
Value mat_sum(vm_t *vm, int argc, Value *argv);
Value mat_mean(vm_t *vm, int argc, Value *argv);
Value mat_min(vm_t *vm, int argc, Value *argv);
Value mat_max(vm_t *vm, int argc, Value *argv);
Value mat_prod(vm_t *vm, int argc, Value *argv);

// Arg operations
Value mat_argmax(vm_t *vm, int argc, Value *argv);
Value mat_argmin(vm_t *vm, int argc, Value *argv);

// Boolean reductions
Value mat_any(vm_t *vm, int argc, Value *argv);
Value mat_all(vm_t *vm, int argc, Value *argv);

#endif // REDUCE_H