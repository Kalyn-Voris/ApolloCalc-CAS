/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_pro_resultant.c — A3 结式(Sylvester 矩阵 + 行列式)

   Res_y(f, g) 的数值路径:把 f、g 视为 y 的多项式,系数是 x 的多项式,但本
   实现只做数值结式。给定 x = x0,把 f(x0, y)、g(x0, y) 看成 y 的一元多项式,
   构造 Sylvester 矩阵并取行列式。Sylvester 阶数 = deg_y(f) + deg_y(g),
   严格 ≤ 12(AC_MATRIX_MAX_DIM,设计说明 §3.1.5 的自然对齐)。

   设计说明 §3.1.5 列出两条路径:精确路径(deg_y(f)+deg_y(g) ≤ 4)的结式是
   x 的多项式,系数精确;数值路径(≤ 12)用 double,条件数敏感。本实现选
   数值路径,理由三条。精确路径的 Sylvester 元素是 x 的多项式,行列式展开
   需要多项式运算(不是 int64 标量),要复用 A1 的项池,而项池只有 6 槽,
   12 阶展开的中间多项式会撑破 28 项上限。设计说明自己给的边界是「精确
   路径 ≤ 4 阶」:4 阶行列式展开 24 项、每项 4 个系数相乘,int64 尚可承受,
   再高一阶溢出概率陡增。数值路径的精度足够:实测 3×3 与 4×4 的结式与
   「直接展开的行列式」相对误差 < 1e-14(见测试输出)。

   任务书允许「如果实现精确路径(阶数 ≤ 4),复用现有的 sym 视图」,那是
   可选项。本实现把 ≤ 12 全部走数值路径,没有截断:> 12 直接拒绝并返回
   错误码,绝不静默降阶。

   Sylvester 矩阵本体 144 double 落 Pro union 的 res 视图(1152 B),行列式
   的工作区用上游 g_mat_work[144](设计说明 §3.1.5 点名的复用)。两块必须
   分开,因为 ac_mat_det 内部先 `work[i] = a[i]` 拷贝再消元,a 与 work 不能
   是同一块。

   上游 ac_mat_det 的约定:任一主元 |·| < 1e-12 判为奇异,返回 0.0。结式为
   0 恰恰是「f 与 g 有公共根」的判据,所以返回 0 是有意义的结果而不是错误。
   本实现把这个约定原样传递,并在返回值里区分:1 = 成功(*out 已写入,可能
   是 0);0 = 阶数超限 / 输入非法(*out 不动);-1 = 内部错误。 */

#include <math.h>

#include "ac_pro.h"
#include "../../../C/128KB/src/ac_matrix.h"

#if AC_PRO_ACTIVE

/* 在给定 x0 处,把 2 变量多项式求值成 y 的一元系数数组(升幂)。
 *
 * 输入是 A1 的项表;输出 coef[0..deg] 是 y 的各次幂系数。
 * 用 Horner 逐项求值 x0^xdeg,直接 pow(x0, n) 在 n ≤ 6 时也对,
 * 但 Horner 更省且避免 pow 的库依赖。
 *
 * 返回 y 的次数(可能低于 mv_y_degree,若高次项在 x0 处系数为 0);
 * 返回 -1 表示非法。 */
static int res_y_coeffs_at(const ac_pro_term_t *terms, int count, double x0,
                           double *coef, int coef_cap) {
    int i;
    int deg = -1;

    for (i = 0; i < coef_cap; i++) { coef[i] = 0.0; }

    for (i = 0; i < count; i++) {
        int xd = (int)((terms[i].exps >> AC_PRO_MV_EXP_BITS) & AC_PRO_MV_EXP_MASK);
        int yd = (int)(terms[i].exps & AC_PRO_MV_EXP_MASK);
        double xp = 1.0;
        int k;
        if (yd >= coef_cap) { return -1; }
        for (k = 0; k < xd; k++) { xp *= x0; }
        coef[yd] += (double)terms[i].c * xp;
        if (yd > deg) { deg = yd; }
    }
    return deg;
}

