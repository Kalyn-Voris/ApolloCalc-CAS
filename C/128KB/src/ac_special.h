#ifndef AC_SPECIAL_H
#define AC_SPECIAL_H

#include "ac_types.h"

/* ============================================================
 * ApolloCalc-CAS · ac_special.h — 特殊函数（C 128KB 第 8 轮，C 独有）
 * JS 128KB 没有特殊函数层。功能：Gamma（Lanczos g=7, n=9）、
 * lgamma（对数形式免溢出）、Beta、erf（正项级数，全 double 精度）、
 * 不完全 Gamma γ(s,x)（x < s+1 级数 / x ≥ s+1 连分式）、
 * Bessel J_n（项比率递推级数）。
 * ----
 * 递归契约：任务卡示例的反射式 Γ(x) = π/(sin πx·Γ(1−x)) 在 C 侧
 * 拆为 gamma_core（x ≥ 0.5 单次直算）+ 入口手工展开——零自调用
 * （Zero-GC 禁递归含函数自身调用）。
 * 值域契约：
 *   - gamma/lgamma：x 为非正整数是极点 → ±Inf/极大值（sin(πx) 趋零）；
 *   - erf：|x| ≥ 6 直接返回 ±1（erfc(6) = 2.2e-17，低于 double 分辨率）；
 *   - igamma：s > 0、x ≥ 0（其余返回 NaN）；
 *   - bessel_j：x ≥ 0（负 x 的非整 n 阶 pow 产生 NaN，不做延拓）。
 * Zero-GC：无 malloc、无 VLA、无递归；系数表 static const（加载期）。
 * ============================================================ */

/* [English] Gamma function. Poles at non-positive integers yield huge
   values (+/-Inf at exact integers). */
double ac_special_gamma(double x);

/* [English] ln|Γ(x)| — Lanczos log-form, no overflow up to huge x. */
double ac_special_lgamma(double x);

/* [English] Beta function B(a, b) = Γ(a)Γ(b)/Γ(a+b). */
double ac_special_beta(double a, double b);

/* [English] Error function. Positive-term series with e^(-x²) factored:
   erf(x) = 2x/√π · e^(-x²) · Σ (2x²)^k / (1·3·5···(2k+1)) — no
   cancellation, full double accuracy for |x| < 6; |x| >= 6 -> ±1
   (erfc(6) = 2.2e-17 is below double resolution of 1). Odd function. */
double ac_special_erf(double x);

/* [English] Lower incomplete gamma γ(s, x) = ∫₀ˣ t^(s-1) e^(-t) dt.
   x < s+1: series; x >= s+1: continued fraction (NR gcf, Lentz).
   Returns NaN for s <= 0 or x < 0; 0 for x == 0. */
double ac_special_igamma(double s, double x);

/* [English] Bessel function of the first kind J_n(x), term-ratio series
   (no factorial overflow). x >= 0; n any real (tests use integers).
   |x| > ~8 loses digits to series cancellation (documented limit). */
double ac_special_bessel_j(double n, double x);

#endif /* AC_SPECIAL_H */
