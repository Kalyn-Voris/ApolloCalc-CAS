/* ============================================================
 * ApolloCalc-CAS · test_smoke_csv.c — 全量冒烟 CSV 输出（第 10 轮 Part B）
 * 把 tests/ 共享 JSON（polynomials / exact-forms / edge-cases）的核心用例
 * 手工转录为 C 表，跑 C 64KB 内核，输出 CSV 供跨版本比对（Part C diff）。
 * 不在 test_runner 校验体系内：独立 target `mingw32-make smoke_runner`，
 *   ./smoke_runner.exe > ../../tests/bench/c-smoke.csv
 * 流水线（门禁先行，与 edge-cases.json 的 solveError 语义一致）：
 *   reset → parse → preCheck → guardDegree → extract → solve
 *   线性组：reset → 填增广矩阵 → solveLinearSystem
 *   precheck：reset → parse → preCheck
 * CSV 三段式（diff 脚本按 stage 分派列）：
 *   solve,<name>,<err>,<count>,re0,im0,...,re5,im5   （未用槽位补 0，-0 归 0）
 *   precheck,<name>,<err>
 *   exact,<name>,<err>,v0,n0,d0,r0,v1,n1,d1,r1       （valid=0 时三元组记 0）
 * 输出精度 %.17g（double 全往返）；err 语义 = 流水线退出码
 * （2=preCheck 拒绝 1=次数门禁 4=提取失败 / 其余 = g_solve_error）。
 * ============================================================ */
#include <stdio.h>
#include "../src/ac_parser.h"
#include "../src/ac_solvers.h"

/* [English] smoke_runner links without test_main.c, so it carries its own
   minimal --wrap forwarders: CRT startup allocations pass straight through
   (the kernel itself is heap-free; enforcement lives in test_runner). */
extern void *__real_malloc(size_t size);
extern void __real_free(void *ptr);
void *__wrap_malloc(size_t size) { return __real_malloc(size); }
void __wrap_free(void *ptr) { __real_free(ptr); }

#ifdef _WIN32
#define SM_I64 "%I64d"
#else
#define SM_I64 "%lld"
#endif

/* ==================== 用例表（tests 目录 JSON 转录） ==================== */

typedef struct {
    const char *name;
    const char *expr;
    int         deg;          /* 2/3/4；5 = 五次门禁用例 */
} poly_case_t;

/* polynomials.json 26 例取 24 + edge-cases 2 例（quad_gold 混合精确、
   quintic_gate 次数门禁）+ XL15 降次链；重根第二例（x^2-6x+9）裁剪 */
