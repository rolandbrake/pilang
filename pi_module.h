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
    list_t *constants;
    list_t *names;
    table_t *instrs;
    table_t *globals;

} ObjModule; // pilang modules

Object *new_module(vm_t *vm, const char *name, const char *path, bool builtin, bool is_main);
BuiltinModule *new_builtinModule(const char *name, BuiltinFunc *functions, int func_count, BuiltinConst *consts, int const_count);
Value load_module(vm_t *vm, const char *name);
char *module_resolvePath(vm_t *vm, const char *name);

#endif // PI_MODULE_H
