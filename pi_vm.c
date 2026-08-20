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
#include "builtin/pi_col.h"
#include "builtin/pi_methods.h"

volatile interrupt_flag_t interrupt_requested = 0;

static PiMap *create_objectProto(vm_t *vm);
static Object *construct(vm_t *vm, PiMap *map, size_t argc, Value *argv, Value kw_args);
static Value bind(vm_t *vm, Function *function, Object *instance);
static Value bind_nativeMethod(vm_t *vm, Object *instance, NativeMethod *method);
static void gc_collect(vm_t *vm);

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

static Value make_iterPair(vm_t *vm, Value first, Value second)
{
    list_t *items = list_create(sizeof(Value));
    list_add(items, &first);
    list_add(items, &second);
    return NEW_OBJ(add_obj(vm, new_list(items)));
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

            _list_expand(items, new_capacity);
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

    return vm_instrForOffset(vm, vm->error_pc);
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
    vm->error_pc = 0;
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
    vm->global_cache = &comp->global_cache;

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
    vm->gc_count = 0;
    vm->gc_requested = false;

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
    vm->error_pc = 0;
    vm->sp = 0;
    vm->bp = 0;
    vm->ip = 0;

    vm->code = comp->code;
    vm->constants = comp->constants;
    vm->names = comp->names;
    vm->instrs = comp->instrs;
    vm->current_instr = NULL;

    /* Preserve vm->globals so shell state survives resets. */
    vm->global_cache = &comp->global_cache;

    vm->iter_sp = -1;
    vm->comp_sp = 0;
    vm->frame_sp = 0;

    vm->running = true;

    // Reset GC stats to trigger collection sooner if needed
    vm->counter = 0;
    vm->gc_count = 0;
    vm->gc_requested = false;
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
    vm->obj_count++;

    int gc_cost = 1;
    switch (obj->type)
    {
    case OBJ_MAP:
        gc_cost = 8;
        break;
    case OBJ_FUN:
        gc_cost = 4;
        break;
    case OBJ_LIST:
    case OBJ_TUPLE:
    case OBJ_SET:
        gc_cost = 2;
        break;
    case OBJ_TENSOR:
    {
        PiTensor *tensor = (PiTensor *)obj;
        size_t data_bytes = (size_t)tensor->size * sizeof(double);
        gc_cost += (int)(data_bytes / (32 * 1024));
        break;
    }
    default:
        break;
    }
    vm->counter += gc_cost;
    if (vm->counter >= vm->next_gc)
        vm->gc_requested = true;

    return obj;
}

static inline void gc_trackReferenceDrop(vm_t *vm, Value old_value, Value new_value)
{
    if (IS_OBJ(old_value) && (!IS_OBJ(new_value) || AS_OBJ(old_value) != AS_OBJ(new_value)))
    {
        vm->gc_count++;
        if (vm->gc_count >= GC_RECLAIM_THRESHOLD)
            vm->gc_requested = true;
    }
}

static void gc_collect(vm_t *vm)
{
    int before = vm->obj_count;
    run_gc(vm);
    int after = vm->obj_count;
    int collected = before - after;

    vm->counter = 0;
    vm->gc_count = 0;
    vm->gc_requested = false;

    if (collected <= 0)
        vm->next_gc += vm->next_gc / 4;
    else
        vm->next_gc = after + (after / 2);

    if (vm->next_gc < GC_MIN_THRESHOLD)
        vm->next_gc = GC_MIN_THRESHOLD;
    else if (vm->next_gc > GC_MAX_THRESHOLD)
        vm->next_gc = GC_MAX_THRESHOLD;

#ifdef DEBUG
    printf("[GC] Before: %d, After: %d, Collected: %d, Next threshold: %d\n",
           before, after, collected, vm->next_gc);
#endif
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

    Value value = vm->stack[--vm->sp];
    vm->stack[vm->sp] = NEW_NIL();
    return value;
}

static inline void set_stackTop(vm_t *vm, int new_sp)
{
    if (new_sp < 0 || new_sp > STACK_MAX)
        vm_error(vm, "Invalid stack pointer update.");

    if (new_sp < vm->sp)
        for (int i = new_sp; i < vm->sp; i++)
            vm->stack[i] = NEW_NIL();
    else
        for (int i = vm->sp; i < new_sp; i++)
            vm->stack[i] = NEW_NIL();

    vm->sp = new_sp;
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

/*
 * Resolve a bytecode global-name index once per global table.  Values inside
 * table_t are individually allocated, so their addresses remain stable when
 * the hash table itself grows.
 */
static inline Value *global_slot(vm_t *vm, uint8_t index, const char *name)
{
    GlobalCache *cache = vm->global_cache;
    if (!cache)
        vm_error(vm, "Missing global cache for active code unit.");

    if (cache->globals != vm->globals || cache->names != vm->names)
    {
        memset(cache->slots, 0, sizeof(cache->slots));
        cache->globals = vm->globals;
        cache->names = vm->names;
    }

    Value *slot = cache->slots[index];
    if (!slot)
    {
        slot = ht_get(vm->globals, name);
        cache->slots[index] = slot;
    }
    return slot;
}

static inline int resolve_localSlot(vm_t *vm, int local)
{
    if (vm->comp_sp > 0)
    {
        int top = vm->comp_sp - 1;
        CompFrame *frame = &vm->comp_frames[top];
        if (vm->bp == frame->bp && local >= frame->local_base)
            return frame->base + (local - frame->local_base);
    }

    return vm->bp + local;
}

static Value bind_nativeMethod(vm_t *vm, Object *instance, NativeMethod *method)
{
    (void)vm;

    if (!method->has_cached_bound)
    {
        method->cached_bound = *new_native(method->name, method->func);
        method->has_cached_bound = true;
    }

    Value native = method->cached_bound;
    Function *bound = AS_FUN(native);
    bound->instance = instance;
    bound->is_method = true;

    return native;
}

static inline void vm_listAppendValue(PiList *list, Value value);

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
        vm_listAppendValue(plist, value);
    }
}

/**
 * Mark function values found in a map literal as methods and attach the map prototype.
 */
