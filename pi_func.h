#ifndef PI_FUNC_H
#define PI_FUNC_H

#include "list.h"
#include "pi_object.h"
#include "pi_value.h"
#include "pi_vm.h"

#define MAX_UPVALUES 32

typedef Value (*native_func)(vm_t *vm, int argc, Value *argv);

typedef struct Function
{
    Object object; // Base object

    char *name;     // Function name
    list_t *params; // PiList of parameters    
    list_t *param_names; // PiList of parameter names
    ObjCode *body;
    list_t *constants; // Constants table for this function's bytecode
    list_t *names;     // Names table for this function's bytecode
    table_t *instrs;   // Instruction metadata table for this function's bytecode
    table_t *globals;  // The global environment where this function was defined

    UpValue **upvalues; // PiList of upvalues used in the function body
    int upvalue_count;  // Number of upvalues
    Object *instance;   // Instance for bound methods
    Object *owner;      // Defining object for methods

    bool is_native;     // Flag to check if it's a native function
    bool is_method;     // Flag to check if it's a part of an object method
    bool need_args; // Whether this function ever reads local 'args'
    bool need_kwargs; // Whether this function ever reads local 'kw_args'
    native_func native; // Pointer to the native function (NULL for bytecode)
} Function;

// Object *new_func(char *name, list_t *body, list_t *params, UpValue **upvalues, Object *instance);
Object *new_func(char *name, ObjCode *body, list_t *params, UpValue **upvalues, Object *instance);
Value *new_native(const char *name, native_func func);
Value call_func(vm_t *vm, Function *function, size_t argc, Value *argv, Value kw_args);
Value call_funcv(vm_t *vm, Function *function, size_t argc, ...);

void free_func(Function *fn);

#endif // PI_FUNC_H
