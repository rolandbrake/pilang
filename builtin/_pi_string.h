#ifndef _PI_STRING_H
#define _PI_STRING_H

#include "../pi_value.h"
#include "../pi_vm.h"

Value pi_char(VM *vm, int argc, Value *argv);
Value pi_ord(VM *vm, int argc, Value *argv);
Value pi_trim(VM *vm, int argc, Value *argv);
Value pi_upper(VM *vm, int argc, Value *argv);
Value pi_lower(VM *vm, int argc, Value *argv);
Value st_replace(VM *vm, int argc, Value *argv);
Value st_isUpper(VM *vm, int argc, Value *argv);
Value st_isLower(VM *vm, int argc, Value *argv);
Value st_isDigit(VM *vm, int argc, Value *argv);
Value st_isNumeric(VM *vm, int argc, Value *argv);
Value st_isAlpha(VM *vm, int argc, Value *argv);
Value st_isAlnum(VM *vm, int argc, Value *argv);
Value st_split(VM *vm, int argc, Value *argv);

#endif // _PI_STRING_H