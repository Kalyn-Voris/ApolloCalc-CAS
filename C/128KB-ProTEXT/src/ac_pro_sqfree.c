/* ApolloCalc · ac_pro_sqfree.c — 单变量多项式 square-free 分解 + 重根重数(字符串进、字符串出)

   v2.4.0 功能 9(C 侧):单变量多项式 square-free 分解与重根重数,字符串进、
   字符串出。行为边界由 docs/contracts/sqfree-format-v24.md 逐字冻结(本头注
   是该契约的镜像;任何变更须先改契约再改码,ROADMAP.md:178)。

   管线(契约 §3-§6):

   解析(契约 §3,自备从严文法):项 = [符号][系数][*][x[^n]],整数字面量、
   |c| ≤ 999、次数 1..8(n ≥ 9 词法层当场 EDEG),'^' 与次数紧连,
   '='/'/'/'.' 一律 EPARSE(非整系数在文法层拒),同次项重复 EPARSE;
   长度 ≤ 128 B。

   SFF(契约 §4,整系数域循环式):本原化(容度 1、首项为正)→
   f_{i+1} = f_i / gcd(f_i, f_i') 的显式循环 → h_j = f_j / g_j →
   q_j = h_j / h_{j+1}(末层 q_L = h_L / 1)= 重数恰为 j 的无重因子。
   重数由整系数 GCD 的层指标精确决定,浮点永不参与合并(契约 §4.3,
   ROADMAP.md:149),「重根只出一次」是结构性保证,不是数值聚类。
   全程 ac_int_t,中间量 > 1e18 即 ECOEF(绝不回绕)。

   逐无重因子求根(契约 §5.1,按因子次数路由):deg ≤ 4 走上游精确根式
   (ac_solvers 硬限 [1,4])标 'e';deg 5..8 走 Durand-Kerner
   (ac_pro_numroots)标 'd'。q_j 无重 ⇒ DK 的重根弱点被结构性规避。
   JS 侧整题 ≥5 拒绝是契约 §5.1 成文的双侧能力边界,C 侧不适用。

   渲染(契约 §6,逐字):`[根@e|d重数, ...]`,空表 "[]";排序 Re 降序、
   Re 并列(1e-9)Im 降序的稳定插入排序;复根 = [re]("+"|"-")[|im|]"i",
   re≈0 整段省略、|im|≈1 只写 i(与 §6.2 冻结范例 i@e1/-i@e1/2i 一致:
   re 省略时正号一并吸收);fmt 三级(整判 1e-9 / 有理重建 q≤1000 /
   定点 12 位半连舍)与 ODE §5.1 同算法,平局再取 |p| 小者(契约 §5.4)。

   f' = 0 与常数的边界(契约 §4.4,写死):全零多项式(输入 "0")→ ECOEF;
   非零常数(deg 0)→ 成功、输出 "[]"(刻意偏离 ODE 契约的 ECOEF,常数的
   (根,重数) 列表良定为空)。

   视图纪律:解析+本原化持有 rat 槽(464 B,「复用 poly 视图零增量」,
   ROADMAP.md:152);此后一切整系数代数与渲染在栈上进行(deg ≤ 8 →
   9 系数 × 8 B/数组),deg 5..8 的 ac_pro_numroots 写同 union 的 nr 视图,
   不构成冲突,rat 槽数据在拷栈后不再读(apart 同款纪律)。

   零存储纪律:零 static 对象、零 malloc、零 VLA、零递归;错误码走返回值
   (ac_pro.h eig 节论证:一个 static = TU 净 +32 B .bss)。 */

#include <string.h>
#include <stdint.h>
#include <math.h>

#include "ac_pro_config.h"
#include "ac_pro.h"
#include "../../../C/128KB/src/ac_solvers.h"

#if AC_PRO_ACTIVE

