/* ============================================================
 * ApolloCalc-CAS · ac_bigint.c — 大整数（C 128KB 第 6 轮，C 独有）
 * 基数 1e9（每 limb 9 位十进制，低位在前），容量 20 × 9 = 180 位。
 * 设计权衡见 ac_bigint.h 头注（任务卡的 2^32×16 装不下 100! 的 158 位）。
 * 全部中间量预分配：全局池 g_bi_a/b/tmp/work1/work2 + 模块内 static
 * 工作区；乘法走 2×LIMBS 原始积缓冲（out 可别名操作数）；
 * 除法按 limb 做 schoolbook + 二分试商（每 limb 至多 30 次试探）。
 * Zero-GC：无 malloc、无 VLA、无递归、无 __int128。
 * ============================================================ */
#include "ac_bigint.h"
#include <stdio.h>

/* ==================== 全局池 + 模块工作区 ==================== */

ac_bigint_t g_bi_a;
ac_bigint_t g_bi_b;
ac_bigint_t g_bi_tmp;
ac_bigint_t g_bi_work1;
ac_bigint_t g_bi_work2;

static ac_bigint_t s_bi_r;     /* divmod 余数工作区 */
static ac_bigint_t s_bi_da;    /* divmod 被除数拷贝（q/r 可别名 a/b） */
static ac_bigint_t s_bi_db;    /* divmod 除数拷贝（q/r 可别名 a/b） */
static uint32_t s_bi_t2[AC_BIGINT_LIMBS + 1];   /* divmod 试探积 b·d（≤ 21 limb） */
static uint32_t s_bi_prod[AC_BIGINT_LIMBS * 2];  /* 乘法原始积（防别名） */
static uint32_t s_bi_q[AC_BIGINT_LIMBS];         /* 除法商数字（base 1e9） */

/* ==================== 内部助手（幅值域，忽略符号） ==================== */

/* 规范化：扫掉高位零 limb；top==0 ⟺ sign==0 */
static void bi_norm(ac_bigint_t *x) {
    while (x->top > 0 && x->limbs[x->top - 1] == 0u) { x->top--; }
    if (x->top == 0) { x->sign = 0; }
    else if (x->sign == 0) { x->sign = 1; }
}

/* 幅值比较（无符号 limb 序列）：-1 / 0 / 1 */
static int bi_mag_cmp(const ac_bigint_t *a, const ac_bigint_t *b) {
    int i;
    if (a->top != b->top) { return a->top < b->top ? -1 : 1; }
    for (i = a->top - 1; i >= 0; i--) {
        if (a->limbs[i] != b->limbs[i]) {
            return a->limbs[i] < b->limbs[i] ? -1 : 1;
        }
    }
    return 0;
}

/* 幅值加：out = |a| + |b|（单遍升序，out 可别名 a/b） */
static void bi_mag_add(ac_bigint_t *out, const ac_bigint_t *a, const ac_bigint_t *b) {
    int n = a->top > b->top ? a->top : b->top;
    uint64_t carry = 0;
    int i;
    for (i = 0; i < n; i++) {
        uint64_t t = carry;
        if (i < a->top) { t += a->limbs[i]; }
        if (i < b->top) { t += b->limbs[i]; }
        out->limbs[i] = (uint32_t)(t % AC_BIGINT_BASE);
        carry = t / AC_BIGINT_BASE;
    }
    while (carry > 0u && n < AC_BIGINT_LIMBS) {
        out->limbs[n++] = (uint32_t)(carry % AC_BIGINT_BASE);
        carry /= AC_BIGINT_BASE;
    }
    out->top = n;
}

/* 幅值减：out = |a| − |b|，要求 |a| ≥ |b|（单遍升序，out 可别名） */
static void bi_mag_sub(ac_bigint_t *out, const ac_bigint_t *a, const ac_bigint_t *b) {
    int64_t borrow = 0;
    int i;
    for (i = 0; i < a->top; i++) {
        int64_t t = (int64_t)a->limbs[i] - borrow;
        if (i < b->top) { t -= (int64_t)b->limbs[i]; }
        if (t < 0) { t += (int64_t)AC_BIGINT_BASE; borrow = 1; }
        else { borrow = 0; }
        out->limbs[i] = (uint32_t)t;
    }
    out->top = a->top;
}

/* ==================== 构造与转换 ==================== */

void ac_bi_zero(ac_bigint_t *x) {
    int i;
    for (i = 0; i < AC_BIGINT_LIMBS; i++) { x->limbs[i] = 0u; }
    x->sign = 0;
    x->top = 0;
}

