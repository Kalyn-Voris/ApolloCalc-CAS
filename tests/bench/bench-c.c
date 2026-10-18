/* ============================================================
 * ApolloCalc-CAS · C 端 100 万次压测脚本
 *
 * 编译：mingw32-make -f bench-c-makefile KERNEL=../../C/<预算>/src
 * 运行：./bench-c.exe > result-c<预算>-1M.csv
 *
 * 输出：CSV，列名固定 stage,name,iterations,total_ms,us_per_op（与 JS 端逐字一致）
 *       尾部 SUMMARY 行 = 全 stage 汇总；STATIC 行 = 表达式池静态占用
 *
 * 池：  pool_c.h（由 tests/bench/gen_pool_c.py 从 pool.json 生成，编译期写死）
 *       C 端不解析 JSON：运行时零解析开销。is_128kb=1 的条目仅 128KB 接受。
 *
 * 版本：64KB / 128KB 共用本文件，编译期用 __has_include("ac_ratroot.h") 区分
 *       （该头文件是 C 128KB 独有；64KB 无 ac_ratroot.c / ac_calculus.c）。
 *
 * 迭代：每 stage 独立 100 万次，按池大小摊到每条的循环遍数：
 *       loops = ceil(totalIter / poolSize)，actualIter = loops * poolSize
 *       实际迭代数略高于 100 万（整遍对齐），CSV 的 iterations 列是真实值。
 * ============================================================ */

#define _POSIX_C_SOURCE 199309L   /* clock_gettime / CLOCK_MONOTONIC */

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#include "ac_parser.h"
#include "ac_evaluator.h"
#include "ac_solvers.h"
#include "ac_exact.h"
#include "ac_math.h"

/* ---- 版本判定：ac_ratroot.h 为 C 128KB 独有 ---- */
#if defined(__has_include)
#  if __has_include("ac_ratroot.h")
#    define AC_IS_128KB 1
#  else
#    define AC_IS_128KB 0
#  endif
#else
#  define AC_IS_128KB 0
#endif

#include "pool_c.h"

/* ---- 计时工具（POSIX 单调时钟，不受系统时间跳变影响） ---- */
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* ---- 3 轮取中位数（与 JS 端同策略，抗抖动） ---- */
#define BENCH_RUNS 3
#define WARMUP     1000

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static double median_of(double *arr, int n) {
    qsort(arr, (size_t)n, sizeof(double), cmp_double);
    return arr[n / 2];
}

/* ---- 汇总累加器 ---- */
static long long g_sum_ops = 0;
static double    g_sum_ms  = 0.0;

/* ==================== 池过滤（版本门控） ==================== */
static const pool_entry_t *g_parse[POOL_MAX_N];
static const pool_entry_t *g_eval[POOL_MAX_N];
static const pool_entry_t *g_solve[POOL_MAX_N];
static const pool_entry_t *g_exact[POOL_MAX_N];
static int g_parse_n = 0, g_eval_n = 0, g_solve_n = 0, g_exact_n = 0;

static int accept_entry(const pool_entry_t *e) {
#if AC_IS_128KB
    (void)e;
    return 1;                 /* 128KB 接受全部条目 */
#else
    return !e->is_128kb;      /* 64KB 拒绝 128KB 专属条目 */
#endif
}

static int filter_into(const pool_entry_t *src, int n,
                       const pool_entry_t **dst) {
    int i, m = 0;
    for (i = 0; i < n; i++) {
        if (accept_entry(&src[i])) { dst[m++] = &src[i]; }
    }
    return m;
}

/* ==================== 各 stage 的单次操作 ==================== */
static void parse_once(const pool_entry_t *e) {
    (void)ac_parse_expression(e->src);
}

/* eval 段：池内每条表达式不同 → 环内必须重设角度制 + 解析 + 求值。
 * 因此测得的是「解析 + 求值」合计成本（与旧版只在环内测 eval 不同）。 */
static void eval_once(const pool_entry_t *e) {
    g_angle_deg = e->angle_deg;
    (void)ac_parse_expression(e->src);
    (void)ac_eval_expression_at(e->x);
}

