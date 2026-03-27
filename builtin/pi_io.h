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

// open file and return file handle
Value io_open(vm_t *vm, int argc, Value *argv);
// read from file and return string
Value io_read(vm_t *vm, int argc, Value *argv);
// write to file
Value io_write(vm_t *vm, int argc, Value *argv);

// seek in file and return if success (bool)
Value io_seek(vm_t *vm, int argc, Value *argv);

// close file
Value io_close(vm_t *vm, int argc, Value *argv);

#endif // PI_IO_H
