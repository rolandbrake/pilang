#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "pi_func.h"
#include "../src/pi_func.h"
#include "../src/pi_list.h"
#include "../src/pi_object.h"
#include "../src/pi_table.h"
#include "../src/pi_value.h"
#include "pi_builtin.h"

static double _now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static Function *_require_fun(VM *vm, Value value, const char *name)
{
    if (!IS_FUN(value))
        vm_errorf(vm, "%s expects a function.", name);
    return AS_FUN(value);
}

static PiList *_require_list(VM *vm, Value value, const char *name)
{
    if (!IS_LIST(value))
        vm_errorf(vm, "%s expects a list.", name);
    return AS_LIST(value);
}

// Function wrappers store their closed-over data in a small map attached to Function::instance.
static Value _state_get(VM *vm, PiMap *state, const char *key, const Value fallback)
{
    Value *value = (Value *)_ht_get(state->table, key);
    return value ? *value : fallback;
}

// Native wrapper callbacks recover their closure state from the currently executing function.
static PiMap *_current_stateMap(VM *vm, const char *name)
{
    if (vm->function == NULL || vm->function->type != OBJ_FUN)
        vm_errorf(vm, "%s: invalid function context.", name);

    Function *fn = (Function *)vm->function;
    if (fn->instance == NULL || fn->instance->type != OBJ_MAP)
        vm_errorf(vm, "%s: missing function state.", name);

    return (PiMap *)fn->instance;
}

// Creates a native Function object that behaves like a normal callable but carries state.
static Value _make_nativeWrapper(VM *vm, const char *name, NativeFunc func, Object *state)
{
    Object *obj = new_func((char *)name, NULL, NULL, NULL, state);
    Function *fn = (Function *)obj;
    fn->flags = FUNC_NATIVE;
    fn->native = func;
    add_obj(vm, obj);
    return NEW_OBJ(obj);
}

// Builds a VM-owned list from raw Value arguments and preserves numeric-list metadata.
static Value _new_valueList(VM *vm, int count, Value *items)
{
    List *list = list_create(sizeof(Value));
    bool is_numeric = TRUE;

    for (int i = 0; i < count; i++)
    {
        _list_add(list, &items[i]);
        if (!IS_NUM(items[i]))
            is_numeric = FALSE;
    }

    PiList *result = (PiList *)add_obj(vm, new_list(list));
    result->is_numeric = is_numeric;
    return NEW_OBJ(result);
}

static Value _clone_argList(VM *vm, Value *argv, int argc)
{
    return _new_valueList(vm, argc, argv);
}

static int _inferred_arity(Function *fn)
{
    return fn->params ? LIST_SIZE(fn->params) : 0;
}

Value _pi_map(VM *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_LIST(argv[0]) || !IS_FUN(argv[1]))
        vm_error(vm, "map(list, fn): expects a list and a function");

    PiList *input = AS_LIST(argv[0]);
    Function *fn = AS_FUN(argv[1]);
    List *list = list_create(sizeof(Value));

    int size = input->items->size;
    for (int i = 0; i < size; i++)
    {
        Value *item = (Value *)_list_getAt(input->items, i);
        Value ret_val = call_func(vm, fn, 1, item, NEW_NIL());
        _list_add(list, &ret_val);
    }

    PiList *result = (PiList *)new_list(list);
    result->is_numeric = FALSE;

    result->is_numeric = TRUE;

    size = result->items->size;
    for (int i = 0; i < size; i++)
    {
        Value item = *(Value *)_list_getAt(result->items, i);
        if (!IS_NUM(item))
        {
            result->is_numeric = FALSE;
            break;
        }
    }

    return NEW_OBJ(result);
}

Value pi_filter(VM *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_LIST(argv[0]) || !IS_FUN(argv[1]))
        vm_error(vm, "filter(list, fn): expects a list and a function");

    PiList *input = AS_LIST(argv[0]);
    Function *fn = AS_FUN(argv[1]);
    List *list = list_create(sizeof(Value));

    int size = input->items->size;
    for (int i = 0; i < size; i++)
    {
        Value *item = (Value *)_list_getAt(input->items, i);
        Value ret_val = call_func(vm, fn, 1, item, NEW_NIL());
        if (as_bool(ret_val))
            _list_add(list, item);
    }

    PiList *result = (PiList *)new_list(list);
    result->is_numeric = input->is_numeric;

    return NEW_OBJ(result);
}

