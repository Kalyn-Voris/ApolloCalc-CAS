/* ============================================================
 * ApolloCalc-CAS · test_complex.c — 复数基础运算层测试（第 6 轮 + 128KB 第 2 轮）
 * Part A：基础四则（add/sub/mul/div + 除零 NaN 路径）
 * Part B：模长 / 辐角 / 共轭
 * Part C：cSqrt 三段分支（零元 / 纯实数 / 极坐标）+ 平方回验
 * Part D：cPowInt 快速幂（正/零/负指数、恒等、NaN 路径）
 * Part F（128KB 第 2 轮）：cExp / cLn / cPowReal——行为基准
 *         JS/128KB/src/core_complex.js 模块五，数值经 Node 核算；
 *         cPowReal 覆写 A 寄存器是头文件写明的设计契约，有专测钉住
 * Part E：复位
 * 跨语言：XL-C01…C12 与 test/test_complex_xlang.js 逐一对应，容差 1e-9。
 * ============================================================ */
#include <stdio.h>
#include <math.h>
#include "../src/ac_types.h"
#include "../src/ac_complex.h"

static int s_pass = 0;
static int s_fail = 0;

#define TST_PI 3.141592653589793
#define TST_E  2.718281828459045

static int approx(double a, double b, double tol) {
    double d = a - b;
    if (d < 0.0) { d = -d; }
    return d <= tol;
}

static void expect_true(int cond, const char *name) {
    if (cond) { s_pass++; }
    else { s_fail++; printf("FAIL %s\n", name); }
}

/* 寄存器装载助手 */
static void set_a(double re, double im) { g_c_a_re = re; g_c_a_im = im; }
static void set_b(double re, double im) { g_c_b_re = re; g_c_b_im = im; }

/* TMP == (want_re, want_im)（容差 tol） */
static void expect_tmp2(double wr, double wi, double tol, const char *name) {
    if (approx(g_c_tmp_re, wr, tol) && approx(g_c_tmp_im, wi, tol)) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL %s: got=(%.17g, %.17g) want=(%.17g, %.17g)\n",
               name, g_c_tmp_re, g_c_tmp_im, wr, wi);
    }
}

/* TMP 两分量均为 NaN（除零 / 0 的负次幂路径） */
static void expect_tmp_nan(const char *name) {
    if (isnan(g_c_tmp_re) && isnan(g_c_tmp_im)) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL %s: got=(%.17g, %.17g) want=(NaN, NaN)\n",
               name, g_c_tmp_re, g_c_tmp_im);
    }
}

/* ==================== Part A：基础四则 ==================== */
static void test_arith(void) {
    /* (3+4i) + (1-2i) = 4+2i */
    set_a(3.0, 4.0); set_b(1.0, -2.0);
    ac_c_add();
    expect_tmp2(4.0, 2.0, 1e-12, "add (3+4i)+(1-2i)");

    /* (3+4i) - (1-2i) = 2+6i */
    set_a(3.0, 4.0); set_b(1.0, -2.0);
    ac_c_sub();
    expect_tmp2(2.0, 6.0, 1e-12, "sub (3+4i)-(1-2i)");

    /* (3+4i) * (1-2i) = 11-2i */
    set_a(3.0, 4.0); set_b(1.0, -2.0);
    ac_c_mul();
    expect_tmp2(11.0, -2.0, 1e-12, "mul (3+4i)*(1-2i)");

    /* 纯虚数相乘：(2i)*(3i) = -6（实部交叉项为负） */
    set_a(0.0, 2.0); set_b(0.0, 3.0);
    ac_c_mul();
    expect_tmp2(-6.0, 0.0, 1e-12, "mul (2i)*(3i)=-6");

    /* (1+2i) / (3-4i) = (-1+2i)/5 = -0.2+0.4i（实部 a·c+b·d，虚部 b·c-a·d） */
    set_a(1.0, 2.0); set_b(3.0, -4.0);
    ac_c_div();
    expect_tmp2(-0.2, 0.4, 1e-12, "div (1+2i)/(3-4i)");

    /* 除以纯虚数：(3+4i)/i = 4-3i */
    set_a(3.0, 4.0); set_b(0.0, 1.0);
    ac_c_div();
    expect_tmp2(4.0, -3.0, 1e-12, "div (3+4i)/i=4-3i");

    /* 除零：整体置 NaN，交由上层判定无效根 */
    set_a(1.0, 1.0); set_b(0.0, 0.0);
    ac_c_div();
    expect_tmp_nan("div by zero -> NaN");
}

