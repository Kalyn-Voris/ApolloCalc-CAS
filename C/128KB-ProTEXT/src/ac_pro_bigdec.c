/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_pro_bigdec.c — v2.1.0 方向⑤ 任意精度定点十进制

   上游 ac_bigint_t(基数 1e9,20 limb,180 位十进制)之上的定点十进制 dec
   视图。value = m · 10^(−scale),m ∈ ac_bigint_t(88 B),scale ∈ int32。
   出处:设计说明(dec 视图 5 × {bigint 88 + scale 8} = 480 B,被 Pro union
   的 mv 峰值 2688 B 完全吸收 → .bss 维持 41456 零增长)。

   解析 [+-]?digits[.digits]:前导零不计数,小数尾零按标度剥离,9 位一组压
   limb(最高位组在前);有效位 > 180 → EDIGITS 熔断。加减对齐到 max(scale)
   (尾数位数 + 对齐位数 ≤ 180,否则 EOVER);同号相加有 10^180 进位风险,
   先用 NINES = 10^180−1 预检(ac_bi_cmp(t_a, NINES − t_b) > 0 ⟺ 进位被
   20 limb 截弃);t_a + t_b == 10^180 的精确特例落 (1, s−180),其余 EOVER。
   上游 bi_mag_add 的进位收尾是「静默丢弃」契约(结果溢出未定义),故这道
   预检是正确性必要条件,不是防御性客套。乘法走上游 alias-safe ac_bi_mul,
   容量 = 位数(m_a) + 位数(m_b) ≤ 180。除法是 divmod 余数扩标长除:
   e = 180 − 位数(m_a),N = m_a·10^e 恰好占满 180 位,
   Q/R = ac_bi_divmod(N, m_b),结果 Q·10^(−(e+s_a−s_b));R ≠ 0 →
   INEXACT(2)(截断近似);Q == 0 且 R ≠ 0 → EOVER(连 1 位有效数字都凑
   不出,e 已是容量内最大,Q 随 e 单调不减,故熔断是诚实的)。

   与上游 g_bi_* 草稿契约的关系:只调用上游公开纯函数
   ac_bi_add/sub/mul/divmod/cmp/from_int64/to_string/zero/copy;dec 的值
   全部存放在 Pro dec 视图(g_ac_pro_storage.dec)与栈上 ac_bigint_t 局部
   变量,不读写 g_bi_a/b/tmp/work1/work2,不调用 factorial/fibonacci。上游
   ac_bigint.h:66-70 的草稿契约(work1/work2 跨调用遗留内容)因此物理不可
   达。上游函数内部自用的 s_bi_* / g_bi_work1(ac_bi_sub 的临时)由上游自身
   维护,与 dec 的操作数无别名。

   规范形(所有写槽路径的输出都满足):m == 0 ⟺ scale == 0;scale > 0 时 m
   十进制末位非零;scale < 0 仅由除法产生。dec_canon 用「整 limb 剥离 + 除
   10 单位剥离」实现(≤ 19 次 limb 移位 + ≤ 8 次 divmod,循环有界)。

   零静态:无 malloc,无 VLA,无递归,无 static 对象(错误码走返回值,一个
   static 就是 .bss +32 B,见 ac_pro.h eig 节实测论证)。栈上最多 ~7 个
   ac_bigint_t(616 B)+ 200 B 字符缓冲。所有循环上界均为编译期常量或被容量
   预检截断(≤ 20 次 limb 级循环 / ≤ 180 次字符串扫描),无无界迭代。 */

#include "ac_pro.h"

#if AC_PRO_ACTIVE

/* 小助手（幅值域） */

/* 十进制有效位数（|x|，零 → 0）。top limb 已规范化（非零）。 */
static int dec_digits(const ac_bigint_t *x) {
    uint32_t v;
    int d = 0;
    if (x->sign == 0 || x->top == 0) { return 0; }
    v = x->limbs[x->top - 1];
    while (v > 0u) { d++; v /= 10u; }
    return 9 * (x->top - 1) + d;
}

/* out = x · 10^k（k ≥ 0）。调用方必须先保证位数(x) + k ≤ 180，
 * 否则上游 mul 的 2×LIMBS 防御截断会变成静默错值。9 位一组乘
 * 1e9，余数按 10^r 一次乘，循环 ≤ 20 次。 */
