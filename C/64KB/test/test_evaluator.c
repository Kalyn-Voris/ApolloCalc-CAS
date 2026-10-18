/* ============================================================
 * ApolloCalc-CAS · test_evaluator.c — 求值层冒烟测试（第 2 轮）
 * 行为对齐 JS/64KB/test/test-evaluator.js；角度模式差异说明：
 * JS 测试先切 angleMode="RAD" 再跑表达式段，C 版分两段——
 * 默认 DEG 段（含 sin(30)=0.5）+ RAD 段（g_angle_deg=0 时逐条移植 JS 用例）
 * ============================================================ */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/ac_parser.h"
#include "../src/ac_evaluator.h"
#include "../src/ac_math.h"   /* 第 5 轮起 g_angle_deg 归 math 层 */

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

/* 表达式求值 @指定 x */
static void eval_at(const char *src, double x, double want, double tol) {
    int perr = ac_parse_expression(src);
    double got = ac_eval_expression_at(x);
    if (perr == ERR_NONE && g_eval_error == EVAL_OK && approx(got, want, tol)) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL eval \"%s\" @%g: got=%.17g want=%.17g perr=%d eerr=%d\n",
               src, x, got, want, perr, g_eval_error);
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

/* 方程做差值校验：evalEquationAt(x) = f(x)-g(x) */
static void eval_eq(const char *src, double x, double want, double tol) {
    int perr = ac_parse_expression(src);
    double got = ac_eval_equation_at(x);
    if (perr == ERR_NONE && g_eval_error == EVAL_OK && approx(got, want, tol)) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL eval_eq \"%s\" @%g: got=%.17g want=%.17g perr=%d eerr=%d\n",
               src, x, got, want, perr, g_eval_error);
    }
}

static void expect_true(int cond, const char *name) {
    if (cond) { s_pass++; }
    else { s_fail++; printf("FAIL %s\n", name); }
}

/* ---- 栈容量边界（静态缓冲构造，零分配） ---- */
static void test_stack_limits(void) {
    static char deep_pow[160];     /* 65 个 2 用 ^ 连接（右结合）→ 求值栈第 65 次压栈溢出 */
    static char uminus_flood[80];  /* 70 个一元负号 → 运算符栈溢出 */
    int i;

    for (i = 0; i < 65; i++) {
        deep_pow[2 * i] = '2';
        if (i < 64) { deep_pow[2 * i + 1] = '^'; }
    }
    deep_pow[129] = '\0';
    for (i = 0; i < 70; i++) { uminus_flood[i] = '-'; }
    uminus_flood[70] = '1';
    uminus_flood[71] = '\0';

    eval_err(deep_pow, EVAL_OVERFLOW);      /* EVAL_OVERFLOW=3：求值栈 64 槽 */
    eval_err(uminus_flood, EVAL_INTERNAL);  /* EVAL_INTERNAL=4：运算符栈 64 槽 */
}

/* ---- 异常 RPN 直喂 ac_eval_rpn（白盒：栈下溢 / 单操作数合法路径） ---- */
static void test_raw_rpn(void) {
    int32_t bad_add[1];
    int32_t bad_uminus[1];
    int32_t single[1];
    double r;

    bad_add[0] = ac_token_make(T_OP, OP_ADD);
    g_eval_error = EVAL_OK;
    r = ac_eval_rpn(bad_add, 1);
    expect_true(g_eval_error == EVAL_INTERNAL && isnan(r), "raw rpn lone ADD underflow");

    bad_uminus[0] = ac_token_make(T_UMINUS, 0);
    g_eval_error = EVAL_OK;
    r = ac_eval_rpn(bad_uminus, 1);
    expect_true(g_eval_error == EVAL_INTERNAL && isnan(r), "raw rpn lone UMINUS underflow");

    /* 合法单操作数 RPN：结果 = numberPool[0]，先解析 "42" 保证确定性 */
    ac_parse_expression("42");
    single[0] = ac_token_make(T_NUM, 0);
    g_eval_error = EVAL_OK;
    r = ac_eval_rpn(single, 1);
    expect_true(g_eval_error == EVAL_OK && r == 42.0, "raw rpn single NUM ok");
}

