#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pi_module.h"
#include "pi_build.h"
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

static bool _file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return FALSE; // Return false if the file could not be opened
    fclose(f);
    return TRUE; // Return true if the file exists
}

// Reads the entire file into a NUL-terminated buffer.
static char *_file_readText(const char *path)
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

static char *_copy_dirName(const char *path)
{
    if (!path)
        return strdup(".");

    char *dir = strdup(path);
    int len = (int)strlen(dir);
    while (len > 0 && dir[len - 1] != '/' && dir[len - 1] != '\\')
        len--;
    if (len == 0)
    {
        free(dir);
        return strdup(".");
    }
    dir[len - 1] = '\0';
    return dir;
}

static char *_build_modulePath(const char *base, const char *normalized)
{
    size_t full_len = strlen(base) + 1 + strlen(normalized) + 3 + 1;
    char *candidate = (char *)malloc(full_len);
    if (!candidate)
        return NULL;

    snprintf(candidate, full_len, "%s/%s.pi", base, normalized);
    return candidate;
}

// Finds libs/<module>.pi in base or one of its parent directories.  This lets
// examples run from nested folders (for example ML/KNN) while still sharing
// the project's top-level libraries.
static char *_find_libraryPath(const char *base, const char *normalized)
{
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd)))
        return NULL;

    bool absolute = base[0] == '/' || base[0] == '\\';
#ifdef _WIN32
    absolute = absolute || (strlen(base) > 1 && base[1] == ':');
#endif

    size_t search_len = absolute ? strlen(base) : strlen(cwd) + 1 + strlen(base);
    char *search = (char *)malloc(search_len + 1);
    if (!search)
        return NULL;

    if (absolute)
        snprintf(search, search_len + 1, "%s", base);
    else
        snprintf(search, search_len + 1, "%s/%s", cwd, base);

    while (TRUE)
    {
        size_t libs_base_len = strlen(search) + strlen("/libs") + 1;
        char *libs_base = (char *)malloc(libs_base_len);
        if (!libs_base)
            break;
        snprintf(libs_base, libs_base_len, "%s/libs", search);

        char *path = _build_modulePath(libs_base, normalized);
        free(libs_base);
        if (path && _file_exists(path))
        {
            free(search);
            return path;
        }
        free(path);

        size_t len = strlen(search);
        while (len > 0 && (search[len - 1] == '/' || search[len - 1] == '\\'))
            len--;
        while (len > 0 && search[len - 1] != '/' && search[len - 1] != '\\')
            len--;
        if (len == 0)
            break;

        // Keep a drive or POSIX root separator while moving to its parent.
        if (len > 1)
            len--;
        search[len] = '\0';
    }

    free(search);
    return NULL;
}

static const BuiltinModule *_find_builtinModule(const char *name)
{
    for (int i = 0; i < BUILTIN_MODULE_COUNT; i++)
    {
        if (strcmp(builtin_modules[i]->name, name) == 0)
            return builtin_modules[i];
    }
    return NULL;
}

#ifdef __EMSCRIPTEN__
static bool _is_browserUnsupportedBuiltin(const char *name)
{
    return strcmp(name, "plot") == 0 || strncmp(name, "plot.", 5) == 0;
}
#endif

static bool _builtin_hasChildren(const char *name)
{
    size_t prefix_len = strlen(name);
    for (int i = 0; i < BUILTIN_MODULE_COUNT; i++)
    {
        const char *builtin_name = builtin_modules[i]->name;
        if (strncmp(builtin_name, name, prefix_len) == 0 &&
            builtin_name[prefix_len] == '.')
            return TRUE;
    }

    return FALSE;
}

static Value _load_builtinNamed(VM *vm, const char *name);

// Exposes nested builtin modules as fields on the parent package.
static void _attach_builtinChildren(VM *vm, ObjModule *module, const char *name)
{
    size_t prefix_len = strlen(name);
    HT_Table *seen = ht_create(sizeof(bool));
    bool yes = TRUE;

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

        if (_ht_get(seen, child_name))
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
        Value child_module = _load_builtinNamed(vm, full_name);

        Value key_val = NEW_OBJ(add_obj(vm, new_pistring(strdup(child_name))));
        map_set(module->exports, key_val, child_module);

        free(full_name);
        free(child_name);
    }

    ht_free(seen);
}