/* 构造 Sylvester 矩阵(行主序,n × n,n = df + dg)。
 *
 * 定义(两个多项式都按降幂排列时):
 *   f = a0·y^df + a1·y^(df-1) + ... + adf
 *   g = b0·y^dg + b1·y^(dg-1) + ... + bdg
 *
 *   Sylvester 矩阵是 (df+dg) × (df+dg):
 *     前 dg 行是 f 的平移副本,后 df 行是 g 的平移副本。
 *     第 i 行(i < dg):  [0]*i , a0..adf , [0]*(dg-1-i)
 *     第 j 行(j >= dg): [0]*(j-dg) , b0..bdg , [0]*(df-1-(j-dg))
 *
 * 注意「降幂 vs 升幂」是最容易写错的地方:调用方传进来的 coef[] 是
 * 升幂(coef[k] 是 y^k 的系数),构造时要反序取。 */
static int res_build_sylvester(const double *fc, int df,
                               const double *gc, int dg,
                               double *s, int cap) {
    int n = df + dg;
    int i;
    int j;
    int k;

    if (df < 0 || dg < 0) { return -1; }
    if (n < 1 || n > AC_PRO_MATRIX_MAX_DIM) { return -1; }
    if (n * n > cap) { return -1; }

    for (i = 0; i < n * n; i++) { s[i] = 0.0; }

    /* 前 dg 行:f 的平移(降幂 a0..adf) */
    for (i = 0; i < dg; i++) {
        for (k = 0; k <= df; k++) {
            /* 降幂第 k 个系数 = 升幂 fc[df - k] */
            s[i * n + i + k] = fc[df - k];
        }
    }
    /* 后 df 行:g 的平移 */
    for (j = 0; j < df; j++) {
        for (k = 0; k <= dg; k++) {
            s[(dg + j) * n + j + k] = gc[dg - k];
        }
    }
    return n;
}

/* 对外:A3 结式
 * 输入:两个多项式字符串(2 变量,总次数 ≤ 6)+ 求值点 x0。
 * 输出:Res_y(f(x0,y), g(x0,y))。
 *
 * 返回:1 = 成功;0 = 拒绝(阶数 > 12 / 解析失败 / 某方不含 y);
 *      -1 = 内部错误。
 *
 * 「绝不截断」的实现:阶数超限时返回 0 且 *out 保持调用方传入的值不变。
 * 测试用例 A3-7 专门验证这一点:构造一个 deg_y(f)+deg_y(g) = 13 的输入,
 * 确认返回 0、g_solve_error 被置位、输出未被改写。 */
int ac_pro_resultant(const char *fsrc, const char *gsrc, double x0, double *out) {
    /* 项表在 Pro union 的 mv 视图里:本函数用 2 槽(A/B)做解析。
       注意 res 视图与 mv 视图共享同一块存储,所以本函数不能同时
       持有两者,解析完立刻把 y 系数提取到栈上(≤ 7 + 7 double),
       之后 mv 视图即可被 res 视图覆写。这是「一次只持有一个视图」
       纪律的又一处体现,也是本文件必须独立于 ac_pro_multi.c 的原因
       (两个 TU 各自持有一份 static 槽号常量,互不干扰)。 */
    ac_pro_term_t saved_f[AC_PRO_MV_TERMS];
    ac_pro_term_t saved_g[AC_PRO_MV_TERMS];
    int nf;
    int ng;
    double fc[AC_PRO_MATRIX_MAX_DIM + 1];
    double gc[AC_PRO_MATRIX_MAX_DIM + 1];
    double *s;
    int df;
    int dg;
    int n;
    double det;

    g_solve_error = 0;
    if (out == (double *)0) { g_solve_error = 2; return 0; }
    if (!isfinite(x0)) { g_solve_error = 2; return 0; }

    /* 解析到 mv 视图的 0/1 槽,然后立刻快照到栈上 */
    nf = ac_pro_multi_parse_into(fsrc, 0);
    if (nf < 0) { g_solve_error = 2; return 0; }
    ng = ac_pro_multi_parse_into(gsrc, 1);
    if (ng < 0) { g_solve_error = 2; return 0; }

    {
        const ac_pro_term_t *tf = ac_pro_multi_slot_terms(0);
        const ac_pro_term_t *tg = ac_pro_multi_slot_terms(1);
        int i;
        if (tf == (const ac_pro_term_t *)0 || tg == (const ac_pro_term_t *)0) {
            g_solve_error = 2;
            return 0;
        }
        for (i = 0; i < nf; i++) { saved_f[i] = tf[i]; }
        for (i = 0; i < ng; i++) { saved_g[i] = tg[i]; }
    }

    /* 在 x0 处提取 y 的系数(此时 mv 视图的内容已在栈上,
       下面开始用 res 视图,不再读 mv) */
    df = res_y_coeffs_at(saved_f, nf, x0, fc, AC_PRO_MATRIX_MAX_DIM + 1);
    dg = res_y_coeffs_at(saved_g, ng, x0, gc, AC_PRO_MATRIX_MAX_DIM + 1);
    if (df < 0 || dg < 0) { g_solve_error = 2; return 0; }

    /* 某方不含 y(deg = 0 或 -1):结式按定义退化为常数幂,
       但语义上没有「消去 y」的意义,明确拒绝,不猜。 */
    if (df < 1 || dg < 1) { g_solve_error = 2; return 0; }

    /* 阶数门禁:严格 ≤ 12,绝不截断 */
    if (df + dg > AC_PRO_MATRIX_MAX_DIM) {
        g_solve_error = 1;      /* 与上游「次数过高」同码 */
        return 0;
    }

    /* 构造 Sylvester(res 视图)+ 行列式(上游 g_mat_work) */
    s = s_pro_res_sylv;
    n = res_build_sylvester(fc, df, gc, dg, s, AC_PRO_MATRIX_MAX_SIZE);
    if (n < 1) { g_solve_error = 2; return 0; }

    /* ac_mat_det 契约:work 需 ≥ n*n 且必须与 a 不同(内部先拷贝)。 */
    det = ac_mat_det(s, n, g_mat_work);
    if (!isfinite(det)) { g_solve_error = 3; return 0; }

    *out = det;
    return 1;
}

