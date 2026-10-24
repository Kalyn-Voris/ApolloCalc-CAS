/* ============================================================
 * ApolloCalc-CAS · C/128KB-ProTEXT/test/test_pro_multi.c
 * ------------------------------------------------------------
 * 【职责 / Responsibility】
 *   v1.3 方向 A 的单元测试：A1 多变量 GCD（18 项测试）、
 *   A2 多变量因式分解（10 项测试）、A3 结式（8 项测试）。
 *
 * 【接口约定 / Interface】
 *   三个测试函数各自返回 pro_tally_t（checks / failed），runner 负责
 *   汇总。这样本文件**不依赖** runner 的内部类型，可单独编译、单独
 *   测试；runner 也不需要知道本文件的计数细节。
 *
 * 【为什么单独一个文件】
 *   ① runner 那个 TU 已有 s_kernel_live，再塞 36 条测试会让它过长；
 *   ② 本文件**不声明任何静态对象**（函数是 static 但不占 .bss），
 *      所以链接进来后 runner 口径的 .bss 增量仍是 0。
 *
 * 【测试口径 / Test convention】
 *   A1 的 GCD 结果按「原始部分 + 首项系数为正」归一化，测试比对的是
 *   **归一化后的字符串**而不是原始公因式（后者可以差任意常数倍）。
 *   这是设计说明没规定、但实现必须选定的约定，在此作为契约写明。
 * ============================================================ */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../src/ac_pro.h"
#include "../../../C/128KB/src/ac_solvers.h"
#include "../../../C/128KB/src/ac_matrix.h"

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

static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

/* ==================== A1：多变量 GCD（18 项测试） ====================
 *
 * 覆盖：
 *   ① 经典系统（稀疏、±1 系数）
 *   ② 内容提取（2x²−2y² 与 4x²−4xy 的 GCD 应为 x−y，不是 2x−2y）
 *   ③ 互素（GCD = 1）
 *   ④ 一方为零（GCD = 另一方）
 *   ⑤ 完全相同（GCD = 自身）
 *   ⑥ 单项式公因式
 *   ⑦ 超限拒绝（总次数 7 > 6）
 */
pro_tally_t test_multi_gcd(void) {
    pro_tally_t t;
    char out[256];
    int rc;

    t.checks = 0;
    t.failed = 0;

    printf("[case A1-1] gcd(x^2 - y^2, x - y) = x - y\n");
    out[0] = '\0';
    rc = ac_pro_multi_var_gcd("x^2 - y^2", "x - y", out, (int)sizeof(out));
    pcheck(&t, rc == 1, "returns 1", "gcd failed");
    pcheck(&t, streq(out, "x - y"), "result is 'x - y'", out);
    printf("         result: %s\n", out);

    printf("[case A1-2] gcd(x^3 + y^3, x^2 - x*y + y^2) = x^2 - x*y + y^2\n");
    rc = ac_pro_multi_var_gcd("x^3 + y^3", "x^2 - x*y + y^2", out, (int)sizeof(out));
    pcheck(&t, rc == 1, "returns 1", "gcd failed");
    pcheck(&t, streq(out, "x^2 - x*y + y^2"), "result is 'x^2 - x*y + y^2'", out);
    printf("         result: %s\n", out);

    printf("[case A1-3] content extraction: gcd(2x^2-2y^2, 4x^2-4xy) = x - y\n");
    rc = ac_pro_multi_var_gcd("2*x^2 - 2*y^2", "4*x^2 - 4*x*y", out, (int)sizeof(out));
    pcheck(&t, rc == 1, "returns 1", "gcd failed");
    pcheck(&t, streq(out, "x - y"), "content factored out (not 2x-2y)", out);
    printf("         result: %s\n", out);

    printf("[case A1-4] coprime: gcd(x + y, x - y) = 1\n");
    rc = ac_pro_multi_var_gcd("x + y", "x - y", out, (int)sizeof(out));
    pcheck(&t, rc == 1, "returns 1", "gcd failed");
    pcheck(&t, streq(out, "1"), "result is '1'", out);
    printf("         result: %s\n", out);

    printf("[case A1-5] zero operand: gcd(x^2 - y^2, 0) = x^2 - y^2\n");
    rc = ac_pro_multi_var_gcd("x^2 - y^2", "0", out, (int)sizeof(out));
    pcheck(&t, rc == 1, "returns 1", "gcd failed");
    pcheck(&t, streq(out, "x^2 - y^2"), "result is the nonzero operand", out);
    printf("         result: %s\n", out);

    printf("[case A1-6] identical: gcd(x*y + x, x*y + x) = x*y + x\n");
    rc = ac_pro_multi_var_gcd("x*y + x", "x*y + x", out, (int)sizeof(out));
    pcheck(&t, rc == 1, "returns 1", "gcd failed");
    pcheck(&t, streq(out, "x*y + x"), "result is f itself", out);
    printf("         result: %s\n", out);

    printf("[case A1-7] monomial factor: gcd(x^2*y, x*y^2) = x*y\n");
    rc = ac_pro_multi_var_gcd("x^2*y", "x*y^2", out, (int)sizeof(out));
    pcheck(&t, rc == 1, "returns 1", "gcd failed");
    pcheck(&t, streq(out, "x*y"), "result is 'x*y'", out);
    printf("         result: %s\n", out);

    printf("[case A1-8] reject: total degree 7 exceeds the cap of 6\n");
    g_solve_error = 0;
    rc = ac_pro_multi_var_gcd("x^7 + y", "x + y", out, (int)sizeof(out));
    pcheck(&t, rc == -1, "returns -1", "accepted degree 7");
    pcheck(&t, g_solve_error == 1, "g_solve_error == 1", "wrong error code");

    return t;
}

