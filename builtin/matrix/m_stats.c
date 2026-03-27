#include "m_stats.h"
#include "../pi_builtin.h"
#include "../../common.h"

#include <stdlib.h>
#include <string.h>

// Internal Helpers

// Extract a column from a MatrixView into a flat double array
static void extract_col(MatrixView *view, int col, double *out)
{
    for (int r = 0; r < view->rows; r++)
        out[r] = get_matrixView(view, r, col);
}

// Extract a row from a MatrixView into a flat double array
static void extract_row(MatrixView *view, int row, double *out)
{
    for (int c = 0; c < view->cols; c++)
        out[c] = get_matrixView(view, row, c);
}

// Compute mean of a flat double array
static double array_mean(double *arr, int n)
{
    // Welford's online algorithm — numerically stable
    double mean = 0.0;
    for (int i = 0; i < n; i++)
        mean += (arr[i] - mean) / (i + 1);
    return mean;
}

// Compute variance of a flat double array (given precomputed mean)
// ddof: 0 = population variance, 1 = sample variance (Bessel's correction)
static double array_var(double *arr, int n, double mean, int ddof)
{
    if (n - ddof <= 0)
        return NAN;
    double sum = 0.0;
    for (int i = 0; i < n; i++)
    {
        double d = arr[i] - mean;
        sum += d * d;
    }
    return sum / (n - ddof);
}

// Comparison function for qsort
static int double_cmp(const void *a, const void *b)
{
    double x = *(double *)a;
    double y = *(double *)b;
    return (x > y) - (x < y);
}

// Sort a copy of arr, return sorted copy (caller must free)
static double *array_sorted(double *arr, int n)
{
    double *sorted = (double *)malloc(n * sizeof(double));
    memcpy(sorted, arr, n * sizeof(double));
    qsort(sorted, n, sizeof(double), double_cmp);
    return sorted;
}

// Box-Muller transform — generates two independent standard normal samples
static void box_muller(double *z0, double *z1)
{
    double u1, u2;
    // Avoid log(0) by rejecting u1 == 0
    do
    {
        u1 = rand() / (double)RAND_MAX;
    } while (u1 == 0.0);
    u2 = rand() / (double)RAND_MAX;

    double mag = sqrt(-2.0 * log(u1));
    *z0 = mag * cos(2.0 * PI * u2);
    *z1 = mag * sin(2.0 * PI * u2);
}

// Parse (matrix) or (matrix, axis) arguments
// axis defaults to -1 (full reduction)
static bool parse_matrix_axis(vm_t *vm, int argc, Value *argv,
                              MatrixView *view, int *axis, const char *fname)
{
    if (argc < 1 || argc > 2 || !matrix_view(argv[0], view))
    {
        vm_errorf(vm, "%s: Expected a matrix and optional axis.", fname);
        return false;
    }
    *axis = -1;
    if (argc == 2)
    {
        if (!IS_NUM(argv[1]))
        {
            vm_errorf(vm, "%s: axis must be a number.", fname);
            return false;
        }
        *axis = (int)AS_NUM(argv[1]);
    }
    return true;
}

// Axis-Aware Variance Engine

