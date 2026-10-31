/* ApolloCalc · test_pro_eigen.c — 实对称矩阵全部特征对的 A5 单元测试

   计数约定与 test_pro_multi.c 一致但不共享类型：本文件自己定义
   pro_tally_t，runner 只声明同名入口并按同构结构体读取，两个 TU 之间没有
   类型依赖，可单独编译、单独跑。

   本文件只有 static 函数（落 .text），不得有静态对象，链接后 runner 口径
   的 .bss 增量必须为 0。矩阵与向量一律是函数作用域的栈上数组。

   每条用例覆盖五个维度：返回码与特征值个数；特征值数值（对拍
   numpy.linalg.eigh 的 17 位参考值）；‖A·v − λ·v‖∞；‖(A − λI)·v‖∞（与
   前者数学等价，但独立算一遍，防止实现把 λ 和 v 从同一个错位的式子里
   两头凑出来）；单位 2-范数 + 两两正交（λ_jv_jv_jᵀ 只有在此前提下才是
   谱投影）。参考值用 %.17g 打印，失败时能看出差在第几位。 */

#include <stdio.h>
#include <math.h>

#include "../src/ac_pro.h"

#if AC_PRO_ACTIVE

typedef struct {
    int checks;
    int failed;
} pro_tally_t;

static void pcheck(pro_tally_t *t, int cond, const char *name, const char *detail) {
    t->checks++;
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        t->failed++;
        printf("  [FAIL] %s  (%s)\n", name, detail);
    }
}

static double tnorm2(const double *v, int n) {
    double s = 0.0;
    int i;
    for (i = 0; i < n; ++i) s += v[i] * v[i];
    return sqrt(s);
}

static double tdot(const double *x, const double *y, int n) {
    double s = 0.0;
    int i;
    for (i = 0; i < n; ++i) s += x[i] * y[i];
    return s;
}

/* ‖A·v − λ·v‖∞ */
static double tresid(const double *a, const double *v, double lam, int n) {
    double best = 0.0;
    int i, j;
    for (i = 0; i < n; ++i) {
        double s = 0.0;
        for (j = 0; j < n; ++j) s += a[i * n + j] * v[j];
        s = fabs(s - lam * v[i]);
        if (s > best) best = s;
    }
    return best;
}

/* ‖(A − λI)·v‖∞，不复用 tresid 的中间量 */
static double tnullresid(const double *a, const double *v, double lam, int n) {
    double best = 0.0;
    int i, j;
    for (i = 0; i < n; ++i) {
        double s = 0.0;
        for (j = 0; j < n; ++j) {
            double m = a[i * n + j];
            if (i == j) m -= lam;
            s += m * v[j];
        }
        s = fabs(s);
        if (s > best) best = s;
    }
    return best;
}

/* 所有 |v_i·v_j|（i<j）的最大值 */
static double tmaxdot(int cnt, int n) {
    double best = 0.0;
    int i, j;
    for (i = 0; i < cnt; ++i)
        for (j = i + 1; j < cnt; ++j) {
            double d = fabs(tdot(ac_pro_eig_vector(i), ac_pro_eig_vector(j), n));
            if (d > best) best = d;
        }
    return best;
}

/* 所有 |‖v_i‖ − 1| 的最大值 */
static double tmaxnormdev(int cnt, int n) {
    double best = 0.0;
    int i;
    for (i = 0; i < cnt; ++i) {
        double d = fabs(tnorm2(ac_pro_eig_vector(i), n) - 1.0);
        if (d > best) best = d;
    }
    return best;
}

/* 实测 λ 与期望集合逐个对拍（实测已按 |λ| 降序），返回最大偏差；
   个数不等直接返回 1e9。 */
static double tmaxlameff(int cnt, const double *want, int nwant) {
    double best = 0.0;
    int i;
    if (cnt != nwant) return 1e9;
    for (i = 0; i < nwant; ++i) {
        double d = fabs(ac_pro_eig_value(i) - want[i]);
        if (d > best) best = d;
    }
    return best;
}

/* A = [[2,1],[1,2]] → λ = 3, 1；v = (1,1)/√2, (1,−1)/√2
 * 能手工验证的最小完整用例，钉住「列主序 + |λ| 降序」两条约定。 */
