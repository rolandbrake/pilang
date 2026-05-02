#ifndef _PI_STRING_H
#define _PI_STRING_H

#include "../pi_value.h"
#include "../pi_vm.h"

Value pi_char(vm_t *vm, int argc, Value *argv);
Value pi_ord(vm_t *vm, int argc, Value *argv);
Value pi_trim(vm_t *vm, int argc, Value *argv);
Value pi_upper(vm_t *vm, int argc, Value *argv);
Value pi_lower(vm_t *vm, int argc, Value *argv);
Value st_replace(vm_t *vm, int argc, Value *argv);
Value st_isUpper(vm_t *vm, int argc, Value *argv);
Value st_isLower(vm_t *vm, int argc, Value *argv);
Value st_isDigit(vm_t *vm, int argc, Value *argv);
Value st_isNumeric(vm_t *vm, int argc, Value *argv);
Value st_isAlpha(vm_t *vm, int argc, Value *argv);
Value st_isAlnum(vm_t *vm, int argc, Value *argv);
Value st_split(vm_t *vm, int argc, Value *argv);

#endif // _PI_STRING_H