// ddof = 0: population, 1: sample
static Value compute_var(vm_t *vm, int argc, Value *argv, int ddof, const char *fname)
{
    MatrixView view;
    int axis;
    parse_matrix_axis(vm, argc, argv, &view, &axis, fname);

    if (axis == -1)
    {
        int n = view.rows * view.cols;
        if (n == 0)
            return NEW_NUM(NAN);

        double *arr = (double *)malloc(n * sizeof(double));
        for (int r = 0; r < view.rows; r++)
            for (int c = 0; c < view.cols; c++)
                arr[r * view.cols + c] = get_matrixView(&view, r, c);

        double mean = array_mean(arr, n);
        double var = array_var(arr, n, mean, ddof);
        free(arr);
        return NEW_NUM(var);
    }

    if (axis == 0) // variance of each column -> 1 × cols
    {
        PiMatrix *result = create_denseMatrix(1, view.cols);
        double *col_buf = (double *)malloc(view.rows * sizeof(double));

        for (int c = 0; c < view.cols; c++)
        {
            extract_col(&view, c, col_buf);
            double mean = array_mean(col_buf, view.rows);
            matrix_set(result, 0, c, array_var(col_buf, view.rows, mean, ddof));
        }
        free(col_buf);
        return NEW_OBJ(result);
    }

    if (axis == 1) // variance of each row -> rows × 1
    {
        PiMatrix *result = create_denseMatrix(view.rows, 1);
        double *row_buf = (double *)malloc(view.cols * sizeof(double));

        for (int r = 0; r < view.rows; r++)
        {
            extract_row(&view, r, row_buf);
            double mean = array_mean(row_buf, view.cols);
            matrix_set(result, r, 0, array_var(row_buf, view.cols, mean, ddof));
        }
        free(row_buf);
        return NEW_OBJ(result);
    }

    vm_errorf(vm, "%s: axis must be -1, 0, or 1.", fname);
    return NEW_NUM(NAN); // unreachable
}

// Descriptive Statistics

Value mat_var(vm_t *vm, int argc, Value *argv)
{
    // Optional third argument: ddof (default 1 = sample variance)
    int ddof = 1;
    if (argc == 3 && IS_NUM(argv[2]))
        ddof = (int)AS_NUM(argv[2]);
    return compute_var(vm, argc < 3 ? argc : 2, argv, ddof, "var");
}

Value mat_std(vm_t *vm, int argc, Value *argv)
{
    int ddof = 1;
    if (argc == 3 && IS_NUM(argv[2]))
        ddof = (int)AS_NUM(argv[2]);

    Value var = compute_var(vm, argc < 3 ? argc : 2, argv, ddof, "std");

    // If result is a scalar, just sqrt it
    if (IS_NUM(var))
        return NEW_NUM(sqrt(AS_NUM(var)));

    // If result is a matrix, sqrt element-wise
    MatrixView view;
    matrix_view(var, &view);
    PiMatrix *result = create_denseMatrix(view.rows, view.cols);
    for (int r = 0; r < view.rows; r++)
        for (int c = 0; c < view.cols; c++)
            matrix_set(result, r, c, sqrt(get_matrixView(&view, r, c)));
    return NEW_OBJ(result);
}

