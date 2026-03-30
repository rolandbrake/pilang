#ifndef PI_OS_H
#define PI_OS_H

#include "../pi_value.h"
#include "../pi_vm.h"

// Runs a shell command and returns stdout/stderr/code.
Value os_run(vm_t *vm, int argc, Value *argv);
// Spawns a subprocess without waiting.
Value os_spawn(vm_t *vm, int argc, Value *argv);

// Finds the path of an executable in $PATH.
Value os_which(vm_t *vm, int argc, Value *argv);

// Registers a handler for a system signal.
Value os_signal(vm_t *vm, int argc, Value *argv);

// Sends a signal to a process by PID.
Value os_kill(vm_t *vm, int argc, Value *argv);

// Returns the machine hostname.
Value os_hostname(vm_t *vm, int argc, Value *argv);

// Returns the number of logical CPU cores.
Value os_cpus(vm_t *vm, int argc, Value *argv);

// Returns total system RAM in bytes.
Value os_ram(vm_t *vm, int argc, Value *argv);

// Returns the current logged-in username.
Value os_user(vm_t *vm, int argc, Value *argv);

#endif // PI_OS_H