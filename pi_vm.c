#include <math.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "pi_vm.h"

#include "pi_opcode.h"
#include "pi_value.h"
#include "pi_module.h"

#include "pi_string.h"
#include "common.h"
#include "pi_func.h"
#include "gc.h"

#include "builtin/pi_builtin.h"
#include "builtin/pi_methods.h"

#define GC_MIN_THRESHOLD 4096
#define GC_MAX_THRESHOLD (1024 * 1024 * 8)
#define BROWSER_YIELD_STEPS 50000

static PiMap *create_objectProto(vm_t *vm);
static Object *construct(vm_t *vm, PiMap *map, size_t argc, Value *argv, Value kw_args);
static Value bind(vm_t *vm, Function *function, Object *instance);
static Value bind_nativeMethod(Object *instance, NativeMethod *method);

/**
 * Return a newly allocated string containing only the directory portion
 * of a filesystem path.
 *
 * Examples:
 *   "/home/user/file.pi" -> "/home/user"
 *   "C:\\temp\\file.pi"  -> "C:\\temp"
 *   "file.pi"            -> "."
 *   NULL                 -> "."
 *
 * The caller owns the returned string and must free it.
 */
static char *copy_dirName(const char *path)
{
    /* Invalid or empty path -> current directory. */
    if (!path || path[0] == '\0')
        return strdup(".");

    /* Work on a writable copy. */
    char *dir = strdup(path);
    int len = (int)strlen(dir);

    /*
     * Walk backwards until we find a directory separator.
     * Supports both Unix ('/') and Windows ('\\') paths.
     */
    while (len > 0 && dir[len - 1] != '/' && dir[len - 1] != '\\')
        len--;

    /*
     * No separator found, therefore the path contains only a file name.
     * Return "." to represent the current directory.
     */
    if (len == 0)
    {
        free(dir);
        return strdup(".");
    }

    /*
     * Replace the separator with a null terminator,
     * leaving only the directory portion.
     */
    dir[len - 1] = '\0';
    return dir;
}

/**
 * Return the module currently executing in the VM.
 *
 * The runtime stores the active module in the global variable "module".
 * If no valid module is available, NULL is returned.
 */
static ObjModule *vm_currentModule(vm_t *vm)
{
    /* VM or global namespace unavailable. */
    if (!vm || !vm->globals)
        return NULL;

    /* Retrieve global "module" binding. */
    Value *module_val = ht_get(vm->globals, "module");

    /* Ensure the binding exists and contains a module object. */
    if (!module_val || !IS_MODULE(*module_val))
        return NULL;

    return AS_MODULE(*module_val);
}

/**
 * Recalculate whether every element in the list is numeric.
 *
 * Some list operations maintain an optimization flag
 * (list->is_numeric) that allows fast numeric operations.
 * Since mutations may invalidate the flag, we rescan the list.
 */
static void list_refreshNumericFlag(PiList *list)
{
    bool numeric = true;

    for (int i = 0; i < LIST_SIZE(list->items); i++)
    {
        /* Any non-number disables the optimization. */
        if (!IS_NUM(*(Value *)list_getAt(list->items, i)))
        {
            numeric = false;
            break;
        }
    }

    list->is_numeric = numeric;
}

/**
 * Assign values into a list slice.
 *
 * Supports:
 *   list[a:b]     = sequence
 *   list[a:b:c]   = sequence
 *
 * Rules:
 *   - RHS must be a list or tuple.
 *   - Step 1 slices may grow or shrink the target list.
 *   - Extended slices (step != 1) require matching lengths.
 *
 * The implementation snapshots the source sequence first so
 * self-assignment scenarios remain safe.
 */
static void list_setSlice(vm_t *vm, PiList *target, PiSlice *slice, Value value)
{
    if (!IS_LIST(value) && !IS_TUPLE(value))
        vm_error(vm, "List slice assignment requires a list or tuple.");

    list_t *items = target->items;
    list_t *source = IS_LIST(value)
                         ? AS_LIST(value)->items
                         : AS_TUPLE(value)->items;

    int size = LIST_SIZE(items);
    int step = (int)slice->step;

    if (step == 0)
        vm_error(vm, "Slice step cannot be zero.");

    /*
     * Convert slice boundaries into concrete list indices.
     * Infinite start/stop values represent omitted bounds.
     */
    int start;
    int end;

    if (isinf(slice->start))
        start = step > 0 ? 0 : size - 1;
    else
        start = slice_index((int)slice->start, size, step);

    if (isinf(slice->stop))
        end = step > 0 ? size : -1;
    else
        end = slice_index((int)slice->stop, size, step);

    /*
     * Copy source values into a temporary buffer.
     * This prevents corruption if source and target refer
     * to the same underlying list.
     */
    int source_count = LIST_SIZE(source);
    Value *snapshot = NULL;

    if (source_count > 0)
    {
        snapshot = malloc(sizeof(Value) * source_count);

        if (!snapshot)
            vm_error(vm, "Memory allocation failed during slice assignment.");

        for (int i = 0; i < source_count; i++)
            snapshot[i] = *(Value *)list_getAt(source, i);
    }

    /*
     * Simple slice assignment:
     *
     *     list[a:b] = values
     *
     * Elements may be inserted or removed, changing the
     * overall length of the list.
     */
    if (step == 1)
    {
        int delete_count = end > start ? end - start : 0;
        int new_size = size - delete_count + source_count;

        /* Ensure enough capacity before moving memory. */
        if (new_size > items->capacity)
        {
            int new_capacity = items->capacity;

            while (new_capacity < new_size)
            {
                new_capacity = new_capacity < 1024
                                   ? new_capacity * 2
                                   : new_capacity + new_capacity / 4 + 256;
            }

            list_expand(items, new_capacity);
        }

        /*
         * Shift the tail portion of the list to create
         * or remove space for replacement values.
         */
        void *insert_at = (byte *)items->data + start * items->i_size;
        void *tail_from =
            (byte *)items->data + (start + delete_count) * items->i_size;
        void *tail_to =
            (byte *)items->data + (start + source_count) * items->i_size;

        memmove(
            tail_to,
            tail_from,
            (size - start - delete_count) * items->i_size);

        /* Copy replacement values into the gap. */
        if (source_count > 0)
            memcpy(insert_at, snapshot, source_count * items->i_size);

        items->size = new_size;
    }
    else
    {
        /*
         * Extended slice assignment:
         *
         *     list[a:b:c] = values
         *
         * Length must match exactly because positions are fixed.
         */
        int target_count = 0;

        if (step > 0)
        {
            for (int current = start; current < end; current += step)
                target_count++;
        }
        else
        {
            for (int current = start; current > end; current += step)
                target_count++;
        }

        if (target_count != source_count)
        {
            free(snapshot);
            vm_error(vm,
                     "Extended slice assignment requires matching lengths.");
        }

        /* Replace each selected element individually. */
        int current = start;

        for (int i = 0; i < source_count; i++)
        {
            list_set(items, current, &snapshot[i]);
            current += step;
        }
    }

    free(snapshot);

    /* Recompute numeric optimization state. */
    list_refreshNumericFlag(target);
}

/**
 * Determine whether two sets contain exactly the same elements.
 *
 * Since hash tables store unique keys, equality is:
 *   - same number of entries
 *   - every key in left exists in right
 */
static bool set_equals(PiSet *left, PiSet *right)
{
    if (set_size(left) != set_size(right))
        return false;

    for (int i = 0; i < set_size(left); i++)
    {
        Value value = set_get(left, i);
        if (!set_has(right, value))
            return false;
    }

    return true;
}

/**
 * Return true if every element in 'left' exists in 'right'.
 *
 * Implements mathematical subset semantics:
 *     left ⊆ right
 */
static bool set_isSubset(PiSet *left, PiSet *right)
{
    for (int i = 0; i < set_size(left); i++)
    {
        Value value = set_get(left, i);
        if (!set_has(right, value))
            return false;
    }

    return true;
}

/**
 * Perform a binary set operation and return a new set.
 *
 * Supported operations:
 *   8  -> intersection
 *   9  -> union
 *   10 -> symmetric difference
 *
 * The original sets are never modified.
 */
static Object *set_ops(vm_t *vm, PiSet *left, PiSet *right, int op)
{
    PiSet *result = (PiSet *)new_set();

    if (op == 9) /* union */
    {
        /*
         * Copy all elements from left, then add any
         * elements from right not already present.
         */
        for (int i = 0; i < set_size(left); i++)
            set_add(result, set_get(left, i));

        for (int i = 0; i < set_size(right); i++)
            set_add(result, set_get(right, i));
    }
    else if (op == 8) /* intersection */
    {
        /*
         * Keep only elements that exist in both sets.
         */
        for (int i = 0; i < set_size(left); i++)
        {
            Value value = set_get(left, i);
            if (set_has(right, value))
                set_add(result, value);
        }
    }
    else if (op == 10) /* symmetric difference */
    {
        /*
         * Elements present in exactly one set.
         */
        for (int i = 0; i < set_size(left); i++)
            set_add(result, set_get(left, i));

        for (int i = 0; i < set_size(right); i++)
        {
            Value value = set_get(right, i);
            if (set_has(result, value))
                set_remove(result, value);
            else
                set_add(result, value);
        }
    }

    return (Object *)result;
}

/**
 * Return a new set containing:
 *
 *     left - right
 *
 * All elements present in left but absent from right.
 */
static Object *set_difference(vm_t *vm, PiSet *left, PiSet *right)
{
    PiSet *result = (PiSet *)new_set();
    for (int i = 0; i < set_size(left); i++)
    {
        Value value = set_get(left, i);
        if (!set_has(right, value))
            set_add(result, value);
    }

    return (Object *)result;
}

/**
 * Return a human-readable label for the currently executing module.
 *
 * Preference order:
 *   1. Full module path
 *   2. Module name
 *   3. NULL if unavailable
 *
 * Useful for diagnostics and stack traces.
 */
static const char *vm_moduleLabel(vm_t *vm)
{
    ObjModule *module = vm_currentModule(vm);

    if (!module)
        return NULL;

    if (module->path && module->path[0] != '\0')
        return module->path;

    if (module->name && module->name[0] != '\0')
        return module->name;

    return NULL;
}

/**
 * Locate the instruction metadata corresponding to the VM's
 * current program counter.
 *
 * Instruction records are stored per scope/function and
 * contain source-level information (offset, line number, etc.).
 *
 * The algorithm returns the last instruction whose bytecode
 * offset does not exceed the current PC.
 */
static instr_t *vm_instrForOffset(vm_t *vm, int target_offset)
{
    if (!vm || !vm->instrs)
        return NULL;

    /*
     * Determine the active scope name.
     * Global code is stored under "<global>".
     */
    char *scope_name = "<global>";

    if (vm->function && IS_FUN(NEW_OBJ(vm->function)))
    {
        Function *fn = (Function *)vm->function;

        if (fn->name && fn->name[0] != '\0')
            scope_name = fn->name;
    }

    /*
     * Retrieve instruction metadata for the active scope.
     * Fallback to global scope if necessary.
     */
    list_t *instrs = ht_get(vm->instrs, scope_name);

    if (!instrs && strcmp(scope_name, "<global>") != 0)
        instrs = ht_get(vm->instrs, "<global>");

    if (!instrs)
        return NULL;

    int size = list_size(instrs);
    instr_t *instr = NULL;

    /*
     * Find the instruction whose offset is the closest
     * one less than or equal to the current program counter.
     */
    for (int i = 0; i < size; i++)
    {
        instr_t *cur = (instr_t *)list_getAt(instrs, i);

        if (cur->offset > target_offset)
            break;

        instr = cur;
    }

    return instr;
}

static instr_t *vm_currentInstr(vm_t *vm)
{
    if (!vm)
        return NULL;

    if (vm->current_instr)
        return vm->current_instr;

    return vm_instrForOffset(vm, vm->pc);
}

/**
 * Initializes the virtual machine by allocating memory and
 * setting initial values for the program counter, stack pointer,
 * base pointer, and other components.
 */
vm_t *init_vm(compiler_t *comp, const char *entry_name, bool is_main)
{

    // Allocate memory for the virtual machine instance
    vm_t *vm = (vm_t *)malloc(sizeof(vm_t));

    // Initialize program counter, stack pointer, and base pointer to 0
    vm->pc = 0;
    vm->sp = 0;
    vm->bp = 0;
    vm->ip = 0;

    // Set the code, constants, and names from the compiler to the VM
    vm->code = comp->code;
    vm->constants = comp->constants;
    vm->names = comp->names;
    vm->instrs = comp->instrs;
    vm->current_instr = NULL;

    // Create a hash table to store global variables
    vm->globals = ht_create(sizeof(Value));

    vm->objects = NULL;

    for (int i = 0; i < BUILTIN_CONST_COUNT; i++)
        ht_put(vm->globals, builtin_constants[i].name, &builtin_constants[i].value);

    for (int i = 0; i < BUILTIN_FUNC_COUNT; i++)
        ht_put(vm->globals, builtin_functions[i].name,
               new_native(builtin_functions[i].name, builtin_functions[i].func));

    vm->iter_sp = -1;
    vm->comp_sp = 0;
    vm->frame_sp = 0;

    vm->running = true;

    vm->fps = TARGET_FPS;

    pthread_mutex_init(&vm->lock, NULL);

    mark_constants(vm);

    vm->counter = 0;

    vm->openUpvalues = NULL;

    vm->function = NULL;
    vm->_kw_args = NEW_NIL();

    // Initialize the garbage collector
    vm->next_gc = NEXT_GC;
    vm->obj_count = 0;

    // Initialize the GC stack to NULL (it will be allocated when needed)
    vm->gc_stack = NULL;

    // Initialize the modules table to store loaded modules by name
    vm->modules = ht_create(sizeof(Value));
    // Set current path to the working directory at VM initialization
    vm->current_path = getcwd(NULL, 0);
    vm->object_proto = NULL;

    if (entry_name && entry_name[0] != '\0')
    {
        char *entry_dir = copy_dirName(entry_name);
        if (entry_dir)
        {
            free(vm->current_path);
            vm->current_path = entry_dir;
        }
    }

    // Expose current module context in every VM as `module`.
    const char *module_name = (entry_name && entry_name[0] != '\0') ? entry_name : "<main>";
    const char *module_path = (entry_name && entry_name[0] != '\0')
                                  ? entry_name
                                  : (vm->current_path ? vm->current_path : "");
    Object *main_moduleObj = new_module(
        vm,
        module_name,
        module_path,
        false,
        is_main);

    // Mark main module as loaded to prevent issues with circular imports in the main file.
    ObjModule *main_module = (ObjModule *)main_moduleObj;
    main_module->state = MODULE_LOADED;

    // Add main module to the global modules table so it can be referenced by name.
    Value main_moduleVal = NEW_OBJ(main_moduleObj);
    ht_put(vm->globals, "module", &main_moduleVal);

    vm->object_proto = create_objectProto(vm);
    Value object_protoVal = NEW_OBJ((Object *)vm->object_proto);
    ht_put(vm->globals, "Object", &object_protoVal);

    return vm;
}

/**
 * Reuse an existing VM for new bytecode while preserving globals.
 */
void vm_reset(vm_t *vm, compiler_t *comp)
{
    vm->pc = 0;
    vm->sp = 0;
    vm->bp = 0;
    vm->ip = 0;

    vm->code = comp->code;
    vm->constants = comp->constants;
    vm->names = comp->names;
    vm->instrs = comp->instrs;
    vm->current_instr = NULL;

    /* Preserve vm->globals so shell state survives resets. */

    vm->iter_sp = -1;
    vm->comp_sp = 0;
    vm->frame_sp = 0;

    vm->running = true;

    // Reset GC stats to trigger collection sooner if needed
    vm->counter = 0;
    vm->next_gc = NEXT_GC;

    vm->openUpvalues = NULL;
    vm->function = NULL;
    vm->_kw_args = NEW_NIL();

    // Mark new constants from the new compiler for GC
    mark_constants(vm);
}

/**
 * Add a newly created object to the VM's GC tracking list.
 */
inline Object *add_obj(vm_t *vm, Object *obj)
{
    if (obj->in_gcList)
        return obj;

    obj->in_gcList = true;
    obj->gc_color = GC_WHITE;

    obj->next = vm->objects;
    vm->objects = obj;
    vm->counter++;
    vm->obj_count++;

    return obj;
}

Value vm_kwargs(vm_t *vm)
{
    if (!vm)
        return NEW_NIL();
    return vm->_kw_args;
}

bool vm_hasKwarg(vm_t *vm, const char *name)
{
    if (!vm || !name || !IS_MAP(vm->_kw_args))
        return false;
    return ht_get(AS_MAP(vm->_kw_args)->table, name) != NULL;
}

bool vm_getKwarg(vm_t *vm, const char *name, Value *out)
{
    if (!vm || !name || !out || !IS_MAP(vm->_kw_args))
        return false;

    Value *value = ht_get(AS_MAP(vm->_kw_args)->table, name);
    if (!value)
        return false;

    *out = *value;
    return true;
}

Value vm_getKwargOr(vm_t *vm, const char *name, Value fallback)
{
    Value value;
    return vm_getKwarg(vm, name, &value) ? value : fallback;
}

/**
 * Counts the number of objects in the virtual machine's object list.
 *
 * This function iterates over the linked list of objects and returns the
 * total count of objects in the list. It is used for debugging purposes
 * to track the number of objects in use.
 *
 * @param vm The virtual machine instance.
 * @return The number of objects in the object list.
 */
#ifdef DEBUG
static inline int count_objs(vm_t *vm)
{
    int count = 0;
    Object *obj = vm->objects;
    while (obj)
    {
#ifdef DEBUG
        // Print debugging information about the object
        printf("[DEBUG] Counting object at %p\n", (void *)obj);
#endif
        count++;
        obj = obj->next;
    }
    return count;
}
#endif

/**
 * Report a runtime error and terminate execution.
 */

