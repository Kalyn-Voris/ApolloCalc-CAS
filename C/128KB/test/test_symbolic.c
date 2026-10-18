/* ============================================================
 * ApolloCalc-CAS · test_symbolic.c — 符号求导 + 因式分解测试
 * （C 128KB 第 10 轮，C 独有）
 * 覆盖：
 *   Part A 树构造/求值/相等：from_poly 求值抽查（2x²+3x+1 @ 2 = 15）、
 *          转置两次还原式幂法则、append-only（求导后原树求值不变）
 *   Part B 符号求导：常数/幂/乘积/链式，多点数值验证（4x+3 主案例）、
 *          化简器结构校验（d(1·x) = 常量 1 节点）、d(x^0)=0
 *          规格化简结构校验：new_pow(x,1)≡x、new_pow(x,0)=CONST 1、
 *          d(x^1)=CONST 1、d(x^2)=MUL(2,VAR) 无 POW(x,1) 残留、d(x^0)@0=0
 *   Part C 因式分解：(x−2)(x−3) / 三根 / 四根 / 部分有理 / 不可约、
 *          精确五分量、剩余多项式、非法次数
 * 测试基准：数学事实（导数多点采样 + 因式乘回展开），勿凭记忆改值。
 * ============================================================ */
#include <stdio.h>
#include <math.h>
#include "../src/ac_types.h"
#include "../src/ac_solvers.h"
#include "../src/ac_symbolic.h"

static int s_pass = 0;
static int s_fail = 0;

static void expect_true(int cond, const char *name) {
    if (cond) { s_pass++; }
    else { s_fail++; printf("FAIL [symbolic] %s\n", name); }
}

static void expect_near(double got, double want, double tol, const char *name) {
    double d = got - want;
    if (d < 0.0) { d = -d; }
    if (d <= tol) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [symbolic] %s: got=%.17g want=%.17g\n", name, got, want);
    }
}

