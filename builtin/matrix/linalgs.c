#include "linalgs.h"
#include "../pi_builtin.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Helper Methods

// Copy a MatrixView into a fresh dense matrix (always work on a copy)
static PiMatrix *view_to_dense(MatrixView *view)
{
    PiMatrix *result = create_denseMatrix(view->rows, view->cols);
    for (int r = 0; r < view->rows; r++)
        for (int c = 0; c < view->cols; c++)
            matrix_set(result, r, c, get_matrixView(view, r, c));
    return result;
}

// Swap two rows in a dense matrix
static void swap_rows(PiMatrix *m, int r1, int r2)
{
    for (int c = 0; c < m->cols; c++)
    {
        double tmp = matrix_get(m, r1, c);
        matrix_set(m, r1, c, matrix_get(m, r2, c));
        matrix_set(m, r2, c, tmp);
    }
}

// Pack multiple PiMatrix pointers into a language-level list of matrices
static Value pack_matrices(PiMatrix **matrices, int count)
{
    list_t *items = list_create(sizeof(Value));
    for (int i = 0; i < count; i++)
    {
        Value v = NEW_OBJ(matrices[i]);
        list_add(items, &v);
    }
    PiList *result = (PiList *)new_list(items);
    result->is_numeric = false;
    result->is_matrix = false;
    result->rows = 1;
    result->cols = count;
    return NEW_OBJ(result);
}

// LU Decomposition (core — used by solve, inv, det)
//
// Computes PA = LU with partial pivoting.
// L and U are stored in a single n×n matrix (L's diagonal is implicitly 1).
// perm[i] = which original row is now at row i.
// Returns the number of row swaps (for det sign).

typedef struct
{
    PiMatrix *LU; // combined storage: L below diagonal, U on and above
    int *perm;    // permutation vector, length n
    int swaps;    // number of row swaps performed
    bool singular;
} LUResult;

static LUResult lu_decompose(PiMatrix *A)
{
    int n = A->rows;
    LUResult res;
    res.LU = create_denseMatrix(n, n);
    res.perm = (int *)malloc(n * sizeof(int));
    res.swaps = 0;
    res.singular = false;

    // Copy A into LU
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            matrix_set(res.LU, r, c, matrix_get(A, r, c));

    for (int i = 0; i < n; i++)
        res.perm[i] = i;

    for (int col = 0; col < n; col++)
    {
        // Find pivot — largest absolute value in this column at or below diagonal
        int pivot_row = col;
        double pivot_val = fabs(matrix_get(res.LU, col, col));

        for (int r = col + 1; r < n; r++)
        {
            double v = fabs(matrix_get(res.LU, r, col));
            if (v > pivot_val)
            {
                pivot_val = v;
                pivot_row = r;
            }
        }

        if (pivot_val < 1e-12)
        {
            res.singular = true;
            return res;
        }

        if (pivot_row != col)
        {
            swap_rows(res.LU, col, pivot_row);
            int tmp = res.perm[col];
            res.perm[col] = res.perm[pivot_row];
            res.perm[pivot_row] = tmp;
            res.swaps++;
        }

        double diag = matrix_get(res.LU, col, col);

        for (int r = col + 1; r < n; r++)
        {
            double factor = matrix_get(res.LU, r, col) / diag;
            matrix_set(res.LU, r, col, factor); // store L factor below diagonal

            for (int c = col + 1; c < n; c++)
            {
                double updated = matrix_get(res.LU, r, c) - factor * matrix_get(res.LU, col, c);
                matrix_set(res.LU, r, c, updated);
            }
        }
    }

    return res;
}

// Forward substitution: solve Ly = b (L has implicit 1s on diagonal)
static void forward_sub(PiMatrix *LU, double *b, double *y, int n)
{
    for (int i = 0; i < n; i++)
    {
        double sum = b[i];
        for (int j = 0; j < i; j++)
            sum -= matrix_get(LU, i, j) * y[j];
        y[i] = sum; // diagonal of L is 1
    }
}

// Back substitution: solve Ux = y
static void back_sub(PiMatrix *LU, double *y, double *x, int n)
{
    for (int i = n - 1; i >= 0; i--)
    {
        double sum = y[i];
        for (int j = i + 1; j < n; j++)
            sum -= matrix_get(LU, i, j) * x[j];
        x[i] = sum / matrix_get(LU, i, i);
    }
}

