#ifndef PI_SYS_H
#define PI_SYS_H

#include "../pi_value.h"
#include "../pi_vm.h"

Value pi_error(vm_t *vm, int argc, Value *argv);
Value pi_assert(vm_t *vm, int argc, Value *argv);
Value pi_zen(vm_t *vm, int argc, Value *argv);

// Returns command-line arguments as a list. Index 0 is the script path.
Value sy_argv(vm_t *vm, int argc, Value *argv);

// Terminates the process with the given exit code.
Value sy_exit(vm_t *vm, int argc, Value *argv);

// Returns the current platform: 'linux', 'darwin', or 'windows'.
Value sy_platform(vm_t *vm, int argc, Value *argv);

// Returns the pilang interpreter version string.
Value sy_version(vm_t *vm, int argc, Value *argv);

// Gets an environment variable. Returns nil if not set.
// sys.env(key: str) -> str
Value sy_env(vm_t *vm, int argc, Value *argv);
// Triggers a garbage collection cycle manually.
// sys.gc()
Value sy_gc(vm_t *vm, int argc, Value *argv);
// Returns current heap memory usage in bytes.
// sys.mem()
Value sy_mem(vm_t *vm, int argc, Value *argv);
// Returns the current process ID.
// sys.pid()
Value sy_pid(vm_t *vm, int argc, Value *argv);

// string represent the current working directory
// sys.path()
Value sy_path(vm_t *vm, int argc, Value *argv);


#endif // PI_SYS_H