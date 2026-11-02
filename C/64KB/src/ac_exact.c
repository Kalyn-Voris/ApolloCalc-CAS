/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ============================================================
 * ApolloCalc-CAS · ac_exact.c — 双模态精确数值系统（第 3 轮）
 * 行为基准：JS/64KB/src/core_exact.js，逐函数对齐（含无效判定边界）
 * 内部表示固定为：(g_exact_num / g_exact_den) * √(g_exact_rad)
 *   - g_exact_den 恒为正整数
 *   - g_exact_rad 为无平方因子的正整数（1 表示无根式）
 * C 与 JS 的差异：分量为 int64_t（63 位）而非 double（53 位）。
 *   JS 的 |x| > 2^53-1 越界判定在 C 中对应 x == INT64_MIN
 *   （|INT64_MIN| = 2^63 > INT64_MAX，是唯一越界的 int64 值）。
 *   int64 溢出是 UB，所有乘/加/减一律先做除法预检再运算，
 *   不用 __builtin_*_overflow，保证 arm-none-eabi-gcc 可移植。
 * Zero-GC：无 malloc、无 VLA、无递归（欧几里得/试除法均迭代）。
 * ============================================================ */
#include <stdint.h>
#include <math.h>
#include "ac_types.h"
#include "ac_exact.h"

/* [English] Largest i whose square fits in int64: sqrt(2^63) ≈ 3.037e9. */
#define AC_SQRT_INT64_MAX ((ac_int_t)3037000499)

ac_int_t g_exact_num = 0;
ac_int_t g_exact_den = 1;
ac_int_t g_exact_rad = 1;
int      g_exact_valid = 0;
double   g_approx_value = 0.0;

/* ============================================================
 * 模块一：安全算术原语（先除法预检，后运算）
 * ============================================================ */

int ac_safe_mul(ac_int_t a, ac_int_t b, ac_int_t *out) {
    if (a == 0 || b == 0) { *out = 0; return 1; }
    if (a > 0) {
        if (b > 0) { if (a > INT64_MAX / b) { return 0; } }
        else       { if (b < INT64_MIN / a) { return 0; } }
    } else {
        if (b > 0) { if (a < INT64_MIN / b) { return 0; } }
        else       { if (a < INT64_MAX / b) { return 0; } }
    }
    *out = a * b;
    return 1;
}

int ac_safe_add(ac_int_t a, ac_int_t b, ac_int_t *out) {
    if (b > 0)       { if (a > INT64_MAX - b) { return 0; } }
    else if (b < 0)  { if (a < INT64_MIN - b) { return 0; } }
    *out = a + b;
    return 1;
}

int ac_safe_sub(ac_int_t a, ac_int_t b, ac_int_t *out) {
    if (b < 0) { if (a > INT64_MAX + b) { return 0; } }
    else       { if (a < INT64_MIN + b) { return 0; } }
    *out = a - b;
    return 1;
}

ac_int_t ac_gcd(ac_int_t a, ac_int_t b) {
    ac_int_t t;
    /* [English] |INT64_MIN| = 2^63 overflows int64: unreachable from call
       sites (all components pre-guarded), clamp to keep negation defined. */
    if (a == INT64_MIN) { a = INT64_MAX; }
    if (b == INT64_MIN) { b = INT64_MAX; }
    if (a < 0) { a = -a; }
    if (b < 0) { b = -b; }
    while (b != 0) {
        t = a % b;
        a = b;
        b = t;
    }
    return a;
}

ac_int_t ac_simplify_radical(ac_int_t D) {
    ac_int_t d = D;
    ac_int_t k = 1;
    ac_int_t i;
    ac_int_t sq;
    if (d <= 0) {
        /* 调用方保证 D >= 0；D == 0 时外系数为 0，根号内置 1 */
        g_exact_rad = 1;
        return 0;
    }
    /* i <= d/i 等价于 i*i <= d 且不计算 i*i（避免 i 接近 3.04e9 时溢出）；
       循环体内 sq <= d <= INT64_MAX，乘法本身安全 */
    for (i = 2; i <= d / i; i++) {
        sq = i * i;
        while (d % sq == 0) {
            d = d / sq;
            /* 不变量 d*k*k == D 保证 k*i <= sqrt(D)，此分支防御性不可达 */
            if (k > INT64_MAX / i) { g_exact_valid = 0; return 0; }
            k = k * i;
        }
    }
    g_exact_rad = d;
    return k;
}

/* ============================================================
 * 模块二：内部约分（对应 JS exactSimplifyFraction，不导出）
 * ============================================================ */

