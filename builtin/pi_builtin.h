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
// Image support is SDL2-dependent and unavailable in the WebAssembly build.
#ifndef __EMSCRIPTEN__
#include "image/pi_image.h"
#endif

typedef struct
{
    char *name;                          // function name as exposed to pi scripts
    Value (*func)(vm_t *, int, Value *); // native implementation
} BuiltinFunc;

typedef struct
{
    char *name;  // constant name as exposed to pi scripts
    Value value;
} BuiltinConst;

typedef struct
{
    const char *name;       // module name; may be dotted for submodules (e.g. "image.filters")
    BuiltinFunc *functions;
    int func_count;
    BuiltinConst *consts;
    int const_count;
} BuiltinModule;

extern BuiltinFunc builtin_functions[];
extern int BUILTIN_FUNC_COUNT;
extern BuiltinConst builtin_constants[];
extern int BUILTIN_CONST_COUNT;

extern BuiltinModule *builtin_modules[];
extern int BUILTIN_MODULE_COUNT;

extern BuiltinModule module_sys;    // sys: System functions (e.g., argv, exit)
extern BuiltinModule module_os;     // os: Operating system functions (e.g., getcwd)
extern BuiltinModule module_math;   // math: Math functions (e.g., sin, cos)
extern BuiltinModule module_stats;  // stats: Statistics functions (e.g., mean, median)
extern BuiltinModule module_func;   // func: Function utilities (e.g., filter, map)
extern BuiltinModule module_io;     // io: Input/Output functions (e.g., print, read)
extern BuiltinModule module_fs;     // fs: Filesystem functions (e.g., readfile, writefile)
extern BuiltinModule module_col;    // col: Color functions (e.g., rgb, hsl)
extern BuiltinModule module_string; // string: String functions (e.g., split, join)
extern BuiltinModule module_time;   // time: Time functions (e.g., now, sleep)
extern BuiltinModule module_type;   // type: Type functions (e.g., type, typeof)
extern BuiltinModule module_random; // random: Random number helpers
extern BuiltinModule module_lang;   // lang: Runtime/language constants (e.g., operator ids)
extern BuiltinModule module_draw;   // draw: 2D canvas (pixel, line, rect, circle, text, …)
extern BuiltinModule module_plot;   // plot: 2D plotting (plot, scatter, bar, show, …)
extern BuiltinModule module_plot3d; // plot3d: 3D plotting (surface, mesh, wireframe, …)
extern BuiltinModule module_tensor; // tensor: Tensor operations (zeros, ones, shape, …)
#ifndef __EMSCRIPTEN__
extern BuiltinModule module_image;        // image: Image loading and manipulation
extern BuiltinModule module_imageFilters; // image.filters: Convolution, blur, edge detection, …
extern BuiltinModule module_imageColor;   // image.color: Color space and palette helpers
#endif

// Defines a BuiltinModule from local func/const arrays, computing counts via sizeof.
// const_list must be an array (not a pointer) — passing NULL requires a sentinel workaround.
#define DEFINE_BUILTIN_MODULE(module, name, func_list, const_list) \
    BuiltinModule module = {                                       \
        name,                                                      \
        func_list,                                                 \
        (int)(sizeof(func_list) / sizeof(BuiltinFunc)),            \
        const_list,                                                \
        (int)(sizeof(const_list) / sizeof(BuiltinConst)),          \
    }

#endif // PI_BUILTIN_H