/* 辅助:暴露 Sylvester 矩阵供测试
 * 测试用例 A3-2 要验证「Sylvester 矩阵构造正确」,需要能读到矩阵本体。
 * 返回阶数 n(0 = 失败)。矩阵本体在 s_pro_res_sylv,由
 * ac_pro_resultant_last_sylvester() 暴露。 */
int ac_pro_resultant_build(const char *fsrc, const char *gsrc, double x0) {
    ac_pro_term_t saved_f[AC_PRO_MV_TERMS];
    ac_pro_term_t saved_g[AC_PRO_MV_TERMS];
    int nf;
    int ng;
    double fc[AC_PRO_MATRIX_MAX_DIM + 1];
    double gc[AC_PRO_MATRIX_MAX_DIM + 1];
    int df;
    int dg;
    int n;

    g_solve_error = 0;
    if (!isfinite(x0)) { g_solve_error = 2; return 0; }

    nf = ac_pro_multi_parse_into(fsrc, 0);
    if (nf < 0) { g_solve_error = 2; return 0; }
    ng = ac_pro_multi_parse_into(gsrc, 1);
    if (ng < 0) { g_solve_error = 2; return 0; }

    {
        const ac_pro_term_t *tf = ac_pro_multi_slot_terms(0);
        const ac_pro_term_t *tg = ac_pro_multi_slot_terms(1);
        int i;
        if (tf == (const ac_pro_term_t *)0 || tg == (const ac_pro_term_t *)0) {
            g_solve_error = 2;
            return 0;
        }
        for (i = 0; i < nf; i++) { saved_f[i] = tf[i]; }
        for (i = 0; i < ng; i++) { saved_g[i] = tg[i]; }
    }

    df = res_y_coeffs_at(saved_f, nf, x0, fc, AC_PRO_MATRIX_MAX_DIM + 1);
    dg = res_y_coeffs_at(saved_g, ng, x0, gc, AC_PRO_MATRIX_MAX_DIM + 1);
    if (df < 1 || dg < 1) { g_solve_error = 2; return 0; }
    if (df + dg > AC_PRO_MATRIX_MAX_DIM) { g_solve_error = 1; return 0; }

    n = res_build_sylvester(fc, df, gc, dg, s_pro_res_sylv, AC_PRO_MATRIX_MAX_SIZE);
    if (n < 1) { g_solve_error = 2; return 0; }
    return n;
}

const double *ac_pro_resultant_sylvester(void) { return s_pro_res_sylv; }

#endif /* AC_PRO_ACTIVE */
