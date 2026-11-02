/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_pro_rational.c — 单变量有理函数:欧几里得 GCD,cancel 约分,apart 部分分式

   v2.3.0 功能 5:单变量有理函数工具,欧几里得 GCD,cancel 约分,apart
   部分分式(ROADMAP.md:117-119)。多项式四则(加/减/乘 + 伪除 pdivmod)是
   内部基建,直接支撑 GCD 与 cancel,不单设字符串 API:调用方(积分前预处理
   / 式化简)需要的终点是本文件三个入口。

   整系数域契约,为什么没有浮点系数:GCD/cancel 这类算法只在整系数域良定
   (与 A1 的 prem 同纪律,ac_pro_multi.c),浮点系数下「公因式」没有精确
   意义。全程 ac_int_t(int64),任何中间量溢出立即返回 AC_PRO_RAT_ECOEF,
   绝不回绕,绝不降级成浮点。唯一的浮点出现在 apart 的数值端:分母根
   (deg ≤ 4 上游精确根式管线 / deg 5..8 Durand-Kerner)与
   A_i = p(r_i)/q'(r_i),且必须过回乘验证(残差 ≤ 1e-9·尺度)才准出串,
   绝不输出未验证的分解(ROADMAP.md:118「验证不过即报错」)。

   错误码走返回值:一个 static 错误槽就让 TU 净增 32 B .bss
   (ac_pro.h:292 同款论证),直接破坏 42608/42640 门禁。全程零 static
   对象,零 malloc,零 VLA,零递归(Euclid / pdivmod / DK 求根全部显式
   循环)。

   视图纪律:解析与整系数代数阶段持有 rat 槽(464 B,union 吸收);apart
   的数值阶段全部在栈上数组进行(次数 ≤ 8),因为 deg(q) 5..8 时
   ac_pro_numroots 写同 union 的 nr 视图,整块覆写 rat,先拷栈,再求根,
   再回代(ac_pro_scratch.h rat 视图头注同款说明)。deg(q) ≤ 4 时上游
   ac_solvers 用的是上游 g_ac_scratch(物理不重叠),但实现统一走栈拷贝,
   两条路径同构,纪律只有一条。 */

#include <string.h>
#include <stdint.h>
#include <math.h>

#include "ac_pro_config.h"
#include "ac_pro.h"
#include "../../../C/128KB/src/ac_solvers.h"

#if AC_PRO_ACTIVE

/* 槽位约定（见 ac_pro_scratch.h rat 视图头注） */
#define RAT_P    0
#define RAT_Q    1
#define RAT_G    2
#define RAT_W    3
#define RAT_W2   4
#define RAT_W3   5

#define RAT_SRC_MAX     128   /* 单个多项式串长度上限（不含 NUL） */
#define RAT_FMT_MAX      32   /* 单个数值串上限 */

#define RAT_WORK_MAX  ((ac_int_t)1000000000000000000LL)  /* 中间量上限 1e18 */

static ac_int_t *rat_c(int slot) { return s_pro_rat_slot(slot).c; }

static void rat_clear(int slot) {
    int k;
    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { rat_c(slot)[k] = 0; }
    s_pro_rat_deg(slot) = -1;
}

static void rat_copy(int dst, int src) {
    int k;
    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { rat_c(dst)[k] = rat_c(src)[k]; }
    s_pro_rat_deg(dst) = s_pro_rat_deg(src);
}

static void rat_fix_deg(int slot) {
    int d = AC_PRO_RAT_MAX_DEG;
    while (d >= 0 && rat_c(slot)[d] == 0) { d--; }
    s_pro_rat_deg(slot) = d;
}

static int rat_deg(int slot) { return s_pro_rat_deg(slot); }

static int rat_is_zero(int slot) { return rat_deg(slot) < 0; }

static ac_int_t rat_lead(int slot) {
    int d = rat_deg(slot);
    return (d >= 0) ? rat_c(slot)[d] : 0;
}

/* 溢出检查的整系数算术（绝不回绕） */
static ac_int_t rat_iabs(ac_int_t x) { return (x < 0) ? -x : x; }

static int rat_add_ov(ac_int_t x, ac_int_t y, ac_int_t *out) {
    if ((y > 0 && x > RAT_WORK_MAX - y) || (y < 0 && x < -RAT_WORK_MAX - y)) {
        return AC_PRO_RAT_ECOEF;
    }
    *out = x + y;
    return 0;
}

static int rat_mul_ov(ac_int_t x, ac_int_t y, ac_int_t *out) {
    if (x == 0 || y == 0) { *out = 0; return 0; }
    if (rat_iabs(x) > RAT_WORK_MAX / rat_iabs(y)) { return AC_PRO_RAT_ECOEF; }
    *out = x * y;
    return 0;
}

