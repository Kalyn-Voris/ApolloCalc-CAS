/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_pro_sturm.c — Sturm 序列与实根隔离(v2.5.0 F3,C 全功能 deg 1..8)

   行为边界由 docs/contracts/sturm-format-v25.md 逐字冻结(本文件头注是该
   契约的实现侧镜像;契约变更须先改契约再改码)。JS 侧 deg ≤ 4 镜像在
   core_ratroot.js 扩段(5..8 → EDEG 成文分叉)。

   数学语义(契约 §2):
     p₀ = p_sf(square-free 预归一后的本原多项式),p₁ = p_sf′,
     p_{k+1} = −rem(p_{k−1}, p_k),rem 为 Q 上唯一带余除法的余式。
     实现取「−rem 的正整数倍」:prem = lc(B)^steps·r(r 为 Q 余数),故
        p_{k+1} := −prem·(lc(B) < 0 且 steps 奇 ? −1 : +1)
     是 −r 的正整数倍,再提正 content 约减、不做任何额外变号 —— 与契约的
     「精确有理余 → 正 lcm 清母线 → 正 content 约减 → 不变号」逐点等价
     (正倍数不改变 V 计数;prem 的符号因子在此被显式校正,符号陷阱解除)。
     gcd(p_sf, p_sf′) = 1 ⟹ 链止于非零常数,成员数 ≤ deg+1 ≤ 9。

     V(t) = 链各成员在 t 处符号序列(跳过精确 0)的变差数;
     count(a,b) = V(a) − V(b) + [p₀(a) = 0](闭区间 [a,b],端点根计入);
     ±∞ 端点 = sign(lc)·(±1)^deg 序列直接计变差(零求值成本)。
     隔离 = (−B,B] 半开格显式栈二分(B = Cauchy 界上取整到 2 的幂),
     中点在同一 2^dep 网格上精确;格内 ≥ 2 根且深度 ≥ 48 → 未解析 "!"。

   精度纪律:C 侧全程 ac_bigint(上游 20 limb = 180 位十进制):
     端点与二分点 = num/2^dep(num ≤ 2^59 < 2^63,int64 精确);
     V 求值 = 整数 Horner:N ← N·num + c_i·2^{e·(dg−i)}(幂表 2^{e·j}),
     恒等式 N = p(num/2^e)·2^{e·dg}(den 恒正 ⟹ 符号 = N 的符号);
     ac_bi_mul 对 > 20 limb 的积「防御性截断」(上游 ac_bigint.c:205),
     故本 TU 的每一次 mul/add/sub 前都做十进制位数预检,超界 → ECOEF
     (绝不消费截断值 —— 链系数离群 + 深端点的极端角落诚实拒绝;
     域内 60K 样本链峰值 144 bits,中间量余量 ≥ 25 bits)。

   内存纪律:零 static 对象、零 malloc、零 VLA、零递归(二分显式栈在
   sturm 视图);链与二分栈跨 V() 求值存活于 sturm 视图(2912 B ≤ 3848
   吸收线,.bss 零增长);bigint 工作槽走栈上 ac_bigint_t(dec 视图先例)。
   错误码走返回值(count 成功值 = 非负根数)。 */

#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>

#include "ac_pro_config.h"
#include "ac_pro.h"
#include "../../../C/128KB/src/ac_exact.h"
#include "../../../C/128KB/src/ac_bigint.h"

#if AC_PRO_ACTIVE

#define ST_SRC_MAX      128    /* 契约 §3:输入长度上限(同 sqfree) */
#define ST_COEF_MAX     999    /* 契约 §3:系数域 */
#define ST_END_MAX      999999 /* 契约 §3:整数端点域 |n| 上限 */
#define ST_CELLS        16     /* 隔离输出格上界(≤ 8 根格 + 4 未解析 + 1 端点格) */

/* 错误码(契约 §6),ac_pro.h STURM 锚点区定义:
 * OK=0 / EPARSE=−1 / EDEG=−2 / ECOEF=−3 / EPARAM=−4 / EOVER=−5 / EINTERNAL=−6 */

/* ---- 视图访问(链三角打包:成员 k 系数 j = 下标 off[k]+j) ---- */

#define ST_IDX(k, j) (s_pro_st_off(k) + (j))

static void st_coef_get(int k, int j, ac_bigint_t *out) {
    int base = ST_IDX(k, j);
    int i;
    ac_bi_zero(out);
    for (i = 0; i < AC_PRO_ST_LIMBS; i++) {
        out->limbs[i] = s_pro_st_limb(base * AC_PRO_ST_LIMBS + i);
    }
    out->top = AC_PRO_ST_LIMBS;
    out->sign = s_pro_st_sgn(base);
    while (out->top > 0 && out->limbs[out->top - 1] == 0u) { out->top--; }
    if (out->top == 0) { out->sign = 0; }
}

