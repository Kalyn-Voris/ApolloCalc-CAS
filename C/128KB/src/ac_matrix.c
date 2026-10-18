/* ============================================================
 * ApolloCalc-CAS · ac_matrix.c — 矩阵扩展（C 128KB 第 7 轮，C 独有）
 * 一维行主序 + 索引寻址，全程无二维数组（任务卡铁律）。
 * 模块 static 工作区：逆矩阵增广 [A|I]（12×24 = 288 双精度，超出
 * 144 的 work 池）+ mul/transpose/QR 的别名中转缓冲。
 * Zero-GC：无 malloc、无 VLA、无递归；全部工作区预分配。
 * ============================================================ */
#include "ac_matrix.h"
#include <math.h>

/* ==================== 全局矩阵 + 模块工作区 ==================== */

double g_mat_a[AC_MATRIX_MAX_SIZE];
double g_mat_b[AC_MATRIX_MAX_SIZE];
double g_mat_c[AC_MATRIX_MAX_SIZE];
double g_mat_work[AC_MATRIX_MAX_SIZE];

static double s_mat_mul[AC_MATRIX_MAX_SIZE];        /* mul 中转（防别名） */
static double s_mat_tr[AC_MATRIX_MAX_SIZE];         /* transpose 中转 */
static double s_mat_aug[AC_MATRIX_MAX_SIZE * 2];    /* 逆矩阵增广 [A|I]：12×24 = 288 */
static double s_qr_a[AC_MATRIX_MAX_SIZE];           /* QR 的 A 拷贝（防别名） */

/* ==================== 乘法 / 转置 ==================== */

void ac_mat_mul(const double *a, int m, int k,
                const double *b, int k2, int n,
                double *c) {
    int i;
    int j;
    int p;
    if (k != k2) {
        /* 内维不匹配：契约违约，防御性清零输出 */
        for (i = 0; i < m * n && i < AC_MATRIX_MAX_SIZE; i++) { s_mat_mul[i] = 0.0; }
        for (i = 0; i < m * n && i < AC_MATRIX_MAX_SIZE; i++) { c[i] = s_mat_mul[i]; }
        return;
    }
    for (i = 0; i < m; i++) {
        for (j = 0; j < n; j++) {
            double acc = 0.0;
            for (p = 0; p < k; p++) {
                acc += a[i * k + p] * b[p * n + j];
            }
            s_mat_mul[i * n + j] = acc;
        }
    }
    for (i = 0; i < m * n; i++) { c[i] = s_mat_mul[i]; }
}

void ac_mat_transpose(const double *a, int m, int n, double *c) {
    int i;
    int j;
    for (i = 0; i < m; i++) {
        for (j = 0; j < n; j++) {
            s_mat_tr[j * m + i] = a[i * n + j];
        }
    }
    for (i = 0; i < m * n; i++) { c[i] = s_mat_tr[i]; }
}

/* ==================== 秩 / 行列式（共用行消元核） ==================== */

/* 行梯形消元（部分主元）：a_work 上原地消元，返回非零主元个数并把
 * 交换次数写入 *swaps。主元阈值 1e-12（与求解器奇异判定同款）。 */
static int mat_echelon(double *a_work, int m, int n, int *swaps) {
    int rank = 0;
    int col = 0;
    int row;
    int piv;
    int k;
    double maxAbs;
    double f;
    double v;

    *swaps = 0;
    while (rank < m && col < n) {
        /* 部分主元：当前列 |·| 最大的行换到 rank 行 */
        piv = rank;
        maxAbs = a_work[rank * n + col];
        if (maxAbs < 0.0) { maxAbs = -maxAbs; }
        for (row = rank + 1; row < m; row++) {
            v = a_work[row * n + col];
            if (v < 0.0) { v = -v; }
            if (v > maxAbs) { maxAbs = v; piv = row; }
        }
        if (maxAbs < 1e-12) {
            /* 该列全零（余下部分）：列右移继续找秩 */
            col++;
            continue;
        }
        if (piv != rank) {
            int c2;
            for (c2 = 0; c2 < n; c2++) {
                double t = a_work[rank * n + c2];
                a_work[rank * n + c2] = a_work[piv * n + c2];
                a_work[piv * n + c2] = t;
            }
            *swaps = *swaps + 1;
        }
        /* 消去下方各行 */
        for (row = rank + 1; row < m; row++) {
            f = a_work[row * n + col] / a_work[rank * n + col];
            if (f == 0.0) { continue; }
            for (k = col; k < n; k++) {
                a_work[row * n + k] -= f * a_work[rank * n + k];
            }
        }
        rank++;
        col++;
    }
    return rank;
}

int ac_mat_rank(const double *a, int m, int n, double *work) {
    int swaps;
    int i;
    for (i = 0; i < m * n; i++) { work[i] = a[i]; }
    return mat_echelon(work, m, n, &swaps);
}

double ac_mat_det(const double *a, int n, double *work) {
    int swaps = 0;
    double det = 1.0;
    int i;
    int rank;
    for (i = 0; i < n * n; i++) { work[i] = a[i]; }
    rank = mat_echelon(work, n, n, &swaps);
    if (rank < n) { return 0.0; }              /* 奇异（含真 0 行列式，约定合并） */
    for (i = 0; i < n; i++) { det *= work[i * n + i]; }
    return (swaps % 2 == 0) ? det : -det;
}

/* ==================== 逆矩阵（Gauss-Jordan，增广 [A|I]） ==================== */

