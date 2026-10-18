/* ============================================================
 * ApolloCalc-CAS · ac_special.c — 特殊函数（C 128KB 第 8 轮，C 独有）
 * Gamma：Lanczos 近似（g = 7, n = 9，标准系数表）；反射公式在入口
 *   手工展开（x < 0.5 → π/(sin πx · core(1−x))），core 单次直算，
 *   全程零自调用（任务卡示例的递归反射被此结构替代）。
 * lgamma：Lanczos 对数形式 lnΓ = ½ln2π + (x−½)ln t − t + ln|a|，
 *   大参数无溢出（Γ(172) 起浮点溢出，lgamma(200) 仍精确）。
 * erf：正项级数 erf(x) = 2x/√π · e^(−x²) · Σ (2x²)^k/(2k+1)!!，
 *   无相消，|x| < 6 全 double 精度；|x| ≥ 6 → ±1（erfc(6) = 2.2e-17）。
 * igamma：x < s+1 级数（NR gser）/ x ≥ s+1 连分式（NR gcf，Lentz），
 *   log 域组装防溢出。
 * Zero-GC：无 malloc、无 VLA、无递归；系数 static const 加载期初始化。
 * ============================================================ */
#include "ac_special.h"
#include "ac_math.h"
#include <math.h>

/* ==================== Lanczos 系数表（g = 7, n = 9） ==================== */

static const double LANCZOS_COEF[9] = {
    0.99999999999980993,
    676.5203681218851,
    -1259.1392167224028,
    771.32342877765313,
    -176.61502916214059,
    12.507343278686905,
    -0.13857109526572012,
    9.9843695780195716e-6,
    1.5056327351493116e-7
};

#define SPEC_LANCZOS_G_HALF 7.5   /* g + 0.5 */

/* ==================== Gamma 核心（x ≥ 0.5 直算） ==================== */

/* Γ(x) 的 Lanczos 核：仅用于 x ≥ 0.5（调用方保证） */
static double special_gamma_core(double x) {
    double xm = x - 1.0;
    double a = LANCZOS_COEF[0];
    double t = xm + SPEC_LANCZOS_G_HALF;
    int i;
    for (i = 1; i < 9; i++) {
        a += LANCZOS_COEF[i] / (xm + (double)i);
    }
    return sqrt(2.0 * AC_PI) * pow(t, xm + 0.5) * exp(-t) * a;
}

/* ln Γ(x) 的 Lanczos 对数形式：仅用于 x ≥ 0.5 */
static double special_lgamma_core(double x) {
    double xm = x - 1.0;
    double a = LANCZOS_COEF[0];
    double t = xm + SPEC_LANCZOS_G_HALF;
    int i;
    for (i = 1; i < 9; i++) {
        a += LANCZOS_COEF[i] / (xm + (double)i);
    }
    return 0.5 * log(2.0 * AC_PI) + (xm + 0.5) * log(t) - t + log(fabs(a));
}

double ac_special_gamma(double x) {
    if (x >= 0.5) { return special_gamma_core(x); }
    /* 反射公式手工展开（无自调用）：Γ(x) = π / (sin πx · Γ(1−x))。
       非正整数 x 是极点：sin(πx) ≈ 0 → ±Inf（符号随 sin 翻转）。 */
    return AC_PI / (sin(AC_PI * x) * special_gamma_core(1.0 - x));
}

double ac_special_lgamma(double x) {
    if (x >= 0.5) { return special_lgamma_core(x); }
    /* ln|Γ(x)| = ln π − ln|sin πx| − lnΓ(1−x)（极点 → +Inf，|·| 吸收符号） */
    return log(AC_PI / fabs(sin(AC_PI * x))) - special_lgamma_core(1.0 - x);
}

/* ==================== Beta ==================== */

double ac_special_beta(double a, double b) {
    return ac_special_gamma(a) * ac_special_gamma(b) / ac_special_gamma(a + b);
}