static int st_coef_put(int base, const ac_bigint_t *x) {
    int i;
    if (x->top > AC_PRO_ST_LIMBS) { return AC_PRO_STM_ECOEF; }   /* > 72 位十进制:离群 */
    for (i = 0; i < AC_PRO_ST_LIMBS; i++) {
        s_pro_st_limb(base * AC_PRO_ST_LIMBS + i) = (i < x->top) ? x->limbs[i] : 0u;
    }
    s_pro_st_sgn(base) = (int8_t)x->sign;
    return 0;
}

/* ---- 带溢出预检的 bigint 算术(ac_bi_mul 对 >20 limb 的积防御性截断,
 * 上游 ac_bigint.c:205 —— 本 TU 绝不消费截断值) ---- */

static int st_dig(const ac_bigint_t *x) {
    uint32_t t;
    int d = 1;
    if (x->top == 0) { return 0; }
    t = x->limbs[x->top - 1];
    while (t >= 10u) { d++; t /= 10u; }
    return 9 * (x->top - 1) + d;
}

static int st_mul_ck(const ac_bigint_t *a, const ac_bigint_t *b, ac_bigint_t *out) {
    ac_bigint_t tmp;
    if (st_dig(a) + st_dig(b) > 180) { return AC_PRO_STM_ECOEF; }
    /* ac_bi_mul 的 out==a 别名会破坏符号:out->sign = a->sign * b->sign 在
     * ac_bi_zero(out) 之后求值(a 即 out,sign 已清 0),bi_norm 把非零幅值的
     * 0 号强制回 +1 —— 负操作数一律翻正(实测踩坑:-3·1 → +3)。
     * 故本 TU 的乘法一律经 tmp 中转,绝不依赖别名安全。 */
    if (out == a || out == b) {
        ac_bi_mul(a, b, &tmp);
        ac_bi_copy(out, &tmp);
    } else {
        ac_bi_mul(a, b, out);
    }
    return 0;
}

static int st_add_ck(const ac_bigint_t *a, const ac_bigint_t *b, ac_bigint_t *out) {
    if (st_dig(a) >= 180 || st_dig(b) >= 180) { return AC_PRO_STM_ECOEF; }
    ac_bi_add(a, b, out);
    return 0;
}

static int st_sub_ck(const ac_bigint_t *a, const ac_bigint_t *b, ac_bigint_t *out) {
    if (st_dig(a) >= 180 || st_dig(b) >= 180) { return AC_PRO_STM_ECOEF; }
    ac_bi_sub(a, b, out);
    return 0;
}

/* ---- bigint 欧几里得 GCD(divmod 迭代;入参被改写,商在 a) ---- */

static void st_gcd_bi(ac_bigint_t *a, ac_bigint_t *b) {
    ac_bigint_t q;
    ac_bigint_t r;
    while (b->sign != 0) {
        ac_bi_divmod(a, b, &q, &r);
        ac_bi_copy(a, b);
        ac_bi_copy(b, &r);
    }
}

/* ---- bigint 本原化:提正 content;lc_flipsign 非 0 时首项化正
 * (SFF gcd 结果用;链成员恒传 0 —— 绝不翻转符号) ---- */

static int st_primitive_bi(ac_bigint_t *c, int dg, int lc_flipsign) {
    ac_bigint_t g;
    ac_bigint_t t;
    ac_bigint_t q;
    ac_bigint_t r;
    int i;
    if (dg < 0) { return 0; }
    ac_bi_from_int64(&g, 0);
    for (i = 0; i <= dg; i++) {
        ac_bi_copy(&t, &c[i]);
        if (t.sign < 0) { t.sign = 1; }
        st_gcd_bi(&t, &g);
        ac_bi_copy(&g, &t);
    }
    if (g.sign == 0) { ac_bi_from_int64(&g, 1); }
    for (i = 0; i <= dg; i++) {
        ac_bi_divmod(&c[i], &g, &q, &r);
        if (r.sign != 0) { return AC_PRO_STM_EINTERNAL; }   /* content 必须整除 */
        ac_bi_copy(&c[i], &q);
    }
    if (lc_flipsign && c[dg].sign < 0) {
        for (i = 0; i <= dg; i++) { c[i].sign = (int8_t)(-c[i].sign); }
    }
    return 0;
}

/* ---- bigint 伪除:w·lc(B)^steps = q·B + r(qout/qd 可为 0 指针)
 * 余式入 w(次数 dw);steps = 消去轮数。溢出预检 → ECOEF。 ---- */

