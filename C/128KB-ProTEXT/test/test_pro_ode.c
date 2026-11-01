/* ApolloCalc · test_pro_ode.c — 二阶常系数线性齐次 ODE 的冻结期望串与错误码测试

   测试比对的是 docs/contracts/ode-format-v23.md 冻结的期望串（§5.4 范例逐字）
   与错误码（§6/§7 逐条，精确比较）。期望串已按契约规则（排序/聚类/fmt/渲染）用
   一次性脚本机械复核过 11/11 一致（见契约 §5.4 注）。

   自带 pro_tally_t，不 include runner 的内部头（与 test_pro_integrate.c 同
   约定）。不声明静态对象：本 TU 的 .bss 必须是 0。 */

#include <stdio.h>
#include <string.h>

#include "../src/ac_pro.h"

#if AC_PRO_ACTIVE

typedef struct {
    int checks;
    int failed;
} pro_tally_t;

static void ocheck(pro_tally_t *t, int cond, const char *name, const char *detail) {
    t->checks++;
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        t->failed++;
        printf("  [FAIL] %s  (%s)\n", name, detail);
    }
}

static void expect_ode(pro_tally_t *t, const char *in, int want_rc,
                       const char *want, const char *name) {
    char out[256];
    char detail[384];
    int rc;

    out[0] = '\0';
    rc = ac_pro_ode(in, out, (int)sizeof(out));
    if (rc == want_rc && (want == 0 || strcmp(out, want) == 0)) {
        ocheck(t, 1, name, "");
        return;
    }
    sprintf(detail, "rc=%d got=\"%.60s\" want rc=%d \"%s\"", rc, out, want_rc,
            (want == 0) ? "<any>" : want);
    ocheck(t, 0, name, detail);
}

/* 契约 §5.4 范例 1..4（单根 / 共轭对 / 纯虚根） */
pro_tally_t test_ode_basic(void) {
    pro_tally_t t;

    t.checks = 0;
    t.failed = 0;
    printf("[case ODE-1] contract 5.4 rows 1-4\n");

    expect_ode(&t, "y''+3y'+2y=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81*exp(-1*x) + C\xe2\x82\x82*exp(-2*x)",
               "y''+3y'+2y=0");
    expect_ode(&t, "y''-3y'+2y=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81*exp(2*x) + C\xe2\x82\x82*exp(1*x)",
               "y''-3y'+2y=0");
    expect_ode(&t, "y''+2y'+5y=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81*exp(-1*x)*cos(2*x)"
               " + C\xe2\x82\x82*exp(-1*x)*sin(2*x)",
               "y''+2y'+5y=0");
    expect_ode(&t, "y''+4y=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81*cos(2*x) + C\xe2\x82\x82*sin(2*x)",
               "y''+4y=0 (α=0 省 exp)");

    return t;
}

/* 零根 / 实重根 / 一阶 / 三阶（§5.4 行 5..7、10） */
pro_tally_t test_ode_zero_repeat(void) {
    pro_tally_t t;

    t.checks = 0;
    t.failed = 0;
    printf("[case ODE-2] zero root, repeated root, first order\n");

    expect_ode(&t, "y''-4y'=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81*exp(4*x) + C\xe2\x82\x82",
               "y''-4y'=0 (λ=0 省 exp)");
    expect_ode(&t, "y''-2y'+y=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81*exp(1*x) + C\xe2\x82\x82*x*exp(1*x)",
               "y''-2y'+y=0 (实重根)");
    expect_ode(&t, "y'=0", AC_PRO_ODE_OK, "y = C\xe2\x82\x81", "y'=0");
    expect_ode(&t, "y'''-y'=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81*exp(1*x) + C\xe2\x82\x82 + C\xe2\x82\x83*exp(-1*x)",
               "y'''-y'=0");

    return t;
}

/* 四阶：共轭对排序与重根×共轭（§5.4 行 8..9） */
pro_tally_t test_ode_quartic(void) {
    pro_tally_t t;

    t.checks = 0;
    t.failed = 0;
    printf("[case ODE-3] quartic ordering and repeated complex pair\n");

    expect_ode(&t, "y''''-y=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81*exp(1*x) + C\xe2\x82\x82*cos(1*x)"
               " + C\xe2\x82\x83*sin(1*x) + C\xe2\x82\x84*exp(-1*x)",
               "y''''-y=0 (1 → ±i → -1)");
    expect_ode(&t, "y''''+2y''+y=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81*cos(1*x) + C\xe2\x82\x82*sin(1*x)"
               " + C\xe2\x82\x83*x*cos(1*x) + C\xe2\x82\x84*x*sin(1*x)",
               "y''''+2y''+y=0 (±i 各 2 重)");

    return t;
}