// Solvers

/**
 * Solve a system of linear equations using LU decomposition.
 * @param A A square matrix representing the left-hand side of the system.
 * @param b A column vector representing the right-hand side of the system.
 * @return A column vector representing the solution to the system.
 */
Value mat_solve(vm_t *vm, int argc, Value *argv)
{
    MatrixView A_view, b_view;

    if (argc != 2 || !matrix_view(argv[0], &A_view) || !matrix_view(argv[1], &b_view))
        vm_error(vm, "solve: Expected a square matrix A and vector b.");

    if (A_view.rows != A_view.cols)
        vm_error(vm, "solve: A must be square.");

    if (b_view.rows != A_view.rows && b_view.cols != 1 && b_view.rows != 1)
        vm_error(vm, "solve: b dimensions do not match A.");

    int n = A_view.rows;
    PiMatrix *A_dense = view_to_dense(&A_view);
    LUResult lu = lu_decompose(A_dense);

    if (lu.singular)
        vm_error(vm, "solve: Matrix is singular.");

    // Build permuted RHS
    double *pb = (double *)malloc(n * sizeof(double));
    double *y = (double *)malloc(n * sizeof(double));
    double *x = (double *)malloc(n * sizeof(double));

    for (int i = 0; i < n; i++)
        pb[i] = get_matrixView(&b_view, lu.perm[i], 0);

    // Forward substitution: Ly = b
    forward_sub(lu.LU, pb, y, n);

    // Back substitution: Ux = y
    back_sub(lu.LU, y, x, n);

    // Return as column vector (n × 1 matrix)
    PiMatrix *result = create_denseMatrix(n, 1);
    for (int i = 0; i < n; i++)
        matrix_set(result, i, 0, x[i]);

    free(pb);
    free(y);
    free(x);
    free(lu.perm);
    return NEW_OBJ(result);
}

/**
 * Compute the determinant of a square matrix.
 * @param A A square matrix.
 * @return The determinant of A.
 */
Value mat_det(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "det: Expected a square matrix.");

    if (view.rows != view.cols)
        vm_error(vm, "det: Matrix must be square.");

    PiMatrix *A_dense = view_to_dense(&view);
    LUResult lu = lu_decompose(A_dense);

    if (lu.singular)
        return NEW_NUM(0.0);

    // det(A) = det(U) * (-1)^swaps
    // det(U) = product of its diagonal
    double d = (lu.swaps % 2 == 0) ? 1.0 : -1.0;
    for (int i = 0; i < view.rows; i++)
        d *= matrix_get(lu.LU, i, i);

    free(lu.perm);
    return NEW_NUM(d);
}

/**
 * Compute the inverse of a square matrix.
 * @param A A square matrix.
 * @return The inverse of A.
 */
Value mat_inv(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;

    // Check the number of arguments and that the first argument is a
    // square matrix.
    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "inv: Expected a square matrix.");

    if (view.rows != view.cols)
        vm_error(vm, "inv: Matrix must be square.");

    int n = view.rows;
    PiMatrix *A_dense = view_to_dense(&view);
    LUResult lu = lu_decompose(A_dense);

    // Check if the matrix is singular.
    if (lu.singular)
        vm_error(vm, "inv: Matrix is singular.");

    // Solve A * x = e_i for each standard basis vector e_i
    // Each solution x is the i-th column of A^-1
    PiMatrix *result = create_denseMatrix(n, n);
    double *pb = (double *)malloc(n * sizeof(double));
    double *y = (double *)malloc(n * sizeof(double));
    double *x = (double *)malloc(n * sizeof(double));

    for (int col = 0; col < n; col++)
    {
        // Initialize the right-hand side vector (pb) with the standard
        // basis vector e_i.
        for (int i = 0; i < n; i++)
            pb[i] = (lu.perm[i] == col) ? 1.0 : 0.0;

        // Solve the system of linear equations A * x = pb using
        // forward and backward substitution.
        forward_sub(lu.LU, pb, y, n);
        back_sub(lu.LU, y, x, n);

        // Copy the solution vector x into the result matrix.
        for (int r = 0; r < n; r++)
            matrix_set(result, r, col, x[r]);
    }

    free(pb);
    free(y);
    free(x);
    free(lu.perm);
    return NEW_OBJ(result);
}

// Decompositions

