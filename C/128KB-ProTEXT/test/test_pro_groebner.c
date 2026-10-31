/* ApolloCalc · test_pro_groebner.c — v2.2.0 方向⑥「2 变量 Groebner 基（M4GB 逐对归约）」的单元测试

   12 个用例、41 条测试，覆盖经典主干（<x²+y²−1, x²−y> 等经典组）、
   reduced-GB 结构性质（首项/尾项互不整除、primitive 正首项系数）、退化与熔断
   （常数生成元 / 零生成元 / 次数超限 / 空指针 / 缓冲不足）。

   期望值来源（确定性）：全部期望基用 node 独立核算后以字面常量写入（脚本与 C
   侧机制不同：node 走 Q[x,y] 有理数单项式化 Buchberger，C 侧走整系数伪除 +
   content 归一；reduced GB 在固定单项式序下唯一，二者必须逐字一致）。node 侧
   还自证了每例的定义性质：两输入模基归约为 0；基内所有 S-多项式模基归约为 0；
   基首项两两不整除、尾项不被其它元素首项整除、首项系数为正整数。不含任何运行
   期随机。

   实测过程记录：C 首版曾在 <x²+y²−1, xy−1> 的归约路径上与 node 有过分歧排查
   （最终确认为手工推演错、实现对）,这正是「期望值必须独立核算、勿凭记忆/手算」
   的实证。

   接口照抄 test/test_pro_sparse.c：本文件定义自己的 pro_tally_t 与 pcheck 助
   手，导出 4 个入口函数返回计数结构，runner 只汇总、不感知内部类型。入口函数
   不带 static（external linkage）。

   本文件不声明任何非 const 静态对象（期望值 const 表进 .rodata，pcheck 是
   static 函数不占 .bss），链接进 runner 后 .bss 增量为 0。 */

#include <stdio.h>
#include <string.h>

#include "../src/ac_pro.h"

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

/* 跑一例并校验 rc + 期望串。返回实际串指针（buf），失败时返回 0。 */
static const char *gb_run(pro_tally_t *t, const char *f, const char *g,
                          char *buf, int buf_len,
                          const char *want, const char *tag) {
    int rc = ac_pro_groebner(f, g, buf, buf_len);
    char name[96];
    sprintf(name, "[%s] rc == AC_PRO_GB_OK", tag);
    pcheck(t, rc == AC_PRO_GB_OK, name, "unexpected return code");
    if (rc != AC_PRO_GB_OK) { return 0; }
    sprintf(name, "[%s] gb == \"%s\"", tag, want);
    pcheck(t, strcmp(buf, want) == 0, name, "basis string mismatch");
    return buf;
}

/* 结构性质：基是 reduced GB（首项互不整除 + 尾项不被其它元素首项
 * 整除）且 primitive 正首项系数。node 侧已对期望值自证同一性质，
 * 这里对 C 侧运行时结果复核。 */
static void gb_check_reduced(pro_tally_t *t, int count, const char *tag) {
    int i;
    int j;
    int q;
    char name[96];

    for (i = 0; i < count; i++) {
        int ti = ac_pro_gb_terms(i);
        int lmi = ac_pro_gb_exps(i, 0);
        sprintf(name, "[%s] elem %d leading coef > 0 (primitive, positive lc)", tag, i);
        pcheck(t, ac_pro_gb_coef(i, 0) > 0, name, "non-positive leading coef");
        for (j = 0; j < count; j++) {
            int lmj;
            if (j == i) { continue; }
            lmj = ac_pro_gb_exps(j, 0);
            sprintf(name, "[%s] lm(elem %d) does not divide lm(elem %d)", tag, j, i);
            pcheck(t, !((lmi >> 4) >= (lmj >> 4) && (lmi & 15) >= (lmj & 15)),
                   name, "leading monomials comparable (not reduced)");
        }
        for (q = 1; q < ti; q++) {
            int eq = ac_pro_gb_exps(i, q);
            int divisible = 0;
            for (j = 0; j < count; j++) {
                int lmj;
                if (j == i) { continue; }
                lmj = ac_pro_gb_exps(j, 0);
                if ((eq >> 4) >= (lmj >> 4) && (eq & 15) >= (lmj & 15)) { divisible = 1; }
            }
            sprintf(name, "[%s] elem %d tail term %d not divisible by other lm", tag, i, q);
            pcheck(t, !divisible, name, "tail term reducible (not reduced GB)");
        }
    }
}

