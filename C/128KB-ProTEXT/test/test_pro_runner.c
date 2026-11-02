/* ApolloCalc · test_pro_runner.c — ProTEXT 层闭环 runner，自带 main()

   覆盖上游回归（1..4 次精确路径、Zero-GC、parse/eval 链路）。阶段一是内存
   水位门禁重写 + 编译期尺寸校验。阶段二 B1 测五次及以上数值求根（7 例），
   A4 测 LU 分解（6 例），A6 测隐函数求导（9 例，含 3 个负向）。另有次数
   路由三态（1..4 精确 / 5..8 数值 / >8 拒绝）与 v2.2.0 ⑥ 的 2 变量
   Groebner 基（test_pro_groebner.c，12 例）。

   除 Zero-GC 开关 s_kernel_live 外，本文件不声明任何静态对象；测试计数
   与失败数放在 main 的栈上，用 tally_t 传指针。

   为什么这么较真：下面这条规则是实测钉死的，不是猜的。
     - TU 完全不产生 .bss 段            -> 全程序 bss = 42608 B（内核口径）
     - TU 只要产生 .bss 段（哪怕只有
       一个 4 B 的 static int）          -> 全程序 bss = 42640 B（+32 B）
   增量恒为 32 B = 16 B（该 TU 的 .bss 段本身）+ 16 B（链接期对齐填充），
   与静态对象的个数无关。

   本文件的 s_kernel_live 是必需的（Zero-GC 硬性校验的状态位，不能挪到
   栈上，它要跨 main 的整个生命周期，且 __wrap_* 在其它栈帧里读写它）。
   因此 runner 口径的门禁值是 42640，而账本的 42608 是内核口径（不链接
   本 TU），由 test/pro_kernel_main.c 单独构建校验。两个数都对，只是口径
   不同（详见 Makefile 的三层账本）。

   与上游 C/128KB/test/test_main.c 同语义：链接期用
   -Wl,--wrap=malloc -Wl,--wrap=free 把本文件里的 __wrap_malloc /
   __wrap_free 顶到 malloc/free 的位置；kernel 期（s_kernel_live=1）任何
   堆调用直接 abort。宿主 CRT 在 main 之前的合法分配转发给 msvcrt 自己的
   分配器（运行时 GetProcAddress 解析，因此二进制里不会出现静态
   malloc/free 符号）。 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* 本文件在 test/ 下：
   ../src/ac_pro.h               -> C/128KB-ProTEXT/src/（同层兄弟目录）
   ../../../C/128KB/src/ac_*.h   -> 仓库根/C/128KB/src/（上游只读头） */
#include "../src/ac_pro.h"
#include "../../../C/128KB/src/ac_parser.h"
#include "../../../C/128KB/src/ac_solvers.h"
#include "../../../C/128KB/src/ac_evaluator.h"
#include "../../../C/128KB/src/ac_matrix.h"

/* Pro 冒烟入口（src/ac_pro_smoke.c） */
int ac_pro_smoke_run(void);

/* Zero-GC 开关与链接期包装
 * s_kernel_live 是本文件唯一的静态对象（4 B），它使本 TU 产生一个
 * 16 B 的 .bss 段，叠加 16 B 链接对齐后把全程序 .bss 从 42608 推到
 * 42640（详见头注的受控实验与 Makefile 三层账本）。这是必需开销：
 * 状态位必须跨 main 生命周期存活，__wrap_* 在其它栈帧里读写它。 */

static int s_kernel_live = 0;   /* 0 = 宿主 CRT 阶段，1 = kernel 阶段 */

#ifdef _WIN32

typedef void *(*ac_host_malloc_t)(size_t);
typedef void  (*ac_host_free_t)(void *);

static void *host_malloc_call(size_t size) {
    ac_host_malloc_t fn =
        (ac_host_malloc_t)(void *)GetProcAddress(GetModuleHandleA("msvcrt.dll"), "malloc");
    if (fn == NULL) {
        fputs("[FATAL] cannot resolve host allocator\n", stderr);
        abort();
    }
    return fn(size);
}

static void host_free_call(void *ptr) {
    ac_host_free_t fn =
        (ac_host_free_t)(void *)GetProcAddress(GetModuleHandleA("msvcrt.dll"), "free");
    if (fn == NULL) {
        fputs("[FATAL] cannot resolve host deallocator\n", stderr);
        abort();
    }
    fn(ptr);
}

void *__wrap_malloc(size_t size) {
    if (s_kernel_live) {
        fputs("[FATAL] malloc called at runtime!\n", stderr);
        abort();
    }
    return host_malloc_call(size);
}

void __wrap_free(void *ptr) {
    if (s_kernel_live) {
        fputs("[FATAL] free called at runtime!\n", stderr);
        abort();
    }
    host_free_call(ptr);
}

#else

extern void *__real_malloc(size_t size);
extern void  __real_free(void *ptr);

void *__wrap_malloc(size_t size) {
    if (s_kernel_live) {
        fputs("[FATAL] malloc called at runtime!\n", stderr);
        abort();
    }
    return __real_malloc(size);
}

void __wrap_free(void *ptr) {
    if (s_kernel_live) {
        fputs("[FATAL] free called at runtime!\n", stderr);
        abort();
    }
    __real_free(ptr);
}

#endif /* _WIN32 */

/* 测试助手 */

#define TOL 1e-9

/* 计数走栈上结构体，刻意不用静态变量（见头注） */
typedef struct {
    int checks;
    int failed;
} tally_t;

static void check(tally_t *t, int cond, const char *name, const char *detail) {
    t->checks++;
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        t->failed++;
        printf("  [FAIL] %s  (%s)\n", name, detail);
    }
}

/* 与上游 test_solvers.c 的 solve_poly 同一流程 */
static int solve_quadratic_src(const char *src) {
    ac_solvers_reset();
    if (ac_parse_expression(src) != ERR_NONE) { return 0; }
    if (ac_extract_coefficients(2) != EVAL_OK) { return 0; }
    ac_solve_quadratic();
    return 1;
}

/* 根集合中是否含 want 这个实根（乱序容忍） */
static int has_real_root(double want) {
    int i;
    for (i = 0; i < g_root_count; i++) {
        if (fabs(g_root_im[i]) > TOL) { continue; }
        if (fabs(g_root_re[i] - want) <= TOL) { return 1; }
    }
    return 0;
}

/* 根集合中是否含 re±im 的共轭对（顺序无关） */
static int has_conjugate_pair(double want_re, double want_im) {
    int i;
    int hit_pos = 0;
    int hit_neg = 0;
    for (i = 0; i < g_root_count; i++) {
        if (fabs(g_root_re[i] - want_re) > TOL) { continue; }
        if (fabs(g_root_im[i] - want_im) <= TOL) { hit_pos = 1; }
        if (fabs(g_root_im[i] + want_im) <= TOL) { hit_neg = 1; }
    }
    return hit_pos && hit_neg;
}

/* 数值求根助手
 * 把根集合代回多项式，返回最大 |P(z)|。用来验证「找到的确实是根」，
 * 比逐根比对更稳健（复根的排序因算法而异，残差判据才是本质）。 */
static double max_poly_residual(const double *coef, int degree) {
    double worst = 0.0;
    int i;
    int k;
    for (k = 0; k < g_root_count; k++) {
        double zr = g_root_re[k];
        double zi = g_root_im[k];
        double pr = coef[degree];
        double pi = 0.0;
        double mag;
        for (i = degree - 1; i >= 0; i--) {
            double tr = pr * zr - pi * zi + coef[i];
            double ti = pr * zi + pi * zr;
            pr = tr;
            pi = ti;
        }
        mag = sqrt(pr * pr + pi * pi);
        if (mag > worst) { worst = mag; }
    }
    return worst;
}

