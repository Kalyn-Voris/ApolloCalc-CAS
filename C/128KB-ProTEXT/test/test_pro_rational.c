/* ApolloCalc · test_pro_rational.c — 有理函数四则 / GCD / cancel / apart 的形态与错误码测试

   测试比对的是规范化后的输出字符串（形态契约）：降幂、±1 折叠、负号并项、
   cancel 恒 "(P)/(Q)"、apart 按 r 降序 " + " 连接。错误码精确比较。

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

static void rcheck(pro_tally_t *t, int cond, const char *name, const char *detail) {
    t->checks++;
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        t->failed++;
        printf("  [FAIL] %s  (%s)\n", name, detail);
    }
}

static void expect_rat(pro_tally_t *t, int rc, const char *out,
                       int want_rc, const char *want, const char *name) {
    char detail[384];
    if (rc == want_rc && (want == 0 || strcmp(out, want) == 0)) {
        rcheck(t, 1, name, "");
        return;
    }
    sprintf(detail, "rc=%d got=\"%s\" want rc=%d \"%s\"", rc, out, want_rc,
            (want == 0) ? "<any>" : want);
    rcheck(t, 0, name, detail);
}

/* 多项式四则（+ - *）与次数边界 */
pro_tally_t test_rat_arith(void) {
    pro_tally_t t;
    char out[128];
    int rc;

    t.checks = 0;
    t.failed = 0;
    printf("[case RAT-1] polynomial arithmetic\n");

    rc = ac_pro_rat_arith("x^2+1", '+', "x-1", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "x^2+x", "x^2+1 + x-1");

    rc = ac_pro_rat_arith("x^2-1", '-', "x^2-1", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "0", "x^2-1 - x^2-1");

    rc = ac_pro_rat_arith("(x+1)", '*', "(x-1)", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_EPARSE, "", "括号 → EPARSE");

    rc = ac_pro_rat_arith("x+1", '*', "x-1", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "x^2-1", "(x+1)*(x-1)");

    rc = ac_pro_rat_arith("x^8", '*', "x", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_EDEG, "", "9 次积 → EDEG");

    rc = ac_pro_rat_arith("x^8", '^', "x", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_EPARSE, "", "非法 op → EPARSE");

    return t;
}

/* 欧几里得 GCD（本原化、互素、零元） */
pro_tally_t test_rat_gcd(void) {
    pro_tally_t t;
    char out[128];
    int rc;

    t.checks = 0;
    t.failed = 0;
    printf("[case RAT-2] euclid gcd\n");

    rc = ac_pro_rat_gcd("x^2-1", "x^2-3x+2", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "x-1", "gcd(x^2-1, x^2-3x+2)");

    rc = ac_pro_rat_gcd("x^2+1", "x-1", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "1", "互素 → 1");

    rc = ac_pro_rat_gcd("2x+2", "4x+4", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "x+1", "内容不进 gcd（本原化）");

    rc = ac_pro_rat_gcd("x^3-1", "x^2-1", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "x-1", "gcd(x^3-1, x^2-1)");

    rc = ac_pro_rat_gcd("0", "x+1", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "x+1", "gcd(0, g) = primitive(g)");

    rc = ac_pro_rat_gcd("0", "0", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_EDOM, "", "gcd(0,0) → EDOM");

    return t;
}

/* cancel 约分（形态契约恒 "(P)/(Q)"） */
pro_tally_t test_rat_cancel(void) {
    pro_tally_t t;
    char out[128];
    int rc;

    t.checks = 0;
    t.failed = 0;
    printf("[case RAT-3] cancel\n");

    rc = ac_pro_rat_cancel("x^2-1/x^2-3x+2", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "(x+1)/(x-2)", "cancel((x²-1)/(x²-3x+2))");

    rc = ac_pro_rat_cancel("2x/4", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "(x)/(2)", "整数容差约尽");

    rc = ac_pro_rat_cancel("x/x", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "1", "Q=1 → 只出分子");

    rc = ac_pro_rat_cancel("x^2/x^2", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "1", "x^2/x^2 → 1");

    rc = ac_pro_rat_cancel("0/x", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "0", "分子为零 → 0");

    rc = ac_pro_rat_cancel("x^2+1", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_EPARSE, "", "缺 '/' → EPARSE");

    rc = ac_pro_rat_cancel("x/(x+1)/(x-1)", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_EPARSE, "", "两个 '/' → EPARSE");

    return t;
}

