/* ============================================================
 * ApolloCalc-CAS · test_special.c — 特殊函数测试（C 128KB 第 8 轮，C 独有）
 * 无 JS 圣经（JS 128KB 无特殊函数层），测试基准为数学事实，经 Node
 * 独立实现核算（勿凭记忆改值）：
 *   Γ 整数/半整数闭式：Γ(10)=362880、Γ(0.5)=√π、Γ(−0.5)=−2√π、Γ(−1.5)=4√π/3
 *   erf 正项级数独立实现：erf(1)=0.8427007929497149、erf(2)=0.9953222650189525
 *   γ 闭式：γ(1,x)=1−e⁻ˣ、γ(2,x)=1−e⁻ˣ(1+x)、γ(3,2)=2−10e⁻²、
 *           γ(0.5,x)=√π·erf(√x)、γ(5,10)=24(1−e⁻¹⁰Σ₁₀ᵏ/k!)
 *   Bessel 整数 n 阶乘级数独立实现：J0(1)=0.7651976865579666、J1(1)=0.4400505857449335
 * 覆盖：Gamma（整数/半整数/负数反射/极点）、lgamma（含 200 大参数）、
 *       Beta、erf（奇函数/饱和）、igamma（级数/连分式双路径）、Bessel。
 * ============================================================ */
#include <stdio.h>
#include <math.h>
#include "../src/ac_types.h"
#include "../src/ac_special.h"

static int s_pass = 0;
static int s_fail = 0;

static void expect_true(int cond, const char *name) {
    if (cond) { s_pass++; }
    else { s_fail++; printf("FAIL [special] %s\n", name); }
}

static void expect_near(double got, double want, double tol, const char *name) {
    double d = got - want;
    if (d < 0.0) { d = -d; }
    if (d <= tol) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [special] %s: got=%.17g want=%.17g (d=%.3g)\n",
               name, got, want, d);
    }
}

#define TST_PI 3.141592653589793
#define SQRTPI 1.7724538509055160