/* 加 / 减 / 乘（次数 ≤ 8、系数溢出即拒绝） */
static int rat_poly_add(int dst, int f, int g) {   /* dst = f + g */
    ac_int_t t[AC_PRO_RAT_MAX_DEG + 1];
    int k;
    int rc;
    int hi = (rat_deg(f) > rat_deg(g)) ? rat_deg(f) : rat_deg(g);
    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { t[k] = 0; }
    for (k = 0; k <= hi; k++) {
        ac_int_t u = (k <= rat_deg(f)) ? rat_c(f)[k] : 0;
        ac_int_t v = (k <= rat_deg(g)) ? rat_c(g)[k] : 0;
        rc = rat_add_ov(u, v, &t[k]);
        if (rc != 0) { return rc; }
    }
    rat_clear(dst);
    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { rat_c(dst)[k] = t[k]; }
    rat_fix_deg(dst);
    return 0;
}

static int rat_poly_sub(int dst, int f, int g) {   /* dst = f - g */
    ac_int_t t[AC_PRO_RAT_MAX_DEG + 1];
    int k;
    int rc;
    int hi = (rat_deg(f) > rat_deg(g)) ? rat_deg(f) : rat_deg(g);
    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { t[k] = 0; }
    for (k = 0; k <= hi; k++) {
        ac_int_t u = (k <= rat_deg(f)) ? rat_c(f)[k] : 0;
        ac_int_t v = (k <= rat_deg(g)) ? rat_c(g)[k] : 0;
        rc = rat_add_ov(u, -v, &t[k]);
        if (rc != 0) { return rc; }
    }
    rat_clear(dst);
    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { rat_c(dst)[k] = t[k]; }
    rat_fix_deg(dst);
    return 0;
}

static int rat_poly_mul(int dst, int f, int g) {   /* dst = f * g */
    ac_int_t t[AC_PRO_RAT_MAX_DEG + 1];
    ac_int_t acc;
    int i;
    int j;
    int rc;
    if (rat_deg(f) < 0 || rat_deg(g) < 0) { rat_clear(dst); return 0; }
    if (rat_deg(f) + rat_deg(g) > AC_PRO_RAT_MAX_DEG) { return AC_PRO_RAT_EDEG; }
    for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { t[i] = 0; }
    for (i = 0; i <= rat_deg(f); i++) {
        if (rat_c(f)[i] == 0) { continue; }
        for (j = 0; j <= rat_deg(g) && i + j <= AC_PRO_RAT_MAX_DEG; j++) {
            if (rat_c(g)[j] == 0) { continue; }
            rc = rat_mul_ov(rat_c(f)[i], rat_c(g)[j], &acc);
            if (rc != 0) { return rc; }
            rc = rat_add_ov(t[i + j], acc, &t[i + j]);
            if (rc != 0) { return rc; }
        }
    }
    rat_clear(dst);
    for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { rat_c(dst)[i] = t[i]; }
    rat_fix_deg(dst);
    return 0;
}

/* int64 欧几里得 gcd（正值） */
static ac_int_t rat_igcd(ac_int_t a, ac_int_t b) {
    ac_int_t t;
    while (b != 0) {
        t = a % b;
        a = b;
        b = t;
    }
    return (a < 0) ? -a : a;
}

/* 本原化：去整数容差 + 首项化正（dst 可 = src） */
static void rat_primitive(int slot) {
    ac_int_t g = 0;
    int k;
    int neg;
    if (rat_is_zero(slot)) { return; }
    for (k = 0; k <= rat_deg(slot); k++) {
        if (rat_c(slot)[k] != 0) {
            g = (g == 0) ? rat_iabs(rat_c(slot)[k]) : rat_igcd(g, rat_c(slot)[k]);
        }
    }
    if (g == 0) { g = 1; }
    neg = (rat_lead(slot) < 0);
    for (k = 0; k <= rat_deg(slot); k++) {
        ac_int_t v = rat_c(slot)[k] / g;   /* 容差整除，精确 */
        rat_c(slot)[k] = neg ? -v : v;
    }
}

/* 伪除：f = q·g·s + r（s = lc(g)^m 标量），商/余都落栈上数组
 * 整系数域的标准做法（A1 prem 同纪律）：每步 W ← lc·W − lfw·x^k·g，
 * 商同步 q ← lc·q + lfw·x^k；恒等式 f·lc^m = q·g + r。r = 0 时
 * q / lc^m = f / g（Gauss 引理：本原 g 整除 f ⇒ 商在 Z[x]）。 */
