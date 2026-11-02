/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_pro_solvers_ext.c — 内存水位门禁重写 + 数值求根 + 次数路由

   阶段一的内存水位门禁重写,阶段二 B1 的数值求根与次数路由。

   为什么是「_ext」而不是新名字:Makefile 的包装清单由 `wildcard ac_*.c`
   推导,每个上游 ac_<mod>.c 都必须有同名包装 src/ac_pro_<mod>.c。本文件是
   额外的 Pro TU,名字不落在包装清单的映射里,只由 Makefile 的 PRO_EXTRA
   显式追加,因此不会被误认为某个上游模块的包装。

   内存纪律:无 malloc,无 VLA,无递归。数值求根的复数工作区全部落在 Pro
   union 的 nr 视图(264 B);系数先拷进 nr.coef,避免调用方缓冲与视图别名。
   唯一「大」的临时量是 8×8 的 Durand-Kerner 乘积,它是栈上标量循环累加,
   不建数组。 */

#include <math.h>

#include "ac_pro.h"
#include "../../../C/128KB/src/ac_parser.h"
#include "../../../C/128KB/src/ac_evaluator.h"

#if AC_PRO_ACTIVE

/* 阶段一①：内存水位门禁
 * 上游 ac_check_memory_watermark（ac_solvers.c:679-724）的估算式基数
 * 11176 B + 动态上限 6144 B = 17320 B，阈值却是 98304 B，比值 5.68×，
 * 任何输入都不可能触发，是死代码。且它漏算两块最大的池：
 *     g_ac_scratch 8224 B + g_sym_nodes 12288 B = 20512 B。
 *
 * Pro 侧改为两段式（设计说明 §6.3）：
 *     used = AC_STATIC_FOOTPRINT（编译期真实静态量）
 *          + 动态项（token / number 游标）
 *          + extra_bytes（测试注入的假池）
 *
 * AC_STATIC_FOOTPRINT = 38672 是内核口径的 `size` 实测值（16 个上游
 * 模块 + trivial main）。它包含了 g_ac_scratch 与 g_sym_nodes，所以上游
 * 那两块漏算在这里被一次性补上。Pro 自己的 3936 B 单独加：
 *     AC_PRO_STATIC_FOOTPRINT = 38672 + 80 + 3848 + 8 = 42608（v2.1.0，sparse 视图定峰）
 * 这两个常量都在 ac_pro_config.h 里，且被下面的编译期断言与 sizeof
 * 硬绑定,不是手抄的数字。 */

/* 账本自证：常量之间必须闭合，且必须等于真实的 sizeof 之和。 */
AC_STATIC_ASSERT(AC_PRO_STATIC_FOOTPRINT == AC_PRO_TARGET_BSS, pro_footprint_matches_target);
AC_STATIC_ASSERT(AC_PRO_STATIC_EXTRA == (int)sizeof(ac_pro_storage_t),
                 pro_extra_matches_real_sizes);
AC_STATIC_ASSERT(AC_STATIC_FOOTPRINT + AC_PRO_STATIC_EXTRA == AC_PRO_TARGET_BSS,
                 pro_ledger_closes);
/* 112 KiB 门禁：静态足迹必须留在门禁内，且留有余量（这条校验让门禁
   从「Makefile 里的一行 echo」变成真正的编译期硬约束）。 */
AC_STATIC_ASSERT(AC_PRO_STATIC_FOOTPRINT <= AC_PRO_GATE_BSS, pro_footprint_under_gate);

/* 动态项：与上游同一口径（每个 token / number 游标 8 B 的保守估算）。 */
double ac_pro_memory_used(double extra_bytes) {
    double used = (double)AC_PRO_STATIC_FOOTPRINT;
    used += (double)(g_token_count << 3);
    used += (double)(g_number_count << 3);
    used += extra_bytes;
    return used;
}

/* extra_bytes 版本：供测试精确钉住门禁边界（设计说明 §9.5 第 3 条：
 * 注入 70000 -> 44816 + 70000 = 114816 > 114688 触发；
 * 注入 69000 -> 113816 < 114688 不触发）。 */
