#ifndef PI_TYPE_H
#define PI_TYPE_H

#include "../src/pi_value.h"
#include "../src/pi_vm.h"

Value _pi_type(VM *vm, int argc, Value *argv);
Value pi_isNum(VM *vm, int argc, Value *argv);
Value pi_isStr(VM *vm, int argc, Value *argv);
Value pi_isBool(VM *vm, int argc, Value *argv);
Value pi_isList(VM *vm, int argc, Value *argv);
Value pi_isMap(VM *vm, int argc, Value *argv);

Value pi_num(VM *vm, int argc, Value *argv);
Value pi_str(VM *vm, int argc, Value *argv);
Value pi_bool(VM *vm, int argc, Value *argv);
// Converts an iterable to a list.
Value pi_list(VM *vm, int argc, Value *argv);

// Returns true if x is of the given type.
Value tp_instanceof(VM *vm, int argc, Value *argv);
// Returns memory size of a value in bytes.
Value tp_size(VM *vm, int argc, Value *argv);
// Returns true if x is nil.
Value tp_nil(VM *vm, int argc, Value *argv);
// Converts x to int. Parses strings, truncates floats.
Value tp_int(VM *vm, int argc, Value *argv);
// Converts x string to a floating-point number.
Value tp_float(VM *vm, int argc, Value *argv);
// Converts x to a boolean value.
Value tp_bool(VM *vm, int argc, Value *argv);
// Converts a string or int list to a bytes object.
Value tp_bytes(VM *vm, int argc, Value *argv);

#endif // PI_TYPE_H
