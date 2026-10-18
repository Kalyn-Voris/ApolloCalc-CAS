/* ============================================================
 * ApolloCalc-CAS · test_numtheory.c — 数论层测试（第 7 轮，C 独有）
 * 本轮无 JS 基准（JS 64KB / 128KB 均无数论层），校验依据为
 * 数学手册标准值，全部经 Node BigInt 先行核算（勿凭记忆改值）：
 *   C(66,33) = 7219428434016265740（int64 内，ncr 溢出误报的判定用例）
 *   C(67,33) = 14226520737620288370（> INT64_MAX，必返 0）
 *   2^30-1 = 3^2 * 7 * 11 * 31 * 151 * 331
 *   1000003 * 1000033 = 1000036000099（半素数，最小因子 1e6 量级）
 *   前 15 个素数之积 = 614889782588491410（int64 内最多互异素因子数）
 * Part A：gcd / lcm（含溢出与负数路径）
 * Part B：is_prime（6k±1 轮盘边界：25/49/121 恰在 i == n/i 上）
 * Part C：factorize（含 15 素因子 primorial 与大半素数）
 * Part D：nCr / nPr（含 C(66,33) 可表示 / C(67,33) 溢出的上界对）
 * Part E：复位
 * 性能上界：全部用例 ≤ 1e12 且最小因子 ≤ 1e6，试除毫秒级完成。
 * ============================================================ */
#include <stdio.h>
#include "../src/ac_types.h"
#include "../src/ac_numtheory.h"
#include "../src/ac_bigint.h"

static int s_pass = 0;
static int s_fail = 0;

/* [English] MinGW's msvcrt printf does not support %lld under -std=c99
   (-Werror=format rejects it); %I64d is its native 64-bit form.
   Non-Windows hosts keep the C99 spelling. */
#ifdef _WIN32
#define NT_I64 "%I64d"
#else
#define NT_I64 "%lld"
#endif

static void expect_int(ac_int_t got, ac_int_t want, const char *name) {
    if (got == want) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL %s: got=" NT_I64 " want=" NT_I64 "\n",
               name, (long long)got, (long long)want);
    }
}

/* factorize 全槽位校验：ret + count + 每对 (prime, exp) 各计 1 项。
   ret != 1 时跳过槽位校验（失败路径槽位未定义）。 */
static void expect_factor(ac_int_t n, int want_ret, int want_count,
                          const ac_int_t *primes, const int *exps,
                          const char *name) {
    int ret = ac_nt_factorize(n);
    int i;
    if (ret == want_ret) { s_pass++; }
    else { s_fail++; printf("FAIL %s ret: got=%d want=%d\n", name, ret, want_ret); }
    if (ret != 1) { return; }
    if (g_nt_count == want_count) { s_pass++; }
    else { s_fail++; printf("FAIL %s count: got=%d want=%d\n", name, g_nt_count, want_count); }
    for (i = 0; i < want_count && i < g_nt_count; i++) {
        if (g_nt_primes[i] == primes[i] && g_nt_exps[i] == exps[i]) { s_pass++; }
        else {
            s_fail++;
            printf("FAIL %s f%d: got=(" NT_I64 "^%d) want=(" NT_I64 "^%d)\n", name, i,
                   (long long)g_nt_primes[i], g_nt_exps[i],
                   (long long)primes[i], exps[i]);
        }
    }
}

