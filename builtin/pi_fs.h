#ifndef PI_FS_H
#define PI_FS_H

#include "../pi_vm.h"
#include "../pi_value.h"

// Reading
Value fs_read(vm_t *vm, int argc, Value *argv);
Value fs_readlines(vm_t *vm, int argc, Value *argv);
Value fs_open(vm_t *vm, int argc, Value *argv);
Value fs_seek(vm_t *vm, int argc, Value *argv);

// Writing
Value fs_write(vm_t *vm, int argc, Value *argv);
Value fs_append(vm_t *vm, int argc, Value *argv);
Value fs_close(vm_t *vm, int argc, Value *argv);

// Paths
Value fs_exists(vm_t *vm, int argc, Value *argv);
Value fs_isdir(vm_t *vm, int argc, Value *argv);
Value fs_isfile(vm_t *vm, int argc, Value *argv);
Value fs_size(vm_t *vm, int argc, Value *argv);
// Value fs_stat(vm_t *vm, int argc, Value *argv);
Value fs_abspath(vm_t *vm, int argc, Value *argv);
Value fs_basename(vm_t *vm, int argc, Value *argv);
Value fs_dirname(vm_t *vm, int argc, Value *argv);
Value fs_ext(vm_t *vm, int argc, Value *argv);
Value fs_join(vm_t *vm, int argc, Value *argv);

// Directories
Value fs_mkdir(vm_t *vm, int argc, Value *argv);
Value fs_rmdir(vm_t *vm, int argc, Value *argv);
Value fs_listdir(vm_t *vm, int argc, Value *argv);
Value fs_cwd(vm_t *vm, int argc, Value *argv);
Value fs_chdir(vm_t *vm, int argc, Value *argv);

// Operators
Value fs_copy(vm_t *vm, int argc, Value *argv);
Value fs_move(vm_t *vm, int argc, Value *argv);
Value fs_delete(vm_t *vm, int argc, Value *argv);

#endif // PI_FS_H