/**
 * Compute the LU decomposition of a square matrix.
 * @param A A square matrix.
 * @return An object containing three matrices: L, U, and P, such that A = P * L * U.
 */
Value mat_lu(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "lu: Expected a square matrix.");

    if (view.rows != view.cols)
        vm_error(vm, "lu: Matrix must be square.");

    int n = view.rows;
    PiMatrix *A_dense = view_to_dense(&view);
    LUResult lu = lu_decompose(A_dense);

    if (lu.singular)
        vm_error(vm, "lu: Matrix is singular.");

    // Extract L, U, and P as separate matrices
    PiMatrix *L = create_denseMatrix(n, n);
    PiMatrix *U = create_denseMatrix(n, n);
    PiMatrix *P = create_denseMatrix(n, n);

    // L is the lower triangular matrix, U is the upper triangular
    // matrix, and P is the permutation matrix such that A = P * L * U
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
        {
            if (r == c)
            {
                matrix_set(L, r, c, 1.0); // implicit diagonal of L
                matrix_set(U, r, c, matrix_get(lu.LU, r, c));
            }
            else if (r > c)
                matrix_set(L, r, c, matrix_get(lu.LU, r, c));
            else
                matrix_set(U, r, c, matrix_get(lu.LU, r, c));
        }

    // P is the permutation matrix
    for (int r = 0; r < n; r++)
        matrix_set(P, r, lu.perm[r], 1.0);

    free(lu.perm);

    PiMatrix *parts[3] = {L, U, P};
    return pack_matrices(parts, 3); // returns [L, U, P]
}

/**
 * Compute the Cholesky decomposition of a square matrix.
 * @param A A square matrix that is symmetric positive definite.
 * @return An object containing the lower triangular matrix L such that A = L * L'.
 */
Value mat_cholesky(vm_t *vm, int argc, Value *argv)
{
    // Cholesky–Banachiewicz algorithm
    // Requires A to be symmetric positive definite
    // Returns L such that A = L * L'
    MatrixView view;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "cholesky: Expected a square matrix.");

    if (view.rows != view.cols)
        vm_error(vm, "cholesky: Matrix must be square.");

    int n = view.rows;
    PiMatrix *L = create_denseMatrix(n, n);

    // Compute L using the Cholesky–Banachiewicz algorithm
    for (int r = 0; r < n; r++)
    {
        for (int c = 0; c <= r; c++)
        {
            // Compute the value at position (r, c)
            double sum = get_matrixView(&view, r, c);

            // Subtract the values of the previously computed elements
            for (int k = 0; k < c; k++)
                sum -= matrix_get(L, r, k) * matrix_get(L, c, k);

            if (r == c)
            {
                // Make sure the matrix is positive definite
                if (sum <= 0.0)
                    vm_error(vm, "cholesky: Matrix is not positive definite.");
                matrix_set(L, r, c, sqrt(sum));
            }
            else
                matrix_set(L, r, c, sum / matrix_get(L, c, c));
        }
    }

    return NEW_OBJ(L);
}

/**
 * @brief Householder QR decomposition — more numerically stable than Gram-Schmidt
 * @param vm The virtual machine
 * @param argc The number of arguments
 * @param argv The arguments
 * @return A Value containing the result of the QR decomposition
 */
