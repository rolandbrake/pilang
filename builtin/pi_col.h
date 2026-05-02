#ifndef PI_COL_H
#define PI_COL_H
#include "../pi_value.h"
#include "../pi_vm.h"

// Removes and returns the last element or character from a list or string.
Value pi_pop(vm_t *vm, int argc, Value *argv);

// Appends one or more elements to the end of a list or string.
Value pi_push(vm_t *vm, int argc, Value *argv);

// Checks if a list, string, or map is empty.
Value pi_empty(vm_t *vm, int argc, Value *argv);

// Inserts a value at a specified index in a list or string.
Value pi_insert(vm_t *vm, int argc, Value *argv);

// Removes and returns the element at a specified index from a list or string.
Value pi_remove(vm_t *vm, int argc, Value *argv);

// Returns a slice of a list or string.
Value pi_slice(vm_t *vm, int argc, Value *argv);

// Returns the length of a list, string, or map.
Value pi_len(vm_t *vm, int argc, Value *argv);

// Returns a list of numbers in a specified range.
Value pi_range(vm_t *vm, int argc, Value *argv);

// Returns the last element or character from a list or string without removing it.
Value pi_peek(vm_t *vm, int argc, Value *argv);

// Creates a new set from a list or other iterable, removing duplicates.
Value _pi_set(vm_t *vm, int argc, Value *argv);

// Adds elements to a set.
Value cl_add(vm_t *vm, int argc, Value *argv);

// Removes all elements from a set.
Value cl_clear(vm_t *vm, int argc, Value *argv);

// Checks if a set contains an element.
Value pi_contains(vm_t *vm, int argc, Value *argv);

// Returns the union of two sets.
Value pi_union(vm_t *vm, int argc, Value *argv);

// Returns the intersection of two sets.
Value pi_intersection(vm_t *vm, int argc, Value *argv);

// Returns the difference of two sets.
Value pi_difference(vm_t *vm, int argc, Value *argv);

// Returns the symmetric difference of two sets.
Value pi_symmetricDiff(vm_t *vm, int argc, Value *argv);

// Checks if one set is a subset of another.
Value pi_issubset(vm_t *vm, int argc, Value *argv);

// Checks if one set is a superset of another.
Value pi_issuperset(vm_t *vm, int argc, Value *argv);

// Checks if two sets are disjoint.
Value pi_isdisjoint(vm_t *vm, int argc, Value *argv);

// Sorts the elements of a list in ascending order.
Value cl_sort(vm_t *vm, int argc, Value *argv);

// Prepends one or more values to the beginning of a list or string.
Value cl_unshift(vm_t *vm, int argc, Value *argv);

// Appends a value to the end of a list or string (alias of push).
Value cl_append(vm_t *vm, int argc, Value *argv);

// Checks if a list or tuple contains a value, or if a map contains a key.
Value cl_contains(vm_t *vm, int argc, Value *argv);

// Returns the index of a value in a list, tuple, or character in a string.
Value cl_indexOf(vm_t *vm, int argc, Value *argv);

// Tuple and collection helpers.
Value cl_count(vm_t *vm, int argc, Value *argv);
Value cl_concat(vm_t *vm, int argc, Value *argv);
Value cl_repeat(vm_t *vm, int argc, Value *argv);
Value pi_tuple(vm_t *vm, int argc, Value *argv);

// Reverses the elements of a list or characters of a string in place.
Value cl_reverse(vm_t *vm, int argc, Value *argv);

// Randomly shuffles the elements of a list.
Value cl_shuffle(vm_t *vm, int argc, Value *argv);

// Returns a shallow copy of a list or string.
Value cl_copy(vm_t *vm, int argc, Value *argv);

// used to combine multiple iterables into a single iterable
Value cl_zip(vm_t *vm, int argc, Value *argv);

Value cl_isIterable(vm_t *vm, int argc, Value *argv);

#endif // PI_COL_H