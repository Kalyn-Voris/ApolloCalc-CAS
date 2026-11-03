/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · eig_csv_probe.c — F4 差分探针（独立测试辅助，非验收链路成员）

   职责：把 C/128KB-ProTEXT 的 ac_pro_schur_all 在 tests/eigen-cases.json 全部
   47 例上的输出导成 CSV（tests/bench/eig128-c.csv），供 tests/bench/
   diff-eigen.js 与 JS 内核 proSchur 逐例对拍（exit 0 为验收线）。
   输出契约（排序全序/共轭对相邻 +Im 在前/错误码/残差闸）见
   docs/contracts/eigen-format-v25.md；特征向量在导出前按契约 §4 做
   符号规范化（每列最大 |·| 分量置正、并列取最小索引）——C API 本体
   （ac_pro_schur_vector）零改动，规范化只发生在本探针的导出侧。

   独立性：不进 test_pro_runner.c tally、不进 Makefile/PRO_EXTRA、
   不触碰 verify.ps1 / smoke_runner / 任何 C 源码与账本；本文件与
   tests/bench/eig128-c.csv 是本探针的全部交付物。编译运行（worktree 内）：
     gcc -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror -O2 -DAC_PRO=1 \
         -Wl,--wrap=malloc -Wl,--wrap=free \
         -o eig_csv_probe.exe test/eig_csv_probe.c \
         src/ac_pro_schur.c src/ac_pro_scratch.c -lm
     ./eig_csv_probe.exe > ../tests/bench/eig128-c.csv   （仓库根相对路径）
   --wrap 链接 + __wrap_malloc 拦截：schur 全链路任何 malloc 即 abort
   （零 GC 纪律的 C 侧同源证明，test_pro_runner.c 同款手法）。

   CSV 格式（%.17g 保证 f64 往返逐位一致）：
     mat,<id>,<n>,<m0>,...,<m_{n*n-1}>     用例矩阵（与契约 JSON 精确比对防转录漂移）
     eig,<id>,<rc>,<count>                  每用例一行
     val,<id>,<k>,<re>,<im>,<hasvec>        rc=1 时 n 行；hasvec=1 表示实特征值（有 vec 行）
     vec,<id>,<k>,<v0>,...,<v_{n-1}>        hasvec=1 的特征值一行（已符号规范化）
   n=0/n=13 拒绝用例只有 eig 行（m 为 dummy，不导 mat 行——JS 侧无对应
   输入域，对拍口径只对 rc/count）。 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ac_pro.h"

#if AC_PRO_ACTIVE

/* ---- malloc 拦截（零 GC 证明；与 test_pro_runner.c 同款语义） ---- */

extern void *__real_malloc(size_t size);
extern void  __real_free(void *ptr);

/* 启动期（构造器建存储池）放行到 real malloc；main 起进入 live 态，
 * schur 全链路再碰 malloc/free 即 abort（零 GC 纪律的 C 侧同源证明）。 */
static int s_probe_live = 0;

void *__wrap_malloc(size_t size) {
    if (s_probe_live) {
        fputs("[FATAL] malloc called in eig probe runtime!\n", stderr);
        abort();
    }
    return __real_malloc(size);
}

void __wrap_free(void *ptr) {
    if (s_probe_live) {
        fputs("[FATAL] free called in eig probe runtime!\n", stderr);
        abort();
    }
    __real_free(ptr);
}

/* ---- 用例表（由 tests/eigen-cases.json 生成，diff-eigen.js 用 mat 行守卫漂移） ---- */

/* 本文件由 .auto/gen-eigen-cases.js 从 tests/eigen-cases.json 生成（一次性Paste，
 * 探针表中矩阵与契约 JSON 的逐字一致性由 diff-eigen.js 的 mat 行精确比对守卫）。 */

typedef struct {
    const char *id;
    int n;
    const double *m;   /* NULL = 无矩阵（n=0 拒绝用例）；n=13 用 16 元 dummy */
} eig_case_t;