#define SQF_SRC_MAX      128   /* 契约 §3.1:输入长度上限(字节,不含 NUL) */
#define SQF_OUT_EST      512   /* 渲染临时缓冲(§6.1 最坏形态估算 344 B) */
#define SQF_FMT_MAX       32
#define SQF_EPS          1e-9  /* 契约的相等判定容差(CONTRIBUTING.md:96) */
#define SQF_WORK_MAX     ((ac_int_t)1000000000000000000LL)  /* 中间量上限 1e18 */
#define SQF_MAX_ITEMS    AC_PRO_RAT_MAX_DEG                /* 相异根 ≤ 8 */
#define SQF_MAX_LAYER    AC_PRO_RAT_MAX_DEG                /* 重数 ≤ 8 = deg f */

/* 错误码(契约 §7),ac_pro.h 已定义 */

/* 溢出检查的整系数算术(绝不回绕,rational 模块同款) */
static ac_int_t sq_iabs(ac_int_t x) { return (x < 0) ? -x : x; }

static int sq_add_ov(ac_int_t x, ac_int_t y, ac_int_t *out) {
    if ((y > 0 && x > SQF_WORK_MAX - y) || (y < 0 && x < -SQF_WORK_MAX - y)) {
        return AC_PRO_SQF_ECOEF;
    }
    *out = x + y;
    return 0;
}

static int sq_mul_ov(ac_int_t x, ac_int_t y, ac_int_t *out) {
    if (x == 0 || y == 0) { *out = 0; return 0; }
    if (sq_iabs(x) > SQF_WORK_MAX / sq_iabs(y)) { return AC_PRO_SQF_ECOEF; }
    *out = x * y;
    return 0;
}

static ac_int_t sq_igcd(ac_int_t a, ac_int_t b) {
    ac_int_t t;
    while (b != 0) {
        t = a % b;
        a = b;
        b = t;
    }
    return (a < 0) ? -a : a;
}

/* 输出缓冲追加 */
static int sq_puts(char *buf, int cap, int *pos, const char *s) {
    while (*s != '\0') {
        if (*pos >= cap - 1) { return AC_PRO_SQF_EOVER; }
        buf[(*pos)++] = *s++;
    }
    buf[*pos] = '\0';
    return 0;
}

static int sq_puti(char *buf, int cap, int *pos, long long v) {
    char tmp[24];
    int i = 0;
    if (v < 0) {
        if (sq_puts(buf, cap, pos, "-") != 0) { return AC_PRO_SQF_EOVER; }
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
        if (sq_puts(buf, cap, pos, one) != 0) { return AC_PRO_SQF_EOVER; }
    }
    return 0;
}

/* 数值三级 fmt(契约 §5.4,与 ODE §5.1 同算法;平局加 |p| 小者)
 * 域 |x| ≤ 1000(§3.4 Cauchy 界)⇒ x·1e12 < 2^53 且 int64 无溢出。 */