static void dec_shift_up(ac_bigint_t *x, long long k) {
    ac_bigint_t p;
    int i;
    if (k <= 0 || x->sign == 0 || x->top == 0) { return; }
    while (k >= 9) {
        ac_bi_from_int64(&p, (int64_t)1000000000);
        ac_bi_mul(x, &p, x);            /* alias-safe（ac_bigint.h:57） */
        k -= 9;
    }
    if (k > 0) {
        int64_t pv = 1;
        for (i = 0; i < (int)k; i++) { pv *= 10; }
        ac_bi_from_int64(&p, pv);
        ac_bi_mul(x, &p, x);
    }
}

/* 把槽内记录规范化（零的标度归 0、尾数末位非零；scale < 0 不动，见
 * scratch.h 注释）。
 * 整 limb 剥离（limbs[0]==0 ⟺ m%1e9==0，每次 −9 位标度）+ 单位剥离
 * （limbs[0]%10==0 ⟺ m%10==0，divmod 除 10；limb 剥后每轮 ≤ 8 次）。 */
static void dec_canon(int slot) {
    ac_pro_dec_t *d = &s_pro_dec_slot(slot);
    ac_bigint_t *m = &d->m;
    ac_bigint_t q, r, ten;

    if (m->sign == 0 || m->top == 0) {
        d->scale = 0;                   /* 零的标度归 0 */
        d->pad = 0;
        return;
    }
    while (d->scale >= 9 && m->top > 1 && m->limbs[0] == 0u) {
        int i;
        for (i = 0; i < m->top - 1; i++) { m->limbs[i] = m->limbs[i + 1]; }
        m->top--;                       /* 顶 limb 非零性保持 */
        d->scale -= 9;
    }
    if (d->scale > 0) {
        ac_bi_from_int64(&ten, (int64_t)10);
        while (d->scale > 0 && m->limbs[0] % 10u == 0u) {
            ac_bi_divmod(m, &ten, &q, &r);   /* limbs[0]%10==0 ⟹ r==0 */
            ac_bi_copy(m, &q);
            d->scale -= 1;
        }
    }
    d->pad = 0;
}

/* 解析 */

/* 语法 [+-]?digits[.digits]（".5"/"5." 合法；无空白/指数/千分位）。
 * 前导零不计数、小数尾零按标度剥离（"1.50" → m=15, s=1）、整数尾部
 * 零保留在尾数里（"1000" → m=1000, s=0，解析不产生负标度）。
 * 有效位 = 首个非零到最后一个非零（含未剥离的尾零）≤ 180。
 * 成功写入槽（规范形），返回 AC_PRO_DEC_OK；失败返回负错误码。 */
