#ifndef PI_FUN_H
#define PI_FUN_H

#include "../pi_value.h"
#include "../pi_vm.h"

// map function to map a function for each element of a list
Value _pi_map(vm_t *vm, int argc, Value *argv);

// filter function to filter a function for each element of a list
Value pi_filter(vm_t *vm, int argc, Value *argv);

// reduce function to reduce a function for each element of a list
Value pi_reduce(vm_t *vm, int argc, Value *argv);

// find function to find a function for each element of a list
Value pi_find(vm_t *vm, int argc, Value *argv);

// Right-to-left function composition. compose(f, g)(x) = f(g(x)). Accepts any number of functions.
Value fn_compose(vm_t *vm, int argc, Value *argv);

// Left-to-right composition. pipe(f, g)(x) = g(f(x)). More readable for data _transformation pipelines.
Value fn_pipe(vm_t *vm, int argc, Value *argv);

// Applies multiple functions to the same argument, returning a list of results.
Value fn_juxt(vm_t *vm, int argc, Value *argv);

// _transforms a multi-argument function into a chain of single-argument functions. Supports auto-currying by arity.
Value fn_curry(vm_t *vm, int argc, Value *argv);

// Partially applies the given arguments, returning a function that takes the rest.
Value fn_partial(vm_t *vm, int argc, Value *argv);

// Converts a function that takes individual args to one that takes a list.
Value fn_spread(vm_t *vm, int argc, Value *argv);

// Converts a function that takes a list to one that takes individual args.
Value fn_unspread(vm_t *vm, int argc, Value *argv);

// Memoizes a function — caches results by arguments. Optional key function to control cache identity.
Value fn_memoize(vm_t *vm, int argc, Value *argv);

// Returns a function that only executes fn on the first call. Subsequent calls return the first result.
Value fn_once(vm_t *vm, int argc, Value *argv);

// Limits fn to at most one call per ms milliseconds. Drops extra calls.
Value fn_throttle(vm_t *vm, int argc, Value *argv);

// Delays fn execution until ms milliseconds after the last call. Resets the timer on each call.
Value fn_debounce(vm_t *vm, int argc, Value *argv);

// Returns a zero-arg function that calls fn(...args) when invoked.
Value fn_thunk(vm_t *vm, int argc, Value *argv);

// Infinite lazy sequence: seed, fn(seed), fn(fn(seed)), … Use col.take to limit.
Value fn_iterate(vm_t *vm, int argc, Value *argv);

// Calls fn with arguments from a list. Like spread but immediate.
Value fn_apply(vm_t *vm, int argc, Value *argv);

// A function that does nothing. Useful as a default no-op callback.
Value fn_noop(vm_t *vm, int argc, Value *argv);

#endif // PI_FUN_H