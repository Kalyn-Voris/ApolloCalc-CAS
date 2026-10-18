#ifndef AC_BASE_H
#define AC_BASE_H

#include "ac_types.h"

/* ============================================================
 * ApolloCalc-CAS · ac_base.h — 进制转换 + 分数显示（第 8 轮，C 独有）
 * JS 64KB 无 base 层；JS 128KB 有 core_base.js（进制 + SI 前缀），
 * 本层只对齐其进制部分的行为约定，不做 SI 前缀 / 工程记数法
 * （留给 C 128KB）。分数显示为 C 64KB 完全独有（JS 侧在 UI 层实现）。
 * ============================================================ */

/* [English] Character buffer for base/fraction output.
   Codes are ASCII: '0'-'9' = 48-57, 'A'-'F' = 65-70,
   '-' = 45, '/' = 47, ' ' = 32. */
#define AC_BASE_BUF_SIZE 64

/* [English] Shared output buffer. Two producers write it with DIFFERENT
   orders, and each producer has exactly one matching string exit:
   - ac_base_dec_to_base stores digits LOW-to-HIGH (reversed; '-' last) —
     read back ONLY via ac_base_to_string, which reverses.
   - ac_base_format_fraction stores characters in display order —
     read back ONLY via ac_fraction_to_string, which copies straight.
   Mixing a producer with the wrong exit yields reversed garbage.
   Invariant: after any call, 0 <= g_base_len <= AC_BASE_BUF_SIZE - 1,
   so a caller buffer of AC_BASE_BUF_SIZE bytes always fits the string
   plus its NUL terminator. */
extern int32_t g_base_buf[AC_BASE_BUF_SIZE];
extern int     g_base_len;         /* number of valid codes, 0 = empty */

/* [English] Convert integer n to the given base (2, 8, 10, 16 only).
   Writes reversed character codes to g_base_buf, sets g_base_len.
   Returns 1 on success, 0 on failure (invalid base, or n == INT64_MIN
   which cannot be negated without overflow); every failure clears
   g_base_len so the string exit yields "".
   Behavior-aligned with JS 128KB core_base.js decToBase (uppercase
   digits, no leading zeros, 0 prints as "0"), except the C edition
   takes true int64 input: the full int64 range is accepted where JS
   rejects |n| > 2^53-1. */
int ac_base_dec_to_base(ac_int_t n, int base);

/* [English] String exit for the base conversion: reverse g_base_buf into
   a NUL-terminated string. buf must have room for g_base_len + 1 bytes
   (AC_BASE_BUF_SIZE bytes always suffice). Returns buf.
   One-shot UI-layer call; never in hot path. */
char *ac_base_to_string(char *buf, int buf_size);

/* [English] Convenience wrappers. Return 1 on success, 0 on failure. */
int ac_base_dec_to_bin(ac_int_t n);
int ac_base_dec_to_oct(ac_int_t n);
int ac_base_dec_to_hex(ac_int_t n);

/* [English] Fraction display mode:
   AC_FRAC_IMPROPER = "3/2" (always numerator/denominator, no reduction)
   AC_FRAC_MIXED    = "1 1/2" (mixed number; sign on whole part;
                      integral value prints without fraction tail;
                      zero prints as "0")
   AC_FRAC_AUTO     = "5" for den==1, "3/2" otherwise */
#define AC_FRAC_IMPROPER 0
#define AC_FRAC_MIXED    1
#define AC_FRAC_AUTO     2

/* [English] Format fraction num/den into g_base_buf in display order.
   Returns 1 on success, 0 on failure (den == 0, either operand ==
   INT64_MIN, or unknown mode); every failure clears g_base_len so the
   string exit yields "".
   den < 0 is normalized here (sign moves onto num) — callers need not
   pre-normalize. Sign of num carries the overall sign. No gcd
   reduction: 4/2 stays "4/2" (the exact layer owns reduction). */
int ac_base_format_fraction(ac_int_t num, ac_int_t den, int mode);

/* [English] String exit for fraction (same contract as ac_base_to_string). */
char *ac_fraction_to_string(char *buf, int buf_size);

/* ==================== SI 前缀 + 工程记数法（128KB 第 4 轮） ====================
 * 行为基准：JS/128KB/src/core_base.js 的 siNormalize/siToString（C 64KB
 * 无此层）。工程记数法为 C 侧扩展（JS 无对应），只输出"尾数 + 指数"
 * 不映射前缀字符，字符串格式为 mantissa + "e" + exp（exp==0 无后缀）。
 * 失效契约（JS 对齐）：0 / 非有限 / 缩放后仍越界（|v| ≥ 1e27 或
 * < 1e-27）→ 四元组/三元组全部清零，出口得空串。
 * 舍入回吸：×1000 连乘累积舍入让精确千的幂（1e-15/1e-24 等）尾数落在
 * 1±2ulp，距 1 不足 1e-12 归拢为 1（窗口远大于舍入噪声、远小于显示精度）。
 * snprintf 仅用于出口函数（ac_base_si_to_string / ac_base_eng_to_string），
 * 内核路径禁用——Zero-GC 契约不变。 */

/* [English] SI prefix normalization state. prefix_code is the ASCII byte
   (181 = U+00B5 MICRO SIGN for exp -6; 0 = empty, exp 0). */
extern double g_base_si_mantissa;
extern int    g_base_si_prefix_code;
extern int    g_base_si_exp;
extern int    g_base_si_valid;

/* [English] Normalize v into mantissa ∈ [1,1000) (sign kept) + prefix code
   + decimal exponent (multiple of 3, -24..+24). Returns 1 on success,
   0 on failure (0 / non-finite / out of SI range); every failure clears
   all four globals so the string exit yields "". */
int ac_base_si_normalize(double v);

/* [English] String exit: "%.15g" mantissa + (space + prefix byte) when the
   prefix is non-empty; "" when invalid. snprintf allowed here (one-shot
   UI-layer exit). buf is NUL-terminated within buf_size; if the prefix
   does not fit, the mantissa alone is kept. */
char *ac_base_si_to_string(char *buf, int buf_size);

/* [English] Engineering notation state: mantissa × 10^exp, exp multiple
   of 3 in -24..+24. Same scaling/回吸/range rules as SI, no prefix. */
extern double g_base_eng_mantissa;
extern int    g_base_eng_exp;
extern int    g_base_eng_valid;

/* [English] Normalize v into engineering form. Returns 1 on success,
   0 on failure (same conditions as SI); failure clears all three globals. */
int ac_base_eng_normalize(double v);

/* [English] String exit: "%.15g" mantissa + "e<exp>" suffix (no suffix
   when exp == 0, e.g. "12.345e3", "5e-3", "999"); "" when invalid. */
char *ac_base_eng_to_string(char *buf, int buf_size);

/* [English] Reset buffer state (zero codes and the length) — since round 4
   also clears the SI / engineering state. */
void ac_base_reset(void);

#endif /* AC_BASE_H */