/* ==================== Part B：模长 / 辐角 / 共轭 ==================== */
static void test_abs_arg_conj(void) {
    /* |3+4i| = 5 */
    set_a(3.0, 4.0);
    expect_true(approx(ac_c_abs(), 5.0, 1e-12), "abs |3+4i|=5");

    /* |0| = 0 */
    set_a(0.0, 0.0);
    expect_true(ac_c_abs() == 0.0, "abs |0|=0");

    /* |-3| = 3（纯实负数） */
    set_a(-3.0, 0.0);
    expect_true(ac_c_abs() == 3.0, "abs |-3|=3");

    /* arg(1+i) = π/4 */
    set_a(1.0, 1.0);
    expect_true(approx(ac_c_arg(), TST_PI * 0.25, 1e-12), "arg(1+i)=pi/4");

    /* arg(-1) = π（负实轴） */
    set_a(-1.0, 0.0);
    expect_true(approx(ac_c_arg(), TST_PI, 1e-12), "arg(-1)=pi");

    /* arg(0+0i) = 0（atan2(0,0) 约定） */
    set_a(0.0, 0.0);
    expect_true(ac_c_arg() == 0.0, "arg(0)=0");

    /* arg(-1-i) = -3π/4（下象限，atan2 符号约定） */
    set_a(-1.0, -1.0);
    expect_true(approx(ac_c_arg(), -TST_PI * 0.75, 1e-12), "arg(-1-i)=-3pi/4");

    /* conj(3+4i) = 3-4i */
    set_a(3.0, 4.0);
    ac_c_conj();
    expect_tmp2(3.0, -4.0, 1e-12, "conj(3+4i)");

    /* conj(-2) = -2（纯实数不变） */
    set_a(-2.0, 0.0);
    ac_c_conj();
    expect_tmp2(-2.0, 0.0, 0.0, "conj(-2)");

    /* conj(5i) = -5i（纯虚数翻转） */
    set_a(0.0, 5.0);
    ac_c_conj();
    expect_tmp2(0.0, -5.0, 0.0, "conj(5i)=-5i");
}

/* ==================== Part C：cSqrt 三段分支 ==================== */
static void test_sqrt(void) {
    /* 零元：sqrt(0+0i) = 0+0i */
    set_a(0.0, 0.0);
    ac_c_sqrt();
    expect_tmp2(0.0, 0.0, 0.0, "sqrt(0)");

    /* 正实数：sqrt(4) = 2（走纯实数捷径，绕开 atan2） */
    set_a(4.0, 0.0);
    ac_c_sqrt();
    expect_tmp2(2.0, 0.0, 0.0, "sqrt(4)=2");

    /* 负实数：sqrt(-4) = 2i（约定取正虚部分支） */
    set_a(-4.0, 0.0);
    ac_c_sqrt();
    expect_tmp2(0.0, 2.0, 0.0, "sqrt(-4)=2i");

    /* 一般情形（极坐标法）：sqrt(3+4i) = 2+i */
    set_a(3.0, 4.0);
    ac_c_sqrt();
    expect_tmp2(2.0, 1.0, 1e-9, "sqrt(3+4i)=2+i");

    /* 平方回验：(2+i)^2 = 3+4i */
    set_a(g_c_tmp_re, g_c_tmp_im);
    set_b(g_c_tmp_re, g_c_tmp_im);
    ac_c_mul();
    expect_tmp2(3.0, 4.0, 1e-9, "sqrt(3+4i)^2 roundtrip");

    /* 第二个一般情形：sqrt(5+12i) = 3+2i */
    set_a(5.0, 12.0);
    ac_c_sqrt();
    expect_tmp2(3.0, 2.0, 1e-9, "sqrt(5+12i)=3+2i");

    /* 平方回验：(3+2i)^2 = 5+12i */
    set_a(g_c_tmp_re, g_c_tmp_im);
    set_b(g_c_tmp_re, g_c_tmp_im);
    ac_c_mul();
    expect_tmp2(5.0, 12.0, 1e-9, "sqrt(5+12i)^2 roundtrip");
}

