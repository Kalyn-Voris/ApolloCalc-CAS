/* ApolloCalc · test_pro_integrate.c — 积分功能 B2 的输出形态、预算门禁与错误码测试

   测试比对的是规范化后的输出字符串：结果差一个常数倍都合法，但本实现对每个
   输入有确定的输出形态（系数折叠、NEG 外提、DIV 展平），形态一变就是契约变了。

   预算用例靠「调用前后 g_sym_count 不变」证明没写 arena：ac_pro_integrate 只
   读它做门禁，这比事后嗅探哨兵值更直接。

   自带 pro_tally_t，不 include runner 的内部头，两个 TU 之间零类型依赖。
   不声明静态对象（static 函数不占 .bss）：本 TU 的 .bss 必须是 0。 */

#include <stdio.h>
#include <string.h>

#include "../src/ac_pro.h"

#if AC_PRO_ACTIVE

typedef struct {
    int checks;
    int failed;
} pro_tally_t;

static void icheck(pro_tally_t *t, int cond, const char *name, const char *detail) {
    t->checks++;
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        t->failed++;
        printf("  [FAIL] %s  (%s)\n", name, detail);
    }
}

static int istreq(const char *a, const char *b) { return strcmp(a, b) == 0; }

static void expect_int(pro_tally_t *t, const char *expr, const char *want) {
    char out[256];
    char detail[384];
    int rc;

    out[0] = '\0';
    rc = ac_pro_integrate(expr, out, (int)sizeof(out));
    if (rc == AC_PRO_INT_OK && istreq(out, want)) {
        icheck(t, 1, expr, "");
        return;
    }
    sprintf(detail, "rc=%d got=\"%s\" want=\"%s\"", rc, out, want);
    icheck(t, 0, expr, detail);
}

/* (x+1)^2 只能走多项式归一化：表匹配看不见展开后的形状。 */
pro_tally_t test_integrate_poly(void) {
    pro_tally_t t;

    t.checks = 0;
    t.failed = 0;

    printf("[case B2-1] polynomial / linearity\n");

    expect_int(&t, "x^2", "x^3/3");
    expect_int(&t, "x", "x^2/2");
    expect_int(&t, "1", "x");
    expect_int(&t, "2x+3", "x^2+3*x");
    expect_int(&t, "x^3+2x^2-5x+1", "x^4/4+2*x^3/3-5*x^2/2+x");
    expect_int(&t, "(x+1)^2", "x^3/3+x^2+x");

    return t;
}

pro_tally_t test_integrate_trig(void) {
    pro_tally_t t;

    t.checks = 0;
    t.failed = 0;

    printf("[case B2-2] elementary functions\n");

    expect_int(&t, "sin(x)", "-cos(x)");
    expect_int(&t, "cos(x)", "sin(x)");
    expect_int(&t, "exp(x)", "exp(x)");
    expect_int(&t, "1/x", "ln(x)");
    expect_int(&t, "tan(x)", "-ln(cos(x))");    /* 输出不带绝对值 */

    return t;
}

/* 分部积分：v2.3.0 功能 4 起固定两层（parts_left 2→1→0 关死，无同形自调用）。
 * x*e^x 是任务书点名的用例；两层路径见 B2-10。 */
pro_tally_t test_integrate_by_parts(void) {
    pro_tally_t t;

    t.checks = 0;
    t.failed = 0;

    printf("[case B2-3] integration by parts (fixed two layers since v2.3.0)\n");

    expect_int(&t, "x*e^x", "exp(x)*(x-1)");
    expect_int(&t, "x*sin(x)", "-x*cos(x)+sin(x)");
    expect_int(&t, "cos(x)*x", "x*sin(x)+cos(x)");
    expect_int(&t, "x^2*exp(x)", "exp(x)*(x^2-2*x+2)");

    return t;
}

/* 表规模 128 是硬性要求（v2.3.0 功能 4），实现里还有编译期断言
 * pro_int_table_has_128_entries。老表 4 条回归 + 宏核对。 */
pro_tally_t test_integrate_table(void) {
    pro_tally_t t;

    t.checks = 0;
    t.failed = 0;

    printf("[case B2-4] 128-entry table hits\n");

    expect_int(&t, "sqrt(x)", "2*x*sqrt(x)/3");
    expect_int(&t, "ln(x)", "x*ln(x)-x");
    expect_int(&t, "1/(1+x^2)", "atan(x)");
    expect_int(&t, "1/x^2", "-1/x");

    icheck(&t, AC_PRO_INT_TABLE_SIZE == 128,
           "AC_PRO_INT_TABLE_SIZE == 128",
           "table size macro is not 128");

    return t;
}

/* 表匹配路径要 256 节点，arena 上限 512：把 g_sym_count 推到 300（剩 212）
 * 必须返回 EBUDGET，而不是硬写 arena 把上游 6 个 TU 的表达式树踩坏。 */