static int st_prem(const ac_bigint_t *a, int da, const ac_bigint_t *b, int db,
                   ac_bigint_t *w, int *dw, int *steps,
                   ac_bigint_t *qout, int *qd_out,
                   ac_bigint_t *t1, ac_bigint_t *t2) {
    ac_bigint_t lcg;
    int i;
    int rc;
    *steps = 0;
    if (qout != 0) { *qd_out = -1; }
    if (db < 0) { return AC_PRO_STM_EINTERNAL; }
    *dw = da;
    if (da < 0) { return 0; }
    for (i = 0; i <= da; i++) {
        ac_bi_copy(&w[i], &a[i]);
        if (qout != 0) { ac_bi_zero(&qout[i]); }
    }
    ac_bi_copy(&lcg, &b[db]);

    while (*dw >= db && *dw >= 0) {
        ac_bigint_t lfw;
        int k = *dw - db;
        ac_bi_copy(&lfw, &w[*dw]);
        for (i = *dw; i >= 0; i--) {
            rc = st_mul_ck(&w[i], &lcg, t1);
            if (rc != 0) { return rc; }
            ac_bi_copy(&w[i], t1);
            if (i >= k && i - k <= db) {
                rc = st_mul_ck(&lfw, &b[i - k], t2);
                if (rc != 0) { return rc; }
                rc = st_sub_ck(&w[i], t2, &w[i]);
                if (rc != 0) { return rc; }
            }
        }
        if (qout != 0) {
            for (i = *dw; i >= 0; i--) {
                rc = st_mul_ck(&qout[i], &lcg, t1);
                if (rc != 0) { return rc; }
                ac_bi_copy(&qout[i], t1);
            }
            rc = st_add_ck(&qout[k], &lfw, &qout[k]);
            if (rc != 0) { return rc; }
        }
        (*steps)++;
        while (*dw >= 0 && w[*dw].sign == 0) { (*dw)--; }
    }
    if (qout != 0) { *qd_out = da - db; }   /* 精确除的商次数 = da−db(末轮 k 是实测踩坑) */
    return 0;
}

/* ---- 词法(sqfree 契约 §3 同域:小写 x、|c| ≤ 999、deg ≤ 8、ASCII、≤128 B) ---- */

static int st_parse(const char *s, ac_int_t *coef, int *deg_out) {
    long long c9[AC_PRO_ST_MAX_DEG + 1];
    int seen[AC_PRO_ST_MAX_DEG + 1];
    const char *p = s;
    int k;
    int any = 0;
    int len = 0;

    for (k = 0; k <= AC_PRO_ST_MAX_DEG; k++) { c9[k] = 0; seen[k] = 0; }

    while (s[len] != '\0') { len++; }
    if (len == 0 || len > ST_SRC_MAX) { return AC_PRO_STM_EPARSE; }
    p = s;

    for (;;) {
        int neg = 0;
        long long c = 0;
        int saw_coef = 0;
        int saw_x = 0;
        int e = 1;

        for (;;) {
            while (*p == ' ' || *p == '\t') { p++; }
            if (*p == '-') { neg = !neg; p++; }
            else if (*p == '+') { p++; }
            else { break; }
        }
        while (*p == ' ' || *p == '\t') { p++; }

        while (*p >= '0' && *p <= '9') {
            c = c * 10 + (*p - '0');
            if (c > ST_COEF_MAX) { return AC_PRO_STM_ECOEF; }
            saw_coef = 1;
            p++;
        }
        while (*p == ' ' || *p == '\t') { p++; }
        if (*p == '*') {
            if (!saw_coef) { return AC_PRO_STM_EPARSE; }
            p++;
            while (*p == ' ' || *p == '\t') { p++; }
            if (*p != 'x') { return AC_PRO_STM_EPARSE; }
        }
        if (*p == 'x') {
            p++;
            saw_x = 1;
            while (*p == ' ' || *p == '\t') { p++; }
            if (*p == '^') {
                p++;
                if (!(*p >= '0' && *p <= '9')) { return AC_PRO_STM_EPARSE; }
                e = 0;
                for (;;) {
                    e = e * 10 + (*p - '0');
                    if (e > AC_PRO_ST_MAX_DEG) { return AC_PRO_STM_EDEG; }   /* ≥9 当场 */
                    p++;
                    if (!(*p >= '0' && *p <= '9')) { break; }
                }
                if (e == 0) { return AC_PRO_STM_EPARSE; }
            }
        } else if (!saw_coef) {
            return AC_PRO_STM_EPARSE;
        }
        while (*p == ' ' || *p == '\t') { p++; }
        if (saw_x && (*p != '\0' && *p != '+' && *p != '-')) {
            return AC_PRO_STM_EPARSE;
        }

        if (saw_x && saw_coef) { c = neg ? -c : c; }
        else if (saw_x) { c = neg ? -1 : 1; }
        else { c = neg ? -c : c; e = 0; }
        if (seen[e]) { return AC_PRO_STM_EPARSE; }   /* 同次项重复 */
        seen[e] = 1;
        c9[e] = c;
        any = 1;

        if (*p == '\0') { break; }
        if (*p != '+' && *p != '-') { return AC_PRO_STM_EPARSE; }
    }
    if (!any) { return AC_PRO_STM_EPARSE; }

    for (k = 0; k <= AC_PRO_ST_MAX_DEG; k++) { coef[k] = (ac_int_t)c9[k]; }
    *deg_out = AC_PRO_ST_MAX_DEG;
    while (*deg_out >= 0 && coef[*deg_out] == 0) { (*deg_out)--; }
    return 0;
}

/* ---- 链构建(契约 §2.1;结果入 sturm 视图) ---- */

