/* 功能 8(一般实矩阵特征值)的验收测试。检查口径:
 *   ① 特征值谱按「多重集 + 容差」匹配(与 tests/polynomials.json 的根多重
 *      集同思想):期望谱逐项在未用槽位里找容差内匹配,不预设顺序 —— 只有
 *      |λ| 拉开(>1e-6)的用例才逐位断言 (|λ|, Re, Im) 降序。
 *   ② 实特征向量:‖Av − λv‖∞ 直接对原始 A 复核(不信任视图内任何中间量);
 *      复特征值 vector(k) 必须返回 NULL(绝不硬凑复向量)。
 *   ③ 对称矩阵与 A5(ac_pro_eig_all)交叉对拍。
 * 自带 pro_tally_t,不 include runner 的内部头(与 test_pro_ode.c 同约定)。
 * 不声明静态对象:本 TU 的 .bss 必须是 0。 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../src/ac_pro.h"

#if AC_PRO_ACTIVE

typedef struct {
    int checks;
    int failed;
} sch_tally_t;

static void scheck(sch_tally_t *t, int cond, const char *name, const char *detail) {
    t->checks++;
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        t->failed++;
        printf("  [FAIL] %s  (%s)\n", name, detail);
    }
}

static int sch_close(double a, double b, double tol) {
    double d = a - b;
    return (d <= tol) && (d >= -tol);
}

/* ‖Av − λv‖∞(对原始 A 直接复核;v 为 NULL 返回 −1) */
static double sch_vec_resid(const double *a, int n, int k) {
    const double *v = ac_pro_schur_vector(k);
    double lam = ac_pro_schur_re(k);
    double resid = 0.0;
    int i, j;
    if (v == 0) { return -1.0; }
    for (i = 0; i < n; i++) {
        double s = 0.0;
        for (j = 0; j < n; j++) { s += a[i * n + j] * v[j]; }
        {
            double d = fabs(s - lam * v[i]);
            if (d > resid) { resid = d; }
        }
    }
    return resid;
}

/* 谱多重集匹配:每个期望 (re, im) 在未用槽位里找容差内匹配。
 * 全部匹配成功且计数一致 → 1。 */
static int sch_spectrum_match(int n, int nexp,
                              const double *ere, const double *eim,
                              double tol) {
    int used[AC_PRO_MATRIX_MAX_DIM];
    int i, j;
    for (i = 0; i < n; i++) { used[i] = 0; }
    if (nexp != n) { return 0; }
    for (i = 0; i < nexp; i++) {
        int found = -1;
        for (j = 0; j < n; j++) {
            if (used[j]) { continue; }
            if (sch_close(ac_pro_schur_re(j), ere[i], tol) &&
                sch_close(ac_pro_schur_im(j), eim[i], tol)) {
                found = j;
                break;
            }
        }
        if (found < 0) { return 0; }
        used[found] = 1;
    }
    return 1;
}

/* ---- [case SCHUR-1] 旋转矩阵:纯虚共轭对,复向量列 NULL ---- */
static void schur_case_rotation(sch_tally_t *t) {
    double m[4] = { 0.0, -1.0, 1.0, 0.0 };
    double ere[2], eim[2];
    char detail[128];
    int rc;

    printf("[case SCHUR-1] rotation matrix (pure imaginary pair)\n");
    rc = ac_pro_schur_all(m, 2);
    sprintf(detail, "rc=%d", rc);
    scheck(t, rc == 1, "rotation: rc == 1", detail);
    scheck(t, ac_pro_schur_count() == 2, "rotation: count == 2", "");
    ere[0] = 0.0; eim[0] = 1.0;
    ere[1] = 0.0; eim[1] = -1.0;
    scheck(t, sch_spectrum_match(2, 2, ere, eim, 1e-9),
           "rotation: spectrum {+i, -i}", "");
    sprintf(detail, "vector(0)=%p vector(1)=%p",
            (const void *)ac_pro_schur_vector(0),
            (const void *)ac_pro_schur_vector(1));
    scheck(t, ac_pro_schur_vector(0) == 0 && ac_pro_schur_vector(1) == 0,
           "rotation: complex eigenvectors are NULL", detail);
}