// Builtin modules are cached once and reused across imports.
static Value _load_builtinModule(VM *vm, const BuiltinModule *builtin)
{
    char cache_key[256];
    snprintf(cache_key, sizeof(cache_key), "@builtin:%s", builtin->name);
    Value *cached = _ht_get(vm->modules, cache_key);
    if (cached)
        return *cached;
    Object *module_obj = new_module(vm, builtin->name, "<builtin>", TRUE, FALSE);
    Value module_val = NEW_OBJ(module_obj);
    ht_put(vm->modules, cache_key, &module_val);
    ObjModule *module = AS_MODULE(module_val);
    PiMap *exports = module->exports;

    for (int i = 0; i < builtin->const_count; i++)
    {
        BuiltinConst *c = &builtin->consts[i];
        Value key_val = NEW_OBJ(add_obj(vm, new_pistring(strdup(c->name))));
        map_set(exports, key_val, c->value);
    }

    for (int i = 0; i < builtin->func_count; i++)
    {
        BuiltinFunc *f = &builtin->functions[i];
        Value *fn_val = new_native(f->name, f->func);
        Value key_val = NEW_OBJ(add_obj(vm, new_pistring(strdup(f->name))));
        map_set(exports, key_val, *fn_val);
        free(fn_val);
    }
    if (_builtin_hasChildren(builtin->name))
        _attach_builtinChildren(vm, module, builtin->name);
    module->state = MODULE_LOADED;

    return module_val;
}

static Value _load_builtinPackage(VM *vm, const char *name)
{
    char cache_key[256];
    snprintf(cache_key, sizeof(cache_key), "@builtin:%s", name);

    Value *cached = _ht_get(vm->modules, cache_key);
    if (cached)
        return *cached;

    Object *module_obj = new_module(vm, name, "<builtin-package>", TRUE, FALSE);
    Value module_val = NEW_OBJ(module_obj);
    ht_put(vm->modules, cache_key, &module_val);

    ObjModule *module = AS_MODULE(module_val);
    _attach_builtinChildren(vm, module, name);
    module->state = MODULE_LOADED;
    return module_val;
}

static Value _load_builtinNamed(VM *vm, const char *name)
{
#ifdef __EMSCRIPTEN__
    if (_is_browserUnsupportedBuiltin(name))
    {
        vm_errorf(vm, "Builtin module '%s' is not available in the browser playground.", name);
        return NEW_NIL();
    }
#endif

    const BuiltinModule *builtin = _find_builtinModule(name);
    if (builtin)
        return _load_builtinModule(vm, builtin);

    if (_builtin_hasChildren(name))
        return _load_builtinPackage(vm, name);

    vm_errorf(vm, "Cannot resolve module '%s'.", name);
    return NEW_NIL();
}

// Creates a module object in the LOADING state. It becomes LOADED after execution completes.
Object *new_module(VM *vm, const char *name, const char *path, bool builtin, bool is_main)
{
    ObjModule *module = (ObjModule *)malloc(sizeof(ObjModule));
    if (!module)
        vm_error(vm, "Out of memory while creating module object.");
    module->object.type = OBJ_MODULE;
    module->object.is_marked = FALSE;
    module->object.in_gcList = FALSE;
    module->object.gc_color = GC_WHITE;
    module->object.next = NULL;
    module->name = strdup(name ? name : "");
    module->path = strdup(path ? path : "");
    module->builtin = builtin;
    module->is_main = is_main;
    module->state = MODULE_LOADING;
    module->code = NULL;
    module->constants = NULL;
    module->names = NULL;
    module->instrs = NULL;
    module->globals = NULL;
    module->source_size = 0;
    module->source_hash = 0;
    Object *exports_obj = add_obj(vm, new_map(ht_create(sizeof(Value))));
    module->exports = (PiMap *)exports_obj;
    return add_obj(vm, (Object *)module);
}

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

static bool _is_PrivateModuleName(const char *name)
{
    return name && name[0] == '_';
}

static HT_Table *_collect_definedGlobals(Compiler *comp)
{
    HT_Table *defined = ht_create(sizeof(bool));
    bool yes = TRUE;

    uint8_t *code = (uint8_t *)comp->code->data;
    int size = LIST_SIZE(comp->code);
    int pc = 0;

    while (pc < size)
    {
        uint8_t op = code[pc++];

        if (op == OP_STORE_GLOBAL && pc < size)
        {
            int name_index = code[pc];
            if (name_index >= 0 && name_index < LIST_SIZE(comp->names))
            {
                char *name = string_get(comp->names, name_index);
                ht_put(defined, name, &yes);
            }
        }
        pc += _operand_count(op);
    }

    return defined;
}