/* 根集合里是否存在实根 want（容差用调用方给的） */
static int num_has_real_root(double want, double tol) {
    int i;
    for (i = 0; i < g_root_count; i++) {
        if (fabs(g_root_im[i]) > tol) { continue; }
        if (fabs(g_root_re[i] - want) <= tol) { return 1; }
    }
    return 0;
}

/* 上游回归 */

/* x^2 - 5x + 6 = 0 -> 两根 2 与 3 */
static void test_quadratic_real(tally_t *t) {
    printf("[case] x^2-5x+6=0  (expect 2 real roots: 2 and 3)\n");
    check(t, solve_quadratic_src("x^2-5x+6=0") == 1, "parse + extract coefficients", "parse/extract failed");
    check(t, g_solve_error == 0, "solver error == 0", "g_solve_error != 0");
    check(t, g_root_count == 2, "root count == 2", "g_root_count != 2");
    check(t, has_real_root(2.0), "root 2 found", "root 2 missing");
    check(t, has_real_root(3.0), "root 3 found", "root 3 missing");
    printf("         roots: %.17g, %.17g\n",
           g_root_count > 0 ? g_root_re[0] : 0.0,
           g_root_count > 1 ? g_root_re[1] : 0.0);
}

/* x^2 + 1 = 0 -> 两根 ±i */
static void test_quadratic_complex(tally_t *t) {
    int i;
    printf("[case] x^2+1=0  (expect 2 complex roots: +i and -i)\n");
    check(t, solve_quadratic_src("x^2+1=0") == 1, "parse + extract coefficients", "parse/extract failed");
    check(t, g_solve_error == 0, "solver error == 0", "g_solve_error != 0");
    check(t, g_root_count == 2, "root count == 2", "g_root_count != 2");
    for (i = 0; i < g_root_count; i++) {
        check(t, fabs(g_root_im[i]) > TOL, "imaginary part non-zero", "g_root_im == 0");
    }
    check(t, has_conjugate_pair(0.0, 1.0), "roots are +i and -i", "conjugate pair (0, +/-1) missing");
}

/* Pro 层自检 */
static void test_pro_smoke(tally_t *t) {
    int rc;
    printf("[case] ac_pro_smoke_run()  (Pro layer self-check)\n");
    rc = ac_pro_smoke_run();
    check(t, rc == 0, "smoke returns 0", "non-zero smoke error code");
    if (rc != 0) { printf("         smoke rc = %d\n", rc); }
}

/* 阶段一：尺寸与门禁 */

static void test_sizes(tally_t *t) {
    printf("[case] compile-time sizes (v2.1.0: scratch 3848, peak view sparse)\n");
    check(t, sizeof(ac_pro_scratch_t) == AC_PRO_SCRATCH_BYTES, "sizeof(ac_pro_scratch_t) == AC_PRO_SCRATCH_BYTES (3848)", "scratch size mismatch");
    check(t, sizeof(g_ac_pro_storage) == AC_PRO_STATIC_EXTRA, "sizeof(g_ac_pro_storage) == AC_PRO_STATIC_EXTRA (3936)", "storage size mismatch");
    check(t, sizeof(g_var_val) == 80, "sizeof(g_var_val) == 80", "var_val size mismatch");
    check(t, sizeof(ac_sym_node_t) == 24, "sizeof(ac_sym_node_t) == 24 (hole reused, no growth)", "sym node grew");
    check(t, offsetof(ac_sym_node_t, value) == 16, "offsetof(value) == 16 (hole at 12..15)", "layout drifted");
    check(t, AC_PRO_STATIC_FOOTPRINT == AC_PRO_TARGET_BSS, "ledger 38672+80+3848+8 == AC_PRO_TARGET_BSS (42608)", "ledger mismatch");
    printf("         scratch=%u storage=%u var_val=%u node=%u footprint=%d\n",
           (unsigned)sizeof(ac_pro_scratch_t), (unsigned)sizeof(g_ac_pro_storage),
           (unsigned)sizeof(g_var_val), (unsigned)sizeof(ac_sym_node_t),
           AC_PRO_STATIC_FOOTPRINT);
}

/* 内存水位门禁：设计说明 §9.5 的四条校验 + 门禁边界。
 * 设计说明 §6.3 的算式是 `38672 + 6144 = 44816`，那是上游口径的静态
 * 基数。Pro 的门禁必须按 Pro 的真实静态足迹算（否则又回到「低估」的
 * 老毛病），所以（v2.1.0，sparse 视图 3848 定峰）：
 *     idle = 38672 + 80 + 3848 + 8 = 42608  = AC_PRO_TARGET_BSS
 *     full = 42608 + 512×8 + 256×8 = 42608 + 6144 = 48752
 * 两者差 3936（var_val 80 + sparse 视图 3848 + align_pad 8），正是 Pro
 * 增量本身。门禁边界随之下移 3936：
 *     触发阈值 extra > 114688 - 48752 = 65936
 * 测试把边界钉在 ±1000 内（boundary 由 used 动态推出，账本改动时校验
 * 自动跟随，不需要再手抄数字）。 */
static void test_memory_watermark(tally_t *t) {
    double base;
    double used;
    double boundary;
    double over;
    double under;

    printf("[case] ac_pro_check_memory_watermark()  (upstream gate was dead code)\n");

    ac_solvers_reset();
    g_token_count = 0;
    g_number_count = 0;

    base = ac_pro_memory_used(0.0);
    /* used 必须 >= 38672（不再低估 g_ac_scratch + g_sym_nodes） */
    check(t, base >= 38672.0, "used >= 38672 (covers g_ac_scratch + g_sym_nodes)", "gate under-estimates");
    /* Pro 的静态足迹精确等于 AC_PRO_TARGET_BSS（v2.1.0: 42608） */
    check(t, base == (double)AC_PRO_TARGET_BSS, "used == AC_PRO_TARGET_BSS at idle (38672 + 80 + 3848 + 8)", "idle used != target");
    /* 满池时 used = 42608 + 6144 = 48752 < 114688 */
    g_token_count = TOKEN_POOL_SIZE;
    g_number_count = NUMBER_POOL_SIZE;
    used = ac_pro_memory_used(0.0);
    check(t, used == (double)(AC_PRO_TARGET_BSS + 6144), "full pools -> used == AC_PRO_TARGET_BSS + 6144 (48752)", "full-pool used != expected");
    check(t, ac_pro_check_memory_watermark() == 0, "full pools still pass the gate", "full pools rejected");
    check(t, g_solve_error == 0, "g_solve_error == 0 after pass", "error code set on pass");

    /* 把边界精确钉在 114688 两侧（各差 1 B 级别的余量）。
       触发侧：extra = 114688 - 47600 + 1000 = 68088 -> 115688 > 114688
       放行侧：extra = 114688 - 47600 - 1000 = 66088 -> 113688 < 114688 */
    boundary = (double)AC_PRO_GATE_BSS - used;
    over = boundary + 1000.0;
    under = boundary - 1000.0;
    check(t, ac_pro_check_memory_watermark_ex(over) == 1, "inject (boundary+1000) -> rejected", "gate did not fire");
    check(t, g_solve_error == 4, "inject (boundary+1000) -> g_solve_error == 4", "wrong error code");
    check(t, ac_pro_check_memory_watermark_ex(under) == 0, "inject (boundary-1000) -> passes", "gate fired too early");
    check(t, g_solve_error == 0, "inject (boundary-1000) -> g_solve_error == 0", "error code leaked");
    /* 边界两侧各差 1 B 的极限测试：114688 恰好不触发，114689 触发 */
    check(t, ac_pro_check_memory_watermark_ex(boundary) == 0, "inject exactly boundary -> passes (<= gate)", "off-by-one at gate");
    check(t, ac_pro_check_memory_watermark_ex(boundary + 1.0) == 1, "inject boundary+1 -> rejected", "off-by-one at gate");

    /* 上游那条死代码在任何输入下都不触发（对照证据）。
       上游实现的上限是 17320 B，阈值 98304 B,这里钉住「上游门禁
       返回 0 且 g_solve_error 保持 0」，作为它是死代码的运行时证据。 */
    g_token_count = TOKEN_POOL_SIZE;
    g_number_count = NUMBER_POOL_SIZE;
    check(t, ac_check_memory_watermark() == 0, "upstream gate never fires (dead code, control)", "upstream gate fired");
    check(t, g_solve_error == 0, "upstream gate leaves g_solve_error == 0", "upstream gate set an error");

    printf("         idle=%.0f full=%.0f gate=%.0f boundary=%.0f (%.0f fires / %.0f passes)\n",
           base, used, (double)AC_PRO_GATE_BSS, boundary, over, under);

    g_token_count = 0;
    g_number_count = 0;
    ac_solvers_reset();
}

