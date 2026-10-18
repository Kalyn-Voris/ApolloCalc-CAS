#ifndef AC_CALCULUS_H
#define AC_CALCULUS_H

#include "ac_types.h"

/* ============================================================
 * ApolloCalc-CAS · ac_calculus.h — 数值微积分（C 128KB 第 3 轮）
 * 行为基准：JS/128KB/src/core_calculus.js（文件 7），逐函数对齐。
 * 功能：一阶导数（中心差分）、二阶导数（五点公式）、
 *       定积分（自适应复合辛普森）、数值极限（左右逼近）、
 *       泰勒展开（逐阶数值导数 + 中心差分表，系数缓存 Horner 求值）。
 * 性能契约：微积分最坏 ~1.3 万次求值，只做一次 toRPN——RPN 缓存
 *   s_rpn_ready 由本模块持有，表达式变更后 UI 层必须先调
 *   ac_calculus_reset()，否则复用旧表达式的缓存 RPN（JS 同款契约）。
 * 错误码（与 JS 128KB 一致）：
 *   0=OK  1=域错误（非有限入参 / 方程输入 / 采样点出域 / 结果非有限）
 *   2=不收敛（极限 8 步未收敛或左右极限不一致）
 *   3=阶数超限（泰勒 order < 0 或 > 16；JS _calcError=3 同义）
 *   4=内部错（toRPN 失败）
 * Zero-GC：无 malloc、无 VLA、无递归（泰勒差分表原地逐层覆盖）。
 * ============================================================ */

/* [English] Result / diagnostics of the most recent calculus operation. */
extern double g_calc_result;
extern int    g_calc_error;
extern int    g_calc_iterations;   /* 最近一次计算的求值次数（诊断用） */

/* [English] Taylor coefficient cache (c_k = f^(k)(x0)/k!, max order 16).
   Exposed for white-box tests; JS keeps these file-local. */
extern double g_taylor_coeffs[16];
extern int    g_taylor_order;
extern double g_taylor_center;

/* [English] First derivative, central difference:
   f'(x) ≈ (f(x+h) - f(x-h)) / (2h), h = 1e-5·max(|x|,1).
   Returns NaN and sets g_calc_error on failure. */
double ac_calc_derivative(double x);

/* [English] Second derivative, five-point formula:
   f''(x) ≈ (-f(x+2h) + 16f(x+h) - 30f(x) + 16f(x-h) - f(x-2h)) / (12h²),
   h = 1e-4·max(|x|,1) (second differences are more rounding-sensitive). */
double ac_calc_derivative2(double x);

/* [English] Definite integral, adaptive composite Simpson:
   n doubles from 100 (cap 10000); stop when successive values differ < 1e-8.
   Single pass per round (sumOdd/sumEven, no sample caching); sample points
   recomputed multiplicatively x = a + i·h (no drifting x += h).
   Hitting the n cap returns the current value with g_calc_error = 0.
   a == b returns 0 immediately (before the equation check, JS order). */
double ac_calc_integral(double a, double b);

/* [English] Numeric limit by epsilon approach.
   side: -1 left / +1 right / 0 both (both must converge and agree < 1e-6;
   result is the mean). One side: eps from 1e-1 down to 1e-8 (8 samples),
   converged when successive values differ < 1e-6 (8 steps + 1e-6 is the
   FINAL JS 128KB tuning: 6 steps + 1e-8 can never converge for the
   first-order-approach family like (x²-1)/(x-1) → 2). */
double ac_calc_limit(double a, int side);

/* [English] Taylor expansion of the current expression around x0.
   Coefficients c_k = f^(k)(x0)/k! land in g_taylor_coeffs[0..order];
   k=0 direct eval, k=1/2 reuse the derivative kernels, k>=3 central
   difference table (grid x0+(j-K)h, in-place forward differences; even
   orders take the center element, odd orders average the two straddling
   elements; h = eps^(1/(K+2))·max(|x0|,1) with eps = 2.22e-16).
   Returns 0 on success, -1 on failure (see g_calc_error codes above). */
int ac_calc_taylor(double x0, int order);

/* [English] Evaluate the cached Taylor polynomial at x (Horner). */
double ac_taylor_eval(double x);

/* [English] UI contract: call after every expression change (also
   invalidates the Taylor cache — coefficients belong to the old
   expression). Clears result/error/iterations/RPN-ready/Taylor state. */
void ac_calculus_reset(void);

#endif /* AC_CALCULUS_H */