/* apart（deg ≤ 4 精确根路径）
 * 「刚好拒绝」三连：重根 / 复根 / 假分式。 */
pro_tally_t test_rat_apart_exact(void) {
    pro_tally_t t;
    char out[192];
    int rc;

    t.checks = 0;
    t.failed = 0;
    printf("[case RAT-4] apart (exact roots, deg <= 4)\n");

    rc = ac_pro_rat_apart("1/x^2-1", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK,
               "1/2/(x - 1) + -1/2/(x + 1)", "apart(1/(x²-1))");

    rc = ac_pro_rat_apart("x/x^2-1", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK,
               "1/2/(x - 1) + 1/2/(x + 1)", "apart(x/(x²-1))");

    rc = ac_pro_rat_apart("1/x", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "1/(x)", "零根 → (x)");

    rc = ac_pro_rat_apart("1/x^2-2x+1", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_EDOM, "", "重根 → EDOM");

    rc = ac_pro_rat_apart("1/x^2-2x+1/x", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_EPARSE, "", "两项含 '/' → EPARSE");

    rc = ac_pro_rat_apart("1/x^2+1", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_EDOM, "", "复根 → EDOM");

    rc = ac_pro_rat_apart("x^2/x-1", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_EDOM, "", "假分式 → EDOM");

    return t;
}

/* apart（deg 5 DK 数值路径 + 回乘验证）
 * q = (x-1)(x-2)(x-3)(x-4)(x-5)，A_i = 2/q'(r_i) = 1/12, -1/3, 1/2, -1/3, 1/12。
 * DK 根的数值噪声被三级 fmt 的有理重建吸收（1e-9 邻域必命中同一分数）。 */
pro_tally_t test_rat_apart_dk(void) {
    pro_tally_t t;
    char out[256];
    int rc;

    t.checks = 0;
    t.failed = 0;
    printf("[case RAT-5] apart (DK roots, deg 5, verified)\n");

    rc = ac_pro_rat_apart("2/x^5-15x^4+85x^3-225x^2+274x-120", out,
                          (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK,
               "1/12/(x - 5) + -1/3/(x - 4) + 1/2/(x - 3)"
               " + -1/3/(x - 2) + 1/12/(x - 1)",
               "apart(2/Π(x-i)), r 降序");

    return t;
}

/* apart 验证拒绝：验证不过即 EDOM
 * p 与 q 无公因子但分解残差超 1e-9 的情形由实现内部构造；这里测
 * 「本原域外」的系数越界与 deg 9。 */
pro_tally_t test_rat_limits(void) {
    pro_tally_t t;
    char out[64];
    int rc;

    t.checks = 0;
    t.failed = 0;
    printf("[case RAT-6] coefficient / degree limits\n");

    rc = ac_pro_rat_gcd("10000x+1", "x", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_ECOEF, "", "系数 10000 → ECOEF");

    rc = ac_pro_rat_gcd("x^9+1", "x", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_EDEG, "", "deg 9 → EDEG");

    rc = ac_pro_rat_cancel("9999x^8/x", out, (int)sizeof(out));
    expect_rat(&t, rc, out, AC_PRO_RAT_OK, "9999*x^7", "9999 刚好通过");

    return t;
}

/* 输出缓冲不足 */
pro_tally_t test_rat_buffer(void) {
    pro_tally_t t;
    char tiny[8];
    int rc;

    t.checks = 0;
    t.failed = 0;
    printf("[case RAT-7] output buffer\n");

    tiny[0] = '\0';
    rc = ac_pro_rat_cancel("x^2-1/x^2-3x+2", tiny, (int)sizeof(tiny));
    expect_rat(&t, rc, "", AC_PRO_RAT_EOVER, "", "tiny buffer → EOVER");

    rc = ac_pro_rat_cancel("x^2-1/x^2-3x+2", 0, 64);
    expect_rat(&t, rc, "", AC_PRO_RAT_EOVER, "", "NULL out → EOVER");

    return t;
}

#endif /* AC_PRO_ACTIVE */
