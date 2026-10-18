/* ============================================================
 * ApolloCalc-CAS · ac_solvers.c — 方程求解器（第 4 轮）
 * 行为基准：JS/64KB/src/core_solvers.js（修复后 6 槽根池版本），
 * 逐分支对齐；严格对等 JS 64KB，不含 JS 128KB 的五分量精确形式、
 * 有理根检验、泰勒展开与 12 元方程组能力。
 * 职责：二次/三次/四次多项式求根、线性方程组高斯消元（n ≤ 6）、
 *       残差回验、采样提取系数、OOM 安全门禁（深度/函数/变量/次数/
 *       内存水位）、复位。
 * C 与 JS 的差异：精确分量为 int64_t（见 ac_exact.c 头注），分量
 *   越界判定沿用 JS 的 2^53-1 上限（AC_SOLVER_INT_MAX），保证两侧
 *   接受/拒绝边界一致；带符号立方根用 C99 cbrt()（比 JS 的
 *   pow(x,1/3) 更精确，ulp 级差异在 1e-9 跨语言容差内）。
 * Zero-GC：无 malloc、无 VLA、无递归（四次→三次→二次为降次链调用，
 *   各层至多一次，无自调用）。
 * ============================================================ */
#include <math.h>
#include "ac_types.h"
#include "ac_parser.h"
#include "ac_evaluator.h"
#include "ac_complex.h"
#include "ac_exact.h"
#include "ac_solvers.h"

/* [English] Exact-component bound, same as JS 64KB SOLVER_INT_MAX (2^53-1).
   Doubles beyond this are not exact integers, so both languages must
   reject identical ranges. */
#define AC_SOLVER_INT_MAX ((ac_int_t)9007199254740991LL)

/* 2π/3（与 JS 同一 double 常量，三角法根序依赖它） */
#define AC_TWO_PI_3 2.0943951023931953

/* ==================== 全局状态（唯一声明处） ==================== */

int      g_root_count = 0;
double   g_root_re[ROOT_POOL_SIZE];
double   g_root_im[ROOT_POOL_SIZE];
ac_int_t g_root_exact_num[ROOT_POOL_SIZE];
ac_int_t g_root_exact_den[ROOT_POOL_SIZE];
ac_int_t g_root_exact_rad[ROOT_POOL_SIZE];
int      g_root_exact_valid[ROOT_POOL_SIZE];

int      g_solve_error = 0;     /* 0=OK 1=次数过高 2=前置体检拒绝 3=求解失败 4=内存水位 */
double   g_residual_max = 0.0;
double   g_coeffs[5];

/* ==================== 内部助手 ==================== */

/* [English] Signed cube root, mirrors JS cbrtSigned. C99 cbrt() is more
   accurate than pow(x,1/3); ulp-level difference stays inside the 1e-9
   cross-language tolerance. */
static double cbrt_signed(double x) {
    if (x == 0.0) { return 0.0; }
    if (x > 0.0) { return cbrt(x); }
    return -cbrt(-x);
}

/* ==================== 模块一：二次方程求解器 ====================
 * 输入 g_coeffs[0..2] = [c, b, a]，即 ax² + bx + c = 0
 * D ≥ 0：两实根，精确形式经 ac_exact_from_discriminant 组装——
 *   仅 rad=1（纯分数）或 b=0（纯根式）两种情形；混合形式（如
 *   (1+√5)/2）三分量模型无法表达，置无效，与 JS 64KB 边界一致
 * D < 0：共轭复根，精确形式置无效
 * 退化 a=0：降为线性（单根，整数系数时保留精确分数）
 */