static int rat_pdivmod(const ac_int_t *f, const ac_int_t *g, int df, int dg,
                       ac_int_t *qout, ac_int_t *rout, ac_int_t *scale) {
    ac_int_t w[AC_PRO_RAT_MAX_DEG + 1];
    ac_int_t q[AC_PRO_RAT_MAX_DEG + 1];
    ac_int_t lcg;
    ac_int_t lcp;
    ac_int_t t;
    int i;
    int k;
    int steps = 0;
    int rc;

    *scale = 1;
    for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { q[i] = 0; w[i] = 0; }
    if (dg < 0) { return AC_PRO_RAT_EDOM; }   /* 零除数：调用方契约防线 */
    if (df < 0) {                       /* f = 0：商余皆零 */
        for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { qout[i] = 0; rout[i] = 0; }
        return 0;
    }
    for (i = 0; i <= df; i++) { w[i] = f[i]; }   /* w 全域先清零：rout 整组拷贝 */
    lcg = g[dg];

    while (df >= dg && df >= 0) {
        ac_int_t lfw = w[df];
        k = df - dg;
        for (i = df; i >= 0; i--) {
            rc = rat_mul_ov(lcg, w[i], &t);
            if (rc != 0) { return rc; }
            w[i] = t;
            if (i >= k && i - k <= dg) {
                rc = rat_mul_ov(lfw, g[i - k], &t);
                if (rc != 0) { return rc; }
                rc = rat_add_ov(w[i], -t, &w[i]);
                if (rc != 0) { return rc; }
            }
        }
        /* 商同步：q ← lc·q + lfw·x^k */
        for (i = AC_PRO_RAT_MAX_DEG; i >= 0; i--) {
            rc = rat_mul_ov(q[i], lcg, &q[i]);
            if (rc != 0) { return rc; }
        }
        rc = rat_add_ov(q[k], lfw, &q[k]);
        if (rc != 0) { return rc; }
        steps++;
        while (df >= 0 && w[df] == 0) { df--; }
    }
    for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { rout[i] = w[i]; }
    for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { qout[i] = q[i]; }
    lcp = 1;
    for (i = 0; i < steps; i++) {
        rc = rat_mul_ov(lcp, lcg, &lcp);
        if (rc != 0) { return rc; }
    }
    *scale = lcp;
    return 0;
}

/* 解析：项 = [符号][系数][*]x[^n]，gb 解析器同风格
 * 同类项重复 → EPARSE（与 gb 对同变量重复报错的先例一致）；
 * 空串 / 非法字符 / 括号 / 分数 → EPARSE；|c| > 9999 → ECOEF；
 * 次数 > 8 → EDEG。返回 0 = 成功。 */
static int rat_parse(const char *s, int slot) {
    long long coef[AC_PRO_RAT_MAX_DEG + 1];
    int seen[AC_PRO_RAT_MAX_DEG + 1];
    const char *p = s;
    int k;
    int any = 0;

    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { coef[k] = 0; seen[k] = 0; }

    while (*p == ' ' || *p == '\t') { p++; }
    if (*p == '\0') { return AC_PRO_RAT_EPARSE; }

    for (;;) {
        int neg = 0;
        long long c = 0;
        int saw_coef = 0;
        int saw_x = 0;
        int e = 1;
        while (*p == ' ' || *p == '\t') { p++; }
        if (*p == '-') { neg = 1; p++; }
        else if (*p == '+') { p++; }
        while (*p == ' ' || *p == '\t') { p++; }

        while (*p >= '0' && *p <= '9') {
            c = c * 10 + (*p - '0');
            if (c > AC_PRO_RAT_COEF_MAX) { return AC_PRO_RAT_ECOEF; }
            saw_coef = 1;
            p++;
        }
        while (*p == ' ' || *p == '\t') { p++; }
        if (*p == '*') {
            p++;
            while (*p == ' ' || *p == '\t') { p++; }
        }
        if (*p == 'x') {
            p++;
            saw_x = 1;
            while (*p == ' ' || *p == '\t') { p++; }
            if (*p == '^') {
                p++;
                while (*p == ' ' || *p == '\t') { p++; }
                if (!(*p >= '0' && *p <= '9')) { return AC_PRO_RAT_EPARSE; }
                e = 0;
                while (*p >= '0' && *p <= '9') {
                    e = e * 10 + (*p - '0');
                    if (e > AC_PRO_RAT_MAX_DEG) { return AC_PRO_RAT_EDEG; }
                    p++;
                }
                if (e == 0) { return AC_PRO_RAT_EPARSE; }   /* x^0 写成系数 */
            }
        } else if (!saw_coef) {
            return AC_PRO_RAT_EPARSE;
        }
        if (saw_x && saw_coef) {
            c = neg ? -c : c;
        } else if (saw_x) {
            c = neg ? -1 : 1;
        } else {
            c = neg ? -c : c;           /* 纯常数项 */
            e = 0;                      /* 常数落 x^0,e 的初值 1 只属于省略 ^ 的 x 项 */
        }
        if (seen[e]) { return AC_PRO_RAT_EPARSE; }
        seen[e] = 1;
        coef[e] = c;
        any = 1;

        while (*p == ' ' || *p == '\t') { p++; }
        if (*p == '\0') { break; }
        if (*p != '+' && *p != '-') { return AC_PRO_RAT_EPARSE; }
    }
    if (!any) { return AC_PRO_RAT_EPARSE; }

    rat_clear(slot);
    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) {
        rat_c(slot)[k] = (ac_int_t)coef[k];
    }
    rat_fix_deg(slot);
    return 0;
}

