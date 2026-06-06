#include "pi_tensor.h"
#include "pi_builtin.h"
#include "../pi_func.h"
#include "../pi_list.h"
#include "../pi_object.h"
#include <stdlib.h>
#include <math.h>
#include <limits.h>

static int double_cmp(const void *a, const void *b)
{
    double x = *(double *)a;
    double y = *(double *)b;
    return (x > y) - (x < y);
}

static int tn_shapeFromArgs(vm_t *vm, int argc, Value *argv, int **out_shape)
{
    if (argc == 1 && IS_LIST(argv[0]))
    {
        PiList *shape_list = AS_LIST(argv[0]);
        int ndim = shape_list->items->size;
        if (ndim <= 0)
            vm_error(vm, "tensor shape cannot be empty.");

        int *shape = malloc(sizeof(int) * (size_t)ndim);
        if (!shape)
            vm_error(vm, "tensor shape allocation failed.");

        for (int i = 0; i < ndim; i++)
        {
            Value dim = *(Value *)list_getAt(shape_list->items, i);
            if (!IS_NUM(dim) || AS_NUM(dim) < 0 || floor(AS_NUM(dim)) != AS_NUM(dim) || AS_NUM(dim) > INT_MAX)
                vm_error(vm, "tensor shape dimensions must be non-negative integer numbers.");
            shape[i] = (int)AS_NUM(dim);
        }
        *out_shape = shape;
        return ndim;
    }

    if (argc <= 0)
        vm_error(vm, "expected a tensor shape.");

    int *shape = malloc(sizeof(int) * (size_t)argc);
    if (!shape)
        vm_error(vm, "tensor shape allocation failed.");

    for (int i = 0; i < argc; i++)
    {
        if (!IS_NUM(argv[i]) || AS_NUM(argv[i]) < 0 || floor(AS_NUM(argv[i])) != AS_NUM(argv[i]) || AS_NUM(argv[i]) > INT_MAX)
            vm_error(vm, "tensor shape dimensions must be non-negative integer numbers.");
        shape[i] = (int)AS_NUM(argv[i]);
    }

    *out_shape = shape;
    return argc;
}

static Value tn_makeFilled(vm_t *vm, int argc, Value *argv, double fill, bool randomize)
{
    int *shape = NULL;
    int ndim = tn_shapeFromArgs(vm, argc, argv, &shape);
    PiTensor *tensor = (PiTensor *)add_obj(vm, randomize || fill != 0.0
                                                   ? new_tensorUninit(ndim, shape, TN_FLOAT64)
                                                   : new_tensor(ndim, shape, TN_FLOAT64));
    free(shape);

    if (randomize)
    {
        for (int i = 0; i < tensor->size; i++)
            tensor->data.f64[i] = rand() / (double)RAND_MAX;
    }
    else if (fill != 0.0)
    {
        for (int i = 0; i < tensor->size; i++)
            tensor->data.f64[i] = fill;
    }

    return NEW_OBJ(tensor);
}

Value tn_zeros(vm_t *vm, int argc, Value *argv)
{
    return tn_makeFilled(vm, argc, argv, 0.0, false);
}

Value tn_ones(vm_t *vm, int argc, Value *argv)
{
    return tn_makeFilled(vm, argc, argv, 1.0, false);
}

Value tn_rand(vm_t *vm, int argc, Value *argv)
{
    return tn_makeFilled(vm, argc, argv, 0.0, true);
}

static bool infer_shape(Value value, int depth, int *shape, int *ndim)
{
    if (IS_NUM(value))
    {
        if (*ndim == -1)
            *ndim = depth;
        return *ndim == depth;
    }

    if (!IS_LIST(value))
        return false;

    PiList *list = AS_LIST(value);
    if (*ndim != -1 && depth >= *ndim)
        return false;

    int size = list->items->size;
    if (shape[depth] == -1)
        shape[depth] = size;
    else if (shape[depth] != size)
        return false;

    for (int i = 0; i < size; i++)
    {
        Value child = *(Value *)list_getAt(list->items, i);
        if (!infer_shape(child, depth + 1, shape, ndim))
            return false;
    }

    if (size == 0 && *ndim == -1)
        *ndim = depth + 1;

    return true;
}

static void flatten_tensor(Value value, PiTensor *tensor, int *cursor)
{
    if (IS_NUM(value))
    {
        tensor_setFlat(tensor, (*cursor)++, AS_NUM(value));
        return;
    }

    PiList *list = AS_LIST(value);
    for (int i = 0; i < list->items->size; i++)
        flatten_tensor(*(Value *)list_getAt(list->items, i), tensor, cursor);
}

static void coords_from_flat(int index, PiTensor *tensor, int *coords)
{
    for (int d = tensor->ndim - 1; d >= 0; d--)
    {
        coords[d] = index % tensor->shape[d];
        index /= tensor->shape[d];
    }
}

static int flat_from_coords(int ndim, int *shape, int *coords)
{
    int flat = 0;
    for (int d = 0; d < ndim; d++)
        flat = flat * shape[d] + coords[d];
    return flat;
}

