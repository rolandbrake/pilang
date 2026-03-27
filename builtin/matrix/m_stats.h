#ifndef M_STATS_H
#define M_STATS_H

#include "../../pi_value.h"
#include "../../pi_vm.h"
#include "matrix.h"

// Descriptive
Value mat_var(vm_t *vm, int argc, Value *argv);
Value mat_std(vm_t *vm, int argc, Value *argv);
Value mat_median(vm_t *vm, int argc, Value *argv);
Value mat_percentile(vm_t *vm, int argc, Value *argv);
Value mat_mode(vm_t *vm, int argc, Value *argv);

// Matrix statistics
Value mat_covariance(vm_t *vm, int argc, Value *argv);
Value mat_correlation(vm_t *vm, int argc, Value *argv);
Value mat_zscore(vm_t *vm, int argc, Value *argv);

// Distributions (sampling)
Value mat_randn(vm_t *vm, int argc, Value *argv);
Value mat_randint(vm_t *vm, int argc, Value *argv);
Value mat_shuffle(vm_t *vm, int argc, Value *argv);

#endif // M_STATS_H