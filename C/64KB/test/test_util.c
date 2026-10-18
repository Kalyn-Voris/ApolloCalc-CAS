/* ============================================================
 * ApolloCalc-CAS · test_util.c — 工具层测试（第 9 轮，C 独有）
 * Part A：CRC32（标准黄金向量 "123456789"→CBF43926 等 + 链式 update
 *         等价单发 + 二进制含 0x00/0xFF 用例）
 * Part B：SHA-256（FIPS 180-4 官方向量 + 55/56/57/64/1000 字节
 *         填充边界，参考值由 Node crypto 生成，先疑实现后疑向量）
 * Part C：Base64（RFC 4648 "foobar" 序列编 + 解 + 往返 + '=' 停止
 *         语义 + 非法字符/缓冲溢出拒绝）
 * Part D：定点数（构造/转换/加减乘除/溢出拒绝/除零/截断语义）
 * Part E：复位（表丢弃后懒重建仍正确）
 * ============================================================ */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/ac_types.h"
#include "../src/ac_util.h"

static int s_pass = 0;
static int s_fail = 0;

/* [English] MinGW's msvcrt printf does not support %lld under -std=c99
   (-Werror=format rejects it); %I64d is its native 64-bit form.
   Non-Windows hosts keep the C99 spelling. (第 7 轮已踩坑，复用宏) */
#ifdef _WIN32
#define UT_I64 "%I64d"
#else
#define UT_I64 "%lld"
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

/* ==================== Part A：CRC32 ==================== */

static void check_crc(const char *bytes, int len, uint32_t want, const char *name) {
    uint32_t got = ac_crc32((const uint8_t *)bytes, len);
    if (got == want) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL %s: got 0x%08X want 0x%08X\n", name, (unsigned)got, (unsigned)want);
    }
}

static void test_crc(void) {
    check_crc("", 0, 0x00000000u, "crc32 empty");
    check_crc("123456789", 9, 0xCBF43926u, "crc32 123456789 (golden)");
    check_crc("The quick brown fox jumps over the lazy dog", 43, 0x414FA339u,
              "crc32 fox");
    check_crc("a", 1, 0xE8B7BE43u, "crc32 a");
    check_crc("abc", 3, 0x352441C2u, "crc32 abc");
    check_crc("\x00\x01\x02\x03\xff", 5, 0x7B35F858u, "crc32 binary 00-03-ff");

    /* 链式 update == 单发（zlib 预/后条件约定） */
    {
        uint32_t c = ac_crc32((const uint8_t *)"1234", 4);
        expect_true(c == 0x9BE3E0A3u, "crc32 1234 prefix value");
        c = ac_crc32_update(c, (const uint8_t *)"56789", 5);
        expect_true(c == 0xCBF43926u, "crc32 chained == single 123456789");
    }
    /* 三段链 */
    {
        uint32_t c = 0;
        c = ac_crc32_update(c, (const uint8_t *)"The quick brown ", 16);
        c = ac_crc32_update(c, (const uint8_t *)"fox jumps over ", 15);
        c = ac_crc32_update(c, (const uint8_t *)"the lazy dog", 12);
        expect_true(c == 0x414FA339u, "crc32 3-way chain == single fox");
    }
}

/* ==================== Part B：SHA-256 ==================== */

static void sha256_to_hex(const uint8_t *d, char *out /* >= 65 bytes */) {
    static const char HEX[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 32; i++) {
        out[2 * i] = HEX[d[i] >> 4];
        out[2 * i + 1] = HEX[d[i] & 0x0F];
    }
    out[64] = '\0';
}

static void check_sha256(const char *bytes, int len, const char *want, const char *name) {
    uint8_t digest[AC_SHA256_DIGEST_SIZE];
    char hex[65];
    ac_sha256((const uint8_t *)bytes, len, digest);
    sha256_to_hex(digest, hex);
    expect_str(hex, want, name);
}

static void test_sha256(void) {
    /* FIPS 180-4 官方向量 */
    check_sha256("", 0,
                 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                 "sha256 empty");
    check_sha256("abc", 3,
                 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                 "sha256 abc");
    check_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
                 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                 "sha256 56-byte (two-block pad)");
    check_sha256("The quick brown fox jumps over the lazy dog", 43,
                 "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592",
                 "sha256 fox");
    /* 填充边界（Node crypto 生成）：55 不触发补块 / 56、57 触发 /
       64 恰好整块 + 独立长度块 / 1000 多块 */
    check_sha256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 55,
                 "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318",
                 "sha256 55a (pad fits)");
    check_sha256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 56,
                 "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a",
                 "sha256 56a (extra block)");
    check_sha256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 57,
                 "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6",
                 "sha256 57a (extra block)");
    check_sha256(
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 64,
        "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
        "sha256 64a (exact block)");
    check_sha256(
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1000,
        "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3",
        "sha256 1000a (multi-block)");}