Value mat_qr(vm_t *vm, int argc, Value *argv)
{
    // Works on any m×n matrix where m >= n
    MatrixView view;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "qr: Expected a matrix (m >= n).");

    if (view.rows < view.cols)
        vm_error(vm, "qr: Matrix must have at least as many rows as columns.");

    int m = view.rows;
    int n = view.cols;

    // R starts as a copy of A, Q starts as identity
    PiMatrix *R = view_to_dense(&view);
    PiMatrix *Q = create_denseMatrix(m, m);
    for (int i = 0; i < m; i++)
        matrix_set(Q, i, i, 1.0);

    double *v = (double *)malloc(m * sizeof(double));

    for (int col = 0; col < n; col++)
    {
        // Build Householder vector v for this column
        double norm_sq = 0.0;
        for (int r = col; r < m; r++)
        {
            v[r] = matrix_get(R, r, col);
            norm_sq += v[r] * v[r];
        }

        double norm = sqrt(norm_sq);
        // Add sign to avoid cancellation
        v[col] += (v[col] >= 0.0) ? norm : -norm;

        // Recompute norm of v after modification
        norm_sq = 0.0;
        for (int r = col; r < m; r++)
            norm_sq += v[r] * v[r];

        if (norm_sq < 1e-14)
            continue; // already zero, skip

        // Apply H = I - (2/||v||^2) * v * v' to R from the left
        for (int c = col; c < n; c++)
        {
            double dot = 0.0;
            for (int r = col; r < m; r++)
                dot += v[r] * matrix_get(R, r, c);
            dot *= 2.0 / norm_sq;
            for (int r = col; r < m; r++)
                matrix_set(R, r, c, matrix_get(R, r, c) - dot * v[r]);
        }

        // Apply H to Q from the right: Q = Q * H'  (H is symmetric so H' = H)
        for (int r = 0; r < m; r++)
        {
            double dot = 0.0;
            for (int k = col; k < m; k++)
                dot += matrix_get(Q, r, k) * v[k];
            dot *= 2.0 / norm_sq;
            for (int k = col; k < m; k++)
                matrix_set(Q, r, k, matrix_get(Q, r, k) - dot * v[k]);
        }
    }

    free(v);

    PiMatrix *parts[2] = {Q, R};
    return pack_matrices(parts, 2); // returns [Q, R]
}

/**
 * @brief Compute the singular value decomposition (SVD) of a matrix.
 *
 * @note The Golub-Reinsch SVD algorithm is used, which is a variant of the
 * QR algorithm that is more efficient for computing the SVD of a matrix.
 *
 * @param vm The virtual machine to use for execution.
 * @param argc The number of arguments passed to the function.
 * @param argv The arguments passed to the function.
 *
 * @return A value containing the U, S, and V matrices of the SVD.
 */
Value mat_svd(vm_t *vm, int argc, Value *argv)
{
    // Golub-Reinsch SVD via bidiagonalization + QR iteration
    // Returns [U, S, V] where A = U * diag(S) * V'
    // S is returned as a column vector of singular values
    MatrixView view;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "svd: Expected a matrix.");

    int m = view.rows;
    int n = view.cols;
    int k = m < n ? m : n; // number of singular values

    // We use the Jacobi one-sided SVD for clarity and correctness.
    // Works well for small-to-medium matrices (up to ~500x500).
    // For m >= n: compute SVD of A directly.
    // For m < n:  compute SVD of A', then swap U and V.

    bool transposed = (m < n);
    PiMatrix *work;

    if (transposed)
    {
        // Transpose A so we always work with tall/square matrices
        work = create_denseMatrix(n, m);
        for (int r = 0; r < m; r++)
            for (int c = 0; c < n; c++)
                matrix_set(work, c, r, get_matrixView(&view, r, c));
        int tmp = m;
        m = n;
        n = tmp;
        k = n;
    }
    else
    {
        work = view_to_dense(&view);
    }

    // V starts as identity (n x n)
    PiMatrix *V = create_denseMatrix(n, n);
    for (int i = 0; i < n; i++)
        matrix_set(V, i, i, 1.0);

    // Jacobi SVD: repeatedly apply Jacobi rotations until off-diagonal is ~0
    const int MAX_ITER = 100;
    const double TOL = 1e-10;

    for (int iter = 0; iter < MAX_ITER; iter++)
    {
        double off = 0.0;

        for (int p = 0; p < n - 1; p++)
        {
            for (int q = p + 1; q < n; q++)
            {
                // Compute dot products of columns p and q
                double app = 0.0, aqq = 0.0, apq = 0.0;
                for (int r = 0; r < m; r++)
                {
                    double wp = matrix_get(work, r, p);
                    double wq = matrix_get(work, r, q);
                    app += wp * wp;
                    aqq += wq * wq;
                    apq += wp * wq;
                }

                off += apq * apq;

                if (fabs(apq) < TOL * sqrt(app * aqq))
                    continue;

                // Compute Jacobi rotation angle
                double tau = (aqq - app) / (2.0 * apq);
                double t = (tau >= 0.0)
                               ? 1.0 / (tau + sqrt(1.0 + tau * tau))
                               : -1.0 / (-tau + sqrt(1.0 + tau * tau));
                double cosv = 1.0 / sqrt(1.0 + t * t);
                double sinv = t * cosv;

                // Apply rotation to columns of work
                for (int r = 0; r < m; r++)
                {
                    double wp = matrix_get(work, r, p);
                    double wq = matrix_get(work, r, q);
                    matrix_set(work, r, p, cosv * wp + sinv * wq);
                    matrix_set(work, r, q, -sinv * wp + cosv * wq);
                }

                // Accumulate rotation into V
                for (int r = 0; r < n; r++)
                {
                    double vp = matrix_get(V, r, p);
                    double vq = matrix_get(V, r, q);
                    matrix_set(V, r, p, cosv * vp + sinv * vq);
                    matrix_set(V, r, q, -sinv * vp + cosv * vq);
                }
            }
        }

        if (off < TOL)
            break;
    }

    // Extract singular values (column norms of work) and normalize columns -> U
    PiMatrix *U = create_denseMatrix(m, k);
    PiMatrix *S = create_denseMatrix(k, 1);

    for (int j = 0; j < k; j++)
    {
        double norm = 0.0;
        for (int r = 0; r < m; r++)
        {
            double v = matrix_get(work, r, j);
            norm += v * v;
        }
        norm = sqrt(norm);
        matrix_set(S, j, 0, norm);

        if (norm > 1e-14)
            for (int r = 0; r < m; r++)
                matrix_set(U, r, j, matrix_get(work, r, j) / norm);
    }

    PiMatrix *parts[3];
    if (transposed)
    {
        // A' = U S V'  ->  A = V S U'
        parts[0] = V;
        parts[1] = S;
        parts[2] = U;
    }
    else
    {
        parts[0] = U;
        parts[1] = S;
        parts[2] = V;
    }

    return pack_matrices(parts, 3); // returns [U, S, V]
}