Value tn_from(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "tensor.from expects one numeric nested list.");

    if (IS_TENSOR(argv[0]))
    {
        PiTensor *src = AS_TENSOR(argv[0]);
        PiTensor *copy = (PiTensor *)add_obj(vm, new_tensor(src->ndim, src->shape, src->type));
        for (int i = 0; i < src->size; i++)
            tensor_setFlat(copy, i, tensor_getFlat(src, i));
        return NEW_OBJ(copy);
    }

    int shape[16];
    for (int i = 0; i < 16; i++)
        shape[i] = -1;

    int ndim = -1;
    if (!infer_shape(argv[0], 0, shape, &ndim) || ndim <= 0)
        vm_error(vm, "tensor.from expects a rectangular numeric nested list.");

    PiTensor *tensor = (PiTensor *)add_obj(vm, new_tensorUninit(ndim, shape, TN_FLOAT64));
    int cursor = 0;
    flatten_tensor(argv[0], tensor, &cursor);
    return NEW_OBJ(tensor);
}

Value tn_fill(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_NUM(argv[argc - 1]))
        vm_error(vm, "tensor.fill expects a shape and a fill value");

    double fill = AS_NUM(argv[argc - 1]);
    return tn_makeFilled(vm, argc - 1, argv, fill, false);
}

Value tn_shape(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.shape expects a tensor.");

    PiTensor *tensor = AS_TENSOR(argv[0]);
    list_t *items = list_create(sizeof(Value));
    for (int i = 0; i < tensor->ndim; i++)
    {
        Value dim = NEW_NUM(tensor->shape[i]);
        list_add(items, &dim);
    }

    PiList *shape = (PiList *)new_list(items);
    shape->is_numeric = true;
    shape->is_matrix = false;
    shape->rows = 1;
    shape->cols = tensor->ndim;
    return NEW_OBJ(add_obj(vm, (Object *)shape));
}

Value tn_ndim(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.ndim expects a tensor.");
    return NEW_NUM(AS_TENSOR(argv[0])->ndim);
}

Value tn_size(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.size expects a tensor.");
    return NEW_NUM(AS_TENSOR(argv[0])->size);
}

Value tn_reshape(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.reshape expects a tensor and a shape.");

    int *shape = NULL;
    int ndim = tn_shapeFromArgs(vm, argc - 1, argv + 1, &shape);
    int size = 1;
    for (int i = 0; i < ndim; i++)
        size *= shape[i];

    PiTensor *src = AS_TENSOR(argv[0]);
    if (size != src->size)
        vm_error(vm, "tensor.reshape cannot change tensor element count.");

    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(ndim, shape, src->type));
    free(shape);

    for (int i = 0; i < src->size; i++)
        tensor_setFlat(result, i, tensor_getFlat(src, i));

    return NEW_OBJ(result);
}

Value tn_eye(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "tensor.eye expects two numbers (rows, cols)");

    int rows = (int)AS_NUM(argv[0]);
    int cols = (int)AS_NUM(argv[1]);
    int shape[2] = {rows, cols};
    PiTensor *tensor = (PiTensor *)add_obj(vm, new_tensor(2, shape, TN_FLOAT64));

    int min_dim = rows < cols ? rows : cols;
    for (int i = 0; i < min_dim; i++)
        tensor_set(tensor, (int[]){i, i}, 1.0);

    return NEW_OBJ(tensor);
}

Value tn_randn(vm_t *vm, int argc, Value *argv)
{
    int *shape = NULL;
    int ndim = tn_shapeFromArgs(vm, argc, argv, &shape);
    PiTensor *tensor = (PiTensor *)add_obj(vm, new_tensor(ndim, shape, TN_FLOAT64));
    free(shape);

    // Box-Muller for normal distribution
    for (int i = 0; i < tensor->size; i += 2)
    {
        double u1 = rand() / (double)RAND_MAX;
        double u2 = rand() / (double)RAND_MAX;
        double mag = sqrt(-2.0 * log(u1));
        double z0 = mag * cos(2.0 * PI * u2);
        double z1 = mag * sin(2.0 * PI * u2);
        tensor->data.f64[i] = z0;
        if (i + 1 < tensor->size)
            tensor->data.f64[i + 1] = z1;
    }

    return NEW_OBJ(tensor);
}

Value tn_randint(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3)
        vm_error(vm, "randint expects shape and high or shape, low, high");

    for (int i = 0; i < argc; i++)
        if (!IS_NUM(argv[i]))
            vm_error(vm, "randint expects numeric arguments");

    int low = 0;
    int high = 0;
    int *shape = NULL;
    int ndim = 0;

    if (argc == 3)
    {
        int shape_vals[2] = {(int)AS_NUM(argv[0]), (int)AS_NUM(argv[1])};
        if (shape_vals[0] <= 0 || shape_vals[1] <= 0)
            vm_error(vm, "randint: shape dimensions must be positive");
        shape = malloc(sizeof(int) * 2);
        shape[0] = shape_vals[0];
        shape[1] = shape_vals[1];
        ndim = 2;
        low = 0;
        high = (int)AS_NUM(argv[2]);
    }
    else if (argc == 4)
    {
        int shape_vals[2] = {(int)AS_NUM(argv[0]), (int)AS_NUM(argv[1])};
        if (shape_vals[0] <= 0 || shape_vals[1] <= 0)
            vm_error(vm, "randint: shape dimensions must be positive");
        shape = malloc(sizeof(int) * 2);
        shape[0] = shape_vals[0];
        shape[1] = shape_vals[1];
        ndim = 2;
        low = (int)AS_NUM(argv[2]);
        high = (int)AS_NUM(argv[3]);
    }
    else
    {
        ndim = argc - 2;

        if (ndim <= 0 || ndim > MAX_TENSOR_DIMS)
        {
            vm_error(vm, "invalid tensor dimensions");
            return NEW_NIL();
        }

        size_t shape_size = (size_t)ndim * sizeof(int);
        shape = malloc(shape_size);

        if (!shape)
            vm_error(vm, "memory allocation failed");

        for (int i = 0; i < ndim; i++)
        {
            shape[i] = (int)AS_NUM(argv[i]);
            if (shape[i] <= 0)
                vm_error(vm, "randint: shape dimensions must be positive");
        }
        low = (int)AS_NUM(argv[argc - 2]);
        high = (int)AS_NUM(argv[argc - 1]);
    }

    if (high <= low)
        vm_error(vm, "randint: high must be greater than low");

    PiTensor *tensor = (PiTensor *)add_obj(vm, new_tensorUninit(ndim, shape, TN_FLOAT64));
    free(shape);

    int range = high - low;
    for (int i = 0; i < tensor->size; i++)
        tensor->data.f64[i] = (double)(low + rand() % range);

    return NEW_OBJ(tensor);
}