static int dec_parse_into(const char *src, int slot) {
    ac_pro_dec_t *d = &s_pro_dec_slot(slot);
    ac_bigint_t *m = &d->m;
    const char *p;
    int neg = 0;
    long long ndigits = 0;          /* 总数字位（long long：防巨串溢出） */
    long long frac = 0;             /* 小数位 */
    long long fi = -1, li = -1;     /* 首个/最后一个非零数字的全序下标 */
    long long idx, tz, strip, scale, span;
    uint32_t cur = 0u;
    int nd = 0, top = 0, i, seen_dot = 0;
    int first_len, need;

    if (src == (const char *)0) { return AC_PRO_DEC_EPARSE; }

    /* 第 1 遍：语法校验 + 计数 + 非零端点 */
    p = src;
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }
    idx = 0;
    for (;;) {
        if (*p >= '0' && *p <= '9') {
            if (*p != '0') { if (fi < 0) { fi = idx; } li = idx; }
            idx++;
            ndigits++;
            p++;
        } else if (*p == '.' && !seen_dot) {
            seen_dot = 1;
            p++;
            /* 小数段起点：之后连续数字都计入小数位 */
            while (*p >= '0' && *p <= '9') {
                if (*p != '0') { if (fi < 0) { fi = idx; } li = idx; }
                idx++;
                ndigits++;
                frac++;
                p++;
            }
            break;                  /* '.' 之后的小数段读完即结束 */
        } else {
            break;
        }
    }
    if (ndigits == 0) { return AC_PRO_DEC_EPARSE; }   /* ""/"+"/"."/非法 */
    if (*p != '\0') { return AC_PRO_DEC_EPARSE; }     /* 尾随垃圾（含 1e5/空白） */

    if (fi < 0) {                                       /* 全零 → 规范零 */
        ac_bi_zero(m);
        d->scale = 0;
        d->pad = 0;
        return AC_PRO_DEC_OK;
    }

    tz = ndigits - 1 - li;                              /* 尾零个数 */
    strip = (tz < frac) ? tz : frac;                    /* 只能剥小数尾零 */
    scale = frac - strip;                               /* ≥ 0：解析不产生负标度 */
    /* 保留的有效位 = [fi, ndigits−strip)：首非零起、止于被剥尾零之前
     * （"1.50" → "15"、s=1；"1000" → "1000"、s=0,整数尾零保留）。 */
    span = ndigits - strip - fi;
    if (span > (long long)AC_PRO_DEC_MAX_DIGITS) {
        return AC_PRO_DEC_EDIGITS;                      /* 位数上限熔断 */
    }
    if (scale > (long long)2147483647LL) {
        return AC_PRO_DEC_EOVER;                        /* 标度超 int32 */
    }

    /* 第 2 遍：重扫字符串，把 [fi, fi+span) 的数字压 limb
     * 基数 1e9 的 limb 必须右对齐分块：首组 = span%9 位（整除则 9
     * 位），其后每 9 位一组，30 位数字的首组是 3 位（"123"），若按
     * 左对齐 9 位切，整个尾数会被放大 10^6（实测 BD-1 钉住的错误）。 */
    first_len = (int)(span % 9);
    if (first_len == 0) { first_len = 9; }
    need = first_len;
    ac_bi_zero(m);
    p = src;
    if (*p == '+' || *p == '-') { p++; }
    idx = 0;
    seen_dot = 0;
    for (;;) {
        if (*p >= '0' && *p <= '9') {
            if (idx >= fi && idx < fi + span) {
                cur = cur * 10u + (uint32_t)(*p - '0');
                nd++;
                if (nd == need) {       /* 一组凑满：整体左移一格、组落底 */
                    for (i = top; i > 0; i--) { m->limbs[i] = m->limbs[i - 1]; }
                    m->limbs[0] = cur;
                    top++;
                    cur = 0u;
                    nd = 0;
                    need = 9;           /* 首组之后一律 9 位一组 */
                    /* 组数 = ceil(span/9) ≤ 20 ⟹ top ≤ 20，limbs[] 不越界 */
                }
            }
            idx++;
            p++;
        } else if (*p == '.' && !seen_dot) {
            seen_dot = 1;
            p++;
        } else {
            break;
        }
    }
    if (nd > 0) {                           /* 防御：右对齐分组下不可达 */
        for (i = top; i > 0; i--) { m->limbs[i] = m->limbs[i - 1]; }
        m->limbs[0] = cur;
        top++;
    }
    m->top = top;
    m->sign = neg ? -1 : 1;
    d->scale = (int32_t)scale;
    d->pad = 0;
    return AC_PRO_DEC_OK;                   /* 已是规范形：span 起于非零、
                                               止于最后保留位（非零或未剥尾零，
                                               未剥尾零段不会尾随小数零） */
}

/* 渲染 */

/* 槽 → 规范形十进制字符串。长度先行核算（long long），不足即 EBUF，
 * 绝不截断半截结果。失败时 out 不被改写。 */
