/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_pro_sysolve.c — 二元多项式方程组 GB 消元数值全解(v2.5.0 F2)

   行为边界由 docs/contracts/sysolve-format-v25.md 逐字冻结(本文件头注是该
   契约的实现侧镜像;契约变更须先改契约再改码)。

   管线(契约 §4):
     f,g 两多项式串(gb 文法,总次数 ≤ 4)
     → 现有 Groebner(lex x>y,M4GB;gb 视图)
     → 经 GB 访问器把 y-单变元消元多项式 h 与混合元素【拷入栈缓冲】
       (copy-out 后 gb 视图即不再持有——随后 nr 视图被 ac_pro_numroots
       复用,与 gb 视图同 union,访问器互斥纪律由此兑现)
     → h 经 square-free 预处理去重根(gcd(h,h') 精确整除;切相系统的
       h=(y-1)^2 在符号层归一,DK 重根弱点被结构性规避)
     → h_sf 求根:1 直接式 / 2..4 上游精确根式 / 5..8 Durand-Kerner
       (ac_pro_numroots,nr 视图)
     → 解耦 GB(全部混合元为纯 x)走交叉积;耦合 GB 逐 y 根 r 代入混合元
       得复系数一元 p_r(x),一律走复系数 Durand-Kerner(栈上,无视图占用;
       r 已是数值,根式是伪精度)
     → 每个 (x,y) 候选代回 f,g 残差 ≤ 1e-9(相对,
       resid = |P|/max(1,Σ|c|·|x|^i·|y|^j))回验才入池;任一候选不过
       → EVERIFY 整体失败(绝不输出部分解)
     → 去重(1e-9 四分量)→ 排序(y Re 降序 → y Im 降序 → x Re 降序 →
       x Im 降序,1e-9 并列)→ 渲染 "(X, Y)" 列表(fmt 三级 = sqfree 契约
       §5.4 同算法;复根 [re](±)|im|i 同 sqfree §6.1)。

   GB={1} → 成功语义 0 解 "[]";无 y-单变元元 / 某 y 根处全部混合元代回
   恒零 → EZDIM(正维数,直线 {y=r} ⊆ V(GB)=V(I),与根式性无关)。

   内存纪律:无 malloc、无 VLA、无递归、零 static 对象(错误码走返回值;
   ac_pro.h eig 节论证:一个 static = TU 净 +32 B .bss)。栈预算:GB 串缓冲
   2 KB + 候选池 4×512×8 = 16 KB + 混合元素稀疏表 + 幂表,合计 < 22 KB
   (单帧,无递归;栈数组先例:pro_eval_vars stack_val、ac_pro_sqfree 全栈
   代数)。候选池上界证明:ny ≤ 8(h_sf deg)× nmixed ≤ 7 × deg p_r ≤ 8
   = 448 < 512,池不可能溢出。求解层 clobber 副作用(g_coeffs / g_root_* /
   g_solve_error / gb·nr 视图)按 sqfree 先例成文(契约 §8)。 */

#include <string.h>
#include <stdint.h>
#include <math.h>

#include "ac_pro_config.h"
#include "ac_pro.h"
#include "../../../C/128KB/src/ac_exact.h"
#include "../../../C/128KB/src/ac_solvers.h"

#if AC_PRO_ACTIVE

#define SY_GB_BUF       2048  /* GB 串临时缓冲(8 元素 × 16 项最坏 ~1.5 KB) */
#define SY_TERMS        AC_PRO_GB_TERMS        /* 16:与 gb 解析容量一致 */
#define SY_MIX_MAX      (AC_PRO_GB_BASIS - 1)  /* 7:至多 1 个 h + 7 个混合元 */
#define SY_ROOTS        AC_PRO_NUMROOT_MAX_DEG /* 8 */
#define SY_SOL_CAP      16                     /* Bézout 界 deg f·deg g ≤ 16 */
#define SY_CAND_MAX     512                    /* 见文件头候选池上界证明(448) */
#define SY_EPS          1e-9
#define SY_FMT_MAX      32
#define SY_WORK_MAX     ((ac_int_t)1000000000000000000LL) /* 1e18,与 sqfree 同纪律 */

/* 错误码(契约 §7):−1..−7 与 AC_PRO_GB_* 同值同义(GB 阶段透传),
 * −8..−11 为本层扩段;定义在 ac_pro.h 的 SYSOLVE 锚点区。 */

/* ---- 稀疏项表(f/g 残差项表与混合元素共用;项 = int64 系数 + 打包指数) ---- */

typedef struct {
    int64_t c[SY_TERMS];
    uint8_t ep[SY_TERMS];   /* (ex<<4)|ey,与 gb 打包一致 */
    int n;
} sy_terms_t;

static int sy_ex_of(int ep) { return (ep >> 4) & 0x0F; }
static int sy_ey_of(int ep) { return ep & 0x0F; }

/* ---- 溢出检查的整系数算术(与 sqfree 同纪律,绝不回绕) ---- */

static ac_int_t sy_iabs(ac_int_t x) { return (x < 0) ? -x : x; }

static int sy_add_ov(ac_int_t x, ac_int_t y, ac_int_t *out) {
    if ((y > 0 && x > SY_WORK_MAX - y) || (y < 0 && x < -SY_WORK_MAX - y)) {
        return AC_PRO_SYS_EOVER;
    }
    *out = x + y;
    return 0;
}

static int sy_mul_ov(ac_int_t x, ac_int_t y, ac_int_t *out) {
    if (x == 0 || y == 0) { *out = 0; return 0; }
    if (sy_iabs(x) > SY_WORK_MAX / sy_iabs(y)) { return AC_PRO_SYS_EOVER; }
    *out = x * y;
    return 0;
}

static ac_int_t sy_igcd(ac_int_t a, ac_int_t b) {
    ac_int_t t;
    while (b != 0) {
        t = a % b;
        a = b;
        b = t;
    }
    return (a < 0) ? -a : a;
}

/* ---- 解析(gb 文法镜像,供残差回验建项表;语义与 gb_parse_term 逐字对齐)
 * 项 = [符号串逐字符消费][系数][*][x[^n]][*][y[^m]],变量序任意、每变量
 * 每项至多一次;系数缺省 1、指数缺省 1;单变量指数 ≤ 8、项总次数 ≤ 4;
 * 同类项重复出现合法(由归一化合并,gb 先例)。返回 1 成功,否则 GB 码。 ---- */

static int sy_parse_term(const char **ps, int64_t *out_c, int *out_nx, int *out_ny) {
    const char *s = *ps;
    int64_t c = 0;
    int nx = 0;
    int ny = 0;
    int saw_coef = 0;
    int neg = 0;

    while (*s == ' ' || *s == '\t') { s++; }
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    while (*s == ' ' || *s == '\t') { s++; }

    while (*s >= '0' && *s <= '9') {
        if (c > (INT64_MAX - 9) / 10) { return AC_PRO_GB_EOVER; }
        c = c * 10 + (*s - '0');
        saw_coef = 1;
        s++;
    }
    if (!saw_coef) { c = 1; }
    while (*s == ' ' || *s == '\t') { s++; }
    if (*s == '*') { s++; }
    while (*s == ' ' || *s == '\t') { s++; }

    for (;;) {
        int var;
        int exp = 1;
        if (*s == 'x' || *s == 'X') { var = 0; }
        else if (*s == 'y' || *s == 'Y') { var = 1; }
        else { break; }
        s++;
        if (*s == '^') {
            s++;
            exp = 0;
            while (*s >= '0' && *s <= '9') {
                if (exp > 1000) { return AC_PRO_GB_EDEG; }
                exp = exp * 10 + (*s - '0');
                s++;
            }
        }
        if (exp > AC_PRO_GB_MAX_DEG) { return AC_PRO_GB_EDEG; }
        if (var == 0) { if (nx > 0) { return AC_PRO_GB_EARG; } nx = exp; }
        else          { if (ny > 0) { return AC_PRO_GB_EARG; } ny = exp; }
        while (*s == ' ' || *s == '\t') { s++; }
        if (*s == '*') { s++; }
        while (*s == ' ' || *s == '\t') { s++; }
    }

    if (nx + ny > AC_PRO_GB_IN_DEG) { return AC_PRO_GB_EDEG; }
    *out_c = neg ? -c : c;
    *out_nx = nx;
    *out_ny = ny;
    *ps = s;
    return 1;
}

/* 解析进稀疏项表并归一化(降序插入排序 + 同类项合并 + 去零;gb 同构) */
static int sy_parse_into(const char *src, sy_terms_t *dst) {
    const char *p = src;
    int n = 0;
    int i;
    int j;

    dst->n = 0;
    while (*p == ' ' || *p == '\t') { p++; }
    if (*p == '\0') { return AC_PRO_GB_EARG; }

    for (;;) {
        int rc;
        int64_t c = 0;
        int nx = 0;
        int ny = 0;

        rc = sy_parse_term(&p, &c, &nx, &ny);
        if (rc != 1) { return rc; }
        if (c != 0) {
            if (n >= SY_TERMS) { return AC_PRO_GB_ETERM; }
            if (sy_iabs(c) > SY_WORK_MAX) { return AC_PRO_SYS_EOVER; }
            dst->c[n] = c;
            dst->ep[n] = (uint8_t)((nx << 4) | ny);
            n++;
        }
        while (*p == ' ' || *p == '\t') { p++; }
        if (*p == '\0') { break; }
        if (*p != '+' && *p != '-') { return AC_PRO_GB_EARG; }
    }

    for (i = 1; i < n; i++) {
        int64_t kc = dst->c[i];
        int ke = dst->ep[i];
        j = i - 1;
        while (j >= 0 && dst->ep[j] < ke) {
            dst->c[j + 1] = dst->c[j];
            dst->ep[j + 1] = dst->ep[j];
            j--;
        }
        dst->c[j + 1] = kc;
        dst->ep[j + 1] = (uint8_t)ke;
    }
    {
        int out = 0;
        i = 0;
        while (i < n) {
            int64_t sum = dst->c[i];
            int e = dst->ep[i];
            int k = i + 1;
            int rc;
            while (k < n && dst->ep[k] == e) {
                rc = sy_add_ov(sum, dst->c[k], &sum);
                if (rc != 0) { return rc; }
                k++;
            }
            if (sum != 0) {
                dst->c[out] = sum;
                dst->ep[out] = (uint8_t)e;
                out++;
            }
            i = k;
        }
        dst->n = out;
    }
    return 1;
}

/* ---- 整系数 SFF 预处理(h 去重根):primitive / pdivmod / gcd / exact div
 * 与 ac_pro_sqfree.c 同款算法(本原归一欧几里得 + prem 逐步 content 控制),
 * 溢出走本层 EOVER。 ---- */

static void sy_primitive(int64_t *f, int *df) {
    int64_t g = 0;
    int k;
    int neg;
    if (*df < 0) { return; }
    for (k = 0; k <= *df; k++) {
        if (f[k] != 0) {
            g = (g == 0) ? sy_iabs(f[k]) : sy_igcd(g, f[k]);
        }
    }
    if (g == 0) { g = 1; }
    neg = (f[*df] < 0);
    for (k = 0; k <= *df; k++) {
        f[k] = neg ? -(f[k] / g) : (f[k] / g);
    }
}

/* 伪除:w·lc^steps = q·g + r(qout/scale_out 均可为 0 指针 = 只要 r)
 * scale_out = lc(g)^steps(商的真实值 = qout / scale_out;精确除需要它) */
static int sy_pdivmod(const int64_t *f, int df, const int64_t *g, int dg,
                      int64_t *qout, int64_t *rout, int64_t *scale_out) {
    int64_t w[AC_PRO_RAT_MAX_DEG + 1];
    int64_t q[AC_PRO_RAT_MAX_DEG + 1];
    int64_t lcg;
    int64_t lcp;
    int64_t t;
    int i;
    int rc;
    int steps = 0;

    for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { q[i] = 0; w[i] = 0; }
    if (scale_out) { *scale_out = 1; }
    if (dg < 0) { return AC_PRO_SYS_EINTERNAL; }
    if (df < 0) {
        for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { rout[i] = 0; if (qout) { qout[i] = 0; } }
        return 0;
    }
    for (i = 0; i <= df; i++) { w[i] = f[i]; }
    lcg = g[dg];

    while (df >= dg && df >= 0) {
        int64_t lfw = w[df];
        int k = df - dg;
        for (i = df; i >= 0; i--) {
            rc = sy_mul_ov(lcg, w[i], &t);
            if (rc != 0) { return rc; }
            w[i] = t;
            /* 消去减法无条件执行(qout 只门控商的记账)——否则 gcd 路径
               (qout = 0)永不消首项,w *= lcg 膨胀至 EOVER(实测踩坑) */
            if (i >= k && i - k <= dg) {
                rc = sy_mul_ov(lfw, g[i - k], &t);
                if (rc != 0) { return rc; }
                rc = sy_add_ov(w[i], -t, &w[i]);
                if (rc != 0) { return rc; }
            }
        }
        if (qout != 0) {
            for (i = AC_PRO_RAT_MAX_DEG; i >= 0; i--) {
                rc = sy_mul_ov(q[i], lcg, &q[i]);
                if (rc != 0) { return rc; }
            }
            rc = sy_add_ov(q[k], lfw, &q[k]);
            if (rc != 0) { return rc; }
        }
        steps++;
        while (df >= 0 && w[df] == 0) { df--; }
    }

    lcp = 1;
    for (i = 0; i < steps; i++) {
        rc = sy_mul_ov(lcp, lcg, &lcp);
        if (rc != 0) { return rc; }
    }
    if (scale_out) { *scale_out = lcp; }
    for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { rout[i] = w[i]; }
    if (qout != 0) {
        for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { qout[i] = q[i]; }
    }
    return 0;
}

/* 整系数欧几里得 GCD:(a,b) ← (b, primitive(rem(a,b))),b=0 时 a 即 gcd */
static int sy_poly_gcd(int64_t *a, int *da, int64_t *b, int *db) {
    int64_t t[AC_PRO_RAT_MAX_DEG + 1];
    int64_t r[AC_PRO_RAT_MAX_DEG + 1];
    int dt;
    int k;
    int rc;
    while (*db >= 0) {
        rc = sy_pdivmod(a, *da, b, *db, 0, r, 0);
        if (rc != 0) { return rc; }
        dt = AC_PRO_RAT_MAX_DEG;
        while (dt >= 0 && r[dt] == 0) { dt--; }
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { t[k] = r[k]; }
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { a[k] = b[k]; }
        *da = *db;
        for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) { b[k] = t[k]; }
        *db = dt;
        sy_primitive(b, db);
    }
    sy_primitive(a, da);
    return 0;
}

