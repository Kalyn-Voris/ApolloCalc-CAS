/* ============================================================
 * ApolloCalc-CAS · ac_numtheory.c — 数论层（第 7 轮，C 独有）
 * JS 64KB / JS 128KB 均无此层，测试基准为数学手册标准值。
 * 功能：gcd/lcm（独立实现，不复用 exact 的 ac_gcd，保持模块隔离）、
 *       is_prime（6k±1 试除，无 Miller-Rabin）、
 *       factorize（试除：先提 2 再扫奇数，无 Pollard-rho）、
 *       ncr/npr（迭代乘除 + 每步溢出预检）。
 * Zero-GC：无 malloc、无 VLA、无递归（ncr 调 ac_nt_gcd 是迭代函数，非自调用）。
 * 溢出判定复用 ac_exact.h 的 ac_safe_mul，不重复实现。
 * ============================================================ */
#include "ac_numtheory.h"
#include "ac_exact.h"

ac_int_t g_nt_primes[AC_NT_MAX_FACTORS];
int      g_nt_exps[AC_NT_MAX_FACTORS];
int      g_nt_count = 0;

/* ==================== gcd / lcm ==================== */

ac_int_t ac_nt_gcd(ac_int_t a, ac_int_t b) {
    /* [English] Negating INT64_MIN overflows (|INT64_MIN| > INT64_MAX).
       Same convention as ac_gcd: clamp to INT64_MAX — unreachable from
       guarded call sites (exact layer rejects INT64_MIN at every entry). */
    if (a == INT64_MIN) { a = INT64_MAX; }
    if (b == INT64_MIN) { b = INT64_MAX; }
    if (a < 0) { a = -a; }
    if (b < 0) { b = -b; }
    while (b != 0) {
        ac_int_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

ac_int_t ac_nt_lcm(ac_int_t a, ac_int_t b) {
    ac_int_t g;
    ac_int_t ag;
    ac_int_t prod;
    if (a == 0 || b == 0) { return 0; }
    /* [English] Any lcm involving 2^63 is a multiple of 2^63 — never
       representable in int64_t. Reject up front instead of clamping. */
    if (a == INT64_MIN || b == INT64_MIN) { return 0; }
    g = ac_nt_gcd(a, b);
    ag = a / g;                    /* g >= 1 and |a| <= INT64_MAX: no UB */
    if (!ac_safe_mul(ag, b, &prod)) { return 0; }
    /* |prod| <= INT64_MAX here (safe_mul rejects 2^63-scale products) */
    return prod < 0 ? -prod : prod;
}

/* ==================== is_prime ==================== */

int ac_nt_is_prime(ac_int_t n) {
    ac_int_t i;
    if (n < 2) { return 0; }
    if (n < 4) { return 1; }             /* 2, 3 */
    if (n % 2 == 0 || n % 3 == 0) { return 0; }
    /* [English] 6k+-1 wheel; bound in division form (i > n/i) to avoid
       i*i overflow near 3.04e9. Equality must be accepted: n = 25 needs
       i = 5 where 5 == 25/5. */
    for (i = 5; i <= n / i; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) { return 0; }
    }
    return 1;
}

/* ==================== factorize ==================== */

int ac_nt_factorize(ac_int_t n) {
    ac_int_t i;
    g_nt_count = 0;
    if (n <= 0) { return 0; }

    /* 提出所有 2 的因子（此后 n 为奇数，步长 2 扫奇数即可：
       合数 i 的奇素因子必已除尽，不会漏也不会误判） */
    if (n % 2 == 0) {
        int e = 0;
        while (n % 2 == 0) { n /= 2; e++; }
        g_nt_primes[g_nt_count] = 2;
        g_nt_exps[g_nt_count] = e;
        g_nt_count++;
    }

    /* [English] Odd trial division; bound in division form (i > n/i) —
       n shrinks as factors are stripped, so the bound tightens too. */
    for (i = 3; i <= n / i; i += 2) {
        if (n % i == 0) {
            if (g_nt_count >= AC_NT_MAX_FACTORS) { return 0; }
            {
                int e = 0;
                while (n % i == 0) { n /= i; e++; }
                g_nt_primes[g_nt_count] = i;
                g_nt_exps[g_nt_count] = e;
                g_nt_count++;
            }
        }
    }

    /* 剩余 n > 1 是最后一个素数因子（> sqrt(已除尽的 n) 至多一个） */
    if (n > 1) {
        if (g_nt_count >= AC_NT_MAX_FACTORS) { return 0; }
        g_nt_primes[g_nt_count] = n;
        g_nt_exps[g_nt_count] = 1;
        g_nt_count++;
    }

    return 1;
}

/* ==================== nCr / nPr ==================== */

int ac_nt_ncr(int n, int r, ac_int_t *out) {
    ac_int_t result;
    ac_int_t base;
    int i;
    if (n < 0 || r < 0 || r > n) { return 0; }
    if (r == 0 || r == n) { *out = 1; return 1; }
    /* 对称性：C(n, r) = C(n, n-r)，取小的 r 减少迭代 */
    if (r > n - r) { r = n - r; }
    base = (ac_int_t)(n - r);
    result = 1;
    /* [English] Invariant: after step i, result == C(base+i, i), rising
       monotonically to C(n, r). Naive multiply-first overflows the
       intermediate on representable answers (C(66,33) = 7219428434016265740
       fits int64_t, but its last step would compute C(65,32)*66 ≈ 2.4e20
       before dividing by 33). Reducing the fraction (n-r+i)/i against
       result first keeps every multiplication an exact binomial
       <= C(n, r), so overflow detection stays exact. */
    for (i = 1; i <= r; i++) {
        ac_int_t m = base + (ac_int_t)i;   /* 乘子 n-r+i */
        ac_int_t d = (ac_int_t)i;          /* 除子 i */
        ac_int_t g;
        g = ac_nt_gcd(m, d);               /* 约掉乘子与除子的公因子 */
        m /= g;
        d /= g;
        g = ac_nt_gcd(result, d);          /* 再约掉 result 里已有的因子 */
        result /= g;
        d /= g;
        if (d != 1) {
            /* [English] Math guarantees d | m after the two reductions
               (gcd(d, result) = 1 and d | result*m). Defensive only. */
            if (m % d != 0) { return 0; }
            m /= d;
        }
        if (!ac_safe_mul(result, m, &result)) { return 0; }
    }
    *out = result;
    return 1;
}

int ac_nt_npr(int n, int r, ac_int_t *out) {
    ac_int_t result;
    int i;
    if (n < 0 || r < 0 || r > n) { return 0; }
    if (r == 0) { *out = 1; return 1; }
    result = 1;
    /* 中间值 P(n,k) 单调升至 P(n,r)：最终答案可表示则中间必可表示，
       safe_mul 的判定即为精确判定，无误报 */
    for (i = 0; i < r; i++) {
        ac_int_t factor = (ac_int_t)(n - i);
        if (!ac_safe_mul(result, factor, &result)) { return 0; }
    }
    *out = result;
    return 1;
}

/* ==================== reset ==================== */

void ac_nt_reset(void) {
    int i;
    for (i = 0; i < AC_NT_MAX_FACTORS; i++) {
        g_nt_primes[i] = 0;
        g_nt_exps[i] = 0;
    }
    g_nt_count = 0;
}

/* ==================== C 128KB 第 9 轮：模算术 + Miller-Rabin ====================
 * mulmod 用俄民乘法（加倍 + 条件减法）避开 128 位中间量：
 * 全程 uint64（m < 2^63 保证 a+b、2a 不回绕出模外）。
 * MR 底数集 {2,3,5,7,11,13,17,19,23,29,31,37} 对全部 int64 确定性
 * （已知结果：该集合在 3.3e24 以下无强伪素数）。 */

static const uint64_t NT_MR_BASES[12] = {
    2u, 3u, 5u, 7u, 11u, 13u, 17u, 19u, 23u, 29u, 31u, 37u
};

/* (a + b) mod m：0 <= a,b < m < 2^63 → uint64 加法不回绕 */
static uint64_t nt_add_mod(uint64_t a, uint64_t b, uint64_t m) {
    uint64_t s = a + b;
    if (s >= m) { s -= m; }
    return s;
}

/* (a * b) mod m：俄民乘法，64 轮加倍 */
static uint64_t nt_mul_mod(uint64_t a, uint64_t b, uint64_t m) {
    uint64_t r = 0u;
    a %= m;
    b %= m;
    while (b > 0u) {
        if (b & 1u) { r = nt_add_mod(r, a, m); }
        a = nt_add_mod(a, a, m);
        b >>= 1u;
    }
    return r;
}

uint64_t ac_nt_pow_mod(uint64_t base, uint64_t exp, uint64_t mod) {
    uint64_t r;
    if (mod <= 1u) { return 0u; }
    base %= mod;
    r = 1u;
    while (exp > 0u) {
        if (exp & 1u) { r = nt_mul_mod(r, base, mod); }
        base = nt_mul_mod(base, base, mod);
        exp >>= 1u;
    }
    return r;
}

int64_t ac_nt_inv_mod(int64_t a, int64_t m) {
    int64_t t = 0;
    int64_t newt = 1;
    int64_t rr;
    int64_t newr;
    int64_t q;
    int64_t tmp;

    if (m <= 1) { return 0; }
    /* a 归一化到 [0, m)：a % m 对负 a 落在 (−m, 0]，+m 无溢出 */
    a = a % m;
    if (a < 0) { a += m; }
    if (a == 0) { return 0; }
    rr = m;
    newr = a;
    while (newr != 0) {
        q = rr / newr;
        tmp = t - q * newt; t = newt; newt = tmp;
        tmp = rr - q * newr; rr = newr; newr = tmp;
    }
    if (rr != 1) { return 0; }       /* gcd != 1：无逆元 */
    if (t < 0) { t += m; }
    return t;
}

int ac_nt_is_prime_mr(int64_t n) {
    uint64_t un;
    uint64_t d;
    uint64_t one;
    uint64_t nm1;
    int s = 0;
    int i;
    int r;
    int b;

    if (n < 2) { return 0; }
    un = (uint64_t)n;
    for (i = 0; i < 12; i++) {
        if (un == NT_MR_BASES[i]) { return 1; }        /* 命中小素数表 */
        if (un % NT_MR_BASES[i] == 0u) { return 0; }   /* 小因子整除 */
    }
    /* n−1 = d·2^s（d 奇） */
    d = un - 1u;
    while ((d & 1u) == 0u) { d >>= 1u; s++; }
    one = 1u;
    nm1 = un - 1u;
    for (b = 0; b < 12; b++) {
        uint64_t a = NT_MR_BASES[b];
        uint64_t x;
        int hit_nm1 = 0;                /* 通过标志：仅命中 n−1 时置位
                                           （对齐 Node 参考——初值必须为"未通过"，
                                           否则 s=1 且 x0 ∉ {±1} 的合成数漏判） */
        if (a >= un) { continue; }                     /* 底数 >= n 跳过 */
        x = ac_nt_pow_mod(a, d, un);
        if (x == one || x == nm1) { continue; }        /* 本底数通过 */
        for (r = 1; r < s; r++) {
            x = nt_mul_mod(x, x, un);
            if (x == nm1) { hit_nm1 = 1; break; }      /* 命中 n−1：通过并停止
                                                          （继续平方得 1，无意义） */
        }
        if (!hit_nm1) { return 0; }                    /* 合成数 */
    }
    return 1;
}