/* GB-1..GB-5：经典主干 */
pro_tally_t test_gb_textbook(void) {
    pro_tally_t t;
    char buf[512];
    int count;

    t.checks = 0;
    t.failed = 0;

    /* 任务书钦定用例
     * <x²+y²−1, x²−y>，lex x>y：node 核算 + CLO 经典一致，
     * reduced GB = {x²−y, y²+y−1}。 */
    printf("[case GB-1] <x^2+y^2-1, x^2-y> lex -> {x^2-y, y^2+y-1} (task-book case)\n");
    if (gb_run(&t, "x^2+y^2-1", "x^2-y", buf, (int)sizeof(buf),
               "x^2 - y | y^2 + y - 1", "GB-1") != 0) {
        count = ac_pro_gb_count();
        pcheck(&t, count == 2, "[GB-1] count == 2", "wrong element count");
        gb_check_reduced(&t, count, "GB-1");
    }

    /* 深归约路径（node 实测 runMaxDeg = 7，视图界内）
     * <x²+y²−1, xy−1>：reduced GB = {x+y³−y, y⁴−y²+1}，次数 4。 */
    printf("[case GB-2] <x^2+y^2-1, x*y-1> lex -> {x+y^3-y, y^4-y^2+1}\n");
    if (gb_run(&t, "x^2+y^2-1", "x*y-1", buf, (int)sizeof(buf),
               "x + y^3 - y | y^4 - y^2 + 1", "GB-2") != 0) {
        count = ac_pro_gb_count();
        pcheck(&t, count == 2, "[GB-2] count == 2", "wrong element count");
        gb_check_reduced(&t, count, "GB-2");
    }

    /* 线性对：<x+y, x−y> = <x, y> */
    printf("[case GB-3] <x+y, x-y> lex -> {x, y}\n");
    gb_run(&t, "x+y", "x-y", buf, (int)sizeof(buf), "x | y", "GB-3");

    /* 无交互变元：<x²−1, y²−1> 不变 */
    printf("[case GB-4] <x^2-1, y^2-1> lex -> {x^2-1, y^2-1}\n");
    gb_run(&t, "x^2-1", "y^2-1", buf, (int)sizeof(buf), "x^2 - 1 | y^2 - 1", "GB-4");

    /* interreduce 主打：<x²+xy, xy+y²>
     * 它自身已是 GB（S-多项式为 0）但非 reduced；终结 interreduce
     * 必须把 x²+xy 的尾项 xy 消干净得 x²−y²。 */
    printf("[case GB-5] <x^2+xy, xy+y^2> lex -> {x^2-y^2, xy+y^2} (interreduce)\n");
    gb_run(&t, "x^2+x*y", "x*y+y^2", buf, (int)sizeof(buf),
           "x^2 - y^2 | x*y + y^2", "GB-5");

    return t;
}