/* 精确整除:dst = f / g(余式非零 → EINTERNAL) */
static int sy_exact_div(const int64_t *f, int df, const int64_t *g, int dg,
                        int64_t *dst, int *ddst) {
    int64_t q[AC_PRO_RAT_MAX_DEG + 1];
    int64_t r[AC_PRO_RAT_MAX_DEG + 1];
    int64_t scale;
    int k;
    int d;
    int rc;
    rc = sy_pdivmod(f, df, g, dg, q, r, &scale);
    if (rc != 0) { return rc; }
    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) {
        if (r[k] != 0) { return AC_PRO_SYS_EINTERNAL; }
    }
    /* prem 商 = 真商·scale(见 sy_pdivmod 头注);scale 整除是 Gauss 引理的
     * 结构性保证(f、g 均本原),除不尽 = 不变式破坏 → EINTERNAL */
    for (k = 0; k <= AC_PRO_RAT_MAX_DEG; k++) {
        if (scale == 1) { dst[k] = q[k]; }
        else if (q[k] % scale == 0) { dst[k] = q[k] / scale; }
        else { return AC_PRO_SYS_EINTERNAL; }
    }
    d = AC_PRO_RAT_MAX_DEG;
    while (d >= 0 && dst[d] == 0) { d--; }
    *ddst = d;
    return 0;
}