static void exact_simplify_fraction(void) {
    ac_int_t an;
    ac_int_t g;
    if (g_exact_den == 0) {
        g_exact_valid = 0;
        return;
    }
    if (g_exact_num == INT64_MIN || g_exact_den == INT64_MIN) {
        /* 翻符号前的防御性保护：分量守卫保证不可达 */
        g_exact_valid = 0;
        return;
    }
    if (g_exact_den < 0) {
        g_exact_num = -g_exact_num;
        g_exact_den = -g_exact_den;
    }
    if (g_exact_num == 0) {
        g_exact_den = 1;
        return;
    }
    an = (g_exact_num < 0) ? -g_exact_num : g_exact_num;
    g = ac_gcd(an, g_exact_den);
    if (g > 1) {
        g_exact_num = g_exact_num / g;
        g_exact_den = g_exact_den / g;
    }
}

/* ============================================================
 * 模块三：智能构造函数（规范化入口）
 * ============================================================ */

void ac_exact_set_fraction(ac_int_t n, ac_int_t d) {
    if (d == 0) {
        /* 分母为零非法：精确模式失效（与 JS 一致：approxValue 不动） */
        ac_exact_invalidate();
        return;
    }
    if (n == INT64_MIN || d == INT64_MIN) {
        /* |x| = 2^63 超出 int64 分量上限（对应 JS 的 2^53-1 越界分支） */
        g_exact_valid = 0;
        g_approx_value = (double)n / (double)d;
        return;
    }
    g_exact_num = n;
    g_exact_den = d;
    g_exact_rad = 1;
    exact_simplify_fraction();
    g_exact_valid = 1;
    ac_exact_update_approx();
}

void ac_exact_set_radical(ac_int_t k, ac_int_t m) {
    ac_int_t s;
    ac_int_t p;
    ac_int_t k2;
    ac_int_t n;
    if (m < 0) {
        /* 负数开方产生复数，不进入精确系统（approxValue 不动） */
        ac_exact_invalidate();
        return;
    }
    if (k == INT64_MIN) {
        g_exact_valid = 0;
        g_approx_value = (double)k * sqrt((double)m);
        return;
    }
    if (m == 0) {
        /* k·√0 = 0，按纯分数零处理 */
        ac_exact_set_fraction(0, 1);
        return;
    }
    /* 完全平方判定：s = round(√m)，s*s == m 则化为纯分数 k·s */
    s = (ac_int_t)floor(sqrt((double)m) + 0.5);
    if (s > 0 && s <= AC_SQRT_INT64_MAX && s * s == m) {
        if (!ac_safe_mul(k, s, &p)) {
            g_exact_valid = 0;
            g_approx_value = (double)k * sqrt((double)m);
            return;
        }
        ac_exact_set_fraction(p, 1);
        return;
    }
    /* 非完全平方：先化简根号内数（写 g_exact_rad），外系数并入分子 */
    k2 = ac_simplify_radical(m);
    if (!ac_safe_mul(k, k2, &n)) {
        g_exact_valid = 0;
        g_approx_value = (double)k * sqrt((double)m);
        return;
    }
    g_exact_num = n;
    g_exact_den = 1;
    g_exact_valid = 1;
    ac_exact_update_approx();
}

void ac_exact_set_pure_real(double v) {
    /* 仅可表为 int64 的整数值进入精确系统，其余只保留近似值。
       强转前在 double 层判界（[-2^63, 2^63)），再要求往返还原精确 */
    if (v >= -9223372036854775808.0 && v < 9223372036854775808.0 &&
        v == (double)(ac_int_t)v) {
        ac_exact_set_fraction((ac_int_t)v, 1);
        return;
    }
    g_exact_valid = 0;
    g_approx_value = v;
}

void ac_exact_invalidate(void) {
    g_exact_valid = 0;
}

void ac_exact_reset(void) {
    g_exact_num = 0;
    g_exact_den = 1;
    g_exact_rad = 1;
    g_exact_valid = 0;
    g_approx_value = 0.0;
}

/* ============================================================
 * 模块四：近似值同步
 * ============================================================ */

void ac_exact_update_approx(void) {
    g_approx_value = ((double)g_exact_num / (double)g_exact_den) * sqrt((double)g_exact_rad);
}

/* ============================================================
 * 模块五：四则运算（原地更新三分量）
 * ============================================================ */

void ac_exact_add(ac_int_t num2, ac_int_t den2, ac_int_t rad2) {
    ac_int_t p1;
    ac_int_t p2;
    ac_int_t newden;
    if (!g_exact_valid) {
        ac_exact_invalidate();
        return;
    }
    if (den2 == 0 || rad2 != 1 || g_exact_rad != 1) {
        /* 仅纯分数可精确相加：三分量无法表达有理数+根式或不同根式之和 */
        ac_exact_invalidate();
        return;
    }
    if (num2 == INT64_MIN || den2 == INT64_MIN) {
        ac_exact_invalidate();
        return;
    }
    /* 溢出预检：|n1*d2|、|n2*d1|、|d1*d2| 均不得越界 */
    if (!ac_safe_mul(g_exact_num, den2, &p1) ||
        !ac_safe_mul(num2, g_exact_den, &p2) ||
        !ac_safe_mul(g_exact_den, den2, &newden)) {
        ac_exact_invalidate();
        return;
    }
    /* safe_add 对同号（可能溢出）与异号（必不溢出）统一判定，
       语义与 JS 的同号预检 + 异号直加完全等价 */
    if (!ac_safe_add(p1, p2, &g_exact_num)) {
        ac_exact_invalidate();
        return;
    }
    g_exact_den = newden;
    exact_simplify_fraction();
    ac_exact_update_approx();
}