/* 阶段二 B1 的前置：次数路由三态 */
static void test_degree_route(tally_t *t) {
    printf("[case] ac_guard_degree three-state routing (1..4 exact / 5..8 numeric / >8 reject)\n");
    g_solve_error = 0;
    check(t, ac_guard_degree(1) == 0 && g_solve_error == 0, "degree 1 -> exact path", "rejected");
    check(t, ac_guard_degree(4) == 0 && g_solve_error == 0, "degree 4 -> exact path", "rejected");
    check(t, ac_guard_degree(5) == 0 && g_solve_error == 0, "degree 5 -> numeric path (was rejected upstream)", "rejected");
    check(t, ac_guard_degree(8) == 0 && g_solve_error == 0, "degree 8 -> numeric path", "rejected");
    check(t, ac_guard_degree(9) == 1 && g_solve_error == 1, "degree 9 -> rejected with error 1", "accepted");
    check(t, ac_guard_degree(0) == 1 && g_solve_error == 1, "degree 0 -> rejected", "accepted");

    check(t, ac_pro_degree_route(1) == 0, "route(1) == exact", "wrong route");
    check(t, ac_pro_degree_route(4) == 0, "route(4) == exact", "wrong route");
    check(t, ac_pro_degree_route(5) == 1, "route(5) == numeric", "wrong route");
    check(t, ac_pro_degree_route(8) == 1, "route(8) == numeric", "wrong route");
    check(t, ac_pro_degree_route(9) == -1, "route(9) == reject", "wrong route");
    check(t, ac_pro_degree_route(0) == -1, "route(0) == reject", "wrong route");
}

/* 阶段二 B1：数值求根 */

/* x^5 - 1 = 0（五个单位根） */
static void test_nr_x5_minus_1(tally_t *t) {
    double c[6];
    printf("[case B1-1] x^5 - 1 = 0  (5 unit roots)\n");
    c[0] = -1.0; c[1] = 0.0; c[2] = 0.0; c[3] = 0.0; c[4] = 0.0; c[5] = 1.0;
    check(t, ac_pro_numroots(c, 5) == 1, "numroots returns 1", "numroots failed");
    check(t, g_solve_error == 0, "g_solve_error == 0", "solver error set");
    check(t, g_root_count == 5, "root count == 5", "root count wrong");
    check(t, num_has_real_root(1.0, 1e-8), "real root x = 1", "root 1 missing");
    check(t, max_poly_residual(c, 5) < 1e-9, "max |P(z)| < 1e-9", "residual too large");
    printf("         max residual = %.3e\n", max_poly_residual(c, 5));
}

/* x^5 - x - 1 = 0（唯一实根 ≈ 1.1673039782614187） */
static void test_nr_x5_minus_x_minus_1(tally_t *t) {
    double c[6];
    printf("[case B1-2] x^5 - x - 1 = 0  (one real root ~1.1673)\n");
    c[0] = -1.0; c[1] = -1.0; c[2] = 0.0; c[3] = 0.0; c[4] = 0.0; c[5] = 1.0;
    check(t, ac_pro_numroots(c, 5) == 1, "numroots returns 1", "numroots failed");
    check(t, g_root_count == 5, "root count == 5", "root count wrong");
    check(t, num_has_real_root(1.1673039782614187, 1e-8), "real root 1.167303978...", "real root missing");
    check(t, max_poly_residual(c, 5) < 1e-9, "max |P(z)| < 1e-9", "residual too large");
    printf("         max residual = %.3e\n", max_poly_residual(c, 5));
}

/* x^5 + 3x^4 - 2x^3 + 7x^2 - 5x + 11（设计说明 §4.1.2 里最难的一例） */
static void test_nr_dense_quintic(tally_t *t) {
    double c[6];
    printf("[case B1-3] x^5+3x^4-2x^3+7x^2-5x+11 = 0  (dense, no real roots)\n");
    c[0] = 11.0; c[1] = -5.0; c[2] = 7.0; c[3] = -2.0; c[4] = 3.0; c[5] = 1.0;
    check(t, ac_pro_numroots(c, 5) == 1, "numroots returns 1", "numroots failed");
    check(t, g_root_count == 5, "root count == 5", "root count wrong");
    check(t, max_poly_residual(c, 5) < 1e-8, "max |P(z)| < 1e-8", "residual too large");
    printf("         max residual = %.3e\n", max_poly_residual(c, 5));
}

/* x^6 + x^3 - 1 = 0（六次） */
static void test_nr_x6_x3_minus_1(tally_t *t) {
    double c[7];
    printf("[case B1-4] x^6 + x^3 - 1 = 0  (degree 6)\n");
    c[0] = -1.0; c[1] = 0.0; c[2] = 0.0; c[3] = 1.0; c[4] = 0.0; c[5] = 0.0; c[6] = 1.0;
    check(t, ac_pro_numroots(c, 6) == 1, "numroots returns 1", "numroots failed");
    check(t, g_root_count == 6, "root count == 6", "root count wrong");
    check(t, max_poly_residual(c, 6) < 1e-8, "max |P(z)| < 1e-8", "residual too large");
    printf("         max residual = %.3e\n", max_poly_residual(c, 6));
}

/* x^8 - 1 = 0（八次上限，八个单位根） */
static void test_nr_x8_minus_1(tally_t *t) {
    double c[9];
    int i;
    printf("[case B1-5] x^8 - 1 = 0  (degree cap, 8 unit roots)\n");
    for (i = 0; i <= 8; i++) { c[i] = 0.0; }
    c[0] = -1.0;
    c[8] = 1.0;
    check(t, ac_pro_numroots(c, 8) == 1, "numroots returns 1", "numroots failed");
    check(t, g_root_count == 8, "root count == 8", "root count wrong");
    check(t, num_has_real_root(1.0, 1e-7), "real root x = 1", "root 1 missing");
    check(t, num_has_real_root(-1.0, 1e-7), "real root x = -1", "root -1 missing");
    check(t, max_poly_residual(c, 8) < 1e-7, "max |P(z)| < 1e-7", "residual too large");
    printf("         max residual = %.3e\n", max_poly_residual(c, 8));
}