static int dec_render_slot(int slot, char *out, int out_len) {
    const ac_pro_dec_t *d = &s_pro_dec_slot(slot);
    const ac_bigint_t *m = &d->m;
    ac_bigint_t mag;
    char dig[AC_BIGINT_LIMBS * 9 + 2];      /* 180 位 + 符号余量 + NUL */
    long long L, s, req, k;
    int neg, pos;
    size_t dl, j;

    if (out == (char *)0 || out_len < 2) { return AC_PRO_DEC_EBUF; }
    if (m->sign == 0 || m->top == 0) {
        out[0] = '0';
        out[1] = '\0';
        return AC_PRO_DEC_OK;
    }
    neg = (m->sign < 0);
    ac_bi_copy(&mag, m);
    mag.sign = 1;                           /* 只渲染幅值，符号自管 */
    ac_bi_to_string(&mag, dig, (int)sizeof dig);
    L = (long long)dec_digits(m);           /* strlen(dig) */
    s = (long long)d->scale;

    /* 需求长度：s==0 → L；0<s<L → L+1（'.'）；0<s≤L → 2+s（"0."+补零）；
       s<0 → L−s（尾补零）；各加符号位。 */
    if (s == 0) { req = L; }
    else if (s > 0) { req = (L > s) ? (L + 1) : (s + 2); }
    else { req = L - s; }
    if (neg) { req += 1; }
    if ((long long)out_len < req + 1) { return AC_PRO_DEC_EBUF; }

    dl = (size_t)L;
    pos = 0;
    if (neg) { out[pos++] = '-'; }
    if (s == 0) {
        for (j = 0; j < dl; j++) { out[pos++] = dig[j]; }
    } else if (s > 0 && L > s) {
        for (j = 0; j < dl - (size_t)s; j++) { out[pos++] = dig[j]; }
        out[pos++] = '.';
        for (j = dl - (size_t)s; j < dl; j++) { out[pos++] = dig[j]; }
    } else if (s > 0) {
        out[pos++] = '0';
        out[pos++] = '.';
        for (k = 0; k < s - L; k++) { out[pos++] = '0'; }
        for (j = 0; j < dl; j++) { out[pos++] = dig[j]; }
    } else {
        for (j = 0; j < dl; j++) { out[pos++] = dig[j]; }
        for (k = 0; k < -s; k++) { out[pos++] = '0'; }
    }
    out[pos] = '\0';
    return AC_PRO_DEC_OK;
}

/* 运算体（读槽 0/1，写槽 2） */

/* 对齐：out = 槽 x 的尾数 · 10^(target − scale_x)。容量不足 → EOVER
 * （对齐是精确移位，绝不截断低位）。零对齐免检（0·10^k = 0）。 */
static int dec_align(int x, long long target, ac_bigint_t *out) {
    const ac_pro_dec_t *d = &s_pro_dec_slot(x);
    long long k = target - (long long)d->scale;

    ac_bi_copy(out, &d->m);
    if (out->sign == 0 || out->top == 0) { return AC_PRO_DEC_OK; }
    if (k > (long long)AC_PRO_DEC_MAX_DIGITS - (long long)dec_digits(out)) {
        return AC_PRO_DEC_EOVER;            /* 标度对齐超 180 位容量 */
    }
    dec_shift_up(out, k);
    return AC_PRO_DEC_OK;
}

/* 同号幅值相加的 10^180 进位预检（幅值域！ta/tb 可为负，先取 |·|）：
 * 溢出 ⟺ |ta| + |tb| ≥ 10^180 ⟺ |ta| > NINES − |tb|（NINES = 10^180−1；
 * NINES − |tb| ≥ 0 无溢出，因为 |tb| < 10^180）。
 * |ta| + |tb| == 10^180 ⟺ |ta| == NINES − |tb| + 1,精确特例置
 * *exact_pow10，由调用方落 (±1, s−180)。其余溢出 EOVER。
 * 返回 AC_PRO_DEC_OK（无溢出 / 精确特例）或 AC_PRO_DEC_EOVER。 */
static int dec_add_carry_check(const ac_bigint_t *ta, const ac_bigint_t *tb,
                               int *exact_pow10) {
    ac_bigint_t nines, mat, mbt, room, room1;
    int i;

    *exact_pow10 = 0;
    if (dec_digits(ta) < AC_PRO_DEC_MAX_DIGITS &&
        dec_digits(tb) < AC_PRO_DEC_MAX_DIGITS) {
        return AC_PRO_DEC_OK;               /* 双双 < 180 位：和 < 2e179 < 1e180 */
    }
    ac_bi_zero(&nines);
    for (i = 0; i < AC_BIGINT_LIMBS; i++) { nines.limbs[i] = 999999999u; }
    nines.top = AC_BIGINT_LIMBS;
    nines.sign = 1;

    ac_bi_copy(&mat, ta);
    if (mat.sign < 0) { mat.sign = 1; }     /* |ta| */
    ac_bi_copy(&mbt, tb);
    if (mbt.sign < 0) { mbt.sign = 1; }     /* |tb| */

    ac_bi_sub(&nines, &mbt, &room);         /* NINES − |tb| ≥ 0，无溢出 */
    if (ac_bi_cmp(&mat, &room) > 0) {
        ac_bi_from_int64(&room1, (int64_t)1);
        ac_bi_add(&room, &room1, &room1);   /* room ≤ NINES−1 ⟹ +1 ≤ NINES 安全 */
        if (ac_bi_cmp(&mat, &room1) == 0) {
            *exact_pow10 = 1;               /* |ta| + |tb| == 10^180 精确特例 */
            return AC_PRO_DEC_OK;
        }
        return AC_PRO_DEC_EOVER;            /* 精确和需 181 位有效数字 */
    }
    return AC_PRO_DEC_OK;
}