/* "p/q" 拆分：恰一个顶层 '/'，两侧非空 */
static int rat_split(const char *f, char *left, char *right) {
    const char *slash = strchr(f, '/');
    size_t ln;
    if (slash == 0) { return AC_PRO_RAT_EPARSE; }
    ln = (size_t)(slash - f);
    if (ln == 0 || ln > RAT_SRC_MAX) { return AC_PRO_RAT_EPARSE; }
    if (strlen(slash + 1) == 0 || strlen(slash + 1) > RAT_SRC_MAX) {
        return AC_PRO_RAT_EPARSE;
    }
    if (strchr(slash + 1, '/') != 0) { return AC_PRO_RAT_EPARSE; }
    memcpy(left, f, ln);
    left[ln] = '\0';
    strcpy(right, slash + 1);
    return 0;
}

/* 输出缓冲追加 */
static int rat_puts(char *buf, int cap, int *pos, const char *s) {
    while (*s != '\0') {
        if (*pos >= cap - 1) { return AC_PRO_RAT_EOVER; }
        buf[(*pos)++] = *s++;
    }
    buf[*pos] = '\0';
    return 0;
}

static int rat_puti(char *buf, int cap, int *pos, long long v) {
    char tmp[24];
    int i = 0;
    if (v < 0) {
        if (rat_puts(buf, cap, pos, "-") != 0) { return AC_PRO_RAT_EOVER; }
        v = -v;
    }
    if (v == 0) { tmp[i++] = '0'; }
    while (v > 0) {
        tmp[i++] = (char)('0' + (int)(v % 10));
        v /= 10;
    }
    while (i > 0) {
        char one[2];
        one[0] = tmp[--i];
        one[1] = '\0';
        if (rat_puts(buf, cap, pos, one) != 0) { return AC_PRO_RAT_EOVER; }
    }
    return 0;
}

/* 多项式降幂输出："3*x^2-x-1"，零多项式 → "0" */
static int rat_emit_poly(int slot, char *buf, int cap, int *pos) {
    int d;
    int first = 1;
    if (rat_is_zero(slot)) { return rat_puts(buf, cap, pos, "0"); }
    for (d = rat_deg(slot); d >= 0; d--) {
        ac_int_t c = rat_c(slot)[d];
        ac_int_t mag;
        if (c == 0) { continue; }
        mag = rat_iabs(c);
        if (first) {
            if (c < 0) {
                if (rat_puts(buf, cap, pos, "-") != 0) { return AC_PRO_RAT_EOVER; }
            }
            first = 0;
        } else {
            if (rat_puts(buf, cap, pos, (c < 0) ? "-" : "+") != 0) {
                return AC_PRO_RAT_EOVER;
            }
        }
        if (mag != 1 || d == 0) {
            if (rat_puti(buf, cap, pos, (long long)mag) != 0) {
                return AC_PRO_RAT_EOVER;
            }
        }
        if (d >= 1) {
            /* 乘号只属于系数：3*x 而系数 ±1 时省（x、-x，绝不 *x） */
            if (mag != 1) {
                if (rat_puts(buf, cap, pos, "*") != 0) { return AC_PRO_RAT_EOVER; }
            }
            if (rat_puts(buf, cap, pos, "x") != 0) { return AC_PRO_RAT_EOVER; }
        }
        if (d >= 2) {
            if (rat_puts(buf, cap, pos, "^") != 0) { return AC_PRO_RAT_EOVER; }
            if (rat_puti(buf, cap, pos, d) != 0) { return AC_PRO_RAT_EOVER; }
        }
    }
    return 0;
}

/* 数值三级 fmt（与 docs/contracts/ode-format-v23.md §5.1 同算法）
 * 整判 1e-9 → 整串；有理重建 q ≤ 1000 → "p/q"（分数间距 ≥ 1e-6 保证
 * 1e-9 邻域互不相交，重建对求根噪声不变）；否则定点 12 位（半连舍远离
 * 零，llround 语义），去尾零。根域 |x| ≤ 1+9999 保证 x·1e12 < 2^53。 */
