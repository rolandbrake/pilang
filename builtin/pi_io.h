#ifndef PI_IO_H
#define PI_IO_H

#include <stdio.h>
#include "../src/pi_value.h"
#include "../src/pi_vm.h"

#define BUFFER_SIZE 2048

#define ANSI_RED "\033[31m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_RESET "\033[0m"

Value pi_println(VM *vm, int argc, Value *argv);
Value pi_print(VM *vm, int argc, Value *argv);
Value pi_printf(VM *vm, int argc, Value *argv);
Value io_format(VM *vm, int argc, Value *argv);

char *pi_displayString(VM *vm, Value value);

Value pi_log(VM *vm, int argc, Value *argv);

Value pi_input(VM *vm, int argc, Value *argv);

// display a formatted string similar to python string format
Value io_clear(VM *vm, int argc, Value *argv);
Value io_pos(VM *vm, int argc, Value *argv);
Value io_cursor(VM *vm, int argc, Value *argv);
Value io_key(VM *vm, int argc, Value *argv);
// read a line from stdin
Value io_readline(VM *vm, int argc, Value *argv);
// display a message and wait for user input
Value io_prompt(VM *vm, int argc, Value *argv);

#endif // PI_IO_H