static int sq_fmt(double x, char *out) {
    double r = (x < 0) ? -x : x;
    if (r < 1e12) {
        double nr = (x < 0) ? -(double)((long long)(-x + 0.5))
                            : (double)((long long)(x + 0.5));
        if ((x - nr) < SQF_EPS && (nr - x) < SQF_EPS) {
            long long v = (long long)nr;
            int pos = 0;
            char tmp[SQF_FMT_MAX];
            tmp[0] = '\0';
            if (sq_puti(tmp, SQF_FMT_MAX, &pos, v) != 0) {
                return AC_PRO_SQF_EINTERNAL;   /* fmt 缓冲越界 = 域缺陷(§5.4) */
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
            if (pp < 0) { g = sq_igcd(-pp, (ac_int_t)qq); }
            else { g = sq_igcd(pp, (ac_int_t)qq); }
            if (g > 1) { pp /= g; qq = (int)((ac_int_t)qq / g); }
            err = x - (double)pp / (double)qq;
            if (err < 0) { err = -err; }
            if (err <= SQF_EPS &&
                (err < best_err ||
                 (err == best_err && qq < best_q) ||
                 (err == best_err && qq == best_q && sq_iabs(pp) < sq_iabs(best_p)))) {
                best_err = err;
                best_p = pp;
                best_q = qq;
            }
        }
        if (best_q > 0) {
            int pos = 0;
            char tmp[SQF_FMT_MAX];
            tmp[0] = '\0';
            if (sq_puti(tmp, SQF_FMT_MAX, &pos, best_p) != 0) {
                return AC_PRO_SQF_EINTERNAL;
            }
            if (best_q != 1) {
                char qbuf[SQF_FMT_MAX];
                int qpos = 0;
                if (sq_puts(tmp, SQF_FMT_MAX, &pos, "/") != 0) {
                    return AC_PRO_SQF_EINTERNAL;
                }
                qbuf[0] = '\0';
                if (sq_puti(qbuf, SQF_FMT_MAX, &qpos, best_q) != 0) {
                    return AC_PRO_SQF_EINTERNAL;
                }
                if (sq_puts(tmp, SQF_FMT_MAX, &pos, qbuf) != 0) {
                    return AC_PRO_SQF_EINTERNAL;
                }
            }
            strcpy(out, tmp);
            return 0;
        }
    }
    {
        double ax = (x < 0) ? -x : x;
        long long s = (long long)(ax * 1e12 + 0.5);   /* 域内 |x| ≤ 1000,正数四舍五入 */
        long long ip = s / 1000000000000LL;
        long long fp = s % 1000000000000LL;
        char tmp[SQF_FMT_MAX];
        int pos = 0;
        char fpbuf[16];
        int digits = 12;
        int i;
        if (x < 0) {
            if (sq_puts(tmp, SQF_FMT_MAX, &pos, "-") != 0) {
                return AC_PRO_SQF_EINTERNAL;
            }
        }
        if (sq_puti(tmp, SQF_FMT_MAX, &pos, ip) != 0) {
            return AC_PRO_SQF_EINTERNAL;
        }
        for (i = 0; i < 12; i++) {
            fpbuf[11 - i] = (char)('0' + (int)(fp % 10));
            fp /= 10;
        }
        fpbuf[12] = '\0';
        while (digits > 0 && fpbuf[digits - 1] == '0') { digits--; }
        if (digits > 0) {
            if (sq_puts(tmp, SQF_FMT_MAX, &pos, ".") != 0) {
                return AC_PRO_SQF_EINTERNAL;
            }
            for (i = 0; i < digits; i++) {
                char one[2];
                one[0] = fpbuf[i];
                one[1] = '\0';
                if (sq_puts(tmp, SQF_FMT_MAX, &pos, one) != 0) {
                    return AC_PRO_SQF_EINTERNAL;
                }
            }
        }
        strcpy(out, tmp);
        return 0;
    }
}

/* 契约 §3:解析(自备从严文法)
 * 项 = [符号串(逐字符消费,gb 先例 ac_pro_groebner.c:129)][系数]
 *      [*][x[^n]];'*' 须夹在系数与 x 之间(悬空 3*、*x → EPARSE);
 *      x 后仅允许 ^/空白/+-/串尾;'^' 与次数紧连;n = 0 → EPARSE、
 *      n ≥ 9 → EDEG 当场;|c| > 999 → ECOEF;同次项重复 → EPARSE;
 *      '='/'/'/'.' 等一切文法外字符 → EPARSE(非整系数文法层拒)。
 * 返回 0 = 成功(*deg_out = −1 表示全零多项式)。 */
static int sq_parse(const char *s, ac_int_t *coef, int *deg_out) {
    long long c9[AC_PRO_RAT_MAX_DEG + 1];
    int seen[AC_PRO_RAT_MAX_DEG + 1];
    const char *p = s;
    int k;
    int any = 0;

    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { c9[k] = 0; seen[k] = 0; }

    while (*p == ' ' || *p == '\t') { p++; }
    if (*p == '\0') { return AC_PRO_SQF_EPARSE; }        /* 空串(空白串) */

    for (;;) {
        int neg = 0;
        long long c = 0;
        int saw_coef = 0;
        int saw_x = 0;
        int e = 1;

        /* 项首符号串:逐字符消费,奇偶取符号(+-3x 合法,契约 §3.2) */
        for (;;) {
            while (*p == ' ' || *p == '\t') { p++; }
            if (*p == '-') { neg = !neg; p++; }
            else if (*p == '+') { p++; }
            else { break; }
        }
        while (*p == ' ' || *p == '\t') { p++; }

        while (*p >= '0' && *p <= '9') {
            c = c * 10 + (*p - '0');
            if (c > 999) { return AC_PRO_SQF_ECOEF; }    /* 契约 §3.4 系数域 */
            saw_coef = 1;
            p++;
        }
        while (*p == ' ' || *p == '\t') { p++; }
        if (*p == '*') {
            if (!saw_coef) { return AC_PRO_SQF_EPARSE; } /* *x:悬空 '*' */
            p++;
            while (*p == ' ' || *p == '\t') { p++; }
            if (*p != 'x') { return AC_PRO_SQF_EPARSE; } /* 3*:'*' 后非 x */
        }
        if (*p == 'x') {
            p++;
            saw_x = 1;
            while (*p == ' ' || *p == '\t') { p++; }
            if (*p == '^') {
                p++;
                if (!(*p >= '0' && *p <= '9')) { return AC_PRO_SQF_EPARSE; }
                e = 0;
                for (;;) {
                    e = e * 10 + (*p - '0');
                    if (e > AC_PRO_RAT_MAX_DEG) {
                        return AC_PRO_SQF_EDEG;          /* n ≥ 9 当场(§7 全序) */
                    }
                    p++;
                    if (!(*p >= '0' && *p <= '9')) { break; }
                }
                if (e == 0) { return AC_PRO_SQF_EPARSE; } /* x^0 写成系数 */
            }
        } else if (!saw_coef) {
            return AC_PRO_SQF_EPARSE;                     /* 裸符号 */
        }
        while (*p == ' ' || *p == '\t') { p++; }
        if (saw_x && (*p != '\0' && *p != '+' && *p != '-')) {
            return AC_PRO_SQF_EPARSE;                     /* x3 / x*3 */
        }

        if (saw_x && saw_coef) { c = neg ? -c : c; }
        else if (saw_x) { c = neg ? -1 : 1; }
        else {
            c = neg ? -c : c;
            e = 0;                                        /* 纯常数项落 x^0 */
        }
        if (seen[e]) { return AC_PRO_SQF_EPARSE; }        /* 同次项重复(§3.3-3) */
        seen[e] = 1;
        c9[e] = (ac_int_t)c;
        any = 1;

        if (*p == '\0') { break; }
        if (*p != '+' && *p != '-') { return AC_PRO_SQF_EPARSE; }
        /* 分隔符不前进:下一项开头的符号串消费处处理(gb 先例) */
    }
    if (!any) { return AC_PRO_SQF_EPARSE; }

    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { coef[k] = (ac_int_t)c9[k]; }
    *deg_out = AC_PRO_RAT_MAX_DEG;
    while (*deg_out >= 0 && coef[*deg_out] == 0) { (*deg_out)--; }
    return 0;
}

/* 本原化:去整数容差 + 首项为正(dst 就地,契约 §4.1) */
static void sq_primitive(ac_int_t *f, int *df) {
    ac_int_t g = 0;
    int k;
    int neg;
    if (*df < 0) { return; }
    for (k = 0; k <= *df; k++) {
        if (f[k] != 0) {
            g = (g == 0) ? sq_iabs(f[k]) : sq_igcd(g, f[k]);
        }
    }
    if (g == 0) { g = 1; }
    neg = (f[*df] < 0);
    for (k = 0; k <= *df; k++) {
        ac_int_t v = f[k] / g;   /* 容差整除,精确 */
        f[k] = neg ? -v : v;
    }
}

/* 伪除:f·lc^m = q·g + r(rational 模块同款,r 就地写 rout) */
static int sq_pdivmod(const ac_int_t *f, int df, const ac_int_t *g, int dg,
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
    if (dg < 0) { return AC_PRO_SQF_EINTERNAL; }   /* 零除数:不变式防线 */
    if (df < 0) {
        for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { qout[i] = 0; rout[i] = 0; }
        return 0;
    }
    for (i = 0; i <= df; i++) { w[i] = f[i]; }
    lcg = g[dg];

    while (df >= dg && df >= 0) {
        ac_int_t lfw = w[df];
        k = df - dg;
        for (i = df; i >= 0; i--) {
            rc = sq_mul_ov(lcg, w[i], &t);
            if (rc != 0) { return rc; }
            w[i] = t;
            if (i >= k && i - k <= dg) {
                rc = sq_mul_ov(lfw, g[i - k], &t);
                if (rc != 0) { return rc; }
                rc = sq_add_ov(w[i], -t, &w[i]);
                if (rc != 0) { return rc; }
            }
        }
        for (i = AC_PRO_RAT_MAX_DEG; i >= 0; i--) {
            rc = sq_mul_ov(q[i], lcg, &q[i]);
            if (rc != 0) { return rc; }
        }
        rc = sq_add_ov(q[k], lfw, &q[k]);
        if (rc != 0) { return rc; }
        steps++;
        while (df >= 0 && w[df] == 0) { df--; }
    }
    for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { rout[i] = w[i]; }
    for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { qout[i] = q[i]; }
    lcp = 1;
    for (i = 0; i < steps; i++) {
        rc = sq_mul_ov(lcp, lcg, &lcp);
        if (rc != 0) { return rc; }
    }
    *scale = lcp;
    return 0;
}

/* 整系数精确除:dst = f / g(双方本原,商在 Z[x],Gauss)
 * 余式非零或 scale 除不尽 = 不变式破坏 → EINTERNAL(契约 §4.2)。 */
static int sq_exact_div(const ac_int_t *f, int df, const ac_int_t *g, int dg,
                        ac_int_t *dst, int *ddst,
                        ac_int_t *qtmp, ac_int_t *rtmp) {
    ac_int_t scale;
    int k;
    int rc;
    int d = AC_PRO_RAT_MAX_DEG;
    rc = sq_pdivmod(f, df, g, dg, qtmp, rtmp, &scale);
    if (rc != 0) { return rc; }
    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) {
        if (rtmp[k] != 0) { return AC_PRO_SQF_EINTERNAL; }
    }
    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) {
        if (scale == 1) { dst[k] = qtmp[k]; }
        else if (qtmp[k] % scale == 0) { dst[k] = qtmp[k] / scale; }
        else { return AC_PRO_SQF_EINTERNAL; }
    }
    while (d >= 0 && dst[d] == 0) { d--; }
    *ddst = d;
    return 0;
}

