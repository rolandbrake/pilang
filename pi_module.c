#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pi_module.h"
#include "pi_lex.h"
#include "pi_parser.h"
#include "pi_compiler.h"
#include "pi_opcode.h"
#include "pi_string.h"
#include "pi_func.h"
#include "builtin/pi_builtin.h"

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#endif

/**
 * Checks if the given file exists.
 *
 * @param path The path to the file to check.
 * @return True if the file exists, false otherwise.
 */
static bool file_exists(const char *path)
{
    // Open the file in read-only binary mode
    FILE *f = fopen(path, "rb");
    if (!f)
        return false; // Return false if the file could not be opened

    // Close the file
    fclose(f);
    return true; // Return true if the file exists
}

/**
 * Reads the contents of a file and returns it as a null-terminated string.
 *
 * This function opens the given file in binary read mode, seeks to the end of the file to determine its length,
 * allocates memory to store the file contents, reads the file contents into the allocated memory, and closes the file.
 * The allocated memory is returned as a null-terminated string.
 *
 * If any error occurs during the file operations, the function returns NULL.
 *
 * @param path The path to the file to read.
 * @return The contents of the file as a null-terminated string, or NULL if an error occurs.
 */
static char *file_readText(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return NULL;
    }

    long length = ftell(file);
    if (length < 0)
    {
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return NULL;
    }

    char *source = (char *)malloc((size_t)length + 1);
    if (!source)
    {
        fclose(file);
        return NULL;
    }

    size_t bytes = fread(source, 1, (size_t)length, file);
    fclose(file);

    if (bytes != (size_t)length)
    {
        free(source);
        return NULL;
    }

    source[length] = '\0';
    return source;
}

/**
 * Copies the directory name from a given path.
 *
 * @param path The path from which to copy the directory name.
 * @return A newly allocated string containing the directory name. If the path is empty or does not contain a directory name, returns a newly allocated string containing ".".
 */
static char *copy_dirName(const char *path)
{
    if (!path)
        return strdup(".");

    char *dir = strdup(path);
    int len = (int)strlen(dir);

    // Find the last directory separator in the path
    while (len > 0 && dir[len - 1] != '/' && dir[len - 1] != '\\')
        len--;

    // If the path is empty or does not contain a directory name, return "."
    if (len == 0)
    {
        free(dir);
        return strdup(".");
    }

    // Null-terminate the string at the last directory separator
    dir[len - 1] = '\0';
    return dir;
}

static char *build_modulePath(const char *base, const char *normalized)
{
    size_t full_len = strlen(base) + 1 + strlen(normalized) + 3 + 1;
    char *candidate = (char *)malloc(full_len);
    if (!candidate)
        return NULL;

    snprintf(candidate, full_len, "%s/%s.pi", base, normalized);
    return candidate;
}

static const BuiltinModule *find_builtinModule(const char *name)
{
    for (int i = 0; i < BUILTIN_MODULE_COUNT; i++)
    {
        if (strcmp(builtin_modules[i]->name, name) == 0)
            return builtin_modules[i];
    }
    return NULL;
}

#ifdef __EMSCRIPTEN__
static bool is_browserUnsupportedBuiltin(const char *name)
{
    return strcmp(name, "draw") == 0 ||
           strcmp(name, "plot") == 0 ||
           strncmp(name, "draw.", 5) == 0 ||
           strncmp(name, "plot.", 5) == 0;
}
#endif

/**
 * Checks if a built-in module has children (i.e. if it has a dot in its name).
 *
 * @param name The name of the built-in module to check.
 * @return True if the built-in module has children, false otherwise.
 */
static bool builtin_hasChildren(const char *name)
{
    size_t prefix_len = strlen(name);

    // Iterate over all built-in modules
    for (int i = 0; i < BUILTIN_MODULE_COUNT; i++)
    {
        const char *builtin_name = builtin_modules[i]->name;
        // Check if the built-in module has the given name as prefix
        // and if it has a dot after the prefix
        if (strncmp(builtin_name, name, prefix_len) == 0 &&
            builtin_name[prefix_len] == '.')
            return true;
    }

    return false;
}

static Value load_builtinNamed(vm_t *vm, const char *name);

