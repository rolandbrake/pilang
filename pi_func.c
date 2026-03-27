#include <stdint.h>
#include "pi_func.h"
#include "pi_object.h"
#include "string.h"

/**
 * Create a new function object.
 *
 * This function creates and initializes a new function object, which can be
 * either a user-defined function or a method bound to an instance.
 *
 * @param name The name of the function.
 * @param body The bytecode instructions of the function.
 * @param params The list of parameters for the function.
 * @param upvalues The list of upvalues used by the function.
 * @param instance The bound instance for methods, or NULL for standalone functions.
 * @return A pointer to the newly created function object.
 */
Object *new_func(char *name, ObjCode *body, list_t *params, UpValue **upvalues, Object *instance)
{

    // Allocate and verify memory
    Object *object = (Object *)malloc(sizeof(Function));

    // Initialize object header
    object->type = OBJ_FUN;
    object->is_marked = false;
    object->in_gcList = false;
    object->gc_color = GC_WHITE;
    object->next = NULL;

    Function *fn = (Function *)object;

    // Handle function name (make copy if needed)
    fn->name = name ? strdup(name) : strdup("<FUN>");

    // Handle parameters
    fn->params = params ? params : list_create(sizeof(Value));
    fn->param_names = (body && body->param_names) ? body->param_names : NULL;

    // Set function body
    fn->body = body;
    fn->constants = NULL;
    fn->names = NULL;
    fn->instrs = NULL;

    // Set function flags
    fn->is_native = false;
    fn->is_method = false;
    fn->need_args = true;
    fn->need_kwargs = true;
    fn->native = NULL;

    // Handle upvalues
    fn->upvalues = upvalues;
    fn->instance = instance;
    fn->owner = NULL;

    // Count upvalues
    int count = 0;
    if (upvalues)
        while (upvalues[count] != NULL)
            count++;

    fn->upvalue_count = count;

    return object;
}

/**
 * Create a new native function.
 *
 * A native function is a function that is not user-defined. It is a function
 * that is defined by the interpreter itself. Native functions are used to
 * implement the built-in functions of the language.
 *
 * @param name The name of the native function.
 * @return A new native function.
 */
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

    // Cast the allocated object to Function
    Function *fn = (Function *)val->data.object;

    // Assign function properties
    fn->name = strdup(name); // Allocate and copy name string

    fn->params = NULL;
    fn->body = NULL;
    fn->constants = NULL;
    fn->names = NULL;
    fn->instrs = NULL;

    fn->is_native = true;
    fn->need_args = false; // Native functions don't use the args slot
    fn->need_kwargs = false; // Native functions don't use the kw_args slot
    fn->native = func;

    fn->instance = NULL;
    fn->owner = NULL;

    return val;
}

// Call a Function (default user-defined implementation)
/**
 * Calls a user-defined or native function. The function is either a native
 * function defined by the interpreter or a user-defined function.
 *
 * @param vm The current VM state.
 * @param function The function to call.
 * @param argc The number of arguments to pass to the function.
 * @param argv The arguments to pass to the function.
 * @return The return value of the function.
 */
