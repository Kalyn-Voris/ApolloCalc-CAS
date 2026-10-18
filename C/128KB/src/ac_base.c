/* ============================================================
 * ApolloCalc-CAS · ac_base.c — 进制转换 + 分数显示（第 8 轮，C 独有）
 *                      + SI 前缀 / 工程记数法（128KB 第 4 轮）
 * 功能：dec_to_base（整数 → 2/8/10/16 进制字符码缓冲，逆序存放，
 *       行为对齐 JS 128KB core_base.js 的 decToBase）、
 *       format_fraction（分数 → 显示字符码，正序存放；假分数 /
 *       带分数 / AUTO 三模式，C 独有功能）、
 *       si_normalize / eng_normalize（尾数 [1,1000) + 3 的倍数指数；
 *       SI 对齐 JS 128KB siNormalize 含舍入回吸，工程记数法为 C 侧扩展）、
 *       四个出口拼串函数（仅有的字符串拼接点；进制/分数出口手写循环，
 *       SI/工程出口允许 snprintf——一次性 UI 层调用，内核路径零格式化）。
 * Zero-GC：无 malloc、无 VLA、无递归。
 * 注意：进制缓冲逆序、分数缓冲正序，各有专属出口，绝不能交叉使用。
 * ============================================================ */
#include "ac_base.h"
#include <stdio.h>

int32_t g_base_buf[AC_BASE_BUF_SIZE];
int     g_base_len = 0;

/* ==================== 进制转换内核 ==================== */

int ac_base_dec_to_base(ac_int_t n, int base) {
    int i = 0;
    int neg = 0;
    ac_int_t v = n;

    g_base_len = 0;
    if (base != 2 && base != 8 && base != 10 && base != 16) { return 0; }
    /* [English] INT64_MIN cannot be negated (-INT64_MIN overflows): reject. */
    if (n == INT64_MIN) { return 0; }
    if (v == 0) {
        g_base_buf[0] = 48;   /* '0' */
        g_base_len = 1;
        return 1;
    }
    if (v < 0) { neg = 1; v = -v; }
    /* [English] |v| <= 2^63-1 has at most 63 digits in base 2, so the cap
       never truncates a representable value; the neg branch below keeps
       g_base_len <= 63 so a 64-byte string buffer always suffices. */
    while (v > 0 && i < AC_BASE_BUF_SIZE - 1) {
        int d = (int)(v % base);
        if (d < 10) { g_base_buf[i] = 48 + d; }        /* '0'-'9' */
        else { g_base_buf[i] = 65 + (d - 10); }        /* 'A'-'F' */
        v = v / base;
        i++;
    }
    if (neg) {
        if (i >= AC_BASE_BUF_SIZE - 1) { g_base_len = 0; return 0; }
        g_base_buf[i] = 45;   /* '-'（逆序存放 → 出口反转后位于最前） */
        i++;
    }
    g_base_len = i;
    return 1;
}

/* ==================== 出口拼串（仅有的字符串拼接点之一） ==================== */

char *ac_base_to_string(char *buf, int buf_size) {
    int i;
    int n;
    if (buf_size <= 0) { return buf; }
    n = (g_base_len < buf_size - 1) ? g_base_len : (buf_size - 1);
    for (i = 0; i < n; i++) {
        /* 逆序：低位在前 → 正序输出 */
        buf[i] = (char)g_base_buf[g_base_len - 1 - i];
    }
    buf[n] = '\0';
    return buf;
}

/* ==================== 便捷封装 ==================== */

int ac_base_dec_to_bin(ac_int_t n) { return ac_base_dec_to_base(n, 2); }
int ac_base_dec_to_oct(ac_int_t n) { return ac_base_dec_to_base(n, 8); }
int ac_base_dec_to_hex(ac_int_t n) { return ac_base_dec_to_base(n, 16); }

/* ==================== 分数显示 ==================== */

/* [English] Append one raw character code with bounds check.
   Returns the new pos, or -1 when out of room (caller fails the call). */
