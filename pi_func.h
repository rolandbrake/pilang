#ifndef PI_FUNC_H
#define PI_FUNC_H

#include "pi_list.h"
#include "pi_object.h"
#include "pi_value.h"
#include "pi_vm.h"

#define MAX_UPVALUES 32

typedef Value (*NativeFunc)(VM *vm, int argc, Value *argv);

typedef enum
{
    FUNC_NATIVE = 1 << 0,
    FUNC_METHOD = 1 << 1,
    FUNC_NEED_ARGS = 1 << 2,
    FUNC_NEED_KWARGS = 1 << 3,
} FunctionFlags;

#define FUNC_HAS_FLAG(function, flag) (((function)->flags & (flag)) != 0)

typedef struct Function
{
    Object object; // Base object

    char *name; // Function name

    List *params; // PiList of parameters
    int arity;      // Cached positional parameter count

    List *param_names; // PiList of parameter names
    bool owns_params;
    ObjCode *body;
    List *constants; // Constants table for this function's bytecode
    List *names;     // Names table for this function's bytecode
    HT_Table *instrs;   // Instruction metadata table for this function's bytecode
    HT_Table *globals;  // The global environment where this function was defined

    UpValue **upvalues; // PiList of upvalues used in the function body
    int upvalue_count;  // Number of upvalues

    Object *instance; // Instance for bound methods

    bool owns_upvalues;
    Object *owner; // Defining object for methods

    Object *bound_source; // Unbound function this bound method was created from

    uint8_t flags; // FUNC_* flags

    bool global_valid; // Whether a same-named global still points at this function object
    int glonal_index;  // Names-table index for a valid same-named global binding

    NativeFunc native; // Pointer to the native function (NULL for bytecode)
} Function;

// Object *new_func(char *name, List *body, List *params, UpValue **upvalues, Object *instance);
Object *new_func(char *name, ObjCode *body, List *params, UpValue **upvalues, Object *instance);
Value *new_native(const char *name, NativeFunc func);

Value call_func(VM *vm, Function *function, size_t argc, Value *argv, Value kw_args);
Value call_funcv(VM *vm, Function *function, size_t argc, ...);

void free_func(Function *fn);

#endif // PI_FUNC_H
