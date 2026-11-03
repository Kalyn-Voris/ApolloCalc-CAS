/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_pro_complex_ext.c — v2.5.0 功能 F1 复变函数扩展（15 API）

   契约：docs/contracts/complex-ext-v25.md（本文件是其 C 侧逐字镜像；JS 侧
   core_complex.js 同表实现）。公式表/奇点表/溢出语义以契约 §4/§5/§6 为唯一
   出处，此处注释只标注对应条目号，不复制定义。

   寄存器协议（契约 §3）：输入 g_c_a_*(单操作数) / g_c_a_*+g_c_b_*(cpow)，
   结果写 g_c_tmp_*。仅 ac_pro_cpow 覆写 A（暂存 w·ln z，沿袭 ac_c_pow_real
   同款约定）；其余 14 API 不改写 A/B。

   Zero-GC 纪律：零 static 可变对象（Lanczos 系数 static const 落 .rodata）、
   零 malloc、零 VLA、零递归——cgamma 的反射式经 cx_gamma_direct 直算因子
   （x < 0.5 时以 Γ(1−z) 直算支组合，无自调用，ac_special.c 同款结构），
   反函数经 casin/catan 静态因子组合而非自调用。错误码一律走返回值
   （一个 static = TU 净 +32 B .bss，会破坏 42608/42640 门禁）。

   带符号零：csqrt(H2) 的第三支用 1/q < 0 区分 ±0（无 signbit 依赖、C/JS
   可移植同构）；割线 ±0 侧语义见契约 §9 实测锚。 */

#include <math.h>

#include "ac_pro_config.h"
#include "ac_pro.h"

#if AC_PRO_ACTIVE

#include "../../../C/128KB/src/ac_complex.h"

/* ---- 契约 §4 常量（与 M_PI/Math.PI 同为 float64 的 3.141592653589793） ---- */
#define CX_PI          3.141592653589793
#define CX_HALF_PI     1.5707963267948966   /* PI/2 精确折半 */
#define CX_LN_SQRT_2PI 0.9189385332046727

/* 契约 §4 Lanczos 系数表（g=7/n=9，与上游 ac_special.c / core_special.js 同表）。
 * static const：加载期初始化，落 .rodata，不进 .bss 账本。 */
static const double CX_LANCZOS[9] = {
    0.99999999999980993,
    676.5203681218851,
    -1259.1392167224028,
    771.32342877765313,
    -176.61502916214059,
    12.507343278686905,
    -0.13857109526572012,
    9.9843695780195716e-6,
    1.5056327351493116e-7
};

/* ---- 结果终检（契约 §6）：TMP 全有限 → OK，否则 EOVER ---- */

static int cx_tmp_finite(void) {
    if (!isfinite(g_c_tmp_re) || !isfinite(g_c_tmp_im)) { return 0; }
    return 1;
}

/* 输入非有限 → EDOM（契约 §5 首条） */
static int cx_input_finite(double x, double y) {
    if (!isfinite(x) || !isfinite(y)) { return 0; }
    return 1;
}

/* ---- H1 cln(p,q)：仅 (p,q) != (0,0) 可调（各 API 调用前已排除） ---- */

static void cx_ln(double p, double q, double *lr, double *li) {
    *lr = 0.5 * log(p * p + q * q);
    *li = atan2(q, p);
}

/* ---- H2 csqrt(p,q)：符号零敏感（契约 §4） ---- */

static void cx_sqrt(double p, double q, double *sr, double *si) {
    double r, t, s;
    if (p == 0.0 && q == 0.0) {
        *sr = 0.0;
        *si = q;                /* 保 ±0：csqrt(±0±i0) = 0 侧共轭成对 */
        return;
    }
    r = sqrt(p * p + q * q);
    if (p >= 0.0) {
        t = sqrt((r + p) / 2.0);
        *sr = t;
        *si = q / (2.0 * t);    /* q=±0 时符号随除法保真 */
    } else {
        t = sqrt((r - p) / 2.0);
        s = (1.0 / q < 0.0) ? -t : t;   /* 1/q 区分 ±0，无 signbit 依赖 */
        *sr = fabs(q) / (2.0 * t);
        *si = s;
    }
}

/* ---- H3 cexp(p,q) ---- */

static void cx_exp(double p, double q, double *er, double *ei) {
    double ea = exp(p);
    *er = ea * cos(q);
    *ei = ea * sin(q);
}

/* ---- H4 cdiv(a,b)/(c,d)：den = c²+d²（域上限见契约 §6） ---- */