Value mat_median(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;
    int axis;
    parse_matrix_axis(vm, argc, argv, &view, &axis, "median");

    if (axis == -1)
    {
        int n = view.rows * view.cols;
        if (n == 0)
            return NEW_NUM(NAN);

        double *arr = (double *)malloc(n * sizeof(double));
        for (int r = 0; r < view.rows; r++)
            for (int c = 0; c < view.cols; c++)
                arr[r * view.cols + c] = get_matrixView(&view, r, c);

        double *sorted = array_sorted(arr, n);
        double median = (n % 2 == 0)
                            ? (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0
                            : sorted[n / 2];
        free(arr);
        free(sorted);
        return NEW_NUM(median);
    }

    if (axis == 0) // median of each column -> 1 × cols
    {
        PiMatrix *result = create_denseMatrix(1, view.cols);
        double *buf = (double *)malloc(view.rows * sizeof(double));

        for (int c = 0; c < view.cols; c++)
        {
            extract_col(&view, c, buf);
            double *sorted = array_sorted(buf, view.rows);
            int n = view.rows;
            double median = (n % 2 == 0)
                                ? (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0
                                : sorted[n / 2];
            matrix_set(result, 0, c, median);
            free(sorted);
        }
        free(buf);
        return NEW_OBJ(result);
    }

    if (axis == 1) // median of each row -> rows × 1
    {
        PiMatrix *result = create_denseMatrix(view.rows, 1);
        double *buf = (double *)malloc(view.cols * sizeof(double));

        for (int r = 0; r < view.rows; r++)
        {
            extract_row(&view, r, buf);
            double *sorted = array_sorted(buf, view.cols);
            int n = view.cols;
            double median = (n % 2 == 0)
                                ? (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0
                                : sorted[n / 2];
            matrix_set(result, r, 0, median);
            free(sorted);
        }
        free(buf);
        return NEW_OBJ(result);
    }

    vm_error(vm, "median: axis must be -1, 0, or 1.");
    return NEW_NUM(NAN);
}

Value mat_percentile(vm_t *vm, int argc, Value *argv)
{
    // Signature: percentile(matrix, p)  or  percentile(matrix, p, axis)
    // p is in [0, 100]. Uses linear interpolation (same as numpy default).
    MatrixView view;

    if (argc < 2 || argc > 3 || !matrix_view(argv[0], &view) || !IS_NUM(argv[1]))
        vm_error(vm, "percentile: Expected (matrix, p) or (matrix, p, axis).");

    double p = AS_NUM(argv[1]);
    if (p < 0.0 || p > 100.0)
        vm_error(vm, "percentile: p must be in [0, 100].");

    int axis = -1;
    if (argc == 3)
    {
        if (!IS_NUM(argv[2]))
            vm_error(vm, "percentile: axis must be a number.");
        axis = (int)AS_NUM(argv[2]);
    }

// Linear interpolation helper
#define PERCENTILE_OF(arr, n, p_frac)                               \
    ({                                                              \
        double *_s = array_sorted(arr, n);                          \
        double _idx = (p_frac) * ((n) - 1);                         \
        int _lo = (int)_idx;                                        \
        int _hi = _lo + 1 < (n) ? _lo + 1 : _lo;                    \
        double _res = _s[_lo] + (_idx - _lo) * (_s[_hi] - _s[_lo]); \
        free(_s);                                                   \
        _res;                                                       \
    })

    double frac = p / 100.0;

    if (axis == -1)
    {
        int n = view.rows * view.cols;
        double *arr = (double *)malloc(n * sizeof(double));
        for (int r = 0; r < view.rows; r++)
            for (int c = 0; c < view.cols; c++)
                arr[r * view.cols + c] = get_matrixView(&view, r, c);
        double result = PERCENTILE_OF(arr, n, frac);
        free(arr);
        return NEW_NUM(result);
    }

    if (axis == 0)
    {
        PiMatrix *result = create_denseMatrix(1, view.cols);
        double *buf = (double *)malloc(view.rows * sizeof(double));
        for (int c = 0; c < view.cols; c++)
        {
            extract_col(&view, c, buf);
            matrix_set(result, 0, c, PERCENTILE_OF(buf, view.rows, frac));
        }
        free(buf);
        return NEW_OBJ(result);
    }

    if (axis == 1)
    {
        PiMatrix *result = create_denseMatrix(view.rows, 1);
        double *buf = (double *)malloc(view.cols * sizeof(double));
        for (int r = 0; r < view.rows; r++)
        {
            extract_row(&view, r, buf);
            matrix_set(result, r, 0, PERCENTILE_OF(buf, view.cols, frac));
        }
        free(buf);
        return NEW_OBJ(result);
    }

#undef PERCENTILE_OF

    vm_error(vm, "percentile: axis must be -1, 0, or 1.");
    return NEW_NUM(NAN);
}

Value mat_mode(vm_t *vm, int argc, Value *argv)
{
    // Mode via sort — O(n log n), returns smallest value on tie
    // Only works on flat reduction (axis = -1) — mode per row/col is rarely useful
    MatrixView view;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "mode: Expected a matrix.");

    int n = view.rows * view.cols;
    if (n == 0)
        return NEW_NUM(NAN);

    double *arr = (double *)malloc(n * sizeof(double));
    for (int r = 0; r < view.rows; r++)
        for (int c = 0; c < view.cols; c++)
            arr[r * view.cols + c] = get_matrixView(&view, r, c);

    double *sorted = array_sorted(arr, n);

    double mode_val = sorted[0];
    int mode_cnt = 1;
    double cur_val = sorted[0];
    int cur_cnt = 1;

    for (int i = 1; i < n; i++)
    {
        if (sorted[i] == cur_val)
        {
            cur_cnt++;
            if (cur_cnt > mode_cnt)
            {
                mode_cnt = cur_cnt;
                mode_val = cur_val;
            }
        }
        else
        {
            cur_val = sorted[i];
            cur_cnt = 1;
        }
    }

    free(arr);
    free(sorted);
    return NEW_NUM(mode_val);
}

// Matrix Statistics 

Value mat_covariance(vm_t *vm, int argc, Value *argv)
{
    // Input: m × n matrix where rows = observations, cols = variables
    // Output: n × n covariance matrix (sample covariance, ddof=1)
    MatrixView view;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "covariance: Expected a matrix (rows=observations, cols=variables).");

    if (view.rows < 2)
        vm_error(vm, "covariance: Need at least 2 observations.");

    int m = view.rows; // observations
    int n = view.cols; // variables

    // Compute column means
    double *means = (double *)calloc(n, sizeof(double));
    for (int c = 0; c < n; c++)
        for (int r = 0; r < m; r++)
            means[c] += get_matrixView(&view, r, c);
    for (int c = 0; c < n; c++)
        means[c] /= m;

    // Cov[i,j] = (1/(m-1)) * sum_r (X[r,i] - mean[i]) * (X[r,j] - mean[j])
    PiMatrix *result = create_denseMatrix(n, n);

    for (int i = 0; i < n; i++)
        for (int j = i; j < n; j++) // exploit symmetry
        {
            double sum = 0.0;
            for (int r = 0; r < m; r++)
                sum += (get_matrixView(&view, r, i) - means[i]) * (get_matrixView(&view, r, j) - means[j]);
            double cov = sum / (m - 1);
            matrix_set(result, i, j, cov);
            matrix_set(result, j, i, cov); // symmetric
        }

    free(means);
    return NEW_OBJ(result);
}

Value mat_correlation(vm_t *vm, int argc, Value *argv)
{
    // Pearson correlation matrix — cov(X) normalized by std devs
    // corr[i,j] = cov[i,j] / (std[i] * std[j])
    MatrixView view;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "correlation: Expected a matrix (rows=observations, cols=variables).");

    if (view.rows < 2)
        vm_error(vm, "correlation: Need at least 2 observations.");

    // Reuse covariance
    Value cov_val = mat_covariance(vm, argc, argv);
    MatrixView cov_view;
    matrix_view(cov_val, &cov_view);

    int n = cov_view.rows;
    PiMatrix *result = create_denseMatrix(n, n);

    // std[i] = sqrt(cov[i,i])
    double *stds = (double *)malloc(n * sizeof(double));
    for (int i = 0; i < n; i++)
        stds[i] = sqrt(get_matrixView(&cov_view, i, i));

    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
        {
            double denom = stds[i] * stds[j];
            double corr = (denom < 1e-14) ? 0.0
                                          : get_matrixView(&cov_view, i, j) / denom;
            matrix_set(result, i, j, corr);
        }

    free(stds);
    return NEW_OBJ(result);
}