static int dec_compute_add(int is_sub) {
    const ac_pro_dec_t *a = &s_pro_dec_slot(0);
    const ac_pro_dec_t *b = &s_pro_dec_slot(1);
    ac_pro_dec_t *o = &s_pro_dec_slot(2);
    ac_bigint_t ta, tb, sum;
    long long s;
    int rc, exact_pow10;

    s = (long long)a->scale;
    if ((long long)b->scale > s) { s = (long long)b->scale; }

    rc = dec_align(0, s, &ta);
    if (rc != AC_PRO_DEC_OK) { return rc; }
    rc = dec_align(1, s, &tb);
    if (rc != AC_PRO_DEC_OK) { return rc; }

    if (is_sub) {
        /* a − b = a + (−b)：b 取负后与加法同路径。注意 ta 与 −tb 同号
           时同样是幅值相加，一样有 10^180 进位风险，统一走预检。 */
        if (tb.sign == 1) { tb.sign = -1; }
        else if (tb.sign == -1) { tb.sign = 1; }
    }

    if (ta.sign != 0 && ta.sign == tb.sign) {
        /* 同号：上游幅值加的收尾进位在 20 limb 处静默截弃（结果溢出为
           契约外行为，ac_bigint.c:70-74），必须先预检，正确性必要条件。 */
        rc = dec_add_carry_check(&ta, &tb, &exact_pow10);
        if (rc != AC_PRO_DEC_OK) { return rc; }
        if (exact_pow10) {
            ac_bi_from_int64(&o->m, ta.sign > 0 ? (int64_t)1 : (int64_t)-1);
            o->scale = (int32_t)(s - (long long)AC_PRO_DEC_MAX_DIGITS);
            o->pad = 0;
            dec_canon(2);
            return AC_PRO_DEC_OK;
        }
    }
    ac_bi_add(&ta, &tb, &sum);              /* 预检已过 ⟹ 无截弃进位 */

    ac_bi_copy(&o->m, &sum);
    o->scale = (int32_t)s;                  /* s = max(两个 int32) ⟹ 必在域内 */
    o->pad = 0;
    dec_canon(2);
    return AC_PRO_DEC_OK;
}

static int dec_compute_mul(void) {
    const ac_pro_dec_t *a = &s_pro_dec_slot(0);
    const ac_pro_dec_t *b = &s_pro_dec_slot(1);
    ac_pro_dec_t *o = &s_pro_dec_slot(2);
    long long sc;
    int da, db;

    if (a->m.sign == 0 || b->m.sign == 0) {
        ac_bi_zero(&o->m);
        o->scale = 0;
        o->pad = 0;
        return AC_PRO_DEC_OK;
    }
    da = dec_digits(&a->m);
    db = dec_digits(&b->m);
    if (da + db > AC_PRO_DEC_MAX_DIGITS) {
        return AC_PRO_DEC_EOVER;            /* 乘积尾数需 > 180 位：熔断 */
    }
    sc = (long long)a->scale + (long long)b->scale;
    if (sc > (long long)2147483647LL || sc < (long long)(-2147483647LL - 1)) {
        return AC_PRO_DEC_EOVER;            /* 标度超 ±2^31 */
    }
    ac_bi_mul(&a->m, &b->m, &o->m);         /* alias-safe（ac_bigint.h:57） */
    o->scale = (int32_t)sc;
    o->pad = 0;
    dec_canon(2);
    return AC_PRO_DEC_OK;
}