static void finalize_mapLiteral(vm_t *vm, PiMap *map)
{
    bool has_methods = false;
    ht_iter it = ht_iterator(map->table);
    while (ht_next(&it))
    {
        const char *key = it.key;
        Value *item = (Value *)it.value; // it.value points to stored Value

        if (!item || !IS_FUN(*item))
            continue;

        Function *fn = AS_FUN(*item);
        fn->is_method = true;
        fn->owner = (Object *)map;
        if (strcmp(key, "compute") == 0)
            MAP_SET_FLAG(map, MAP_HAS_COMPUTE, true);
        else if (strcmp(key, "rcompute") == 0)
            MAP_SET_FLAG(map, MAP_HAS_RCOMPUTE, true);

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
    ht_iter it = ht_iterator(map->table);
    while (ht_next(&it))
    {
        const char *key = it.key;
        Value *item = (Value *)it.value;
        if (item == NULL)
            continue;

        if (IS_OBJ(*item))
            add_obj(vm, AS_OBJ(*item));

        if (ht_put(target->table, key, item))
            map_dirty(target);
        if (IS_FUN(*item))
        {
            if (strcmp(key, "compute") == 0)
                MAP_SET_FLAG(target, MAP_HAS_COMPUTE, true);
            else if (strcmp(key, "rcompute") == 0)
                MAP_SET_FLAG(target, MAP_HAS_RCOMPUTE, true);
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
        if (MAP_HAS_FLAG(map, MAP_IS_INSTANCE))
        {
            if (object_instanceCall(vm, map, num_args, args, kw_args, &result))
            {
                free(args);
                return result;
            }
            free(args);
            vm_error(vm, "Attempt to call an Object instance.");
        }

        result = NEW_OBJ(construct(vm, map, num_args, args, kw_args));
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

static inline void vm_storeLocalSlot(vm_t *vm, int slot, Value value)
{
    vm->stack[slot] = value;
    if (vm->sp <= slot)
    {
        for (int i = vm->sp; i < slot; i++)
            vm->stack[i] = NEW_NIL();
        vm->sp = slot + 1;
    }
}

static inline bool vm_storeLoopValueIfLocal(vm_t *vm, uint8_t *code, int *pc, Value value)
{
    int next_pc = *pc + 2;
    if (vm->comp_sp > 0 || code[next_pc] != OP_STORE_LOCAL)
        return false;

    vm_storeLocalSlot(vm, vm->bp + code[next_pc + 1], value);
    *pc += 4;
    return true;
}

static inline void vm_listAppendValue(PiList *list, Value value)
{
    list_t *items = list->items;
    if (items->size == items->capacity)
        _list_grow(items);
    ((Value *)items->data)[items->size++] = value;

    if (!IS_NUM(value))
        list->is_numeric = false;
}

static bool list_getNumericMatrixShape(PiList *list, int *rows, int *cols)
{
    int count = LIST_SIZE(list->items);
    if (count == 0)
        return false;

    int width = -1;
    for (int i = 0; i < count; i++)
    {
        Value value = *(Value *)list_getAt(list->items, i);
        if (!IS_LIST(value))
            return false;
        PiList *row = AS_LIST(value);
        if (!row->is_numeric)
            return false;
        if (width < 0)
            width = LIST_SIZE(row->items);
        else if (width != LIST_SIZE(row->items))
            return false;
    }

    *rows = count;
    *cols = width;
    return true;
}

static inline Value op_binaryNum(int op, double l, double r)
{
    switch (op)
    {
    case 0:
        return NEW_NUM(l + r);
    case 1:
        return NEW_NUM(l - r);
    case 2:
        return NEW_NUM(l * r);
    case 3:
        return NEW_NUM(r == 0.0 ? INFINITY : l / r);
    case 4:
    {
        int ir = (int)r;
        return ir == 0 ? NEW_NAN() : NEW_NUM((int)l % ir);
    }
    case 5:
        return NEW_BOOL(l && r);
    case 6:
        return NEW_BOOL(l || r);
    case 7:
        return NEW_NUM(pow(l, r));
    case 8:
        return NEW_NUM((int)l & (int)r);
    case 9:
        return NEW_NUM((int)l | (int)r);
    case 10:
        return NEW_NUM((int)l ^ (int)r);
    case 11:
        return NEW_NUM((int)l << (int)r);
    case 12:
        return NEW_NUM((int)l >> (int)r);
    case 13:
        return NEW_NUM((uint32_t)l >> (uint32_t)r);
    default:
        return NEW_NUM(0);
    }
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
    _upvalue->ref_count = 0;

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
        bound->bound_source = (Object *)function;
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
    if (function->upvalue_count > 0 && function->upvalues)
    {
        UpValue **upvalues = ALLOCATE(UpValue *, function->upvalue_count + 1);
        for (int i = 0; i < function->upvalue_count; i++)
        {
            upvalues[i] = function->upvalues[i];
            if (upvalues[i])
                upvalues[i]->ref_count++;
        }
        upvalues[function->upvalue_count] = NULL;
        ((Function *)fn)->upvalues = upvalues;
        ((Function *)fn)->owns_upvalues = true;
    }
    else
    {
        ((Function *)fn)->upvalues = NULL;
        ((Function *)fn)->owns_upvalues = false;
    }
    ((Function *)fn)->upvalue_count = function->upvalue_count;
    ((Function *)fn)->need_args = function->need_args;
    ((Function *)fn)->need_kwargs = function->need_kwargs;
    ((Function *)fn)->owner = function->owner;
    ((Function *)fn)->bound_source = (Object *)function;

    // Set the is_method flag to true
    ((Function *)fn)->is_method = true;

    ((Function *)fn)->need_args = function->body ? function->body->method_need_args : false;
    ((Function *)fn)->need_kwargs = function->body ? function->body->method_need_kwargs : false;

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
    if (!IS_MAP(receiver) || !MAP_HAS_FLAG(AS_MAP(receiver), MAP_IS_INSTANCE))
        return receiver;

    PiMap *owner = map_ownerByKey(AS_MAP(receiver), name);
    if (owner == NULL)
        return receiver;

    Value *method_ptr = ht_get(owner->table, name);
    Value method = method_ptr ? *method_ptr : NEW_NIL();
    if (!IS_FUN(method))
        return receiver;

    if (MAP_HAS_FLAG(AS_MAP(receiver), MAP_IS_INSTANCE))
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
    if (!IS_MAP(receiver) || !MAP_HAS_FLAG(AS_MAP(receiver), MAP_IS_INSTANCE))
        return false;

    PiMap *owner = map_ownerByKey(AS_MAP(receiver), name);
    if (owner == NULL)
        return false;

    Value *method_ptr = ht_get(owner->table, name);
    Value method = method_ptr ? *method_ptr : NEW_NIL();
    if (!IS_FUN(method))
        return false;

    if (MAP_HAS_FLAG(AS_MAP(receiver), MAP_IS_INSTANCE))
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
    if (!IS_MAP(receiver) || !MAP_HAS_FLAG(AS_MAP(receiver), MAP_IS_INSTANCE))
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
    if (!IS_MAP(receiver) || !MAP_HAS_FLAG(AS_MAP(receiver), MAP_IS_INSTANCE))
        return false;

    PiMap *owner = map_ownerByKey(AS_MAP(receiver), name);
    if (owner == NULL)
        return false;

    Value *method_ptr = ht_get(owner->table, name);
    Value method = method_ptr ? *method_ptr : NEW_NIL();
    if (!IS_FUN(method))
        return false;

    if (MAP_HAS_FLAG(AS_MAP(receiver), MAP_IS_INSTANCE))
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
    if (!IS_MAP(value) || !MAP_HAS_FLAG(AS_MAP(value), MAP_IS_INSTANCE))
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
    Object *instance = add_obj(vm, new_map(table, true));

    ((PiMap *)instance)->proto = map;
    ((PiMap *)instance)->flags = map->flags;
    MAP_SET_FLAG((PiMap *)instance, MAP_IS_INSTANCE, true);

    // Push the new instance onto the VM stack
    // vm->stack[vm->sp] = NEW_OBJ(instance);

    // Invoke the constructor if it exists
    Value constructor = map_getValueByKey(map, "constructor");

    if (IS_FUN(constructor))
    {
        Value bound = bind(vm, AS_FUN(constructor), instance);
        push_stack(vm, NEW_OBJ(instance));
        call_func(vm, AS_FUN(bound), argc, argv, kw_args);
        pop_stack(vm);
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

#define vm_error(vm, message)      \
    do                             \
    {                              \
        VM_SYNC_PC();              \
        vm_error((vm), (message)); \
    } while (0)

#define vm_errorf(vm, ...)            \
    do                                \
    {                                 \
        VM_SYNC_PC();                 \
        vm_errorf((vm), __VA_ARGS__); \
    } while (0)

/**
 * @brief Runs the virtual machine.
 *
 * @param vm The virtual machine to run.
 */
void vm_run(vm_t *vm)
{
    static const void *dispatch[256] = {
        [OP_LOAD_CONST] = VM_TARGET(OP_LOAD_CONST),
        [OP_STORE_GLOBAL] = VM_TARGET(OP_STORE_GLOBAL),
        [OP_LOAD_GLOBAL] = VM_TARGET(OP_LOAD_GLOBAL),
        [OP_LOAD_LOCAL] = VM_TARGET(OP_LOAD_LOCAL),
        [OP_LOAD_SUPER] = VM_TARGET(OP_LOAD_SUPER),
        [OP_STORE_LOCAL] = VM_TARGET(OP_STORE_LOCAL),
        [OP_POP] = VM_TARGET(OP_POP),
        [OP_POP_N] = VM_TARGET(OP_POP_N),
        [OP_DUP_TOP] = VM_TARGET(OP_DUP_TOP),
        [OP_JUMP_IF_FALSE] = VM_TARGET(OP_JUMP_IF_FALSE),
        [OP_JUMP] = VM_TARGET(OP_JUMP),
        [OP_JUMP_IF_TRUE] = VM_TARGET(OP_JUMP_IF_TRUE),
        [OP_COMPARE] = VM_TARGET(OP_COMPARE),
        [OP_BINARY] = VM_TARGET(OP_BINARY),
        [OP_UNARY] = VM_TARGET(OP_UNARY),
        [OP_CALL_FUNCTION] = VM_TARGET(OP_CALL_FUNCTION),
        [OP_CALL_FUNCTION_KW] = VM_TARGET(OP_CALL_FUNCTION_KW),
        [OP_CALL_SPREAD] = VM_TARGET(OP_CALL_SPREAD),
        [OP_PUSH_ITER] = VM_TARGET(OP_PUSH_ITER),
        [OP_LOOP] = VM_TARGET(OP_LOOP),
        [OP_POP_ITER] = VM_TARGET(OP_POP_ITER),
        [OP_PUSH_RANGE] = VM_TARGET(OP_PUSH_RANGE),
        [OP_PUSH_LIST] = VM_TARGET(OP_PUSH_LIST),
        [OP_PUSH_SET] = VM_TARGET(OP_PUSH_SET),
        [OP_PUSH_TUPLE] = VM_TARGET(OP_PUSH_TUPLE),
        [OP_LIST_APPEND] = VM_TARGET(OP_LIST_APPEND),
        [OP_COMP_APPEND] = VM_TARGET(OP_COMP_APPEND),
        [OP_COMP_BEGIN] = VM_TARGET(OP_COMP_BEGIN),
        [OP_COMP_END] = VM_TARGET(OP_COMP_END),
        [OP_LIST_EXTEND] = VM_TARGET(OP_LIST_EXTEND),
        [OP_PUSH_MAP] = VM_TARGET(OP_PUSH_MAP),
        [OP_MAP_SET] = VM_TARGET(OP_MAP_SET),
        [OP_MAP_EXTEND] = VM_TARGET(OP_MAP_EXTEND),
        [OP_MAP_FINALIZE] = VM_TARGET(OP_MAP_FINALIZE),
        [OP_PUSH_FUNCTION] = VM_TARGET(OP_PUSH_FUNCTION),
        [OP_PUSH_CLOSURE] = VM_TARGET(OP_PUSH_CLOSURE),
        [OP_LOAD_UPVALUE] = VM_TARGET(OP_LOAD_UPVALUE),
        [OP_STORE_UPVALUE] = VM_TARGET(OP_STORE_UPVALUE),
        [OP_PUSH_SLICE] = VM_TARGET(OP_PUSH_SLICE),
        [OP_GET_ITEM] = VM_TARGET(OP_GET_ITEM),
        [OP_GET_MEMBER] = VM_TARGET(OP_GET_MEMBER),
        [OP_TENSOR_GET] = VM_TARGET(OP_TENSOR_GET),
        [OP_SET_ITEM] = VM_TARGET(OP_SET_ITEM),
        [OP_SET_MEMBER] = VM_TARGET(OP_SET_MEMBER),
        [OP_TENSOR_SET] = VM_TARGET(OP_TENSOR_SET),
        [OP_IMPORT] = VM_TARGET(OP_IMPORT),
        [OP_GET_EXPORT] = VM_TARGET(OP_GET_EXPORT),
        [OP_IMPORT_ALL] = VM_TARGET(OP_IMPORT_ALL),
        [OP_IMPORT_DEFAULT] = VM_TARGET(OP_IMPORT_DEFAULT),
        [OP_RETURN] = VM_TARGET(OP_RETURN),
        [OP_RETURN_NIL] = VM_TARGET(OP_RETURN_NIL),
        [OP_HALT] = VM_TARGET(OP_HALT),
        [OP_NO] = VM_TARGET(OP_NO),
        [OP_PUSH_NIL] = VM_TARGET(OP_PUSH_NIL),
        [OP_DEBUG] = VM_TARGET(OP_DEBUG),
        [OP_PRINT] = VM_TARGET(OP_PRINT),
    };

    int frame_sp = vm->frame_sp;
    int length = vm->code->size;
    int pc = vm->pc;
    int instr_pc = pc;
    uint8_t current_op = OP_NO;
#ifdef __EMSCRIPTEN__
    int browser_steps = 0;
#else
    int interrupt_steps = 0;
#endif
    int safepoint_steps = 0;

    uint8_t *code = (uint8_t *)vm->code->data;
    Value *constants_data = (Value *)vm->constants->data;

    Value value;
    Value nilValue;
    Object *iter = NULL;
    UpValue *upValue;
    Function *function = (Function *)vm->function;

#define VM_RETURN_WITH(value_expr)                                                 \
    do                                                                             \
    {                                                                              \
        Value retval = (value_expr);                                               \
                                                                                   \
        if (vm->openUpvalues)                                                      \
            for (int i = vm->sp - 1; i >= vm->bp; i--)                             \
                remove_upvalue(vm, i);                                             \
                                                                                   \
        if (vm->frame_sp <= 0)                                                     \
            vm_error(vm, "Stack underflow: Attempted to pop from an empty stack"); \
        Frame *frame = &vm->frames[--vm->frame_sp];                                \
                                                                                   \
        while (vm->iter_sp > frame->iters_top)                                     \
            vm->iter_sp--;                                                         \
                                                                                   \
        if (vm->iter_sp != -1)                                                     \
            iter = vm->iters[vm->iter_sp];                                         \
                                                                                   \
        vm->pc = frame->pc;                                                        \
        vm->bp = frame->bp;                                                        \
        vm->sp = frame->sp;                                                        \
        vm->ip = frame->ip;                                                        \
                                                                                   \
        if (!frame->is_recursive)                                                  \
        {                                                                          \
            vm->globals = frame->globals;                                          \
            vm->global_cache = frame->global_cache;                                \
            vm->code = frame->code;                                                \
            vm->constants = frame->constants;                                      \
            vm->names = frame->names;                                              \
            vm->instrs = frame->instrs;                                            \
            vm->function = (Object *)frame->function;                              \
        }                                                                          \
                                                                                   \
        PUSH(retval);                                                              \
                                                                                   \
        code = (uint8_t *)vm->code->data;                                          \
        constants_data = (Value *)vm->constants->data;                             \
        length = vm->code->size;                                                   \
        pc = vm->pc;                                                               \
        if (!frame->is_recursive)                                                  \
            function = frame->function;                                            \
                                                                                   \
        if (vm->frame_sp < frame_sp)                                               \
        {                                                                          \
            if (vm->gc_requested)                                                  \
                gc_collect(vm);                                                    \
            return;                                                                \
        }                                                                          \
                                                                                   \
        VM_DISPATCH_SAFE();                                                        \
    } while (0)

    BEGIN_VM_LOOP();

OP_LOAD_CONST:
{
    int index = (code[pc++] << 8);
    index |= code[pc++];
    vm->stack[vm->sp++] = constants_data[index];
    VM_DISPATCH_SAFE();
}

OP_STORE_GLOBAL:
{
    uint8_t index = code[pc++];
    char *name = read_name(vm, index);

    Value _newValue = POP();
    Value *oldValue = global_slot(vm, index, name);
    if (oldValue && IS_FUN(*oldValue))
    {
        AS_FUN(*oldValue)->global_valid = false;
        AS_FUN(*oldValue)->glonal_index = -1;
    }

    if (oldValue)
        *oldValue = _newValue;
    else
    {
        ht_put(vm->globals, name, &_newValue);
        vm->global_cache->slots[index] = ht_get(vm->globals, name);
    }
    if (IS_FUN(_newValue) && AS_FUN(_newValue)->name &&
        strcmp(AS_FUN(_newValue)->name, name) == 0)
    {
        AS_FUN(_newValue)->global_valid = true;
        AS_FUN(_newValue)->glonal_index = index;
    }
    VM_DISPATCH_SAFE();
}

OP_LOAD_GLOBAL:
{
    uint8_t index = code[pc++];
    if (function && function->global_valid &&
        function->glonal_index == index &&
        function->globals == vm->globals)
    {
        vm->stack[vm->sp++] = NEW_OBJ((Object *)function);
        VM_DISPATCH_SAFE();
    }

    char *name = string_get(vm->names, index);
    if (function && function->global_valid &&
        function->globals == vm->globals &&
        function->name && strcmp(function->name, name) == 0)
    {
        vm->stack[vm->sp++] = NEW_OBJ((Object *)function);
        VM_DISPATCH_SAFE();
    }

    Value *_value = global_slot(vm, index, name);
    if (_value == NULL)
    {
        nilValue = NEW_NIL();
        _value = &nilValue;
    }
    PUSH(*_value);
    VM_DISPATCH_SAFE();
}

OP_LOAD_LOCAL:
{
    uint8_t local = code[pc++];
    int slot = vm->bp + local;
    if (vm->comp_sp > 0)
        slot = resolve_localSlot(vm, local);

    if (vm->comp_sp == 0)
    {
        Value *left = &vm->stack[slot];

        if (pc + 3 < length &&
            code[pc] == OP_UNARY &&
            (code[pc + 1] == 5 || code[pc + 1] == 6) &&
            code[pc + 2] == OP_STORE_LOCAL &&
            code[pc + 3] == local &&
            IS_NUM(*left))
        {
            left->data.number += code[pc + 1] == 5 ? 1.0 : -1.0;
            pc += 4;
            VM_DISPATCH_SAFE();
        }

        if (pc + 5 < length &&
            code[pc] == OP_LOAD_LOCAL &&
            code[pc + 2] == OP_BINARY &&
            code[pc + 3] == 0 &&
            code[pc + 4] == OP_STORE_LOCAL &&
            code[pc + 5] == local)
        {
            Value right = vm->stack[vm->bp + code[pc + 1]];
            if (IS_NUM(*left) && IS_NUM(right))
            {
                left->data.number += AS_NUM(right);
                pc += 6;
                VM_DISPATCH_SAFE();
            }
            if (IS_LIST(*left))
            {
                vm_listAppendValue(AS_LIST(*left), right);
                pc += 6;
                VM_DISPATCH_SAFE();
            }
        }
    }

    vm->stack[vm->sp++] = vm->stack[slot];
    VM_DISPATCH_SAFE();
}

OP_LOAD_SUPER:
{
    if (!function->is_method || function->instance == NULL)
        vm_error(vm, "super is only available inside object methods.");

    Object *super_obj = add_obj(vm, new_map(ht_create(sizeof(Value)), true));
    PiMap *super = (PiMap *)super_obj;
    super->proto = function->owner ? ((PiMap *)function->owner)->proto : NULL;
    super->super_instance = function->instance;
    push_stack(vm, NEW_OBJ(super_obj));
    VM_DISPATCH_SAFE();
}

OP_STORE_LOCAL:
{
    uint8_t local = code[pc++];
    int slot = vm->bp + local;
    if (vm->comp_sp > 0)
        slot = resolve_localSlot(vm, local);
    vm_storeLocalSlot(vm, slot, POP());
    VM_DISPATCH_SAFE();
}

OP_POP:
{
    remove_upvalue(vm, vm->sp - 1);
    Value value = POP();
    (void)value;
    VM_DISPATCH_SAFE();
}

OP_POP_N:
{
    uint8_t n = code[pc++];
    for (int i = 0; i < n; i++)
    {
        remove_upvalue(vm, vm->sp - 1);
        POP();
    }
    VM_DISPATCH_SAFE();
}

OP_DUP_TOP:
{
    Value value = peek_stack(vm);
    PUSH(value);
    VM_DISPATCH_SAFE();
}

OP_JUMP_IF_FALSE:
{
    int offset = (int16_t)((code[pc] << 8) | code[pc + 1]);
    Value value = POP();
    bool condition = IS_BOOL(value) ? AS_BOOL(value) : as_bool(value);
    if (!condition)
        pc += offset - 1;
    else
        pc += 2;
    VM_DISPATCH_SAFE();
}

OP_JUMP:
{
    int offset = (int16_t)((code[pc] << 8) | code[pc + 1]);
    pc += offset - 1;
    VM_DISPATCH_SAFE();
}

OP_JUMP_IF_TRUE:
{
    int offset = (int16_t)((code[pc] << 8) | code[pc + 1]);
    Value value = POP();
    bool condition = IS_BOOL(value) ? AS_BOOL(value) : as_bool(value);
    if (condition)
        pc += offset - 1;
    else
        pc += 2;
    VM_DISPATCH_SAFE();
}

OP_COMPARE:
{
    uint8_t op = code[pc++];

    Value right = vm->stack[vm->sp - 1];
    Value left = vm->stack[vm->sp - 2];

    if (op <= 5 && IS_NUM(left) && IS_NUM(right))
    {
        double l = AS_NUM(left);
        double r = AS_NUM(right);
        bool result = false;
        switch (op)
        {
        case 0:
            result = (l == r);
            break;
        case 1:
            result = (l != r);
            break;
        case 2:
            result = (l > r);
            break;
        case 3:
            result = (l < r);
            break;
        case 4:
            result = (l >= r);
            break;
        case 5:
            result = (l <= r);
            break;
        }
        if (code[pc] == OP_JUMP_IF_FALSE)
        {
            int offset = (int16_t)((code[pc + 1] << 8) | code[pc + 2]);
            vm->sp -= 2;
            pc = result ? pc + 3 : pc + offset;
            VM_DISPATCH_SAFE();
        }
        vm->sp--;
        vm->stack[vm->sp - 1] = NEW_BOOL(result);
        VM_DISPATCH_SAFE();
    }

    right = POP();
    left = POP();

    if (op <= 5 && is_numeric(left) && is_numeric(right))
    {
        double l = as_number(left);
        double r = as_number(right);
        bool result = false;
        switch (op)
        {
        case 0:
            result = (l == r);
            break;
        case 1:
            result = (l != r);
            break;
        case 2:
            result = (l > r);
            break;
        case 3:
            result = (l < r);
            break;
        case 4:
            result = (l >= r);
            break;
        case 5:
            result = (l <= r);
            break;
        }
        PUSH(NEW_BOOL(result));
        VM_DISPATCH_SAFE();
    }

    if (IS_BOOL(left) && IS_BOOL(right) && (op == 0 || op == 1))
    {
        bool result = (AS_BOOL(left) == AS_BOOL(right));
        PUSH(NEW_BOOL(op == 0 ? result : !result));
        VM_DISPATCH_SAFE();
    }

    if (IS_NIL(left) && IS_NIL(right) && (op == 0 || op == 1))
    {
        PUSH(NEW_BOOL(op == 0));
        VM_DISPATCH_SAFE();
    }

    if (!IS_OBJ(left) && !IS_OBJ(right) && (op == 0 || op == 1))
    {
        PUSH(NEW_BOOL(op == 1));
        VM_DISPATCH_SAFE();
    }

    if (op <= 5 && IS_STRING(left) && IS_STRING(right))
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
            result = false;
            break;
        }
        PUSH(NEW_BOOL(result));
        VM_DISPATCH_SAFE();
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
        case 0:
            result = set_equals(left_set, right_set);
            break;
        case 1:
            result = !set_equals(left_set, right_set);
            break;
        case 2:
            result = set_isSubset(right_set, left_set) && set_size(left_set) != set_size(right_set);
            break;
        case 3:
            result = set_isSubset(left_set, right_set) && set_size(left_set) != set_size(right_set);
            break;
        case 4:
            result = set_isSubset(right_set, left_set);
            break;
        case 5:
            result = set_isSubset(left_set, right_set);
            break;
        }
        push_stack(vm, NEW_BOOL(result));
        VM_DISPATCH_SAFE();
    }

    if (op == 6)
    {
        if (!IS_OBJ(right) || !is_iterable(AS_OBJ(right)))
            vm_error(vm, "Right operand of 'in' must be iterable.");

        bool result = false;
        switch (OBJ_TYPE(right))
        {
        case OBJ_LIST:
        {
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
                break;
            result = (strstr(AS_STRING(right)->chars, AS_STRING(left)->chars) != NULL);
            break;
        }
        case OBJ_MAP:
        {
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
                break;
            PiRange *range = AS_RANGE(right);
            double num = AS_NUM(left);
            double start = range->start;
            double end = range->end;
            double step = range->step;
            if (step > 0)
                result = (num >= start && num <= end && fmod(num - start, step) < 1e-10);
            else if (step < 0)
                result = (num <= start && num >= end && fmod(start - num, -step) < 1e-10);
            else
                result = (num == start);
            break;
        }
        default:
        {
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
        VM_DISPATCH_SAFE();
    }

    if (op <= 1)
    {
        bool result = false;
        if (IS_MAP(left) || IS_MAP(right))
        {
            if (try_overloadedEquals(vm, left, right, &result))
            {
                push_stack(vm, NEW_BOOL(op == 0 ? result : !result));
                VM_DISPATCH_SAFE();
            }
        }
        if (IS_OBJ(left) && IS_OBJ(right) && AS_OBJ(left) == AS_OBJ(right))
        {
            push_stack(vm, NEW_BOOL(op == 0));
            VM_DISPATCH_SAFE();
        }
        Value l = TO_PRIM_NUM(left);
        Value r = TO_PRIM_NUM(right);
        result = (compare(l, r) == 0);
        push_stack(vm, NEW_BOOL(op == 0 ? result : !result));
        VM_DISPATCH_SAFE();
    }

    {
        int cmp = 0;
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
    }
    VM_DISPATCH_SAFE();
}

OP_BINARY:
{
    uint8_t op = code[pc++];
    Value right = vm->stack[vm->sp - 1];
    Value left = vm->stack[vm->sp - 2];

    if (op <= 4 && IS_NUM(left) && IS_NUM(right))
    {
        double l = AS_NUM(left);
        double r = AS_NUM(right);
        vm->sp--;
        switch (op)
        {
        case 0:
            vm->stack[vm->sp - 1] = NEW_NUM(l + r);
            break;
        case 1:
            vm->stack[vm->sp - 1] = NEW_NUM(l - r);
            break;
        case 2:
            vm->stack[vm->sp - 1] = NEW_NUM(l * r);
            break;
        case 3:
            vm->stack[vm->sp - 1] = NEW_NUM(r == 0.0 ? INFINITY : l / r);
            break;
        case 4:
        {
            int ir = (int)r;
            vm->stack[vm->sp - 1] = ir == 0 ? NEW_NAN() : NEW_NUM((int)l % ir);
            break;
        }
        }
        VM_DISPATCH_SAFE();
    }
    if (op <= 13 && IS_NUM(left) && IS_NUM(right))
    {
        vm->sp--;
        vm->stack[vm->sp - 1] = op_binaryNum(op, AS_NUM(left), AS_NUM(right));
        VM_DISPATCH_SAFE();
    }

    right = pop_stack(vm);
    left = pop_stack(vm);

    if (op <= 13 && is_numeric(left) && is_numeric(right))
    {
        PUSH(op_binaryNum(op, as_number(left), as_number(right)));
        VM_DISPATCH_SAFE();
    }

    if (op != 5 && op != 6 && op != 15 && IS_MAP(left) && MAP_HAS_FLAG(AS_MAP(left), MAP_HAS_COMPUTE))
    {
        Value computed = NEW_NIL();
        if (try_callCompute(vm, left, op, true, right, &computed))
        {
            push_stack(vm, computed);
            VM_DISPATCH_SAFE();
        }
    }

    if (op != 5 && op != 6 && op != 15 && IS_MAP(right) && MAP_HAS_FLAG(AS_MAP(right), MAP_HAS_RCOMPUTE))
    {
        Value computed = NEW_NIL();
        if (try_callReflectedCompute(vm, right, op, left, &computed))
        {
            push_stack(vm, computed);
            VM_DISPATCH_SAFE();
        }
    }

    switch (op)
    {
    case 0: // +
    {
        if (IS_NAN(left) || IS_NAN(right))
        {
            push_stack(vm, NEW_NUM(NAN));
            break;
        }

        if (IS_LIST(left))
        {
            PiList *list = AS_LIST(left);
            vm_listAppendValue(list, right);
            push_stack(vm, left);
            break;
        }

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
            push_stack(vm, NEW_OBJ(add_obj(vm, new_tuple(result))));
            break;
        }

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
    case 1: // -
    {
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
        {
            Value _left = TO_PRIM(vm, left, false);
            Value _right = TO_PRIM(vm, right, false);
            if (is_numeric(_left) && is_numeric(_right))
            {
                push_stack(vm, NEW_NUM(as_number(_left) - as_number(_right)));
                break;
            }
        }
        vm_error(vm, "Unsupported operand types for binary operator [-].");
        break;
    }
    case 2: // *
    {
        if (IS_LIST(left) && IS_LIST(right))
        {
            PiList *A = AS_LIST(left);
            PiList *B = AS_LIST(right);
            int m, n, n_right, p;
            if (!list_getNumericMatrixShape(A, &m, &n) ||
                !list_getNumericMatrixShape(B, &n_right, &p))
                vm_error(vm, "Matrix multiplication requires rectangular numeric lists.");
            if (n != n_right)
                vm_error(vm, "Matrix multiplication dimension mismatch.");
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
            push_stack(vm, NEW_OBJ(res_obj));
            break;
        }
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
            push_stack(vm, NEW_OBJ(add_obj(vm, new_tuple(result))));
            break;
        }
        if (is_numeric(left) && IS_TUPLE(right))
        {
            int repeatCount = (int)as_number(left);
            PiTuple *tuple = AS_TUPLE(right);
            list_t *result = list_create(sizeof(Value));
            for (int i = 0; i < repeatCount; i++)
                list_addAll(result, tuple->items);
            push_stack(vm, NEW_OBJ(add_obj(vm, new_tuple(result))));
            break;
        }
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
        {
            Value _left = TO_PRIM(vm, left, false);
            Value _right = TO_PRIM(vm, right, false);
            if (is_numeric(_left) && is_numeric(_right))
            {
                push_stack(vm, NEW_NUM(as_number(_left) * as_number(_right)));
                break;
            }
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
        {
            Value _left = TO_PRIM(vm, left, false);
            Value _right = TO_PRIM(vm, right, false);
            double denom = as_number(_right);
            push_stack(vm, NEW_NUM(denom == 0.0 ? INFINITY : as_number(_left) / denom));
        }
        break;
    }
    case 4: // %
    {
        Value _left = TO_PRIM(vm, left, false);
        Value _right = TO_PRIM(vm, right, false);
        int denom = (int)as_number(_right);
        push_stack(vm, denom == 0 ? NEW_NAN() : NEW_NUM((int)as_number(_left) % denom));
        break;
    }
    case 5:
        push_stack(vm, NEW_BOOL(as_bool(left) && as_bool(right)));
        break;
    case 6:
        push_stack(vm, NEW_BOOL(as_bool(left) || as_bool(right)));
        break;
    case 7:
    {
        Value _left = TO_PRIM(vm, left, false);
        Value _right = TO_PRIM(vm, right, false);
        push_stack(vm, NEW_NUM(pow(as_number(_left), as_number(_right))));
        break;
    }
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    {
        if ((op == 8 || op == 9 || op == 10) && IS_SET(left) && IS_SET(right))
        {
            push_stack(vm, NEW_OBJ(add_obj(vm, set_ops(vm, AS_SET(left), AS_SET(right), op))));
            break;
        }
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
        {
            Value _left = TO_PRIM(vm, left, false);
            Value _right = TO_PRIM(vm, right, false);
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
        }
        const char *op_names[] = {"&", "|", "^", "<<", ">>", ">>>"};
        vm_errorf(vm, "Unsupported operand types for binary operator [%s].", op_names[op - 8]);
        break;
    }
    case 14:
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
        Value *a = (Value *)l_list->items->data;
        Value *b = (Value *)r_list->items->data;
        double result = 0.0;
        for (int i = 0; i < l_size; i++)
            result += as_number(a[i]) * as_number(b[i]);
        push_stack(vm, NEW_NUM(result));
        break;
    }
    case 15:
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
                goto binary_is_done;
            }
            map = map->proto;
        }
        push_stack(vm, NEW_BOOL(false));
    binary_is_done:;
        break;
    }
    default:
        vm_errorf(vm, "Unknown binary opcode: [%d]", op);
        break;
    }
    VM_DISPATCH_SAFE();
}

OP_UNARY:
{
    uint8_t op = code[pc++];
    Value operand = vm->stack[vm->sp - 1];

    if (op == 7)
    {
        vm->stack[vm->sp - 1] = NEW_OBJ(add_obj(vm, new_pistring(strdup(type_name(operand)))));
        VM_DISPATCH_SAFE();
    }

    if (op == 4)
    {
        if (!IS_OBJ(operand))
            vm_error(vm, "Operator '#' requires a collection.");
        switch (OBJ_TYPE(operand))
        {
        case OBJ_LIST:
            vm->stack[vm->sp - 1] = NEW_NUM(list_size(AS_LIST(operand)->items));
            break;
        case OBJ_TENSOR:
            vm->stack[vm->sp - 1] = NEW_NUM(AS_TENSOR(operand)->ndim == 0 ? 0 : AS_TENSOR(operand)->shape[0]);
            break;
        case OBJ_STRING:
            vm->stack[vm->sp - 1] = NEW_NUM(AS_STRING(operand)->length);
            break;
        case OBJ_MAP:
            vm->stack[vm->sp - 1] = NEW_NUM(map_size(AS_MAP(operand)));
            break;
        default:
            vm_error(vm, "Unsupported operand type for '#' operator.");
        }
        VM_DISPATCH_SAFE();
    }

    if (IS_NUM(operand))
    {
        double n = AS_NUM(operand);
        switch (op)
        {
        case 0:
            vm->stack[vm->sp - 1] = NEW_NUM(n);
            break;
        case 1:
            vm->stack[vm->sp - 1] = NEW_NUM(-n);
            break;
        case 2:
            vm->stack[vm->sp - 1] = NEW_BOOL(n == 0.0);
            break;
        case 3:
            vm->stack[vm->sp - 1] = NEW_NUM(~(int)n);
            break;
        case 5:
            vm->stack[vm->sp - 1] = NEW_NUM(n + 1.0);
            break;
        case 6:
            vm->stack[vm->sp - 1] = NEW_NUM(n - 1.0);
            break;
        default:
            vm_error(vm, "Unknown unary operator.");
        }
        VM_DISPATCH_SAFE();
    }

    if (IS_BOOL(operand) && op == 2)
    {
        vm->stack[vm->sp - 1] = NEW_BOOL(!AS_BOOL(operand));
        VM_DISPATCH_SAFE();
    }

    if (IS_NIL(operand) && op == 2)
    {
        vm->stack[vm->sp - 1] = NEW_BOOL(true);
        VM_DISPATCH_SAFE();
    }

    if (IS_MAP(operand) && (op == 0 || op == 1 || op == 3))
    {
        Value computed = NEW_NIL();
        if (try_callCompute(vm, operand, 100 + op, false, NEW_NIL(), &computed))
        {
            vm->stack[vm->sp - 1] = computed;
            VM_DISPATCH_SAFE();
        }
    }

    if (op == 2)
    {
        vm->stack[vm->sp - 1] = NEW_BOOL(!as_bool(operand));
        VM_DISPATCH_SAFE();
    }

    {
        double n = as_number(TO_PRIM_NUM(operand));
        switch (op)
        {
        case 0:
            vm->stack[vm->sp - 1] = NEW_NUM(n);
            break;
        case 1:
            vm->stack[vm->sp - 1] = NEW_NUM(-n);
            break;
        case 3:
            vm->stack[vm->sp - 1] = NEW_NUM(~(int)n);
            break;
        case 5:
            vm->stack[vm->sp - 1] = NEW_NUM(n + 1.0);
            break;
        case 6:
            vm->stack[vm->sp - 1] = NEW_NUM(n - 1.0);
            break;
        default:
            vm_error(vm, "Unknown unary operator.");
        }
    }
    VM_DISPATCH_SAFE();
}

OP_CALL_FUNCTION:
{
    uint8_t num_args = code[pc++];
    int args_base = vm->sp - num_args;
    int obj_slot = args_base - 1;

    if (obj_slot < 0)
        vm_errorf(vm, "Invalid function call: expected a callable object");

    Value callee = vm->stack[obj_slot];
    vm->error_pc = vm->pc;
    vm->pc = pc;

    if (IS_FUN(callee))
    {
        Function *callee_fn = AS_FUN(callee);

        if (callee_fn->is_native && callee_fn->is_method &&
            callee_fn->native == pi_push && callee_fn->instance &&
            callee_fn->instance->type == OBJ_LIST)
        {
            list_t *items = ((PiList *)callee_fn->instance)->items;
            for (uint8_t i = 0; i < num_args; i++)
                list_add(items, &vm->stack[args_base + i]);
            vm->sp = obj_slot;
            PUSH(NEW_NUM(items->size));
            VM_DISPATCH_SAFE();
        }

        size_t param_count = (!callee_fn->is_native && callee_fn->params)
                                 ? (size_t)callee_fn->arity
                                 : 0;
        bool param_this = callee_fn->is_method && callee_fn->param_names &&
                          (size_t)callee_fn->param_names->size + 1 == param_count;
        size_t _param_count = param_count - (param_this ? 1 : 0);

        if (!callee_fn->is_native &&
            (!callee_fn->is_method || callee_fn->instance != NULL) &&
            !callee_fn->need_args && !callee_fn->need_kwargs &&
            (size_t)num_args == _param_count)
        {
            if (vm->frame_sp >= STACK_MAX)
                vm_error(vm, "[frame] Stack overflow.");

            bool self_recursive = callee_fn == function;
            Frame *frame = &vm->frames[vm->frame_sp++];
            frame->pc = pc;
            frame->sp = obj_slot;
            frame->bp = vm->bp;
            frame->ip = vm->ip;
            frame->iters_top = vm->iter_sp;
            frame->is_recursive = self_recursive;
            frame->global_cache = vm->global_cache;

            if (!self_recursive)
            {
                frame->code = vm->code;
                frame->constants = vm->constants;
                frame->names = vm->names;
                frame->instrs = vm->instrs;
                frame->globals = vm->globals;
                frame->function = function;

                vm->function = (Object *)callee_fn;
                vm->code = callee_fn->body->data;
                if (callee_fn->constants)
                    vm->constants = callee_fn->constants;
                if (callee_fn->names)
                    vm->names = callee_fn->names;
                if (callee_fn->instrs)
                    vm->instrs = callee_fn->instrs;
                if (callee_fn->globals)
                    vm->globals = callee_fn->globals;
                vm->global_cache = &callee_fn->body->global_cache;
            }

            vm->pc = 0;
            vm->ip = 0;
            vm->bp = obj_slot;

            if (callee_fn->is_method)
            {
                vm->stack[vm->bp] = NEW_OBJ(callee_fn->instance);
                if (param_this)
                {
                    for (uint8_t i = 0; i < num_args; i++)
                        vm->stack[vm->bp + (int)i + 1] = vm->stack[args_base + i];
                }
                int aux_base = vm->bp + (int)param_count + (param_this ? 0 : 1);
                vm->stack[aux_base] = NEW_NIL();
                vm->stack[aux_base + 1] = NEW_NIL();
                vm->sp = vm->bp + (int)param_count + (param_this ? 2 : 3);
            }
            else
            {
                for (uint8_t i = 0; i < num_args; i++)
                    vm->stack[vm->bp + i] = vm->stack[args_base + i];
                int aux_base = vm->bp + (int)param_count;
                vm->stack[aux_base] = NEW_NIL();
                vm->stack[aux_base + 1] = NEW_NIL();
                vm->sp = vm->bp + (int)param_count + 2;
            }

            code = (uint8_t *)vm->code->data;
            constants_data = (Value *)vm->constants->data;
            length = vm->code->size;
            pc = vm->pc;
            function = callee_fn;
            VM_DISPATCH_SAFE();
        }
    }

    Value stack_args[8];
    Value *args = num_args <= 8 ? stack_args : (Value *)malloc(num_args * sizeof(Value));
    if (num_args > 8 && !args)
        vm_error(vm, "Memory allocation failed for argument list.");

    for (int i = num_args - 1; i >= 0; i--)
        args[i] = POP();
    callee = POP();

    if (IS_FUN(callee))
    {
        Value result = call_func(vm, AS_FUN(callee), num_args, args, NEW_NIL());
        PUSH(result);
    }
    else if (IS_MAP(callee))
    {
        PiMap *map = AS_MAP(callee);
        Value result;
        if (MAP_HAS_FLAG(map, MAP_IS_INSTANCE))
        {
            if (object_instanceCall(vm, map, num_args, args, NEW_NIL(), &result))
                PUSH(result);
            else
            {
                if (num_args > 8)
                    free(args);
                vm_error(vm, "Attempt to call an Object instance with no `call` method.");
            }
        }
        else
        {
            result = NEW_OBJ(construct(vm, map, num_args, args, NEW_NIL()));
            PUSH(result);
        }
    }
    else
    {
        if (num_args > 8)
            free(args);
        vm_error(vm, "Attempt to call a non-function value.");
    }

    if (num_args > 8)
        free(args);
    VM_DISPATCH_SAFE();
}

OP_CALL_FUNCTION_KW:
{
    uint8_t num_args = code[pc++];
    Value kw_args = pop_stack(vm);
    if (!IS_OBJ(kw_args) || OBJ_TYPE(kw_args) != OBJ_MAP)
        vm_error(vm, "Named arguments must be a map.");

    Value stack_args[8];
    Value *args = num_args <= 8 ? stack_args : (Value *)malloc(num_args * sizeof(Value));
    if (num_args > 8 && !args)
        vm_error(vm, "Memory allocation failed for argument list.");

    for (int i = num_args - 1; i >= 0; i--)
        args[i] = pop_stack(vm);
    Value callee = pop_stack(vm);
    Value result = NEW_NIL();

    if (IS_FUN(callee))
    {
        vm->error_pc = vm->pc;
        vm->pc = pc;
        result = call_func(vm, AS_FUN(callee), num_args, args, kw_args);
        if (IS_OBJ(result))
            add_obj(vm, AS_OBJ(result));
    }
    else if (IS_MAP(callee))
    {
        PiMap *map = AS_MAP(callee);
        if (MAP_HAS_FLAG(map, MAP_IS_INSTANCE))
        {
            if (!object_instanceCall(vm, map, num_args, args, kw_args, &result))
            {
                if (num_args > 8)
                    free(args);
                vm_error(vm, "Attempt to call an Object instance.");
            }
        }
        else
            result = NEW_OBJ(construct(vm, AS_MAP(callee), num_args, args, kw_args));
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
    VM_DISPATCH_SAFE();
}

OP_CALL_SPREAD:
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
    vm->error_pc = vm->pc;
    vm->pc = pc;
    push_stack(vm, call_withArgList(vm, callee, AS_LIST(arg_list_value), kw_args, has_named));
    VM_DISPATCH_SAFE();
}

OP_PUSH_ITER:
{
    Value iterable = POP();
    if (!IS_OBJ(iterable) || !is_iterable(AS_OBJ(iterable)))
        vm_error(vm, "Error: Object is not iterable.");
    iter = AS_OBJ(iterable);
    iter_reset(iter);
    if (vm->iter_sp + 1 >= STACK_MAX)
        vm_error(vm, "[iter] Iterator stack overflow.");
    vm->iters[++vm->iter_sp] = iter;
    VM_DISPATCH_SAFE();
}

OP_LOOP:
{
    uint16_t encoded = (code[pc] << 8);
    encoded |= code[pc + 1];
    bool pair_loop = (encoded & OP_LOOP_TARGET_PAIR_FLAG) != 0;
    uint16_t address = encoded & OP_LOOP_OFFSET_MASK;

    if (vm->iter_sp == -1)
        vm_error(vm, "Error: No active iterator.");
    iter = vm->iters[vm->iter_sp];

    if (iter->type == OBJ_LIST)
    {
        PiList *list = (PiList *)iter;
        list_t *items = list->items;
        if (list->current < items->size)
        {
            int index = list->current;
            Value value = ((Value *)items->data)[list->current++];
            if (IS_OBJ(value))
                add_obj(vm, AS_OBJ(value));
            if (pair_loop)
            {
                push_stack(vm, NEW_NIL());
                push_stack(vm, make_iterPair(vm, NEW_NUM(index), value));
                pc += 2;
            }
            else
            {
                bool fused_store = vm_storeLoopValueIfLocal(vm, code, &pc, value);
                if (!fused_store)
                {
                    PUSH(value);
                    pc += 2;
                }
            }
        }
        else
        {
            vm->iter_sp--;
            pc += address - 1;
        }
        VM_DISPATCH_SAFE();
    }

    if (iter->type == OBJ_RANGE)
    {
        PiRange *range = (PiRange *)iter;
        double value = range->current;
        bool has_next = range->step > 0 ? value < range->end : value > range->end;
        if (has_next)
        {
            range->current = value + range->step;
            if (pair_loop)
            {
                push_stack(vm, NEW_NIL());
                push_stack(vm, make_iterPair(vm, NEW_NUM((value - range->start) / range->step), NEW_NUM(value)));
                pc += 2;
            }
            else
            {
                bool fused_store = vm_storeLoopValueIfLocal(vm, code, &pc, NEW_NUM(value));
                if (!fused_store)
                {
                    PUSH(NEW_NUM(value));
                    pc += 2;
                }
            }
        }
        else
        {
            vm->iter_sp--;
            pc += address - 1;
        }
        VM_DISPATCH_SAFE();
    }

    if (iter->type == OBJ_TUPLE)
    {
        PiTuple *tuple = (PiTuple *)iter;
        list_t *items = tuple->items;
        if (tuple->current < items->size)
        {
            int index = tuple->current;
            Value value = ((Value *)items->data)[tuple->current++];
            if (IS_OBJ(value))
                add_obj(vm, AS_OBJ(value));
            if (pair_loop)
            {
                push_stack(vm, NEW_NIL());
                push_stack(vm, make_iterPair(vm, NEW_NUM(index), value));
                pc += 2;
            }
            else
            {
                bool fused_store = vm_storeLoopValueIfLocal(vm, code, &pc, value);
                if (!fused_store)
                {
                    push_stack(vm, value);
                    pc += 2;
                }
            }
        }
        else
        {
            vm->iter_sp--;
            pc += address - 1;
        }
        VM_DISPATCH_SAFE();
    }

    if (iter->type == OBJ_STRING)
    {
        PiString *string = (PiString *)iter;
        if (string->current < string->length)
        {
            int index = string->current;
            char *chars = malloc(2);
            if (!chars)
                vm_error(vm, "Out of memory while iterating string.");
            chars[0] = string->chars[string->current++];
            chars[1] = '\0';
            Value value = NEW_OBJ(add_obj(vm, new_pistring(chars)));
            if (pair_loop)
            {
                push_stack(vm, NEW_NIL());
                push_stack(vm, make_iterPair(vm, NEW_NUM(index), value));
            }
            else
                push_stack(vm, value);
            pc += 2;
        }
        else
        {
            vm->iter_sp--;
            pc += address - 1;
        }
        VM_DISPATCH_SAFE();
    }

    if (iter_hasNext(iter))
    {
        if (iter->type == OBJ_MAP)
        {
            PiMap *map = (PiMap *)iter;
            ht_next(&map->it);
            if (pair_loop)
            {
                Value value = *(Value *)map->it.value;
                if (IS_OBJ(value))
                    add_obj(vm, AS_OBJ(value));
                push_stack(vm, NEW_NIL());
                push_stack(vm, make_iterPair(vm, NEW_OBJ(add_obj(vm, new_pistring(strdup(map->it.key)))), value));
            }
            else
                push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(strdup(map->it.key)))));
        }
        else
        {
            int index = 0;
            if (pair_loop)
            {
                switch (iter->type)
                {
                case OBJ_TENSOR:
                    index = ((PiTensor *)iter)->current;
                    break;
                case OBJ_SET:
                    index = ((PiSet *)iter)->current;
                    break;
                default:
                    break;
                }
            }
            Value value = iter_next(iter);
            if (IS_OBJ(value))
                add_obj(vm, AS_OBJ(value));
            if (pair_loop)
            {
                push_stack(vm, NEW_NIL());
                push_stack(vm, make_iterPair(vm, NEW_NUM(index), value));
            }
            else
                push_stack(vm, value);
        }
        pc += 2;
    }
    else
    {
        vm->iter_sp--;
        pc += address - 1;
    }
    VM_DISPATCH_SAFE();
}

