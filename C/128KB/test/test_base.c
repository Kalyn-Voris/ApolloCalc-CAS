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

/* ==================== Part E：SI 前缀（128KB 第 4 轮） ====================
 * 行为基准：JS/128KB/src/core_base.js siNormalize/siToString 逐分支对齐；
 * 工程记数法为 C 侧扩展（JS 无对应），字符串格式 mantissa + "e" + exp
 * （exp==0 无后缀）。mantissa 校验用精确相等——十进制输入 v/1000^k 的
 * 最近双机就是字面量本身的最近双机，%.15g 可无损还原。 */

/* SI 四元组端到端：返回值 + mantissa(精确) + prefix + exp + valid */
static void si_case(double v, int want_valid, double want_m, int want_code,
                    int want_exp, const char *name) {
    int ret = ac_base_si_normalize(v);
    int ok = ret == want_valid && g_base_si_valid == want_valid;
    if (ok && want_valid) {
        ok = g_base_si_mantissa == want_m && g_base_si_prefix_code == want_code &&
             g_base_si_exp == want_exp;
    }
    if (ok) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL %s: ret=%d m=%.17g code=%d exp=%d valid=%d "
               "want valid=%d m=%.17g code=%d exp=%d\n",
               name, ret, g_base_si_mantissa, g_base_si_prefix_code,
               g_base_si_exp, g_base_si_valid, want_valid, want_m, want_code, want_exp);
    }
}

/* SI 字符串端到端 */
static void si_str(double v, const char *want, const char *name) {
    char buf[AC_BASE_BUF_SIZE];
    (void)ac_base_si_normalize(v);
    ac_base_si_to_string(buf, (int)sizeof(buf));
    expect_str(buf, want, name);
}

/* 工程记数法三元组端到端 */
static void eng_case(double v, int want_valid, double want_m, int want_exp,
                     const char *name) {
    int ret = ac_base_eng_normalize(v);
    int ok = ret == want_valid && g_base_eng_valid == want_valid;
    if (ok && want_valid) {
        ok = g_base_eng_mantissa == want_m && g_base_eng_exp == want_exp;
    }
    if (ok) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL %s: ret=%d m=%.17g exp=%d valid=%d "
               "want valid=%d m=%.17g exp=%d\n",
               name, ret, g_base_eng_mantissa, g_base_eng_exp, g_base_eng_valid,
               want_valid, want_m, want_exp);
    }
}

/* 工程记数法字符串端到端 */
static void eng_str(double v, const char *want, const char *name) {
    char buf[AC_BASE_BUF_SIZE];
    (void)ac_base_eng_normalize(v);
    ac_base_eng_to_string(buf, (int)sizeof(buf));
    expect_str(buf, want, name);
}

