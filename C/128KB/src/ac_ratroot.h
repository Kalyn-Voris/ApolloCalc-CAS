#ifndef AC_RATROOT_H
#define AC_RATROOT_H

#include "ac_types.h"

/* ============================================================
 * ApolloCalc-CAS · ac_ratroot.h — 有理根检验与降次（C 128KB 第 5 轮）
 * 行为基准：JS/128KB/src/core_ratroot.js（文件 9），逐函数对齐。
 * 有理根定理：整系数多项式 an·x^n+…+a0 的有理根 p/q 满足 p | a0、
 * q | an。枚举候选（约简去重，至多 32 组）→ 综合除法验证（余项
 * < 1e-6·尺度 + 商近整复核，高斯引理）→ 降次迭代（显式栈，无递归）。
 * 供 ac_solve_cubic / ac_solve_quartic 开头调用：
 *   - 找到有理根 → 商系数写回 g_coeffs 交低次求解器收尾；
 *   - 无有理根 → g_coeffs 原样保留，调用方走原求解路径。
 * 有理根的精确形式走 ac_exact_set_fraction（五分量）。
 * 数值约定：采样提取的系数经 |c − round(c)| ≤ 1e-6·max(1,|c|) 判为整数；
 *   真有理根的综合除法余项为浮点噪声（< 1e-6·尺度），非根余项 O(1)。
 * Zero-GC：无 malloc、无 VLA、无递归（降次用显式栈，最多 8 层）。
 * ============================================================ */

/* [English] Rational-root stack (discovery order), exposed for white-box
 * tests; JS keeps these file-local. found = stack depth (0..8). */
extern double g_rat_root_values[8];   /* 根值 p/q（约简后） */
extern ac_int_t g_rat_root_num[8];    /* 精确形式分子 p */
extern ac_int_t g_rat_root_den[8];    /* 精确形式分母 q（>0，已约简） */
extern int      g_rat_root_found;     /* 实际找到的根数（0..8） */

/* [English] Iterate rational-root descent on g_coeffs[0..degree]
 * (ascending layout). Returns the number of rational roots found:
 *   0  = none (g_coeffs untouched, caller falls through to its own path)
 *   >0 = roots pushed on the stack (discovery order); g_coeffs[0..deg-k]
 *        holds the depressed integer polynomial, tail slots cleared.
 * degree must be 1..4; anything else returns 0. */
int ac_rat_root_descent(int degree);

/* [English] Merge the descent results into the solver root pools:
 * the k stack roots occupy the first k slots (each via
 * ac_exact_set_fraction -> five-component exact form), the remaining
 * dd = degree-k polynomial (already in g_coeffs) is delegated to the
 * lower-degree solver whose roots are shifted right by k.
 * dd 1..2 -> ac_solve_quadratic(); dd 3 -> ac_solve_cubic(1) (rational
 * hook skipped: the descent already proved no rational roots remain).
 * Returns 0 on success; on sub-solver failure g_solve_error is set and
 * that error code is returned. */
int ac_rat_root_finish(int k, int degree);

#endif /* AC_RATROOT_H */
