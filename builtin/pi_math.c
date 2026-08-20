#include <math.h>
#include <time.h>
#include <stdlib.h>

#include "pi_math.h"
#include "pi_builtin.h"
#include "../common.h"

static uint32_t state = 2463534242;

// xoshiro32** uses four 32-bit state values. splitmix32 expands a single seed into this state.
static uint32_t rng_state[4];
static int rng_initialized = 0;

Value pi_round(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[round] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
        return NEW_NUM(round(as_number(arg)));

    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[round] All elements in the list must be numeric.");

            double _round = round(as_number(item));
            Value val = NEW_NUM(_round);
            list_add(result, &val);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[round] expects a numeric value or a list of numeric values.");
    return NEW_NIL();
}

// Used only for seeding xoshiro32**; it spreads nearby seeds into different internal states.
static uint32_t splitmix32(uint32_t *seed)
{
    uint32_t z = (*seed += 0x9e3779b9);
    z = (z ^ (z >> 16)) * 0x85ebca6b;
    z = (z ^ (z >> 13)) * 0xc2b2ae35;
    return z ^ (z >> 16);
}

void rng_seed(uint32_t seed)
{
    for (int i = 0; i < 4; i++)
        rng_state[i] = splitmix32(&seed);
    rng_initialized = 1;
}

// xoshiro32** PRNG step. Fast and deterministic once seeded.
uint32_t xoshiro32(void)
{
    uint32_t *s = rng_state;

    uint32_t result = s[1] * 5;
    result = ((result << 7) | (result >> (32 - 7))) * 9;

    uint32_t t = s[1] << 9;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];

    s[2] ^= t;
    s[3] = (s[3] << 11) | (s[3] >> (32 - 11));

    return result;
}

// Returns a floating-point value in [0.0, 1.0). Auto-seeds on first use.
double rand_num()
{
    if (!rng_initialized)
        rng_seed((uint32_t)time(NULL));

    return xoshiro32() / (double)UINT32_MAX;
}

Value pi_seed(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !is_numeric(argv[0]))
        vm_error(vm, "[seed] expects a single numeric argument.");

    rng_seed((uint32_t)as_number(argv[0]));

    return NEW_NIL();
}

// rand() -> float in [0, 1), rand(max) -> integer in [0, max], rand(min, max) -> integer in [min, max].
Value pi_rand(vm_t *vm, int argc, Value *argv)
{
    if (!rng_initialized)
        rng_seed((uint32_t)time(NULL));

    if (argc == 0)
        return NEW_NUM(rand_num());

    else if (argc == 1 && is_numeric(argv[0]))
    {
        int max = (int)as_number(argv[0]);
        int min = 0;

        if (max < min)
            vm_error(vm, "[rand] max must be >= 0");

        int range = max - min + 1;
        int result = min + (int)(rand_num() * range);
        return NEW_NUM(result);
    }

    else if (argc == 2 && is_numeric(argv[0]) && is_numeric(argv[1]))
    {
        int min = (int)as_number(argv[0]);
        int max = (int)as_number(argv[1]);

        if (min > max)
            vm_error(vm, "[rand] min must not be greater than max");

        int range = max - min + 1;
        int result = min + (int)(rand_num() * range);
        return NEW_NUM(result);
    }

    else
        vm_error(vm, "[rand] expects 0, 1, or 2 numeric arguments.");
}

Value pi_rand_n(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !is_numeric(argv[0]))
        vm_error(vm, "[rand_n] expects a single numeric argument representing the size.");

    int size = (int)as_number(argv[0]);
    if (size < 0)
        vm_error(vm, "[rand_n] size must be non-negative.");

    list_t *list = list_create(sizeof(Value));

    for (int i = 0; i < size; i++)
    {
        double r = (double)rand() / RAND_MAX;
        Value val = NEW_NUM(r);
        list_add(list, &val);
    }

    PiList *result = (PiList *)new_list(list);
    result->is_numeric = true;

    return NEW_OBJ(result);
}

