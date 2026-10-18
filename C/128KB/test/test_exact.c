/* ============================================================
 * ApolloCalc-CAS · test_exact.c — 精确数值系统测试（C 128KB 第 1 轮）
 * 五分量模型：value = ((rat + radNum·√rad) / den) × 10^exp
 * 行为基准：JS/128KB/src/core_exact.js（全部新用例期望值经该实现
 * 核算，见 test/test_exact_xlang.js 驱动输出；勿凭记忆改值）。
 * 覆盖：安全原语 / gcd / 根式化简 / 五种构造 / 混合形式 / 科学计数法 /
 *       四则（同类根式合并 + 异类失效 + 共轭有理化）/ 判别式提取 /
 *       规范化 / INT64 溢出边界。
 * 相对 64KB 三分量的语义升级（旧期望已按 JS 128KB 圣经更新）：
 *   ①set_radical(0,m)：rad 归 1（radNum==0 ⟺ rad==1 不变式），不再保留
 *   ②√2+√2 = 2√2 有效合并（同类根式）；√2+√3 失效
 *   ③纯分数加到根式上有效合并：(1+2√2)/2
 *   ④mul 操作数 rad2==0 → 失效（rad 必须 ≥ 1），不再走零路径
 *   ⑤√2·√3 失效（三根式项路径；任务卡草稿 "=√6" 不采用，圣经优先）
 * XL01-XL12 为跨语言用例（纯分数/纯根式期望与 64KB 一致，字段重命名）。
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

/* 五分量 + 有效标志全比对 */
static void check_exact6(ac_int_t rat, ac_int_t rn, ac_int_t rad, ac_int_t den,
                         int exp, int v, const char *msg) {
    int ok = g_exact_rat == rat && g_exact_rad_num == rn && g_exact_rad == rad &&
             g_exact_den == den && g_exact_exp == exp && g_exact_valid == v;
    if (ok) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [exact] %s got=(%" PRId64 "+%" PRId64 "rad%" PRId64 ")/%" PRId64
               "e%d valid=%d want=(%" PRId64 "+%" PRId64 "rad%" PRId64 ")/%" PRId64
               "e%d valid=%d\n",
               msg, g_exact_rat, g_exact_rad_num, g_exact_rad, g_exact_den,
               g_exact_exp, g_exact_valid, rat, rn, rad, den, exp, v);
    }
}

/* 五分量 + 有效标志 + 近似值比对 */
static void check_exact6_approx(ac_int_t rat, ac_int_t rn, ac_int_t rad, ac_int_t den,
                                int exp, int v, double av, const char *msg) {
    int ok = g_exact_rat == rat && g_exact_rad_num == rn && g_exact_rad == rad &&
             g_exact_den == den && g_exact_exp == exp && g_exact_valid == v &&
             approx(g_approx_value, av, 1e-9);
    if (ok) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [exact] %s got=(%" PRId64 "+%" PRId64 "rad%" PRId64 ")/%" PRId64
               "e%d valid=%d approx=%.17g\n",
               msg, g_exact_rat, g_exact_rad_num, g_exact_rad, g_exact_den,
               g_exact_exp, g_exact_valid, g_approx_value);
    }
}