/* 整系数欧几里得 GCD(本原归一,契约 §4.2;ac_pro_rat_gcd 同款)
 * (a, b) ← (b, primitive(prem(a, b))),b = 0 时 a 即 gcd。 */
static int sq_gcd(ac_int_t *a, int *da, ac_int_t *b, int *db,
                  ac_int_t *qtmp, ac_int_t *rtmp) {
    ac_int_t scale;
    ac_int_t t[AC_PRO_RAT_MAX_DEG + 1];
    int dt;
    int k;
    int rc;
    while (*db >= 0) {
        rc = sq_pdivmod(a, *da, b, *db, qtmp, rtmp, &scale);
        if (rc != 0) { return rc; }
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { t[k] = rtmp[k]; }
        dt = AC_PRO_RAT_MAX_DEG;
        while (dt >= 0 && t[dt] == 0) { dt--; }
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { a[k] = b[k]; }
        *da = *db;
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { b[k] = t[k]; }
        *db = dt;
        sq_primitive(b, db);
    }
    sq_primitive(a, da);
    return 0;
}

/* 求根产物项(契约 §5/§6) */
typedef struct {
    double re;
    double im;
    int mult;
    char tag;   /* 'e' = 精确根式管线,'d' = DK 数值管线(契约 §5.1) */
} sq_item_t;