pro_tally_t test_eigen_2x2(void) {
    pro_tally_t t;
    const double a[4] = { 2.0, 1.0,
                          1.0, 2.0 };
    double want[2];
    double v0[2], v1[2];
    int rc, cnt;
    const double *p;

    t.checks = 0;
    t.failed = 0;

    printf("[case A5-1] 2x2 [[2,1],[1,2]] -> lambda = 3, 1\n");
    rc = ac_pro_eig_all(a, 2);
    pcheck(&t, rc == 1, "returns 1", "ac_pro_eig_all did not succeed");
    cnt = ac_pro_eig_count();
    pcheck(&t, cnt == 2, "count == 2", "wrong eigenvalue count");

    want[0] = 3.0;
    want[1] = 1.0;
    pcheck(&t, tmaxlameff(cnt, want, 2) < 1e-10,
           "lambda = {3, 1}", "eigenvalue mismatch");
    printf("         lambda: %.17g %.17g\n",
           ac_pro_eig_value(0), ac_pro_eig_value(1));

    p = ac_pro_eig_vector(0);
    v0[0] = p[0]; v0[1] = p[1];
    p = ac_pro_eig_vector(1);
    v1[0] = p[0]; v1[1] = p[1];

    pcheck(&t, fabs(tnorm2(v0, 2) - 1.0) < 1e-12, "|v0| == 1", "v0 not unit");
    pcheck(&t, fabs(tnorm2(v1, 2) - 1.0) < 1e-12, "|v1| == 1", "v1 not unit");
    pcheck(&t, fabs(tdot(v0, v1, 2)) < 1e-12, "v0 . v1 == 0", "not orthogonal");
    pcheck(&t, fabs(fabs(v0[0]) - fabs(v0[1])) < 1e-10,
           "v0 parallel to (1,1)", "v0 is not the (1,1) direction");
    pcheck(&t, fabs(fabs(v1[0]) - fabs(v1[1])) < 1e-10,
           "v1 parallel to (1,-1)", "v1 is not the (1,-1) direction");

    pcheck(&t, tresid(a, v0, ac_pro_eig_value(0), 2) < 1e-12,
           "|A v0 - l0 v0| < 1e-12", "residual too large");
    pcheck(&t, tresid(a, v1, ac_pro_eig_value(1), 2) < 1e-12,
           "|A v1 - l1 v1| < 1e-12", "residual too large");
    pcheck(&t, tnullresid(a, v0, ac_pro_eig_value(0), 2) < 1e-12,
           "|(A - l0 I) v0| < 1e-12", "null-space residual too large");
    pcheck(&t, tnullresid(a, v1, ac_pro_eig_value(1), 2) < 1e-12,
           "|(A - l1 I) v1| < 1e-12", "null-space residual too large");
    printf("         v0 = (%.17g, %.17g)  v1 = (%.17g, %.17g)\n",
           v0[0], v0[1], v1[0], v1[1]);

    return t;
}

/* A = [[4,1,2],[1,3,0],[2,0,5]]
 *   λ = 6.669079088282288, 3.476023602918134, 1.854897308799577
 * 覆盖 n = 3 的完整 deflation 链（3 轮）与排序。 */
pro_tally_t test_eigen_3x3(void) {
    pro_tally_t t;
    const double a[9] = { 4.0, 1.0, 2.0,
                          1.0, 3.0, 0.0,
                          2.0, 0.0, 5.0 };
    double want[3];
    double v[3];
    int rc, cnt, i;

    t.checks = 0;
    t.failed = 0;

    printf("[case A5-2] 3x3 [[4,1,2],[1,3,0],[2,0,5]] -> 3 eigenvalues\n");
    rc = ac_pro_eig_all(a, 3);
    pcheck(&t, rc == 1, "returns 1", "ac_pro_eig_all did not succeed");
    cnt = ac_pro_eig_count();
    pcheck(&t, cnt == 3, "count == 3", "wrong eigenvalue count");

    want[0] = 6.669079088282288;
    want[1] = 3.476023602918134;
    want[2] = 1.854897308799577;
    pcheck(&t, tmaxlameff(cnt, want, 3) < 1e-9,
           "lambda matches eigh reference (|d| < 1e-9)", "eigenvalue mismatch");
    printf("         lambda: %.17g %.17g %.17g\n",
           ac_pro_eig_value(0), ac_pro_eig_value(1), ac_pro_eig_value(2));

    pcheck(&t, tmaxnormdev(cnt, 3) < 1e-12,
           "all |v_i| == 1", "not unit 2-norm");
    pcheck(&t, tmaxdot(cnt, 3) < 1e-12,
           "all v_i . v_j == 0 (i<j)", "eigenvectors not orthogonal");

    for (i = 0; i < 3; ++i) {
        const double *p = ac_pro_eig_vector(i);
        double lam = ac_pro_eig_value(i);
        v[0] = p[0]; v[1] = p[1]; v[2] = p[2];
        pcheck(&t, tresid(a, v, lam, 3) < 1e-9,
               "|A v - l v| < 1e-9", "residual too large");
        pcheck(&t, tnullresid(a, v, lam, 3) < 1e-9,
               "|(A - l I) v| < 1e-9", "null-space residual too large");
    }

    /* 迹不变：Σλ = tr(A) = 4 + 3 + 5 = 12 */
    {
        double sum = 0.0;
        for (i = 0; i < cnt; ++i) sum += ac_pro_eig_value(i);
        pcheck(&t, fabs(sum - 12.0) < 1e-9, "sum(lambda) == trace(A) == 12",
               "trace mismatch");
        printf("         sum(lambda) = %.17g\n", sum);
    }

    return t;
}