static int st_build(const char *src) {
    ac_int_t c0[AC_PRO_ST_MAX_DEG + 1];
    int d0;
    ac_bigint_t p[AC_PRO_ST_MAX_DEG + 1];
    ac_bigint_t tmp[AC_PRO_ST_MAX_DEG + 1];
    ac_bigint_t w[AC_PRO_ST_MAX_DEG + 1];
    ac_bigint_t q[AC_PRO_ST_MAX_DEG + 1];
    ac_bigint_t der[AC_PRO_ST_MAX_DEG + 1];
    ac_bigint_t ga[AC_PRO_ST_MAX_DEG + 1];
    ac_bigint_t gb2[AC_PRO_ST_MAX_DEG + 1];
    ac_bigint_t t1;
    ac_bigint_t t2;
    int dp;
    int rc;
    int i;
    int k;
    int steps;
    int dw;
    int dga;
    int dgb;

    rc = st_parse(src, c0, &d0);
    if (rc != 0) { return rc; }
    if (d0 < 0) { return AC_PRO_STM_ECOEF; }   /* 全零多项式 */

    /* 本原化 + 首项化正(int64 域,|c| ≤ 999 无溢出;计数对标量缩放不变) */
    {
        ac_int_t g = 0;
        int neg;
        for (i = 0; i <= d0; i++) {
            ac_int_t a = (c0[i] < 0) ? -c0[i] : c0[i];
            if (a != 0) { g = (g == 0) ? a : ac_gcd(g, a); }
        }
        if (g == 0) { g = 1; }
        neg = (c0[d0] < 0);
        for (i = 0; i <= d0; i++) { c0[i] = neg ? -(c0[i] / g) : (c0[i] / g); }
    }

    for (i = 0; i <= AC_PRO_ST_MAX_DEG; i++) {
        ac_bi_zero(&p[i]);
        ac_bi_zero(&tmp[i]);
        ac_bi_zero(&w[i]);
        ac_bi_zero(&q[i]);
        ac_bi_zero(&der[i]);
        ac_bi_zero(&ga[i]);
        ac_bi_zero(&gb2[i]);
    }
    for (i = 0; i <= d0; i++) { ac_bi_from_int64(&p[i], (int64_t)c0[i]); }
    dp = d0;

    /* SFF(契约 §2.1-2):g = gcd(p, p′)(bigint prem + 逐步本原),
     * p_sf = p / g(精确除);gcd(p_sf, p_sf′) = 1 */
    if (dp >= 1) {
        int dder;
        for (i = 1; i <= dp; i++) {
            ac_bi_from_int64(&t1, (int64_t)i);
            rc = st_mul_ck(&p[i], &t1, &der[i - 1]);
            if (rc != 0) { return rc; }
        }
        dder = dp - 1;
        for (i = 0; i <= dp; i++) { ac_bi_copy(&ga[i], &p[i]); }
        dga = dp;
        for (i = 0; i <= dder; i++) { ac_bi_copy(&gb2[i], &der[i]); }
        dgb = dder;
        /* 欧几里得:(ga, gb2) ← (gb2, primitive(prem(ga, gb2)));b = 0 时 a 即 gcd */
        while (dgb >= 0) {
            rc = st_prem(ga, dga, gb2, dgb, w, &dw, &steps, 0, 0, &t1, &t2);
            if (rc != 0) { return rc; }
            for (i = 0; i <= AC_PRO_ST_MAX_DEG; i++) { ac_bi_copy(&tmp[i], &gb2[i]); }
            dga = dgb;
            for (i = 0; i <= AC_PRO_ST_MAX_DEG; i++) { ac_bi_copy(&ga[i], &tmp[i]); }
            dgb = dw;
            for (i = 0; i <= AC_PRO_ST_MAX_DEG; i++) { ac_bi_copy(&gb2[i], &w[i]); }
            if (dgb >= 0) {
                rc = st_primitive_bi(gb2, dgb, 1);
                if (rc != 0) { return rc; }
            }
        }
        rc = st_primitive_bi(ga, dga, 1);
        if (rc != 0) { return rc; }
        if (dga >= 1) {
            /* 精确除 p_sf = p / ga:prem 商 = 真商·lc(ga)^steps */
            int dq2;
            rc = st_prem(p, dp, ga, dga, w, &dw, &steps, q, &dq2, &t1, &t2);
            if (rc != 0) { return rc; }
            if (dw >= 0) { return AC_PRO_STM_EINTERNAL; }   /* 余式非零 */
            {
                ac_bigint_t scale;
                ac_bigint_t qq;
                ac_bigint_t rr;
                ac_bi_from_int64(&scale, 1);
                for (i = 0; i < steps; i++) {
                    rc = st_mul_ck(&scale, &ga[dga], &scale);
                    if (rc != 0) { return rc; }
                }
                for (i = 0; i <= dq2; i++) {
                    ac_bi_divmod(&q[i], &scale, &qq, &rr);
                    if (rr.sign != 0) { return AC_PRO_STM_EINTERNAL; }
                    ac_bi_copy(&q[i], &qq);
                }
            }
            for (i = 0; i <= AC_PRO_ST_MAX_DEG; i++) { ac_bi_zero(&p[i]); }
            for (i = 0; i <= dq2; i++) { ac_bi_copy(&p[i], &q[i]); }
            dp = dq2;
        }
    }

    /* 链成员 0 = p_sf */
    s_pro_st_off(0) = 0;
    s_pro_st_deg(0) = dp;
    for (i = 0; i <= dp; i++) {
        rc = st_coef_put(i, &p[i]);
        if (rc != 0) { return rc; }
    }
    s_pro_st_nmem = 1;
    if (dp == 0) { return 0; }   /* 常数:单成员链(计数 0 / 隔离 []) */

    /* 链成员 1 = p_sf′(符号随导数,不修饰) */
    k = 1;
    s_pro_st_off(1) = dp + 1;
    s_pro_st_deg(1) = dp - 1;
    for (i = 1; i <= dp; i++) {
        ac_bi_from_int64(&t1, (int64_t)i);
        rc = st_mul_ck(&p[i], &t1, &t2);
        if (rc != 0) { return rc; }
        rc = st_coef_put(s_pro_st_off(1) + (i - 1), &t2);
        if (rc != 0) { return rc; }
    }
    s_pro_st_nmem = 2;

    /* 迭代:p_{k+1} = ±prem(p_{k−1}, p_k)(符号校正见文件头),content 约减 */
    while (s_pro_st_deg(k) >= 1) {
        int da = s_pro_st_deg(k - 1);
        int db = s_pro_st_deg(k);
        int lc_neg;
        int flip;
        int base;
        ac_bigint_t cc[AC_PRO_ST_MAX_DEG + 1];
        if (k + 1 >= AC_PRO_ST_MEMS) { return AC_PRO_STM_EINTERNAL; }
        for (i = 0; i <= da; i++) { st_coef_get(k - 1, i, &tmp[i]); }
        for (i = 0; i <= db; i++) { st_coef_get(k, i, &gb2[i]); }
        rc = st_prem(tmp, da, gb2, db, w, &dw, &steps, 0, 0, &t1, &t2);
        if (rc != 0) { return rc; }
        if (dw < 0) { return AC_PRO_STM_EINTERNAL; }   /* sf 后整除 = 不变式破坏 */
        lc_neg = (gb2[db].sign < 0);
        flip = lc_neg ? ((steps % 2) == 0) : 1;
        k++;
        s_pro_st_off(k) = s_pro_st_off(k - 1) + db + 1;
        s_pro_st_deg(k) = dw;
        base = s_pro_st_off(k);
        for (i = 0; i <= dw; i++) {
            if (flip) { w[i].sign = (int8_t)(-w[i].sign); }
            rc = st_coef_put(base + i, &w[i]);
            if (rc != 0) { return rc; }
            ac_bi_copy(&cc[i], &w[i]);
        }
        rc = st_primitive_bi(cc, dw, 0);
        if (rc != 0) { return rc; }
        for (i = 0; i <= dw; i++) {
            rc = st_coef_put(base + i, &cc[i]);
            if (rc != 0) { return rc; }
        }
        s_pro_st_nmem = k + 1;
    }
    return 0;
}