int test_special_run(void) {
    /* ==================== Part A：Gamma ==================== */
    expect_near(ac_special_gamma(1.0), 1.0, 1e-12, "G(1)=1");
    expect_near(ac_special_gamma(2.0), 1.0, 1e-12, "G(2)=1");
    expect_near(ac_special_gamma(3.0), 2.0, 1e-12, "G(3)=2");
    expect_near(ac_special_gamma(4.0), 6.0, 1e-12, "G(4)=6");
    expect_near(ac_special_gamma(5.0), 24.0, 1e-11, "G(5)=24");
    /* G(10) 容差按 Lanczos 固有相对精度 1e-13 × 3.6e5 ≈ 4e-8 取 1e-7 */
    expect_near(ac_special_gamma(10.0), 362880.0, 1e-7, "G(10)=362880");
    expect_near(ac_special_gamma(0.5), SQRTPI, 1e-13, "G(0.5)=sqrt(pi)");
    expect_near(ac_special_gamma(-0.5), -2.0 * SQRTPI, 1e-12, "G(-0.5)=-2sqrt(pi)");
    expect_near(ac_special_gamma(-1.5), 4.0 * SQRTPI / 3.0, 1e-12,
                "G(-1.5)=4sqrt(pi)/3");
    expect_near(ac_special_gamma(2.5), 0.75 * SQRTPI, 1e-13, "G(2.5)=0.75sqrt(pi)");
    /* 递推抽查：Γ(n+1) = n·Γ(n) */
    expect_near(ac_special_gamma(7.5) / ac_special_gamma(6.5), 6.5, 1e-12,
                "recurrence G(7.5)=6.5*G(6.5)");
    /* 极点：Γ(0)、Γ(−1) 发散（|Γ| 巨大即视为命中极点） */
    expect_true(fabs(ac_special_gamma(0.0)) > 1e15, "G(0) pole -> huge");
    expect_true(fabs(ac_special_gamma(-1.0)) > 1e15, "G(-1) pole -> huge");

    /* ==================== Part B：lgamma ==================== */
    expect_near(ac_special_lgamma(1.0), 0.0, 1e-12, "lgamma(1)=0");
    expect_near(ac_special_lgamma(2.0), 0.0, 1e-12, "lgamma(2)=0");
    expect_near(ac_special_lgamma(0.5), 0.5723649429247000, 1e-13,
                "lgamma(0.5)=ln sqrt(pi)");
    expect_near(ac_special_lgamma(10.0), 12.801827480081469, 1e-9,
                "lgamma(10)=ln 362880");
    expect_near(ac_special_lgamma(-0.5), 1.2655121234846454, 1e-12,
                "lgamma(-0.5)=ln 2sqrt(pi)");
    /* 大参数：lgamma(200) = Σ ln(1..199)（Node 核算），Γ(200) 已溢出 */
    expect_near(ac_special_lgamma(200.0), 857.93366982585735, 1e-6,
                "lgamma(200) no overflow");

    /* ==================== Part C：Beta ==================== */
    expect_near(ac_special_beta(1.0, 1.0), 1.0, 1e-12, "B(1,1)=1");
    expect_near(ac_special_beta(2.0, 3.0), 1.0 / 12.0, 1e-12, "B(2,3)=1/12");
    expect_near(ac_special_beta(0.5, 0.5), TST_PI, 1e-12, "B(0.5,0.5)=pi");
    expect_near(ac_special_beta(3.0, 4.0), 1.0 / 60.0, 1e-12, "B(3,4)=1/60");
    expect_near(ac_special_beta(0.5, 2.0), 4.0 / 3.0, 1e-12, "B(0.5,2)=4/3");

    /* ==================== Part D：erf ==================== */
    expect_near(ac_special_erf(0.0), 0.0, 1e-15, "erf(0)=0");
    expect_near(ac_special_erf(1.0), 0.8427007929497149, 1e-13, "erf(1)");
    expect_near(ac_special_erf(2.0), 0.9953222650189525, 1e-13, "erf(2)");
    expect_near(ac_special_erf(0.5), 0.5204998778130466, 1e-13, "erf(0.5)");
    expect_near(ac_special_erf(3.0), 0.9999779095030016, 1e-12, "erf(3)");
    /* 奇函数 */
    expect_near(ac_special_erf(-1.0), -0.8427007929497149, 1e-13, "erf(-1)=-erf(1)");
    expect_near(ac_special_erf(-2.0), -0.9953222650189525, 1e-13, "erf(-2)=-erf(2)");
    /* 饱和：|x| ≥ 6 → ±1（erfc(6) = 2.2e-17 低于分辨率） */
    expect_true(fabs(ac_special_erf(6.0) - 1.0) < 1e-15, "erf(6) saturates to 1");
    expect_true(fabs(ac_special_erf(10.0) - 1.0) < 1e-15, "erf(10) saturates");
    expect_true(ac_special_erf(-10.0) == -1.0, "erf(-10) saturates to -1");

    /* ==================== Part E：不完全 Gamma γ(s,x) ==================== */
    /* 级数路径（x < s+1） */
    expect_near(ac_special_igamma(1.0, 1.0), 0.6321205588285577, 1e-13,
                "g(1,1)=1-1/e (series)");
    expect_near(ac_special_igamma(2.0, 1.0), 0.2642411176571153, 1e-13,
                "g(2,1)=1-2/e (series)");
    expect_near(ac_special_igamma(3.0, 2.0), 0.6466471676338730, 1e-13,
                "g(3,2)=2-10/e^2 (series)");
    expect_near(ac_special_igamma(0.5, 1.0), 1.4936482656248542, 1e-13,
                "g(0.5,1)=sqrt(pi)*erf(1)");
    expect_near(ac_special_igamma(0.5, 4.0), 1.7641627815248428, 1e-13,
                "g(0.5,4)=sqrt(pi)*erf(2)");
    /* 连分式路径（x >= s+1） */
    expect_near(ac_special_igamma(2.0, 5.0), 0.9595723180054872, 1e-12,
                "g(2,5)=1-6/e^5 (CF)");
    expect_near(ac_special_igamma(1.0, 3.0), 0.9502129316321361, 1e-13,
                "g(1,3)=1-e^-3 (CF)");
    expect_near(ac_special_igamma(5.0, 10.0), 23.297935486152937, 1e-10,
                "g(5,10) (CF)");
    /* 边界：x=0 → 0；非法域 NaN */
    expect_true(ac_special_igamma(2.0, 0.0) == 0.0, "g(2,0)=0");
    expect_true(ac_special_igamma(0.0, 1.0) != ac_special_igamma(0.0, 1.0),
                "g(s=0) NaN");
    expect_true(ac_special_igamma(2.0, -1.0) != ac_special_igamma(2.0, -1.0),
                "g(x<0) NaN");

    /* ==================== Part F：Bessel J_n ==================== */
    expect_near(ac_special_bessel_j(0.0, 0.0), 1.0, 1e-15, "J0(0)=1");
    expect_near(ac_special_bessel_j(1.0, 0.0), 0.0, 1e-15, "J1(0)=0");
    expect_near(ac_special_bessel_j(0.0, 1.0), 0.7651976865579666, 1e-13, "J0(1)");
    expect_near(ac_special_bessel_j(1.0, 1.0), 0.4400505857449335, 1e-13, "J1(1)");
    expect_near(ac_special_bessel_j(0.0, 2.0), 0.2238907791412356, 1e-12, "J0(2)");
    expect_near(ac_special_bessel_j(1.0, 2.0), 0.5767248077568736, 1e-12, "J1(2)");
    expect_near(ac_special_bessel_j(2.0, 1.0), 0.1149034849319005, 1e-13, "J2(1)");
    expect_near(ac_special_bessel_j(0.0, 5.0), -0.1775967713143385, 1e-11, "J0(5)");
    expect_near(ac_special_bessel_j(1.0, 5.0), -0.3275791375914651, 1e-11, "J1(5)");
    /* 恒等式抽查：J0 + 2J2 = ... 用 J0(x)+J2(x) = 2J1(x)/x（x=2） */
    expect_near(ac_special_bessel_j(0.0, 2.0) + ac_special_bessel_j(2.0, 2.0),
                2.0 * ac_special_bessel_j(1.0, 2.0) / 2.0, 1e-9,
                "recurrence J0+J2=2J1/x @2");

    printf("==== C99 special: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
