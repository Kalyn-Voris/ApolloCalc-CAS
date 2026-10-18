/* ============================================================
 * ApolloCalc-CAS · test_matrix.c — 矩阵扩展测试（C 128KB 第 7 轮，C 独有）
 * 无 JS 圣经（JS 128KB 无完整矩阵层），测试基准为数学事实：
 *   det([6 1 1;4 -2 5;2 8 7]) = -306（经典例题，Node 核算）
 *   det 三对角 [2 1 0 0;1 3 1 0;0 1 2 1;0 0 1 1] = 3（递推 D4=3，Node 核算）
 *   [4 1;2 3] 主特征值 = 5（特征值 5 与 2，(1,1) 即优势特征向量）
 *   inverse([1 2;3 4]) = [-2 1;1.5 -0.5]（det = -2）
 * 覆盖：乘法（含矩形/恒等/零）、转置（含 (AB)ᵀ=BᵀAᵀ）、秩、行列式、
 *       逆（含回验 A·A⁻¹=I 与奇异拒绝）、幂法特征值、QR（回验 QR=A、
 *       QᵀQ=I、R 上三角）、别名安全。
 * ============================================================ */
#include <stdio.h>
#include <math.h>
#include "../src/ac_types.h"
#include "../src/ac_matrix.h"

static int s_pass = 0;
static int s_fail = 0;

static void expect_true(int cond, const char *name) {
    if (cond) { s_pass++; }
    else { s_fail++; printf("FAIL [matrix] %s\n", name); }
}

/* 矩阵逐元素校验（1 项测试 + 失败打印首个偏差） */
static void expect_mat(const double *got, const double *want, int rows, int cols,
                       double tol, const char *name) {
    int i;
    int ok = 1;
    for (i = 0; i < rows * cols && ok; i++) {
        double d = got[i] - want[i];
        if (d < 0.0) { d = -d; }
        if (d > tol) { ok = 0; }
    }
    if (ok) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [matrix] %s: got[0..]=%.9g,%.9g,%.9g,%.9g want[0..]=%.9g,%.9g,%.9g,%.9g\n",
               name, got[0], got[1], got[2], got[3], want[0], want[1], want[2], want[3]);
    }
}