/* A = diag(2, 2, 1)，λ = 2 是二重根。
 * 重根最容易翻车：第一轮收缩把 λ=2 压成 0 后，第二个 λ=2 仍在谱里
 * （收缩只压掉那一个方向），必须还能被找出来；两个重根向量也必须正交，
 * 否则 λ_jv_jv_jᵀ 不再是投影。 */
pro_tally_t test_eigen_repeated(void) {
    pro_tally_t t;
    const double a[9] = { 2.0, 0.0, 0.0,
                          0.0, 2.0, 0.0,
                          0.0, 0.0, 1.0 };
    double v[3];
    int rc, cnt, i;

    t.checks = 0;
    t.failed = 0;

    printf("[case A5-3] diag(2,2,1) -> repeated eigenvalue lambda = 2 (x2)\n");
    rc = ac_pro_eig_all(a, 3);
    pcheck(&t, rc == 1, "returns 1", "ac_pro_eig_all did not succeed");
    cnt = ac_pro_eig_count();
    pcheck(&t, cnt == 3, "count == 3", "wrong eigenvalue count");

    pcheck(&t, fabs(ac_pro_eig_value(0) - 2.0) < 1e-12,
           "lambda[0] == 2", "first repeated eigenvalue wrong");
    pcheck(&t, fabs(ac_pro_eig_value(1) - 2.0) < 1e-12,
           "lambda[1] == 2 (deflation kept the second copy)",
           "second repeated eigenvalue lost by deflation");
    pcheck(&t, fabs(ac_pro_eig_value(2) - 1.0) < 1e-12,
           "lambda[2] == 1", "third eigenvalue wrong");
    printf("         lambda: %.17g %.17g %.17g\n",
           ac_pro_eig_value(0), ac_pro_eig_value(1), ac_pro_eig_value(2));

    pcheck(&t, tmaxnormdev(cnt, 3) < 1e-12,
           "all |v_i| == 1", "not unit 2-norm");
    pcheck(&t, tmaxdot(cnt, 3) < 1e-12,
           "repeated eigenvectors are orthogonal", "degenerate basis not orthogonal");

    for (i = 0; i < 3; ++i) {
        const double *p = ac_pro_eig_vector(i);
        double lam = ac_pro_eig_value(i);
        v[0] = p[0]; v[1] = p[1]; v[2] = p[2];
        pcheck(&t, tresid(a, v, lam, 3) < 1e-12,
               "|A v - l v| < 1e-12", "residual too large");
        pcheck(&t, tnullresid(a, v, lam, 3) < 1e-12,
               "|(A - l I) v| < 1e-12", "null-space residual too large");
    }

    return t;
}

/* H[i][j] = 1/(i+j+1)，cond(H) ≈ 5.2e5。
 *   λ = 1.4083189271236538, 0.12232706585390568, 0.00268734035577355
 * λ3/λ1 ≈ 0.0019 对幂迭代容易，难的是 deflation 的舍入：第一轮收缩的
 * 1e-16 级噪声落到 λ3 上就是 4e-14 的相对扰动，向量角度误差又被
 * gap(λ2,λ3) 放大。阈值放宽到 1e-8，仍远小于真实实现错误的量级。 */