/* ---- [case SCHUR-2] 对角阵:|λ| 降序逐位(3 > 2 > 1,序确定) ---- */
static void schur_case_diag(sch_tally_t *t) {
    double m[9] = { 1.0, 0.0, 0.0,
                    0.0, 2.0, 0.0,
                    0.0, 0.0, 3.0 };
    int rc;

    printf("[case SCHUR-2] diagonal (deterministic |lambda| desc order)\n");
    rc = ac_pro_schur_all(m, 3);
    scheck(t, rc == 1 && ac_pro_schur_count() == 3, "diag: rc/count", "");
    scheck(t, sch_close(ac_pro_schur_re(0), 3.0, 1e-9) &&
              sch_close(ac_pro_schur_re(1), 2.0, 1e-9) &&
              sch_close(ac_pro_schur_re(2), 1.0, 1e-9),
           "diag: 3, 2, 1 (|lambda| desc)", "");
    scheck(t, ac_pro_schur_im(0) == 0.0 && ac_pro_schur_im(1) == 0.0 &&
              ac_pro_schur_im(2) == 0.0, "diag: all imaginary parts 0", "");
    scheck(t, sch_vec_resid(m, 3, 0) <= 1e-9 &&
              sch_vec_resid(m, 3, 1) <= 1e-9 &&
              sch_vec_resid(m, 3, 2) <= 1e-9,
           "diag: vector residuals <= 1e-9", "");
}

/* ---- [case SCHUR-3] 非对称三角阵(实特征值,降序) ---- */
static void schur_case_asym(sch_tally_t *t) {
    double m[4] = { 1.0, 1.0,
                    0.0, 2.0 };
    int rc;

    printf("[case SCHUR-3] asymmetric triangular (real, desc)\n");
    rc = ac_pro_schur_all(m, 2);
    scheck(t, rc == 1 && ac_pro_schur_count() == 2, "asym: rc/count", "");
    scheck(t, sch_close(ac_pro_schur_re(0), 2.0, 1e-9) &&
              sch_close(ac_pro_schur_re(1), 1.0, 1e-9),
           "asym: 2 then 1", "");
    scheck(t, sch_vec_resid(m, 2, 0) <= 1e-9 && sch_vec_resid(m, 2, 1) <= 1e-9,
           "asym: vector residuals <= 1e-9", "");
}

/* ---- [case SCHUR-4] 混合谱:实 3 与 ±i 块(块对角) ---- */
static void schur_case_mixed(sch_tally_t *t) {
    double m[9] = { 0.0, -1.0, 0.0,
                    1.0,  0.0, 0.0,
                    0.0,  0.0, 3.0 };
    double ere[3], eim[3];
    int rc;

    printf("[case SCHUR-4] mixed real + complex pair\n");
    rc = ac_pro_schur_all(m, 3);
    scheck(t, rc == 1 && ac_pro_schur_count() == 3, "mixed: rc/count", "");
    ere[0] = 3.0;  eim[0] = 0.0;
    ere[1] = 0.0;  eim[1] = 1.0;
    ere[2] = 0.0;  eim[2] = -1.0;
    scheck(t, sch_spectrum_match(3, 3, ere, eim, 1e-9),
           "mixed: spectrum {3, +i, -i}", "");
    scheck(t, sch_close(ac_pro_schur_re(0), 3.0, 1e-9),
           "mixed: |lambda|=3 first (order pinned)", "");
    scheck(t, sch_vec_resid(m, 3, 0) <= 1e-9, "mixed: real vector residual", "");
    scheck(t, ac_pro_schur_vector(1) == 0, "mixed: +i has no vector", "");
}

/* ---- [case SCHUR-5] x³−1 的友矩阵:1 与 (−1±i√3)/2 ---- */
static void schur_case_companion3(sch_tally_t *t) {
    double m[9];
    double ere[3], eim[3];
    int i;
    int rc;

    printf("[case SCHUR-5] companion of x^3-1 (cube roots of unity)\n");
    for (i = 0; i < 9; i++) { m[i] = 0.0; }
    m[3] = 1.0;               /* 次对角:C[1][0] = C[2][1] = 1 */
    m[7] = 1.0;
    m[2] = 1.0;               /* 末列首行:-c_0 = -(-1) = 1 */
    rc = ac_pro_schur_all(m, 3);
    scheck(t, rc == 1 && ac_pro_schur_count() == 3, "comp3: rc/count", "");
    ere[0] = 1.0;              eim[0] = 0.0;
    ere[1] = -0.5;             eim[1] = 0.8660254037844386;
    ere[2] = -0.5;             eim[2] = -0.8660254037844386;
    scheck(t, sch_spectrum_match(3, 3, ere, eim, 1e-9),
           "comp3: spectrum {1, -1/2±i√3/2}", "");
    /* 共轭对相邻且 +im 在前(|λ| 并列的单元按值比较,次序不预设:
       复对可能排在实根 1 之前或之后,故扫描找相邻对) */
    {
        int pair_ok = 0;
        int k;
        for (k = 0; k + 1 < ac_pro_schur_count(); k++) {
            if (ac_pro_schur_im(k) > 0.0 && ac_pro_schur_im(k + 1) < 0.0 &&
                sch_close(ac_pro_schur_re(k), ac_pro_schur_re(k + 1), 1e-9) &&
                sch_close(fabs(ac_pro_schur_im(k)),
                          fabs(ac_pro_schur_im(k + 1)), 1e-9)) {
                pair_ok = 1;
                break;
            }
        }
        scheck(t, pair_ok, "comp3: conjugate pair adjacent, +Im first", "");
    }
}

