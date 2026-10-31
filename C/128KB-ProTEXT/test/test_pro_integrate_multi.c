/* ApolloCalc · test_pro_integrate_multi.c — 多变量多项式积分（ac_pro_integrate_multi）的单元测试，10 项测试

   接口与 test_pro_multi.c 同款：本文件定义自己的 pro_tally_t / pcheck，测试函数
   返回 pro_tally_t（checks / failed），runner 只汇总。本文件不声明任何静态对象
   （函数不占 .bss），链接进 runner 后 runner 口径的 .bss 增量仍是 0。

   错误通道：ac_pro_integrate_multi 的返回契约是 1 = 成功；-1 = 一般错误（粗因看
   g_solve_error）；AC_PRO_MVI_ENONEXACT(-6) = 非精确整除（直接作为返回值，
   g_solve_error 同时为 2）。校验按此逐条核对两个通道（见 ac_pro.h 的「返回 /
   Error channel」注释）。

   任务卡的两条用例在「精确整除」硬约束下算错了被积函数，这里修正系数、保留任务
   卡期望的输出串。任务卡写 ∫(x*y^2)dy == "x*y^3"，但 ∫x·y²dy = x·y³/3，1 % 3 != 0，
   按整除约束必须拒绝，期望串只在被积函数为 3*x*y^2 时成立（3/3 = 1），故修正为
   ∫(3*x*y^2)dy == "x*y^3"，保留「dy 积分 happy path」的覆盖意图。任务卡写
   ∫(2*x^3)dx == "x^4"（整除 happy path），但 ∫2x³dx = x⁴/2，2 % 4 != 0 且无整系数
   原函数，故修正为 ∫(4*x^3)dx == "x^4"（4/4 = 1），保留任务卡的期望串与「整除通过」
   的覆盖意图。任务卡自己的负向用例 ∫(x*y^2)dx（1/2 非整）与上述修正自洽：它验证的
   正是这两处修正掉的那类输入。 */

#include <stdio.h>
#include <string.h>

#include "../src/ac_pro.h"
#include "../../../C/128KB/src/ac_solvers.h"

#if AC_PRO_ACTIVE

typedef struct {
    int checks;
    int failed;
} pro_tally_t;

static void pcheck(pro_tally_t *t, int cond, const char *name, const char *detail) {
    t->checks++;
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        t->failed++;
        printf("  [FAIL] %s  (%s)\n", name, detail);
    }
}

static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

/* 多变量积分（10 项测试）

   覆盖：∫·dx 逐项 happy path（含 y 因子保持）；∫·dy happy path；非精确整除
   -> AC_PRO_MVI_ENONEXACT(-6) 且 g_solve_error = 2；整除 happy path（4/4 = 1）；
   常数项（除数 1 恒整除）；零多项式 -> "0"；次数超限 dx（6+1 = 7 > 6）-> -1 /
   g_solve_error = 1；次数超限 dx（3+1+3 = 7 > 6）-> -1 / g_solve_error = 1；超越
   函数 sin -> 解析期拒绝 -> -1 / g_solve_error = 2；var_index 非法 -> -1 /
   g_solve_error = 2。 */
