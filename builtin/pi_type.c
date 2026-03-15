#include "pi_type.h"

/**
 * @brief Returns the type of the given value as a string.
 *
 * @param vm The virtual machine instance.
 * @param argc The number of arguments passed to the function.
 * @param argv The arguments provided to the function.
 * @return A string representing the type of the argument.
 */
Value _pi_type(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[type] expects at least one argument.");

    // Get the type name of the argument
    char *type = type_name(argv[0]);

    // Return the type name as a string object
    return NEW_OBJ(new_pistring(strdup(type)));
}

// Returns true if the argument is a list
Value pi_isList(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[is_list] expects one argument.");

    return NEW_BOOL(IS_LIST(argv[0]));
}

// Returns true if the argument is a map
Value pi_isMap(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[is_map] expects one argument.");

    return NEW_BOOL(IS_MAP(argv[0]));
}

// Returns true if the argument is numeric (integer or float)
Value pi_isNum(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[is_num] expects one argument.");

    return NEW_BOOL(is_numeric(argv[0]));
}

// Returns true if the argument is a string
Value pi_isStr(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[is_str] expects one argument.");

    return NEW_BOOL(IS_STRING(argv[0]));
}

// Returns true if the argument is a boolean
Value pi_isBool(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[is_bool] expects one argument.");

    return NEW_BOOL(IS_BOOL(argv[0]));
}

// Converts the argument to a list if possible, else throws an error

// Converts the argument to a number if possible, else throws an error
Value pi_asNum(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[as_num] expects one argument.");

    if (is_numeric(argv[0]))
        return NEW_NUM(as_number(argv[0]));
    else
        vm_error(vm, "[as_num] argument is not numeric.");

    return NEW_NIL();
}

// Converts the argument to a string if possible, else throws an error
Value pi_asStr(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[as_str] expects one argument.");

    return NEW_OBJ(new_pistring(as_string(argv[0])));

    return NEW_NIL();
}

// Converts the argument to a boolean if possible, else throws an error
Value pi_asBool(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[as_bool] expects one argument.");

    return NEW_BOOL(as_bool(argv[0]));

    return NEW_NIL();
}
