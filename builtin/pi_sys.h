#ifndef PI_SYS_H
#define PI_SYS_H

#include "../src/pi_value.h"
#include "../src/pi_vm.h"

Value pi_error(VM *vm, int argc, Value *argv);
Value pi_assert(VM *vm, int argc, Value *argv);
Value pi_zen(VM *vm, int argc, Value *argv);

// Returns command-line arguments as a list. Index 0 is the script path.
Value sy_argv(VM *vm, int argc, Value *argv);

// Terminates the process with the given exit code.
Value sy_exit(VM *vm, int argc, Value *argv);

// Returns the current platform: 'linux', 'darwin', or 'windows'.
Value sy_platform(VM *vm, int argc, Value *argv);

// Returns the pilang interpreter version string.
Value sy_version(VM *vm, int argc, Value *argv);

// Gets an environment variable. Returns nil if not set.
// sys.env(key: str) -> str
Value sy_env(VM *vm, int argc, Value *argv);
// Triggers a garbage collection cycle manually.
// sys.gc()
Value sy_gc(VM *vm, int argc, Value *argv);
// Returns current heap memory usage in bytes.
// sys.mem()
Value sy_mem(VM *vm, int argc, Value *argv);
// Returns the current process ID.
// sys.pid()
Value sy_pid(VM *vm, int argc, Value *argv);

// string represent the current working directory
// sys.path()
Value sy_path(VM *vm, int argc, Value *argv);


#endif // PI_SYS_H