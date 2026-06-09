#ifndef PI_MATH_H
#define PI_MATH_H

#include <stdint.h>
#include "../pi_vm.h"
#include "../pi_value.h"

void rng_seed(uint32_t seed);
double rand_num(void);

// Returns the absolute value of a number or each element in a list.
Value pi_abs(vm_t *vm, int argc, Value *argv);

// Returns the maximum value from a list of numbers.
Value pi_max(vm_t *vm, int argc, Value *argv);

// Returns the minimum value from a list of numbers.
Value pi_min(vm_t *vm, int argc, Value *argv);

// Returns a number raised to the power of another number.
Value pi_pow(vm_t *vm, int argc, Value *argv);

// Rounds a number or each element in a list to the nearest integer.
Value pi_round(vm_t *vm, int argc, Value *argv);

// Sets the seed for the random number generator.
Value pi_seed(vm_t *vm, int argc, Value *argv);

// Returns a random float between 0 and 1.
Value pi_rand(vm_t *vm, int argc, Value *argv);

// Returns a list of random floats of specified size.
Value pi_rand_n(vm_t *vm, int argc, Value *argv);

// Returns the floor of a number or each element in a list.
Value mt_floor(vm_t *vm, int argc, Value *argv);

// Returns the ceiling of a number or each element in a list.
Value mt_ceil(vm_t *vm, int argc, Value *argv);

// Returns the square root of a number or each element in a list.
Value mt_sqrt(vm_t *vm, int argc, Value *argv);

// Returns the sine of a number or each element in a list (input in radians).
Value mt_sin(vm_t *vm, int argc, Value *argv);

// Returns the cosine of a number or each element in a list (input in radians).
Value mt_cos(vm_t *vm, int argc, Value *argv);

// Returns the tangent of a number or each element in a list (input in radians).
Value mt_tan(vm_t *vm, int argc, Value *argv);

// Returns the arcsine of a number or each element in a list (output in radians).
Value mt_asin(vm_t *vm, int argc, Value *argv);

// Returns the arccosine of a number or each element in a list (output in radians).
Value mt_acos(vm_t *vm, int argc, Value *argv);

// Returns the arctangent of a number or each element in a list (output in radians).
Value mt_atan(vm_t *vm, int argc, Value *argv);

// Converts radians to degrees for a number or each element in a list.
Value mt_deg(vm_t *vm, int argc, Value *argv);

// Converts degrees to radians for a number or each element in a list.
Value mt_rad(vm_t *vm, int argc, Value *argv);

// Returns the sum of all numbers in a list.
Value mt_sum(vm_t *vm, int argc, Value *argv);

// Returns e raised to the power of a number or each element in a list.
Value mt_exp(vm_t *vm, int argc, Value *argv);

// Returns the base-2 logarithm of a number or each element in a list.
Value mt_log2(vm_t *vm, int argc, Value *argv);

// Returns the base-10 logarithm of a number or each element in a list.
Value mt_log10(vm_t *vm, int argc, Value *argv);

// Returns the natural logarithm of a number or each element in a list.
Value mt_logE(vm_t *vm, int argc, Value *argv);

// Returns a list of evenly spaced numbers over a specified interval.
Value mt_linspace(vm_t *vm, int argc, Value *argv);

// Returns a list of numbers in a specified range with a given step.
Value mt_arange(vm_t *vm, int argc, Value *argv);

#endif // PI_MATH_H