static void cx_div(double ar, double ai, double br, double bi,
                   double *qr, double *qi) {
    double den = br * br + bi * bi;
    *qr = (ar * br + ai * bi) / den;
    *qi = (ai * br - ar * bi) / den;
}

/* ==================== 整函数族（契约 §4 #1-#6） ==================== */

int ac_pro_csin(void) {
    double x = g_c_a_re, y = g_c_a_im;
    if (!cx_input_finite(x, y)) { return AC_PRO_CX_EDOM; }
    g_c_tmp_re = sin(x) * cosh(y);
    g_c_tmp_im = cos(x) * sinh(y);
    return cx_tmp_finite() ? AC_PRO_CX_OK : AC_PRO_CX_EOVER;
}

int ac_pro_ccos(void) {
    double x = g_c_a_re, y = g_c_a_im;
    if (!cx_input_finite(x, y)) { return AC_PRO_CX_EDOM; }
    g_c_tmp_re = cos(x) * cosh(y);
    g_c_tmp_im = -sin(x) * sinh(y);
    return cx_tmp_finite() ? AC_PRO_CX_OK : AC_PRO_CX_EOVER;
}

int ac_pro_ctan(void) {
    /* 契约 §4 #3 混合式（r2 因式分解形），阈值 1.0 冻结：|y|≤1 比值形（避开
     * 实极点灾难性消去）；|y|>1 按 cosh(2y) 因式分解（与 sinh(2y)/(cos(2x)+
     * cosh(2y)) 代数恒等），sech ∈ [0,1] 恒不溢出，den ≥ 1−1/cosh(2) > 0。 */
    double x = g_c_a_re, y = g_c_a_im;
    double sr, si, cr, ci, sech, c, den;
    if (!cx_input_finite(x, y)) { return AC_PRO_CX_EDOM; }
    if (fabs(y) <= 1.0) {
        sr = sin(x) * cosh(y);
        si = cos(x) * sinh(y);
        cr = cos(x) * cosh(y);
        ci = -sin(x) * sinh(y);
        cx_div(sr, si, cr, ci, &g_c_tmp_re, &g_c_tmp_im);
    } else {
        sech = 1.0 / cosh(2.0 * y);
        c = cos(2.0 * x) * sech;
        den = 1.0 + c;
        g_c_tmp_re = sin(2.0 * x) * sech / den;
        g_c_tmp_im = tanh(2.0 * y) / den;
    }
    return cx_tmp_finite() ? AC_PRO_CX_OK : AC_PRO_CX_EOVER;
}

int ac_pro_csinh(void) {
    double x = g_c_a_re, y = g_c_a_im;
    if (!cx_input_finite(x, y)) { return AC_PRO_CX_EDOM; }
    g_c_tmp_re = sinh(x) * cos(y);
    g_c_tmp_im = cosh(x) * sin(y);
    return cx_tmp_finite() ? AC_PRO_CX_OK : AC_PRO_CX_EOVER;
}

int ac_pro_ccosh(void) {
    double x = g_c_a_re, y = g_c_a_im;
    if (!cx_input_finite(x, y)) { return AC_PRO_CX_EDOM; }
    g_c_tmp_re = cosh(x) * cos(y);
    g_c_tmp_im = sinh(x) * sin(y);
    return cx_tmp_finite() ? AC_PRO_CX_OK : AC_PRO_CX_EOVER;
}

int ac_pro_ctanh(void) {
    /* 契约 §4 #6：与 ctan 对称的混合式 + cosh(2x) 因式分解（r2），阈值 1.0 冻结。 */
    double x = g_c_a_re, y = g_c_a_im;
    double sr, si, cr, ci, sech, c, den;
    if (!cx_input_finite(x, y)) { return AC_PRO_CX_EDOM; }
    if (fabs(x) <= 1.0) {
        sr = sinh(x) * cos(y);
        si = cosh(x) * sin(y);
        cr = cosh(x) * cos(y);
        ci = sinh(x) * sin(y);
        cx_div(sr, si, cr, ci, &g_c_tmp_re, &g_c_tmp_im);
    } else {
        sech = 1.0 / cosh(2.0 * x);
        c = cos(2.0 * y) * sech;
        den = 1.0 + c;
        g_c_tmp_re = tanh(2.0 * x) / den;
        g_c_tmp_im = sin(2.0 * y) * sech / den;
    }
    return cx_tmp_finite() ? AC_PRO_CX_OK : AC_PRO_CX_EOVER;
}

/* ==================== casin 静态因子（契约 §4 #7） ====================
 * 输入 (x,y)，输出 TMP。casin/casinh 共用；casinh 以 iw=(−y,x) 直取后调本
 * 因子（无自调用）。 */