static void test_si_eng(void) {
    /* ---- SI 基本档位 ---- */
    si_case(12345.0, 1, 12.345, 107, 3, "si 12345 -> 12.345 k");
    si_case(0.005, 1, 5.0, 109, -3, "si 0.005 -> 5 m");
    si_case(1000.0, 1, 1.0, 107, 3, "si 1000 -> 1 k");
    si_case(999.0, 1, 999.0, 0, 0, "si 999 -> 999 (empty prefix)");
    si_case(1e6, 1, 1.0, 77, 6, "si 1e6 -> 1 M");
    si_case(1e-6, 1, 1.0, 181, -6, "si 1e-6 -> 1 u(micro)");
    si_case(999.9999, 1, 999.9999, 0, 0, "si 999.9999 stays at exp 0");
    si_case(0.001, 1, 1.0, 109, -3, "si 0.001 -> 1 m");
    si_case(-1234.0, 1, -1.234, 107, 3, "si -1234 -> -1.234 k (sign on mantissa)");
    si_case(0.5, 1, 500.0, 109, -3, "si 0.5 -> 500 m");

    /* ---- 无效条件（0 / 非有限 / 越界）——四元组全清零 ---- */
    si_case(0.0, 0, 0.0, 0, 0, "si 0 invalid");
    si_case((double)INFINITY, 0, 0.0, 0, 0, "si +Inf invalid");
    si_case((double)NAN, 0, 0.0, 0, 0, "si NaN invalid");
    si_case(1e27, 0, 0.0, 0, 0, "si 1e27 above SI range");
    si_case(1e-27, 0, 0.0, 0, 0, "si 1e-27 below SI range");

    /* ---- 舍入回吸：精确千的幂的尾数必须精确等于 1 ---- */
    si_case(1e-15, 1, 1.0, 102, -15, "si 1e-15 absorbed to exactly 1 f");
    si_case(1e-24, 1, 1.0, 121, -24, "si 1e-24 absorbed to exactly 1 y");
    si_case(1e-12, 1, 1.0, 112, -12, "si 1e-12 absorbed to exactly 1 p");
    si_case(1e21, 1, 1.0, 90, 21, "si 1e21 exact scaling to 1 Z");

    /* ---- 全 17 档前缀（y..Y，含空前缀档） ---- */
    {
        static const struct { double v; int code; } PFX[17] = {
            {1e-24, 121}, {1e-21, 122}, {1e-18, 97},  {1e-15, 102}, {1e-12, 112},
            {1e-9, 110},  {1e-6, 181},  {1e-3, 109},  {1e0, 0},     {1e3, 107},
            {1e6, 77},    {1e9, 71},    {1e12, 84},   {1e15, 80},   {1e18, 69},
            {1e21, 90},   {1e24, 89}
        };
        int i;
        for (i = 0; i < 17; i++) {
            char name[64];
            int ret = ac_base_si_normalize(PFX[i].v);
            int ok = ret == 1 && g_base_si_valid == 1 &&
                     g_base_si_mantissa == 1.0 &&
                     g_base_si_exp == (i - 8) * 3 &&
                     g_base_si_prefix_code == PFX[i].code;
            sprintf(name, "si prefix table 1e%+d -> code %d", (i - 8) * 3, PFX[i].code);
            if (ok) { s_pass++; }
            else {
                s_fail++;
                printf("FAIL %s: got m=%.17g code=%d exp=%d\n", name,
                       g_base_si_mantissa, g_base_si_prefix_code, g_base_si_exp);
            }
        }
    }

    /* ---- SI 字符串出口 ---- */
    si_str(12345.0, "12.345 k", "si str 12345");
    si_str(0.005, "5 m", "si str 0.005");
    si_str(999.0, "999", "si str 999 (no space, no prefix)");
    si_str(-1234.0, "-1.234 k", "si str -1234");
    si_str(1e-15, "1 f", "si str 1e-15 = '1 f' (no rounding glitch)");
    si_str(1e-24, "1 y", "si str 1e-24 = '1 y'");
    si_str(0.0, "", "si str 0 -> empty (invalid)");
    si_str(1e27, "", "si str 1e27 -> empty (invalid)");
    /* µ 是 U+00B5 单字节 181：按字节核验（UTF-8 场景由 UI 层二次编码） */
    {
        char buf[AC_BASE_BUF_SIZE];
        (void)ac_base_si_normalize(1e-6);
        ac_base_si_to_string(buf, (int)sizeof(buf));
        expect_true(buf[0] == '1' && buf[1] == ' ' &&
                    (unsigned char)buf[2] == 181 && buf[3] == '\0',
                    "si str 1e-6 byte-level '1 ' + 0xB5");
    }
    /* 截断安全：缓冲不足时保留 mantissa、无 NUL 越界 */
    {
        char small[4];
        (void)ac_base_si_normalize(12345.0);
        ac_base_si_to_string(small, (int)sizeof(small));
        expect_str(small, "12.", "si str truncated to buf_size-1");
    }

    /* ---- 工程记数法 ---- */
    eng_case(12345.0, 1, 12.345, 3, "eng 12345 -> 12.345e3");
    eng_case(0.005, 1, 5.0, -3, "eng 0.005 -> 5e-3");
    eng_case(1000.0, 1, 1.0, 3, "eng 1000 -> 1e3");
    eng_case(999.0, 1, 999.0, 0, "eng 999 -> 999e0 (exp 0)");
    eng_case(1e24, 1, 1.0, 24, "eng 1e24 -> 1e24");
    eng_case(1e-24, 1, 1.0, -24, "eng 1e-24 -> 1e-24");
    eng_case(-1234.0, 1, -1.234, 3, "eng -1234 -> -1.234e3");
    eng_case(0.0, 0, 0.0, 0, "eng 0 invalid");
    eng_case((double)NAN, 0, 0.0, 0, "eng NaN invalid");
    eng_case(1e27, 0, 0.0, 0, "eng 1e27 out of range");
    /* 回吸对工程记数法同样生效 */
    eng_case(1e-15, 1, 1.0, -15, "eng 1e-15 absorbed to exactly 1");

    /* ---- 工程记数法字符串出口 ---- */
    eng_str(12345.0, "12.345e3", "eng str 12345");
    eng_str(0.005, "5e-3", "eng str 0.005");
    eng_str(999.0, "999", "eng str 999 (exp 0 no suffix)");
    eng_str(1e-15, "1e-15", "eng str 1e-15");
    eng_str(1e-24, "1e-24", "eng str 1e-24");
    eng_str(-1234.0, "-1.234e3", "eng str -1234");
    eng_str(0.0, "", "eng str 0 -> empty (invalid)");

    /* ---- SI / 工程状态互不干扰 + 复位覆盖 ---- */
    (void)ac_base_si_normalize(1000.0);
    (void)ac_base_eng_normalize(0.005);
    expect_true(g_base_si_mantissa == 1.0 && g_base_si_prefix_code == 107 &&
                g_base_si_exp == 3, "SI state intact after eng call");
    expect_true(g_base_eng_mantissa == 5.0 && g_base_eng_exp == -3,
                "eng state intact after SI call");
    ac_base_reset();
    expect_true(g_base_si_valid == 0 && g_base_si_mantissa == 0.0 &&
                g_base_si_prefix_code == 0 && g_base_si_exp == 0 &&
                g_base_eng_valid == 0 && g_base_eng_mantissa == 0.0 &&
                g_base_eng_exp == 0, "reset clears SI + eng state");
    {
        char buf[8];
        ac_base_si_to_string(buf, (int)sizeof(buf));
        expect_str(buf, "", "si string empty after reset");
        ac_base_eng_to_string(buf, (int)sizeof(buf));
        expect_str(buf, "", "eng string empty after reset");
    }
}

int test_base_run(void) {
    test_base_conv();
    test_fraction();
    test_reset();
    test_interference();
    test_si_eng();

    printf("==== C99 base: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