Value tn_slice(vm_t *vm, int argc, Value *argv)
{
    if (argc < 4 || !IS_TENSOR(argv[0]) || !IS_NUM(argv[1]) || !IS_NUM(argv[2]) || !IS_NUM(argv[3]))
        vm_error(vm, "slice expects tensor, axis, start, stop");

    PiTensor *src = AS_TENSOR(argv[0]);
    int axis = (int)AS_NUM(argv[1]);
    int start = (int)AS_NUM(argv[2]);
    int stop = (int)AS_NUM(argv[3]);

    if (axis < 0 || axis >= src->ndim)
        vm_error(vm, "invalid axis");

    if (start < 0 || stop < start || stop > src->shape[axis])
        vm_error(vm, "invalid slice range");

    int new_shape[MAX_TENSOR_DIMS];
    for (int i = 0; i < src->ndim; i++)
        new_shape[i] = src->shape[i];
    new_shape[axis] = stop - start;

    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(src->ndim, new_shape, src->type));

    int coords[MAX_TENSOR_DIMS];
    int new_coords[MAX_TENSOR_DIMS];

    for (int flat = 0; flat < src->size; flat++)
    {
        coords_from_flat(flat, src, coords);
        if (coords[axis] < start || coords[axis] >= stop)
            continue;

        for (int d = 0; d < src->ndim; d++)
            new_coords[d] = coords[d] - (d == axis ? start : 0);

        int target_flat = flat_from_coords(src->ndim, new_shape, new_coords);
        tensor_setFlat(result, target_flat, tensor_getFlat(src, flat));
    }

    return NEW_OBJ(result);
}

Value tn_concat(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3 || !IS_TENSOR(argv[0]) || !IS_TENSOR(argv[1]) || !IS_NUM(argv[2]))
        vm_error(vm, "concat expects two tensors and axis");

    PiTensor *a = AS_TENSOR(argv[0]);
    PiTensor *b = AS_TENSOR(argv[1]);
    int axis = (int)AS_NUM(argv[2]);

    if (axis < 0 || axis >= a->ndim || a->ndim != b->ndim)
        vm_error(vm, "invalid axis or mismatched dimensions");

    for (int i = 0; i < a->ndim; i++)
        if (i != axis && a->shape[i] != b->shape[i])
            vm_error(vm, "tensors must have same shape except along axis");

    int new_shape[MAX_TENSOR_DIMS];
    for (int i = 0; i < a->ndim; i++)
        new_shape[i] = a->shape[i];
    new_shape[axis] += b->shape[axis];

    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(a->ndim, new_shape, a->type));

    int coords[MAX_TENSOR_DIMS];
    int target_coords[MAX_TENSOR_DIMS];

    for (int flat = 0; flat < a->size; flat++)
    {
        coords_from_flat(flat, a, coords);
        for (int d = 0; d < a->ndim; d++)
            target_coords[d] = coords[d];
        int target_flat = flat_from_coords(a->ndim, new_shape, target_coords);
        tensor_setFlat(result, target_flat, tensor_getFlat(a, flat));
    }

    for (int flat = 0; flat < b->size; flat++)
    {
        coords_from_flat(flat, b, coords);
        for (int d = 0; d < b->ndim; d++)
            target_coords[d] = coords[d] + (d == axis ? a->shape[axis] : 0);
        int target_flat = flat_from_coords(b->ndim, new_shape, target_coords);
        tensor_setFlat(result, target_flat, tensor_getFlat(b, flat));
    }

    return NEW_OBJ(result);
}

Value tn_transpose(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]) || argc == 2)
        vm_error(vm, "transpose expects a tensor and optional axes");

    PiTensor *src = AS_TENSOR(argv[0]);
    if (src->ndim != 2)
        vm_error(vm, "transpose only supports 2D tensors");

    int new_shape[2] = {src->shape[1], src->shape[0]};
    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(2, new_shape, src->type));

    for (int i = 0; i < src->shape[0]; i++)
        for (int j = 0; j < src->shape[1]; j++)
            tensor_set(result, (int[]){j, i}, tensor_get(src, (int[]){i, j}));

    return NEW_OBJ(result);
}