OP_POP_ITER:
{
    if (vm->iter_sp != -1)
        iter = vm->iters[vm->iter_sp--];
    VM_DISPATCH_SAFE();
}

OP_PUSH_RANGE:
{
    Value step = pop_stack(vm);
    Value end = pop_stack(vm);
    Value start = pop_stack(vm);
    if (!IS_NUM(start) || !IS_NUM(end))
        vm_error(vm, "PiRange `start` and `end` must be numbers.");
    if (!IS_NIL(step) && !IS_NUM(step))
        vm_error(vm, "PiRange `step` must be nil or a number.");
    double _start = as_number(start);
    double _end = as_number(end);
    double _step = IS_NIL(step) ? ((_start < _end) ? 1.0 : -1.0) : as_number(step);
    push_stack(vm, NEW_OBJ(add_obj(vm, new_range(_start, _end, _step))));
    VM_DISPATCH_SAFE();
}

OP_PUSH_LIST:
{
    int numElements = (code[pc++] << 8) | code[pc++];
    list_t *list = list_create(sizeof(Value));

    if (numElements == 0)
    {
        Object *l_obj = add_obj(vm, new_list(list));
        push_stack(vm, NEW_OBJ(l_obj));
        VM_DISPATCH_SAFE();
    }

    int element_base = vm->sp - numElements;
    for (int i = 0; i < numElements; i++)
    {
        Value v = vm->stack[element_base + i];
        list_add(list, &v);
    }

    Object *l_obj = add_obj(vm, new_list(list));
    set_stackTop(vm, element_base);
    push_stack(vm, NEW_OBJ(l_obj));
    VM_DISPATCH_SAFE();
}