/* ==================== Part C：Base64 ==================== */

static void check_b64enc(const char *bytes, int len, const char *want, const char *name) {
    char buf[64];
    int ret = ac_base64_encode((const uint8_t *)bytes, len, buf, (int)sizeof(buf));
    if (ret >= 0 && strcmp(buf, want) == 0) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL %s: ret=%d got \"%s\" want \"%s\"\n", name, ret, buf, want);
    }
}

static void test_base64(void) {
    uint8_t buf[32];
    char enc[64];
    int ret;

    /* RFC 4648 编码序列（0/1/2 字节余数全覆盖） */
    check_b64enc("", 0, "", "b64enc empty");
    check_b64enc("f", 1, "Zg==", "b64enc f (1 rem)");
    check_b64enc("fo", 2, "Zm8=", "b64enc fo (2 rem)");
    check_b64enc("foo", 3, "Zm9v", "b64enc foo");
    check_b64enc("foob", 4, "Zm9vYg==", "b64enc foob");
    check_b64enc("fooba", 5, "Zm9vYmE=", "b64enc fooba");
    check_b64enc("foobar", 6, "Zm9vYmFy", "b64enc foobar");

    /* 解码 */
    ret = ac_base64_decode("Zm9vYmFy", 8, buf, (int)sizeof(buf));
    expect_true(ret == 6 && memcmp(buf, "foobar", 6) == 0, "b64dec Zm9vYmFy");
    ret = ac_base64_decode("Zg==", 4, buf, (int)sizeof(buf));
    expect_true(ret == 1 && buf[0] == 'f', "b64dec Zg==");
    /* '=' 停止但不报错（尾部垃圾忽略） */
    ret = ac_base64_decode("Zg==Junk", 8, buf, (int)sizeof(buf));
    expect_true(ret == 1 && buf[0] == 'f', "b64dec stops at padding");
    /* 非法字符 */
    expect_true(ac_base64_decode("Zm9v!", 5, buf, (int)sizeof(buf)) == -1,
                "b64dec invalid char -> -1");
    /* 输出缓冲不足 */
    expect_true(ac_base64_decode("Zm9v", 4, buf, 2) == -1, "b64dec overflow -> -1");

    /* 往返（含二进制） */
    ret = ac_base64_encode((const uint8_t *)"hello world", 11, enc, (int)sizeof(enc));
    expect_true(ret == 16, "roundtrip hello world enc len");
    ret = ac_base64_decode(enc, ret, buf, (int)sizeof(buf));
    expect_true(ret == 11 && memcmp(buf, "hello world", 11) == 0,
                "roundtrip hello world");
    ret = ac_base64_encode((const uint8_t *)"The quick brown fox", 19, enc, (int)sizeof(enc));
    ret = ac_base64_decode(enc, ret, buf, (int)sizeof(buf));
    expect_true(ret == 19 && memcmp(buf, "The quick brown fox", 19) == 0,
                "roundtrip fox (rem 1)");
    {
        const uint8_t bin[5] = {0x00, 0x01, 0x02, 0x03, 0xFF};
        uint8_t back[5];
        ret = ac_base64_encode(bin, 5, enc, (int)sizeof(enc));
        expect_true(ret == 8, "roundtrip binary enc len");
        ret = ac_base64_decode(enc, ret, back, (int)sizeof(back));
        expect_true(ret == 5 && memcmp(back, bin, 5) == 0, "roundtrip binary");
    }
    /* 编码输出缓冲不足 */
    expect_true(ac_base64_encode((const uint8_t *)"foobar", 6, enc, 8) == -1,
                "b64enc overflow -> -1");
}

/* ==================== Part D：定点数 ==================== */

static void check_fx_from(double v, int want_ret, ac_fixed_t want_fx, const char *name) {
    ac_fixed_t fx = 0;
    int ret = ac_fx_from_double(v, &fx);
    if (ret == want_ret && (want_ret == 0 || fx == want_fx)) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL %s: ret=%d fx=" UT_I64 " (want ret=%d fx=" UT_I64 ")\n",
               name, ret, (long long)fx, want_ret, (long long)want_fx);
    }
}