void ac_solve_quadratic(void) {
    double c = g_coeffs[0];
    double b = g_coeffs[1];
    double a = g_coeffs[2];
    g_root_count = 0;
    g_solve_error = 0;

    if (!isfinite(a) || !isfinite(b) || !isfinite(c)) { g_solve_error = 3; return; }

    /* ---- 退化：a = 0 → 线性方程 bx + c = 0 ---- */
    if (a == 0.0) {
        if (b == 0.0) {
            if (c == 0.0) { g_solve_error = 3; return; }   /* 恒等式：无穷多根无法枚举 */
            g_root_count = 0;                              /* 矛盾式：无根，非错误 */
            return;
        }
        g_root_re[0] = -c / b;
        g_root_im[0] = 0.0;
        g_root_count = 1;
        /* 内存策略：精确形式仅整数系数可组装，复用 exact 模块全局三分量 */
        if (b == floor(b) && c == floor(c) &&
            b <= 9007199254740991.0 && b >= -9007199254740991.0 &&
            c <= 9007199254740991.0 && c >= -9007199254740991.0) {
            ac_exact_set_fraction((ac_int_t)(-c), (ac_int_t)b);
        } else {
            ac_exact_invalidate();
        }
        g_root_exact_valid[0] = g_exact_valid;
        g_root_exact_num[0] = g_exact_num;
        g_root_exact_den[0] = g_exact_den;
        g_root_exact_rad[0] = g_exact_rad;
        return;
    }

    {
        double D = b * b - 4.0 * a * c;
        double twoA = 2.0 * a;
        if (!isfinite(D)) { g_solve_error = 3; return; }

        if (D >= 0.0) {
            /* ---- 两实根：x = (-b ± √D) / 2a ---- */
            double sq = sqrt(D);
            ac_int_t n;
            ac_int_t d;
            ac_int_t rad;
            g_root_re[0] = (-b + sq) / twoA;
            g_root_re[1] = (-b - sq) / twoA;
            g_root_im[0] = 0.0;
            g_root_im[1] = 0.0;
            g_root_count = 2;

            /* ---- 精确形式组装（√D = (n/d)·√r，d>0）---- */
            ac_exact_from_discriminant(D);
            if (!g_exact_valid) {
                g_root_exact_valid[0] = 0;
                g_root_exact_valid[1] = 0;
            } else {
                n = g_exact_num;
                d = g_exact_den;
                rad = g_exact_rad;
                if (rad == 1) {
                    /* r=1：D 为有理数平方 → 纯分数 (−b·d ± n)/(2a·d)。
                       预检在 double 层进行（对齐 JS 的浮点判定语义），
                       随后的 int64 运算由预检保证不越 2^53。 */
                    if (a == floor(a) && b == floor(b) &&
                        a <= 9007199254740991.0 && a >= -9007199254740991.0 &&
                        b <= 9007199254740991.0 && b >= -9007199254740991.0 &&
                        fabs(b) <= 9007199254740991.0 / (double)d &&
                        fabs(a) <= 9007199254740991.0 / (2.0 * (double)d)) {
                        ac_int_t base = -(ac_int_t)b * d;
                        ac_int_t den = 2 * (ac_int_t)a * d;
                        ac_int_t an = (n < 0) ? -n : n;
                        ac_int_t ab = (base < 0) ? -base : base;
                        int ok0 = ab <= AC_SOLVER_INT_MAX - an;
                        if (ok0) {
                            /* 根 0：(base + n)/den，根 1：(base − n)/den，逐个借全局约分 */
                            g_exact_num = base + n;
                            g_exact_den = den;
                            g_exact_rad = 1;
                            ac_exact_normalize();
                            ac_exact_update_approx();
                            g_root_exact_num[0] = g_exact_num;
                            g_root_exact_den[0] = g_exact_den;
                            g_root_exact_rad[0] = g_exact_rad;
                            g_root_exact_valid[0] = g_exact_valid;
                            g_exact_num = base - n;
                            g_exact_den = den;
                            g_exact_rad = 1;
                            ac_exact_normalize();
                            ac_exact_update_approx();
                            g_root_exact_num[1] = g_exact_num;
                            g_root_exact_den[1] = g_exact_den;
                            g_root_exact_rad[1] = g_exact_rad;
                            g_root_exact_valid[1] = g_exact_valid;
                        } else {
                            g_root_exact_valid[0] = 0;
                            g_root_exact_valid[1] = 0;
                        }
                    } else {
                        g_root_exact_valid[0] = 0;
                        g_root_exact_valid[1] = 0;
                    }
                } else if (b == 0.0) {
                    /* b=0：纯根式 (±n)/(2a·d)·√r */
                    if (a == floor(a) &&
                        a <= 9007199254740991.0 && a >= -9007199254740991.0 &&
                        fabs(a) <= 9007199254740991.0 / (2.0 * (double)d)) {
                        double den2d = 2.0 * a * (double)d;    /* 分母归正：符号翻进分子 */
                        ac_int_t den2;
                        int sgn = 1;
                        if (den2d < 0.0) { sgn = -1; den2d = -den2d; }
                        den2 = (ac_int_t)den2d;
                        g_exact_num = sgn * n;
                        g_exact_den = den2;
                        g_exact_rad = rad;
                        ac_exact_normalize();
                        ac_exact_update_approx();
                        g_root_exact_num[0] = g_exact_num;
                        g_root_exact_den[0] = g_exact_den;
                        g_root_exact_rad[0] = g_exact_rad;
                        g_root_exact_valid[0] = g_exact_valid;
                        g_root_exact_num[1] = -g_exact_num;    /* 共轭：仅分子变号 */
                        g_root_exact_den[1] = g_exact_den;
                        g_root_exact_rad[1] = g_exact_rad;
                        g_root_exact_valid[1] = g_root_exact_valid[0];
                    } else {
                        g_root_exact_valid[0] = 0;
                        g_root_exact_valid[1] = 0;
                    }
                } else {
                    g_root_exact_valid[0] = 0;                 /* 有理+根式混合：不可表达 */
                    g_root_exact_valid[1] = 0;
                }
            }
        } else {
            /* ---- 共轭复根：x = (-b ± i√|D|) / 2a ---- */
            double sqI = sqrt(-D);
            double reC = -b / twoA;
            double imC = sqI / twoA;
            g_root_re[0] = reC;
            g_root_im[0] = imC;
            g_root_re[1] = reC;
            g_root_im[1] = -imC;
            g_root_count = 2;
            g_root_exact_valid[0] = 0;
            g_root_exact_valid[1] = 0;
        }
    }
}