OP_PUSH_SET:
{
    int numElements = (code[pc++] << 8) | code[pc++];
    PiSet *set = (PiSet *)new_set();

    if (numElements == 0)
    {
        push_stack(vm, NEW_OBJ(add_obj(vm, (Object *)set)));
        VM_DISPATCH_SAFE();
    }

    int element_base = vm->sp - numElements;
    for (int i = 0; i < numElements; i++)
    {
        Value element = vm->stack[element_base + i];
        if (IS_OBJ(element))
            add_obj(vm, AS_OBJ(element));
        set_add(set, element);
    }
    Object *set_obj = add_obj(vm, (Object *)set);
    set_stackTop(vm, element_base);
    push_stack(vm, NEW_OBJ(set_obj));
    VM_DISPATCH_SAFE();
}

OP_PUSH_TUPLE:
{
    int numElements = (code[pc++] << 8) | code[pc++];
    list_t *items = list_create(sizeof(Value));
    if (numElements > 0)
    {
        int element_base = vm->sp - numElements;
        for (int i = 0; i < numElements; i++)
        {
            Value element = vm->stack[element_base + i];
            if (IS_OBJ(element))
                add_obj(vm, AS_OBJ(element));
            list_add(items, &element);
        }
        set_stackTop(vm, element_base);
    }
    push_stack(vm, NEW_OBJ(add_obj(vm, new_tuple(items))));
    VM_DISPATCH_SAFE();
}

