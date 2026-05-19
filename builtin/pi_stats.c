#include "pi_stats.h"
#include "pi_builtin.h"

/**
 * Calculates the mean (average) of a list of numeric values.
 *
 * @param vm The virtual machine.
 * @param argc Number of arguments (expects exactly 1).
 * @param argv The arguments: a list of numeric values.
 * @return The mean (average) value as a number.
 */
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

/**
 * Alias for mean: calculates the average of a list of numeric values.
 */
Value pi_avg(vm_t *vm, int argc, Value *argv)
{
    return pi_mean(vm, argc, argv);
}

/**
 * Calculates the variance of a list of numeric values.
 * Expects a single argument: a list of numbers.
 * Returns the variance as a number.
 */
Value pi_var(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0 || !IS_LIST(argv[0]))
        vm_error(vm, "[var] expects a single argument: a list of numbers.");

    list_t *input = AS_CLIST(argv[0]);
    if (input->size == 0)
        vm_error(vm, "[var] Cannot calculate variance of an empty list.");

    // Calculate mean
    double sum = 0.0;
    for (int i = 0; i < input->size; i++)
    {
        Value item = *(Value *)list_getAt(input, i);
        if (!is_numeric(item))
            vm_error(vm, "[var] All elements in the list must be numeric.");

        sum += as_number(item);
    }
    double mean = sum / input->size;

    // Calculate variance
    double variance = 0.0;
    for (int i = 0; i < input->size; i++)
    {
        Value item = *(Value *)list_getAt(input, i);
        double diff = as_number(item) - mean;
        variance += diff * diff;
    }
    variance /= input->size;

    return NEW_NUM(variance);
}

/**
 * Calculates the standard deviation of a list of numeric values.
 * Expects a single argument: a list of numbers.
 * Returns the standard deviation as a number.
 */
Value pi_dev(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0 || !IS_LIST(argv[0]))
        vm_error(vm, "[dev] expects a single argument: a list of numbers.");

    list_t *input = AS_CLIST(argv[0]);
    if (input->size == 0)
        vm_error(vm, "[dev] Cannot calculate standard deviation of an empty list.");

    // Calculate mean
    double sum = 0.0;
    for (int i = 0; i < input->size; i++)
    {
        Value item = *(Value *)list_getAt(input, i);
        if (!is_numeric(item))
            vm_error(vm, "[dev] All elements in the list must be numeric.");

        sum += as_number(item);
    }
    double mean = sum / input->size;

    // Calculate variance
    double variance = 0.0;
    for (int i = 0; i < input->size; i++)
    {
        Value item = *(Value *)list_getAt(input, i);
        double diff = as_number(item) - mean;
        variance += diff * diff;
    }
    variance /= input->size;

    // Standard deviation is the square root of variance
    double stddev = sqrt(variance);

    return NEW_NUM(stddev);
}

/**
 * Compares two values as numbers.
 *
 * This function is a comparison function for qsort that compares two values as
 * numbers. It returns a negative value if the first value is less than the
 * second, zero if they are equal, and a positive value if the first value is
 * greater than the second.
 *
 * @param a The first value to compare.
 * @param b The second value to compare.
 * @return A negative value if the first value is less than the second, zero if
 *         they are equal, and a positive value if the first value is greater
 *         than the second.
 */
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

/**
 * Calculates the median of a list of numeric values.
 * Expects a single argument: a list of numbers.
 * Returns the median as a number.
 */
Value pi_median(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0 || !IS_LIST(argv[0]))
        vm_error(vm, "[median] expects a single argument: a list of numbers.");

    list_t *input = AS_CLIST(argv[0]);
    int size = input->size;
    if (size == 0)
        vm_error(vm, "[median] Cannot calculate median of an empty list.");

    // Copy the values to a temporary array for sorting
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

    // Sort the copy
    qsort(copy, size, sizeof(Value), compare_values);

    Value median;
    if (size % 2 == 1)
    {
        // Odd number of elements, take the middle one
        median = copy[size / 2];
    }
    else
    {
        // Even number of elements, average the two middle ones
        double mid1 = as_number(copy[size / 2 - 1]);
        double mid2 = as_number(copy[size / 2]);
        median = NEW_NUM((mid1 + mid2) / 2.0);
    }

    free(copy);
    return median;
}

/**
 * Returns the mode (most frequent numeric value) from a list of numbers.
 * Expects one argument: a list of numeric values.
 * Returns the mode as a numeric value.
 */
Value pi_mode(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0 || !IS_LIST(argv[0]))
        vm_error(vm, "[mode] expects a single argument: a list of numbers.");

    list_t *input = AS_CLIST(argv[0]);
    int size = input->size;

    if (size == 0)
        vm_error(vm, "[mode] Cannot calculate mode of an empty list.");

    // Copy values to temporary array for sorting
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

    // Sort the copy to group identical values together
    qsort(copy, size, sizeof(Value), compare_values);

    // Find the mode by counting frequencies
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

    // Check last run
    if (current_count > max_count)
    {
        max_count = current_count;
        mode = copy[size - 1];
    }

    free(copy);
    return mode;
}

// Module Registration
static BuiltinFunc stats_funcs[] = {
    {"mean", pi_mean},
    {"avg", pi_avg},
    {"var", pi_var},
    {"dev", pi_dev},
    {"median", pi_median},
    {"mode", pi_mode}};

DEFINE_BUILTIN_MODULE(module_stats, "stats", stats_funcs, NULL);