/* ==================== 误差函数 ==================== */

double ac_special_erf(double x) {
    double s = 1.0;
    double ax = x;
    double x2;
    double t;
    double sum;
    int k;

    if (x != x) { return (double)NAN; }
    if (x < 0.0) { s = -1.0; ax = -x; }
    if (ax >= 6.0) { return s; }   /* erfc(6) = 2.2e-17，低于 double 分辨率 */

    /* erf(ax) = 2·ax/√π · e^(−ax²) · Σ (2ax²)^k / (2k+1)!!（全正项） */
    x2 = 2.0 * ax * ax;
    t = 1.0;
    sum = 1.0;
    for (k = 0; k < 500; k++) {
        t *= x2 / (double)(2 * k + 3);
        sum += t;
        if (t < 1e-18 * sum) { break; }
    }
    return s * (2.0 * ax / sqrt(AC_PI)) * exp(-ax * ax) * sum;
}

/* ==================== 不完全 Gamma γ(s, x) ==================== */

double ac_special_igamma(double s, double x) {
    double lgs;
    double xl;
    double lead;   /* e^(−x)·x^s（配合 lgamma 组装，防溢出） */

    if (s != s || x != x || s <= 0.0 || x < 0.0) { return (double)NAN; }
    if (x == 0.0) { return 0.0; }

    lgs = ac_special_lgamma(s);
    xl = s * log(x);
    lead = exp(-x + xl);

    if (x < s + 1.0) {
        /* 级数（NR gser）：P(s,x) = lead/Γ(s) · Σ x^k/(s(s+1)…(s+k)) */
        double ap = s;
        double sum = 1.0 / s;
        double del = sum;
        int n;
        for (n = 1; n < 500; n++) {
            ap += 1.0;
            del *= x / ap;
            sum += del;
            if (fabs(del) < fabs(sum) * 1e-17) { break; }
        }
        /* γ(s,x) = lead·Σ（lead = e^(−x)·x^s 已含全部尺度因子） */
        return lead * sum;
    }

    /* 连分式（NR gcf，Lentz 修正）：Q(s,x) = Γ(s,x)/Γ(s)；
       γ = Γ(s)·(1 − Q) = Γ(s) − Γ(s)·Q，Q = lead/Γ(s)·h */
    {
        double b = x + 1.0 - s;
        double c = 1.0 / 1e-300;
        double d = 1.0 / b;
        double h = d;
        double del;
        int i;
        for (i = 1; i < 500; i++) {
            double an = -(double)i * ((double)i - s);
            b += 2.0;
            d = an * d + b;
            if (fabs(d) < 1e-300) { d = 1e-300; }
            c = b + an / c;
            if (fabs(c) < 1e-300) { c = 1e-300; }
            d = 1.0 / d;
            del = d * c;
            h *= del;
            if (fabs(del - 1.0) < 1e-17) { break; }
        }
        /* γ = Γ(s) − lead·h（lead·h = Γ(s)·Q）；相消温和（x ≥ s+1 蕴含 Q 远离 1） */
        return exp(lgs) - lead * h;
    }
}

/* ==================== Bessel J_n ==================== */

double ac_special_bessel_j(double n, double x) {
    double half = 0.5 * x;
    double half2 = half * half;
    double t;
    double sum;
    int k;

    if (x != x || n != n) { return (double)NAN; }
    /* k = 0 项：(x/2)^n / Γ(n+1)（x = 0 且 n > 0 → 0；n = 0 → 1） */
    t = pow(half, n) / ac_special_gamma(n + 1.0);
    sum = t;
    for (k = 0; k < 300; k++) {
        t *= -half2 / ((double)(k + 1) * (n + (double)k + 1.0));
        sum += t;
        if (fabs(t) <= 1e-18 * (fabs(sum) + 1e-300)) { break; }
    }
    return sum;
}