int ac_pro_check_memory_watermark_ex(double extra_bytes) {
    double used = ac_pro_memory_used(extra_bytes);
    g_solve_error = 0;
    if (used > (double)AC_PRO_GATE_BSS) {
        g_solve_error = 4;
        return 1;
    }
    return 0;
}

/* 与上游同签名的对外门禁。注意：它不是上游那个死代码的包装，
 * 而是完整重写,上游版本在 AC_PRO_ACTIVE=0 的退化构建里才生效。 */
int ac_pro_check_memory_watermark(void) {
    return ac_pro_check_memory_watermark_ex(0.0);
}

/* 阶段二 B1：次数路由
 * 上游 ac_guard_degree 硬拒绝 degree > 4（阿贝尔-鲁菲尼）。
 * Pro 改成三态（设计说明 §4.1.3）：
 *     1..4  -> 精确/根式路径（走上游 ac_solve_quadratic/cubic/quartic）
 *     5..8  -> Durand-Kerner 数值路径（本文件）
 *     > 8   -> 拒绝，g_solve_error = 1
 *
 * 上游原件已被 ac_pro_solvers.c 的宏改名为 ac_pro_upstream_guard_degree，
 * 这里显式声明它，保证「1..4 的行为与上游逐位一致」这件事是调用
 * 上游代码实现的，而不是抄一遍判断。 */
int ac_pro_upstream_guard_degree(int degree);

int ac_guard_degree(int degree) {
    if (degree < 1) {
        g_solve_error = 1;
        return 1;
    }
    if (degree <= 4) {
        /* 逐位对齐上游：直接调用上游原件，不复制它的判断逻辑。 */
        return ac_pro_upstream_guard_degree(degree);
    }
    if (degree <= AC_PRO_NUMROOT_MAX_DEG) {
        /* 5..8：放行到数值路径。此处不置 g_solve_error，与上游「放行
           时 error 保持 0」的约定一致；不收敛由数值求解器置 3。 */
        return 0;
    }
    g_solve_error = 1;      /* > 8：与上游同码，明确拒绝 */
    return 1;
}

int ac_pro_degree_route(int degree) {
    if (degree < 1 || degree > AC_PRO_NUMROOT_MAX_DEG) { return -1; }
    return (degree <= 4) ? 0 : 1;
}

/* 阶段二 B1：Durand-Kerner 数值求根
 * 设计说明 §4.1.2 实测：5 次 10–13 次迭代、6 次 15 次迭代收敛到 1e−15 量级，
 * max_iter 取 200 有 13.3× 余量。
 *
 * 迭代式（Weierstrass / Durand-Kerner）：
 *     z_i ← z_i − P(z_i) / Π_{j≠i}(z_i − z_j)
 *
 * 初始值：Aberth 圆盘 R = 1 + max|c_k|/|c_n|，取 z_i = R·e^{2πi(i+0.5)/n}。
 * 用 0.5 偏移而不是从实轴起，是为了避免 n 为偶数时初值落在实轴上导致
 * 两个根在第一步就重合（实测退化情形）。
 *
 * 边界处理（设计说明 §4.1.2 实测边界）：
 *   - 高次零系数先行降次：|c_n| < 1e-300 时把次数降下来，否则 R 除零；
 *   - 迭代中若 Π 分母为 0（两点重合），给一个确定性的微小扰动而不是
 *     直接放弃，这是 DK 法对重根的已知弱点，扰动后仍不收敛则报 3。 */

#define PRO_NR_MAX_ITER 200
#define PRO_NR_TOL      1e-12
#define PRO_NR_TINY     1e-300

/* 多项式求值（Horner，复数）。返回 P(z)。 */
static void nr_eval(const double *c, int n, double zre, double zim,
                    double *ore, double *oim) {
    double re = c[n];
    double im = 0.0;
    int i;
    for (i = n - 1; i >= 0; i--) {
        /* (re + i·im) · (zre + i·zim) + c[i] */
        double tr = re * zre - im * zim + c[i];
        double ti = re * zim + im * zre;
        re = tr;
        im = ti;
    }
    *ore = re;
    *oim = im;
}