#define TST_SQRT2 1.4142135623730951
#define TST_SQRT5 2.2360679774997896

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
    check_exact6(0, 0, 1, 1, 0, 0, "reset baseline");

    ac_exact_set_fraction((ac_int_t)2, (ac_int_t)4);
    check_exact6_approx(1, 0, 1, 2, 0, 1, 0.5, "XL01 set_fraction(2,4)=1/2");       /* XL01 */

    ac_exact_set_fraction((ac_int_t)-6, (ac_int_t)3);
    check_exact6_approx(-2, 0, 1, 1, 0, 1, -2.0, "XL02 set_fraction(-6,3)=-2");     /* XL02 */

    /* 分母为零：仅失效，五分量与 approx 全保持 */
    ac_exact_set_fraction((ac_int_t)5, (ac_int_t)0);
    check_exact6(-2, 0, 1, 1, 0, 0, "set_fraction(5,0) invalid, state untouched");
    expect_true(approx(g_approx_value, -2.0, 0.0), "set_fraction(5,0) approx untouched");

    /* int64 越界唯一形态：INT64_MIN（|x| = 2^63 > INT64_MAX） */
    ac_exact_set_fraction(INT64_MIN, (ac_int_t)1);
    check_exact6(-2, 0, 1, 1, 0, 0, "set_fraction(INT64_MIN,1) invalid, state untouched");
    expect_true(approx(g_approx_value, -9223372036854775808.0, 0.0),
                "set_fraction(INT64_MIN,1) approx=-2^63");

    ac_exact_set_fraction((ac_int_t)1, INT64_MIN);
    check_exact6(-2, 0, 1, 1, 0, 0, "set_fraction(1,INT64_MIN) invalid");
    expect_true(approx(g_approx_value, 1.0 / -9223372036854775808.0, 1e-30),
                "set_fraction(1,INT64_MIN) approx tiny");

    ac_exact_set_radical((ac_int_t)2, (ac_int_t)8);
    check_exact6_approx(0, 4, 2, 1, 0, 1, 4.0 * TST_SQRT2, "XL03 set_radical(2,8)=4rad2"); /* XL03 */

    ac_exact_set_radical((ac_int_t)1, (ac_int_t)4);
    check_exact6_approx(2, 0, 1, 1, 0, 1, 2.0, "XL04 set_radical(1,4)=2");          /* XL04 */

    /* 128KB 语义升级：radNum==0 ⟺ rad==1，零值 rad 不再保留化简值 */
    ac_exact_set_radical((ac_int_t)0, (ac_int_t)5);
    check_exact6_approx(0, 0, 1, 1, 0, 1, 0.0, "set_radical(0,5)=0, rad normalized to 1");

    /* 负被开方数：仅失效，五分量与 approx 全保持 */
    ac_exact_set_radical((ac_int_t)3, (ac_int_t)-2);
    check_exact6(0, 0, 1, 1, 0, 0, "set_radical(3,-2) invalid, state untouched");
    expect_true(approx(g_approx_value, 0.0, 0.0), "set_radical(3,-2) approx untouched");

    ac_exact_set_radical((ac_int_t)2, (ac_int_t)9);
    check_exact6_approx(6, 0, 1, 1, 0, 1, 6.0, "set_radical(2,9)=6 (sqrt9=3)");

    ac_exact_set_radical(INT64_MIN, (ac_int_t)5);
    check_exact6(6, 0, 1, 1, 0, 0, "set_radical(INT64_MIN,5) invalid, state untouched");
    expect_true(approx(g_approx_value, (double)INT64_MIN * sqrt(5.0), 0.0),
                "set_radical(INT64_MIN,5) approx kept");

    ac_exact_set_pure_real(7.0);
    check_exact6(7, 0, 1, 1, 0, 1, "set_pure_real(7)");

    ac_exact_set_pure_real(0.5);
    check_exact6(7, 0, 1, 1, 0, 0, "set_pure_real(0.5) invalid, state untouched");
    expect_true(approx(g_approx_value, 0.5, 0.0), "set_pure_real(0.5) approx=0.5");

    /* C 特有边界：1e18 可精确进入 int64 分量（JS 2^53 之外必失效） */
    ac_exact_set_pure_real(1e18);
    check_exact6((ac_int_t)1000000000000000000LL, 0, 1, 1, 0, 1, "set_pure_real(1e18) exact");

    ac_exact_set_pure_real(-2.5);
    check_exact6((ac_int_t)1000000000000000000LL, 0, 1, 1, 0, 0, "set_pure_real(-2.5) invalid");

    /* ---- 混合形式（五分量新能力；期望值经 JS 128KB 圣经核算） ---- */
    ac_exact_reset();
    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)1, (ac_int_t)5, (ac_int_t)2);
    check_exact6_approx(1, 1, 5, 2, 0, 1, (1.0 + TST_SQRT5) / 2.0,
                        "set_mixed golden (1+rad5)/2");

    ac_exact_set_mixed((ac_int_t)3, (ac_int_t)-2, (ac_int_t)7, (ac_int_t)5);
    check_exact6_approx(3, -2, 7, 5, 0, 1, (3.0 - 2.0 * sqrt(7.0)) / 5.0,
                        "set_mixed (3-2rad7)/5");

    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)1, (ac_int_t)8, (ac_int_t)2);
    check_exact6_approx(1, 2, 2, 2, 0, 1, (1.0 + 2.0 * TST_SQRT2) / 2.0,
                        "set_mixed sqrt8 extract -> (1+2rad2)/2");

    ac_exact_set_mixed((ac_int_t)2, (ac_int_t)4, (ac_int_t)9, (ac_int_t)4);
    check_exact6_approx(7, 0, 1, 2, 0, 1, 3.5,
                        "set_mixed perfect-sq chain -> 7/2");

    ac_exact_set_mixed((ac_int_t)0, (ac_int_t)0, (ac_int_t)5, (ac_int_t)3);
    check_exact6_approx(0, 0, 1, 1, 0, 1, 0.0, "set_mixed zero -> 0/0/1/1/0");

    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)1, (ac_int_t)5, (ac_int_t)-2);
    check_exact6_approx(-1, -1, 5, 2, 0, 1, -(1.0 + TST_SQRT5) / 2.0,
                        "set_mixed neg den -> sign flipped");

    ac_exact_set_mixed((ac_int_t)5, (ac_int_t)9, (ac_int_t)0, (ac_int_t)2);
    check_exact6_approx(5, 0, 1, 2, 0, 1, 2.5, "set_mixed c=0 -> pure fraction 5/2");

    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)1, (ac_int_t)5, (ac_int_t)0);
    check_exact6(5, 0, 1, 2, 0, 0, "set_mixed d=0 invalid, state untouched");
    expect_true(approx(g_approx_value, 2.5, 0.0), "set_mixed d=0 approx untouched");

    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)1, (ac_int_t)-5, (ac_int_t)2);
    check_exact6(5, 0, 1, 2, 0, 0, "set_mixed c<0 invalid, state untouched");

    ac_exact_set_mixed(INT64_MIN, (ac_int_t)1, (ac_int_t)5, (ac_int_t)2);
    check_exact6(5, 0, 1, 2, 0, 0, "set_mixed rat INT64_MIN invalid");

    /* ---- 科学计数法（五分量新能力） ---- */
    ac_exact_reset();
    ac_exact_set_sci((ac_int_t)25, 3);
    check_exact6_approx(25, 0, 1, 1, 3, 1, 25000.0, "set_sci(25,3)=2.5e4");

    ac_exact_set_sci((ac_int_t)0, 5);
    check_exact6_approx(0, 0, 1, 1, 0, 1, 0.0, "set_sci(0,5) zero keeps exp=0");

    ac_exact_set_sci((ac_int_t)1, -3);
    check_exact6_approx(1, 0, 1, 1, -3, 1, 0.001, "set_sci(1,-3)=1e-3");

    ac_exact_set_sci((ac_int_t)1, -18);
    check_exact6_approx(1, 0, 1, 1, -18, 1, 1e-18, "set_sci(1,-18)=1e-18");

    ac_exact_set_sci(INT64_MIN, 3);
    check_exact6(1, 0, 1, 1, -18, 0, "set_sci(INT64_MIN,3) invalid, state untouched");

    /* 当前值带指数时不参与四则（JS exp!=0 门禁） */
    ac_exact_set_sci((ac_int_t)25, 3);
    ac_exact_add((ac_int_t)1, (ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_exact6(25, 0, 1, 1, 3, 0, "sci then add -> invalid (exp gate), state kept");

    ac_exact_reset();
    check_exact6(0, 0, 1, 1, 0, 0, "reset restores defaults");
    expect_true(approx(g_approx_value, 0.0, 0.0), "reset approx=0");

    /* ==================== 模块五：加法（同类根式合并） ==================== */
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)2);
    ac_exact_add((ac_int_t)1, (ac_int_t)0, (ac_int_t)1, (ac_int_t)3);
    check_exact6_approx(5, 0, 1, 6, 0, 1, 5.0 / 6.0, "XL05 1/2+1/3=5/6");           /* XL05 */

    /* 128KB 语义升级：√2+√2 = 2√2（同类根式合并，JS 圣经 valid） */
    ac_exact_set_radical((ac_int_t)1, (ac_int_t)2);
    ac_exact_add((ac_int_t)0, (ac_int_t)1, (ac_int_t)2, (ac_int_t)1);
    check_exact6_approx(0, 2, 2, 1, 0, 1, 2.0 * TST_SQRT2, "sqrt2+sqrt2=2sqrt2");

    /* 合并态继续加纯分数仍有效：1 + 2√2 */
    ac_exact_add((ac_int_t)1, (ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_exact6_approx(1, 2, 2, 1, 0, 1, 1.0 + 2.0 * TST_SQRT2,
                        "2sqrt2 + 1 = 1+2rad2");

    /* 128KB 语义升级：纯分数加到根式上有效合并 */
    ac_exact_reset();
    ac_exact_set_radical((ac_int_t)1, (ac_int_t)2);
    ac_exact_add((ac_int_t)1, (ac_int_t)0, (ac_int_t)1, (ac_int_t)2);
    check_exact6_approx(1, 2, 2, 2, 0, 1, (1.0 + 2.0 * TST_SQRT2) / 2.0,
                        "rad + frac = (1+2rad2)/2");

    /* 异类根式之和：五分量无法表达 → 失效（√2+√3），保持旧值 */
    ac_exact_set_radical((ac_int_t)1, (ac_int_t)2);
    ac_exact_add((ac_int_t)1, (ac_int_t)1, (ac_int_t)3, (ac_int_t)1);
    check_exact6(0, 1, 2, 1, 0, 0, "sqrt2+sqrt3 invalid, state retained");

    /* 失效态下继续运算保持失效、五分量保持旧值 */
    ac_exact_add((ac_int_t)1, (ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_exact6(0, 1, 2, 1, 0, 0, "add on invalid stays invalid");

    ac_exact_reset();
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)2);
    ac_exact_add((ac_int_t)1, (ac_int_t)0, (ac_int_t)1, (ac_int_t)0);
    check_exact6(1, 0, 1, 2, 0, 0, "add den2=0 invalid");

    /* 操作数 INT64_MIN：取负前拒绝 */
    ac_exact_reset();
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)2);
    ac_exact_add(INT64_MIN, (ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_exact6(1, 0, 1, 2, 0, 0, "add rat2=INT64_MIN invalid");

    /* 同号加法溢出（INT64 边界） */
    ac_exact_reset();
    ac_exact_set_fraction(INT64_MAX, (ac_int_t)1);
    ac_exact_add(INT64_MAX, (ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_exact6(INT64_MAX, 0, 1, 1, 0, 0, "add overflow at INT64_MAX");

    /* 右操作数根式部并入有理部时溢出（JS |a|+|b| 保守预检路径） */
    ac_exact_reset();
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)1);
    ac_exact_add(INT64_MAX, INT64_MAX, (ac_int_t)1, (ac_int_t)1);
    check_exact6(1, 0, 1, 1, 0, 0, "add fold overflow at INT64_MAX+INT64_MAX");

    /* 异号减法不溢出的边界：MAX - (MAX-1) = 1 */
    ac_exact_reset();
    ac_exact_set_fraction(INT64_MAX, (ac_int_t)1);
    ac_exact_sub(INT64_MAX - 1, (ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_exact6_approx(1, 0, 1, 1, 0, 1, 1.0, "sub MAX-(MAX-1)=1 boundary");

    ac_exact_reset();
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)2);
    ac_exact_sub((ac_int_t)1, (ac_int_t)0, (ac_int_t)1, (ac_int_t)3);
    check_exact6_approx(1, 0, 1, 6, 0, 1, 1.0 / 6.0, "XL10 1/2-1/3=1/6");           /* XL10 */

    ac_exact_reset();
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)2);
    ac_exact_sub(INT64_MIN, (ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_exact6(1, 0, 1, 2, 0, 0, "sub rat2=INT64_MIN invalid");

    /* ---- 混合形式加法 ---- */
    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)1, (ac_int_t)5, (ac_int_t)2);
    ac_exact_add((ac_int_t)1, (ac_int_t)-1, (ac_int_t)5, (ac_int_t)2);
    check_exact6_approx(1, 0, 1, 1, 0, 1, 1.0, "golden + conj = 1");

    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)1, (ac_int_t)5, (ac_int_t)2);
    ac_exact_add((ac_int_t)1, (ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_exact6_approx(3, 1, 5, 2, 0, 1, (3.0 + TST_SQRT5) / 2.0, "golden + 1");

    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)2, (ac_int_t)3, (ac_int_t)5);
    ac_exact_add((ac_int_t)3, (ac_int_t)4, (ac_int_t)3, (ac_int_t)5);
    check_exact6_approx(4, 6, 3, 5, 0, 1, (4.0 + 6.0 * sqrt(3.0)) / 5.0,
                        "mixed + mixed same rad");

    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)2, (ac_int_t)3, (ac_int_t)5);
    ac_exact_sub((ac_int_t)3, (ac_int_t)4, (ac_int_t)3, (ac_int_t)5);
    check_exact6_approx(-2, -2, 3, 5, 0, 1, (-2.0 - 2.0 * sqrt(3.0)) / 5.0,
                        "mixed - mixed same rad");

    /* ==================== 模块六：乘法 ==================== */
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)2);
    ac_exact_mul((ac_int_t)2, (ac_int_t)0, (ac_int_t)1, (ac_int_t)3);
    check_exact6_approx(1, 0, 1, 3, 0, 1, 1.0 / 3.0, "mul 1/2*2/3=1/3");

    ac_exact_set_radical((ac_int_t)1, (ac_int_t)2);
    ac_exact_mul((ac_int_t)0, (ac_int_t)1, (ac_int_t)2, (ac_int_t)1);
    check_exact6_approx(2, 0, 1, 1, 0, 1, 2.0, "XL07 sqrt2*sqrt2=2");               /* XL07 */

    ac_exact_set_radical((ac_int_t)1, (ac_int_t)2);
    ac_exact_mul((ac_int_t)0, (ac_int_t)1, (ac_int_t)2, (ac_int_t)2);
    check_exact6_approx(1, 0, 1, 1, 0, 1, 1.0, "sqrt2*(sqrt2/2)=1");

    /* 异类根式乘积：三根式项不可表达 → 失效（√2·√3，圣经裁决） */
    ac_exact_set_radical((ac_int_t)1, (ac_int_t)2);
    ac_exact_mul((ac_int_t)1, (ac_int_t)1, (ac_int_t)3, (ac_int_t)1);
    check_exact6(0, 1, 2, 1, 0, 0, "sqrt2*sqrt3 invalid, state retained");

    ac_exact_reset();
    ac_exact_set_radical((ac_int_t)0, (ac_int_t)5);
    ac_exact_mul((ac_int_t)3, (ac_int_t)0, (ac_int_t)7, (ac_int_t)2);
    check_exact6(0, 0, 1, 1, 0, 1, "mul zero path num=0");

    /* 128KB 语义升级：rad2==0 非法（rad >= 1 门禁），不再走零路径 */
    ac_exact_reset();
    ac_exact_set_fraction((ac_int_t)3, (ac_int_t)1);
    ac_exact_mul((ac_int_t)1, (ac_int_t)0, (ac_int_t)0, (ac_int_t)1);
    check_exact6(3, 0, 1, 1, 0, 0, "mul rad2=0 invalid");

    ac_exact_reset();
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)1);
    ac_exact_mul((ac_int_t)1, (ac_int_t)0, (ac_int_t)1, (ac_int_t)0);
    check_exact6(1, 0, 1, 1, 0, 0, "mul den2=0 invalid");

    ac_exact_reset();
    ac_exact_set_radical((ac_int_t)1, (ac_int_t)2);
    ac_exact_mul((ac_int_t)1, (ac_int_t)1, (ac_int_t)-1, (ac_int_t)1);
    check_exact6(0, 1, 2, 1, 0, 0, "mul rad2<0 invalid");

    ac_exact_reset();
    ac_exact_set_fraction(INT64_MAX, (ac_int_t)1);
    ac_exact_mul((ac_int_t)2, (ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_exact6(INT64_MAX, 0, 1, 1, 0, 0, "mul overflow at INT64_MAX*2");

    /* ---- 混合形式乘法 ---- */
    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)1, (ac_int_t)5, (ac_int_t)2);
    ac_exact_mul((ac_int_t)1, (ac_int_t)-1, (ac_int_t)5, (ac_int_t)2);
    check_exact6_approx(-1, 0, 1, 1, 0, 1, -1.0, "golden * conj = -1");

    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)1, (ac_int_t)2, (ac_int_t)1);
    ac_exact_mul((ac_int_t)-1, (ac_int_t)1, (ac_int_t)2, (ac_int_t)1);
    check_exact6_approx(1, 0, 1, 1, 0, 1, 1.0, "(1+rad2)(rad2-1)=1");

    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)1, (ac_int_t)5, (ac_int_t)2);
    ac_exact_mul((ac_int_t)2, (ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_exact6_approx(1, 1, 5, 1, 0, 1, 1.0 + TST_SQRT5, "golden * 2 = 1+rad5");

    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)1, (ac_int_t)2, (ac_int_t)1);
    ac_exact_mul((ac_int_t)2, (ac_int_t)1, (ac_int_t)2, (ac_int_t)1);
    check_exact6_approx(4, 3, 2, 1, 0, 1, 4.0 + 3.0 * TST_SQRT2,
                        "(1+rad2)(2+rad2)=4+3rad2");

    ac_exact_set_radical((ac_int_t)3, (ac_int_t)2);
    ac_exact_mul((ac_int_t)0, (ac_int_t)4, (ac_int_t)2, (ac_int_t)1);
    check_exact6_approx(24, 0, 1, 1, 0, 1, 24.0, "3rad2*4rad2=24 (cross b*b'*r)");

    /* ==================== 模块七：除法 ==================== */
    ac_exact_set_fraction((ac_int_t)3, (ac_int_t)1);
    ac_exact_div((ac_int_t)1, (ac_int_t)0, (ac_int_t)1, (ac_int_t)2);
    check_exact6_approx(6, 0, 1, 1, 0, 1, 6.0, "XL08 3/(1/2)=6");                   /* XL08 */

    ac_exact_set_radical((ac_int_t)2, (ac_int_t)3);
    ac_exact_div((ac_int_t)0, (ac_int_t)1, (ac_int_t)3, (ac_int_t)2);
    check_exact6_approx(4, 0, 1, 1, 0, 1, 4.0, "XL09 2rad3/(rad3/2)=4");            /* XL09 */

    ac_exact_reset();
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)1);
    ac_exact_div((ac_int_t)0, (ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_exact6(1, 0, 1, 1, 0, 0, "div num2=0 invalid");

    ac_exact_reset();
    ac_exact_set_radical((ac_int_t)0, (ac_int_t)5);
    ac_exact_div((ac_int_t)1, (ac_int_t)1, (ac_int_t)2, (ac_int_t)2);
    check_exact6(0, 0, 1, 1, 0, 1, "0/x=0 exact");

    ac_exact_reset();
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)1);
    ac_exact_div((ac_int_t)1, (ac_int_t)0, (ac_int_t)0, (ac_int_t)1);
    check_exact6(1, 0, 1, 1, 0, 0, "div rad2=0 invalid");

    ac_exact_reset();
    ac_exact_set_fraction(INT64_MAX, (ac_int_t)1);
    ac_exact_div((ac_int_t)1, (ac_int_t)0, (ac_int_t)1, INT64_MAX);
    check_exact6(INT64_MAX, 0, 1, 1, 0, 0, "div overflow n1*d2 at boundary");

    ac_exact_reset();
    ac_exact_set_fraction(INT64_MAX, (ac_int_t)1);
    ac_exact_div((ac_int_t)1, (ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_exact6(INT64_MAX, 0, 1, 1, 0, 1, "div by 1 keeps INT64_MAX");

    /* ---- 共轭有理化（五分量的标志性能力） ---- */
    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)1);
    ac_exact_div((ac_int_t)1, (ac_int_t)1, (ac_int_t)2, (ac_int_t)1);
    check_exact6_approx(-1, 1, 2, 1, 0, 1, -1.0 + TST_SQRT2, "1/(1+rad2)=rad2-1");

    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)1);
    ac_exact_div((ac_int_t)3, (ac_int_t)2, (ac_int_t)7, (ac_int_t)1);
    check_exact6_approx(-3, 2, 7, 19, 0, 1, (-3.0 + 2.0 * sqrt(7.0)) / 19.0,
                        "1/(3+2rad7) conjugate");

    ac_exact_set_mixed((ac_int_t)3, (ac_int_t)2, (ac_int_t)7, (ac_int_t)5);
    ac_exact_div((ac_int_t)2, (ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_exact6_approx(3, 2, 7, 10, 0, 1, (3.0 + 2.0 * sqrt(7.0)) / 10.0,
                        "mixed / 2");

    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)1, (ac_int_t)5, (ac_int_t)2);
    ac_exact_div((ac_int_t)3, (ac_int_t)0, (ac_int_t)1, (ac_int_t)1);
    check_exact6_approx(1, 1, 5, 6, 0, 1, (1.0 + TST_SQRT5) / 6.0, "golden / 3");

    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)1, (ac_int_t)2, (ac_int_t)1);
    ac_exact_div((ac_int_t)-1, (ac_int_t)1, (ac_int_t)2, (ac_int_t)1);
    check_exact6_approx(3, 2, 2, 1, 0, 1, 3.0 + 2.0 * TST_SQRT2,
                        "(1+rad2)/(rad2-1) = 3+2rad2");

    /* ==================== 模块八：判别式提取 ==================== */
    ac_exact_from_discriminant(8.0);
    check_exact6_approx(0, 2, 2, 1, 0, 1, 2.0 * TST_SQRT2, "XL11 sqrt(8)=2rad2");   /* XL11 */

    ac_exact_from_discriminant(4.0);
    check_exact6_approx(2, 0, 1, 1, 0, 1, 2.0, "sqrt(4)=2");

    ac_exact_from_discriminant(0.0);
    check_exact6(0, 0, 1, 1, 0, 1, "sqrt(0)=0");

    ac_exact_from_discriminant(-1.0);
    check_exact6(0, 0, 1, 1, 0, 0, "sqrt(-1) invalid");

    ac_exact_from_discriminant(0.25);
    check_exact6_approx(1, 0, 1, 2, 0, 1, 0.5, "XL12 sqrt(0.25)=1/2");              /* XL12 */

    ac_exact_from_discriminant(2.0);
    check_exact6_approx(0, 1, 2, 1, 0, 1, TST_SQRT2, "sqrt(2)=rad2");

    ac_exact_from_discriminant(1.5);
    check_exact6_approx(0, 1, 6, 2, 0, 1, 0.5 * sqrt(6.0), "sqrt(1.5)=(1/2)rad6");

    ac_exact_from_discriminant(0.1);
    check_exact6_approx(0, 1, 10, 10, 0, 1, 0.1 * sqrt(10.0), "sqrt(0.1)=(1/10)rad10");

    ac_exact_from_discriminant(0.5);
    check_exact6_approx(0, 1, 2, 2, 0, 1, 0.5 * TST_SQRT2, "sqrt(0.5)=(1/2)rad2");

    ac_exact_from_discriminant(2.25);
    check_exact6_approx(3, 0, 1, 2, 0, 1, 1.5, "sqrt(2.25)=3/2");

    /* C 特有边界：1e18 判别式（> JS 2^53 上限）——int64 域内完全平方 */
    ac_exact_from_discriminant(1e18);
    check_exact6_approx(1000000000, 0, 1, 1, 0, 1, 1e9,
                        "sqrt(1e18)=1e9 (beyond JS 2^53)");

    ac_exact_from_discriminant(1e19);
    check_exact6(1000000000, 0, 1, 1, 0, 0, "sqrt(1e19) invalid (>= 2^63)");

    ac_exact_from_discriminant(0.1234567890123);
    check_exact6(1000000000, 0, 1, 1, 0, 0,
                 "sqrt(irrational decimal) invalid, state retained");

    /* ==================== 模块九：规范化与近似同步 ==================== */
    ac_exact_reset();
    g_exact_rat = 1;
    g_exact_rad_num = 1;
    g_exact_rad = 8;
    g_exact_den = 2;
    g_exact_exp = 0;
    g_exact_valid = 1;
    ac_exact_normalize();
    check_exact6(1, 2, 2, 2, 0, 1, "normalize extracts sqrt8 chain");

    ac_exact_reset();
    g_exact_rat = 2;
    g_exact_rad_num = 4;
    g_exact_rad = 9;
    g_exact_den = 4;
    g_exact_exp = 0;
    g_exact_valid = 1;
    ac_exact_normalize();
    check_exact6(7, 0, 1, 2, 0, 1, "normalize perfect-sq + merge + gcd chain");

    ac_exact_reset();
    g_exact_rat = 1;
    g_exact_rad_num = 1;
    g_exact_rad = 5;
    g_exact_den = -2;
    g_exact_exp = 0;
    g_exact_valid = 1;
    ac_exact_normalize();
    check_exact6(-1, -1, 5, 2, 0, 1, "normalize flips negative den");

    ac_exact_reset();
    g_exact_rat = 4;
    g_exact_rad_num = 0;
    g_exact_rad = 5;
    g_exact_den = 2;
    g_exact_exp = 0;
    g_exact_valid = 1;
    ac_exact_normalize();
    check_exact6_approx(2, 0, 1, 1, 0, 1, 2.0,
                        "normalize radNum=0 -> rad=1, then gcd 4/2=2");

    ac_exact_set_radical((ac_int_t)2, (ac_int_t)8);
    ac_exact_normalize();
    check_exact6(0, 4, 2, 1, 0, 1, "normalize identity on 4rad2");

    ac_exact_set_fraction((ac_int_t)-6, (ac_int_t)3);
    ac_exact_normalize();
    check_exact6(-2, 0, 1, 1, 0, 1, "normalize identity on -2");

    ac_exact_set_fraction((ac_int_t)1, (ac_int_t)2);
    ac_exact_mul((ac_int_t)2, (ac_int_t)0, (ac_int_t)1, (ac_int_t)3);
    ac_exact_normalize();
    check_exact6(1, 0, 1, 3, 0, 1, "normalize identity on 1/3");

    ac_exact_set_radical((ac_int_t)1, (ac_int_t)2);
    ac_exact_update_approx();
    expect_true(approx(g_approx_value, TST_SQRT2, 1e-9), "update_approx sqrt2");

    ac_exact_set_mixed((ac_int_t)1, (ac_int_t)1, (ac_int_t)5, (ac_int_t)2);
    ac_exact_update_approx();
    expect_true(approx(g_approx_value, (1.0 + TST_SQRT5) / 2.0, 1e-9),
                "update_approx golden");

    ac_exact_set_sci((ac_int_t)25, 3);
    ac_exact_update_approx();
    expect_true(approx(g_approx_value, 25000.0, 1e-9), "update_approx sci 25e3");

    printf("==== C99 exact: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