static int dec_compute_div(void) {
    const ac_pro_dec_t *a = &s_pro_dec_slot(0);
    const ac_pro_dec_t *b = &s_pro_dec_slot(1);
    ac_pro_dec_t *o = &s_pro_dec_slot(2);
    ac_bigint_t n, q, r;
    long long e, sc;

    if (b->m.sign == 0 || b->m.top == 0) { return AC_PRO_DEC_EDIV0; }
    if (a->m.sign == 0 || a->m.top == 0) {
        ac_bi_zero(&o->m);
        o->scale = 0;
        o->pad = 0;
        return AC_PRO_DEC_OK;               /* 0 / b = 0，精确 */
    }

    /* 余数扩标：N = |m_a|·10^e 恰好占满 180 位（e ≥ 0），Q = ⌊N/|m_b|⌋。 */
    e = (long long)AC_PRO_DEC_MAX_DIGITS - (long long)dec_digits(&a->m);
    ac_bi_copy(&n, &a->m);
    n.sign = 1;                             /* 幅值域做除法 */
    dec_shift_up(&n, e);                    /* 位数 + e == 180 ⟹ 无截断 */
    ac_bi_divmod(&n, &b->m, &q, &r);        /* alias-safe（ac_bigint.h:58-60） */

    sc = e + (long long)a->scale - (long long)b->scale;
    if (sc > (long long)2147483647LL || sc < (long long)(-2147483647LL - 1)) {
        return AC_PRO_DEC_EOVER;
    }
    if (q.sign == 0 && r.top > 0) {
        return AC_PRO_DEC_EOVER;            /* 位数上限熔断：e 已是容量内最大
                                               （Q 随 e 单调不减），最大扩标下
                                               商仍为 0 ⟹ 连 1 位有效数字都
                                               凑不出，返回近似零即是错值 */
    }

    ac_bi_copy(&o->m, &q);
    /* divmod 里 n 已强制正号 ⟹ q.sign = sign(m_b)；期望符号 = sa·sb。 */
    if (o->m.top > 0) { o->m.sign = a->m.sign * q.sign; }
    o->scale = (int32_t)sc;
    o->pad = 0;
    dec_canon(2);
    /* 余数非零 ⟺ 结果在 sc 位标度处截断（真商还有更多位）→ INEXACT。 */
    return (r.top > 0) ? AC_PRO_DEC_INEXACT : AC_PRO_DEC_OK;
}

/* 公共驱动 */

/* 解析 a→槽 0、b→槽 1，运算写槽 2，渲染到 out。任一步失败即短路
 * 返回错误码；除法可能合法返回 INEXACT(2)。 */
static int dec_binary(const char *a, const char *b, char *out, int out_len,
                      int is_sub, int is_mul, int is_div) {
    int rc, rr;

    if (out == (char *)0 || out_len < 2) { return AC_PRO_DEC_EBUF; }
    rc = dec_parse_into(a, 0);
    if (rc != AC_PRO_DEC_OK) { return rc; }
    rc = dec_parse_into(b, 1);
    if (rc != AC_PRO_DEC_OK) { return rc; }

    if (is_div) {
        rc = dec_compute_div();
    } else if (is_mul) {
        rc = dec_compute_mul();
    } else {
        rc = dec_compute_add(is_sub);
    }
    if (rc != AC_PRO_DEC_OK && rc != AC_PRO_DEC_INEXACT) { return rc; }

    rr = dec_render_slot(2, out, out_len);
    return (rr != AC_PRO_DEC_OK) ? rr : rc;
}

/* 公开 API（ac_pro.h 声明） */

int ac_pro_dec_parse(const char *src, int slot) {
    if (slot < 0 || slot >= AC_PRO_DEC_SLOTS) { return AC_PRO_DEC_ESLOT; }
    return dec_parse_into(src, slot);
}

int ac_pro_dec_render(int slot, char *out, int out_len) {
    if (slot < 0 || slot >= AC_PRO_DEC_SLOTS) { return AC_PRO_DEC_ESLOT; }
    return dec_render_slot(slot, out, out_len);
}

int ac_pro_dec_add(const char *a, const char *b, char *out, int out_len) {
    return dec_binary(a, b, out, out_len, 0, 0, 0);
}

int ac_pro_dec_sub(const char *a, const char *b, char *out, int out_len) {
    return dec_binary(a, b, out, out_len, 1, 0, 0);
}

int ac_pro_dec_mul(const char *a, const char *b, char *out, int out_len) {
    return dec_binary(a, b, out, out_len, 0, 1, 0);
}

int ac_pro_dec_div(const char *a, const char *b, char *out, int out_len) {
    return dec_binary(a, b, out, out_len, 0, 0, 1);
}

#endif /* AC_PRO_ACTIVE */