/* 重根 (x-1)^3 (x+2)^2（条件数最差的情形，设计说明 §4.1.3 已预警）
 *
 * 展开（升幂，numpy.polymul 核验）：
 *     (x-1)^3 = x^3 - 3x^2 + 3x - 1
 *     (x+2)^2 = x^2 + 4x + 4
 *     乘积     = x^5 + x^4 - 5x^3 - x^2 + 8x - 4
 * 升幂系数 c[0..5] = [-4, 8, -1, -5, 1, 1]。 */
static void test_nr_repeated_roots(tally_t *t) {
    double c[6];
    double resid;
    printf("[case B1-6] (x-1)^3 (x+2)^2 = x^5 + x^4 - 5x^3 - x^2 + 8x - 4\n");
    c[0] = -4.0;
    c[1] = 8.0;
    c[2] = -1.0;
    c[3] = -5.0;
    c[4] = 1.0;
    c[5] = 1.0;
    check(t, ac_pro_numroots(c, 5) == 1, "numroots returns 1 (repeated roots converge)", "numroots failed");
    check(t, g_solve_error == 0, "g_solve_error == 0", "solver error set");
    check(t, g_root_count == 5, "root count == 5", "root count wrong");
    check(t, num_has_real_root(1.0, 1e-4), "triple root x = 1 found", "root 1 missing");
    check(t, num_has_real_root(-2.0, 1e-4), "double root x = -2 found", "root -2 missing");
    resid = max_poly_residual(c, 5);
    /* 重根的条件数自然差，残差阈值放宽到 1e-3 */
    check(t, resid < 1e-3, "max |P(z)| < 1e-3 (repeated roots, loose)", "residual too large");
    printf("         max residual = %.3e (repeated roots, condition-sensitive)\n", resid);
}

/* 负向：次数 > 8 必须被拒绝，且不写根池 */
static void test_nr_reject(tally_t *t) {
    double c[10];
    int i;
    printf("[case B1-7] degree 9 must be rejected (cap is 8)\n");
    for (i = 0; i <= 9; i++) { c[i] = (double)(i + 1); }
    check(t, ac_pro_numroots(c, 9) == 0, "numroots(deg 9) returns 0", "accepted degree 9");
    check(t, g_solve_error == 1, "g_solve_error == 1", "wrong error code");
    check(t, g_root_count == 0, "root count untouched", "root pool polluted");
}

/* 阶段二 A4：LU 分解 */

/* 验证 P·A == L·U。
 * P·A 的第 i 行 = A 的第 piv[i] 行。 */
static double lu_reconstruct_error(const double *a, int n) {
    const double *L = ac_pro_lu_l();
    const double *U = ac_pro_lu_u();
    const int32_t *piv = ac_pro_lu_piv();
    double worst = 0.0;
    int i;
    int j;
    int k;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            double s = 0.0;
            double pa;
            double d;
            for (k = 0; k < n; k++) { s += L[i * n + k] * U[k * n + j]; }
            pa = a[piv[i] * n + j];
            d = fabs(s - pa);
            if (d > worst) { worst = d; }
        }
    }
    return worst;
}

/* L 是否为合法单位下三角（对角 = 1，上三角 = 0） */
static int lu_is_unit_lower(int n) {
    const double *L = ac_pro_lu_l();
    int i;
    int j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            if (i == j) {
                if (fabs(L[i * n + j] - 1.0) > 1e-12) { return 0; }
            } else if (j > i) {
                if (fabs(L[i * n + j]) > 1e-12) { return 0; }
            }
        }
    }
    return 1;
}

/* U 是否为上三角 */
static int lu_is_upper(int n) {
    const double *U = ac_pro_lu_u();
    int i;
    int j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            if (j < i && fabs(U[i * n + j]) > 1e-12) { return 0; }
        }
    }
    return 1;
}

/* 3×3 需要换行的一般矩阵 */
static void test_lu_3x3(tally_t *t) {
    double a[9];
    printf("[case A4-1] LU 3x3 (pivoting required)\n");
    a[0] = 0.0; a[1] = 2.0; a[2] = 1.0;
    a[3] = 1.0; a[4] = 1.0; a[5] = 1.0;
    a[6] = 2.0; a[7] = 1.0; a[8] = 3.0;
    check(t, ac_mat_lu(a, 3) == 1, "ac_mat_lu returns 1", "lu failed");
    check(t, lu_is_unit_lower(3), "L is unit lower triangular", "L shape wrong");
    check(t, lu_is_upper(3), "U is upper triangular", "U shape wrong");
    check(t, lu_reconstruct_error(a, 3) < 1e-12, "P*A == L*U", "reconstruction mismatch");
    printf("         ||P*A - L*U||max = %.3e, piv = [%d %d %d]\n",
           lu_reconstruct_error(a, 3),
           (int)ac_pro_lu_piv()[0], (int)ac_pro_lu_piv()[1], (int)ac_pro_lu_piv()[2]);
}

/* 4×4 Hilbert 式病态矩阵（4 阶仍稳定） */
static void test_lu_4x4(tally_t *t) {
    double a[16];
    int i;
    int j;
    printf("[case A4-2] LU 4x4 Hilbert-like (ill-conditioned)\n");
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            a[i * 4 + j] = 1.0 / (double)(i + j + 1);
        }
    }
    check(t, ac_mat_lu(a, 4) == 1, "ac_mat_lu returns 1", "lu failed");
    check(t, lu_is_unit_lower(4), "L is unit lower triangular", "L shape wrong");
    check(t, lu_is_upper(4), "U is upper triangular", "U shape wrong");
    check(t, lu_reconstruct_error(a, 4) < 1e-14, "P*A == L*U", "reconstruction mismatch");
    printf("         ||P*A - L*U||max = %.3e\n", lu_reconstruct_error(a, 4));
}

/* 对角矩阵（置换应为恒等，U == A，L == I） */
static void test_lu_diagonal(tally_t *t) {
    double a[9];
    const double *U;
    int i;
    int identity = 1;
    printf("[case A4-3] LU diagonal matrix (no pivoting)\n");
    a[0] = 2.0; a[1] = 0.0; a[2] = 0.0;
    a[3] = 0.0; a[4] = -3.0; a[5] = 0.0;
    a[6] = 0.0; a[7] = 0.0; a[8] = 5.0;
    check(t, ac_mat_lu(a, 3) == 1, "ac_mat_lu returns 1", "lu failed");
    for (i = 0; i < 3; i++) {
        if (ac_pro_lu_piv()[i] != i) { identity = 0; }
    }
    check(t, identity, "permutation is identity", "spurious pivoting");
    U = ac_pro_lu_u();
    check(t, fabs(U[0] - 2.0) < 1e-12 && fabs(U[4] + 3.0) < 1e-12 && fabs(U[8] - 5.0) < 1e-12,
          "U == A (diagonal preserved)", "U diagonal wrong");
    check(t, lu_reconstruct_error(a, 3) < 1e-12, "P*A == L*U", "reconstruction mismatch");
}