/* ==================== 模块二：三次方程求解器（卡尔达诺 + 三角切换） ====================
 * 输入 g_coeffs[0..3] = [d, c, b, a]，即 ax³ + bx² + cx + d = 0
 * 消去二次项：x = y − b/(3a) → y³ + py + q = 0
 * Δ > 0：一实根 + 共轭复对（卡尔达诺，全实标量路径）
 * Δ = 0：三实根（含重根）
 * Δ < 0：不可约情形，必须切换三角法（卡尔达诺会因复数中间量丢失精度）
 * 三次方程不支持精确形式，g_root_exact_valid[0..2] 全 0
 */
void ac_solve_cubic(void) {
    double d = g_coeffs[0];
    double c = g_coeffs[1];
    double b = g_coeffs[2];
    double a = g_coeffs[3];
    g_root_count = 0;
    g_solve_error = 0;

    if (!isfinite(a) || !isfinite(b) || !isfinite(c) || !isfinite(d)) { g_solve_error = 3; return; }

    /* 退化 a = 0 → 降次为二次（g_coeffs[0..2] 恰为二次布局，直接委托） */
    if (a == 0.0) { ac_solve_quadratic(); return; }

    {
        double a2 = a * a;
        double a3 = a2 * a;
        double p = (3.0 * a * c - b * b) / (3.0 * a2);
        double q = (2.0 * b * b * b - 9.0 * a * b * c + 27.0 * a2 * d) / (27.0 * a3);
        double off;
        double qh;
        double p3;
        double p3c;
        double dlt;
        double tolD;
        if (!isfinite(p) || !isfinite(q)) { g_solve_error = 3; return; }
        off = -b / (3.0 * a);                /* x = y + off */

        qh = q * 0.5;
        p3 = p / 3.0;
        p3c = p3 * p3 * p3;
        dlt = qh * qh + p3c;
        if (!isfinite(dlt)) { g_solve_error = 3; return; }

        /* Δ 零判定容差（相对量级），避免浮点抖动把重根误判为三根/一根 */
        tolD = 1e-14 * (fabs(qh * qh) + fabs(p3c));

        if (dlt > tolD) {
            /* ---- Δ > 0：卡尔达诺（u/v 为带符号实立方根标量） ---- */
            double sD = sqrt(dlt);
            double u = cbrt_signed(-qh + sD);
            double v = cbrt_signed(-qh - sD);
            double re2;
            double imS;
            g_root_re[0] = u + v + off;
            g_root_im[0] = 0.0;
            re2 = -(u + v) * 0.5 + off;
            imS = 0.8660254037844386 * (u - v);          /* √3/2 */
            g_root_re[1] = re2;
            g_root_im[1] = imS;
            g_root_re[2] = re2;
            g_root_im[2] = -imS;
            g_root_count = 3;
        } else if (dlt >= -tolD) {
            /* ---- Δ = 0：三实根（y1 = 2∛(−q/2) 单根，y2 = y3 = −∛(−q/2) 二重根） ---- */
            double t0 = cbrt_signed(-qh);
            g_root_re[0] = 2.0 * t0 + off;
            g_root_re[1] = -t0 + off;
            g_root_re[2] = -t0 + off;
            g_root_im[0] = 0.0;
            g_root_im[1] = 0.0;
            g_root_im[2] = 0.0;
            g_root_count = 3;
        } else {
            /* ---- Δ < 0 不可约：三角法 ----
             * y_k = 2√(−p/3)·cos(θ − 2πk/3)，θ = acos((3q)/(2p)·√(−3/p))/3
             * acos 入参钳位到 [−1,1] 防浮点越界 */
            double mm = 2.0 * sqrt(-p3);
            double arg = (3.0 * q) / (2.0 * p) * sqrt(-3.0 / p);
            double th;
            if (arg > 1.0) { arg = 1.0; }
            else if (arg < -1.0) { arg = -1.0; }
            th = acos(arg) / 3.0;
            g_root_re[0] = mm * cos(th) + off;
            g_root_re[1] = mm * cos(th - AC_TWO_PI_3) + off;
            g_root_re[2] = mm * cos(th - 2.0 * AC_TWO_PI_3) + off;
            g_root_im[0] = 0.0;
            g_root_im[1] = 0.0;
            g_root_im[2] = 0.0;
            g_root_count = 3;
        }
    }

    /* 三次方程不支持精确形式 */
    g_root_exact_valid[0] = 0;
    g_root_exact_valid[1] = 0;
    g_root_exact_valid[2] = 0;
}