Value pi_min(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[min] expects numerical values or a list or set of numeric values.");

    double min_val = 0;
    bool initialized = false;

    if (argc > 1 && is_numeric(argv[0]))
    {
        for (int i = 0; i < argc; i++)
        {
            if (!is_numeric(argv[i]))
                vm_error(vm, "[min] All arguments must be numeric.");

            double num = as_number(argv[i]);
            if (!initialized || num < min_val)
            {
                min_val = num;
                initialized = true;
            }
        }
        return NEW_NUM(min_val);
    }
    else if (IS_LIST(argv[0]))
    {
        list_t *input = AS_CLIST(argv[0]);

        if (input->size == 0)
            vm_error(vm, "[min] cannot operate on an empty list.");

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[min] All elements in the list must be numeric.");

            double num = as_number(item);
            if (!initialized || num < min_val)
            {
                min_val = num;
                initialized = true;
            }
        }
    }
    else if (IS_SET(argv[0]))
    {
        PiSet *set = AS_SET(argv[0]);

        if (set_size(set) == 0)
            vm_error(vm, "[min] cannot operate on an empty set.");

        for (int i = 0; i < set_size(set); i++)
        {
            Value actual = set_get(set, i);

            if (!is_numeric(actual))
                vm_error(vm, "[min] All elements in the set must be numeric.");

            double num = as_number(actual);
            if (!initialized || num < min_val)
            {
                min_val = num;
                initialized = true;
            }
        }
    }
    else
        vm_error(vm, "[min] expects a list or set of numeric values.");

    return NEW_NUM(min_val);
}

Value pi_max(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[max] expects numerical values or a list or set of numeric values.");

    double max_val = 0;
    bool initialized = false;
    if (argc > 1 && is_numeric(argv[0]))
    {
        for (int i = 0; i < argc; i++)
        {
            if (!is_numeric(argv[i]))
                vm_error(vm, "[max] All arguments must be numeric.");

            double num = as_number(argv[i]);
            if (!initialized || num > max_val)
            {
                max_val = num;
                initialized = true;
            }
        }
        return NEW_NUM(max_val);
    }
    else if (IS_LIST(argv[0]))
    {
        list_t *input = AS_CLIST(argv[0]);

        if (input->size == 0)
            vm_error(vm, "[max] cannot operate on an empty list.");

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[max] All elements in the list must be numeric.");

            double num = as_number(item);
            if (!initialized || num > max_val)
            {
                max_val = num;
                initialized = true;
            }
        }
    }
    else if (IS_SET(argv[0]))
    {
        PiSet *set = AS_SET(argv[0]);

        if (set_size(set) == 0)
            vm_error(vm, "[max] cannot operate on an empty set.");

        for (int i = 0; i < set_size(set); i++)
        {
            Value actual = set_get(set, i);

            if (!is_numeric(actual))
                vm_error(vm, "[max] All elements in the set must be numeric.");

            double num = as_number(actual);
            if (!initialized || num > max_val)
            {
                max_val = num;
                initialized = true;
            }
        }
    }
    else
        vm_error(vm, "[max] expects a list or set of numeric values.");

    return NEW_NUM(max_val);
}

Value pi_abs(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[abs] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
        return NEW_NUM(fabs(as_number(arg)));

    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[abs] All elements in the list must be numeric.");

            double _abs = fabs(as_number(item));
            Value val = NEW_NUM(_abs);
            list_add(result, &val);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[abs] expects a numeric value or a list of numeric values.");

    return NEW_NIL();
}

Value mt_floor(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[floor] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
        return NEW_NUM(floor(as_number(arg)));

    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[floor] All elements in the list must be numeric.");

            double _floor = floor(as_number(item));
            Value val = NEW_NUM(_floor);
            list_add(result, &val);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[floor] expects a numeric value or a list of numberic values.");
    return NEW_NIL();
}

Value mt_ceil(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[ceil] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
        return NEW_NUM(ceil(as_number(arg)));

    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[ceil] All elements in the list must be numeric.");

            double _ceil = ceil(as_number(item));
            Value val = NEW_NUM(_ceil);
            list_add(result, &val);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[ceil] expects a numeric value or a list of numeric values.");
    return NEW_NIL();
}

Value mt_sqrt(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[sqrt] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
        return NEW_NUM(sqrt(as_number(arg)));

    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[sqrt] All elements in the list must be numeric.");

            double _sqrt = sqrt(as_number(item));
            Value val = NEW_NUM(_sqrt);
            list_add(result, &val);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[sqrt] expects a numeric value or a list of numeric values.");
    return NEW_NIL();
}

Value mt_sin(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[sin] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
        return NEW_NUM(sin(as_number(arg)));

    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[sin] All elements in the list must be numeric.");

            double _sin = sin(as_number(item));
            Value val = NEW_NUM(_sin);
            list_add(result, &val);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[sin] expects a numeric value or a list of numeric values.");
    return NEW_NIL();
}

Value mt_cos(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[cos] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
        return NEW_NUM(cos(as_number(arg)));

    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[cos] All elements in the list must be numeric.");

            double _cos = cos(as_number(item));
            Value val = NEW_NUM(_cos);
            list_add(result, &val);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[cos] expects a numeric value or a list of numeric values.");
    return NEW_NIL();
}

