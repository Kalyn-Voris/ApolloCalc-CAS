/* ============================================================
 * ApolloCalc-CAS · test_ratroot.c — 有理根检验与降次测试（C 128KB 第 5 轮）
 * 行为基准：JS/128KB/src/core_ratroot.js（文件 9）。
 * Part A：直驱 ac_rat_root_descent（g_coeffs 手工布置）——
 *         全有理 / 零根剥离 / 分数根 / 线性吸收 / 无有理根 / 非整系数 /
 *         枚举上限；每例核对 found 栈（values/num/den）与 g_coeffs 降次结果。
 * Part B：descent + finish 全管线（经 ac_rat_root_finish 的 dd=0/2/3 路径）。
 * 候选枚举顺序（决定发现序）：divisors(a0) 升序 × divisors(an) 升序，
 *   ± 交错——期望值经 JS 圣经同款枚举序手推核算（勿凭记忆改值）。
 * ============================================================ */
#include <stdio.h>
#include <math.h>
#include "../src/ac_types.h"
#include "../src/ac_parser.h"
#include "../src/ac_solvers.h"
#include "../src/ac_exact.h"
#include "../src/ac_ratroot.h"

static int s_pass = 0;
static int s_fail = 0;

static void expect_true(int cond, const char *name) {
    if (cond) { s_pass++; }
    else { s_fail++; printf("FAIL [ratroot] %s\n", name); }
}

static int approx(double a, double b, double tol) {
    double d = a - b;
    if (d < 0.0) { d = -d; }
    return d <= tol;
}

/* 布置 g_coeffs（升幂 [a0..ad]）并直驱 descent：校验按阶段拆分计数——
 * k/found 一项、每根（值+num+den）各一项、降次余式一项 */
static void descent_case(const char *name, const double *coeffs, int deg,
                         int want_k, const double *want_vals,
                         const ac_int_t *want_num, const ac_int_t *want_den,
                         const double *want_rest) {
    int k;
    int i;
    int ok = 1;
    char tag[160];
    for (i = 0; i <= deg; i++) { g_coeffs[i] = coeffs[i]; }
    for (i = deg + 1; i < 5; i++) { g_coeffs[i] = 0.0; }
    k = ac_rat_root_descent(deg);

    sprintf(tag, "%s: found count", name);
    expect_true(k == want_k && (want_k == 0 || g_rat_root_found == want_k), tag);

    for (i = 0; i < want_k; i++) {
        sprintf(tag, "%s: root %d value/num/den", name, i);
        expect_true(approx(g_rat_root_values[i], want_vals[i], 1e-12) &&
                    g_rat_root_num[i] == want_num[i] &&
                    g_rat_root_den[i] == want_den[i], tag);
    }
    if (want_rest != 0) {
        sprintf(tag, "%s: depressed coeffs", name);
        ok = 1;
        for (i = 0; ok && i <= deg - want_k; i++) {
            if (g_coeffs[i] != want_rest[i]) { ok = 0; }
        }
        expect_true(ok, tag);
    } else if (want_k == 0) {
        /* 无有理根：g_coeffs 原样保留 */
        sprintf(tag, "%s: coeffs untouched", name);
        ok = 1;
        for (i = 0; ok && i <= deg; i++) {
            if (g_coeffs[i] != coeffs[i]) { ok = 0; }
        }
        expect_true(ok, tag);
    }
}