/* ==================== 模块三：四次方程求解器（笛卡尔分解法） ====================
 * 输入 g_coeffs[0..4] = [e, d, c, b, a]，即 ax⁴ + bx³ + cx² + dx + e = 0
 * 消去三次项：x = y − b/(4a) → y⁴ + py² + qy + r = 0
 * q ≈ 0 退化：双二次 z² + pz + r = 0（z = y²），逐 z 用复数开方
 * 一般情形：预解三次 8m³ + 8pm² + (2p²−8r)m − q² = 0，取最大实根 m，
 *   α=√(2m)，β=m+p/2−q/(2α)，γ=m+p/2+q/(2α)，分解为
 *   (y²+αy+β)(y²−αy+γ)，两个二次因式的 4 个 y 根合成后平移回 x
 * 注意：原四次系数在入口已缓存为局部标量；预解/因式复用 g_coeffs
 *   传参，求解完成后 g_coeffs 不再是原四次系数（对齐 JS）
 * 四次方程不支持精确形式，g_root_exact_valid[0..3] 全 0
 */
void ac_solve_quartic(void) {
    double e = g_coeffs[0];
    double d = g_coeffs[1];
    double c = g_coeffs[2];
    double b = g_coeffs[3];
    double a = g_coeffs[4];
    g_root_count = 0;
    g_solve_error = 0;

    if (!isfinite(a) || !isfinite(b) || !isfinite(c) || !isfinite(d) || !isfinite(e)) {
        g_solve_error = 3;
        return;
    }

    /* 退化 a = 0 → 降次为三次 */
    if (a == 0.0) { ac_solve_cubic(); return; }

    {
        double a2 = a * a;
        double a3 = a2 * a;
        double a4 = a2 * a2;
        double b2 = b * b;
        double p = (8.0 * a * c - 3.0 * b2) / (8.0 * a2);
        double q = (b * b2 - 4.0 * a * b * c + 8.0 * a2 * d) / (8.0 * a3);
        double r = (-3.0 * b * b2 * b + 256.0 * a3 * e - 64.0 * a2 * b * d + 16.0 * a * b2 * c) / (256.0 * a4);
        double off;
        double qTol;
        int i = 0;
        if (!isfinite(p) || !isfinite(q) || !isfinite(r)) { g_solve_error = 3; return; }
        off = -b / (4.0 * a);                /* x = y + off */

        qTol = 1e-12 * (1.0 + fabs(p) + fabs(r));

        if (fabs(q) < qTol) {
            /* ---- 双二次退化路径：z² + pz + r = 0，z = y² ----
             * z 根先缓存为局部标量再展开写回，避免读写区重叠；
             * 每个 z 用复数开方（负 z 自动落到正虚部分支），正负两根就地取反 */
            double z0Re;
            double z0Im;
            double z1Re;
            double z1Im;
            g_coeffs[0] = r;
            g_coeffs[1] = p;
            g_coeffs[2] = 1.0;
            ac_solve_quadratic();
            if (g_solve_error != 0) { return; }
            z0Re = g_root_re[0];
            z0Im = g_root_im[0];
            z1Re = g_root_re[1];
            z1Im = g_root_im[1];
            g_c_a_re = z0Re;
            g_c_a_im = z0Im;
            ac_c_sqrt();                         /* 复数层：TMP = sqrt(z0) */
            g_root_re[0] = g_c_tmp_re;
            g_root_im[0] = g_c_tmp_im;
            g_root_re[1] = -g_c_tmp_re;
            g_root_im[1] = -g_c_tmp_im;
            g_c_a_re = z1Re;
            g_c_a_im = z1Im;
            ac_c_sqrt();                         /* 复数层：TMP = sqrt(z1) */
            g_root_re[2] = g_c_tmp_re;
            g_root_im[2] = g_c_tmp_im;
            g_root_re[3] = -g_c_tmp_re;
            g_root_im[3] = -g_c_tmp_im;
            g_root_count = 4;
        } else {
            /* ---- 一般路径：预解三次 → 待定系数分解 ----
             * m/α/β/γ 全为标量；两因式根用 g_root_re[0..3] 就地接力
             * （因式1 根暂存 [2..3]，因式2 根落 [0..1]），零临时数组 */
            double m;
            int found = 0;
            double alpha;
            double halfP;
            double beta;
            double gamma;
            g_coeffs[0] = -q * q / 8.0;
            g_coeffs[1] = p * p * 0.25 - r;
            g_coeffs[2] = p;
            g_coeffs[3] = 1.0;
            ac_solve_cubic();
            if (g_solve_error != 0) { return; }

            m = -1e308;
            for (i = 0; i < g_root_count; i++) {
                if (g_root_im[i] == 0.0 && g_root_re[i] > m) { m = g_root_re[i]; found = 1; }
            }
            if (found == 0 || m <= 0.0 || !isfinite(m)) {
                g_solve_error = 3;
                g_root_count = 0;
                return;
            }

            alpha = sqrt(2.0 * m);
            halfP = m + p * 0.5;
            beta = halfP - q / (2.0 * alpha);
            gamma = halfP + q / (2.0 * alpha);

            /* 因式 1：y² + αy + β = 0 → 根暂存 g_root_re[2..3] */
            g_coeffs[0] = beta;
            g_coeffs[1] = alpha;
            g_coeffs[2] = 1.0;
            ac_solve_quadratic();
            if (g_solve_error != 0) { return; }
            g_root_re[2] = g_root_re[0];
            g_root_im[2] = g_root_im[0];
            g_root_re[3] = g_root_re[1];
            g_root_im[3] = g_root_im[1];

            /* 因式 2：y² − αy + γ = 0 → 根落 g_root_re[0..1] */
            g_coeffs[0] = gamma;
            g_coeffs[1] = -alpha;
            g_coeffs[2] = 1.0;
            ac_solve_quadratic();
            if (g_solve_error != 0) { return; }
            g_root_count = 4;
        }

        /* ---- 平移回 x = y + off；四次方程不支持精确形式 ---- */
        for (i = 0; i < 4; i++) {
            g_root_re[i] = g_root_re[i] + off;
            g_root_exact_valid[i] = 0;
        }
    }
}