Value mat_zscore(vm_t *vm, int argc, Value *argv)
{
    // Standardize columns: z = (X - mean) / std, axis=0 by default
    // Each column becomes mean=0, std=1
    MatrixView view;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "zscore: Expected a matrix.");

    int m = view.rows;
    int n = view.cols;

    PiMatrix *result = create_denseMatrix(m, n);
    double *buf = (double *)malloc(m * sizeof(double));

    for (int c = 0; c < n; c++)
    {
        extract_col(&view, c, buf);
        double mean = array_mean(buf, m);
        double var = array_var(buf, m, mean, 1); // sample std
        double std = sqrt(var);

        for (int r = 0; r < m; r++)
        {
            double z = (std < 1e-14) ? 0.0 : (buf[r] - mean) / std;
            matrix_set(result, r, c, z);
        }
    }

    free(buf);
    return NEW_OBJ(result);
}

// Distributions

Value mat_randn(vm_t *vm, int argc, Value *argv)
{
    // randn(rows, cols) — standard normal N(0,1) via Box-Muller
    if (argc != 2 || !IS_NUM(argv[0]) || !IS_NUM(argv[1]))
        vm_error(vm, "randn: Expected two numbers (rows, cols).");

    int rows = (int)AS_NUM(argv[0]);
    int cols = (int)AS_NUM(argv[1]);

    if (rows <= 0 || cols <= 0)
        vm_error(vm, "randn: rows and cols must be positive.");

    PiMatrix *result = create_denseMatrix(rows, cols);
    int total = rows * cols;
    int i = 0;

    while (i < total)
    {
        double z0, z1;
        box_muller(&z0, &z1);
        result->data[i++] = z0;
        if (i < total)
            result->data[i++] = z1;
    }

    return NEW_OBJ(result);
}

