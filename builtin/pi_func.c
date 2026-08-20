#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "pi_func.h"
#include "../pi_func.h"
#include "../pi_list.h"
#include "../pi_object.h"
#include "../pi_table.h"
#include "../pi_value.h"
#include "pi_builtin.h"

static double now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static Function *require_fun(vm_t *vm, Value value, const char *name)
{
    if (!IS_FUN(value))
        vm_errorf(vm, "%s expects a function.", name);
    return AS_FUN(value);
}

static PiList *require_list(vm_t *vm, Value value, const char *name)
{
    if (!IS_LIST(value))
        vm_errorf(vm, "%s expects a list.", name);
    return AS_LIST(value);
}

// Function wrappers store their closed-over data in a small map attached to Function::instance.
static Value state_get(vm_t *vm, PiMap *state, const char *key, const Value fallback)
{
    Value *value = (Value *)ht_get(state->table, key);
    return value ? *value : fallback;
}

static void state_set(PiMap *state, const char *key, Value value)
{
    if (ht_set(state->table, key, &value) || ht_put(state->table, key, &value))
        map_dirty(state);
}

// Native wrapper callbacks recover their closure state from the currently executing function.
static PiMap *current_stateMap(vm_t *vm, const char *name)
{
    if (vm->function == NULL || vm->function->type != OBJ_FUN)
        vm_errorf(vm, "%s: invalid function context.", name);

    Function *fn = (Function *)vm->function;
    if (fn->instance == NULL || fn->instance->type != OBJ_MAP)
        vm_errorf(vm, "%s: missing function state.", name);

    return (PiMap *)fn->instance;
}

// Creates a native Function object that behaves like a normal callable but carries state.
static Value make_nativeWrapper(vm_t *vm, const char *name, native_func func, Object *state)
{
    Object *obj = new_func((char *)name, NULL, NULL, NULL, state);
    Function *fn = (Function *)obj;
    fn->is_native = true;
    fn->native = func;
    fn->need_args = false;
    fn->need_kwargs = false;
    add_obj(vm, obj);
    return NEW_OBJ(obj);
}

// Builds a VM-owned list from raw Value arguments and preserves numeric-list metadata.
static Value new_valueList(vm_t *vm, int count, Value *items)
{
    list_t *list = list_create(sizeof(Value));
    bool is_numeric = true;

    for (int i = 0; i < count; i++)
    {
        list_add(list, &items[i]);
        if (!IS_NUM(items[i]))
            is_numeric = false;
    }

    PiList *result = (PiList *)add_obj(vm, new_list(list));
    result->is_numeric = is_numeric;
    return NEW_OBJ(result);
}

static Value clone_argList(vm_t *vm, Value *argv, int argc)
{
    return new_valueList(vm, argc, argv);
}

static int inferred_arity(Function *fn)
{
    return fn->params ? list_size(fn->params) : 0;
}

Value _pi_map(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_LIST(argv[0]) || !IS_FUN(argv[1]))
        vm_error(vm, "map(list, fn): expects a list and a function");

    PiList *input = AS_LIST(argv[0]);
    Function *fn = AS_FUN(argv[1]);
    list_t *list = list_create(sizeof(Value));

    int size = input->items->size;
    for (int i = 0; i < size; i++)
    {
        Value *item = (Value *)list_getAt(input->items, i);
        Value ret_val = call_func(vm, fn, 1, item, NEW_NIL());
        list_add(list, &ret_val);
    }

    PiList *result = (PiList *)new_list(list);
    result->is_numeric = false;

    result->is_numeric = true;

    size = result->items->size;
    for (int i = 0; i < size; i++)
    {
        Value item = *(Value *)list_getAt(result->items, i);
        if (!IS_NUM(item))
        {
            result->is_numeric = false;
            break;
        }
    }

    return NEW_OBJ(result);
}

Value pi_filter(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_LIST(argv[0]) || !IS_FUN(argv[1]))
        vm_error(vm, "filter(list, fn): expects a list and a function");

    PiList *input = AS_LIST(argv[0]);
    Function *fn = AS_FUN(argv[1]);
    list_t *list = list_create(sizeof(Value));

    int size = input->items->size;
    for (int i = 0; i < size; i++)
    {
        Value *item = (Value *)list_getAt(input->items, i);
        Value ret_val = call_func(vm, fn, 1, item, NEW_NIL());
        if (as_bool(ret_val))
            list_add(list, item);
    }

    PiList *result = (PiList *)new_list(list);
    result->is_numeric = input->is_numeric;

    return NEW_OBJ(result);
}

