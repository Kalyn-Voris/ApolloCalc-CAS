/* ============================================================
 * ApolloCalc-CAS · test_base.c — 进制转换 + 分数显示测试（第 8 轮，C 独有）
 * Part A：进制转换（255 全基底 / 0 / 负数 / INT64_MAX 全基底 /
 *         INT64_MIN 拒绝 / 非法 base 拒绝 / 失败态出口得空串）
 * Part B：分数显示（IMPROPER / MIXED / AUTO 三模式全路径 +
 *         den==0、INT64_MIN 双侧、非法 mode、den 负数归正）
 * Part C：复位（脏缓冲全槽位清零）
 * Part D：跨调用干扰（后次调用完整覆盖前次，不残留尾迹；
 *         进制与分数共用缓冲的双向交叉覆盖）
 * 测试基准：手工核算（BEEF=48879、F4240=1000000、
 *         2^63-1 = 二进制 63 个 1 = 八进制 21 个 7、
 *         INT64_MAX/2 = 4611686018427387903 余 1）
 * ============================================================ */
#include <stdio.h>
#include <string.h>
#include "../src/ac_types.h"
#include "../src/ac_base.h"

static int s_pass = 0;
static int s_fail = 0;

/* [English] MinGW's msvcrt printf does not support %lld under -std=c99
   (-Werror=format rejects it); %I64d is its native 64-bit form.
   Non-Windows hosts keep the C99 spelling. */
#ifdef _WIN32
#define AB_I64 "%I64d"
#else
#define AB_I64 "%lld"
#endif

static void expect_true(int cond, const char *name) {
    if (cond) { s_pass++; }
    else { s_fail++; printf("FAIL %s\n", name); }
}

static void expect_str(const char *got, const char *want, const char *name) {
    if (strcmp(got, want) == 0) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL %s: got \"%s\" want \"%s\"\n", name, got, want);
    }
}

/* 进制转换端到端：ret 与串合计 1 项测试 */
static void check_base(ac_int_t n, int base, const char *want, const char *name) {
    char buf[AC_BASE_BUF_SIZE];
    int ret = ac_base_dec_to_base(n, base);
    (void)ac_base_to_string(buf, (int)sizeof(buf));
    if (ret == 1 && strcmp(buf, want) == 0) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL %s: n=" AB_I64 " base=%d ret=%d got \"%s\" want \"%s\"\n",
               name, (long long)n, base, ret, buf, want);
    }
}

/* 分数显示端到端：ret 与串合计 1 项测试 */
static void check_frac(ac_int_t num, ac_int_t den, int mode, const char *want,
                       const char *name) {
    char buf[AC_BASE_BUF_SIZE];
    int ret = ac_base_format_fraction(num, den, mode);
    (void)ac_fraction_to_string(buf, (int)sizeof(buf));
    if (ret == 1 && strcmp(buf, want) == 0) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL %s: num=" AB_I64 " den=" AB_I64 " mode=%d ret=%d got \"%s\" want \"%s\"\n",
               name, (long long)num, (long long)den, mode, ret, buf, want);
    }
}