pro_tally_t test_eigen_illconditioned(void) {
    pro_tally_t t;
    double a[9];
    double want[3];
    double v[3];
    int rc, cnt, i, j;

    t.checks = 0;
    t.failed = 0;

    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
            a[i * 3 + j] = 1.0 / (double)(i + j + 1);

    printf("[case A5-4] Hilbert 3x3 (cond ~ 5.2e5)\n");
    rc = ac_pro_eig_all(a, 3);
    pcheck(&t, rc == 1, "returns 1", "ac_pro_eig_all did not succeed");
    cnt = ac_pro_eig_count();
    pcheck(&t, cnt == 3, "count == 3", "wrong eigenvalue count");

    want[0] = 1.4083189271236538;
    want[1] = 0.12232706585390568;
    want[2] = 0.00268734035577355;
    pcheck(&t, tmaxlameff(cnt, want, 3) < 1e-9,
           "lambda matches eigh reference (|d| < 1e-9)", "eigenvalue mismatch");
    printf("         lambda: %.17g %.17g %.17g\n",
           ac_pro_eig_value(0), ac_pro_eig_value(1), ac_pro_eig_value(2));

    pcheck(&t, tmaxnormdev(cnt, 3) < 1e-12,
           "all |v_i| == 1", "not unit 2-norm");
    pcheck(&t, tmaxdot(cnt, 3) < 1e-10,
           "all v_i . v_j == 0 (i<j)", "eigenvectors not orthogonal");

    for (i = 0; i < 3; ++i) {
        const double *p = ac_pro_eig_vector(i);
        double lam = ac_pro_eig_value(i);
        v[0] = p[0]; v[1] = p[1]; v[2] = p[2];
        pcheck(&t, tresid(a, v, lam, 3) < 1e-8,
               "|A v - l v| < 1e-8", "residual too large on ill-conditioned input");
        pcheck(&t, tnullresid(a, v, lam, 3) < 1e-8,
               "|(A - l I) v| < 1e-8", "null-space residual too large");
    }

    /* 迹不变：Σλ = tr(H) = 1 + 1/3 + 1/5 */
    {
        double sum = 0.0;
        for (i = 0; i < cnt; ++i) sum += ac_pro_eig_value(i);
        pcheck(&t, fabs(sum - (1.0 + 1.0 / 3.0 + 1.0 / 5.0)) < 1e-9,
               "sum(lambda) == trace(H)", "trace mismatch on Hilbert matrix");
        printf("         sum(lambda) = %.17g (trace = %.17g)\n",
               sum, 1.0 + 1.0 / 3.0 + 1.0 / 5.0);
    }

    return t;
}

/* A = [[4,1,0,1],[1,3,1,0],[0,1,2,1],[1,0,1,4]]
 *   λ = 5.51413692933529, 3.5719932683162035, 3.0, 0.9138698023485048
 *
 * 为什么这个矩阵能验 deflation：第 2、3 轮之间 |λ3|/|λ2| = 0.84，是幂迭代
 * 收敛最慢的一段，专门压住「deflated 主特征对没收敛就交卷」的实现；4 个 λ
 * 全互异且分离度不均，少扣一项或扣错一项会让 Σλ ≠ tr(A) = 13（收缩不改
 * 迹：tr(A − Σλ_jv_jv_jᵀ) = tr(A) − Σλ_j，因 ‖v_j‖ = 1）；「每轮从原始
 * const 矩阵重拷」与「常驻 deflated 矩阵」的经典做法结果一致。
 * 末尾补边界与错误码（-1 / -2 / -4，以及失败后 count 归零）。 */