/* ==================== Part D：cPowInt 快速幂 ==================== */
static void test_pow_int(void) {
    /* (1+i)^3 = -2+2i */
    set_a(1.0, 1.0);
    ac_c_pow_int(3);
    expect_tmp2(-2.0, 2.0, 1e-12, "pow (1+i)^3=-2+2i");

    /* (1+i)^10 = 32i（平方-乘法：10=1010b） */
    set_a(1.0, 1.0);
    ac_c_pow_int(10);
    expect_tmp2(0.0, 32.0, 1e-12, "pow (1+i)^10=32i");

    /* 一次幂恒等：(1+i)^1 = 1+i */
    set_a(1.0, 1.0);
    ac_c_pow_int(1);
    expect_tmp2(1.0, 1.0, 0.0, "pow (1+i)^1 identity");

    /* 零次幂：z^0 = 1+0i */
    set_a(5.0, 7.0);
    ac_c_pow_int(0);
    expect_tmp2(1.0, 0.0, 0.0, "pow (5+7i)^0=1");

    /* 负指数（实数）：2^-2 = 0.25 */
    set_a(2.0, 0.0);
    ac_c_pow_int(-2);
    expect_tmp2(0.25, 0.0, 1e-15, "pow 2^-2=0.25");

    /* 负指数（复数）：(1+i)^-2 = -i/2 */
    set_a(1.0, 1.0);
    ac_c_pow_int(-2);
    expect_tmp2(0.0, -0.5, 1e-15, "pow (1+i)^-2=-i/2");

    /* 大指数（验证平方-乘法而非朴素连乘）：2^10 = 1024 */
    set_a(2.0, 0.0);
    ac_c_pow_int(10);
    expect_tmp2(1024.0, 0.0, 1e-9, "pow 2^10=1024");

    /* i^4 = 1 */
    set_a(0.0, 1.0);
    ac_c_pow_int(4);
    expect_tmp2(1.0, 0.0, 1e-12, "pow i^4=1");

    /* (-2)^3 = -8（负底数整幂） */
    set_a(-2.0, 0.0);
    ac_c_pow_int(3);
    expect_tmp2(-8.0, 0.0, 1e-12, "pow (-2)^3=-8");

    /* 0 的负次幂：NaN */
    set_a(0.0, 0.0);
    ac_c_pow_int(-1);
    expect_tmp_nan("pow 0^-1 = NaN");
}

/* ==================== Part F：cExp / cLn / cPowReal（128KB 第 2 轮） ====================
 * 行为基准：JS/128KB/src/core_complex.js 模块五。数值基准经 Node 核算：
 *   e^(iπ)   = (-1, 1.2246e-16)        ln(-1)    = (0, π)
 *   2^3      = (7.999999999999998, 0)  ← e^(3·ln2) 舍入，容差 1e-14 非精确 8
 *   (-1)^0.5 = (6.12e-17, 1)           (-8)^(1/3)= (1, √3)（主值分支）
 *   e^(1+i)  = (1.4686939399158851, 2.2873552871788423) */
