#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "pi_vm.h"
#include "pi_build.h"
#include "pi_class.h"

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

volatile InterruptFlag interrupt_requested = 0;

static void _add_objectClassMembers(PiClass *klass);
static Value _construct(VM *vm, PiClass *_class, size_t argc, Value *argv, Value kw_args);

static Value _bind(VM *vm, Function *function, Object *instance);
static Value _bind_callable(VM *vm, PiInstance *instance);
static Value _bind_nativeMethod(Object *instance, NativeMethod *method);

static void _gc_collect(VM *vm);

static char *_copy_dirName(const char *path)
{
    if (!path || path[0] == '\0')
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

static ObjModule *_vm_currentModule(VM *vm)
{
    if (!vm || !vm->globals)
        return NULL;

    Value *module_val = _ht_get(vm->globals, "module");

    if (!module_val || !IS_MODULE(*module_val))
        return NULL;

    return AS_MODULE(*module_val);
}

static void _list_refreshNumericFlag(PiList *list)
{
    bool numeric = TRUE;

    for (int i = 0; i < LIST_SIZE(list->items); i++)
    {
        if (!IS_NUM(*(Value *)_list_getAt(list->items, i)))
        {
            numeric = FALSE;
            break;
        }
    }

    list->is_numeric = numeric;
}

static Value _make_iterPair(VM *vm, Value first, Value second)
{
    List *items = list_create(sizeof(Value));
    _list_add(items, &first);
    _list_add(items, &second);
    return NEW_OBJ(add_obj(vm, new_list(items)));
}

static void _list_setSlice(VM *vm, PiList *target, PiSlice *slice, Value value)
{
    if (!IS_LIST(value) && !IS_TUPLE(value))
        vm_error(vm, "List slice assignment requires a list or tuple.");

    List *items = target->items;
    List *source = IS_LIST(value)
                       ? AS_LIST(value)->items
                       : AS_TUPLE(value)->items;

    int size = LIST_SIZE(items);
    int step = (int)slice->step;

    if (step == 0)
        vm_error(vm, "Slice step cannot be zero.");

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

    int source_count = LIST_SIZE(source);
    Value *snapshot = NULL;

    if (source_count > 0)
    {
        snapshot = malloc(sizeof(Value) * source_count);

        if (!snapshot)
            vm_error(vm, "Memory allocation failed during slice assignment.");

        for (int i = 0; i < source_count; i++)
            snapshot[i] = *(Value *)_list_getAt(source, i);
    }

    if (step == 1)
    {
        int delete_count = end > start ? end - start : 0;
        int new_size = size - delete_count + source_count;

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

        void *insert_at = (Byte *)items->data + start * items->i_size;
        void *tail_from =
            (Byte *)items->data + (start + delete_count) * items->i_size;
        void *tail_to =
            (Byte *)items->data + (start + source_count) * items->i_size;

        memmove(
            tail_to,
            tail_from,
            (size - start - delete_count) * items->i_size);

        if (source_count > 0)
            memcpy(insert_at, snapshot, source_count * items->i_size);

        items->size = new_size;
    }
    else
    {
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

        int current = start;

        for (int i = 0; i < source_count; i++)
        {
            _list_set(items, current, &snapshot[i]);
            current += step;
        }
    }

    free(snapshot);

    _list_refreshNumericFlag(target);
}

static bool _set_equals(PiSet *left, PiSet *right)
{
    if (set_size(left) != set_size(right))
        return FALSE;

    for (int i = 0; i < set_size(left); i++)
    {
        Value value = set_get(left, i);
        if (!set_has(right, value))
            return FALSE;
    }

    return TRUE;
}

static bool _set_isSubset(PiSet *left, PiSet *right)
{
    for (int i = 0; i < set_size(left); i++)
    {
        Value value = set_get(left, i);
        if (!set_has(right, value))
            return FALSE;
    }

    return TRUE;
}

static Object *_set_ops(PiSet *left, PiSet *right, int op)
{
    PiSet *result = (PiSet *)new_set();

    if (op == 9) /* union */
    {
        for (int i = 0; i < set_size(left); i++)
            set_add(result, set_get(left, i));

        for (int i = 0; i < set_size(right); i++)
            set_add(result, set_get(right, i));
    }
    else if (op == 8) /* intersection */
    {
        for (int i = 0; i < set_size(left); i++)
        {
            Value value = set_get(left, i);
            if (set_has(right, value))
                set_add(result, value);
        }
    }
    else if (op == 10) /* symmetric difference */
    {
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

static Object *_set_difference(PiSet *left, PiSet *right)
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

static const char *_vm_moduleLabel(VM *vm)
{
    ObjModule *module = _vm_currentModule(vm);

    if (!module)
        return NULL;

    if (module->path && module->path[0] != '\0')
        return module->path;

    if (module->name && module->name[0] != '\0')
        return module->name;

    return NULL;
}

static Instruction *_vm_instrForOffset(VM *vm, int target_offset)
{
    if (!vm || !vm->instrs)
        return NULL;

    char *scope_name = "<global>";

    if (vm->function && IS_FUN(NEW_OBJ(vm->function)))
    {
        Function *fn = (Function *)vm->function;

        if (fn->name && fn->name[0] != '\0')
            scope_name = fn->name;
    }

    List *instrs = _ht_get(vm->instrs, scope_name);

    if (!instrs && strcmp(scope_name, "<global>") != 0)
        instrs = _ht_get(vm->instrs, "<global>");

    if (!instrs)
        return NULL;

    int size = LIST_SIZE(instrs);
    Instruction *instr = NULL;

    for (int i = 0; i < size; i++)
    {
        Instruction *cur = (Instruction *)_list_getAt(instrs, i);

        if (cur->offset > target_offset)
            break;

        instr = cur;
    }

    return instr;
}

static Instruction *_vm_currentInstr(VM *vm)
{
    if (!vm)
        return NULL;

    if (vm->current_instr)
        return vm->current_instr;

    return _vm_instrForOffset(vm, vm->error_pc);
}

static const char *_vm_callName(VM *vm, int offset)
{
    Instruction *instr = _vm_instrForOffset(vm, offset);

    if (!instr || !instr->descr || instr->descr[0] == '\0' || strcmp(instr->descr, "<FUN>") == 0)
        return NULL;

    return instr->descr;
}

VM *init_vm(Compiler *comp, const char *entry_name, bool is_main)
{
    VM *vm = (VM *)malloc(sizeof(VM));

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

    vm->running = TRUE;

    vm->fps = TARGET_FPS;

    pthread_mutex_init(&vm->lock, NULL);

    mark_constants(vm);

    vm->gc_bytes = 0;
    vm->next_gc_bytes = GC_INITIAL_BYTES;
    vm->gc_requested = FALSE;

    vm->open_upvalues = NULL;

    vm->function = NULL;
    vm->_kw_args = NEW_NIL();

    vm->obj_count = 0;

    vm->gc_stack = NULL;

    vm->modules = ht_create(sizeof(Value));
    vm->project_build = NULL;
    vm->current_path = getcwd(NULL, 0);
    vm->object_class = NULL;

    if (entry_name && entry_name[0] != '\0')
    {
        char *entry_dir = _copy_dirName(entry_name);
        if (entry_dir)
        {
            free(vm->current_path);
            vm->current_path = entry_dir;
        }
    }

    const char *module_name = (entry_name && entry_name[0] != '\0') ? entry_name : "<main>";
    const char *module_path = (entry_name && entry_name[0] != '\0')
                                  ? entry_name
                                  : (vm->current_path ? vm->current_path : "");
    Object *main_module_obj = new_module(
        vm,
        module_name,
        module_path,
        FALSE,
        is_main);

    /* Treat the entry module as loaded so imports cannot re-enter it. */
    ObjModule *main_module = (ObjModule *)main_module_obj;
    main_module->state = MODULE_LOADED;

    Value main_module_val = NEW_OBJ(main_module_obj);
    ht_put(vm->globals, "module", &main_module_val);

    vm->object_class = (PiClass *)add_obj(vm, new_class("Object", NULL, ht_create(sizeof(Value))));
    _add_objectClassMembers(vm->object_class);
    Value object_class_val = NEW_OBJ((Object *)vm->object_class);
    ht_put(vm->globals, "Object", &object_class_val);

    return vm;
}

void vm_reset(VM *vm, Compiler *comp)
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

    /* Globals intentionally survive shell resets. */
    vm->global_cache = &comp->global_cache;

    vm->iter_sp = -1;
    vm->comp_sp = 0;
    vm->frame_sp = 0;

    vm->running = TRUE;

    vm->gc_bytes = gc_recountBytes(vm);
    vm->next_gc_bytes = vm->gc_bytes + GC_INITIAL_BYTES;
    vm->gc_requested = FALSE;

    vm->open_upvalues = NULL;
    vm->function = NULL;
    vm->_kw_args = NEW_NIL();

    mark_constants(vm);
}

inline Object *add_obj(VM *vm, Object *obj)
{
    if (obj->in_gcList)
        return obj;

    obj->in_gcList = TRUE;
    obj->gc_color = GC_WHITE;

    obj->next = vm->objects;
    vm->objects = obj;
    vm->obj_count++;

    obj->gc_bytes = gc_objectBytes(obj);
    vm->gc_bytes += obj->gc_bytes;
    if (vm->gc_bytes >= vm->next_gc_bytes)
        vm->gc_requested = TRUE;

    return obj;
}

static void _gc_collect(VM *vm)
{
    size_t before = vm->gc_bytes;
    run_gc(vm);
    size_t after = gc_recountBytes(vm);
    size_t collected = before > after ? before - after : 0;
    size_t allocation_budget = after / 2;

    if (allocation_budget < GC_MIN_ALLOCATION_BYTES)
        allocation_budget = GC_MIN_ALLOCATION_BYTES;
    else if (allocation_budget > GC_MAX_ALLOCATION_BYTES)
        allocation_budget = GC_MAX_ALLOCATION_BYTES;

    vm->gc_bytes = after;
    vm->next_gc_bytes = after + allocation_budget;
    vm->gc_requested = FALSE;

#ifdef DEBUG
    printf("[GC] Before: %zu bytes, After: %zu bytes, Collected: %zu bytes, Next threshold: %zu bytes\n",
           before, after, collected, vm->next_gc_bytes);
#endif
}

#ifdef DEBUG
static inline int _count_objs(VM *vm)
{
    int count = 0;
    Object *obj = vm->objects;
    while (obj)
    {
        printf("[DEBUG] Counting object at %p\n", (void *)obj);
        count++;
        obj = obj->next;
    }
    return count;
}
#endif

void vm_error(VM *vm, const char *message)
{
    Instruction *instr = _vm_currentInstr(vm);
    const char *module_label = _vm_moduleLabel(vm);

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

void vm_errorf(VM *vm, const char *fmt, ...)
{
    Instruction *instr = _vm_currentInstr(vm);
    const char *module_label = _vm_moduleLabel(vm);

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

static inline Value _pop_stack(VM *vm)
{
    if (vm->sp <= 0)
        vm_error(vm, "Stack underflow: Attempted to pop from an empty stack");

    Value value = vm->stack[--vm->sp];
    vm->stack[vm->sp] = NEW_NIL();
    return value;
}

static inline void _set_stackTop(VM *vm, int new_sp)
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

static inline void _push_stack(VM *vm, Value value)
{
    if (vm->sp >= STACK_MAX)
        vm_error(vm, "[stack] Stack overflow: Attempted to push onto a full stack");

    vm->stack[vm->sp++] = value;
}

static inline Value _peek_stack(VM *vm)
{
    if (vm->sp <= 0)
        vm_error(vm, "Stack underflow: Attempted to peek at an empty stack");

    return vm->stack[vm->sp - 1];
}

/* Hash-table growth relocates inline values, so cached slots track its version. */
static inline Value *_global_slot(VM *vm, uint8_t index, const char *name)
{
    GlobalCache *cache = vm->global_cache;
    if (!cache)
        vm_error(vm, "Missing global cache for active code unit.");

    if (cache->globals != vm->globals || cache->names != vm->names ||
        cache->globals_version != vm->globals->version)
    {
        memset(cache->slots, 0, sizeof(cache->slots));
        cache->globals = vm->globals;
        cache->names = vm->names;
        cache->globals_version = vm->globals->version;
    }

    Value *slot = cache->slots[index];
    if (!slot)
    {
        slot = _ht_get(vm->globals, name);
        cache->slots[index] = slot;
    }
    return slot;
}

static inline int _resolve_localSlot(VM *vm, int local)
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

static Value _bind_nativeMethod(Object *instance, NativeMethod *method)
{
    if (!method->has_cached_bound)
    {
        method->cached_bound = *new_native(method->name, method->func);
        method->has_cached_bound = TRUE;
    }

    Value native = method->cached_bound;
    Function *bound = AS_FUN(native);
    bound->instance = instance;
    bound->flags |= FUNC_METHOD;

    return native;
}

static inline void _vm_listAppendValue(PiList *list, Value value);

static void _list_extendFromIterable(VM *vm, PiList *plist, Value iterable)
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
        _vm_listAppendValue(plist, value);
    }
}

static void _map_extendFromMap(VM *vm, PiMap *target, Value source)
{
    if (!IS_MAP(source))
        vm_error(vm, "Map spread expects a map value.");

    PiMap *map = AS_MAP(source);
    HT_Iterator it = _ht_iterator(map->table);
    while (_ht_next(&it))
    {
        const char *key = it.key;
        Value *item = (Value *)it.value;
        if (item == NULL)
            continue;

        if (IS_OBJ(*item))
            add_obj(vm, AS_OBJ(*item));

        ht_put(target->table, key, item);
    }
}

/* Used to preserve last-write-wins semantics when removing repeated spreads. */
static bool _map_hasKeyOverlap(PiMap *left, PiMap *right)
{
    PiMap *smaller = left->table->size <= right->table->size ? left : right;
    PiMap *larger = smaller == left ? right : left;
    HT_Iterator it = _ht_iterator(smaller->table);

    while (_ht_next(&it))
    {
        if (ht_has(larger->table, it.key))
            return TRUE;
    }
    return FALSE;
}

static bool _map_extendSourceIsRedundant(Value *sources, int index)
{
    PiMap *source = AS_MAP(sources[index]);
    int previous = -1;

    for (int i = index - 1; i >= 0; i--)
    {
        if (sources[i].type == sources[index].type && AS_MAP(sources[i]) == source)
        {
            previous = i;
            break;
        }
    }

    if (previous < 0)
        return FALSE;

    for (int i = previous + 1; i < index; i++)
    {
        PiMap *intermediate = AS_MAP(sources[i]);
        if (_map_hasKeyOverlap(source, intermediate))
            return FALSE;
    }

    return TRUE;
}

static Value _call_withArgList(VM *vm, Value callee, PiList *arg_list, Value kw_args)
{
    int num_args = arg_list->items->size;

    /* Keep large argument lists off the C stack. */
    Value *args = num_args > 0 ? (Value *)malloc(num_args * sizeof(Value)) : NULL;
    if (num_args > 0 && !args)
        vm_error(vm, "Memory allocation failed for argument list.");

    for (int i = 0; i < num_args; i++)
        args[i] = *(Value *)_list_getAt(arg_list->items, i);

    Value result;

    if (IS_FUN(callee))
    {
        result = call_func(vm, AS_FUN(callee), num_args, args, kw_args);
        if (IS_OBJ(result))
            add_obj(vm, AS_OBJ(result));
    }
    else if (IS_INSTANCE(callee))
    {
        Value bound = _bind_callable(vm, AS_INSTANCE(callee));
        if (!IS_FUN(bound))
        {
            free(args);
            vm_error(vm, "Attempt to call a non-function object.");
        }
        result = call_func(vm, AS_FUN(bound), num_args, args, kw_args);
        if (IS_OBJ(result))
            add_obj(vm, AS_OBJ(result));
    }
    else if (IS_MAP(callee))
    {
        free(args);
        vm_error(vm, "Maps are not callable; use a class to create instances.");
    }
    else if (IS_CLASS(callee))
    {
        result = _construct(vm, AS_CLASS(callee), num_args, args, kw_args);
    }
    else
    {
        free(args);
        vm_error(vm, "Attempt to call a non-function object.");
        result = NEW_NIL();
    }

    free(args);
    return result;
}

void push_frame(VM *vm, Frame *frame)
{
    if (vm->frame_sp >= STACK_MAX)
        vm_error(vm, "[frame] Stack overflow: Attempted to push onto a full stack");

    vm->frames[vm->frame_sp++] = *frame;
}

Frame *pop_frame(VM *vm)
{
    if (vm->frame_sp <= 0)
        vm_error(vm, "Stack underflow: Attempted to pop from an empty stack");

    return &vm->frames[--vm->frame_sp];
}

static inline char *_read_name(VM *vm, int index)
{
    return string_get(vm->names, index);
}

static inline bool _is_false(VM *vm, Value value)
{
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static inline int _vm_readShort(VM *vm)
{
    uint8_t *code = (uint8_t *)vm->code->data;
    int high = code[vm->pc++] & 0xFF;
    int low = code[vm->pc++] & 0xFF;

    return (high << 8) | low;
}

static inline int _read_short(uint8_t *code, int pc)
{
    int high = code[pc] & 0xFF;
    int low = code[pc + 1] & 0xFF;
    return (high << 8) | low;
}

static inline void _vm_storeLocalSlot(VM *vm, int slot, Value value)
{
    vm->stack[slot] = value;
    if (vm->sp <= slot)
    {
        for (int i = vm->sp; i < slot; i++)
            vm->stack[i] = NEW_NIL();
        vm->sp = slot + 1;
    }
}

static inline bool _vm_storeLoopValueIfLocal(VM *vm, uint8_t *code, int *pc, Value value)
{
    int next_pc = *pc + 2;
    if (vm->comp_sp > 0 || code[next_pc] != OP_STORE_LOCAL)
        return FALSE;

    _vm_storeLocalSlot(vm, vm->bp + code[next_pc + 1], value);
    *pc += 4;
    return TRUE;
}

static inline void _vm_listAppendValue(PiList *list, Value value)
{
    List *items = list->items;
    if (items->size == items->capacity)
        _list_grow(items);
    ((Value *)items->data)[items->size++] = value;

    if (!IS_NUM(value))
        list->is_numeric = FALSE;
}

static bool _list_getNumericMatrixShape(PiList *list, int *rows, int *cols)
{
    int count = LIST_SIZE(list->items);
    if (count == 0)
        return FALSE;

    int width = -1;
    for (int i = 0; i < count; i++)
    {
        Value value = *(Value *)_list_getAt(list->items, i);
        if (!IS_LIST(value))
            return FALSE;
        PiList *row = AS_LIST(value);
        if (!row->is_numeric)
            return FALSE;
        if (width < 0)
            width = LIST_SIZE(row->items);
        else if (width != LIST_SIZE(row->items))
            return FALSE;
    }

    *rows = count;
    *cols = width;
    return TRUE;
}

static inline Value _op_binaryNum(int op, double l, double r)
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
        return NEW_NUM((uint32_t)l & (uint32_t)r);
    case 9:
        return NEW_NUM((uint32_t)l | (uint32_t)r);
    case 10:
        return NEW_NUM((uint32_t)l ^ (uint32_t)r);
    case 11:
        return NEW_NUM((uint32_t)l << (uint32_t)r);
    case 12:
        return NEW_NUM((int)l >> (int)r);
    case 13:
        return NEW_NUM((uint32_t)l >> (uint32_t)r);
    default:
        return NEW_NUM(0);
    }
}

static UpValue *_capture_upvalue(VM *vm, int index)
{
    UpValue *prev = NULL;
    UpValue *upvalue = vm->open_upvalues;
    while (upvalue != NULL && upvalue->index != index)
    {
        prev = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->index == index)
        return upvalue;

    UpValue *created_upvalue = (UpValue *)malloc(sizeof(UpValue));
    created_upvalue->value = vm->stack[index];
    created_upvalue->index = index;
    created_upvalue->ref_count = 0;

    created_upvalue->next = upvalue;
    if (prev == NULL)
        vm->open_upvalues = created_upvalue;
    else
        prev->next = created_upvalue;

    return created_upvalue;
}

static void _remove_upvalue(VM *vm, int index)
{
    UpValue *prev = NULL;
    UpValue *upvalue = vm->open_upvalues;

    while (upvalue != NULL && upvalue->index != index)
    {
        prev = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->index == index)
    {
        upvalue->index = -1;

        upvalue->value = vm->stack[index];

        if (prev == NULL)
            vm->open_upvalues = upvalue->next;
        else
            prev->next = upvalue->next;
    }
}

static Value _bind(VM *vm, Function *function, Object *instance)
{
    if (FUNC_HAS_FLAG(function, FUNC_NATIVE))
    {
        Value *native = new_native(function->name, function->native);
        Function *bound = AS_FUN(*native);
        bound->instance = instance;
        bound->owner = function->owner;
        bound->bound_source = (Object *)function;
        bound->flags |= FUNC_METHOD;
        return *native;
    }

    Object *fn = new_func(function->name, function->body,
                          function->params, NULL, instance);
    ((Function *)fn)->constants = function->constants;
    ((Function *)fn)->names = function->names;
    ((Function *)fn)->instrs = function->instrs;
    ((Function *)fn)->globals = function->globals;
    ((Function *)fn)->param_names = function->param_names;
    ((Function *)fn)->owns_params = FALSE;
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
        ((Function *)fn)->owns_upvalues = TRUE;
    }
    else
    {
        ((Function *)fn)->upvalues = NULL;
        ((Function *)fn)->owns_upvalues = FALSE;
    }
    ((Function *)fn)->upvalue_count = function->upvalue_count;
    ((Function *)fn)->flags = (function->flags & ~(FUNC_NEED_ARGS | FUNC_NEED_KWARGS)) |
                              FUNC_METHOD;
    ((Function *)fn)->owner = function->owner;
    ((Function *)fn)->bound_source = (Object *)function;

    if (function->body && function->body->method_need_args)
        ((Function *)fn)->flags |= FUNC_NEED_ARGS;
    if (function->body && function->body->method_need_kwargs)
        ((Function *)fn)->flags |= FUNC_NEED_KWARGS;

    add_obj(vm, fn);

    return NEW_OBJ(fn);
}