static const poly_case_t POLY[] = {
    { "quad_56",        "x^2-5x+6=0",              2 },  /* 整数根 */
    { "quad_cplx",      "x^2+1=0",                 2 },  /* 纯虚根 */
    { "quad_rhs9",      "x^2=9",                   2 },  /* 右端常数 */
    { "quad_dup",       "x^2-4x+4=0",              2 },  /* 二重根 */
    { "quad_frac",      "2x^2+3x-2=0",             2 },  /* 分数根 */
    { "quad_cplx5",     "x^2+2x+5=0",              2 },  /* 一般复根 */
    { "quad_352",       "3x^2-5x+2=0",             2 },
    { "quad_12",        "x^2+x-12=0",              2 },
    { "quad_cplx13",    "x^2-4x+13=0",             2 },
    { "quad_irr2x",     "x^2-2x-1=0",              2 },  /* 无理根 √2 型 */
    { "quad_rhs2x",     "x^2=2x+3",                2 },  /* 右端含 x */
    { "quad_degen",     "x^2=0",                   2 },  /* 退化零根 */
    { "quad_cplx2",     "x^2+2x+2=0",              2 },
    { "quad_gold",      "x^2-x-1=0",               2 },  /* 金率：数值根有效、精确形式无效 */
    { "cubic_123",      "x^3-6x^2+11x-6=0",        3 },
    { "cubic_8",        "x^3=8",                   3 },  /* 1 实 + 共轭复对 */
    { "cubic_trip",     "x^3-6x^2+12x-8=0",        3 },  /* 三重根 */
    { "cubic_irr",      "x^3-3x=0",                3 },  /* 不可约 → 三角法 */
    { "cubic_cardano",  "2x^3-x-1=0",              3 },  /* 卡尔达诺复对中间量 */
    { "cubic_1",        "x^3-1=0",                 3 },
    { "cubic_1111",     "x^3+x^2+x+1=0",           3 },
    { "quart_biquad",   "x^4-5x^2+4=0",            4 },  /* 双二次 */
    { "quart_1",        "x^4-1=0",                 4 },  /* 双二次复根 */
    { "quart_1234",     "x^4-10x^3+35x^2-50x+24=0",4 },
    { "quart_cplx",     "x^4+5x^2+4=0",            4 },  /* 全虚 */
    { "quart_general",  "2x^4-x^3-14x^2-5x+6=0",   4 },  /* 一般路径（笛卡尔分解） */
    { "quart_quad4",    "x^4-4x^3+6x^2-4x+1=0",    4 },  /* 四重根 */
    { "quart_4",        "x^4+4=0",                 4 },
    { "quart_deleg",    "x^3-6x^2+11x-6=0",        4 },  /* XL15：a=0 → 三次委托 */
    { "quintic_gate",   "x^5-1=0",                 5 }   /* 阿贝尔-鲁菲尼：门禁 err=1 */
};

typedef struct {
    const char *name;
    int         n;
    double      aug[42];      /* n×(n+1) 行主序增广矩阵 */
} lin_case_t;

static const lin_case_t LIN[] = {
    { "lin2_ok", 2, { 2, 3, 8,
                      1, -1, -1 } },                    /* x=1, y=2 */
    { "lin3_ok", 3, { 1, 1, 1, 6,
                      1, 2, 3, 14,
                      2, 3, -1, 5 } },
    { "lin6_ok", 6, { 1, 0, 0, 0, 0, 0, 1,          /* 对角恒等 → x_i = i */
                      0, 1, 0, 0, 0, 0, 2,
                      0, 0, 1, 0, 0, 0, 3,
                      0, 0, 0, 1, 0, 0, 4,
                      0, 0, 0, 0, 1, 0, 5,
                      0, 0, 0, 0, 0, 1, 6 } },
    { "lin2_sing", 2, { 1, 1, 1,
                        1, 1, 2 } }                     /* 奇异 → err=3 */
};

typedef struct {
    const char *name;
    const char *expr;
} pre_case_t;

static const pre_case_t PRE[] = {
    { "pre_depth", "((((x))))" },              /* 括号深度 > 3 → err=2 */
    { "pre_funcs", "sin(x)+cos(x)+tan(x)" },   /* 函数 > 2 → err=2 */
    { "pre_vars",  "x+x+x+x+x+x" }             /* 变量 > 5 → err=2 */
};

/* exact-forms.json 全 12 例 + 金率混合（valid=0 路径） */
typedef struct {
    const char *name;
    const char *expr;
} exact_case_t;

static const exact_case_t EXACT[] = {
    { "exact_56",   "x^2-5x+6=0" },
    { "exact_2",    "x^2-2=0" },
    { "exact_3",    "x^2-3=0" },
    { "exact_5",    "x^2-5=0" },
    { "exact_9",    "x^2=9" },
    { "exact_49",   "x^2-49=0" },
    { "exact_dup4", "x^2-4x+4=0" },
    { "exact_frac", "2x^2+3x-2=0" },
    { "exact_352",  "3x^2-5x+2=0" },
    { "exact_41",   "4x^2-1=0" },
    { "exact_6x1",  "6x^2-x-1=0" },
    { "exact_2x",   "x^2+2x=0" },
    { "exact_gold", "x^2-x-1=0" }              /* 混合形式 → valid=0 */
};

