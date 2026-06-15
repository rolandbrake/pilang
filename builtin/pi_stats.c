#include "pi_stats.h"
#include "pi_builtin.h"

Value pi_mean(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "[mean] expects exactly one argument: a list of numeric values.");

    Value arg = argv[0];

    if (!IS_LIST(arg))
        vm_error(vm, "[mean] expects a list of numeric values.");

    list_t *input = AS_CLIST(arg);

    if (input->size == 0)
        vm_error(vm, "[mean] cannot compute mean of an empty list.");

    double sum = 0.0;

    for (int i = 0; i < input->size; i++)
    {
        Value item = *(Value *)list_getAt(input, i);

        if (!is_numeric(item))
            vm_error(vm, "[mean] all elements in the list must be numeric.");

        sum += as_number(item);
    }

    double mean = sum / input->size;
    return NEW_NUM(mean);
}

Value pi_avg(vm_t *vm, int argc, Value *argv)
{
    return pi_mean(vm, argc, argv);
}

Value pi_var(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0 || !IS_LIST(argv[0]))
        vm_error(vm, "[var] expects a single argument: a list of numbers.");

    list_t *input = AS_CLIST(argv[0]);
    if (input->size == 0)
        vm_error(vm, "[var] Cannot calculate variance of an empty list.");

    double sum = 0.0;
    for (int i = 0; i < input->size; i++)
    {
        Value item = *(Value *)list_getAt(input, i);
        if (!is_numeric(item))
            vm_error(vm, "[var] All elements in the list must be numeric.");

        sum += as_number(item);
    }

    double mean = sum / input->size;
    double variance = 0.0;

    for (int i = 0; i < input->size; i++)
    {
        Value item = *(Value *)list_getAt(input, i);
        double diff = as_number(item) - mean;
        variance += diff * diff;
    }

    // Population variance: divide by N, not N - 1.
    variance /= input->size;

    return NEW_NUM(variance);
}

Value pi_dev(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0 || !IS_LIST(argv[0]))
        vm_error(vm, "[dev] expects a single argument: a list of numbers.");

    list_t *input = AS_CLIST(argv[0]);
    if (input->size == 0)
        vm_error(vm, "[dev] Cannot calculate standard deviation of an empty list.");

    double sum = 0.0;
    for (int i = 0; i < input->size; i++)
    {
        Value item = *(Value *)list_getAt(input, i);
        if (!is_numeric(item))
            vm_error(vm, "[dev] All elements in the list must be numeric.");

        sum += as_number(item);
    }

    double mean = sum / input->size;
    double variance = 0.0;

    for (int i = 0; i < input->size; i++)
    {
        Value item = *(Value *)list_getAt(input, i);
        double diff = as_number(item) - mean;
        variance += diff * diff;
    }

    // Population standard deviation, matching pi_var().
    variance /= input->size;

    return NEW_NUM(sqrt(variance));
}

static int compare_values(const void *a, const void *b)
{
    const Value *va = (const Value *)a;
    const Value *vb = (const Value *)b;

    double diff = as_number(*va) - as_number(*vb);
    if (diff < 0)
        return -1;
    if (diff > 0)
        return 1;
    return 0;
}

Value pi_median(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0 || !IS_LIST(argv[0]))
        vm_error(vm, "[median] expects a single argument: a list of numbers.");

    list_t *input = AS_CLIST(argv[0]);
    int size = input->size;
    if (size == 0)
        vm_error(vm, "[median] Cannot calculate median of an empty list.");

    // Sort a temporary copy so the original list order is preserved.
    Value *copy = malloc(size * sizeof(Value));
    if (!copy)
        vm_error(vm, "[median] Memory allocation failed.");

    for (int i = 0; i < size; i++)
    {
        Value item = *(Value *)list_getAt(input, i);
        if (!is_numeric(item))
        {
            free(copy);
            vm_error(vm, "[median] All elements in the list must be numeric.");
        }

        copy[i] = item;
    }

    qsort(copy, size, sizeof(Value), compare_values);

    Value median;
    if (size % 2 == 1)
    {
        median = copy[size / 2];
    }
    else
    {
        double mid1 = as_number(copy[size / 2 - 1]);
        double mid2 = as_number(copy[size / 2]);
        median = NEW_NUM((mid1 + mid2) / 2.0);
    }

    free(copy);
    return median;
}

Value pi_mode(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0 || !IS_LIST(argv[0]))
        vm_error(vm, "[mode] expects a single argument: a list of numbers.");

    list_t *input = AS_CLIST(argv[0]);
    int size = input->size;

    if (size == 0)
        vm_error(vm, "[mode] Cannot calculate mode of an empty list.");

    // Sorting groups equal values, making frequency counting linear.
    Value *copy = malloc(size * sizeof(Value));
    if (!copy)
        vm_error(vm, "[mode] Memory allocation failed.");

    for (int i = 0; i < size; i++)
    {
        Value item = *(Value *)list_getAt(input, i);
        if (!is_numeric(item))
        {
            free(copy);
            vm_error(vm, "[mode] All elements in the list must be numeric.");
        }

        copy[i] = item;
    }

    qsort(copy, size, sizeof(Value), compare_values);

    int max_count = 1;
    int current_count = 1;
    Value mode = copy[0];

    for (int i = 1; i < size; i++)
    {
        if (as_number(copy[i]) == as_number(copy[i - 1]))
            current_count++;
        else
        {
            if (current_count > max_count)
            {
                max_count = current_count;
                mode = copy[i - 1];
            }

            current_count = 1;
        }
    }

    if (current_count > max_count)
    {
        max_count = current_count;
        mode = copy[size - 1];
    }

    free(copy);
    return mode;
}

static BuiltinFunc stats_funcs[] = {
    {"mean", pi_mean},
    {"avg", pi_avg},
    {"var", pi_var},
    {"dev", pi_dev},
    {"median", pi_median},
    {"mode", pi_mode},
};

DEFINE_BUILTIN_MODULE(module_stats, "stats", stats_funcs, NULL);
