/* ============================================================
 * ApolloCalc-CAS · test_exact.c — 精确数值系统测试（第 3 轮）
 * 覆盖：安全算术原语 / gcd / 根式化简 / 构造函数 / 四则运算 /
 *       判别式提取 / 规范化 / int64 溢出边界（JS 2^53 边界的 C 对应）
 * XL01-XL12 为跨语言用例，与 test/test_exact_xlang.js（JS 基准
 * JS/64KB/src/core_exact.js）逐项比对 num/den/rad/valid。
 * ============================================================ */
#include <stdio.h>
#include <inttypes.h>
#include <math.h>
#include "../src/ac_parser.h"
#include "../src/ac_evaluator.h"
#include "../src/ac_exact.h"

static int s_pass = 0;
static int s_fail = 0;

static void expect_true(int cond, const char *msg) {
    if (cond) { s_pass++; }
    else { s_fail++; printf("FAIL [exact] %s\n", msg); }
}

static int approx(double a, double b, double tol) {
    double d = a - b;
    if (d < 0.0) { d = -d; }
    return d <= tol;
}

/* 三分量 + 有效标志全比对 */
static void check_triple(ac_int_t n, ac_int_t d, ac_int_t r, int v, const char *msg) {
    int ok = g_exact_num == n && g_exact_den == d && g_exact_rad == r && g_exact_valid == v;
    if (ok) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [exact] %s got=(%" PRId64 "/%" PRId64 ")rad=%" PRId64 " valid=%d "
               "want=(%" PRId64 "/%" PRId64 ")rad=%" PRId64 " valid=%d\n",
               msg, g_exact_num, g_exact_den, g_exact_rad, g_exact_valid, n, d, r, v);
    }
}

/* 三分量 + 有效标志 + 近似值比对 */
static void check_triple_approx(ac_int_t n, ac_int_t d, ac_int_t r, int v,
                                double av, const char *msg) {
    int ok = g_exact_num == n && g_exact_den == d && g_exact_rad == r &&
             g_exact_valid == v && approx(g_approx_value, av, 1e-9);
    if (ok) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [exact] %s got=(%" PRId64 "/%" PRId64 ")rad=%" PRId64 " valid=%d approx=%.17g\n",
               msg, g_exact_num, g_exact_den, g_exact_rad, g_exact_valid, g_approx_value);
    }
}

#define TST_SQRT2 1.4142135623730951

