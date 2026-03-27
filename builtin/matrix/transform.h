#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "../../pi_value.h"
#include "../../pi_vm.h"
#include "matrix.h"



// Element-wise math
Value mat_apply(vm_t *vm, int argc, Value *argv); // apply any language function to each element
Value mat_add(vm_t *vm, int argc, Value *argv); // element-wise, supports scalar broadcast
Value mat_sub(vm_t *vm, int argc, Value *argv);
Value mat_mul(vm_t *vm, int argc, Value *argv);
Value mat_div(vm_t *vm, int argc, Value *argv);

// Element-wise activation functions (used heavily in ML)
Value mat_exp(vm_t *vm, int argc, Value *argv);
Value mat_log(vm_t *vm, int argc, Value *argv);
Value mat_sqrt(vm_t *vm, int argc, Value *argv);
Value mat_abs(vm_t *vm, int argc, Value *argv);
Value mat_clip(vm_t *vm, int argc, Value *argv);
Value mat_sign(vm_t *vm, int argc, Value *argv);

// Shaping
Value mat_flatten(vm_t *vm, int argc, Value *argv);
Value mat_expand_dims(vm_t *vm, int argc, Value *argv);
Value mat_squeeze(vm_t *vm, int argc, Value *argv);

#endif // TRANSFORM_H