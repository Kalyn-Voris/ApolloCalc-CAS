/* ============================================================
 * ApolloCalc-CAS · test_math.c — 数学函数层测试（第 5 轮）
 * Part A：验证 applyFunction 迁出 evaluator 后行为逐位不变
 *         （DEG/RAD 三角 + 对数指数 + 奇点/域错误路径）。
 * Part B：AC_MATH_USE_LUT 开启时验证查表路径（本段默认不编译）。
 * 跨语言：XL-M01…M15 与 test/test_math_xlang.js（JS 64KB 内核）
 *         逐一对应，容差 1e-9（libm 与 V8 有 ulp 级差异）。
 * 注意：XL-M 段必须在 LUT 初始化之前执行——查表精度 ~4.7e-6
 *       （h^2/8，h=2π/1024），满足不了 1e-9；初始化后容差放宽到 1e-5。
 * ============================================================ */
#include <stdio.h>
#include <math.h>
#include "../src/ac_parser.h"
#include "../src/ac_math.h"
#include "../src/ac_evaluator.h"

static int s_pass = 0;
static int s_fail = 0;

#define TST_PI 3.141592653589793
#define TST_E  2.718281828459045

static int approx(double a, double b, double tol) {
    double d = a - b;
    if (d < 0.0) { d = -d; }
    return d <= tol;
}

/* 表达式求值 @x=0：解析须成功、错误码须 0、值须落在容差内 */
static void eval_ok(const char *src, double want, double tol) {
    int perr = ac_parse_expression(src);
    double got = ac_eval_expression_at(0.0);
    if (perr == ERR_NONE && g_eval_error == EVAL_OK && approx(got, want, tol)) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL eval \"%s\": got=%.17g want=%.17g perr=%d eerr=%d\n",
               src, got, want, perr, g_eval_error);
    }
}

/* 错误码校验：解析须成功、求值错误码须命中 */
static void eval_err(const char *src, int want_err) {
    int perr = ac_parse_expression(src);
    double got = ac_eval_expression_at(0.0);
    if (perr == ERR_NONE && g_eval_error == want_err) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL eval_err \"%s\": got eerr=%d want=%d (result=%.17g)\n",
               src, g_eval_error, want_err, got);
    }
}

static void expect_true(int cond, const char *name) {
    if (cond) { s_pass++; }
    else { s_fail++; printf("FAIL %s\n", name); }
}

/* 跨语言用例行：expr / 角度模式 / JS 侧期望值（test_math_xlang.js 转录） */
typedef struct {
    const char *expr;
    int         deg;      /* 1=DEG, 0=RAD */
    double      want;
} xl_math_row_t;

/* ---- 由 test/test_math_xlang.js 输出转录（node test/test_math_xlang.js） ---- */
static const xl_math_row_t XL_MATH[] = {
    {"sin(30)", 1, 0.49999999999999994},   /* XL-M01 */
    {"cos(60)", 1, 0.50000000000000011},   /* XL-M02 */
    {"tan(45)", 1, 0.99999999999999989},   /* XL-M03 */
    {"asin(0.5)", 1, 30.000000000000004},   /* XL-M04 */
    {"acos(0.5)", 1, 60.000000000000007},   /* XL-M05 */
    {"atan(1)", 1, 45.000000000000000},   /* XL-M06 */
    {"atan2(3,4)", 1, 36.869897645844020},   /* XL-M07 */
    {"ln(2)", 1, 0.69314718055994529},   /* XL-M08 */
    {"log(100)", 1, 2.0000000000000000},   /* XL-M09 */
    {"exp(1)", 1, 2.7182818284590451},   /* XL-M10 */
    {"sqrt(2)", 1, 1.4142135623730951},   /* XL-M11 */
    {"sin(pi/6)", 0, 0.49999999999999994},   /* XL-M12 */
    {"cos(pi/3)", 0, 0.50000000000000011},   /* XL-M13 */
    {"tan(pi/4)", 0, 0.99999999999999989},   /* XL-M14 */
    {"atan2(1,1)", 0, 0.78539816339744828},   /* XL-M15 */
};
/* ---- XL-M 表结束（15 条） ---- */