/* 12×12 上限（对角占优，稳定） */
static void test_lu_12x12(tally_t *t) {
    double a[144];
    int i;
    int j;
    printf("[case A4-4] LU 12x12 (dimension cap)\n");
    for (i = 0; i < 12; i++) {
        for (j = 0; j < 12; j++) {
            a[i * 12 + j] = (i == j) ? 20.0 : (double)((i + 1) * (j + 1) % 7) - 3.0;
        }
    }
    check(t, ac_mat_lu(a, 12) == 1, "ac_mat_lu returns 1", "lu failed");
    check(t, lu_is_unit_lower(12), "L is unit lower triangular", "L shape wrong");
    check(t, lu_is_upper(12), "U is upper triangular", "U shape wrong");
    check(t, lu_reconstruct_error(a, 12) < 1e-10, "P*A == L*U", "reconstruction mismatch");
    printf("         ||P*A - L*U||max = %.3e\n", lu_reconstruct_error(a, 12));
}

/* 奇异矩阵必须返回 0 */
static void test_lu_singular(tally_t *t) {
    double a[9];
    printf("[case A4-5] LU singular matrix (rank 2 of 3)\n");
    a[0] = 1.0; a[1] = 2.0; a[2] = 3.0;
    a[3] = 4.0; a[4] = 5.0; a[5] = 6.0;
    a[6] = 7.0; a[7] = 8.0; a[8] = 9.0;   /* 第三行 = 2*第二行 - 第一行 */
    check(t, ac_mat_lu(a, 3) == 0, "singular -> returns 0", "singular accepted");
}

/* 与上游 ac_mat_det 交叉验证。
 * det(A) = (-1)^swaps · Π U[i][i]，swaps 由置换的逆序数决定。 */
static void test_lu_matches_det(tally_t *t) {
    double a[9];
    double work[9];
    double det_up;
    double det_lu = 1.0;
    int swaps = 0;
    int i;
    int j;
    printf("[case A4-6] LU pivots vs upstream ac_mat_det (cross-check)\n");
    a[0] = 3.0; a[1] = -1.0; a[2] = 2.0;
    a[3] = 1.0; a[4] = 5.0; a[5] = 4.0;
    a[6] = -2.0; a[7] = 6.0; a[8] = 1.0;
    det_up = ac_mat_det(a, 3, work);
    check(t, ac_mat_lu(a, 3) == 1, "ac_mat_lu returns 1", "lu failed");
    for (i = 0; i < 3; i++) { det_lu *= ac_pro_lu_u()[i * 3 + i]; }
    for (i = 0; i < 3; i++) {
        for (j = i + 1; j < 3; j++) {
            if (ac_pro_lu_piv()[i] > ac_pro_lu_piv()[j]) { swaps++; }
        }
    }
    if (swaps % 2 != 0) { det_lu = -det_lu; }
    check(t, fabs(det_lu - det_up) < 1e-12, "det from LU == det from ac_mat_det", "determinants disagree");
    printf("         det(upstream) = %.17g   det(LU) = %.17g   swaps = %d\n",
           det_up, det_lu, swaps);
}

/* 阶段二 A6：隐函数求导 */

static void test_implicit_circle(tally_t *t) {
    double dy = 0.0;
    printf("[case A6-1] x^2 + y^2 - 25 = 0  at (3,4) -> dy/dx = -3/4\n");
    check(t, ac_pro_implicit_derivative("x^2+y^2-25", 3.0, 4.0, &dy) == AC_PRO_IMP_OK,
          "returns OK", "implicit derivative failed");
    check(t, fabs(dy + 0.75) < 1e-12, "dy/dx == -3/4", "wrong value");
    printf("         dy/dx = %.17g\n", dy);
}

static void test_implicit_circle_neg(tally_t *t) {
    double dy = 0.0;
    printf("[case A6-2] x^2 + y^2 - 25 = 0  at (-3,4) -> dy/dx = +3/4\n");
    check(t, ac_pro_implicit_derivative("x^2+y^2-25", -3.0, 4.0, &dy) == AC_PRO_IMP_OK,
          "returns OK", "implicit derivative failed");
    check(t, fabs(dy - 0.75) < 1e-12, "dy/dx == +3/4", "wrong value");
    printf("         dy/dx = %.17g\n", dy);
}

static void test_implicit_product(tally_t *t) {
    double dy = 0.0;
    printf("[case A6-3] x*y - 6 = 0  at (2,3) -> dy/dx = -y/x = -1.5\n");
    check(t, ac_pro_implicit_derivative("x*y-6", 2.0, 3.0, &dy) == AC_PRO_IMP_OK,
          "returns OK", "implicit derivative failed");
    check(t, fabs(dy + 1.5) < 1e-12, "dy/dx == -1.5", "wrong value");
    printf("         dy/dx = %.17g\n", dy);
}

static void test_implicit_cubic(tally_t *t) {
    double dy = 0.0;
    printf("[case A6-4] x^3 + y^3 - 9 = 0  at (1,2) -> dy/dx = -3/12 = -0.25\n");
    check(t, ac_pro_implicit_derivative("x^3+y^3-9", 1.0, 2.0, &dy) == AC_PRO_IMP_OK,
          "returns OK", "implicit derivative failed");
    check(t, fabs(dy + 0.25) < 1e-12, "dy/dx == -0.25", "wrong value");
    printf("         dy/dx = %.17g\n", dy);
}

/* 与显式解对拍：x^2 + y^2 = 25 在 (3,4) 处 y = sqrt(25-x^2) 的解析导数
 * dy/dx = -x/y，用中心差分数值验证（不依赖符号层）。 */
static void test_implicit_vs_numeric(tally_t *t) {
    double dy = 0.0;
    double h = 1e-6;
    double yp;
    double ym;
    double num;
    printf("[case A6-5] x^2+y^2-25 at (3,4): symbolic vs central difference\n");
    check(t, ac_pro_implicit_derivative("x^2+y^2-25", 3.0, 4.0, &dy) == AC_PRO_IMP_OK,
          "symbolic OK", "implicit derivative failed");
    yp = sqrt(25.0 - (3.0 + h) * (3.0 + h));
    ym = sqrt(25.0 - (3.0 - h) * (3.0 - h));
    num = (yp - ym) / (2.0 * h);
    check(t, fabs(dy - num) < 1e-6, "symbolic == numeric (1e-6)", "symbolic/numeric disagree");
    printf("         symbolic = %.17g   numeric = %.17g   |diff| = %.3e\n",
           dy, num, fabs(dy - num));
}

/* 负向：F_y = 0（圆在 (5,0) 处切线竖直）必须报 EFLAT */
static void test_implicit_vertical(tally_t *t) {
    double dy = 12345.0;
    int rc;
    printf("[case A6-6] x^2+y^2-25 at (5,0): F_y = 0 -> must reject (vertical tangent)\n");
    rc = ac_pro_implicit_derivative("x^2+y^2-25", 5.0, 0.0, &dy);
    check(t, rc == AC_PRO_IMP_EFLAT, "returns EFLAT (3)", "wrong return code");
    check(t, dy == 12345.0, "output untouched on failure", "output was clobbered");
}

/* 负向：表达式不含 y -> 隐函数定理不适用 */
static void test_implicit_no_y(tally_t *t) {
    double dy = 0.0;
    int rc;
    printf("[case A6-7] x^2 - 4 (no y): must reject\n");
    rc = ac_pro_implicit_derivative("x^2-4", 2.0, 1.0, &dy);
    check(t, rc == AC_PRO_IMP_EPARSE, "returns EPARSE (1)", "wrong return code");
}

