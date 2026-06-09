#ifndef PI_TYPE_H
#define PI_TYPE_H

#include "../pi_value.h"
#include "../pi_vm.h"

Value _pi_type(vm_t *vm, int argc, Value *argv);
Value pi_isNum(vm_t *vm, int argc, Value *argv);
Value pi_isStr(vm_t *vm, int argc, Value *argv);
Value pi_isBool(vm_t *vm, int argc, Value *argv);
Value pi_isList(vm_t *vm, int argc, Value *argv);
Value pi_isMap(vm_t *vm, int argc, Value *argv);

Value pi_num(vm_t *vm, int argc, Value *argv);
Value pi_str(vm_t *vm, int argc, Value *argv);
Value pi_bool(vm_t *vm, int argc, Value *argv);



// Returns true if x is of the given type.
Value tp_is(vm_t *vm, int argc, Value *argv);
// Returns the type name of any value.
Value tp_of(vm_t *vm, int argc, Value *argv);
// Returns memory size of a value in bytes.
Value tp_size(vm_t *vm, int argc, Value *argv);
// Returns true if x is nil.
Value tp_nil(vm_t *vm, int argc, Value *argv);
// Converts x to int. Parses strings, truncates floats.
Value tp_int(vm_t *vm, int argc, Value *argv);
// Converts x string to a floating-point number.
Value tp_float(vm_t *vm, int argc, Value *argv);
// Converts x to its string representation.
Value tp_string(vm_t *vm, int argc, Value *argv);
// Converts x to a boolean value.
Value tp_bool(vm_t *vm, int argc, Value *argv);
// Converts an iterable to a list.
Value tp_list(vm_t *vm, int argc, Value *argv);
// Converts a string or int list to a bytes object.
Value tp_bytes(vm_t *vm, int argc, Value *argv);

#endif // PI_TYPE_H