pro_tally_t test_integrate_multi(void) {
    pro_tally_t t;
    char out[128];
    int rc;

    t.checks = 0;
    t.failed = 0;

    printf("[case MVI-1] integral(3*x^2*y) dx = x^3*y\n");
    out[0] = '\0';
    g_solve_error = 0;
    rc = ac_pro_integrate_multi("3*x^2*y", 0, out, (int)sizeof(out));
    pcheck(&t, rc == 1 && streq(out, "x^3*y"),
           "returns 1, result 'x^3*y'", out);
    printf("         result: %s\n", out);

    /* 任务卡算术修正:被积函数 3*x*y^2（x*y^2 的原函数是 x*y^3/3，非整） */
    printf("[case MVI-2] integral(3*x*y^2) dy = x*y^3\n");
    out[0] = '\0';
    g_solve_error = 0;
    rc = ac_pro_integrate_multi("3*x*y^2", 1, out, (int)sizeof(out));
    pcheck(&t, rc == 1 && streq(out, "x*y^3"),
           "returns 1, result 'x*y^3' (y-integration path)", out);
    printf("         result: %s\n", out);

    printf("[case MVI-3] integral(x*y^2) dx -> non-exact (x^2*y^2/2)\n");
    out[0] = '\0';
    g_solve_error = 0;
    rc = ac_pro_integrate_multi("x*y^2", 0, out, (int)sizeof(out));
    pcheck(&t, rc == AC_PRO_MVI_ENONEXACT && g_solve_error == 2,
           "returns ENONEXACT(-6), g_solve_error == 2", out);
    printf("         rc = %d, g_solve_error = %d\n", rc, g_solve_error);

    /* 任务卡算术修正:被积函数 4*x^3（2*x^3 的原函数是 x^4/2，非整） */
    printf("[case MVI-4] integral(4*x^3) dx = x^4  (exact division happy path)\n");
    out[0] = '\0';
    g_solve_error = 0;
    rc = ac_pro_integrate_multi("4*x^3", 0, out, (int)sizeof(out));
    pcheck(&t, rc == 1 && streq(out, "x^4"),
           "returns 1, result 'x^4'", out);
    printf("         result: %s\n", out);

    printf("[case MVI-5] integral(5) dx = 5*x  (constant term, divisor 1)\n");
    out[0] = '\0';
    g_solve_error = 0;
    rc = ac_pro_integrate_multi("5", 0, out, (int)sizeof(out));
    pcheck(&t, rc == 1 && streq(out, "5*x"),
           "returns 1, result '5*x'", out);
    printf("         result: %s\n", out);

    printf("[case MVI-6] integral(0) dx = 0  (zero polynomial is a success)\n");
    out[0] = '\0';
    g_solve_error = 0;
    rc = ac_pro_integrate_multi("0", 0, out, (int)sizeof(out));
    pcheck(&t, rc == 1 && streq(out, "0"),
           "returns 1, result '0'", out);
    printf("         result: %s\n", out);

    printf("[case MVI-7] integral(x^6) dx -> degree 7 > 6, rejected\n");
    g_solve_error = 0;
    rc = ac_pro_integrate_multi("x^6", 0, out, (int)sizeof(out));
    pcheck(&t, rc == -1 && g_solve_error == 1,
           "returns -1, g_solve_error == 1 (degree)", "accepted degree 7");
    printf("         rc = %d, g_solve_error = %d\n", rc, g_solve_error);

    printf("[case MVI-8] integral(x^3*y^3) dx -> total degree 7 > 6, rejected\n");
    g_solve_error = 0;
    rc = ac_pro_integrate_multi("x^3*y^3", 0, out, (int)sizeof(out));
    pcheck(&t, rc == -1 && g_solve_error == 1,
           "returns -1, g_solve_error == 1 (degree)", "accepted degree 7");
    printf("         rc = %d, g_solve_error = %d\n", rc, g_solve_error);

    printf("[case MVI-9] integral(sin(x)) dx -> parse-time rejection (pure-poly parser)\n");
    g_solve_error = 0;
    rc = ac_pro_integrate_multi("sin(x)", 0, out, (int)sizeof(out));
    pcheck(&t, rc == -1 && g_solve_error == 2,
           "returns -1, g_solve_error == 2 (transcendental = syntax at parse)", "accepted sin");
    printf("         rc = %d, g_solve_error = %d\n", rc, g_solve_error);

    printf("[case MVI-10] var_index = 2 -> rejected\n");
    g_solve_error = 0;
    rc = ac_pro_integrate_multi("x", 2, out, (int)sizeof(out));
    pcheck(&t, rc == -1 && g_solve_error == 2,
           "returns -1, g_solve_error == 2 (bad var_index)", "accepted var_index 2");
    printf("         rc = %d, g_solve_error = %d\n", rc, g_solve_error);

    return t;
}

#endif /* AC_PRO_ACTIVE */
