/* ============================================================
 * ApolloCalc-CAS · test_calculus.c — 数值微积分测试（C 128KB 第 3 轮）
 * 行为基准：JS/128KB/src/core_calculus.js（文件 7），参数逐项对齐：
 *   导数 h=1e-5·max(|x|,1) / 二阶 h=1e-4 / 积分 n=100..10000 阈值 1e-8 /
 *   极限 8 步 + 1e-6（JS 最终版；6 步 + 1e-8 对一阶逼近型恒不收敛）/
 *   泰勒 k≥3 中心差分表 h = eps^(1/(K+2))·max(|x0|,1)。
 * 容差依据：中心差分误差 O(h²)+舍入 → 1e-6；辛普森收敛停机点残差 ~1e-10
 *   → 1e-7；泰勒高阶系数舍入放大 ~1e-8 → 1e-6（k≥10 固有衰减）。
 * 专测钉住的契约：
 *   - RPN 缓存：表达式变更后不 reset 会复用旧 RPN（可观测），reset 后恢复；
 *   - 迭代次数确定性：一阶=2、二阶=5、∫x dx=2+99+199=300、a==b=0。
 * ============================================================ */
#include <stdio.h>
#include <math.h>
#include "../src/ac_types.h"
#include "../src/ac_parser.h"
#include "../src/ac_evaluator.h"
#include "../src/ac_math.h"
#include "../src/ac_calculus.h"

static int s_pass = 0;
static int s_fail = 0;

#define TST_PI 3.141592653589793
#define TST_E  2.718281828459045

static int approx(double a, double b, double tol) {
    double d = a - b;
    if (d < 0.0) { d = -d; }
    return d <= tol;
}

/* LUT 构建特有取舍（第 5 轮同款思路）：sin/cos 走 1024 项查表（最大误差
   4.7e-6），微积分的差分运算会放大表误差——一阶导/积分/极限实测 ~6.3e-6，
   泰勒 k=3 差分表 ÷h³ 放大到 ~5e-3，k=5 ÷h⁵ 到 O(1)（数值求导高阶的
   固有极限在 LUT 下提前到来）。sin 相关用例按实测值放宽一档；非 sin
   用例（多项式 / exp 走 libm）容差不变。 */
#ifdef AC_MATH_USE_LUT
#define SIN_D_TOL   1e-4    /* d/dx sin */
#define SIN_I_TOL   1e-4    /* ∫ sin */
#define SIN_L_TOL   1e-4    /* lim sin/x */
#define SIN_C1_TOL  1e-4
#define SIN_C2_TOL  1e-7
#define SIN_C3_TOL  1e-2
#define SIN_C5_MODE 0       /* k=5 差分表噪声 O(1)：只做 sanity */
#define SIN_C6_TOL  1e-5
#define SIN_EV_TOL  1e-4
#else
#define SIN_D_TOL   1e-6
#define SIN_I_TOL   1e-7
#define SIN_L_TOL   1e-6
#define SIN_C1_TOL  1e-9
#define SIN_C2_TOL  1e-9
#define SIN_C3_TOL  1e-4
#define SIN_C5_MODE 1
#define SIN_C6_TOL  1e-6
#define SIN_EV_TOL  1e-6
#endif

static void expect_true(int cond, const char *name) {
    if (cond) { s_pass++; }
    else { s_fail++; printf("FAIL [calc] %s\n", name); }
}

/* 一阶导数端到端：reset → parse → derivative，校验 err=0 + 值 */
static void d_ok(const char *src, double x, double want, double tol, const char *name) {
    double got;
    ac_calculus_reset();
    ac_parse_expression(src);
    got = ac_calc_derivative(x);
    if (g_calc_error == 0 && got == got && approx(got, want, tol)) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [calc] %s: got=%.17g err=%d want=%.17g\n",
               name, got, g_calc_error, want);
    }
}

/* 二阶导数端到端 */
static void d2_ok(const char *src, double x, double want, double tol, const char *name) {
    double got;
    ac_calculus_reset();
    ac_parse_expression(src);
    got = ac_calc_derivative2(x);
    if (g_calc_error == 0 && got == got && approx(got, want, tol)) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [calc] %s: got=%.17g err=%d want=%.17g\n",
               name, got, g_calc_error, want);
    }
}