/* 单个无重因子 q(deg dq ≥ 1)的全部根 → items */
static int sq_factor_roots(const ac_int_t *q, int dq, int mult,
                           sq_item_t *items, int *nitems) {
    double qdc[AC_PRO_RAT_MAX_DEG + 1];
    int k;
    int n = 0;
    char tag = (dq <= 4) ? 'e' : 'd';
    double rre[AC_PRO_RAT_MAX_DEG];
    double rim[AC_PRO_RAT_MAX_DEG];

    for (k = 0; k <= dq; k++) { qdc[k] = (double)q[k]; }

    if (dq == 1) {
        rre[0] = -qdc[0] / qdc[1];
        rim[0] = 0.0;
        n = 1;
    } else if (dq <= 4) {
        for (k = 0; k < 5; k++) { g_coeffs[k] = 0.0; }
        for (k = 0; k <= dq; k++) { g_coeffs[k] = qdc[k]; }
        if (dq == 2) { ac_solve_quadratic(); }
        else if (dq == 3) { ac_solve_cubic(0); }
        else { ac_solve_quartic(); }
        if (g_solve_error != 0) { return AC_PRO_SQF_EINTERNAL; }
        if (g_root_count != dq) { return AC_PRO_SQF_EINTERNAL; }
        n = dq;
        for (k = 0; k < n; k++) { rre[k] = g_root_re[k]; rim[k] = g_root_im[k]; }
    } else {
        if (ac_pro_numroots(qdc, dq) != 1) { return AC_PRO_SQF_EINTERNAL; }
        n = dq;
        for (k = 0; k < n; k++) { rre[k] = g_root_re[k]; rim[k] = g_root_im[k]; }
    }

    for (k = 0; k < n; k++) {
        if (*nitems >= SQF_MAX_ITEMS) { return AC_PRO_SQF_EINTERNAL; }
        items[*nitems].re = rre[k];
        items[*nitems].im = rim[k];
        items[*nitems].mult = mult;
        items[*nitems].tag = tag;
        (*nitems)++;
    }
    return 0;
}