/* 单条 XL-M 用例：按行切角度模式，求值比对 1e-9 */
static void check_xl_math(const xl_math_row_t *row, int idx) {
    int perr;
    double got;
    g_angle_deg = row->deg;
    perr = ac_parse_expression(row->expr);
    got = ac_eval_expression_at(0.0);
    if (perr == ERR_NONE && g_eval_error == EVAL_OK && approx(got, row->want, 1e-9)) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [math] XL-M%02d %s deg=%d: got=%.17g want=%.17g perr=%d eerr=%d\n",
               idx, row->expr, row->deg, got, row->want, perr, g_eval_error);
    }
    g_angle_deg = 1;   /* 每行复位，避免污染后续段 */
}

/* ---- 封装函数直调（白盒：不经 parser/evaluator 的新 API 面） ---- */
static void test_direct_api(void) {
    expect_true(approx(ac_math_sin(30.0), 0.5, 1e-9), "direct ac_math_sin(30) DEG");
    expect_true(approx(ac_math_cos(60.0), 0.5, 1e-9), "direct ac_math_cos(60) DEG");
    expect_true(isinf(ac_math_tan(90.0)), "direct ac_math_tan(90) clamp to Inf");
    expect_true(approx(ac_math_asin(0.5), 30.0, 1e-9), "direct ac_math_asin(0.5) DEG out");
    expect_true(approx(ac_math_atan2(3.0, 4.0), 36.86989764584402, 1e-9),
                "direct ac_math_atan2(3,4) DEG out");
    expect_true(approx(ac_math_ln(2.0), 0.6931471805599453, 1e-9), "direct ac_math_ln(2)");
    expect_true(ac_math_log10(100.0) == 2.0, "direct ac_math_log10(100)");
    expect_true(ac_math_exp(0.0) == 1.0, "direct ac_math_exp(0)");
    expect_true(ac_math_abs(-5.0) == 5.0, "direct ac_math_abs(-5)");

    /* 派发器：单参/双参/未知 ID */
    expect_true(ac_apply_function(F_SQRT, 16.0, 0.0) == 4.0, "dispatch F_SQRT");
    expect_true(approx(ac_apply_function(F_SIN, 30.0, 0.0), 0.5, 1e-9), "dispatch F_SIN");
    expect_true(approx(ac_apply_function(F_ATAN2, 3.0, 4.0), 36.86989764584402, 1e-9),
                "dispatch F_ATAN2 both args");
    expect_true(isnan(ac_apply_function(200, 1.0, 1.0)), "dispatch unknown id -> NaN");

    /* RAD 模式直调（入参弧度、出参弧度） */
    g_angle_deg = 0;
    expect_true(approx(ac_math_sin(TST_PI / 6.0), 0.5, 1e-9), "direct ac_math_sin RAD");
    expect_true(approx(ac_math_atan2(1.0, 1.0), TST_PI / 4.0, 1e-9),
                "direct ac_math_atan2 RAD out");
    g_angle_deg = 1;   /* 恢复默认 DEG */
}

/* ---- 双曲 / 反双曲（128KB 第 2 轮）----
 * 非角度量纲：DEG/RAD 均直通不换算（JS mathSinh..mathAtanh 同款）。
 * 数值基准经 Node Math.sinh 系核算（勿凭记忆改值）。
 * 前缀不冲突：sinh/sin、cosh/cos、tanh/tan、asinh/asin、acosh/acos、
 * atanh/atan/atan2 全部走完整 parse→eval 管线验证（最长前缀优先）。 */
