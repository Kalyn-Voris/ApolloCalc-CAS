/* ============================================================
 * ApolloCalc-CAS · ac_base.c — 进制转换 + 分数显示（第 8 轮，C 独有）
 * 功能：dec_to_base（整数 → 2/8/10/16 进制字符码缓冲，逆序存放，
 *       行为对齐 JS 128KB core_base.js 的 decToBase）、
 *       format_fraction（分数 → 显示字符码，正序存放；假分数 /
 *       带分数 / AUTO 三模式，C 独有功能）、
 *       两个出口拼串函数（全模块仅有的字符串拼接点，UI 层一次性调用）。
 * Zero-GC：内核只填充预分配字符码缓冲，无 malloc、无 VLA、无递归、
 *          无 snprintf；出口手写循环拼串，缓冲空间由调用方（UI 层）提供。
 * 注意：进制缓冲逆序、分数缓冲正序，各有专属出口，绝不能交叉使用。
 * ============================================================ */
#include "ac_base.h"

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
}