/* ---- [case SCHUR-6] 简并:diag(2,2,2),三条特征向量均过残差闸 ---- */
static void schur_case_degenerate(sch_tally_t *t) {
    double m[9] = { 2.0, 0.0, 0.0,
                    0.0, 2.0, 0.0,
                    0.0, 0.0, 2.0 };
    int rc;

    printf("[case SCHUR-6] degenerate diag(2,2,2)\n");
    rc = ac_pro_schur_all(m, 3);
    scheck(t, rc == 1 && ac_pro_schur_count() == 3, "degen: rc/count", "");
    scheck(t, sch_close(ac_pro_schur_re(0), 2.0, 1e-9) &&
              sch_close(ac_pro_schur_re(1), 2.0, 1e-9) &&
              sch_close(ac_pro_schur_re(2), 2.0, 1e-9),
           "degen: all eigenvalues 2", "");
    scheck(t, sch_vec_resid(m, 3, 0) <= 1e-9 &&
              sch_vec_resid(m, 3, 1) <= 1e-9 &&
              sch_vec_resid(m, 3, 2) <= 1e-9,
           "degen: all vector residuals <= 1e-9", "");
}

/* ---- [case SCHUR-7] 亏损(Jordan 块 [[2,1],[0,2]]):几何重数 1,
 *     两条特征向量同向(诚实,不做正交化修饰) ---- */
static void schur_case_defective(sch_tally_t *t) {
    double m[4] = { 2.0, 1.0,
                    0.0, 2.0 };
    const double *v0;
    const double *v1;
    double dot;
    int rc;

    printf("[case SCHUR-7] defective Jordan block\n");
    rc = ac_pro_schur_all(m, 2);
    scheck(t, rc == 1 && ac_pro_schur_count() == 2, "defect: rc/count", "");
    scheck(t, sch_close(ac_pro_schur_re(0), 2.0, 1e-9) &&
              sch_close(ac_pro_schur_re(1), 2.0, 1e-9),
           "defect: both eigenvalues 2", "");
    scheck(t, sch_vec_resid(m, 2, 0) <= 1e-9 && sch_vec_resid(m, 2, 1) <= 1e-9,
           "defect: both vector residuals <= 1e-9", "");
    v0 = ac_pro_schur_vector(0);
    v1 = ac_pro_schur_vector(1);
    dot = v0[0] * v1[0] + v0[1] * v1[1];
    scheck(t, sch_close(fabs(dot), 1.0, 1e-9),
           "defect: both vectors along the same 1-dim eigenspace", "");
}

/* ---- [case SCHUR-8] 4×4 两个复共轭对(e^{±i60°}, e^{±i90°}) ---- */
static void schur_case_twopairs(sch_tally_t *t) {
    double s60 = 0.8660254037844386;
    double m[16] = { 0.0, -1.0, 0.0, 0.0,
                     1.0,  0.0, 0.0, 0.0,
                     0.0,  0.0, 0.5, -s60,
                     0.0,  0.0, s60, 0.5 };
    double ere[4], eim[4];
    int rc;

    printf("[case SCHUR-8] two complex conjugate pairs\n");
    rc = ac_pro_schur_all(m, 4);
    scheck(t, rc == 1 && ac_pro_schur_count() == 4, "2pairs: rc/count", "");
    ere[0] = 0.5; eim[0] = s60;
    ere[1] = 0.5; eim[1] = -s60;
    ere[2] = 0.0; eim[2] = 1.0;
    ere[3] = 0.0; eim[3] = -1.0;
    scheck(t, sch_spectrum_match(4, 4, ere, eim, 1e-9),
           "2pairs: spectrum {e^(±i60°), e^(±i90°)}", "");
    scheck(t, ac_pro_schur_vector(0) == 0 && ac_pro_schur_vector(1) == 0 &&
              ac_pro_schur_vector(2) == 0 && ac_pro_schur_vector(3) == 0,
           "2pairs: all complex, all vectors NULL", "");
}

