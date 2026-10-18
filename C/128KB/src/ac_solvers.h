#ifndef AC_SOLVERS_H
#define AC_SOLVERS_H

#include "ac_types.h"

/* [English] Root result pools (12 slots: 12-unknown linear systems land in
   round 5; polynomials use the first 4). Exact forms are five-component:
   root = ((rat + rad_num·√rad)/den) × 10^exp, mirroring the exact layer. */
extern int      g_root_count;
extern double   g_root_re[ROOT_POOL_SIZE];
extern double   g_root_im[ROOT_POOL_SIZE];
extern ac_int_t g_root_exact_rat[ROOT_POOL_SIZE];
extern ac_int_t g_root_exact_rad_num[ROOT_POOL_SIZE];
extern ac_int_t g_root_exact_rad[ROOT_POOL_SIZE];
extern ac_int_t g_root_exact_den[ROOT_POOL_SIZE];
extern int      g_root_exact_exp[ROOT_POOL_SIZE];
extern int      g_root_exact_valid[ROOT_POOL_SIZE];

extern int      g_solve_error;      /* 0=OK 1=degree 2=precheck 3=singular 4=mem */
extern double   g_residual_max;

/* [English] Polynomial coefficient buffer (degree 0..4). */
extern double   g_coeffs[5];

/* [English] Quadratic/cubic/quartic solvers. Read g_coeffs[0..degree].
   Write g_root_count and root pools. The cubic takes skip_rational:
   1 = skip the rational-root hook (quartic resolvent: derived float
   coefficients would poison the integer snapshot), 0 = hook active. */
void ac_solve_quadratic(void);
void ac_solve_cubic(int skip_rational);
void ac_solve_quartic(void);

/* [English] Newton's method on the current parsed expression (C 128KB
   round 5; no JS counterpart). x_{n+1} = x_n - f(x_n)/f'(x_n), f' via the
   calculus layer's central difference (ac_calc_derivative). On success
   writes the root to slot 0 (im = 0, exact form invalid), root count 1,
   returns 1. On failure (non-finite f/f', zero derivative, or no
   convergence within max_iter) sets g_solve_error = 3 and returns 0. */
int ac_solve_newton(double x0, int max_iter, double tol);

/* [English] Linear system solver. Reads g_matrix_pool (n x (n+1) augmented),
   1 <= n <= 12 (pool 156 = 12x13, C 128KB round 5). */
void ac_solve_linear_system(int n);

/* [English] Residual verification: for each real root, compute relative residual. */
void ac_verify_roots(void);

/* [English] Pre-check: scan g_token_pool for depth/func/var limits. */
int ac_pre_check_expression(void);

/* [English] Degree guard (Abel-Ruffini: degree > 4 rejected). */
int ac_guard_degree(int degree);

/* [English] Memory watermark check (96KB = 75% of the 128KB budget). */
int ac_check_memory_watermark(void);

/* [English] Reset all solver state. */
void ac_solvers_reset(void);

/* [English] Extract polynomial coefficients via numerical sampling.
   Reads g_token_pool, writes g_coeffs[0..degree]. */
int ac_extract_coefficients(int max_degree);

#endif /* AC_SOLVERS_H */