pro_tally_t test_integrate_budget(void) {
    pro_tally_t t;
    char out[64];
    int saved;
    int before;
    int rc;
    char detail[160];

    t.checks = 0;
    t.failed = 0;

    printf("[case B2-5] node budget exhaustion\n");

    saved = g_sym_count;

    /* 剩余 212 < 256 */
    g_sym_count = AC_SYM_MAX_NODES - 212;
    out[0] = '\0';
    rc = ac_pro_integrate("x^2", out, (int)sizeof(out));
    sprintf(detail, "g_sym_count=%d rc=%d (want %d)", g_sym_count, rc,
            AC_PRO_INT_EBUDGET);
    icheck(&t, rc == AC_PRO_INT_EBUDGET, "budget exhausted -> EBUDGET", detail);

    /* 拒绝时 arena 不动 */
    before = g_sym_count;
    icheck(&t, g_sym_count == before && g_sym_count <= AC_SYM_MAX_NODES,
           "arena untouched on EBUDGET",
           "g_sym_count changed or arena overrun");

    /* 刚好 256 应放行 */
    g_sym_count = AC_SYM_MAX_NODES - 256;
    out[0] = '\0';
    rc = ac_pro_integrate("x^2", out, (int)sizeof(out));
    sprintf(detail, "g_sym_count=%d rc=%d out=\"%s\"", g_sym_count, rc, out);
    icheck(&t, rc == AC_PRO_INT_OK && istreq(out, "x^3/3"),
           "budget exactly 256 -> OK", detail);

    /* 门禁不是粘滞状态 */
    g_sym_count = 0;
    out[0] = '\0';
    rc = ac_pro_integrate("x", out, (int)sizeof(out));
    sprintf(detail, "rc=%d out=\"%s\"", rc, out);
    icheck(&t, rc == AC_PRO_INT_OK && istreq(out, "x^2/2"),
           "restored arena -> OK", detail);

    g_sym_count = saved;  /* 交还共享 arena */
    return t;
}

/* 上层靠错误码分流，每条都必须返回精确值，不能糊成同一个。 */
pro_tally_t test_integrate_errors(void) {
    pro_tally_t t;
    char tiny[4];
    char out[64];
    int rc;
    char detail[160];

    t.checks = 0;
    t.failed = 0;

    printf("[case B2-6] error paths\n");

    out[0] = '\0';
    rc = ac_pro_integrate("", out, (int)sizeof(out));
    sprintf(detail, "rc=%d want %d", rc, AC_PRO_INT_EPARSE);
    icheck(&t, rc == AC_PRO_INT_EPARSE, "empty -> EPARSE", detail);

    rc = ac_pro_integrate("   ", out, (int)sizeof(out));
    sprintf(detail, "rc=%d want %d", rc, AC_PRO_INT_EPARSE);
    icheck(&t, rc == AC_PRO_INT_EPARSE, "blank -> EPARSE", detail);

    rc = ac_pro_integrate("z + 1", out, (int)sizeof(out));
    sprintf(detail, "rc=%d want %d", rc, AC_PRO_INT_EPARSE);
    icheck(&t, rc == AC_PRO_INT_EPARSE, "unknown var -> EPARSE", detail);

    rc = ac_pro_integrate("x^", out, (int)sizeof(out));
    sprintf(detail, "rc=%d want %d", rc, AC_PRO_INT_EPARSE);
    icheck(&t, rc == AC_PRO_INT_EPARSE, "trailing '^' -> EPARSE", detail);

    /* 不能退化成 EPARSE */
    rc = ac_pro_integrate("x^1000", out, (int)sizeof(out));
    sprintf(detail, "rc=%d want %d", rc, AC_PRO_INT_EDEGREE);
    icheck(&t, rc == AC_PRO_INT_EDEGREE, "x^1000 -> EDEGREE", detail);

    /* tiny[4] 装不下 "x^3/3"+NUL */
    tiny[0] = '\0';
    rc = ac_pro_integrate("x^2", tiny, (int)sizeof(tiny));
    sprintf(detail, "rc=%d want %d (out_len=4)", rc, AC_PRO_INT_EOVER);
    icheck(&t, rc == AC_PRO_INT_EOVER, "tiny buffer -> EOVER", detail);

    rc = ac_pro_integrate("x^2", 0, 64);
    sprintf(detail, "rc=%d want %d (out=NULL)", rc, AC_PRO_INT_EOVER);
    icheck(&t, rc == AC_PRO_INT_EOVER, "NULL out -> EOVER", detail);

    return t;
}

/* v2.3.0 功能 4：128 项新表与固定两层分部 */

/* 二次核族（QUAD_L 线性内层 / QUAD_K 完全平方 k≥2 / XQUAD）。
 * 「刚好拒绝」：1/(x²+8) 的 8 非完全平方，诚实 ETABLE。 */
