#ifndef PI_BUILTIN_H
#define PI_BUILTIN_H

#include "pi_math.h"   // Math functions
#include "pi_string.h" // String functions
#include "pi_io.h"     // Input/Output functions
#include "pi_sys.h"    // System-related functions
#include "pi_time.h"   // Time functions
#include "pi_col.h"    // Color functions
#include "pi_fun.h"    // Function functions
#include "pi_mat.h"    // Matrix functions
#include "pi_type.h"   // Type functions
#include "pi_obj.h"    // Object functions

// Builtin functions struct definition
typedef struct
{
    char *name;                          // the name of the function
    Value (*func)(vm_t *, int, Value *); // the body of the function
} BuiltinFunc;

typedef struct
{
    char *name;  // the name of the constant
    Value value; // the value of the constant
} BuiltinConst;

typedef struct
{
    const char *name;       // the name of the module
    BuiltinFunc *functions; // array of builtin functions
    int func_count;         // number of builtin functions
    BuiltinConst *consts;   // array of builtin constants
    int const_count;        // number of builtin constants
} BuiltinModule;

// List of all builtin functions
extern BuiltinFunc builtin_functions[];
// Number of builtin functions
extern int BUILTIN_FUNC_COUNT;
// List of all builtin constants
extern BuiltinConst builtin_constants[];
// Number of builtin constants
extern int BUILTIN_CONST_COUNT;

// Builtin modules (e.g., math, time, io)
extern BuiltinModule *builtin_modules[];
extern int BUILTIN_MODULE_COUNT;

// Builtin module exports
extern BuiltinModule sys_module;
extern BuiltinModule math_module;
extern BuiltinModule io_module;
extern BuiltinModule col_module;
extern BuiltinModule time_module;
extern BuiltinModule fun_module;
extern BuiltinModule mat_module;
extern BuiltinModule type_module;
extern BuiltinModule obj_module;

// Helper macro to define a builtin module from local arrays.
#define DEFINE_BUILTIN_MODULE(module, name, func_list, const_list) \
    BuiltinModule module = {                                       \
        name,                                                      \
        func_list,                                                 \
        (int)(sizeof(func_list) / sizeof(BuiltinFunc)),            \
        const_list,                                                \
        (int)(sizeof(const_list) / sizeof(BuiltinConst)),          \
    }

#endif // PI_BUILTIN_H