void vm_error(vm_t *vm, const char *message)
{
    instr_t *instr = vm_currentInstr(vm);
    const char *module_label = vm_moduleLabel(vm);

    if (global_errorHandler)
    {
        char buffer[1024];
        if (module_label && instr && instr->fun_name)
            snprintf(buffer, sizeof(buffer), "%s (module '%s', function '%s')", message, module_label, instr->fun_name);
        else if (module_label)
            snprintf(buffer, sizeof(buffer), "%s (module '%s')", message, module_label);
        else if (instr && instr->fun_name)
            snprintf(buffer, sizeof(buffer), "%s (in function '%s')", message, instr->fun_name);
        else
            snprintf(buffer, sizeof(buffer), "%s", message);

        global_errorHandler(buffer, instr ? instr->line : -1, instr ? instr->column : 0);
        return;
    }

    if (instr)
    {
        fprintf(stderr, "\n\033[1;31m[RUNTIME ERROR]");
        if (module_label)
            fprintf(stderr, " in %s", module_label);
        fprintf(stderr, " at line %d, column %d", instr->line, instr->column);
        if (instr->fun_name)
            fprintf(stderr, " in function '%s'", instr->fun_name);
        fprintf(stderr, ":\033[0m %s\n\n", message);
    }
    else
    {
        fprintf(stderr, "\n\033[1;31m[RUNTIME ERROR]");
        if (module_label)
            fprintf(stderr, " in %s", module_label);
        fprintf(stderr, " at unknown location:\033[0m %s\n\n", message);
    }

    exit(EXIT_FAILURE);
    return; // Unreachable, but added to satisfy non-void return type
}

/**
 * Format and report a runtime error.
 */
#include <stdarg.h>

void vm_errorf(vm_t *vm, const char *fmt, ...)
{
    instr_t *instr = vm_currentInstr(vm);
    const char *module_label = vm_moduleLabel(vm);

    // Format the message first
    char message[1024];

    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (global_errorHandler)
    {
        char buffer[1024];

        if (module_label && instr && instr->fun_name)
            snprintf(buffer, sizeof(buffer), "%s (module '%s', function '%s')", message, module_label, instr->fun_name);
        else if (module_label)
            snprintf(buffer, sizeof(buffer), "%s (module '%s')", message, module_label);
        else if (instr && instr->fun_name)
            snprintf(buffer, sizeof(buffer), "%s (in function '%s')", message, instr->fun_name);
        else
            snprintf(buffer, sizeof(buffer), "%s", message);

        global_errorHandler(buffer, instr ? instr->line : -1, instr ? instr->column : 0);
        return;
    }

    if (instr)
    {
        fprintf(stderr, "\n\033[1;31m[RUNTIME ERROR]");
        if (module_label)
            fprintf(stderr, " in %s", module_label);
        fprintf(stderr, " at line %d, column %d", instr->line, instr->column);
        if (instr->fun_name)
            fprintf(stderr, " in function '%s'", instr->fun_name);
        fprintf(stderr, ":\033[0m %s\n\n", message);
    }
    else
    {
        fprintf(stderr, "\n\033[1;31m[RUNTIME ERROR]");
        if (module_label)
            fprintf(stderr, " in %s", module_label);
        fprintf(stderr, " at unknown location:\033[0m %s\n\n", message);
    }

    exit(EXIT_FAILURE);
}

/**
 * Pop the top value from the VM stack.
 */
static inline Value pop_stack(vm_t *vm)
{
    if (vm->sp <= 0)
        vm_error(vm, "Stack underflow: Attempted to pop from an empty stack");

    return vm->stack[--vm->sp];
}

/**
 * Push a value onto the VM stack.
 */
static inline void push_stack(vm_t *vm, Value value)
{
    if (vm->sp >= STACK_MAX)
        vm_error(vm, "[stack] Stack overflow: Attempted to push onto a full stack");

    vm->stack[vm->sp++] = value;
}

/**
 * Return the top VM stack value without popping it.
 */
static inline Value peek_stack(vm_t *vm)
{
    if (vm->sp <= 0)
        vm_error(vm, "Stack underflow: Attempted to peek at an empty stack");

    return vm->stack[vm->sp - 1];
}

static inline int resolve_localSlot(vm_t *vm, int local)
{
    if (vm->comp_sp > 0)
    {
        int top = vm->comp_sp - 1;
        if (vm->bp == vm->comp_bps[top] && local >= vm->comp_local_bases[top])
            return vm->comp_bases[top] + (local - vm->comp_local_bases[top]);
    }

    return vm->bp + local;
}

static Value bind_nativeMethod(Object *instance, NativeMethod *method)
{
    Value native = *new_native(method->name, method->func);
    Function *bound = AS_FUN(native);
    bound->instance = instance;
    bound->is_method = true;
    return native;
}

/**
 * True if no values exist above the current base pointer.
 */
static bool stack_isEmpty(vm_t *vm)
{
    return vm->sp == vm->bp;
}

/**
 * Refresh list metadata flags for numeric/matrix optimizations.
 */
static void refresh_listMeta(PiList *plist)
{
    int size = LIST_SIZE(plist->items);

    if (size == 0)
    {
        plist->is_numeric = true;
        plist->is_matrix = false;
        plist->rows = 0;
        plist->cols = 0;
        return;
    }

    bool is_numeric = true;
    for (int i = 0; i < size; i++)
    {
        Value value = *(Value *)list_getAt(plist->items, i);
        if (!IS_NUM(value))
        {
            is_numeric = false;
            break;
        }
    }

    if (is_numeric)
    {
        plist->is_numeric = true;
        plist->is_matrix = false;
        plist->rows = 1;
        plist->cols = size;
        return;
    }

    bool is_matrix = true;
    int cols = -1;
    for (int i = 0; i < size; i++)
    {
        Value value = *(Value *)list_getAt(plist->items, i);
        if (!IS_LIST(value))
        {
            is_matrix = false;
            break;
        }

        PiList *row = AS_LIST(value);
        if (!row->is_numeric)
        {
            is_matrix = false;
            break;
        }

        if (cols == -1)
            cols = row->items->size;
        else if (row->items->size != cols)
        {
            is_matrix = false;
            break;
        }
    }

    plist->is_numeric = false;
    plist->is_matrix = is_matrix;
    plist->rows = is_matrix ? size : -1;
    plist->cols = is_matrix ? cols : -1;
}

/**
 * Append all values from an iterable into the target list.
 */
static void list_extendFromIterable(vm_t *vm, PiList *plist, Value iterable)
{
    if (!IS_OBJ(iterable) || !is_iterable(AS_OBJ(iterable)))
        vm_error(vm, "Spread expects an iterable value.");

    Object *iter = AS_OBJ(iterable);
    iter_reset(iter);

    while (iter_hasNext(iter))
    {
        Value value = iter_next(iter);
        if (IS_OBJ(value))
            add_obj(vm, AS_OBJ(value));
        list_add(plist->items, &value);
    }
}

/**
 * Mark function values found in a map literal as methods and attach the map prototype.
 */
static void finalize_mapLiteral(vm_t *vm, PiMap *map)
{
    bool has_methods = false;
    char **keys = ht_keys(map->table);
    int size = ht_length(map->table);

    for (int i = 0; i < size; i++)
    {
        Value *item = ht_get(map->table, keys[i]);

        if (!item || !IS_FUN(*item))
            continue;

        Function *fn = AS_FUN(*item);

        fn->is_method = true;
        fn->owner = (Object *)map;

        if (strcmp(keys[i], "compute") == 0)
            map->has_compute = true;
        else if (strcmp(keys[i], "rcompute") == 0)
            map->has_rcompute = true;

        has_methods = true;
    }

    if (map->proto == NULL && has_methods)
        map->proto = vm->object_proto;
}

/**
 * Copy entries from source map into target map, preserving object references.
 */
static void map_extendFromMap(vm_t *vm, PiMap *target, Value source)
{
    if (!IS_MAP(source))
        vm_error(vm, "Map spread expects a map value.");

    PiMap *map = AS_MAP(source);
    char **keys = ht_keys(map->table);
    int size = ht_length(map->table);

    for (int i = 0; i < size; i++)
    {
        Value *item = ht_get(map->table, keys[i]);
        if (item == NULL)
            continue;

        if (IS_OBJ(*item))
            add_obj(vm, AS_OBJ(*item));

        ht_put(target->table, keys[i], item);
        if (IS_FUN(*item))
        {
            if (strcmp(keys[i], "compute") == 0)
                target->has_compute = true;
            else if (strcmp(keys[i], "rcompute") == 0)
                target->has_rcompute = true;
        }
    }
}

/**
 * Attempt an object instance call via its `call` method.
 */

static bool object_instanceCall(vm_t *vm, PiMap *map, size_t argc, Value *args, Value kw_args, Value *result)
{
    Value call_method = map_getValueByKey(map, "call");
    if (!IS_FUN(call_method))
        return false;

    Value bound = bind(vm, AS_FUN(call_method), (Object *)map);
    *result = call_func(vm, AS_FUN(bound), argc, args, kw_args);
    if (IS_OBJ(*result))
        add_obj(vm, AS_OBJ(*result));
    return true;
}

static Value call_withArgList(vm_t *vm, Value callee, PiList *arg_list, Value kw_args, bool has_named)
{
    int num_args = arg_list->items->size;

    // Heap-allocate instead of VLA to avoid silent stack overflow on large arg lists
    Value *args = num_args > 0 ? (Value *)malloc(num_args * sizeof(Value)) : NULL;
    if (num_args > 0 && !args)
        vm_error(vm, "Memory allocation failed for argument list.");

    for (int i = 0; i < num_args; i++)
        args[i] = *(Value *)list_getAt(arg_list->items, i);

    Value result;

    if (IS_FUN(callee))
    {
        result = call_func(vm, AS_FUN(callee), num_args, args, kw_args);
        if (IS_OBJ(result))
            add_obj(vm, AS_OBJ(result));
    }
    else if (IS_MAP(callee))
    {
        PiMap *map = AS_MAP(callee);
        if (map->is_instance)
        {
            if (object_instanceCall(vm, map, num_args, args, kw_args, &result))
            {
                free(args);
                return result;
            }
            free(args);
            vm_error(vm, "Attempt to call an Object instance.");
        }

        Value constructor = map_getValueByKey(map, "constructor");
        if (IS_FUN(constructor))
        {
            result = NEW_OBJ(add_obj(vm, construct(vm, map, num_args, args, kw_args)));
        }
        else
        {
            result = NEW_OBJ(add_obj(vm, construct(vm, map, num_args, args, kw_args)));
        }
    }
    else
    {
        free(args);
        vm_error(vm, "Attempt to call a non-function object.");
        result = NEW_NIL(); // unreachable, but keeps compiler happy
    }

    free(args);
    return result;
}

/**
 * Push a VM frame onto the call stack.
 */
void push_frame(vm_t *vm, Frame *frame)
{
    if (vm->frame_sp >= STACK_MAX)
        vm_error(vm, "[frame] Stack overflow: Attempted to push onto a full stack");

    vm->frames[vm->frame_sp++] = *frame;
}

/**
 * Pop a frame from the call stack.
 */
Frame *pop_frame(vm_t *vm)
{
    if (vm->frame_sp <= 0)
        vm_error(vm, "Stack underflow: Attempted to pop from an empty stack");

    return &vm->frames[--vm->frame_sp];
}

/**
 * Reads a name from the list of names stored in the virtual machine.
 *
 * @param index The index of the name to read from the list of names.
 * @return A C string containing the name at the specified index.
 */
static inline char *read_name(vm_t *vm, int index)
{
    return string_get(vm->names, index);
}

/**
 * Checks if the given value is considered false.
 *
 * This function is used to compare a value to a boolean false value.
 * It checks if the value is NULL or if the type of the value is NIL.
 *
 * @param value The value to check.
 * @return true if the value is false, false otherwise.
 */
