#ifndef PI_OBJ_H
#define PI_OBJ_H

#include "../pi_value.h"
#include "../pi_vm.h"

Value pi_values(vm_t *vm, int argc, Value *argv);
Value pi_keys(vm_t *vm, int argc, Value *argv);
Value pi_clone(vm_t *vm, int argc, Value *argv);
Value pi_toString(vm_t *vm, int argc, Value *argv);
Value pi_valueOf(vm_t *vm, int argc, Value *argv);
Value pi_hashCode(vm_t *vm, int argc, Value *argv);
Value pi_extends(vm_t *vm, int argc, Value *argv);
Value pi_equals(vm_t *vm, int argc, Value *argv);
Value pi_ident(vm_t *vm, int argc, Value *argv);
Value pi_compare(vm_t *vm, int argc, Value *argv);
Value pi_type(vm_t *vm, int argc, Value *argv);
Value pi_name(vm_t *vm, int argc, Value *argv);
Value pi_setName(vm_t *vm, int argc, Value *argv);
Value pi_get(vm_t *vm, int argc, Value *argv);
Value pi_set(vm_t *vm, int argc, Value *argv);
Value pi_has(vm_t *vm, int argc, Value *argv);
Value pi_delete(vm_t *vm, int argc, Value *argv);
Value pi_iterator(vm_t *vm, int argc, Value *argv);
Value pi_next(vm_t *vm, int argc, Value *argv);

#endif // PI_OBJ_H
