#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pi_module.h"
#include "pi_lex.h"
#include "pi_parser.h"
#include "pi_compiler.h"
#include "pi_opcode.h"
#include "string.h"

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

Object *new_module(vm_t *vm, const char *name, const char *path, bool builtin, bool is_main)
{
    ObjModule *module = (ObjModule *)malloc(sizeof(ObjModule));
    if (!module)
        vm_error(vm, "Out of memory while creating module object.");

    module->object.type = OBJ_MODULE;
    module->object.is_marked = false;
    module->object.in_gcList = false;
    module->object.gc_color = GC_WHITE;
    module->object.next = NULL;

    module->name = strdup(name ? name : "");
    module->path = strdup(path ? path : "");
    module->builtin = builtin;
    module->is_main = is_main;
    module->state = MODULE_LOADING;
    module->constants = NULL;
    module->names = NULL;

    Object *exports_obj = add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    module->exports = (PiMap *)exports_obj;

    return add_obj(vm, (Object *)module);
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

    size_t full_len = strlen(base) + 1 + name_len + 3 + 1; // base + "/" + name + ".pi" + '\0'
    char *candidate = (char *)malloc(full_len);
    snprintf(candidate, full_len, "%s/%s.pi", base, normalized);

    if (file_exists(candidate))
    {
        free(normalized);
        return candidate;
    }

    size_t local_len = name_len + 3 + 1;
    char *fallback = (char *)malloc(local_len);
    snprintf(fallback, local_len, "%s.pi", normalized);

    free(candidate);
    free(normalized);

    if (file_exists(fallback))
        return fallback;

    free(fallback);
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
        vm_errorf(vm, "Cannot resolve module '%s'.", name);

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

    // Detach shared module cache so free_vm(module_vm) doesn't free parent cache.
    module_vm->modules = NULL;
    free_vm(module_vm);

    free(source);
    free(resolved);

    return module_val;
}