static int append_code(int32_t *buf, int pos, int32_t code) {
    if (pos < 0 || pos >= AC_BASE_BUF_SIZE) { return -1; }
    buf[pos] = code;
    return pos + 1;
}

/* [English] Append the decimal digits of v (v >= 0) in display order
   (most significant first). Returns the new pos, or -1 on overflow.
   tmp is a fixed-length stack array — not a VLA, not malloc: Zero-GC OK. */
static int append_u64(int32_t *buf, int pos, ac_int_t v) {
    int32_t tmp[24];   /* int64 最多 19 位十进制，24 留余量 */
    int n = 0;
    int i;
    if (v < 0) { return -1; }
    if (v == 0) {
        return append_code(buf, pos, 48);
    }
    while (v > 0 && n < 24) {
        tmp[n] = (int32_t)(48 + (int)(v % 10));
        v /= 10;
        n++;
    }
    if (v > 0) { return -1; }   /* int64 不可能走到（防御性兜底） */
    for (i = n - 1; i >= 0; i--) {
        pos = append_code(buf, pos, tmp[i]);
        if (pos < 0) { return -1; }
    }
    return pos;
}

int ac_base_format_fraction(ac_int_t num, ac_int_t den, int mode) {
    int pos = 0;
    ac_int_t abs_num;
    ac_int_t whole;
    ac_int_t rem;

    g_base_len = 0;
    if (den == 0) { return 0; }
    if (mode != AC_FRAC_IMPROPER && mode != AC_FRAC_MIXED &&
        mode != AC_FRAC_AUTO) { return 0; }
    /* [English] INT64_MIN cannot be negated and INT64_MIN / -1 traps:
       reject both operands before the sign normalization below. */
    if (num == INT64_MIN || den == INT64_MIN) { return 0; }
    if (den < 0) { num = -num; den = -den; }   /* 符号归到分子上 */

    if (num < 0) {
        pos = append_code(g_base_buf, pos, 45);   /* '-' */
        if (pos < 0) { return 0; }
        abs_num = -num;
    } else {
        abs_num = num;
    }

    if (mode == AC_FRAC_IMPROPER) {
        pos = append_u64(g_base_buf, pos, abs_num);
        if (pos < 0) { return 0; }
        pos = append_code(g_base_buf, pos, 47);   /* '/' */
        if (pos < 0) { return 0; }
        pos = append_u64(g_base_buf, pos, den);
        if (pos < 0) { return 0; }
    } else if (mode == AC_FRAC_AUTO) {
        pos = append_u64(g_base_buf, pos, abs_num);
        if (pos < 0) { return 0; }
        if (den != 1) {
            pos = append_code(g_base_buf, pos, 47);
            if (pos < 0) { return 0; }
            pos = append_u64(g_base_buf, pos, den);
            if (pos < 0) { return 0; }
        }
    } else {   /* AC_FRAC_MIXED */
        if (abs_num == 0) {
            /* 0 显示为 "0"（不输出 "0/den"） */
            pos = append_u64(g_base_buf, pos, 0);
            if (pos < 0) { return 0; }
        } else if (abs_num < den) {
            /* 真分数：直接输出 num/den */
            pos = append_u64(g_base_buf, pos, abs_num);
            if (pos < 0) { return 0; }
            pos = append_code(g_base_buf, pos, 47);
            if (pos < 0) { return 0; }
            pos = append_u64(g_base_buf, pos, den);
            if (pos < 0) { return 0; }
        } else {
            /* 带分数：whole [rem/den]；rem == 0 时只输出 whole */
            whole = abs_num / den;
            rem = abs_num % den;
            pos = append_u64(g_base_buf, pos, whole);
            if (pos < 0) { return 0; }
            if (rem > 0) {
                pos = append_code(g_base_buf, pos, 32);   /* ' ' */
                if (pos < 0) { return 0; }
                pos = append_u64(g_base_buf, pos, rem);
                if (pos < 0) { return 0; }
                pos = append_code(g_base_buf, pos, 47);
                if (pos < 0) { return 0; }
                pos = append_u64(g_base_buf, pos, den);
                if (pos < 0) { return 0; }
            }
        }
    }
    g_base_len = pos;
    return 1;
}