static void cx_asin_core(double x, double y) {
    double tre, tim, wr, wi, uz, vi, lr, li;
    tre = 1.0 - x * x + y * y;
    tim = -2.0 * x * y;
    cx_sqrt(tre, tim, &wr, &wi);
    uz = -y + wr;
    vi = x + wi;
    cx_ln(uz, vi, &lr, &li);
    g_c_tmp_re = li;
    g_c_tmp_im = -lr;
}

int ac_pro_casin(void) {
    double x = g_c_a_re, y = g_c_a_im;
    if (!cx_input_finite(x, y)) { return AC_PRO_CX_EDOM; }
    cx_asin_core(x, y);
    return cx_tmp_finite() ? AC_PRO_CX_OK : AC_PRO_CX_EOVER;
}

int ac_pro_cacos(void) {
    double x = g_c_a_re, y = g_c_a_im;
    double ur, ui;
    if (!cx_input_finite(x, y)) { return AC_PRO_CX_EDOM; }
    cx_asin_core(x, y);
    ur = g_c_tmp_re;
    ui = g_c_tmp_im;
    g_c_tmp_re = CX_HALF_PI - ur;
    g_c_tmp_im = -ui;
    return cx_tmp_finite() ? AC_PRO_CX_OK : AC_PRO_CX_EOVER;
}

/* ==================== catan 静态因子（契约 §4 #9） ====================
 * 输入 (x,y)。catan/catanh 共用；支点判据由调用方负责（catanh 经 iw 落 ±i）。 */

static int cx_atan_core(double x, double y) {
    double num, den;
    if (x == 0.0 && (y == 1.0 || y == -1.0)) {
        return AC_PRO_CX_EDOM;          /* 支点 ±i（契约 §5） */
    }
    num = x * x + (y + 1.0) * (y + 1.0);
    den = x * x + (y - 1.0) * (y - 1.0);
    g_c_tmp_re = 0.5 * atan2(2.0 * x, 1.0 - x * x - y * y);
    g_c_tmp_im = 0.25 * (log(num) - log(den));
    return cx_tmp_finite() ? AC_PRO_CX_OK : AC_PRO_CX_EOVER;
}

int ac_pro_catan(void) {
    double x = g_c_a_re, y = g_c_a_im;
    if (!cx_input_finite(x, y)) { return AC_PRO_CX_EDOM; }
    return cx_atan_core(x, y);
}

/* ==================== 反函数组合（契约 §4 #10-#12） ==================== */

int ac_pro_casinh(void) {
    /* asinh(z) = −i·asin(iz)，iw = (−y, x) 直取保 ±0。 */
    double x = g_c_a_re, y = g_c_a_im;
    double ur, ui;
    if (!cx_input_finite(x, y)) { return AC_PRO_CX_EDOM; }
    cx_asin_core(-y, x);
    ur = g_c_tmp_re;
    ui = g_c_tmp_im;
    g_c_tmp_re = ui;
    g_c_tmp_im = -ur;
    return cx_tmp_finite() ? AC_PRO_CX_OK : AC_PRO_CX_EOVER;
}

int ac_pro_cacosh(void) {
    /* acosh(z) = cln(z + csqrt(z−1)·csqrt(z+1))，z±1 分量直取保 ±0。 */
    double x = g_c_a_re, y = g_c_a_im;
    double p, q, m, n, pr, pi, lr, li;
    if (!cx_input_finite(x, y)) { return AC_PRO_CX_EDOM; }
    cx_sqrt(x - 1.0, y, &p, &q);
    cx_sqrt(x + 1.0, y, &m, &n);
    pr = p * m - q * n;
    pi = p * n + q * m;
    cx_ln(x + pr, y + pi, &lr, &li);
    g_c_tmp_re = lr;
    g_c_tmp_im = li;
    return cx_tmp_finite() ? AC_PRO_CX_OK : AC_PRO_CX_EOVER;
}

int ac_pro_catanh(void) {
    /* atanh(z) = −i·atan(iz)；z=±1 的支点经 iw=±i 落入 cx_atan_core 判据。 */
    double x = g_c_a_re, y = g_c_a_im;
    int rc;
    double ur, ui;
    if (!cx_input_finite(x, y)) { return AC_PRO_CX_EDOM; }
    rc = cx_atan_core(-y, x);
    if (rc != AC_PRO_CX_OK) { return rc; }
    ur = g_c_tmp_re;
    ui = g_c_tmp_im;
    g_c_tmp_re = ui;
    g_c_tmp_im = -ur;
    return AC_PRO_CX_OK;
}

/* ==================== cpow（契约 §4 #13，覆写 A） ==================== */