// Resolution order: current module directory -> local file -> libs/ directory.
char *module_resolvePathFrom(const char *base, const char *name)
{
    if (!name || !*name)
        return NULL;

    char cwd[4096];
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

    char *candidate = _build_modulePath(base, normalized);
    if (!candidate)
    {
        free(normalized);
        return NULL;
    }

    if (_file_exists(candidate))
    {
        free(normalized);
        return candidate;
    }

    size_t local_len = name_len + 3 + 1;
    char *fallback = (char *)malloc(local_len);
    snprintf(fallback, local_len, "%s.pi", normalized);

    free(candidate);

    if (_file_exists(fallback))
        return fallback;

    free(fallback);

    char *libs_path = _find_libraryPath(base, normalized);
    free(normalized);
    if (!libs_path)
        return NULL;

    if (_file_exists(libs_path))
        return libs_path;

    free(libs_path);
    return NULL;
}

char *module_resolvePath(VM *vm, const char *name)
{
    return module_resolvePathFrom(vm ? vm->current_path : NULL, name);
}

// Modules are cached before execution to support recursive/cyclic imports.
Value load_module(VM *vm, const char *name)
{
    char *resolved = module_resolvePath(vm, name);
    if (!resolved)
    {
        PX_BuiltModule *built_module = px_findBuiltModuleByName(vm->project_build, name);
        if (built_module)
            resolved = strdup(built_module->path);
    }
    if (!resolved)
        return _load_builtinNamed(vm, name);

    Value *cached = _ht_get(vm->modules, resolved);
    if (cached)
    {
        Value loaded = *cached;
        free(resolved);
        return loaded;
    }

    // Create/cache module object early to support recursive imports.
    Object *module_obj = new_module(vm, name, resolved, FALSE, FALSE);
    Value module_val = NEW_OBJ(module_obj);
    ht_put(vm->modules, resolved, &module_val);

    ObjModule *module = AS_MODULE(module_val);
    char *source = NULL;
    Parser *parser = NULL;
    Compiler *comp = NULL;
    PX_BuiltModule *built_module = px_findBuiltModule(vm->project_build, resolved);
    if (built_module && built_module->compiler)
    {
        comp = built_module->compiler;
        built_module->compiler = NULL;
        module->source_size = built_module->source_size;
        module->source_hash = built_module->source_hash;
        comp->source_name = strdup(resolved);
    }
    else
    {
        source = _file_readText(resolved);
        if (!source)
            vm_errorf(vm, "Cannot read module '%s' (%s).", resolved, strerror(errno));

        module->source_size = strlen(source);
        module->source_hash = string_hash(source, module->source_size);

        init_scanner(source);
        Token *tokens = scan();
        comp = init_compiler();
        comp->source_name = strdup(resolved);
        parser = init_parser(comp, tokens, MODE_FILE);
        parse(parser);
    }

    VM *module_vm = init_vm(comp, NULL, FALSE);

    // Share the parent VM's module cache with the module VM to allow caching of nested imports.
    ht_free(module_vm->modules);
    module_vm->modules = vm->modules;
    module_vm->project_build = vm->project_build;

    // Set the module VM's current path to the directory of the resolved module to
    // allow relative imports within the module.
    if (module_vm->current_path)
        free(module_vm->current_path);
    module_vm->current_path = _copy_dirName(resolved);

    // Inside imported files, `module` refers to that file's module object.
    ht_set(module_vm->globals, "module", &module_val);

    while (module_vm->running)
        vm_run(module_vm);

    PiMap *exports = module->exports;
    HT_Table *defined_globals = _collect_definedGlobals(comp);

    // Iterate over defined_globals using the new iterator API
    HT_Iterator it = _ht_iterator(defined_globals);
    while (_ht_next(&it))
    {
        const char *key = it.key;
        if (_is_PrivateModuleName(key))
            continue;

        Value *value = _ht_get(module_vm->globals, key);
        if (!value)
            continue;

        Value key_val = NEW_OBJ(add_obj(vm, new_pistring(strdup(key))));
        map_set(exports, key_val, *value);
    }
    ht_free(defined_globals);
    module->state = MODULE_LOADED;

    // Preserve compiled module data for later function calls and .px caching.
    module->code = comp->code;
    module->constants = comp->constants;
    module->names = comp->names;
    module->instrs = comp->instrs;
    
    comp->code = NULL;
    comp->constants = NULL;
    comp->names = NULL;
    comp->instrs = NULL;

    if (parser)
        free_parser(parser);
    free_compiler(comp);

    // Preserve the module VM global table for later calls to functions defined in this module.
    module->globals = module_vm->globals;
    module_vm->globals = NULL;

    // Detach shared module cache so free_vm(module_vm) doesn't free parent cache.
    module_vm->modules = NULL;
    module_vm->project_build = NULL;
    free_vm(module_vm);

    free(source);
    free(resolved);

    return module_val;
}