/* ---- 复数 Durand-Kerner(耦合代回路径专用;栈上工作区,无视图占用)
 * 迭代格式与 ac_pro_numroots 逐字同构(Aberth 圆盘初值半格偏移、200 轮
 * 预算、步长 1e-12 + 残差门 1e-12·(1+scale) 双判据、分母重合确定性扰动
 * 1e-7·(i+1)、导出时 |im| < 1e-10 归零),差异仅在系数为复。
 * 返回 0 成功 / −1 不收敛(调用方 → EINTERNAL)。 ---- */

#define SY_DK_MAX_ITER 200
#define SY_DK_TOL      1e-12
#define SY_DK_TINY     1e-300

static void sy_ceval(const double *cre, const double *cim, int n,
                     double zre, double zim, double *ore, double *oim) {
    double re = cre[n];
    double im = cim[n];
    int i;
    for (i = n - 1; i >= 0; i--) {
        double tr = re * zre - im * zim + cre[i];
        double ti = re * zim + im * zre + cim[i];
        re = tr;
        im = ti;
    }
    *ore = re;
    *oim = im;
}

static int sy_dk_complex(const double *cre, const double *cim, int n,
                         double *zre, double *zim) {
    double rmax = 0.0;
    double cn;
    double radius;
    double two_pi_over_n;
    double max_delta = 0.0;
    int i;
    int j;
    int iter;

    cn = sqrt(cre[n] * cre[n] + cim[n] * cim[n]);
    for (i = 0; i < n; i++) {
        double a = sqrt(cre[i] * cre[i] + cim[i] * cim[i]);
        if (a > rmax) { rmax = a; }
    }
    if (!(cn > SY_DK_TINY)) { return -1; }
    radius = 1.0 + rmax / cn;
    if (!isfinite(radius) || radius < 1.0) { return -1; }

    two_pi_over_n = 6.283185307179586 / (double)n;
    for (i = 0; i < n; i++) {
        double ang = two_pi_over_n * ((double)i + 0.5);
        zre[i] = radius * cos(ang);
        zim[i] = radius * sin(ang);
    }

    for (iter = 0; iter < SY_DK_MAX_ITER; iter++) {
        max_delta = 0.0;
        for (i = 0; i < n; i++) {
            double pre, pim, dre = 1.0, dim = 0.0, den_mag2, qre, qim;
            double d_re, d_im, dabs;
            sy_ceval(cre, cim, n, zre[i], zim[i], &pre, &pim);
            for (j = 0; j < n; j++) {
                double er, ei, tr, ti;
                if (j == i) { continue; }
                er = zre[i] - zre[j];
                ei = zim[i] - zim[j];
                tr = dre * er - dim * ei;
                ti = dre * ei + dim * er;
                dre = tr;
                dim = ti;
            }
            den_mag2 = dre * dre + dim * dim;
            if (den_mag2 < 1e-28) {
                dre = 1e-7 * (double)(i + 1);
                dim = 1e-7 * (double)(i + 1);
                den_mag2 = dre * dre + dim * dim;
            }
            qre = (pre * dre + pim * dim) / den_mag2;
            qim = (pim * dre - pre * dim) / den_mag2;
            d_re = -qre;
            d_im = -qim;
            dabs = sqrt(d_re * d_re + d_im * d_im);
            if (dabs > max_delta) { max_delta = dabs; }
            zre[i] += d_re;
            zim[i] += d_im;
        }
        if (!isfinite(max_delta)) { return -1; }
        if (max_delta < SY_DK_TOL) { break; }
    }

    /* 残差门(重根步长停滞的出口,与 ac_pro_numroots 同式) */
    if (max_delta >= SY_DK_TOL) {
        double residual = 0.0;
        double scale = 0.0;
        for (i = 0; i < n; i++) {
            double pre, pim, mag, sc = 0.0, pw = 1.0;
            double zabs = sqrt(zre[i] * zre[i] + zim[i] * zim[i]);
            sy_ceval(cre, cim, n, zre[i], zim[i], &pre, &pim);
            mag = sqrt(pre * pre + pim * pim);
            if (mag > residual) { residual = mag; }
            for (j = 0; j <= n; j++) {
                double aj = sqrt(cre[j] * cre[j] + cim[j] * cim[j]);
                sc += aj * pw;
                pw *= zabs;
            }
            if (sc > scale) { scale = sc; }
        }
        if (residual > 1e-12 * (1.0 + scale)) { return -1; }
    }

    for (i = 0; i < n; i++) {
        if (fabs(zim[i]) < 1e-10) { zim[i] = 0.0; }
    }
    return 0;
}

