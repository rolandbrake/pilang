#ifndef PI_MATH_H
#define PI_MATH_H

#include <stdint.h>
#include "../src/pi_vm.h"
#include "../src/pi_value.h"

void rng_seed(uint32_t seed);
double rand_num(void);

// Returns the absolute value of a number or each element in a list.
Value pi_abs(VM *vm, int argc, Value *argv);

// Returns the maximum value from a list of numbers.
Value pi_max(VM *vm, int argc, Value *argv);

// Returns the minimum value from a list of numbers.
Value pi_min(VM *vm, int argc, Value *argv);

// Returns a number raised to the power of another number.
Value pi_pow(VM *vm, int argc, Value *argv);

// Rounds a number or each element in a list to the nearest integer.
Value pi_round(VM *vm, int argc, Value *argv);

// Sets the seed for the random number generator.
Value pi_seed(VM *vm, int argc, Value *argv);

// Returns a random float between 0 and 1.
Value pi_rand(VM *vm, int argc, Value *argv);

// Returns a list of random floats of specified size.
Value pi_RandN(VM *vm, int argc, Value *argv);

// Returns the floor of a number or each element in a list.
Value mt_floor(VM *vm, int argc, Value *argv);

// Returns the ceiling of a number or each element in a list.
Value mt_ceil(VM *vm, int argc, Value *argv);

// Returns the square root of a number or each element in a list.
Value mt_sqrt(VM *vm, int argc, Value *argv);

// Returns the sine of a number or each element in a list (input in radians).
Value mt_sin(VM *vm, int argc, Value *argv);

// Returns the cosine of a number or each element in a list (input in radians).
Value mt_cos(VM *vm, int argc, Value *argv);

// Returns the tangent of a number or each element in a list (input in radians).
Value mt_tan(VM *vm, int argc, Value *argv);

// Returns the arcsine of a number or each element in a list (output in radians).
Value mt_asin(VM *vm, int argc, Value *argv);

// Returns the arccosine of a number or each element in a list (output in radians).
Value mt_acos(VM *vm, int argc, Value *argv);

// Returns the arctangent of a number or each element in a list (output in radians).
Value mt_atan(VM *vm, int argc, Value *argv);

// Converts radians to degrees for a number or each element in a list.
Value mt_deg(VM *vm, int argc, Value *argv);

// Converts degrees to radians for a number or each element in a list.
Value mt_rad(VM *vm, int argc, Value *argv);

// Returns the sum of all numbers in a list.
Value mt_sum(VM *vm, int argc, Value *argv);

// Returns e raised to the power of a number or each element in a list.
Value mt_exp(VM *vm, int argc, Value *argv);

// Returns the base-2 logarithm of a number or each element in a list.
Value mt_log2(VM *vm, int argc, Value *argv);

// Returns the base-10 logarithm of a number or each element in a list.
Value mt_log10(VM *vm, int argc, Value *argv);

// Returns the natural logarithm of a number or each element in a list.
Value mt_logE(VM *vm, int argc, Value *argv);

// Returns a list of evenly spaced numbers over a specified interval.
Value mt_linspace(VM *vm, int argc, Value *argv);

// Returns a list of numbers in a specified range with a given step.
Value mt_arange(VM *vm, int argc, Value *argv);

#endif // PI_MATH_H