/* ---- V(num, e):链在 num/2^e 处的符号变差数(整数 Horner + 幂表)
 * 返回变差数(≥0)或负错误码。 ---- */

static int st_horner_sign(int k, int64_t num, int e, int *sg) {
#define ST_TRACE 1
#ifdef ST_TRACE
    fprintf(stderr, "[horns k=%d num=%d e=%d dg=%d]", k, (int)num, e, s_pro_st_deg(k));
#endif
    ac_bigint_t pw[AC_PRO_ST_MAX_DEG + 1];
    ac_bigint_t n;
    ac_bigint_t t1;
    int dg = s_pro_st_deg(k);
    int j;
    int i;
    int rc;

    ac_bi_from_int64(&pw[0], 1);
    for (j = 1; j <= dg; j++) {
        ac_bi_from_int64(&t1, (int64_t)1 << e);
        rc = st_mul_ck(&pw[j - 1], &t1, &pw[j]);
        if (rc != 0) { return rc; }
    }
    st_coef_get(k, dg, &n);
    for (i = dg - 1; i >= 0; i--) {
        ac_bigint_t term;
        ac_bigint_t nn;
        st_coef_get(k, i, &term);
        fprintf(stderr, " [i=%d cs=%d]", i, (int)term.sign);
        rc = st_mul_ck(&term, &pw[dg - i], &term);
        if (rc != 0) { return rc; }
        fprintf(stderr, " ts=%d", (int)term.sign);
        ac_bi_from_int64(&nn, num);
        rc = st_mul_ck(&n, &nn, &t1);
        if (rc != 0) { return rc; }
        fprintf(stderr, " t1s=%d", (int)t1.sign);
        rc = st_add_ck(&t1, &term, &n);
        if (rc != 0) { return rc; }
        fprintf(stderr, " ns=%d", (int)n.sign);
    }
    *sg = n.sign;
#ifdef ST_TRACE
    fprintf(stderr, " sg=%d\n", (int)n.sign); fflush(stderr);
#endif
    return 0;
}