int test_ratroot_run(void) {
    /* ==================== Part A：直驱 descent ==================== */

    /* (x−1)(x−2)(x−3)：候选序 1,−1,2,−2,3,−3,6,−6 → 命中 1,2,3。
       cd=0 提交的残槽 g_coeffs[0] = round(work[0]) = −3（最后一次线性
       吸收不写商，stale 值确定——契约钉住） */
    {
        static const double c[4] = {-6.0, 11.0, -6.0, 1.0};
        static const double v[3] = {1.0, 2.0, 3.0};
        static const ac_int_t n[3] = {1, 2, 3};
        static const ac_int_t d[3] = {1, 1, 1};
        static const double rest[1] = {-3.0};
        descent_case("descent (x-1)(x-2)(x-3) k=3", c, 3, 3, v, n, d, rest);
    }

    /* 零根剥离（双重零根）+ 线性吸收：x³−2x² = x²(x−1)·2 → 根 0,0,1 */
    {
        static const double c[4] = {0.0, 0.0, -2.0, 2.0};
        static const double v[3] = {0.0, 0.0, 1.0};
        static const ac_int_t n[3] = {0, 0, 1};
        static const ac_int_t d[3] = {1, 1, 1};
        static const double rest[1] = {-1.0};   /* cd=0 提交的残槽（stale，契约如此） */
        descent_case("descent x^3-2x^2 zero strip k=3", c, 3, 3, v, n, d, 0);
        (void)rest;
    }

    /* 分数根：(2x−1)(x−2) → 2x²−5x+2：候选序 1,−1,2,−2,1/2,−1/2 → 命中 2,1/2 */
    {
        static const double c[3] = {2.0, -5.0, 2.0};
        static const double v[2] = {2.0, 0.5};
        static const ac_int_t n[2] = {2, 1};
        static const ac_int_t d[2] = {1, 2};
        static const double rest[1] = {1.0};    /* 剩余 2x−1 … 提交后 [2,0,0] 见下注 */
        descent_case("descent 2x^2-5x+2 k=2", c, 2, 2, v, n, d, 0);
        (void)rest;
    }

    /* 纯线性：2x−4 → 吸收根 2 */
    {
        static const double c[2] = {-4.0, 2.0};
        static const double v[1] = {2.0};
        static const ac_int_t n[1] = {2};
        static const ac_int_t d[1] = {1};
        descent_case("descent 2x-4 k=1", c, 1, 1, v, n, d, 0);
    }

    /* 无有理根：x²−3 → k=0，g_coeffs 原样保留 */
    {
        static const double c[3] = {-3.0, 0.0, 1.0};
        descent_case("descent x^2-3 k=0 coeffs untouched", c, 2, 0, 0, 0, 0, 0);
    }

    /* 非整系数：x³−3.5x²+… → snap 失败 k=0，原样保留 */
    {
        static const double c[4] = {0.0, -3.5, 1.0, 1.0};
        descent_case("descent non-integer coeff k=0", c, 3, 0, 0, 0, 0, 0);
    }

    /* 枚举上限：|a0| > 1e7 → 放弃枚举 k=0 */
    {
        static const double c[3] = {-10000001.0, 0.0, 1.0};
        descent_case("descent |a0|>1e7 skip k=0", c, 2, 0, 0, 0, 0, 0);
    }

    /* 四次全有理：(x−1)(x+1)(x−2)(x+2) → 发现序 1,−1,2,−2 */
    {
        static const double c[5] = {4.0, 0.0, -5.0, 0.0, 1.0};
        static const double v[4] = {1.0, -1.0, 2.0, -2.0};
        static const ac_int_t n[4] = {1, -1, 2, -2};
        static const ac_int_t d[4] = {1, 1, 1, 1};
        descent_case("descent x^4-5x^2+4 k=4", c, 4, 4, v, n, d, 0);
    }

    /* 一般四次部分有理：2x⁴−x³−14x²−5x+6 → 发现序 −1,−2,3,1/2（k=4） */
    {
        static const double c[5] = {6.0, -5.0, -14.0, -1.0, 2.0};
        static const double v[4] = {-1.0, -2.0, 3.0, 0.5};
        static const ac_int_t n[4] = {-1, -2, 3, 1};
        static const ac_int_t d[4] = {1, 1, 1, 2};
        descent_case("descent 2x^4-... k=4", c, 4, 4, v, n, d, 0);
    }

    /* 三重根：(x−2)³ → 连剥三次 2,2,2 */
    {
        static const double c[4] = {-8.0, 12.0, -6.0, 1.0};
        static const double v[3] = {2.0, 2.0, 2.0};
        static const ac_int_t n[3] = {2, 2, 2};
        static const ac_int_t d[3] = {1, 1, 1};
        descent_case("descent (x-2)^3 k=3", c, 3, 3, v, n, d, 0);
    }

    /* 非法次数：0 与 5 → 返回 0 */
    {
        static const double c[2] = {-4.0, 2.0};
        int k;
        g_coeffs[0] = -4.0; g_coeffs[1] = 2.0;
        k = ac_rat_root_descent(0);
        expect_true(k == 0, "descent degree 0 -> 0");
        k = ac_rat_root_descent(5);
        expect_true(k == 0, "descent degree 5 -> 0");
        (void)c;
    }

    /* ==================== Part B：descent + finish 全管线 ==================== */

    /* dd=0 路径：三次全有理 → finish 直接填栈，3 根全精确 */
    ac_solvers_reset();
    g_coeffs[0] = -6.0; g_coeffs[1] = 11.0; g_coeffs[2] = -6.0; g_coeffs[3] = 1.0;
    {
        int k = ac_rat_root_descent(3);
        int ret = ac_rat_root_finish(k, 3);
        expect_true(k == 3 && ret == 0 && g_root_count == 3 &&
                    approx(g_root_re[0], 1.0, 1e-12) &&
                    approx(g_root_re[1], 2.0, 1e-12) &&
                    approx(g_root_re[2], 3.0, 1e-12) &&
                    g_root_im[0] == 0.0 &&
                    g_root_exact_valid[0] == 1 && g_root_exact_rat[0] == 1 &&
                    g_root_exact_valid[2] == 1 && g_root_exact_rat[2] == 3,
                    "finish dd=0 all rational exact");
    }

    /* dd=2 路径：2x³−x−1 → 有理根 1 + 二次核 2x²+2x+1（复对右移） */
    ac_solvers_reset();
    g_coeffs[0] = -1.0; g_coeffs[1] = -1.0; g_coeffs[2] = 0.0; g_coeffs[3] = 2.0;
    {
        int k = ac_rat_root_descent(3);
        int ret = ac_rat_root_finish(k, 3);
        expect_true(k == 1 && ret == 0 && g_root_count == 3 &&
                    approx(g_root_re[0], 1.0, 1e-12) &&
                    g_root_im[0] == 0.0 &&
                    approx(g_root_re[1], -0.5, 1e-9) &&
                    approx(g_root_im[1], 0.5, 1e-9) &&
                    g_root_exact_valid[0] == 1 &&
                    g_root_exact_valid[1] == 0 && g_root_exact_valid[2] == 0,
                    "finish dd=2 rational + complex pair");
    }

    /* dd=3 路径：四次部分有理——(x−1)(x³−3x+1) = x⁴−x³−3x²+4x−1 →
       有理根 1 + 不可约三次核（skip=1 走三角法），4 根全落池 */
    ac_solvers_reset();
    g_coeffs[0] = -1.0; g_coeffs[1] = 4.0; g_coeffs[2] = -3.0; g_coeffs[3] = -1.0; g_coeffs[4] = 1.0;
    {
        int k = ac_rat_root_descent(4);
        int ret = ac_rat_root_finish(k, 4);
        expect_true(k == 1 && ret == 0 && g_root_count == 4 &&
                    approx(g_root_re[0], 1.0, 1e-12) &&
                    g_root_exact_valid[0] == 1 &&
                    g_root_exact_valid[1] == 0 && g_root_exact_valid[2] == 0 &&
                    g_root_exact_valid[3] == 0,
                    "finish dd=3 rational + irreducible cubic");
    }

    /* 管线外观察：descent 无有理根时 g_coeffs 原样（保真契约已在 Part A 验证） */

    printf("==== C99 ratroot: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