Value mat_eig(vm_t *vm, int argc, Value *argv)
{
    // QR algorithm for real symmetric matrices.
    // Returns [eigenvalues, eigenvectors] — eigenvalues as column vector,
    // eigenvectors as columns of the returned matrix.
    // Only correct for symmetric matrices — add a check.
    MatrixView view;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "eig: Expected a square matrix.");

    if (view.rows != view.cols)
        vm_error(vm, "eig: Matrix must be square.");

    int n = view.rows;

    // Symmetry check (within tolerance)
    for (int r = 0; r < n; r++)
        for (int c = 0; c < r; c++)
            if (fabs(get_matrixView(&view, r, c) - get_matrixView(&view, c, r)) > 1e-9)
                vm_error(vm, "eig: Matrix must be symmetric (use svd for general matrices).");

    // A_iter will converge to diagonal (eigenvalues)
    // Q_total accumulates the eigenvectors
    PiMatrix *A_iter = view_to_dense(&view);
    PiMatrix *Q_total = create_denseMatrix(n, n);
    for (int i = 0; i < n; i++)
        matrix_set(Q_total, i, i, 1.0);

    const int MAX_ITER = 1000;
    const double TOL = 1e-10;

    double *v = (double *)malloc(n * sizeof(double));

    for (int iter = 0; iter < MAX_ITER; iter++)
    {
        // Wilkinson shift — improves convergence near eigenvalues
        double a_nn = matrix_get(A_iter, n - 1, n - 1);
        double a_n1 = matrix_get(A_iter, n - 2, n - 2);
        double b = matrix_get(A_iter, n - 1, n - 2);
        double delta = (a_n1 - a_nn) / 2.0;
        double sign = (delta >= 0.0) ? 1.0 : -1.0;
        double shift = a_nn - sign * b * b / (fabs(delta) + sqrt(delta * delta + b * b));

        // Subtract shift from diagonal before QR step
        for (int i = 0; i < n; i++)
            matrix_set(A_iter, i, i, matrix_get(A_iter, i, i) - shift);

        // QR decomposition of A_iter (reuse Householder logic inline)
        PiMatrix *Q_step = create_denseMatrix(n, n);
        for (int i = 0; i < n; i++)
            matrix_set(Q_step, i, i, 1.0);

        PiMatrix *R_step = A_iter; // in-place

        for (int col = 0; col < n - 1; col++)
        {
            double norm_sq = 0.0;
            for (int r = col; r < n; r++)
            {
                v[r] = matrix_get(R_step, r, col);
                norm_sq += v[r] * v[r];
            }
            double norm = sqrt(norm_sq);
            v[col] += (v[col] >= 0.0) ? norm : -norm;
            norm_sq = 0.0;
            for (int r = col; r < n; r++)
                norm_sq += v[r] * v[r];
            if (norm_sq < 1e-14)
                continue;

            for (int c = col; c < n; c++)
            {
                double dot = 0.0;
                for (int r = col; r < n; r++)
                    dot += v[r] * matrix_get(R_step, r, c);
                dot *= 2.0 / norm_sq;
                for (int r = col; r < n; r++)
                    matrix_set(R_step, r, c, matrix_get(R_step, r, c) - dot * v[r]);
            }
            for (int r = 0; r < n; r++)
            {
                double dot = 0.0;
                for (int k = col; k < n; k++)
                    dot += matrix_get(Q_step, r, k) * v[k];
                dot *= 2.0 / norm_sq;
                for (int k = col; k < n; k++)
                    matrix_set(Q_step, r, k, matrix_get(Q_step, r, k) - dot * v[k]);
            }
        }

        // A_iter = R * Q + shift*I
        PiMatrix *new_A = create_denseMatrix(n, n);
        for (int r = 0; r < n; r++)
            for (int c = 0; c < n; c++)
            {
                double sum = 0.0;
                for (int k = 0; k < n; k++)
                    sum += matrix_get(R_step, r, k) * matrix_get(Q_step, k, c);
                matrix_set(new_A, r, c, sum);
            }
        for (int i = 0; i < n; i++)
            matrix_set(new_A, i, i, matrix_get(new_A, i, i) + shift);

        // Q_total = Q_total * Q_step
        PiMatrix *new_Q = create_denseMatrix(n, n);
        for (int r = 0; r < n; r++)
            for (int c = 0; c < n; c++)
            {
                double sum = 0.0;
                for (int k = 0; k < n; k++)
                    sum += matrix_get(Q_total, r, k) * matrix_get(Q_step, k, c);
                matrix_set(new_Q, r, c, sum);
            }

        A_iter = new_A;
        Q_total = new_Q;

        // Convergence check — off-diagonal norm
        double off = 0.0;
        for (int r = 0; r < n; r++)
            for (int c = 0; c < n; c++)
                if (r != c)
                {
                    double x = matrix_get(A_iter, r, c);
                    off += x * x;
                }
        if (off < TOL)
            break;
    }

    free(v);

    // Eigenvalues are the diagonal of A_iter
    PiMatrix *eigenvalues = create_denseMatrix(n, 1);
    for (int i = 0; i < n; i++)
        matrix_set(eigenvalues, i, 0, matrix_get(A_iter, i, i));

    PiMatrix *parts[2] = {eigenvalues, Q_total};
    return pack_matrices(parts, 2); // returns [eigenvalues, eigenvectors]
}

