/* ============================================================
 * ApolloCalc-CAS · ac_complex.c — 复数基础运算层（第 6 轮）
 * 行为基准：JS/64KB/src/core_complex.js，逐字对齐；严格对等
 *   JS 64KB——不含 JS 128KB 的 cExp/cLn/cPowReal 扩展（留给 C 128KB）。
 * 职责：六寄存器（操作数 A/B、结果 TMP）上的基础四则、模长/辐角/
 *       共轭、复数开方（三段分支）、整数快速幂（平方-乘法）、复位。
 *   原第 4 轮 ac_solvers.c 内的 static ac_csqrt 已迁移至此（对外
 *   即 ac_c_sqrt），四次方程双二次路径经寄存器调用，行为零变化。
 * Zero-GC：全部为全局标量与栈上局部标量，无堆、无 VLA、无递归。
 * ============================================================ */
#include <math.h>
#include "ac_types.h"
#include "ac_complex.h"

/* ==================== 全局寄存器（唯一声明处） ==================== */

double g_c_a_re = 0.0;
double g_c_a_im = 0.0;
double g_c_b_re = 0.0;
double g_c_b_im = 0.0;
double g_c_tmp_re = 0.0;
double g_c_tmp_im = 0.0;

/* ==================== 模块一：基础四则运算 ==================== */

void ac_c_add(void) {
    g_c_tmp_re = g_c_a_re + g_c_b_re;
    g_c_tmp_im = g_c_a_im + g_c_b_im;
}

void ac_c_sub(void) {
    g_c_tmp_re = g_c_a_re - g_c_b_re;
    g_c_tmp_im = g_c_a_im - g_c_b_im;
}

void ac_c_mul(void) {
    /* 用局部变量暂存，避免读写序污染（对齐 JS 的局部 let） */
    double re = g_c_a_re * g_c_b_re - g_c_a_im * g_c_b_im;
    double im = g_c_a_re * g_c_b_im + g_c_a_im * g_c_b_re;
    g_c_tmp_re = re;
    g_c_tmp_im = im;
}

void ac_c_div(void) {
    double d = g_c_b_re * g_c_b_re + g_c_b_im * g_c_b_im;
    if (d == 0.0) {
        /* 分母为零：整体置 NaN，交由上层判定无效根 */
        g_c_tmp_re = (double)NAN;
        g_c_tmp_im = (double)NAN;
        return;
    }
    double re = (g_c_a_re * g_c_b_re + g_c_a_im * g_c_b_im) / d;
    double im = (g_c_a_im * g_c_b_re - g_c_a_re * g_c_b_im) / d;
    g_c_tmp_re = re;
    g_c_tmp_im = im;
}

/* ==================== 模块二：模长、辐角与共轭 ==================== */

double ac_c_abs(void) {
    return sqrt(g_c_a_re * g_c_a_re + g_c_a_im * g_c_a_im);
}

double ac_c_arg(void) {
    return atan2(g_c_a_im, g_c_a_re);
}

void ac_c_conj(void) {
    g_c_tmp_re = g_c_a_re;
    g_c_tmp_im = -g_c_a_im;
}

/* ==================== 模块三：复数开方（核心，精度敏感） ==================== */

void ac_c_sqrt(void) {
    double re = g_c_a_re;
    double im = g_c_a_im;

    /* 零元情形：0 + 0i 的开方仍为 0 + 0i */
    if (re == 0.0 && im == 0.0) {
        g_c_tmp_re = 0.0;
        g_c_tmp_im = 0.0;
        return;
    }

    /* 纯实数退化：虚部为 0 时直接开实部，绕开 atan2(0, x) 的精度损失 */
    if (im == 0.0) {
        if (re >= 0.0) {
            g_c_tmp_re = sqrt(re);
            g_c_tmp_im = 0.0;
        } else {
            /* 负实数：根为纯虚数 ±√|re|·i，约定取正虚部分支 */
            g_c_tmp_re = 0.0;
            g_c_tmp_im = sqrt(-re);
        }
        return;
    }

    /* 极坐标法：√r · (cos(θ/2) + i·sin(θ/2))，另一根取反（求解器按需取反） */
    double r = sqrt(re * re + im * im);
    double theta = atan2(im, re);
    double half = theta * 0.5;
    double sr = sqrt(r);
    g_c_tmp_re = sr * cos(half);
    g_c_tmp_im = sr * sin(half);
}