Value pi_reduce(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_LIST(argv[0]) || !IS_FUN(argv[1]))
        vm_error(vm, "reduce(list, fn, [initial]): expects a list, a function, and optional initial value");

    PiList *input = AS_LIST(argv[0]);
    Function *fn = AS_FUN(argv[1]);
    if (argc < 3 && input->items->size == 0)
        vm_error(vm, "reduce(list, fn): cannot reduce an empty list without an initial value");

    Value acc = (argc >= 3) ? argv[2] : *(Value *)list_getAt(input->items, 0);
    int start = (argc >= 3) ? 0 : 1;

    int size = input->items->size;
    for (int i = start; i < size; i++)
    {
        Value item = *(Value *)list_getAt(input->items, i);
        acc = call_funcv(vm, fn, 2, acc, item);
    }

    return acc;
}

Value pi_find(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_FUN(argv[1]))
        vm_error(vm, "[find] expects two arguments: a function and a collection.");

    Value collection = argv[0];
    Function *fn = AS_FUN(argv[1]);

    if (IS_LIST(collection))
    {
        PiList *list = AS_LIST(collection);
        for (int i = 0; i < list->items->size; i++)
        {
            Value *item = (Value *)list_getAt(list->items, i);
            Value result = call_func(vm, fn, 1, item, NEW_NIL());
            if (as_bool(result))
                return NEW_NUM(i);
        }
    }
    else if (IS_STRING(collection))
    {
        PiString *str = AS_STRING(collection);
        char ch[2] = {'\0', '\0'};
        for (int i = 0; i < str->length; i++)
        {
            ch[0] = str->chars[i];
            Value arg = NEW_OBJ(add_obj(vm, new_pistring(strdup(ch))));
            Value result = call_func(vm, fn, 1, &arg, NEW_NIL());
            if (as_bool(result))
                return NEW_NUM(i);
        }
    }
    else
        vm_error(vm, "[find] Second argument must be a list or a string.");

    return NEW_NUM(-1);
}

// compose(f, g, h)(x) calls h first, then g, then f.
static Value fn_composeCall(vm_t *vm, int argc, Value *argv)
{
    PiList *fns = require_list(vm, state_get(vm, current_stateMap(vm, "compose"), "fns", NEW_NIL()), "compose");
    int size = fns->items->size;
    Function *fn = AS_FUN(*(Value *)list_getAt(fns->items, size - 1));
    Value result = call_func(vm, fn, argc, argv, NEW_NIL());

    for (int i = size - 2; i >= 0; i--)
    {
        fn = AS_FUN(*(Value *)list_getAt(fns->items, i));
        result = call_funcv(vm, fn, 1, result);
    }

    return result;
}

Value fn_compose(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "compose(fn, ...): expects at least one function");

    list_t *fns = list_create(sizeof(Value));
    for (int i = 0; i < argc; i++)
    {
        require_fun(vm, argv[i], "compose");
        list_add(fns, &argv[i]);
    }

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    state_set(state, "fns", NEW_OBJ(add_obj(vm, new_list(fns))));
    return make_nativeWrapper(vm, "compose", fn_composeCall, (Object *)state);
}

// pipe(f, g, h)(x) calls f first, then g, then h.
static Value fn_pipeCall(vm_t *vm, int argc, Value *argv)
{
    PiList *fns = require_list(vm, state_get(vm, current_stateMap(vm, "pipe"), "fns", NEW_NIL()), "pipe");
    int size = fns->items->size;
    Function *fn = AS_FUN(*(Value *)list_getAt(fns->items, 0));
    Value result = call_func(vm, fn, argc, argv, NEW_NIL());

    for (int i = 1; i < size; i++)
    {
        fn = AS_FUN(*(Value *)list_getAt(fns->items, i));
        result = call_funcv(vm, fn, 1, result);
    }

    return result;
}

Value fn_pipe(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "pipe(fn, ...): expects at least one function");

    list_t *fns = list_create(sizeof(Value));
    for (int i = 0; i < argc; i++)
    {
        require_fun(vm, argv[i], "pipe");
        list_add(fns, &argv[i]);
    }

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    state_set(state, "fns", NEW_OBJ(add_obj(vm, new_list(fns))));
    return make_nativeWrapper(vm, "pipe", fn_pipeCall, (Object *)state);
}

