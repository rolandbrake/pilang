#ifndef PI_SYS_H
#define PI_SYS_H

#include "../pi_value.h"
#include "../pi_vm.h"

Value pi_error(vm_t *vm, int argc, Value *argv);
Value pi_assert(vm_t *vm, int argc, Value *argv);
Value pi_zen(vm_t *vm, int argc, Value *argv);

Value pi_argv(vm_t *vm, int argc, Value *argv);
Value pi_exit(vm_t *vm, int argc, Value *argv);
Value pi_platform(vm_t *vm, int argc, Value *argv);
Value pi_version(vm_t *vm, int argc, Value *argv);
Value pi_path(vm_t *vm, int argc, Value *argv);

#endif // PI_SYS_H