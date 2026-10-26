/* ============================================================
 * ApolloCalc-CAS · C/128KB-ProTEXT/test/test_pro_bigdec.c
 * ------------------------------------------------------------
 * 【职责 / Responsibility】
 *   v2.1.0 方向⑤「任意精度定点十进制」（ac_pro_dec_*）的单元测试：
 *   12 项测试、用例编号 [case BD-1..BD-12]，覆盖任务书要求的全部维度：
 *     加减乘除进位/借位（BD-2/3/4）、标度对齐（BD-8）、除法扩标
 *     （BD-5/6/11）、位数上限熔断（BD-6/7/9/10）、字符串往返（BD-1）。
 *
 * 【期望值来源 / Expected values】
 *   全部期望串由 node（BigInt）离线核算后以**字符串常量**写入本文件：
 *   node 脚本按 src/ac_pro_bigdec.c 的同一算法复算
 *   （对齐 → 运算 → 规范化 → 规范形渲染），非运行期生成、非目测。
 *   长期望串（181/180/203 字符）按 58 列拆行的相邻字面量拼接。
 *
 * 【接口约定 / Interface】
 *   照抄 test/test_pro_multi.c：本文件定义自己的 pro_tally_t 与
 *   pcheck 助手，导出 test_bigdec(void) 返回计数结构，runner 只汇总、
 *   不感知内部类型（test_pro_runner.c 的跨 TU 约定）。入口函数不带
 *   static（external linkage），与 runner 的外部原型一致。
 *
 * 【零静态存储纪律 / Zero static storage】
 *   本文件不声明任何静态对象（pcheck/streq 是 static 函数，不占
 *   .bss），链接进来后 runner 口径的 .bss 增量仍是 0。所有缓冲都是
 *   栈上定长数组（无 VLA）。
 * ============================================================ */

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

static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

/* 构造测试输入用的 180 个 '9'（10^180 − 1，输入串而非期望值）。 */
static void make_nines(char *buf, int cap) {
    int i;
    if (cap < 181) { buf[0] = '\0'; return; }
    for (i = 0; i < 180; i++) { buf[i] = '9'; }
    buf[180] = '\0';
}

/* 构造 "0." + zeros 个 '0' + "1"（1e−(zeros+1) 形状的输入串）。 */
static void make_tiny(char *buf, int cap, int zeros) {
    int i, pos = 0;
    if (cap < zeros + 4) { buf[0] = '\0'; return; }
    buf[pos++] = '0';
    buf[pos++] = '.';
    for (i = 0; i < zeros; i++) { buf[pos++] = '0'; }
    buf[pos++] = '1';
    buf[pos] = '\0';
}

/* 构造 "1" + zeros 个 '0'（10^zeros 形状的输入串）。 */
static void make_pow10(char *buf, int cap, int zeros) {
    int i;
    if (cap < zeros + 2) { buf[0] = '\0'; return; }
    buf[0] = '1';
    for (i = 0; i < zeros; i++) { buf[1 + i] = '0'; }
    buf[1 + zeros] = '\0';
}

