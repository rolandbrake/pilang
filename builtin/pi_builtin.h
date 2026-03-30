#ifndef PI_BUILTIN_H
#define PI_BUILTIN_H

#include "pi_math.h"       // Math functions
#include "pi_string.h"     // String functions
#include "pi_io.h"         // Input/Output functions
#include "pi_sys.h"        // System-related functions
#include "pi_os.h"         // Operating system functions
#include "pi_time.h"       // Time functions
#include "pi_col.h"        // Color functions
#include "pi_func.h"       // Function functions
#include "matrix/matrix.h" // Matrix functions
#include "pi_type.h"       // Type functions
#include "pi_obj.h"        // Object functions

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
    const char *name;       // the builtin module name (may be dotted, e.g. "matrix.reduce")
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
extern BuiltinModule module_sys;    // sys: System functions (e.g., argv, exit)
extern BuiltinModule module_os;     // os: Operating system functions (e.g., getcwd)
extern BuiltinModule module_math;   // math: Math functions (e.g., sin, cos)
extern BuiltinModule module_stats;  // stats: Statistics functions (e.g., mean, median)
extern BuiltinModule module_func;   // func: Function functions (e.g., filter, map)
extern BuiltinModule module_io;     // io: Input/Output functions (e.g., print, read)
extern BuiltinModule module_fs;     // fs: Filesystem functions (e.g., readfile, writefile)
extern BuiltinModule module_col;    // col: Color functions (e.g., rgb, hsl)
extern BuiltinModule module_string; // string: String functions (e.g., split, join)
extern BuiltinModule module_time;   // time: Time functions (e.g., now, sleep)
extern BuiltinModule module_type;   // type: Type functions (e.g., type, typeof)
extern BuiltinModule module_plot;   // plot: Plot functions (e.g., plot, show, scatter, bar)
extern BuiltinModule module_screen; // screen: Screen functions (e.g., clear, cls)

// Builtin matrix module
extern BuiltinModule module_matrix;       // matrix: Matrix functions (e.g., size, zeros, ones)
extern BuiltinModule module_matReduce;    // matrix.reduce: Matrix reduction functions (e.g., sum, mean, min, max, prod, argmax, argmin, any, all)
extern BuiltinModule module_matlinalgs;   // matrix.linalgs: Matrix linear algebra functions (e.g., det, inv, solve, eig, qr)
extern BuiltinModule module_matStats;     // matrix.stats: Matrix statistics functions (e.g., cov, corr)
extern BuiltinModule module_matTransform; // matrix.transform: Matrix transformation functions (e.g., transpose, reshape)

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