int test_matrix_run(void) {
    /* ==================== Part A：乘法 ==================== */
    {
        static const double a[4] = {1, 2, 3, 4};
        static const double b[4] = {5, 6, 7, 8};
        static const double want[4] = {19, 22, 43, 50};
        ac_mat_mul(a, 2, 2, b, 2, 2, g_mat_c);
        expect_mat(g_mat_c, want, 2, 2, 1e-12, "mul [1 2;3 4]x[5 6;7 8]");
    }
    {
        /* 恒等：I×A = A = A×I（3×3） */
        static const double a[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        static const double e[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        ac_mat_mul(e, 3, 3, a, 3, 3, g_mat_c);
        expect_mat(g_mat_c, a, 3, 3, 1e-12, "I x A = A");
        ac_mat_mul(a, 3, 3, e, 3, 3, g_mat_c);
        expect_mat(g_mat_c, a, 3, 3, 1e-12, "A x I = A");
    }
    {
        /* 矩形：(2×3)×(3×2) = 2×2 */
        static const double a[6] = {1, 2, 3, 4, 5, 6};
        static const double b[6] = {7, 8, 9, 10, 11, 12};
        static const double want[4] = {58, 64, 139, 154};
        ac_mat_mul(a, 2, 3, b, 3, 2, g_mat_c);
        expect_mat(g_mat_c, want, 2, 2, 1e-12, "mul (2x3)(3x2)");
    }
    {
        /* 零矩阵乘 */
        static const double a[4] = {1, 2, 3, 4};
        static const double z[4] = {0, 0, 0, 0};
        static const double want[4] = {0, 0, 0, 0};
        ac_mat_mul(a, 2, 2, z, 2, 2, g_mat_c);
        expect_mat(g_mat_c, want, 2, 2, 1e-12, "mul A x 0");
    }
    {
        /* 结合律抽查：(AB)C = A(BC)（2×2 全 1 系数） */
        static const double a[4] = {1, 2, 3, 4};
        static const double b[4] = {5, 6, 7, 8};
        static const double c[4] = {9, 10, 11, 12};
        double ab[4], bc[4], abc1[4], abc2[4];
        ac_mat_mul(a, 2, 2, b, 2, 2, ab);
        ac_mat_mul(ab, 2, 2, c, 2, 2, abc1);
        ac_mat_mul(b, 2, 2, c, 2, 2, bc);
        ac_mat_mul(a, 2, 2, bc, 2, 2, abc2);
        expect_mat(abc1, abc2, 2, 2, 1e-9, "mul associativity (AB)C=A(BC)");
    }

    /* ==================== Part B：转置 ==================== */
    {
        static const double a[6] = {1, 2, 3, 4, 5, 6};
        static const double want[6] = {1, 4, 2, 5, 3, 6};
        ac_mat_transpose(a, 2, 3, g_mat_c);
        expect_mat(g_mat_c, want, 3, 2, 1e-12, "transpose 2x3");
    }
    {
        /* 转置两次还原 */
        static const double a[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        ac_mat_transpose(a, 3, 3, g_mat_c);
        ac_mat_transpose(g_mat_c, 3, 3, g_mat_work);
        expect_mat(g_mat_work, a, 3, 3, 1e-12, "transpose twice = identity");
    }
    {
        /* (AB)ᵀ = BᵀAᵀ（2×2） */
        static const double a[4] = {1, 2, 3, 4};
        static const double b[4] = {5, 6, 7, 8};
        double ab[4], bt[4], at[4], want[4];
        ac_mat_mul(a, 2, 2, b, 2, 2, ab);
        ac_mat_transpose(ab, 2, 2, want);
        ac_mat_transpose(b, 2, 2, bt);
        ac_mat_transpose(a, 2, 2, at);
        ac_mat_mul(bt, 2, 2, at, 2, 2, g_mat_c);
        expect_mat(g_mat_c, want, 2, 2, 1e-12, "(AB)^T = B^T A^T");
    }

    /* ==================== Part C：秩 ==================== */
    {
        static const double a[4] = {1, 2, 3, 4};
        expect_true(ac_mat_rank(a, 2, 2, g_mat_work) == 2, "rank [1 2;3 4] = 2");
    }
    {
        static const double a[4] = {1, 2, 2, 4};
        expect_true(ac_mat_rank(a, 2, 2, g_mat_work) == 1, "rank [1 2;2 4] = 1");
    }
    {
        static const double a[6] = {1, 2, 3, 4, 5, 6};
        expect_true(ac_mat_rank(a, 3, 2, g_mat_work) == 2, "rank (3x2) = 2");
    }
    {
        static const double z[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        expect_true(ac_mat_rank(z, 3, 3, g_mat_work) == 0, "rank 0 = 0");
    }
    {
        /* 4×4 秩 2（后两行是前两行的组合） */
        static const double a[16] = {1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 2, 1, 2, 1};
        expect_true(ac_mat_rank(a, 4, 4, g_mat_work) == 2, "rank 4x4 dependent = 2");
    }

    /* ==================== Part D：行列式 ==================== */
    {
        static const double a[4] = {1, 2, 3, 4};
        expect_true(fabs(ac_mat_det(a, 2, g_mat_work) - (-2.0)) < 1e-9,
                    "det [1 2;3 4] = -2");
    }
    {
        static const double a[4] = {2, 0, 0, 3};
        expect_true(fabs(ac_mat_det(a, 2, g_mat_work) - 6.0) < 1e-9,
                    "det diag(2,3) = 6");
    }
    {
        static const double a[4] = {0, 1, 1, 0};
        expect_true(fabs(ac_mat_det(a, 2, g_mat_work) - (-1.0)) < 1e-9,
                    "det [0 1;1 0] = -1 (odd swap)");
    }
    {
        static const double e5[25] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0,
                                      0, 0, 0, 1, 0, 0, 0, 0, 0, 1};
        expect_true(fabs(ac_mat_det(e5, 5, g_mat_work) - 1.0) < 1e-9,
                    "det I(5) = 1");
    }
    {
        static const double a[9] = {6, 1, 1, 4, -2, 5, 2, 8, 7};
        expect_true(fabs(ac_mat_det(a, 3, g_mat_work) - (-306.0)) < 1e-9,
                    "det 3x3 classic = -306");
    }
    {
        static const double a[16] = {2, 1, 0, 0, 1, 3, 1, 0, 0, 1, 2, 1, 0, 0, 1, 1};
        expect_true(fabs(ac_mat_det(a, 4, g_mat_work) - 3.0) < 1e-9,
                    "det tridiagonal 4x4 = 3");
    }
    {
        static const double a[4] = {1, 2, 2, 4};
        expect_true(fabs(ac_mat_det(a, 2, g_mat_work)) < 1e-9,
                    "det singular = 0");
    }

    /* ==================== Part E：逆矩阵 ==================== */
    {
        static const double a[4] = {1, 2, 3, 4};
        static const double want[4] = {-2, 1, 1.5, -0.5};
        expect_true(ac_mat_inverse(a, 2, g_mat_c, g_mat_work) == 1,
                    "inverse [1 2;3 4] success");
        expect_mat(g_mat_c, want, 2, 2, 1e-12, "inverse values [-2 1;1.5 -0.5]");
        /* 回验：A × A⁻¹ = I */
        ac_mat_mul(a, 2, 2, g_mat_c, 2, 2, g_mat_work);
        {
            static const double e[4] = {1, 0, 0, 1};
            expect_mat(g_mat_work, e, 2, 2, 1e-9, "A x A^-1 = I (2x2)");
        }
    }
    {
        /* 3×3 逆回验（det = −1）：A = [2 1 1;1 3 2;1 0 0] */
        static const double a[9] = {2, 1, 1, 1, 3, 2, 1, 0, 0};
        int ok = 1;
        int i;
        expect_true(ac_mat_inverse(a, 3, g_mat_c, g_mat_work) == 1,
                    "inverse 3x3 success");
        ac_mat_mul(a, 3, 3, g_mat_c, 3, 3, g_mat_work);
        for (i = 0; i < 9; i++) {
            double want = (i % 4 == 0) ? 1.0 : 0.0;
            double d = g_mat_work[i] - want;
            if (d < 0.0) { d = -d; }
            if (d > 1e-9) { ok = 0; }
        }
        expect_true(ok, "A x A^-1 = I (3x3)");
    }
    {
        /* 奇异拒绝 */
        static const double a[4] = {1, 2, 2, 4};
        expect_true(ac_mat_inverse(a, 2, g_mat_c, g_mat_work) == 0,
                    "inverse singular rejected");
    }
    {
        /* 5×5 逆回验（对角占优三对角，det = 3 ≠ 0） */
        static const double a[25] = {2, 1, 0, 0, 0, 1, 3, 1, 0, 0, 0, 1, 2, 1, 0,
                                     0, 0, 1, 2, 1, 0, 0, 0, 1, 1};
        static const double e[25] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0,
                                     0, 0, 0, 1, 0, 0, 0, 0, 0, 1};
        int ok = 1;
        int i;
        expect_true(ac_mat_inverse(a, 5, g_mat_c, g_mat_work) == 1,
                    "inverse 5x5 success");
        ac_mat_mul(a, 5, 5, g_mat_c, 5, 5, g_mat_work);
        for (i = 0; i < 25; i++) {
            double d = g_mat_work[i] - e[i];
            if (d < 0.0) { d = -d; }
            if (d > 1e-9) { ok = 0; }
        }
        expect_true(ok, "A x A^-1 = I (5x5)");
    }

    /* ==================== Part F：特征值（幂法） ==================== */
    {
        static const double a[4] = {2, 0, 0, 3};
        double lam = ac_mat_eigenvalue_power(a, 2, 200, g_mat_work);
        expect_true(fabs(lam - 3.0) < 1e-6, "eigen diag(2,3) dominant = 3");
    }
    {
        static const double a[4] = {4, 1, 2, 3};
        double lam = ac_mat_eigenvalue_power(a, 2, 200, g_mat_work);
        expect_true(fabs(lam - 5.0) < 1e-6, "eigen [4 1;2 3] dominant = 5");
    }
    {
        static const double a[9] = {1, 0, 0, 0, 2, 0, 0, 0, 3};
        double lam = ac_mat_eigenvalue_power(a, 3, 200, g_mat_work);
        expect_true(fabs(lam - 3.0) < 1e-6, "eigen diag(1,2,3) dominant = 3");
    }
    {
        static const double e3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        double lam = ac_mat_eigenvalue_power(e3, 3, 200, g_mat_work);
        expect_true(fabs(lam - 1.0) < 1e-9, "eigen I(3) = 1");
    }
    {
        /* 零矩阵 → 0 */
        static const double z[4] = {0, 0, 0, 0};
        double lam = ac_mat_eigenvalue_power(z, 2, 200, g_mat_work);
        expect_true(fabs(lam) < 1e-9, "eigen zero matrix = 0");
    }

    /* ==================== Part G：QR 分解 ==================== */
    {
        /* 3×3：QR = A 回验 + QᵀQ = I + R 上三角 */
        static const double a[9] = {1, 2, 3, 4, 5, 6, 7, 8, 10};
        int ok_qr = 1;
        int ok_orth = 1;
        int ok_upper = 1;
        int i;
        int j;
        ac_mat_qr(a, 3, 3, g_mat_c, g_mat_work, g_mat_a);
        /* QR = A：Q(m×n)×R(n×n) */
        ac_mat_mul(g_mat_c, 3, 3, g_mat_work, 3, 3, g_mat_b);
        for (i = 0; i < 9; i++) {
            double d = g_mat_b[i] - a[i];
            if (d < 0.0) { d = -d; }
            if (d > 1e-9) { ok_qr = 0; }
        }
        expect_true(ok_qr, "QR 3x3: Q x R = A");
        /* QᵀQ = I */
        ac_mat_transpose(g_mat_c, 3, 3, g_mat_b);
        ac_mat_mul(g_mat_b, 3, 3, g_mat_c, 3, 3, g_mat_a);
        for (i = 0; i < 3; i++) {
            for (j = 0; j < 3; j++) {
                double want = (i == j) ? 1.0 : 0.0;
                double d = g_mat_a[i * 3 + j] - want;
                if (d < 0.0) { d = -d; }
                if (d > 1e-9) { ok_orth = 0; }
            }
        }
        expect_true(ok_orth, "QR 3x3: Q^T Q = I");
        /* R 上三角：下三角（不含对角）全零 */
        for (i = 1; i < 3 && ok_upper; i++) {
            for (j = 0; j < i; j++) {
                if (fabs(g_mat_work[i * 3 + j]) > 1e-9) { ok_upper = 0; }
            }
        }
        expect_true(ok_upper, "QR 3x3: R upper triangular");
    }
    {
        /* 3×2 矩形：QR = A + QᵀQ = I(2×2) */
        static const double a[6] = {1, 2, 3, 4, 5, 6};
        static const double qt_e[4] = {1, 0, 0, 1};
        int ok_qr = 1;
        int i;
        ac_mat_qr(a, 3, 2, g_mat_c, g_mat_work, g_mat_a);
        /* Q(3×2) × R(2×2) = A(3×2) */
        ac_mat_mul(g_mat_c, 3, 2, g_mat_work, 2, 2, g_mat_b);
        for (i = 0; i < 6; i++) {
            double d = g_mat_b[i] - a[i];
            if (d < 0.0) { d = -d; }
            if (d > 1e-9) { ok_qr = 0; }
        }
        expect_true(ok_qr, "QR 3x2: Q x R = A");
        /* QᵀQ = I(2×2) */
        ac_mat_transpose(g_mat_c, 3, 2, g_mat_b);
        ac_mat_mul(g_mat_b, 2, 3, g_mat_c, 3, 2, g_mat_a);
        expect_mat(g_mat_a, qt_e, 2, 2, 1e-9, "QR 3x2: Q^T Q = I");
    }

    /* ==================== Part H：别名安全 ==================== */
    {
        /* c 别名 a 的乘法 */
        static const double a[4] = {1, 2, 3, 4};
        static const double b[4] = {5, 6, 7, 8};
        static const double want[4] = {19, 22, 43, 50};
        double alias[4] = {1, 2, 3, 4};
        ac_mat_mul(alias, 2, 2, b, 2, 2, alias);
        expect_mat(alias, want, 2, 2, 1e-12, "mul c aliases a");
        (void)a;
    }
    {
        /* inverse 别名 a */
        static const double want[4] = {-2, 1, 1.5, -0.5};
        double alias[4] = {1, 2, 3, 4};
        ac_mat_inverse(alias, 2, alias, g_mat_work);
        expect_mat(alias, want, 2, 2, 1e-12, "inverse out aliases a");
    }

    printf("==== C99 matrix: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