/* ==================== 任意精度定点十进制（12 项测试） ==================== */
pro_tally_t test_bigdec(void) {
    pro_tally_t t;
    char out[256];
    char in1[256], in2[256];
    int rc;

    t.checks = 0;
    t.failed = 0;

    /* ---------- [case BD-1] 字符串往返：解析 → 渲染 = 规范形 ---------- */
    printf("[case BD-1] string round-trip: parse -> render canonical forms\n");
    {
        int ok;
        rc = ac_pro_dec_parse("0", 0);
        ok = (rc == AC_PRO_DEC_OK) && (ac_pro_dec_render(0, out, (int)sizeof out) == AC_PRO_DEC_OK)
             && streq(out, "0");
        rc = ac_pro_dec_parse("1.50", 0);
        ok = ok && (rc == AC_PRO_DEC_OK)
             && (ac_pro_dec_render(0, out, (int)sizeof out) == AC_PRO_DEC_OK)
             && streq(out, "1.5");
        rc = ac_pro_dec_parse("1000", 0);
        ok = ok && (rc == AC_PRO_DEC_OK)
             && (ac_pro_dec_render(0, out, (int)sizeof out) == AC_PRO_DEC_OK)
             && streq(out, "1000");
        rc = ac_pro_dec_parse("0.0000123", 0);
        ok = ok && (rc == AC_PRO_DEC_OK)
             && (ac_pro_dec_render(0, out, (int)sizeof out) == AC_PRO_DEC_OK)
             && streq(out, "0.0000123");
        rc = ac_pro_dec_parse("-0.0", 0);
        ok = ok && (rc == AC_PRO_DEC_OK)
             && (ac_pro_dec_render(0, out, (int)sizeof out) == AC_PRO_DEC_OK)
             && streq(out, "0");
        rc = ac_pro_dec_parse("12345678901234567890.0987654321", 0);
        ok = ok && (rc == AC_PRO_DEC_OK)
             && (ac_pro_dec_render(0, out, (int)sizeof out) == AC_PRO_DEC_OK)
             && streq(out, "12345678901234567890.0987654321");
        pcheck(&t, ok,
               "round-trip: 0 / 1.50->1.5 / 1000 / 0.0000123 / -0.0->0 / 30-digit",
               "some parse/render pair mismatched its node-computed canonical form");
        printf("         last render: %.34s\n", out);
    }

    /* ---------- [case BD-2] 加法进位（含 10^180 精确特例） ---------- */
    printf("[case BD-2] addition carry chain + exact 10^180 corner\n");
    {
        int ok;
        rc = ac_pro_dec_add("999.999", "0.001", out, (int)sizeof out);
        ok = (rc == AC_PRO_DEC_OK) && streq(out, "1000");
        printf("         999.999 + 0.001 = %s (rc=%d)\n", out, rc);
        make_nines(in1, (int)sizeof in1);
        rc = ac_pro_dec_add(in1, "1", out, (int)sizeof out);
        ok = ok && (rc == AC_PRO_DEC_OK)
             && streq(out, "1000000000000000000000000000000000000000000000000000000000"
                           "0000000000000000000000000000000000000000000000000000000000"
                           "0000000000000000000000000000000000000000000000000000000000"
                           "0000000");
        printf("         180-nines + 1 = 1e180 (rc=%d, len=%d)\n", rc, (int)strlen(out));
        pcheck(&t, ok,
               "add carry: 999.999+0.001=1000; NINES+1 falls to exact (1, -180)",
               "carry chain or exact-10^180 corner mismatched node expectation");
    }

    /* ---------- [case BD-3] 减法借位与符号 ---------- */
    printf("[case BD-3] subtraction borrow chain and signs\n");
    {
        int ok;
        rc = ac_pro_dec_sub("1000", "0.001", out, (int)sizeof out);
        ok = (rc == AC_PRO_DEC_OK) && streq(out, "999.999");
        printf("         1000 - 0.001 = %s (rc=%d)\n", out, rc);
        rc = ac_pro_dec_sub("1.5", "2.75", out, (int)sizeof out);
        ok = ok && (rc == AC_PRO_DEC_OK) && streq(out, "-1.25");
        printf("         1.5 - 2.75 = %s (rc=%d)\n", out, rc);
        rc = ac_pro_dec_sub("2.5", "2.5", out, (int)sizeof out);
        ok = ok && (rc == AC_PRO_DEC_OK) && streq(out, "0");
        printf("         2.5 - 2.5 = %s (rc=%d)\n", out, rc);
        pcheck(&t, ok,
               "sub borrow: 1000-0.001=999.999; 1.5-2.75=-1.25; 2.5-2.5=0",
               "borrow chain or zero result mismatched node expectation");
    }

    /* ---------- [case BD-4] 乘法（alias-safe 复用 + 标度相加） ---------- */
    printf("[case BD-4] multiplication: 18+18 digit product, tiny factors\n");
    {
        int ok;
        rc = ac_pro_dec_mul("123456789.123456789", "987654321.987654321",
                            out, (int)sizeof out);
        ok = (rc == AC_PRO_DEC_OK)
             && streq(out, "121932631356500531.347203169112635269");
        printf("         123456789.123456789 * 987654321.987654321 = %s (rc=%d)\n", out, rc);
        rc = ac_pro_dec_mul("0.0000001", "0.0000001", out, (int)sizeof out);
        ok = ok && (rc == AC_PRO_DEC_OK) && streq(out, "0.00000000000001");
        printf("         1e-7 * 1e-7 = %s (rc=%d)\n", out, rc);
        pcheck(&t, ok,
               "mul: 36-digit product exact; 1e-14 keeps scale in the exponent",
               "product or scale handling mismatched node expectation");
    }

    /* ---------- [case BD-5] 除法扩标：精确终点 ---------- */
    printf("[case BD-5] division with scale extension, exact terminators\n");
    {
        int ok;
        rc = ac_pro_dec_div("1", "8", out, (int)sizeof out);
        ok = (rc == AC_PRO_DEC_OK) && streq(out, "0.125");
        printf("         1 / 8 = %s (rc=%d, exact)\n", out, rc);
        rc = ac_pro_dec_div("7.5", "2.5", out, (int)sizeof out);
        ok = ok && (rc == AC_PRO_DEC_OK) && streq(out, "3");
        printf("         7.5 / 2.5 = %s (rc=%d)\n", out, rc);
        rc = ac_pro_dec_div("1", "0.01", out, (int)sizeof out);
        ok = ok && (rc == AC_PRO_DEC_OK) && streq(out, "100");
        printf("         1 / 0.01 = %s (rc=%d, negative quotient scale path)\n", out, rc);
        pcheck(&t, ok,
               "div exact: 1/8=0.125; 7.5/2.5=3; 1/0.01=100 (all rc=OK)",
               "exact division mismatched node expectation");
    }

    /* ---------- [case BD-6] 除法位数上限熔断（INEXACT 截断） ---------- */
    printf("[case BD-6] division fuse: 1/3, 2/3 truncated at 179 digits (rc=2)\n");
    {
        int ok;
        rc = ac_pro_dec_div("1", "3", out, (int)sizeof out);
        ok = (rc == AC_PRO_DEC_INEXACT)
             && streq(out, "0.33333333333333333333333333333333333333333333333333333333"
                           "3333333333333333333333333333333333333333333333333333333333"
                           "3333333333333333333333333333333333333333333333333333333333"
                           "3333333");
        printf("         1 / 3 = 0.<179 threes> (rc=%d, len=%d)\n", rc, (int)strlen(out));
        rc = ac_pro_dec_div("2", "3", out, (int)sizeof out);
        ok = ok && (rc == AC_PRO_DEC_INEXACT)
             && streq(out, "0.66666666666666666666666666666666666666666666666666666666"
                           "6666666666666666666666666666666666666666666666666666666666"
                           "6666666666666666666666666666666666666666666666666666666666"
                           "6666666");
        printf("         2 / 3 = 0.<179 sixes> (rc=%d, len=%d)\n", rc, (int)strlen(out));
        pcheck(&t, ok,
               "div fuse: 1/3 and 2/3 return INEXACT with the exact 179-digit tail",
               "fused quotient or return code mismatched node expectation");
    }

    /* ---------- [case BD-7] 除法错误：除零 + 商下溢熔断 ---------- */
    printf("[case BD-7] division errors: EDIV0 and quotient-underflow fuse\n");
    {
        int ok;
        rc = ac_pro_dec_div("5", "0", out, (int)sizeof out);
        ok = (rc == AC_PRO_DEC_EDIV0);
        printf("         5 / 0 -> %d (EDIV0 expected)\n", rc);
        make_nines(in1, (int)sizeof in1);
        rc = ac_pro_dec_div("1", in1, out, (int)sizeof out);
        ok = ok && (rc == AC_PRO_DEC_EOVER);
        printf("         1 / NINES(180) -> %d (EOVER: Q=0 with R!=0, < 1 sig digit)\n", rc);
        pcheck(&t, ok,
               "div errors: 5/0 = EDIV0; 1/(10^180-1) fuses to EOVER (no sig digit)",
               "error codes mismatched AC_PRO_DEC_EDIV0/EOVER");
    }

    /* ---------- [case BD-8] 标度对齐：正常 + 超容量熔断 ---------- */
    printf("[case BD-8] scale alignment: cross-scale add and alignment fuse\n");
    {
        int ok;
        rc = ac_pro_dec_add("1.5", "0.0000000000000000001", out, (int)sizeof out);
        ok = (rc == AC_PRO_DEC_OK) && streq(out, "1.5000000000000000001");
        printf("         1.5 + 1e-19 = %s (rc=%d)\n", out, rc);
        make_tiny(in1, (int)sizeof in1, 200);           /* 1e-201 */
        rc = ac_pro_dec_add("1", in1, out, (int)sizeof out);
        ok = ok && (rc == AC_PRO_DEC_EOVER);
        printf("         1 + 1e-201 -> %d (EOVER: aligning 1 by 201 digits > 180)\n", rc);
        pcheck(&t, ok,
               "alignment: 1.5+1e-19 exact; 1+1e-201 refuses (EOVER, never truncates)",
               "alignment result or fuse mismatched node expectation");
    }

    /* ---------- [case BD-9] 解析位数上限熔断（180 边界） ---------- */
    printf("[case BD-9] parse digit-limit fuse: 180 ok, 181 refused\n");
    {
        int ok;
        make_pow10(in1, (int)sizeof in1, 179);          /* 10^179：恰好 180 位 */
        rc = ac_pro_dec_parse(in1, 0);
        ok = (rc == AC_PRO_DEC_OK)
             && (ac_pro_dec_render(0, out, (int)sizeof out) == AC_PRO_DEC_OK)
             && streq(out, "1000000000000000000000000000000000000000000000000000000000"
                           "0000000000000000000000000000000000000000000000000000000000"
                           "0000000000000000000000000000000000000000000000000000000000"
                           "000000");
        printf("         10^179 (180 digits) parses, len=%d (rc=%d)\n",
               (int)strlen(out), rc);
        make_pow10(in1, (int)sizeof in1, 180);          /* 10^180：181 位 → 熔断 */
        rc = ac_pro_dec_parse(in1, 0);
        ok = ok && (rc == AC_PRO_DEC_EDIGITS);
        printf("         10^180 (181 digits) -> %d (EDIGITS expected)\n", rc);
        pcheck(&t, ok,
               "parse fuse: 180-digit value round-trips; 181 digits -> EDIGITS",
               "180/181-digit boundary mismatched node expectation");
    }

    /* ---------- [case BD-10] 语法错误 + 乘法容量熔断 ---------- */
    printf("[case BD-10] parse errors and multiplication capacity fuse\n");
    {
        int ok, all_eparse;
        all_eparse = (ac_pro_dec_parse("abc", 0) == AC_PRO_DEC_EPARSE);
        all_eparse = all_eparse && (ac_pro_dec_parse("1.2.3", 0) == AC_PRO_DEC_EPARSE);
        all_eparse = all_eparse && (ac_pro_dec_parse("+", 0) == AC_PRO_DEC_EPARSE);
        all_eparse = all_eparse && (ac_pro_dec_parse("", 0) == AC_PRO_DEC_EPARSE);
        all_eparse = all_eparse && (ac_pro_dec_parse("1e5", 0) == AC_PRO_DEC_EPARSE);
        all_eparse = all_eparse && (ac_pro_dec_parse(" 1", 0) == AC_PRO_DEC_EPARSE);
        printf("         abc/1.2.3/+//(empty)/1e5/<space>1 all -> EPARSE: %d\n", all_eparse);
        make_pow10(in1, (int)sizeof in1, 99);           /* 10^99：100 位 */
        make_pow10(in2, (int)sizeof in2, 99);
        rc = ac_pro_dec_mul(in1, in2, out, (int)sizeof out);
        ok = all_eparse && (rc == AC_PRO_DEC_EOVER);
        printf("         1e99 * 1e99 -> %d (EOVER: 100+100=200 mantissa digits > 180)\n", rc);
        pcheck(&t, ok,
               "errors: six malformed inputs = EPARSE; 200-digit product = EOVER",
               "syntax rejection or mul capacity fuse mismatched");
    }

    /* ---------- [case BD-11] 除法扩标：商标度为负的整数放大 ---------- */
    printf("[case BD-11] division scale extension: negative quotient scale\n");
    {
        int ok;
        rc = ac_pro_dec_div("10", "4", out, (int)sizeof out);
        ok = (rc == AC_PRO_DEC_OK) && streq(out, "2.5");
        printf("         10 / 4 = %s (rc=%d)\n", out, rc);
        make_tiny(in1, (int)sizeof in1, 23);            /* 1e-24 */
        rc = ac_pro_dec_div("1", in1, out, (int)sizeof out);
        ok = ok && (rc == AC_PRO_DEC_OK) && streq(out, "1000000000000000000000000");
        printf("         1 / 1e-24 = %s (rc=%d, len=%d)\n", out, rc, (int)strlen(out));
        pcheck(&t, ok,
               "div extension: 10/4=2.5; 1/1e-24=10^24 via scale=-render path",
               "scale-extension result mismatched node expectation");
    }

    /* ---------- [case BD-12] 负数四则与规范零 ---------- */
    printf("[case BD-12] negatives: mul/sub/add identities and canonical zero\n");
    {
        int ok;
        rc = ac_pro_dec_mul("-2.5", "4", out, (int)sizeof out);
        ok = (rc == AC_PRO_DEC_OK) && streq(out, "-10");
        printf("         -2.5 * 4 = %s (rc=%d)\n", out, rc);
        rc = ac_pro_dec_div("-7", "2", out, (int)sizeof out);
        ok = ok && (rc == AC_PRO_DEC_OK) && streq(out, "-3.5");
        printf("         -7 / 2 = %s (rc=%d, truncate-toward-zero sign)\n", out, rc);
        rc = ac_pro_dec_sub("-1", "-1", out, (int)sizeof out);
        ok = ok && (rc == AC_PRO_DEC_OK) && streq(out, "0");
        printf("         -1 - -1 = %s (rc=%d)\n", out, rc);
        rc = ac_pro_dec_add("-0.5", "0.5", out, (int)sizeof out);
        ok = ok && (rc == AC_PRO_DEC_OK) && streq(out, "0");
        printf("         -0.5 + 0.5 = %s (rc=%d)\n", out, rc);
        pcheck(&t, ok,
               "negatives: -2.5*4=-10; -7/2=-3.5; -1-(-1)=0; -0.5+0.5=0",
               "sign handling mismatched node expectation");
    }

    return t;
}

#endif /* AC_PRO_ACTIVE */