Value pi_reduce(VM *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_LIST(argv[0]) || !IS_FUN(argv[1]))
        vm_error(vm, "reduce(list, fn, [initial]): expects a list, a function, and optional initial value");

    PiList *input = AS_LIST(argv[0]);
    Function *fn = AS_FUN(argv[1]);
    if (argc < 3 && input->items->size == 0)
        vm_error(vm, "reduce(list, fn): cannot reduce an empty list without an initial value");

    Value acc = (argc >= 3) ? argv[2] : *(Value *)_list_getAt(input->items, 0);
    int start = (argc >= 3) ? 0 : 1;

    int size = input->items->size;
    for (int i = start; i < size; i++)
    {
        Value item = *(Value *)_list_getAt(input->items, i);
        acc = call_funcv(vm, fn, 2, acc, item);
    }

    return acc;
}

Value pi_find(VM *vm, int argc, Value *argv)
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
            Value *item = (Value *)_list_getAt(list->items, i);
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
static Value _fn_composeCall(VM *vm, int argc, Value *argv)
{
    PiList *fns = _require_list(vm, _state_get(vm, _current_stateMap(vm, "compose"), "fns", NEW_NIL()), "compose");
    int size = fns->items->size;
    Function *fn = AS_FUN(*(Value *)_list_getAt(fns->items, size - 1));
    Value result = call_func(vm, fn, argc, argv, NEW_NIL());

    for (int i = size - 2; i >= 0; i--)
    {
        fn = AS_FUN(*(Value *)_list_getAt(fns->items, i));
        result = call_funcv(vm, fn, 1, result);
    }

    return result;
}

Value fn_compose(VM *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "compose(fn, ...): expects at least one function");

    List *fns = list_create(sizeof(Value));
    for (int i = 0; i < argc; i++)
    {
        _require_fun(vm, argv[i], "compose");
        _list_add(fns, &argv[i]);
    }

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    map_setValueByKey(state, "fns", NEW_OBJ(add_obj(vm, new_list(fns))));
    return _make_nativeWrapper(vm, "compose", _fn_composeCall, (Object *)state);
}

// pipe(f, g, h)(x) calls f first, then g, then h.
static Value _fn_pipeCall(VM *vm, int argc, Value *argv)
{
    PiList *fns = _require_list(vm, _state_get(vm, _current_stateMap(vm, "pipe"), "fns", NEW_NIL()), "pipe");
    int size = fns->items->size;
    Function *fn = AS_FUN(*(Value *)_list_getAt(fns->items, 0));
    Value result = call_func(vm, fn, argc, argv, NEW_NIL());

    for (int i = 1; i < size; i++)
    {
        fn = AS_FUN(*(Value *)_list_getAt(fns->items, i));
        result = call_funcv(vm, fn, 1, result);
    }

    return result;
}

Value fn_pipe(VM *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "pipe(fn, ...): expects at least one function");

    List *fns = list_create(sizeof(Value));
    for (int i = 0; i < argc; i++)
    {
        _require_fun(vm, argv[i], "pipe");
        _list_add(fns, &argv[i]);
    }

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    map_setValueByKey(state, "fns", NEW_OBJ(add_obj(vm, new_list(fns))));
    return _make_nativeWrapper(vm, "pipe", _fn_pipeCall, (Object *)state);
}

static Value _fn_juxtCall(VM *vm, int argc, Value *argv)
{
    PiList *fns = _require_list(vm, _state_get(vm, _current_stateMap(vm, "juxt"), "fns", NEW_NIL()), "juxt");
    List *results = list_create(sizeof(Value));
    bool is_numeric = TRUE;

    for (int i = 0; i < fns->items->size; i++)
    {
        Function *fn = AS_FUN(*(Value *)_list_getAt(fns->items, i));
        Value result = call_func(vm, fn, argc, argv, NEW_NIL());
        _list_add(results, &result);
        if (!IS_NUM(result))
            is_numeric = FALSE;
    }

    PiList *out = (PiList *)add_obj(vm, new_list(results));
    out->is_numeric = is_numeric;
    return NEW_OBJ(out);
}