static void check_fx_op(int ret, ac_fixed_t got, ac_fixed_t want, const char *name) {
    if (ret == 1 && got == want) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL %s: ret=%d got=" UT_I64 " want=" UT_I64 "\n",
               name, ret, (long long)got, (long long)want);
    }
}

static void test_fixed(void) {
    ac_fixed_t out = 0;
    int ret;

    /* 构造与转换 */
    check_fx_from(1.5, 1, 1500000LL, "fx from 1.5");
    check_fx_from(-0.001, 1, -1000LL, "fx from -0.001");
    check_fx_from(0.0, 1, 0LL, "fx from 0.0");
    check_fx_from(0.123456, 1, 123456LL, "fx from 0.123456");
    check_fx_from(2.5, 1, 2500000LL, "fx from 2.5");
    check_fx_from(1e13, 0, 0, "fx from 1e13 overflow");
    check_fx_from(-1e13, 0, 0, "fx from -1e13 overflow");
    check_fx_from((double)NAN, 0, 0, "fx from NaN rejected");
    check_fx_from(9223372036855.0, 0, 0, "fx from above MAX rejected");
    expect_true(ac_fx_from_double(9223372036854.0, &out) == 1, "fx from MAX accepted");
    expect_true(ac_fx_to_double(1500000LL) == 1.5, "fx to 1.5");
    expect_true(ac_fx_to_double(-1000LL) == -0.001, "fx to -0.001");

    /* 加减（先调用后校验：C 实参求值顺序未指定，不能把 out 和
       带副作用的调用挤在同一个调用表达式里） */
    ret = ac_fx_add(1500000LL, 2500000LL, &out);
    check_fx_op(ret, out, 4000000LL, "fx 1.5+2.5");
    ret = ac_fx_add(1500000LL, -2500000LL, &out);
    check_fx_op(ret, out, -1000000LL, "fx 1.5-2.5");
    expect_true(ac_fx_add((ac_fixed_t)INT64_MAX, 1, &out) == 0, "fx add overflow -> 0");
    ret = ac_fx_sub(4000000LL, 1500000LL, &out);
    check_fx_op(ret, out, 2500000LL, "fx 4.0-1.5");
    ret = ac_fx_sub(1000000LL, 2000000LL, &out);
    check_fx_op(ret, out, -1000000LL, "fx negative result");

    /* 乘（结果 = prod/1e6，不整除截断） */
    ret = ac_fx_mul(1500000LL, 2000000LL, &out);
    check_fx_op(ret, out, 3000000LL, "fx 1.5*2.0");
    ret = ac_fx_mul(500000LL, 500000LL, &out);
    check_fx_op(ret, out, 250000LL, "fx 0.5*0.5");
    ret = ac_fx_mul(-1500000LL, 2000000LL, &out);
    check_fx_op(ret, out, -3000000LL, "fx -1.5*2.0");
    expect_true(ac_fx_mul(9223372036854LL, 9223372036854LL, &out) == 0,
                "fx mul overflow -> 0");

    /* 除（a*1e6/b；不整除截断 ≤ 1e-6，定点数固有） */
    ret = ac_fx_div(3000000LL, 2000000LL, &out);
    check_fx_op(ret, out, 1500000LL, "fx 3.0/2.0");
    expect_true(ac_fx_div(1000000LL, 0LL, &out) == 0, "fx div by zero -> 0");
    expect_true(ac_fx_div(1000000LL, 3000000LL, &out) == 1 && out == 333333LL,
                "fx 1/3 truncates to 333333 (0.333333)");
    expect_true(ac_fx_div((ac_fixed_t)INT64_MAX, 1, &out) == 0,
                "fx div scale overflow -> 0");
}

/* ==================== Part E：复位 ==================== */

static void test_reset(void) {
    ac_util_reset();                       /* 丢弃 CRC / Base64 懒表 */
    expect_true(ac_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u,
                "crc32 correct after reset (table rebuilt)");
    {
        char buf[16];
        (void)ac_base64_encode((const uint8_t *)"foo", 3, buf, (int)sizeof(buf));
        expect_str(buf, "Zm9v", "b64enc correct after reset");
    }
}

int test_util_run(void) {
    test_crc();
    test_sha256();
    test_base64();
    test_fixed();
    test_reset();

    printf("==== C99 util: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