/* 有理根的 fmt 重建（§5.4 行 11） */
pro_tally_t test_ode_rational(void) {
    pro_tally_t t;

    t.checks = 0;
    t.failed = 0;
    printf("[case ODE-4] rational root reconstruction\n");

    expect_ode(&t, "2y''+2y'+y=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81*exp(-1/2*x)*cos(1/2*x)"
               " + C\xe2\x82\x82*exp(-1/2*x)*sin(1/2*x)",
               "2y''+2y'+y=0");
    expect_ode(&t, "999y''+998y'-y=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81*exp(1/999*x) + C\xe2\x82\x82*exp(-1*x)",
               "999y''+998y'-y=0 (q=999 边界,v2.4.1 回归)");
    expect_ode(&t, "3y''+2y'-y=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81*exp(1/3*x) + C\xe2\x82\x82*exp(-1*x)",
               "3y''+2y'-y=0 (1/3,v2.4.1 回归)");
    expect_ode(&t, "100y''+99y'-y=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81*exp(1/100*x) + C\xe2\x82\x82*exp(-1*x)",
               "100y''+99y'-y=0 (1/100,v2.4.1 回归)");

    return t;
}

/* 拒绝情形（§7 逐条 + §6 判定全序交叉例） */
pro_tally_t test_ode_reject(void) {
    pro_tally_t t;

    t.checks = 0;
    t.failed = 0;
    printf("[case ODE-5] rejections (contract section 7)\n");

    expect_ode(&t, "y''+y=1", AC_PRO_ODE_ENONHOMO, "", "非齐次 → -4");
    expect_ode(&t, "y''=x", AC_PRO_ODE_ENONHOMO, "", "右端 x → -4");
    expect_ode(&t, "x*y''+y=0", AC_PRO_ODE_EVARCOEF, "", "变系数 → -3");
    expect_ode(&t, "y''+x*y=0", AC_PRO_ODE_EVARCOEF, "", "变系数 → -3");
    expect_ode(&t, "x^2*y=0", AC_PRO_ODE_EVARCOEF, "", "x^2*y → -3");
    expect_ode(&t, "x^y''=0", AC_PRO_ODE_EVARCOEF, "", "x 幂值可空 → -3(v2.4.1 镜像)");
    expect_ode(&t, "y'''''+y=0", AC_PRO_ODE_EDEG, "", "5 撇号 → -2");
    expect_ode(&t, "y=0", AC_PRO_ODE_ECOEF, "", "无导数项 → -5");
    expect_ode(&t, "0y''+0y=0", AC_PRO_ODE_ECOEF, "", "恒等式 → -5");
    expect_ode(&t, "1000y''+y=0", AC_PRO_ODE_ECOEF, "", "系数 1000 → -5");
    expect_ode(&t, "y^2+y=0", AC_PRO_ODE_EPARSE, "", "^ 非法 → -1");
    expect_ode(&t, "Y''=0", AC_PRO_ODE_EPARSE, "", "大写 Y → -1");
    expect_ode(&t, "y''+y''=0", AC_PRO_ODE_EPARSE, "", "同阶重复 → -1");
    expect_ode(&t, "y''*3+y=0", AC_PRO_ODE_EPARSE, "", "y 后 '*' → -1");
    expect_ode(&t, "y''x=0", AC_PRO_ODE_EPARSE, "", "y 后尾随 x → -1(v2.4.1 裁决)");
    expect_ode(&t, "y''+y", AC_PRO_ODE_EPARSE, "", "缺 '=' → -1");
    expect_ode(&t, "2=0", AC_PRO_ODE_EPARSE, "", "缺 y → -1");
    expect_ode(&t, "y''+y=1/x", AC_PRO_ODE_EPARSE, "", "右端含文法外 '/' → -1");

    return t;
}

/* 判定全序交叉例（§6） */
pro_tally_t test_ode_order(void) {
    pro_tally_t t;

    t.checks = 0;
    t.failed = 0;
    printf("[case ODE-6] error-precedence cross cases\n");

    expect_ode(&t, "x*y''=1", AC_PRO_ODE_EVARCOEF, "",
               "EVARCOEF 先于 ENONHOMO");
    expect_ode(&t, "x$+y''=0", AC_PRO_ODE_EPARSE, "",
               "结构错先于 x 标志");
    expect_ode(&t, "y'''''+x=0", AC_PRO_ODE_EDEG, "",
               "EDEG 词法当场，先于一切标志");

    return t;
}

/* 边界值（§9-4）：阶 4 刚好通过 / 系数 999/1000 / EOVER */
pro_tally_t test_ode_bounds(void) {
    pro_tally_t t;
    char tiny[8];
    char detail[64];
    int rc;

    t.checks = 0;
    t.failed = 0;
    printf("[case ODE-7] boundary values\n");

    expect_ode(&t, "y''''=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81 + C\xe2\x82\x82*x + C\xe2\x82\x83*x^2"
               " + C\xe2\x82\x84*x^3",
               "阶 4 刚好通过（λ=0 四重）");
    expect_ode(&t, "999y''+999y'=0", AC_PRO_ODE_OK,
               "y = C\xe2\x82\x81 + C\xe2\x82\x82*exp(-1*x)",
               "系数 999 刚好通过");

    tiny[0] = '\0';
    rc = ac_pro_ode("y''+3y'+2y=0", tiny, (int)sizeof(tiny));
    sprintf(detail, "rc=%d want %d", rc, AC_PRO_ODE_EOVER);
    ocheck(&t, rc == AC_PRO_ODE_EOVER, "tiny buffer → EOVER", detail);

    return t;
}

#endif /* AC_PRO_ACTIVE */