int ac_pro_numroots(const double *coef, int degree) {
    int n;
    int i;
    int j;
    int iter;
    double cn;
    double rmax;
    double radius;
    double two_pi_over_n;
    double max_delta;

    g_root_count = 0;
    g_solve_error = 0;

    if (coef == (const double *)0) { g_solve_error = 2; return 0; }
    if (degree < 1 || degree > AC_PRO_NUMROOT_MAX_DEG) { g_solve_error = 1; return 0; }

    /* 拷进 nr 视图：一来避免调用方缓冲与视图别名（coef 可能就是
       s_pro_nr_coef），二来后面的降次要原地改它。 */
    for (i = 0; i <= degree; i++) { s_pro_nr_coef[i] = coef[i]; }

    /* 前置：系数必须有限 */
    for (i = 0; i <= degree; i++) {
        if (!isfinite(s_pro_nr_coef[i])) { g_solve_error = 2; return 0; }
    }

    /* 高次零系数降次（|c_n| < 1e-300 会让 Aberth 半径除零） */
    n = degree;
    while (n >= 1 && fabs(s_pro_nr_coef[n]) < PRO_NR_TINY) { n--; }
    if (n < 1) { g_solve_error = 1; return 0; }   /* 全零 / 退化为常数 */

    /* Aberth 初始半径 R = 1 + max|c_k|/|c_n|（k = 0..n-1） */
    rmax = 0.0;
    for (i = 0; i < n; i++) {
        double a = fabs(s_pro_nr_coef[i]);
        if (a > rmax) { rmax = a; }
    }
    cn = fabs(s_pro_nr_coef[n]);
    radius = 1.0 + rmax / cn;
    if (!isfinite(radius) || radius < 1.0) { g_solve_error = 2; return 0; }

    /* 初值：圆盘上均匀分布，半格偏移 */
    two_pi_over_n = 6.283185307179586 / (double)n;
    for (i = 0; i < n; i++) {
        double ang = two_pi_over_n * ((double)i + 0.5);
        s_pro_nr_re[i] = radius * cos(ang);
        s_pro_nr_im[i] = radius * sin(ang);
        s_pro_nr_den[i] = 0.0;      /* Aberth 备用槽，DK 不读 */
    }

    /* Durand-Kerner 迭代 */
    for (iter = 0; iter < PRO_NR_MAX_ITER; iter++) {
        max_delta = 0.0;
        for (i = 0; i < n; i++) {
            double pre;
            double pim;
            double dre = 1.0;
            double dim = 0.0;
            double den_mag2;
            double qre;
            double qim;
            double d_re;
            double d_im;
            double dabs;

            nr_eval(s_pro_nr_coef, n, s_pro_nr_re[i], s_pro_nr_im[i], &pre, &pim);

            /* Π_{j≠i}(z_i − z_j)，复数累乘 */
            for (j = 0; j < n; j++) {
                double er;
                double ei;
                double tr;
                double ti;
                if (j == i) { continue; }
                er = s_pro_nr_re[i] - s_pro_nr_re[j];
                ei = s_pro_nr_im[i] - s_pro_nr_im[j];
                tr = dre * er - dim * ei;
                ti = dre * ei + dim * er;
                dre = tr;
                dim = ti;
            }

            /* 分母为 0：两点重合。给一个确定性的微小扰动（按 i 错开），
               避免 0/0 = NaN 直接污染整轮迭代。 */
            den_mag2 = dre * dre + dim * dim;
            if (den_mag2 < 1e-28) {
                dre = 1e-7 * (double)(i + 1);
                dim = 1e-7 * (double)(i + 1);
                den_mag2 = dre * dre + dim * dim;
            }

            /* q = P(z) / Π = (pre + i·pim)/(dre + i·dim) */
            qre = (pre * dre + pim * dim) / den_mag2;
            qim = (pim * dre - pre * dim) / den_mag2;

            d_re = -qre;
            d_im = -qim;
            dabs = sqrt(d_re * d_re + d_im * d_im);
            if (dabs > max_delta) { max_delta = dabs; }

            s_pro_nr_re[i] += d_re;
            s_pro_nr_im[i] += d_im;
        }
        if (!isfinite(max_delta)) { g_solve_error = 3; return 0; }
        if (max_delta < PRO_NR_TOL) { break; }
    }

    /* 收敛判定：步长判据 + 残差判据，二者取「或」
     *
     * 为什么单靠步长判据会误杀重根
     *   实测：P(x) = (x−1)³(x+2)² 在 200 次迭代内步长始终停在 ~1e-5
     *   量级，永远达不到 1e-12，于是旧实现返回「不收敛」，但根其实已经
     *   找对了（残差 2.2e-16）。
     *
     *   这是重根的固有性质，不是 bug：m 重根附近
     *       |Δz| ≈ ε^(1/m)
     *   m = 3 时 1e-16 的机器精度只能换来 1e-5 的步长。设计说明 §4.1.3 已把
     *   「重根（判别式近零）时的收敛停滞检测」列为 B1 的主要难点。
     *
     *   正确做法是把「收敛」定义成「残差足够小」而不是「步长足够小」：
     *       max_i |P(z_i)| ≤ tol_abs + tol_rel · Σ|c_k|·|z_i|^k
     *   后一项是求值的绝对误差尺度（Horner 的舍入量级），用相对判据可以
     *   让大系数多项式不被误判。
     *
     *   注意：没有放宽步长阈值，步长 1e-12 仍是收敛的主要判据，
     *   残差只是给重根开的一扇门。若 200 次后两者都不满足，仍然返回 3。 */
    {
        double residual = 0.0;
        double scale = 0.0;
        for (i = 0; i < n; i++) {
            double pre;
            double pim;
            double mag;
            double sc = 0.0;
            double zabs = sqrt(s_pro_nr_re[i] * s_pro_nr_re[i] +
                               s_pro_nr_im[i] * s_pro_nr_im[i]);
            double pw = 1.0;
            nr_eval(s_pro_nr_coef, n, s_pro_nr_re[i], s_pro_nr_im[i], &pre, &pim);
            mag = sqrt(pre * pre + pim * pim);
            if (mag > residual) { residual = mag; }
            /* 该根的求值尺度 Σ|c_k|·|z|^k */
            for (j = 0; j <= n; j++) {
                sc += fabs(s_pro_nr_coef[j]) * pw;
                pw *= zabs;
            }
            if (sc > scale) { scale = sc; }
        }
        /* 步长没收敛，但残差已经到机器精度量级：接受（重根情形） */
        if (max_delta >= PRO_NR_TOL &&
            residual > 1e-12 * (1.0 + scale)) {
            g_solve_error = 3;
            return 0;
        }
    }

    /* 落根：写上游根池（12 槽，n ≤ 8 装得下） */
    for (i = 0; i < n; i++) {
        g_root_re[i] = s_pro_nr_re[i];
        /* 数值噪声归零：|im| < 1e-10 时判为实根，避免 ac_verify_roots
           因为一个 1e-17 的虚部而跳过该根（上游 ac_solvers.c:613）。 */
        g_root_im[i] = (fabs(s_pro_nr_im[i]) < 1e-10) ? 0.0 : s_pro_nr_im[i];
        g_root_exact_valid[i] = 0;
        g_root_exact_rat[i] = 0;
        g_root_exact_rad_num[i] = 0;
        g_root_exact_rad[i] = 1;
        g_root_exact_den[i] = 1;
        g_root_exact_exp[i] = 0;
    }
    g_root_count = n;
    g_solve_error = 0;
    return 1;
}