static void test_exp_ln_pow_real(void) {
    /* --- cExp --- */
    set_a(0.0, TST_PI);
    ac_c_exp();
    expect_tmp2(-1.0, 0.0, 1e-15, "cExp e^(i*pi) = -1");

    set_a(0.0, 0.0);
    ac_c_exp();
    expect_tmp2(1.0, 0.0, 1e-15, "cExp e^0 = 1");

    set_a(1.0, 1.0);
    ac_c_exp();
    expect_tmp2(1.4686939399158851, 2.2873552871788423, 1e-9, "cExp e^(1+i)");

    /* --- cLn --- */
    set_a(-1.0, 0.0);
    ac_c_ln();
    expect_tmp2(0.0, TST_PI, 1e-9, "cLn(-1) = i*pi");

    set_a(1.0, 0.0);
    ac_c_ln();
    expect_tmp2(0.0, 0.0, 1e-15, "cLn(1) = 0");

    set_a(TST_E, 0.0);
    ac_c_ln();
    expect_tmp2(1.0, 0.0, 1e-12, "cLn(e) = 1");

    set_a(0.0, 0.0);
    ac_c_ln();
    expect_true(g_c_tmp_re == -(double)INFINITY && g_c_tmp_im == 0.0,
                "cLn(0) = -Inf + 0i");

    /* --- cPowReal --- */
    set_a(2.0, 0.0);
    ac_c_pow_real(3.0);
    expect_tmp2(8.0, 0.0, 1e-14, "cPowReal 2^3 (e^(3*ln2) rounding)");

    /* A 寄存器覆写契约（ac_complex.h 写明的设计行为）：调用后 A = w·ln z */
    expect_true(approx(g_c_a_re, 3.0 * log(2.0), 1e-12) && g_c_a_im == 0.0,
                "cPowReal clobbers A register (documented contract)");

    set_a(-1.0, 0.0);
    ac_c_pow_real(0.5);
    expect_tmp2(0.0, 1.0, 1e-9, "cPowReal (-1)^0.5 = i");

    set_a(-8.0, 0.0);
    ac_c_pow_real(1.0 / 3.0);
    expect_tmp2(1.0, sqrt(3.0), 1e-9, "cPowReal (-8)^(1/3) principal branch");

    set_a(4.0, 0.0);
    ac_c_pow_real(0.5);
    expect_tmp2(2.0, 0.0, 1e-12, "cPowReal 4^0.5 = 2");

    set_a(0.0, 0.0);
    ac_c_pow_real(0.0);
    expect_tmp_nan("cPowReal 0^0 = NaN");

    set_a(0.0, 0.0);
    ac_c_pow_real(-1.0);
    expect_tmp_nan("cPowReal 0^-1 = NaN");

    set_a(0.0, 0.0);
    ac_c_pow_real(2.0);
    expect_tmp2(0.0, 0.0, 0.0, "cPowReal 0^2 = 0");

    /* cExp∘cLn 往返恒等（非零 z，主值分支内） */
    set_a(3.0, -4.0);
    ac_c_ln();
    {
        double lr = g_c_tmp_re;
        double li = g_c_tmp_im;
        set_a(lr, li);
        ac_c_exp();
        expect_tmp2(3.0, -4.0, 1e-12, "cExp after cLn roundtrip (3-4i)");
    }
}

/* ==================== Part E：复位 ==================== */
static void test_reset(void) {
    set_a(1.0, 2.0); set_b(3.0, 4.0);
    ac_c_add();          /* 先弄脏 TMP，再验证整体清零 */
    ac_c_reset();
    expect_true(g_c_a_re == 0.0 && g_c_a_im == 0.0 &&
                g_c_b_re == 0.0 && g_c_b_im == 0.0 &&
                g_c_tmp_re == 0.0 && g_c_tmp_im == 0.0,
                "reset clears all six registers");
}

/* ==================== 跨语言用例表（XL-C01-C12） ====================
 * 数值由 test/test_complex_xlang.js（JS 64KB 内核 core_complex.js）
 * 输出直接转录；ABS/ARG 为标量结果，存于 want_re。
 * 操作码（与 JS 驱动输出同序） */
enum { XLC_ADD, XLC_SUB, XLC_MUL, XLC_DIV, XLC_ABS, XLC_ARG, XLC_CONJ, XLC_SQRT, XLC_POW };

typedef struct {
    double a_re;
    double a_im;
    double b_re;
    double b_im;
    int    op;
    int    n;          /* XLC_POW 的指数，其余忽略 */
    double want_re;
    double want_im;
} xl_crow_t;