int ac_mat_inverse(const double *a, int n, double *inverse, double *work) {
    int i;
    int j;
    int col;
    int row;
    int piv;
    int c2;
    double maxAbs;
    double v;
    double f;
    (void)work;   /* 增广 2n×n = 288 超 144 池，走模块静态区（契约见头文件） */

    /* 增广 [A | I]：行主序，行 i = [a_i0..a_i,n-1 | I 行] */
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            s_mat_aug[i * (2 * n) + j] = a[i * n + j];
            s_mat_aug[i * (2 * n) + n + j] = (i == j) ? 1.0 : 0.0;
        }
    }

    /* Gauss-Jordan：逐列归一主元并消散至全列 */
    for (col = 0; col < n; col++) {
        piv = col;
        maxAbs = s_mat_aug[col * (2 * n) + col];
        if (maxAbs < 0.0) { maxAbs = -maxAbs; }
        for (row = col + 1; row < n; row++) {
            v = s_mat_aug[row * (2 * n) + col];
            if (v < 0.0) { v = -v; }
            if (v > maxAbs) { maxAbs = v; piv = row; }
        }
        if (maxAbs < 1e-12) { return 0; }      /* 奇异 */
        if (piv != col) {
            for (c2 = 0; c2 < 2 * n; c2++) {
                double t = s_mat_aug[col * (2 * n) + c2];
                s_mat_aug[col * (2 * n) + c2] = s_mat_aug[piv * (2 * n) + c2];
                s_mat_aug[piv * (2 * n) + c2] = t;
            }
        }
        f = s_mat_aug[col * (2 * n) + col];
        for (c2 = 0; c2 < 2 * n; c2++) { s_mat_aug[col * (2 * n) + c2] /= f; }
        for (row = 0; row < n; row++) {
            if (row == col) { continue; }
            f = s_mat_aug[row * (2 * n) + col];
            if (f == 0.0) { continue; }
            for (c2 = 0; c2 < 2 * n; c2++) {
                s_mat_aug[row * (2 * n) + c2] -= f * s_mat_aug[col * (2 * n) + c2];
            }
        }
    }

    /* 右半区 = A⁻¹ */
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            inverse[i * n + j] = s_mat_aug[i * (2 * n) + n + j];
        }
    }
    return 1;
}

/* ==================== 特征值（幂法 + Rayleigh 商） ==================== */

double ac_mat_eigenvalue_power(const double *a, int n, int max_iter, double *work) {
    double *v = work;          /* work[0..n-1]：当前向量（归一化） */
    double *w = work + n;      /* work[n..2n-1]：A·v */
    double norm = 0.0;
    double lam = 0.0;
    double lam_new = 0.0;
    double d;
    int it;
    int i;
    int j;

    /* 初始向量：全 1 归一化（对角阵/优势特征向量与 e1 正交的退化规避） */
    for (i = 0; i < n; i++) {
        v[i] = 1.0;
        norm += v[i] * v[i];
    }
    norm = sqrt(norm);
    if (norm < 1e-300) { return 0.0; }
    for (i = 0; i < n; i++) { v[i] /= norm; }

    for (it = 0; it < max_iter; it++) {
        /* w = A·v */
        for (i = 0; i < n; i++) {
            double acc = 0.0;
            for (j = 0; j < n; j++) {
                acc += a[i * n + j] * v[j];
            }
            w[i] = acc;
        }
        /* Rayleigh 商：λ = vᵀ·w（v 已归一，vᵀv = 1） */
        lam_new = 0.0;
        for (i = 0; i < n; i++) { lam_new += v[i] * w[i]; }
        d = lam_new - lam;
        if (d < 0.0) { d = -d; }
        lam = lam_new;
        if (d < 1e-12) { break; }
        /* v = w / ||w||；零矩阵/停滞防御 */
        norm = 0.0;
        for (i = 0; i < n; i++) { norm += w[i] * w[i]; }
        norm = sqrt(norm);
        if (norm < 1e-300) { break; }
        for (i = 0; i < n; i++) { v[i] = w[i] / norm; }
    }
    return lam;
}

/* ==================== QR 分解（Gram-Schmidt） ==================== */

void ac_mat_qr(const double *a, int m, int n, double *q, double *r, double *work) {
    int j;
    int i;
    int k;

    /* A 先拷内部静态区（q/r 可别名 a）；R 全清零（下三角保持 0） */
    for (i = 0; i < m * n; i++) { s_qr_a[i] = a[i]; }
    for (i = 0; i < n * n; i++) { r[i] = 0.0; }

    for (j = 0; j < n; j++) {
        /* v = A[:, j]（work[0..m-1]） */
        for (i = 0; i < m; i++) { work[i] = s_qr_a[i * n + j]; }
        /* 减去在前各列 q_i 上的投影：R[i][j] = q_i·v，v -= R[i][j]·q_i */
        for (i = 0; i < j; i++) {
            double rij = 0.0;
            for (k = 0; k < m; k++) { rij += q[k * n + i] * work[k]; }
            r[i * n + j] = rij;
            for (k = 0; k < m; k++) { work[k] -= rij * q[k * n + i]; }
        }
        /* R[j][j] = ||v||；归一化得 q_j（退化列 → 零列 + R[j][j] = 0） */
        {
            double nj = 0.0;
            for (k = 0; k < m; k++) { nj += work[k] * work[k]; }
            nj = sqrt(nj);
            r[j * n + j] = nj;
            for (k = 0; k < m; k++) {
                q[k * n + j] = (nj > 1e-12) ? work[k] / nj : 0.0;
            }
        }
    }
}
