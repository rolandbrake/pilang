#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "pi_sys.h"
#include "pi_builtin.h"

#include "../pi_value.h"
#include "../pi_object.h"
#include "../pi_list.h"
#include "../common.h"
#include "../gc.h"

Value pi_error(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[error] expects at least one argument.");

    const char *str = as_string(argv[0]);
    printf("Error: %s\n", str);
    free((void *)str);
    return NEW_NIL();
}

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

Value pi_zen(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;

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

Value sy_argv(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;

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

Value sy_exit(vm_t *vm, int argc, Value *argv)
{
    (void)vm;

    int code = 0;
    if (argc > 0)
        code = (int)as_number(argv[0]);

    exit(code);
}

Value sy_platform(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    (void)argc;
    (void)argv;

#ifdef __EMSCRIPTEN__
    return NEW_OBJ(new_pistring(strdup("Emscripten")));
#elif defined(_WIN32)
    return NEW_OBJ(new_pistring(strdup("Windows")));
#else
    struct utsname buf;

    if (uname(&buf) == 0)
        return NEW_OBJ(new_pistring(strdup(buf.sysname)));

    // Fallback compile-time checks are used only if uname() fails.
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

Value sy_version(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    (void)argc;
    (void)argv;

    return NEW_OBJ(new_pistring(strdup(PI_VERSION)));
}

Value sy_path(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    (void)argc;
    (void)argv;

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        return NEW_OBJ(new_pistring(strdup(cwd)));

    return NEW_OBJ(new_pistring(strdup("unknown")));
}

Value sy_env(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "[env] expects one argument.");

    char *key = as_string(argv[0]);
    char *value = getenv(key);

    if (value == NULL)
    {
        free(key);
        return NEW_BOOL(false);
    }

    Value result = NEW_OBJ(new_pistring(strdup(value)));
    free(key);
    return result;
}

Value sy_gc(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;

    run_gc(vm);
    gc_trimHeap();
    return NEW_BOOL(true);
}

#ifdef __linux__
#include <sys/resource.h>
#endif

Value sy_mem(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    (void)argc;
    (void)argv;

#ifdef __linux__
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        return NEW_NUM((double)usage.ru_maxrss * 1024); // Linux reports max RSS in KB.
#endif

    return NEW_NUM(0);
}

Value sy_pid(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    (void)argc;
    (void)argv;

    return NEW_NUM((double)getpid());
}

static BuiltinConst sys_consts[] = {
    {"EXIT_SUCCESS", {VAL_NUM, {.number = EXIT_SUCCESS}}},
    {"EXIT_FAILURE", {VAL_NUM, {.number = EXIT_FAILURE}}},
};

static BuiltinFunc sys_funcs[] = {
    {"argv", sy_argv},
    {"exit", sy_exit},
    {"platform", sy_platform},
    {"version", sy_version},
    {"path", sy_path},
    {"env", sy_env},
    {"gc", sy_gc},
    {"mem", sy_mem},
    {"pid", sy_pid},
};

DEFINE_BUILTIN_MODULE(module_sys, "sys", sys_funcs, sys_consts);