Value tn_flatten(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.flatten expects a tensor");

    PiTensor *src = AS_TENSOR(argv[0]);
    int new_shape[1] = {src->size};
    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(1, new_shape, src->type));

    for (int i = 0; i < src->size; i++)
        tensor_setFlat(result, i, tensor_getFlat(src, i));

    return NEW_OBJ(result);
}

Value tn_expand_dims(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_TENSOR(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "tensor.expand_dims expects a tensor and axis");

    PiTensor *src = AS_TENSOR(argv[0]);
    int axis = (int)AS_NUM(argv[1]);

    if (axis < 0 || axis > src->ndim)
        vm_error(vm, "invalid axis");

    int new_shape[MAX_TENSOR_DIMS];
    for (int i = 0; i < src->ndim + 1; i++)
    {
        if (i < axis)
            new_shape[i] = src->shape[i];
        else if (i == axis)
            new_shape[i] = 1;
        else
            new_shape[i] = src->shape[i - 1];
    }

    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(src->ndim + 1, new_shape, src->type));

    // Copy data
    for (int i = 0; i < src->size; i++)
    {
        int indices[MAX_TENSOR_DIMS];
        int temp = i;
        for (int d = src->ndim - 1; d >= 0; d--)
        {
            indices[d] = temp % src->shape[d];
            temp /= src->shape[d];
        }
        int new_indices[MAX_TENSOR_DIMS];
        for (int d = 0; d < src->ndim + 1; d++)
        {
            if (d < axis)
                new_indices[d] = indices[d];
            else if (d == axis)
                new_indices[d] = 0;
            else
                new_indices[d] = indices[d - 1];
        }
        tensor_set(result, new_indices, tensor_getFlat(src, i));
    }

    return NEW_OBJ(result);
}

static Value elementwise_unary(vm_t *vm, PiTensor *tensor, double (*fn)(double))
{
    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(tensor->ndim, tensor->shape, tensor->type));
    for (int i = 0; i < tensor->size; i++)
        tensor_setFlat(result, i, fn(tensor_getFlat(tensor, i)));
    return NEW_OBJ(result);
}

static Value elementwise_binary(vm_t *vm, PiTensor *a, PiTensor *b, double (*op)(double, double))
{
    if (a->ndim != b->ndim)
        vm_error(vm, "tensors must have same dimensions");

    for (int i = 0; i < a->ndim; i++)
        if (a->shape[i] != b->shape[i])
            vm_error(vm, "tensors must have same shape");

    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(a->ndim, a->shape, a->type));
    for (int i = 0; i < a->size; i++)
        tensor_setFlat(result, i, op(tensor_getFlat(a, i), tensor_getFlat(b, i)));
    return NEW_OBJ(result);
}

static double op_add(double a, double b) { return a + b; }
static double op_sub(double a, double b) { return a - b; }
static double op_mul(double a, double b) { return a * b; }
static double op_div(double a, double b) { return a / b; }

Value tn_add(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_TENSOR(argv[0]) || !IS_TENSOR(argv[1]))
        vm_error(vm, "tensor.add expects two tensors");
    return elementwise_binary(vm, AS_TENSOR(argv[0]), AS_TENSOR(argv[1]), op_add);
}

Value tn_sub(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_TENSOR(argv[0]) || !IS_TENSOR(argv[1]))
        vm_error(vm, "tensor.sub expects two tensors");
    return elementwise_binary(vm, AS_TENSOR(argv[0]), AS_TENSOR(argv[1]), op_sub);
}

Value tn_mult(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_TENSOR(argv[0]) || !IS_TENSOR(argv[1]))
        vm_error(vm, "tensor.mult expects two tensors");
    return elementwise_binary(vm, AS_TENSOR(argv[0]), AS_TENSOR(argv[1]), op_mul);
}

Value tn_div(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_TENSOR(argv[0]) || !IS_TENSOR(argv[1]))
        vm_error(vm, "tensor.div expects two tensors");
    return elementwise_binary(vm, AS_TENSOR(argv[0]), AS_TENSOR(argv[1]), op_div);
}

Value tn_exp(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.exp expects a tensor");
    return elementwise_unary(vm, AS_TENSOR(argv[0]), exp);
}

Value tn_log(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.log expects a tensor");
    return elementwise_unary(vm, AS_TENSOR(argv[0]), log);
}

Value tn_sqrt(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.sqrt expects a tensor");
    return elementwise_unary(vm, AS_TENSOR(argv[0]), sqrt);
}

Value tn_abs(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.abs expects a tensor");
    return elementwise_unary(vm, AS_TENSOR(argv[0]), fabs);
}

Value tn_clip(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3 || !IS_TENSOR(argv[0]) || !IS_NUM(argv[1]) || !IS_NUM(argv[2]))
        vm_error(vm, "tensor.clip expects tensor, min, max");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    double min_val = AS_NUM(argv[1]);
    double max_val = AS_NUM(argv[2]);
    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(tensor->ndim, tensor->shape, tensor->type));
    for (int i = 0; i < tensor->size; i++)
    {
        double val = tensor_getFlat(tensor, i);
        if (val < min_val)
            val = min_val;
        if (val > max_val)
            val = max_val;
        tensor_setFlat(result, i, val);
    }
    return NEW_OBJ(result);
}

