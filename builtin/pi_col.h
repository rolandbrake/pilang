#ifndef PI_COL_H
#define PI_COL_H
#include "../src/pi_value.h"
#include "../src/pi_vm.h"

// Removes and returns the last element or character from a list or string.
Value pi_pop(VM *vm, int argc, Value *argv);

// Appends one or more elements to the end of a list or string.
Value pi_push(VM *vm, int argc, Value *argv);

// Checks if a list, string, or map is empty.
Value pi_empty(VM *vm, int argc, Value *argv);

// Inserts a value at a specified index in a list or string.
Value pi_insert(VM *vm, int argc, Value *argv);

// Removes and returns the element at a specified index from a list or string.
Value pi_remove(VM *vm, int argc, Value *argv);

// Returns a slice of a list or string.
Value pi_slice(VM *vm, int argc, Value *argv);

// Returns the length of a list, string, or map.
Value pi_len(VM *vm, int argc, Value *argv);

// Returns a list of numbers in a specified range.
Value pi_range(VM *vm, int argc, Value *argv);

// Returns the last element or character from a list or string without removing it.
Value pi_peek(VM *vm, int argc, Value *argv);

// Creates a new set from a list or other iterable, removing duplicates.
Value _pi_set(VM *vm, int argc, Value *argv);

// Adds elements to a set.
Value cl_add(VM *vm, int argc, Value *argv);

// Removes all elements from a set.
Value cl_clear(VM *vm, int argc, Value *argv);

// Checks if a set contains an element.
Value pi_contains(VM *vm, int argc, Value *argv);

// Returns the union of two sets.
Value pi_union(VM *vm, int argc, Value *argv);

// Returns the intersection of two sets.
Value pi_intersection(VM *vm, int argc, Value *argv);

// Returns the difference of two sets.
Value pi_difference(VM *vm, int argc, Value *argv);

// Returns the symmetric difference of two sets.
Value pi_symmetricDiff(VM *vm, int argc, Value *argv);

// Checks if one set is a subset of another.
Value pi_issubset(VM *vm, int argc, Value *argv);

// Checks if one set is a superset of another.
Value pi_issuperset(VM *vm, int argc, Value *argv);

// Checks if two sets are disjoint.
Value pi_isdisjoint(VM *vm, int argc, Value *argv);

Value pi_copy(VM *vm, int argc, Value *argv);
Value pi_join(VM *vm, int argc, Value *argv);

// Sorts the elements of a list in ascending order.
Value cl_sort(VM *vm, int argc, Value *argv);

// Prepends one or more values to the beginning of a list or string.
Value cl_unshift(VM *vm, int argc, Value *argv);

// Appends a value to the end of a list or string (alias of push).
Value cl_append(VM *vm, int argc, Value *argv);

// Checks if a list or tuple contains a value, or if a map contains a key.
Value cl_contains(VM *vm, int argc, Value *argv);

// Returns the index of a value in a list, tuple, or character in a string.
Value cl_indexOf(VM *vm, int argc, Value *argv);

// Tuple and collection helpers.
Value cl_count(VM *vm, int argc, Value *argv);
Value cl_concat(VM *vm, int argc, Value *argv);
Value cl_repeat(VM *vm, int argc, Value *argv);
Value pi_tuple(VM *vm, int argc, Value *argv);

// Reverses the elements of a list or characters of a string in place.
Value cl_reverse(VM *vm, int argc, Value *argv);

// Flattens one level of nested list items.
Value cl_flat(VM *vm, int argc, Value *argv);

// Randomly shuffles the elements of a list.
Value cl_shuffle(VM *vm, int argc, Value *argv);

// Returns a shallow copy of a list or string.
Value cl_copy(VM *vm, int argc, Value *argv);

// used to combine multiple iterables into a single iterable
Value cl_zip(VM *vm, int argc, Value *argv);
Value cl_join(VM *vm, int argc, Value *argv);

Value cl_isIterable(VM *vm, int argc, Value *argv);

#endif // PI_COL_H