/* ==================== Part A：gcd / lcm ==================== */
static void test_gcd_lcm(void) {
    /* gcd 基本值 */
    expect_int(ac_nt_gcd(12, 18), 6, "gcd(12,18)=6");
    expect_int(ac_nt_gcd(0, 5), 5, "gcd(0,5)=5");
    expect_int(ac_nt_gcd(5, 0), 5, "gcd(5,0)=5");
    expect_int(ac_nt_gcd(0, 0), 0, "gcd(0,0)=0");
    /* 负数取绝对值 */
    expect_int(ac_nt_gcd(-12, 18), 6, "gcd(-12,18)=6");
    expect_int(ac_nt_gcd(12, -18), 6, "gcd(12,-18)=6");
    /* 2 的幂与互素对（BigInt 核算：gcd(2^40, 2^20)=2^20、gcd(2^62, 3^39)=1） */
    expect_int(ac_nt_gcd(1099511627776LL, 1048576LL), 1048576LL, "gcd(2^40,2^20)");
    expect_int(ac_nt_gcd(4611686018427387904LL, 4052555153018976267LL), 1,
               "gcd(2^62,3^39)=1");

    /* lcm 基本值 */
    expect_int(ac_nt_lcm(4, 6), 12, "lcm(4,6)=12");
    expect_int(ac_nt_lcm(0, 5), 0, "lcm(0,5)=0");
    expect_int(ac_nt_lcm(21, 6), 42, "lcm(21,6)=42");
    expect_int(ac_nt_lcm(-4, 6), 12, "lcm(-4,6)=12");
    /* 除法先行：lcm(2^62, 2)：g=2, ag=2^61, 2^61*2=2^62 可表示 */
    expect_int(ac_nt_lcm(4611686018427387904LL, 2), 4611686018427387904LL,
               "lcm(2^62,2)=2^62");
    /* 溢出：2^62 与 3^39 互素，lcm = 乘积 ≈ 1.87e37 → 返 0 */
    expect_int(ac_nt_lcm(4611686018427387904LL, 4052555153018976267LL), 0,
               "lcm(2^62,3^39) overflow -> 0");
    /* 素数自乘 */
    expect_int(ac_nt_lcm(2147483647LL, 2147483647LL), 2147483647LL,
               "lcm(p,p)=p (p=2^31-1)");
}

/* ==================== Part B：is_prime ==================== */
static void test_is_prime(void) {
    expect_int(ac_nt_is_prime(0), 0, "is_prime(0)=0");
    expect_int(ac_nt_is_prime(1), 0, "is_prime(1)=0");
    expect_int(ac_nt_is_prime(2), 1, "is_prime(2)=1");
    expect_int(ac_nt_is_prime(3), 1, "is_prime(3)=1");
    expect_int(ac_nt_is_prime(4), 0, "is_prime(4)=0");
    expect_int(ac_nt_is_prime(5), 1, "is_prime(5)=1");
    expect_int(ac_nt_is_prime(17), 1, "is_prime(17)=1");
    /* 完全平方数恰落在 i == n/i 边界（查 i*i==n 的 off-by-one） */
    expect_int(ac_nt_is_prime(25), 0, "is_prime(25)=0 (5^2, i==n/i)");
    expect_int(ac_nt_is_prime(49), 0, "is_prime(49)=0 (7^2, wheel i+2)");
    expect_int(ac_nt_is_prime(121), 0, "is_prime(121)=0 (11^2, i==n/i)");
    expect_int(ac_nt_is_prime(100), 0, "is_prime(100)=0");
    expect_int(ac_nt_is_prime(997), 1, "is_prime(997)=1");
    /* 数学手册已知素数（<= 2^31-1，试除毫秒级） */
    expect_int(ac_nt_is_prime(1000000007LL), 1, "is_prime(1e9+7)=1");
    expect_int(ac_nt_is_prime(1000000009LL), 1, "is_prime(1e9+9)=1");
    expect_int(ac_nt_is_prime(999999937LL), 1, "is_prime(999999937)=1");
    expect_int(ac_nt_is_prime(2147483647LL), 1, "is_prime(2^31-1 Mersenne)=1");
    /* 2^63-1 = 7^2*73*127*...：轮盘第一拍（i=5, i+2=7）即命中 */
    expect_int(ac_nt_is_prime(9223372036854775807LL), 0,
               "is_prime(INT64_MAX)=0 (divisible by 7)");
    /* INT64_MIN < 2 直接否 */
    expect_int(ac_nt_is_prime(INT64_MIN), 0, "is_prime(INT64_MIN)=0");
}

