#ifndef AC_MATRIX_H
#define AC_MATRIX_H

#include "ac_types.h"

/* ============================================================
 * ApolloCalc-CAS · ac_matrix.h — 矩阵扩展（C 128KB 第 7 轮，C 独有）
 * JS 128KB 没有完整矩阵功能（仅求解器内部的增广矩阵消元）。
 * 功能：乘法 / 转置 / 秩 / 行列式（LU 部分主元）/ 逆（Gauss-Jordan）/
 *       最大特征值（幂法 + Rayleigh 商）/ QR 分解（Gram-Schmidt）。
 * 尺寸上限 12×12（与线性方程组一致）。
 * ----
 * 布局契约：一维行主序，元素 (i, j) = a[i * cols + j]，全程无二维数组。
 * ----
 * 别名契约（接口按此实现，测试钉住）：
 *   - mul / transpose：c 可别名 a/b（内部缓冲中转）；
 *   - rank / det / eigenvalue：a 只读（work 为调用方草稿，见下）；
 *   - inverse：a 只读，inverse 可别名 a（内部增广静态区 288 B）；
 *   - QR：q / r 可别名 a（内部先拷 A）。
 * work 参数尺寸要求（调用方可传 g_mat_work，144 双精度充裕）：
 *   rank / det：≥ m·n；inverse：不使用（增广 2n×n=288 超 144，走模块
 *   静态区）；eigenvalue：≥ 2n（两个向量）；QR：≥ n（列草稿）。
 * Zero-GC：无 malloc、无 VLA、无递归；模块 static 工作区预分配。
 * ============================================================ */

#define AC_MATRIX_MAX_DIM  12
#define AC_MATRIX_MAX_SIZE (AC_MATRIX_MAX_DIM * AC_MATRIX_MAX_DIM)   /* 144 */

extern double g_mat_a[AC_MATRIX_MAX_SIZE];   /* 矩阵 A */
extern double g_mat_b[AC_MATRIX_MAX_SIZE];   /* 矩阵 B */
extern double g_mat_c[AC_MATRIX_MAX_SIZE];   /* 结果 C */
extern double g_mat_work[AC_MATRIX_MAX_SIZE];/* 工作区 */

/* [English] Multiply: C = A × B. A: m×k, B: k2×n (k must equal k2 — the
   inner dimension; contract), C: m×n. c may alias a/b. */
void ac_mat_mul(const double *a, int m, int k,
                const double *b, int k2, int n,
                double *c);

/* [English] Transpose: C = A^T. A: m×n, C: n×m. c may alias a. */
void ac_mat_transpose(const double *a, int m, int n, double *c);

/* [English] Rank via row-echelon elimination (partial pivot,
   |pivot| < 1e-12 counts as zero). work: >= m*n scratch. */
int ac_mat_rank(const double *a, int m, int n, double *work);

/* [English] Determinant via LU with partial pivoting:
   det = (product of pivots) * (-1)^swaps. Returns 0.0 for singular
   (any pivot under 1e-12) — indistinguishable from a true 0 det, which
   is the documented convention. work: >= n*n scratch. */
double ac_mat_det(const double *a, int n, double *work);

/* [English] Inverse via Gauss-Jordan on the augmented [A | I] (n × 2n,
   module static — 288 doubles exceed the 144 work pool). Returns 1 on
   success, 0 on singular. inverse may alias a. work: unused. */
int ac_mat_inverse(const double *a, int n, double *inverse, double *work);

/* [English] Dominant eigenvalue via the power method with Rayleigh
   quotient. Start vector = normalized all-ones. Iterates until
   |λ_new − λ| < 1e-12 or max_iter. Returns the estimate (0.0 for the
   zero matrix). work: >= 2n (two vectors). */
double ac_mat_eigenvalue_power(const double *a, int n, int max_iter, double *work);

/* [English] QR decomposition via Gram-Schmidt: A (m×n, m >= n expected,
   full column rank for a clean factorization) → Q (m×n, orthonormal
   columns), R (n×n upper triangular). Rank-deficient columns leave
   R[j][j] = 0 with a zero q column (documented). q/r may alias a.
   work: >= n scratch. */
void ac_mat_qr(const double *a, int m, int n, double *q, double *r, double *work);

#endif /* AC_MATRIX_H */