/* 定积分端到端 */
static void int_ok(const char *src, double a, double b, double want, double tol,
                   const char *name) {
    double got;
    ac_calculus_reset();
    ac_parse_expression(src);
    got = ac_calc_integral(a, b);
    if (g_calc_error == 0 && got == got && approx(got, want, tol)) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [calc] %s: got=%.17g err=%d want=%.17g\n",
               name, got, g_calc_error, want);
    }
}

/* 极限端到端（成功路径） */
static void lim_ok(const char *src, double a, int side, double want, double tol,
                   const char *name) {
    double got;
    ac_calculus_reset();
    ac_parse_expression(src);
    got = ac_calc_limit(a, side);
    if (g_calc_error == 0 && got == got && approx(got, want, tol)) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [calc] %s: got=%.17g err=%d want=%.17g\n",
               name, got, g_calc_error, want);
    }
}

/* 极限失败路径（err 码校验 + NaN 返回） */
static void lim_err(const char *src, double a, int side, int want_err, const char *name) {
    double got;
    ac_calculus_reset();
    ac_parse_expression(src);
    got = ac_calc_limit(a, side);
    if (g_calc_error == want_err && got != got) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [calc] %s: got=%.17g err=%d want err=%d\n",
               name, got, g_calc_error, want_err);
    }
}

/* 失败路径通用（err 码 + NaN） */
static void op_err(const char *src, double x, int want_err, const char *name) {
    double got;
    ac_calculus_reset();
    ac_parse_expression(src);
    got = ac_calc_derivative(x);
    if (g_calc_error == want_err && got != got) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [calc] %s: got=%.17g err=%d want err=%d\n",
               name, got, g_calc_error, want_err);
    }
}