Value fn_juxt(VM *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "juxt(fn, ...): expects at least one function");

    List *fns = list_create(sizeof(Value));
    for (int i = 0; i < argc; i++)
    {
        _require_fun(vm, argv[i], "juxt");
        _list_add(fns, &argv[i]);
    }

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    map_setValueByKey(state, "fns", NEW_OBJ(add_obj(vm, new_list(fns))));
    return _make_nativeWrapper(vm, "juxt", _fn_juxtCall, (Object *)state);
}

// Each partial curry call returns a new wrapper until the required arity is reached.
static Value _fn_curryCall(VM *vm, int argc, Value *argv)
{
    PiMap *state = _current_stateMap(vm, "curry");
    Value fn_value = _state_get(vm, state, "fn", NEW_NIL());
    PiList *collected = _require_list(vm, _state_get(vm, state, "args", NEW_NIL()), "curry");
    int arity = (int)as_number(_state_get(vm, state, "arity", NEW_NUM(0)));

    int total = collected->items->size + argc;
    Value *all_args = malloc(sizeof(Value) * total);
    for (int i = 0; i < collected->items->size; i++)
        all_args[i] = *(Value *)_list_getAt(collected->items, i);
    for (int i = 0; i < argc; i++)
        all_args[collected->items->size + i] = argv[i];

    if (total >= arity)
    {
        Value result = call_func(vm, AS_FUN(fn_value), total, all_args, NEW_NIL());
        free(all_args);
        return result;
    }

    Value arg_list = _new_valueList(vm, total, all_args);
    free(all_args);

    PiMap *next = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    map_setValueByKey(next, "fn", fn_value);
    map_setValueByKey(next, "arity", NEW_NUM(arity));
    map_setValueByKey(next, "args", arg_list);

    return _make_nativeWrapper(vm, "curry", _fn_curryCall, (Object *)next);
}

Value fn_curry(VM *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "curry(fn, [arity]): expects a function and optional arity");

    Function *fn = _require_fun(vm, argv[0], "curry");
    if (argc >= 2 && !IS_NUM(argv[1]))
        vm_error(vm, "curry(fn, [arity]): arity must be a number");
    int arity = (argc >= 2) ? (int)as_number(argv[1]) : _inferred_arity(fn);
    if (arity <= 0)
        vm_error(vm, "curry(fn, [arity]): arity must be positive");

    List *empty = list_create(sizeof(Value));
    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    map_setValueByKey(state, "fn", argv[0]);
    map_setValueByKey(state, "arity", NEW_NUM(arity));
    map_setValueByKey(state, "args", NEW_OBJ(add_obj(vm, new_list(empty))));

    return _make_nativeWrapper(vm, "curry", _fn_curryCall, (Object *)state);
}

static Value _fn_partialCall(VM *vm, int argc, Value *argv)
{
    PiMap *state = _current_stateMap(vm, "partial");
    Value fn_value = _state_get(vm, state, "fn", NEW_NIL());
    PiList *bound = _require_list(vm, _state_get(vm, state, "args", NEW_NIL()), "partial");

    int total = bound->items->size + argc;
    Value *all_args = malloc(sizeof(Value) * total);
    for (int i = 0; i < bound->items->size; i++)
        all_args[i] = *(Value *)_list_getAt(bound->items, i);
    for (int i = 0; i < argc; i++)
        all_args[bound->items->size + i] = argv[i];

    Value result = call_func(vm, AS_FUN(fn_value), total, all_args, NEW_NIL());
    free(all_args);
    return result;
}

Value fn_partial(VM *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "partial(fn, ...args): expects at least one function");

    _require_fun(vm, argv[0], "partial");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    map_setValueByKey(state, "fn", argv[0]);
    map_setValueByKey(state, "args", _clone_argList(vm, argv + 1, argc - 1));
    return _make_nativeWrapper(vm, "partial", _fn_partialCall, (Object *)state);
}

static Value _fn_spreadCall(VM *vm, int argc, Value *argv)
{
    PiMap *state = _current_stateMap(vm, "spread");
    Value fn_value = _state_get(vm, state, "fn", NEW_NIL());

    if (argc < 1 || !IS_LIST(argv[0]))
        vm_error(vm, "spread(fn): returned function expects a single list argument");

    PiList *items = AS_LIST(argv[0]);
    int count = items->items->size;
    Value *args = malloc(sizeof(Value) * count);
    for (int i = 0; i < count; i++)
        args[i] = *(Value *)_list_getAt(items->items, i);

    Value result = call_func(vm, AS_FUN(fn_value), count, args, NEW_NIL());
    free(args);
    return result;
}