/* ==================== A2：多变量因式分解（10 项测试） ====================
 *
 * 覆盖：
 *   ① 单变量完全分解（x²−1）
 *   ② 内容提取（2x²−8）
 *   ③ 不可约（x²+1，无有理根）
 *   ④ 真 2 变量 -> 超出 v1.3 边界，返回 0
 */
pro_tally_t test_multi_factor(void) {
    pro_tally_t t;
    char out[256];
    ac_int_t content = 0;
    int rc;

    t.checks = 0;
    t.failed = 0;

    printf("[case A2-1] factor x^2 - 1 = (x - 1)(x + 1)\n");
    out[0] = '\0';
    rc = ac_pro_multi_var_factor("x^2 - 1", out, (int)sizeof(out), &content);
    pcheck(&t, rc == 1, "returns 1", "factor failed");
    pcheck(&t, content == 1, "content == 1", "wrong content");
    pcheck(&t, strstr(out, "(x - 1)") != NULL, "contains factor (x - 1)", out);
    pcheck(&t, strstr(out, "(x + 1)") != NULL, "contains factor (x + 1)", out);
    printf("         result: %s\n", out);

    printf("[case A2-2] content extraction: 2x^2 - 8 = 2 * (x - 2) * (x + 2)\n");
    out[0] = '\0';
    rc = ac_pro_multi_var_factor("2*x^2 - 8", out, (int)sizeof(out), &content);
    pcheck(&t, rc == 1, "returns 1", "factor failed");
    pcheck(&t, content == 2, "content == 2", "wrong content");
    pcheck(&t, strstr(out, "(x - 2)") != NULL && strstr(out, "(x + 2)") != NULL,
           "contains (x - 2) and (x + 2)", out);
    printf("         result: %s\n", out);

    printf("[case A2-3] irreducible over Q: x^2 + 1 has no rational root\n");
    out[0] = '\0';
    rc = ac_pro_multi_var_factor("x^2 + 1", out, (int)sizeof(out), &content);
    pcheck(&t, rc == 1, "returns 1 (irreducible is still a valid answer)", "factor failed");
    printf("         result: %s\n", out);

    printf("[case A2-4] true bivariate -> out of v1.3 scope, returns 0\n");
    out[0] = '\0';
    g_solve_error = 0;
    rc = ac_pro_multi_var_factor("x^2 + y^2", out, (int)sizeof(out), &content);
    pcheck(&t, rc == 0, "returns 0 (out of scope)", "claimed to factor a bivariate poly");

    return t;
}