// Utilities

Value mat_norm(vm_t *vm, int argc, Value *argv)
{
    // Supports order: 1, 2, -1 (inf), 0 (Frobenius)
    MatrixView view;

    if (argc < 1 || argc > 2 || !matrix_view(argv[0], &view))
        vm_error(vm, "norm: Expected a matrix and optional order (1, 2, -1 for inf, 0 for Frobenius).");

    int order = 0; // default: Frobenius
    if (argc == 2)
    {
        if (!IS_NUM(argv[1]))
            vm_error(vm, "norm: order must be a number.");
        order = (int)AS_NUM(argv[1]);
    }

    double result = 0.0;

    if (order == 0) // Frobenius: sqrt(sum of squares)
    {
        for (int r = 0; r < view.rows; r++)
            for (int c = 0; c < view.cols; c++)
            {
                double v = get_matrixView(&view, r, c);
                result += v * v;
            }
        result = sqrt(result);
    }
    else if (order == 1) // max absolute column sum
    {
        for (int c = 0; c < view.cols; c++)
        {
            double col_sum = 0.0;
            for (int r = 0; r < view.rows; r++)
                col_sum += fabs(get_matrixView(&view, r, c));
            if (col_sum > result)
                result = col_sum;
        }
    }
    else if (order == -1) // inf norm: max absolute row sum
    {
        for (int r = 0; r < view.rows; r++)
        {
            double row_sum = 0.0;
            for (int c = 0; c < view.cols; c++)
                row_sum += fabs(get_matrixView(&view, r, c));
            if (row_sum > result)
                result = row_sum;
        }
    }
    else if (order == 2) // spectral norm: largest singular value
    {
        // Reuse SVD — expensive but correct
        Value svd_result = mat_svd(vm, 1, argv);
        PiList *parts = AS_LIST(svd_result);
        Value *s_val = (Value *)list_getAt(parts->items, 1);
        MatrixView S_view;
        matrix_view(*s_val, &S_view);
        result = get_matrixView(&S_view, 0, 0); // largest singular value is first
    }
    else
        vm_error(vm, "norm: Unsupported order. Use 0 (Frobenius), 1, 2, or -1 (inf).");

    return NEW_NUM(result);
}