static void test_hyperbolic(void) {
    /* 数值（默认 DEG 模式，双曲不受影响） */
    eval_ok("sinh(1)", 1.1752011936438014, 1e-9);
    eval_ok("cosh(1)", 1.5430806348152437, 1e-9);
    eval_ok("tanh(1)", 0.7615941559557649, 1e-9);
    eval_ok("asinh(1)", 0.8813735870195430, 1e-9);
    eval_ok("acosh(2)", 1.3169578969248166, 1e-9);
    eval_ok("atanh(0.5)", 0.5493061443340548, 1e-9);

    /* 奇偶性与饱和 */
    eval_ok("asinh(-1)", -0.8813735870195430, 1e-9);
    eval_ok("sinh(-1)", -1.1752011936438014, 1e-9);
    eval_ok("tanh(20)", 1.0, 1e-12);
    eval_ok("tanh(-20)", -1.0, 1e-12);
    eval_ok("cosh(0)", 1.0, 1e-12);
    eval_ok("sinh(0)", 0.0, 1e-12);
    eval_ok("tanh(0)", 0.0, 1e-12);

    /* 恒等式 cosh²−sinh² = 1（穿过完整管线的组合表达式） */
    eval_ok("cosh(1)^2-sinh(1)^2", 1.0, 1e-9);

    /* 域错误：NaN → EVAL_DOMAIN；atanh(±1) → ±Inf → EVAL_OVERFLOW */
    eval_err("acosh(0)", EVAL_DOMAIN);
    eval_err("acosh(0.5)", EVAL_DOMAIN);
    eval_err("atanh(2)", EVAL_DOMAIN);
    eval_err("atanh(-2)", EVAL_DOMAIN);
    eval_err("atanh(1)", EVAL_OVERFLOW);
    eval_err("atanh(-1)", EVAL_OVERFLOW);

    /* RAD 模式：双曲同样不换量纲 */
    g_angle_deg = 0;
    eval_ok("sinh(1)", 1.1752011936438014, 1e-9);
    eval_ok("atanh(0.5)", 0.5493061443340548, 1e-9);
    g_angle_deg = 1;

    /* 前缀不冲突（六对同名前缀 + atan 家族三兄弟） */
    eval_ok("sin(0)", 0.0, 1e-12);
    eval_ok("sinh(0)", 0.0, 1e-12);
    eval_ok("cos(0)", 1.0, 1e-12);
    eval_ok("cosh(0)", 1.0, 1e-12);
    eval_ok("tan(0)", 0.0, 1e-12);
    eval_ok("tanh(0)", 0.0, 1e-12);
    eval_ok("asin(0)", 0.0, 1e-12);
    eval_ok("asinh(0)", 0.0, 1e-12);
    eval_ok("acos(1)", 0.0, 1e-12);
    eval_ok("acosh(1)", 0.0, 1e-12);
    eval_ok("atan(0)", 0.0, 1e-12);
    eval_ok("atanh(0)", 0.0, 1e-12);
    eval_ok("atan2(0,1)", 0.0, 1e-12);

    /* 直调 API（白盒）+ 派发器 */
    expect_true(approx(ac_math_sinh(1.0), 1.1752011936438014, 1e-12),
                "direct ac_math_sinh(1)");
    expect_true(approx(ac_math_cosh(1.0), 1.5430806348152437, 1e-12),
                "direct ac_math_cosh(1)");
    expect_true(ac_math_tanh(30.0) == 1.0, "direct ac_math_tanh(30) saturated to 1");
    expect_true(ac_math_tanh(-30.0) == -1.0, "direct ac_math_tanh(-30) saturated");
    expect_true(approx(ac_math_asinh(1.0), 0.8813735870195430, 1e-12),
                "direct ac_math_asinh(1)");
    expect_true(isnan(ac_math_acosh(0.5)), "direct ac_math_acosh(0.5) = NaN");
    expect_true(isnan(ac_math_atanh(2.0)), "direct ac_math_atanh(2) = NaN");
    expect_true(approx(ac_apply_function(F_TANH, 1.0, 0.0), 0.7615941559557649, 1e-12),
                "dispatch F_TANH");
    expect_true(approx(ac_apply_function(F_ATANH, 0.5, 0.0), 0.5493061443340548, 1e-12),
                "dispatch F_ATANH");
    expect_true(isnan(ac_apply_function(F_ACOSH, 0.0, 0.0)), "dispatch F_ACOSH(0) NaN");
}

#ifdef AC_MATH_USE_LUT
/* ---- LUT 路径：初始化、就绪标志、非有限守卫、实测精度扫描 ----
 * 前置：本段在 XL-M 之后执行（保 XL-M 的 1e-9 走 libm）。
 * 精度上限：线性插值 h^2/8 = (2π/1024)^2/8 ≈ 4.7e-6 → 容差取 1e-5。 */