/* ---- [case SCHUR-9] n = 1 边界 ---- */
static void schur_case_n1(sch_tally_t *t) {
    double m[1] = { 5.0 };
    const double *v;
    int rc;

    printf("[case SCHUR-9] n=1 boundary\n");
    rc = ac_pro_schur_all(m, 1);
    scheck(t, rc == 1 && ac_pro_schur_count() == 1, "n1: rc/count", "");
    scheck(t, sch_close(ac_pro_schur_re(0), 5.0, 0.0) &&
              ac_pro_schur_im(0) == 0.0, "n1: eigenvalue exactly 5", "");
    v = ac_pro_schur_vector(0);
    scheck(t, v != 0 && sch_close(fabs(v[0]), 1.0, 1e-12),
           "n1: unit eigenvector", "");
    scheck(t, sch_vec_resid(m, 1, 0) <= 1e-12, "n1: residual 0", "");
}

/* ---- [case SCHUR-10] 12×12 友矩阵(x^12−1):单位圆上 12 个根 ---- */
static void schur_case_c12(sch_tally_t *t) {
    double m[144];
    double ang[12];
    double ere[12], eim[12];
    int i, rc;
    int nreal = 0, ncplx = 0;
    double maxerr = 0.0;

    printf("[case SCHUR-10] 12x12 companion of x^12-1\n");
    for (i = 0; i < 144; i++) { m[i] = 0.0; }
    for (i = 0; i < 11; i++) { m[(i + 1) * 12 + i] = 1.0; }
    m[11] = 1.0;               /* C[0][11] = -c_0 = 1 */
    rc = ac_pro_schur_all(m, 12);
    scheck(t, rc == 1 && ac_pro_schur_count() == 12, "c12: rc/count", "");
    for (i = 0; i < 12; i++) {
        double mod = sqrt(ac_pro_schur_re(i) * ac_pro_schur_re(i)
                        + ac_pro_schur_im(i) * ac_pro_schur_im(i));
        double d = fabs(mod - 1.0);
        if (d > maxerr) { maxerr = d; }
        if (ac_pro_schur_im(i) == 0.0) { nreal++; }
        else { ncplx++; }
        ang[i] = 6.283185307179586 * (double)i / 12.0;
        ere[i] = cos(ang[i]);
        eim[i] = sin(ang[i]);
    }
    scheck(t, nreal == 2 && ncplx == 10,
           "c12: 2 real (+-1) + 10 complex", "");
    scheck(t, maxerr <= 1e-9, "c12: all |lambda| within 1e-9 of 1", "");
    scheck(t, sch_spectrum_match(12, 12, ere, eim, 1e-9),
           "c12: spectrum == 12th roots of unity", "");
}

/* ---- [case SCHUR-11] 对称矩阵与 A5 交叉对拍 ---- */
static void schur_case_symmetric(sch_tally_t *t) {
    double m[9] = { 2.0, 1.0, 0.0,
                    1.0, 2.0, 1.0,
                    0.0, 1.0, 2.0 };
    double expect[3];
    char detail[256];
    int i, rc5, rcs;
    int matched = 1;

    printf("[case SCHUR-11] symmetric matrix cross-checked with A5\n");
    rc5 = ac_pro_eig_all(m, 3);
    /* 视图持有纪律(ac_pro_scratch.h 头注):eig 与 schur 是同一 union 的
     * 两个视图,ac_pro_schur_all 会整块覆写 eig 结果(含 a[0] 的 count
     * 停泊位)—— A5 的特征值必须在调 schur 之前拷贝到栈上。 */
    for (i = 0; i < 3; i++) { expect[i] = ac_pro_eig_value(i); }
    rcs = ac_pro_schur_all(m, 3);
    scheck(t, rc5 == 1 && rcs == 1, "sym: both pipelines succeed", "");
    /* 两侧都按 |λ| 降序;本例 |λ| 互异(0.586 / 2 / 3.414)→ 逐位应一致 */
    for (i = 0; i < 3; i++) {
        if (!sch_close(ac_pro_schur_re(i), expect[i], 1e-7)) { matched = 0; }
    }
    sprintf(detail, "schur=%.12g,%.12g,%.12g a5=%.12g,%.12g,%.12g",
            ac_pro_schur_re(0), ac_pro_schur_re(1), ac_pro_schur_re(2),
            expect[0], expect[1], expect[2]);
    scheck(t, matched, "sym: schur eigenvalues match A5 within 1e-7", detail);
    scheck(t, sch_vec_resid(m, 3, 0) <= 1e-9 &&
              sch_vec_resid(m, 3, 1) <= 1e-9 &&
              sch_vec_resid(m, 3, 2) <= 1e-9,
           "sym: vector residuals <= 1e-9", "");
}

