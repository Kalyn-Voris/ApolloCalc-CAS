/* ============================================================
 * ApolloCalc-CAS · ac_math.c — 数学函数层（第 5 轮）
 * C99 64KB / 静态内存 / Zero-GC：禁 malloc、禁 VLA、禁递归
 * 对等基准：JS/64KB/src/core_math_functions.js（逐分支行为一致）
 * 本轮从 ac_evaluator.c 整体迁入：
 *   1) 4 个双精度常量（现定义在 ac_math.h 供 evaluator 的 T_CONST 共享）；
 *   2) 角度模式 g_angle_deg（默认 DEG，对应 JS angleMode="DEG"）；
 *   3) applyFunction 派发器（12 分支，原为 evaluator 内 static，行为逐位不变）。
 * Part B：三角查表加速 —— AC_MATH_USE_LUT 编译期开关，默认关闭走 libm。
 * ============================================================ */
#include "ac_math.h"

/* ==================== Part B：sin/cos 查表（默认不编译） ====================
 * 须先于三角封装定义：ac_math_sin / ac_math_cos 的条件分支引用本段静态表。 */
#ifdef AC_MATH_USE_LUT

/* [English] 1024-entry sin/cos lookup table with linear interpolation.
   Table covers [0, 2*pi). Worst-case interpolation error h^2/8 ~ 4.7e-6. */
#define AC_LUT_SIZE 1024
static double s_lut_sin[AC_LUT_SIZE];
static double s_lut_cos[AC_LUT_SIZE];
static int    s_lut_ready = 0;

void ac_math_lut_init(void) {
    int i;
    for (i = 0; i < AC_LUT_SIZE; i++) {
        double t = 2.0 * AC_PI * i / AC_LUT_SIZE;
        s_lut_sin[i] = sin(t);
        s_lut_cos[i] = cos(t);
    }
    s_lut_ready = 1;
}

int ac_math_lut_ready(void) {
    return s_lut_ready;
}

/* [English] Linear interpolation lookup. Non-finite guard first: libm
   sin/cos(NaN/±Inf) = NaN, but the range-reduction loops below would never
   terminate on ±Inf and would invoke UB on the (int)NaN cast. */
static double ac_lut_lookup(const double *table, double rad) {
    double t = rad;
    double two_pi = 2.0 * AC_PI;
    double fi;
    int i0;
    int i1;
    double frac;

    if (t != t || t == (double)INFINITY || t == -(double)INFINITY) {
        return (double)NAN;
    }
    /* 归一化到 [0, 2π) */
    while (t < 0.0) { t += two_pi; }
    while (t >= two_pi) { t -= two_pi; }
    /* 线性插值 */
    fi = t * AC_LUT_SIZE / two_pi;
    i0 = (int)fi;
    if (i0 >= AC_LUT_SIZE) { i0 = AC_LUT_SIZE - 1; }
    i1 = i0 + 1;
    if (i1 >= AC_LUT_SIZE) { i1 = 0; }
    frac = fi - (double)i0;
    return table[i0] * (1.0 - frac) + table[i1] * frac;
}

#endif /* AC_MATH_USE_LUT */

/* ==================== 角度模式 ==================== */
int g_angle_deg = 1;

/* ==================== 三角函数（DEG/RAD 封装，与原内联实现逐字一致） ==================== */

double ac_math_sin(double v) {
    double rad = g_angle_deg ? v * AC_DEG_TO_RAD : v;
#ifdef AC_MATH_USE_LUT
    if (s_lut_ready) { return ac_lut_lookup(s_lut_sin, rad); }
#endif
    return sin(rad);
}

double ac_math_cos(double v) {
    double rad = g_angle_deg ? v * AC_DEG_TO_RAD : v;
#ifdef AC_MATH_USE_LUT
    if (s_lut_ready) { return ac_lut_lookup(s_lut_cos, rad); }
#endif
    return cos(rad);
}

double ac_math_tan(double v) {
    double r = g_angle_deg ? tan(v * AC_DEG_TO_RAD) : tan(v);
    if (fabs(r) > 1e10) { return (double)INFINITY; }
    return r;
}

double ac_math_asin(double v) {
    return g_angle_deg ? asin(v) * AC_RAD_TO_DEG : asin(v);
}

double ac_math_acos(double v) {
    return g_angle_deg ? acos(v) * AC_RAD_TO_DEG : acos(v);
}

double ac_math_atan(double v) {
    return g_angle_deg ? atan(v) * AC_RAD_TO_DEG : atan(v);
}

double ac_math_atan2(double y, double x) {
    return g_angle_deg ? atan2(y, x) * AC_RAD_TO_DEG : atan2(y, x);
}

/* ==================== 非三角函数（libm 直通） ==================== */

double ac_math_ln(double v)    { return log(v); }
double ac_math_log10(double v) { return log10(v); }
double ac_math_exp(double v)   { return exp(v); }
double ac_math_abs(double v)   { return fabs(v); }

/* ==================== applyFunction 派发器（12 分支） ====================
 * funcId 与 JS applyFunction 一致；atan2 双参：arg1 = y（对边），arg2 = x（邻边）；
 * 其余函数只读 arg1。tan 奇点钳制 |r|>1e10 → Inf（与 JS mathTan 一致）。
 */
double ac_apply_function(int func_id, double arg1, double arg2) {
    switch (func_id) {
    case F_SIN:   return ac_math_sin(arg1);
    case F_COS:   return ac_math_cos(arg1);
    case F_TAN:   return ac_math_tan(arg1);
    case F_ASIN:  return ac_math_asin(arg1);
    case F_ACOS:  return ac_math_acos(arg1);
    case F_ATAN:  return ac_math_atan(arg1);
    case F_ATAN2: return ac_math_atan2(arg1, arg2);
    case F_SQRT:  return sqrt(arg1);
    case F_LN:    return ac_math_ln(arg1);
    case F_LOG:   return ac_math_log10(arg1);
    case F_EXP:   return ac_math_exp(arg1);
    case F_ABS:   return ac_math_abs(arg1);
    default:      return (double)NAN;
    }
}