void ac_exact_sub(ac_int_t num2, ac_int_t den2, ac_int_t rad2) {
    ac_int_t p1;
    ac_int_t p2;
    ac_int_t newden;
    if (!g_exact_valid) {
        ac_exact_invalidate();
        return;
    }
    if (den2 == 0 || rad2 != 1 || g_exact_rad != 1) {
        ac_exact_invalidate();
        return;
    }
    if (num2 == INT64_MIN || den2 == INT64_MIN) {
        ac_exact_invalidate();
        return;
    }
    if (!ac_safe_mul(g_exact_num, den2, &p1) ||
        !ac_safe_mul(num2, g_exact_den, &p2) ||
        !ac_safe_mul(g_exact_den, den2, &newden)) {
        ac_exact_invalidate();
        return;
    }
    /* safe_sub 失败条件 = JS 的异号 |p1|+|p2| 越界判定；
       同号相减的模不超过 max(|p1|,|p2|)，必不溢出 */
    if (!ac_safe_sub(p1, p2, &g_exact_num)) {
        ac_exact_invalidate();
        return;
    }
    g_exact_den = newden;
    exact_simplify_fraction();
    ac_exact_update_approx();
}

void ac_exact_mul(ac_int_t num2, ac_int_t den2, ac_int_t rad2) {
    ac_int_t newNum;
    ac_int_t newDen;
    ac_int_t newRad;
    ac_int_t k;
    ac_int_t t;
    if (!g_exact_valid) {
        ac_exact_invalidate();
        return;
    }
    if (den2 == 0 || rad2 < 0) {
        ac_exact_invalidate();
        return;
    }
    if (num2 == 0 || g_exact_num == 0 || rad2 == 0) {
        /* 零乘任何数仍为零，精确可表示 */
        ac_exact_set_fraction(0, 1);
        return;
    }
    if (num2 == INT64_MIN || den2 == INT64_MIN) {
        ac_exact_invalidate();
        return;
    }
    /* 溢出预检：|n1*n2|、|d1*d2|、r1*r2 均不得越界 */
    if (!ac_safe_mul(g_exact_num, num2, &newNum) ||
        !ac_safe_mul(g_exact_den, den2, &newDen) ||
        !ac_safe_mul(g_exact_rad, rad2, &newRad)) {
        ac_exact_invalidate();
        return;
    }
    /* 根式化简：提取完全平方因子，外系数并入分子（写 g_exact_rad） */
    g_exact_den = newDen;
    k = ac_simplify_radical(newRad);
    if (k > 1) {
        if (!ac_safe_mul(newNum, k, &t)) {
            ac_exact_invalidate();
            return;
        }
        newNum = t;
    }
    g_exact_num = newNum;
    exact_simplify_fraction();
    ac_exact_update_approx();
}

void ac_exact_div(ac_int_t num2, ac_int_t den2, ac_int_t rad2) {
    ac_int_t newNum;
    ac_int_t t;
    ac_int_t newDen;
    ac_int_t newRad;
    ac_int_t k;
    ac_int_t an2;
    ac_int_t tmp;
    if (!g_exact_valid) {
        ac_exact_invalidate();
        return;
    }
    if (num2 == 0 || den2 == 0 || rad2 < 0 || rad2 == 0) {
        /* 除数为零值（num2=0 或 rad2=0）或非法分母：精确模式失效 */
        ac_exact_invalidate();
        return;
    }
    if (g_exact_num == 0) {
        /* 0 除以非零值仍为 0，精确可表示 */
        ac_exact_set_fraction(0, 1);
        return;
    }
    if (num2 == INT64_MIN || den2 == INT64_MIN) {
        ac_exact_invalidate();
        return;
    }
    an2 = (num2 < 0) ? -num2 : num2;
    /* 有理化：(n1/d1)√r1 ÷ (n2/d2)√r2 = (n1·d2)/(d1·|n2|·r2) · √(r1·r2)，
       分母符号随后随 num2 恢复 */
    if (!ac_safe_mul(g_exact_num, den2, &newNum) ||   /* n1*d2 */
        !ac_safe_mul(g_exact_den, an2, &t) ||         /* d1*|n2| */
        !ac_safe_mul(t, rad2, &newDen) ||             /* d1*|n2|*r2 */
        !ac_safe_mul(g_exact_rad, rad2, &newRad)) {   /* r1*r2 */
        ac_exact_invalidate();
        return;
    }
    if (num2 < 0) {
        newDen = -newDen;
    }
    g_exact_den = newDen;
    g_exact_num = newNum;
    /* 根式化简：提取完全平方因子，外系数并入分子（写 g_exact_rad） */
    k = ac_simplify_radical(newRad);
    if (k > 1) {
        if (!ac_safe_mul(newNum, k, &tmp)) {
            ac_exact_invalidate();
            return;
        }
        g_exact_num = tmp;
    }
    exact_simplify_fraction();
    ac_exact_update_approx();
}