static Value _bind_callable(VM *vm, PiInstance *instance)
{
    const uint64_t call_hash = 0;
    const uint64_t epoch = class_mutationVersion();
    BoundCache *cache = instance->bound_cache;
    HT_Table *owner_table = NULL;
    Value method;

    if (!instance_getMember(instance, "call", &method) || !IS_FUN(method))
        return NEW_NIL();

    for (PiClass *current = instance->_class; current != NULL; current = current->super)
    {
        if (current->members && _ht_get(current->members, "call"))
        {
            owner_table = current->members;
            break;
        }
    }

    for (int i = 0; i < BOUND_CACHE_SIZE; i++)
    {
        BoundCache *entry = &cache[i];
        if (entry->valid && entry->key == NULL && entry->key_hash == call_hash &&
            entry->class_epoch == epoch && entry->owner_table == owner_table &&
            entry->owner_table && entry->owner_version == owner_table->version &&
            entry->fields_version == (instance->fields ? instance->fields->version : 0))
            return entry->bound_fn;
    }

    Value bound = _bind(vm, AS_FUN(method), (Object *)instance);
    BoundCache *entry = &cache[instance->bound_cache_next++ % BOUND_CACHE_SIZE];
    entry->key_hash = call_hash;
    entry->key = NULL;
    entry->owner_table = owner_table;
    entry->owner_version = owner_table ? owner_table->version : 0;
    entry->class_epoch = epoch;
    entry->fields_version = instance->fields ? instance->fields->version : 0;
    entry->bound_fn = bound;
    entry->valid = TRUE;
    return bound;
}