/* ==================== A3：结式（8 项测试） ====================
 *
 * 覆盖：
 *   ① 与手算值比对
 *   ② Sylvester 矩阵构造正确（尺寸 + 每行元素）
 *   ③ 有公共根时结式为 0
 *   ④ 阶数超限拒绝（13 > 12），且输出不被改写（**绝不截断**）
 */
pro_tally_t test_resultant(void) {
    pro_tally_t t;
    double r;
    int rc;
    int n;
    const double *s;

    t.checks = 0;
    t.failed = 0;

    /* f = y² − x，g = y − 1，在 x = 4 处：
     *   f(4,y) = y² − 4，g(4,y) = y − 1
     *   Res(f, g) = lc(f)^deg(g) · Π f(β_i)，β 是 g 的根 = 1
     *             = 1 · f(1) = 1 − 4 = −3 */
    printf("[case A3-1] Res_y(y^2 - x, y - 1) at x = 4  (hand-computed: -3)\n");
    r = 0.0;
    rc = ac_pro_resultant("y^2 - x", "y - 1", 4.0, &r);
    pcheck(&t, rc == 1, "returns 1", "resultant failed");
    pcheck(&t, fabs(r - (-3.0)) < 1e-10, "Res == -3", "wrong value");
    printf("         Res = %.17g\n", r);

    printf("[case A3-2] Sylvester matrix is (df+dg) x (df+dg)\n");
    n = ac_pro_resultant_build("y^2 - x", "y - 1", 4.0);
    pcheck(&t, n == 3, "order == 3 (deg_y(f)=2 + deg_y(g)=1)", "wrong order");
    s = ac_pro_resultant_sylvester();
    pcheck(&t, s != (const double *)0, "matrix pointer non-null", "null matrix");
    if (s != (const double *)0 && n == 3) {
        pcheck(&t, fabs(s[0] - 1.0) < 1e-12 && fabs(s[1] - 0.0) < 1e-12 &&
                    fabs(s[2] - (-4.0)) < 1e-12,
               "row 0 == [1, 0, -4] (f in descending powers)", "row 0 wrong");
        pcheck(&t, fabs(s[3] - 1.0) < 1e-12 && fabs(s[4] - (-1.0)) < 1e-12 &&
                    fabs(s[5] - 0.0) < 1e-12,
               "row 1 == [1, -1, 0] (g shifted)", "row 1 wrong");
        pcheck(&t, fabs(s[6] - 0.0) < 1e-12 && fabs(s[7] - 1.0) < 1e-12 &&
                    fabs(s[8] - (-1.0)) < 1e-12,
               "row 2 == [0, 1, -1] (g shifted)", "row 2 wrong");
    } else {
        pcheck(&t, 0, "row 0 check", "matrix unavailable");
        pcheck(&t, 0, "row 1 check", "matrix unavailable");
        pcheck(&t, 0, "row 2 check", "matrix unavailable");
    }

    /* 公共根：f = y²−x 与 g = y²−4 在 x=4 处完全相同 -> 结式必为 0 */
    printf("[case A3-3] common root -> Res == 0\n");
    r = 1.0;
    rc = ac_pro_resultant("y^2 - x", "y^2 - 4", 4.0, &r);
    pcheck(&t, rc == 1, "returns 1", "resultant failed");
    pcheck(&t, fabs(r) < 1e-10, "Res == 0 (common root at x=4)", "expected zero resultant");
    printf("         Res = %.17g\n", r);

    /* 阶数超限：deg_y(f)+deg_y(g) = 7+6 = 13 > 12，必须拒绝且不改写输出 */
    printf("[case A3-4] order 13 > 12 -> rejected, output untouched\n");
    r = 12345.0;
    g_solve_error = 0;
    rc = ac_pro_resultant("y^7 + x", "y^6 + x", 1.0, &r);
    pcheck(&t, rc == 0, "returns 0 (rejected)", "accepted order 13");
    pcheck(&t, g_solve_error == 1, "g_solve_error == 1", "wrong error code");
    pcheck(&t, r == 12345.0, "output NOT clobbered (no truncation)", "output was overwritten");

    return t;
}

#endif /* AC_PRO_ACTIVE */