int ac_pro_cpow(void) {
    double x = g_c_a_re, y = g_c_a_im;
    double u = g_c_b_re, v = g_c_b_im;
    double lr, li;
    if (!cx_input_finite(x, y) || !cx_input_finite(u, v)) {
        return AC_PRO_CX_EDOM;
    }
    if (x == 0.0 && y == 0.0) {
        /* 契约 §5 零底约定：0^0:=1；0^正实:=0；其余 EDOM。 */
        if (u == 0.0 && v == 0.0) {
            g_c_tmp_re = 1.0;
            g_c_tmp_im = 0.0;
            return AC_PRO_CX_OK;
        }
        if (u > 0.0 && v == 0.0) {
            g_c_tmp_re = 0.0;
            g_c_tmp_im = 0.0;
            return AC_PRO_CX_OK;
        }
        return AC_PRO_CX_EDOM;
    }
    cx_ln(x, y, &lr, &li);
    /* A 覆写：A := B·cln(z)（契约 §3，沿袭 ac_c_pow_real 约定） */
    g_c_a_re = u * lr - v * li;
    g_c_a_im = u * li + v * lr;
    cx_exp(g_c_a_re, g_c_a_im, &g_c_tmp_re, &g_c_tmp_im);
    return cx_tmp_finite() ? AC_PRO_CX_OK : AC_PRO_CX_EOVER;
}

/* ==================== cnthroot（契约 §4 #14） ==================== */

int ac_pro_cnthroot(int k) {
    double x = g_c_a_re, y = g_c_a_im;
    double r, th, base;
    if (k <= 0) { return AC_PRO_CX_EPARAM; }
    if (!cx_input_finite(x, y)) { return AC_PRO_CX_EDOM; }
    if (x == 0.0 && y == 0.0) {
        g_c_tmp_re = 0.0;
        g_c_tmp_im = 0.0;
        return AC_PRO_CX_OK;
    }
    r = sqrt(x * x + y * y);
    th = atan2(y, x) / (double)k;
    base = pow(r, 1.0 / (double)k);
    g_c_tmp_re = base * cos(th);
    g_c_tmp_im = base * sin(th);
    return cx_tmp_finite() ? AC_PRO_CX_OK : AC_PRO_CX_EOVER;
}

/* ==================== cgamma（契约 §4 #15） ====================
 * 直算因子 x ≥ 0.5；反射支以 Γ(1−z) 直算组合，无自调用。 */

static void cx_gamma_direct(double x, double y, double *gr, double *gi) {
    double ar = CX_LANCZOS[0];
    double ai = 0.0;
    double lr, li, m1r, m1i, lgr, lgi;
    int k;
    for (k = 1; k < 9; k++) {
        double dr = (x - 1.0) + (double)k;
        double qr, qi;
        cx_div(CX_LANCZOS[k], 0.0, dr, y, &qr, &qi);
        ar = ar + qr;
        ai = ai + qi;
    }
    cx_ln(x + 6.5, y, &lr, &li);
    m1r = (x - 0.5) * lr - y * li;
    m1i = (x - 0.5) * li + y * lr;
    lgr = CX_LN_SQRT_2PI + m1r - (x + 6.5) + 0.5 * log(ar * ar + ai * ai);
    lgi = m1i - y + atan2(ai, ar);
    cx_exp(lgr, lgi, gr, gi);
}

int ac_pro_cgamma(void) {
    double x = g_c_a_re, y = g_c_a_im;
    double gr, gi, sx, sy, sr, si, dr2, di2;
    if (!cx_input_finite(x, y)) { return AC_PRO_CX_EDOM; }
    if (y == 0.0 && x <= 0.0 && x == floor(x)) {
        return AC_PRO_CX_EDOM;          /* 非正整数极点（契约 §5） */
    }
    if (x >= 0.5) {
        cx_gamma_direct(x, y, &g_c_tmp_re, &g_c_tmp_im);
    } else {
        /* 反射式 Γ(z) = π / (sin(πz)·Γ(1−z))；1−z 的虚部取 −y（conj）。 */
        sx = CX_PI * x;
        sy = CX_PI * y;
        sr = sin(sx) * cosh(sy);
        si = cos(sx) * sinh(sy);
        cx_gamma_direct(1.0 - x, -y, &gr, &gi);
        dr2 = sr * gr - si * gi;
        di2 = sr * gi + si * gr;
        cx_div(CX_PI, 0.0, dr2, di2, &g_c_tmp_re, &g_c_tmp_im);
    }
    return cx_tmp_finite() ? AC_PRO_CX_OK : AC_PRO_CX_EOVER;
}

#endif /* AC_PRO_ACTIVE */
