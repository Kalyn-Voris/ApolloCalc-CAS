/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
#ifndef AC_SOLVERS_H
#define AC_SOLVERS_H

#include "ac_types.h"

/* [English] Root result pools (6 slots: max of 4 polynomial roots and 6 linear unknowns). */
extern int      g_root_count;
extern double   g_root_re[ROOT_POOL_SIZE];
extern double   g_root_im[ROOT_POOL_SIZE];
extern ac_int_t g_root_exact_num[ROOT_POOL_SIZE];
extern ac_int_t g_root_exact_den[ROOT_POOL_SIZE];
extern ac_int_t g_root_exact_rad[ROOT_POOL_SIZE];
extern int      g_root_exact_valid[ROOT_POOL_SIZE];

extern int      g_solve_error;      /* 0=OK 1=degree 2=precheck 3=singular 4=mem */
extern double   g_residual_max;

/* [English] Polynomial coefficient buffer (degree 0..4). */
extern double   g_coeffs[5];

/* [English] Quadratic/cubic/quartic solvers. Read g_coeffs[0..degree].
   Write g_root_count and root pools. */
void ac_solve_quadratic(void);
void ac_solve_cubic(void);
void ac_solve_quartic(void);

/* [English] Linear system solver. Reads g_matrix_pool (n x (n+1) augmented). */
void ac_solve_linear_system(int n);

/* [English] Residual verification: for each real root, compute relative residual. */
void ac_verify_roots(void);

/* [English] Pre-check: scan g_token_pool for depth/func/var limits. */
int ac_pre_check_expression(void);

/* [English] Degree guard (Abel-Ruffini: degree > 4 rejected). */
int ac_guard_degree(int degree);

/* [English] Memory watermark check (48KB for 64KB version). */
int ac_check_memory_watermark(void);

/* [English] Reset all solver state. */
void ac_solvers_reset(void);

/* [English] Extract polynomial coefficients via numerical sampling.
   Reads g_token_pool, writes g_coeffs[0..degree]. */
int ac_extract_coefficients(int max_degree);

#endif /* AC_SOLVERS_H */
