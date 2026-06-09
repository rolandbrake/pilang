#include <math.h>
#include <stdint.h>

#include "pi_builtin.h"
#include "pi_math.h"
#include "pi_random.h"
#include "../pi_list.h"
#include "../pi_object.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int random_intInclusive(vm_t *vm, int min, int max, const char *name)
{
    if (min > max)
        vm_errorf(vm, "[random.%s] min must not be greater than max.", name);

    int range = max - min + 1;
    return min + (int)(rand_num() * range);
}

Value rd_seed(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_NUM(argv[0]))
        vm_error(vm, "[random.seed] expects a numeric seed.");

    rng_seed((uint32_t)AS_NUM(argv[0]));
    return NEW_NIL();
}

Value rd_rand(vm_t *vm, int argc, Value *argv)
{
    (void)argv;
    // if (argc != 0)
    //     vm_error(vm, "[random.rand] expects no arguments.");

    return NEW_NUM(rand_num());
}

Value rd_uniform(vm_t *vm, int argc, Value *argv)
{
    if (argc == 0)
        return NEW_NUM(rand_num());
    if (argc != 2 || !IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "[random.uniform] expects optional numeric min and max arguments.");

    double min = AS_NUM(argv[0]);
    double max = AS_NUM(argv[1]);
    if (min > max)
        vm_error(vm, "[random.uniform] min must not be greater than max.");

    return NEW_NUM(min + rand_num() * (max - min));
}

Value rd_randint(vm_t *vm, int argc, Value *argv)
{
    if (argc == 1 && IS_NUM(argv[0]))
        return NEW_NUM(random_intInclusive(vm, 0, (int)AS_NUM(argv[0]), "randint"));
    if (argc == 2 && IS_NUM(argv[0]) && IS_NUM(argv[1]))
        return NEW_NUM(random_intInclusive(vm, (int)AS_NUM(argv[0]), (int)AS_NUM(argv[1]), "randint"));

    vm_error(vm, "[random.randint] expects max or min, max numeric arguments.");
    return NEW_NIL();
}

Value rd_normal(vm_t *vm, int argc, Value *argv)
{
    double mean = 0.0;
    double stddev = 1.0;

    if (argc > 0)
    {
        if (!IS_NUM(argv[0]))
            vm_error(vm, "[random.normal] mean must be numeric.");
        mean = AS_NUM(argv[0]);
    }
    if (argc > 1)
    {
        if (!IS_NUM(argv[1]))
            vm_error(vm, "[random.normal] stddev must be numeric.");
        stddev = AS_NUM(argv[1]);
    }
    if (argc > 2)
        vm_error(vm, "[random.normal] expects optional mean and stddev arguments.");
    if (stddev < 0)
        vm_error(vm, "[random.normal] stddev must be non-negative.");

    double u1 = rand_num();
    double u2 = rand_num();
    if (u1 <= 0.0)
        u1 = 1.0 / (double)UINT32_MAX;

    double z = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
    return NEW_NUM(mean + z * stddev);
}

Value rd_choice(vm_t *vm, int argc, Value *argv)
{
    if (argc != 1 || !IS_LIST(argv[0]))
        vm_error(vm, "[random.choice] expects a list.");

    PiList *list = AS_LIST(argv[0]);
    int size = LIST_SIZE(list->items);
    if (size <= 0)
        vm_error(vm, "[random.choice] cannot choose from an empty list.");

    int index = random_intInclusive(vm, 0, size - 1, "choice");
    return *(Value *)list_getAt(list->items, index);
}

Value rd_shuffle(vm_t *vm, int argc, Value *argv)
{
    if (argc != 1 || !IS_LIST(argv[0]))
        vm_error(vm, "[random.shuffle] expects a list.");

    PiList *source = AS_LIST(argv[0]);
    list_t *items = list_create(sizeof(Value));

    for (int i = 0; i < LIST_SIZE(source->items); i++)
    {
        Value item = *(Value *)list_getAt(source->items, i);
        list_add(items, &item);
    }

    for (int i = LIST_SIZE(items) - 1; i > 0; i--)
    {
        int j = random_intInclusive(vm, 0, i, "shuffle");
        Value tmp = *(Value *)list_getAt(items, i);
        *(Value *)list_getAt(items, i) = *(Value *)list_getAt(items, j);
        *(Value *)list_getAt(items, j) = tmp;
    }

    PiList *result = (PiList *)new_list(items);
    result->is_numeric = source->is_numeric;
    result->is_matrix = source->is_matrix;
    result->rows = source->rows;
    result->cols = source->cols;
    return NEW_OBJ(add_obj(vm, (Object *)result));
}

static BuiltinFunc random_funcs[] = {
    {"seed", rd_seed},
    {"rand", rd_rand},
    {"randi", rd_randint},
    {"uniform", rd_uniform},
    {"randint", rd_randint},    
    {"normal", rd_normal},
    {"choice", rd_choice},
    {"shuffle", rd_shuffle},
};

DEFINE_BUILTIN_MODULE(module_random, "random", random_funcs, NULL);