/* ==================== 模块四：线性方程组求解器（列主元高斯消元） ====================
 * 输入 g_matrix_pool 前 n·(n+1) 个元素：增广矩阵（行主序，1 ≤ n ≤ 6）
 * 列主元选取 → 行交换 → 消元 → 回代，结果写 g_root_re[0..n-1]
 * 主元 |·| < 1e-12 → 奇异，g_solve_error = 3
 * 内存策略：原地作用于 g_matrix_pool，零复制零分配
 */
void ac_solve_linear_system(int n) {
    int stride;
    int col;
    int row;
    int k;
    int piv;
    double maxAbs;
    double v;
    double f;
    double s;

    g_root_count = 0;
    g_solve_error = 0;
    if (n < 1 || n > 6) { g_solve_error = 3; return; }

    stride = n + 1;
    for (col = 0; col < n; col++) {
        /* 列主元：当前列 |·| 最大的行换到对角位 */
        piv = col;
        maxAbs = g_matrix_pool[col * stride + col];
        if (maxAbs < 0.0) { maxAbs = -maxAbs; }
        for (row = col + 1; row < n; row++) {
            v = g_matrix_pool[row * stride + col];
            if (v < 0.0) { v = -v; }
            if (v > maxAbs) { maxAbs = v; piv = row; }
        }
        if (maxAbs < 1e-12) {
            g_solve_error = 3;                   /* 奇异矩阵 */
            g_root_count = 0;
            return;
        }
        if (piv != col) {
            for (k = 0; k < stride; k++) {
                v = g_matrix_pool[col * stride + k];
                g_matrix_pool[col * stride + k] = g_matrix_pool[piv * stride + k];
                g_matrix_pool[piv * stride + k] = v;
            }
        }
        for (row = col + 1; row < n; row++) {
            f = g_matrix_pool[row * stride + col] / g_matrix_pool[col * stride + col];
            if (f != 0.0) {
                for (k = col; k < stride; k++) {
                    g_matrix_pool[row * stride + k] = g_matrix_pool[row * stride + k] - f * g_matrix_pool[col * stride + k];
                }
            }
        }
    }

    /* 回代：解直接落 g_root_re，不建临时数组 */
    for (row = n - 1; row >= 0; row--) {
        s = g_matrix_pool[row * stride + n];
        for (k = row + 1; k < n; k++) {
            s = s - g_matrix_pool[row * stride + k] * g_root_re[k];
        }
        g_root_re[row] = s / g_matrix_pool[row * stride + row];
    }
    for (row = 0; row < n; row++) {
        g_root_im[row] = 0.0;
        g_root_exact_valid[row] = 0;
    }
    g_root_count = n;
}

