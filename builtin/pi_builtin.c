#include "pi_builtin.h"
#include "../pi_value.h"

BuiltinConst builtin_constants[] = {
    {"PI", {VAL_NUM, {.number = PI}}},
    {"E", {VAL_NUM, {.number = E}}},
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
    {"replace", pi_replace},
    {"is_upper", pi_isUpper},
    {"is_lower", pi_isLower},
    {"is_digit", pi_isDigit},
    {"is_numeric", pi_isNumeric},
    {"is_alpha", pi_isAlpha},
    {"is_alnum", pi_isAlnum},
    {"split", pi_split},

    // System
    {"fps", pi_fps},
    {"error", pi_error},
    {"zen", pi_zen},

    // Type
    {"type", _pi_type},
    {"is_num", pi_isNum},
    {"is_str", pi_isStr},
    {"is_bool", pi_isBool},
    {"is_list", pi_isList},
    {"is_map", pi_isMap},
    {"as_num", pi_asNum},
    {"as_str", pi_asStr},
    {"as_bool", pi_asBool},

    // Collections
    {"push", pi_push},
    {"pop", pi_pop},
    {"peek", pi_peek},
    {"empty", pi_empty},
    {"sort", pi_sort},
    {"insert", pi_insert},
    {"unshift", pi_unshift},
    {"remove", pi_remove},
    {"append", pi_append},
    {"contains", pi_contains},
    {"index_of", pi_indexOf},
    {"reverse", pi_reverse},
    {"shuffle", pi_shuffle},
    {"copy", pi_copy},
    {"slice", pi_slice},
    {"len", pi_len},
    {"range", pi_range},

    // Functional
    {"map", _pi_map},
    {"filter", pi_filter},
    {"reduce", pi_reduce},
    {"find", pi_find},

    // Matrix
    {"size", pi_size},
    {"mult", pi_mult},
    {"dot", pi_dot},
    {"cross", pi_cross},
    {"eye", pi_eye},
    {"zeros", pi_zeros},
    {"ones", pi_ones},
    {"is_mat", pi_isMat},

    // Object
    {"clone", pi_clone},
    {"values", pi_values},
    {"keys", pi_keys},
};

int BUILTIN_FUNC_COUNT = sizeof(builtin_functions) / sizeof(BuiltinFunc);

BuiltinModule *builtin_modules[] = {
    &math_module,    
    &io_module,};

int BUILTIN_MODULE_COUNT = sizeof(builtin_modules) / sizeof(BuiltinModule *);
