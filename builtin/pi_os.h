#ifndef PI_OS_H
#define PI_OS_H

#include "../src/pi_value.h"
#include "../src/pi_vm.h"

// Runs a shell command and returns stdout/stderr/code.
Value os_run(VM *vm, int argc, Value *argv);
// Spawns a subprocess without waiting.
Value os_spawn(VM *vm, int argc, Value *argv);

// Finds the path of an executable in $PATH.
Value os_which(VM *vm, int argc, Value *argv);

// Registers a handler for a system signal.
Value os_signal(VM *vm, int argc, Value *argv);

// Sends a signal to a process by PID.
Value os_kill(VM *vm, int argc, Value *argv);

// Returns the machine hostname.
Value os_hostname(VM *vm, int argc, Value *argv);

// Returns the number of logical CPU cores.
Value os_cpus(VM *vm, int argc, Value *argv);

// Returns total system RAM in bytes.
Value os_ram(VM *vm, int argc, Value *argv);

// Returns the current logged-in username.
Value os_user(VM *vm, int argc, Value *argv);

#endif // PI_OS_H