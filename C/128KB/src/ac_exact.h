#ifndef AC_EXACT_H
#define AC_EXACT_H

#include "ac_types.h"

/* ============================================================
 * ApolloCalc-CAS · ac_exact.h — 双模态精确数值系统（C 128KB 第 1 轮）
 * 行为基准：JS/128KB/src/core_exact.js（五分量模型），逐函数对齐。
 * 内部表示固定为五分量：
 *   value = ((g_exact_rat + g_exact_rad_num·√g_exact_rad) / g_exact_den) × 10^g_exact_exp
 *   - rat：有理部分子（可与根式部共存，如黄金比例 (1+√5)/2）
 *   - rad_num：根式部系数（0 = 无根式部）
 *   - rad：根号内数（无平方因子正整数，1 = 无根式）
 *   - den：公共分母（恒为正）
 *   - exp：科学计数法指数（10 的幂，0 为主）
 * 规范化不变式（ac_exact_normalize 保证）：
 *   - den > 0；rad_num == 0 ⟺ rad == 1
 *   - gcd(|rat|, |rad_num|, den) = 1；零值 ⟺ 0/0/1/1/0
 * C 与 JS 的差异：分量为 int64_t（63 位）而非 double（53 位）。
 *   溢出阈值为 INT64_MAX（任务约定：不是 2^53）；int64 域内唯一越界
 *   形态是 INT64_MIN（|INT64_MIN| = 2^63），所有构造/运算/规范化入口
 *   先拒绝它再取负。int64 溢出是 UB，所有乘/加/减一律先做除法预检
 *   再运算，不用 __builtin_*_overflow，保证 arm-none-eabi-gcc 可移植。
 * Zero-GC：无 malloc、无 VLA、无递归（欧几里得/试除法均迭代）。
 * ============================================================ */

/* [English] Five-component state. JS 128KB threshold EXACT_INT_MAX
   (2^53-1) widens to INT64_MAX here; the only int64 value beyond it is
   INT64_MIN, rejected at every entry before any negation. */
extern ac_int_t g_exact_rat;      /* 有理部分子 */
extern ac_int_t g_exact_rad_num;  /* 根式部系数（0 = 无根式部） */
extern ac_int_t g_exact_rad;      /* 根号内数（无平方因子，1 = 无根式） */
extern ac_int_t g_exact_den;      /* 公共分母（恒为正） */
extern int      g_exact_exp;      /* 10 的指数 */
extern int      g_exact_valid;    /* 有效标志 */
extern double   g_approx_value;   /* 双精度近似值 */

/* [English] Safe arithmetic primitives (division-form pre-check, then op).
   INT64_MIN-safe: they never negate an operand. */
int ac_safe_mul(ac_int_t a, ac_int_t b, ac_int_t *out);
int ac_safe_add(ac_int_t a, ac_int_t b, ac_int_t *out);
int ac_safe_sub(ac_int_t a, ac_int_t b, ac_int_t *out);

/* [English] GCD for int64_t (iterative Euclidean). */
ac_int_t ac_gcd(ac_int_t a, ac_int_t b);

/* [English] Extract perfect square factors from D. Returns outside
   coefficient, writes remaining squarefree part to g_exact_rad. */
ac_int_t ac_simplify_radical(ac_int_t D);

/* [English] Normalize the five-component state in place (the ONLY
   normalization entry; every constructor/op ends here). */
void ac_exact_normalize(void);

/* [English] Smart constructors. All funnel through normalize. */
void ac_exact_set_fraction(ac_int_t n, ac_int_t d);                        /* n/d */
void ac_exact_set_radical(ac_int_t k, ac_int_t m);                         /* k·√m */
void ac_exact_set_mixed(ac_int_t a, ac_int_t b, ac_int_t c, ac_int_t d);   /* (a + b·√c)/d */
void ac_exact_set_sci(ac_int_t v, int e);                                  /* v × 10^e */
void ac_exact_set_pure_real(double v);                                     /* 整数值浮点 */

/* [English] Invalidate (keeps approx + components, per JS contract) / reset. */
void ac_exact_invalidate(void);
void ac_exact_reset(void);

/* [English] Sync approx from the five components. */
void ac_exact_update_approx(void);

/* [English] Arithmetic. Current value op (r2 + rn2·√rd2)/d2 (operand exp
   treated as 0). A current value with exp != 0 (set_sci product) does not
   participate: invalidates. Same-class radicals merge (r == rd2 or one
   side radical-free); mismatched radicals (√2+√3 / √2·√3) invalidate. */
void ac_exact_add(ac_int_t r2, ac_int_t rn2, ac_int_t rd2, ac_int_t d2);
void ac_exact_sub(ac_int_t r2, ac_int_t rn2, ac_int_t rd2, ac_int_t d2);
void ac_exact_mul(ac_int_t r2, ac_int_t rn2, ac_int_t rd2, ac_int_t d2);
void ac_exact_div(ac_int_t r2, ac_int_t rn2, ac_int_t rd2, ac_int_t d2);

/* [English] Extract √D as exact form (quadratic solver interface). */
void ac_exact_from_discriminant(double D);

#endif /* AC_EXACT_H */