/* ---- [case SCHUR-12] 参数拒绝(n 越界 / 空指针),count 归零 ---- */
static void schur_case_reject(sch_tally_t *t) {
    double m[4] = { 1.0, 0.0, 0.0, 2.0 };
    char detail[64];
    int rc;

    printf("[case SCHUR-12] argument rejections\n");
    rc = ac_pro_schur_all(m, 0);
    sprintf(detail, "rc=%d", rc);
    scheck(t, rc == -1 && ac_pro_schur_count() == 0, "n=0 -> -1, count 0", detail);
    rc = ac_pro_schur_all(m, 13);
    scheck(t, rc == -1 && ac_pro_schur_count() == 0, "n=13 -> -1, count 0", "");
    rc = ac_pro_schur_all(0, 2);
    scheck(t, rc == -1, "NULL matrix -> -1", "");
}

/* ---- [case SCHUR-13] 实特征向量残差全查(4×4 混合块对角) ---- */
static void schur_case_vecresid(sch_tally_t *t) {
    double m[16] = { 3.0, 1.0,  0.0, 0.0,
                     0.5, 1.0,  0.0, 0.0,
                     0.0, 0.0,  0.0, -2.0,
                     0.0, 0.0,  2.0,  0.0 };
    double ere[4], eim[4];
    int rc;
    int i;
    int all_ok = 1;

    printf("[case SCHUR-13] real eigenvector residuals (mixed 4x4)\n");
    rc = ac_pro_schur_all(m, 4);
    scheck(t, rc == 1 && ac_pro_schur_count() == 4, "resid: rc/count", "");
    /* 谱:2×2 非对称块 [[3,1],[0.5,1]] 的特征值 2±√1.5,旋转 2 倍块 ±2i */
    ere[0] = 2.0 + 1.224744871391589; eim[0] = 0.0;
    ere[1] = 2.0 - 1.224744871391589; eim[1] = 0.0;
    ere[2] = 0.0; eim[2] = 2.0;
    ere[3] = 0.0; eim[3] = -2.0;
    scheck(t, sch_spectrum_match(4, 4, ere, eim, 1e-9),
           "resid: spectrum {2±√1.5, ±2i}", "");
    for (i = 0; i < 4; i++) {
        if (ac_pro_schur_im(i) == 0.0) {
            double r = sch_vec_resid(m, 4, i);
            if (!(r >= 0.0 && r <= 1e-8)) { all_ok = 0; }
        } else if (ac_pro_schur_vector(i) != 0) {
            all_ok = 0;
        }
    }
    scheck(t, all_ok, "resid: real vectors pass, complex are NULL", "");
}

/* ---- 测试入口(runner 调用) ---- */
sch_tally_t test_schur_basic(void) {
    sch_tally_t t;
    t.checks = 0; t.failed = 0;
    schur_case_rotation(&t);
    schur_case_diag(&t);
    schur_case_asym(&t);
    return t;
}

sch_tally_t test_schur_complex(void) {
    sch_tally_t t;
    t.checks = 0; t.failed = 0;
    schur_case_mixed(&t);
    schur_case_companion3(&t);
    return t;
}

sch_tally_t test_schur_degenerate(void) {
    sch_tally_t t;
    t.checks = 0; t.failed = 0;
    schur_case_degenerate(&t);
    schur_case_defective(&t);
    schur_case_twopairs(&t);
    return t;
}

sch_tally_t test_schur_large(void) {
    sch_tally_t t;
    t.checks = 0; t.failed = 0;
    schur_case_n1(&t);
    schur_case_c12(&t);
    schur_case_symmetric(&t);
    return t;
}

sch_tally_t test_schur_reject(void) {
    sch_tally_t t;
    t.checks = 0; t.failed = 0;
    schur_case_reject(&t);
    schur_case_vecresid(&t);
    return t;
}

#endif /* AC_PRO_ACTIVE */