pro_tally_t test_integrate_quad_kernels(void) {
    pro_tally_t t;
    char out[64];
    int rc;
    char detail[160];

    t.checks = 0;
    t.failed = 0;

    printf("[case B2-7] quadratic kernels (linear inner, perfect-square k)\n");

    expect_int(&t, "1/(1+(2*x+1)^2)", "atan(2*x+1)/2");
    expect_int(&t, "1/(x^2+4)", "atan(x/2)/2");
    expect_int(&t, "1/(x^2-9)", "ln((x-3)/(x+3))/6");
    expect_int(&t, "1/(9-x^2)", "atanh(x/3)/3");
    expect_int(&t, "sqrt(x^2+4)", "(x*sqrt(x^2+4)+4*asinh(x/2))/2");
    expect_int(&t, "x/(x^2+1)", "ln(x^2+1)/2");

    out[0] = '\0';
    rc = ac_pro_integrate("1/(x^2+8)", out, (int)sizeof(out));
    sprintf(detail, "rc=%d want %d (8 is not a perfect square)", rc,
            AC_PRO_INT_ETABLE);
    icheck(&t, rc == AC_PRO_INT_ETABLE, "1/(x^2+8) -> ETABLE", detail);

    return t;
}

/* 倒数函数族：sec/csc/cot/coth 拼法与 ln 核。 */
pro_tally_t test_integrate_recip_funcs(void) {
    pro_tally_t t;

    t.checks = 0;
    t.failed = 0;

    printf("[case B2-8] reciprocal-function family\n");

    expect_int(&t, "1/cos(x)", "ln(1/cos(x)+sin(x)/cos(x))");
    expect_int(&t, "cos(x)/sin(x)", "ln(sin(x))");
    expect_int(&t, "1/cos(x)^2", "sin(x)/cos(x)");
    expect_int(&t, "1/sin(x)^2", "-cos(x)/sin(x)");
    expect_int(&t, "sinh(x)/cosh(x)", "ln(sinh(x))");
    expect_int(&t, "sin(x)*cos(x)", "sin(x)^2/2");
    expect_int(&t, "1/cos(x)*tan(x)", "1/cos(x)");
    expect_int(&t, "ln(x)/x", "ln(x)^2/2");
    expect_int(&t, "1/(x*ln(x))", "ln(ln(x))");

    return t;
}

/* 函数幂扩充：exp^n 与三角/双曲幂（倍角/恒等式模板）。 */
pro_tally_t test_integrate_func_powers(void) {
    pro_tally_t t;

    t.checks = 0;
    t.failed = 0;

    printf("[case B2-9] function powers\n");

    expect_int(&t, "exp(x)^2", "exp(2*x)/2");
    expect_int(&t, "sin(x)^4", "3*x/8-sin(2*x)/4+sin(4*x)/32");
    expect_int(&t, "tan(x)^3", "tan(x)^2/2+ln(cos(x))");
    expect_int(&t, "sinh(x)^3", "cosh(x)^3/3-cosh(x)");

    return t;
}

/* x^n·f(x) 扩充 + 固定两层分部：
 *   x^6·sin  = 两层（x^5·cos → x^4·sin 入表）
 *   x^7·exp  = 两层（x^6·exp → x^5·exp 入表）
 * 「刚好拒绝」：x^7·sin 需要三层；x^6·ln 的 ln 不走分部且表止于 n=5。 */
pro_tally_t test_integrate_two_layer(void) {
    pro_tally_t t;
    char out[64];
    int rc;
    char detail[160];

    t.checks = 0;
    t.failed = 0;

    printf("[case B2-10] x^n*f(x) table additions and two-layer parts\n");

    expect_int(&t, "x^4*sin(x)",
               "-x^4*cos(x)+4*x^3*sin(x)+12*x^2*cos(x)-24*x*sin(x)-24*cos(x)");
    expect_int(&t, "x^4*ln(x)", "x^5*ln(x)/5-x^5/25");
    expect_int(&t, "x^2*atan(x)", "((2*x^3-x^2-1)*atan(x)+x)/6");
    expect_int(&t, "x^6*sin(x)",
               "-x^6*cos(x)+6*x^5*sin(x)+30*x^4*cos(x)-120*x^3*sin(x)"
               "-360*x^2*cos(x)+720*x*sin(x)+720*cos(x)");
    expect_int(&t, "x^7*exp(x)",
               "x^7*exp(x)-7*x^6*exp(x)"
               "+42*exp(x)*(x^5-5*x^4+20*x^3-60*x^2+120*x-120)");

    out[0] = '\0';
    rc = ac_pro_integrate("x^7*sin(x)", out, (int)sizeof(out));
    sprintf(detail, "rc=%d want %d (three layers needed)", rc,
            AC_PRO_INT_ETABLE);
    icheck(&t, rc == AC_PRO_INT_ETABLE, "x^7*sin(x) -> ETABLE", detail);

    out[0] = '\0';
    rc = ac_pro_integrate("x^6*ln(x)", out, (int)sizeof(out));
    sprintf(detail, "rc=%d want %d (ln not in parts, table stops at n=5)", rc,
            AC_PRO_INT_ETABLE);
    icheck(&t, rc == AC_PRO_INT_ETABLE, "x^6*ln(x) -> ETABLE", detail);

    return t;
}

#endif /* AC_PRO_ACTIVE */