static inline bool is_false(vm_t *vm, Value value)
{
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static inline int read_short(vm_t *vm)
{
    uint8_t *code = (uint8_t *)vm->code->data; // Access the bytecode from the VM's code
    int high = code[vm->pc++] & 0xFF;          // Get the high byte and mask it
    int low = code[vm->pc++] & 0xFF;           // Get the low byte and mask it

    return (high << 8) | low; // Combine high and low bytes into a 16-bit short
}

static inline int _read_short(uint8_t *code, int pc)
{
    int high = code[pc] & 0xFF;    // Get the high byte and mask it
    int low = code[pc + 1] & 0xFF; // Get the low byte and mask it
    return (high << 8) | low;      // Combine high and low bytes into a 16-bit short
}

/**
 * Scan function bytecode for local access of a specific argument slot.
 */
static bool fun_scanSlot(ObjCode *body, uint8_t args_slot)
{
    uint8_t *bytecode = (uint8_t *)body->data->data;
    int length = body->data->size;

    // Iterate through the bytecode of the function
    for (int i = 0; i < length;)
    {
        uint8_t op = bytecode[i++]; // Get the opcode at the current index

        // Check if the opcode is either OP_LOAD_LOCAL or OP_STORE_LOCAL
        if ((op == OP_LOAD_LOCAL || op == OP_STORE_LOCAL) &&
            // Check if the argument slot matches the given slot
            i < length && bytecode[i] == args_slot)
            return true;

        // Advance the index by the number of operands the instruction has
        i += operand_count(op);
    }

    return false;
}

/**
 * Capture or reuse an open upvalue for a stack slot.
 */
static UpValue *capture_upvalue(vm_t *vm, int index)
{
    // Iterate through the linked list of open upvalues until the upvalue with the
    // given index is found.
    UpValue *prev = NULL;
    UpValue *upvalue = vm->openUpvalues;
    while (upvalue != NULL && upvalue->index != index)
    {
        prev = upvalue;
        upvalue = upvalue->next;
    }

    // If the upvalue with the given index is found, return it.
    if (upvalue != NULL && upvalue->index == index)
        return upvalue;

    // Create a new upvalue if it does not exist.
    UpValue *_upvalue = (UpValue *)malloc(sizeof(UpValue));
    _upvalue->value = vm->stack[index]; // Reference stack value
    _upvalue->index = index;

    // Append the new upvalue to the linked list of open upvalues.
    _upvalue->next = upvalue;
    if (prev == NULL)
        vm->openUpvalues = _upvalue;
    else
        prev->next = _upvalue;

    return _upvalue;
}

/**
 * Removes an upvalue from the linked list of open upvalues in the VM.
 * This function iterates through the linked list of open upvalues and finds
 * the upvalue with the given index. It then removes the upvalue from the
 * list and updates the previous upvalue's next pointer if necessary.
 *
 * @param vm The virtual machine instance.
 * @param index The index of the upvalue to remove.
 */
static void remove_upvalue(vm_t *vm, int index)
{
    UpValue *prev = NULL;                // Previous upvalue in the list
    UpValue *upvalue = vm->openUpvalues; // Current upvalue in the list

    // Iterate through the list of open upvalues until the upvalue with the
    // given index is found.
    while (upvalue != NULL && upvalue->index != index)
    {
        prev = upvalue;
        upvalue = upvalue->next;
    }

    // If the upvalue with the given index is found, remove it from the list
    if (upvalue != NULL && upvalue->index == index)
    {
        // Mark the upvalue as removed by setting its index to -1
        upvalue->index = -1;

        // Set the upvalue's value to the value at the given index in the stack
        upvalue->value = vm->stack[index];

        // If the upvalue is at the beginning of the list, update the VM's openUpvalues
        // pointer to point to the next upvalue in the list.
        if (prev == NULL)
            vm->openUpvalues = upvalue->next;
        else
            // Otherwise, update the previous upvalue's next pointer to point to the next
            // upvalue in the list, effectively removing the upvalue from the list.
            prev->next = upvalue->next;
    }
}

/**
 * Bind a function to an instance. The function is returned as a new function
 * with the first argument set to the instance.
 *
 * @param function The function to bind.
 * @param instance The instance to bind to.
 * @return A new function bound to the given instance.
 */
static Value bind(vm_t *vm, Function *function, Object *instance)
{
    if (function->is_native)
    {
        Value *native = new_native(function->name, function->native);
        Function *bound = AS_FUN(*native);
        bound->instance = instance;
        bound->owner = function->owner;
        bound->is_method = true;
        return *native;
    }

    // Copy the function object to keep the original intact
    Object *fn = new_func(function->name, function->body,
                          function->params, NULL, instance);
    ((Function *)fn)->constants = function->constants;
    ((Function *)fn)->names = function->names;
    ((Function *)fn)->instrs = function->instrs;
    ((Function *)fn)->globals = function->globals;
    ((Function *)fn)->param_names = function->param_names;
    ((Function *)fn)->owns_params = false;
    ((Function *)fn)->upvalues = function->upvalues;
    ((Function *)fn)->upvalue_count = function->upvalue_count;
    ((Function *)fn)->need_args = function->need_args;
    ((Function *)fn)->need_kwargs = function->need_kwargs;
    ((Function *)fn)->owner = function->owner;

    // Set the is_method flag to true
    ((Function *)fn)->is_method = true;

    int param_count = list_size(function->params);
    ((Function *)fn)->need_args = fun_scanSlot(function->body, (uint8_t)(param_count + 1));
    ((Function *)fn)->need_kwargs = fun_scanSlot(function->body, (uint8_t)(param_count + 2));

    add_obj(vm, fn); // Critical - adds to GC tracking

    // Return the new function
    return NEW_OBJ(fn);
}

/**
 * Creates the object prototype map.
 *
 * @param vm The virtual machine instance.
 * @return The object prototype map.
 */
static PiMap *create_objectProto(vm_t *vm)
{
    Object *obj = add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    PiMap *proto = (PiMap *)obj;

    // Create built-in functions
    Value format = *new_native("format", pi_toString);

    Value hash = *new_native("hash", pi_hashCode);

    Value clone = *new_native("clone", pi_clone);

    Value extends_fn = *new_native("extends", pi_extends);

    Value equals_fn = *new_native("equals", pi_equals);
    Value ident_fn = *new_native("ident", pi_ident);
    Value compare_fn = *new_native("compare", pi_compare);

    Value type_fn = *new_native("type", pi_type);
    Value name_fn = *new_native("name", pi_name);
    Value set_name_fn = *new_native("setName", pi_setName);
    Value lock_fn = *new_native("lock", pi_lock);
    Value bracket_access_fn = *new_native("bracketAccess", pi_bracketAccess);

    Value get_fn = *new_native("get", pi_get);
    Value set_fn = *new_native("set", pi_set);
    Value has_fn = *new_native("has", pi_has);
    Value delete_fn = *new_native("delete", pi_delete);

    Value iterator_fn = *new_native("iterator", pi_iterator);
    Value next_fn = *new_native("next", pi_next);

    Value keys = *new_native("keys", pi_keys);
    Value values = *new_native("values", pi_values);

    // Add built-in functions to the object prototype map
    ht_put(proto->table, "format", &format);
    ht_put(proto->table, "hash", &hash);
    ht_put(proto->table, "clone", &clone);
    ht_put(proto->table, "extends", &extends_fn);
    ht_put(proto->table, "equals", &equals_fn);
    ht_put(proto->table, "ident", &ident_fn);
    ht_put(proto->table, "compare", &compare_fn);
    ht_put(proto->table, "type", &type_fn);
    ht_put(proto->table, "name", &name_fn);
    ht_put(proto->table, "setName", &set_name_fn);
    ht_put(proto->table, "lock", &lock_fn);
    ht_put(proto->table, "bracketAccess", &bracket_access_fn);
    ht_put(proto->table, "get", &get_fn);
    ht_put(proto->table, "set", &set_fn);
    ht_put(proto->table, "has", &has_fn);
    ht_put(proto->table, "delete", &delete_fn);
    ht_put(proto->table, "iterator", &iterator_fn);
    ht_put(proto->table, "next", &next_fn);
    ht_put(proto->table, "keys", &keys);
    ht_put(proto->table, "values", &values);

    return proto;
}

static PiMap *map_ownerByKey(PiMap *map, const char *key)
{
    while (map != NULL)
    {
        if (ht_get(map->table, key) != NULL)
            return map;

        map = map->proto;
    }

    return NULL;
}

/**
 * Calls a method on an object without any arguments.
 *
 * This function attempts to find the named method on the given object, and if
 * it exists, calls it with no arguments. If the object does not contain the
 * named method, or if the method does not return a primitive value, this
 * function returns the original object.
 *
 * @param vm The virtual machine instance.
 * @param receiver The object to call the method on.
 * @param name The name of the method to call.
 * @return The result of calling the method, or the original object if it cannot
 *         be called.
 */
static Value call_methodNoArgs(vm_t *vm, Value receiver, const char *name)
{
    if (!IS_MAP(receiver) || !AS_MAP(receiver)->is_instance)
        return receiver;

    PiMap *owner = map_ownerByKey(AS_MAP(receiver), name);
    if (owner == NULL)
        return receiver;

    Value *method_ptr = ht_get(owner->table, name);
    Value method = method_ptr ? *method_ptr : NEW_NIL();
    if (!IS_FUN(method))
        return receiver;

    if (AS_MAP(receiver)->is_instance)
    {
        Object *target = AS_MAP(receiver)->super_instance ? AS_MAP(receiver)->super_instance : AS_OBJ(receiver);
        method = bind(vm, AS_FUN(method), target);
    }

    return call_func(vm, AS_FUN(method), 0, NULL, NEW_NIL());
}

Value vm_callMethodNoArgs(vm_t *vm, Value receiver, const char *name)
{
    return call_methodNoArgs(vm, receiver, name);
}

/**
 * Attempts to call a method on an object with one argument.
 *
 * This function attempts to find the named method on the given object, and if
 * it exists, calls it with the given argument. If the object does not contain the
 * named method, or if the method does not return a primitive value, this
 * function returns false.
 *
 * @param vm The virtual machine instance.
 * @param receiver The object to call the method on.
 * @param name The name of the method to call.
 * @param arg The argument to pass to the method.
 * @param result If the method call is successful, the result of the method call.
 * @return true if the method call is successful, false otherwise.
 */
static bool try_callMethodOneArg(vm_t *vm, Value receiver, const char *name, Value arg, Value *result)
{
    if (!IS_MAP(receiver) || !AS_MAP(receiver)->is_instance)
        return false;

    PiMap *owner = map_ownerByKey(AS_MAP(receiver), name);
    if (owner == NULL)
        return false;

    Value *method_ptr = ht_get(owner->table, name);
    Value method = method_ptr ? *method_ptr : NEW_NIL();
    if (!IS_FUN(method))
        return false;

    if (AS_MAP(receiver)->is_instance)
    {
        Object *target = AS_MAP(receiver)->super_instance ? AS_MAP(receiver)->super_instance : AS_OBJ(receiver);
        method = bind(vm, AS_FUN(method), target);
    }

    Value args[1];
    args[0] = arg;

    *result = call_func(vm, AS_FUN(method), 1, args, NEW_NIL());
    if (IS_OBJ(*result))
        add_obj(vm, AS_OBJ(*result));

    return true;
}

/**
 * Attempts to call a method on an object with an arbitrary argument list.
 *
 * Used by syntax-level overrides where the number of arguments is fixed by the
 * operator, such as indexing and slicing. Only object instances participate so
 * plain map indexing keeps its direct key lookup behavior.
 */
static bool try_callMethodArgs(vm_t *vm, Value receiver, const char *name, int argc, Value *args, Value *result)
{
    if (!IS_MAP(receiver) || !AS_MAP(receiver)->is_instance)
        return false;

    PiMap *owner = map_ownerByKey(AS_MAP(receiver), name);
    if (owner == NULL)
        return false;

    Value *method_ptr = ht_get(owner->table, name);
    Value method = method_ptr ? *method_ptr : NEW_NIL();
    if (!IS_FUN(method))
        return false;

    Object *target = AS_MAP(receiver)->super_instance ? AS_MAP(receiver)->super_instance : AS_OBJ(receiver);
    method = bind(vm, AS_FUN(method), target);

    *result = call_func(vm, AS_FUN(method), argc, args, NEW_NIL());
    if (IS_OBJ(*result))
        add_obj(vm, AS_OBJ(*result));

    return true;
}

/**
 * Attempts to call the compute method on the given object.
 *
 * This function attempts to call the compute method on the given object. The compute
 * method takes two arguments: the first is an integer representing the operation to
 * perform, and the second is the other object to use in the operation. If the method
 * call is successful, the result of the method call is stored in result.
 *
 * @param vm The virtual machine instance.
 * @param receiver The object to call the method on.
 * @param op The operation to perform.
 * @param has_other true if the other object should be passed to the method, false otherwise.
 * @param other The other object to use in the operation.
 * @param result If the method call is successful, the result of the method call.
 * @return true if the method call is successful, false otherwise.
 */
static bool try_callComputeMethod(vm_t *vm, Value receiver, const char *name, int op, bool has_other, Value other, Value *result)
{
    if (!IS_MAP(receiver) || !AS_MAP(receiver)->is_instance)
        return false;

    PiMap *owner = map_ownerByKey(AS_MAP(receiver), name);
    if (owner == NULL)
        return false;

    Value *method_ptr = ht_get(owner->table, name);
    Value method = method_ptr ? *method_ptr : NEW_NIL();
    if (!IS_FUN(method))
        return false;

    if (AS_MAP(receiver)->is_instance)
    {
        Object *target = AS_MAP(receiver)->super_instance ? AS_MAP(receiver)->super_instance : AS_OBJ(receiver);
        method = bind(vm, AS_FUN(method), target);
    }

    Value args[2];
    args[0] = NEW_NUM(op);
    if (has_other)
        args[1] = other;

    *result = call_func(vm, AS_FUN(method), has_other ? 2 : 1, args, NEW_NIL());
    if (IS_OBJ(*result))
        add_obj(vm, AS_OBJ(*result));

    return true;
}

static bool try_callCompute(vm_t *vm, Value receiver, int op, bool has_other, Value other, Value *result)
{
    return try_callComputeMethod(vm, receiver, "compute", op, has_other, other, result);
}

static bool try_callReflectedCompute(vm_t *vm, Value receiver, int op, Value other, Value *result)
{
    return try_callComputeMethod(vm, receiver, "rcompute", op, true, other, result);
}

/**
 * Attempts to call the equals method on the given objects.
 *
 * This function attempts to call the equals method on the given objects. The equals
 * method takes one argument: the other object to compare to. If the method call
 * is successful, the result of the method call is used to determine if the
 * objects are equal.
 *
 * @param vm The virtual machine instance.
 * @param left The first object to compare.
 * @param right The second object to compare.
 * @param result If the method call is successful, this is set to true if the objects
 *         are equal, and false otherwise.
 * @return true if the method call is successful, false otherwise.
 */
static bool try_overloadedEquals(vm_t *vm, Value left, Value right, bool *result)
{
    Value method_result = NEW_NIL();
    if (try_callMethodOneArg(vm, left, "equals", right, &method_result))
    {
        *result = !is_false(vm, method_result);
        return true;
    }

    if (try_callMethodOneArg(vm, right, "equals", left, &method_result))
    {
        *result = !is_false(vm, method_result);
        return true;
    }

    return false;
}

/**
 * Attempts to call the compare method on the given objects.
 *
 * This function first attempts to call the object's "compare" method with the
 * given other object as an argument. If the method call is successful, the result
 * of the method call is used to determine the comparison result.
 *
 * If the first method call is not successful, this function then attempts to call
 * the other object's "compare" method with the given object as an argument. If the
 * second method call is successful, the result of the method call is used to
 * determine the comparison result, but with the sign flipped.
 *
 * If neither method call is successful, this function returns false.
 *
 * @param vm The virtual machine instance.
 * @param left The first object to compare.
 * @param right The second object to compare.
 * @param cmp If the method call is successful, this is set to a negative value if the
 *         first object is less than the second, zero if they are equal, and a positive
 *         value if the first object is greater than the second.
 * @return true if the method call is successful, false otherwise.
 */
static bool try_overloadedCompare(vm_t *vm, Value left, Value right, int *cmp)
{
    Value method_result = NEW_NIL();
    if (try_callMethodOneArg(vm, left, "compare", right, &method_result))
    {
        // The compare method must return a number.
        if (!is_numeric(method_result))
            vm_error(vm, "Object compare(other) must return a number.");
        double value = as_number(method_result);
        *cmp = (value > 0) - (value < 0);
        return true;
    }

    if (try_callMethodOneArg(vm, right, "compare", left, &method_result))
    {
        // The compare method must return a number.
        if (!is_numeric(method_result))
            vm_error(vm, "Object compare(other) must return a number.");
        double value = as_number(method_result);
        *cmp = -((value > 0) - (value < 0));
        return true;
    }

    return false;
}

/**
 * Attempts to coerce a given object into a primitive value.
 *
 * This function attempts to call the object's "format" method to coerce it
 * into a primitive value. If the object does not contain a format method,
 * or if the method does not return a primitive string, this function returns
 * the original object.
 *
 * @param vm The virtual machine instance.
 * @param value The object to coerce into a primitive value.
 * @param pref_string Unused; kept for API compatibility.
 * @return The coerced primitive value, or the original object if it cannot be coerced.
 */
static Value to_primitive(vm_t *vm, Value value, bool pref_string)
{
    if (!IS_MAP(value) || !AS_MAP(value)->is_instance)
        return value;

    (void)pref_string;
    Value result = call_methodNoArgs(vm, value, "format");
    if (result.type != VAL_OBJ || IS_STRING(result))
        return result;

    return value;
}

/**
 * Constructs a new object instance from a given prototype map.
 *
 * This function creates a new map instance, setting the original map as its
 * prototype and copying over its key-value pairs. If a key holds a function,
 * it is bound to the new instance. The constructor function is called if it exists.
 *
 * @param vm The virtual machine instance.
 * @param map The prototype map from which to construct the object.
 * @param argc The number of arguments provided for the constructor.
 * @param argv The arguments to pass to the constructor.
 * @return A new object instance.
 */
static Object *construct(vm_t *vm, PiMap *map, size_t argc, Value *argv, Value kw_args)
{

    // ensure that the object prototype is set if the object has no parent
    if (map != NULL && map->proto == NULL && vm->object_proto != NULL && map != vm->object_proto)
        map->proto = vm->object_proto;

    // Create a new table for the instance
    table_t *table = ht_create(sizeof(Value));

    // Create a new map instance and set its prototype.
    // Members stay on the prototype map by default; only `this.*`
    // assignments create instance-local state.
    Object *instance = new_map(table, true);

    ((PiMap *)instance)->proto = map;
    ((PiMap *)instance)->bracket_access = map->bracket_access;
    ((PiMap *)instance)->has_compute = map->has_compute;
    ((PiMap *)instance)->has_rcompute = map->has_rcompute;

    // Push the new instance onto the VM stack
    // vm->stack[vm->sp] = NEW_OBJ(instance);

    // Invoke the constructor if it exists
    Value constructor = map_getValueByKey(map, "constructor");

    if (IS_FUN(constructor))
    {
        Value bound = bind(vm, AS_FUN(constructor), instance);
        call_func(vm, AS_FUN(bound), argc, argv, kw_args);
    }

    return instance;
}

/**
 * Applies a binary operation to two doubles.
 *
 * This function applies a binary operation to two doubles and returns the result.
 * The operation is specified by the `op` parameter, which can take on the following values:
 *   - 0: add the two doubles together
 *   - 1: subtract the second double from the first double
 *   - 2: multiply the two doubles together
 *   - 3: divide the first double by the second double
 * Any other value of `op` will result in `NAN` being returned.
 *
 * @param op The operation to apply.
 * @param left The first double to operate on.
 * @param right The second double to operate on.
 * @return The result of applying the binary operation to the two doubles.
 */
static double tensor_applyBinary(int op, double left, double right)
{
    switch (op)
    {
    case 0:
        return left + right;
    case 1:
        return left - right;
    case 2:
        return left * right;
    case 3:
        return right == 0.0 ? INFINITY : left / right;
    default:
        return NAN;
    }
}

/**
 * Applies a binary operation to a scalar and a tensor.
 *
 * This function applies a binary operation to a scalar and a tensor and returns the result.
 * The operation is specified by the `op` parameter, which can take on the following values:
 *   - 0: add the scalar to each element of the tensor
 *   - 1: subtract the scalar from each element of the tensor
 *   - 2: multiply each element of the tensor by the scalar
 *   - 3: divide each element of the tensor by the scalar
 * Any other value of `op` will result in `NAN` being returned.
 *
 * @param vm The virtual machine to allocate memory on.
 * @param tensor The tensor to operate on.
 * @param scalar The scalar to operate on.
 * @param op The operation to apply.
 * @param scalar_left Whether the scalar is on the left side of the operation.
 * @return The result of applying the binary operation to the scalar and the tensor.
 */
static Value tensor_scalarBinary(vm_t *vm, PiTensor *tensor, double scalar, int op, bool scalar_left)
{
    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(tensor->ndim, tensor->shape, tensor->type));
    for (int i = 0; i < tensor->size; i++)
    {
        double cell = tensor_getFlat(tensor, i);
        double value = scalar_left ? tensor_applyBinary(op, scalar, cell)
                                   : tensor_applyBinary(op, cell, scalar);
        tensor_setFlat(result, i, value);
    }
    return NEW_OBJ(result);
}

static bool tensor_broadcastShape(PiTensor *left, PiTensor *right, int *ndim, int *shape)
{
    *ndim = left->ndim > right->ndim ? left->ndim : right->ndim;
    for (int i = 0; i < *ndim; i++)
    {
        int li = left->ndim - *ndim + i;
        int ri = right->ndim - *ndim + i;
        int ldim = li < 0 ? 1 : left->shape[li];
        int rdim = ri < 0 ? 1 : right->shape[ri];

        if (ldim != rdim && ldim != 1 && rdim != 1)
            return false;

        shape[i] = ldim > rdim ? ldim : rdim;
    }
    return true;
}

static int tensor_projectOffset(PiTensor *tensor, int result_ndim, int *result_indices)
{
    int offset = 0;
    int shift = result_ndim - tensor->ndim;
    for (int i = 0; i < tensor->ndim; i++)
    {
        int result_i = i + shift;
        int index = tensor->shape[i] == 1 ? 0 : result_indices[result_i];
        offset += index * tensor->strides[i];
    }
    return offset;
}

static Value tensor_broadcastBinary(vm_t *vm, PiTensor *left, PiTensor *right, int op)
{
    int shape[16];
    int ndim = 0;
    if (left->ndim > 16 || right->ndim > 16 || !tensor_broadcastShape(left, right, &ndim, shape))
        vm_error(vm, "Tensor broadcast dimension mismatch.");

    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(ndim, shape, TN_FLOAT64));
    int indices[16] = {0};

    for (int flat = 0; flat < result->size; flat++)
    {
        int remaining = flat;
        for (int dim = 0; dim < ndim; dim++)
        {
            indices[dim] = remaining / result->strides[dim];
            remaining %= result->strides[dim];
        }

        double l = tensor_getFlat(left, tensor_projectOffset(left, ndim, indices));
        double r = tensor_getFlat(right, tensor_projectOffset(right, ndim, indices));
        tensor_setFlat(result, flat, tensor_applyBinary(op, l, r));
    }

    return NEW_OBJ(result);
}

// Tensor slice specification
typedef struct TensorSliceSpec
{
    int start;
    int end;
    int step;
    int count;
} TensorSliceSpec;

/**
 * Returns a tensor slice specification from a given index and length.
 *
 * This function takes a length and an index and returns a tensor slice specification
 * that can be used to slice a 2D tensor. The index must be a number.
 *
 * @param vm The virtual machine to allocate memory on.
 * @param length The length of the tensor dimension.
 * @param index The index of the tensor dimension to slice at.
 * @return A tensor slice specification that can be used to slice a 2D tensor.
 */