/**
 * Attaches the children of a built-in module to its exports map.
 *
 * A child of a built-in module is a built-in module that has a name that is a prefix of the given name.
 * For example, if the given name is "tensor", the children of the built-in module could be "tensor.sort", "tensor.reduce", etc.
 *
 * @param vm The VM to use.
 * @param module The module to attach the children to.
 * @param name The name of the module to use as prefix to find children.
 */
static void attach_builtinChildren(vm_t *vm, ObjModule *module, const char *name)
{
    size_t prefix_len = strlen(name);
    table_t *seen = ht_create(sizeof(bool));
    bool yes = true;

    for (int i = 0; i < BUILTIN_MODULE_COUNT; i++)
    {
        const char *builtin_name = builtin_modules[i]->name;
        if (strncmp(builtin_name, name, prefix_len) != 0 ||
            builtin_name[prefix_len] != '.')
            continue;

        const char *child_start = builtin_name + prefix_len + 1;
        const char *child_end = strchr(child_start, '.');
        size_t child_len = child_end ? (size_t)(child_end - child_start) : strlen(child_start);

        char *child_name = (char *)malloc(child_len + 1);
        if (!child_name)
            vm_error(vm, "Out of memory while loading builtin package.");

        memcpy(child_name, child_start, child_len);
        child_name[child_len] = '\0';

        if (ht_get(seen, child_name))
        {
            free(child_name);
            continue;
        }
        ht_put(seen, child_name, &yes);

        size_t full_len = prefix_len + 1 + child_len + 1;
        char *full_name = (char *)malloc(full_len);
        if (!full_name)
        {
            free(child_name);
            ht_free(seen);
            vm_error(vm, "Out of memory while loading builtin package.");
        }

        snprintf(full_name, full_len, "%s.%s", name, child_name);
        Value child_module = load_builtinNamed(vm, full_name);

        Value key_val = NEW_OBJ(add_obj(vm, new_pistring(strdup(child_name))));
        map_set(module->exports, key_val, child_module);

        free(full_name);
        free(child_name);
    }

    ht_free(seen);
}

/**
 * Loads a built-in module from the given built-in module definition.
 *
 * Checks if the module is already cached in the VM. If so, returns the cached value.
 * Otherwise, creates a new module object, caches it, and populates its exports map with the given built-in module's functions and constants.
 *
 * @param vm The VM to use.
 * @param builtin The built-in module definition to load.
 * @return The loaded module as a value.
 */
static Value load_builtinModule(vm_t *vm, const BuiltinModule *builtin)
{
    // Create a cache key for the module
    char cache_key[256];
    snprintf(cache_key, sizeof(cache_key), "@builtin:%s", builtin->name);

    // Check if the module is already cached in the VM
    Value *cached = ht_get(vm->modules, cache_key);
    if (cached)
        return *cached;

    // Create a new module object and cache it
    Object *module_obj = new_module(vm, builtin->name, "<builtin>", true, false);
    Value module_val = NEW_OBJ(module_obj);
    ht_put(vm->modules, cache_key, &module_val);

    // Populate the module's exports map with the given built-in module's functions and constants
    ObjModule *module = AS_MODULE(module_val);
    PiMap *exports = module->exports;

    for (int i = 0; i < builtin->const_count; i++)
    {
        // Add the built-in module's constants to the module's exports map
        BuiltinConst *c = &builtin->consts[i];
        Value key_val = NEW_OBJ(add_obj(vm, new_pistring(strdup(c->name))));
        map_set(exports, key_val, c->value);
    }

    for (int i = 0; i < builtin->func_count; i++)
    {
        // Add the built-in module's functions to the module's exports map
        BuiltinFunc *f = &builtin->functions[i];
        Value *fn_val = new_native(f->name, f->func);
        Value key_val = NEW_OBJ(add_obj(vm, new_pistring(strdup(f->name))));
        map_set(exports, key_val, *fn_val);
        free(fn_val);
    }

    // If the built-in module has children, attach them to the module
    if (builtin_hasChildren(builtin->name))
        attach_builtinChildren(vm, module, builtin->name);

    // Mark the module as loaded
    module->state = MODULE_LOADED;

    return module_val;
}