/* 负向：函数调用超出 v1.2 符号层边界 */
static void test_implicit_unsupported(tally_t *t) {
    double dy = 0.0;
    int rc;
    printf("[case A6-8] sin(x)+y (function calls unsupported): must reject\n");
    rc = ac_pro_implicit_derivative("sin(x)+y", 0.0, 1.0, &dy);
    check(t, rc == AC_PRO_IMP_EPARSE, "returns EPARSE (1)", "wrong return code");
}

/* 填充洞自证：建树后，VAR 节点的 var 标签确实落在偏移 12..15，
 * 且 sizeof 仍是 24、arena 仍是 12288 B。 */
static void test_var_tag_hole(tally_t *t) {
    int root;
    int i;
    int saw_x = 0;
    int saw_y = 0;
    printf("[case A6-9] var tag lives in the 12..15 padding hole (0 B .bss)\n");
    ac_sym_reset();
    root = ac_pro_sym_parse("x*y+1");
    check(t, root >= 0, "parse x*y+1", "parse failed");
    for (i = 0; i < g_sym_count; i++) {
        const unsigned char *base;
        int32_t v;
        if (g_sym_nodes[i].type != NODE_VAR) { continue; }
        base = (const unsigned char *)(const void *)&g_sym_nodes[i];
        v = (int32_t)base[12] | ((int32_t)base[13] << 8) |
            ((int32_t)base[14] << 16) | ((int32_t)base[15] << 24);
        if (v == 0) { saw_x = 1; }
        if (v == 1) { saw_y = 1; }
    }
    check(t, saw_x, "x tagged as var 0 in the hole", "x tag missing");
    check(t, saw_y, "y tagged as var 1 in the hole", "y tag missing");
    check(t, sizeof(ac_sym_node_t) == 24, "node size still 24 (no growth)", "node grew");
    check(t, AC_SYM_MAX_NODES * 24 == 12288, "arena still 12288 B", "arena grew");
}

/* 端到端：表达式 -> 5 次数值求根 */
static void test_end_to_end_quintic(tally_t *t) {
    int i;
    double c[6];
    printf("[case E2E] parse x^5-1=0 -> extract -> numeric roots\n");
    ac_solvers_reset();
    check(t, ac_parse_expression("x^5-1=0") == ERR_NONE, "parse x^5-1=0", "parse failed");
    check(t, ac_guard_degree(5) == 0, "guard allows degree 5", "guard rejected");
    check(t, ac_pro_solve_numeric_from_expr(5) == 1, "extract + numroots", "numeric path failed");
    check(t, g_root_count == 5, "root count == 5", "root count wrong");
    for (i = 0; i < 6; i++) { c[i] = 0.0; }
    c[0] = -1.0;
    c[5] = 1.0;
    check(t, max_poly_residual(c, 5) < 1e-8, "residual of the true polynomial < 1e-8", "residual too large");
    printf("         roots:");
    for (i = 0; i < g_root_count; i++) {
        printf(" %.6f%+.6fi", g_root_re[i], g_root_im[i]);
    }
    printf("\n");
}


/* v1.3 方向 A 测试（独立 TU）
 * 用例实现在 test/test_pro_multi.c，本文件只声明入口并汇总。
 * 接口用「返回计数结构」而不是「传指针进去累加」：那个文件定义了自己的
 * pro_tally_t（不与本文件的 tally_t 共享类型），因此两个 TU 之间没有类型
 * 依赖，它可以单独编译、单独跑。代价是这里多写四行汇总代码，换来的是
 * 「测试文件可以独立验证」。
 * 三个函数名不带 static，因为它们是跨 TU 的入口。C99 里
 * `void test_x(void);` 的声明隐含 external linkage，与定义端一致。 */
typedef struct {
    int checks;
    int failed;
} pro_multi_tally_t;

pro_multi_tally_t test_multi_gcd(void);
pro_multi_tally_t test_multi_factor(void);
pro_multi_tally_t test_resultant(void);

/* v1.3.5 测试（独立 TU）
 * A5 实特征向量 + Hotelling 收缩：test/test_pro_eigen.c
 * B2 符号积分（表匹配 + 一层分部）：test/test_pro_integrate.c
 * 同样用「返回计数结构」的跨 TU 约定，本文件不感知其内部类型。 */
pro_multi_tally_t test_eigen_2x2(void);
pro_multi_tally_t test_eigen_3x3(void);
pro_multi_tally_t test_eigen_repeated(void);
pro_multi_tally_t test_eigen_illconditioned(void);
pro_multi_tally_t test_eigen_deflation(void);

pro_multi_tally_t test_integrate_poly(void);
pro_multi_tally_t test_integrate_trig(void);
pro_multi_tally_t test_integrate_by_parts(void);
pro_multi_tally_t test_integrate_table(void);
pro_multi_tally_t test_integrate_budget(void);
pro_multi_tally_t test_integrate_errors(void);
pro_multi_tally_t test_integrate_quad_kernels(void);
pro_multi_tally_t test_integrate_recip_funcs(void);
pro_multi_tally_t test_integrate_func_powers(void);
pro_multi_tally_t test_integrate_two_layer(void);

/* v2.0.0：多变量符号求导（test/test_pro_diff_multi.c），同一跨 TU 约定。 */
pro_multi_tally_t test_diff_multi(void);
/* 多变量多项式积分：test/test_pro_integrate_multi.c（同「返回计数结构」约定） */
pro_multi_tally_t test_integrate_multi(void);

/* v2.1.0 方向④：稀疏线性方程组（test/test_pro_sparse.c），同一跨 TU 约定。
 * 6 个入口覆盖 12 个用例：主干（压缩/合并/交换）、稠密退化 CSR 对照、
 * n=32 带状系统、n/nnz/index 熔断、填充溢出/奇异/主元下限、阈值主元。 */
pro_multi_tally_t test_sparse_basic(void);
pro_multi_tally_t test_sparse_dense_control(void);
pro_multi_tally_t test_sparse_banded32(void);
pro_multi_tally_t test_sparse_limits(void);
pro_multi_tally_t test_sparse_robustness(void);
pro_multi_tally_t test_sparse_threshold(void);

/* v2.1.0 方向⑤：任意精度定点十进制真实实现（test/test_pro_bigdec.c），
 * 同一跨 TU 约定。双方的骨架探针 test_sparse_scaffold / test_bigdec_scaffold
 * 已分别被真实 API 的 test_sparse_*（12 用例）与 test_bigdec（12 项测试）
 * 整体取代。 */
pro_multi_tally_t test_bigdec(void);

/* v2.2.0 方向⑥：2 变量 Groebner 基（M4GB 逐对归约，test/test_pro_groebner.c），
 * 同一跨 TU 约定。4 个入口覆盖 12 个用例：经典主干（含任务书钦定的
 * <x²+y²−1, x²−y>）、退化与消融、访问器结构（reduced-GB 性质）、
 * 熔断与幂等。期望基全部经 node 有理数独立核算（见测试文件头）。 */
pro_multi_tally_t test_gb_textbook(void);
pro_multi_tally_t test_gb_degenerate(void);
pro_multi_tally_t test_gb_accessors(void);
pro_multi_tally_t test_gb_fuses(void);

/* v2.3.0 功能 5：单变量有理函数工具（test/test_pro_rational.c，同跨 TU 约定） */
pro_multi_tally_t test_rat_arith(void);
pro_multi_tally_t test_rat_gcd(void);
pro_multi_tally_t test_rat_cancel(void);
pro_multi_tally_t test_rat_apart_exact(void);
pro_multi_tally_t test_rat_apart_dk(void);
pro_multi_tally_t test_rat_limits(void);
pro_multi_tally_t test_rat_buffer(void);