static TensorSliceSpec tensor2d_indexSpec(vm_t *vm, int length, Value index)
{
    TensorSliceSpec spec;

    if (!IS_NUM(index))
        vm_error(vm, "Tensor index must be a number.");

    spec.start = get_index((int)as_number(index), length);
    spec.end = spec.start + 1;
    spec.step = 1;
    spec.count = 1;
    return spec;
}

/**
 * Returns a bound index for a tensor slice operation.
 *
 * This function takes a length, a value, and a sign and returns a bound index
 * that can be used to slice a tensor. The sign is used to determine whether the
 * bound should be ceilinged or floored.
 *
 * @param length The length of the tensor dimension.
 * @param value The value to bound.
 * @param sign The sign of the value. If the sign is positive, the bound is
 *        ceilinged. If the sign is negative, the bound is floored.
 * @return The bound index for the tensor slice operation.
 */
static int tensor_sliceBound(int length, double value, int sign)
{
    int bound = (int)value;

    if (bound < 0)
        bound += length;

    if (sign > 0)
    {
        if (bound < 0)
            return 0;
        if (bound > length)
            return length;
        return bound;
    }

    if (bound < -1)
        return -1;
    if (bound >= length)
        return length - 1;
    return bound;
}

/**
 * Returns a tensor slice specification from a given start, end, and step.
 *
 * This function takes a length, a start value, an end value, and a step value and
 * returns a tensor slice specification that can be used to slice a 2D tensor.
 *
 * The start and end values must be numbers, and the step value must be a non-zero
 * number. The sign of the step value determines whether the slice is taken from
 * the start to the end (positive step) or from the end to the start (negative step).
 *
 * If the start or end values are positive infinity, the slice is taken from the
 * start of the tensor. If the start or end values are negative infinity, the
 * slice is taken from the end of the tensor.
 *
 * @param vm The virtual machine to allocate memory on.
 * @param length The length of the tensor dimension.
 * @param start The starting index of the slice.
 * @param end The ending index of the slice.
 * @param step The step value of the slice.
 * @return A tensor slice specification that can be used to slice a 2D tensor.
 */
static TensorSliceSpec tensor2d_sliceSpec(vm_t *vm, int length, Value start, Value end, Value step)
{
    TensorSliceSpec spec;
    int sign;
    int current;

    if (!IS_NUM(start) || !IS_NUM(end))
        vm_error(vm, "Tensor slice bounds must be numbers.");

    if (!IS_NUM(step))
        vm_error(vm, "Tensor slice step must be a number.");

    spec.step = (int)as_number(step);
    if (spec.step == 0)
        vm_error(vm, "Tensor slice step cannot be zero.");

    sign = spec.step > 0 ? 1 : -1;
    spec.start = isinf(as_number(start)) ? (sign > 0 ? length : -1) : tensor_sliceBound(length, as_number(start), sign);
    spec.end = isinf(as_number(end)) ? (sign > 0 ? length : -1) : tensor_sliceBound(length, as_number(end), sign);
    spec.count = 0;

    for (current = spec.start; sign * (spec.end - current) > 0; current += spec.step)
        spec.count++;

    return spec;
}

/**
 * Retrieves a value from a 2D tensor at a given row and column index.
 *
 * If the row or column index is a slice, the function will return a new 2D tensor
 * containing the values from the slice of the original tensor.
 *
 * @param vm The virtual machine to allocate memory on.
 * @param tensor The tensor to retrieve the value from.
 * @param row_is_slice Whether the row index is a slice.
 * @param row_start The starting index of the row slice.
 * @param row_end The ending index of the row slice.
 * @param row_step The step value of the row slice.
 * @param row_index The row index of the value to retrieve.
 * @param col_is_slice Whether the column index is a slice.
 * @param col_start The starting index of the column slice.
 * @param col_end The ending index of the column slice.
 * @param col_step The step value of the column slice.
 * @param col_index The column index of the value to retrieve.
 * @return A value from the tensor at the given row and column index, or a new
 *         tensor containing the values from the slice of the original tensor.
 */
static Value tensor_get2d(vm_t *vm, PiTensor *tensor,
                          bool row_is_slice, Value row_start, Value row_end, Value row_step, Value row_index,
                          bool col_is_slice, Value col_start, Value col_end, Value col_step, Value col_index)
{
    if (tensor->ndim != 2)
        vm_error(vm, "Two-dimensional indexing requires a rank-2 tensor.");

    TensorSliceSpec row = row_is_slice
                              ? tensor2d_sliceSpec(vm, tensor->shape[0], row_start, row_end, row_step)
                              : tensor2d_indexSpec(vm, tensor->shape[0], row_index);
    TensorSliceSpec col = col_is_slice
                              ? tensor2d_sliceSpec(vm, tensor->shape[1], col_start, col_end, col_step)
                              : tensor2d_indexSpec(vm, tensor->shape[1], col_index);

    if (!row_is_slice && !col_is_slice)
    {
        int indices[2] = {row.start, col.start};
        return NEW_NUM(tensor_get(tensor, indices));
    }

    int shape[2] = {row.count, col.count};
    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(2, shape, tensor->type));
    int out_row = 0;

    for (int src_row = row.start; (row.step > 0 ? src_row < row.end : src_row > row.end); src_row += row.step)
    {
        int out_col = 0;
        for (int src_col = col.start; (col.step > 0 ? src_col < col.end : src_col > col.end); src_col += col.step)
        {
            int src[2] = {src_row, src_col};
            int dst[2] = {out_row, out_col};
            tensor_set(result, dst, tensor_get(tensor, src));
            out_col++;
        }
        out_row++;
    }

    return NEW_OBJ(result);
}

static void tensor_set2d(vm_t *vm, PiTensor *tensor, Value row_index,
                         Value col_index, Value value)
{
    if (tensor->ndim != 2)
        vm_error(vm, "Two-dimensional assignment requires a rank-2 tensor.");

    if (!IS_NUM(row_index) || !IS_NUM(col_index))
        vm_error(vm, "Tensor assignment indices must be numbers.");

    if (!is_numeric(value))
        vm_error(vm, "Tensor cell assignment requires a numeric value.");

    int indices[2] = {
        get_index((int)as_number(row_index), tensor->shape[0]),
        get_index((int)as_number(col_index), tensor->shape[1]),
    };
    tensor_set(tensor, indices, as_number(value));
}

/**
 * Checks if a given module name is private.
 *
 * Private module names are any module name that starts with an underscore
 * character and is not empty. This is used to prevent modules from being
 * imported by other modules.
 *
 * @param name The module name to check.
 * @return True if the module name is private, false otherwise.
 */
static bool is_private_moduleName(const char *name)
{
    return name != NULL && name[0] == '_' && name[1] != '\0';
}

/**
 * @brief Runs the virtual machine.
 *
 * @param vm The virtual machine to run.
 */