static Value load_builtinPackage(vm_t *vm, const char *name)
{
    char cache_key[256];
    snprintf(cache_key, sizeof(cache_key), "@builtin:%s", name);

    Value *cached = ht_get(vm->modules, cache_key);
    if (cached)
        return *cached;

    Object *module_obj = new_module(vm, name, "<builtin-package>", true, false);
    Value module_val = NEW_OBJ(module_obj);
    ht_put(vm->modules, cache_key, &module_val);

    ObjModule *module = AS_MODULE(module_val);
    attach_builtinChildren(vm, module, name);
    module->state = MODULE_LOADED;
    return module_val;
}

static Value load_builtinNamed(vm_t *vm, const char *name)
{
#ifdef __EMSCRIPTEN__
    if (is_browserUnsupportedBuiltin(name))
    {
        vm_errorf(vm, "Builtin module '%s' is not available in the browser playground.", name);
        return NEW_NIL();
    }
#endif

    const BuiltinModule *builtin = find_builtinModule(name);
    if (builtin)
        return load_builtinModule(vm, builtin);

    if (builtin_hasChildren(name))
        return load_builtinPackage(vm, name);

    vm_errorf(vm, "Cannot resolve module '%s'.", name);
    return NEW_NIL();
}

/**
 * Creates a new module object.
 *
 * @param vm The virtual machine.
 * @param name The name of the module.
 * @param path The path to the module file.
 * @param builtin Whether the module is a builtin module.
 * @param is_main Whether the module is the entry point of the program.
 *
 * @return The new module object.
 */
Object *new_module(vm_t *vm, const char *name, const char *path, bool builtin, bool is_main)
{
    // Allocate memory for the module object
    ObjModule *module = (ObjModule *)malloc(sizeof(ObjModule));
    if (!module)
        // Out of memory
        vm_error(vm, "Out of memory while creating module object.");

    // Initialize the module object
    module->object.type = OBJ_MODULE;
    module->object.is_marked = false;
    module->object.in_gcList = false;
    module->object.gc_color = GC_WHITE;
    module->object.next = NULL;

    // Set the module name and path
    module->name = strdup(name ? name : "");
    module->path = strdup(path ? path : "");

    // Set whether the module is a builtin module or the entry point
    module->builtin = builtin;
    module->is_main = is_main;

    // Set the initial state of the module
    module->state = MODULE_LOADING;

    // Initialize the constants and names tables
    module->constants = NULL;
    module->names = NULL;
    module->globals = NULL;

    // Create a new map to store the module's exports
    Object *exports_obj = add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    module->exports = (PiMap *)exports_obj;

    // Add the module object to the VM
    return add_obj(vm, (Object *)module);
}

/**
 * Creates a new BuiltinModule.
 *
 * @param name The name of the module.
 * @param functions An array of BuiltinFuncs that will be exposed by the module.
 * @param func_count The number of functions in the functions array.
 * @param consts An array of BuiltinConsts that will be exposed by the module.
 * @param const_count The number of constants in the consts array.
 *
 * @return A new BuiltinModule.
 */
BuiltinModule *new_builtinModule(const char *name, BuiltinFunc *functions,
                                 int func_count, BuiltinConst *consts, int const_count)
{
    BuiltinModule *module = (BuiltinModule *)malloc(sizeof(BuiltinModule));
    if (!module)
        return NULL;

    module->name = strdup(name);
    module->functions = functions;
    module->func_count = func_count;
    module->consts = consts;
    module->const_count = const_count;

    return module;
}

static bool is_private_moduleName(const char *name)
{
    return name && name[0] == '_';
}

static table_t *collect_definedGlobals(compiler_t *comp)
{
    table_t *defined = ht_create(sizeof(bool));
    bool yes = true;

    uint8_t *code = (uint8_t *)comp->code->data;
    int size = list_size(comp->code);
    int pc = 0;

    while (pc < size)
    {
        uint8_t op = code[pc++];

        if (op == OP_STORE_GLOBAL && pc < size)
        {
            int name_index = code[pc];
            if (name_index >= 0 && name_index < list_size(comp->names))
            {
                char *name = string_get(comp->names, name_index);
                ht_put(defined, name, &yes);
            }
        }
        pc += operand_count(op);
    }

    return defined;
}

