#ifndef PI_OBJ_H
#define PI_OBJ_H

#include "../pi_value.h"
#include "../pi_vm.h"

Value pi_values(VM *vm, int argc, Value *argv);
Value pi_keys(VM *vm, int argc, Value *argv);
Value pi_clone(VM *vm, int argc, Value *argv);
Value pi_toString(VM *vm, int argc, Value *argv);
Value pi_valueOf(VM *vm, int argc, Value *argv);
Value pi_hashCode(VM *vm, int argc, Value *argv);
Value pi_extends(VM *vm, int argc, Value *argv);
Value pi_equals(VM *vm, int argc, Value *argv);
Value pi_ident(VM *vm, int argc, Value *argv);
Value pi_id(VM *vm, int argc, Value *argv);
Value pi_compare(VM *vm, int argc, Value *argv);
Value pi_type(VM *vm, int argc, Value *argv);
Value pi_name(VM *vm, int argc, Value *argv);
Value pi_setName(VM *vm, int argc, Value *argv);
Value pi_lock(VM *vm, int argc, Value *argv);
Value pi_bracketAccess(VM *vm, int argc, Value *argv);
Value pi_get(VM *vm, int argc, Value *argv);
Value pi_set(VM *vm, int argc, Value *argv);
Value pi_has(VM *vm, int argc, Value *argv);
Value pi_delete(VM *vm, int argc, Value *argv);
Value pi_iterator(VM *vm, int argc, Value *argv);
Value pi_next(VM *vm, int argc, Value *argv);

#endif // PI_OBJ_H