/* ==================== Part C：factorize ==================== */
static void test_factorize(void) {
    /* 12 = 2^2 * 3 */
    {
        static const ac_int_t p[2] = {2, 3};
        static const int e[2] = {2, 1};
        expect_factor(12, 1, 2, p, e, "fac(12)=2^2*3");
    }
    /* 100 = 2^2 * 5^2 */
    {
        static const ac_int_t p[2] = {2, 5};
        static const int e[2] = {2, 2};
        expect_factor(100, 1, 2, p, e, "fac(100)=2^2*5^2");
    }
    /* 1024 = 2^10 */
    {
        static const ac_int_t p[1] = {2};
        static const int e[1] = {10};
        expect_factor(1024, 1, 1, p, e, "fac(1024)=2^10");
    }
    /* 素数本身 */
    {
        static const ac_int_t p[1] = {17};
        static const int e[1] = {1};
        expect_factor(17, 1, 1, p, e, "fac(17)=17");
    }
    /* 1 无因子（成功，count=0） */
    expect_factor(1, 1, 0, 0, 0, "fac(1) -> count=0");
    /* 0 / 负数失败 */
    expect_factor(0, 0, 0, 0, 0, "fac(0) -> ret 0");
    expect_factor(-5, 0, 0, 0, 0, "fac(-5) -> ret 0");
    /* 2^30-1 = 3^2 * 7 * 11 * 31 * 151 * 331（BigInt 核算） */
    {
        static const ac_int_t p[6] = {3, 7, 11, 31, 151, 331};
        static const int e[6] = {2, 1, 1, 1, 1, 1};
        expect_factor(1073741823LL, 1, 6, p, e, "fac(2^30-1)");
    }
    /* 大素数（试除至 31623，快） */
    {
        static const ac_int_t p[1] = {1000000007LL};
        static const int e[1] = {1};
        expect_factor(1000000007LL, 1, 1, p, e, "fac(1e9+7) prime");
    }
    /* 半素数 1000003 * 1000033 = 1000036000099（最小因子 ~1e6，毫秒级） */
    {
        static const ac_int_t p[2] = {1000003LL, 1000033LL};
        static const int e[2] = {1, 1};
        expect_factor(1000036000099LL, 1, 2, p, e, "fac(semiprime 1e6^2)");
    }
    /* 前 15 个素数之积 = 614889782588491410（int64 内最大互异素因子数） */
    {
        static const ac_int_t p[15] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29,
                                       31, 37, 41, 43, 47};
        static const int e[15] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                  1, 1, 1, 1, 1};
        expect_factor(614889782588491410LL, 1, 15, p, e, "fac(primorial 15)");
    }
}

/* ==================== Part D：nCr / nPr ==================== */
static void test_ncr_npr(void) {
    ac_int_t v;

    /* --- nCr 手册标准值 --- */
    v = 0;
    expect_int((ac_int_t)ac_nt_ncr(10, 3, &v), 1, "ncr(10,3) ret");
    expect_int(v, 120, "ncr(10,3)=120");

    v = 0;
    expect_int((ac_int_t)ac_nt_ncr(10, 0, &v), 1, "ncr(10,0) ret");
    expect_int(v, 1, "ncr(10,0)=1");

    v = 0;
    expect_int((ac_int_t)ac_nt_ncr(10, 10, &v), 1, "ncr(10,10) ret");
    expect_int(v, 1, "ncr(10,10)=1");

    v = 0;
    expect_int((ac_int_t)ac_nt_ncr(0, 0, &v), 1, "ncr(0,0) ret");
    expect_int(v, 1, "ncr(0,0)=1");

    v = 0;
    expect_int((ac_int_t)ac_nt_ncr(5, 2, &v), 1, "ncr(5,2) ret");
    expect_int(v, 10, "ncr(5,2)=10");

    v = 0;
    expect_int((ac_int_t)ac_nt_ncr(20, 10, &v), 1, "ncr(20,10) ret");
    expect_int(v, 184756, "ncr(20,10)=184756");

    v = 0;
    expect_int((ac_int_t)ac_nt_ncr(40, 20, &v), 1, "ncr(40,20) ret");
    expect_int(v, 137846528820LL, "ncr(40,20)=137846528820");

    v = 0;
    expect_int((ac_int_t)ac_nt_ncr(52, 5, &v), 1, "ncr(52,5) ret");
    expect_int(v, 2598960, "ncr(52,5)=2598960");

    /* C(66,33) = 7219428434016265740 可表示——若实现为先乘后除的朴素版，
       最后一步中间积 C(65,32)*66 ≈ 2.4e20 会把它误判为溢出 */
    v = 0;
    expect_int((ac_int_t)ac_nt_ncr(66, 33, &v), 1, "ncr(66,33) ret");
    expect_int(v, 7219428434016265740LL, "ncr(66,33)=7219428434016265740");

    /* C(67,33) = C(67,34) ≈ 1.42e19 > INT64_MAX → 必返 0 */
    expect_int((ac_int_t)ac_nt_ncr(67, 33, &v), 0, "ncr(67,33) overflow -> 0");
    expect_int((ac_int_t)ac_nt_ncr(67, 34, &v), 0, "ncr(67,34) overflow -> 0");

    /* 非法输入 */
    expect_int((ac_int_t)ac_nt_ncr(5, 7, &v), 0, "ncr(5,7) invalid -> 0");
    expect_int((ac_int_t)ac_nt_ncr(-1, 0, &v), 0, "ncr(-1,0) invalid -> 0");

    /* --- nPr 手册标准值 --- */
    v = 0;
    expect_int((ac_int_t)ac_nt_npr(5, 3, &v), 1, "npr(5,3) ret");
    expect_int(v, 60, "npr(5,3)=60");

    v = 0;
    expect_int((ac_int_t)ac_nt_npr(10, 4, &v), 1, "npr(10,4) ret");
    expect_int(v, 5040, "npr(10,4)=5040");

    v = 0;
    expect_int((ac_int_t)ac_nt_npr(52, 5, &v), 1, "npr(52,5) ret");
    expect_int(v, 311875200LL, "npr(52,5)=311875200");

    /* 20! = 2432902008176640000，int64 边界内最大阶乘 */
    v = 0;
    expect_int((ac_int_t)ac_nt_npr(20, 20, &v), 1, "npr(20,20) ret");
    expect_int(v, 2432902008176640000LL, "npr(20,20)=20!");

    /* 21! ≈ 5.1e19 > INT64_MAX → 必返 0 */
    expect_int((ac_int_t)ac_nt_npr(21, 21, &v), 0, "npr(21,21) overflow -> 0");

    /* 非法输入 */
    expect_int((ac_int_t)ac_nt_npr(0, 0, &v), 1, "npr(0,0) ret");
    expect_int(v, 1, "npr(0,0)=1");
    expect_int((ac_int_t)ac_nt_npr(3, 5, &v), 0, "npr(3,5) invalid -> 0");
    expect_int((ac_int_t)ac_nt_npr(5, -1, &v), 0, "npr(5,-1) invalid -> 0");
}