/* 主入口(契约 §3-§6 判定全序:EPARSE → EDEG 当场
 * → ECOEF → [SFF 期] ECOEF(溢出)→ [求根渲染期] EOVER / EINTERNAL) */
int ac_pro_sqfree(const char *src, char *out, int out_len) {
    /* 整系数 SFF 工作区(栈;deg ≤ 8 → 9 系数/数组) */
    ac_int_t fc[AC_PRO_RAT_MAX_DEG + 1];    /* f_i(当前层输入) */
    ac_int_t der[AC_PRO_RAT_MAX_DEG + 1];   /* f_i' */
    ac_int_t gc[AC_PRO_RAT_MAX_DEG + 1];    /* g_i = gcd(f_i, f_i') */
    ac_int_t hcur[AC_PRO_RAT_MAX_DEG + 1];  /* h_i = f_i / g_i */
    ac_int_t hprev[AC_PRO_RAT_MAX_DEG + 1]; /* h_{i-1} */
    ac_int_t qpoly[AC_PRO_RAT_MAX_DEG + 1]; /* q_j = h_j / h_{j+1} */
    ac_int_t qtmp[AC_PRO_RAT_MAX_DEG + 1];
    ac_int_t rtmp[AC_PRO_RAT_MAX_DEG + 1];
    sq_item_t items[SQF_MAX_ITEMS];
    char tmp[SQF_OUT_EST];
    char num[SQF_FMT_MAX];
    int usedp[SQF_MAX_ITEMS];
    int dfc, dder, dgc, dhc = 0, dhp = 0, dqp = 0;
    int nitems = 0;
    int layer = 0;
    int hprev_valid = 0;
    int ledger = 0;
    int deg0 = 0;
    int pos = 0;
    int i, j, k, rc;

    if (out == 0 || out_len <= 0) { return AC_PRO_SQF_EOVER; }
    out[0] = '\0';
    if (src == 0) { return AC_PRO_SQF_EPARSE; }
    if (strlen(src) == 0 || strlen(src) > SQF_SRC_MAX) {
        return AC_PRO_SQF_EPARSE;                        /* 空/超长(§3.1) */
    }

    /* 解析进 rat 槽(复用 poly 视图),再拷栈(此后不读 rat 槽) */
    {
        ac_int_t *pc = s_pro_rat_slot(0).c;
        rc = sq_parse(src, pc, &s_pro_rat_deg(0));
        if (rc != 0) { return rc; }
        if (s_pro_rat_deg(0) < 0) { return AC_PRO_SQF_ECOEF; }  /* 全零(§4.4) */
        sq_primitive(pc, &s_pro_rat_deg(0));
        dfc = s_pro_rat_deg(0);
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { fc[k] = pc[k]; }
        deg0 = dfc;
    }
    if (dfc == 0) {
        /* 非零常数:f' = 0 边界(契约 §4.4)→ 成功、空列表 */
        if (sq_puts(tmp, SQF_OUT_EST, &pos, "[]") != 0) {
            return AC_PRO_SQF_EOVER;
        }
        if (pos + 1 > out_len) { return AC_PRO_SQF_EOVER; }
        memcpy(out, tmp, (size_t)pos + 1);
        return AC_PRO_SQF_OK;
    }

    /* 契约 §4.2:循环式 SFF(f_{i+1} = f_i / gcd(f_i, f_i'))
     * h_i = f_i / g_i;q_j = h_j / h_{j+1}(j = 1..L−1),末层 q_L = h_L / 1
     * (f_{L+1} = g_L 本原归一下恒为 1,Gauss)。迭代数 ≤ deg f ≤ 8。 */
    while (dfc >= 1) {
        layer++;
        if (layer > SQF_MAX_LAYER) { return AC_PRO_SQF_EINTERNAL; }
        for (k = 1; k <= AC_PRO_RAT_MAX_DEG; k++) { der[k - 1] = 0; }
        for (k = 1; k <= dfc; k++) { der[k - 1] = (ac_int_t)k * fc[k]; }
        dder = dfc - 1;
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { gc[k] = fc[k]; }
        dgc = dfc;
        {
            ac_int_t gbak[AC_PRO_RAT_MAX_DEG + 1];
            int dgb = dder;
            for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { gbak[k] = der[k]; }
            rc = sq_gcd(gc, &dgc, gbak, &dgb, qtmp, rtmp);
            if (rc != 0) { return rc; }
        }
        rc = sq_exact_div(fc, dfc, gc, dgc, hcur, &dhc, qtmp, rtmp);
        if (rc != 0) { return rc; }                 /* h_i = f_i / g_i */
        if (hprev_valid) {
            rc = sq_exact_div(hprev, dhp, hcur, dhc, qpoly, &dqp, qtmp, rtmp);
            if (rc != 0) { return rc; }             /* q_{i-1} = h_{i-1} / h_i */
            if (dqp >= 1) {
                ledger += (layer - 1) * dqp;
                rc = sq_factor_roots(qpoly, dqp, layer - 1, items, &nitems);
                if (rc != 0) { return rc; }
            }
        }
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { hprev[k] = hcur[k]; }
        dhp = dhc;
        hprev_valid = 1;
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { fc[k] = gc[k]; }
        dfc = dgc;                                  /* f_{i+1} = g_i */
    }
    if (hprev_valid) {
        /* 末层 q_L = h_L / f_{L+1};f_{L+1} = g_L 恒为 1,防御性检查 */
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { qpoly[k] = hprev[k]; }
        dqp = dhp;
        if (dqp >= 1) {
            ledger += layer * dqp;
            rc = sq_factor_roots(qpoly, dqp, layer, items, &nitems);
            if (rc != 0) { return rc; }
        }
    }
    if (ledger != deg0) {
        /* 账本断言(契约 §4.3):Σ j·deg(q_j) = 本原部分次数;
           不平即不变式破坏,绝不输出不完整分解。 */
        return AC_PRO_SQF_EINTERNAL;
    }

    /* 契约 §5.2:共轭完整性(残缺 → EINTERNAL,绝不硬凑) */
    for (i = 0; i < nitems; i++) { usedp[i] = 0; }
    for (i = 0; i < nitems; i++) {
        if (items[i].im < SQF_EPS && items[i].im > -SQF_EPS) { continue; }
        if (usedp[i]) { continue; }
        {
            int v = -1;
            for (j = i + 1; j < nitems; j++) {
                double dr = items[j].re - items[i].re;
                double ds = items[j].im + items[i].im;
                if (usedp[j]) { continue; }
                if (dr < SQF_EPS && dr > -SQF_EPS &&
                    ds < SQF_EPS && ds > -SQF_EPS &&
                    ((items[i].im > 0 && items[j].im < 0) ||
                     (items[i].im < 0 && items[j].im > 0))) {
                    v = j;
                    break;
                }
            }
            if (v < 0) { return AC_PRO_SQF_EINTERNAL; }
            usedp[i] = 1;
            usedp[v] = 1;
        }
    }

    /* 契约 §5.3:排序(渲染顺序的根源),稳定插入,Re 降序,
       Re 并列(1e-9)按 Im 降序 */
    for (i = 1; i < nitems; i++) {
        sq_item_t cr = items[i];
        j = i - 1;
        while (j >= 0) {
            double dr = items[j].re - cr.re;
            double di = (dr < SQF_EPS && dr > -SQF_EPS) ? (items[j].im - cr.im)
                                                        : 0.0;
            if (dr < -SQF_EPS ||
                (dr <= SQF_EPS && dr >= -SQF_EPS && di < -SQF_EPS)) {
                items[j + 1] = items[j];
                j--;
            } else {
                break;
            }
        }
        items[j + 1] = cr;
    }

    /* 契约 §6:渲染(机械拼接,特例仅二) */
    rc = sq_puts(tmp, SQF_OUT_EST, &pos, "[");
    if (rc != 0) { return rc; }
    for (i = 0; i < nitems; i++) {
        if (i > 0) {
            rc = sq_puts(tmp, SQF_OUT_EST, &pos, ", ");
            if (rc != 0) { return rc; }
        }
        if (items[i].im < SQF_EPS && items[i].im > -SQF_EPS) {
            rc = sq_fmt(items[i].re, num);
            if (rc != 0) { return rc; }
            rc = sq_puts(tmp, SQF_OUT_EST, &pos, num);
        } else {
            /* 复根:[re] ("+"|"-") [|im|] "i";re≈0 整段省略(正号一并
               吸收,与 §6.2 冻结范例 i/-i/2i 一致);|im|≈1 只写 i。 */
            if (items[i].re > SQF_EPS || items[i].re < -SQF_EPS) {
                rc = sq_fmt(items[i].re, num);
                if (rc != 0) { return rc; }
                rc = sq_puts(tmp, SQF_OUT_EST, &pos, num);
                if (rc != 0) { return rc; }
                rc = sq_puts(tmp, SQF_OUT_EST, &pos,
                             (items[i].im > 0) ? "+" : "-");
            } else if (items[i].im < 0) {
                rc = sq_puts(tmp, SQF_OUT_EST, &pos, "-");
            }
            if (rc != 0) { return rc; }
            {
                double aim = (items[i].im > 0) ? items[i].im : -items[i].im;
                if (aim > 1.0 + SQF_EPS || aim < 1.0 - SQF_EPS) {
                    rc = sq_fmt(aim, num);
                    if (rc != 0) { return rc; }
                    rc = sq_puts(tmp, SQF_OUT_EST, &pos, num);
                }
            }
            if (rc != 0) { return rc; }
            rc = sq_puts(tmp, SQF_OUT_EST, &pos, "i");
        }
        if (rc != 0) { return rc; }
        rc = sq_puts(tmp, SQF_OUT_EST, &pos, "@");
        if (rc == 0) {
            char one[2];
            one[0] = items[i].tag;
            one[1] = '\0';
            rc = sq_puts(tmp, SQF_OUT_EST, &pos, one);
        }
        if (rc == 0) {
            rc = sq_puti(tmp, SQF_OUT_EST, &pos, items[i].mult);
        }
        if (rc != 0) { return rc; }
    }
    rc = sq_puts(tmp, SQF_OUT_EST, &pos, "]");
    if (rc != 0) { return rc; }

    if (pos + 1 > out_len) { return AC_PRO_SQF_EOVER; }   /* 不写部分内容 */
    memcpy(out, tmp, (size_t)pos + 1);
    return AC_PRO_SQF_OK;
}

#endif /* AC_PRO_ACTIVE */