/* ==================== Part A：进制转换 ==================== */
static void test_base_conv(void) {
    check_base(255, 16, "FF", "hex 255");
    check_base(255, 2, "11111111", "bin 255");
    check_base(255, 8, "377", "oct 255");
    check_base(255, 10, "255", "dec 255");
    check_base(0, 16, "0", "hex 0");
    check_base(-42, 16, "-2A", "hex -42");
    check_base(1000000, 16, "F4240", "hex 1e6");
    check_base(10, 2, "1010", "bin 10");
    check_base(123, 10, "123", "dec 123");
    check_base(4095, 16, "FFF", "hex 4095");
    check_base(1, 2, "1", "bin 1");
    check_base(7, 8, "7", "oct 7");
    check_base(-1, 2, "-1", "bin -1");
    check_base(48879, 16, "BEEF", "hex 48879=BEEF");

    /* INT64_MAX = 2^63-1 全基底（手工核算） */
    check_base(9223372036854775807LL, 16, "7FFFFFFFFFFFFFFF", "hex INT64_MAX");
    check_base(9223372036854775807LL, 8, "777777777777777777777", "oct INT64_MAX");
    check_base(9223372036854775807LL, 10, "9223372036854775807", "dec INT64_MAX");
    /* 2^63-1 二进制 = 63 个 1：逐位核验（不手抄 63 字符长串） */
    {
        char buf[AC_BASE_BUF_SIZE];
        int i;
        int all_ones = 1;
        expect_true(ac_base_dec_to_base(9223372036854775807LL, 2) == 1,
                    "bin INT64_MAX ret");
        ac_base_to_string(buf, (int)sizeof(buf));
        expect_true((int)strlen(buf) == 63, "bin INT64_MAX len=63");
        for (i = 0; i < 63; i++) {
            if (buf[i] != '1') { all_ones = 0; }
        }
        expect_true(all_ones, "bin INT64_MAX all ones");
    }

    /* 非法 base 拒绝（失败态：g_base_len 清零 → 出口得空串） */
    expect_true(ac_base_dec_to_base(255, 7) == 0, "base 7 rejected");
    expect_true(ac_base_dec_to_base(255, 0) == 0, "base 0 rejected");
    expect_true(ac_base_dec_to_base(255, 3) == 0, "base 3 rejected");
    expect_true(ac_base_dec_to_base(255, -2) == 0, "base -2 rejected");
    {
        char buf[AC_BASE_BUF_SIZE];
        ac_base_to_string(buf, (int)sizeof(buf));
        expect_str(buf, "", "failed conv -> empty string");
    }

    /* INT64_MIN 拒绝（-INT64_MIN 溢出） */
    expect_true(ac_base_dec_to_base(INT64_MIN, 16) == 0, "INT64_MIN rejected");
    expect_true(ac_base_dec_to_base(INT64_MIN, 2) == 0, "INT64_MIN bin rejected");

    /* 便捷封装返回值 */
    expect_true(ac_base_dec_to_bin(255) == 1, "dec_to_bin ret");
    expect_true(ac_base_dec_to_oct(255) == 1, "dec_to_oct ret");
    expect_true(ac_base_dec_to_hex(255) == 1, "dec_to_hex ret");
}