static const double g_case_n1_pos[] = { 5 };
static const double g_case_n1_neg[] = { -3 };
static const double g_case_n1_zero[] = { 0 };
static const double g_case_diag3[] = { 1, 0, 0, 0, 2, 0, 0, 0, 3 };
static const double g_case_diag3_neg[] = { -1, 0, 0, 0, -2, 0, 0, 0, -3 };
static const double g_case_diag12[] = { 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
static const double g_case_diag_frac[] = { 0.5, 0, 0, 0, -0.25, 0, 0, 0, 1.5 };
static const double g_case_p2_big[] = { 1099511627776, 0, 0, 9.094947017729282e-13 };
static const double g_case_p2_small[] = { 1e-150, 0, 0, -2e-150 };
static const double g_case_p2_mixed4[] = { 1152921504606847000, 1, 0, 0, 0, 8.673617379884035e-19, 0, 0, 0, 0, 0, -1099511627776, 0, 0, 1099511627776, 0 };
static const double g_case_zero2[] = { 0, 0, 0, 0 };
static const double g_case_rotation90[] = { 0, -1, 1, 0 };
static const double g_case_rotation60[] = { 0.5, -0.8660254037844386, 0.8660254037844386, 0.5 };
static const double g_case_rot_scaled[] = { 0, -2, 2, 0 };
static const double g_case_mixed_block3[] = { 0, -1, 0, 1, 0, 0, 0, 0, 3 };
static const double g_case_mixed4b[] = { 5, 0, 0, 0, 0, 0.8660254037844387, -0.5, 0, 0, 0.5, 0.8660254037844387, 0, 0, 0, 0, -3 };
static const double g_case_skew3[] = { 0, -1, 0, 1, 0, 0, 0, 0, 0 };
static const double g_case_skew4[] = { 0, -1.5, 0, 0, 1.5, 0, 0, 0, 0, 0, 0, -0.5, 0, 0, 0.5, 0 };
static const double g_case_two_pairs[] = { 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 0.5, -0.8660254037844386, 0, 0, 0.8660254037844386, 0.5 };
static const double g_case_asym_tri2[] = { 1, 1, 0, 2 };
static const double g_case_asym_tri4[] = { 4, 1, 0, 1, 0, 3, 1, 0, 0, 0, 2, 1, 0, 0, 0, 1 };
static const double g_case_nonnorm2[] = { 3, 1, 0.5, 1 };
static const double g_case_fib2[] = { 0, 1, 1, 1 };
static const double g_case_int_asym3[] = { 0, 0, 6, 1, 0, -11, 0, 1, 6 };
static const double g_case_mixed4[] = { 3, 1, 0, 0, 0.5, 1, 0, 0, 0, 0, 0, -2, 0, 0, 2, 0 };
static const double g_case_jordan2[] = { 2, 1, 0, 2 };
static const double g_case_jordan3[] = { 2, 1, 0, 0, 2, 1, 0, 0, 2 };
static const double g_case_degen222[] = { 2, 0, 0, 0, 2, 0, 0, 0, 2 };
static const double g_case_degen5511[] = { 5, 0, 0, 0, 0, 5, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
static const double g_case_nilpotent4[] = { 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0 };
static const double g_case_tri3[] = { 2, 1, 0, 1, 2, 1, 0, 1, 2 };
static const double g_case_tri4[] = { 2, 1, 0, 0, 1, 2, 1, 0, 0, 1, 2, 1, 0, 0, 1, 2 };
static const double g_case_tri12[] = { 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2 };
static const double g_case_hilbert4[] = { 1, 0.5, 0.3333333333333333, 0.25, 0.5, 0.3333333333333333, 0.25, 0.2, 0.3333333333333333, 0.25, 0.2, 0.16666666666666666, 0.25, 0.2, 0.16666666666666666, 0.14285714285714285 };
static const double g_case_tol_pass[] = { 1, 0, 1e-13, 2 };
static const double g_case_tol_iterate[] = { 1, 0, 1e-11, 2 };
static const double g_case_neardef2[] = { 1, 1, 1e-9, 1 };
static const double g_case_c3[] = { 0, 0, 1, 1, 0, -0.0, 0, 1, -0.0 };
static const double g_case_c4[] = { 0, 0, 0, 1, 1, 0, 0, -0.0, 0, 1, 0, -0.0, 0, 0, 1, -0.0 };
static const double g_case_c8[] = { 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, -0.0, 0, 1, 0, 0, 0, 0, 0, -0.0, 0, 0, 1, 0, 0, 0, 0, -0.0, 0, 0, 0, 1, 0, 0, 0, -0.0, 0, 0, 0, 0, 1, 0, 0, -0.0, 0, 0, 0, 0, 0, 1, 0, -0.0, 0, 0, 0, 0, 0, 0, 1, -0.0 };
static const double g_case_c12[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -0.0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, -0.0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, -0.0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, -0.0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, -0.0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, -0.0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, -0.0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, -0.0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, -0.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, -0.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -0.0 };
static const double g_case_perm4_fail3[] = { 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0 };
static const double g_case_dense3[] = { 2, 1, 3, 4, 5, 6, 7, 8, 9 };
static const double g_case_dense_sym3[] = { 4, 1, 2, 1, 5, 3, 2, 3, 6 };
static const double g_case_dense4[] = { 3, 1, 0, 2, 1, 2, 5, 1, 0, 5, 2, 3, 2, 1, 3, 1 };

static const eig_case_t g_eig_cases[] = {
    { "n1_pos", 1, g_case_n1_pos },
    { "n1_neg", 1, g_case_n1_neg },
    { "n1_zero", 1, g_case_n1_zero },
    { "diag3", 3, g_case_diag3 },
    { "diag3_neg", 3, g_case_diag3_neg },
    { "diag12", 12, g_case_diag12 },
    { "diag_frac", 3, g_case_diag_frac },
    { "p2_big", 2, g_case_p2_big },
    { "p2_small", 2, g_case_p2_small },
    { "p2_mixed4", 4, g_case_p2_mixed4 },
    { "zero2", 2, g_case_zero2 },
    { "rotation90", 2, g_case_rotation90 },
    { "rotation60", 2, g_case_rotation60 },
    { "rot_scaled", 2, g_case_rot_scaled },
    { "mixed_block3", 3, g_case_mixed_block3 },
    { "mixed4b", 4, g_case_mixed4b },
    { "skew3", 3, g_case_skew3 },
    { "skew4", 4, g_case_skew4 },
    { "two_pairs", 4, g_case_two_pairs },
    { "asym_tri2", 2, g_case_asym_tri2 },
    { "asym_tri4", 4, g_case_asym_tri4 },
    { "nonnorm2", 2, g_case_nonnorm2 },
    { "fib2", 2, g_case_fib2 },
    { "int_asym3", 3, g_case_int_asym3 },
    { "mixed4", 4, g_case_mixed4 },
    { "jordan2", 2, g_case_jordan2 },
    { "jordan3", 3, g_case_jordan3 },
    { "degen222", 3, g_case_degen222 },
    { "degen5511", 4, g_case_degen5511 },
    { "nilpotent4", 4, g_case_nilpotent4 },
    { "tri3", 3, g_case_tri3 },
    { "tri4", 4, g_case_tri4 },
    { "tri12", 12, g_case_tri12 },
    { "hilbert4", 4, g_case_hilbert4 },
    { "tol_pass", 2, g_case_tol_pass },
    { "tol_iterate", 2, g_case_tol_iterate },
    { "neardef2", 2, g_case_neardef2 },
    { "c3", 3, g_case_c3 },
    { "c4", 4, g_case_c4 },
    { "c8", 8, g_case_c8 },
    { "c12", 12, g_case_c12 },
    { "perm4_fail3", 4, g_case_perm4_fail3 },
    { "dense3", 3, g_case_dense3 },
    { "dense_sym3", 3, g_case_dense_sym3 },
    { "dense4", 4, g_case_dense4 },
    { "rej_n0", 0, 0 },
    { "rej_n13", 13, 0 },
};

#define PROBE_VEC_RESID 1e-9   /* 与 ac_pro_schur.c PRO_SCHUR_VEC_RESID 同值（导出侧自检用） */

/* 符号规范化（契约 §4 两侧同式同序）：最大 |·| 分量置正，并列取最小索引 */
static void probe_sign_normalize(double *v, int n) {
    int i, imax = 0;
    double big = 0.0, a;
    for (i = 0; i < n; i++) {
        a = v[i];
        if (a < 0.0) { a = -a; }
        if (a > big) { big = a; imax = i; }
    }
    if (v[imax] < 0.0) {
        for (i = 0; i < n; i++) { v[i] = -v[i]; }
    }
}

int main(void) {
    const int ncases = (int)(sizeof(g_eig_cases) / sizeof(g_eig_cases[0]));
    int ci;
    int failures = 0;

    s_probe_live = 1;

    for (ci = 0; ci < ncases; ci++) {
        const eig_case_t *c = &g_eig_cases[ci];
        double dummy[16];
        const double *m = c->m;
        int rc, k;

        if (m == 0) {
            /* 拒绝用例：n=0 直接传 dummy 指针（C 侧 n 判据先于解引用）；
             * n=13 传 16 元 dummy（13*13 > 16 不会越界读：n 判据先行）。 */
            for (k = 0; k < 16; k++) { dummy[k] = 0.0; }
            m = dummy;
        } else {
            int i;
            printf("mat,%s,%d", c->id, c->n);
            for (i = 0; i < c->n * c->n; i++) {
                printf(",%.17g", m[i]);
            }
            printf("\n");
        }

        rc = ac_pro_schur_all(m, c->n);
        printf("eig,%s,%d,%d\n", c->id, rc, ac_pro_schur_count());

        if (rc != 1) {
            /* 失败时视图内容未定义（契约 §3.7），只对拍 rc/count */
            continue;
        }
        for (k = 0; k < c->n; k++) {
            if (ac_pro_schur_im(k) != 0.0) {
                printf("val,%s,%d,%.17g,%.17g,0\n", c->id, k,
                       ac_pro_schur_re(k), ac_pro_schur_im(k));
            } else {
                double v[AC_PRO_MATRIX_MAX_DIM];
                const double *raw = ac_pro_schur_vector(k);
                double resid = 0.0, anorm = 0.0;
                int i, i2, j2;
                if (raw == 0) {
                    /* 实特征值无向量：契约禁止，导出侧判 fail */
                    printf("val,%s,%d,%.17g,%.17g,1\n", c->id, k,
                           ac_pro_schur_re(k), ac_pro_schur_im(k));
                    failures++;
                    continue;
                }
                for (i = 0; i < c->n; i++) { v[i] = raw[i]; }
                probe_sign_normalize(v, c->n);
                /* 用规范化后的向量复核残差 + ‖A‖∞（重算，不信任视图中间量） */
                for (i2 = 0; i2 < c->n; i2++) {
                    double s = 0.0;
                    for (j2 = 0; j2 < c->n; j2++) { s += m[i2 * c->n + j2] * v[j2]; }
                    {
                        double d = fabs(s - ac_pro_schur_re(k) * v[i2]);
                        if (d > resid) { resid = d; }
                    }
                }
                for (i2 = 0; i2 < c->n; i2++) {
                    double s = 0.0;
                    for (j2 = 0; j2 < c->n; j2++) { s += fabs(m[i2 * c->n + j2]); }
                    if (s > anorm) { anorm = s; }
                }
                if (!(resid >= 0.0) || resid > PROBE_VEC_RESID * (1.0 + anorm)) {
                    failures++;
                }
                printf("val,%s,%d,%.17g,%.17g,1\n", c->id, k,
                       ac_pro_schur_re(k), ac_pro_schur_im(k));
                printf("vec,%s,%d", c->id, k);
                for (i = 0; i < c->n; i++) {
                    printf(",%.17g", v[i]);
                }
                printf("\n");
            }
        }
    }

    if (failures != 0) {
        fprintf(stderr, "eig_csv_probe: %d export-side failures (residual/missing-vector)\n", failures);
        return 1;
    }
    return 0;
}

#else /* !AC_PRO_ACTIVE */

int main(void) {
    fputs("eig_csv_probe: AC_PRO inactive\n", stderr);
    return 1;
}

#endif /* AC_PRO_ACTIVE */
