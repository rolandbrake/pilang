#include <stdint.h>
#include "pi_func.h"
#include "pi_object.h"
#include "pi_string.h"

Object *new_func(char *name, ObjCode *body, list_t *params, UpValue **upvalues, Object *instance)
{

    Object *object = (Object *)malloc(sizeof(Function));

    object->type = OBJ_FUN;
    object->is_marked = false;
    object->in_gcList = false;
    object->gc_color = GC_WHITE;
    object->next = NULL;

    Function *fn = (Function *)object;

    fn->name = name ? strdup(name) : strdup("<FUN>");

    fn->params = params ? params : list_create(sizeof(Value));
    fn->arity = fn->params ? fn->params->size : 0;
    // Parameter names are shared with the compiled function body.
    fn->param_names = (body && body->param_names) ? body->param_names : NULL;
    fn->owns_params = true;

    fn->body = body;
    fn->constants = NULL;
    fn->names = NULL;
    fn->instrs = NULL;

    fn->is_native = false;
    fn->is_method = false;
    fn->need_args = true;
    fn->need_kwargs = true;
    fn->global_valid = false;
    fn->glonal_index = -1;
    fn->native = NULL;
    fn->globals = NULL;

    fn->upvalues = upvalues;
    fn->owns_upvalues = upvalues != NULL;
    fn->instance = instance;
    fn->owner = NULL;

    int count = 0;
    if (upvalues)
        while (upvalues[count] != NULL)
            count++;

    fn->upvalue_count = count;

    return object;
}

Value *new_native(const char *name, native_func func)
{

    Value *val = malloc(sizeof(Value));
    val->type = VAL_OBJ;
    val->data.object = (Object *)malloc(sizeof(Function));

    val->data.object->type = OBJ_FUN;
    val->data.object->is_marked = true;
    val->data.object->in_gcList = false;
    val->data.object->gc_color = GC_WHITE;
    val->data.object->next = NULL;

    Function *fn = (Function *)val->data.object;

    fn->name = strdup(name); // Allocate and copy name string

    fn->params = NULL;
    fn->arity = 0;
    fn->param_names = NULL;
    fn->owns_params = false;

    fn->body = NULL;
    fn->constants = NULL;
    fn->names = NULL;

    fn->instrs = NULL;
    fn->globals = NULL;

    fn->is_native = true;
    fn->is_method = false;

    fn->need_args = false;
    fn->need_kwargs = false;
    fn->global_valid = false;
    fn->glonal_index = -1;
    fn->native = func;

    fn->upvalues = NULL;
    fn->upvalue_count = 0;
    fn->owns_upvalues = false;
    fn->instance = NULL;
    fn->owner = NULL;

    return val;
}

/**
 * Calls a Pilang function, setting up the call frame, stack layout,
 * parameter defaults, positional args, and implicit args/kwargs locals.
 *
 * Stack layout on entry to the callee:
 *
 *   bp+0          : this (methods only, when !param_this)
 *   bp+arg_offset : param_0 ... param_N   (defaults then overwritten by args)
 *   aux_base+0    : args  (list, or NIL if need_args is false)
 *   aux_base+1    : kwargs (map, or NIL if need_kwargs is false)
 */