Value tn_sign(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.sign expects a tensor");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(tensor->ndim, tensor->shape, tensor->type));
    for (int i = 0; i < tensor->size; i++)
    {
        double val = tensor_getFlat(tensor, i);
        tensor_setFlat(result, i, val > 0 ? 1 : val < 0 ? -1
                                                        : 0);
    }
    return NEW_OBJ(result);
}

Value tn_matmult(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_TENSOR(argv[0]) || !IS_TENSOR(argv[1]))
        vm_error(vm, "tensor.matmult expects two tensors");

    PiTensor *a = AS_TENSOR(argv[0]);
    PiTensor *b = AS_TENSOR(argv[1]);

    if (a->ndim != 2 || b->ndim != 2 || a->shape[1] != b->shape[0])
        vm_error(vm, "tensor.matmult requires 2D tensors with compatible shapes");

    int shape[2] = {a->shape[0], b->shape[1]};
    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(2, shape, a->type));

    for (int i = 0; i < a->shape[0]; i++)
        for (int j = 0; j < b->shape[1]; j++)
        {
            double sum = 0;
            for (int k = 0; k < a->shape[1]; k++)
                sum += tensor_get(a, (int[]){i, k}) * tensor_get(b, (int[]){k, j});
            tensor_set(result, (int[]){i, j}, sum);
        }

    return NEW_OBJ(result);
}

Value tn_dot(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_TENSOR(argv[0]) || !IS_TENSOR(argv[1]))
        vm_error(vm, "tensor.dot expects two tensors");

    PiTensor *a = AS_TENSOR(argv[0]);
    PiTensor *b = AS_TENSOR(argv[1]);

    if (a->ndim != 1 || b->ndim != 1 || a->shape[0] != b->shape[0])
        vm_error(vm, "tensor.dot requires 1D tensors of same size");

    double sum = 0;
    for (int i = 0; i < a->shape[0]; i++)
        sum += tensor_getFlat(a, i) * tensor_getFlat(b, i);

    return NEW_NUM(sum);
}

static Value tensor_reduce(vm_t *vm, PiTensor *tensor, double (*fn)(double, double), double init)
{
    double accum = init;
    for (int i = 0; i < tensor->size; i++)
        accum = fn(accum, tensor_getFlat(tensor, i));
    return NEW_NUM(accum);
}

static double reduce_sum(double accum, double val) { return accum + val; }
static double reduce_prod(double accum, double val) { return accum * val; }
static double reduce_min(double accum, double val) { return val < accum ? val : accum; }
static double reduce_max(double accum, double val) { return val > accum ? val : accum; }

Value tn_sum(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.sum expects a tensor");
    return tensor_reduce(vm, AS_TENSOR(argv[0]), reduce_sum, 0.0);
}

Value tn_mean(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "mean expects a tensor");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    double sum = AS_NUM(tensor_reduce(vm, tensor, reduce_sum, 0.0));
    return NEW_NUM(sum / tensor->size);
}

Value tn_min(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.min expects a tensor");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    if (tensor->size == 0)
        vm_error(vm, "empty tensor");
    return tensor_reduce(vm, tensor, reduce_min, tensor_getFlat(tensor, 0));
}

Value tn_max(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.max expects a tensor");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    if (tensor->size == 0)
        vm_error(vm, "empty tensor");
    return tensor_reduce(vm, tensor, reduce_max, tensor_getFlat(tensor, 0));
}

Value tn_prod(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.prod expects a tensor");
    return tensor_reduce(vm, AS_TENSOR(argv[0]), reduce_prod, 1.0);
}

Value tn_argmax(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.argmax expects a tensor");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    if (tensor->size == 0)
        vm_error(vm, "empty tensor");
    int idx = 0;
    double max_val = tensor_getFlat(tensor, 0);
    for (int i = 1; i < tensor->size; i++)
    {
        double val = tensor_getFlat(tensor, i);
        if (val > max_val)
        {
            max_val = val;
            idx = i;
        }
    }
    return NEW_NUM(idx);
}

Value tn_argmin(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.argmin expects a tensor");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    if (tensor->size == 0)
        vm_error(vm, "tensor.argmin expects a non-empty tensor");
    int idx = 0;
    double min_val = tensor_getFlat(tensor, 0);
    for (int i = 1; i < tensor->size; i++)
    {
        double val = tensor_getFlat(tensor, i);
        if (val < min_val)
        {
            min_val = val;
            idx = i;
        }
    }
    return NEW_NUM(idx);
}

Value tn_any(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.any expects a tensor");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    for (int i = 0; i < tensor->size; i++)
        if (tensor_getFlat(tensor, i) != 0)
            return NEW_BOOL(true);
    return NEW_BOOL(false);
}

Value tn_all(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.all expects a tensor");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    for (int i = 0; i < tensor->size; i++)
        if (tensor_getFlat(tensor, i) == 0)
            return NEW_BOOL(false);
    return NEW_BOOL(true);
}

Value tn_reduce(vm_t *vm, int argc, Value *argv)
{
    if (argc < 3 || !IS_TENSOR(argv[0]) || !IS_FUN(argv[1]) || !IS_NUM(argv[2]))
        vm_error(vm, "tensor.reduce expects a tensor, a binary function, and an initial value");

    PiTensor *tensor = AS_TENSOR(argv[0]);
    Value init = argv[2];
    for (int i = 0; i < tensor->size; i++)
    {
        Value arg = NEW_NUM(tensor_getFlat(tensor, i));
        init = call_func(vm, AS_FUN(argv[1]), 2, &arg, NEW_NIL());
    }
    return init;
}