static Value fn_juxtCall(vm_t *vm, int argc, Value *argv)
{
    PiList *fns = require_list(vm, state_get(vm, current_stateMap(vm, "juxt"), "fns", NEW_NIL()), "juxt");
    list_t *results = list_create(sizeof(Value));
    bool is_numeric = true;

    for (int i = 0; i < fns->items->size; i++)
    {
        Function *fn = AS_FUN(*(Value *)list_getAt(fns->items, i));
        Value result = call_func(vm, fn, argc, argv, NEW_NIL());
        list_add(results, &result);
        if (!IS_NUM(result))
            is_numeric = false;
    }

    PiList *out = (PiList *)add_obj(vm, new_list(results));
    out->is_numeric = is_numeric;
    return NEW_OBJ(out);
}

Value fn_juxt(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "juxt(fn, ...): expects at least one function");

    list_t *fns = list_create(sizeof(Value));
    for (int i = 0; i < argc; i++)
    {
        require_fun(vm, argv[i], "juxt");
        list_add(fns, &argv[i]);
    }

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    state_set(state, "fns", NEW_OBJ(add_obj(vm, new_list(fns))));
    return make_nativeWrapper(vm, "juxt", fn_juxtCall, (Object *)state);
}

// Each partial curry call returns a new wrapper until the required arity is reached.
static Value fn_curryCall(vm_t *vm, int argc, Value *argv)
{
    PiMap *state = current_stateMap(vm, "curry");
    Value fn_value = state_get(vm, state, "fn", NEW_NIL());
    PiList *collected = require_list(vm, state_get(vm, state, "args", NEW_NIL()), "curry");
    int arity = (int)as_number(state_get(vm, state, "arity", NEW_NUM(0)));

    int total = collected->items->size + argc;
    Value *all_args = malloc(sizeof(Value) * total);
    for (int i = 0; i < collected->items->size; i++)
        all_args[i] = *(Value *)list_getAt(collected->items, i);
    for (int i = 0; i < argc; i++)
        all_args[collected->items->size + i] = argv[i];

    if (total >= arity)
    {
        Value result = call_func(vm, AS_FUN(fn_value), total, all_args, NEW_NIL());
        free(all_args);
        return result;
    }

    Value arg_list = new_valueList(vm, total, all_args);
    free(all_args);

    PiMap *next = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    state_set(next, "fn", fn_value);
    state_set(next, "arity", NEW_NUM(arity));
    state_set(next, "args", arg_list);

    return make_nativeWrapper(vm, "curry", fn_curryCall, (Object *)next);
}

Value fn_curry(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "curry(fn, [arity]): expects a function and optional arity");

    Function *fn = require_fun(vm, argv[0], "curry");
    if (argc >= 2 && !IS_NUM(argv[1]))
        vm_error(vm, "curry(fn, [arity]): arity must be a number");
    int arity = (argc >= 2) ? (int)as_number(argv[1]) : inferred_arity(fn);
    if (arity <= 0)
        vm_error(vm, "curry(fn, [arity]): arity must be positive");

    list_t *empty = list_create(sizeof(Value));
    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    state_set(state, "fn", argv[0]);
    state_set(state, "arity", NEW_NUM(arity));
    state_set(state, "args", NEW_OBJ(add_obj(vm, new_list(empty))));

    return make_nativeWrapper(vm, "curry", fn_curryCall, (Object *)state);
}

static Value fn_partialCall(vm_t *vm, int argc, Value *argv)
{
    PiMap *state = current_stateMap(vm, "partial");
    Value fn_value = state_get(vm, state, "fn", NEW_NIL());
    PiList *bound = require_list(vm, state_get(vm, state, "args", NEW_NIL()), "partial");

    int total = bound->items->size + argc;
    Value *all_args = malloc(sizeof(Value) * total);
    for (int i = 0; i < bound->items->size; i++)
        all_args[i] = *(Value *)list_getAt(bound->items, i);
    for (int i = 0; i < argc; i++)
        all_args[bound->items->size + i] = argv[i];

    Value result = call_func(vm, AS_FUN(fn_value), total, all_args, NEW_NIL());
    free(all_args);
    return result;
}

Value fn_partial(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "partial(fn, ...args): expects at least one function");

    require_fun(vm, argv[0], "partial");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    state_set(state, "fn", argv[0]);
    state_set(state, "args", clone_argList(vm, argv + 1, argc - 1));
    return make_nativeWrapper(vm, "partial", fn_partialCall, (Object *)state);
}

static Value fn_spreadCall(vm_t *vm, int argc, Value *argv)
{
    PiMap *state = current_stateMap(vm, "spread");
    Value fn_value = state_get(vm, state, "fn", NEW_NIL());

    if (argc < 1 || !IS_LIST(argv[0]))
        vm_error(vm, "spread(fn): returned function expects a single list argument");

    PiList *items = AS_LIST(argv[0]);
    int count = items->items->size;
    Value *args = malloc(sizeof(Value) * count);
    for (int i = 0; i < count; i++)
        args[i] = *(Value *)list_getAt(items->items, i);

    Value result = call_func(vm, AS_FUN(fn_value), count, args, NEW_NIL());
    free(args);
    return result;
}