/* ---- 单个整系数多项式求根路由(契约 §4.5;解耦交叉积的 x 侧复用)
 * 入口 c[0..d] 升幂 int64(dense,无空洞);出参 re/im。
 * 返回根数,失败返回负错误码。 ---- */

static int sy_roots_int(const int64_t *c, int d, double *re, double *im) {
    double dc[AC_PRO_RAT_MAX_DEG + 1];
    int k;

    if (d == 1) {
        re[0] = -(double)c[0] / (double)c[1];
        im[0] = 0.0;
        return 1;
    }
    for (k = 0; k <= d; k++) { dc[k] = (double)c[k]; }
    if (d <= 4) {
        for (k = 0; k < 5; k++) { g_coeffs[k] = 0.0; }
        for (k = 0; k <= d; k++) { g_coeffs[k] = dc[k]; }
        if (d == 2) { ac_solve_quadratic(); }
        else if (d == 3) { ac_solve_cubic(0); }
        else { ac_solve_quartic(); }
        if (g_solve_error != 0) { return AC_PRO_SYS_EINTERNAL; }
        if (g_root_count != d) { return AC_PRO_SYS_EINTERNAL; }
        for (k = 0; k < d; k++) { re[k] = g_root_re[k]; im[k] = g_root_im[k]; }
        return d;
    }
    if (ac_pro_numroots(dc, d) != 1) { return AC_PRO_SYS_EINTERNAL; }
    for (k = 0; k < d; k++) { re[k] = g_root_re[k]; im[k] = g_root_im[k]; }
    return d;
}

/* ---- 渲染(fmt 三级 = sqfree 契约 §5.4 同算法;复根 = sqfree §6.1 同形) ---- */

static int sy_puts(char *buf, int cap, int *pos, const char *s) {
    while (*s != '\0') {
        if (*pos >= cap - 1) { return AC_PRO_SYS_EOVER; }
        buf[(*pos)++] = *s++;
    }
    buf[*pos] = '\0';
    return 0;
}

static int sy_puti(char *buf, int cap, int *pos, long long v) {
    char tmp[24];
    int i = 0;
    if (v < 0) {
        if (sy_puts(buf, cap, pos, "-") != 0) { return AC_PRO_SYS_EOVER; }
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
        if (sy_puts(buf, cap, pos, one) != 0) { return AC_PRO_SYS_EOVER; }
    }
    return 0;
}