Value tn_isTensor(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "tensor.is_tensor expects one argument");
    return NEW_BOOL(IS_TENSOR(argv[0]));
}

Value tn_isScalar(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "tensor.is_scalar expects one argument");
    if (!IS_TENSOR(argv[0]))
        return NEW_BOOL(false);
    PiTensor *tensor = AS_TENSOR(argv[0]);
    return NEW_BOOL(tensor->ndim == 0);
}

Value tn_isVector(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "tensor.is_vector expects one argument");
    if (!IS_TENSOR(argv[0]))
        return NEW_BOOL(false);
    PiTensor *tensor = AS_TENSOR(argv[0]);
    return NEW_BOOL(tensor->ndim == 1);
}

Value tn_isMatrix(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1)
        vm_error(vm, "tensor.is_matrix expects one argument");
    if (!IS_TENSOR(argv[0]))
        return NEW_BOOL(false);
    PiTensor *tensor = AS_TENSOR(argv[0]);
    return NEW_BOOL(tensor->ndim == 2);
}

Value tn_apply(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_TENSOR(argv[0]) || !IS_FUN(argv[1]))
        vm_error(vm, "tensor.apply expects a tensor and a function");

    PiTensor *src = AS_TENSOR(argv[0]);
    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(src->ndim, src->shape, src->type));

    for (int i = 0; i < src->size; i++)
    {
        Value arg = NEW_NUM(tensor_getFlat(src, i));
        Value out = call_func(vm, AS_FUN(argv[1]), 1, &arg, NEW_NIL());
        if (!IS_NUM(out))
            vm_error(vm, "tensor.apply function must return numbers");
        tensor_setFlat(result, i, AS_NUM(out));
    }

    return NEW_OBJ(result);
}

Value tn_cross(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_TENSOR(argv[0]) || !IS_TENSOR(argv[1]))
        vm_error(vm, "tensor.cross expects two tensors");

    PiTensor *a = AS_TENSOR(argv[0]);
    PiTensor *b = AS_TENSOR(argv[1]);
    if (a->ndim != 1 || b->ndim != 1 || a->shape[0] != 3 || b->shape[0] != 3)
        vm_error(vm, "tensor.cross requires two 1D tensors of length 3");

    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(1, (int[]){3}, a->type));
    tensor_setFlat(result, 0, tensor_getFlat(a, 1) * tensor_getFlat(b, 2) - tensor_getFlat(a, 2) * tensor_getFlat(b, 1));
    tensor_setFlat(result, 1, tensor_getFlat(a, 2) * tensor_getFlat(b, 0) - tensor_getFlat(a, 0) * tensor_getFlat(b, 2));
    tensor_setFlat(result, 2, tensor_getFlat(a, 0) * tensor_getFlat(b, 1) - tensor_getFlat(a, 1) * tensor_getFlat(b, 0));
    return NEW_OBJ(result);
}

Value tn_squeeze(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.squeeze expects a tensor");

    PiTensor *src = AS_TENSOR(argv[0]);
    int new_shape[MAX_TENSOR_DIMS];
    int ndim = 0;

    for (int i = 0; i < src->ndim; i++)
    {
        if (src->shape[i] != 1)
            new_shape[ndim++] = src->shape[i];
    }

    if (ndim == 0)
    {
        if (src->size != 1)
            vm_error(vm, "squeeze resulted in invalid scalar tensor");
        return NEW_NUM(tensor_getFlat(src, 0));
    }

    if (ndim == src->ndim)
    {
        PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(src->ndim, src->shape, src->type));
        for (int i = 0; i < src->size; i++)
            tensor_setFlat(result, i, tensor_getFlat(src, i));
        return NEW_OBJ(result);
    }

    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(ndim, new_shape, src->type));
    for (int i = 0; i < src->size; i++)
        tensor_setFlat(result, i, tensor_getFlat(src, i));
    return NEW_OBJ(result);
}

Value tn_var(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.var expects a tensor");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    if (tensor->size < 2)
        vm_error(vm, "tensor.var needs at least 2 elements");
    double mean = AS_NUM(tn_mean(vm, 1, argv));
    double sum_sq = 0;
    for (int i = 0; i < tensor->size; i++)
    {
        double d = tensor_getFlat(tensor, i) - mean;
        sum_sq += d * d;
    }
    return NEW_NUM(sum_sq / (tensor->size - 1)); // sample variance
}

Value tn_std(vm_t *vm, int argc, Value *argv)
{
    double var = AS_NUM(tn_var(vm, argc, argv));
    return NEW_NUM(sqrt(var));
}

Value tn_median(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.median expects a tensor");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    if (tensor->size == 0)
        vm_error(vm, "empty tensor");
    double *arr = malloc(tensor->size * sizeof(double));
    for (int i = 0; i < tensor->size; i++)
        arr[i] = tensor_getFlat(tensor, i);
    qsort(arr, tensor->size, sizeof(double), double_cmp);
    double med = arr[tensor->size / 2];
    free(arr);
    return NEW_NUM(med);
}