pro_tally_t test_eigen_deflation(void) {
    pro_tally_t t;
    const double a[16] = { 4.0, 1.0, 0.0, 1.0,
                           1.0, 3.0, 1.0, 0.0,
                           0.0, 1.0, 2.0, 1.0,
                           1.0, 0.0, 1.0, 4.0 };
    double want[4];
    double v[4];
    int rc, cnt, i;

    t.checks = 0;
    t.failed = 0;

    printf("[case A5-5] 4x4 deflation chain, 4 eigenvalues\n");
    rc = ac_pro_eig_all(a, 4);
    pcheck(&t, rc == 1, "returns 1", "ac_pro_eig_all did not succeed");
    cnt = ac_pro_eig_count();
    pcheck(&t, cnt == 4, "count == 4", "wrong eigenvalue count");

    want[0] = 5.51413692933529;
    want[1] = 3.5719932683162035;
    want[2] = 3.0;
    want[3] = 0.9138698023485048;
    pcheck(&t, tmaxlameff(cnt, want, 4) < 1e-8,
           "all 4 lambda match eigh reference", "eigenvalue mismatch after deflation");
    printf("         lambda: %.17g %.17g %.17g %.17g\n",
           ac_pro_eig_value(0), ac_pro_eig_value(1),
           ac_pro_eig_value(2), ac_pro_eig_value(3));

    /* deflation 不改迹：Σλ = tr(A) = 4+3+2+4 = 13 */
    {
        double sum = 0.0;
        for (i = 0; i < cnt; ++i) sum += ac_pro_eig_value(i);
        pcheck(&t, fabs(sum - 13.0) < 1e-8,
               "sum(lambda) == trace(A) == 13 (deflation preserves trace)",
               "trace changed -> deflation subtracted the wrong amount");
        printf("         sum(lambda) = %.17g\n", sum);
    }

    pcheck(&t, tmaxnormdev(cnt, 4) < 1e-12,
           "all |v_i| == 1 (required by Hotelling)", "not unit 2-norm");
    pcheck(&t, tmaxdot(cnt, 4) < 1e-10,
           "all v_i . v_j == 0 (i<j)", "eigenvectors not orthogonal");

    for (i = 0; i < 4; ++i) {
        const double *p = ac_pro_eig_vector(i);
        double lam = ac_pro_eig_value(i);
        v[0] = p[0]; v[1] = p[1]; v[2] = p[2]; v[3] = p[3];
        pcheck(&t, tresid(a, v, lam, 4) < 1e-8,
               "|A v - l v| < 1e-8", "residual too large after deflation");
        pcheck(&t, tnullresid(a, v, lam, 4) < 1e-8,
               "|(A - l I) v| < 1e-8", "null-space residual too large");
    }

    printf("[case A5-6] bounds and error codes\n");

    rc = ac_pro_eig_all(a, 0);
    pcheck(&t, rc == -1, "n = 0 -> -1", "wrong code for n = 0");
    pcheck(&t, ac_pro_eig_count() == 0, "count == 0 after failed call",
           "stale count leaked from a previous call");

    rc = ac_pro_eig_all(a, 13);
    pcheck(&t, rc == -1, "n = 13 -> -1 (max dim is 12)", "wrong code for n = 13");

    rc = ac_pro_eig_all((const double *)0, 2);
    pcheck(&t, rc == -1, "NULL matrix -> -1", "NULL not rejected");

    {
        /* 非对称 [[1,2],[0,1]]：适用范围外，必须 -2 而不是硬算 */
        const double ns[4] = { 1.0, 2.0, 0.0, 1.0 };
        rc = ac_pro_eig_all(ns, 2);
        pcheck(&t, rc == -2, "non-symmetric -> -2", "non-symmetric input accepted");
    }

    {
        /* 零矩阵：没有可归一化的特征向量 → -4 */
        const double z[9] = { 0.0, 0.0, 0.0,
                              0.0, 0.0, 0.0,
                              0.0, 0.0, 0.0 };
        rc = ac_pro_eig_all(z, 3);
        pcheck(&t, rc == -4, "zero matrix -> -4", "zero matrix accepted");
        pcheck(&t, ac_pro_eig_count() == 0, "count == 0 after zero matrix",
               "count not reset");
    }

    /* 越界读取：count 归零后 value/vector 必须给 0 / NULL，而不是越界指针 */
    pcheck(&t, ac_pro_eig_value(-1) == 0.0, "value(-1) == 0", "negative index not guarded");
    pcheck(&t, ac_pro_eig_value(99) == 0.0, "value(99) == 0", "large index not guarded");
    pcheck(&t, ac_pro_eig_vector(-1) == (const double *)0,
           "vector(-1) == NULL", "negative index not guarded");
    pcheck(&t, ac_pro_eig_vector(99) == (const double *)0,
           "vector(99) == NULL", "large index not guarded");

    /* n = 1：最小合法输入，λ = 5、v = (1) */
    {
        const double one[1] = { 5.0 };
        rc = ac_pro_eig_all(one, 1);
        pcheck(&t, rc == 1 && ac_pro_eig_count() == 1,
               "n = 1 works (lambda = 5)", "n = 1 rejected");
        pcheck(&t, fabs(ac_pro_eig_value(0) - 5.0) < 1e-12,
               "n = 1 lambda == 5", "wrong eigenvalue for 1x1");
    }

    return t;
}

#endif /* AC_PRO_ACTIVE */
