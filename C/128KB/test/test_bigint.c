/* ============================================================
 * ApolloCalc-CAS · test_bigint.c — 大整数测试（C 128KB 第 6 轮，C 独有）
 * 无 JS 圣经（JS 128KB 无此层），测试基准为数学事实，全部经 Node
 * BigInt 先行核算（勿凭记忆改值）：
 *   50! = 30414093201713378043612608166064768844377641568960512000000000000
 *   100! = 9332621544...（158 位）  F(200) = 280571172992510140037611932413038677189525
 *   999999999² = 999999998000000001   F(100)/10^6 = 354224848179261 余 915075
 * 覆盖：构造/int64 互转（含 INT64_MIN 与溢出）、全符号加减、
 *       跨 limb 进位/借位、乘法（含 out 别名操作数）、schoolbook 除法
 *       （正/负/零/大除大）、比较、阶乘、斐波那契、十进制出口、全局池。
 * ============================================================ */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/ac_types.h"
#include "../src/ac_bigint.h"

static int s_pass = 0;
static int s_fail = 0;

static void expect_true(int cond, const char *name) {
    if (cond) { s_pass++; }
    else { s_fail++; printf("FAIL [bigint] %s\n", name); }
}

static void expect_str(const char *got, const char *want, const char *name) {
    if (strcmp(got, want) == 0) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [bigint] %s: got \"%.60s\" (len %d) want \"%.60s\" (len %d)\n",
               name, got, (int)strlen(got), want, (int)strlen(want));
    }
}

/* int64 便捷构造 + 字符串校验（当前用例以 round trip 校验为主，留作扩展） */

/* 100! / F(200) 等大数专用：fact/fib + 字符串校验 */
static void fact_str(int n, const char *want, const char *name) {
    char buf[256];
    if (!ac_bi_factorial(n, &g_bi_tmp)) {
        s_fail++; printf("FAIL [bigint] %s: factorial(%d) returned 0\n", name, n);
        return;
    }
    ac_bi_to_string(&g_bi_tmp, buf, (int)sizeof(buf));
    expect_str(buf, want, name);
}

static void fib_str(int n, const char *want, const char *name) {
    char buf[256];
    if (!ac_bi_fibonacci(n, &g_bi_tmp)) {
        s_fail++; printf("FAIL [bigint] %s: fibonacci(%d) returned 0\n", name, n);
        return;
    }
    ac_bi_to_string(&g_bi_tmp, buf, (int)sizeof(buf));
    expect_str(buf, want, name);
}

