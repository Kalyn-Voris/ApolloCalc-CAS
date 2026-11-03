/* ApolloCalc · test_pro_sturm.c — F3 Sturm 序列与实根隔离(C 侧)验收测试

   比对 docs/contracts/sturm-format-v25.md 冻结的语义:计数(闭区间 [a,b],
   端点根计入)与隔离(半开 (lo,hi] 二分、深度 ≤ 48、既约分数端点、未解析
   "!")。期望值独立核算:node BigInt 精确 oracle(见 tests/sturm.json,
   链/计数/隔离三层全部离线核算)并与契约 JSON 交叉锚定;差分驱动
   diff-sturm.js 另行对拍。错误码(契约 §6)精确比较。

   自带 stm_tally_t,不 include runner 内部头(与 test_pro_sqfree.c 同约定)。
   不声明静态对象:本 TU 的 .bss 必须是 0。 */

#include <stdio.h>
#include <string.h>

#include "../src/ac_pro.h"
#include "../../../C/128KB/src/ac_solvers.h"   /* DK 交叉校验:根池全局(见 ac_solvers.h) */

#if AC_PRO_ACTIVE

typedef struct {
    int checks;
    int failed;
} stm_tally_t;

static void stmcheck(stm_tally_t *t, int cond, const char *name, const char *detail) {
    t->checks++;
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        t->failed++;
        printf("  [FAIL] %s  (%s)\n", name, detail);
    }
}

static void expect_count(stm_tally_t *t, const char *p, const char *a, const char *b,
                         int want, const char *name) {
    int rc = ac_pro_sturm_count(p, a, b);
    if (rc == want) {
        stmcheck(t, 1, name, "");
    } else {
        char detail[160];
        sprintf(detail, "rc=%d want=%d", rc, want);
        stmcheck(t, 0, name, detail);
    }
}

static void expect_isolate(stm_tally_t *t, const char *p, const char *want, const char *name) {
    char out[512];
    int rc = ac_pro_sturm_isolate(p, out, (int)sizeof(out));
    if (rc == AC_PRO_STM_OK && strcmp(out, want) == 0) {
        stmcheck(t, 1, name, "");
    } else {
        char detail[288];
        sprintf(detail, "rc=%d got=\"%.120s\" want=\"%s\"", rc, out, want);
        stmcheck(t, 0, name, detail);
    }
}

/* 契约 dualCases st-lin:线性 */
void test_sturm_linear(stm_tally_t *t) {
    printf("[case STURM-1] contract st-lin: 2x-3\n");
    expect_count(t, "2x-3", "0", "2", 1, "count[0,2]=1");
    expect_count(t, "2x-3", "-inf", "inf", 1, "count[-inf,inf]=1");
    expect_isolate(t, "2x-3", "[[-4, 4]]", "isolate (1 root, top cell)");
}

/* 契约 st-cubic:三实根 + 混合无限端点 */
void test_sturm_cubic(stm_tally_t *t) {
    printf("[case STURM-2] contract st-cubic: x^3-x\n");
    expect_count(t, "x^3-x", "-2", "2", 3, "count[-2,2]=3");
    expect_count(t, "x^3-x", "-inf", "inf", 3, "count[-inf,inf]=3");
    expect_count(t, "x^3-x", "0", "1", 2, "count[0,1]=2 (端点根计入)");
    expect_count(t, "x^3-x", "-1", "inf", 3, "count[-1,inf]=3");
    expect_count(t, "x^3-x", "-inf", "1", 3, "count[-inf,1]=3");
    expect_count(t, "x^3-x", "-1", "1", 3, "count[-1,1]=3 (闭区间含全部三根)");
    expect_isolate(t, "x^3-x", "[[-2, -1], [-1, 0], [0, 2]]", "isolate");
}

/* 契约 st-x2p1 / st-x4p1:零实根 */
void test_sturm_no_real(stm_tally_t *t) {
    printf("[case STURM-3] contract st-x2p1/st-x4p1: zero real roots\n");
    expect_count(t, "x^2+1", "-5", "5", 0, "x^2+1 count[-5,5]=0");
    expect_count(t, "x^2+1", "-inf", "inf", 0, "x^2+1 count[-inf,inf]=0");
    expect_count(t, "x^2+1", "-999999", "999999", 0, "x^2+1 大端点(链小,JS 精确 Horner 域内)");
    expect_isolate(t, "x^2+1", "[]", "x^2+1 isolate []");
    expect_count(t, "x^4+1", "-3", "3", 0, "x^4+1 count[-3,3]=0");
    expect_isolate(t, "x^4+1", "[]", "x^4+1 isolate []");
}

/* 契约 st-quartic4:4 实根 */
void test_sturm_quartic4(stm_tally_t *t) {
    printf("[case STURM-4] contract st-quartic4: x^4-5x^2+4\n");
    expect_count(t, "x^4-5x^2+4", "-inf", "inf", 4, "count[-inf,inf]=4");
    expect_count(t, "x^4-5x^2+4", "0", "10", 2, "count[0,10]=2");
    expect_count(t, "x^4-5x^2+4", "-99", "99", 4, "count[-99,99]=4");
    expect_isolate(t, "x^4-5x^2+4", "[[-4, -2], [-2, 0], [0, 1], [1, 2]]", "isolate");
}