/* GB-6..GB-10：退化与消融 */
pro_tally_t test_gb_degenerate(void) {
    pro_tally_t t;
    char buf[512];

    t.checks = 0;
    t.failed = 0;

    /* 冗余生成元：<x²−y, 2x²−2y> -> {x²−y}
     * 第二生成元是第一生成元的 2 倍；primitive 归一必须去掉 content 2。 */
    printf("[case GB-6] redundant generator <x^2-y, 2x^2-2y> -> {x^2-y}\n");
    gb_run(&t, "x^2-y", "2*x^2-2*y", buf, (int)sizeof(buf), "x^2 - y", "GB-6");

    /* 不相容 → 理想 = 全环：{1} */
    printf("[case GB-7] <x^2+y^2-1, x^2+y^2-2> -> {1} (whole ring)\n");
    gb_run(&t, "x^2+y^2-1", "x^2+y^2-2", buf, (int)sizeof(buf), "1", "GB-7");

    /* 单变量公约数：<x²−1, x³−1> = <x−1> */
    printf("[case GB-8] <x^2-1, x^3-1> lex -> {x-1}\n");
    gb_run(&t, "x^2-1", "x^3-1", buf, (int)sizeof(buf), "x - 1", "GB-8");

    /* 常数 / 零生成元
     * ⟨5, x²−y⟩ = 全环（Q 上 5 是单位）→ {1}；⟨0, x²−y⟩ → {x²−y}。 */
    printf("[case GB-9] constant <5, x^2-y> -> {1}; zero <0, x^2-y> -> {x^2-y}\n");
    gb_run(&t, "5", "x^2-y", buf, (int)sizeof(buf), "1", "GB-9a");
    gb_run(&t, "0", "x^2-y", buf, (int)sizeof(buf), "x^2 - y", "GB-9b");

    /* 曲线交点形：<x²−y, y²−x>
     * variety = x = y², y⁴ = y 的 4 个复点；reduced GB = {x−y², y⁴−y}。 */
    printf("[case GB-10] <x^2-y, y^2-x> lex -> {x-y^2, y^4-y}\n");
    gb_run(&t, "x^2-y", "y^2-x", buf, (int)sizeof(buf), "x - y^2 | y^4 - y", "GB-10");

    return t;
}

/* GB-11：访问器结构校验（GB-2 的基） */
pro_tally_t test_gb_accessors(void) {
    pro_tally_t t;
    char buf[512];
    int count;

    t.checks = 0;
    t.failed = 0;

    printf("[case GB-11] accessor structure of <x^2+y^2-1, x*y-1>'s basis\n");
    {
        int rc = ac_pro_groebner("x^2+y^2-1", "x*y-1", buf, (int)sizeof(buf));
        pcheck(&t, rc == AC_PRO_GB_OK, "[GB-11] rc == AC_PRO_GB_OK", "unexpected return");
    }
    count = ac_pro_gb_count();
    pcheck(&t, count == 2, "[GB-11] count == 2", "wrong element count");

    /* 元素 0 = x + y³ − y：项 (1,0):1, (0,3):1, (0,1):−1（lex 降序） */
    pcheck(&t, ac_pro_gb_terms(0) == 3, "[GB-11] terms(0) == 3", "term count wrong");
    pcheck(&t, ac_pro_gb_exps(0, 0) == 0x10, "[GB-11] exps(0,0) == 0x10 (x)", "lm wrong");
    pcheck(&t, ac_pro_gb_exps(0, 1) == 0x03, "[GB-11] exps(0,1) == 0x03 (y^3)", "term order wrong");
    pcheck(&t, ac_pro_gb_exps(0, 2) == 0x01, "[GB-11] exps(0,2) == 0x01 (y)", "term order wrong");
    pcheck(&t, ac_pro_gb_coef(0, 0) == 1 && ac_pro_gb_coef(0, 1) == 1 &&
               ac_pro_gb_coef(0, 2) == -1, "[GB-11] coefs(0) == 1,1,-1", "coefficients wrong");

    /* 元素 1 = y⁴ − y² + 1：项 (0,4):1, (0,2):−1, (0,0):1 */
    pcheck(&t, ac_pro_gb_terms(1) == 3, "[GB-11] terms(1) == 3", "term count wrong");
    pcheck(&t, ac_pro_gb_exps(1, 0) == 0x04 && ac_pro_gb_exps(1, 1) == 0x02 &&
               ac_pro_gb_exps(1, 2) == 0x00, "[GB-11] exps(1) == 04,02,00 (y^4,y^2,1)", "terms wrong");
    pcheck(&t, ac_pro_gb_coef(1, 0) == 1 && ac_pro_gb_coef(1, 1) == -1 &&
               ac_pro_gb_coef(1, 2) == 1, "[GB-11] coefs(1) == 1,-1,1", "coefficients wrong");

    /* 越界访问：返回 0 / -1，绝不越界读视图 */
    pcheck(&t, ac_pro_gb_coef(2, 0) == 0 && ac_pro_gb_exps(2, 0) == -1 &&
               ac_pro_gb_terms(-1) == -1 && ac_pro_gb_count() == 2,
           "[GB-11] out-of-range accessors are inert", "accessor out of range");

    gb_check_reduced(&t, count, "GB-11");
    return t;
}