/* v2.3.0 功能 3 C 侧：常系数线性齐次 ODE（test/test_pro_ode.c，同跨 TU 约定） */
pro_multi_tally_t test_ode_basic(void);
pro_multi_tally_t test_ode_zero_repeat(void);
pro_multi_tally_t test_ode_quartic(void);
pro_multi_tally_t test_ode_rational(void);
pro_multi_tally_t test_ode_reject(void);
pro_multi_tally_t test_ode_order(void);
pro_multi_tally_t test_ode_bounds(void);
/* v2.4.0 功能 8:一般实矩阵特征值(test/test_pro_schur.c,13 用例) */
typedef struct { int checks; int failed; } sch_tally_t;
sch_tally_t test_schur_basic(void);
sch_tally_t test_schur_complex(void);
sch_tally_t test_schur_degenerate(void);
sch_tally_t test_schur_large(void);
sch_tally_t test_schur_reject(void);
/* v2.4.0 功能 9 C 侧:square-free 分解(test/test_pro_sqfree.c,10 用例) */
typedef struct { int checks; int failed; } sq_tally_t;
void test_sqfree_basic(sq_tally_t *t);
void test_sqfree_complex(sq_tally_t *t);
void test_sqfree_route(sq_tally_t *t);
void test_sqfree_const_dk(sq_tally_t *t);
void test_sqfree_x8(sq_tally_t *t);
void test_sqfree_fmt(sq_tally_t *t);
void test_sqfree_x5(sq_tally_t *t);
void test_sqfree_reject(sq_tally_t *t);
void test_sqfree_order(sq_tally_t *t);
void test_sqfree_bounds(sq_tally_t *t);

/* ==== v2.5 ANCHOR: CX (feat/math-ext) ==== */

/* ==== END ANCHOR CX ==== */

/* ==== v2.5 ANCHOR: SYSOLVE (feat/solvers) ==== */

/* ==== END ANCHOR SYSOLVE ==== */

/* ==== v2.5 ANCHOR: STURM (feat/solvers) ==== */

/* ==== END ANCHOR STURM ==== */