/* ==================== 模块五：残差回验 ====================
 * 对每个实根（g_root_im[i] == 0）代回原方程 f(x)（ac_eval_equation_at，
 * 基于 g_token_pool）相对残差 = |f(root)| / max(|f(root−δ)|, |f(root+δ)|,
 * 1e-300)，δ = |root| > 10 时取 0.1·|root| 否则 1——邻域值自然反映系数
 * 整体量级，采样拟合失真时比值近 1；真根时残差为浮点噪声，比值 ~1e-12
 * 只记录 g_residual_max 不拦截，UI 层据 > 1e-6 决定是否提示
 */
void ac_verify_roots(void) {
    int i = 0;
    double x = 0.0;
    double f0 = 0.0;
    double f1 = 0.0;
    double f2 = 0.0;
    double del = 1.0;
    double scale = 0.0;
    double rel = 0.0;
    double ax = 0.0;

    g_residual_max = 0.0;
    for (i = 0; i < g_root_count; i++) {
        if (g_root_im[i] != 0.0) { continue; }
        x = g_root_re[i];
        f0 = ac_eval_equation_at(x);
        if (g_eval_error != EVAL_OK || !isfinite(f0)) {
            rel = 1.0;                           /* 求值失败：视作不可信 */
        } else {
            ax = x < 0.0 ? -x : x;
            del = ax > 10.0 ? 0.1 * ax : 1.0;
            f1 = ac_eval_equation_at(x - del);
            f2 = ac_eval_equation_at(x + del);
            scale = f1 < 0.0 ? -f1 : f1;
            if (f2 > scale) { scale = f2; }
            else if (-f2 > scale) { scale = -f2; }
            if (scale < 1e-300) { scale = 1e-300; }
            rel = f0 < 0.0 ? -f0 : f0;
            if (rel < 1e-300) { rel = 0.0; }
            else { rel = rel / scale; }
        }
        if (rel > g_residual_max) { g_residual_max = rel; }
    }
}