static int st_variations(int64_t num, int e) {
    int last = 0;
    int v = 0;
    int k;
    for (k = 0; k < s_pro_st_nmem; k++) {
        int sg;
        int rc = st_horner_sign(k, num, e, &sg);
        if (rc != 0) { return rc; }
        if (sg == 0) { continue; }
        if (last != 0 && sg != last) { v++; }
        last = sg;
    }
    return v;
}

/* ---- p₀ 在 num/2^e 处是否恰为零(精确;1 = 零) ---- */

static int st_p0_is_zero(int64_t num, int e) {
    int sg;
    int rc = st_horner_sign(0, num, e, &sg);
    if (rc != 0) { return rc; }
    return (sg == 0) ? 1 : 0;
}

/* ---- 端点解析(契约 §3:"inf"/"-inf"/整数 |n| ≤ 999999) ---- */

static int st_parse_endpoint(const char *s, int *kind, int64_t *val) {
    const char *p;
    int neg = 0;
    long long v = 0;
    int nd = 0;
    if (s == 0) { return AC_PRO_STM_EPARAM; }
    if (s[0] == 'i' && s[1] == 'n' && s[2] == 'f' && s[3] == '\0') {
        *kind = 1;
        *val = 0;
        return 0;
    }
    if (s[0] == '-' && s[1] == 'i' && s[2] == 'n' && s[3] == 'f' && s[4] == '\0') {
        *kind = -1;
        *val = 0;
        return 0;
    }
    p = s;
    while (*p == ' ' || *p == '\t') { p++; }
    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') { p++; }
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        nd++;
        if (nd > 6 || v > ST_END_MAX) { return AC_PRO_STM_EPARAM; }
        p++;
    }
    if (nd == 0) { return AC_PRO_STM_EPARAM; }
    while (*p == ' ' || *p == '\t') { p++; }
    if (*p != '\0') { return AC_PRO_STM_EPARAM; }
    *kind = 0;
    *val = neg ? -v : v;
    return 0;
}

/* ---- 链构建入口(parse → SFF → chain;调用前整块清零视图) ---- */

static int st_prepare(const char *src) {
    int rc;
    memset(&g_ac_pro_scratch.sturm, 0, sizeof(g_ac_pro_scratch.sturm));
    rc = st_build(src);
    if (rc != 0) {
        memset(&g_ac_pro_scratch.sturm, 0, sizeof(g_ac_pro_scratch.sturm));
        return rc;
    }
    return 0;
}

/* ---- V 端点包装:±∞ 走 lc/奇偶符号序列(零求值成本);有限走精确 Horner ---- */

static int st_var_at(int kind, int64_t val) {
    int last = 0;
    int v = 0;
    int k;
    if (kind != 0) {
        /* sign(p_k(+inf)) = sign(lc_k);sign(p_k(-inf)) = (-1)^deg_k·sign(lc_k)
         * ——奇偶规则对每个成员独立生效(只翻 member 0 是实测踩坑) */
        for (k = 0; k < s_pro_st_nmem; k++) {
            ac_bigint_t lc;
            int sg;
            int dkk = s_pro_st_deg(k);
            st_coef_get(k, dkk, &lc);
            sg = lc.sign;
            if (kind < 0 && (dkk % 2) != 0) { sg = -sg; }
            if (last != 0 && sg != last) { v++; }
            last = sg;
        }
        return v;
    }
    return st_variations(val, 0);
}

/* ---- 输出缓冲 ---- */

static int st_puts(char *buf, int cap, int *pos, const char *s) {
    while (*s != '\0') {
        if (*pos >= cap - 1) { return AC_PRO_STM_EOVER; }
        buf[(*pos)++] = *s++;
    }
    buf[*pos] = '\0';
    return 0;
}

static int st_puti64(char *buf, int cap, int *pos, int64_t v) {
    char tmp[24];
    int i = 0;
    int64_t m;
    if (v < 0) {
        if (st_puts(buf, cap, pos, "-") != 0) { return AC_PRO_STM_EOVER; }
        m = -v;   /* 本 TU 的 int64 值域(端点 ≤ 1e6、分子 ≤ 2^59)不含 INT64_MIN */
    } else {
        m = v;
    }
    if (m == 0) { tmp[i++] = '0'; }
    while (m > 0) {
        tmp[i++] = (char)('0' + (int)(m % 10));
        m /= 10;
    }
    while (i > 0) {
        char one[2];
        one[0] = tmp[--i];
        one[1] = '\0';
        if (st_puts(buf, cap, pos, one) != 0) { return AC_PRO_STM_EOVER; }
    }
    return 0;
}

