/* ============================================================
 * ApolloCalc-CAS · C/128KB-ProTEXT/test/test_pro_diff_multi.c
 * ------------------------------------------------------------
 * 【职责 / Responsibility】
 *   v2.0.0：多变量符号求导（ac_pro_diff_multi）的单元测试，
 *   共 12 条测试（runner 基线 259 + 12 = 271）。
 *
 * 【接口约定 / Interface】
 *   照抄 test/test_pro_multi.c：本文件定义自己的 pro_tally_t 与
 *   pcheck 助手，导出 test_diff_multi(void) 返回计数结构，runner
 *   只汇总、不感知内部类型。这样两个 TU 之间没有类型依赖。
 *
 * 【零静态存储纪律 / Zero static storage】
 *   本文件不声明任何静态对象（函数是 static 但不占 .bss），
 *   链接进来后 runner 口径的 .bss 增量仍是 0。
 *
 * 【用例选材说明 / Case notes】
 *   - 溢出例：解析器的字面量守卫是 `c > (INT64_MAX - 9) / 10`，对
 *     末位数字偏保守 —— INT64_MAX 本身（9223372036854775807）会在
 *     **词法层**被拒（第 18 位后的 922337203685477580 已越界）。
 *     故按任务卡的替换规则改用**能进池的最大字面量**
 *     9223372036854775799，对 x 求导时 ×2 必然溢出（走 ac_safe_mul
 *     拒绝路径，g_solve_error = 5）。
 *   - 语法错例：任务卡建议的 "x^" 实测**不是**语法错 —— 词法分析把
 *     悬空 ^ 读成指数 0，整个项退化为常数 1。故换 "sin(x)"：
 *     多项式词法里 's' 是非法分隔符，mv_parse 必拒。
 * ============================================================ */

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

/* ==================== 多变量符号求导（12 项测试） ==================== */
pro_tally_t test_diff_multi(void) {
    pro_tally_t t;
    char out[256];
    int rc;
    int rc1;
    int rc2;

    t.checks = 0;
    t.failed = 0;

    printf("[case MD-1] d/dx(x^2*y + x*y^2) = 2*x*y + y^2\n");
    out[0] = '\0';
    rc = ac_pro_diff_multi("x^2*y + x*y^2", 0, out, (int)sizeof(out));
    pcheck(&t, rc == 1 && streq(out, "2*x*y + y^2"),
           "d/dx == '2*x*y + y^2'", out);
    printf("         result: %s\n", out);

    printf("[case MD-2] d/dy(x^2*y + x*y^2) = x^2 + 2*x*y\n");
    out[0] = '\0';
    rc = ac_pro_diff_multi("x^2*y + x*y^2", 1, out, (int)sizeof(out));
    pcheck(&t, rc == 1 && streq(out, "x^2 + 2*x*y"),
           "d/dy == 'x^2 + 2*x*y'", out);
    printf("         result: %s\n", out);

    printf("[case MD-3] d/dx(3*x^4) = 12*x^3\n");
    out[0] = '\0';
    rc = ac_pro_diff_multi("3*x^4", 0, out, (int)sizeof(out));
    pcheck(&t, rc == 1 && streq(out, "12*x^3"),
           "d/dx == '12*x^3'", out);
    printf("         result: %s\n", out);

    printf("[case MD-4] d/dx(5) = 0  (constant input)\n");
    out[0] = '\0';
    rc = ac_pro_diff_multi("5", 0, out, (int)sizeof(out));
    pcheck(&t, rc == 1 && streq(out, "0"),
           "constant differentiates to '0' (still returns 1)", out);
    printf("         result: %s\n", out);

    printf("[case MD-5] d/dx(0) = 0  (zero polynomial)\n");
    out[0] = '\0';
    rc = ac_pro_diff_multi("0", 0, out, (int)sizeof(out));
    pcheck(&t, rc == 1 && streq(out, "0"),
           "zero polynomial -> '0' (still returns 1)", out);
    printf("         result: %s\n", out);

    printf("[case MD-6] d/dy(y^3) = 3*y^2\n");
    out[0] = '\0';
    rc = ac_pro_diff_multi("y^3", 1, out, (int)sizeof(out));
    pcheck(&t, rc == 1 && streq(out, "3*y^2"),
           "d/dy == '3*y^2'", out);
    printf("         result: %s\n", out);

    printf("[case MD-7] d/dx(x^3*y^2) = 3*x^2*y^2  (both variables, high degree)\n");
    out[0] = '\0';
    rc = ac_pro_diff_multi("x^3*y^2", 0, out, (int)sizeof(out));
    pcheck(&t, rc == 1 && streq(out, "3*x^2*y^2"),
           "d/dx == '3*x^2*y^2'", out);
    printf("         result: %s\n", out);

    printf("[case MD-8] coefficient overflow: 9223372036854775799*x^2 -> reject\n");
    g_solve_error = 0;
    out[0] = '\0';
    rc = ac_pro_diff_multi("9223372036854775799*x^2", 0, out, (int)sizeof(out));
    pcheck(&t, rc == -1 && g_solve_error == 5,
           "returns -1 with g_solve_error == 5 (ac_safe_mul refused c*2)", out);
    printf("         rc = %d, g_solve_error = %d\n", rc, g_solve_error);

    printf("[case MD-9] invalid var_index (2 and -1) -> reject\n");
    g_solve_error = 0;
    rc1 = ac_pro_diff_multi("x", 2, out, (int)sizeof(out));
    rc2 = ac_pro_diff_multi("x", -1, out, (int)sizeof(out));
    pcheck(&t, rc1 == -1 && rc2 == -1 && g_solve_error == 2,
           "both var_index values rejected with g_solve_error == 2", out);
    printf("         rc(2) = %d, rc(-1) = %d, g_solve_error = %d\n",
           rc1, rc2, g_solve_error);

    printf("[case MD-10] syntax error: sin(x) is outside the polynomial grammar\n");
    g_solve_error = 0;
    out[0] = '\0';
    rc = ac_pro_diff_multi("sin(x)", 0, out, (int)sizeof(out));
    pcheck(&t, rc == -1 && g_solve_error == 2,
           "returns -1 with g_solve_error == 2", out);
    printf("         rc = %d, g_solve_error = %d\n", rc, g_solve_error);

    printf("[case MD-11] slot reuse: second call unaffected by first call's residue\n");
    rc1 = ac_pro_diff_multi("x^2*y", 0, out, (int)sizeof(out));   /* leaves 2*x*y in slot 1 */
    out[0] = '\0';
    rc2 = ac_pro_diff_multi("5", 0, out, (int)sizeof(out));       /* must be '0', not residue */
    pcheck(&t, rc1 == 1 && rc2 == 1 && streq(out, "0"),
           "second call yields '0' (slot 1 residue ignored)", out);
    printf("         first: rc = %d, second result: %s\n", rc1, out);

    printf("[case MD-12] d/dx(x^6) = 6*x^5  (top degree 6)\n");
    out[0] = '\0';
    rc = ac_pro_diff_multi("x^6", 0, out, (int)sizeof(out));
    pcheck(&t, rc == 1 && streq(out, "6*x^5"),
           "d/dx == '6*x^5'", out);
    printf("         result: %s\n", out);

    return t;
}

#endif /* AC_PRO_ACTIVE */