Value call_func(vm_t *vm, Function *function, size_t argc, Value *argv, Value kw_args)
{

    // Native fast path
    if (function->is_native)
    {
        Object *prev_function = vm->function;
        Value prev_kwargs = vm->_kw_args;

        vm->function = (Object *)function;
        vm->_kw_args = kw_args;

        /* Bound native methods receive the instance as argv[0]. */
        if (function->is_method && function->instance != NULL)
        {
            /*
             * Methods such as list.push() are commonly called in tight loops.
             * Do not make each call pay for a heap allocation merely to prepend
             * the bound instance.  Eight arguments is also the VM call site's
             * inline-argument capacity; larger, uncommon calls keep the heap
             * fallback.
             */
            Value inline_argv[9];
            Value *method_argv = argc <= 8
                                     ? inline_argv
                                     : malloc(sizeof(Value) * (argc + 1));
            if (!method_argv)
                vm_error(vm, "Memory allocation failed for native method arguments.");

            method_argv[0] = NEW_OBJ(function->instance);
            for (size_t i = 0; i < argc; i++)
                method_argv[i + 1] = argv[i];

            Value result = function->native(vm, (int)argc + 1, method_argv);
            if (argc > 8)
                free(method_argv);

            vm->_kw_args = prev_kwargs;
            vm->function = prev_function;
            return result;
        }

        Value result = function->native(vm, argc, argv);
        vm->_kw_args = prev_kwargs;
        vm->function = prev_function;
        return result;
    }

    // save caller frame
    if (vm->frame_sp >= STACK_MAX)
        vm_error(vm, "[frame] Stack overflow.");

    Frame *frame = &vm->frames[vm->frame_sp++];
    frame->pc = vm->pc;
    frame->sp = vm->sp;
    frame->bp = vm->bp;
    frame->ip = vm->ip;
    frame->code = vm->code;
    frame->constants = vm->constants;
    frame->names = vm->names;
    frame->instrs = vm->instrs;
    frame->iters_top = vm->iter_sp;
    frame->globals = vm->globals;
    frame->global_cache = vm->global_cache;
    frame->function = (Function *)vm->function; /* save BEFORE overwrite */
    frame->same_context = false;

    // switch to callee context
    vm->function = (Object *)function;
    vm->code = function->body->data;
    vm->global_cache = &function->body->global_cache;

    /* Only override if the function carries its own tables. */
    if (function->constants)
        vm->constants = function->constants;
    if (function->names)
        vm->names = function->names;
    if (function->instrs)
        vm->instrs = function->instrs;
    if (function->globals)
        vm->globals = function->globals;

    vm->pc = 0;
    vm->ip = 0;
    vm->bp = vm->sp;

    // Resolve param count and stack layout offsets
    size_t param_count = (size_t)function->arity;
    size_t arg_offset = 0;   /* extra leading slot for implicit `this` */
    size_t param_offset = 0; /* skip slot 0 when `this` is a named param */
    Value instance = NEW_NIL();

    if (function->is_method)
    {
        if (function->instance != NULL)
            instance = NEW_OBJ(add_obj(vm, function->instance));

        /* param_this: user declared `this` explicitly in the param list. */
        bool param_this = function->param_names &&
                          (size_t)list_size(function->param_names) + 1 == param_count;

        if (!param_this)
        {
            vm->stack[vm->bp] = instance; /* implicit this at slot 0 */
            arg_offset = 1;
        }
        else
            param_offset = 1; /* named this occupies param slot 0; skip for positionals */
    }

    // set up stack frame
    size_t param_base = vm->bp + arg_offset;
    size_t aux_base = param_base + param_count;

    /*
     * Defaults are only needed for parameters not supplied positionally.
     * Recursive numeric functions commonly pass every parameter, so avoid
     * rewriting those slots on the hot path.
     */
    if (argc < param_count)
    {
        Value *defaults = (Value *)function->params->data;
        for (size_t i = argc + param_offset; i < param_count; i++)
            vm->stack[param_base + i] = defaults[i];
    }

    /* Overwrite slot 0 with instance when this is a named param. */
    if (function->is_method && param_offset == 1 && param_count > 0)
        vm->stack[param_base] = instance;

    // Copy positional arguments
    if (argc > 0)
    {
        size_t positional_count = argc < param_count ? argc : param_count;
        for (size_t i = 0; i < positional_count; i++)
        {
            size_t slot = i + param_offset;
            if (slot < param_count)
                vm->stack[param_base + slot] = argv[i];
        }
    }

    // Apply keyword arguments overrides
    if (!IS_NIL(kw_args) && IS_MAP(kw_args) && function->param_names)
    {
        PiMap *kw_map = AS_MAP(kw_args);
        int name_count = list_size(function->param_names);

        for (int i = 0; i < name_count; i++)
        {
            char *param_name = string_get(function->param_names, i);
            Value *kw_value = ht_get(kw_map->table, param_name);
            if (!kw_value)
                continue;

            size_t slot = (size_t)i + param_offset;
            if (slot >= param_count)
                continue;

            /* Reject: same slot already filled by a positional arg. */
            if (slot < argc + param_offset)
                vm_error(vm, "Function argument got multiple values.");

            vm->stack[param_base + slot] = *kw_value;
        }
    }

    if (!function->need_args && !function->need_kwargs)
    {
        vm->sp = aux_base + 2;
        goto execute;
    }

    // Implicit args and kwargs locals
    if (function->need_args)
    {
        list_t *_args = list_create(sizeof(Value));
        if (function->is_method)
            list_add(_args, &instance);
        for (size_t i = 0; i < argc; i++)
            list_add(_args, &argv[i]);
        vm->stack[aux_base] = NEW_OBJ(add_obj(vm, new_list(_args)));
    }
    else
        vm->stack[aux_base] = NEW_NIL();

    vm->stack[aux_base + 1] = function->need_kwargs
                                  ? ((IS_OBJ(kw_args) && OBJ_TYPE(kw_args) == OBJ_MAP)
                                         ? kw_args
                                         : NEW_OBJ(add_obj(vm, new_map(ht_create(sizeof(Value)), false))))
                                  : NEW_NIL();

    vm->sp = aux_base + 2;

execute:
    // Execute callee
    run(vm);

    /* Return value sits on top of stack after run() returns. */
    if (vm->sp <= 0)
        vm_error(vm, "Stack underflow after function return.");
    return vm->stack[--vm->sp];
}

Value call_funcv(vm_t *vm, Function *function, size_t argc, ...)
{
    va_list args;
    va_start(args, argc);

    Value *argv = malloc(sizeof(Value) * argc);
    for (size_t i = 0; i < argc; i++)
        argv[i] = va_arg(args, Value);

    va_end(args);

    Value result = call_func(vm, function, argc, argv, NEW_NIL());

    free(argv);

    return result;
}
// Frees only resources owned directly by the Function object.
void free_func(Function *fn)
{
    free(fn->name);
    if (fn->owns_params && fn->params)
        list_free(fn->params); // Free the parameter list
    if (fn->owns_upvalues && fn->upvalues)
    {
        for (int i = 0; i < fn->upvalue_count; i++)
        {
            UpValue *upvalue = fn->upvalues[i];
            if (upvalue && --upvalue->ref_count <= 0)
                free(upvalue);
        }
        free(fn->upvalues);
    }
}