/* num/2^dep 的既约分数渲染(契约 §2.4:母线 1 写整数,否则 num/den) */
static int st_render_rational(int64_t num, int dep, char *buf, int cap) {
    int pos = 0;
    if (dep == 0) {
        return st_puti64(buf, cap, &pos, num);
    }
    if (num < 0) {
        if (st_puts(buf, cap, &pos, "-") != 0) { return AC_PRO_STM_EOVER; }
        num = -num;
    }
    if (st_puti64(buf, cap, &pos, num) != 0) { return AC_PRO_STM_EOVER; }
    if (st_puts(buf, cap, &pos, "/") != 0) { return AC_PRO_STM_EOVER; }
    if (st_puti64(buf, cap, &pos, (int64_t)1 << dep) != 0) { return AC_PRO_STM_EOVER; }
    return 0;
}

/* ---- 对外:计数(契约 §2.3;错误码走返回值,成功值 = 非负根数) ---- */

int ac_pro_sturm_count(const char *src, const char *a, const char *b) {
    int ka;
    int kb;
    int64_t va;
    int64_t vb;
    int rc;
    int n;
    int cmp;

    if (src == 0 || a == 0 || b == 0) { return AC_PRO_STM_EPARAM; }
    rc = st_prepare(src);
    if (rc != 0) { return rc; }
    rc = st_parse_endpoint(a, &ka, &va);
    if (rc != 0) { return rc; }
    rc = st_parse_endpoint(b, &kb, &vb);
    if (rc != 0) { return rc; }
    /* a ≤ b(-inf < 整数 < +inf;同类按值) */
    if (ka == -1) { cmp = (kb == -1) ? 0 : -1; }
    else if (ka == 1) { cmp = (kb == 1) ? 0 : 1; }
    else if (kb == -1) { cmp = 1; }
    else if (kb == 1) { cmp = -1; }
    else { cmp = (va < vb) ? -1 : ((va == vb) ? 0 : 1); }
    if (cmp > 0) { return AC_PRO_STM_EPARAM; }   /* a > b */

    rc = st_var_at(ka, va);
    if (rc < 0) { return rc; }
    n = rc;
    rc = st_var_at(kb, vb);
    if (rc < 0) { return rc; }
    n -= rc;
    if (ka == 0) {
        rc = st_p0_is_zero(va, 0);
        if (rc < 0) { return rc; }
        if (rc == 1) { n += 1; }
    }
    return n;
}

/* ---- 对外:隔离(契约 §2.4;显式栈二分,深度 ≤ 48) ---- */

typedef struct {
    int64_t lo;
    int64_t hi;
    int dep;
    int unres;
} st_cell_t;