Value fn_spread(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "spread(fn): expects exactly one function");

    require_fun(vm, argv[0], "spread");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    state_set(state, "fn", argv[0]);
    return make_nativeWrapper(vm, "spread", fn_spreadCall, (Object *)state);
}

static Value fn_unspreadCall(vm_t *vm, int argc, Value *argv)
{
    PiMap *state = current_stateMap(vm, "unspread");
    Value fn_value = state_get(vm, state, "fn", NEW_NIL());
    Value packed = clone_argList(vm, argv, argc);
    return call_funcv(vm, AS_FUN(fn_value), 1, packed);
}

Value fn_unspread(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "unspread(fn): expects exactly one function");

    require_fun(vm, argv[0], "unspread");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    state_set(state, "fn", argv[0]);
    return make_nativeWrapper(vm, "unspread", fn_unspreadCall, (Object *)state);
}

// Cache keys are generated either by key_fn(...) or by stringifying the argument list.
static Value fn_memoizeCall(vm_t *vm, int argc, Value *argv)
{
    PiMap *state = current_stateMap(vm, "memoize");
    Value fn_value = state_get(vm, state, "fn", NEW_NIL());
    Value key_fn = state_get(vm, state, "key_fn", NEW_NIL());
    PiMap *cache = AS_MAP(state_get(vm, state, "cache", NEW_NIL()));

    Value key_value = IS_FUN(key_fn)
                          ? call_func(vm, AS_FUN(key_fn), argc, argv, NEW_NIL())
                          : clone_argList(vm, argv, argc);

    char *key = as_string(key_value);
    Value *cached = (Value *)ht_get(cache->table, key);
    if (cached)
    {
        free(key);
        return *cached;
    }

    Value result = call_func(vm, AS_FUN(fn_value), argc, argv, NEW_NIL());
    ht_put(cache->table, key, &result);
    free(key);
    return result;
}

Value fn_memoize(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "memoize(fn, [key_fn]): expects a function and optional key function");

    require_fun(vm, argv[0], "memoize");
    if (argc >= 2)
        require_fun(vm, argv[1], "memoize");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    state_set(state, "fn", argv[0]);
    state_set(state, "key_fn", argc >= 2 ? argv[1] : NEW_NIL());
    state_set(state, "cache", NEW_OBJ(add_obj(vm, new_map(ht_create(sizeof(Value)), false))));
    return make_nativeWrapper(vm, "memoize", fn_memoizeCall, (Object *)state);
}

static Value fn_onceCall(vm_t *vm, int argc, Value *argv)
{
    PiMap *state = current_stateMap(vm, "once");
    if (as_bool(state_get(vm, state, "done", NEW_BOOL(false))))
        return state_get(vm, state, "result", NEW_NIL());

    Value fn_value = state_get(vm, state, "fn", NEW_NIL());
    Value result = call_func(vm, AS_FUN(fn_value), argc, argv, NEW_NIL());
    state_set(state, "done", NEW_BOOL(true));
    state_set(state, "result", result);
    return result;
}

Value fn_once(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "once(fn): expects exactly one function");

    require_fun(vm, argv[0], "once");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    state_set(state, "fn", argv[0]);
    state_set(state, "done", NEW_BOOL(false));
    state_set(state, "result", NEW_NIL());
    return make_nativeWrapper(vm, "once", fn_onceCall, (Object *)state);
}

static Value fn_throttleCall(vm_t *vm, int argc, Value *argv)
{
    PiMap *state = current_stateMap(vm, "throttle");
    double ms = as_number(state_get(vm, state, "ms", NEW_NUM(0)));
    double last = as_number(state_get(vm, state, "last", NEW_NUM(-1)));

    if (last >= 0 && now_ms() - last < ms)
        return state_get(vm, state, "result", NEW_NIL());

    Value fn_value = state_get(vm, state, "fn", NEW_NIL());
    Value result = call_func(vm, AS_FUN(fn_value), argc, argv, NEW_NIL());
    state_set(state, "last", NEW_NUM(now_ms()));
    state_set(state, "result", result);
    return result;
}

Value fn_throttle(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_NUM(argv[0]))
        vm_error(vm, "throttle(ms, fn): expects a number and a function");

    require_fun(vm, argv[1], "throttle");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    state_set(state, "ms", argv[0]);
    state_set(state, "fn", argv[1]);
    state_set(state, "last", NEW_NUM(-1));
    state_set(state, "result", NEW_NIL());
    return make_nativeWrapper(vm, "throttle", fn_throttleCall, (Object *)state);
}

