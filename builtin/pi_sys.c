#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "pi_sys.h"
#include "../pi_value.h"
#include "../pi_object.h"
#include "../list.h"
#include "../common.h"
#include "pi_builtin.h"

/**
 * Prints an error message to the standard error stream and returns a null value.
 * If no arguments are provided, an error is raised.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be at least 1).
 * @param argv Arguments: [error message string]
 * @return A null value.
 */

Value pi_error(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[error] expects at least one argument.");

    const char *str = as_string(argv[0]);
    printf("Error: %s\n", str);
    free((void *)str);
    return NEW_NIL();
}

/**
 * Asserts that a condition is true, else prints an error message and exits the program.
 *
 * This function takes two arguments: a condition and an error message string. If the condition is false,
 * the error message is printed to the standard error stream and the program exits with a status code
 * of 1.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 2).
 * @param argv Arguments: [condition, error message string]
 * @return true if the condition is true, otherwise prints an error message and exits the program.
 */
Value pi_assert(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[assert] expects at least two arguments: condition and message.");

    Value condition = argv[0];
    const char *message = as_string(argv[1]);

    if (!as_bool(condition))
    {
        printf("Assertion failed: %s\n", message);
        free((void *)message);
        exit(EXIT_FAILURE);
    }

    free((void *)message);
    return NEW_BOOL(true);
}

/**
 * Prints the Zen of Pi-Lang to the standard output stream.
 *
 * The Zen of Pi-Lang is a set of tenets that describe the philosophy and
 * guiding principles of the language.
 *
 * @param vm The virtual machine instance.
 * @param argc Number of arguments (should be 0).
 * @param argv No arguments.
 * @return A string value containing the Zen of Pi-Lang.
 */
Value pi_zen(vm_t *vm, int argc, Value *argv)
{
    if (argc != 0)
        vm_error(vm, "[zen] expects no arguments.");

    return NEW_OBJ(new_pistring(strdup(
        " -------------------\n"
        " The Zen of Pi-Lang\n"
        " -------------------\n"
        " 1. Simplicity is power.\n"
        " 2. Functions shape the flow.\n"
        " 3. Tables hold the world.\n"
        " 4. Readability counts.\n"
        " 5. Special cases aren't special enough to break the rules.\n"
        " 6. Freedom in code, structure in choice.\n"
        " 7. Dynamic, yet precise.\n"
        " 8. Expressive, yet concise.\n"
        " 9. Less syntax, more meaning.\n"
        " 10. A script should feel like art.\n"
        "----------------------------------------\n")));
}

// read command-line arguments from stdin and return them as a list of strings argv[0]
// is the program name, argv[1] is the first argument, and so on.
Value pi_argv(vm_t *vm, int argc, Value *argv)
{
    list_t *args = list_create(sizeof(Value));

    for (int i = 0; i < pi_cli_argc; i++)
    {
        Object *str_obj = add_obj(vm, new_pistring(strdup(pi_cli_argv[i])));
        Value val = NEW_OBJ(str_obj);
        list_add(args, &val);
    }

    Object *list_obj = add_obj(vm, new_list(args));
    return NEW_OBJ(list_obj);
}

Value pi_exit(vm_t *vm, int argc, Value *argv)
{
    int code = 0;
    if (argc > 0)
        code = (int)as_number(argv[0]);
    exit(code);
}

// A string that identifies the underlying operating system (e.g., 'win32', 'linux', 'darwin').
Value pi_platform(vm_t *vm, int argc, Value *argv)
{
#ifdef _WIN32
    return NEW_OBJ(new_pistring(strdup("Windows")));
#else
    struct utsname buf;

    if (uname(&buf) == 0)
        return NEW_OBJ(new_pistring(strdup(buf.sysname)));

/* Fallback if uname fails */
#if defined(__linux__)
    return NEW_OBJ(new_pistring(strdup("Linux")));
#elif defined(__APPLE__) && defined(__MACH__)
    return NEW_OBJ(new_pistring(strdup("macOS")));
#elif defined(__unix__)
    return NEW_OBJ(new_pistring(strdup("Unix")));
#else
    return NEW_OBJ(new_pistring(strdup("unknown")));
#endif

#endif
}

// A string that represents the current version of Pi-Lang.
Value pi_version(vm_t *vm, int argc, Value *argv)
{
    return NEW_OBJ(new_pistring(strdup(PI_VERSION)));
}

// A string that represents the current working directory.
Value pi_path(vm_t *vm, int argc, Value *argv)
{
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        return NEW_OBJ(new_pistring(strdup(cwd)));
    else
        return NEW_OBJ(new_pistring(strdup("unknown")));
}

// Module Defenition
static BuiltinConst sys_consts[] = {
    {"EXIT_SUCCESS", {VAL_NUM, {.number = EXIT_SUCCESS}}},
    {"EXIT_FAILURE", {VAL_NUM, {.number = EXIT_FAILURE}}},

};

static BuiltinFunc sys_funcs[] = {
    {"argv", pi_argv},
    {"exit", pi_exit},
    {"platform", pi_platform},
    {"version", pi_version},
    {"path", pi_path},
};

DEFINE_BUILTIN_MODULE(sys_module, "sys", sys_funcs, sys_consts);