int test_calculus_run(void) {
    /* ==================== Part A：一阶导数（中心差分） ==================== */
    g_angle_deg = 0;   /* RAD：sin/exp 用弧度基准值 */
    d_ok("x^2", 3.0, 6.0, 1e-6, "d/dx x^2 @3 = 6");
    d_ok("x^3", 2.0, 12.0, 1e-6, "d/dx x^3 @2 = 12");
    d_ok("sin(x)", 0.0, 1.0, SIN_D_TOL, "d/dx sin @0 = 1 (RAD)");
    d_ok("exp(x)", 1.0, TST_E, 1e-6, "d/dx exp @1 = e");
    d_ok("x^2", 0.0, 0.0, 1e-9, "d/dx x^2 @0 = 0 (boundary)");
    expect_true(g_calc_iterations == 2, "derivative iterations = 2");
    g_angle_deg = 1;

    /* ==================== Part B：二阶导数（五点公式） ==================== */
    g_angle_deg = 0;
    d2_ok("x^3", 2.0, 12.0, 1e-6, "d2/dx2 x^3 @2 = 12");
    d2_ok("sin(x)", 0.0, 0.0, 1e-6, "d2/dx2 sin @0 = 0 (odd symmetry)");
    d2_ok("x^4", 1.0, 12.0, 1e-6, "d2/dx2 x^4 @1 = 12");
    expect_true(g_calc_iterations == 5, "derivative2 iterations = 5");
    g_angle_deg = 1;

    /* ==================== Part C：定积分（自适应辛普森） ==================== */
    g_angle_deg = 0;
    int_ok("x", 0.0, 1.0, 0.5, 1e-9, "int[0,1] x = 0.5");
    expect_true(g_calc_iterations == 300, "integral x dx iterations = 2+99+199");
    int_ok("x^2", 0.0, 1.0, 1.0 / 3.0, 1e-7, "int[0,1] x^2 = 1/3");
    int_ok("x^2", -1.0, 1.0, 2.0 / 3.0, 1e-7, "int[-1,1] x^2 = 2/3");
    int_ok("sin(x)", 0.0, TST_PI, 2.0, SIN_I_TOL, "int[0,pi] sin = 2 (RAD)");
    int_ok("exp(x)", 0.0, 1.0, TST_E - 1.0, 1e-7, "int[0,1] exp = e-1");
    int_ok("1", 0.0, 5.0, 5.0, 1e-9, "int[0,5] 1 = 5");
    int_ok("x^3", 0.0, 1.0, 0.25, 1e-9, "int[0,1] x^3 = 0.25");
    g_angle_deg = 1;
    /* 空区间：a==b 在方程检查之前短路（JS 顺序），err=0 结果 0 */
    ac_calculus_reset();
    ac_parse_expression("x^2");
    expect_true(ac_calc_integral(3.0, 3.0) == 0.0 && g_calc_error == 0 &&
                g_calc_iterations == 0, "integral [3,3] = 0 (empty)");

    /* ==================== Part D：极限（8 步 + 1e-6） ==================== */
    g_angle_deg = 0;
    lim_ok("sin(x)/x", 0.0, 0, 1.0, SIN_L_TOL, "lim sin(x)/x @0 = 1 (both sides)");
    lim_ok("(x^2-1)/(x-1)", 1.0, 0, 2.0, 1e-6, "lim (x^2-1)/(x-1) @1 = 2");
    lim_ok("(x^2-1)/(x-1)", 1.0, -1, 2.0, 1e-6, "left limit = 2");
    lim_ok("(x^2-1)/(x-1)", 1.0, 1, 2.0, 1e-6, "right limit = 2");
    lim_ok("exp(x)", 0.0, 0, 1.0, 1e-6, "lim exp @0 = 1 (continuous)");
    lim_err("1/x", 0.0, 0, 2, "lim 1/x @0 diverges -> err=2");
    lim_err("1/x", 0.0, 1, 2, "lim 1/x right diverges -> err=2");
    g_angle_deg = 1;

    /* ==================== Part E：泰勒展开（中心差分表） ==================== */
    g_angle_deg = 0;
    ac_calculus_reset();
    ac_parse_expression("sin(x)");
    expect_true(ac_calc_taylor(0.0, 6) == 0 && g_calc_error == 0,
                "taylor sin @0 order 6 ok");
    expect_true(g_taylor_order == 6 && g_taylor_center == 0.0,
                "taylor order/center cached");
    /* sin 的偶数阶系数为 0（奇函数对称性在差分表中精确相消） */
    expect_true(g_taylor_coeffs[0] == 0.0, "taylor sin c0 = 0 exact");
    expect_true(approx(g_taylor_coeffs[1], 1.0, SIN_C1_TOL), "taylor sin c1 = 1");
    expect_true(fabs(g_taylor_coeffs[2]) < SIN_C2_TOL, "taylor sin c2 = 0");
    /* 奇数阶跨中心平均估计带固有 O(h²)·f⁽⁵⁾/24 截断（实测 |err| = h²/24），
       容差按算法实际精度取 1e-4——JS 圣经同款噪声，勿收紧 */
    expect_true(approx(g_taylor_coeffs[3], -1.0 / 6.0, SIN_C3_TOL), "taylor sin c3 = -1/6");
    expect_true(fabs(g_taylor_coeffs[4]) < 1e-6, "taylor sin c4 = 0");
#if SIN_C5_MODE
    expect_true(approx(g_taylor_coeffs[5], 1.0 / 120.0, 1e-6), "taylor sin c5 = 1/120");
#else
    /* LUT 下 k=5 差分 ÷h⁵ 把表误差放大到 O(1)：只验有限且量级 sane */
    expect_true(fabs(g_taylor_coeffs[5]) < 10.0, "taylor sin c5 finite (LUT sanity)");
#endif
    expect_true(fabs(g_taylor_coeffs[6]) < SIN_C6_TOL, "taylor sin c6 = 0");
    expect_true(approx(ac_taylor_eval(0.1), sin(0.1), SIN_EV_TOL),
                "taylor_eval(0.1) ~ sin(0.1)");

    ac_calculus_reset();
    ac_parse_expression("exp(x)");
    expect_true(ac_calc_taylor(0.0, 5) == 0, "taylor exp @0 order 5 ok");
    expect_true(approx(g_taylor_coeffs[1], 1.0, 1e-9), "taylor exp c1 = 1");
    /* c2 容差 1e-7：五点公式舍入放大 ~1e-8（12h²=1.2e-7 分母） */
    expect_true(approx(g_taylor_coeffs[2], 0.5, 1e-7), "taylor exp c2 = 1/2");
    expect_true(approx(g_taylor_coeffs[3], 1.0 / 6.0, 1e-4), "taylor exp c3 = 1/6");
    expect_true(approx(g_taylor_coeffs[4], 1.0 / 24.0, 1e-6), "taylor exp c4 = 1/24");
    expect_true(approx(g_taylor_coeffs[5], 1.0 / 120.0, 1e-6), "taylor exp c5 = 1/120");
    expect_true(approx(ac_taylor_eval(0.5), exp(0.5), 1e-4),
                "taylor_eval(0.5) ~ e^0.5 (order-5 truncation ~2e-5)");

    /* 展开中心非零：x^2 @ 2 → c0=4, c1=4, c2=1；taylor_eval(3) = 9 */
    ac_calculus_reset();
    ac_parse_expression("x^2");
    expect_true(ac_calc_taylor(2.0, 2) == 0, "taylor x^2 @2 ok");
    expect_true(approx(g_taylor_coeffs[0], 4.0, 1e-9) &&
                approx(g_taylor_coeffs[1], 4.0, 1e-6) &&
                approx(g_taylor_coeffs[2], 1.0, 1e-6), "taylor x^2 @2 coeffs 4/4/1");
    expect_true(approx(ac_taylor_eval(3.0), 9.0, 1e-6), "taylor_eval(3) = 9");

    /* 零阶：只有常数项 */
    expect_true(ac_calc_taylor(2.0, 0) == 0 && g_taylor_order == 0 &&
                g_taylor_coeffs[0] == 4.0, "taylor order 0 = f(2) = 4");
    expect_true(ac_taylor_eval(100.0) == 4.0, "order-0 eval constant anywhere");

    /* 阶数超限 */
    expect_true(ac_calc_taylor(0.0, 17) == -1 && g_calc_error == 3,
                "taylor order 17 -> err=3");
    expect_true(ac_calc_taylor(0.0, -1) == -1 && g_calc_error == 3,
                "taylor order -1 -> err=3");
    g_angle_deg = 1;

    /* ==================== Part F：错误路径 ==================== */
    /* 方程输入（含 =）→ 域错误 */
    op_err("x^2=4", 1.0, 1, "derivative on equation -> err=1");
    /* 非有限入参 */
    op_err("x^2", (double)NAN, 1, "derivative NaN -> err=1");
    {
        double got;
        ac_calculus_reset();
        ac_parse_expression("x^2");
        got = ac_calc_integral((double)NAN, 1.0);
        expect_true(g_calc_error == 1 && got != got, "integral NaN bound -> err=1");
        got = ac_calc_limit((double)NAN, 0);
        expect_true(g_calc_error == 1 && got != got, "limit NaN point -> err=1");
        expect_true(ac_calc_taylor((double)NAN, 2) == -1 && g_calc_error == 1,
                    "taylor NaN center -> err=1");
    }
    /* 采样点出域：ln(x) 在负半轴 NaN → 积分域错误 */
    ac_calculus_reset();
    ac_parse_expression("ln(x)");
    {
        double got = ac_calc_integral(-1.0, 1.0);
        expect_true(g_calc_error == 1 && got != got,
                    "integral of ln over [-1,1] -> domain err=1");
    }

    /* ==================== Part G：RPN 缓存契约（表达式变更必须 reset） ====================
     * T_NUM 在 RPN 中存数字池下标：新旧表达式同构时旧 RPN + 新数字池会
     * "碰巧正确"，故用结构差异钉契约——新表达式 "x" 无数字，旧 RPN
     * （x^2+1）整体失效但仍被求值 → 结果停在旧表达式导数 2（真值 1）。 */
    ac_calculus_reset();
    ac_parse_expression("x^2+1");
    expect_true(approx(ac_calc_derivative(1.0), 2.0, 1e-6), "cache: d(x^2+1)@1 = 2");
    /* 不 reset 直接换表达式：复用旧结构 RPN → 结果 2（x 的真导数是 1） */
    ac_parse_expression("x");
    expect_true(approx(ac_calc_derivative(1.0), 2.0, 1e-6),
                "stale RPN reused without reset (documented hazard)");
    /* reset 后恢复正确 */
    ac_calculus_reset();
    ac_parse_expression("x");
    expect_true(approx(ac_calc_derivative(1.0), 1.0, 1e-6),
                "reset clears cache: d(x)@1 = 1");

    printf("==== C99 calculus: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