Value fn_spread(VM *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "spread(fn): expects exactly one function");

    _require_fun(vm, argv[0], "spread");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    map_setValueByKey(state, "fn", argv[0]);
    return _make_nativeWrapper(vm, "spread", _fn_spreadCall, (Object *)state);
}

static Value _fn_unspreadCall(VM *vm, int argc, Value *argv)
{
    PiMap *state = _current_stateMap(vm, "unspread");
    Value fn_value = _state_get(vm, state, "fn", NEW_NIL());
    Value packed = _clone_argList(vm, argv, argc);
    return call_funcv(vm, AS_FUN(fn_value), 1, packed);
}

Value fn_unspread(VM *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "unspread(fn): expects exactly one function");

    _require_fun(vm, argv[0], "unspread");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    map_setValueByKey(state, "fn", argv[0]);
    return _make_nativeWrapper(vm, "unspread", _fn_unspreadCall, (Object *)state);
}

// Cache keys are generated either by key_fn(...) or by stringifying the argument list.
static Value _fn_memoizeCall(VM *vm, int argc, Value *argv)
{
    PiMap *state = _current_stateMap(vm, "memoize");
    Value fn_value = _state_get(vm, state, "fn", NEW_NIL());
    Value key_fn = _state_get(vm, state, "key_fn", NEW_NIL());
    PiMap *cache = AS_MAP(_state_get(vm, state, "cache", NEW_NIL()));

    Value key_value = IS_FUN(key_fn)
                          ? call_func(vm, AS_FUN(key_fn), argc, argv, NEW_NIL())
                          : _clone_argList(vm, argv, argc);

    char *key = as_string(key_value);
    Value *cached = (Value *)_ht_get(cache->table, key);
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

Value fn_memoize(VM *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "memoize(fn, [key_fn]): expects a function and optional key function");

    _require_fun(vm, argv[0], "memoize");
    if (argc >= 2)
        _require_fun(vm, argv[1], "memoize");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    map_setValueByKey(state, "fn", argv[0]);
    map_setValueByKey(state, "key_fn", argc >= 2 ? argv[1] : NEW_NIL());
    map_setValueByKey(state, "cache", NEW_OBJ(add_obj(vm, new_map(ht_create(sizeof(Value))))));
    return _make_nativeWrapper(vm, "memoize", _fn_memoizeCall, (Object *)state);
}

static Value _fn_onceCall(VM *vm, int argc, Value *argv)
{
    PiMap *state = _current_stateMap(vm, "once");
    if (as_bool(_state_get(vm, state, "done", NEW_BOOL(FALSE))))
        return _state_get(vm, state, "result", NEW_NIL());

    Value fn_value = _state_get(vm, state, "fn", NEW_NIL());
    Value result = call_func(vm, AS_FUN(fn_value), argc, argv, NEW_NIL());
    map_setValueByKey(state, "done", NEW_BOOL(TRUE));
    map_setValueByKey(state, "result", result);
    return result;
}

Value fn_once(VM *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "once(fn): expects exactly one function");

    _require_fun(vm, argv[0], "once");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    map_setValueByKey(state, "fn", argv[0]);
    map_setValueByKey(state, "done", NEW_BOOL(FALSE));
    map_setValueByKey(state, "result", NEW_NIL());
    return _make_nativeWrapper(vm, "once", _fn_onceCall, (Object *)state);
}

static Value _fn_throttleCall(VM *vm, int argc, Value *argv)
{
    PiMap *state = _current_stateMap(vm, "throttle");
    double ms = as_number(_state_get(vm, state, "ms", NEW_NUM(0)));
    double last = as_number(_state_get(vm, state, "last", NEW_NUM(-1)));

    if (last >= 0 && _now_ms() - last < ms)
        return _state_get(vm, state, "result", NEW_NIL());

    Value fn_value = _state_get(vm, state, "fn", NEW_NIL());
    Value result = call_func(vm, AS_FUN(fn_value), argc, argv, NEW_NIL());
    map_setValueByKey(state, "last", NEW_NUM(_now_ms()));
    map_setValueByKey(state, "result", result);
    return result;
}