static void _add_objectClassMembers(PiClass *klass)
{
    HT_Table *members = klass->members;
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

    ht_put(members, "format", &format);
    ht_put(members, "hash", &hash);
    ht_put(members, "clone", &clone);
    ht_put(members, "extends", &extends_fn);
    ht_put(members, "equals", &equals_fn);
    ht_put(members, "ident", &ident_fn);
    ht_put(members, "compare", &compare_fn);
    ht_put(members, "type", &type_fn);
    ht_put(members, "name", &name_fn);
    ht_put(members, "setName", &set_name_fn);
    ht_put(members, "lock", &lock_fn);
    ht_put(members, "bracketAccess", &bracket_access_fn);
    ht_put(members, "get", &get_fn);
    ht_put(members, "set", &set_fn);
    ht_put(members, "has", &has_fn);
    ht_put(members, "delete", &delete_fn);
    ht_put(members, "iterator", &iterator_fn);
    ht_put(members, "next", &next_fn);
    ht_put(members, "keys", &keys);
    ht_put(members, "values", &values);
}

static Value _call_methodNoArgs(VM *vm, Value receiver, const char *name)
{
    if (IS_INSTANCE(receiver) || IS_CLASS(receiver))
    {
        Value method;
        bool found = IS_INSTANCE(receiver)
                         ? instance_getMember(AS_INSTANCE(receiver), name, &method)
                         : class_getMember(AS_CLASS(receiver), name, &method);
        if (!found || !IS_FUN(method))
            return receiver;

        Object *target = IS_INSTANCE(receiver) || FUNC_HAS_FLAG(AS_FUN(method), FUNC_NATIVE)
                             ? AS_OBJ(receiver)
                             : NULL;
        Value bound = _bind(vm, AS_FUN(method), target);
        return call_func(vm, AS_FUN(bound), 0, NULL, NEW_NIL());
    }

    return receiver;
}

static bool _call_methodWithArgs(VM *vm, Value receiver, const char *name,
                                 Value *args, int argc, Value *result)
{
    if (!IS_INSTANCE(receiver) && !IS_CLASS(receiver))
        return FALSE;

    Value method;
    bool found = IS_INSTANCE(receiver)
                     ? instance_getMember(AS_INSTANCE(receiver), name, &method)
                     : class_getMember(AS_CLASS(receiver), name, &method);
    if (!found || !IS_FUN(method))
        return FALSE;

    Object *target = IS_INSTANCE(receiver) || FUNC_HAS_FLAG(AS_FUN(method), FUNC_NATIVE)
                         ? AS_OBJ(receiver)
                         : NULL;
    Value bound = _bind(vm, AS_FUN(method), target);
    *result = call_func(vm, AS_FUN(bound), argc, args, NEW_NIL());
    return TRUE;
}

Value vm_callMethodNoArgs(VM *vm, Value receiver, const char *name)
{
    return _call_methodNoArgs(vm, receiver, name);
}

static Value _to_primitive(VM *vm, Value value, bool pref_string)
{
    if (!IS_INSTANCE(value))
        return value;

    (void)pref_string;
    Value result = _call_methodNoArgs(vm, value, "format");
    if (result.type != VAL_OBJ || IS_STRING(result))
        return result;

    return value;
}

static Value _construct(VM *vm, PiClass *_class, size_t argc, Value *argv, Value kw_args)
{
    if (!_class)
        vm_error(vm, "Cannot construct a null class.");

    Object *instance = add_obj(vm, new_instance(_class));
    Value constructor;
    if (class_getMember(_class, "constructor", &constructor) && IS_FUN(constructor))
    {
        Value bound = _bind(vm, AS_FUN(constructor), instance);
        _push_stack(vm, NEW_OBJ(instance));
        Value result = call_func(vm, AS_FUN(bound), argc, argv, kw_args);
        _pop_stack(vm);

        /* Match JavaScript: object returns replace the instance; primitives do not. */
        if (IS_OBJ(result) && OBJ_TYPE(result) != OBJ_STRING)
            return result;
    }
    return NEW_OBJ(instance);
}

static double _tensor_applyBinary(int op, double left, double right)
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
    case 7:
        return pow(left, right);
    default:
        return NAN;
    }
}

static Value _tensor_scalarBinary(VM *vm, PiTensor *tensor, double scalar, int op, bool scalar_left)
{
    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(tensor->ndim, tensor->shape, tensor->type));
    for (int i = 0; i < tensor->size; i++)
    {
        double cell = tensor_getFlat(tensor, i);
        double value = scalar_left ? _tensor_applyBinary(op, scalar, cell)
                                   : _tensor_applyBinary(op, cell, scalar);
        tensor_setFlat(result, i, value);
    }
    return NEW_OBJ(result);
}

static bool _tensor_broadcastShape(PiTensor *left, PiTensor *right, int *ndim, int *shape)
{
    *ndim = left->ndim > right->ndim ? left->ndim : right->ndim;
    for (int i = 0; i < *ndim; i++)
    {
        int li = left->ndim - *ndim + i;
        int ri = right->ndim - *ndim + i;
        int ldim = li < 0 ? 1 : left->shape[li];
        int rdim = ri < 0 ? 1 : right->shape[ri];

        if (ldim != rdim && ldim != 1 && rdim != 1)
            return FALSE;

        shape[i] = ldim > rdim ? ldim : rdim;
    }
    return TRUE;
}

static int _tensor_projectOffset(PiTensor *tensor, int result_ndim, int *result_indices)
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

static Value _tensor_broadcastBinary(VM *vm, PiTensor *left, PiTensor *right, int op)
{
    int shape[16];
    int ndim = 0;
    if (left->ndim > 16 || right->ndim > 16 || !_tensor_broadcastShape(left, right, &ndim, shape))
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

        double l = tensor_getFlat(left, _tensor_projectOffset(left, ndim, indices));
        double r = tensor_getFlat(right, _tensor_projectOffset(right, ndim, indices));
        tensor_setFlat(result, flat, _tensor_applyBinary(op, l, r));
    }

    return NEW_OBJ(result);
}

typedef struct TensorSliceSpec
{
    int start;
    int end;
    int step;
    int count;
} TensorSliceSpec;

static int _tensor_sliceBound(int length, double value, int sign)
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

static bool _is_PrivateModuleName(const char *name)
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

void vm_run(VM *vm)
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
        [OP_COMP_APPEND] = VM_TARGET(OP_COMP_APPEND),
        [OP_COMP_BEGIN] = VM_TARGET(OP_COMP_BEGIN),
        [OP_COMP_END] = VM_TARGET(OP_COMP_END),
        [OP_LIST_EXTEND] = VM_TARGET(OP_LIST_EXTEND),
        [OP_PUSH_MAP] = VM_TARGET(OP_PUSH_MAP),
        [OP_PUSH_CLASS] = VM_TARGET(OP_PUSH_CLASS),
        [OP_MAP_EXTEND] = VM_TARGET(OP_MAP_EXTEND),
        [OP_PUSH_FUNCTION] = VM_TARGET(OP_PUSH_FUNCTION),
        [OP_PUSH_CLOSURE] = VM_TARGET(OP_PUSH_CLOSURE),
        [OP_LOAD_UPVALUE] = VM_TARGET(OP_LOAD_UPVALUE),
        [OP_STORE_UPVALUE] = VM_TARGET(OP_STORE_UPVALUE),
        [OP_PUSH_SLICE] = VM_TARGET(OP_PUSH_SLICE),
        [OP_GET_ITEM] = VM_TARGET(OP_GET_ITEM),
        [OP_GET_MEMBER] = VM_TARGET(OP_GET_MEMBER),
        [OP_GET_SLOT] = VM_TARGET(OP_GET_SLOT),
        [OP_TENSOR_GET] = VM_TARGET(OP_TENSOR_GET),
        [OP_SET_ITEM] = VM_TARGET(OP_SET_ITEM),
        [OP_SET_MEMBER] = VM_TARGET(OP_SET_MEMBER),
        [OP_SET_SLOT] = VM_TARGET(OP_SET_SLOT),
        [OP_SWITCH_COMPARE] = VM_TARGET(OP_SWITCH_COMPARE),
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
#endif
    int safepoint_steps = 0;

    uint8_t *code = (uint8_t *)vm->code->data;
    Value *constants_data = (Value *)vm->constants->data;

    Value nil_value;
    Object *iter = NULL;
    Function *function = (Function *)vm->function;

#define VM_RETURN_WITH(value_expr)                                                 \
    do                                                                             \
    {                                                                              \
        Value retval = (value_expr);                                               \
                                                                                   \
        if (vm->open_upvalues)                                                     \
            for (int i = vm->sp - 1; i >= vm->bp; i--)                             \
                _remove_upvalue(vm, i);                                            \
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
                _gc_collect(vm);                                                   \
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
        char *name = _read_name(vm, index);

        Value new_value = POP();
        Value *old_value = _global_slot(vm, index, name);
        if (old_value && IS_FUN(*old_value))
        {
            AS_FUN(*old_value)->global_valid = FALSE;
            AS_FUN(*old_value)->glonal_index = -1;
        }

        if (old_value)
            *old_value = new_value;
        else
        {
            ht_put(vm->globals, name, &new_value);
            vm->global_cache->slots[index] = _ht_get(vm->globals, name);
        }
        if (IS_FUN(new_value) && AS_FUN(new_value)->name &&
            strcmp(AS_FUN(new_value)->name, name) == 0)
        {
            AS_FUN(new_value)->global_valid = TRUE;
            AS_FUN(new_value)->glonal_index = index;
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

        Value *_value = _global_slot(vm, index, name);
        if (_value == NULL)
        {
            nil_value = NEW_NIL();
            _value = &nil_value;
        }
        PUSH(*_value);
        VM_DISPATCH_SAFE();
    }

OP_LOAD_LOCAL:
    {
        uint8_t local = code[pc++];
        int slot = vm->bp + local;
        if (vm->comp_sp > 0)
            slot = _resolve_localSlot(vm, local);

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
                    _vm_listAppendValue(AS_LIST(*left), right);
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
        if (!FUNC_HAS_FLAG(function, FUNC_METHOD) || function->instance == NULL)
            vm_error(vm, "super is only available inside object methods.");

        if (!function->owner || function->owner->type != OBJ_CLASS)
            vm_error(vm, "super is only available inside a class method.");
        PiInstance *instance = (PiInstance *)function->instance;
        PiClass *owner = (PiClass *)function->owner;
        if (!owner->super)
            vm_error(vm, "Class has no superclass.");
        PiInstance *super_view = (PiInstance *)new_instance(owner->super);
        free(super_view->slots);
        super_view->fields = instance->fields;
        super_view->slots = instance->slots;
        super_view->owns_storage = FALSE;
        _push_stack(vm, NEW_OBJ(add_obj(vm, (Object *)super_view)));
        VM_DISPATCH_SAFE();
    }

OP_STORE_LOCAL:
    {
        uint8_t local = code[pc++];
        int slot = vm->bp + local;
        if (vm->comp_sp > 0)
            slot = _resolve_localSlot(vm, local);
        _vm_storeLocalSlot(vm, slot, POP());
        VM_DISPATCH_SAFE();
    }

OP_POP:
    {
        _remove_upvalue(vm, vm->sp - 1);
        POP();
        VM_DISPATCH_SAFE();
    }

OP_POP_N:
    {
        uint8_t n = code[pc++];
        for (int i = 0; i < n; i++)
        {
            _remove_upvalue(vm, vm->sp - 1);
            POP();
        }
        VM_DISPATCH_SAFE();
    }

OP_DUP_TOP:
    {
        Value value = _peek_stack(vm);
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

OP_SWITCH_COMPARE:
    {
        int constant_index = (code[pc] << 8) | code[pc + 1];
        int offset = (int16_t)((code[pc + 2] << 8) | code[pc + 3]);
        Value subject = vm->stack[vm->sp - 1];
        Value constant = constants_data[constant_index];

        if (equals(subject, constant))
        {
            vm->sp--;
            pc += 4;
        }
        else
        {
            pc += offset;
        }
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
            bool result = FALSE;
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
            bool result = FALSE;
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
                result = FALSE;
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
            bool result = FALSE;
            switch (op)
            {
            case 0:
                result = _set_equals(left_set, right_set);
                break;
            case 1:
                result = !_set_equals(left_set, right_set);
                break;
            case 2:
                result = _set_isSubset(right_set, left_set) && set_size(left_set) != set_size(right_set);
                break;
            case 3:
                result = _set_isSubset(left_set, right_set) && set_size(left_set) != set_size(right_set);
                break;
            case 4:
                result = _set_isSubset(right_set, left_set);
                break;
            case 5:
                result = _set_isSubset(left_set, right_set);
                break;
            }
            _push_stack(vm, NEW_BOOL(result));
            VM_DISPATCH_SAFE();
        }

        if (op == 6)
        {
            if (!IS_OBJ(right) || !is_iterable(AS_OBJ(right)))
                vm_error(vm, "Right operand of 'in' must be iterable.");

            bool result = FALSE;
            switch (OBJ_TYPE(right))
            {
            case OBJ_LIST:
            {
                PiList *list = AS_LIST(right);
                Value *items = (Value *)list->items->data;
                int size = LIST_SIZE(list->items);
                for (int i = 0; i < size; i++)
                {
                    if (equals(left, items[i]))
                    {
                        result = TRUE;
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
                result = map_has(AS_MAP(right), left);
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
                        result = TRUE;
                        break;
                    }
                }
                break;
            }
            }
            _push_stack(vm, NEW_BOOL(result));
            VM_DISPATCH_SAFE();
        }

        if (op <= 1)
        {
            bool result = FALSE;
            if (IS_OBJ(left) && IS_OBJ(right) && AS_OBJ(left) == AS_OBJ(right))
            {
                _push_stack(vm, NEW_BOOL(op == 0));
                VM_DISPATCH_SAFE();
            }
            Value l = TO_PRIM_NUM(left);
            Value r = TO_PRIM_NUM(right);
            result = (compare(l, r) == 0);
            _push_stack(vm, NEW_BOOL(op == 0 ? result : !result));
            VM_DISPATCH_SAFE();
        }

        {
            int cmp = 0;
            Value l = TO_PRIM_NUM(left);
            Value r = TO_PRIM_NUM(right);
            cmp = compare(l, r);
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
                result = FALSE;
            }
            _push_stack(vm, NEW_BOOL(result));
        }
        VM_DISPATCH_SAFE();
    }