/* 契约 st-sff-dual:重根经 SFF 后计数 */
void test_sturm_sff(stm_tally_t *t) {
    printf("[case STURM-5] contract st-sff-dual: (x-1)^2(x+2) SFF dedup\n");
    expect_count(t, "x^3-3x+2", "-10", "10", 2, "count[-10,10]=2 (重根只计一次)");
    expect_isolate(t, "x^3-3x+2", "[[-4, 0], [0, 4]]", "isolate");
}

/* 契约 st-endpoints:端点恰为根的开闭语义与 a=b */
void test_sturm_endpoints(stm_tally_t *t) {
    printf("[case STURM-6] contract st-endpoints: x^2-4 boundary semantics\n");
    expect_count(t, "x^2-4", "-2", "3", 2, "count[-2,3]=2 (左端根计入)");
    expect_count(t, "x^2-4", "-3", "2", 2, "count[-3,2]=2 (右端根计入)");
    expect_count(t, "x^2-4", "-2", "2", 2, "count[-2,2]=2 (两端皆根)");
    expect_count(t, "x^2-4", "0", "10", 1, "count[0,10]=1");
    expect_count(t, "x^2-4", "3", "10", 0, "count[3,10]=0");
    expect_count(t, "x^2-4", "2", "2", 1, "count[2,2]=1 (a=b 退化)");
    expect_count(t, "x^2-4", "0", "0", 0, "count[0,0]=0");
}

/* 契约 st-neglc / st-content:负首项与非常数内容 */
void test_sturm_normalize(stm_tally_t *t) {
    printf("[case STURM-7] contract st-neglc/st-content: normalization\n");
    expect_count(t, "-x^3+x", "-2", "2", 3, "负首项 count[-2,2]=3");
    expect_count(t, "6x^2-6", "-2", "2", 2, "content 6 count=2");
    expect_isolate(t, "6x^2-6", "[[-2, 0], [0, 2]]", "content isolate");
}

/* 契约 st-const:非零常数 */
void test_sturm_const(stm_tally_t *t) {
    printf("[case STURM-8] contract st-const: 7\n");
    expect_count(t, "7", "-3", "3", 0, "count=0");
    expect_isolate(t, "7", "[]", "isolate []");
}

/* 契约 st-close / st-close2:近根正常隔离 */
void test_sturm_close(stm_tally_t *t) {
    printf("[case STURM-9] contract st-close/st-close2: close roots\n");
    expect_count(t, "999x^2-2", "0", "1", 1, "count[0,1]=1");
    expect_isolate(t, "999x^2-2", "[[-2, 0], [0, 2]]", "isolate (B=1+2/999, snap 2)");
    expect_count(t, "x^2-999", "0", "100", 1, "count[0,100]=1");
    expect_isolate(t, "x^2-999", "[[-1024, 0], [0, 1024]]", "isolate");
}

/* 契约 st-mignotte-iso:浮点轨道对照(dual) */
void test_sturm_mignotte(stm_tally_t *t) {
    printf("[case STURM-10] contract st-mignotte-iso: 999x^4-999x^3+1\n");
    expect_isolate(t, "999x^4-999x^3+1", "[[0, 1/2], [1/2, 1]]", "isolate (dyadic fractions)");
}

/* 契约 §3 端点与文法负向 */
void test_sturm_reject(stm_tally_t *t) {
    printf("[case STURM-11] contract section 3/6: rejections\n");
    expect_count(t, "x^2-4", "5", "3", AC_PRO_STM_EPARAM, "a>b → EPARAM");
    expect_count(t, "x^2-4", "abc", "1", AC_PRO_STM_EPARAM, "非法端点");
    expect_count(t, "x^2-4", "1000000", "inf", AC_PRO_STM_EPARAM, "|n|>999999");
    expect_count(t, "x^2-4", "1.5", "2", AC_PRO_STM_EPARAM, "小数端点");
    expect_count(t, "X^2-4", "-3", "3", AC_PRO_STM_EPARSE, "大写 X");
    expect_count(t, "x^2=4", "-3", "3", AC_PRO_STM_EPARSE, "'=' 非法");
    expect_count(t, "x^2/2", "-3", "3", AC_PRO_STM_EPARSE, "非整系数文法层拒");
    expect_count(t, "", "-3", "3", AC_PRO_STM_EPARSE, "空串");
    expect_count(t, "x^2+x^2", "-3", "3", AC_PRO_STM_EPARSE, "同次项重复");
    expect_count(t, "x^9+1", "-3", "3", AC_PRO_STM_EDEG, "次数 9(两侧同码)");
    expect_count(t, "0", "-3", "3", AC_PRO_STM_ECOEF, "全零多项式");
    expect_count(t, "1000x", "-3", "3", AC_PRO_STM_ECOEF, "|c|>999 刚好拒绝");
    expect_count(t, "999x", "-3", "3", 1, "|c|=999 刚好通过对照");
}