/* ==================== 模块六：OOM 安全防护与异常拦截 ====================
 * 求解前置门禁：全部只读扫描 g_token_pool，零分配
 */

/* Token 流体检：括号嵌套 ≤ 3、函数调用 ≤ 2、变量出现 ≤ 5
 * 返回 0 = 通过；1 = 拒绝（g_solve_error = 2） */
int ac_pre_check_expression(void) {
    int depth = 0;
    int funcCount = 0;
    int varCount = 0;
    int i = 0;
    int t = 0;

    g_solve_error = 0;
    for (i = 0; i < g_token_count; i++) {
        t = ac_token_type(g_token_pool[i]);
        if (t == T_LP) {
            depth++;
            if (depth > 3) { g_solve_error = 2; return 1; }
        } else if (t == T_RP) {
            depth--;
        } else if (t == T_FUNC) {
            funcCount++;
            if (funcCount > 2) { g_solve_error = 2; return 1; }
        } else if (t == T_VAR) {
            varCount++;
            if (varCount > 5) { g_solve_error = 2; return 1; }
        }
    }
    return 0;
}

/* 次数门禁：degree > 4 拒绝（阿贝尔-鲁菲尼定理，五次及以上无根式解）
 * 返回 0 = 通过；1 = 拒绝（g_solve_error = 1） */
int ac_guard_degree(int degree) {
    if (degree > 4) { g_solve_error = 1; return 1; }
    return 0;
}

/* 内存水位门禁：估算用量 > 48KB（64KB × 75%）拒绝求解
 * JS 优先 Heap API，C 无堆可查：直接用静态池 sizeof 之和 +
 * 已用游标估算（含 2KB 运行时冗余），口径与 JS 降级估算一致
 * 返回 0 = 通过；1 = 拒绝（g_solve_error = 4） */
int ac_check_memory_watermark(void) {
    double used;

    g_solve_error = 0;
    used = (double)(sizeof(g_token_pool) + sizeof(g_number_pool) +
                    sizeof(g_eval_stack) + sizeof(g_matrix_pool) +
                    sizeof(g_rpn_token) + sizeof(g_op_stack) +
                    sizeof(g_lhs_token) + sizeof(g_rhs_token) +
                    sizeof(g_coeffs) +
                    sizeof(g_root_re) + sizeof(g_root_im) +
                    sizeof(g_root_exact_num) + sizeof(g_root_exact_den) +
                    sizeof(g_root_exact_rad) + sizeof(g_root_exact_valid)) +
           (double)(g_token_count << 3) + (double)(g_number_count << 3) + 2048.0;
    if (used > 49152.0) { g_solve_error = 4; return 1; }
    return 0;
}