Value tn_percentile(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_TENSOR(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "tensor.percentile expects a tensor and a percentile (0-100)");

    PiTensor *tensor = AS_TENSOR(argv[0]);
    double q = AS_NUM(argv[1]);
    if (q < 0.0 || q > 100.0)
        vm_error(vm, "percentile must be between 0 and 100");

    if (tensor->size == 0)
        vm_error(vm, "empty tensor");

    double *data = malloc(tensor->size * sizeof(double));
    for (int i = 0; i < tensor->size; i++)
        data[i] = tensor_getFlat(tensor, i);
    qsort(data, tensor->size, sizeof(double), double_cmp);

    double idx = (q / 100.0) * (tensor->size - 1);
    int lo = (int)floor(idx);
    int hi = (int)ceil(idx);
    double result = data[lo];
    if (lo != hi)
        result += (data[hi] - data[lo]) * (idx - lo);

    free(data);
    return NEW_NUM(result);
}

Value tn_mode(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.mode expects a tensor");

    PiTensor *tensor = AS_TENSOR(argv[0]);
    if (tensor->size == 0)
        vm_error(vm, "empty tensor");

    double *data = malloc(tensor->size * sizeof(double));
    for (int i = 0; i < tensor->size; i++)
        data[i] = tensor_getFlat(tensor, i);
    qsort(data, tensor->size, sizeof(double), double_cmp);

    double mode = data[0];
    int max_count = 1, cur_count = 1;
    for (int i = 1; i < tensor->size; i++)
    {
        if (data[i] == data[i - 1])
        {
            cur_count++;
        }
        else
        {
            if (cur_count > max_count)
            {
                max_count = cur_count;
                mode = data[i - 1];
            }
            cur_count = 1;
        }
    }
    if (cur_count > max_count)
        mode = data[tensor->size - 1];

    free(data);
    return NEW_NUM(mode);
}

Value tn_covariance(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_TENSOR(argv[0]) || !IS_TENSOR(argv[1]))
        vm_error(vm, "tensor.covariance expects two tensors");

    PiTensor *a = AS_TENSOR(argv[0]);
    PiTensor *b = AS_TENSOR(argv[1]);
    if (a->size != b->size)
        vm_error(vm, "covariance requires tensors of equal size");

    double mean_a = AS_NUM(tn_mean(vm, 1, &argv[0]));
    double mean_b = AS_NUM(tn_mean(vm, 1, &argv[1]));
    double sum = 0.0;
    for (int i = 0; i < a->size; i++)
    {
        double da = tensor_getFlat(a, i) - mean_a;
        double db = tensor_getFlat(b, i) - mean_b;
        sum += da * db;
    }
    return NEW_NUM(sum / (a->size - 1));
}

Value tn_correlation(vm_t *vm, int argc, Value *argv)
{
    if (argc < 2 || !IS_TENSOR(argv[0]) || !IS_TENSOR(argv[1]))
        vm_error(vm, "tensor.correlation expects two tensors");

    PiTensor *a = AS_TENSOR(argv[0]);
    PiTensor *b = AS_TENSOR(argv[1]);
    if (a->size != b->size)
        vm_error(vm, "correlation requires tensors of equal size");

    double var_a = AS_NUM(tn_var(vm, 1, &argv[0]));
    double var_b = AS_NUM(tn_var(vm, 1, &argv[1]));
    if (var_a == 0.0 || var_b == 0.0)
        return NEW_NUM(0.0);

    double cov = AS_NUM(tn_covariance(vm, 2, argv));
    return NEW_NUM(cov / sqrt(var_a * var_b));
}

Value tn_zscore(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.zscore expects a tensor");

    PiTensor *src = AS_TENSOR(argv[0]);
    double mean = AS_NUM(tn_mean(vm, 1, argv));
    double std = AS_NUM(tn_std(vm, 1, argv));
    if (std == 0.0)
        vm_error(vm, "zscore undefined for zero standard deviation");

    PiTensor *result = (PiTensor *)add_obj(vm, new_tensor(src->ndim, src->shape, src->type));
    for (int i = 0; i < src->size; i++)
        tensor_setFlat(result, i, (tensor_getFlat(src, i) - mean) / std);
    return NEW_OBJ(result);
}

Value tn_solve(vm_t *vm, int argc, Value *argv)
{
    vm_error(vm, "solve not implemented");
    return NEW_NUM(0);
}

Value tn_inv(vm_t *vm, int argc, Value *argv)
{
    vm_error(vm, "inv not implemented");
    return NEW_NUM(0);
}

Value tn_det(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "det expects a 2x2 tensor");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    if (tensor->ndim != 2 || tensor->shape[0] != 2 || tensor->shape[1] != 2)
        vm_error(vm, "det requires 2x2 matrix");
    double a = tensor_get(tensor, (int[]){0, 0});
    double b = tensor_get(tensor, (int[]){0, 1});
    double c = tensor_get(tensor, (int[]){1, 0});
    double d = tensor_get(tensor, (int[]){1, 1});
    return NEW_NUM(a * d - b * c);
}

Value tn_svd(vm_t *vm, int argc, Value *argv)
{
    vm_error(vm, "svd not implemented");
    return NEW_NUM(0);
}

Value tn_eig(vm_t *vm, int argc, Value *argv)
{
    vm_error(vm, "eig not implemented");
    return NEW_NUM(0);
}