static int rat_fmt(double x, char *out) {
    double r = (x < 0) ? -x : x;
    if (r < 1e12) {
        double nr = (x < 0) ? -(double)((long long)(-x + 0.5))
                            : (double)((long long)(x + 0.5));
        if ((x - nr) < 1e-9 && (nr - x) < 1e-9) {
            long long v = (long long)nr;
            int pos = 0;
            char tmp[RAT_FMT_MAX];
            tmp[0] = '\0';
            if (rat_puti(tmp, RAT_FMT_MAX, &pos, v) != 0) {
                return AC_PRO_RAT_EOVER;
            }
            strcpy(out, tmp);
            return 0;
        }
    }
    {
        int q;
        long long best_p = 0;
        int best_q = 0;
        double best_err = 1e300;
        for (q = 1; q <= 1000; q++) {
            long long p = (long long)((x < 0) ? (x * (double)q - 0.5)
                                              : (x * (double)q + 0.5));
            double err;
            long long g;
            long long pp = p;
            int qq = q;
            if (pp < 0) { g = rat_igcd(-pp, (ac_int_t)qq); }
            else { g = rat_igcd(pp, (ac_int_t)qq); }
            if (g > 1) { pp /= g; qq = (int)((ac_int_t)qq / g); }
            err = x - (double)pp / (double)qq;
            if (err < 0) { err = -err; }
            if (err <= 1e-9 && (err < best_err ||
                                (err == best_err && qq < best_q))) {
                best_err = err;
                best_p = pp;
                best_q = qq;
            }
        }
        if (best_q > 0) {
            int pos = 0;
            char tmp[RAT_FMT_MAX];
            tmp[0] = '\0';
            if (rat_puti(tmp, RAT_FMT_MAX, &pos, best_p) != 0) {
                return AC_PRO_RAT_EOVER;
            }
            if (best_q != 1) {
                char one[2] = { '/', '\0' };
                char qbuf[RAT_FMT_MAX];
                int qpos = 0;
                if (rat_puts(tmp, RAT_FMT_MAX, &pos, one) != 0) {
                    return AC_PRO_RAT_EOVER;
                }
                qbuf[0] = '\0';
                if (rat_puti(qbuf, RAT_FMT_MAX, &qpos, best_q) != 0) {
                    return AC_PRO_RAT_EOVER;
                }
                if (rat_puts(tmp, RAT_FMT_MAX, &pos, qbuf) != 0) {
                    return AC_PRO_RAT_EOVER;
                }
            }
            strcpy(out, tmp);
            return 0;
        }
    }
    {
        double ax = (x < 0) ? -x : x;
        long long s = (long long)(ax * 1e12 + 0.5);   /* 域内 |x|<1e4，正数四舍五入 */
        long long ip = s / 1000000000000LL;
        long long fp = s % 1000000000000LL;
        char tmp[RAT_FMT_MAX];
        int pos = 0;
        char fpbuf[16];
        int digits = 12;
        int trailing = 0;
        int i;
        if (x < 0) {
            if (rat_puts(tmp, RAT_FMT_MAX, &pos, "-") != 0) {
                return AC_PRO_RAT_EOVER;
            }
        }
        if (rat_puti(tmp, RAT_FMT_MAX, &pos, ip) != 0) {
            return AC_PRO_RAT_EOVER;
        }
        for (i = 0; i < 12; i++) {
            fpbuf[11 - i] = (char)('0' + (int)(fp % 10));
            fp /= 10;
        }
        fpbuf[12] = '\0';
        while (digits > 0 && fpbuf[digits - 1] == '0') { digits--; }
        trailing = digits;
        if (trailing > 0) {
            char dot[2] = { '.', '\0' };
            char one[2];
            if (rat_puts(tmp, RAT_FMT_MAX, &pos, dot) != 0) {
                return AC_PRO_RAT_EOVER;
            }
            for (i = 0; i < trailing; i++) {
                one[0] = fpbuf[i];
                one[1] = '\0';
                if (rat_puts(tmp, RAT_FMT_MAX, &pos, one) != 0) {
                    return AC_PRO_RAT_EOVER;
                }
            }
        }
        strcpy(out, tmp);
        return 0;
    }
}

/* 数值求值（Horner，升序系数） */
static double rat_eval_d(const double *c, int d, double x) {
    double acc = 0.0;
    int k;
    for (k = d; k >= 0; k--) {
        acc = acc * x + c[k];
    }
    return acc;
}

/* ac_pro_rat_gcd */
int ac_pro_rat_gcd(const char *f, const char *g, char *out, int out_len) {
    ac_int_t q[AC_PRO_RAT_MAX_DEG + 1];
    ac_int_t r[AC_PRO_RAT_MAX_DEG + 1];
    ac_int_t scale;
    int pos = 0;
    int rc;
    int k;

    if (out == 0 || out_len <= 0) { return AC_PRO_RAT_EOVER; }
    out[0] = '\0';
    if (f == 0 || g == 0) { return AC_PRO_RAT_EPARSE; }

    rc = rat_parse(f, RAT_P);
    if (rc != 0) { return rc; }
    rc = rat_parse(g, RAT_Q);
    if (rc != 0) { return rc; }
    rat_primitive(RAT_P);
    rat_primitive(RAT_Q);

    if (rat_is_zero(RAT_P) && rat_is_zero(RAT_Q)) {
        return AC_PRO_RAT_EDOM;   /* gcd(0,0) 未定 */
    }
    /* 欧几里得主循环：(f, g) ← (g, primitive(prem(f, g)))，deg 严格下降 */
    while (!rat_is_zero(RAT_Q)) {
        rc = rat_pdivmod(rat_c(RAT_P), rat_c(RAT_Q), rat_deg(RAT_P),
                         rat_deg(RAT_Q), q, r, &scale);
        if (rc != 0) { return rc; }
        rat_copy(RAT_P, RAT_Q);
        rat_clear(RAT_Q);
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { rat_c(RAT_Q)[k] = r[k]; }
        rat_fix_deg(RAT_Q);
        rat_primitive(RAT_Q);
    }
    rat_copy(RAT_G, RAT_P);             /* gcd = 最后的非零 f（已本原） */
    rat_primitive(RAT_G);

    rc = rat_emit_poly(RAT_G, out, out_len, &pos);
    if (rc != 0) { return rc; }
    return AC_PRO_RAT_OK;
}