#define N_POLY   ((int)(sizeof(POLY)   / sizeof(POLY[0])))
#define N_LIN    ((int)(sizeof(LIN)    / sizeof(LIN[0])))
#define N_PRE    ((int)(sizeof(PRE)    / sizeof(PRE[0])))
#define N_EXACT  ((int)(sizeof(EXACT)  / sizeof(EXACT[0])))

/* ==================== 流水线 ==================== */

/* 返回流水线退出码（与 JS js-smoke.js 逐行同构） */
static int run_poly(const char *expr, int deg) {
    int err;
    int ee;
    ac_solvers_reset();
    ac_parse_expression(expr);
    if (ac_pre_check_expression() == 1) {
        err = g_solve_error;
    } else if (ac_guard_degree(deg) == 1) {
        err = g_solve_error;
    } else {
        ee = ac_extract_coefficients(deg);
        if (ee != 0) {
            err = ee;
        } else {
            if (deg == 2) { ac_solve_quadratic(); }
            else if (deg == 3) { ac_solve_cubic(); }
            else { ac_solve_quartic(); }
            err = g_solve_error;
        }
    }
    return err;
}

static int run_linear(const double *aug, int n) {
    int i;
    ac_solvers_reset();
    for (i = 0; i < n * (n + 1); i++) { g_matrix_pool[i] = aug[i]; }
    ac_solve_linear_system(n);
    return g_solve_error;
}

static int run_precheck(const char *expr) {
    ac_solvers_reset();
    ac_parse_expression(expr);
    return (ac_pre_check_expression() == 1) ? g_solve_error : 0;
}

/* -0 归一为 +0（跨语言文本可比；数值上 -0 == 0 本就相等） */
static double norm0(double v) {
    if (v == 0.0) { return 0.0; }
    return v;
}

static void print_solve_row(const char *name, int err) {
    int i;
    printf("solve,%s,%d,%d", name, err, g_root_count);
    for (i = 0; i < 6; i++) {
        if (i < g_root_count) {
            printf(",%.17g,%.17g", norm0(g_root_re[i]), norm0(g_root_im[i]));
        } else {
            printf(",0,0");
        }
    }
    printf("\n");
}

static void print_exact_row(const char *name, int err) {
    int s;
    printf("exact,%s,%d", name, err);
    for (s = 0; s < 2; s++) {
        if (s < g_root_count && g_root_exact_valid[s] == 1) {
            printf(",1," SM_I64 "," SM_I64 "," SM_I64,
                   (long long)g_root_exact_num[s],
                   (long long)g_root_exact_den[s],
                   (long long)g_root_exact_rad[s]);
        } else {
            /* 无精确形式（或槽位无根）：三元组记 0，两侧确定性一致 */
            printf(",0,0,0,0");
        }
    }
    printf("\n");
}

int main(void) {
    int i;
    printf("stage,name,err,count,re0,im0,re1,im1,re2,im2,re3,im3,re4,im4,re5,im5\n");
    for (i = 0; i < N_POLY; i++) {
        int err = run_poly(POLY[i].expr, POLY[i].deg);
        print_solve_row(POLY[i].name, err);
    }
    for (i = 0; i < N_LIN; i++) {
        int err = run_linear(LIN[i].aug, LIN[i].n);
        print_solve_row(LIN[i].name, err);
    }

    printf("precheck,name,err\n");
    for (i = 0; i < N_PRE; i++) {
        printf("precheck,%s,%d\n", PRE[i].name, run_precheck(PRE[i].expr));
    }

    printf("exact,name,err,valid0,num0,den0,rad0,valid1,num1,den1,rad1\n");
    for (i = 0; i < N_EXACT; i++) {
        int err = run_poly(EXACT[i].expr, 2);
        print_exact_row(EXACT[i].name, err);
    }
    return 0;
}