int test_evaluator_run(void) {
    /* ==================== 基础四则（默认 DEG 模式） ==================== */
    eval_ok("2+3", 5.0, 1e-12);
    eval_ok("2+3*4", 14.0, 1e-12);
    eval_ok("(2+3)*4", 20.0, 1e-12);
    eval_ok("2^3^2", 512.0, 1e-9);           /* ^ 右结合：2^(3^2) */
    eval_ok("2^-1", 0.5, 1e-12);             /* 一元负号无条件压栈 */
    eval_ok("-2^2", -4.0, 1e-12);            /* -(2^2) */
    eval_ok("--4", 4.0, 1e-12);
    eval_ok("10/4", 2.5, 1e-12);

    /* ==================== 科学计数法 ==================== */
    eval_ok("2e5", 200000.0, 1e-9);
    /* 容差 1e-24：libm 运行时 pow(10,-10) 与 V8 有数 ulp 位级差异（相对 ~1e-14），
       但仍比任何指数级解析错误（≥1.5e-11）小 13 个数量级 */
    eval_ok("1.5e-10", 1.5e-10, 1e-24);
    eval_ok("2ex", 0.0, 1e-12);              /* x=0：2*e*x = 0 */
    eval_ok("2e", 2.0 * TST_E, 1e-12);       /* 'e' 歧义判定归主循环当常数 */

    /* ==================== 隐式乘法 ==================== */
    eval_ok("2(3+4)", 14.0, 1e-12);
    eval_ok("(2+3)(4+5)", 45.0, 1e-12);

    /* ==================== 变量代入 ==================== */
    eval_at("x^2+3x-5", 2.0, 5.0, 1e-12);
    eval_at("3x^2", -2.0, 12.0, 1e-12);
    eval_at("2x^2-8", 3.0, 10.0, 1e-12);
    eval_at("sqrt(2)x", 4.0, 4.0 * sqrt(2.0), 1e-9);

    /* ==================== 函数 ==================== */
    eval_ok("sin(0)", 0.0, 1e-12);
    eval_ok("cos(0)", 1.0, 1e-12);
    eval_ok("sqrt(4)", 2.0, 1e-12);
    eval_ok("ln(1)", 0.0, 1e-12);
    eval_ok("ln(e)", 1.0, 1e-9);
    eval_ok("abs(-5)", 5.0, 1e-12);

    /* ==================== 角度模式：默认 DEG ==================== */
    eval_ok("sin(30)", 0.5, 1e-9);
    eval_ok("tan(45)", 1.0, 1e-9);
    eval_ok("atan2(1,1)", 45.0, 1e-9);       /* DEG 模式出参乘 RAD_TO_DEG */

    /* ==================== RAD 模式段（移植 JS test-evaluator.js） ==================== */
    g_angle_deg = 0;
    eval_ok("sin(pi/2)", 1.0, 1e-9);
    eval_ok("sin(pi)", 0.0, 1e-9);
    eval_ok("2pi", 2.0 * TST_PI, 1e-9);
    eval_ok("e^2", TST_E * TST_E, 1e-9);
    eval_ok("atan2(1,1)", TST_PI / 4.0, 1e-9);
    g_angle_deg = 1;                          /* 恢复默认 DEG */

    /* ==================== 错误路径 ==================== */
    eval_err("1/0", EVAL_DIV_ZERO);           /* 1 */
    eval_err("sqrt(-1)", EVAL_DOMAIN);        /* 2 */
    eval_err("ln(-1)", EVAL_DOMAIN);          /* 2 */
    eval_err("ln(0)", EVAL_OVERFLOW);         /* 3：-Inf 按溢出（JS 允许 2/3，C 确定走 3） */
    eval_err("tan(90)", EVAL_OVERFLOW);       /* 3：DEG 奇点钳制 Infinity → 溢出 */
    eval_err("0^-1", EVAL_DOMAIN);            /* 2：0^负数 */
    eval_err("()", EVAL_INTERNAL);            /* 4：空 RPN → 栈剩 0 个 */
    /* T_EQ 流入 toRPN：表达式路径必须报内部错 */
    ac_parse_expression("1=2");
    (void)ac_eval_expression_at(0.0);
    expect_true(g_eval_error == EVAL_INTERNAL, "T_EQ via expression path err=4");

    /* ==================== 方程 f(x)-g(x) ==================== */
    eval_eq("x^2-5x+6=0", 3.0, 0.0, 1e-9);
    eval_eq("x^2-5x+6=0", 2.0, 0.0, 1e-9);
    eval_eq("x^2-4=0", 3.0, 5.0, 1e-9);
    eval_eq("2x+1=x+5", 1.0, -3.0, 1e-9);
    eval_eq("2x+1=x+5", 4.0, 0.0, 1e-9);
    eval_eq("2x+6=9", 1.5, 0.0, 1e-9);
    eval_eq("x^2-5x+6", 3.0, 0.0, 1e-9);      /* 无等号 → 退化为表达式路径 */

    /* ==================== 栈容量与异常 RPN ==================== */
    test_stack_limits();
    test_raw_rpn();

    /* ==================== 复位 ==================== */
    ac_evaluator_reset();
    expect_true(g_rpn_len == 0 && g_eval_sp == 0 && g_op_sp == 0 &&
                g_eval_error == EVAL_OK && g_eval_x == 0.0 &&
                g_eval_result == 0.0 && g_lhs_count == 0 && g_rhs_count == 0 &&
                g_lhs_result == 0.0 && g_rhs_result == 0.0, "evaluator reset");

    printf("==== C99 evaluator: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