OP_BINARY:
    {
        uint8_t op = code[pc++];
        Value right = vm->stack[vm->sp - 1];
        Value left = vm->stack[vm->sp - 2];

        if (op <= 13 && IS_NUM(left) && IS_NUM(right))
        {
            vm->sp--;
            vm->stack[vm->sp - 1] = _op_binaryNum(op, AS_NUM(left), AS_NUM(right));
            VM_DISPATCH_SAFE();
        }

        right = _pop_stack(vm);
        left = _pop_stack(vm);

        if (op <= 13 && is_numeric(left) && is_numeric(right))
        {
            PUSH(_op_binaryNum(op, as_number(left), as_number(right)));
            VM_DISPATCH_SAFE();
        }

        if (op <= 13)
        {
            Value operator_args[2] = {NEW_NUM(op), right};
            Value reflected_args[2] = {NEW_NUM(op), left};
            Value result;
            if (_call_methodWithArgs(vm, left, "compute", operator_args, 2, &result) ||
                _call_methodWithArgs(vm, right, "rcompute", reflected_args, 2, &result))
            {
                PUSH(result);
                VM_DISPATCH_SAFE();
            }
        }

        switch (op)
        {
        case 0: // +
        {
            if (IS_NAN(left) || IS_NAN(right))
            {
                _push_stack(vm, NEW_NUM(NAN));
                break;
            }

            if (IS_LIST(left))
            {
                PiList *list = AS_LIST(left);
                _vm_listAppendValue(list, right);
                _push_stack(vm, left);
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
                _push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(res))));
                break;
            }

            if (IS_TUPLE(left) && IS_TUPLE(right))
            {
                PiTuple *l_tuple = AS_TUPLE(left);
                PiTuple *r_tuple = AS_TUPLE(right);
                List *result = list_copy(l_tuple->items);
                list_addAll(result, r_tuple->items);
                _push_stack(vm, NEW_OBJ(add_obj(vm, new_tuple(result))));
                break;
            }

            if (IS_TENSOR(left))
            {
                if (IS_TENSOR(right))
                    _push_stack(vm, _tensor_broadcastBinary(vm, AS_TENSOR(left), AS_TENSOR(right), 0));
                else if (is_numeric(right))
                    _push_stack(vm, _tensor_scalarBinary(vm, AS_TENSOR(left), as_number(right), 0, FALSE));
                else
                    vm_error(vm, "Unsupported right operand for tensor [+].");
                break;
            }
            if (IS_TENSOR(right) && is_numeric(left))
            {
                _push_stack(vm, _tensor_scalarBinary(vm, AS_TENSOR(right), as_number(left), 0, TRUE));
                break;
            }

            bool pref_string = IS_STRING(left) || IS_STRING(right);
            Value _left = TO_PRIM(vm, left, pref_string);
            Value _right = TO_PRIM(vm, right, pref_string);
            if (is_numeric(_left) && is_numeric(_right))
            {
                _push_stack(vm, NEW_NUM(as_number(_left) + as_number(_right)));
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
                _push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(res))));
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
                int size = LIST_SIZE(list->items);
                for (int i = 0; i < size; i++)
                {
                    Value item = *(Value *)_list_getAt(list->items, i);
                    if (equals(item, right))
                    {
                        list_remove(list->items, i);
                        break;
                    }
                }
                _push_stack(vm, left);
                break;
            }
            if (IS_STRING(left))
            {
                Value _right = TO_PRIM(vm, right, FALSE);
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
                _push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(res))));
                free(l_str);
                free(r_str);
                break;
            }
            if (IS_SET(left) && IS_SET(right))
            {
                _push_stack(vm, NEW_OBJ(add_obj(vm, _set_difference(AS_SET(left), AS_SET(right)))));
                break;
            }
            if (IS_TENSOR(left))
            {
                if (IS_TENSOR(right))
                    _push_stack(vm, _tensor_broadcastBinary(vm, AS_TENSOR(left), AS_TENSOR(right), 1));
                else if (is_numeric(right))
                    _push_stack(vm, _tensor_scalarBinary(vm, AS_TENSOR(left), as_number(right), 1, FALSE));
                else
                    vm_error(vm, "Unsupported right operand for tensor [-].");
                break;
            }
            if (IS_TENSOR(right) && is_numeric(left))
            {
                _push_stack(vm, _tensor_scalarBinary(vm, AS_TENSOR(right), as_number(left), 1, TRUE));
                break;
            }
            {
                Value _left = TO_PRIM(vm, left, FALSE);
                Value _right = TO_PRIM(vm, right, FALSE);
                if (is_numeric(_left) && is_numeric(_right))
                {
                    _push_stack(vm, NEW_NUM(as_number(_left) - as_number(_right)));
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
                if (!_list_getNumericMatrixShape(A, &m, &n) ||
                    !_list_getNumericMatrixShape(B, &n_right, &p))
                    vm_error(vm, "Matrix multiplication requires rectangular numeric lists.");
                if (n != n_right)
                    vm_error(vm, "Matrix multiplication dimension mismatch.");
                List *result = list_create(sizeof(Value));
                for (int i = 0; i < m; i++)
                {
                    List *row_a = as_list(*(Value *)_list_getAt(A->items, i));
                    List *temp = list_create(sizeof(Value));
                    for (int j = 0; j < p; j++)
                    {
                        double sum = 0.0;
                        for (int k = 0; k < n; k++)
                        {
                            double a = as_number(*(Value *)_list_getAt(row_a, k));
                            List *row_b = as_list(*(Value *)_list_getAt(B->items, k));
                            double b = as_number(*(Value *)_list_getAt(row_b, j));
                            sum += a * b;
                        }
                        Value num = NEW_NUM(sum);
                        _list_add(temp, &num);
                    }
                    Value row = NEW_OBJ(new_list(temp));
                    _list_add(result, &row);
                }
                Object *res_obj = add_obj(vm, new_list(result));
                _push_stack(vm, NEW_OBJ(res_obj));
                break;
            }
            if (IS_LIST(left))
            {
                Value right_prim = TO_PRIM(vm, right, FALSE);
                double count_number = as_number(right_prim);
                List *list = as_list(left);
                if (!isfinite(count_number) || count_number < 0 || floor(count_number) != count_number)
                    vm_error(vm, "List repetition count must be a non-negative integer.");
                if (count_number > 0 && list->size > PI_MAX_LIST_SIZE / count_number)
                    vm_errorf(vm, "List repetition exceeds the maximum of %d elements.", PI_MAX_LIST_SIZE);

                int count = (int)count_number;
                int result_size = (int)(list->size * count_number);
                List *result = list_createCap(list->i_size, result_size);
                for (int i = 0; i < count; i++)
                    list_addAll(result, list);
                Object *res_obj = new_list(result);
                if (AS_LIST(left)->is_numeric)
                    ((PiList *)res_obj)->is_numeric = TRUE;
                _push_stack(vm, NEW_OBJ(add_obj(vm, res_obj)));
                break;
            }
            if (IS_STRING(left))
            {
                Value right_prim = TO_PRIM(vm, right, FALSE);
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
                _push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(result))));
                break;
            }
            if (IS_TUPLE(left) && is_numeric(right))
            {
                int repeat_count = (int)as_number(right);
                PiTuple *tuple = AS_TUPLE(left);
                List *result = list_create(sizeof(Value));
                for (int i = 0; i < repeat_count; i++)
                    list_addAll(result, tuple->items);
                _push_stack(vm, NEW_OBJ(add_obj(vm, new_tuple(result))));
                break;
            }
            if (is_numeric(left) && IS_TUPLE(right))
            {
                int repeat_count = (int)as_number(left);
                PiTuple *tuple = AS_TUPLE(right);
                List *result = list_create(sizeof(Value));
                for (int i = 0; i < repeat_count; i++)
                    list_addAll(result, tuple->items);
                _push_stack(vm, NEW_OBJ(add_obj(vm, new_tuple(result))));
                break;
            }
            if (IS_TENSOR(left))
            {
                if (IS_TENSOR(right))
                    _push_stack(vm, _tensor_broadcastBinary(vm, AS_TENSOR(left), AS_TENSOR(right), 2));
                else if (is_numeric(right))
                    _push_stack(vm, _tensor_scalarBinary(vm, AS_TENSOR(left), as_number(right), 2, FALSE));
                else
                    vm_error(vm, "Unsupported right operand for tensor [*].");
                break;
            }
            if (IS_TENSOR(right) && is_numeric(left))
            {
                _push_stack(vm, _tensor_scalarBinary(vm, AS_TENSOR(right), as_number(left), 2, TRUE));
                break;
            }
            {
                Value _left = TO_PRIM(vm, left, FALSE);
                Value _right = TO_PRIM(vm, right, FALSE);
                if (is_numeric(_left) && is_numeric(_right))
                {
                    _push_stack(vm, NEW_NUM(as_number(_left) * as_number(_right)));
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
                    _push_stack(vm, _tensor_broadcastBinary(vm, AS_TENSOR(left), AS_TENSOR(right), 3));
                else if (is_numeric(right))
                    _push_stack(vm, _tensor_scalarBinary(vm, AS_TENSOR(left), as_number(right), 3, FALSE));
                else
                    vm_error(vm, "Unsupported right operand for tensor [/].");
                break;
            }
            if (IS_TENSOR(right) && is_numeric(left))
            {
                _push_stack(vm, _tensor_scalarBinary(vm, AS_TENSOR(right), as_number(left), 3, TRUE));
                break;
            }
            {
                Value _left = TO_PRIM(vm, left, FALSE);
                Value _right = TO_PRIM(vm, right, FALSE);
                double denom = as_number(_right);
                _push_stack(vm, NEW_NUM(denom == 0.0 ? INFINITY : as_number(_left) / denom));
            }
            break;
        }
        case 4: // %
        {
            Value _left = TO_PRIM(vm, left, FALSE);
            Value _right = TO_PRIM(vm, right, FALSE);
            int denom = (int)as_number(_right);
            _push_stack(vm, denom == 0 ? NEW_NAN() : NEW_NUM((int)as_number(_left) % denom));
            break;
        }
        case 5:
            _push_stack(vm, NEW_BOOL(as_bool(left) && as_bool(right)));
            break;
        case 6:
            _push_stack(vm, NEW_BOOL(as_bool(left) || as_bool(right)));
            break;
        case 7:
        {
            // TODO: Implement tensor power operation
            if (IS_TENSOR(left))
            {
                if (IS_TENSOR(right))
                    _push_stack(vm, _tensor_broadcastBinary(vm, AS_TENSOR(left), AS_TENSOR(right), 7));
                else if (is_numeric(right))
                    _push_stack(vm, _tensor_scalarBinary(vm, AS_TENSOR(left), as_number(right), 7, FALSE));
                else
                    vm_error(vm, "Unsupported right operand for tensor [**].");
                break;
            }
            if (IS_TENSOR(right) && is_numeric(left))
            {
                _push_stack(vm, _tensor_scalarBinary(vm, AS_TENSOR(right), as_number(left), 7, TRUE));
                break;
            }
            Value _left = TO_PRIM(vm, left, FALSE);
            Value _right = TO_PRIM(vm, right, FALSE);
            _push_stack(vm, NEW_NUM(pow(as_number(_left), as_number(_right))));
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
                _push_stack(vm, NEW_OBJ(add_obj(vm, _set_ops(AS_SET(left), AS_SET(right), op))));
                break;
            }
            if (op == 10 && IS_LIST(left) && IS_LIST(right))
            {
                PiList *l_list = AS_LIST(left);
                PiList *r_list = AS_LIST(right);
                if (!l_list->is_numeric || !r_list->is_numeric)
                    vm_error(vm, "Cross product requires numeric lists.");
                if (LIST_SIZE(l_list->items) != 3 || LIST_SIZE(r_list->items) != 3)
                    vm_error(vm, "Cross product is defined for 3-dimensional vectors only.");
                Value *a = (Value *)l_list->items->data;
                Value *b = (Value *)r_list->items->data;
                double x = as_number(a[1]) * as_number(b[2]) - as_number(a[2]) * as_number(b[1]);
                double y = as_number(a[2]) * as_number(b[0]) - as_number(a[0]) * as_number(b[2]);
                double z = as_number(a[0]) * as_number(b[1]) - as_number(a[1]) * as_number(b[0]);
                List *res = list_create(sizeof(Value));
                Value vx = NEW_NUM(x), vy = NEW_NUM(y), vz = NEW_NUM(z);
                _list_add(res, &vx);
                _list_add(res, &vy);
                _list_add(res, &vz);
                _push_stack(vm, NEW_OBJ(add_obj(vm, new_list(res))));
                break;
            }
            {
                Value _left = TO_PRIM(vm, left, FALSE);
                Value _right = TO_PRIM(vm, right, FALSE);
                if (is_numeric(_left) && is_numeric(_right))
                {
                    double l = as_number(_left), r = as_number(_right);
                    double result;
                    switch (op)
                    {
                    case 8:
                        result = (uint32_t)l & (uint32_t)r;
                        break;
                    case 9:
                        result = (uint32_t)l | (uint32_t)r;
                        break;
                    case 10:
                        result = (uint32_t)l ^ (uint32_t)r;
                        break;
                    case 11:
                        result = (uint32_t)l << (uint32_t)r;
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
                    _push_stack(vm, NEW_NUM(result));
                    break;
                }
                if (IS_LIST(left))
                {
                    List *list = as_list(left);
                    List *result = list_create(sizeof(Value));
                    int size = LIST_SIZE(list);
                    if (op == 13)
                    {
                        uint32_t r = (uint32_t)as_number(_right);
                        for (int i = 0; i < size; i++)
                        {
                            Value item = *(Value *)_list_getAt(list, i);
                            Value v = NEW_NUM((uint32_t)as_number(item) >> r);
                            _list_add(result, &v);
                        }
                    }
                    else
                    {
                        int r = (int)as_number(_right);
                        for (int i = 0; i < size; i++)
                        {
                            Value item = *(Value *)_list_getAt(list, i);
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
                            _list_add(result, &v);
                        }
                    }
                    _push_stack(vm, NEW_OBJ(add_obj(vm, new_list(result))));
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
            int l_size = LIST_SIZE(l_list->items);
            if (l_size != LIST_SIZE(r_list->items))
                vm_error(vm, "Dot product requires lists of the same length.");
            Value *a = (Value *)l_list->items->data;
            Value *b = (Value *)r_list->items->data;
            double result = 0.0;
            for (int i = 0; i < l_size; i++)
                result += as_number(a[i]) * as_number(b[i]);
            _push_stack(vm, NEW_NUM(result));
            break;
        }
        case 15:
        {
            if (IS_INSTANCE(left) && IS_CLASS(right))
            {
                bool matches = FALSE;
                for (PiClass *current = AS_INSTANCE(left)->_class; current; current = current->super)
                {
                    if (current == AS_CLASS(right))
                    {
                        matches = TRUE;
                        break;
                    }
                }
                _push_stack(vm, NEW_BOOL(matches));
                break;
            }
            if (IS_CLASS(left) && IS_CLASS(right))
            {
                bool matches = FALSE;
                for (PiClass *current = AS_CLASS(left); current; current = current->super)
                {
                    if (current == AS_CLASS(right))
                    {
                        matches = TRUE;
                        break;
                    }
                }
                _push_stack(vm, NEW_BOOL(matches));
                break;
            }
            if (!IS_MAP(left) || !IS_MAP(right))
            {
                _push_stack(vm, NEW_BOOL(FALSE));
                break;
            }
            _push_stack(vm, NEW_BOOL(AS_MAP(left) == AS_MAP(right)));
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
                vm->stack[vm->sp - 1] = NEW_NUM(LIST_SIZE(AS_LIST(operand)->items));
                break;
            case OBJ_TUPLE:
                vm->stack[vm->sp - 1] = NEW_NUM(LIST_SIZE(AS_TUPLE(operand)->items));
                break;
            case OBJ_SET:
                vm->stack[vm->sp - 1] = NEW_NUM(set_size(AS_SET(operand)));
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
                vm->stack[vm->sp - 1] = NEW_NUM(~(uint32_t)n);
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
            vm->stack[vm->sp - 1] = NEW_BOOL(TRUE);
            VM_DISPATCH_SAFE();
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
                vm->stack[vm->sp - 1] = NEW_NUM(~(uint32_t)n);
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

            if (FUNC_HAS_FLAG(callee_fn, FUNC_NATIVE) &&
                FUNC_HAS_FLAG(callee_fn, FUNC_METHOD) &&
                callee_fn->native == pi_push && callee_fn->instance &&
                callee_fn->instance->type == OBJ_LIST)
            {
                List *items = ((PiList *)callee_fn->instance)->items;
                for (uint8_t i = 0; i < num_args; i++)
                    _list_add(items, &vm->stack[args_base + i]);
                vm->sp = obj_slot;
                PUSH(NEW_NUM(items->size));
                VM_DISPATCH_SAFE();
            }

            size_t param_count = (!FUNC_HAS_FLAG(callee_fn, FUNC_NATIVE) && callee_fn->params)
                                     ? (size_t)callee_fn->arity
                                     : 0;
            bool param_this = FUNC_HAS_FLAG(callee_fn, FUNC_METHOD) && callee_fn->param_names &&
                              (size_t)callee_fn->param_names->size + 1 == param_count;
            size_t _param_count = param_count - (param_this ? 1 : 0);

            if (!FUNC_HAS_FLAG(callee_fn, FUNC_NATIVE) &&
                (!FUNC_HAS_FLAG(callee_fn, FUNC_METHOD) || callee_fn->instance != NULL) &&
                !FUNC_HAS_FLAG(callee_fn, FUNC_NEED_ARGS) &&
                !FUNC_HAS_FLAG(callee_fn, FUNC_NEED_KWARGS) &&
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

                if (FUNC_HAS_FLAG(callee_fn, FUNC_METHOD))
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
        else if (IS_INSTANCE(callee))
        {
            Value bound = _bind_callable(vm, AS_INSTANCE(callee));
            if (!IS_FUN(bound))
            {
                if (num_args > 8)
                    free(args);
                vm_error(vm, "Attempt to call a non-function object.");
            }
            Value result = call_func(vm, AS_FUN(bound), num_args, args, NEW_NIL());
            PUSH(result);
        }
        else if (IS_CLASS(callee))
        {
            Value result = _construct(vm, AS_CLASS(callee), num_args, args, NEW_NIL());
            PUSH(result);
        }
        else if (IS_MAP(callee))
        {
            if (num_args > 8)
                free(args);
            vm_error(vm, "Maps are not callable; use a class to create instances.");
        }
        else
        {
            if (num_args > 8)
                free(args);
            const char *name = _vm_callName(vm, instr_pc);
            if (name)
                vm_errorf(vm, "Attempt to call '%s', which is not a function.", name);
            else
                vm_error(vm, "Attempt to call a non-function value.");
        }

        if (num_args > 8)
            free(args);
        VM_DISPATCH_SAFE();
    }

OP_CALL_FUNCTION_KW:
    {
        uint8_t num_args = code[pc++];
        Value kw_args = _pop_stack(vm);
        if (!IS_OBJ(kw_args) || OBJ_TYPE(kw_args) != OBJ_MAP)
            vm_error(vm, "Named arguments must be a map.");

        Value stack_args[8];
        Value *args = num_args <= 8 ? stack_args : (Value *)malloc(num_args * sizeof(Value));
        if (num_args > 8 && !args)
            vm_error(vm, "Memory allocation failed for argument list.");

        for (int i = num_args - 1; i >= 0; i--)
            args[i] = _pop_stack(vm);
        Value callee = _pop_stack(vm);
        Value result = NEW_NIL();

        if (IS_FUN(callee))
        {
            vm->error_pc = vm->pc;
            vm->pc = pc;
            result = call_func(vm, AS_FUN(callee), num_args, args, kw_args);
            if (IS_OBJ(result))
                add_obj(vm, AS_OBJ(result));
        }
        else if (IS_INSTANCE(callee))
        {
            Value bound = _bind_callable(vm, AS_INSTANCE(callee));
            if (!IS_FUN(bound))
            {
                if (num_args > 8)
                    free(args);
                vm_error(vm, "Attempt to call a non-function object.");
            }
            vm->error_pc = vm->pc;
            vm->pc = pc;
            result = call_func(vm, AS_FUN(bound), num_args, args, kw_args);
        }
        else if (IS_CLASS(callee))
        {
            result = _construct(vm, AS_CLASS(callee), num_args, args, kw_args);
        }
        else if (IS_MAP(callee))
        {
            if (num_args > 8)
                free(args);
            vm_error(vm, "Maps are not callable; use a class to create instances.");
        }
        else
        {
            if (num_args > 8)
                free(args);
            const char *name = _vm_callName(vm, instr_pc);
            if (name)
                vm_errorf(vm, "Attempt to call '%s', which is not a function.", name);
            else
                vm_error(vm, "Attempt to call a non-function object.");
        }

        if (num_args > 8)
            free(args);
        _push_stack(vm, result);
        VM_DISPATCH_SAFE();
    }

OP_CALL_SPREAD:
    {
        bool has_named = code[pc++] != 0;
        Value kw_args = NEW_NIL();
        if (has_named)
        {
            kw_args = _pop_stack(vm);
            if (!IS_OBJ(kw_args) || OBJ_TYPE(kw_args) != OBJ_MAP)
                vm_error(vm, "Named arguments must be a map.");
        }
        Value arg_list_value = _pop_stack(vm);
        if (!IS_LIST(arg_list_value))
            vm_error(vm, "Spread call arguments must be collected in a list.");
        Value callee = _pop_stack(vm);
        vm->error_pc = vm->pc;
        vm->pc = pc;
        _push_stack(vm, _call_withArgList(vm, callee, AS_LIST(arg_list_value), kw_args));
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
            List *items = list->items;
            if (list->current < items->size)
            {
                int index = list->current;
                Value value = ((Value *)items->data)[list->current++];
                if (IS_OBJ(value))
                    add_obj(vm, AS_OBJ(value));
                if (pair_loop)
                {
                    _push_stack(vm, NEW_NIL());
                    _push_stack(vm, _make_iterPair(vm, NEW_NUM(index), value));
                    pc += 2;
                }
                else
                {
                    bool fused_store = _vm_storeLoopValueIfLocal(vm, code, &pc, value);
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
                    _push_stack(vm, NEW_NIL());
                    _push_stack(vm, _make_iterPair(vm, NEW_NUM((value - range->start) / range->step), NEW_NUM(value)));
                    pc += 2;
                }
                else
                {
                    bool fused_store = _vm_storeLoopValueIfLocal(vm, code, &pc, NEW_NUM(value));
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
            List *items = tuple->items;
            if (tuple->current < items->size)
            {
                int index = tuple->current;
                Value value = ((Value *)items->data)[tuple->current++];
                if (IS_OBJ(value))
                    add_obj(vm, AS_OBJ(value));
                if (pair_loop)
                {
                    _push_stack(vm, NEW_NIL());
                    _push_stack(vm, _make_iterPair(vm, NEW_NUM(index), value));
                    pc += 2;
                }
                else
                {
                    bool fused_store = _vm_storeLoopValueIfLocal(vm, code, &pc, value);
                    if (!fused_store)
                    {
                        _push_stack(vm, value);
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
                    _push_stack(vm, NEW_NIL());
                    _push_stack(vm, _make_iterPair(vm, NEW_NUM(index), value));
                }
                else
                    _push_stack(vm, value);
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
                _ht_next(&map->it);
                if (pair_loop)
                {
                    Value value = *(Value *)map->it.value;
                    if (IS_OBJ(value))
                        add_obj(vm, AS_OBJ(value));
                    _push_stack(vm, NEW_NIL());
                    _push_stack(vm, _make_iterPair(vm, NEW_OBJ(add_obj(vm, new_pistring(strdup(map->it.key)))), value));
                }
                else
                    _push_stack(vm, NEW_OBJ(add_obj(vm, new_pistring(strdup(map->it.key)))));
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
                    _push_stack(vm, NEW_NIL());
                    _push_stack(vm, _make_iterPair(vm, NEW_NUM(index), value));
                }
                else
                    _push_stack(vm, value);
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
        Value step = _pop_stack(vm);
        Value end = _pop_stack(vm);
        Value start = _pop_stack(vm);
        if (!IS_NUM(start) || !IS_NUM(end))
            vm_error(vm, "PiRange `start` and `end` must be numbers.");
        if (!IS_NIL(step) && !IS_NUM(step))
            vm_error(vm, "PiRange `step` must be nil or a number.");
        double _start = as_number(start);
        double _end = as_number(end);
        double _step = IS_NIL(step) ? ((_start < _end) ? 1.0 : -1.0) : as_number(step);
        _push_stack(vm, NEW_OBJ(add_obj(vm, new_range(_start, _end, _step))));
        VM_DISPATCH_SAFE();
    }

OP_PUSH_LIST:
    {
        uint8_t high = code[pc++];
        uint8_t low = code[pc++];
        int num_elements = (high << 8) | low;
        List *list = list_create(sizeof(Value));

        if (num_elements == 0)
        {
            Object *l_obj = add_obj(vm, new_list(list));
            _push_stack(vm, NEW_OBJ(l_obj));
            VM_DISPATCH_SAFE();
        }

        int element_base = vm->sp - num_elements;
        for (int i = 0; i < num_elements; i++)
        {
            Value v = vm->stack[element_base + i];
            _list_add(list, &v);
        }

        Object *l_obj = add_obj(vm, new_list(list));
        _set_stackTop(vm, element_base);
        _push_stack(vm, NEW_OBJ(l_obj));
        VM_DISPATCH_SAFE();
    }

OP_PUSH_SET:
    {
        uint8_t high = code[pc++];
        uint8_t low = code[pc++];
        int num_elements = (high << 8) | low;
        PiSet *set = (PiSet *)new_set();

        if (num_elements == 0)
        {
            _push_stack(vm, NEW_OBJ(add_obj(vm, (Object *)set)));
            VM_DISPATCH_SAFE();
        }

        int element_base = vm->sp - num_elements;
        for (int i = 0; i < num_elements; i++)
        {
            Value element = vm->stack[element_base + i];
            if (IS_OBJ(element))
                add_obj(vm, AS_OBJ(element));
            set_add(set, element);
        }
        Object *set_obj = add_obj(vm, (Object *)set);
        _set_stackTop(vm, element_base);
        _push_stack(vm, NEW_OBJ(set_obj));
        VM_DISPATCH_SAFE();
    }

OP_PUSH_TUPLE:
    {
        uint8_t high = code[pc++];
        uint8_t low = code[pc++];
        int num_elements = (high << 8) | low;
        List *items = list_create(sizeof(Value));
        if (num_elements > 0)
        {
            int element_base = vm->sp - num_elements;
            for (int i = 0; i < num_elements; i++)
            {
                Value element = vm->stack[element_base + i];
                if (IS_OBJ(element))
                    add_obj(vm, AS_OBJ(element));
                _list_add(items, &element);
            }
            _set_stackTop(vm, element_base);
        }
        _push_stack(vm, NEW_OBJ(add_obj(vm, new_tuple(items))));
        VM_DISPATCH_SAFE();
    }

OP_COMP_APPEND:
    {
        int local = code[pc++];
        Value value = _pop_stack(vm);
        int slot = _resolve_localSlot(vm, local);
        if (slot < vm->bp || slot >= vm->sp)
            vm_error(vm, "List append local expects a list target.");
        Value target = vm->stack[slot];
        if (!IS_LIST(target))
            vm_error(vm, "List append local expects a list target.");
        _vm_listAppendValue(AS_LIST(target), value);
        VM_DISPATCH_SAFE();
    }

OP_COMP_BEGIN:
    {
        int local_base = code[pc++];
        if (vm->comp_sp >= COMP_MAX)
            vm_error(vm, "Too many nested list comprehensions.");
        List *list = list_create(sizeof(Value));
        Object *l_obj = add_obj(vm, new_list(list));
        PiList *plist = (PiList *)l_obj;
        plist->is_numeric = TRUE;
        _push_stack(vm, NEW_OBJ(l_obj));
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
        if (!IS_LIST(_peek_stack(vm)))
            vm_error(vm, "List comprehension end expects a list accumulator.");
        int top = vm->comp_sp - 1;
        if (vm->comp_frames[top].base != vm->sp - 1)
            vm_error(vm, "List comprehension stack is unbalanced.");
        vm->comp_sp--;
        VM_DISPATCH_SAFE();
    }

OP_LIST_EXTEND:
    {
        int source_count = code[pc++];
        int source_base = vm->sp - source_count;
        if (source_count <= 0 || source_base <= 0 || !IS_LIST(vm->stack[source_base - 1]))
            vm_error(vm, "List extend expects a list target.");
        PiList *target = AS_LIST(vm->stack[source_base - 1]);
        for (int i = 0; i < source_count; i++)
            _list_extendFromIterable(vm, target, vm->stack[source_base + i]);
        _set_stackTop(vm, source_base);
        VM_DISPATCH_SAFE();
    }

OP_PUSH_MAP:
    {
        int num_elements = code[pc++] << 8;
        num_elements |= code[pc++];
        HT_Table *table = ht_create(sizeof(Value));
        int _sp = vm->sp - (num_elements * 2);

        for (int i = _sp; i < vm->sp; i += 2)
        {
            Value value = vm->stack[i];
            char *key = AS_CSTRING(vm->stack[i + 1]);
            ht_put(table, key, &value);
        }
        _set_stackTop(vm, _sp);
        Object *map = add_obj(vm, new_map(table));
        _push_stack(vm, NEW_OBJ(map));
        VM_DISPATCH_SAFE();
    }

OP_PUSH_CLASS:
    {
        int num_members = code[pc++] << 8;
        num_members |= code[pc++];
        int num_fields = code[pc++] << 8;
        num_fields |= code[pc++];
        int base = vm->sp - (num_members * 2 + num_fields + 2);
        int fields_base = base + num_members * 2;
        int name_slot = fields_base + num_fields;
        if (base < 0 || !IS_STRING(vm->stack[name_slot]) ||
            !IS_CLASS(vm->stack[name_slot + 1]))
            vm_error(vm, "PUSH_CLASS expects members, field names, a class name, and a superclass.");

        HT_Table *members = ht_create(sizeof(Value));
        for (int i = 0; i < num_members * 2; i += 2)
        {
            Value value = vm->stack[base + i];
            Value key = vm->stack[base + i + 1];
            if (!IS_STRING(key))
                vm_error(vm, "Class member names must be strings.");
            ht_put(members, AS_CSTRING(key), &value);
        }

        const char *name = AS_CSTRING(vm->stack[name_slot]);
        PiClass *super = AS_CLASS(vm->stack[name_slot + 1]);
        Object *klass = add_obj(vm, new_class(name, super, members));
        PiClass *pi_class = (PiClass *)klass;
        for (int i = 0; i < num_fields; i++)
        {
            Value field_name = vm->stack[fields_base + i];
            if (!IS_STRING(field_name))
                vm_error(vm, "Instance field names must be strings.");
            const char *field = AS_CSTRING(field_name);
            if (!ht_has(pi_class->field_names, field))
            {
                if (pi_class->slot_count == UINT16_MAX)
                    vm_error(vm, "Class has too many instance fields.");
                uint16_t slot = pi_class->slot_count++;
                ht_put(pi_class->field_names, field, &slot);
            }
        }
        HT_Iterator member_it = _ht_iterator(members);
        while (_ht_next(&member_it))
        {
            Value *member = (Value *)member_it.value;
            if (member && IS_FUN(*member))
                AS_FUN(*member)->owner = klass;
        }
        _set_stackTop(vm, base);
        _push_stack(vm, NEW_OBJ(klass));
        VM_DISPATCH_SAFE();
    }

OP_MAP_EXTEND:
    {
        int source_count = code[pc++];
        int source_base = vm->sp - source_count;
        if (source_count <= 0 || source_base <= 0 || !IS_MAP(vm->stack[source_base - 1]))
            vm_error(vm, "Map extend expects a map target.");
        PiMap *target = AS_MAP(vm->stack[source_base - 1]);

        for (int i = 0; i < source_count; i++)
        {
            if (!IS_MAP(vm->stack[source_base + i]))
                vm_error(vm, "Map spread expects a map value.");
        }

        for (int i = 0; i < source_count; i++)
        {
            Value *sources = &vm->stack[source_base];
            if (!_map_extendSourceIsRedundant(sources, i))
                _map_extendFromMap(vm, target, sources[i]);
        }
        _set_stackTop(vm, source_base);
        VM_DISPATCH_SAFE();
    }

OP_PUSH_FUNCTION:
    {
        int num_params = code[pc++];
        ObjCode *body = AS_CODE(_pop_stack(vm));
        char *name = AS_CSTRING(_pop_stack(vm));
        List *defaults = list_create(sizeof(Value));
        int param_base = vm->sp - num_params;
        for (int i = 0; i < num_params; i++)
        {
            Value param = vm->stack[param_base + i];
            _list_add(defaults, &param);
        }
        _set_stackTop(vm, param_base);
        Object *fn = new_func(name, body, defaults, NULL, NULL);
        ((Function *)fn)->flags = (body->need_args ? FUNC_NEED_ARGS : 0) |
                                  (body->need_kwargs ? FUNC_NEED_KWARGS : 0);
        ((Function *)fn)->constants = vm->constants;
        ((Function *)fn)->names = vm->names;
        ((Function *)fn)->instrs = vm->instrs;
        ((Function *)fn)->globals = vm->globals;
        _push_stack(vm, NEW_OBJ(add_obj(vm, fn)));
        VM_DISPATCH_SAFE();
    }

OP_PUSH_CLOSURE:
    {
        int num_params = code[pc++];
        int num_upvalues = code[pc++];
        UpValue **upvalues = ALLOCATE(UpValue *, num_upvalues + 1);
        for (int i = 0; i < num_upvalues; i++)
        {
            bool is_local = as_bool(_pop_stack(vm));
            int index = as_number(_pop_stack(vm));
            UpValue *upvalue = is_local
                                   ? _capture_upvalue(vm, vm->bp + index)
                                   : function->upvalues[index];
            if (upvalue)
                upvalue->ref_count++;
            upvalues[num_upvalues - i - 1] = upvalue;
        }
        upvalues[num_upvalues] = NULL;
        ObjCode *body = AS_CODE(_pop_stack(vm));
        char *name = AS_CSTRING(_pop_stack(vm));
        List *defaults = list_create(sizeof(Value));
        int param_base = vm->sp - num_params;
        for (int i = 0; i < num_params; i++)
        {
            Value param = vm->stack[param_base + i];
            _list_add(defaults, &param);
        }
        _set_stackTop(vm, param_base);
        Object *fun_obj = new_func(name, body, defaults, upvalues, NULL);
        ((Function *)fun_obj)->flags = (body->need_args ? FUNC_NEED_ARGS : 0) |
                                       (body->need_kwargs ? FUNC_NEED_KWARGS : 0);
        ((Function *)fun_obj)->constants = vm->constants;
        ((Function *)fun_obj)->names = vm->names;
        ((Function *)fun_obj)->instrs = vm->instrs;
        ((Function *)fun_obj)->globals = vm->globals;
        _push_stack(vm, NEW_OBJ(add_obj(vm, fun_obj)));
        VM_DISPATCH_SAFE();
    }

OP_LOAD_UPVALUE:
    {
        int index = code[pc++];
        if (function->upvalues == NULL || function->upvalues[index] == NULL)
            vm_error(vm, "Invalid method binding: closure lost its captured variables while binding a method.");
        UpValue *up_value = function->upvalues[index];
        _push_stack(vm, up_value->index != -1 ? vm->stack[up_value->index] : up_value->value);
        VM_DISPATCH_SAFE();
    }

OP_STORE_UPVALUE:
    {
        int index = code[pc++];
        if (function->upvalues == NULL || function->upvalues[index] == NULL)
            vm_error(vm, "Invalid method binding: closure lost its captured variables while binding a method.");
        UpValue *up_value = function->upvalues[index];
        Value stored = _pop_stack(vm);
        if (up_value->index != -1)
            vm->stack[up_value->index] = stored;
        else
            function->upvalues[index]->value = stored;
        VM_DISPATCH_SAFE();
    }

OP_PUSH_SLICE:
    {
        Value _step = _pop_stack(vm);
        Value _end = _pop_stack(vm);
        Value _start = _pop_stack(vm);
        if (!IS_NUM(_start) || !IS_NUM(_end))
            vm_error(vm, "Slice start and end must be numbers");
        if (!IS_NIL(_step) && !IS_NUM(_step))
            vm_error(vm, "Slice step must be nil or a number");
        double step = IS_NIL(_step) ? 1.0 : as_number(_step);
        if (step == 0.0)
            vm_error(vm, "Slice step cannot be zero");
        _push_stack(vm, NEW_OBJ(add_obj(vm, new_slice(as_number(_start), as_number(_end), step))));
        VM_DISPATCH_SAFE();
    }

OP_GET_SLOT:
    {
        uint16_t encoded_slot = (uint16_t)(code[pc++] << 8);
        encoded_slot |= code[pc++];
        Value container = vm->stack[vm->sp - 1];
        MemberCache *cache = function && function->body &&
                                     instr_pc < function->body->member_cache_count
                                 ? &function->body->member_caches[instr_pc]
                                 : NULL;

        if (cache && cache->valid && IS_INSTANCE(container))
        {
            PiInstance *instance = AS_INSTANCE(container);
            if (instance->_class == cache->cached_class)
            {
                vm->stack[vm->sp - 1] = instance->slots[encoded_slot];
                VM_DISPATCH_SAFE();
            }

            Value name_value = constants_data[cache->name_index];
            if (IS_STRING(name_value))
            {
                uint16_t resolved_slot;
                PiString *name = AS_STRING(name_value);
                if (class_getFieldSlotHash(instance->_class, name->chars, name->hash,
                                           &resolved_slot))
                {
                    cache->cached_class = instance->_class;
                    cache->slot = resolved_slot;
                    code[instr_pc + 1] = (uint8_t)(resolved_slot >> 8);
                    code[instr_pc + 2] = (uint8_t)resolved_slot;
                    vm->stack[vm->sp - 1] = instance->slots[resolved_slot];
                    VM_DISPATCH_SAFE();
                }
            }
        }

        if (!cache || !cache->valid)
            vm_error(vm, "GET_SLOT has no member cache.");
        code[instr_pc] = OP_GET_MEMBER;
        code[instr_pc + 1] = (uint8_t)(cache->name_index >> 8);
        code[instr_pc + 2] = (uint8_t)cache->name_index;
        pc = instr_pc;
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
            uint8_t high = code[pc++];
            uint8_t low = code[pc++];
            uint16_t name_idx = (uint16_t)((high << 8) | low);
            index = constants_data[name_idx];
        }
        Value container = vm->stack[vm->sp - 1];
        if (!IS_OBJ(container))
            vm_error(vm, "Unsupported operand type for get item operator.\n");

        if (!bracket_access && IS_INSTANCE(container) && IS_STRING(index))
        {
            PiInstance *instance = AS_INSTANCE(container);
            PiString *name = AS_STRING(index);
            uint16_t slot;
            if (class_getFieldSlotHash(instance->_class, name->chars, name->hash, &slot))
            {
                if (function && function->body && instr_pc < function->body->member_cache_count)
                {
                    MemberCache *cache = &function->body->member_caches[instr_pc];
                    cache->cached_class = instance->_class;
                    cache->name_index = (uint16_t)((code[instr_pc + 1] << 8) |
                                                   code[instr_pc + 2]);
                    cache->slot = slot;
                    cache->valid = TRUE;
                    code[instr_pc] = OP_GET_SLOT;
                    code[instr_pc + 1] = (uint8_t)(slot >> 8);
                    code[instr_pc + 2] = (uint8_t)slot;
                }
                vm->stack[vm->sp - 1] = instance->slots[slot];
                VM_DISPATCH_SAFE();
            }
        }

        if (OBJ_TYPE(container) == OBJ_CLASS || OBJ_TYPE(container) == OBJ_INSTANCE)
        {
            char *owned_key = NULL;
            const char *key = IS_STRING(index) ? AS_CSTRING(index) : (owned_key = as_string(index));
            Value item = NEW_NIL();
            bool found;
            bool bound_cache_hit = FALSE;
            if (!bracket_access && IS_STRING(index))
            {
                uint64_t hash = AS_STRING(index)->hash;
                BoundCache *bound_cache = OBJ_TYPE(container) == OBJ_CLASS
                                              ? AS_CLASS(container)->bound_cache
                                              : AS_INSTANCE(container)->bound_cache;
                uint64_t fields_version = OBJ_TYPE(container) == OBJ_INSTANCE &&
                                                  AS_INSTANCE(container)->fields
                                              ? AS_INSTANCE(container)->fields->version
                                              : 0;
                uint64_t epoch = class_mutationVersion();

                for (int i = 0; i < BOUND_CACHE_SIZE; i++)
                {
                    BoundCache *entry = &bound_cache[i];
                    if (entry->valid && entry->key_hash == hash && entry->key == AS_OBJ(index) &&
                        entry->class_epoch == epoch && entry->fields_version == fields_version &&
                        entry->owner_table && entry->owner_table->version == entry->owner_version)
                    {
                        item = entry->bound_fn;
                        found = TRUE;
                        bound_cache_hit = TRUE;
                        break;
                    }
                }

                if (!bound_cache_hit)
                    found = OBJ_TYPE(container) == OBJ_CLASS
                                ? class_getMemberHash(AS_CLASS(container), key, hash, &item)
                                : instance_getMemberHash(AS_INSTANCE(container), key, hash, &item);
            }
            else
            {
                found = OBJ_TYPE(container) == OBJ_CLASS
                            ? class_getMember(AS_CLASS(container), key, &item)
                            : instance_getMember(AS_INSTANCE(container), key, &item);
            }
            if (!found)
            {
                vm_errorf(vm, "Member '%s' was not found on %s.", key, type_name(container));
            }
            free(owned_key);
            if (IS_FUN(item) && !bound_cache_hit)
            {
                Object *receiver = NULL;
                if (OBJ_TYPE(container) == OBJ_INSTANCE || FUNC_HAS_FLAG(AS_FUN(item), FUNC_NATIVE))
                    receiver = AS_OBJ(container);
                item = _bind(vm, AS_FUN(item), receiver);

                if (!bracket_access && IS_STRING(index))
                {
                    uint64_t hash = AS_STRING(index)->hash;
                    BoundCache *bound_cache = OBJ_TYPE(container) == OBJ_CLASS
                                                  ? AS_CLASS(container)->bound_cache
                                                  : AS_INSTANCE(container)->bound_cache;
                    uint8_t *next = OBJ_TYPE(container) == OBJ_CLASS
                                        ? &AS_CLASS(container)->bound_cache_next
                                        : &AS_INSTANCE(container)->bound_cache_next;
                    HT_Table *owner_table = NULL;

                    if (OBJ_TYPE(container) == OBJ_INSTANCE)
                        owner_table = AS_INSTANCE(container)->fields &&
                                              _ht_getHash(AS_INSTANCE(container)->fields, key, hash)
                                          ? AS_INSTANCE(container)->fields
                                          : NULL;

                    for (PiClass *current = OBJ_TYPE(container) == OBJ_CLASS
                                                ? AS_CLASS(container)
                                                : AS_INSTANCE(container)->_class;
                         !owner_table && current != NULL; current = current->super)
                    {
                        if (current->members && _ht_getHash(current->members, key, hash))
                            owner_table = current->members;
                    }

                    if (owner_table)
                    {
                        BoundCache *entry = &bound_cache[(*next)++ % BOUND_CACHE_SIZE];
                        entry->key_hash = hash;
                        entry->key = AS_OBJ(index);
                        entry->owner_table = owner_table;
                        entry->owner_version = owner_table->version;
                        entry->class_epoch = class_mutationVersion();
                        entry->fields_version = OBJ_TYPE(container) == OBJ_INSTANCE &&
                                                        AS_INSTANCE(container)->fields
                                                    ? AS_INSTANCE(container)->fields->version
                                                    : 0;
                        entry->bound_fn = item;
                        entry->valid = TRUE;
                    }
                }
            }
            vm->stack[vm->sp - 1] = item;
            VM_DISPATCH_SAFE();
        }

        if (IS_SLICE(index) &&
            (OBJ_TYPE(container) == OBJ_LIST ||
             OBJ_TYPE(container) == OBJ_TUPLE ||
             OBJ_TYPE(container) == OBJ_STRING))
        {
            PiSlice *s = AS_SLICE(index);
            Value slice = get_slice(AS_OBJ(container), s->start, s->stop, s->step);
            vm->stack[vm->sp - 1] = NEW_OBJ(add_obj(vm, AS_OBJ(slice)));
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
                vm->stack[vm->sp - 1] = _bind_nativeMethod(AS_OBJ(container), method);
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
            List *list = AS_LIST(container)->items;
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
        // TODO: check out later!
        case OBJ_MAP:
        {
            PiMap *map = AS_MAP(container);
            vm->stack[vm->sp - 1] = map_get(map, index);
            break;
        }
        case OBJ_MODULE:
        {
            ObjModule *module = AS_MODULE(container);
            char *owned_property = NULL;
            const char *property = IS_STRING(index) ? AS_CSTRING(index) : (owned_property = as_string(index));
            Value item = NEW_NIL();
            if (_is_PrivateModuleName(property))
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
            vm->stack[vm->sp - 1] = *(Value *)_list_getAt(tuple->items, _index);
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
            indices[i] = _pop_stack(vm);

        Value container = _pop_stack(vm);
        if (!IS_TENSOR(container))
            vm_error(vm, "N-dimensional indexing is only supported for tensors.");

        PiTensor *tensor = AS_TENSOR(container);
        if (ndim > tensor->ndim)
            vm_error(vm, "Too many tensor indices.");

        TensorSliceSpec specs[MAX_TENSOR_DIMS];
        bool has_slice = FALSE;

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
                    specs[i].start = isinf(slice->start) ? (sign > 0 ? tensor->shape[i] : -1) : _tensor_sliceBound(tensor->shape[i], slice->start, sign);
                    specs[i].end = isinf(slice->stop) ? (sign > 0 ? tensor->shape[i] : -1) : _tensor_sliceBound(tensor->shape[i], slice->stop, sign);
                    specs[i].count = 0;
                    for (int current = specs[i].start; sign * (specs[i].end - current) > 0; current += specs[i].step)
                        specs[i].count++;
                    has_slice = TRUE;
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
                has_slice = TRUE;
            }
        }

        if (!has_slice)
        {
            int coords[MAX_TENSOR_DIMS];
            for (int i = 0; i < tensor->ndim; i++)
                coords[i] = specs[i].start;
            _push_stack(vm, NEW_NUM(tensor_get(tensor, coords)));
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
            _push_stack(vm, NEW_NUM(tensor_get(tensor, coords)));
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
        _push_stack(vm, NEW_OBJ(result));
        VM_DISPATCH_SAFE();
    }

OP_SET_SLOT:
    {
        uint16_t encoded_slot = (uint16_t)(code[pc++] << 8);
        encoded_slot |= code[pc++];
        if (vm->sp < 2)
            vm_error(vm, "SET_SLOT expects a value and an instance.");
        Value container = vm->stack[vm->sp - 1];
        MemberCache *cache = function && function->body &&
                                     instr_pc < function->body->member_cache_count
                                 ? &function->body->member_caches[instr_pc]
                                 : NULL;

        if (cache && cache->valid && IS_INSTANCE(container))
        {
            PiInstance *instance = AS_INSTANCE(container);
            uint16_t slot = encoded_slot;
            if (instance->_class != cache->cached_class)
            {
                Value name_value = constants_data[cache->name_index];
                if (!IS_STRING(name_value) ||
                    !class_getFieldSlotHash(instance->_class, AS_CSTRING(name_value),
                                            AS_STRING(name_value)->hash, &slot))
                    goto deopt_set_slot;
                cache->cached_class = instance->_class;
                cache->slot = slot;
                code[instr_pc + 1] = (uint8_t)(slot >> 8);
                code[instr_pc + 2] = (uint8_t)slot;
            }
            Value receiver = _pop_stack(vm);
            (void)receiver;
            instance->slots[slot] = _pop_stack(vm);
            VM_DISPATCH_SAFE();
        }

    deopt_set_slot:
        if (!cache || !cache->valid)
            vm_error(vm, "SET_SLOT has no member cache.");
        code[instr_pc] = OP_SET_MEMBER;
        code[instr_pc + 1] = (uint8_t)(cache->name_index >> 8);
        code[instr_pc + 2] = (uint8_t)cache->name_index;
        pc = instr_pc;
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
            index = _pop_stack(vm);
            container = _pop_stack(vm);
            value = _pop_stack(vm);
        }
        else
        {
            uint8_t high = code[pc++];
            uint8_t low = code[pc++];
            uint16_t name_idx = (uint16_t)((high << 8) | low);
            index = constants_data[name_idx];
            container = _pop_stack(vm);
            value = _pop_stack(vm);
        }
        if (!IS_OBJ(container))
            vm_error(vm, "Unsupported operand type for set item operator.\n");

        if (!bracket_access && IS_INSTANCE(container) && IS_STRING(index))
        {
            PiInstance *instance = AS_INSTANCE(container);
            PiString *name = AS_STRING(index);
            uint16_t slot;
            if (class_getFieldSlotHash(instance->_class, name->chars, name->hash, &slot))
            {
                if (function && function->body && instr_pc < function->body->member_cache_count)
                {
                    MemberCache *cache = &function->body->member_caches[instr_pc];
                    cache->cached_class = instance->_class;
                    cache->name_index = (uint16_t)((code[instr_pc + 1] << 8) |
                                                   code[instr_pc + 2]);
                    cache->slot = slot;
                    cache->valid = TRUE;
                    code[instr_pc] = OP_SET_SLOT;
                    code[instr_pc + 1] = (uint8_t)(slot >> 8);
                    code[instr_pc + 2] = (uint8_t)slot;
                }
                instance->slots[slot] = value;
                VM_DISPATCH_SAFE();
            }
        }

        if (OBJ_TYPE(container) == OBJ_CLASS || OBJ_TYPE(container) == OBJ_INSTANCE)
        {
            char *owned_key = NULL;
            const char *key = IS_STRING(index) ? AS_CSTRING(index) : (owned_key = as_string(index));
            if (OBJ_TYPE(container) == OBJ_CLASS)
                class_setMember(AS_CLASS(container), key, value);
            else
                instance_setMember(AS_INSTANCE(container), key, value);
            free(owned_key);
            VM_DISPATCH_SAFE();
        }

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
                    tensor_set(tensor, indices, as_number(*(Value *)_list_getAt(src->items, col)));
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
                _list_setSlice(vm, pi_list, AS_SLICE(index), value);
                break;
            }
            List *list = pi_list->items;
            int _index = get_index(as_number(index), LIST_SIZE(list));
            _list_set(list, _index, &value);
            _list_refreshNumericFlag(pi_list);
            break;
        }
        case OBJ_MAP:
        {
            PiMap *map = AS_MAP(container);

            map_set(map, index, value);
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
            indices[i] = _pop_stack(vm);

        Value container = _pop_stack(vm);
        Value assign_value = _pop_stack(vm);

        if (!IS_TENSOR(container))
            vm_error(vm, "N-dimensional assignment is only supported for tensors.");

        PiTensor *tensor = AS_TENSOR(container);
        if (ndim > tensor->ndim)
            vm_error(vm, "Too many tensor indices.");

        TensorSliceSpec specs[MAX_TENSOR_DIMS];
        bool has_slice = FALSE;

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
                    specs[i].start = isinf(slice->start) ? (sign > 0 ? tensor->shape[i] : -1) : _tensor_sliceBound(tensor->shape[i], slice->start, sign);
                    specs[i].end = isinf(slice->stop) ? (sign > 0 ? tensor->shape[i] : -1) : _tensor_sliceBound(tensor->shape[i], slice->stop, sign);
                    specs[i].count = 0;
                    for (int current = specs[i].start; sign * (specs[i].end - current) > 0; current += specs[i].step)
                        specs[i].count++;
                    has_slice = TRUE;
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
                has_slice = TRUE;
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
            _push_stack(vm, assign_value);
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
                Value item = *(Value *)_list_getAt(src_list->items, flat);
                if (!IS_NUM(item))
                    vm_error(vm, "Assigned list must contain only numeric values.");
                assign_num = as_number(item);
            }
            else
                vm_error(vm, "Tensor slice assignment requires a numeric scalar, numeric list, or tensor.");
            tensor_set(tensor, src_coords, assign_num);
        }
        _push_stack(vm, assign_value);
        VM_DISPATCH_SAFE();
    }

OP_IMPORT:
    {
        Value name = _pop_stack(vm);
        if (!IS_STRING(name))
            vm_error(vm, "Module name must be a string.");
        _push_stack(vm, load_module(vm, AS_STRING(name)->chars));
        VM_DISPATCH_SAFE();
    }

OP_GET_EXPORT:
    {
        Value name = _pop_stack(vm);
        Value module = _pop_stack(vm);

        if (!IS_OBJ(module) || (OBJ_TYPE(module) != OBJ_MAP && OBJ_TYPE(module) != OBJ_MODULE))
            vm_error(vm, "Attempt to access export from non-module object.");

        if (!IS_STRING(name))
            vm_error(vm, "Export name must be a string.");

        char *export_name = AS_STRING(name)->chars;
        if (OBJ_TYPE(module) == OBJ_MODULE && _is_PrivateModuleName(export_name))
            vm_error(vm, "Cannot import private module member.");

        PiMap *_module = (OBJ_TYPE(module) == OBJ_MODULE) ? AS_MODULE(module)->exports : AS_MAP(module);

        _push_stack(vm, map_get(_module, name));

        VM_DISPATCH_SAFE();
    }

OP_IMPORT_ALL:
    {
        Value module = _pop_stack(vm);
        if (!IS_OBJ(module) || (OBJ_TYPE(module) != OBJ_MAP &&
                                OBJ_TYPE(module) != OBJ_MODULE))
            vm_error(vm, "Attempt to import from non-module object.");

        PiMap *_module = (OBJ_TYPE(module) == OBJ_MODULE) ? AS_MODULE(module)->exports : AS_MAP(module);
        HT_Table *table = _module->table;

        HT_Iterator it = _ht_iterator(table);
        while (_ht_next(&it))
        {
            const char *key = it.key;
            Value *value = (Value *)it.value;
            if (!value)
                continue;
            if (OBJ_TYPE(module) == OBJ_MODULE && _is_PrivateModuleName(key))
                continue;
            Value *old_value = _ht_get(vm->globals, key);
            if (old_value && IS_FUN(*old_value))
            {
                AS_FUN(*old_value)->global_valid = FALSE;
                AS_FUN(*old_value)->glonal_index = -1;
            }
            if (!ht_set(vm->globals, key, value))
                ht_put(vm->globals, key, value);

            if (IS_FUN(*value) && AS_FUN(*value)->name &&
                strcmp(AS_FUN(*value)->name, key) == 0)
            {
                AS_FUN(*value)->global_valid = TRUE;
                AS_FUN(*value)->glonal_index = -1;
            }
        }
        vm->global_cache->globals = NULL;
        vm->global_cache->names = NULL;

        VM_DISPATCH_SAFE();
    }

OP_IMPORT_DEFAULT:
    {
        Value name = _pop_stack(vm);
        Value module = _pop_stack(vm);

        if (!IS_OBJ(module) || (OBJ_TYPE(module) != OBJ_MAP && OBJ_TYPE(module) != OBJ_MODULE))
            vm_error(vm, "Attempt to import from non-module object.");

        if (!IS_STRING(name))
            vm_error(vm, "Export name must be a string.");

        char *export_name = AS_STRING(name)->chars;
        if (OBJ_TYPE(module) == OBJ_MODULE && _is_PrivateModuleName(export_name))
            vm_error(vm, "Cannot import private module member.");

        PiMap *_module = (OBJ_TYPE(module) == OBJ_MODULE) ? AS_MODULE(module)->exports : AS_MAP(module);

        Value value = map_get(_module, name);
        _push_stack(vm, map_has(_module, name) ? value : module);

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
            _gc_collect(vm);
        vm->running = FALSE;
        goto L_VM_DONE;
    }

OP_NO:
    VM_DISPATCH_SAFE();

OP_PUSH_NIL:
    _push_stack(vm, NEW_NIL());
    VM_DISPATCH_SAFE();

OP_DEBUG:
    printf("[DEBUG] Current PC: %d\n", pc);
    VM_DISPATCH_SAFE();

OP_PRINT:
    {
        Value value = _pop_stack(vm);
        char *str = as_string(value);
        printf("%s\n", str);
        free(str);
        VM_DISPATCH_SAFE();
    }

L_VM_DONE:
    vm->pc = pc;
}

void free_vm(VM *vm)
{
    if (vm->globals)
        ht_free(vm->globals);
    if (vm->modules)
        ht_free(vm->modules);
    if (vm->project_build)
        px_freeProjectBuild(vm->project_build);

    if (vm->current_path)
        free(vm->current_path);

    pthread_mutex_destroy(&vm->lock);

    free(vm);
}