/* B1 配套：1..8 次系数采样提取
 * 上游 ac_extract_coefficients 硬限 max_degree ∈ [1,4]（ac_solvers.c:752），
 * 且用 g_matrix_pool 装 5×6。Pro 需要到 8 次：9×10 = 90 double，
 * g_matrix_pool 有 156 个 double，装得下，所以不新增任何存储。
 *
 * 采样点与上游同一约定：x_k = k − degree/2（半整数点）。
 * 结果写 nr 视图的 coef[0..degree]，返回 0 = 成功。 */
int ac_pro_extract_coefficients(int degree) {
    int n;
    int stride;
    int i;
    int j;
    int k;
    int col;
    int row;
    int piv;
    int c;
    int base;
    double xk;
    double pw;
    double y;
    double maxAbs;
    double v;
    double f;
    double s;

    if (degree < 1 || degree > AC_PRO_NUMROOT_MAX_DEG) {
        g_eval_error = EVAL_INTERNAL;
        return EVAL_INTERNAL;
    }

    n = degree + 1;                 /* 采样点数 = 未知数个数 */
    stride = n + 1;                 /* 增广列 */
    if (n * stride > MATRIX_POOL_SIZE) {     /* 9 × 10 = 90 ≤ 156 */
        g_eval_error = EVAL_INTERNAL;
        return EVAL_INTERNAL;
    }

    for (k = 0; k < n; k++) {
        xk = (double)k - (double)degree / 2.0;
        base = k * stride;
        pw = 1.0;
        for (j = 0; j < n; j++) {
            g_matrix_pool[base + j] = pw;
            pw = pw * xk;
        }
        y = ac_eval_equation_at(xk);
        if (g_eval_error != EVAL_OK) { return g_eval_error; }
        if (!isfinite(y)) { g_eval_error = EVAL_OVERFLOW; return g_eval_error; }
        g_matrix_pool[base + n] = y;
    }

    /* 列主元高斯消元（与上游同一阈值 1e-12） */
    for (col = 0; col < n; col++) {
        piv = col;
        maxAbs = fabs(g_matrix_pool[col * stride + col]);
        for (row = col + 1; row < n; row++) {
            v = fabs(g_matrix_pool[row * stride + col]);
            if (v > maxAbs) { maxAbs = v; piv = row; }
        }
        if (maxAbs < 1e-12) { g_eval_error = EVAL_INTERNAL; return EVAL_INTERNAL; }
        if (piv != col) {
            for (c = 0; c < stride; c++) {
                v = g_matrix_pool[col * stride + c];
                g_matrix_pool[col * stride + c] = g_matrix_pool[piv * stride + c];
                g_matrix_pool[piv * stride + c] = v;
            }
        }
        for (row = col + 1; row < n; row++) {
            f = g_matrix_pool[row * stride + col] / g_matrix_pool[col * stride + col];
            if (f != 0.0) {
                for (c = col; c < stride; c++) {
                    g_matrix_pool[row * stride + c] -=
                        f * g_matrix_pool[col * stride + c];
                }
            }
        }
    }

    /* 回代直接落 nr.coef（不建临时数组） */
    for (i = n - 1; i >= 0; i--) {
        s = g_matrix_pool[i * stride + n];
        for (j = i + 1; j < n; j++) {
            s -= g_matrix_pool[i * stride + j] * s_pro_nr_coef[j];
        }
        s_pro_nr_coef[i] = s / g_matrix_pool[i * stride + i];
    }

    g_eval_error = EVAL_OK;
    return 0;
}