/* ac_pro_rat_cancel */
int ac_pro_rat_cancel(const char *f, char *out, int out_len) {
    char left[RAT_SRC_MAX + 2];
    char right[RAT_SRC_MAX + 2];
    ac_int_t q[AC_PRO_RAT_MAX_DEG + 1];
    ac_int_t r[AC_PRO_RAT_MAX_DEG + 1];
    ac_int_t scale;
    ac_int_t cg;
    int pos = 0;
    int rc;

    if (out == 0 || out_len <= 0) { return AC_PRO_RAT_EOVER; }
    out[0] = '\0';
    if (f == 0) { return AC_PRO_RAT_EPARSE; }

    rc = rat_split(f, left, right);
    if (rc != 0) { return rc; }
    rc = rat_parse(left, RAT_P);
    if (rc != 0) { return rc; }
    rc = rat_parse(right, RAT_Q);
    if (rc != 0) { return rc; }

    if (rat_is_zero(RAT_Q)) { return AC_PRO_RAT_EDOM; }   /* 分母为零 */
    if (rat_is_zero(RAT_P)) {                             /* 0/q → "0" */
        rc = rat_puts(out, out_len, &pos, "0");
        if (rc != 0) { return rc; }
        return AC_PRO_RAT_OK;
    }

    rat_copy(RAT_G, RAT_P);
    rat_copy(RAT_W, RAT_Q);
    rat_primitive(RAT_G);
    rat_primitive(RAT_W);
    /* Euclid 求 g = gcd(p, q)（本原），(G, W) ← (W, primitive(prem)) */
    while (!rat_is_zero(RAT_W)) {
        rc = rat_pdivmod(rat_c(RAT_G), rat_c(RAT_W), rat_deg(RAT_G),
                         rat_deg(RAT_W), q, r, &scale);
        if (rc != 0) { return rc; }
        rat_copy(RAT_G, RAT_W);
        rat_clear(RAT_W);
        {
            int k;
            for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { rat_c(RAT_W)[k] = r[k]; }
            rat_fix_deg(RAT_W);
        }
        rat_primitive(RAT_W);
    }
    rat_primitive(RAT_G);

    /* p' = p/g：伪除 + 除以 lc^m（Gauss：本原 g 整除 p ⇒ 商在 Z[x]） */
    rc = rat_pdivmod(rat_c(RAT_P), rat_c(RAT_G), rat_deg(RAT_P),
                     rat_deg(RAT_G), q, r, &scale);
    if (rc != 0) { return rc; }
    {
        int k;
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) {
            if (r[k] != 0) { return AC_PRO_RAT_EDOM; }   /* 不整除：内部错误防线 */
        }
    }
    rat_clear(RAT_W);
    {
        int k;
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) {
            if (scale == 1) { rat_c(RAT_W)[k] = q[k]; }
            else if (q[k] % scale == 0) { rat_c(RAT_W)[k] = q[k] / scale; }
            else { return AC_PRO_RAT_ECOEF; }
        }
        rat_fix_deg(RAT_W);
    }
    /* q' = q/g */
    rc = rat_pdivmod(rat_c(RAT_Q), rat_c(RAT_G), rat_deg(RAT_Q),
                     rat_deg(RAT_G), q, r, &scale);
    if (rc != 0) { return rc; }
    rat_clear(RAT_W2);
    {
        int k;
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) {
            if (r[k] != 0) { return AC_PRO_RAT_EDOM; }
        }
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) {
            if (scale == 1) { rat_c(RAT_W2)[k] = q[k]; }
            else if (q[k] % scale == 0) { rat_c(RAT_W2)[k] = q[k] / scale; }
            else { return AC_PRO_RAT_ECOEF; }
        }
        rat_fix_deg(RAT_W2);
    }
    /* 整数容差约尽 + 分母首项化正 */
    cg = 0;
    {
        int k;
        for (k = 0; k <= rat_deg(RAT_W); k++) {
            if (rat_c(RAT_W)[k] != 0) {
                cg = (cg == 0) ? rat_iabs(rat_c(RAT_W)[k])
                               : rat_igcd(cg, rat_c(RAT_W)[k]);
            }
        }
        for (k = 0; k <= rat_deg(RAT_W2); k++) {
            if (rat_c(RAT_W2)[k] != 0) {
                cg = (cg == 0) ? rat_iabs(rat_c(RAT_W2)[k])
                               : rat_igcd(cg, rat_c(RAT_W2)[k]);
            }
        }
    }
    if (cg > 1) {
        int k;
        for (k = 0; k <= rat_deg(RAT_W); k++) { rat_c(RAT_W)[k] /= cg; }
        for (k = 0; k <= rat_deg(RAT_W2); k++) { rat_c(RAT_W2)[k] /= cg; }
    }
    if (rat_lead(RAT_W2) < 0) {
        int k;
        for (k = 0; k <= rat_deg(RAT_W); k++) { rat_c(RAT_W)[k] = -rat_c(RAT_W)[k]; }
        for (k = 0; k <= rat_deg(RAT_W2); k++) { rat_c(RAT_W2)[k] = -rat_c(RAT_W2)[k]; }
        rat_fix_deg(RAT_W);
        rat_fix_deg(RAT_W2);
    }

    if (rat_deg(RAT_W2) == 0 && rat_c(RAT_W2)[0] == 1) {
        rc = rat_emit_poly(RAT_W, out, out_len, &pos);   /* 分母 = 1 → 只出分子 */
    } else {
        rc = rat_puts(out, out_len, &pos, "(");
        if (rc == 0) { rc = rat_emit_poly(RAT_W, out, out_len, &pos); }
        if (rc == 0) { rc = rat_puts(out, out_len, &pos, ")/("); }
        if (rc == 0) { rc = rat_emit_poly(RAT_W2, out, out_len, &pos); }
        if (rc == 0) { rc = rat_puts(out, out_len, &pos, ")"); }
    }
    if (rc != 0) { return rc; }
    return AC_PRO_RAT_OK;
}