/* solve / exact 段：规范链路（与 test_solvers / test_smoke_csv 一致）：
 *   ac_solvers_reset → ac_parse_expression → ac_extract_coefficients → ac_solve_xxx
 * exact 段只测耗时（不校验精确形式），池偏向精确形式路径。 */
static void solve_once(const pool_entry_t *e) {
    ac_solvers_reset();
    (void)ac_parse_expression(e->src);
    if (ac_extract_coefficients(e->degree) != 0) { return; }
    if (e->degree == 2) {
        ac_solve_quadratic();
    } else if (e->degree == 3) {
#if AC_IS_128KB
        ac_solve_cubic(0);    /* skipRational = 0（128KB 签名带参） */
#else
        ac_solve_cubic();     /* 64KB 无参 */
#endif
    } else if (e->degree == 4) {
        ac_solve_quartic();
    }
}

/* ==================== 通用池式迭代 ==================== */
typedef void (*per_iter_fn)(const pool_entry_t *);

static void bench_pool(const char *stage, const pool_entry_t **items,
                       int pool_size, long long total_iter, per_iter_fn fn) {
    long long loops;
    long long actual_iter;
    double times[BENCH_RUNS];
    double med;
    int i, r;
    long long l;

    if (pool_size <= 0) {
        fprintf(stderr, "bench-c: %s 池在本版本下为空，跳过\n", stage);
        return;
    }

    loops = (total_iter + pool_size - 1) / pool_size;   /* ceil */
    actual_iter = loops * pool_size;

    /* 预热：按池顺序循环，覆盖全部条目 */
    for (i = 0; i < WARMUP; i++) { fn(items[i % pool_size]); }

    for (r = 0; r < BENCH_RUNS; r++) {
        double t0 = now_ms();
        for (l = 0; l < loops; l++) {
            for (i = 0; i < pool_size; i++) { fn(items[i]); }
        }
        times[r] = now_ms() - t0;
    }
    med = median_of(times, BENCH_RUNS);

    g_sum_ops += actual_iter;
    g_sum_ms  += med;

    printf("%s,pool%d,%lld,%.3f,%.4f\n",
           stage, pool_size, actual_iter, med, med / (double)actual_iter * 1000.0);
}

/* ==================== 表达式池静态占用 ====================
 * 仅统计本 bench 实际使用的表达式类池（编译期 sizeof 确定，确定性无波动）。
 * 不新增任何内核静态数组；整版 .bss 另见 BENCH-1M.md（size 口径）。 */
static size_t static_pool_bytes(void) {
    size_t t = 0;
    t += sizeof(g_token_pool);
    t += sizeof(g_number_pool);
    t += sizeof(g_eval_stack);
    t += sizeof(g_matrix_pool);
    t += sizeof(g_rpn_token);
    t += sizeof(g_op_stack);
    t += sizeof(g_lhs_token);
    t += sizeof(g_rhs_token);
    t += sizeof(g_coeffs);
    t += sizeof(g_root_re);
    t += sizeof(g_root_im);
    return t;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    g_parse_n = filter_into(POOL_PARSE, POOL_PARSE_N, g_parse);
    g_eval_n  = filter_into(POOL_EVAL,  POOL_EVAL_N,  g_eval);
    g_solve_n = filter_into(POOL_SOLVE, POOL_SOLVE_N, g_solve);
    g_exact_n = filter_into(POOL_EXACT, POOL_EXACT_N, g_exact);

    printf("STATIC,kernel_tag,%s\n", AC_IS_128KB ? "128KB" : "64KB");
    printf("STATIC,expr_pools_bytes,%zu\n", static_pool_bytes());
    printf("stage,name,iterations,total_ms,us_per_op\n");

    bench_pool("parse", g_parse, g_parse_n, 1000000LL, parse_once);
    bench_pool("eval",  g_eval,  g_eval_n,  1000000LL, eval_once);
    bench_pool("solve", g_solve, g_solve_n, 1000000LL, solve_once);
    bench_pool("exact", g_exact, g_exact_n, 1000000LL, solve_once);

    printf("SUMMARY,total_ops,%lld,total_ms,%.3f,avg_us_per_op,%.4f\n",
           g_sum_ops, g_sum_ms,
           (g_sum_ops > 0) ? g_sum_ms / (double)g_sum_ops * 1000.0 : 0.0);

    return 0;
}