char *module_resolvePath(vm_t *vm, const char *name)
{
    if (!name || !*name)
        return NULL;

    char cwd[4096];
    const char *base = vm->current_path;

    if (!base)
    {
        if (!getcwd(cwd, sizeof(cwd)))
            return NULL;
        base = cwd;
    }

    size_t name_len = strlen(name);
    char *normalized = strdup(name);
    for (size_t i = 0; i < name_len; i++)
    {
        if (normalized[i] == '.')
            normalized[i] = '/';
    }

    char *candidate = build_modulePath(base, normalized);
    if (!candidate)
    {
        free(normalized);
        return NULL;
    }

    if (file_exists(candidate))
    {
        free(normalized);
        return candidate;
    }

    size_t local_len = name_len + 3 + 1;
    char *fallback = (char *)malloc(local_len);
    snprintf(fallback, local_len, "%s.pi", normalized);

    free(candidate);

    if (file_exists(fallback))
        return fallback;

    free(fallback);

    char *libs_path = build_modulePath("libs", normalized);
    free(normalized);
    if (!libs_path)
        return NULL;

    if (file_exists(libs_path))
        return libs_path;

    free(libs_path);
    return NULL;
}

/**
 * Loads a module from a file path.
 *
 * Resolves the given module name relative to the current module path, if any.
 * If the module has already been loaded, returns the cached value.
 * Otherwise, reads the module file, parses it, and runs it in a new VM.
 * The module's globals are then exposed as a map.
 *
 * @param vm The current VM.
 * @param name The name of the module to load.
 * @return A value representing the loaded module.
 */
Value load_module(vm_t *vm, const char *name)
{
    char *resolved = module_resolvePath(vm, name);
    if (!resolved)
        return load_builtinNamed(vm, name);

    Value *cached = ht_get(vm->modules, resolved);
    if (cached)
    {
        Value loaded = *cached;
        free(resolved);
        return loaded;
    }

    // Create/cache module object early to support recursive imports.
    Object *module_obj = new_module(vm, name, resolved, false, false);
    Value module_val = NEW_OBJ(module_obj);
    ht_put(vm->modules, resolved, &module_val);

    char *source = file_readText(resolved);
    if (!source)
        vm_errorf(vm, "Cannot read module '%s' (%s).", resolved, strerror(errno));

    init_scanner(source);
    token_t *tokens = scan();
    compiler_t *comp = init_compiler();
    comp->source_name = strdup(resolved);
    parser_t *parser = init_parser(comp, tokens, MODE_FILE);
    parse(parser);

    // printf("Module '%s' loaded from '%s'.\n", name, resolved);
    // dis(comp);

    vm_t *module_vm = init_vm(comp, NULL, false);

    // Share the parent VM's module cache with the module VM to allow caching of nested imports.
    ht_free(module_vm->modules);
    module_vm->modules = vm->modules;

    // Set the module VM's current path to the directory of the resolved module to
    // allow relative imports within the module.
    if (module_vm->current_path)
        free(module_vm->current_path);
    module_vm->current_path = copy_dirName(resolved);

    // Inside imported files, `module` refers to that file's module object.
    ht_set(module_vm->globals, "module", &module_val);

    while (module_vm->running)
        run(module_vm);

    ObjModule *module = AS_MODULE(module_val);
    PiMap *exports = module->exports;
    table_t *defined_globals = collect_definedGlobals(comp);

    int keys_count = ht_length(defined_globals);
    char **keys = ht_keys(defined_globals);
    for (int i = 0; i < keys_count; i++)
    {
        char *key = keys[i];
        if (is_private_moduleName(key))
            continue;

        Value *value = ht_get(module_vm->globals, key);
        if (!value)
            continue;

        Value key_val = NEW_OBJ(add_obj(vm, new_pistring(strdup(key))));
        map_set(exports, key_val, *value);
    }
    ht_free(defined_globals);
    module->state = MODULE_LOADED;

    // Preserve module constants/names for functions created in this module.
    module->constants = comp->constants;
    module->names = comp->names;
    comp->constants = NULL;
    comp->names = NULL;

    free_parser(parser);
    free_compiler(comp);

    // Preserve the module VM global table for later calls to functions defined in this module.
    module->globals = module_vm->globals;
    module_vm->globals = NULL;

    // Detach shared module cache so free_vm(module_vm) doesn't free parent cache.
    module_vm->modules = NULL;
    free_vm(module_vm);

    free(source);
    free(resolved);

    return module_val;
}
