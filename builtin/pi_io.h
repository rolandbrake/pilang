#ifndef PI_IO_H
#define PI_IO_H

#include <stdio.h>
#include "../pi_value.h"
#include "../pi_vm.h"

#define BUFFER_SIZE 2048

#define ANSI_RED "\033[31m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_RESET "\033[0m"

Value pi_println(vm_t *vm, int argc, Value *argv);
Value pi_print(vm_t *vm, int argc, Value *argv);
Value pi_printf(vm_t *vm, int argc, Value *argv);

Value pi_log(vm_t *vm, int argc, Value *argv);

Value pi_input(vm_t *vm, int argc, Value *argv);

#endif // PI_IO_H