/* ==================== 模块四：复数乘幂（快速幂，平方-乘法） ==================== */

void ac_c_pow_int(int n) {
    double base_re = g_c_a_re;
    double base_im = g_c_a_im;
    int exp = n;

    /* 负指数：先取倒数，再按正指数求幂 */
    if (exp < 0) {
        double d = base_re * base_re + base_im * base_im;
        if (d == 0.0) {
            /* 0 的负次幂无定义：置 NaN 交由上层处理 */
            g_c_tmp_re = (double)NAN;
            g_c_tmp_im = (double)NAN;
            return;
        }
        double inv_re = base_re / d;
        double inv_im = -base_im / d;
        base_re = inv_re;
        base_im = inv_im;
        exp = -exp;
    }

    /* 结果累加器初始化为 1 + 0i */
    double acc_re = 1.0;
    double acc_im = 0.0;
    int e = exp;

    /* 平方-乘法：从最低位起逐位消费指数 */
    for (;;) {
        if ((e & 1) == 1) {
            double n_re = acc_re * base_re - acc_im * base_im;
            double n_im = acc_re * base_im + acc_im * base_re;
            acc_re = n_re;
            acc_im = n_im;
        }
        e = e >> 1;
        if (e == 0) { break; }
        /* base = base * base（原地平方） */
        double sq_re = base_re * base_re - base_im * base_im;
        double sq_im = 2.0 * base_re * base_im;
        base_re = sq_re;
        base_im = sq_im;
    }

    g_c_tmp_re = acc_re;
    g_c_tmp_im = acc_im;
}

/* ==================== 模块五：复数指数 / 对数 / 实数幂（128KB 第 2 轮） ====================
 * 行为基准：JS/128KB/src/core_complex.js 模块五，逐分支一致。 */

void ac_c_exp(void) {
    /* e^(a+bi) = e^a · (cos b + i·sin b) */
    double ea = exp(g_c_a_re);
    double re = ea * cos(g_c_a_im);
    double im = ea * sin(g_c_a_im);
    g_c_tmp_re = re;
    g_c_tmp_im = im;
}

void ac_c_ln(void) {
    /* 主值对数 ln(z) = ln|z| + i·arg(z)；
       ln|z| 用 0.5·log(re²+im²) 直取，避免先开方再取对数的精度损失 */
    double re = g_c_a_re;
    double im = g_c_a_im;
    if (re == 0.0 && im == 0.0) {
        /* ln(0) 无定义：返回 -Inf + 0i，上层按 isFinite 判错 */
        g_c_tmp_re = -(double)INFINITY;
        g_c_tmp_im = 0.0;
        return;
    }
    g_c_tmp_re = 0.5 * log(re * re + im * im);
    g_c_tmp_im = atan2(im, re);
}

void ac_c_pow_real(double w) {
    /* z^w = e^(w·ln z)，w 为实数。
       ⚠ 内部把 w·ln z 暂存进 A 寄存器（复用 cLn/cExp，与 JS 同款实现）——
       调用后 g_c_a_re/g_c_a_im 被覆写，UI 层需重新装载 A（头文件已写明）。 */
    double re = g_c_a_re;
    double im = g_c_a_im;
    double ln_re;
    double ln_im;
    if (re == 0.0 && im == 0.0) {
        /* z = 0 且 w > 0 → 0；w <= 0 → 无定义 NaN */
        if (w > 0.0) {
            g_c_tmp_re = 0.0;
            g_c_tmp_im = 0.0;
        } else {
            g_c_tmp_re = (double)NAN;
            g_c_tmp_im = (double)NAN;
        }
        return;
    }
    ac_c_ln();                       /* tmp = (ln|z|, arg z) */
    ln_re = g_c_tmp_re;
    ln_im = g_c_tmp_im;
    g_c_a_re = w * ln_re;            /* A 寄存器被覆写（设计如此） */
    g_c_a_im = w * ln_im;
    ac_c_exp();                      /* 结果回 tmp */
}

/* ==================== 模块六：复位辅助 ==================== */

void ac_c_reset(void) {
    g_c_a_re = 0.0;
    g_c_a_im = 0.0;
    g_c_b_re = 0.0;
    g_c_b_im = 0.0;
    g_c_tmp_re = 0.0;
    g_c_tmp_im = 0.0;
}