/* ==================== 分数出口（仅有的字符串拼接点之二） ==================== */

char *ac_fraction_to_string(char *buf, int buf_size) {
    int i;
    int n;
    if (buf_size <= 0) { return buf; }
    n = (g_base_len < buf_size - 1) ? g_base_len : (buf_size - 1);
    for (i = 0; i < n; i++) {
        /* 正序：内核已按显示顺序写，直接拷 */
        buf[i] = (char)g_base_buf[i];
    }
    buf[n] = '\0';
    return buf;
}

/* ==================== 复位 ==================== */

void ac_base_reset(void) {
    int i;
    for (i = 0; i < AC_BASE_BUF_SIZE; i++) { g_base_buf[i] = 0; }
    g_base_len = 0;
    /* [English] SI / engineering state (round 4) */
    g_base_si_mantissa = 0.0;
    g_base_si_prefix_code = 0;
    g_base_si_exp = 0;
    g_base_si_valid = 0;
    g_base_eng_mantissa = 0.0;
    g_base_eng_exp = 0;
    g_base_eng_valid = 0;
}

/* ==================== SI 前缀 + 工程记数法（128KB 第 4 轮） ====================
 * 行为基准：JS/128KB/src/core_base.js 的 siNormalize/siToString 逐分支对齐；
 * 工程记数法与 SI 共用缩放内核（回吸 / 越界判定一致），不映射前缀字符。 */

double g_base_si_mantissa = 0.0;
int    g_base_si_prefix_code = 0;
int    g_base_si_exp = 0;
int    g_base_si_valid = 0;

double g_base_eng_mantissa = 0.0;
int    g_base_eng_exp = 0;
int    g_base_eng_valid = 0;

/* [English] Decimal exponent (multiple of 3) -> prefix byte; -1 = out of
   range, 0 = empty (exp 0). 181 = U+00B5 MICRO SIGN (exp -6). If the
   target font lacks U+00B5, switch to 956 (μ, GREEK SMALL LETTER MU). */
static int si_prefix_code(int exp) {
    switch (exp) {
    case -24: return 121;   /* 'y' */
    case -21: return 122;   /* 'z' */
    case -18: return 97;    /* 'a' */
    case -15: return 102;   /* 'f' */
    case -12: return 112;   /* 'p' */
    case -9:  return 110;   /* 'n' */
    case -6:  return 181;   /* 'µ' */
    case -3:  return 109;   /* 'm' */
    case 0:   return 0;     /* 空前缀 */
    case 3:   return 107;   /* 'k' */
    case 6:   return 77;    /* 'M' */
    case 9:   return 71;    /* 'G' */
    case 12:  return 84;    /* 'T' */
    case 15:  return 80;    /* 'P' */
    case 18:  return 69;    /* 'E' */
    case 21:  return 90;    /* 'Z' */
    case 24:  return 89;    /* 'Y' */
    default:  return -1;
    }
}

/* [English] Shared scaling kernel: |v| into [1,1000) with exp multiple of 3.
   Returns 1 = ok (writes |mantissa| and exp), 0 = out of SI range, -1 =
   zero / non-finite input. 舍入回吸：×1000 连乘累积舍入让精确千的幂
   （1e-15/1e-24 等）尾数落在 1±2ulp，距 1 不足 1e-12 归拢为 1——窗口
   远大于 1e-16 量级舍入、远小于显示精度（JS 同款常数）。 */