OP_COMP_APPEND:
{
    int local = code[pc++];
    Value value = pop_stack(vm);
    int slot = resolve_localSlot(vm, local);
    if (slot < vm->bp || slot >= vm->sp)
        vm_error(vm, "List append local expects a list target.");
    Value target = vm->stack[slot];
    if (!IS_LIST(target))
        vm_error(vm, "List append local expects a list target.");
    vm_listAppendValue(AS_LIST(target), value);
    VM_DISPATCH_SAFE();
}

OP_COMP_BEGIN:
{
    int local_base = code[pc++];
    if (vm->comp_sp >= COMP_MAX)
        vm_error(vm, "Too many nested list comprehensions.");
    list_t *list = list_create(sizeof(Value));
    Object *l_obj = add_obj(vm, new_list(list));
    PiList *plist = (PiList *)l_obj;
    plist->is_numeric = true;
    push_stack(vm, NEW_OBJ(l_obj));
    int top = vm->comp_sp++;
    vm->comp_frames[top].base = vm->sp - 1;
    vm->comp_frames[top].local_base = local_base;
    vm->comp_frames[top].bp = vm->bp;
    VM_DISPATCH_SAFE();
}

OP_COMP_END:
{
    if (vm->comp_sp <= 0)
        vm_error(vm, "List comprehension end without a matching begin.");
    if (!IS_LIST(peek_stack(vm)))
        vm_error(vm, "List comprehension end expects a list accumulator.");
    int top = vm->comp_sp - 1;
    if (vm->comp_frames[top].base != vm->sp - 1)
        vm_error(vm, "List comprehension stack is unbalanced.");
    vm->comp_sp--;
    VM_DISPATCH_SAFE();
}