Value mt_asin(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[asin] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
    {
        double val = as_number(arg);
        if (val < -1.0 || val > 1.0)
            vm_error(vm, "[asin] argument must be in the range [-1, 1].");

        double result = asin(val);
        return NEW_NUM(result);
    }
    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[asin] All elements in the list must be numeric.");

            double val = as_number(item);
            if (val < -1.0 || val > 1.0)
                vm_error(vm, "[asin] All list elements must be in the range [-1, 1].");

            double res = asin(val);
            Value val_obj = NEW_NUM(res);
            list_add(result, &val_obj);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[asin] expects a numeric value or a list of numeric values.");
    return NEW_NIL();
}

Value mt_tan(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[tan] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
        return NEW_NUM(tan(as_number(arg)));

    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[tan] All elements in the list must be numeric.");

            double _tan = tan(as_number(item));
            Value val = NEW_NUM(_tan);
            list_add(result, &val);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[tan] expects a numeric value or a list of numeric values.");
    return NEW_NIL();
}

Value mt_acos(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[acos] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
    {
        double val = as_number(arg);
        if (val < -1.0 || val > 1.0)
            vm_error(vm, "[acos] argument must be in the range [-1, 1].");

        double result = acos(val);
        return NEW_NUM(result);
    }
    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[acos] All elements in the list must be numeric.");

            double val = as_number(item);
            if (val < -1.0 || val > 1.0)
                vm_error(vm, "[acos] All list elements must be in the range [-1, 1].");

            double res = acos(val);
            Value val_obj = NEW_NUM(res);
            list_add(result, &val_obj);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[acos] expects a numeric value or a list of numeric values.");

    return NEW_NIL();
}

Value mt_atan(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[atan] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
    {
        double val = as_number(arg);
        double result = atan(val);
        return NEW_NUM(result);
    }
    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[atan] All elements in the list must be numeric.");

            double val = as_number(item);
            double res = atan(val);
            Value val_obj = NEW_NUM(res);
            list_add(result, &val_obj);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[atan] expects a numeric value or a list of numeric values.");
    return NEW_NIL();
}

Value mt_deg(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[deg] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
    {
        double val = as_number(arg);
        double result = val * RAD_TO_DEG;
        return NEW_NUM(result);
    }
    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[deg] All elements in the list must be numeric.");

            double val = as_number(item);
            double res = val * RAD_TO_DEG;
            Value val_obj = NEW_NUM(res);
            list_add(result, &val_obj);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[deg] expects a numeric value or a list of numeric values.");

    return NEW_NIL();
}

Value mt_rad(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[rad] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
    {
        double val = as_number(arg);
        double result = val * DEG_TO_RAD;
        return NEW_NUM(result);
    }
    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[rad] All elements in the list must be numeric.");

            double val = as_number(item);
            double res = val * DEG_TO_RAD;
            Value val_obj = NEW_NUM(res);
            list_add(result, &val_obj);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[rad] expects a numeric value or a list of numeric values.");

    return NEW_NIL();
}

Value mt_sum(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0 || !IS_LIST(argv[0]))
        vm_error(vm, "[sum] expects a single list of numeric values.");

    list_t *input = AS_CLIST(argv[0]);
    double total = 0.0;

    for (int i = 0; i < input->size; i++)
    {
        Value item = *(Value *)list_getAt(input, i);
        if (!is_numeric(item))
            vm_error(vm, "[sum] All elements in the list must be numeric.");
        total += as_number(item);
    }

    return NEW_NUM(total);
}

Value mt_exp(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[exp] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
        return NEW_NUM(exp(as_number(arg)));

    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[exp] All elements in the list must be numeric.");

            double val = exp(as_number(item));
            Value v = NEW_NUM(val);
            list_add(result, &v);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[exp] expects a numeric value or a list of numeric values.");

    return NEW_NIL();
}

Value mt_log2(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[log2] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
    {
        double num = as_number(arg);
        if (num <= 0)
            vm_error(vm, "[log2] input must be positive.");
        return NEW_NUM(log2(num));
    }
    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[log2] All elements in the list must be numeric.");

            double val = as_number(item);
            if (val <= 0)
                vm_error(vm, "[log2] All elements must be positive.");

            Value v = NEW_NUM(log2(val));
            list_add(result, &v);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[log2] expects a numeric value or a list of numeric values.");
    return NEW_NIL();
}

Value mt_log10(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        vm_error(vm, "[log10] expects a numeric value or a list of numeric values.");

    Value arg = argv[0];

    if (is_numeric(arg))
    {
        double num = as_number(arg);
        if (num <= 0)
            vm_error(vm, "[log10] input must be positive.");
        return NEW_NUM(log10(num));
    }
    else if (IS_LIST(arg))
    {
        list_t *input = AS_CLIST(arg);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);
            if (!is_numeric(item))
                vm_error(vm, "[log10] All elements in the list must be numeric.");

            double val = as_number(item);
            if (val <= 0)
                vm_error(vm, "[log10] All elements must be positive.");

            Value v = NEW_NUM(log10(val));
            list_add(result, &v);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;
        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[log10] expects a numeric value or a list of numeric values.");
    return NEW_NIL();
}

