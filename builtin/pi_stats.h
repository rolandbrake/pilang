#ifndef STATS_H
#define STATS_H

#include "../pi_vm.h"
#include "../pi_value.h"

// Returns the mean (average) of a list of numbers.
Value pi_mean(VM *vm, int argc, Value *argv);

// Returns the average of a list of numbers (alias for mean).
Value pi_avg(VM *vm, int argc, Value *argv);

// Returns the variance of a list of numbers.
Value pi_var(VM *vm, int argc, Value *argv);

// Returns the standard deviation of a list of numbers.
Value pi_dev(VM *vm, int argc, Value *argv);

// Returns the median value of a list of numbers.
Value pi_median(VM *vm, int argc, Value *argv);

// Returns the mode (most frequent value) of a list of numbers.
Value pi_mode(VM *vm, int argc, Value *argv);

#endif // STATS_H