/* ==================== 模块七：系数采样提取 ====================
 * 数值采样法：x_k = k − max_degree/2（double 除法，3 次取半整数点），
 * 对每个采样点调 ac_eval_equation_at 得 y_k，构造范德蒙德方程组
 * （5×6 增广复用 g_matrix_pool），列主元高斯消元后回代落 g_coeffs
 * 返回 0 = 成功；非 0 = g_eval_error（次数越界/求值失败/奇异）
 */
int ac_extract_coefficients(int max_degree) {
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

    for (i = 0; i < 5; i++) { g_coeffs[i] = 0.0; }
    if (max_degree < 1 || max_degree > 4) { g_eval_error = EVAL_INTERNAL; return EVAL_INTERNAL; }

    n = max_degree + 1;                  /* 采样点数 = 方程组元数 */
    stride = n + 1;                      /* 增广矩阵行宽 */

    /* ---- 采样并构造增广矩阵：第 k 行 = [1, xk, xk², .., xkⁿ⁻¹ | y_k] ----
     * 幂用循环累乘（内存策略：仅标量累加器） */
    for (k = 0; k < n; k++) {
        xk = (double)k - (double)max_degree / 2.0;   /* 如 4 次取 −2..2，3 次取 −1.5..1.5 */
        base = k * stride;
        pw = 1.0;
        for (j = 0; j < n; j++) {
            g_matrix_pool[base + j] = pw;
            pw = pw * xk;
        }
        y = ac_eval_equation_at(xk);
        if (g_eval_error != EVAL_OK) {
            for (i = 0; i < 5; i++) { g_coeffs[i] = 0.0; }
            return g_eval_error;
        }
        g_matrix_pool[base + max_degree + 1] = y;
    }

    /* ---- 高斯消元（列主元部分选主，显式循环，零递归） ---- */
    for (col = 0; col < n; col++) {
        piv = col;
        maxAbs = g_matrix_pool[col * stride + col];
        if (maxAbs < 0.0) { maxAbs = -maxAbs; }
        for (row = col + 1; row < n; row++) {
            v = g_matrix_pool[row * stride + col];
            if (v < 0.0) { v = -v; }
            if (v > maxAbs) { maxAbs = v; piv = row; }
        }
        if (maxAbs < 1e-12) {                /* 奇异：方程含非多项式成分或次数超限 */
            g_eval_error = EVAL_INTERNAL;
            return EVAL_INTERNAL;
        }
        if (piv != col) {                    /* 整行交换 */
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
                    g_matrix_pool[row * stride + c] = g_matrix_pool[row * stride + c] - f * g_matrix_pool[col * stride + c];
                }
            }
        }
    }

    /* ---- 回代（内存策略：结果直接落 g_coeffs，不建临时数组） ---- */
    for (i = n - 1; i >= 0; i--) {
        s = g_matrix_pool[i * stride + n];
        for (j = i + 1; j < n; j++) {
            s = s - g_matrix_pool[i * stride + j] * g_coeffs[j];
        }
        g_coeffs[i] = s / g_matrix_pool[i * stride + i];
    }

    g_eval_error = EVAL_OK;
    return 0;
}

/* ==================== 模块八：复位 ====================
 * 每轮求解前调用（内存策略：只清标量与预分配数组，零分配） */
void ac_solvers_reset(void) {
    int i = 0;
    g_root_count = 0;
    for (i = 0; i < 6; i++) {
        g_root_re[i] = 0.0;
        g_root_im[i] = 0.0;
        g_root_exact_num[i] = 0;
        g_root_exact_den[i] = 1;
        g_root_exact_rad[i] = 1;
        g_root_exact_valid[i] = 0;
    }
    g_solve_error = 0;
    g_residual_max = 0.0;
}