/* ==================== Part E：复位 ==================== */
static void test_reset(void) {
    (void)ac_nt_factorize(360);          /* 2^3*3^2*5，先弄脏全局槽位 */
    ac_nt_reset();
    expect_int(g_nt_count, 0, "reset count=0");
    expect_int(g_nt_primes[0], 0, "reset primes[0]=0");
    expect_int(g_nt_exps[0], 0, "reset exps[0]=0");
    expect_int(g_nt_primes[15], 0, "reset primes[15]=0");
    expect_int(g_nt_exps[15], 0, "reset exps[15]=0");
}

/* ==================== Part F：Miller-Rabin（第 9 轮） ==================== */
static void test_miller_rabin(void) {
    int i;
    /* 12 个底数本身 */
    {
        static const int64_t bases[12] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
        for (i = 0; i < 12; i++) {
            if (ac_nt_is_prime_mr(bases[i]) != 1) {
                s_fail++;
                printf("FAIL [numtheory] MR base " NT_I64 " not prime\n", bases[i]);
            } else { s_pass++; }
        }
    }
    expect_int(ac_nt_is_prime_mr(0), 0, "MR(0)=0");
    expect_int(ac_nt_is_prime_mr(1), 0, "MR(1)=0");
    expect_int(ac_nt_is_prime_mr(-7), 0, "MR(-7)=0");
    expect_int(ac_nt_is_prime_mr(4), 0, "MR(4)=0 (even)");
    expect_int(ac_nt_is_prime_mr(25), 0, "MR(25)=0");
    /* 强伪素数：2047 = 23·89 是 base-2 spsp（底数集整体仍判合成） */
    expect_int(ac_nt_is_prime_mr(2047), 0, "MR(2047)=0 (base-2 spsp)");
    /* 3215031751 = 151·751·28351：前 4 底数的最小强伪素数 */
    expect_int(ac_nt_is_prime_mr(3215031751LL), 0, "MR(3215031751)=0 (spsp 2,3,5,7)");
    /* Carmichael 数：费马通过、MR 拒绝 */
    expect_int(ac_nt_is_prime_mr(561), 0, "MR(561)=0 (Carmichael)");
    expect_int(ac_nt_is_prime_mr(1105), 0, "MR(1105)=0 (Carmichael)");
    expect_int(ac_nt_is_prime_mr(1729), 0, "MR(1729)=0 (Carmichael)");
    /* 大素数（试除版够不着的域） */
    expect_int(ac_nt_is_prime_mr(1000000007LL), 1, "MR(1e9+7)=1");
    expect_int(ac_nt_is_prime_mr(1000000009LL), 1, "MR(1e9+9)=1");
    expect_int(ac_nt_is_prime_mr(2305843009213693951LL), 1, "MR(2^61-1 Mersenne)=1");
    expect_int(ac_nt_is_prime_mr(9223372036854775783LL), 1,
               "MR(largest prime < 2^63)=1");
    /* 与试除版全量交叉验证 0..5000（实现自洽性） */
    {
        int mismatch = 0;
        for (i = 0; i <= 5000; i++) {
            if (ac_nt_is_prime_mr(i) != ac_nt_is_prime(i)) { mismatch++; }
        }
        expect_int(mismatch, 0, "MR vs trial cross-check 0..5000");
    }
}