// This debounce is synchronous: rapid calls reuse the previous result instead of scheduling a delayed call.
static Value fn_debounceCall(vm_t *vm, int argc, Value *argv)
{
    PiMap *state = current_stateMap(vm, "debounce");
    double ms = as_number(state_get(vm, state, "ms", NEW_NUM(0)));
    double last = as_number(state_get(vm, state, "last_call", NEW_NUM(-1)));
    double now = now_ms();

    state_set(state, "last_call", NEW_NUM(now));
    if (last >= 0 && now - last < ms)
        return state_get(vm, state, "result", NEW_NIL());

    Value fn_value = state_get(vm, state, "fn", NEW_NIL());
    Value result = call_func(vm, AS_FUN(fn_value), argc, argv, NEW_NIL());
    state_set(state, "result", result);
    return result;
}

Value fn_debounce(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_NUM(argv[0]))
        vm_error(vm, "debounce(ms, fn): expects a number and a function");

    require_fun(vm, argv[1], "debounce");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    state_set(state, "ms", argv[0]);
    state_set(state, "fn", argv[1]);
    state_set(state, "last_call", NEW_NUM(-1));
    state_set(state, "result", NEW_NIL());
    return make_nativeWrapper(vm, "debounce", fn_debounceCall, (Object *)state);
}

static Value fn_thunkCall(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;

    PiMap *state = current_stateMap(vm, "thunk");
    Value fn_value = state_get(vm, state, "fn", NEW_NIL());
    PiList *bound = require_list(vm, state_get(vm, state, "args", NEW_NIL()), "thunk");

    int count = bound->items->size;
    Value *args = malloc(sizeof(Value) * count);
    for (int i = 0; i < count; i++)
        args[i] = *(Value *)list_getAt(bound->items, i);

    Value result = call_func(vm, AS_FUN(fn_value), count, args, NEW_NIL());
    free(args);
    return result;
}

Value fn_thunk(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "thunk(fn, ...args): expects at least one function");

    require_fun(vm, argv[0], "thunk");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    state_set(state, "fn", argv[0]);
    state_set(state, "args", clone_argList(vm, argv + 1, argc - 1));
    return make_nativeWrapper(vm, "thunk", fn_thunkCall, (Object *)state);
}

// Returns the current value, then advances the stored state with fn(current).
static Value fn_iterateCall(vm_t *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;

    PiMap *state = current_stateMap(vm, "iterate");
    Value current = state_get(vm, state, "current", NEW_NIL());
    Value fn_value = state_get(vm, state, "fn", NEW_NIL());
    Value next = call_funcv(vm, AS_FUN(fn_value), 1, current);
    state_set(state, "current", next);
    return current;
}

Value fn_iterate(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "iterate(seed, fn): expects a seed value and a function");

    require_fun(vm, argv[1], "iterate");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value)), false));
    state_set(state, "current", argv[0]);
    state_set(state, "fn", argv[1]);
    return make_nativeWrapper(vm, "iterate", fn_iterateCall, (Object *)state);
}

Value fn_apply(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_FUN(argv[0]) || !IS_LIST(argv[1]))
        vm_error(vm, "apply(fn, args): expects a function and a list");

    PiList *items = AS_LIST(argv[1]);
    int count = items->items->size;
    Value *args = malloc(sizeof(Value) * count);
    for (int i = 0; i < count; i++)
        args[i] = *(Value *)list_getAt(items->items, i);

    Value result = call_func(vm, AS_FUN(argv[0]), count, args, NEW_NIL());
    free(args);
    return result;
}

Value fn_noop(vm_t *vm, int argc, Value *argv)
{
    (void)vm;
    (void)argc;
    (void)argv;
    return NEW_NIL();
}

// Expose the higher-order function helpers as the `func` builtin module.
static BuiltinFunc func_funcs[] = {
    {"compose", fn_compose},
    {"pipe", fn_pipe},
    {"juxt", fn_juxt},
    {"curry", fn_curry},
    {"partial", fn_partial},
    {"spread", fn_spread},
    {"unspread", fn_unspread},
    {"memoize", fn_memoize},
    {"once", fn_once},
    {"throttle", fn_throttle},
    {"debounce", fn_debounce},
    {"thunk", fn_thunk},
    {"iterate", fn_iterate},
    {"apply", fn_apply},
    {"noop", fn_noop},
};

DEFINE_BUILTIN_MODULE(module_func, "func", func_funcs, NULL);