void run(vm_t *vm)
{
    int length = vm->code->size;
    int pc = vm->pc;
#ifdef __EMSCRIPTEN__
    int browser_steps = 0;
#endif

    uint8_t op;
    uint16_t index;
    int address;

    uint8_t *code = (uint8_t *)vm->code->data;

    Value value;

    Value nilValue;

    Object *iter = NULL;

    UpValue *upValue;

    Function *function = (Function *)vm->function;

    while (pc < length && vm->running)
    {
        vm->pc = pc;
        vm->current_instr = vm_instrForOffset(vm, pc);
        op = code[pc++];

        vm->ip++; // Advance instruction index

        // printf("OP: %d, PC: %d, IP: %d\n", op, pc, vm->ip);

        // Cast the opcode to the OpCode enum
        switch ((OpCode)op)
        {
        case OP_LOAD_CONST:
        {
            // Read a two-byte short value from the bytecode to get the constant index
            index = (code[pc++] << 8);
            index |= code[pc++];
            // Get the constant from the constants list using the index
            Value constant = *(Value *)list_getAt(vm->constants, index);

            // Push the constant onto the stack
            push_stack(vm, constant);

            break;
        }

        case OP_STORE_GLOBAL:
        {
            index = code[pc++];
            char *name = read_name(vm, index);

            Value _newValue = pop_stack(vm);
            ht_put(vm->globals, name, &_newValue); // Store directly, no malloc!

            break;
        }

        case OP_LOAD_GLOBAL:
        {
            index = code[pc++];
            char *name = string_get(vm->names, index);
            Value *_value = ht_get(vm->globals, name);
            if (_value == NULL)
            {
                nilValue = NEW_NIL();
                _value = &nilValue;
            }
            push_stack(vm, *_value);
            break;
        }

        case OP_LOAD_LOCAL:
        {
            op = code[pc++];
            Value value = vm->stack[resolve_localSlot(vm, op)];
            push_stack(vm, value);
            break;
        }

        case OP_LOAD_SUPER:
        {
            if (!function->is_method || function->instance == NULL)
                vm_error(vm, "super is only available inside object methods.");

            Object *super_obj = add_obj(vm, new_map(ht_create(sizeof(Value)), true));
            PiMap *super = (PiMap *)super_obj;

            super->proto = function->owner ? ((PiMap *)function->owner)->proto : NULL;
            super->super_instance = function->instance;

            push_stack(vm, NEW_OBJ(super_obj));
            break;
        }

        case OP_STORE_LOCAL:
        {
            op = code[pc++];
            int slot = resolve_localSlot(vm, op);
            vm->stack[slot] = pop_stack(vm);

            // Ensure the stack pointer reserves space for locals.
            if (vm->sp <= slot)
                vm->sp = slot + 1;
            break;
        }

        case OP_POP:
        {
            remove_upvalue(vm, vm->sp - 1);
            Value value = pop_stack(vm);
            break;
        }
        case OP_POP_N:
        {
            op = code[pc++];
            for (int i = 0; i < op; i++)
            {
                remove_upvalue(vm, vm->sp - 1);
                pop_stack(vm);
            }
        }
        break;

        case OP_DUP_TOP:
            push_stack(vm, peek_stack(vm));
            break;

        case OP_JUMP_IF_FALSE:
        {
            int offset = (int16_t)((code[pc] << 8) | code[pc + 1]); // Signed 16-bit offset

            Value value = pop_stack(vm);
            if (!as_bool(value))
                pc += offset - 1; // relative jump
            else
                pc += 2;
            break;
        }

        case OP_JUMP:
        {
            int offset = (int16_t)((code[pc] << 8) | code[pc + 1]); // Signed 16-bit offset
            pc += offset - 1;
            break;
        }

        case OP_JUMP_IF_TRUE:
        {
            int offset = (int16_t)((code[pc] << 8) | code[pc + 1]); // Signed 16-bit offset

            Value value = pop_stack(vm);
            if (as_bool(value))
                pc += offset - 1; // relative jump
            else
                pc += 2;
            break;
        }

        case OP_COMPARE:
        {
            uint8_t op = code[pc++];
            Value right = pop_stack(vm);
            Value left = pop_stack(vm);

            // Fast path: both plain numbers (most common case)
            // Zero hash lookups, zero overload checks, zero to_primitive calls
            if (is_numeric(left) && is_numeric(right))
            {
                double l = as_number(left);
                double r = as_number(right);
                bool result;
                switch (op)
                {
                case 0: // "=="
                    result = (l == r);
                    break;
                case 1: // "!="
                    result = (l != r);
                    break;
                case 2: // ">"
                    result = (l > r);
                    break;
                case 3: // "<"
                    result = (l < r);
                    break;
                case 4: // ">="
                    result = (l >= r);
                    break;
                case 5: // "<="
                    result = (l <= r);
                    break;
                default:
                    goto LABEL_COMPARE; // op 6 = 'in', needs objects
                }
                push_stack(vm, NEW_BOOL(result));
                break;
            }

            // Fast path: both bools
            if (IS_BOOL(left) && IS_BOOL(right) && (op == 0 || op == 1))
            {
                bool result = (AS_BOOL(left) == AS_BOOL(right));
                push_stack(vm, NEW_BOOL(op == 0 ? result : !result));
                break;
            }

            // Fast path: both nil
            if (IS_NIL(left) && IS_NIL(right) && (op == 0 || op == 1))
            {
                push_stack(vm, NEW_BOOL(op == 0)); // nil == nil is always true
                break;
            }

            // Fast path: different primitive types are never equal
            if (!IS_OBJ(left) && !IS_OBJ(right) && (op == 0 || op == 1))
            {
                push_stack(vm, NEW_BOOL(op == 1)); // op==0 -> false, op==1 -> true
                break;
            }

            //  Fast path: both strings
            if (IS_STRING(left) && IS_STRING(right))
            {
                int cmp = strcmp(AS_STRING(left)->chars, AS_STRING(right)->chars);
                bool result;
                switch (op)
                {
                case 0:
                    result = (cmp == 0);
                    break;
                case 1:
                    result = (cmp != 0);
                    break;
                case 2:
                    result = (cmp > 0);
                    break;
                case 3:
                    result = (cmp < 0);
                    break;
                case 4:
                    result = (cmp >= 0);
                    break;
                case 5:
                    result = (cmp <= 0);
                    break;
                default:
                    goto LABEL_COMPARE;
                }
                push_stack(vm, NEW_BOOL(result));
                break;
            }

            if ((IS_SET(left) || IS_SET(right)) && op <= 5)
            {
                if (!IS_SET(left) || !IS_SET(right))
                    vm_error(vm, "Set comparison requires two sets.");

                PiSet *left_set = AS_SET(left);
                PiSet *right_set = AS_SET(right);
                bool result = false;

                switch (op)
                {
                case 0: // ==
                    result = set_equals(left_set, right_set);
                    break;
                case 1: // !=
                    result = !set_equals(left_set, right_set);
                    break;
                case 2: // >
                    result = set_isSubset(right_set, left_set) && set_size(left_set) != set_size(right_set);
                    break;
                case 3: // <
                    result = set_isSubset(left_set, right_set) && set_size(left_set) != set_size(right_set);
                    break;
                case 4: // >=
                    result = set_isSubset(right_set, left_set);
                    break;
                case 5: // <=
                    result = set_isSubset(left_set, right_set);
                    break;
                }

                push_stack(vm, NEW_BOOL(result));
                break;
            }

        LABEL_COMPARE:

            //  op 6: 'in' operator
            if (op == 6)
            {
                if (!IS_OBJ(right) || !is_iterable(AS_OBJ(right)))
                    vm_error(vm, "Right operand of 'in' must be iterable.");

                bool result = false;

                switch (OBJ_TYPE(right))
                {
                case OBJ_LIST:
                {
                    // Direct pointer scan — avoids list_getAt call overhead per item
                    PiList *list = AS_LIST(right);
                    Value *items = (Value *)list->items->data;
                    int size = list_size(list->items);
                    for (int i = 0; i < size; i++)
                    {
                        if (equals(left, items[i]))
                        {
                            result = true;
                            break;
                        }
                    }
                    break;
                }

                case OBJ_STRING:
                {
                    if (!IS_STRING(left))
                        break; // result stays false
                    // Direct char pointer — no alloc needed for substring check
                    result = (strstr(AS_STRING(right)->chars,
                                     AS_STRING(left)->chars) != NULL);
                    break;
                }

                case OBJ_MAP:
                {
                    // Key existence check — no iteration needed
                    result = (map_owner(AS_MAP(right), left) != NULL);
                    break;
                }

                case OBJ_SET:
                {
                    result = set_has(AS_SET(right), left);
                    break;
                }

                case OBJ_RANGE:
                {
                    if (!IS_NUM(left))
                        break; // result stays false
                    PiRange *range = AS_RANGE(right);
                    double num = AS_NUM(left);
                    double start = range->start;
                    double end = range->end;
                    double step = range->step;

                    if (step > 0)
                        result = (num >= start && num <= end &&
                                  fmod(num - start, step) < 1e-10);
                    else if (step < 0)
                        result = (num <= start && num >= end &&
                                  fmod(start - num, -step) < 1e-10);
                    else
                        result = (num == start);
                    break;
                }

                default:
                {
                    // Generic iterator fallback for any other iterable
                    Object *iterable = AS_OBJ(right);
                    iter_reset(iterable);
                    while (iter_hasNext(iterable))
                    {
                        if (equals(left, iter_next(iterable)))
                        {
                            result = true;
                            break;
                        }
                    }
                    break;
                }
                }

                push_stack(vm, NEW_BOOL(result));
                break;
            }

            //  ops 0-1: equality
            if (op <= 1)
            {
                bool result = false;

                // Overload check — only pays cost when has_equals flag is set
                if (IS_MAP(left) || IS_MAP(right))
                {
                    if (try_overloadedEquals(vm, left, right, &result))
                    {
                        push_stack(vm, NEW_BOOL(op == 0 ? result : !result));
                        break;
                    }
                }

                // Same object pointer -> always equal
                if (IS_OBJ(left) && IS_OBJ(right) && AS_OBJ(left) == AS_OBJ(right))
                {
                    push_stack(vm, NEW_BOOL(op == 0));
                    break;
                }

                // Coerced comparison as last resort
                Value l = TO_PRIM_NUM(left);
                Value r = TO_PRIM_NUM(right);
                result = (compare(l, r) == 0);
                push_stack(vm, NEW_BOOL(op == 0 ? result : !result));
                break;
            }

            //  ops 2-5: ordered comparison
            {
                int cmp = 0;

                // Overload check — only pays cost when has_compare flag is set
                if (IS_MAP(left) || IS_MAP(right))
                {
                    if (!try_overloadedCompare(vm, left, right, &cmp))
                    {
                        Value l = TO_PRIM_NUM(left);
                        Value r = TO_PRIM_NUM(right);
                        cmp = compare(l, r);
                    }
                }
                else
                {
                    Value l = TO_PRIM_NUM(left);
                    Value r = TO_PRIM_NUM(right);
                    cmp = compare(l, r);
                }

                bool result;
                switch (op)
                {
                case 2:
                    result = (cmp > 0);
                    break;
                case 3:
                    result = (cmp < 0);
                    break;
                case 4:
                    result = (cmp >= 0);
                    break;
                case 5:
                    result = (cmp <= 0);
                    break;
                default:
                    vm_errorf(vm, "Unknown compare opcode: [%d]", op);
                    result = false;
                }
                push_stack(vm, NEW_BOOL(result));
                break; // exits OP_COMPARE
            }
        }
        case OP_BINARY:
        {
            uint8_t op = code[pc++];
            Value right = pop_stack(vm);
            Value left = pop_stack(vm);

            // Global numeric fast path
            // Covers the vast majority of arithmetic — zero hash lookups, zero
            // to_primitive calls, no overload check. Falls through to slow path
            // only for objects, strings, matrices, and special ops.
            if (is_numeric(left) && is_numeric(right))
            {
                double l = as_number(left);
                double r = as_number(right);
                switch (op)
                {
                case 0:
                    push_stack(vm, NEW_NUM(l + r));
                    break;
                case 1:
                    push_stack(vm, NEW_NUM(l - r));
                    break;
                case 2:
                    push_stack(vm, NEW_NUM(l * r));
                    break;
                case 3:
                    push_stack(vm, NEW_NUM(r == 0.0 ? INFINITY : l / r));
                    break;
                case 4:
                {
                    int ir = (int)r;
                    push_stack(vm, ir == 0 ? NEW_NAN() : NEW_NUM((int)l % ir));
                    break;
                }
                case 5:
                    push_stack(vm, NEW_BOOL(l && r));
                    break;
                case 6:
                    push_stack(vm, NEW_BOOL(l || r));
                    break;
                case 7:
                    push_stack(vm, NEW_NUM(pow(l, r)));
                    break;
                case 8:
                    push_stack(vm, NEW_NUM((int)l & (int)r));
                    break;
                case 9:
                    push_stack(vm, NEW_NUM((int)l | (int)r));
                    break;
                case 10:
                    push_stack(vm, NEW_NUM((int)l ^ (int)r));
                    break;
                case 11:
                    push_stack(vm, NEW_NUM((int)l << (int)r));
                    break;
                case 12:
                    push_stack(vm, NEW_NUM((int)l >> (int)r));
                    break;
                case 13:
                    push_stack(vm, NEW_NUM((uint32_t)l >> (uint32_t)r));
                    break;
                // ops 14 (dot), 15 (is) require objects - fall to LABEL_BINARY
                default:
                    goto LABEL_BINARY;
                }
                break; // done - exits OP_BINARY
            }

        LABEL_BINARY:

            // Overload check - only for flagged instances. Numeric primitives
            // have already exited through the fast path above.
            if (op != 5 && op != 6 && op != 15 &&
                IS_MAP(left) && AS_MAP(left)->has_compute)
            {
                Value computed = NEW_NIL();
                if (try_callCompute(vm, left, op, true, right, &computed))
                {
                    push_stack(vm, computed);
                    break;
                }
            }

            if (op != 5 && op != 6 && op != 15 &&
                IS_MAP(right) && AS_MAP(right)->has_rcompute)
            {
                Value computed = NEW_NIL();
                if (try_callReflectedCompute(vm, right, op, left, &computed))
                {
                    push_stack(vm, computed);
                    break;
                }
            }

            switch (op)
            {
            case 0: // +
            {
                // NaN short-circuit — before any object inspection
                if (IS_NAN(left) || IS_NAN(right))
                {
                    push_stack(vm, NEW_NUM(NAN));
                    break;
                }

                // List append
                if (IS_LIST(left))
                {
                    PiList *list = AS_LIST(left);
                    list_add(list->items, &right);

                    if (list->rows == 1 && list->cols >= 0)
                    {
                        if (!IS_NUM(right))
                        {
                            list->rows = -1;
                            list->cols = -1;
                            list->is_numeric = false;
                        }
                        else
                            list->cols++;
                    }
                    else if (list->rows > 1 && list->cols > 0)
                    {
                        if (!IS_LIST(right))
                        {
                            list->rows = -1;
                            list->cols = -1;
                            list->is_numeric = false;
                        }
                        else
                        {
                            PiList *r_list = AS_LIST(right);
                            if (!r_list->is_numeric || r_list->items->size != (size_t)list->cols)
                            {
                                list->rows = -1;
                                list->cols = -1;
                                list->is_numeric = false;
                            }
                            else
                                list->rows++;
                        }
                    }
                    else
                    {
                        // Check if list can become a numeric row vector
                        if (list->items->size == 2 && IS_NUM(right) &&
                            IS_NUM(*(Value *)list_getAt(list->items, 0)))
                        {
                            list->is_numeric = true;
                            list->rows = 1;
                            list->cols = 2;
                        }
                    }

                    push_stack(vm, left);
                    break;
                }

                // String concat — fast path when both are already strings
                if (IS_STRING(left) && IS_STRING(right))
                {
                    const char *l_str = AS_STRING(left)->chars;
                    const char *r_str = AS_STRING(right)->chars;
                    size_t l_len = AS_STRING(left)->length;
                    size_t r_len = AS_STRING(right)->length;

                    char *res = (char *)malloc(l_len + r_len + 1);
                    if (!res)
                        vm_error(vm, "Memory allocation failed.");
                    memcpy(res, l_str, l_len);
                    memcpy(res + l_len, r_str, r_len + 1);
                    push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(res))));
                    break;
                }

                if (IS_TUPLE(left) && IS_TUPLE(right))
                {
                    PiTuple *l_tuple = AS_TUPLE(left);
                    PiTuple *r_tuple = AS_TUPLE(right);
                    list_t *result = list_copy(l_tuple->items);
                    list_addAll(result, r_tuple->items);
                    Object *tuple_obj = add_obj(vm, new_tuple(result));
                    push_stack(vm, NEW_OBJ(tuple_obj));
                    break;
                }

                // Tensor paths
                if (IS_TENSOR(left))
                {
                    if (IS_TENSOR(right))
                        push_stack(vm, tensor_broadcastBinary(vm, AS_TENSOR(left), AS_TENSOR(right), 0));
                    else if (is_numeric(right))
                        push_stack(vm, tensor_scalarBinary(vm, AS_TENSOR(left), as_number(right), 0, false));
                    else
                        vm_error(vm, "Unsupported right operand for tensor [+].");
                    break;
                }
                if (IS_TENSOR(right) && is_numeric(left))
                {
                    push_stack(vm, tensor_scalarBinary(vm, AS_TENSOR(right), as_number(left), 0, true));
                    break;
                }

                // Mixed/coerced path — only reaches here for num+obj or obj+str etc.
                bool pref_string = IS_STRING(left) || IS_STRING(right);
                Value _left = TO_PRIM(vm, left, pref_string);
                Value _right = TO_PRIM(vm, right, pref_string);

                if (is_numeric(_left) && is_numeric(_right))
                {
                    push_stack(vm, NEW_NUM(as_number(_left) + as_number(_right)));
                    break;
                }

                if (IS_STRING(_left) || IS_STRING(_right))
                {
                    char *l_str = as_stringWithFormat(vm, _left);
                    char *r_str = as_stringWithFormat(vm, _right);
                    size_t l_len = strlen(l_str);
                    size_t r_len = strlen(r_str);
                    char *res = (char *)malloc(l_len + r_len + 1);
                    if (!res)
                    {
                        free(l_str);
                        free(r_str);
                        vm_error(vm, "Memory allocation failed.");
                    }
                    memcpy(res, l_str, l_len);
                    memcpy(res + l_len, r_str, r_len + 1);
                    push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(res))));
                    free(l_str);
                    free(r_str);
                    break;
                }

                vm_error(vm, "Unsupported operand types for binary operator [+].");
                break;
            }

            case 1: // "-"
            {

                // List remove first occurrence
                if (IS_LIST(left))
                {
                    PiList *list = AS_LIST(left);
                    int size = list_size(list->items);
                    for (int i = 0; i < size; i++)
                    {
                        Value item = *(Value *)list_getAt(list->items, i);
                        if (equals(item, right))
                        {
                            list_remove(list->items, i);
                            break;
                        }
                    }
                    push_stack(vm, left);
                    break;
                }

                // String remove all occurrences of substring
                if (IS_STRING(left))
                {
                    Value _right = TO_PRIM(vm, right, false);

                    char *l_str = as_stringWithFormat(vm, left);
                    char *r_str = as_stringWithFormat(vm, _right);

                    size_t l_len = strlen(l_str);
                    size_t r_len = strlen(r_str);

                    char *res = (char *)malloc(l_len + 1);

                    char *w_ptr = res;
                    char *r_ptr = l_str;

                    char *match;
                    while ((match = strstr(r_ptr, r_str)) != NULL)
                    {
                        size_t chunk = match - r_ptr;
                        memcpy(w_ptr, r_ptr, chunk);
                        w_ptr += chunk;
                        r_ptr = match + r_len;
                    }

                    strcpy(w_ptr, r_ptr);
                    push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(res))));

                    free(l_str);
                    free(r_str);
                    break;
                }

                if (IS_SET(left) && IS_SET(right))
                {
                    push_stack(vm, NEW_OBJ(add_obj(vm, set_difference(vm, AS_SET(left), AS_SET(right)))));
                    break;
                }

                // Tensor paths
                if (IS_TENSOR(left))
                {
                    if (IS_TENSOR(right))
                        push_stack(vm, tensor_broadcastBinary(vm, AS_TENSOR(left), AS_TENSOR(right), 1));
                    else if (is_numeric(right))
                        push_stack(vm, tensor_scalarBinary(vm, AS_TENSOR(left), as_number(right), 1, false));
                    else
                        vm_error(vm, "Unsupported right operand for tensor [-].");
                    break;
                }

                if (IS_TENSOR(right) && is_numeric(left))
                {
                    push_stack(vm, tensor_scalarBinary(vm, AS_TENSOR(right), as_number(left), 1, true));
                    break;
                }

                // Coerced numeric subtraction (obj with value/format method)
                Value _left = TO_PRIM(vm, left, false);
                Value _right = TO_PRIM(vm, right, false);

                if (is_numeric(_left) && is_numeric(_right))
                {
                    push_stack(vm, NEW_NUM(as_number(_left) - as_number(_right)));
                    break;
                }

                vm_error(vm, "Unsupported operand types for binary operator [-].");
                break;
            }
            case 2: // "*"
            {
                // Matrix multiplication (list * list)
                if (IS_LIST(left) && IS_LIST(right))
                {
                    PiList *A = AS_LIST(left);
                    PiList *B = AS_LIST(right);

                    if (!A->is_numeric || !B->is_numeric)
                        vm_error(vm, "Matrix multiplication requires numeric lists.");
                    if (A->cols == -1 || B->cols == -1)
                        vm_error(vm, "Matrix dimensions are not set properly.");
                    if (A->cols != B->rows)
                        vm_error(vm, "Matrix multiplication dimension mismatch.");

                    int m = A->rows, n = A->cols, p = B->cols;
                    list_t *result = list_create(sizeof(Value));

                    for (int i = 0; i < m; i++)
                    {
                        list_t *rowA = as_list(*(Value *)list_getAt(A->items, i));
                        list_t *temp = list_create(sizeof(Value));

                        for (int j = 0; j < p; j++)
                        {
                            double sum = 0.0;
                            for (int k = 0; k < n; k++)
                            {
                                double a = as_number(*(Value *)list_getAt(rowA, k));
                                list_t *rowB = as_list(*(Value *)list_getAt(B->items, k));
                                double b = as_number(*(Value *)list_getAt(rowB, j));
                                sum += a * b;
                            }
                            Value num = NEW_NUM(sum);
                            list_add(temp, &num);
                        }
                        Value row = NEW_OBJ(new_list(temp));
                        list_add(result, &row);
                    }

                    Object *res_obj = add_obj(vm, new_list(result));
                    ((PiList *)res_obj)->is_numeric = true;
                    ((PiList *)res_obj)->rows = m;
                    ((PiList *)res_obj)->cols = p;
                    push_stack(vm, NEW_OBJ(res_obj));
                    break;
                }

                // List repeat: list * n
                if (IS_LIST(left))
                {
                    Value right_prim = TO_PRIM(vm, right, false);
                    int count = (int)as_number(right_prim);
                    list_t *list = as_list(left);
                    list_t *result = list_create(list->i_size);
                    for (int i = 0; i < count; i++)
                        list_addAll(result, list);
                    Object *res_obj = new_list(result);
                    if (AS_LIST(left)->is_numeric)
                        ((PiList *)res_obj)->is_numeric = true;
                    push_stack(vm, NEW_OBJ(add_obj(vm, res_obj)));
                    break;
                }

                // String repeat: str * n
                if (IS_STRING(left))
                {
                    Value right_prim = TO_PRIM(vm, right, false);
                    int count = (int)as_number(right_prim);
                    const char *str = AS_STRING(left)->chars;
                    size_t o_len = AS_STRING(left)->length;
                    size_t r_len = o_len * (size_t)count;
                    char *result = (char *)malloc(r_len + 1);
                    if (!result)
                        vm_error(vm, "Memory allocation failed.");
                    for (int i = 0; i < count; i++)
                        memcpy(result + i * o_len, str, o_len);
                    result[r_len] = '\0';
                    push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(result))));
                    break;
                }

                if (IS_TUPLE(left) && is_numeric(right))
                {
                    int repeatCount = (int)as_number(right);
                    PiTuple *tuple = AS_TUPLE(left);
                    list_t *result = list_create(sizeof(Value));
                    for (int i = 0; i < repeatCount; i++)
                        list_addAll(result, tuple->items);
                    Object *tuple_obj = add_obj(vm, new_tuple(result));
                    push_stack(vm, NEW_OBJ(tuple_obj));
                    break;
                }

                if (is_numeric(left) && IS_TUPLE(right))
                {
                    int repeatCount = (int)as_number(left);
                    PiTuple *tuple = AS_TUPLE(right);
                    list_t *result = list_create(sizeof(Value));
                    for (int i = 0; i < repeatCount; i++)
                        list_addAll(result, tuple->items);
                    Object *tuple_obj = add_obj(vm, new_tuple(result));
                    push_stack(vm, NEW_OBJ(tuple_obj));
                    break;
                }

                // Tensor paths
                if (IS_TENSOR(left))
                {
                    if (IS_TENSOR(right))
                        push_stack(vm, tensor_broadcastBinary(vm, AS_TENSOR(left), AS_TENSOR(right), 2));
                    else if (is_numeric(right))
                        push_stack(vm, tensor_scalarBinary(vm, AS_TENSOR(left), as_number(right), 2, false));
                    else
                        vm_error(vm, "Unsupported right operand for tensor [*].");
                    break;
                }

                if (IS_TENSOR(right) && is_numeric(left))
                {
                    push_stack(vm, tensor_scalarBinary(vm, AS_TENSOR(right), as_number(left), 2, true));
                    break;
                }

                // Coerced numeric multiply
                Value _left = TO_PRIM(vm, left, false);
                Value _right = TO_PRIM(vm, right, false);
                if (is_numeric(_left) && is_numeric(_right))
                {
                    push_stack(vm, NEW_NUM(as_number(_left) *
                                           as_number(_right)));
                    break;
                }

                vm_error(vm, "Unsupported operand types for binary operator [*].");
                break;
            }
            case 3: // /
            {
                if (IS_TENSOR(left))
                {
                    if (IS_TENSOR(right))
                        push_stack(vm, tensor_broadcastBinary(vm, AS_TENSOR(left), AS_TENSOR(right), 3));
                    else if (is_numeric(right))
                        push_stack(vm, tensor_scalarBinary(vm, AS_TENSOR(left), as_number(right), 3, false));
                    else
                        vm_error(vm, "Unsupported right operand for tensor [/].");
                    break;
                }
                if (IS_TENSOR(right) && is_numeric(left))
                {
                    push_stack(vm, tensor_scalarBinary(vm, AS_TENSOR(right), as_number(left), 3, true));
                    break;
                }

                Value _left = TO_PRIM(vm, left, false);
                Value _right = TO_PRIM(vm, right, false);

                double denom = as_number(_right);
                push_stack(vm, NEW_NUM(denom == 0.0 ? INFINITY : as_number(_left) / denom));
                break;
            }
            case 4: // "%"
            {
                Value _left = TO_PRIM(vm, left, false);
                Value _right = TO_PRIM(vm, right, false);

                int denom = (int)as_number(_right);
                push_stack(vm, denom == 0 ? NEW_NAN() : NEW_NUM((int)as_number(_left) % denom));
                break;
            }
            case 5: // "&&"
                push_stack(vm, NEW_BOOL(as_bool(left) && as_bool(right)));
                break;
            case 6: // "||"
                push_stack(vm, NEW_BOOL(as_bool(left) || as_bool(right)));
                break;
            case 7: // "**"
            {
                Value _left = TO_PRIM(vm, left, false);
                Value _right = TO_PRIM(vm, right, false);
                push_stack(vm, NEW_NUM(pow(as_number(_left), as_number(_right))));
                break;
            }

            //  ops 8-13: bitwise & list-vectorized
            // Shared pattern: num op num, or list op num (vectorized)
            case 8:  // "&"
            case 9:  // "|"
            case 10: // "^" (also handles cross product for list*list)
            case 11: // "<<"
            case 12: // ">>"
            case 13: // ">>>"
            {
                // Set operations for two sets
                if ((op == 8 || op == 9 || op == 10) && IS_SET(left) && IS_SET(right))
                {
                    push_stack(vm, NEW_OBJ(add_obj(vm, set_ops(vm, AS_SET(left), AS_SET(right), op))));
                    break;
                }

                // Cross product: special case for op 10 with two lists
                if (op == 10 && IS_LIST(left) && IS_LIST(right))
                {
                    PiList *l_list = AS_LIST(left);
                    PiList *r_list = AS_LIST(right);

                    if (!l_list->is_numeric || !r_list->is_numeric)
                        vm_error(vm, "Cross product requires numeric lists.");

                    if (list_size(l_list->items) != 3 || list_size(r_list->items) != 3)
                        vm_error(vm, "Cross product is defined for 3-dimensional vectors only.");

                    Value *a = (Value *)l_list->items->data;
                    Value *b = (Value *)r_list->items->data;
                    double x = as_number(a[1]) * as_number(b[2]) - as_number(a[2]) * as_number(b[1]);
                    double y = as_number(a[2]) * as_number(b[0]) - as_number(a[0]) * as_number(b[2]);
                    double z = as_number(a[0]) * as_number(b[1]) - as_number(a[1]) * as_number(b[0]);

                    list_t *res = list_create(sizeof(Value));
                    Value vx = NEW_NUM(x), vy = NEW_NUM(y), vz = NEW_NUM(z);
                    list_add(res, &vx);
                    list_add(res, &vy);
                    list_add(res, &vz);
                    push_stack(vm, NEW_OBJ(add_obj(vm, new_list(res))));
                    break;
                }

                Value _left = TO_PRIM(vm, left, false);
                Value _right = TO_PRIM(vm, right, false);

                // Scalar path
                if (is_numeric(_left) && is_numeric(_right))
                {
                    double l = as_number(_left), r = as_number(_right);
                    double result;
                    switch (op)
                    {
                    case 8:
                        result = (int)l & (int)r;
                        break;
                    case 9:
                        result = (int)l | (int)r;
                        break;
                    case 10:
                        result = (int)l ^ (int)r;
                        break;
                    case 11:
                        result = (int)l << (int)r;
                        break;
                    case 12:
                        result = (int)l >> (int)r;
                        break;
                    case 13:
                        result = (uint32_t)l >> (uint32_t)r;
                        break;
                    default:
                        result = 0;
                    }
                    push_stack(vm, NEW_NUM(result));
                    break;
                }

                // Vectorized list op num
                if (IS_LIST(left))
                {
                    list_t *list = as_list(left);
                    list_t *result = list_create(sizeof(Value));
                    int size = list_size(list);

                    if (op == 13)
                    {
                        uint32_t r = (uint32_t)as_number(_right);
                        for (int i = 0; i < size; i++)
                        {
                            Value item = *(Value *)list_getAt(list, i);
                            Value v = NEW_NUM((uint32_t)as_number(item) >> r);
                            list_add(result, &v);
                        }
                    }
                    else
                    {
                        int r = (int)as_number(_right);
                        for (int i = 0; i < size; i++)
                        {
                            Value item = *(Value *)list_getAt(list, i);
                            int lv = (int)as_number(item);
                            int rv;
                            switch (op)
                            {
                            case 8:
                                rv = lv & r;
                                break;
                            case 9:
                                rv = lv | r;
                                break;
                            case 10:
                                rv = lv ^ r;
                                break;
                            case 11:
                                rv = lv << r;
                                break;
                            case 12:
                                rv = lv >> r;
                                break;
                            default:
                                rv = 0;
                            }
                            Value v = NEW_NUM(rv);
                            list_add(result, &v);
                        }
                    }
                    push_stack(vm, NEW_OBJ(add_obj(vm, new_list(result))));
                    break;
                }

                // Friendly error with op name
                const char *op_names[] = {"&", "|", "^", "<<", ">>", ">>>"};
                vm_errorf(vm, "Unsupported operand types for binary operator [%s].",
                          op_names[op - 8]);
                break;
            }

            case 14: // "." dot product
            {
                if (!IS_LIST(left) || !IS_LIST(right))
                    vm_error(vm, "Dot product requires two numeric lists.");

                PiList *l_list = AS_LIST(left);
                PiList *r_list = AS_LIST(right);

                if (!l_list->is_numeric || !r_list->is_numeric)
                    vm_error(vm, "Dot product requires numeric lists.");

                int l_size = list_size(l_list->items);
                if (l_size != list_size(r_list->items))
                    vm_error(vm, "Dot product requires lists of the same length.");

                // Direct pointer access — avoids list_getAt overhead in tight loop
                Value *a = (Value *)l_list->items->data;
                Value *b = (Value *)r_list->items->data;
                double result = 0.0;
                for (int i = 0; i < l_size; i++)
                    result += as_number(a[i]) * as_number(b[i]);

                push_stack(vm, NEW_NUM(result));
                break;
            }
            case 15: // "is" (instanceof)
            {
                if (!IS_MAP(left) || !IS_MAP(right))
                {
                    push_stack(vm, NEW_BOOL(false));
                    break;
                }

                PiMap *map = AS_MAP(left);
                PiMap *proto = AS_MAP(right);

                while (map != NULL)
                {
                    if (map == proto)
                    {
                        push_stack(vm, NEW_BOOL(true));
                        goto is_done;
                    }
                    map = map->proto;
                }
                push_stack(vm, NEW_BOOL(false));
            is_done:;
                break;
            }

            default:
                vm_errorf(vm, "Unknown binary opcode: [%d]", op);
                break;
            }
            break; // exits OP_BINARY
        }
        case OP_UNARY:
        {
            uint8_t op = code[pc++];
            Value operand = pop_stack(vm);

            if (op == 7)
            {
                push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(strdup(type_name(operand))))));
                break;
            }

            // Fast path: plain number (most common case)
            // ops 0,1,3,5,6 are purely numeric - zero overload check, zero coercion
            if (is_numeric(operand))
            {
                double n = as_number(operand);
                switch (op)
                {
                case 0:
                    push_stack(vm, NEW_NUM(n));
                    break; // unary +
                case 1:
                    push_stack(vm, NEW_NUM(-n));
                    break; // unary -
                case 2:
                    push_stack(vm, NEW_BOOL(n == 0.0));
                    break; // logical NOT (0 is falsy)
                case 3:
                    push_stack(vm, NEW_NUM(~(int)n));
                    break; // bitwise NOT
                case 5:
                    push_stack(vm, NEW_NUM(n + 1.0));
                    break; // ++
                case 6:
                    push_stack(vm, NEW_NUM(n - 1.0));
                    break; // --
                case 4:    // # on a number makes no sense
                    vm_error(vm, "Operator '#' is not defined for numbers.");
                default:
                    vm_error(vm, "Unknown unary operator.");
                }
                break;
            }

            // Fast path: bool - only logical NOT makes sense
            if (IS_BOOL(operand))
            {
                if (op == 2)
                {
                    push_stack(vm, NEW_BOOL(!AS_BOOL(operand)));
                    break;
                }
                // fall through to slow path for anything else (e.g. +true coerces to 1)
            }

            //  Fast path: nil - only logical NOT makes sense─
            if (IS_NIL(operand))
            {
                if (op == 2)
                {
                    push_stack(vm, NEW_BOOL(true));
                    break;
                }
                // nil coerces to 0 for numeric ops - fall through
            }

            //  Overload check - only for maps, only ops 0/1/3, only if flagged
            if (IS_MAP(operand) &&
                (op == 0 || op == 1 || op == 3))
            {
                Value computed = NEW_NIL();
                if (try_callCompute(vm, operand, 100 + op, false, NEW_NIL(), &computed))
                {
                    push_stack(vm, computed);
                    break;
                }
            }

            //  op 2: logical NOT - works on any type via truthiness
            if (op == 2)
            {
                push_stack(vm, NEW_BOOL(!as_bool(operand)));
                break;
            }

            //  op 4: collection size '#'─
            // Pulled before PRIM_AS_NUM - size needs the object itself, not coerced
            if (op == 4)
            {
                if (!IS_OBJ(operand))
                    vm_error(vm, "Operator '#' requires a collection.");

                switch (OBJ_TYPE(operand))
                {
                case OBJ_LIST:
                    push_stack(vm, NEW_NUM(list_size(AS_LIST(operand)->items)));
                    break;
                case OBJ_TENSOR:
                    push_stack(vm, NEW_NUM(AS_TENSOR(operand)->ndim == 0 ? 0 : AS_TENSOR(operand)->shape[0]));
                    break;
                case OBJ_STRING:
                    push_stack(vm, NEW_NUM(AS_STRING(operand)->length));
                    break;
                case OBJ_MAP:
                    push_stack(vm, NEW_NUM(map_size(AS_MAP(operand))));
                    break;
                default:
                    vm_error(vm, "Unsupported operand type for '#' operator.");
                }
                break;
            }

            //  Slow path: coerce to number for ops 0 - 1 - 3 - 5 - 6
            // Only maps with val/fmt methods and nil/bool fallbacks reach here
            double n = as_number(TO_PRIM_NUM(operand));
            switch (op)
            {
            case 0:
                push_stack(vm, NEW_NUM(n));
                break; // unary +
            case 1:
                push_stack(vm, NEW_NUM(-n));
                break; // unary -
            case 3:
                push_stack(vm, NEW_NUM(~(int)n));
                break; // bitwise NOT
            case 5:
                push_stack(vm, NEW_NUM(n + 1.0));
                break; // ++
            case 6:
                push_stack(vm, NEW_NUM(n - 1.0));
                break; // --
            default:
                vm_error(vm, "Unknown unary operator.");
            }

            break;
        }
        // regular call, zero kwargs overhead
        case OP_CALL_FUNCTION:
        {

            // Read the number of arguments from the bytecode
            uint8_t num_args = code[pc++];

            // Copy arguments before changing the stack; callees reuse these slots.
            Value args[num_args];

            for (int i = num_args - 1; i >= 0; i--)
                args[i] = pop_stack(vm);

            Value callee = pop_stack(vm);

            if (IS_FUN(callee))
            {
                vm->pc = pc;
                // Call native function if it's a built-in
                Value result = call_func(vm, AS_FUN(callee), num_args, args, NEW_NIL());
                if (IS_OBJ(result))
                    add_obj(vm, AS_OBJ(result));
                push_stack(vm, result);
            }
            else if (IS_MAP(callee))
            {
                PiMap *map = AS_MAP(callee);
                Value result;
                if (map->is_instance)
                {
                    if (object_instanceCall(vm, map, num_args, args, NEW_NIL(), &result))
                    {
                        push_stack(vm, result);
                        break;
                    }
                    vm_error(vm, "Attempt to call an Object instance.");
                }

                Value constructor = map_getValueByKey(map, "constructor");
                push_stack(vm, NEW_OBJ(add_obj(vm, construct(vm, map, num_args, args, NEW_NIL()))));
            }
            else
                vm_error(vm, "Attempt to call a non-function object.");

            break;
        }

        // only emitted when named args are present
        case OP_CALL_FUNCTION_KW:
        {
            uint8_t num_args = code[pc++];

            Value kw_args = pop_stack(vm);
            if (!IS_OBJ(kw_args) || OBJ_TYPE(kw_args) != OBJ_MAP)
                vm_error(vm, "Named arguments must be a map.");

            Value stack_args[8];
            Value *args = num_args <= 8
                              ? stack_args
                              : (Value *)malloc(num_args * sizeof(Value));

            if (num_args > 8 && !args)
                vm_error(vm, "Memory allocation failed for argument list.");

            for (int i = num_args - 1; i >= 0; i--)
                args[i] = pop_stack(vm);

            Value callee = pop_stack(vm);
            Value result = NEW_NIL();

            if (IS_FUN(callee))
            {
                vm->pc = pc;
                result = call_func(vm, AS_FUN(callee), num_args, args, kw_args);
                if (IS_OBJ(result))
                    add_obj(vm, AS_OBJ(result));
            }
            else if (IS_MAP(callee))
            {
                PiMap *map = AS_MAP(callee);
                if (map->is_instance)
                {
                    if (object_instanceCall(vm, map, num_args, args, kw_args, &result))
                    {
                        if (num_args > 8)
                            free(args);
                    }
                    else
                    {
                        if (num_args > 8)
                            free(args);
                        vm_error(vm, "Attempt to call an Object instance.");
                    }
                }
                else
                {
                    result = NEW_OBJ(add_obj(vm, construct(vm, AS_MAP(callee), num_args, args, kw_args)));
                }
            }
            else
            {
                if (num_args > 8)
                    free(args);
                vm_error(vm, "Attempt to call a non-function object.");
            }

            if (num_args > 8)
                free(args);
            push_stack(vm, result);
            break;
        }

        case OP_CALL_SPREAD:
        {
            bool has_named = code[pc++] != 0;
            Value kw_args = NEW_NIL();

            if (has_named)
            {
                kw_args = pop_stack(vm);
                if (!IS_OBJ(kw_args) || OBJ_TYPE(kw_args) != OBJ_MAP)
                    vm_error(vm, "Named arguments must be a map.");
            }

            Value arg_list_value = pop_stack(vm);
            if (!IS_LIST(arg_list_value))
                vm_error(vm, "Spread call arguments must be collected in a list.");

            Value callee = pop_stack(vm);
            vm->pc = pc;
            push_stack(vm, call_withArgList(vm, callee, AS_LIST(arg_list_value), kw_args, has_named));
            break;
        }

        case OP_PUSH_ITER:
        {
            // Pop the iterable object from the stack
            Value iterable = pop_stack(vm);

            // Ensure the object is iterable
            if (!IS_OBJ(iterable) || !is_iterable(AS_OBJ(iterable)))
                vm_error(vm, "Error: Object is not iterable.");

            iter = AS_OBJ(iterable);

            // Reset iterator state (common for all iterators)
            iter_reset(iter);

            // Push the iterator onto the iterator stack
            if (vm->iter_sp + 1 >= STACK_MAX)
                vm_error(vm, "[iter] Iterator stack overflow.");

            vm->iters[++vm->iter_sp] = iter; // Push a pointer to the iterator
            break;
        }

        case OP_LOOP:
        {
            // Read the jump address from the bytecode
            uint16_t address = (code[pc] << 8);
            address |= code[pc + 1];

            // Get the current iterator from the top of the stack
            if (vm->iter_sp == -1)
                vm_error(vm, "Error: No active iterator.");

            iter = vm->iters[vm->iter_sp];

            // Check if the iterator has more elements
            if (iter_hasNext(iter))
            {
                if (iter->type == OBJ_MAP)
                {
                    PiMap *map = (PiMap *)iter;
                    ht_next(&map->it);
                    char *key = map->it.key;
                    push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(key))));
                }
                else
                {
                    // Get the next value from the iterator
                    Value value = iter_next(iter);
                    if (IS_OBJ(value))
                        add_obj(vm, AS_OBJ(value));
                    push_stack(vm, value);
                }
                pc += 2;
            }
            else
            {
                // Iterator exhausted; pop it from the stack
                vm->iter_sp--;

                // Jump to the specified address
                pc += address - 1;
            }
            break;
        }

        case OP_POP_ITER:
        {
            if (vm->iter_sp != -1)
                iter = vm->iters[vm->iter_sp--];
            // Perform cleanup if needed
            break;
        }
        case OP_PUSH_RANGE:
        {
            // Pop the range values from the stack
            Value step = pop_stack(vm);
            Value end = pop_stack(vm);
            Value start = pop_stack(vm);

            if (!IS_NUM(start) || !IS_NUM(end))
                vm_error(vm, "PiRange `start` and `end` must be numbers.");

            // Create a new range object
            if (!IS_NIL(step) && !IS_NUM(step))
                vm_error(vm, "PiRange `step` must be nil or a number.");
            else
            {

                // Extract numerical values
                double _start = as_number(start);
                double _end = as_number(end);
                double _step;
                if (IS_NIL(step))
                    _step = (_start < _end) ? 1.0 : -1.0;
                else
                    _step = as_number(step);
                Object *range = add_obj(vm, new_range(_start, _end, _step));
                push_stack(vm, NEW_OBJ(range)); // Push the range onto the stack
            }

            break;
        }

        case OP_PUSH_LIST:
        {
            int numElements = (code[pc++] << 8) | code[pc++];
            list_t *list = list_create(sizeof(Value));

            if (numElements == 0)
            {
                Object *l_obj = add_obj(vm, new_list(list));
                PiList *plist = (PiList *)l_obj;
                plist->is_numeric = true;
                plist->is_matrix = false;
                plist->rows = 0;
                plist->cols = 0;
                push_stack(vm, NEW_OBJ(l_obj));
                break;
            }

            vm->sp -= numElements;

            bool is_numeric = true;
            bool is_matrix = true;
            int rows = -1, cols = -1;

            // First: collect all values and add to list
            for (int i = 0; i < numElements; i++)
            {
                Value v = vm->stack[vm->sp + i];
                if (is_numeric && !IS_NUM(v))
                    is_numeric = false;
                list_add(list, &v);
            }

            if (is_numeric)
            {
                is_matrix = false;
                rows = 1;
                cols = numElements;
            }
            else
            {
                // check for matrix: list of equal-sized numeric lists
                Value first = vm->stack[vm->sp];
                if (IS_LIST(first))
                {
                    PiList *pl0 = (PiList *)AS_OBJ(first);
                    if (pl0->is_numeric)
                    {
                        cols = pl0->items->size;
                        rows = numElements;
                        for (int i = 0; i < numElements; i++)
                        {
                            Value v = vm->stack[vm->sp + i];
                            if (!IS_LIST(v))
                            {
                                is_matrix = false;
                                break;
                            }
                            PiList *pl = (PiList *)AS_OBJ(v);
                            if (!pl->is_numeric || pl->items->size != cols)
                            {
                                is_matrix = false;
                                break;
                            }
                        }
                    }
                    else
                        is_matrix = false;
                }
                else
                    is_matrix = false;
            }

            Object *l_obj = add_obj(vm, new_list(list));
            PiList *plist = (PiList *)l_obj;
            plist->is_numeric = is_numeric;
            plist->is_matrix = is_matrix;
            plist->rows = is_matrix ? rows : -1;
            plist->cols = is_matrix ? cols : -1;

            push_stack(vm, NEW_OBJ(l_obj));
            break;
        }

        case OP_PUSH_SET:
        {
            int numElements = (code[pc++] << 8) | code[pc++];
            PiSet *set = (PiSet *)new_set();

            if (numElements == 0)
            {
                Object *set_obj = add_obj(vm, (Object *)set);
                push_stack(vm, NEW_OBJ(set_obj));
                break;
            }

            vm->sp -= numElements;

            for (int i = 0; i < numElements; i++)
            {
                Value element = vm->stack[vm->sp + i];
                if (IS_OBJ(element))
                    add_obj(vm, AS_OBJ(element));
                set_add(set, element);
            }

            Object *set_obj = add_obj(vm, (Object *)set);
            push_stack(vm, NEW_OBJ(set_obj));
            break;
        }

        case OP_PUSH_TUPLE:
        {
            int numElements = (code[pc++] << 8) | code[pc++];
            list_t *items = list_create(sizeof(Value));

            if (numElements > 0)
            {
                vm->sp -= numElements;
                for (int i = 0; i < numElements; i++)
                {
                    Value element = vm->stack[vm->sp + i];
                    if (IS_OBJ(element))
                        add_obj(vm, AS_OBJ(element));
                    list_add(items, &element);
                }
            }

            Object *tuple_obj = add_obj(vm, new_tuple(items));
            push_stack(vm, NEW_OBJ(tuple_obj));
            break;
        }

        case OP_LIST_APPEND:
        {
            Value value = pop_stack(vm);
            if (!IS_LIST(peek_stack(vm)))
                vm_error(vm, "List append expects a list target.");

            PiList *plist = AS_LIST(peek_stack(vm));
            list_add(plist->items, &value);
            break;
        }

        case OP_COMP_APPEND:
        {
            int local = code[pc++];
            Value value = pop_stack(vm);
            int slot = resolve_localSlot(vm, local);
            if (slot < vm->bp || slot >= vm->sp)
                vm_error(vm, "List append local expects a list target.");

            Value target = vm->stack[slot];
            if (!IS_LIST(target))
                vm_error(vm, "List append local expects a list target.");

            list_add(AS_LIST(target)->items, &value);
            break;
        }

        case OP_COMP_BEGIN:
        {
            int local_base = code[pc++];
            if (vm->comp_sp >= STACK_MAX)
                vm_error(vm, "Too many nested list comprehensions.");

            list_t *list = list_create(sizeof(Value));
            Object *l_obj = add_obj(vm, new_list(list));
            PiList *plist = (PiList *)l_obj;
            plist->is_numeric = true;
            plist->is_matrix = false;
            plist->rows = 0;
            plist->cols = 0;
            push_stack(vm, NEW_OBJ(l_obj));

            int top = vm->comp_sp++;
            vm->comp_bases[top] = vm->sp - 1;
            vm->comp_local_bases[top] = local_base;
            vm->comp_bps[top] = vm->bp;
            break;
        }

        case OP_COMP_END:
        {
            if (vm->comp_sp <= 0)
                vm_error(vm, "List comprehension end without a matching begin.");
            if (!IS_LIST(peek_stack(vm)))
                vm_error(vm, "List comprehension end expects a list accumulator.");

            int top = vm->comp_sp - 1;
            if (vm->comp_bases[top] != vm->sp - 1)
                vm_error(vm, "List comprehension stack is unbalanced.");

            refresh_listMeta(AS_LIST(peek_stack(vm)));
            vm->comp_sp--;
            break;
        }

        case OP_LIST_EXTEND:
        {
            Value iterable = pop_stack(vm);
            if (!IS_LIST(peek_stack(vm)))
                vm_error(vm, "List extend expects a list target.");

            PiList *plist = AS_LIST(peek_stack(vm));
            list_extendFromIterable(vm, plist, iterable);
            break;
        }

        case OP_LIST_FINALIZE:
        {
            if (!IS_LIST(peek_stack(vm)))
                vm_error(vm, "List finalize expects a list target.");

            refresh_listMeta(AS_LIST(peek_stack(vm)));
            break;
        }

        case OP_PUSH_MAP:
        {

            // Read the number of elements in the map
            int numElements = code[pc++] << 8;
            numElements |= code[pc++];
            // create a new hashtable
            table_t *table = ht_create(sizeof(Value));
            bool has_compute = false;
            bool has_rcompute = false;

            // Adjust the stack pointer to the first element of the map
            int _sp = vm->sp - (numElements * 2);

            // Populate the map directly from the stack
            for (int i = _sp; i < vm->sp; i += 2)
            {
                Value value = vm->stack[i];

                char *key = AS_CSTRING(vm->stack[i + 1]);
                ht_put(table, key, &value);
                if (IS_FUN(value))
                {
                    if (strcmp(key, "compute") == 0)
                        has_compute = true;
                    else if (strcmp(key, "rcompute") == 0)
                        has_rcompute = true;
                }
            }

            vm->sp = _sp;

            // Push the new map onto the stack
            Object *map = add_obj(vm, new_map(table, false));
            PiMap *pimap = (PiMap *)map;
            pimap->has_compute = has_compute;
            pimap->has_rcompute = has_rcompute;
            push_stack(vm, NEW_OBJ(map));

            break;
        }

        case OP_MAP_SET:
        {
            Value key = pop_stack(vm);
            Value value = pop_stack(vm);
            if (!IS_MAP(peek_stack(vm)))
                vm_error(vm, "Map set expects a map target.");
            if (!IS_STRING(key))
                vm_error(vm, "Map literal keys must be strings.");

            PiMap *map = AS_MAP(peek_stack(vm));
            ht_put(map->table, AS_CSTRING(key), &value);
            if (IS_FUN(value))
            {
                if (strcmp(AS_CSTRING(key), "compute") == 0)
                    map->has_compute = true;
                else if (strcmp(AS_CSTRING(key), "rcompute") == 0)
                    map->has_rcompute = true;
            }
            break;
        }

        case OP_MAP_EXTEND:
        {
            Value source = pop_stack(vm);
            if (!IS_MAP(peek_stack(vm)))
                vm_error(vm, "Map extend expects a map target.");

            map_extendFromMap(vm, AS_MAP(peek_stack(vm)), source);
            break;
        }

        case OP_MAP_FINALIZE:
        {
            int name_index = code[pc++];
            if (!IS_MAP(peek_stack(vm)))
                vm_error(vm, "Map finalize expects a map target.");

            PiMap *map = AS_MAP(peek_stack(vm));
            finalize_mapLiteral(vm, map);

            if (name_index != 0xFF && map->proto != NULL && map->intrinsic_name == NULL)
                map->intrinsic_name = strdup(string_get(vm->names, name_index));
            break;
        }

        case OP_PUSH_FUNCTION:
        {
            // Read the number of parameters
            int numParams = code[pc++];

            ObjCode *body = AS_CODE(pop_stack(vm));
            char *name = AS_CSTRING(pop_stack(vm));

            list_t *defaults = list_create(sizeof(Value));

            // Adjust the stack pointer to the first parameter
            vm->sp -= numParams;

            // Populate the parameter list directly from the stack
            for (int i = 0; i < numParams; i++)
            {
                Value param = vm->stack[vm->sp + i];
                list_add(defaults, &param);
            }

            // Create a new function object
            Object *function = new_func(name, body, defaults, NULL, NULL);
            ((Function *)function)->need_args = fun_scanSlot(body, (uint8_t)numParams);
            ((Function *)function)->need_kwargs = fun_scanSlot(body, (uint8_t)(numParams + 1));
            ((Function *)function)->constants = vm->constants;
            ((Function *)function)->names = vm->names;
            ((Function *)function)->instrs = vm->instrs;
            ((Function *)function)->globals = vm->globals;

            // Push the new function onto the stack
            push_stack(vm, NEW_OBJ(add_obj(vm, function)));

            break;
        }

        case OP_PUSH_CLOSURE:
        {
            int numParams = code[pc++];
            // Read the number of upvalues
            int numUpvalues = code[pc++];

            UpValue **upvalues = ALLOCATE(UpValue *, numUpvalues + 1);

            // Populate the upvalue list directly from the stack
            for (int i = 0; i < numUpvalues; i++)
            {
                bool is_local = as_bool(pop_stack(vm));
                int index = as_number(pop_stack(vm));
                UpValue *upvalue;

                if (is_local)
                    upvalue = capture_upvalue(vm, vm->bp + index);
                else
                    upvalue = function->upvalues[index];

                upvalues[numUpvalues - i - 1] = upvalue;
            }
            upvalues[numUpvalues] = NULL;

            ObjCode *body = AS_CODE(pop_stack(vm));
            char *name = AS_CSTRING(pop_stack(vm));

            list_t *defaults = list_create(sizeof(Value));

            // Adjust the stack pointer to the first parameter
            vm->sp -= numParams;

            // Populate the parameter list directly from the stack
            for (int i = 0; i < numParams; i++)
            {
                Value param = vm->stack[vm->sp + i];
                list_add(defaults, &param);
            }

            Object *fun_obj = new_func(name, body, defaults, upvalues, NULL);
            ((Function *)fun_obj)->need_args = fun_scanSlot(body, (uint8_t)numParams);
            ((Function *)fun_obj)->need_kwargs = fun_scanSlot(body, (uint8_t)(numParams + 1));
            ((Function *)fun_obj)->constants = vm->constants;
            ((Function *)fun_obj)->names = vm->names;
            ((Function *)fun_obj)->instrs = vm->instrs;
            ((Function *)fun_obj)->globals = vm->globals;

            // Push the new closure onto the stack
            push_stack(vm, NEW_OBJ(add_obj(vm, fun_obj)));

            break;
        }

        case OP_LOAD_UPVALUE:
        {
            int index = code[pc++];
            if (function->upvalues == NULL || function->upvalues[index] == NULL)
                vm_error(vm, "Invalid method binding: closure lost its captured variables while binding a method.");
            UpValue *upValue = function->upvalues[index];
            if (upValue->index != -1)
                push_stack(vm, vm->stack[upValue->index]);
            else
                push_stack(vm, upValue->value);
            break;
        }

        case OP_STORE_UPVALUE:
        {
            int index = code[pc++];
            if (function->upvalues == NULL || function->upvalues[index] == NULL)
                vm_error(vm, "Invalid method binding: closure lost its captured variables while binding a method.");
            UpValue *upValue = function->upvalues[index];
            Value stored = pop_stack(vm);

            if (upValue->index != -1)
                vm->stack[upValue->index] = stored;
            else
                function->upvalues[index]->value = stored;
            break;
        }

        case OP_PUSH_SLICE:
        {

            Value _step = pop_stack(vm);
            Value _end = pop_stack(vm);
            Value _start = pop_stack(vm);

            double start = as_number(_start);
            double end = as_number(_end);
            double step = IS_NIL(_step) ? 1.0 : as_number(_step);

            if (!IS_NUM(_start) || !IS_NUM(_end))
                vm_error(vm, "Slice start and end must be numbers");
            if (!IS_NIL(_step) && !IS_NUM(_step))
                vm_error(vm, "Slice step must be nil or a number");
            if (step == 0.0)
                vm_error(vm, "Slice step cannot be zero");

            Object *slice_obj = add_obj(vm, new_slice(start, end, step));
            push_stack(vm, NEW_OBJ(slice_obj));
            break;
        }

        case OP_GET_ITEM:
        case OP_GET_MEMBER:
        {
            bool bracket_access = (OpCode)op == OP_GET_ITEM;
            Value index = pop_stack(vm);     // Get the index from the stack
            Value container = pop_stack(vm); // Get the container from the stack
            Value method_result;

            if (!IS_OBJ(container))
                vm_error(vm, "Unsupported operand type for get item operator.\n");

            // If it's a slice, handle it specially for sequences
            if (IS_SLICE(index) &&
                (OBJ_TYPE(container) == OBJ_LIST ||
                 OBJ_TYPE(container) == OBJ_TUPLE ||
                 OBJ_TYPE(container) == OBJ_STRING))
            {
                PiSlice *s = AS_SLICE(index);
                Value result = get_slice(AS_OBJ(container), s->start, s->stop, s->step);
                push_stack(vm, result);
                break;
            }

            if (!bracket_access && IS_STRING(index) &&
                OBJ_TYPE(container) != OBJ_MAP &&
                OBJ_TYPE(container) != OBJ_MODULE)
            {
                char *method_name = AS_CSTRING(index);
                NativeMethod *method = pi_nativeMethodFor(OBJ_TYPE(container), method_name);

                if (method)
                {
                    push_stack(vm, bind_nativeMethod(AS_OBJ(container), method));
                    break;
                }

                char available_methods[512];
                pi_nativeMethodNames(OBJ_TYPE(container), available_methods, sizeof(available_methods));

                if (available_methods[0] != '\0')
                    vm_errorf(vm, "Type '%s' has no method '%s'. Available methods: %s.",
                              type_name(container), method_name, available_methods);
                else
                    vm_errorf(vm, "Type '%s' has no method '%s'.",
                              type_name(container), method_name);
            }

            switch (OBJ_TYPE(container))
            {
            case OBJ_TENSOR:
            {
                PiTensor *tensor = AS_TENSOR(container);
                if (tensor->ndim == 0)
                    vm_error(vm, "Cannot index a scalar tensor.");
                int row = get_index(as_number(index), tensor->shape[0]);
                push_stack(vm, NEW_OBJ(add_obj(vm, tensor_rowAsList(tensor, row))));
                break;
            }
            case OBJ_LIST:
            {
                list_t *list = as_list(container);
                if (list->size == 0)
                    push_stack(vm, NEW_NIL());
                else
                {
                    int _index = as_number(index);
                    Value item = *(Value *)list_getAt(list, _index);
                    push_stack(vm, item); // Avoid unsafe memory access
                }
                break;
            }
            case OBJ_MAP:
            {
                PiMap *map = AS_MAP(container);
                if (!IS_STRING(index) && try_callMethodArgs(vm, container, "getItem", 1, &index, &method_result))
                {
                    push_stack(vm, method_result);
                    break;
                }

                if (bracket_access && IS_STRING(index) && !map->bracket_access)
                    vm_error(vm, "Bracket member access is disabled for this object.");

                PiMap *owner = map_owner(map, index);
                Value item = owner ? map_get(owner, index) : NEW_NIL();

                bool bind_object_method = owner != NULL &&
                                          IS_FUN(item) &&
                                          owner == vm->object_proto;

                if ((map->is_instance && owner != NULL && IS_FUN(item)) || bind_object_method)
                {
                    Object *target = map->super_instance ? map->super_instance : AS_OBJ(container);
                    item = bind(vm, AS_FUN(item), target);
                }

                push_stack(vm, item); // Push NIL if key not found
                break;
            }

            case OBJ_MODULE:
            {
                ObjModule *module = AS_MODULE(container);
                char *property = as_string(index);
                Value item = NEW_NIL();

                if (is_private_moduleName(property))
                {
                    free(property);
                    vm_error(vm, "Cannot access private module member.");
                }

                if (strcmp(property, "name") == 0)
                {
                    char *name = module->name ? module->name : "";
                    item = NEW_OBJ(add_obj(vm, new_pistring(strdup(name))));
                }
                else if (strcmp(property, "is_main") == 0)
                {
                    item = NEW_BOOL(module->is_main);
                }
                else if (strcmp(property, "path") == 0)
                {
                    char *path = module->path ? module->path : "";
                    item = NEW_OBJ(add_obj(vm, new_pistring(strdup(path))));
                }

                else if (strcmp(property, "exports") == 0)
                    item = NEW_OBJ((Object *)module->exports);

                else if (module->exports)
                    item = map_get(module->exports, index);

                free(property);
                push_stack(vm, item);
                break;
            }

            case OBJ_TUPLE:
            {
                PiTuple *tuple = AS_TUPLE(container);
                int _index = get_index(as_number(index), LIST_SIZE(tuple->items));
                Value item = *(Value *)list_getAt(tuple->items, _index);
                push_stack(vm, item);
                break;
            }
            case OBJ_STRING:
            {

                char *str = as_string(container);                      // Convert Value to char*
                int _index = get_index(as_number(index), strlen(str)); // Convert index to int

                // Convert the character to a string (newly allocated)
                char *_char = malloc(2); // 1 char + null terminator
                _char[0] = str[_index];
                _char[1] = '\0';
                push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(_char))));
                free(str);
                break;
            }

            default:
                vm_error(vm, "Unsupported operand type for get item operator.\n");
            }
            break;
        }

        case OP_TENSOR_GET:
        {
            uint8_t ndim = code[pc++];
            Value indices[MAX_TENSOR_DIMS];
            for (int i = ndim - 1; i >= 0; i--)
                indices[i] = pop_stack(vm);

            Value container = pop_stack(vm);
            if (!IS_TENSOR(container))
                vm_error(vm, "N-dimensional indexing is only supported for tensors.");

            PiTensor *tensor = AS_TENSOR(container);
            if (ndim > tensor->ndim)
                vm_error(vm, "Too many tensor indices.");

            TensorSliceSpec specs[MAX_TENSOR_DIMS];
            bool has_slice = false;

            for (int i = 0; i < tensor->ndim; i++)
            {
                if (i < ndim)
                {
                    if (IS_SLICE(indices[i]))
                    {
                        PiSlice *slice = AS_SLICE(indices[i]);
                        if (slice->step == 0)
                            vm_error(vm, "Tensor slice step cannot be zero.");

                        specs[i].step = (int)slice->step;
                        int sign = specs[i].step > 0 ? 1 : -1;
                        specs[i].start = isinf(slice->start)
                                             ? (sign > 0 ? tensor->shape[i] : -1)
                                             : tensor_sliceBound(tensor->shape[i], slice->start, sign);
                        specs[i].end = isinf(slice->stop)
                                           ? (sign > 0 ? tensor->shape[i] : -1)
                                           : tensor_sliceBound(tensor->shape[i], slice->stop, sign);
                        specs[i].count = 0;
                        for (int current = specs[i].start; sign * (specs[i].end - current) > 0; current += specs[i].step)
                            specs[i].count++;
                        has_slice = true;
                    }
                    else
                    {
                        if (!IS_NUM(indices[i]))
                            vm_error(vm, "Tensor index must be a number.");
                        specs[i].start = get_index((int)as_number(indices[i]), tensor->shape[i]);
                        specs[i].end = specs[i].start + 1;
                        specs[i].step = 1;
                        specs[i].count = 1;
                    }
                }
                else
                {
                    specs[i].start = 0;
                    specs[i].end = tensor->shape[i];
                    specs[i].step = 1;
                    specs[i].count = tensor->shape[i];
                    has_slice = true;
                }
            }

            if (!has_slice)
            {
                int coords[MAX_TENSOR_DIMS];
                for (int i = 0; i < tensor->ndim; i++)
                    coords[i] = specs[i].start;
                push_stack(vm, NEW_NUM(tensor_get(tensor, coords)));
                break;
            }

            int out_shape[MAX_TENSOR_DIMS];
            int dim_map[MAX_TENSOR_DIMS];
            int out_ndim = 0;

            for (int i = 0; i < tensor->ndim; i++)
            {
                if (i >= ndim || IS_SLICE(indices[i]))
                {
                    out_shape[out_ndim] = specs[i].count;
                    dim_map[out_ndim] = i;
                    out_ndim++;
                }
            }

            if (out_ndim == 0)
            {
                int coords[MAX_TENSOR_DIMS];
                for (int i = 0; i < tensor->ndim; i++)
                    coords[i] = specs[i].start;
                push_stack(vm, NEW_NUM(tensor_get(tensor, coords)));
                break;
            }

            int out_strides[MAX_TENSOR_DIMS];
            out_strides[out_ndim - 1] = 1;
            for (int i = out_ndim - 2; i >= 0; i--)
                out_strides[i] = out_strides[i + 1] * out_shape[i + 1];

            PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(out_ndim, out_shape, tensor->type));
            int total = result->size;
            int src_coords[MAX_TENSOR_DIMS];

            for (int i = 0; i < tensor->ndim; i++)
            {
                if (i < ndim && !IS_SLICE(indices[i]))
                    src_coords[i] = specs[i].start;
            }

            for (int flat = 0; flat < total; flat++)
            {
                int remainder = flat;
                for (int j = 0; j < out_ndim; j++)
                {
                    int coord = remainder / out_strides[j];
                    remainder %= out_strides[j];
                    int src_dim = dim_map[j];
                    src_coords[src_dim] = specs[src_dim].start + coord * specs[src_dim].step;
                }
                tensor_setFlat(result, flat, tensor_get(tensor, src_coords));
            }

            push_stack(vm, NEW_OBJ(result));
            break;
        }

        case OP_SET_ITEM:
        case OP_SET_MEMBER:
        {
            bool bracket_access = (OpCode)op == OP_SET_ITEM;
            Value index = pop_stack(vm);     // The index/key
            Value container = pop_stack(vm); // The container (list/map)
            Value value = pop_stack(vm);     // The value to set
            Value method_result;

            if (!IS_OBJ(container))
                vm_error(vm, "Unsupported operand type for set item operator.\n");

            switch (OBJ_TYPE(container))
            {
            case OBJ_TENSOR:
            {
                PiTensor *tensor = AS_TENSOR(container);
                if (tensor->ndim != 2)
                    vm_error(vm, "Tensor row assignment requires a rank-2 tensor.");

                int row = get_index(as_number(index), tensor->shape[0]);
                if (IS_LIST(value))
                {
                    PiList *src = AS_LIST(value);
                    if (!src->is_numeric || src->items->size != tensor->shape[1])
                        vm_error(vm, "Tensor row assignment requires a numeric list of matching width.");
                    for (int col = 0; col < tensor->shape[1]; col++)
                    {
                        int indices[2] = {row, col};
                        tensor_set(tensor, indices, as_number(*(Value *)list_getAt(src->items, col)));
                    }
                }
                else
                    vm_error(vm, "Tensor row assignment requires a list.");
                break;
            }
            case OBJ_LIST:
            {
                PiList *pi_list = AS_LIST(container);
                if (IS_SLICE(index))
                {
                    list_setSlice(vm, pi_list, AS_SLICE(index), value);
                    break;
                }

                list_t *list = pi_list->items;
                int _index = get_index(as_number(index), list_size(list));
                list_set(list, _index, &value);
                if (!IS_NUM(value))
                    pi_list->is_numeric = false;
                break;
            }

            case OBJ_MAP:
            {
                PiMap *map = AS_MAP(container);
                Value args[2] = {index, value};
                if (!IS_STRING(index) && try_callMethodArgs(vm, container, "setItem", 2, args, &method_result))
                    break;

                if (bracket_access && IS_STRING(index) && !map->bracket_access)
                    vm_error(vm, "Bracket member access is disabled for this object.");

                if (map->is_instance)
                {
                    char *key = as_string(index);
                    if (!ht_set(map->table, key, &value))
                        ht_put(map->table, key, &value);
                    free(key);
                }
                else
                {
                    if (map->locked && map_owner(map, index) == NULL)
                        vm_error(vm, "Cannot add a new key to a locked object.");
                    map_set(map, index, value);
                }

                break;
            }

            case OBJ_MODULE:
                vm_error(vm, "Cannot modify module object directly.");
                break;

            case OBJ_STRING:
                vm_error(vm, "Cannot modify immutable string.\n");
                break;

            default:
                vm_error(vm, "Unsupported operand type for set item operator.\n");
            }
            break;
        }

        case OP_TENSOR_SET:
        {
            uint8_t ndim = code[pc++];
            Value indices[MAX_TENSOR_DIMS];
            for (int i = ndim - 1; i >= 0; i--)
                indices[i] = pop_stack(vm);

            Value container = pop_stack(vm);
            Value assign_value = pop_stack(vm); /* pushed before the subscript chain */

            if (!IS_TENSOR(container))
                vm_error(vm, "N-dimensional assignment is only supported for tensors.");

            PiTensor *tensor = AS_TENSOR(container);
            if (ndim > tensor->ndim)
                vm_error(vm, "Too many tensor indices.");

            TensorSliceSpec specs[MAX_TENSOR_DIMS];
            bool has_slice = false;

            for (int i = 0; i < tensor->ndim; i++)
            {
                if (i < ndim)
                {
                    if (IS_SLICE(indices[i]))
                    {
                        PiSlice *slice = AS_SLICE(indices[i]);
                        if (slice->step == 0)
                            vm_error(vm, "Tensor slice step cannot be zero.");

                        specs[i].step = (int)slice->step;
                        int sign = specs[i].step > 0 ? 1 : -1;
                        specs[i].start = isinf(slice->start)
                                             ? (sign > 0 ? tensor->shape[i] : -1)
                                             : tensor_sliceBound(tensor->shape[i], slice->start, sign);
                        specs[i].end = isinf(slice->stop)
                                           ? (sign > 0 ? tensor->shape[i] : -1)
                                           : tensor_sliceBound(tensor->shape[i], slice->stop, sign);
                        specs[i].count = 0;
                        for (int current = specs[i].start; sign * (specs[i].end - current) > 0; current += specs[i].step)
                            specs[i].count++;
                        has_slice = true;
                    }
                    else
                    {
                        if (!IS_NUM(indices[i]))
                            vm_error(vm, "Tensor index must be a number.");
                        specs[i].start = get_index((int)as_number(indices[i]), tensor->shape[i]);
                        specs[i].end = specs[i].start + 1;
                        specs[i].step = 1;
                        specs[i].count = 1;
                    }
                }
                else
                {
                    specs[i].start = 0;
                    specs[i].end = tensor->shape[i];
                    specs[i].step = 1;
                    specs[i].count = tensor->shape[i];
                    has_slice = true;
                }
            }

            if (!has_slice)
            {
                int coords[MAX_TENSOR_DIMS];
                for (int i = 0; i < tensor->ndim; i++)
                    coords[i] = specs[i].start;

                if (IS_NUM(assign_value))
                    tensor_set(tensor, coords, as_number(assign_value));
                else if (IS_TENSOR(assign_value) && AS_TENSOR(assign_value)->size == 1)
                    tensor_set(tensor, coords, tensor_getFlat(AS_TENSOR(assign_value), 0));
                else
                    vm_error(vm, "Tensor assignment requires a numeric value.");

                push_stack(vm, assign_value); /* assignment is an expression, leave value on stack */
                break;
            }

            int out_shape[MAX_TENSOR_DIMS];
            int dim_map[MAX_TENSOR_DIMS];
            int out_ndim = 0;

            for (int i = 0; i < tensor->ndim; i++)
            {
                if (i >= ndim || IS_SLICE(indices[i]))
                {
                    out_shape[out_ndim] = specs[i].count;
                    dim_map[out_ndim] = i;
                    out_ndim++;
                }
            }

            int out_strides[MAX_TENSOR_DIMS];
            if (out_ndim > 0)
            {
                out_strides[out_ndim - 1] = 1;
                for (int i = out_ndim - 2; i >= 0; i--)
                    out_strides[i] = out_strides[i + 1] * out_shape[i + 1];
            }

            int total = 1;
            for (int i = 0; i < out_ndim; i++)
                total *= out_shape[i];

            bool scalar_assign = IS_NUM(assign_value) || (IS_TENSOR(assign_value) && AS_TENSOR(assign_value)->size == 1);
            bool tensor_assign = IS_TENSOR(assign_value) && AS_TENSOR(assign_value)->size != 1;
            bool list_assign = IS_LIST(assign_value) && AS_LIST(assign_value)->is_numeric && out_ndim == 1;

            PiTensor *src_tensor = tensor_assign ? AS_TENSOR(assign_value) : NULL;
            PiList *src_list = list_assign ? AS_LIST(assign_value) : NULL;

            if (tensor_assign)
            {
                if (src_tensor->ndim != out_ndim)
                    vm_error(vm, "Assigned tensor shape does not match target tensor slice.");
                for (int i = 0; i < out_ndim; i++)
                {
                    if (src_tensor->shape[i] != out_shape[i])
                        vm_error(vm, "Assigned tensor shape does not match target tensor slice.");
                }
            }

            if (list_assign && LIST_SIZE(src_list->items) != total)
                vm_error(vm, "Assigned list length does not match target tensor slice.");

            int src_coords[MAX_TENSOR_DIMS];
            for (int i = 0; i < tensor->ndim; i++)
            {
                if (i < ndim && !IS_SLICE(indices[i]))
                    src_coords[i] = specs[i].start;
            }

            for (int flat = 0; flat < total; flat++)
            {
                int remainder = flat;
                for (int j = 0; j < out_ndim; j++)
                {
                    int coord = remainder / out_strides[j];
                    remainder %= out_strides[j];
                    int src_dim = dim_map[j];
                    src_coords[src_dim] = specs[src_dim].start + coord * specs[src_dim].step;
                }

                double assign_num = 0.0;

                if (scalar_assign)
                {
                    if (IS_NUM(assign_value))
                        assign_num = as_number(assign_value);
                    else
                        assign_num = tensor_getFlat(AS_TENSOR(assign_value), 0);
                }
                else if (tensor_assign)
                {
                    assign_num = tensor_getFlat(src_tensor, flat);
                }
                else if (list_assign)
                {
                    Value item = *(Value *)list_getAt(src_list->items, flat);
                    if (!IS_NUM(item))
                        vm_error(vm, "Assigned list must contain only numeric values.");
                    assign_num = as_number(item);
                }
                else
                {
                    vm_error(vm, "Tensor slice assignment requires a numeric scalar, numeric list, or tensor.");
                }

                tensor_set(tensor, src_coords, assign_num);
            }

            push_stack(vm, assign_value); /* assignment is an expression, leave value on stack */
            break;
        }

        case OP_IMPORT:
        {
            Value name = pop_stack(vm);
            if (!IS_STRING(name))
                vm_error(vm, "Module name must be a string.");

            Value module = load_module(vm, AS_STRING(name)->chars);
            push_stack(vm, module);
            break;
        }

        case OP_GET_EXPORT:
        {
            Value name = pop_stack(vm);
            Value module = pop_stack(vm);
            char *export_name;

            if (!IS_OBJ(module) || (OBJ_TYPE(module) != OBJ_MAP && OBJ_TYPE(module) != OBJ_MODULE))
                vm_error(vm, "Attempt to access export from non-module object.");

            if (!IS_STRING(name))
                vm_error(vm, "Export name must be a string.");

            export_name = AS_STRING(name)->chars;
            if (OBJ_TYPE(module) == OBJ_MODULE && is_private_moduleName(export_name))
                vm_error(vm, "Cannot import private module member.");

            PiMap *_module = (OBJ_TYPE(module) == OBJ_MODULE) ? AS_MODULE(module)->exports : AS_MAP(module);
            Value value = map_get(_module, name);
            push_stack(vm, value); // Push NIL if export not found
            break;
        }

        case OP_IMPORT_ALL:
        {
            Value module = pop_stack(vm);

            if (!IS_OBJ(module) || (OBJ_TYPE(module) != OBJ_MAP && OBJ_TYPE(module) != OBJ_MODULE))
                vm_error(vm, "Attempt to import from non-module object.");

            PiMap *_module = (OBJ_TYPE(module) == OBJ_MODULE) ? AS_MODULE(module)->exports : AS_MAP(module);
            table_t *table = _module->table;

            int size = ht_length(table);
            char **keys = ht_keys(table);
            for (int i = 0; i < size; i++)
            {
                char *key = keys[i];
                Value *value = (Value *)ht_get(table, key);
                if (!value)
                    continue;
                if (OBJ_TYPE(module) == OBJ_MODULE && is_private_moduleName(key))
                    continue;

                if (!ht_set(vm->globals, key, value))
                    ht_put(vm->globals, key, value);
            }

            break;
        }

        case OP_IMPORT_DEFAULT:
        {
            Value name = pop_stack(vm);
            Value module = pop_stack(vm);
            char *export_name;

            if (!IS_OBJ(module) || (OBJ_TYPE(module) != OBJ_MAP && OBJ_TYPE(module) != OBJ_MODULE))
                vm_error(vm, "Attempt to import from non-module object.");

            if (!IS_STRING(name))
                vm_error(vm, "Export name must be a string.");

            export_name = AS_STRING(name)->chars;
            if (OBJ_TYPE(module) == OBJ_MODULE && is_private_moduleName(export_name))
                vm_error(vm, "Cannot import private module member.");

            PiMap *_module = (OBJ_TYPE(module) == OBJ_MODULE) ? AS_MODULE(module)->exports : AS_MAP(module);
            Value value = map_get(_module, name);

            if (IS_FUN(value))
                push_stack(vm, value);
            else
                push_stack(vm, module);
            break;
        }

        case OP_RETURN:
        {
            // Handle return operation
            Value retval = pop_stack(vm);

            for (int i = vm->sp - 1; i >= vm->bp; i--)
                remove_upvalue(vm, i);

            Frame *frame = pop_frame(vm);

            while (vm->iter_sp > frame->iters_top)
                vm->iter_sp--;

            if (vm->iter_sp != -1)
                iter = vm->iters[vm->iter_sp];

            vm->pc = frame->pc;
            vm->bp = frame->bp;
            vm->sp = frame->sp;
            vm->ip = frame->ip;
            vm->globals = frame->globals;

            vm->code = frame->code;
            vm->constants = frame->constants;
            vm->names = frame->names;
            vm->instrs = frame->instrs;
            vm->function = (Object *)frame->function;

            push_stack(vm, retval);

            return;
        }

        case OP_HALT:
        {
            vm->running = false;
            // Halt the VM
            return;
        }

        case OP_NO:
            break;

        case OP_PUSH_NIL:
            push_stack(vm, NEW_NIL());
            break;

        case OP_DEBUG:
            // Handle debug operation
            printf("[DEBUG] Current PC: %d\n", pc);
            break;

        case OP_PRINT:
        {         
            Value value = pop_stack(vm);
            char *str = as_string(value);            
            printf("%s\n", str);
            free(str);
            break;
        }

        // Add more cases for other opcodes as needed
        default:
            vm_errorf(vm, "Unknown opcode: [%d]\n", op);

            vm->pc = pc;
        }