void ac_bi_copy(ac_bigint_t *dst, const ac_bigint_t *src) {
    int i;
    for (i = 0; i < AC_BIGINT_LIMBS; i++) { dst->limbs[i] = src->limbs[i]; }
    dst->sign = src->sign;
    dst->top = src->top;
}

void ac_bi_from_int64(ac_bigint_t *x, int64_t v) {
    uint64_t mag;
    int n = 0;
    ac_bi_zero(x);
    if (v == 0) { return; }
    /* [English] |INT64_MIN| = 2^63 has no positive int64 form: negate in
       uint64 (wraps once) to get the exact magnitude. */
    mag = (v < 0) ? (0u - (uint64_t)v) : (uint64_t)v;
    x->sign = (v < 0) ? -1 : 1;
    while (mag > 0u) {
        x->limbs[n++] = (uint32_t)(mag % AC_BIGINT_BASE);
        mag /= AC_BIGINT_BASE;
    }
    x->top = n;
}

int ac_bi_to_int64(const ac_bigint_t *x, int64_t *out) {
    uint64_t mag = 0u;
    int i;
    if (x->sign == 0 || x->top == 0) { *out = 0; return 1; }
    for (i = x->top - 1; i >= 0; i--) {
        if (mag > (0xFFFFFFFFFFFFFFFFu - (uint64_t)x->limbs[i]) / AC_BIGINT_BASE) {
            return 0;   /* 幅值超 uint64：必然超 int64 */
        }
        mag = mag * AC_BIGINT_BASE + (uint64_t)x->limbs[i];
    }
    if (x->sign > 0) {
        if (mag > (uint64_t)INT64_MAX) { return 0; }
        *out = (int64_t)mag;
    } else {
        if (mag > (uint64_t)INT64_MAX + 1u) { return 0; }
        *out = (mag == (uint64_t)INT64_MAX + 1u) ? INT64_MIN : -(int64_t)mag;
    }
    return 1;
}

/* ==================== 加减（全符号） ==================== */

void ac_bi_add(const ac_bigint_t *a, const ac_bigint_t *b, ac_bigint_t *out) {
    int c;
    if (a->sign == 0) { ac_bi_copy(out, b); return; }
    if (b->sign == 0) { ac_bi_copy(out, a); return; }
    if (a->sign == b->sign) {
        bi_mag_add(out, a, b);
        out->sign = a->sign;
    } else {
        c = bi_mag_cmp(a, b);
        if (c == 0) { ac_bi_zero(out); return; }
        if (c > 0) { bi_mag_sub(out, a, b); out->sign = a->sign; }
        else { bi_mag_sub(out, b, a); out->sign = b->sign; }
    }
    bi_norm(out);
}

void ac_bi_sub(const ac_bigint_t *a, const ac_bigint_t *b, ac_bigint_t *out) {
    /* a − b = a + (−b)：b 拷到模块工作区翻符号后复用加法 */
    ac_bi_copy(&g_bi_work1, b);
    if (g_bi_work1.sign == 1) { g_bi_work1.sign = -1; }
    else if (g_bi_work1.sign == -1) { g_bi_work1.sign = 1; }
    ac_bi_add(a, &g_bi_work1, out);
}

/* ==================== 乘法 ==================== */

void ac_bi_mul(const ac_bigint_t *a, const ac_bigint_t *b, ac_bigint_t *out) {
    uint64_t carry;
    uint64_t t;
    int i;
    int j;
    int k;
    int n;

    if (a->sign == 0 || b->sign == 0) { ac_bi_zero(out); return; }

    /* 原始积进 2×LIMBS 缓冲（out 可别名 a/b）；防御性：只清所需区间 */
    n = a->top + b->top;
    for (k = 0; k < n; k++) { s_bi_prod[k] = 0u; }
    for (i = 0; i < a->top; i++) {
        carry = 0u;
        for (j = 0; j < b->top; j++) {
            /* 1e9 × 1e9 ≈ 1e18 + 两个 < 1e9 的加项 < 2^63：uint64 一次装下 */
            t = (uint64_t)a->limbs[i] * (uint64_t)b->limbs[j]
                + (uint64_t)s_bi_prod[i + j] + carry;
            s_bi_prod[i + j] = (uint32_t)(t % AC_BIGINT_BASE);
            carry = t / AC_BIGINT_BASE;
        }
        k = i + b->top;
        while (carry > 0u && k < AC_BIGINT_LIMBS * 2) {
            /* 数学上乘积 ≤ B^(top_a+top_b)，此分支实际不可达，纯防御 */
            t = (uint64_t)s_bi_prod[k] + carry;
            s_bi_prod[k] = (uint32_t)(t % AC_BIGINT_BASE);
            carry = t / AC_BIGINT_BASE;
            k++;
        }
    }

    /* 拷回 LIMBS limb（超 180 位为契约外输入，防御性截断） */
    ac_bi_zero(out);
    if (n > AC_BIGINT_LIMBS) { n = AC_BIGINT_LIMBS; }
    for (k = 0; k < n; k++) { out->limbs[k] = s_bi_prod[k]; }
    out->top = n;
    out->sign = a->sign * b->sign;
    bi_norm(out);
}