static int si_scale(double v, double *out_av, int *out_exp) {
    double av;
    int exp = 0;
    if (v != v || v == (double)INFINITY || v == -(double)INFINITY || v == 0.0) {
        return -1;
    }
    av = (v < 0.0) ? -v : v;
    while (av >= 1000.0 && exp < 24) { av /= 1000.0; exp += 3; }
    while (av < 1.0 && exp > -24) { av *= 1000.0; exp -= 3; }
    if (av > 0.999999999999 && av < 1.000000000001) { av = 1.0; }
    if (av >= 1000.0 || av < 1.0) { return 0; }
    *out_av = av;
    *out_exp = exp;
    return 1;
}

int ac_base_si_normalize(double v) {
    double av;
    int exp;
    int pc;
    int r;

    g_base_si_mantissa = 0.0;
    g_base_si_prefix_code = 0;
    g_base_si_exp = 0;
    g_base_si_valid = 0;
    r = si_scale(v, &av, &exp);
    if (r != 1) { return 0; }
    pc = si_prefix_code(exp);
    if (pc < 0) { return 0; }
    g_base_si_mantissa = (v < 0.0) ? -av : av;
    g_base_si_prefix_code = pc;
    g_base_si_exp = exp;
    g_base_si_valid = 1;
    return 1;
}

int ac_base_eng_normalize(double v) {
    double av;
    int exp;
    int r;

    g_base_eng_mantissa = 0.0;
    g_base_eng_exp = 0;
    g_base_eng_valid = 0;
    r = si_scale(v, &av, &exp);
    if (r != 1) { return 0; }
    g_base_eng_mantissa = (v < 0.0) ? -av : av;
    g_base_eng_exp = exp;
    g_base_eng_valid = 1;
    return 1;
}

/* [English] String exit for SI (one-shot UI-layer call; snprintf allowed
   HERE only). "%.15g" recovers the shortest decimal form for mantissas
   that arise from decimal inputs (v/1000^k rounds to the nearest double
   of the true quotient, so ≤15 significant digits round-trip cleanly —
   matching JS shortest-repr for the same values). buf is always
   NUL-terminated within buf_size; when the prefix does not fit, the
   mantissa alone is kept. */
char *ac_base_si_to_string(char *buf, int buf_size) {
    int n;
    if (buf_size <= 0) { return buf; }
    buf[0] = '\0';
    if (!g_base_si_valid) { return buf; }
    n = snprintf(buf, (size_t)buf_size, "%.15g", g_base_si_mantissa);
    /* [English] msvcrt's _snprintf returns -1 on truncation (not the C99
       would-be length) and does not NUL-terminate: clamp n and terminate
       manually — both snprintf backends converge to "truncated mantissa". */
    if (n < 0 || n > buf_size - 1) { n = buf_size - 1; }
    buf[buf_size - 1] = '\0';
    if (g_base_si_prefix_code != 0 && n + 2 < buf_size) {
        buf[n] = ' ';
        buf[n + 1] = (char)g_base_si_prefix_code;
        buf[n + 2] = '\0';
    }
    return buf;
}

/* [English] String exit for engineering notation: mantissa + "e<exp>"
   suffix (no suffix when exp == 0, parallel to the empty SI prefix). */
char *ac_base_eng_to_string(char *buf, int buf_size) {
    int n;
    if (buf_size <= 0) { return buf; }
    buf[0] = '\0';
    if (!g_base_eng_valid) { return buf; }
    n = snprintf(buf, (size_t)buf_size, "%.15g", g_base_eng_mantissa);
    /* [English] Same msvcrt truncation semantics as the SI exit. */
    if (n < 0 || n > buf_size - 1) { n = buf_size - 1; }
    buf[buf_size - 1] = '\0';
    if (g_base_eng_exp != 0 && n + 12 < buf_size) {
        /* "e%d" 最长 11 字符（含符号）+ NUL，12 字节余量足够；
           后缀 snprintf 截断时 mantissa 兜底保留 */
        if (snprintf(buf + n, (size_t)(buf_size - n), "e%d", g_base_eng_exp) < 0) {
            buf[n] = '\0';
        }
    }
    return buf;
}