OP_LIST_APPEND:
{
    Value value = pop_stack(vm);
    if (!IS_LIST(peek_stack(vm)))
        vm_error(vm, "List append expects a list target.");
    vm_listAppendValue(AS_LIST(peek_stack(vm)), value);
    VM_DISPATCH_SAFE();
}

OP_LIST_EXTEND:
{
    Value iterable = pop_stack(vm);
    if (!IS_LIST(peek_stack(vm)))
        vm_error(vm, "List extend expects a list target.");
    list_extendFromIterable(vm, AS_LIST(peek_stack(vm)), iterable);
    VM_DISPATCH_SAFE();
}

OP_PUSH_MAP:
{
    int numElements = code[pc++] << 8;
    numElements |= code[pc++];
    table_t *table = ht_create(sizeof(Value));
    bool has_compute = false, has_rcompute = false;
    int _sp = vm->sp - (numElements * 2);

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
    set_stackTop(vm, _sp);
    Object *map = add_obj(vm, new_map(table, false));
    PiMap *pimap = (PiMap *)map;
    MAP_SET_FLAG(pimap, MAP_HAS_COMPUTE, has_compute);
    MAP_SET_FLAG(pimap, MAP_HAS_RCOMPUTE, has_rcompute);
    push_stack(vm, NEW_OBJ(map));
    VM_DISPATCH_SAFE();
}

OP_MAP_SET:
{
    Value key = pop_stack(vm);
    Value value = pop_stack(vm);
    if (!IS_MAP(peek_stack(vm)))
        vm_error(vm, "Map set expects a map target.");
    if (!IS_STRING(key))
        vm_error(vm, "Map literal keys must be strings.");
    PiMap *map = AS_MAP(peek_stack(vm));
    if (ht_put(map->table, AS_CSTRING(key), &value))
        map_dirty(map);
    if (IS_FUN(value))
    {
        if (strcmp(AS_CSTRING(key), "compute") == 0)
            MAP_SET_FLAG(map, MAP_HAS_COMPUTE, true);
        else if (strcmp(AS_CSTRING(key), "rcompute") == 0)
            MAP_SET_FLAG(map, MAP_HAS_RCOMPUTE, true);
    }
    VM_DISPATCH_SAFE();
}

OP_MAP_EXTEND:
{
    Value source = pop_stack(vm);
    if (!IS_MAP(peek_stack(vm)))
        vm_error(vm, "Map extend expects a map target.");
    map_extendFromMap(vm, AS_MAP(peek_stack(vm)), source);
    VM_DISPATCH_SAFE();
}

OP_MAP_FINALIZE:
{
    int name_index = code[pc++];
    if (!IS_MAP(peek_stack(vm)))
        vm_error(vm, "Map finalize expects a map target.");
    PiMap *map = AS_MAP(peek_stack(vm));
    finalize_mapLiteral(vm, map);
    if (name_index != 0xFF && map->proto != NULL && map->intrinsic_name == NULL)
        map->intrinsic_name = strdup(string_get(vm->names, name_index));
    VM_DISPATCH_SAFE();
}