/* ==================== Part G：模幂 ==================== */
static void test_pow_mod(void) {
    expect_int((int64_t)ac_nt_pow_mod(2u, 10u, 1000u), 24, "2^10 mod 1000 = 24");
    expect_int((int64_t)ac_nt_pow_mod(3u, 100u, 7u), 4, "3^100 mod 7 = 4");
    expect_int((int64_t)ac_nt_pow_mod(5u, 3u, 1u), 0, "x^y mod 1 = 0");
    expect_int((int64_t)ac_nt_pow_mod(7u, 0u, 13u), 1, "x^0 mod m = 1");
    expect_int((int64_t)ac_nt_pow_mod(123u, 456u, 1000000007u), 565291922,
                "123^456 mod 1e9+7 = 565291922");
    expect_int((int64_t)ac_nt_pow_mod(2u, 62u, 4000000000u), 2427387904,
                "2^62 mod 4e9 = 2427387904");
    /* 与 bigint 交叉验证：2^100 mod (1e9+7) */
    {
        int i;
        ac_bi_from_int64(&g_bi_a, 2);
        for (i = 0; i < 99; i++) {
            ac_bi_from_int64(&g_bi_work1, 2);
            ac_bi_mul(&g_bi_a, &g_bi_work1, &g_bi_a);
        }
        ac_bi_from_int64(&g_bi_b, 1000000007);
        ac_bi_divmod(&g_bi_a, &g_bi_b, &g_bi_tmp, &g_bi_work2);
        {
            uint64_t pm = ac_nt_pow_mod(2u, 100u, 1000000007u);
            int64_t bi_r = 0;
            (void)ac_bi_to_int64(&g_bi_work2, &bi_r);
            expect_int(bi_r, (int64_t)pm, "pow_mod 2^100 == bigint remainder");
        }
    }
}

/* ==================== Part H：模逆元 ==================== */
static void test_inv_mod(void) {
    expect_int(ac_nt_inv_mod(3, 7), 5, "inv(3,7)=5");
    expect_int(ac_nt_inv_mod(2, 4), 0, "inv(2,4)=0 (gcd=2)");
    expect_int(ac_nt_inv_mod(6, 9), 0, "inv(6,9)=0 (gcd=3)");
    expect_int(ac_nt_inv_mod(7, 13), 2, "inv(7,13)=2");
    expect_int(ac_nt_inv_mod(11, 29), 8, "inv(11,29)=8");
    expect_int(ac_nt_inv_mod(5, 13), 8, "inv(5,13)=8");
    expect_int(ac_nt_inv_mod(1, 7), 1, "inv(1,7)=1");
    expect_int(ac_nt_inv_mod(-3, 7), 2, "inv(-3,7)=2 (normalized)");
    expect_int(ac_nt_inv_mod(0, 7), 0, "inv(0,7)=0");
    expect_int(ac_nt_inv_mod(5, 1), 0, "inv(x,1)=0");
    /* 一致性：inv == a^(m-2) mod m（费马小定律，m 素数） */
    expect_int(ac_nt_inv_mod(12345, 1000000007),
               (int64_t)ac_nt_pow_mod(12345u, 1000000005u, 1000000007u),
               "inv(12345,1e9+7) == a^(m-2) (Fermat)");
}

int test_numtheory_run(void) {
    test_gcd_lcm();
    test_is_prime();
    test_factorize();
    test_ncr_npr();
    test_reset();
    test_miller_rabin();
    test_pow_mod();
    test_inv_mod();

    printf("==== C99 numtheory: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