int ac_pro_sturm_isolate(const char *src, char *out, int out_len) {
    st_cell_t cells[ST_CELLS];
    int ncells = 0;
    int64_t snap;
    int rc;
    int used = 0;
    int i;
    int j;
    int dg;
    ac_bigint_t lc;

    if (out == 0 || src == 0) { return AC_PRO_STM_EPARAM; }
    out[0] = '\0';
    if (out_len < 2) { return AC_PRO_STM_EOVER; }
    rc = st_prepare(src);
    if (rc != 0) { return rc; }

    dg = s_pro_st_deg(0);
    if (dg == 0) {
        /* 非零常数:无实根(契约 §2.1) */
        if (st_puts(out, out_len, &used, "[]") != 0) { return AC_PRO_STM_EOVER; }
        return AC_PRO_STM_OK;
    }

    /* Cauchy 界 B = 1 + max|c_i|/|c_n|(i < n),上取整到 2 的幂;
     * 1e-3 相对余量吸收 bigint→double 舍入(幂 2 边界不敏感) */
    ac_bigint_t mx_bi;
    st_coef_get(0, dg, &lc);
    if (lc.sign == 0) { return AC_PRO_STM_EINTERNAL; }
    ac_bi_zero(&mx_bi);
    for (i = 0; i < dg; i++) {
        ac_bigint_t ci;
        st_coef_get(0, i, &ci);
        if (ci.sign < 0) { ci.sign = 1; }
        if (ac_bi_cmp(&ci, &mx_bi) > 0) { ac_bi_copy(&mx_bi, &ci); }
    }
    /* 上取整到 2 的幂,全整数精确比较:snap·|c_n| ≥ |c_n| + max|c_i|
     * (double 相对余量在 bnd 恰为 2 的幂时仍会虚涨一档——实测踩坑;
     *  mx 双精度值只作 sanity,真正判据走 bigint) */

    {
        ac_bigint_t cn_pos;
        ac_bigint_t sum;
        ac_bigint_t t1s;
        ac_bigint_t pr;
        ac_bi_copy(&cn_pos, &lc);
        if (cn_pos.sign < 0) { cn_pos.sign = 1; }
        ac_bi_copy(&sum, &cn_pos);
        ac_bi_copy(&t1s, &mx_bi);
        rc = st_add_ck(&sum, &t1s, &sum);
        if (rc != 0) { return rc; }
        snap = 1;
        for (;;) {
            ac_bi_from_int64(&t1s, snap);
            rc = st_mul_ck(&t1s, &cn_pos, &pr);
            if (rc != 0) { return rc; }
            if (ac_bi_cmp(&pr, &sum) >= 0) { break; }
            snap <<= 1;
        }
    }

    /* p(−snap) = 0 → 前置退化区间(半开格 (lo,hi] 不含左端点) */
    rc = st_p0_is_zero(-snap, 0);
    if (rc < 0) { return rc; }
    if (rc == 1) {
        cells[0].lo = -snap;
        cells[0].hi = -snap;
        cells[0].dep = 0;
        cells[0].unres = 0;
        ncells = 1;
    }

    /* 显式栈二分(DFS;待处理格 ≤ 深度 + 1 ≤ 49 ≤ 64) */
    {
        int64_t slo[AC_PRO_ST_BSTACK];
        int64_t shi[AC_PRO_ST_BSTACK];
        int sdep[AC_PRO_ST_BSTACK];
        int sp = 0;
        slo[sp] = -snap;
        shi[sp] = snap;
        sdep[sp] = 0;
        sp++;
        while (sp > 0) {
            int64_t lo;
            int64_t hi;
            int dep;
            int nlo;
            int nhi;
            int n;
            sp--;
            lo = slo[sp];
            hi = shi[sp];
            dep = sdep[sp];
            nlo = st_variations(lo, dep);
            if (nlo < 0) { return nlo; }
            nhi = st_variations(hi, dep);
            if (nhi < 0) { return nhi; }
            n = nlo - nhi;
            if (n == 0) { continue; }
            if (n == 1) {
                if (ncells >= ST_CELLS) { return AC_PRO_STM_EINTERNAL; }
                cells[ncells].lo = lo;
                cells[ncells].hi = hi;
                cells[ncells].dep = dep;
                cells[ncells].unres = 0;
                ncells++;
                continue;
            }
            if (dep >= AC_PRO_ST_BDEPTH_MAX) {
                if (ncells >= ST_CELLS) { return AC_PRO_STM_EINTERNAL; }
                cells[ncells].lo = lo;
                cells[ncells].hi = hi;
                cells[ncells].dep = dep;
                cells[ncells].unres = 1;
                ncells++;
                continue;
            }
            if (sp + 2 > AC_PRO_ST_BSTACK) { return AC_PRO_STM_EINTERNAL; }
            slo[sp] = 2 * lo;
            shi[sp] = lo + hi;
            sdep[sp] = dep + 1;
            sp++;
            slo[sp] = lo + hi;
            shi[sp] = 2 * hi;
            sdep[sp] = dep + 1;
            sp++;
        }
    }

    /* 按 lo 值升序(格两两不交,double 比较可分辨;并列不可能) */
    for (i = 1; i < ncells; i++) {
        st_cell_t key = cells[i];
        double kv = (double)key.lo / (double)((int64_t)1 << key.dep);
        j = i - 1;
        while (j >= 0) {
            double jv = (double)cells[j].lo / (double)((int64_t)1 << cells[j].dep);
            if (jv <= kv) { break; }
            cells[j + 1] = cells[j];
            j--;
        }
        cells[j + 1] = key;
    }

    /* 渲染:既约分数(母线 = 2^dep,约去公因子 2);未解析尾标 "!" */
    if (st_puts(out, out_len, &used, "[") != 0) { return AC_PRO_STM_EOVER; }
    for (i = 0; i < ncells; i++) {
        int64_t lo = cells[i].lo;
        int64_t hi = cells[i].hi;
        int dlo = cells[i].dep;
        int dhi = cells[i].dep;
        char buf[64];
        if (i > 0) {
            if (st_puts(out, out_len, &used, ", ") != 0) { return AC_PRO_STM_EOVER; }
        }
        if (st_puts(out, out_len, &used, "[") != 0) { return AC_PRO_STM_EOVER; }
        while (dlo > 0 && (lo % 2) == 0) { lo /= 2; dlo--; }
        while (dhi > 0 && (hi % 2) == 0) { hi /= 2; dhi--; }
        if (st_render_rational(lo, dlo, buf, (int)sizeof(buf)) != 0) { return AC_PRO_STM_EINTERNAL; }
        if (st_puts(out, out_len, &used, buf) != 0) { return AC_PRO_STM_EOVER; }
        if (st_puts(out, out_len, &used, ", ") != 0) { return AC_PRO_STM_EOVER; }
        if (st_render_rational(hi, dhi, buf, (int)sizeof(buf)) != 0) { return AC_PRO_STM_EINTERNAL; }
        if (st_puts(out, out_len, &used, buf) != 0) { return AC_PRO_STM_EOVER; }
        if (st_puts(out, out_len, &used, "]") != 0) { return AC_PRO_STM_EOVER; }
        if (cells[i].unres) {
            if (st_puts(out, out_len, &used, "!") != 0) { return AC_PRO_STM_EOVER; }
        }
    }
    if (st_puts(out, out_len, &used, "]") != 0) { return AC_PRO_STM_EOVER; }
    return AC_PRO_STM_OK;
}

#endif /* AC_PRO_ACTIVE */
