#ifndef PI_RANDOM_H
#define PI_RANDOM_H

#include "../pi_value.h"
#include "../pi_vm.h"

Value rd_seed(vm_t *vm, int argc, Value *argv);
Value rd_rand(vm_t *vm, int argc, Value *argv);
Value rd_uniform(vm_t *vm, int argc, Value *argv);
Value rd_randint(vm_t *vm, int argc, Value *argv);
Value rd_normal(vm_t *vm, int argc, Value *argv);
Value rd_choice(vm_t *vm, int argc, Value *argv);
Value rd_shuffle(vm_t *vm, int argc, Value *argv);

#endif