Value fn_throttle(VM *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_NUM(argv[0]))
        vm_error(vm, "throttle(ms, fn): expects a number and a function");

    _require_fun(vm, argv[1], "throttle");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    map_setValueByKey(state, "ms", argv[0]);
    map_setValueByKey(state, "fn", argv[1]);
    map_setValueByKey(state, "last", NEW_NUM(-1));
    map_setValueByKey(state, "result", NEW_NIL());
    return _make_nativeWrapper(vm, "throttle", _fn_throttleCall, (Object *)state);
}

// This debounce is synchronous: rapid calls reuse the previous result instead of scheduling a delayed call.
static Value _fn_debounceCall(VM *vm, int argc, Value *argv)
{
    PiMap *state = _current_stateMap(vm, "debounce");
    double ms = as_number(_state_get(vm, state, "ms", NEW_NUM(0)));
    double last = as_number(_state_get(vm, state, "last_call", NEW_NUM(-1)));
    double now = _now_ms();

    map_setValueByKey(state, "last_call", NEW_NUM(now));
    if (last >= 0 && now - last < ms)
        return _state_get(vm, state, "result", NEW_NIL());

    Value fn_value = _state_get(vm, state, "fn", NEW_NIL());
    Value result = call_func(vm, AS_FUN(fn_value), argc, argv, NEW_NIL());
    map_setValueByKey(state, "result", result);
    return result;
}

Value fn_debounce(VM *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_NUM(argv[0]))
        vm_error(vm, "debounce(ms, fn): expects a number and a function");

    _require_fun(vm, argv[1], "debounce");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    map_setValueByKey(state, "ms", argv[0]);
    map_setValueByKey(state, "fn", argv[1]);
    map_setValueByKey(state, "last_call", NEW_NUM(-1));
    map_setValueByKey(state, "result", NEW_NIL());
    return _make_nativeWrapper(vm, "debounce", _fn_debounceCall, (Object *)state);
}

static Value _fn_thunkCall(VM *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;

    PiMap *state = _current_stateMap(vm, "thunk");
    Value fn_value = _state_get(vm, state, "fn", NEW_NIL());
    PiList *bound = _require_list(vm, _state_get(vm, state, "args", NEW_NIL()), "thunk");

    int count = bound->items->size;
    Value *args = malloc(sizeof(Value) * count);
    for (int i = 0; i < count; i++)
        args[i] = *(Value *)_list_getAt(bound->items, i);

    Value result = call_func(vm, AS_FUN(fn_value), count, args, NEW_NIL());
    free(args);
    return result;
}

Value fn_thunk(VM *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "thunk(fn, ...args): expects at least one function");

    _require_fun(vm, argv[0], "thunk");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    map_setValueByKey(state, "fn", argv[0]);
    map_setValueByKey(state, "args", _clone_argList(vm, argv + 1, argc - 1));
    return _make_nativeWrapper(vm, "thunk", _fn_thunkCall, (Object *)state);
}

// Returns the current value, then advances the stored state with fn(current).
static Value _fn_iterateCall(VM *vm, int argc, Value *argv)
{
    (void)argc;
    (void)argv;

    PiMap *state = _current_stateMap(vm, "iterate");
    Value current = _state_get(vm, state, "current", NEW_NIL());
    Value fn_value = _state_get(vm, state, "fn", NEW_NIL());
    Value next = call_funcv(vm, AS_FUN(fn_value), 1, current);
    map_setValueByKey(state, "current", next);
    return current;
}

Value fn_iterate(VM *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "iterate(seed, fn): expects a seed value and a function");

    _require_fun(vm, argv[1], "iterate");

    PiMap *state = (PiMap *)add_obj(vm, new_map(ht_create(sizeof(Value))));
    map_setValueByKey(state, "current", argv[0]);
    map_setValueByKey(state, "fn", argv[1]);
    return _make_nativeWrapper(vm, "iterate", _fn_iterateCall, (Object *)state);
}

Value fn_apply(VM *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_FUN(argv[0]) || !IS_LIST(argv[1]))
        vm_error(vm, "apply(fn, args): expects a function and a list");

    PiList *items = AS_LIST(argv[1]);
    int count = items->items->size;
    Value *args = malloc(sizeof(Value) * count);
    for (int i = 0; i < count; i++)
        args[i] = *(Value *)_list_getAt(items->items, i);

    Value result = call_func(vm, AS_FUN(argv[0]), count, args, NEW_NIL());
    free(args);
    return result;
}

Value fn_noop(VM *vm, int argc, Value *argv)
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
