#ifndef PI_BUILTIN_H
#define PI_BUILTIN_H

#include "pi_math.h"    // Math functions
#include "_pi_string.h" // String functions
#include "pi_io.h"      // Input/Output functions
#include "pi_sys.h"     // System-related functions
#include "pi_os.h"      // Operating system functions
#include "pi_time.h"    // Time functions
#include "pi_col.h"     // Color functions
#include "pi_func.h"    // Function functions
#include "pi_tensor.h"  // Tensor functions
#include "pi_type.h"    // Type functions
#include "pi_obj.h"     // Object functions
#include "pi_random.h"  // Random functions
#include "pi_lang.h"    // Language/runtime constants
#include "pi_plot3d.h"  // 3D plot functions
#ifndef __EMSCRIPTEN__
#include "image/pi_image.h" // Image package functions
#endif

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
    const char *name;       // the builtin module name (may be dotted, e.g. "tensor.reduce")
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
extern BuiltinModule module_random; // random: Random number helpers
extern BuiltinModule module_lang;   // lang: Runtime/language constants (e.g., operator ids)
extern BuiltinModule module_draw;   // draw: Draw functions (e.g., canvas, run, clear, pixel, line, triangle, rect, polygon, circle, text, image, push, pop, translate, scale, rotate, alpha, on, off, poll, wait, mouse, key, open, close, title, resize, fullscreen, size, fps)
extern BuiltinModule module_plot;   // plot: Plot functions (e.g., plot, show, scatter, bar)
extern BuiltinModule module_plot3d; // plot3d: 3D plot functions (e.g., surface, mesh, wireframe)
extern BuiltinModule module_tensor; // tensor: Tensor functions (e.g., zeros, ones, shape)
#ifndef __EMSCRIPTEN__
extern BuiltinModule module_image;  // image: Image loading/manipulation
extern BuiltinModule module_imageFilters; // image.filters: Image filtering helpers
extern BuiltinModule module_imageColor;  // image.color: Image color helpers
#endif

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
