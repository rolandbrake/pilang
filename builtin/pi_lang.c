#include "pi_lang.h"
#include "pi_builtin.h"

static BuiltinConst lang_const[] = {
    {"OP_ADD", NEW_NUM(0)},
    {"OP_SUB", NEW_NUM(1)},
    {"OP_MUL", NEW_NUM(2)},
    {"OP_DIV", NEW_NUM(3)},
    {"OP_MOD", NEW_NUM(4)},
    {"OP_LAND", NEW_NUM(5)},
    {"OP_LOR", NEW_NUM(6)},
    {"OP_POW", NEW_NUM(7)},
    {"OP_BAND", NEW_NUM(8)},
    {"OP_BOR", NEW_NUM(9)},
    {"OP_BXOR", NEW_NUM(10)},
    {"OP_SHL", NEW_NUM(11)},
    {"OP_SHR", NEW_NUM(12)},
    {"OP_USHR", NEW_NUM(13)},
    {"OP_DOT", NEW_NUM(14)},
    {"OP_IS", NEW_NUM(15)},

    {"OP_POS", NEW_NUM(100)},
    {"OP_NEG", NEW_NUM(101)},
    {"OP_BNOT", NEW_NUM(103)},

    {"OP_EQ", NEW_NUM(200)},
    {"OP_NE", NEW_NUM(201)},
    {"OP_GT", NEW_NUM(202)},
    {"OP_LT", NEW_NUM(203)},
    {"OP_GE", NEW_NUM(204)},
    {"OP_LE", NEW_NUM(205)},
};

static BuiltinFunc lang_funcs[] = {};

DEFINE_BUILTIN_MODULE(module_lang, "lang", lang_funcs, lang_const);