/* ==================== 除法（schoolbook 按 limb + 二分试商） ==================== */

/* 小因子乘：out = b · m（m < 1e9），out 为原始 uint32 数组 + top 返回 */
static int bi_mul_small(const ac_bigint_t *b, uint32_t m,
                        uint32_t *out, int out_cap) {
    uint64_t carry = 0u;
    uint64_t t;
    int i;
    for (i = 0; i < b->top; i++) {
        t = (uint64_t)b->limbs[i] * (uint64_t)m + carry;
        out[i] = (uint32_t)(t % AC_BIGINT_BASE);
        carry = t / AC_BIGINT_BASE;
    }
    if (carry > 0u) {
        if (b->top >= out_cap) { return b->top + 1; }
        out[b->top] = (uint32_t)carry;
        return b->top + 1;
    }
    return b->top;
}

/* 原始数组幅值比较（out/top 形式） */
static int bi_raw_cmp(const uint32_t *a, int atop, const ac_bigint_t *b) {
    int i;
    if (atop != b->top) { return atop < b->top ? -1 : 1; }
    for (i = atop - 1; i >= 0; i--) {
        if (a[i] != b->limbs[i]) { return a[i] < b->limbs[i] ? -1 : 1; }
    }
    return 0;
}

/* 原始数组幅值减：ra = ra − sub（|ra| ≥ |sub|，raw 形式对 raw 形式） */
static void bi_raw_sub(uint32_t *ra, int atop, const uint32_t *sub, int stop) {
    int64_t borrow = 0;
    int i;
    for (i = 0; i < atop; i++) {
        int64_t t = (int64_t)ra[i] - borrow;
        if (i < stop) { t -= (int64_t)sub[i]; }
        if (t < 0) { t += (int64_t)AC_BIGINT_BASE; borrow = 1; }
        else { borrow = 0; }
        ra[i] = (uint32_t)t;
    }
}

void ac_bi_divmod(const ac_bigint_t *a, const ac_bigint_t *b,
                  ac_bigint_t *q, ac_bigint_t *r) {
    const ac_bigint_t *da;
    const ac_bigint_t *db;
    int a_zero;
    int b_zero;
    int atop;
    int i;

    /* 操作数先拷模块静态区并快照零值标志：下面的 ac_bi_zero(q/r) 会清掉
       别名的 a/b——拷贝与判零都必须先于清零（fx_from_bigint 测试钉住） */
    ac_bi_copy(&s_bi_da, a);
    ac_bi_copy(&s_bi_db, b);
    a_zero = (a->sign == 0 || a->top == 0);
    b_zero = (b->sign == 0 || b->top == 0);

    ac_bi_zero(q);
    ac_bi_zero(r);
    if (b_zero || a_zero) {
        return;   /* b == 0 或 a == 0：q = r = 0（契约：调用方查 b） */
    }
    da = &s_bi_da;
    db = &s_bi_db;
    atop = da->top;

    /* 余数工作区：s_bi_r 逐 limb 吃进被除数的高位数字（升幂 schoolbook） */
    ac_bi_zero(&s_bi_r);
    for (i = 0; i < AC_BIGINT_LIMBS; i++) { s_bi_q[i] = 0u; }

    for (i = atop - 1; i >= 0; i--) {
        /* r = r·1e9 + da[i]（乘小数加一位） */
        uint64_t carry = (uint64_t)da->limbs[i];
        int k;
        for (k = 0; k < s_bi_r.top; k++) {
            uint64_t t = (uint64_t)s_bi_r.limbs[k] * AC_BIGINT_BASE + carry;
            s_bi_r.limbs[k] = (uint32_t)(t % AC_BIGINT_BASE);
            carry = t / AC_BIGINT_BASE;
        }
        while (carry > 0u) {
            s_bi_r.limbs[s_bi_r.top] = (uint32_t)(carry % AC_BIGINT_BASE);
            carry /= AC_BIGINT_BASE;
            s_bi_r.top++;
        }
        /* 二分试商：d ∈ [0, 1e9)，使 db·d ≤ r < db·(d+1) */
        {
            uint32_t lo = 0u;
            uint32_t hi = AC_BIGINT_BASE - 1u;
            while (lo < hi) {
                uint32_t mid = lo + (hi - lo + 1u) / 2u;
                int mt = bi_mul_small(db, mid, s_bi_t2, AC_BIGINT_LIMBS + 1);
                if (bi_raw_cmp(s_bi_t2, mt, &s_bi_r) <= 0) { lo = mid; }
                else { hi = mid - 1u; }
            }
            s_bi_q[i] = lo;
            /* r = r − db·lo */
            if (lo > 0u) {
                int mt = bi_mul_small(db, lo, s_bi_t2, AC_BIGINT_LIMBS + 1);
                bi_raw_sub(s_bi_r.limbs, s_bi_r.top, s_bi_t2, mt);
                while (s_bi_r.top > 0 && s_bi_r.limbs[s_bi_r.top - 1] == 0u) {
                    s_bi_r.top--;
                }
            }
        }
    }

    /* 商：符号 = sa·sb（截断语义）；余数：符号 = sa（C 语义） */
    for (i = 0; i < AC_BIGINT_LIMBS; i++) { q->limbs[i] = s_bi_q[i]; }
    q->top = atop;
    q->sign = da->sign * db->sign;
    bi_norm(q);
    ac_bi_copy(r, &s_bi_r);
    if (r->top > 0) { r->sign = da->sign; }
    bi_norm(r);
}