int test_symbolic_run(void) {
    /* ==================== Part A：树构造 / 求值 / append-only ==================== */
    {
        /* 2x² + 3x + 1：升幂 [1, 3, 2]，@x=2 → 15，@x=0 → 1 */
        static const ac_int_t c[3] = {1, 3, 2};
        int root = ac_sym_from_poly(c, 2);
        expect_true(root >= 0, "from_poly 2x^2+3x+1 builds");
        expect_near(ac_sym_eval(root, 2.0), 15.0, 1e-9, "eval(2x^2+3x+1) @2 = 15");
        expect_near(ac_sym_eval(root, 0.0), 1.0, 1e-12, "eval @0 = 1");
        expect_near(ac_sym_eval(root, -1.5), 1.0, 1e-12, "eval @-1.5 = 1 (4.5-4.5+1)");
    }
    {
        /* x³ + 1 @2 = 9（跳零系数） */
        static const ac_int_t c[4] = {1, 0, 0, 1};
        int root = ac_sym_from_poly(c, 3);
        expect_near(ac_sym_eval(root, 2.0), 9.0, 1e-9, "eval (x^3+1) @2 = 9");
    }
    {
        /* 全零系数 → 0 */
        static const ac_int_t c[2] = {0, 0};
        int root = ac_sym_from_poly(c, 1);
        expect_near(ac_sym_eval(root, 5.0), 0.0, 1e-15, "eval zero poly = 0");
    }
    {
        /* 手工节点：x²·x³（先乘后导的素材）+ append-only 前照 */
        int x = ac_sym_new_var();
        int p2 = ac_sym_new_pow(x, 2);
        int p3 = ac_sym_new_pow(x, 3);
        int mul = ac_sym_new_mul(p2, p3);
        int d;
        expect_true(mul >= 0, "manual mul tree builds");
        d = ac_sym_derivative(mul);
        expect_true(d >= 0, "derivative of x^2*x^3 builds");
        /* append-only：原节点求值不变（x²@1.3 仍 = 1.69） */
        expect_near(ac_sym_eval(p2, 1.3), 1.69, 1e-9, "append-only: original intact");
        /* d(x²·x³) = d(x⁵) = 5x⁴，@2 = 80 */
        expect_near(ac_sym_eval(d, 2.0), 80.0, 1e-9, "d(x^2*x^3) = 5x^4 @2 = 80");
    }

    /* ==================== Part B：符号求导 ==================== */
    {
        /* d/dx(5) = 0：结构（单 CONST 节点）+ 数值 */
        int root = ac_sym_new_const(5);
        int d = ac_sym_derivative(root);
        expect_true(d >= 0 && g_sym_nodes[d].type == NODE_CONST &&
                    g_sym_nodes[d].value == 0, "d(5) = const 0 node");
        expect_near(ac_sym_eval(d, 3.7), 0.0, 1e-15, "d(5) evaluates 0");
    }
    {
        /* d/dx(x) = 1 */
        int x = ac_sym_new_var();
        int d = ac_sym_derivative(x);
        expect_true(d >= 0 && g_sym_nodes[d].type == NODE_CONST &&
                    g_sym_nodes[d].value == 1, "d(x) = const 1 node");
    }
    {
        /* d/dx(x^2) = 2x：三点采样 */
        int x = ac_sym_new_var();
        int p = ac_sym_new_pow(x, 2);
        int d = ac_sym_derivative(p);
        expect_true(d >= 0, "d(x^2) builds");
        expect_near(ac_sym_eval(d, 0.5), 1.0, 1e-9, "d(x^2) @0.5 = 1");
        expect_near(ac_sym_eval(d, 2.0), 4.0, 1e-9, "d(x^2) @2 = 4");
        expect_near(ac_sym_eval(d, -3.0), -6.0, 1e-9, "d(x^2) @-3 = -6");
    }
    {
        /* d/dx(x^3) = 3x^2 */
        int x = ac_sym_new_var();
        int p = ac_sym_new_pow(x, 3);
        int d = ac_sym_derivative(p);
        expect_near(ac_sym_eval(d, 2.0), 12.0, 1e-9, "d(x^3) @2 = 12");
        expect_near(ac_sym_eval(d, -1.0), 3.0, 1e-9, "d(x^3) @-1 = 3");
    }
    {
        /* 主案例：d/dx(2x^2+3x+1) = 4x+3（三点采样） */
        static const ac_int_t c[3] = {1, 3, 2};
        int root = ac_sym_from_poly(c, 2);
        int d = ac_sym_derivative(root);
        expect_true(d >= 0, "d(2x^2+3x+1) builds");
        expect_near(ac_sym_eval(d, 0.0), 3.0, 1e-9, "d(2x^2+3x+1) @0 = 3");
        expect_near(ac_sym_eval(d, 1.0), 7.0, 1e-9, "d(2x^2+3x+1) @1 = 7");
        expect_near(ac_sym_eval(d, 2.5), 13.0, 1e-9, "d(2x^2+3x+1) @2.5 = 13");
        /* 原树求值不受影响（append-only） */
        expect_near(ac_sym_eval(root, 2.0), 15.0, 1e-9, "original tree intact after d");
    }
    {
        /* 化简器：d(1·x) = 1（结构校验：单 CONST 值 1 节点） */
        int one = ac_sym_new_const(1);
        int x = ac_sym_new_var();
        int m = ac_sym_new_mul(one, x);
        int d = ac_sym_derivative(m);
        expect_true(d >= 0 && g_sym_nodes[d].type == NODE_CONST &&
                    g_sym_nodes[d].value == 1, "simplify d(1*x) = const 1");
    }
    {
        /* 化简器：d(x·1) = 1（另一侧单位元） */
        int x = ac_sym_new_var();
        int one = ac_sym_new_const(1);
        int m = ac_sym_new_mul(x, one);
        int d = ac_sym_derivative(m);
        expect_true(d >= 0 && g_sym_nodes[d].type == NODE_CONST &&
                    g_sym_nodes[d].value == 1, "simplify d(x*1) = const 1");
    }
    {
        /* d(x^0) = 0（x^0 经 u^0→1 化简为 CONST 1，其导数为 CONST 0） */
        int x = ac_sym_new_var();
        int p = ac_sym_new_pow(x, 0);
        int d = ac_sym_derivative(p);
        expect_true(d >= 0 && g_sym_nodes[d].type == NODE_CONST &&
                    g_sym_nodes[d].value == 0, "d(x^0) = const 0");
    }
    {
        /* ---- 结构校验：规格化简 u^1→u、u^0→1（不只测数值） ---- */
        /* 检查 3：new_pow(x,1) 直接返回基节点索引，不新增 POW 节点 */
        int x = ac_sym_new_var();
        int p1 = ac_sym_new_pow(x, 1);
        expect_true(p1 == x, "new_pow(x,1) returns base index (x^1 -> x)");

        /* 检查 4：new_pow(x,0) 返回 CONST 1 节点 */
        int p0 = ac_sym_new_pow(x, 0);
        expect_true(p0 >= 0 && g_sym_nodes[p0].type == NODE_CONST &&
                    g_sym_nodes[p0].value == 1, "new_pow(x,0) returns const 1");

        /* 检查 1：d(x^1) 根节点是 CONST 1（u^1→u 后对 VAR 求导） */
        int d1 = ac_sym_derivative(p1);
        expect_true(d1 >= 0 && g_sym_nodes[d1].type == NODE_CONST &&
                    g_sym_nodes[d1].value == 1, "d(x^1) root is const 1");

        /* 检查 2：d(x^2) = MUL(CONST 2, VAR)——右子树是 VAR 而非 POW(x,1) */
        {
            int x2 = ac_sym_new_var();
            int p2 = ac_sym_new_pow(x2, 2);
            int d2 = ac_sym_derivative(p2);
            expect_true(d2 >= 0 && g_sym_nodes[d2].type == NODE_MUL &&
                        g_sym_nodes[g_sym_nodes[d2].left].type == NODE_CONST &&
                        g_sym_nodes[g_sym_nodes[d2].left].value == 2 &&
                        g_sym_nodes[g_sym_nodes[d2].right].type == NODE_VAR,
                        "d(x^2) = MUL(2, VAR), no POW(x,1) residue");
        }

        /* 检查 5：数值回验 d(x^0) 在 x=0 处 == 0（化简后恒 0，不依赖 pow(0,0)） */
        {
            int x0 = ac_sym_new_var();
            int pz = ac_sym_new_pow(x0, 0);
            int dz = ac_sym_derivative(pz);
            expect_true(dz >= 0, "d(x^0) builds for x=0 probe");
            expect_near(ac_sym_eval(dz, 0.0), 0.0, 1e-15, "d(x^0) @x=0 = 0");
        }
    }
    {
        /* d(ADD(x, x^2)) = 1 + 2x */
        int x = ac_sym_new_var();
        int p = ac_sym_new_pow(x, 2);
        int s = ac_sym_new_add(x, p);
        int d = ac_sym_derivative(s);
        expect_near(ac_sym_eval(d, 3.0), 7.0, 1e-9, "d(x+x^2) @3 = 7");
    }
    {
        /* d(SUB(x^3, x)) = 3x^2 − 1 */
        int x = ac_sym_new_var();
        int p = ac_sym_new_pow(x, 3);
        int s = ac_sym_new_sub(p, x);
        int d = ac_sym_derivative(s);
        expect_near(ac_sym_eval(d, 2.0), 11.0, 1e-9, "d(x^3-x) @2 = 11");
        expect_near(ac_sym_eval(d, -2.0), 11.0, 1e-9, "d(x^3-x) @-2 = 11");
    }
    {
        /* 非法根 */
        expect_true(ac_sym_derivative(-1) == -1, "derivative(-1) -> -1");
        expect_true(ac_sym_derivative(9999) == -1, "derivative(9999) -> -1");
    }
    {
        /* 结构相等：同一棵树构造两次 equal=1；x² vs x³ equal=0 */
        static const ac_int_t c[3] = {1, 3, 2};
        int r1 = ac_sym_from_poly(c, 2);
        int r2 = ac_sym_from_poly(c, 2);
        int x = ac_sym_new_var();
        int p3 = ac_sym_new_pow(x, 3);
        expect_true(ac_sym_equal(r1, r2) == 1, "equal identical trees");
        expect_true(ac_sym_equal(r1, p3) == 0, "equal different trees = 0");
    }

    /* ==================== Part C：因式分解 ==================== */
    {
        /* x²−5x+6 = (x−2)(x−3)：发现序 [2,3]，剩余 1 */
        static const double c[3] = {6, -5, 1};
        int k = ac_sym_factor_poly(c, 2);
        expect_true(k == 2 && g_sym_factor_count == 2, "factor x^2-5x+6 count 2");
        expect_true(g_sym_factor_rat[0] == 2 && g_sym_factor_rat[1] == 3 &&
                    g_sym_factor_den[0] == 1 && g_sym_factor_den[1] == 1,
                    "factor roots 2,3 exact");
        expect_true(g_sym_factor_rad_num[0] == 0 && g_sym_factor_rad[0] == 1 &&
                    g_sym_factor_exp[0] == 0, "factor five-component rational");
        expect_true(g_sym_remainder_degree == 0 && g_sym_remainder_coeffs[0] == 1.0,
                    "factor x^2-5x+6 fully factored");
    }
    {
        /* x³−6x²+11x−6 = (x−1)(x−2)(x−3)：发现序 [1,2,3] */
        static const double c[4] = {-6, 11, -6, 1};
        int k = ac_sym_factor_poly(c, 3);
        expect_true(k == 3 && g_sym_factor_rat[0] == 1 &&
                    g_sym_factor_rat[1] == 2 && g_sym_factor_rat[2] == 3,
                    "factor x^3-6x^2+11x-6 roots 1,2,3");
        expect_true(g_sym_remainder_degree == 0, "cubic fully factored");
    }
    {
        /* x⁴−5x²+4 = (x−1)(x+1)(x−2)(x+2)：发现序 [1,−1,2,−2] */
        static const double c[5] = {4, 0, -5, 0, 1};
        int k = ac_sym_factor_poly(c, 4);
        expect_true(k == 4 && g_sym_factor_rat[0] == 1 &&
                    g_sym_factor_rat[1] == -1 && g_sym_factor_rat[2] == 2 &&
                    g_sym_factor_rat[3] == -2, "factor x^4-5x^2+4 four roots");
        expect_true(g_sym_remainder_degree == 0, "quartic fully factored");
    }
    {
        /* 部分有理：2x³−x−1 = (x−1)(2x²+2x+1)：发现序 [1]，剩余 2x²+2x+1 */
        static const double c[4] = {-1, -1, 0, 2};
        int k = ac_sym_factor_poly(c, 3);
        expect_true(k == 1 && g_sym_factor_rat[0] == 1,
                    "factor 2x^3-x-1 one rational root");
        expect_true(g_sym_remainder_degree == 2 &&
                    g_sym_remainder_coeffs[0] == 1.0 &&
                    g_sym_remainder_coeffs[1] == 2.0 &&
                    g_sym_remainder_coeffs[2] == 2.0,
                    "remainder 2x^2+2x+1");
    }
    {
        /* 不可约：x³−2 → 无有理因式，剩余 degree 3 */
        static const double c[4] = {-2, 0, 0, 1};
        int k = ac_sym_factor_poly(c, 3);
        expect_true(k == 0 && g_sym_factor_count == 0,
                    "factor x^3-2 irreducible count 0");
        expect_true(g_sym_remainder_degree == 3 &&
                    g_sym_remainder_coeffs[0] == -2.0 &&
                    g_sym_remainder_coeffs[3] == 1.0,
                    "irreducible remainder = original");
    }
    {
        /* 分数根：(2x−1)(x−2) = 2x²−5x+2 → 发现序 [2, 1/2] */
        static const double c[3] = {2, -5, 2};
        int k = ac_sym_factor_poly(c, 2);
        expect_true(k == 2 && g_sym_factor_rat[0] == 2 && g_sym_factor_den[0] == 1 &&
                    g_sym_factor_rat[1] == 1 && g_sym_factor_den[1] == 2,
                    "factor 2x^2-5x+2 roots 2, 1/2");
    }
    {
        /* 非法次数 */
        static const double c[6] = {1, 0, 0, 0, 0, 1};
        expect_true(ac_sym_factor_poly(c, 0) == -1, "factor degree 0 -> -1");
        expect_true(ac_sym_factor_poly(c, 5) == -1, "factor degree 5 -> -1");
    }

    printf("==== C99 symbolic: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