Value pi_pow(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2)
        vm_error(vm, "[pow] expects exactly two arguments: base and exponent.");

    Value base = argv[0];
    Value exponent = argv[1];

    if (!is_numeric(exponent))
        vm_error(vm, "[pow] The exponent must be a numeric value.");

    double exp_num = as_number(exponent);

    if (is_numeric(base))
    {
        double base_num = as_number(base);
        return NEW_NUM(pow(base_num, exp_num));
    }
    else if (IS_LIST(base))
    {
        list_t *input = AS_CLIST(base);
        list_t *result = list_create(sizeof(Value));

        for (int i = 0; i < input->size; i++)
        {
            Value item = *(Value *)list_getAt(input, i);

            if (!is_numeric(item))
                vm_error(vm, "[pow] All elements in the base list must be numeric.");

            double val = as_number(item);
            double pow_val = pow(val, exp_num);
            Value v = NEW_NUM(pow_val);
            list_add(result, &v);
        }

        PiList *list = (PiList *)new_list(result);
        list->is_numeric = true;

        return NEW_OBJ(list);
    }
    else
        vm_error(vm, "[pow] The base argument must be a numeric value or a list of numeric values.");
    return NEW_NIL();
}

Value mt_logE(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0 || !is_numeric(argv[0]))
        vm_error(vm, "[log] expects a single numeric argument.");

    double result = log(as_number(argv[0]));

    return NEW_NUM(result);
}

Value mt_linspace(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3 || !is_numeric(argv[0]) || !is_numeric(argv[1]) || !is_numeric(argv[2]))
        vm_error(vm, "[linspace] expects three numeric arguments: start, end, and count.");

    double start = as_number(argv[0]);
    double end = as_number(argv[1]);
    int count = (int)as_number(argv[2]);

    if (count <= 0)
        vm_error(vm, "[linspace] count must be a positive integer.");

    list_t *list = list_create(sizeof(Value));

    if (count == 1)
    {
        Value val = NEW_NUM(start);
        list_add(list, &val);
    }
    else
    {
        double step = (end - start) / (count - 1);
        for (int i = 0; i < count; i++)
        {
            double val = start + i * step;
            Value v = NEW_NUM(val);
            list_add(list, &v);
        }
    }

    PiList *result = (PiList *)new_list(list);
    result->is_numeric = true;

    return NEW_OBJ(result);
}

// Produces values in [start, end), matching Python/NumPy arange semantics.
Value mt_arange(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !is_numeric(argv[0]) || !is_numeric(argv[1]) || (argc >= 3 && !is_numeric(argv[2])))
        vm_error(vm, "[arange] expects 2 or 3 numeric arguments: start, end, and optional step.");

    double start = as_number(argv[0]);
    double end = as_number(argv[1]);
    double step = (argc >= 3) ? as_number(argv[2]) : 1.0;

    if (step <= 0)
        vm_error(vm, "[arange] step must be a positive number.");

    list_t *list = list_create(sizeof(Value));

    for (double val = start; val < end; val += step)
    {
        Value v = NEW_NUM(val);
        list_add(list, &v);
    }

    PiList *result = (PiList *)new_list(list);
    result->is_numeric = true;

    return NEW_OBJ(result);
}

// Module definition
static BuiltinConst math_consts[] = {
    {"PI", {VAL_NUM, {.number = PI}}},
    {"E", {VAL_NUM, {.number = E}}},
    {"TAU", {VAL_NUM, {.number = TAU}}},
    {"PHI", {VAL_NUM, {.number = PHI}}},
    {"NaN", {VAL_NUM, {.number = NAN}}},
    {"INF", {VAL_NUM, {.number = INFINITY}}},
};

static BuiltinFunc math_funcs[] = {
    {"floor", mt_floor},
    {"ceil", mt_ceil},
    {"sqrt", mt_sqrt},
    {"sin", mt_sin},
    {"cos", mt_cos},
    {"tan", mt_tan},
    {"asin", mt_asin},
    {"acos", mt_acos},
    {"atan", mt_atan},
    {"deg", mt_deg},
    {"rad", mt_rad},
    {"sum", mt_sum},
    {"exp", mt_exp},
    {"log2", mt_log2},
    {"log10", mt_log10},
    {"logE", mt_logE},
    {"linspace", mt_linspace},
    {"arange", mt_arange}};

DEFINE_BUILTIN_MODULE(module_math, "math", math_funcs, math_consts);