Value call_func(vm_t *vm, Function *function, size_t argc, Value *argv, Value kw_args)
{
    // If the function is a native function, call it directly
    if (function->is_native)
    {
        if (function->is_method && function->instance != NULL)
        {
            Value *method_argv = malloc(sizeof(Value) * (argc + 1));
            method_argv[0] = NEW_OBJ(add_obj(vm, function->instance));
            for (size_t i = 0; i < argc; i++)
                method_argv[i + 1] = argv[i];

            Value result = function->native(vm, (int)argc + 1, method_argv);
            free(method_argv);
            return result;
        }

        return function->native(vm, argc, argv);
    }

    // Push the current frame onto the call stack
    Frame frame = {
        .pc = vm->pc,
        .sp = vm->sp,
        .bp = vm->bp,
        .ip = vm->ip,
        .code = vm->code,
        .constants = vm->constants,
        .names = vm->names,
        .instrs = vm->instrs,
        .iters_top = vm->iter_sp,
        .function = function};
    push_frame(vm, &frame);

    // Update the VM state with the function's bytecode
    vm->function = (Object *)function;
    vm->code = function->body->data;
    if (function->constants)
        vm->constants = function->constants;
    if (function->names)
        vm->names = function->names;
    if (function->instrs)
        vm->instrs = function->instrs;

    vm->pc = 0;
    vm->ip = 0;
    vm->bp = vm->sp;
    size_t param_count = list_size(function->params);
    size_t aux_base = vm->bp + param_count;
    vm->sp = aux_base;

    size_t arg_offset = 0;
    size_t param_offset = 0;
    Value instance = NEW_NIL();

    // Bind the function instance (if present)
    if (function->is_method)
    {
        instance = function->instance == NULL ? NEW_NIL() : NEW_OBJ(add_obj(vm, function->instance));
    }

    // check if [this] instance is part of the parameters list
    bool param_this = false;
    if (function->is_method && function->param_names &&
        list_size(function->param_names) + 1 == (int)param_count)
        param_this = true;

    if (function->is_method && !param_this)
    {
        vm->stack[vm->bp] = instance;
        arg_offset = 1;
    }
    else if (param_this)
    {
        arg_offset = 0;
        param_offset = 1;
    }

    // Set function parameters and arguments
    Value *param_vals = NULL;
    if (param_count > 0)
    {
        param_vals = malloc(sizeof(Value) * param_count);
        for (size_t i = 0; i < param_count; i++)
        {
            Value _default = *(Value *)list_getAt(function->params, i);
            param_vals[i] = _default;
        }
    }

    if (param_this && param_count > 0)
        param_vals[0] = instance;

    // Positional arguments
    size_t positional_count = argc;
    if (positional_count > param_count)
        positional_count = param_count;
    for (size_t i = 0; i < positional_count; i++)
    {
        size_t slot = i + param_offset;
        if (slot < param_count)
        {
            param_vals[slot] = argv[i];
        }
    }

    for (size_t i = 0; i < param_count; i++)
        vm->stack[vm->bp + arg_offset + i] = param_vals[i];

    if (param_vals)
        free(param_vals);

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

    if (function->need_kwargs)
    {
        if (IS_OBJ(kw_args) && OBJ_TYPE(kw_args) == OBJ_MAP)
            vm->stack[aux_base + 1] = kw_args;
        else
            vm->stack[aux_base + 1] = NEW_OBJ(add_obj(vm, new_map(ht_create(sizeof(Value)), false)));
    }
    else
        vm->stack[aux_base + 1] = NEW_NIL();

    vm->sp = aux_base + 2;

    run(vm);

    // Pop and return the value pushed by OP_RETURN in the caller frame.
    if (vm->sp <= 0)
        vm_error(vm, "Stack underflow: Attempted to pop from an empty stack");
    return vm->stack[--vm->sp];
}

/**
 * Call a function with variable arguments.
 *
 * This function is a wrapper around the regular call_func function that takes
 * a variable number of arguments.
 *
 * @param vm The current VM state.
 * @param function The function to call.
 * @param argc The number of arguments to pass to the function.
 * @param ... The arguments to pass to the function.
 * @return The return value of the function.
 */
Value call_funcv(vm_t *vm, Function *function, size_t argc, ...)
{
    va_list args;
    va_start(args, argc);

    Value *argv = malloc(sizeof(Value) * argc);
    for (size_t i = 0; i < argc; i++)
        argv[i] = va_arg(args, Value);

    va_end(args);

    // Call the function with the prepared arguments
    Value result = call_func(vm, function, argc, argv, NEW_NIL());

    // Clean up after ourselves
    free(argv);

    return result;
}
/**
 * Free the memory allocated for a function.
 *
 * @param fn The function to free.
 */
void free_func(Function *fn)
{
    free(fn->name);        // Free the function name
    list_free(fn->params); // Free the parameter list
}


