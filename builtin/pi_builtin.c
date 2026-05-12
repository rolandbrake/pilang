#include "pi_builtin.h"
#include "pi_col.h"
#include "../pi_value.h"

BuiltinConst builtin_constants[] = {
    {"INF", NEW_NUM(INFINITY)},
    {"NAN", NEW_NUM(NAN)},

};
int BUILTIN_CONST_COUNT = sizeof(builtin_constants) / sizeof(BuiltinConst);

BuiltinFunc builtin_functions[] = {

    // Time
    {"sleep", pi_sleep},
    {"time", _pi_time},

    // IO
    {"println", pi_println},
    {"print", pi_print},
    {"printf", pi_printf},
    {"log", pi_log},
    {"input", pi_input},

    // Math
    {"abs", pi_abs},
    {"min", pi_min},
    {"max", pi_max},
    {"pow", pi_pow},
    {"round", pi_round},
    {"seed", pi_seed},
    {"rand", pi_rand},
    {"rand_n", pi_rand_n},

    // String
    {"char", pi_char},
    {"ord", pi_ord},
    {"trim", pi_trim},
    {"upper", pi_upper},
    {"lower", pi_lower},

    // System
    {"error", pi_error},
    {"assert", pi_assert},
    {"zen", pi_zen},

    // Type
    {"type", _pi_type},
    {"is_num", pi_isNum},
    {"is_str", pi_isStr},
    {"is_bool", pi_isBool},
    {"is_list", pi_isList},
    {"is_map", pi_isMap},
    {"as_num", pi_asNum},
    {"as_string", pi_asStr},
    {"as_bool", pi_asBool},

    // Collections
    {"push", pi_push},
    {"pop", pi_pop},
    {"empty", pi_empty},
    {"insert", pi_insert},
    {"remove", pi_remove},
    {"slice", pi_slice},
    {"len", pi_len},
    {"tuple", pi_tuple},
    {"contains", cl_contains},
    {"index", cl_indexOf},
    {"count", cl_count},
    {"concat", cl_concat},
    {"repeat", cl_repeat},
    {"range", pi_range},

    // set operations
    {"set", _pi_set},
    {"union", pi_union},
    {"intersection", pi_intersection},
    {"difference", pi_difference},
    {"symmetric_diff", pi_symmetricDiff},
    {"issubset", pi_issubset},
    {"issuperset", pi_issuperset},
    {"isdisjoint", pi_isdisjoint},

    // Functional
    {"map", _pi_map},
    {"filter", pi_filter},
    {"reduce", pi_reduce},
    {"find", pi_find},

    // Object
    {"clone", pi_clone},
    {"values", pi_values},
    {"keys", pi_keys},
};

int BUILTIN_FUNC_COUNT = sizeof(builtin_functions) / sizeof(BuiltinFunc);

BuiltinModule *builtin_modules[] = {
    &module_sys,
    &module_math,
    &module_stats,
    &module_string,
    &module_io,
    &module_fs,
    &module_col,
    &module_func,
    &module_time,
    &module_type,
    &module_lang,
#ifndef __EMSCRIPTEN__
    &module_os,
    &module_draw,
    &module_plot,
#endif
    &module_tensor,

};

int BUILTIN_MODULE_COUNT = sizeof(builtin_modules) / sizeof(BuiltinModule *);
