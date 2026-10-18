/* ============================================================
 * ApolloCalc-CAS · test_solvers.c — 方程求解器测试（第 4 轮）
 * 覆盖：二次/三次/四次求根（含复根、重根、退化降次链）、线性方程组
 *       （2/3/6 元 + 奇异，6 元回归根池 6 槽越界 bug）、残差回验、
 *       门禁（preCheck / guardDegree / 内存水位）、复位。
 * XL01-XL19 为跨语言用例，与 test/test_solvers_xlang.js
 * （JS 基准 JS/64KB/src，只读）逐项比对 root_count 与 6 槽根
 * re/im（容差 1e-9）；XL 表数值由该脚本输出直接转录。
 * 校验风格与 JS/64KB/test/test-solvers.js 一致。
 * ============================================================ */
#include <stdio.h>
#include <math.h>
#include "../src/ac_parser.h"
#include "../src/ac_evaluator.h"
#include "../src/ac_exact.h"
#include "../src/ac_solvers.h"

static int s_pass = 0;
static int s_fail = 0;

static void expect_true(int cond, const char *msg) {
    if (cond) { s_pass++; }
    else { s_fail++; printf("FAIL [solvers] %s\n", msg); }
}

static int approx(double a, double b, double tol) {
    double d = a - b;
    if (d < 0.0) { d = -d; }
    return d <= tol;
}

/* 打印当前根池（失败诊断用） */
static void dump_roots(void) {
    int j;
    printf("  err=%d count=%d roots=[", g_solve_error, g_root_count);
    for (j = 0; j < g_root_count; j++) {
        printf("%.17g%s ", g_root_re[j], g_root_im[j] == 0.0 ? "" : "+I");
    }
    printf("]\n");
}

/* 解多项式方程：reset → parse → 采样提取系数 → 按次数求解
 * （对齐 JS 测试助手 solvePoly） */
static void solve_poly(const char *src, int deg) {
    ac_solvers_reset();
    ac_parse_expression(src);
    ac_extract_coefficients(deg);
    if (deg == 2) { ac_solve_quadratic(); }
    else if (deg == 3) { ac_solve_cubic(); }
    else if (deg == 4) { ac_solve_quartic(); }
}

/* 期望实根集合全部命中（允许乱序，对齐 JS checkRoots：
   不校验 count 相等，重根用重复期望值表达） */
static void check_roots(const char *name, const double *want, int n_want, double tol) {
    int ok = (g_solve_error == 0);
    int w;
    int i;
    if (ok) {
        for (w = 0; w < n_want && ok; w++) {
            int hit = 0;
            for (i = 0; i < g_root_count; i++) {
                if (approx(g_root_re[i], want[w], tol) && fabs(g_root_im[i]) <= tol) { hit = 1; break; }
            }
            if (!hit) { ok = 0; }
        }
    }
    if (ok) { s_pass++; }
    else { s_fail++; printf("FAIL [solvers] %s\n", name); dump_roots(); }
}

/* 期望 (re,im) 根对集合全部命中（允许乱序，对齐 JS checkRootsC：
   校验 count 相等，用于复根） */
static void check_roots_c(const char *name, const double (*want)[2], int n_want, double tol) {
    int ok = (g_solve_error == 0 && g_root_count == n_want);
    int w;
    int i;
    if (ok) {
        for (w = 0; w < n_want && ok; w++) {
            int hit = 0;
            for (i = 0; i < g_root_count; i++) {
                if (approx(g_root_re[i], want[w][0], tol) && approx(g_root_im[i], want[w][1], tol)) {
                    hit = 1;
                    break;
                }
            }
            if (!hit) { ok = 0; }
        }
    }
    if (ok) { s_pass++; }
    else { s_fail++; printf("FAIL [solvers] %s\n", name); dump_roots(); }
}

/* ==================== 跨语言用例表（XL01-XL15） ====================
 * 数值由 test/test_solvers_xlang.js 在 JS/64KB 基准上输出转录；
 * 未用槽位两侧均为 0（reset 后求解器只写前 count 个槽） */