int main(void) {
    tally_t t;

    t.checks = 0;
    t.failed = 0;

    printf("=== ApolloCalc-CAS ProTEXT runner (AC_PRO=%d, layer=%s) ===\n",
           AC_PRO, AC_PRO_LAYER_NAME);
    printf("    .bss ledger: %d (upstream kernel) + %d (var_val) + %d (Pro union) = %d B\n",
           AC_STATIC_FOOTPRINT, AC_PRO_VAR_VAL_BYTES, AC_PRO_SCRATCH_BYTES,
           AC_PRO_TARGET_BSS);
    printf("    gate: %d B (112 KiB)\n", AC_PRO_GATE_BSS);

    s_kernel_live = 1;          /* Zero-GC 硬性校验开启 */

    /* 上游回归 */
    test_quadratic_real(&t);
    test_quadratic_complex(&t);
    test_pro_smoke(&t);

    /* 阶段一 */
    test_sizes(&t);
    test_memory_watermark(&t);
    test_degree_route(&t);

    /* 阶段二 B1 */
    test_nr_x5_minus_1(&t);
    test_nr_x5_minus_x_minus_1(&t);
    test_nr_dense_quintic(&t);
    test_nr_x6_x3_minus_1(&t);
    test_nr_x8_minus_1(&t);
    test_nr_repeated_roots(&t);
    test_nr_reject(&t);

    /* 阶段二 A4 */
    test_lu_3x3(&t);
    test_lu_4x4(&t);
    test_lu_diagonal(&t);
    test_lu_12x12(&t);
    test_lu_singular(&t);
    test_lu_matches_det(&t);

    /* 阶段二 A6 */
    test_implicit_circle(&t);
    test_implicit_circle_neg(&t);
    test_implicit_product(&t);
    test_implicit_cubic(&t);
    test_implicit_vs_numeric(&t);
    test_implicit_vertical(&t);
    test_implicit_no_y(&t);
    test_implicit_unsupported(&t);
    test_var_tag_hole(&t);

    /* 端到端 */
    test_end_to_end_quintic(&t);

    /* v1.3 方向 A：多变量代数结构（实现在 test/test_pro_multi.c） */
    {
        pro_multi_tally_t a1 = test_multi_gcd();
        pro_multi_tally_t a2 = test_multi_factor();
        pro_multi_tally_t a3 = test_resultant();
        t.checks += a1.checks + a2.checks + a3.checks;
        t.failed += a1.failed + a2.failed + a3.failed;
        printf("--- v1.3 direction A: A1 %d/%d, A2 %d/%d, A3 %d/%d (checks/failed) ---\n",
               a1.checks, a1.failed, a2.checks, a2.failed, a3.checks, a3.failed);
    }

    /* v1.3.5 A5：实特征向量 + Hotelling 收缩（test/test_pro_eigen.c） */
    {
        pro_multi_tally_t e1 = test_eigen_2x2();
        pro_multi_tally_t e2 = test_eigen_3x3();
        pro_multi_tally_t e3 = test_eigen_repeated();
        pro_multi_tally_t e4 = test_eigen_illconditioned();
        pro_multi_tally_t e5 = test_eigen_deflation();
        t.checks += e1.checks + e2.checks + e3.checks + e4.checks + e5.checks;
        t.failed += e1.failed + e2.failed + e3.failed + e4.failed + e5.failed;
        printf("--- v1.3.5 A5 eigen: %d/%d, %d/%d, %d/%d, %d/%d, %d/%d (checks/failed) ---\n",
               e1.checks, e1.failed, e2.checks, e2.failed, e3.checks, e3.failed,
               e4.checks, e4.failed, e5.checks, e5.failed);
    }

    /* v1.3.5 B2：符号积分（test/test_pro_integrate.c） */
    {
        pro_multi_tally_t i1 = test_integrate_poly();
        pro_multi_tally_t i2 = test_integrate_trig();
        pro_multi_tally_t i3 = test_integrate_by_parts();
        pro_multi_tally_t i4 = test_integrate_table();
        pro_multi_tally_t i5 = test_integrate_budget();
        pro_multi_tally_t i6 = test_integrate_errors();
        pro_multi_tally_t i7 = test_integrate_quad_kernels();
        pro_multi_tally_t i8 = test_integrate_recip_funcs();
        pro_multi_tally_t i9 = test_integrate_func_powers();
        pro_multi_tally_t i10 = test_integrate_two_layer();
        t.checks += i1.checks + i2.checks + i3.checks + i4.checks + i5.checks + i6.checks
                  + i7.checks + i8.checks + i9.checks + i10.checks;
        t.failed += i1.failed + i2.failed + i3.failed + i4.failed + i5.failed + i6.failed
                  + i7.failed + i8.failed + i9.failed + i10.failed;
        printf("--- v1.3.5 B2 integrate: %d/%d, %d/%d, %d/%d, %d/%d, %d/%d, %d/%d, %d/%d, %d/%d, %d/%d, %d/%d (checks/failed) ---\n",
               i1.checks, i1.failed, i2.checks, i2.failed, i3.checks, i3.failed,
               i4.checks, i4.failed, i5.checks, i5.failed, i6.checks, i6.failed,
               i7.checks, i7.failed, i8.checks, i8.failed, i9.checks, i9.failed,
               i10.checks, i10.failed);
    }

    /* v2.0.0：多变量符号求导（test/test_pro_diff_multi.c） */
    {
        pro_multi_tally_t d1 = test_diff_multi();
        t.checks += d1.checks;
        t.failed += d1.failed;
        printf("--- v2.0.0 multi-diff: %d/%d (checks/failed) ---\n", d1.checks, d1.failed);
    }

    /* 多变量多项式积分（test/test_pro_integrate_multi.c） */
    {
        pro_multi_tally_t m1 = test_integrate_multi();
        t.checks += m1.checks;
        t.failed += m1.failed;
        printf("--- multi-var integrate: %d/%d (checks/failed) ---\n", m1.checks, m1.failed);
    }

    /* v2.1.0 方向④：稀疏线性方程组（test/test_pro_sparse.c，12 用例 35 项测试） */
    {
        pro_multi_tally_t sp1 = test_sparse_basic();
        pro_multi_tally_t sp2 = test_sparse_dense_control();
        pro_multi_tally_t sp3 = test_sparse_banded32();
        pro_multi_tally_t sp4 = test_sparse_limits();
        pro_multi_tally_t sp5 = test_sparse_robustness();
        pro_multi_tally_t sp6 = test_sparse_threshold();
        t.checks += sp1.checks + sp2.checks + sp3.checks + sp4.checks + sp5.checks + sp6.checks;
        t.failed += sp1.failed + sp2.failed + sp3.failed + sp4.failed + sp5.failed + sp6.failed;
        printf("--- v2.1.0 sparse linsolve: basic %d/%d, dense %d/%d, banded %d/%d, limits %d/%d, robust %d/%d, threshold %d/%d (checks/failed) ---\n",
               sp1.checks, sp1.failed, sp2.checks, sp2.failed, sp3.checks, sp3.failed,
               sp4.checks, sp4.failed, sp5.checks, sp5.failed, sp6.checks, sp6.failed);
    }

    /* v2.1.0 方向⑤：任意精度定点十进制（test/test_pro_bigdec.c，12 项测试） */
    {
        pro_multi_tally_t bd1 = test_bigdec();
        t.checks += bd1.checks;
        t.failed += bd1.failed;
        printf("--- v2.1.0 bigdec: %d/%d (checks/failed) ---\n", bd1.checks, bd1.failed);
    }

    /* v2.2.0 方向⑥：2 变量 Groebner 基（test/test_pro_groebner.c，12 用例） */
    {
        pro_multi_tally_t g1 = test_gb_textbook();
        pro_multi_tally_t g2 = test_gb_degenerate();
        pro_multi_tally_t g3 = test_gb_accessors();
        pro_multi_tally_t g4 = test_gb_fuses();
        t.checks += g1.checks + g2.checks + g3.checks + g4.checks;
        t.failed += g1.failed + g2.failed + g3.failed + g4.failed;
        printf("--- v2.2.0 groebner: textbook %d/%d, degenerate %d/%d, accessors %d/%d, fuses %d/%d (checks/failed) ---\n",
               g1.checks, g1.failed, g2.checks, g2.failed,
               g3.checks, g3.failed, g4.checks, g4.failed);
    }

    /* v2.3.0 功能 5：单变量有理函数工具（test/test_pro_rational.c，7 用例） */
    {
        pro_multi_tally_t r1 = test_rat_arith();
        pro_multi_tally_t r2 = test_rat_gcd();
        pro_multi_tally_t r3 = test_rat_cancel();
        pro_multi_tally_t r4 = test_rat_apart_exact();
        pro_multi_tally_t r5 = test_rat_apart_dk();
        pro_multi_tally_t r6 = test_rat_limits();
        pro_multi_tally_t r7 = test_rat_buffer();
        t.checks += r1.checks + r2.checks + r3.checks + r4.checks
                  + r5.checks + r6.checks + r7.checks;
        t.failed += r1.failed + r2.failed + r3.failed + r4.failed
                  + r5.failed + r6.failed + r7.failed;
        printf("--- v2.3.0 rational: arith %d/%d, gcd %d/%d, cancel %d/%d, exact %d/%d, dk %d/%d, limits %d/%d, buffer %d/%d (checks/failed) ---\n",
               r1.checks, r1.failed, r2.checks, r2.failed,
               r3.checks, r3.failed, r4.checks, r4.failed,
               r5.checks, r5.failed, r6.checks, r6.failed,
               r7.checks, r7.failed);
    }

    /* v2.3.0 功能 3 C 侧：常系数线性齐次 ODE（test/test_pro_ode.c，7 用例） */
    {
        pro_multi_tally_t o1 = test_ode_basic();
        pro_multi_tally_t o2 = test_ode_zero_repeat();
        pro_multi_tally_t o3 = test_ode_quartic();
        pro_multi_tally_t o4 = test_ode_rational();
        pro_multi_tally_t o5 = test_ode_reject();
        pro_multi_tally_t o6 = test_ode_order();
        pro_multi_tally_t o7 = test_ode_bounds();
        t.checks += o1.checks + o2.checks + o3.checks + o4.checks
                  + o5.checks + o6.checks + o7.checks;
        t.failed += o1.failed + o2.failed + o3.failed + o4.failed
                  + o5.failed + o6.failed + o7.failed;
        printf("--- v2.3.0 ode: basic %d/%d, zero %d/%d, quartic %d/%d, rational %d/%d, reject %d/%d, order %d/%d, bounds %d/%d (checks/failed) ---\n",
               o1.checks, o1.failed, o2.checks, o2.failed,
               o3.checks, o3.failed, o4.checks, o4.failed,
               o5.checks, o5.failed, o6.checks, o6.failed,
               o7.checks, o7.failed);
    }

    /* v2.4.0 功能 8:一般实矩阵特征值(test/test_pro_schur.c,13 用例) */
    {
        sch_tally_t s1 = test_schur_basic();
        sch_tally_t s2 = test_schur_complex();
        sch_tally_t s3 = test_schur_degenerate();
        sch_tally_t s4 = test_schur_large();
        sch_tally_t s5 = test_schur_reject();
        t.checks += s1.checks + s2.checks + s3.checks + s4.checks + s5.checks;
        t.failed += s1.failed + s2.failed + s3.failed + s4.failed + s5.failed;
        printf("--- v2.4.0 schur: basic %d/%d, complex %d/%d, degen %d/%d, large %d/%d, reject %d/%d (checks/failed) ---\n",
               s1.checks, s1.failed, s2.checks, s2.failed,
               s3.checks, s3.failed, s4.checks, s4.failed,
               s5.checks, s5.failed);
    }

    /* v2.4.0 功能 9 C 侧:square-free 分解(test/test_pro_sqfree.c,10 用例) */
    {
        sq_tally_t q;
        q.checks = 0;
        q.failed = 0;
        test_sqfree_basic(&q);
        test_sqfree_complex(&q);
        test_sqfree_route(&q);
        test_sqfree_const_dk(&q);
        test_sqfree_x8(&q);
        test_sqfree_fmt(&q);
        test_sqfree_x5(&q);
        test_sqfree_reject(&q);
        test_sqfree_order(&q);
        test_sqfree_bounds(&q);
        t.checks += q.checks;
        t.failed += q.failed;
        printf("--- v2.4.0 sqfree: %d/%d (checks/failed) ---\n", q.checks, q.failed);
    }

    s_kernel_live = 0;          /* 允许 CRT 收尾释放 */

    printf("--- summary: %d checks, %d failed ---\n", t.checks, t.failed);
    if (t.failed == 0) {
        printf("RESULT: PASS\n");
        return 0;
    }
    printf("RESULT: FAIL\n");
    return 1;
}