int test_exact_run(void) {
    ac_int_t r;
    ac_int_t got;

    /* ==================== 模块一：安全算术原语 ==================== */
    expect_true(ac_safe_mul((ac_int_t)3, (ac_int_t)4, &r) == 1 && r == 12, "safe_mul 3*4");
    expect_true(ac_safe_mul((ac_int_t)-3, (ac_int_t)-4, &r) == 1 && r == 12, "safe_mul (-3)*(-4)");
    expect_true(ac_safe_mul((ac_int_t)-3, (ac_int_t)4, &r) == 1 && r == -12, "safe_mul (-3)*4");
    expect_true(ac_safe_mul(INT64_MAX, (ac_int_t)2, &r) == 0, "safe_mul MAX*2 overflow");
    expect_true(ac_safe_mul(INT64_MIN, (ac_int_t)-1, &r) == 0, "safe_mul MIN*(-1) overflow");
    expect_true(ac_safe_add(INT64_MAX, (ac_int_t)1, &r) == 0, "safe_add MAX+1 overflow");
    expect_true(ac_safe_add(INT64_MIN, (ac_int_t)-1, &r) == 0, "safe_add MIN-1 overflow");
    expect_true(ac_safe_sub(INT64_MIN, (ac_int_t)1, &r) == 0, "safe_sub MIN-1 overflow");
    expect_true(ac_safe_sub(INT64_MAX, (ac_int_t)-1, &r) == 0, "safe_sub MAX+1 overflow");
    expect_true(ac_safe_add((ac_int_t)-5, (ac_int_t)3, &r) == 1 && r == -2, "safe_add -5+3");
    expect_true(ac_safe_sub((ac_int_t)-5, (ac_int_t)3, &r) == 1 && r == -8, "safe_sub -5-3");

    /* ==================== 模块二：gcd ==================== */
    expect_true(ac_gcd((ac_int_t)12, (ac_int_t)18) == 6, "gcd(12,18)");
    expect_true(ac_gcd((ac_int_t)-12, (ac_int_t)18) == 6, "gcd(-12,18)");
    expect_true(ac_gcd((ac_int_t)0, (ac_int_t)5) == 5, "gcd(0,5)");
    expect_true(ac_gcd((ac_int_t)5, (ac_int_t)0) == 5, "gcd(5,0)");

    /* ==================== 模块三：根式化简（写 g_exact_rad） ==================== */
    got = ac_simplify_radical((ac_int_t)8);
    expect_true(got == 2 && g_exact_rad == 2, "simplify_radical(8) -> 2rad2");
    got = ac_simplify_radical((ac_int_t)12);
    expect_true(got == 2 && g_exact_rad == 3, "simplify_radical(12) -> 2rad3");
    got = ac_simplify_radical((ac_int_t)7);
    expect_true(got == 1 && g_exact_rad == 7, "simplify_radical(7) -> rad7");
    got = ac_simplify_radical((ac_int_t)16);
    expect_true(got == 4 && g_exact_rad == 1, "simplify_radical(16) -> 4");
    got = ac_simplify_radical((ac_int_t)1);
    expect_true(got == 1 && g_exact_rad == 1, "simplify_radical(1) -> 1");
    got = ac_simplify_radical((ac_int_t)0);
    expect_true(got == 0 && g_exact_rad == 1, "simplify_radical(0) -> 0");

    /* ==================== 模块四：构造函数 ==================== */
    ac_exact_reset();
    check_triple(0, 1, 1, 0, "reset baseline");

    ac_exact_set_fraction((ac_int_t)2, (ac_int_t)4);
    check_triple_approx(1, 2, 1, 1, 0.5, "XL01 set_fraction(2,4)=1/2");            /* XL01 */

    ac_exact_set_fraction((ac_int_t)-6, (ac_int_t)3);
    check_triple_approx(-2, 1, 1, 1, -2.0, "XL02 set_fraction(-6,3)=-2");          /* XL02 */

    /* 分母为零：仅失效，三分量与 approx 全保持（JS exactSetFraction d==0 分支） */
    ac_exact_set_fraction((ac_int_t)5, (ac_int_t)0);
    check_triple(-2, 1, 1, 0, "set_fraction(5,0) invalid, triple untouched");
    expect_true(approx(g_approx_value, -2.0, 0.0), "set_fraction(5,0) approx untouched");

    /* int64 越界唯一形态：|INT64_MIN| = 2^63 > INT64_MAX（JS 2^53 越界的 C 对应） */
    ac_exact_set_fraction(INT64_MIN, (ac_int_t)1);
    check_triple(-2, 1, 1, 0, "set_fraction(INT64_MIN,1) invalid, triple untouched");
    expect_true(approx(g_approx_value, -9223372036854775808.0, 0.0),
                "set_fraction(INT64_MIN,1) approx=-2^63");

    ac_exact_set_radical((ac_int_t)2, (ac_int_t)8);
    check_triple_approx(4, 1, 2, 1, 4.0 * TST_SQRT2, "XL03 set_radical(2,8)=4rad2");  /* XL03 */

    ac_exact_set_radical((ac_int_t)1, (ac_int_t)4);
    check_triple_approx(2, 1, 1, 1, 2.0, "XL04 set_radical(1,4)=2");               /* XL04 */

    ac_exact_set_radical((ac_int_t)0, (ac_int_t)5);
    check_triple_approx(0, 1, 5, 1, 0.0, "set_radical(0,5)=0, rad=5 kept");

    /* 负被开方数：仅失效，三分量与 approx 全保持 */
    ac_exact_set_radical((ac_int_t)3, (ac_int_t)-2);
    check_triple(0, 1, 5, 0, "set_radical(3,-2) invalid, triple untouched");
    expect_true(approx(g_approx_value, 0.0, 0.0), "set_radical(3,-2) approx untouched");

    ac_exact_set_radical((ac_int_t)2, (ac_int_t)9);
    check_triple_approx(6, 1, 1, 1, 6.0, "set_radical(2,9)=6 (sqrt9=3)");

    ac_exact_set_pure_real(7.0);
    check_triple(7, 1, 1, 1, "set_pure_real(7)");

    ac_exact_set_pure_real(0.5);
    check_triple(7, 1, 1, 0, "set_pure_real(0.5) invalid, triple untouched");
    expect_true(approx(g_approx_value, 0.5, 0.0), "set_pure_real(0.5) approx=0.5");

    /* C 特有边界：1e18 可精确进入 int64 分量（JS 2^53 之外必失效） */
    ac_exact_set_pure_real(1e18);
    check_triple((ac_int_t)1000000000000000000LL, 1, 1, 1, "set_pure_real(1e18) exact");

    ac_exact_set_pure_real(-2.5);
    check_triple((ac_int_t)1000000000000000000LL, 1, 1, 0, "set_pure_real(-2.5) invalid, triple untouched");

    ac_exact_reset();
    check_triple(0, 1, 1, 0, "reset restores defaults");
    expect_true(approx(g_approx_value, 0.0, 0.0), "reset approx=0");

    /* ==================== 模块五：加减（仅纯分数） ==================== */
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)2);
    ac_exact_add((ac_int_t)1, (ac_int_t)3, (ac_int_t)1);
    check_triple_approx(5, 6, 1, 1, 5.0 / 6.0, "XL05 1/2+1/3=5/6");                /* XL05 */

    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)2);
    ac_exact_sub((ac_int_t)1, (ac_int_t)3, (ac_int_t)1);
    check_triple_approx(1, 6, 1, 1, 1.0 / 6.0, "XL10 1/2-1/3=1/6");                /* XL10 */

    /* 根式相加边界：三分量无法表达 -> 失效，且三分量保持旧值 */
    ac_exact_set_radical((ac_int_t)1, (ac_int_t)2);
    ac_exact_add((ac_int_t)1, (ac_int_t)1, (ac_int_t)2);
    check_triple(1, 1, 2, 0, "XL06 rad+rad add invalid, triple retained");         /* XL06 */

    /* 失效态下继续运算保持失效 */
    ac_exact_add((ac_int_t)1, (ac_int_t)2, (ac_int_t)1);
    check_triple(1, 1, 2, 0, "add on invalid stays invalid");

    /* 当前为根式时纯分数加法也失效（exactRad != 1） */
    ac_exact_reset();
    ac_exact_set_radical((ac_int_t)1, (ac_int_t)2);
    ac_exact_add((ac_int_t)1, (ac_int_t)2, (ac_int_t)1);
    check_triple(1, 1, 2, 0, "frac add onto radical invalid");

    ac_exact_reset();
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)2);
    ac_exact_add((ac_int_t)1, (ac_int_t)0, (ac_int_t)1);
    check_triple(1, 2, 1, 0, "add den2=0 invalid");

    /* 同号加法溢出（int64 边界，JS 2^53 对应） */
    ac_exact_reset();
    ac_exact_set_fraction(INT64_MAX, (ac_int_t)1);
    ac_exact_add(INT64_MAX, (ac_int_t)1, (ac_int_t)1);
    check_triple(INT64_MAX, 1, 1, 0, "add overflow at INT64_MAX");

    /* 异号减法不溢出的边界：MAX - (MAX-1) = 1 */
    ac_exact_reset();
    ac_exact_set_fraction(INT64_MAX, (ac_int_t)1);
    ac_exact_sub(INT64_MAX - 1, (ac_int_t)1, (ac_int_t)1);
    check_triple_approx(1, 1, 1, 1, 1.0, "sub MAX-(MAX-1)=1 boundary");

    /* ==================== 模块六：乘法 ==================== */
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)2);
    ac_exact_mul((ac_int_t)2, (ac_int_t)3, (ac_int_t)1);
    check_triple_approx(1, 3, 1, 1, 1.0 / 3.0, "mul 1/2*2/3=1/3");

    ac_exact_set_radical((ac_int_t)1, (ac_int_t)2);
    ac_exact_mul((ac_int_t)1, (ac_int_t)1, (ac_int_t)2);
    check_triple_approx(2, 1, 1, 1, 2.0, "XL07 sqrt2*sqrt2=2");                    /* XL07 */

    ac_exact_set_radical((ac_int_t)1, (ac_int_t)2);
    ac_exact_mul((ac_int_t)1, (ac_int_t)2, (ac_int_t)2);
    check_triple_approx(1, 1, 1, 1, 1.0, "sqrt2*(sqrt2/2)=1");

    ac_exact_reset();
    ac_exact_set_radical((ac_int_t)0, (ac_int_t)5);
    ac_exact_mul((ac_int_t)3, (ac_int_t)2, (ac_int_t)7);
    check_triple(0, 1, 1, 1, "mul zero path num=0");

    ac_exact_reset();
    ac_exact_set_fraction((ac_int_t)3, (ac_int_t)1);
    ac_exact_mul((ac_int_t)1, (ac_int_t)1, (ac_int_t)0);
    check_triple(0, 1, 1, 1, "mul zero path rad2=0");

    ac_exact_reset();
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)1);
    ac_exact_mul((ac_int_t)1, (ac_int_t)0, (ac_int_t)1);
    check_triple(1, 1, 1, 0, "mul den2=0 invalid");

    ac_exact_reset();
    ac_exact_set_radical((ac_int_t)1, (ac_int_t)2);
    ac_exact_mul((ac_int_t)1, (ac_int_t)1, (ac_int_t)-1);
    check_triple(1, 1, 2, 0, "mul rad2<0 invalid");

    ac_exact_reset();
    ac_exact_set_fraction(INT64_MAX, (ac_int_t)1);
    ac_exact_mul((ac_int_t)2, (ac_int_t)1, (ac_int_t)1);
    check_triple(INT64_MAX, 1, 1, 0, "mul overflow at INT64_MAX*2");

    /* ==================== 模块七：除法 ==================== */
    ac_exact_set_fraction((ac_int_t)3, (ac_int_t)1);
    ac_exact_div((ac_int_t)1, (ac_int_t)2, (ac_int_t)1);
    check_triple_approx(6, 1, 1, 1, 6.0, "XL08 3/(1/2)=6");                        /* XL08 */

    ac_exact_set_radical((ac_int_t)2, (ac_int_t)3);
    ac_exact_div((ac_int_t)1, (ac_int_t)2, (ac_int_t)3);
    check_triple_approx(4, 1, 1, 1, 4.0, "XL09 2rad3/(rad3/2)=4");                 /* XL09 */

    ac_exact_reset();
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)1);
    ac_exact_div((ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_triple(1, 1, 1, 0, "div num2=0 invalid");

    ac_exact_reset();
    ac_exact_set_radical((ac_int_t)0, (ac_int_t)5);
    ac_exact_div((ac_int_t)1, (ac_int_t)2, (ac_int_t)2);
    check_triple(0, 1, 1, 1, "0/x=0 exact");

    ac_exact_reset();
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)1);
    ac_exact_div((ac_int_t)1, (ac_int_t)1, (ac_int_t)0);
    check_triple(1, 1, 1, 0, "div rad2=0 invalid");

    ac_exact_reset();
    ac_exact_set_fraction(INT64_MAX, (ac_int_t)1);
    ac_exact_div((ac_int_t)1, INT64_MAX, (ac_int_t)1);
    check_triple(INT64_MAX, 1, 1, 0, "div overflow n1*d2 at boundary");

    ac_exact_reset();
    ac_exact_set_fraction(INT64_MAX, (ac_int_t)1);
    ac_exact_div((ac_int_t)1, (ac_int_t)1, (ac_int_t)1);
    check_triple(INT64_MAX, 1, 1, 1, "div by 1 keeps INT64_MAX");

    /* ==================== 模块八：判别式提取 ==================== */
    ac_exact_from_discriminant(8.0);
    check_triple_approx(2, 1, 2, 1, 2.0 * TST_SQRT2, "XL11 sqrt(8)=2rad2");        /* XL11 */

    ac_exact_from_discriminant(4.0);
    check_triple_approx(2, 1, 1, 1, 2.0, "sqrt(4)=2");

    ac_exact_from_discriminant(0.0);
    check_triple(0, 1, 1, 1, "sqrt(0)=0");

    ac_exact_from_discriminant(-1.0);
    check_triple(0, 1, 1, 0, "sqrt(-1) invalid");

    ac_exact_from_discriminant(0.25);
    check_triple_approx(1, 2, 1, 1, 0.5, "XL12 sqrt(0.25)=1/2");                   /* XL12 */

    ac_exact_from_discriminant(2.0);
    check_triple_approx(1, 1, 2, 1, TST_SQRT2, "sqrt(2)=rad2");

    ac_exact_from_discriminant(1.5);
    check_triple_approx(1, 2, 6, 1, 0.5 * sqrt(6.0), "sqrt(1.5)=(1/2)rad6");

    ac_exact_from_discriminant(0.1);
    check_triple_approx(1, 10, 10, 1, 0.1 * sqrt(10.0), "sqrt(0.1)=(1/10)rad10");

    /* ==================== 模块九：规范化与近似同步 ==================== */
    ac_exact_reset();
    ac_exact_set_radical((ac_int_t)2, (ac_int_t)8);
    ac_exact_normalize();
    check_triple(4, 1, 2, 1, "normalize identity on 4rad2");

    ac_exact_set_fraction((ac_int_t)-6, (ac_int_t)3);
    ac_exact_normalize();
    check_triple(-2, 1, 1, 1, "normalize identity on -2");

    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)2);
    ac_exact_mul((ac_int_t)2, (ac_int_t)3, (ac_int_t)1);
    ac_exact_normalize();
    check_triple(1, 3, 1, 1, "normalize identity on 1/3");

    ac_exact_set_radical((ac_int_t)1, (ac_int_t)2);
    ac_exact_update_approx();
    expect_true(approx(g_approx_value, TST_SQRT2, 1e-9), "update_approx sqrt2");

    printf("==== C99 exact: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