OP_PUSH_FUNCTION:
{
    int numParams = code[pc++];
    ObjCode *body = AS_CODE(pop_stack(vm));
    char *name = AS_CSTRING(pop_stack(vm));
    list_t *defaults = list_create(sizeof(Value));
    int param_base = vm->sp - numParams;
    for (int i = 0; i < numParams; i++)
    {
        Value param = vm->stack[param_base + i];
        list_add(defaults, &param);
    }
    set_stackTop(vm, param_base);
    Object *fn = new_func(name, body, defaults, NULL, NULL);
    ((Function *)fn)->need_args = body->need_args;
    ((Function *)fn)->need_kwargs = body->need_kwargs;
    ((Function *)fn)->constants = vm->constants;
    ((Function *)fn)->names = vm->names;
    ((Function *)fn)->instrs = vm->instrs;
    ((Function *)fn)->globals = vm->globals;
    push_stack(vm, NEW_OBJ(add_obj(vm, fn)));
    VM_DISPATCH_SAFE();
}

OP_PUSH_CLOSURE:
{
    int numParams = code[pc++];
    int numUpvalues = code[pc++];
    UpValue **upvalues = ALLOCATE(UpValue *, numUpvalues + 1);
    for (int i = 0; i < numUpvalues; i++)
    {
        bool is_local = as_bool(pop_stack(vm));
        int index = as_number(pop_stack(vm));
        UpValue *upvalue = is_local
                               ? capture_upvalue(vm, vm->bp + index)
                               : function->upvalues[index];
        if (upvalue)
            upvalue->ref_count++;
        upvalues[numUpvalues - i - 1] = upvalue;
    }
    upvalues[numUpvalues] = NULL;
    ObjCode *body = AS_CODE(pop_stack(vm));
    char *name = AS_CSTRING(pop_stack(vm));
    list_t *defaults = list_create(sizeof(Value));
    int param_base = vm->sp - numParams;
    for (int i = 0; i < numParams; i++)
    {
        Value param = vm->stack[param_base + i];
        list_add(defaults, &param);
    }
    set_stackTop(vm, param_base);
    Object *fun_obj = new_func(name, body, defaults, upvalues, NULL);
    ((Function *)fun_obj)->need_args = body->need_args;
    ((Function *)fun_obj)->need_kwargs = body->need_kwargs;
    ((Function *)fun_obj)->constants = vm->constants;
    ((Function *)fun_obj)->names = vm->names;
    ((Function *)fun_obj)->instrs = vm->instrs;
    ((Function *)fun_obj)->globals = vm->globals;
    push_stack(vm, NEW_OBJ(add_obj(vm, fun_obj)));
    VM_DISPATCH_SAFE();
}

OP_LOAD_UPVALUE:
{
    int index = code[pc++];
    if (function->upvalues == NULL || function->upvalues[index] == NULL)
        vm_error(vm, "Invalid method binding: closure lost its captured variables while binding a method.");
    UpValue *upValue = function->upvalues[index];
    push_stack(vm, upValue->index != -1 ? vm->stack[upValue->index] : upValue->value);
    VM_DISPATCH_SAFE();
}

OP_STORE_UPVALUE:
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
    VM_DISPATCH_SAFE();
}

OP_PUSH_SLICE:
{
    Value _step = pop_stack(vm);
    Value _end = pop_stack(vm);
    Value _start = pop_stack(vm);
    if (!IS_NUM(_start) || !IS_NUM(_end))
        vm_error(vm, "Slice start and end must be numbers");
    if (!IS_NIL(_step) && !IS_NUM(_step))
        vm_error(vm, "Slice step must be nil or a number");
    double step = IS_NIL(_step) ? 1.0 : as_number(_step);
    if (step == 0.0)
        vm_error(vm, "Slice step cannot be zero");
    push_stack(vm, NEW_OBJ(add_obj(vm, new_slice(as_number(_start), as_number(_end), step))));
    VM_DISPATCH_SAFE();
}

OP_GET_SLOT:
{
    uint8_t slot = code[pc++];
    Value container = vm->stack[vm->sp - 1];

    if (!IS_MAP(container) || !AS_MAP(container)->slots || slot >= AS_MAP(container)->slot_count)
        vm_error(vm, "Invalid slot access.");

    vm->stack[vm->sp - 1] = AS_MAP(container)->slots[slot];
    VM_DISPATCH_SAFE();
}

OP_SET_SLOT:
{
    uint8_t slot = code[pc++];
    Value container = pop_stack(vm);
    Value value = pop_stack(vm);

    if (!IS_MAP(container) || !AS_MAP(container)->slots || slot >= AS_MAP(container)->slot_count)
        vm_error(vm, "Invalid slot assignment.");

    AS_MAP(container)->slots[slot] = value;
    map_dirty(AS_MAP(container));
    VM_DISPATCH_SAFE();
}

OP_GET_ITEM:
OP_GET_MEMBER:
{
    bool bracket_access = current_op == OP_GET_ITEM;
    Value index;
    if (bracket_access)
        index = POP();
    else
    {
        uint16_t name_idx = (uint16_t)((code[pc++] << 8) | code[pc++]);
        index = constants_data[name_idx];
    }
    Value container = vm->stack[vm->sp - 1];
    Value method_result;

    if (!IS_OBJ(container))
        vm_error(vm, "Unsupported operand type for get item operator.\n");

    if (IS_SLICE(index) &&
        (OBJ_TYPE(container) == OBJ_LIST ||
         OBJ_TYPE(container) == OBJ_TUPLE ||
         OBJ_TYPE(container) == OBJ_STRING))
    {
        PiSlice *s = AS_SLICE(index);
        vm->stack[vm->sp - 1] = get_slice(AS_OBJ(container), s->start, s->stop, s->step);
        VM_DISPATCH_SAFE();
    }

    if (!bracket_access && IS_STRING(index) &&
        OBJ_TYPE(container) != OBJ_MAP &&
        OBJ_TYPE(container) != OBJ_MODULE)
    {
        char *method_name = AS_CSTRING(index);
        NativeMethod *method = pi_nativeMethodFor(OBJ_TYPE(container), method_name);
        if (method)
        {
            vm->stack[vm->sp - 1] = bind_nativeMethod(vm, AS_OBJ(container), method);
            VM_DISPATCH_SAFE();
        }
        char available_methods[512];
        pi_nativeMethodNames(OBJ_TYPE(container), available_methods, sizeof(available_methods));
        if (available_methods[0] != '\0')
            vm_errorf(vm, "Type '%s' has no method '%s'. Available methods: %s.", type_name(container), method_name, available_methods);
        else
            vm_errorf(vm, "Type '%s' has no method '%s'.", type_name(container), method_name);
    }

    switch (OBJ_TYPE(container))
    {
    case OBJ_TENSOR:
    {
        PiTensor *tensor = AS_TENSOR(container);
        if (tensor->ndim == 0)
            vm_error(vm, "Cannot index a scalar tensor.");
        int row = get_index(as_number(index), tensor->shape[0]);
        vm->stack[vm->sp - 1] = NEW_OBJ(add_obj(vm, tensor_rowAsList(tensor, row)));
        break;
    }
    case OBJ_LIST:
    {
        list_t *list = AS_LIST(container)->items;
        if (list->size == 0)
            vm->stack[vm->sp - 1] = NEW_NIL();
        else
        {
            int _index = (int)as_number(index);
            if (_index < 0)
                _index += list->size;
            if (_index < 0 || _index >= list->size)
                vm_error(vm, "List index out of range.");
            vm->stack[vm->sp - 1] = ((Value *)list->data)[_index];
        }
        break;
    }
    case OBJ_MAP:
    {
        PiMap *map = AS_MAP(container);
        if (!IS_STRING(index) && try_callMethodArgs(vm, container, "getItem", 1, &index, &method_result))
        {
            vm->stack[vm->sp - 1] = method_result;
            break;
        }
        if (bracket_access && IS_STRING(index) && !MAP_HAS_FLAG(map, MAP_BRACKET))
            vm_error(vm, "Bracket member access is disabled for this object.");

        PiMap *owner = map_owner(map, index);
        Value item = NEW_NIL();
        if (owner)
        {
            if (IS_STRING(index) &&
                map->_key == AS_OBJ(index) &&
                map->owner == owner &&
                owner->version == map->owner_version)
            {
                item = map->_value;
            }
            else if (IS_STRING(index))
            {
                Value *val = ht_get(owner->table, AS_CSTRING(index));
                if (val)
                    item = *val;
            }
            else
                item = map_get(owner, index);
        }

        bool bind_object_method = owner != NULL && IS_FUN(item) &&
                                  owner == vm->object_proto;

        if ((MAP_HAS_FLAG(map, MAP_IS_INSTANCE) && owner != NULL && IS_FUN(item)) || bind_object_method)
        {
            Object *target = map->super_instance
                                 ? map->super_instance
                                 : AS_OBJ(container);

            /*
             * Caching is only worthwhile when:
             *   - the key is a compiled string constant (pointer-stable)
             *   - the method lives on a prototype, not the instance itself
             *     (owner != map means we walked the chain at least one step)
             *   - the target is a PiMap we can attach a BoundCache to
             *
             * Dynamic string keys (obj["m1"+"m2"]) always miss the cache and
             * fall through to bind() directly — correct but uncached.
             */
            bool cacheable = IS_STRING(index) &&
                             owner != map &&
                             target->type == OBJ_MAP;

            if (cacheable)
            {
                PiMap *instance_cache = (PiMap *)target;
                Object *key_obj = AS_OBJ(index);         // stable PiString pointer
                uint32_t mhash = AS_STRING(index)->hash; // already computed at parse time

                Value cached = get_boundCache(instance_cache, mhash, key_obj, owner);

                if (IS_FUN(cached))
                {
                    // Cache hit:
                    // bind() was called at some earlier point; reuse the result.
                    // No allocation, no hash table, no prototype walk.
                    item = cached;
                }
                else
                {
                    // bind() allocates a new Function on the GC heap.
                    // We store it so the next access to the same method on the
                    // same instance is free.
                    item = bind(vm, AS_FUN(item), target);
                    put_boundCache(instance_cache, mhash, key_obj, owner, item);
                }
            }
            else
            {
                // Not cacheable: super call, bracket access with dynamic key,
                // or method defined directly on the instance map itself.
                item = bind(vm, AS_FUN(item), target);
            }
        }

        vm->stack[vm->sp - 1] = item;
        break;
    }
    case OBJ_MODULE:
    {
        ObjModule *module = AS_MODULE(container);
        char *owned_property = NULL;
        const char *property = IS_STRING(index) ? AS_CSTRING(index) : (owned_property = as_string(index));
        Value item = NEW_NIL();
        if (is_private_moduleName(property))
        {
            free(owned_property);
            vm_error(vm, "Cannot access private module member.");
        }
        if (property[0] == 'n' && strcmp(property, "name") == 0)
            item = NEW_OBJ(add_obj(vm, new_pistring(strdup(module->name ? module->name : ""))));
        else if (property[0] == 'i' && strcmp(property, "is_main") == 0)
            item = NEW_BOOL(module->is_main);
        else if (property[0] == 'p' && strcmp(property, "path") == 0)
            item = NEW_OBJ(add_obj(vm, new_pistring(strdup(module->path ? module->path : ""))));
        else if (property[0] == 'e' && strcmp(property, "exports") == 0)
            item = NEW_OBJ((Object *)module->exports);
        else if (module->exports && IS_STRING(index))
            item = map_get(module->exports, index);
        else if (module->exports)
            item = map_getValueByKey(module->exports, property);
        free(owned_property);
        vm->stack[vm->sp - 1] = item;
        break;
    }
    case OBJ_TUPLE:
    {
        PiTuple *tuple = AS_TUPLE(container);
        int _index = get_index(as_number(index), LIST_SIZE(tuple->items));
        vm->stack[vm->sp - 1] = *(Value *)list_getAt(tuple->items, _index);
        break;
    }
    case OBJ_STRING:
    {
        char *str = as_string(container);
        int _index = get_index(as_number(index), strlen(str));
        char *_char = malloc(2);
        _char[0] = str[_index];
        _char[1] = '\0';
        vm->stack[vm->sp - 1] = NEW_OBJ(add_obj(vm, new_pistring(_char)));
        free(str);
        break;
    }
    default:
        vm_error(vm, "Unsupported operand type for get item operator.\n");
    }
    VM_DISPATCH_SAFE();
}