typedef struct {
    const char *src;
    int         deg;          /* 拟合次数（XL15 为三次方程按四次拟合） */
    int         want_count;
    double      want_re[6];
    double      want_im[6];
} xl_row_t;

static const xl_row_t XL[] = {
    { "x^2-5x+6=0", 2, 2,
     {3, 2, 0, 0, 0, 0},
     {0, 0, 0, 0, 0, 0} },               /* XL01 */
    { "x^2-2=0", 2, 2,
     {1.4142135623730951, -1.4142135623730951, 0, 0, 0, 0},
     {0, 0, 0, 0, 0, 0} },               /* XL02 */
    { "x^2+1=0", 2, 2,
     {0, 0, 0, 0, 0, 0},
     {1, -1, 0, 0, 0, 0} },              /* XL03 */
    { "2x^2+3x-2=0", 2, 2,
     {0.5, -2, 0, 0, 0, 0},
     {0, 0, 0, 0, 0, 0} },               /* XL04 */
    { "x^2-4x+4=0", 2, 2,
     {2, 2, 0, 0, 0, 0},
     {0, 0, 0, 0, 0, 0} },               /* XL05 */
    { "x^3-6x^2+11x-6=0", 3, 3,
     {3, 2, 0.9999999999999998, 0, 0, 0},
     {0, 0, 0, 0, 0, 0} },               /* XL06 不可约 → 三角法 */
    { "x^3-1=0", 3, 3,
     {1, -0.5, -0.5, 0, 0, 0},
     {0, 0.8660254037844386, -0.8660254037844386, 0, 0, 0} },  /* XL07 卡尔达诺 */
    { "x^3-3x=0", 3, 3,
     {1.7320508075688774, 1.2246467991473532e-16, -1.7320508075688776, 0, 0, 0},
     {0, 0, 0, 0, 0, 0} },               /* XL08 不可约 → 三角法，±√3 与 0 */
    { "x^3-6x^2+12x-8=0", 3, 3,
     {2, 2, 2, 0, 0, 0},
     {0, 0, 0, 0, 0, 0} },               /* XL09 三重根（Δ=0 路径） */
    { "2x^3-x-1=0", 3, 3,
     {1, -0.5, -0.5, 0, 0, 0},
     {0, 0.49999999999999994, -0.49999999999999994, 0, 0, 0} },  /* XL10 卡尔达诺复对 */
    { "x^4-5x^2+4=0", 4, 4,
     {2, -2, 1, -1, 0, 0},
     {0, 0, 0, 0, 0, 0} },               /* XL11 双二次 */
    { "x^4-1=0", 4, 4,
     {1, -1, 0, 0, 0, 0},
     {0, 0, 1, -1, 0, 0} },              /* XL12 双二次复根 */
    { "x^4-10x^3+35x^2-50x+24=0", 4, 4,
     {4, 1, 3, 2, 0, 0},
     {0, 0, 0, 0, 0, 0} },               /* XL13 */
    { "2x^4-x^3-14x^2-5x+6=0", 4, 4,
     {3, 0.5, -1, -2, 0, 0},
     {0, 0, 0, 0, 0, 0} },               /* XL14 一般路径（笛卡尔分解） */
    { "x^3-6x^2+11x-6=0", 4, 3,
     {3, 2, 0.9999999999999998, 0, 0, 0},
     {0, 0, 0, 0, 0, 0} }                /* XL15 四次 a=0 → 三次委托 */
};

/* 单条 XL 用例：解方程后逐槽比对 count/re/im（1e-9） */
static void check_xl(const xl_row_t *row, int idx) {
    int ok;
    int i;
    solve_poly(row->src, row->deg);
    ok = (g_solve_error == 0 && g_root_count == row->want_count);
    for (i = 0; ok && i < ROOT_POOL_SIZE; i++) {
        if (!approx(g_root_re[i], row->want_re[i], 1e-9) ||
            !approx(g_root_im[i], row->want_im[i], 1e-9)) { ok = 0; }
    }
    if (ok) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [solvers] XL%02d %s deg=%d (want count=%d)\n",
               idx, row->src, row->deg, row->want_count);
        dump_roots();
    }
}