/* ac_pro_rat_apart */
int ac_pro_rat_apart(const char *f, char *out, int out_len) {
    char left[RAT_SRC_MAX + 2];
    char right[RAT_SRC_MAX + 2];
    ac_int_t pc[AC_PRO_RAT_MAX_DEG + 1];
    ac_int_t qc[AC_PRO_RAT_MAX_DEG + 1];
    double qdc[AC_PRO_RAT_MAX_DEG + 1];
    double pdc[AC_PRO_RAT_MAX_DEG + 1];
    double rre[AC_PRO_RAT_MAX_DEG];
    double rim[AC_PRO_RAT_MAX_DEG];
    double ai[AC_PRO_RAT_MAX_DEG];
    double qder[AC_PRO_RAT_MAX_DEG + 1];
    int order[AC_PRO_RAT_MAX_DEG];
    int dp;
    int dq;
    int n = 0;
    int i;
    int j;
    int k;
    int pos = 0;
    int rc;
    double rmax = 0.0;
    char num[RAT_FMT_MAX];
    char den[RAT_FMT_MAX];

    if (out == 0 || out_len <= 0) { return AC_PRO_RAT_EOVER; }
    out[0] = '\0';
    if (f == 0) { return AC_PRO_RAT_EPARSE; }

    rc = rat_split(f, left, right);
    if (rc != 0) { return rc; }
    rc = rat_parse(left, RAT_P);
    if (rc != 0) { return rc; }
    rc = rat_parse(right, RAT_Q);
    if (rc != 0) { return rc; }

    dq = rat_deg(RAT_Q);
    dp = rat_deg(RAT_P);
    if (dq < 1) { return AC_PRO_RAT_EDEG; }       /* 分母必须是真多项式 */
    if (dp >= dq) { return AC_PRO_RAT_EDOM; }     /* 假分式：先多项式除法（本版不做） */

    /* 拷栈（此后 rat 视图不再读,deg 5..8 的 DK 写 nr 视图会覆写 union） */
    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) {
        pc[k] = rat_c(RAT_P)[k];
        qc[k] = rat_c(RAT_Q)[k];
        qdc[k] = (double)qc[k];
        pdc[k] = (double)pc[k];
    }
    if (rat_is_zero(RAT_P)) {                     /* 0/q → "0" */
        rc = rat_puts(out, out_len, &pos, "0");
        if (rc != 0) { return rc; }
        return AC_PRO_RAT_OK;
    }

    /* 求根 */
    if (dq == 1) {
        if (qc[1] == 0) { return AC_PRO_RAT_EDOM; }
        rre[0] = -(double)qc[0] / (double)qc[1];
        rim[0] = 0.0;
        n = 1;
    } else if (dq <= 4) {
        for (k = 0; k < 5; k++) { g_coeffs[k] = 0.0; }
        for (k = 0; k <= dq; k++) { g_coeffs[k] = (double)qc[k]; }
        if (dq == 2) { ac_solve_quadratic(); }
        else if (dq == 3) { ac_solve_cubic(0); }
        else { ac_solve_quartic(); }
        if (g_solve_error != 0) { return AC_PRO_RAT_EDOM; }
        n = g_root_count;
        if (n != dq) { return AC_PRO_RAT_EDOM; }
        for (k = 0; k < n; k++) { rre[k] = g_root_re[k]; rim[k] = g_root_im[k]; }
    } else {
        if (ac_pro_numroots(qdc, dq) != 1) { return AC_PRO_RAT_EDOM; }
        n = dq;
        for (k = 0; k < n; k++) { rre[k] = g_root_re[k]; rim[k] = g_root_im[k]; }
    }

    /* 互异实根判据（写死）：|im| ≤ 1e-9 实根；两两 |Δre| < 1e-6 视为重根 */
    for (k = 0; k < n; k++) {
        if (rim[k] > 1e-9 || rim[k] < -1e-9) { return AC_PRO_RAT_EDOM; }
        if (rre[k] > rmax) { rmax = rre[k]; }
    }
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            double d = rre[i] - rre[j];
            if (d < -1e-6 || d > 1e-6) { continue; }
            return AC_PRO_RAT_EDOM;   /* 重根 */
        }
    }

    /* A_i = p(r_i)/q'(r_i) */
    {
        ac_int_t der[AC_PRO_RAT_MAX_DEG + 1];
        for (k = 0; k <= dq; k++) { der[k] = 0; }
        for (k = 1; k <= dq; k++) { der[k - 1] = (ac_int_t)k * qc[k]; }
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) {
            qder[k] = (double)der[k];
        }
    }
    for (i = 0; i < n; i++) {
        double pri = rat_eval_d(pdc, dp, rre[i]);
        double qpi = rat_eval_d(qder, dq - 1, rre[i]);
        if (!(qpi > 1e-12 || qpi < -1e-12)) { return AC_PRO_RAT_EDOM; }
        ai[i] = pri / qpi;
    }

    /* 回乘验证：Σ A_i·Π_{j≠i}(t − r_j) 对拍 p(t)，dq 个采样点 */
    for (k = 0; k < dq; k++) {
        double t = rmax + 1.0 + (double)k;
        double lhs = 0.0;
        double rhs = rat_eval_d(pdc, dp, t);
        double scale;
        double res;
        for (i = 0; i < n; i++) {
            double term = 1.0;
            for (j = 0; j < n; j++) {
                if (j == i) { continue; }
                term *= (t - rre[j]);
            }
            lhs += ai[i] * term;
        }
        scale = 1.0;
        if (rhs > scale) { scale = rhs; }
        if (-rhs > scale) { scale = -rhs; }
        if (lhs > scale) { scale = lhs; }
        if (-lhs > scale) { scale = -lhs; }
        res = lhs - rhs;
        if (res < 0) { res = -res; }
        if (res > 1e-9 * scale) { return AC_PRO_RAT_EDOM; }
    }

    /* 渲染：按 r 降序（显式选择序），项 = A/(x ∓ r)，" + " 连接 */
    for (i = 0; i < n; i++) { order[i] = i; }
    for (i = 0; i < n; i++) {
        int bi = i;
        for (j = i + 1; j < n; j++) {
            if (rre[order[j]] > rre[order[bi]]) { bi = j; }
        }
        j = order[i];
        order[i] = order[bi];
        order[bi] = j;
    }
    for (i = 0; i < n; i++) {
        int idx = order[i];
        if (i > 0) {
            rc = rat_puts(out, out_len, &pos, " + ");
            if (rc != 0) { return rc; }
        }
        rc = rat_fmt(ai[idx], num);
        if (rc != 0) { return rc; }
        if (rre[idx] > 1e-9 || rre[idx] < -1e-9) {
            rc = rat_fmt((rre[idx] > 0) ? rre[idx] : -rre[idx], den);
        } else {
            den[0] = '0';
            den[1] = '\0';
        }
        if (rc != 0) { return rc; }
        rc = rat_puts(out, out_len, &pos, num);
        if (rc != 0) { return rc; }
        rc = rat_puts(out, out_len, &pos, "/(");
        if (rc != 0) { return rc; }
        if (rre[idx] > 1e-9 || rre[idx] < -1e-9) {
            rc = rat_puts(out, out_len, &pos,
                          (rre[idx] > 0) ? "x - " : "x + ");
            if (rc != 0) { return rc; }
            rc = rat_puts(out, out_len, &pos, den);
        } else {
            rc = rat_puts(out, out_len, &pos, "x");
        }
        if (rc != 0) { return rc; }
        rc = rat_puts(out, out_len, &pos, ")");
        if (rc != 0) { return rc; }
    }
    return AC_PRO_RAT_OK;
}

