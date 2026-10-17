#ifndef PI_RANDOM_H
#define PI_RANDOM_H

#include "../pi_value.h"
#include "../pi_vm.h"

Value rd_seed(VM *vm, int argc, Value *argv);
Value rd_rand(VM *vm, int argc, Value *argv);
Value rd_uniform(VM *vm, int argc, Value *argv);
Value rd_randint(VM *vm, int argc, Value *argv);
Value rd_normal(VM *vm, int argc, Value *argv);
Value rd_choice(VM *vm, int argc, Value *argv);
Value rd_shuffle(VM *vm, int argc, Value *argv);

#endif