int test_solvers_run(void) {
    const double SQRT2 = 1.4142135623730951;
    const double SQRT3 = 1.7320508075688772;
    int i;

    /* ==================== 模块一：二次 ==================== */
    solve_poly("x^2-5x+6=0", 2);
    {
        double want[2];
        want[0] = 3; want[1] = 2;
        check_roots("quad (x-3)(x-2)", want, 2, 1e-9);
    }
    expect_true(g_root_exact_valid[0] == 1 && g_root_exact_valid[1] == 1 &&
                g_root_exact_num[0] == 3 && g_root_exact_den[0] == 1 && g_root_exact_rad[0] == 1 &&
                g_root_exact_num[1] == 2 && g_root_exact_den[1] == 1 && g_root_exact_rad[1] == 1,
                "quad exact integer roots");

    solve_poly("x^2-x-1=0", 2);
    {
        double want[2];
        want[0] = (1.0 + sqrt(5.0)) / 2.0;
        want[1] = (1.0 - sqrt(5.0)) / 2.0;
        check_roots("quad golden", want, 2, 1e-9);
    }
    expect_true(g_root_exact_valid[0] == 0 && g_root_exact_valid[1] == 0,
                "quad golden exact should be invalid");   /* 混合形式置无效 */

    solve_poly("x^2-2=0", 2);
    {
        double want[2];
        want[0] = SQRT2;
        want[1] = -SQRT2;
        check_roots("quad sqrt2", want, 2, 1e-9);
    }
    expect_true(g_root_exact_valid[0] == 1 && g_root_exact_num[0] == 1 &&
                g_root_exact_den[0] == 1 && g_root_exact_rad[0] == 2 &&
                g_root_exact_valid[1] == 1 && g_root_exact_num[1] == -1,
                "quad sqrt2 exact (1/1)·√2 与共轭");

    solve_poly("x^2+1=0", 2);
    expect_true(g_root_count == 2 && approx(g_root_re[0], 0, 1e-12) &&
                approx(g_root_im[0], 1, 1e-9) && approx(g_root_im[1], -1, 1e-9) &&
                g_root_exact_valid[0] == 0,
                "quad complex x^2+1");

    solve_poly("2x^2+3x-2=0", 2);
    {
        double want[2];
        want[0] = 0.5; want[1] = -2;
        check_roots("quad 2x^2+3x-2", want, 2, 1e-9);
    }

    solve_poly("x^2-4x+4=0", 2);
    {
        double want[2];
        want[0] = 2; want[1] = 2;
        check_roots("quad double root", want, 2, 1e-9);
    }
    expect_true(g_root_exact_valid[0] == 1 && g_root_exact_valid[1] == 1,
                "quad double root exact valid");   /* D=0 仍精确 */

    /* 退化 a=0 → 线性单根，整数系数保留精确分数 */
    solve_poly("2x-4=0", 2);
    expect_true(g_solve_error == 0 && g_root_count == 1 &&
                approx(g_root_re[0], 2, 1e-9) && g_root_im[0] == 0.0 &&
                g_root_exact_valid[0] == 1 && g_root_exact_num[0] == 2 &&
                g_root_exact_den[0] == 1 && g_root_exact_rad[0] == 1,
                "quad degenerate linear 2x-4");

    /* 矛盾式：无根但非错误；恒等式：报错 3 */
    solve_poly("x-x+1=0", 2);
    expect_true(g_solve_error == 0 && g_root_count == 0, "quad contradiction x-x+1=0");
    solve_poly("x-x=0", 2);
    expect_true(g_solve_error == 3 && g_root_count == 0, "quad identity x-x=0");

    /* ==================== 模块二：三次 ==================== */
    solve_poly("x^3-6x^2+11x-6=0", 3);
    {
        double want[3];
        want[0] = 1; want[1] = 2; want[2] = 3;
        check_roots("cubic (x-1)(x-2)(x-3) trig", want, 3, 1e-9);
    }
    expect_true(g_root_exact_valid[0] == 0 && g_root_exact_valid[1] == 0 &&
                g_root_exact_valid[2] == 0,
                "cubic exact should be all invalid");

    solve_poly("x^3-1=0", 3);
    {
        double want[3][2];
        want[0][0] = 1;    want[0][1] = 0;
        want[1][0] = -0.5; want[1][1] = SQRT3 / 2.0;
        want[2][0] = -0.5; want[2][1] = -SQRT3 / 2.0;
        check_roots_c("cubic cardano x^3-1", want, 3, 1e-9);
    }

    solve_poly("x^3-6x^2+12x-8=0", 3);
    {
        double want[3];
        want[0] = 2; want[1] = 2; want[2] = 2;
        check_roots("cubic triple root", want, 3, 1e-9);
    }

    solve_poly("x^3-3x=0", 3);
    {
        double want[3];
        want[0] = SQRT3; want[1] = 0; want[2] = -SQRT3;
        check_roots("cubic irreducible x^3-3x", want, 3, 1e-9);
    }

    /* 非首一项系数：2x^3-x-1 = (x-1)(2x^2+2x+1) → 1 与 -0.5±0.5i */
    solve_poly("2x^3-x-1=0", 3);
    {
        double want[3][2];
        want[0][0] = 1;    want[0][1] = 0;
        want[1][0] = -0.5; want[1][1] = 0.5;
        want[2][0] = -0.5; want[2][1] = -0.5;
        check_roots_c("cubic 2x^3-x-1", want, 3, 1e-9);
    }

    /* ==================== 模块三：四次 ==================== */
    solve_poly("x^4-5x^2+4=0", 4);
    {
        double want[4];
        want[0] = 1; want[1] = -1; want[2] = 2; want[3] = -2;
        check_roots("quartic biquad (x^2-1)(x^2-4)", want, 4, 1e-9);
    }
    expect_true(g_root_exact_valid[0] == 0 && g_root_exact_valid[3] == 0,
                "quartic biquad exact should be invalid");

    solve_poly("x^4-1=0", 4);
    {
        double want[4][2];
        want[0][0] = 1;  want[0][1] = 0;
        want[1][0] = -1; want[1][1] = 0;
        want[2][0] = 0;  want[2][1] = 1;
        want[3][0] = 0;  want[3][1] = -1;
        check_roots_c("quartic x^4-1 biquad", want, 4, 1e-9);
    }

    solve_poly("x^4+5x^2+4=0", 4);
    expect_true(g_solve_error == 0 && g_root_count == 4 &&
                approx(fabs(g_root_im[0]), 1, 1e-9) && approx(fabs(g_root_im[2]), 2, 1e-9) &&
                approx(g_root_re[0], 0, 1e-9) && approx(g_root_re[2], 0, 1e-9),
                "quartic complex biquad x^4+5x^2+4");

    solve_poly("x^4-10x^3+35x^2-50x+24=0", 4);
    {
        double want[4];
        want[0] = 1; want[1] = 2; want[2] = 3; want[3] = 4;
        check_roots("quartic general (1,2,3,4)", want, 4, 1e-8);
    }

    solve_poly("x^4-3x^3+3x^2-3x+2=0", 4);
    {
        double want[2];
        want[0] = 1; want[1] = 2;
        check_roots("quartic mixed real+complex", want, 2, 1e-8);
    }

    /* 首系数非 1：(2x-1)(x-3)(x+2)(x+1) = 2x^4-x^3-14x^2-5x+6 */
    solve_poly("2x^4-x^3-14x^2-5x+6=0", 4);
    {
        double want[4];
        want[0] = 0.5; want[1] = 3; want[2] = -2; want[3] = -1;
        check_roots("quartic frac leading (0.5,3,-2,-1)", want, 4, 1e-7);
    }

    /* ==================== 降次链：四次 a=0 → 三次委托 ====================
     * extract_coefficients(3) 后 g_coeffs[4] 恒为 0，solve_quartic 应降次 */
    solve_poly("x^3-6x^2+11x-6=0", 4);
    {
        double want[3];
        want[0] = 1; want[1] = 2; want[2] = 3;
        check_roots("quartic degenerate → cubic", want, 3, 1e-9);
    }

    /* ==================== 模块四：线性方程组 ==================== */
    ac_solvers_reset();
    g_matrix_pool[0] = 2; g_matrix_pool[1] = 3; g_matrix_pool[2] = 8;
    g_matrix_pool[3] = 1; g_matrix_pool[4] = -1; g_matrix_pool[5] = -1;
    ac_solve_linear_system(2);
    expect_true(g_solve_error == 0 && g_root_count == 2 &&
                approx(g_root_re[0], 1, 1e-9) && approx(g_root_re[1], 2, 1e-9) &&
                g_root_im[0] == 0.0 && g_root_im[1] == 0.0,
                "lin2 2x+3y=8, x-y=-1");

    ac_solvers_reset();
    g_matrix_pool[0] = 1; g_matrix_pool[1] = 1; g_matrix_pool[2] = 1; g_matrix_pool[3] = 6;
    g_matrix_pool[4] = 1; g_matrix_pool[5] = 2; g_matrix_pool[6] = 3; g_matrix_pool[7] = 14;
    g_matrix_pool[8] = 2; g_matrix_pool[9] = 3; g_matrix_pool[10] = -1; g_matrix_pool[11] = 5;
    ac_solve_linear_system(3);
    expect_true(g_solve_error == 0 && g_root_count == 3 &&
                approx(g_root_re[0], 1, 1e-9) && approx(g_root_re[1], 2, 1e-9) &&
                approx(g_root_re[2], 3, 1e-9),
                "lin3 x+y+z=6, x+2y+3z=14, 2x+3y-z=5");

    /* 6 元对角方程组：解 x_i = i（根池 6 槽上限，回归 4 槽越界 bug） */
    ac_solvers_reset();
    g_matrix_pool[0] = 1; g_matrix_pool[1] = 0; g_matrix_pool[2] = 0; g_matrix_pool[3] = 0; g_matrix_pool[4] = 0; g_matrix_pool[5] = 0; g_matrix_pool[6] = 1;
    g_matrix_pool[7] = 0; g_matrix_pool[8] = 1; g_matrix_pool[9] = 0; g_matrix_pool[10] = 0; g_matrix_pool[11] = 0; g_matrix_pool[12] = 0; g_matrix_pool[13] = 2;
    g_matrix_pool[14] = 0; g_matrix_pool[15] = 0; g_matrix_pool[16] = 1; g_matrix_pool[17] = 0; g_matrix_pool[18] = 0; g_matrix_pool[19] = 0; g_matrix_pool[20] = 3;
    g_matrix_pool[21] = 0; g_matrix_pool[22] = 0; g_matrix_pool[23] = 0; g_matrix_pool[24] = 1; g_matrix_pool[25] = 0; g_matrix_pool[26] = 0; g_matrix_pool[27] = 4;
    g_matrix_pool[28] = 0; g_matrix_pool[29] = 0; g_matrix_pool[30] = 0; g_matrix_pool[31] = 0; g_matrix_pool[32] = 1; g_matrix_pool[33] = 0; g_matrix_pool[34] = 5;
    g_matrix_pool[35] = 0; g_matrix_pool[36] = 0; g_matrix_pool[37] = 0; g_matrix_pool[38] = 0; g_matrix_pool[39] = 0; g_matrix_pool[40] = 1; g_matrix_pool[41] = 6;
    ac_solve_linear_system(6);
    expect_true(g_solve_error == 0 && g_root_count == 6 &&
                approx(g_root_re[0], 1, 1e-9) && approx(g_root_re[1], 2, 1e-9) &&
                approx(g_root_re[2], 3, 1e-9) && approx(g_root_re[3], 4, 1e-9) &&
                approx(g_root_re[4], 5, 1e-9) && approx(g_root_re[5], 6, 1e-9),
                "lin6 diagonal x_i=i (6-slot root pool)");

    ac_solvers_reset();
    g_matrix_pool[0] = 1; g_matrix_pool[1] = 1; g_matrix_pool[2] = 1;
    g_matrix_pool[3] = 1; g_matrix_pool[4] = 1; g_matrix_pool[5] = 2;
    ac_solve_linear_system(2);
    expect_true(g_solve_error == 3 && g_root_count == 0, "lin singular");

    /* ==================== 模块五：残差回验 ==================== */
    solve_poly("x^2-5x+6=0", 2);
    ac_verify_roots();
    expect_true(g_residual_max < 1e-6, "residual clean");

    /* 坏根注入：根偏移 0.001 → 相对残差应远超 1e-6 */
    solve_poly("x^2-5x+6=0", 2);
    g_root_re[0] = g_root_re[0] + 0.001;
    ac_verify_roots();
    expect_true(g_residual_max > 1e-6, "residual bad root");

    /* 复根跳过：x^2+1 无实根 → residual 0 */
    solve_poly("x^2+1=0", 2);
    ac_verify_roots();
    expect_true(g_residual_max == 0.0, "residual complex skip");

    /* ==================== 模块六：门禁 ==================== */
    ac_solvers_reset();
    ac_parse_expression("((((x))))");
    expect_true(ac_pre_check_expression() == 1 && g_solve_error == 2,
                "preCheck depth4 rejected");

    ac_parse_expression("sin(x)+cos(x)");
    expect_true(ac_pre_check_expression() == 0, "preCheck 2 funcs should pass");

    ac_parse_expression("sin(x)+cos(x)+tan(x)");
    expect_true(ac_pre_check_expression() == 1 && g_solve_error == 2,
                "preCheck 3 funcs rejected");

    ac_parse_expression("x+x+x+x+x+x");
    expect_true(ac_pre_check_expression() == 1 && g_solve_error == 2,
                "preCheck 6 vars rejected");

    ac_parse_expression("x+x+x+x+x");
    expect_true(ac_pre_check_expression() == 0, "preCheck 5 vars should pass");

    expect_true(ac_guard_degree(5) == 1 && g_solve_error == 1 && ac_guard_degree(4) == 0,
                "guardDegree 5 rejected / 4 passed");

    expect_true(ac_check_memory_watermark() == 0, "watermark baseline");

    /* ==================== 模块七：复位 ==================== */
    solve_poly("x^2-5x+6=0", 2);
    ac_solvers_reset();
    expect_true(g_root_count == 0 && g_solve_error == 0 && g_residual_max == 0.0 &&
                g_root_exact_den[0] == 1 && g_root_exact_rad[3] == 1 &&
                g_root_exact_valid[2] == 0,
                "solversReset");

    /* ==================== 模块八：跨语言 XL01-XL19 ==================== */
    for (i = 0; i < (int)(sizeof(XL) / sizeof(XL[0])); i++) {
        check_xl(&XL[i], i + 1);
    }

    /* XL16 lin2：2x+3y=8, x−y=−1 → x=1, y=2 */
    ac_solvers_reset();
    g_matrix_pool[0] = 2; g_matrix_pool[1] = 3; g_matrix_pool[2] = 8;
    g_matrix_pool[3] = 1; g_matrix_pool[4] = -1; g_matrix_pool[5] = -1;
    ac_solve_linear_system(2);
    expect_true(g_solve_error == 0 && g_root_count == 2 &&
                approx(g_root_re[0], 1, 1e-9) && approx(g_root_re[1], 2, 1e-9) &&
                g_root_im[0] == 0.0 && g_root_im[1] == 0.0 &&
                g_root_exact_valid[0] == 0 && g_root_exact_valid[1] == 0,
                "XL16 lin2");

    /* XL17 lin3：x+y+z=6, x+2y+3z=14, 2x+3y−z=5 → 1,2,3 */
    ac_solvers_reset();
    g_matrix_pool[0] = 1; g_matrix_pool[1] = 1; g_matrix_pool[2] = 1; g_matrix_pool[3] = 6;
    g_matrix_pool[4] = 1; g_matrix_pool[5] = 2; g_matrix_pool[6] = 3; g_matrix_pool[7] = 14;
    g_matrix_pool[8] = 2; g_matrix_pool[9] = 3; g_matrix_pool[10] = -1; g_matrix_pool[11] = 5;
    ac_solve_linear_system(3);
    expect_true(g_solve_error == 0 && g_root_count == 3 &&
                approx(g_root_re[0], 1, 1e-9) && approx(g_root_re[1], 2, 1e-9) &&
                approx(g_root_re[2], 3, 1e-9),
                "XL17 lin3");

    /* XL18 lin6：6 元对角方程组，6 个根全部返回（对齐 JS 64KB 根池修复） */
    ac_solvers_reset();
    g_matrix_pool[0] = 1; g_matrix_pool[1] = 0; g_matrix_pool[2] = 0; g_matrix_pool[3] = 0; g_matrix_pool[4] = 0; g_matrix_pool[5] = 0; g_matrix_pool[6] = 1;
    g_matrix_pool[7] = 0; g_matrix_pool[8] = 1; g_matrix_pool[9] = 0; g_matrix_pool[10] = 0; g_matrix_pool[11] = 0; g_matrix_pool[12] = 0; g_matrix_pool[13] = 2;
    g_matrix_pool[14] = 0; g_matrix_pool[15] = 0; g_matrix_pool[16] = 1; g_matrix_pool[17] = 0; g_matrix_pool[18] = 0; g_matrix_pool[19] = 0; g_matrix_pool[20] = 3;
    g_matrix_pool[21] = 0; g_matrix_pool[22] = 0; g_matrix_pool[23] = 0; g_matrix_pool[24] = 1; g_matrix_pool[25] = 0; g_matrix_pool[26] = 0; g_matrix_pool[27] = 4;
    g_matrix_pool[28] = 0; g_matrix_pool[29] = 0; g_matrix_pool[30] = 0; g_matrix_pool[31] = 0; g_matrix_pool[32] = 1; g_matrix_pool[33] = 0; g_matrix_pool[34] = 5;
    g_matrix_pool[35] = 0; g_matrix_pool[36] = 0; g_matrix_pool[37] = 0; g_matrix_pool[38] = 0; g_matrix_pool[39] = 0; g_matrix_pool[40] = 1; g_matrix_pool[41] = 6;
    ac_solve_linear_system(6);
    expect_true(g_solve_error == 0 && g_root_count == 6 &&
                approx(g_root_re[0], 1, 1e-9) && approx(g_root_re[1], 2, 1e-9) &&
                approx(g_root_re[2], 3, 1e-9) && approx(g_root_re[3], 4, 1e-9) &&
                approx(g_root_re[4], 5, 1e-9) && approx(g_root_re[5], 6, 1e-9),
                "XL18 lin6 diagonal x_i=i");

    /* XL19 lin-singular：主元 < 1e-12 → err=3, count=0 */
    ac_solvers_reset();
    g_matrix_pool[0] = 1; g_matrix_pool[1] = 1; g_matrix_pool[2] = 1;
    g_matrix_pool[3] = 1; g_matrix_pool[4] = 1; g_matrix_pool[5] = 2;
    ac_solve_linear_system(2);
    expect_true(g_solve_error == 3 && g_root_count == 0, "XL19 lin-singular");

    printf("==== solvers: %d pass, %d fail ====\n", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}