Value tn_norm(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "norm expects a tensor");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    double sum_sq = 0;
    for (int i = 0; i < tensor->size; i++)
    {
        double val = tensor_getFlat(tensor, i);
        sum_sq += val * val;
    }
    return NEW_NUM(sqrt(sum_sq));
}

Value tn_rank(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "tensor.rank expects a matrix");

    PiTensor *A = AS_TENSOR(argv[0]);
    if (A->ndim != 2)
        vm_error(vm, "tensor.rank requires 2D tensor");

    int m = A->shape[0], n = A->shape[1];
    PiTensor *U, *Vt;
    // Use SVD for stable rank determination; fallback to Gaussian elimination.
    // Simplified: use row reduction
    double **mat = malloc(m * sizeof(double *));
    for (int i = 0; i < m; i++)
    {
        mat[i] = malloc(n * sizeof(double));
        for (int j = 0; j < n; j++)
            mat[i][j] = tensor_get(A, (int[]){i, j});
    }

    int rank = 0;
    int row = 0;
    for (int col = 0; col < n && row < m; col++)
    {
        // Find pivot
        int pivot = -1;
        for (int i = row; i < m; i++)
        {
            if (fabs(mat[i][col]) > 1e-12)
            {
                pivot = i;
                break;
            }
        }
        if (pivot == -1)
            continue;
        // Swap rows
        double *tmp = mat[row];
        mat[row] = mat[pivot];
        mat[pivot] = tmp;
        // Eliminate below
        for (int i = row + 1; i < m; i++)
        {
            double factor = mat[i][col] / mat[row][col];
            for (int j = col; j < n; j++)
                mat[i][j] -= factor * mat[row][j];
        }
        rank++;
        row++;
    }

    for (int i = 0; i < m; i++)
        free(mat[i]);
    free(mat);
    return NEW_NUM(rank);
}
Value tn_trace(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "trace expects a 2D tensor");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    if (tensor->ndim != 2 || tensor->shape[0] != tensor->shape[1])
        vm_error(vm, "trace requires square matrix");
    double trace = 0;
    for (int i = 0; i < tensor->shape[0]; i++)
        trace += tensor_get(tensor, (int[]){i, i});
    return NEW_NUM(trace);
}

Value tn_pinv(vm_t *vm, int argc, Value *argv)
{
    vm_error(vm, "pinv not implemented");
    return NEW_NUM(0);
}

Value tn_shuffle(vm_t *vm, int argc, Value *argv)
{
    if (argc < 1 || !IS_TENSOR(argv[0]))
        vm_error(vm, "shuffle expects a tensor");
    PiTensor *tensor = AS_TENSOR(argv[0]);
    // Fisher-Yates shuffle
    for (int i = tensor->size - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        double temp = tensor_getFlat(tensor, i);
        tensor_setFlat(tensor, i, tensor_getFlat(tensor, j));
        tensor_setFlat(tensor, j, temp);
    }
    return NEW_OBJ(tensor);
}

static BuiltinFunc tensor_funcs[] = {
    {"zeros", tn_zeros},
    {"ones", tn_ones},
    {"eye", tn_eye},
    {"rand", tn_rand},
    {"randn", tn_randn},
    {"randint", tn_randint},
    {"from", tn_from},
    {"fill", tn_fill},
    {"shape", tn_shape},
    {"ndim", tn_ndim},
    {"size", tn_size},
    {"reshape", tn_reshape},
    {"slice", tn_slice},
    {"concat", tn_concat},
    {"transpose", tn_transpose},
    {"flatten", tn_flatten},
    {"expand_dims", tn_expand_dims},
    {"squeeze", tn_squeeze},
    {"is_tensor", tn_isTensor},
    {"is_matrix", tn_isMatrix},
    {"is_vector", tn_isVector},
    {"is_scalar", tn_isScalar},
    {"add", tn_add},
    {"sub", tn_sub},
    {"mult", tn_mult},
    {"div", tn_div},
    {"exp", tn_exp},
    {"log", tn_log},
    {"sqrt", tn_sqrt},
    {"abs", tn_abs},
    {"clip", tn_clip},
    {"sign", tn_sign},
    {"apply", tn_apply},
    {"matmult", tn_matmult},
    {"dot", tn_dot},
    {"cross", tn_cross},
    {"solve", tn_solve},
    {"inv", tn_inv},
    {"det", tn_det},
    {"svd", tn_svd},
    {"eig", tn_eig},
    {"norm", tn_norm},
    {"rank", tn_rank},
    {"trace", tn_trace},
    {"pinv", tn_pinv},
    {"sum", tn_sum},
    {"mean", tn_mean},
    {"min", tn_min},
    {"max", tn_max},
    {"prod", tn_prod},
    {"argmax", tn_argmax},
    {"argmin", tn_argmin},
    {"any", tn_any},
    {"all", tn_all},
    {"reduce", tn_reduce},
    {"var", tn_var},
    {"std", tn_std},
    {"median", tn_median},
    {"percentile", tn_percentile},
    {"mode", tn_mode},
    {"covariance", tn_covariance},
    {"correlation", tn_correlation},
    {"zscore", tn_zscore},
    {"shuffle", tn_shuffle},
};

DEFINE_BUILTIN_MODULE(module_tensor, "tensor", tensor_funcs, NULL);
