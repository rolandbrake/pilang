#ifndef PI_FS_H
#define PI_FS_H

#include "../src/pi_vm.h"
#include "../src/pi_value.h"

// Reading
Value fs_read(VM *vm, int argc, Value *argv);
Value fs_readBytes(VM *vm, int argc, Value *argv);
Value fs_writeBytes(VM *vm, int argc, Value *argv);
Value fs_readlines(VM *vm, int argc, Value *argv);
Value fs_readLinesLimit(VM *vm, int argc, Value *argv);
Value fs_open(VM *vm, int argc, Value *argv);
Value fs_seek(VM *vm, int argc, Value *argv);

// Writing
Value fs_write(VM *vm, int argc, Value *argv);
Value fs_append(VM *vm, int argc, Value *argv);
Value fs_close(VM *vm, int argc, Value *argv);

// Paths
Value fs_exists(VM *vm, int argc, Value *argv);
Value fs_isdir(VM *vm, int argc, Value *argv);
Value fs_isfile(VM *vm, int argc, Value *argv);
Value fs_size(VM *vm, int argc, Value *argv);
// Value fs_stat(VM *vm, int argc, Value *argv);
Value fs_abspath(VM *vm, int argc, Value *argv);
Value fs_basename(VM *vm, int argc, Value *argv);
Value fs_dirname(VM *vm, int argc, Value *argv);
Value fs_ext(VM *vm, int argc, Value *argv);
Value fs_join(VM *vm, int argc, Value *argv);

// Directories
Value fs_mkdir(VM *vm, int argc, Value *argv);
Value fs_rmdir(VM *vm, int argc, Value *argv);
Value fs_listdir(VM *vm, int argc, Value *argv);
Value fs_cwd(VM *vm, int argc, Value *argv);
Value fs_chdir(VM *vm, int argc, Value *argv);

// Operators
Value fs_copy(VM *vm, int argc, Value *argv);
Value fs_move(VM *vm, int argc, Value *argv);
Value fs_delete(VM *vm, int argc, Value *argv);

#endif // PI_FS_H