Value mat_rank(vm_t *vm, int argc, Value *argv)
{
    // Rank via SVD — count singular values above threshold
    MatrixView view;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "rank: Expected a matrix.");

    Value svd_result = mat_svd(vm, 1, argv);
    PiList *parts = AS_LIST(svd_result);
    Value *s_val = (Value *)list_getAt(parts->items, 1);
    MatrixView S_view;
    matrix_view(*s_val, &S_view);

    double tol = 1e-10;
    int rank = 0;
    int k = S_view.rows;

    // Scale tolerance by largest singular value (relative threshold)
    double sigma_max = get_matrixView(&S_view, 0, 0);
    tol = sigma_max * (view.rows > view.cols ? view.rows : view.cols) * 1e-14;

    for (int i = 0; i < k; i++)
        if (get_matrixView(&S_view, i, 0) > tol)
            rank++;

    return NEW_NUM(rank);
}

Value mat_trace(vm_t *vm, int argc, Value *argv)
{
    MatrixView view;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "trace: Expected a square matrix.");

    if (view.rows != view.cols)
        vm_error(vm, "trace: Matrix must be square.");

    double sum = 0.0;
    for (int i = 0; i < view.rows; i++)
        sum += get_matrixView(&view, i, i);

    return NEW_NUM(sum);
}

Value mat_pinv(vm_t *vm, int argc, Value *argv)
{
    // Moore-Penrose pseudoinverse via SVD: A+ = V * S+ * U'
    // where S+ replaces each nonzero singular value with its reciprocal
    MatrixView view;

    if (argc != 1 || !matrix_view(argv[0], &view))
        vm_error(vm, "pinv: Expected a matrix.");

    int m = view.rows;
    int n = view.cols;

    Value svd_result = mat_svd(vm, 1, argv);
    PiList *parts = AS_LIST(svd_result);
    Value *u_val = (Value *)list_getAt(parts->items, 0);
    Value *s_val = (Value *)list_getAt(parts->items, 1);
    Value *v_val = (Value *)list_getAt(parts->items, 2);

    MatrixView U_view, S_view, V_view;
    matrix_view(*u_val, &U_view);
    matrix_view(*s_val, &S_view);
    matrix_view(*v_val, &V_view);

    int k = S_view.rows;

    double sigma_max = get_matrixView(&S_view, 0, 0);
    double tol = sigma_max * (m > n ? m : n) * 1e-14;

    // Build S+ as diagonal (k×k), then compute V * S+ * U'
    // Result shape: n × m
    PiMatrix *result = create_denseMatrix(n, m);

    for (int j = 0; j < k; j++)
    {
        double sigma = get_matrixView(&S_view, j, 0);
        if (sigma < tol)
            continue; // treat as zero

        double inv_sigma = 1.0 / sigma;

        // Outer product: v_j * u_j' scaled by 1/sigma, accumulated into result
        for (int r = 0; r < n; r++)
            for (int c = 0; c < m; c++)
                matrix_set(result, r, c,
                           matrix_get(result, r, c) + inv_sigma * get_matrixView(&V_view, r, j) * get_matrixView(&U_view, c, j));
    }

    return NEW_OBJ(result);
}

// Module Registration
static BuiltinFunc linalgs_funcs[] = {
    {"solve", mat_solve},
    {"det", mat_det},
    {"inv", mat_inv},
    {"lu", mat_lu},
    {"cholesky", mat_cholesky},
    {"qr", mat_qr},
    {"svd", mat_svd},
    {"eig", mat_eig},
    {"norm", mat_norm},
    {"rank", mat_rank},
    {"trace", mat_trace},
    {"pinv", mat_pinv},
};

DEFINE_BUILTIN_MODULE(module_matlinalgs, "matrix.linalgs", linalgs_funcs, NULL);