/* GB-12：熔断与幂等 */
pro_tally_t test_gb_fuses(void) {
    pro_tally_t t;
    char buf[512];
    char small[8];
    int rc;
    char again[512];

    t.checks = 0;
    t.failed = 0;

    /* 输入超限 / 非法 / 缓冲不足 + 幂等
     * 全部熔断不写 out；同一输入连续两次调用结果逐字一致
     * （跨调用无残留,是 gb 视图入口 memset 整块清零的直接证据）。 */
    printf("[case GB-12] fuses: EDEG / EARG / EBUF; idempotent re-call\n");
    rc = ac_pro_groebner("x^5", "y", buf, (int)sizeof(buf));
    pcheck(&t, rc == AC_PRO_GB_EDEG, "[GB-12] x^5 (deg 5 > 4) -> AC_PRO_GB_EDEG", "wrong code");
    rc = ac_pro_groebner("x^3*y^3", "y", buf, (int)sizeof(buf));
    pcheck(&t, rc == AC_PRO_GB_EDEG, "[GB-12] x^3*y^3 (deg 6 > 4) -> AC_PRO_GB_EDEG", "wrong code");
    rc = ac_pro_groebner("x^2;", "y", buf, (int)sizeof(buf));
    pcheck(&t, rc == AC_PRO_GB_EARG, "[GB-12] trailing garbage -> AC_PRO_GB_EARG", "wrong code");
    rc = ac_pro_groebner("x*x", "y", buf, (int)sizeof(buf));
    pcheck(&t, rc == AC_PRO_GB_EARG, "[GB-12] duplicate variable in term -> AC_PRO_GB_EARG", "wrong code");
    rc = ac_pro_groebner("", "y", buf, (int)sizeof(buf));
    pcheck(&t, rc == AC_PRO_GB_EARG, "[GB-12] empty input -> AC_PRO_GB_EARG", "wrong code");
    rc = ac_pro_groebner(0, "y", buf, (int)sizeof(buf));
    pcheck(&t, rc == AC_PRO_GB_EARG, "[GB-12] NULL fsrc -> AC_PRO_GB_EARG", "wrong code");
    rc = ac_pro_groebner("x", 0, buf, (int)sizeof(buf));
    pcheck(&t, rc == AC_PRO_GB_EARG, "[GB-12] NULL gsrc -> AC_PRO_GB_EARG", "wrong code");
    rc = ac_pro_groebner("x^2+y^2-1", "x^2-y", small, (int)sizeof(small));
    pcheck(&t, rc == AC_PRO_GB_EBUF, "[GB-12] 8-byte buffer -> AC_PRO_GB_EBUF", "wrong code");

    /* 幂等：先跑 GB-2，再跑 GB-1，再跑一遍 GB-2,结果必须逐字一致 */
    (void)ac_pro_groebner("x^2+y^2-1", "x*y-1", buf, (int)sizeof(buf));
    (void)ac_pro_groebner("x^2+y^2-1", "x^2-y", buf, (int)sizeof(buf));
    rc = ac_pro_groebner("x^2+y^2-1", "x*y-1", again, (int)sizeof(again));
    pcheck(&t, rc == AC_PRO_GB_OK && strcmp(again, "x + y^3 - y | y^4 - y^2 + 1") == 0,
           "[GB-12] re-call after another instance reproduces GB-2 verbatim",
           "stale view state across calls");

    /* 两输入皆零：空基输出 "0" */
    rc = ac_pro_groebner("0", "0", buf, (int)sizeof(buf));
    pcheck(&t, rc == AC_PRO_GB_OK && strcmp(buf, "0") == 0 && ac_pro_gb_count() == 0,
           "[GB-12] <0, 0> -> empty basis \"0\", count 0", "empty ideal mishandled");
    return t;
}

#endif /* AC_PRO_ACTIVE */