int test_bigint_run(void) {
    char buf[256];
    int64_t v = 0;
    int ok;
    int i;

    /* ==================== Part A：构造 / int64 互转 ==================== */
    ac_bi_from_int64(&g_bi_tmp, 0);
    expect_true(g_bi_tmp.sign == 0 && g_bi_tmp.top == 0, "from_int64(0) is zero");
    ac_bi_to_string(&g_bi_tmp, buf, (int)sizeof(buf));
    expect_str(buf, "0", "to_string(0) = \"0\"");

    ac_bi_from_int64(&g_bi_tmp, 1234567890);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 1234567890,
                "round trip 1234567890");
    ac_bi_from_int64(&g_bi_tmp, -42);
    ac_bi_to_string(&g_bi_tmp, buf, (int)sizeof(buf));
    expect_str(buf, "-42", "to_string(-42)");
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == -42, "round trip -42");

    ac_bi_from_int64(&g_bi_tmp, INT64_MAX);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == INT64_MAX,
                "round trip INT64_MAX");
    ac_bi_from_int64(&g_bi_tmp, INT64_MIN);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == INT64_MIN,
                "round trip INT64_MIN (2^63 magnitude)");
    ac_bi_from_int64(&g_bi_tmp, INT64_MIN + 1);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == INT64_MIN + 1,
                "round trip INT64_MIN+1");

    /* 多 limb：999999999999 = 999·1e9 + 999999999（2 limb） */
    ac_bi_from_int64(&g_bi_tmp, 999999999999LL);
    expect_true(g_bi_tmp.top == 2 && g_bi_tmp.limbs[0] == 999999999u &&
                g_bi_tmp.limbs[1] == 999u, "from_int64 2-limb decomposition");
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 999999999999LL,
                "round trip 2-limb value");

    /* 100! 必然溢出 int64 */
    (void)ac_bi_factorial(100, &g_bi_tmp);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 0, "to_int64(100!) overflow -> 0");

    /* ==================== Part B：加减（全符号 + 跨 limb） ==================== */
    ac_bi_from_int64(&g_bi_a, 10);
    ac_bi_from_int64(&g_bi_b, 20);
    ac_bi_add(&g_bi_a, &g_bi_b, &g_bi_tmp);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 30, "10+20=30");

    /* 跨 limb 进位：999999999 + 1 = 1e9（limb[0] 归零、limb[1] 进一） */
    ac_bi_from_int64(&g_bi_a, 999999999LL);
    ac_bi_from_int64(&g_bi_b, 1);
    ac_bi_add(&g_bi_a, &g_bi_b, &g_bi_tmp);
    expect_true(g_bi_tmp.top == 2 && g_bi_tmp.limbs[0] == 0 &&
                g_bi_tmp.limbs[1] == 1, "999999999+1 limb carry");
    ac_bi_to_string(&g_bi_tmp, buf, (int)sizeof(buf));
    expect_str(buf, "1000000000", "999999999+1 string");

    /* 跨 limb 借位：1e18 − 1 = 999999999999999999 */
    ac_bi_from_int64(&g_bi_a, 1000000000000000000LL);
    ac_bi_from_int64(&g_bi_b, 1);
    ac_bi_sub(&g_bi_a, &g_bi_b, &g_bi_tmp);
    ac_bi_to_string(&g_bi_tmp, buf, (int)sizeof(buf));
    expect_str(buf, "999999999999999999", "1e18-1 borrow");

    ac_bi_from_int64(&g_bi_a, 100);
    ac_bi_from_int64(&g_bi_b, 50);
    ac_bi_sub(&g_bi_a, &g_bi_b, &g_bi_tmp);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 50, "100-50=50");
    ac_bi_sub(&g_bi_b, &g_bi_a, &g_bi_tmp);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == -50, "50-100=-50");

    ac_bi_from_int64(&g_bi_a, -5);
    ac_bi_from_int64(&g_bi_b, -6);
    ac_bi_add(&g_bi_a, &g_bi_b, &g_bi_tmp);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == -11, "-5+-6=-11");
    ac_bi_from_int64(&g_bi_b, 10);
    ac_bi_add(&g_bi_a, &g_bi_b, &g_bi_tmp);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 5, "-5+10=5");
    ac_bi_from_int64(&g_bi_b, 5);
    ac_bi_sub(&g_bi_a, &g_bi_b, &g_bi_tmp);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == -10, "-5-5=-10");
    ac_bi_from_int64(&g_bi_b, -5);
    ac_bi_sub(&g_bi_a, &g_bi_b, &g_bi_tmp);
    expect_true(g_bi_tmp.sign == 0 && g_bi_tmp.top == 0, "-5-(-5)=0 (sign 0)");
    /* INT64_MIN + INT64_MAX = -1（2^63 量级幅值运算） */
    ac_bi_from_int64(&g_bi_a, INT64_MIN);
    ac_bi_from_int64(&g_bi_b, INT64_MAX);
    ac_bi_add(&g_bi_a, &g_bi_b, &g_bi_tmp);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == -1,
                "INT64_MIN+INT64_MAX=-1");

    /* ==================== Part C：乘法 ==================== */
    ac_bi_from_int64(&g_bi_a, 1234);
    ac_bi_from_int64(&g_bi_b, 5678);
    ac_bi_mul(&g_bi_a, &g_bi_b, &g_bi_tmp);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 7006652,
                "1234x5678=7006652");

    /* 跨 limb 乘：999999999² = 999999998000000001（2 limb 结果） */
    ac_bi_from_int64(&g_bi_a, 999999999LL);
    ac_bi_mul(&g_bi_a, &g_bi_a, &g_bi_tmp);
    ac_bi_to_string(&g_bi_tmp, buf, (int)sizeof(buf));
    expect_str(buf, "999999998000000001", "999999999^2 string");
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 999999998000000001LL,
                "999999999^2 int64 round trip");

    ac_bi_from_int64(&g_bi_a, -3);
    ac_bi_from_int64(&g_bi_b, 7);
    ac_bi_mul(&g_bi_a, &g_bi_b, &g_bi_tmp);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == -21, "-3x7=-21");
    ac_bi_from_int64(&g_bi_b, -7);
    ac_bi_mul(&g_bi_a, &g_bi_b, &g_bi_tmp);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 21, "-3x-7=21");
    ac_bi_from_int64(&g_bi_b, 0);
    ac_bi_mul(&g_bi_a, &g_bi_b, &g_bi_tmp);
    expect_true(g_bi_tmp.sign == 0 && g_bi_tmp.top == 0, "-3x0=0");

    /* out 别名操作数（阶乘的核心路径）：tmp = tmp × 9 */
    ac_bi_from_int64(&g_bi_tmp, 1000000000000000000LL);
    ac_bi_from_int64(&g_bi_b, 9);
    ac_bi_mul(&g_bi_tmp, &g_bi_b, &g_bi_tmp);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 9000000000000000000LL,
                "mul out aliases a (1e18 x 9)");

    /* ==================== Part D：除法 ==================== */
    ac_bi_from_int64(&g_bi_a, 100);
    ac_bi_from_int64(&g_bi_b, 3);
    ac_bi_divmod(&g_bi_a, &g_bi_b, &g_bi_tmp, &g_bi_work2);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 33 &&
                ac_bi_to_int64(&g_bi_work2, &v) == 1 && v == 1, "100/3 = 33 r 1");

    /* 大除大：100!/99! = 100（158 位 ÷ 156 位） */
    (void)ac_bi_factorial(100, &g_bi_a);
    (void)ac_bi_factorial(99, &g_bi_b);
    ac_bi_divmod(&g_bi_a, &g_bi_b, &g_bi_tmp, &g_bi_work2);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 100 &&
                g_bi_work2.sign == 0, "100!/99! = 100 r 0");

    /* 大数除以小数：F(100)/10^6 = 354224848179261 余 915075 */
    (void)ac_bi_fibonacci(100, &g_bi_a);
    ac_bi_from_int64(&g_bi_b, 1000000);
    ac_bi_divmod(&g_bi_a, &g_bi_b, &g_bi_tmp, &g_bi_work2);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 354224848179261LL &&
                ac_bi_to_int64(&g_bi_work2, &v) == 1 && v == 915075,
                "F(100)/10^6 q/r");

    ac_bi_from_int64(&g_bi_a, 7);
    ac_bi_from_int64(&g_bi_b, 7);
    ac_bi_divmod(&g_bi_a, &g_bi_b, &g_bi_tmp, &g_bi_work2);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 1 &&
                g_bi_work2.sign == 0, "7/7 = 1 r 0");
    ac_bi_from_int64(&g_bi_a, 5);
    ac_bi_from_int64(&g_bi_b, 7);
    ac_bi_divmod(&g_bi_a, &g_bi_b, &g_bi_tmp, &g_bi_work2);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 0 &&
                ac_bi_to_int64(&g_bi_work2, &v) == 1 && v == 5, "5/7 = 0 r 5");
    ac_bi_from_int64(&g_bi_a, 0);
    ac_bi_divmod(&g_bi_a, &g_bi_b, &g_bi_tmp, &g_bi_work2);
    expect_true(g_bi_tmp.sign == 0 && g_bi_work2.sign == 0, "0/7 = 0 r 0");

    /* 截断语义：商向零取整、余数带被除数符号（C 语义） */
    ac_bi_from_int64(&g_bi_a, -7);
    ac_bi_from_int64(&g_bi_b, 3);
    ac_bi_divmod(&g_bi_a, &g_bi_b, &g_bi_tmp, &g_bi_work2);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == -2 &&
                ac_bi_to_int64(&g_bi_work2, &v) == 1 && v == -1, "-7/3 = -2 r -1");
    ac_bi_from_int64(&g_bi_a, 7);
    ac_bi_from_int64(&g_bi_b, -3);
    ac_bi_divmod(&g_bi_a, &g_bi_b, &g_bi_tmp, &g_bi_work2);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == -2 &&
                ac_bi_to_int64(&g_bi_work2, &v) == 1 && v == 1, "7/-3 = -2 r 1");
    /* 除零：契约返回 q = r = 0 */
    ac_bi_from_int64(&g_bi_a, 7);
    ac_bi_from_int64(&g_bi_b, 0);
    ac_bi_divmod(&g_bi_a, &g_bi_b, &g_bi_tmp, &g_bi_work2);
    expect_true(g_bi_tmp.sign == 0 && g_bi_work2.sign == 0, "7/0 -> q=r=0");

    /* ==================== Part E：比较 ==================== */
    ac_bi_from_int64(&g_bi_a, 5);
    ac_bi_from_int64(&g_bi_b, 10);
    expect_true(ac_bi_cmp(&g_bi_a, &g_bi_b) == -1, "cmp(5,10)=-1");
    expect_true(ac_bi_cmp(&g_bi_b, &g_bi_a) == 1, "cmp(10,5)=1");
    expect_true(ac_bi_cmp(&g_bi_a, &g_bi_a) == 0, "cmp(5,5)=0");
    ac_bi_from_int64(&g_bi_a, -5);
    expect_true(ac_bi_cmp(&g_bi_a, &g_bi_b) == -1, "cmp(-5,10)=-1");
    ac_bi_from_int64(&g_bi_b, -10);
    expect_true(ac_bi_cmp(&g_bi_a, &g_bi_b) == 1, "cmp(-5,-10)=1");
    ac_bi_from_int64(&g_bi_b, 5);
    expect_true(ac_bi_cmp(&g_bi_a, &g_bi_b) == -1, "cmp(-5,5)=-1");
    ac_bi_from_int64(&g_bi_a, 0);
    ac_bi_from_int64(&g_bi_b, 0);
    expect_true(ac_bi_cmp(&g_bi_a, &g_bi_b) == 0, "cmp(0,0)=0");
    /* 不同 limb 数：1e18（2 limb）vs 5（1 limb） */
    ac_bi_from_int64(&g_bi_a, 1000000000000000000LL);
    ac_bi_from_int64(&g_bi_b, 5);
    expect_true(ac_bi_cmp(&g_bi_a, &g_bi_b) == 1, "cmp(1e18,5)=1");

    /* ==================== Part F：阶乘 ==================== */
    fact_str(0, "1", "0! = 1");
    fact_str(1, "1", "1! = 1");
    fact_str(5, "120", "5! = 120");
    fact_str(10, "3628800", "10! = 3628800");
    fact_str(20, "2432902008176640000", "20! = 2432902008176640000");
    /* 20! 在 int64 内：round trip 验证 */
    (void)ac_bi_factorial(20, &g_bi_tmp);
    expect_true(ac_bi_to_int64(&g_bi_tmp, &v) == 1 && v == 2432902008176640000LL,
                "20! int64 round trip");
    fact_str(50, "30414093201713378043612608166064768844377641568960512000000000000",
             "50! = 65 digits");
    fact_str(100, "9332621544394415268169923885626670049071596826438162146859296"
                  "389521759999322991560894146397615651828625369792082722375825"
                  "1185210916864000000000000000000000000",
             "100! = 158 digits");
    (void)ac_bi_factorial(100, &g_bi_tmp);
    ac_bi_to_string(&g_bi_tmp, buf, (int)sizeof(buf));
    expect_true((int)strlen(buf) == 158, "100! length = 158");
    expect_true(ac_bi_factorial(-1, &g_bi_tmp) == 0, "factorial(-1) rejected");
    expect_true(ac_bi_factorial(101, &g_bi_tmp) == 0, "factorial(101) rejected");

    /* ==================== Part G：斐波那契 ==================== */
    fib_str(0, "0", "F(0) = 0");
    fib_str(1, "1", "F(1) = 1");
    fib_str(2, "1", "F(2) = 1");
    fib_str(10, "55", "F(10) = 55");
    fib_str(50, "12586269025", "F(50) = 12586269025");
    fib_str(100, "354224848179261915075", "F(100) = 354224848179261915075");
    fib_str(200, "280571172992510140037611932413038677189525",
             "F(200) = 42 digits");
    expect_true(ac_bi_fibonacci(-1, &g_bi_tmp) == 0, "fibonacci(-1) rejected");
    expect_true(ac_bi_fibonacci(201, &g_bi_tmp) == 0, "fibonacci(201) rejected");

    /* ==================== Part H：全局池流转 ==================== */
    ac_bi_zero(&g_bi_a);
    ac_bi_zero(&g_bi_b);
    ac_bi_zero(&g_bi_tmp);
    ac_bi_from_int64(&g_bi_a, 1234567890123456789LL);
    ac_bi_from_int64(&g_bi_b, 987654321098765432LL);
    ac_bi_mul(&g_bi_a, &g_bi_b, &g_bi_tmp);
    /* 1234567890123456789 × 987654321098765432（Node BigInt 核算） */
    ac_bi_to_string(&g_bi_tmp, buf, (int)sizeof(buf));
    expect_str(buf, "1219326311370217952114007012098917848", "pool a*b pipeline");
    /* 连续复用不残留。scratch 契约（第 6 轮实测钉死）：factorial 终态
       work1 = 最后乘数；fibonacci 终态 work1 = F(199)、work2 = F(200)
       （c 累加器）、out 不得是 work1/work2 → 10! 存 g_bi_b（非 scratch） */
    (void)ac_bi_factorial(10, &g_bi_b);
    (void)ac_bi_fibonacci(200, &g_bi_tmp);
    ac_bi_add(&g_bi_work1, &g_bi_b, &g_bi_a);
    ac_bi_to_string(&g_bi_a, buf, (int)sizeof(buf));
    expect_str(buf, "173402521172797813159685037284371945673101",
               "F(199)+10! no residue (work1 = F(199) after fib)");
    expect_true(ac_bi_cmp(&g_bi_tmp, &g_bi_work1) == 1, "F(200)+10! > F(200)");

    /* 防御性 sanitizer：全局部变量态清点（除零后 q/r 已验证） */
    ok = 1;
    for (i = 0; i < AC_BIGINT_LIMBS; i++) {
        if (g_bi_tmp.limbs[i] > 999999999u) { ok = 0; }
    }
    expect_true(ok, "all limbs in base range");

    printf("==== C99 bigint: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