/* 契约 cOnlyDEG(deg 5..8:C 全功能,JS EDEG 成文分叉) */
void test_sturm_deg58(stm_tally_t *t) {
    printf("[case STURM-12] contract cOnlyDEG: degree 5..8\n");
    expect_count(t, "x^5-1", "-2", "2", 1, "x^5-1 count[-2,2]=1");
    expect_isolate(t, "x^5-1", "[[-2, 2]]", "x^5-1 isolate");
    expect_count(t, "x^5+x^4-5x^3-x^2+8x-4", "-10", "10", 2, "(x-1)^3(x+2)^2 SFF 后 2");
    expect_isolate(t, "x^5+x^4-5x^3-x^2+8x-4", "[[-4, 0], [0, 4]]", "isolate");
    expect_count(t, "x^8+1", "-3", "3", 0, "x^8+1 count=0");
    expect_count(t, "9x^8+8x^7+7x^6+6x^5+5x^4+4x^3+3x^2+2x+1", "-2", "2", 0,
                 "dense8 count[-2,2]=0(链系数峰值 72)");
}

/* 契约 cOnlyOVF(JS 2^53 审计触发;C ac_bigint 精确) */
void test_sturm_ovf(stm_tally_t *t) {
    printf("[case STURM-13] contract cOnlyOVF: big-endpoint exact Horner\n");
    expect_count(t, "x^4+1", "-9741", "9741", 0, "9741^4+1 <= 2^53-1(刚好通过域)");
    expect_count(t, "x^4+1", "-9742", "9742", 0, "9742^4+1 > 2^53-1(C 精确仍 0)");
    expect_count(t, "999x^4-999x^3+1", "-999999", "999999", 2, "C ac_bigint 精确计数 2");
    expect_count(t, "x^3-2", "-999999", "999999", 1, "t^3 超 2^53;C 计数 1");
}

/* 空白容忍与幂等(跨调用视图无残留) */
void test_sturm_misc(stm_tally_t *t) {
    char a[512];
    char b[512];
    printf("[case STURM-14] whitespace tolerance + idempotence\n");
    expect_count(t, " - x^3 + x ", "-2", "2", 3, "空白容忍");
    expect_isolate(t, " 6x^2 - 6 ", "[[-2, 0], [0, 2]]", "空白容忍 isolate");
    (void)ac_pro_sturm_isolate("x^3-x", a, (int)sizeof(a));
    (void)ac_pro_sturm_isolate("x^3-x", b, (int)sizeof(b));
    stmcheck(t, strcmp(a, b) == 0, "repeat call idempotent", "cross-call residue");
}

/* EOVER 边界:隔离输出的「刚好通过/刚好拒绝」 */
void test_sturm_buffer(stm_tally_t *t) {
    char big[512];
    char slim[512];
    int need;
    int rc;
    printf("[case STURM-15] buffer boundary (exact fit passes, len-1 rejects)\n");
    rc = ac_pro_sturm_isolate("x^4-5x^2+4", big, (int)sizeof(big));
    stmcheck(t, rc == AC_PRO_STM_OK, "reference output computed", "ref rc");
    need = (int)strlen(big);
    rc = ac_pro_sturm_isolate("x^4-5x^2+4", slim, need + 1);
    stmcheck(t, rc == AC_PRO_STM_OK && strcmp(slim, big) == 0, "out_len = len+1 passes", "fit rc");
    rc = ac_pro_sturm_isolate("x^4-5x^2+4", slim, need);
    stmcheck(t, rc == AC_PRO_STM_EOVER, "out_len = len -> EOVER", "tight rc");
    rc = ac_pro_sturm_isolate("x^4-5x^2+4", 0, 512);
    stmcheck(t, rc == AC_PRO_STM_EPARAM, "null out -> EPARAM", "null rc");
}

/* Sturm 计数 vs 求解层实根数交叉校验(DK 路径的可验证计数保证) */
void test_sturm_cross_dk(stm_tally_t *t) {
    double coef[6];
    int nreal;
    int i;
    printf("[case STURM-16] cross-check: Sturm count vs numeric roots (x^5-5x^3+4x)\n");
    expect_count(t, "x^5-5x^3+4x", "-inf", "inf", 5, "Sturm count = 5");
    /* 上游 DK(n ≤ 8)独立求根,数实根个数 */
    coef[0] = 0.0;
    coef[1] = 4.0;
    coef[2] = 0.0;
    coef[3] = -5.0;
    coef[4] = 0.0;
    coef[5] = 1.0;
    (void)ac_pro_numroots(coef, 5);
    nreal = 0;
    for (i = 0; i < g_root_count; i++) {
        if (g_root_im[i] == 0.0) { nreal++; }
    }
    stmcheck(t, g_solve_error == 0 && nreal == 5, "DK real roots = 5 agrees", "dk cross");
    ac_solvers_reset();
}

#endif /* AC_PRO_ACTIVE */
