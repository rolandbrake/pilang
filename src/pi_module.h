#ifndef PI_MODULE_H
#define PI_MODULE_H

#include <stdbool.h>

#include "pi_vm.h"
#include "pi_value.h"
#include "pi_object.h"
#include "builtin/pi_builtin.h"

typedef enum
{
    MODULE_UNLOADED,
    MODULE_LOADING,
    MODULE_LOADED,
} ModuleState;

typedef struct ObjModule
{

    Object object;
    char *name;
    char *path;

    bool builtin;
    bool is_main;
    ModuleState state;
    PiMap *exports;
    List *code;
    List *constants;
    List *names;
    HT_Table *instrs;
    HT_Table *globals;
    size_t source_size;
    uint64_t source_hash;

} ObjModule;

Object *new_module(VM *vm, const char *name, const char *path, bool builtin, bool is_main);
BuiltinModule *new_builtinModule(const char *name, BuiltinFunc *functions, int func_count, BuiltinConst *consts, int const_count);
Value load_module(VM *vm, const char *name);
char *module_resolvePath(VM *vm, const char *name);
char *module_resolvePathFrom(const char *base, const char *name);

#endif // PI_MODULE_H