/* ============================================================
 * 模块六：从判别式 D 提取精确根（方程求解器接口）
 * ============================================================ */

void ac_exact_from_discriminant(double D) {
    ac_int_t d;
    ac_int_t di;
    ac_int_t foundDen;
    ac_int_t foundNum;
    ac_int_t m;
    ac_int_t k;
    ac_int_t k2;
    double v;
    double r;
    if (!isfinite(D) || D < 0.0) {
        /* 复数根或非有限值不进入精确系统 */
        ac_exact_invalidate();
        return;
    }
    if (D == 0.0) {
        /* √0 = 0，按纯分数零处理 */
        ac_exact_set_fraction(0, 1);
        return;
    }
    if (D == floor(D)) {
        /* 整数判别式：直接化简根式 k√m。2^63 及以上无法转 int64 */
        if (D >= 9223372036854775808.0) {
            ac_exact_invalidate();
            return;
        }
        di = (ac_int_t)D;
        k = ac_simplify_radical(di);
        g_exact_num = k;
        g_exact_den = 1;
        g_exact_valid = 1;
        ac_exact_update_approx();
        return;
    }
    /* 简单分数判定：寻找最小分母 d 使 D*d 为整数（D>0，无符号负担） */
    foundDen = 0;
    foundNum = 0;
    for (d = 2; d <= 1000000; d++) {
        v = D * (double)d;
        if (v >= 9223372036854775808.0) {
            /* 分子超出 int64 可表示范围：放弃精确表示 */
            break;
        }
        r = floor(v + 0.5);   /* 正数域上的 Math.round */
        if (v == r || (v - r < 1e-9 && r - v < 1e-9)) {
            foundDen = d;
            foundNum = (ac_int_t)r;
            break;
        }
    }
    if (foundDen == 0) {
        /* 无限小数或超出搜索范围：精确模式失效 */
        ac_exact_invalidate();
        return;
    }
    /* √(num/den) = √(num*den)/den：根号内通分后化简 */
    if (!ac_safe_mul(foundNum, foundDen, &m)) {
        ac_exact_invalidate();
        return;
    }
    k2 = ac_simplify_radical(m);
    g_exact_num = k2;
    g_exact_den = foundDen;
    exact_simplify_fraction();
    g_exact_valid = 1;
    ac_exact_update_approx();
}

/* ============================================================
 * 模块七：三元组规范化（C 侧辅助入口；JS 64KB 无对应函数，
 * 各入口函数自身已保证规范化，本函数用于外部直接改写后的修复）
 * ============================================================ */

void ac_exact_normalize(void) {
    ac_int_t k;
    ac_int_t an;
    ac_int_t g;
    ac_int_t t;
    if (g_exact_den == 0) {
        g_exact_valid = 0;
        return;
    }
    if (g_exact_num == INT64_MIN || g_exact_den == INT64_MIN) {
        g_exact_valid = 0;
        return;
    }
    if (g_exact_den < 0) {
        g_exact_num = -g_exact_num;
        g_exact_den = -g_exact_den;
    }
    if (g_exact_num == 0) {
        /* 零无根式：规范化为 0/1·√1 */
        g_exact_den = 1;
        g_exact_rad = 1;
        return;
    }
    if (g_exact_rad < 0) {
        g_exact_valid = 0;
        return;
    }
    if (g_exact_rad == 0) {
        /* (num/den)·√0 = 0 */
        g_exact_num = 0;
        g_exact_den = 1;
        g_exact_rad = 1;
        return;
    }
    k = ac_simplify_radical(g_exact_rad);
    if (k > 1) {
        if (!ac_safe_mul(g_exact_num, k, &t)) {
            g_exact_valid = 0;
            return;
        }
        g_exact_num = t;
    }
    an = (g_exact_num < 0) ? -g_exact_num : g_exact_num;
    g = ac_gcd(an, g_exact_den);
    if (g > 1) {
        g_exact_num = g_exact_num / g;
        g_exact_den = g_exact_den / g;
    }
}