/* ==================== Part B：分数显示 ==================== */
static void test_fraction(void) {
    /* IMPROPER：恒为 num/den（不约分） */
    check_frac(3, 2, AC_FRAC_IMPROPER, "3/2", "improper 3/2");
    check_frac(5, 1, AC_FRAC_IMPROPER, "5/1", "improper 5/1");
    check_frac(-3, 2, AC_FRAC_IMPROPER, "-3/2", "improper -3/2");
    check_frac(0, 1, AC_FRAC_IMPROPER, "0/1", "improper 0/1");
    check_frac(4, 2, AC_FRAC_IMPROPER, "4/2", "improper no reduction");

    /* MIXED：带分数 */
    check_frac(3, 2, AC_FRAC_MIXED, "1 1/2", "mixed 3/2");
    check_frac(5, 1, AC_FRAC_MIXED, "5", "mixed 5/1");
    check_frac(-3, 2, AC_FRAC_MIXED, "-1 1/2", "mixed -3/2");
    check_frac(1, 2, AC_FRAC_MIXED, "1/2", "mixed proper 1/2");
    check_frac(-1, 2, AC_FRAC_MIXED, "-1/2", "mixed proper -1/2");
    check_frac(7, 3, AC_FRAC_MIXED, "2 1/3", "mixed 7/3");
    check_frac(-7, 3, AC_FRAC_MIXED, "-2 1/3", "mixed -7/3");
    check_frac(4, 2, AC_FRAC_MIXED, "2", "mixed integral 4/2");
    check_frac(2, 2, AC_FRAC_MIXED, "1", "mixed 2/2");
    check_frac(0, 1, AC_FRAC_MIXED, "0", "mixed zero 0/1");
    check_frac(0, 5, AC_FRAC_MIXED, "0", "mixed zero 0/5");

    /* AUTO：den==1 出整数，否则出分数 */
    check_frac(5, 1, AC_FRAC_AUTO, "5", "auto 5/1");
    check_frac(3, 2, AC_FRAC_AUTO, "3/2", "auto 3/2");
    check_frac(-5, 1, AC_FRAC_AUTO, "-5", "auto -5/1");
    check_frac(0, 1, AC_FRAC_AUTO, "0", "auto 0/1");

    /* int64 边界值（INT64_MAX/2 = 4611686018427387903 余 1） */
    check_frac(9223372036854775807LL, 1, AC_FRAC_AUTO,
               "9223372036854775807", "auto INT64_MAX");
    check_frac(9223372036854775807LL, 2, AC_FRAC_MIXED,
               "4611686018427387903 1/2", "mixed INT64_MAX/2");

    /* 失败路径 */
    expect_true(ac_base_format_fraction(1, 0, AC_FRAC_IMPROPER) == 0,
                "den 0 improper rejected");
    expect_true(ac_base_format_fraction(1, 0, AC_FRAC_MIXED) == 0,
                "den 0 mixed rejected");
    expect_true(ac_base_format_fraction(INT64_MIN, 1, AC_FRAC_IMPROPER) == 0,
                "num INT64_MIN rejected");
    expect_true(ac_base_format_fraction(1, INT64_MIN, AC_FRAC_IMPROPER) == 0,
                "den INT64_MIN rejected");
    expect_true(ac_base_format_fraction(1, 2, 99) == 0,
                "unknown mode rejected");
    {
        char buf[AC_BASE_BUF_SIZE];
        ac_fraction_to_string(buf, (int)sizeof(buf));
        expect_str(buf, "", "failed frac -> empty string");
    }

    /* 分母负数归正（符号移到分子） */
    check_frac(1, -2, AC_FRAC_IMPROPER, "-1/2", "den -2 normalized");
    check_frac(-1, -2, AC_FRAC_IMPROPER, "1/2", "both negative normalized");
    check_frac(3, -2, AC_FRAC_MIXED, "-1 1/2", "den -2 mixed normalized");
}

/* ==================== Part C：复位 ==================== */
static void test_reset(void) {
    (void)ac_base_dec_to_hex(65535);                     /* FFFF，置脏 */
    (void)ac_base_format_fraction(-7, 3, AC_FRAC_MIXED); /* -2 1/3，再置脏 */
    ac_base_reset();
    expect_true(g_base_len == 0, "reset len=0");
    expect_true(g_base_buf[0] == 0, "reset buf[0]=0");
    expect_true(g_base_buf[31] == 0, "reset buf[31]=0");
    expect_true(g_base_buf[63] == 0, "reset buf[63]=0");
}

/* ==================== Part D：跨调用干扰 ==================== */
static void test_interference(void) {
    char buf[AC_BASE_BUF_SIZE];

    /* 进制内覆盖：FFFF(4 字符) → 101(3 字符)，不残留尾部 */
    check_base(65535, 16, "FFFF", "hex 65535 pre");
    expect_true(ac_base_dec_to_bin(5) == 1, "bin 5 ret");
    ac_base_to_string(buf, (int)sizeof(buf));
    expect_str(buf, "101", "bin overwrite no residue");

    /* 分数内覆盖：2 1/3(5 字符) → 1/2(3 字符)，不残留 */
    check_frac(7, 3, AC_FRAC_MIXED, "2 1/3", "mixed 7/3 pre");
    check_frac(1, 2, AC_FRAC_MIXED, "1/2", "frac overwrite no residue");
    expect_true(g_base_len == 3, "frac len=3 after overwrite");

    /* 交叉覆盖：进制写 → 分数写 → 分数出口正确（不串用出口） */
    (void)ac_base_dec_to_hex(65535);
    check_frac(1, 2, AC_FRAC_IMPROPER, "1/2", "cross base->frac");

    /* 交叉覆盖：分数写 → 进制写 → 进制出口正确 */
    (void)ac_base_format_fraction(7, 3, AC_FRAC_MIXED);
    check_base(65535, 16, "FFFF", "cross frac->base");
}

int test_base_run(void) {
    test_base_conv();
    test_fraction();
    test_reset();
    test_interference();

    printf("==== C99 base: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