OP_TENSOR_GET:
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
                specs[i].start = isinf(slice->start) ? (sign > 0 ? tensor->shape[i] : -1) : tensor_sliceBound(tensor->shape[i], slice->start, sign);
                specs[i].end = isinf(slice->stop) ? (sign > 0 ? tensor->shape[i] : -1) : tensor_sliceBound(tensor->shape[i], slice->stop, sign);
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
        VM_DISPATCH_SAFE();
    }

    int out_shape[MAX_TENSOR_DIMS], dim_map[MAX_TENSOR_DIMS];
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
        VM_DISPATCH_SAFE();
    }

    int out_strides[MAX_TENSOR_DIMS];
    out_strides[out_ndim - 1] = 1;
    for (int i = out_ndim - 2; i >= 0; i--)
        out_strides[i] = out_strides[i + 1] * out_shape[i + 1];

    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(out_ndim, out_shape, tensor->type));
    int total = result->size;
    int src_coords[MAX_TENSOR_DIMS];

    for (int i = 0; i < tensor->ndim; i++)
        if (i < ndim && !IS_SLICE(indices[i]))
            src_coords[i] = specs[i].start;

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
    VM_DISPATCH_SAFE();
}

OP_SET_ITEM:
OP_SET_MEMBER:
{
    bool bracket_access = current_op == OP_SET_ITEM;
    Value index;
    Value container;
    Value value;

    if (bracket_access)
    {
        index = pop_stack(vm);
        container = pop_stack(vm);
        value = pop_stack(vm);
    }
    else
    {
        uint16_t name_idx = (uint16_t)((code[pc++] << 8) | code[pc++]);
        index = constants_data[name_idx];
        container = pop_stack(vm);
        value = pop_stack(vm);
    }
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
        list_refreshNumericFlag(pi_list);
        break;
    }
    case OBJ_MAP:
    {
        PiMap *map = AS_MAP(container);
        Value args[2] = {index, value};
        if (!IS_STRING(index) && try_callMethodArgs(vm, container, "setItem", 2, args, &method_result))
            break;
        if (bracket_access && IS_STRING(index) && !MAP_HAS_FLAG(map, MAP_BRACKET))
            vm_error(vm, "Bracket member access is disabled for this object.");
        if (MAP_HAS_FLAG(map, MAP_IS_INSTANCE))
        {
            char *owned_key = NULL;
            const char *key = IS_STRING(index) ? AS_CSTRING(index) : (owned_key = as_string(index));
            if (!ht_set(map->table, key, &value))
            {
                if (ht_put(map->table, key, &value))
                    map_dirty(map);
            }
            else
                map_dirty(map);
            free(owned_key);
        }
        else
        {
            if (MAP_HAS_FLAG(map, MAP_LOCKED) && map_owner(map, index) == NULL)
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
    VM_DISPATCH_SAFE();
}

OP_TENSOR_SET:
{
    uint8_t ndim = code[pc++];
    Value indices[MAX_TENSOR_DIMS];
    for (int i = ndim - 1; i >= 0; i--)
        indices[i] = pop_stack(vm);

    Value container = pop_stack(vm);
    Value assign_value = pop_stack(vm);

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
                specs[i].start = isinf(slice->start) ? (sign > 0 ? tensor->shape[i] : -1) : tensor_sliceBound(tensor->shape[i], slice->start, sign);
                specs[i].end = isinf(slice->stop) ? (sign > 0 ? tensor->shape[i] : -1) : tensor_sliceBound(tensor->shape[i], slice->stop, sign);
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
        push_stack(vm, assign_value);
        VM_DISPATCH_SAFE();
    }

    int out_shape[MAX_TENSOR_DIMS], dim_map[MAX_TENSOR_DIMS];
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
            if (src_tensor->shape[i] != out_shape[i])
                vm_error(vm, "Assigned tensor shape does not match target tensor slice.");
    }
    if (list_assign && LIST_SIZE(src_list->items) != total)
        vm_error(vm, "Assigned list length does not match target tensor slice.");

    int src_coords[MAX_TENSOR_DIMS];
    for (int i = 0; i < tensor->ndim; i++)
        if (i < ndim && !IS_SLICE(indices[i]))
            src_coords[i] = specs[i].start;

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
        double assign_num;
        if (scalar_assign)
            assign_num = IS_NUM(assign_value) ? as_number(assign_value) : tensor_getFlat(AS_TENSOR(assign_value), 0);
        else if (tensor_assign)
            assign_num = tensor_getFlat(src_tensor, flat);
        else if (list_assign)
        {
            Value item = *(Value *)list_getAt(src_list->items, flat);
            if (!IS_NUM(item))
                vm_error(vm, "Assigned list must contain only numeric values.");
            assign_num = as_number(item);
        }
        else
            vm_error(vm, "Tensor slice assignment requires a numeric scalar, numeric list, or tensor.");
        tensor_set(tensor, src_coords, assign_num);
    }
    push_stack(vm, assign_value);
    VM_DISPATCH_SAFE();
}

OP_IMPORT:
{
    Value name = pop_stack(vm);
    if (!IS_STRING(name))
        vm_error(vm, "Module name must be a string.");
    push_stack(vm, load_module(vm, AS_STRING(name)->chars));
    VM_DISPATCH_SAFE();
}

OP_GET_EXPORT:
{
    Value name = pop_stack(vm);
    Value module = pop_stack(vm);

    if (!IS_OBJ(module) || (OBJ_TYPE(module) != OBJ_MAP && OBJ_TYPE(module) != OBJ_MODULE))
        vm_error(vm, "Attempt to access export from non-module object.");

    if (!IS_STRING(name))
        vm_error(vm, "Export name must be a string.");

    char *export_name = AS_STRING(name)->chars;
    if (OBJ_TYPE(module) == OBJ_MODULE && is_private_moduleName(export_name))
        vm_error(vm, "Cannot import private module member.");

    PiMap *_module = (OBJ_TYPE(module) == OBJ_MODULE) ? AS_MODULE(module)->exports : AS_MAP(module);

    push_stack(vm, map_get(_module, name));

    VM_DISPATCH_SAFE();
}

OP_IMPORT_ALL:
{
    Value module = pop_stack(vm);
    if (!IS_OBJ(module) || (OBJ_TYPE(module) != OBJ_MAP && OBJ_TYPE(module) != OBJ_MODULE))
        vm_error(vm, "Attempt to import from non-module object.");

    PiMap *_module = (OBJ_TYPE(module) == OBJ_MODULE) ? AS_MODULE(module)->exports : AS_MAP(module);
    table_t *table = _module->table;

    ht_iter it = ht_iterator(table);
    while (ht_next(&it))
    {
        const char *key = it.key;
        Value *value = (Value *)it.value;
        if (!value)
            continue;
        if (OBJ_TYPE(module) == OBJ_MODULE && is_private_moduleName(key))
            continue;
        Value *oldValue = ht_get(vm->globals, key);
        if (oldValue && IS_FUN(*oldValue))
        {
            AS_FUN(*oldValue)->global_valid = false;
            AS_FUN(*oldValue)->glonal_index = -1;
        }
        if (!ht_set(vm->globals, key, value))
            ht_put(vm->globals, key, value);

        if (IS_FUN(*value) && AS_FUN(*value)->name && strcmp(AS_FUN(*value)->name, key) == 0)
        {
            AS_FUN(*value)->global_valid = true;
            AS_FUN(*value)->glonal_index = -1;
        }
    }
    vm->global_cache->globals = NULL;
    vm->global_cache->names = NULL;

    VM_DISPATCH_SAFE();
}

OP_IMPORT_DEFAULT:
{
    Value name = pop_stack(vm);
    Value module = pop_stack(vm);

    if (!IS_OBJ(module) || (OBJ_TYPE(module) != OBJ_MAP && OBJ_TYPE(module) != OBJ_MODULE))
        vm_error(vm, "Attempt to import from non-module object.");

    if (!IS_STRING(name))
        vm_error(vm, "Export name must be a string.");

    char *export_name = AS_STRING(name)->chars;
    if (OBJ_TYPE(module) == OBJ_MODULE && is_private_moduleName(export_name))
        vm_error(vm, "Cannot import private module member.");

    PiMap *_module = (OBJ_TYPE(module) == OBJ_MODULE) ? AS_MODULE(module)->exports : AS_MAP(module);

    Value value = map_get(_module, name);
    push_stack(vm, IS_FUN(value) ? value : module);

    VM_DISPATCH_SAFE();
}

OP_RETURN:
{
    VM_RETURN_WITH(POP());
}

OP_RETURN_NIL:
{
    VM_RETURN_WITH(NEW_NIL());
}

OP_HALT:
{
    if (vm->gc_requested)
        gc_collect(vm);
    vm->running = false;
    goto L_VM_DONE;
}

OP_NO:
    VM_DISPATCH_SAFE();

OP_PUSH_NIL:
    push_stack(vm, NEW_NIL());
    VM_DISPATCH_SAFE();

OP_DEBUG:
    printf("[DEBUG] Current PC: %d\n", pc);
    VM_DISPATCH_SAFE();

OP_PRINT:
{
    Value value = pop_stack(vm);
    char *str = as_string(value);
    printf("%s\n", str);
    free(str);
    VM_DISPATCH_SAFE();
}

L_VM_DONE:
    vm->pc = pc;
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