/* ac_pro_rat_arith
 * 多项式四则的 + - *（公开入口；op ∈ '+', '-', '*'）。
 * 和/差次数 ≤ 8；积次数 = deg f + deg g ≤ 8，超限 EDEG。 */
int ac_pro_rat_arith(const char *f, char op, const char *g,
                     char *out, int out_len) {
    int pos = 0;
    int rc;

    if (out == 0 || out_len <= 0) { return AC_PRO_RAT_EOVER; }
    out[0] = '\0';
    if (f == 0 || g == 0) { return AC_PRO_RAT_EPARSE; }

    rc = rat_parse(f, RAT_P);
    if (rc != 0) { return rc; }
    rc = rat_parse(g, RAT_Q);
    if (rc != 0) { return rc; }

    if (op == '+') { rc = rat_poly_add(RAT_W, RAT_P, RAT_Q); }
    else if (op == '-') { rc = rat_poly_sub(RAT_W, RAT_P, RAT_Q); }
    else if (op == '*') { rc = rat_poly_mul(RAT_W, RAT_P, RAT_Q); }
    else { return AC_PRO_RAT_EPARSE; }
    if (rc != 0) { return rc; }

    rc = rat_emit_poly(RAT_W, out, out_len, &pos);
    if (rc != 0) { return rc; }
    return AC_PRO_RAT_OK;
}

#endif /* AC_PRO_ACTIVE */