/* ==================== 比较 ==================== */

int ac_bi_cmp(const ac_bigint_t *a, const ac_bigint_t *b) {
    int m;
    if (a->sign != b->sign) { return a->sign < b->sign ? -1 : 1; }
    if (a->sign == 0) { return 0; }
    m = bi_mag_cmp(a, b);
    return (a->sign > 0) ? m : -m;
}

/* ==================== 阶乘 / 斐波那契 ==================== */

int ac_bi_factorial(int n, ac_bigint_t *out) {
    int i;
    if (n < 0 || n > 100) { return 0; }
    ac_bi_from_int64(out, 1);
    for (i = 2; i <= n; i++) {
        ac_bi_from_int64(&g_bi_work1, i);
        ac_bi_mul(out, &g_bi_work1, out);   /* out 别名安全（原始积缓冲） */
    }
    return 1;
}

int ac_bi_fibonacci(int n, ac_bigint_t *out) {
    int i;
    if (n < 0 || n > 200) { return 0; }
    if (n == 0) { ac_bi_zero(out); return 1; }
    ac_bi_from_int64(&g_bi_work1, 0);   /* a = F(0) */
    ac_bi_from_int64(out, 1);           /* b = F(1) */
    for (i = 2; i <= n; i++) {
        ac_bi_add(&g_bi_work1, out, &g_bi_work2);  /* c = a + b */
        ac_bi_copy(&g_bi_work1, out);              /* a = b */
        ac_bi_copy(out, &g_bi_work2);              /* b = c */
    }
    return 1;
}

/* ==================== 十进制出口 ==================== */

char *ac_bi_to_string(const ac_bigint_t *x, char *buf, int buf_size) {
    int pos = 0;
    int i;
    int n;
    if (buf_size <= 0) { return buf; }
    buf[0] = '\0';
    if (x->sign == 0 || x->top == 0) {
        if (buf_size > 1) { buf[0] = '0'; buf[1] = '\0'; }
        return buf;
    }
    if (x->sign < 0 && buf_size > 1) { buf[pos++] = '-'; }
    for (i = x->top - 1; i >= 0; i--) {
        /* 最高 limb 无前导零，其余按 9 位十进制补零（基数 1e9 的红利） */
        if (pos >= buf_size - 1) { break; }
        if (i == x->top - 1) {
            n = snprintf(buf + pos, (size_t)(buf_size - pos), "%u",
                         (unsigned)x->limbs[i]);
        } else {
            n = snprintf(buf + pos, (size_t)(buf_size - pos), "%09u",
                         (unsigned)x->limbs[i]);
        }
        if (n < 0 || n >= buf_size - pos) {
            pos = buf_size - 1;   /* 截断（msvcrt 返 -1 契约，见第 4 轮） */
            break;
        }
        pos += n;
    }
    buf[pos] = '\0';
    return buf;
}