/* 端到端：已解析表达式 -> 提取系数 -> 数值求根。
 * 注意 nr.coef 是同一个视图的两段使用者：extract 写它，numroots 读它，
 * 但 numroots 开头会先把它拷到自己（同一块），别名安全，因为拷贝是
 * 恒等拷贝，随后只在原地读。为避免「看起来像 bug」，这里显式先提取到
 * nr.coef，再让 numroots 用同一个指针，并在注释里说明。 */
int ac_pro_solve_numeric_from_expr(int degree) {
    int rc;
    double coef_copy[AC_PRO_NUMROOT_MAX_DEG + 1];
    int i;

    if (ac_pro_degree_route(degree) != 1) { g_solve_error = 1; return 0; }

    rc = ac_pro_extract_coefficients(degree);
    if (rc != 0) { return 0; }

    /* 栈上 9 个 double（72 B）：把系数挪出视图再交给 numroots，
       这样 numroots 内部的「拷进 nr.coef」与提取结果不构成别名。 */
    for (i = 0; i <= degree; i++) { coef_copy[i] = s_pro_nr_coef[i]; }

    return ac_pro_numroots(coef_copy, degree);
}

/* 阶段二 A4：LU 分解
 * 设计说明 §3.2：上游 mat_echelon（ac_matrix.c:74-121）就是部分主元消元核，
 * 但它是 static，Pro 拿不到。且它把乘数写在原矩阵里、把置换记成交换次数
 * （单个 int），拿不回置换向量。所以 Pro 侧重写一个 Doolittle 版本：
 *
 *     P·A = L·U
 *     (P·A)[i][j] = A[piv[i]][j]
 *
 * 与上游保持一致的约定：
 *   - 主元阈值 1e-12（ac_matrix.c:95 同款）；
 *   - 奇异判定 = 任一主元 |·| < 1e-12，返回 0。
 *
 * 存储：L/U/piv 全落 Pro union 的 lu 视图（2352 B），无栈上大数组。
 * L 的对角线恒为 1，但仍按满阵存（设计说明 §3.2.2 的口径），
 * 这样 ac_mat_lu 的结果可以直接被 ac_mat_mul 之类的上游函数消费。 */