Value mat_randint(vm_t *vm, int argc, Value *argv)
{
    // randint(rows, cols, low, high) — uniform integers in [low, high)
    if (argc != 4 || !IS_NUM(argv[0]) || !IS_NUM(argv[1]) || !IS_NUM(argv[2]) || !IS_NUM(argv[3]))
        vm_error(vm, "randint: Expected (rows, cols, low, high).");

    int rows = (int)AS_NUM(argv[0]);
    int cols = (int)AS_NUM(argv[1]);
    int low = (int)AS_NUM(argv[2]);
    int high = (int)AS_NUM(argv[3]);

    if (rows <= 0 || cols <= 0)
        vm_error(vm, "randint: rows and cols must be positive.");

    if (high <= low)
        vm_error(vm, "randint: high must be greater than low.");

    PiMatrix *result = create_denseMatrix(rows, cols);
    int range = high - low;
    int total = rows * cols;

    for (int i = 0; i < total; i++)
        result->data[i] = (double)(low + rand() % range);

    return NEW_OBJ(result);
}

Value mat_shuffle(vm_t *vm, int argc, Value *argv)
{
    // Fisher-Yates shuffle — shuffles rows of a matrix in-place
    // Returns a new shuffled matrix (does not mutate input)
    MatrixView view;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "shuffle: Expected a matrix.");

    int m = view.rows;
    int n = view.cols;

    PiMatrix *result = create_denseMatrix(m, n);

    // Copy into result first
    for (int r = 0; r < m; r++)
        for (int c = 0; c < n; c++)
            matrix_set(result, r, c, get_matrixView(&view, r, c));

    // Fisher-Yates: walk backwards, swap row i with random row in [0, i]
    double *tmp = (double *)malloc(n * sizeof(double));

    for (int i = m - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        if (i == j)
            continue;

        // Swap rows i and j
        for (int c = 0; c < n; c++)
            tmp[c] = matrix_get(result, i, c);
        for (int c = 0; c < n; c++)
            matrix_set(result, i, c, matrix_get(result, j, c));
        for (int c = 0; c < n; c++)
            matrix_set(result, j, c, tmp[c]);
    }

    free(tmp);
    return NEW_OBJ(result);
}

// Module Registration

static BuiltinFunc stats_funcs[] = {
    {"var", mat_var},
    {"std", mat_std},
    {"median", mat_median},
    {"percentile", mat_percentile},
    {"mode", mat_mode},
    {"covariance", mat_covariance},
    {"correlation", mat_correlation},
    {"zscore", mat_zscore},
    {"randn", mat_randn},
    {"randint", mat_randint},
    {"shuffle", mat_shuffle},
};

DEFINE_BUILTIN_MODULE(module_matStats, "matrix.stats", stats_funcs, NULL);