static void test_lut(void) {
    double max_err_sin = 0.0;
    double max_err_cos = 0.0;
    double v;

    expect_true(ac_math_lut_ready() == 0, "LUT not ready before init");
    ac_math_lut_init();
    expect_true(ac_math_lut_ready() == 1, "LUT ready after init");

    /* 非有限守卫：与 libm sin/cos(NaN/±Inf)=NaN 语义一致（守卫同时防死循环） */
    expect_true(isnan(ac_math_sin((double)INFINITY)), "LUT sin(+Inf) -> NaN");
    expect_true(isnan(ac_math_sin(-(double)INFINITY)), "LUT sin(-Inf) -> NaN");
    expect_true(isnan(ac_math_sin((double)NAN)), "LUT sin(NaN) -> NaN");

    /* 任务示例点：实测 ~2.1e-6，容差 1e-5 */
    eval_ok("sin(30)", 0.5, 1e-5);
    eval_ok("cos(60)", 0.5, 1e-5);
    eval_ok("sin(-90)", -1.0, 1e-5);
    eval_ok("sin(0)", 0.0, 1e-5);
    eval_ok("cos(0)", 1.0, 1e-5);

    /* 差分对照：tan/asin 不走查表，仍保持 libm 1e-9 精度 */
    eval_ok("tan(45)", 1.0, 1e-9);
    eval_ok("asin(0.5)", 30.0, 1e-9);

    /* 0.5° 步进全圆扫描，实测最大插值误差（理论 4.7e-6） */
    for (v = 0.0; v < 360.0; v += 0.5) {
        double rad = v * AC_DEG_TO_RAD;
        double ds = ac_math_sin(v) - sin(rad);
        double dc = ac_math_cos(v) - cos(rad);
        if (ds < 0.0) { ds = -ds; }
        if (dc < 0.0) { dc = -dc; }
        if (ds > max_err_sin) { max_err_sin = ds; }
        if (dc > max_err_cos) { max_err_cos = dc; }
    }
    printf("LUT measured max err: sin=%.3g cos=%.3g\n", max_err_sin, max_err_cos);
    expect_true(max_err_sin < 6e-6 && max_err_cos < 6e-6, "LUT interpolation error < 6e-6");
}
#endif /* AC_MATH_USE_LUT */

int test_math_run(void) {
    /* ==================== 默认 DEG：三角全函数 ==================== */
    eval_ok("sin(30)", 0.5, 1e-9);
    eval_ok("cos(60)", 0.5, 1e-9);
    eval_ok("tan(45)", 1.0, 1e-9);
    eval_ok("asin(0.5)", 30.0, 1e-9);
    eval_ok("acos(0.5)", 60.0, 1e-9);
    eval_ok("atan(1)", 45.0, 1e-9);
    eval_ok("atan2(1,1)", 45.0, 1e-9);
    eval_ok("atan2(3,4)", 36.86989764584402, 1e-9);

    /* ==================== RAD 模式段 ==================== */
    g_angle_deg = 0;
    eval_ok("sin(pi/6)", 0.5, 1e-9);
    eval_ok("cos(pi/3)", 0.5, 1e-9);
    eval_ok("tan(pi/4)", 1.0, 1e-9);
    eval_ok("atan(1)", TST_PI / 4.0, 1e-9);
    eval_ok("atan2(1,1)", TST_PI / 4.0, 1e-9);
    g_angle_deg = 1;   /* 恢复默认 DEG */

    /* ==================== 对数 / 指数 / 绝对值 / 开方 ==================== */
    eval_ok("ln(e)", 1.0, 1e-9);
    eval_ok("ln(2)", 0.6931471805599453, 1e-9);
    eval_ok("log(100)", 2.0, 1e-9);          /* 词法只认 "log"；"log10(" 两侧一致切成 log+10 */
    eval_ok("exp(0)", 1.0, 1e-9);
    eval_ok("exp(1)", TST_E, 1e-9);
    eval_ok("abs(-5)", 5.0, 1e-12);
    eval_ok("sqrt(9)", 3.0, 1e-12);

    /* ==================== 错误路径 ==================== */
    eval_err("tan(90)", EVAL_OVERFLOW);      /* DEG 奇点钳制 Inf → 溢出 */
    eval_err("sqrt(-1)", EVAL_DOMAIN);
    eval_err("ln(-1)", EVAL_DOMAIN);
    eval_err("asin(2)", EVAL_DOMAIN);

    /* ==================== 封装直调 + 派发器 ==================== */
    test_direct_api();

    /* ==================== 双曲 / 反双曲（128KB 第 2 轮） ==================== */
    test_hyperbolic();

    /* ==================== 跨语言 XL-M01…M15（须先于 LUT 初始化） ==================== */
    {
        int i;
        for (i = 0; i < (int)(sizeof(XL_MATH) / sizeof(XL_MATH[0])); i++) {
            check_xl_math(&XL_MATH[i], i + 1);
        }
    }

    /* ==================== Part B：LUT 段（默认不编译） ==================== */
#ifdef AC_MATH_USE_LUT
    test_lut();
#endif

    printf("==== C99 math: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