int ac_mat_lu(const double *a, int n) {
    int i;
    int j;
    int k;
    int piv;
    double maxAbs;
    double v;
    double f;

    if (a == (const double *)0) { return 0; }
    if (n < 1 || n > AC_PRO_MATRIX_MAX_DIM) { return 0; }

    /* 初始化：U = A 的拷贝（按行），L = I，piv = 恒等置换 */
    for (i = 0; i < n; i++) {
        s_pro_lu_piv[i] = i;
        for (j = 0; j < n; j++) {
            s_pro_lu_u[i * n + j] = a[i * n + j];
            s_pro_lu_l[i * n + j] = (i == j) ? 1.0 : 0.0;
        }
    }

    /* 逐列部分主元消元 */
    for (k = 0; k < n; k++) {
        /* 选列主元：在当前列 k 的 [k, n) 行里取 |·| 最大者 */
        piv = k;
        maxAbs = fabs(s_pro_lu_u[k * n + k]);
        for (i = k + 1; i < n; i++) {
            v = fabs(s_pro_lu_u[i * n + k]);
            if (v > maxAbs) { maxAbs = v; piv = i; }
        }
        if (maxAbs < 1e-12) {
            /* 奇异：L/U 内容未定义，调用方不得使用（头文件契约已写明） */
            return 0;
        }
        if (piv != k) {
            /* 交换 U 的两行 + 交换已算出的 L 乘数（列 0..k-1）+ 置换 */
            int t;
            for (j = 0; j < n; j++) {
                v = s_pro_lu_u[k * n + j];
                s_pro_lu_u[k * n + j] = s_pro_lu_u[piv * n + j];
                s_pro_lu_u[piv * n + j] = v;
            }
            for (j = 0; j < k; j++) {
                v = s_pro_lu_l[k * n + j];
                s_pro_lu_l[k * n + j] = s_pro_lu_l[piv * n + j];
                s_pro_lu_l[piv * n + j] = v;
            }
            t = s_pro_lu_piv[k];
            s_pro_lu_piv[k] = s_pro_lu_piv[piv];
            s_pro_lu_piv[piv] = t;
        }

        /* 消去下方各行：乘数存 L，余项存 U */
        for (i = k + 1; i < n; i++) {
            f = s_pro_lu_u[i * n + k] / s_pro_lu_u[k * n + k];
            s_pro_lu_l[i * n + k] = f;
            if (f == 0.0) {
                s_pro_lu_u[i * n + k] = 0.0;
                continue;
            }
            for (j = k; j < n; j++) {
                s_pro_lu_u[i * n + j] -= f * s_pro_lu_u[k * n + j];
            }
            /* 显式清零，避免 -0.0 / 舍入残渣留在下三角 */
            s_pro_lu_u[i * n + k] = 0.0;
        }
    }
    return 1;
}

const double  *ac_pro_lu_l(void)   { return s_pro_lu_l; }
const double  *ac_pro_lu_u(void)   { return s_pro_lu_u; }
const int32_t *ac_pro_lu_piv(void) { return s_pro_lu_piv; }

#endif /* AC_PRO_ACTIVE */