/* ---- 由 test/test_complex_xlang.js 输出转录（node test/test_complex_xlang.js） ---- */
static const xl_crow_t XL_COMPLEX[] = {
    {3.0000000000000000, 4.0000000000000000, 1.0000000000000000, -2.0000000000000000, XLC_ADD, 0, 4.0000000000000000, 2.0000000000000000},   /* XL-C01 */
    {3.0000000000000000, 4.0000000000000000, 1.0000000000000000, -2.0000000000000000, XLC_MUL, 0, 11.000000000000000, -2.0000000000000000},   /* XL-C02 */
    {1.0000000000000000, 2.0000000000000000, 3.0000000000000000, -4.0000000000000000, XLC_DIV, 0, -0.20000000000000001, 0.40000000000000002},   /* XL-C03 */
    {3.0000000000000000, 4.0000000000000000, 0.0000000000000000, 0.0000000000000000, XLC_ABS, 0, 5.0000000000000000, 0.0000000000000000},   /* XL-C04 */
    {1.0000000000000000, 1.0000000000000000, 0.0000000000000000, 0.0000000000000000, XLC_ARG, 0, 0.78539816339744828, 0.0000000000000000},   /* XL-C05 */
    {3.0000000000000000, 4.0000000000000000, 0.0000000000000000, 0.0000000000000000, XLC_CONJ, 0, 3.0000000000000000, -4.0000000000000000},   /* XL-C06 */
    {3.0000000000000000, 4.0000000000000000, 0.0000000000000000, 0.0000000000000000, XLC_SQRT, 0, 2.0000000000000000, 1.0000000000000000},   /* XL-C07 */
    {-4.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, XLC_SQRT, 0, 0.0000000000000000, 2.0000000000000000},   /* XL-C08 */
    {1.0000000000000000, 1.0000000000000000, 0.0000000000000000, 0.0000000000000000, XLC_POW, 3, -2.0000000000000000, 2.0000000000000000},   /* XL-C09 */
    {1.0000000000000000, 1.0000000000000000, 0.0000000000000000, 0.0000000000000000, XLC_POW, 10, 0.0000000000000000, 32.000000000000000},   /* XL-C10 */
    {2.0000000000000000, 0.0000000000000000, 0.0000000000000000, 0.0000000000000000, XLC_POW, -2, 0.25000000000000000, 0.0000000000000000},   /* XL-C11 */
    {5.0000000000000000, 7.0000000000000000, 0.0000000000000000, 0.0000000000000000, XLC_POW, 0, 1.0000000000000000, 0.0000000000000000},   /* XL-C12 */
};
/* ---- XL-C 表结束（12 条） ---- */

/* 单条 XL-C 用例：装载 A/B → 按操作码驱动 → 比对 1e-9 */
static void check_xl_complex(const xl_crow_t *row, int idx) {
    double got_re = 0.0;
    double got_im = 0.0;
    int scalar = (row->op == XLC_ABS || row->op == XLC_ARG);

    g_c_a_re = row->a_re;
    g_c_a_im = row->a_im;
    g_c_b_re = row->b_re;
    g_c_b_im = row->b_im;

    switch (row->op) {
        case XLC_ADD:  ac_c_add();  break;
        case XLC_MUL:  ac_c_mul();  break;
        case XLC_DIV:  ac_c_div();  break;
        case XLC_ABS:  got_re = ac_c_abs(); break;
        case XLC_ARG:  got_re = ac_c_arg(); break;
        case XLC_CONJ: ac_c_conj(); break;
        case XLC_SQRT: ac_c_sqrt(); break;
        case XLC_POW:  ac_c_pow_int(row->n); break;
        default: break;
    }
    if (!scalar) {
        got_re = g_c_tmp_re;
        got_im = g_c_tmp_im;
    }
    if (approx(got_re, row->want_re, 1e-9) && approx(got_im, row->want_im, 1e-9)) {
        s_pass++;
    } else {
        s_fail++;
        printf("FAIL [complex] XL-C%02d op=%d n=%d: got=(%.17g, %.17g) want=(%.17g, %.17g)\n",
               idx, row->op, row->n, got_re, got_im, row->want_re, row->want_im);
    }
}

int test_complex_run(void) {
    test_arith();
    test_abs_arg_conj();
    test_sqrt();
    test_pow_int();
    test_exp_ln_pow_real();
    test_reset();

    /* ==================== 跨语言 XL-C01-C12 ==================== */
    {
        int i;
        for (i = 0; i < (int)(sizeof(XL_COMPLEX) / sizeof(XL_COMPLEX[0])); i++) {
            check_xl_complex(&XL_COMPLEX[i], i + 1);
        }
    }

    printf("==== C99 complex: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