#ifdef __EMSCRIPTEN__
        if (++browser_steps >= BROWSER_YIELD_STEPS)
        {
            vm->pc = pc;
            browser_steps = 0;
            emscripten_sleep(0);
        }

        // Allocation-driven threshold to avoid collecting on instruction-heavy loops.
        if (vm->counter >= vm->next_gc)
        {
            run_gc(vm);
            vm->counter = 0;
        }
#else
        if (vm->counter >= vm->next_gc)
        {
            int before = vm->obj_count;
            run_gc(vm);
            int after = vm->obj_count;
            int collected = before - after;

            vm->counter = 0;

            // Adapt threshold to avoid over-collecting in long-running loops.
            if (collected <= 0)
                vm->next_gc += vm->next_gc / 2; // GC reclaimed nothing: back off.
            else
                vm->next_gc = after + (after / 2); // Target ~1.5x live set allocations.
            vm->obj_count = after;

            // Clamp bounds (prevent very frequent or very rare GC).
            if (vm->next_gc < GC_MIN_THRESHOLD)
                vm->next_gc = GC_MIN_THRESHOLD;
            else if (vm->next_gc > GC_MAX_THRESHOLD)
                vm->next_gc = GC_MAX_THRESHOLD;

#ifdef DEBUG
            int checked = count_objs(vm);
            printf("[DEBUG] SP: %d\n", vm->sp);
            printf("[GC] Running garbage collection...\n");
            printf("[GC] Before: %d objects in memory\n", before);
            printf("[GC] After: %d objects in memory\n", after);
            printf("[GC] Checked live objects: %d\n", checked);
            printf("[GC] Collected: %d, Next threshold: %d\n", collected, vm->next_gc);
#endif
        }
#endif
        vm->pc = pc;
    }
}

/**
 * Frees the memory allocated for a virtual machine instance.
 *
 * This function is used to clean up the memory allocated to the virtual
 * machine structure. It first frees the memory allocated to the global
 * hash table and then frees the virtual machine structure itself.
 *
 * @param vm The virtual machine instance to be deallocated.
 */
void free_vm(vm_t *vm)
{

    // Free the memory allocated for hash tables
    if (vm->globals)
        ht_free(vm->globals);
    if (vm->modules)
        ht_free(vm->modules);

    if (vm->current_path)
        free(vm->current_path);

    // Free the memory allocated for the mutex
    pthread_mutex_destroy(&vm->lock);

    // Free the virtual machine structure itself
    free(vm);
}