static int sy_fmt(double x, char *out) {
    double r = (x < 0) ? -x : x;
    if (r < 1e12) {
        double nr = (x < 0) ? -(double)((long long)(-x + 0.5))
                            : (double)((long long)(x + 0.5));
        if ((x - nr) < SY_EPS && (nr - x) < SY_EPS) {
            long long v = (long long)nr;
            int pos = 0;
            char tmp[SY_FMT_MAX];
            tmp[0] = '\0';
            if (sy_puti(tmp, SY_FMT_MAX, &pos, v) != 0) {
                return AC_PRO_SYS_EINTERNAL;
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
            if (pp < 0) { g = sy_igcd(-pp, (ac_int_t)qq); }
            else { g = sy_igcd(pp, (ac_int_t)qq); }
            if (g > 1) { pp /= g; qq = (int)((ac_int_t)qq / g); }
            err = x - (double)pp / (double)qq;
            if (err < 0) { err = -err; }
            if (err <= SY_EPS &&
                (err < best_err ||
                 (err == best_err && qq < best_q) ||
                 (err == best_err && qq == best_q && sy_iabs(pp) < sy_iabs(best_p)))) {
                best_err = err;
                best_p = pp;
                best_q = qq;
            }
        }
        if (best_q > 0) {
            int pos = 0;
            char tmp[SY_FMT_MAX];
            tmp[0] = '\0';
            if (sy_puti(tmp, SY_FMT_MAX, &pos, best_p) != 0) {
                return AC_PRO_SYS_EINTERNAL;
            }
            if (best_q != 1) {
                char qbuf[SY_FMT_MAX];
                int qpos = 0;
                if (sy_puts(tmp, SY_FMT_MAX, &pos, "/") != 0) {
                    return AC_PRO_SYS_EINTERNAL;
                }
                qbuf[0] = '\0';
                if (sy_puti(qbuf, SY_FMT_MAX, &qpos, best_q) != 0) {
                    return AC_PRO_SYS_EINTERNAL;
                }
                if (sy_puts(tmp, SY_FMT_MAX, &pos, qbuf) != 0) {
                    return AC_PRO_SYS_EINTERNAL;
                }
            }
            strcpy(out, tmp);
            return 0;
        }
    }
    {
        double ax = (x < 0) ? -x : x;
        long long s = (long long)(ax * 1e12 + 0.5);
        long long ip = s / 1000000000000LL;
        long long fp = s % 1000000000000LL;
        char tmp[SY_FMT_MAX];
        int pos = 0;
        char fpbuf[16];
        int digits = 12;
        int i;
        if (x < 0) {
            if (sy_puts(tmp, SY_FMT_MAX, &pos, "-") != 0) {
                return AC_PRO_SYS_EINTERNAL;
            }
        }
        if (sy_puti(tmp, SY_FMT_MAX, &pos, ip) != 0) {
            return AC_PRO_SYS_EINTERNAL;
        }
        for (i = 0; i < 12; i++) {
            fpbuf[11 - i] = (char)('0' + (int)(fp % 10));
            fp /= 10;
        }
        fpbuf[12] = '\0';
        while (digits > 0 && fpbuf[digits - 1] == '0') { digits--; }
        if (digits > 0) {
            if (sy_puts(tmp, SY_FMT_MAX, &pos, ".") != 0) {
                return AC_PRO_SYS_EINTERNAL;
            }
            for (i = 0; i < digits; i++) {
                char one[2];
                one[0] = fpbuf[i];
                one[1] = '\0';
                if (sy_puts(tmp, SY_FMT_MAX, &pos, one) != 0) {
                    return AC_PRO_SYS_EINTERNAL;
                }
            }
        }
        strcpy(out, tmp);
        return 0;
    }
}

/* 坐标渲染:实根 fmt(re);复根 [re](+/-)|im|i(re≈0 省略、|im|≈1 只写 i) */
static int sy_fmt_coord(double re, double im, char *out, int cap) {
    int pos = 0;
    double aim = (im < 0) ? -im : im;
    if (aim < SY_EPS) {
        if (sy_fmt(re, out) != 0) { return AC_PRO_SYS_EINTERNAL; }
        return 0;
    }
    out[0] = '\0';
    if (fabs(re) >= SY_EPS) {
        char num[SY_FMT_MAX];
        if (sy_fmt(re, num) != 0) { return AC_PRO_SYS_EINTERNAL; }
        if (sy_puts(out, cap, &pos, "[") != 0) { return AC_PRO_SYS_EOVER; }
        if (sy_puts(out, cap, &pos, num) != 0) { return AC_PRO_SYS_EOVER; }
        if (sy_puts(out, cap, &pos, "]") != 0) { return AC_PRO_SYS_EOVER; }
        if (sy_puts(out, cap, &pos, (im > 0) ? "+" : "-") != 0) { return AC_PRO_SYS_EOVER; }
    } else if (im < 0) {
        if (sy_puts(out, cap, &pos, "-") != 0) { return AC_PRO_SYS_EOVER; }
    }
    if (fabs(aim - 1.0) >= SY_EPS) {
        char num[SY_FMT_MAX];
        if (sy_fmt(aim, num) != 0) { return AC_PRO_SYS_EINTERNAL; }
        if (sy_puts(out, cap, &pos, num) != 0) { return AC_PRO_SYS_EOVER; }
    }
    if (sy_puts(out, cap, &pos, "i") != 0) { return AC_PRO_SYS_EOVER; }
    return 0;
}

/* ---- 主入口(契约 §4 判定全序:EARG/EDEG/GB 透传 → EZDIM → EOVER(SFF)
 * → EINTERNAL(求根)→ ESOLT → EVERIFY → [渲染期] EOVER/EINTERNAL) ---- */

int ac_pro_sysolve(const char *fsrc, const char *gsrc, char *out, int out_len) {
    char gbuf[SY_GB_BUF];
    sy_terms_t fterms;
    sy_terms_t gterms;
    sy_terms_t mixed[SY_MIX_MAX];
    int nmixed = 0;
    int64_t hc[AC_PRO_RAT_MAX_DEG + 1];
    int64_t hsf[AC_PRO_RAT_MAX_DEG + 1];
    int dh = -1;
    int dhsf = -1;
    int decoupled = 1;
    int has_x_el = 0;
    int64_t xc[AC_PRO_RAT_MAX_DEG + 1];
    int dxc = -1;
    double xre[SY_ROOTS];
    double xim[SY_ROOTS];
    double yre[SY_ROOTS];
    double yim[SY_ROOTS];
    int nx = 0;
    int ny = 0;
    /* 候选池(回验前;上界 448 < 512,见文件头) */
    double sxre[SY_CAND_MAX];
    double sxim[SY_CAND_MAX];
    double syre[SY_CAND_MAX];
    double syim[SY_CAND_MAX];
    int nsol = 0;
    /* 解池(回验 + 去重后;≤ 16) */
    double vre[SY_SOL_CAP];
    double vire[SY_SOL_CAP];
    double vjre[SY_SOL_CAP];
    double vjim[SY_SOL_CAP];
    int nacc = 0;
    int rc;
    int k;
    int t;
    int i;
    int j;
    int e;

    if (out == 0 || fsrc == 0 || gsrc == 0) { return AC_PRO_SYS_EARG; }
    out[0] = '\0';

    /* [1] GB 阶段(契约 §4.1;GB 串只作丢弃产物,访问器读视图) */
    rc = ac_pro_groebner(fsrc, gsrc, gbuf, (int)sizeof(gbuf));
    if (rc != AC_PRO_GB_OK) {
        if (rc == AC_PRO_GB_EINTERNAL) { return AC_PRO_SYS_EINTERNAL; }
        if (rc == AC_PRO_GB_EBUF) { return AC_PRO_SYS_EINTERNAL; }  /* 本 TU 缓冲不足=域缺陷 */
        return rc;                                                  /* −1..−6 同值透传 */
    }

    /* [2] copy-out + 分类(契约 §4.2/§4.3;此后 gb 视图不再读) */
    for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { hc[i] = 0; xc[i] = 0; }
    {
        int n = ac_pro_gb_count();
        for (k = 0; k < n; k++) {
            int nt = ac_pro_gb_terms(k);
            int all_x0 = 1;
            int is_const = 1;
            if (nt <= 0) { return AC_PRO_SYS_EINTERNAL; }
            for (t = 0; t < nt; t++) {
                int ep = ac_pro_gb_exps(k, t);
                if (sy_ex_of(ep) != 0) { all_x0 = 0; is_const = 0; }
                else if (sy_ey_of(ep) != 0) { is_const = 0; }
            }
            if (is_const) {
                /* 非零常数元素 = GB 塌缩 {1}(reduced GB 的结构性等价) */
                int used = 0;
                if (sy_puts(out, out_len, &used, "[]") != 0) { return AC_PRO_SYS_EOVER; }
                return AC_PRO_SYS_OK;
            }
            if (all_x0) {
                if (dh >= 0) { return AC_PRO_SYS_EINTERNAL; }  /* 双 h:interreduce 不变式破坏 */
                for (t = 0; t < nt; t++) {
                    int ey = sy_ey_of(ac_pro_gb_exps(k, t));
                    if (ey > AC_PRO_RAT_MAX_DEG) { return AC_PRO_SYS_EINTERNAL; }
                    hc[ey] = ac_pro_gb_coef(k, t);
                    if (ey > dh) { dh = ey; }
                }
            } else {
                int pure_x = 1;
                if (nmixed >= SY_MIX_MAX) { return AC_PRO_SYS_EINTERNAL; }
                mixed[nmixed].n = nt;
                for (t = 0; t < nt; t++) {
                    mixed[nmixed].c[t] = ac_pro_gb_coef(k, t);
                    mixed[nmixed].ep[t] = (uint8_t)ac_pro_gb_exps(k, t);
                    if (sy_ey_of(mixed[nmixed].ep[t]) != 0) { pure_x = 0; }
                }
                if (!pure_x) {
                    decoupled = 0;
                } else if (dxc < 0) {
                    for (t = 0; t < nt; t++) {
                        int ex = sy_ex_of(mixed[nmixed].ep[t]);
                        if (ex > AC_PRO_RAT_MAX_DEG) { return AC_PRO_SYS_EINTERNAL; }
                        xc[ex] = mixed[nmixed].c[t];
                        if (ex > dxc) { dxc = ex; }
                    }
                    has_x_el = 1;
                } else {
                    return AC_PRO_SYS_EINTERNAL;  /* 双纯 x 元素:antichain 不变式破坏 */
                }
                nmixed++;
            }
        }
    }

    /* f,g 残差项表(与 GB 解析同文法;此处失败=GB 词法放行而本表拒绝,
     * 属两侧解析器漂移 → EINTERNAL 防御) */
    rc = sy_parse_into(fsrc, &fterms);
    if (rc != 1) { return (rc == AC_PRO_GB_EOVER) ? AC_PRO_SYS_EOVER : AC_PRO_SYS_EINTERNAL; }
    rc = sy_parse_into(gsrc, &gterms);
    if (rc != 1) { return (rc == AC_PRO_GB_EOVER) ? AC_PRO_SYS_EOVER : AC_PRO_SYS_EINTERNAL; }

    if (dh < 0) { return AC_PRO_SYS_EZDIM; }   /* 无 y-单变元元(含零理想) */

    /* [3] h 的 square-free 预处理(契约 §4.4) */
    for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { hsf[i] = hc[i]; }
    dhsf = dh;
    sy_primitive(hsf, &dhsf);
    {
        int64_t der[AC_PRO_RAT_MAX_DEG + 1];
        int64_t gg[AC_PRO_RAT_MAX_DEG + 1];
        int dder;
        int dgg;
        for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { der[i] = 0; gg[i] = 0; }
        for (i = 1; i <= dhsf; i++) { der[i - 1] = hsf[i] * (int64_t)i; }
        dder = dhsf - 1;
        for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { gg[i] = hsf[i]; }
        dgg = dhsf;
        rc = sy_poly_gcd(gg, &dgg, der, &dder);
        if (rc != 0) { return rc; }
        if (dgg >= 1) {
            int64_t q[AC_PRO_RAT_MAX_DEG + 1];
            int dq;
            rc = sy_exact_div(hsf, dhsf, gg, dgg, q, &dq);
            if (rc != 0) { return rc; }
            for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { hsf[i] = q[i]; }
            dhsf = dq;
        }
    }
    if (dhsf < 1) { return AC_PRO_SYS_EINTERNAL; }

    /* [4] h_sf 求根(契约 §4.5 路由) */
    {
        int64_t hsf2[AC_PRO_RAT_MAX_DEG + 1];
        for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { hsf2[i] = hsf[i]; }
        rc = sy_roots_int(hsf2, dhsf, yre, yim);
        if (rc < 0) { return rc; }
        ny = rc;
    }

    /* [5] 解耦交叉积 或 耦合代回(契约 §4.6/§4.7) */
    if (decoupled) {
        if (!has_x_el) { return AC_PRO_SYS_EZDIM; }   /* 只有 h:x 自由,维数 ≥ 1 */
        {
            int64_t xc2[AC_PRO_RAT_MAX_DEG + 1];
            for (i = 0; i <= AC_PRO_RAT_MAX_DEG; i++) { xc2[i] = xc[i]; }
            rc = sy_roots_int(xc2, dxc, xre, xim);
            if (rc < 0) { return rc; }
            nx = rc;
        }
        if (nx * ny > SY_CAND_MAX) { return AC_PRO_SYS_EINTERNAL; }
        for (i = 0; i < nx; i++) {
            for (j = 0; j < ny; j++) {
                sxre[nsol] = xre[i]; sxim[nsol] = xim[i];
                syre[nsol] = yre[j]; syim[nsol] = yim[j];
                nsol++;
            }
        }
    } else {
        /* 耦合:逐 y 根 r 代回全部混合元素(契约 §4.7) */
        for (j = 0; j < ny; j++) {
            double pwre[SY_ROOTS + 1];
            double pwim[SY_ROOTS + 1];
            int all_zero = 1;

            pwre[0] = 1.0; pwim[0] = 0.0;
            for (e = 1; e <= SY_ROOTS; e++) {
                pwre[e] = pwre[e - 1] * yre[j] - pwim[e - 1] * yim[j];
                pwim[e] = pwre[e - 1] * yim[j] + pwim[e - 1] * yre[j];
            }

            for (i = 0; i < nmixed && all_zero; i++) {
                double pre[SY_ROOTS + 1];
                double pim[SY_ROOTS + 1];
                double mx = 0.0;
                int nonzero = 0;
                for (e = 0; e <= SY_ROOTS; e++) { pre[e] = 0.0; pim[e] = 0.0; }
                for (t = 0; t < mixed[i].n; t++) {
                    int ex = sy_ex_of(mixed[i].ep[t]);
                    int ey = sy_ey_of(mixed[i].ep[t]);
                    double c = (double)mixed[i].c[t];
                    pre[ex] += c * pwre[ey];
                    pim[ex] += c * pwim[ey];
                }
                for (e = 0; e <= SY_ROOTS; e++) {
                    double m = sqrt(pre[e] * pre[e] + pim[e] * pim[e]);
                    if (m > mx) { mx = m; }
                }
                for (e = 0; e <= SY_ROOTS; e++) {
                    double m = sqrt(pre[e] * pre[e] + pim[e] * pim[e]);
                    if (m > 1e-9 * ((mx > 1.0) ? mx : 1.0)) { nonzero = 1; break; }
                }
                if (nonzero) { all_zero = 0; }
            }
            if (all_zero) { return AC_PRO_SYS_EZDIM; }   /* 直线 {y=r} ⊆ V(I) */

            for (i = 0; i < nmixed; i++) {
                double pre[SY_ROOTS + 1];
                double pim[SY_ROOTS + 1];
                double cre[SY_ROOTS + 1];
                double cim[SY_ROOTS + 1];
                double mx = 0.0;
                int deg = -1;
                for (e = 0; e <= SY_ROOTS; e++) { pre[e] = 0.0; pim[e] = 0.0; cre[e] = 0.0; cim[e] = 0.0; }
                for (t = 0; t < mixed[i].n; t++) {
                    int ex = sy_ex_of(mixed[i].ep[t]);
                    int ey = sy_ey_of(mixed[i].ep[t]);
                    double c = (double)mixed[i].c[t];
                    pre[ex] += c * pwre[ey];
                    pim[ex] += c * pwim[ey];
                }
                for (e = 0; e <= SY_ROOTS; e++) {
                    double m = sqrt(pre[e] * pre[e] + pim[e] * pim[e]);
                    if (m > mx) { mx = m; }
                }
                /* 首项剥离(相对该多项式自身 max|c|)后取次数(契约 §4.7) */
                for (e = SY_ROOTS; e >= 0; e--) {
                    double m = sqrt(pre[e] * pre[e] + pim[e] * pim[e]);
                    if (m > 1e-9 * mx) { deg = e; break; }
                }
                if (deg < 1) { continue; }   /* 退化常数/零:不约束 x */
                for (e = 0; e <= deg; e++) { cre[e] = pre[e]; cim[e] = pim[e]; }
                int dkr = sy_dk_complex(cre, cim, deg, xre, xim);
                            if (dkr != 0) {
                    return AC_PRO_SYS_EINTERNAL;
                }
                if (nsol + deg > SY_CAND_MAX) { return AC_PRO_SYS_EINTERNAL; }
                for (e = 0; e < deg; e++) {
                    sxre[nsol] = xre[e]; sxim[nsol] = xim[e];
                    syre[nsol] = yre[j]; syim[nsol] = yim[j];
                    nsol++;
                }
            }
        }
    }

    /* [6] 残差回验 + 去重(契约 §4.8/§5.3;任一候选不过 → EVERIFY 全局失败) */
    for (k = 0; k < nsol; k++) {
        double pwxe[SY_ROOTS + 1];
        double pwxi[SY_ROOTS + 1];
        double pwye[SY_ROOTS + 1];
        double pwyi[SY_ROOTS + 1];
        const sy_terms_t *gr[2];
        int gi;
        int dup = 0;

        pwxe[0] = 1.0; pwxi[0] = 0.0;
        pwye[0] = 1.0; pwyi[0] = 0.0;
        for (e = 1; e <= SY_ROOTS; e++) {
            pwxe[e] = pwxe[e - 1] * sxre[k] - pwxi[e - 1] * sxim[k];
            pwxi[e] = pwxe[e - 1] * sxim[k] + pwxi[e - 1] * sxre[k];
            pwye[e] = pwye[e - 1] * syre[k] - pwyi[e - 1] * syim[k];
            pwyi[e] = pwye[e - 1] * syim[k] + pwyi[e - 1] * syre[k];
        }
        gr[0] = &fterms;
        gr[1] = &gterms;
        for (gi = 0; gi < 2; gi++) {
            double re = 0.0;
            double im = 0.0;
            double scale = 0.0;
            double resid;
            for (t = 0; t < gr[gi]->n; t++) {
                int ex = sy_ex_of(gr[gi]->ep[t]);
                int ey = sy_ey_of(gr[gi]->ep[t]);
                double cr = (double)gr[gi]->c[t];
                double xr = pwxe[ex], xi = pwxi[ex];
                double yr = pwye[ey], yi = pwyi[ey];
                /* 复数乘积 x^ex·y^ey:交叉项不可省(两侧因子皆复时,
                   漏交叉项 = 残差虚高 0.1+ 量级,实测踩坑) */
                double ax = sqrt(xr * xr + xi * xi);
                double ay = sqrt(yr * yr + yi * yi);
                re += cr * (xr * yr - xi * yi);
                im += cr * (xr * yi + xi * yr);
                /* 契约 §4.8:scale = Σ|c|·|x|^i·|y|^j */
                scale += fabs(cr) * ax * ay;
            }
            resid = sqrt(re * re + im * im) / ((scale > 1.0) ? scale : 1.0);
                    if (!(resid <= 1e-9)) {
                return AC_PRO_SYS_EVERIFY;
            }
        }
        /* 回验通过 → 去重插入(保留先插入者,契约 §5.3) */
        for (i = 0; i < nacc && !dup; i++) {
            if (fabs(vre[i] - sxre[k]) <= SY_EPS &&
                fabs(vire[i] - sxim[k]) <= SY_EPS &&
                fabs(vjre[i] - syre[k]) <= SY_EPS &&
                fabs(vjim[i] - syim[k]) <= SY_EPS) {
                dup = 1;
            }
        }
        if (!dup) {
            if (nacc >= SY_SOL_CAP) { return AC_PRO_SYS_ESOLT; }
            vre[nacc] = sxre[k];
            vire[nacc] = sxim[k];
            vjre[nacc] = syre[k];
            vjim[nacc] = syim[k];
            nacc++;
        }
    }
    nsol = nacc;

    /* [7] 排序(契约 §5.2:y Re 降序 → y Im 降序 → x Re 降序 → x Im 降序,
     * 1e-9 并列;插入排序稳定) */
    for (i = 1; i < nsol; i++) {
        double xr = vre[i], xi = vire[i], yr = vjre[i], yi = vjim[i];
        j = i - 1;
        while (j >= 0) {
            int less = 0;
            if (vjre[j] < yr - SY_EPS) { less = 1; }
            else if (fabs(vjre[j] - yr) <= SY_EPS) {
                if (vjim[j] < yi - SY_EPS) { less = 1; }
                else if (fabs(vjim[j] - yi) <= SY_EPS) {
                    if (vre[j] < xr - SY_EPS) { less = 1; }
                    else if (fabs(vre[j] - xr) <= SY_EPS) {
                        if (vire[j] < xi - SY_EPS) { less = 1; }
                    }
                }
            }
            if (!less) { break; }
            vre[j + 1] = vre[j]; vire[j + 1] = vire[j];
            vjre[j + 1] = vjre[j]; vjim[j + 1] = vjim[j];
            j--;
        }
        vre[j + 1] = xr; vire[j + 1] = xi;
        vjre[j + 1] = yr; vjim[j + 1] = yi;
    }

    /* [8] 渲染(契约 §5.4/§6) */
    {
        int used = 0;
        if (sy_puts(out, out_len, &used, "[") != 0) { return AC_PRO_SYS_EOVER; }
        for (k = 0; k < nsol; k++) {
            char cx[SY_FMT_MAX * 2];
            char cy[SY_FMT_MAX * 2];
            if (k > 0) {
                if (sy_puts(out, out_len, &used, ", ") != 0) { return AC_PRO_SYS_EOVER; }
            }
            if (sy_fmt_coord(vre[k], vire[k], cx, (int)sizeof(cx)) != 0) { return AC_PRO_SYS_EINTERNAL; }
            if (sy_fmt_coord(vjre[k], vjim[k], cy, (int)sizeof(cy)) != 0) { return AC_PRO_SYS_EINTERNAL; }
            if (sy_puts(out, out_len, &used, "(") != 0) { return AC_PRO_SYS_EOVER; }
            if (sy_puts(out, out_len, &used, cx) != 0) { return AC_PRO_SYS_EOVER; }
            if (sy_puts(out, out_len, &used, ", ") != 0) { return AC_PRO_SYS_EOVER; }
            if (sy_puts(out, out_len, &used, cy) != 0) { return AC_PRO_SYS_EOVER; }
            if (sy_puts(out, out_len, &used, ")") != 0) { return AC_PRO_SYS_EOVER; }
        }
        if (sy_puts(out, out_len, &used, "]") != 0) { return AC_PRO_SYS_EOVER; }
    }
    return AC_PRO_SYS_OK;
}

#endif /* AC_PRO_ACTIVE */
