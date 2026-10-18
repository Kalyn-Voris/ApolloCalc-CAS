/* ============================================================
 * ApolloCalc-CAS · test_newton.c — 牛顿法求根测试（C 128KB 第 5 轮，C 侧扩展）
 * JS 128KB 无对应实现，验收依据为数学事实（Node 核算基准值）：
 *   x⁵−5x+3 从 0.5 出发收敛到 0.6180339887498948（黄金比例共轭）
 *   x⁵−5x+3 从 −1.7 出发收敛到 −1.6180339887498949（−φ）
 *   x = cos(x) 的 Dottie 数 0.7390851332151607（RAD 模式）
 * 容差 1e-6：f' 为中心差分（h=1e-5），根定位精度 ~1e-10，留足裕量。
 * 失败路径：x²+1 无实根（牛顿轨道游走不收敛）、驻点导数为零、
 *           非有限入参、非法 max_iter/tol。
 * ============================================================ */
#include <stdio.h>
#include <math.h>
#include "../src/ac_types.h"
#include "../src/ac_parser.h"
#include "../src/ac_math.h"
#include "../src/ac_solvers.h"

static int s_pass = 0;
static int s_fail = 0;

static void expect_true(int cond, const char *name) {
    if (cond) { s_pass++; }
    else { s_fail++; printf("FAIL [newton] %s\n", name); }
}

static int approx(double a, double b, double tol) {
    double d = a - b;
    if (d < 0.0) { d = -d; }
    return d <= tol;
}

/* 牛顿端到端：reset → parse → newton，校验成功 + 根值 */
static void newton_ok(const char *src, double x0, double want, double tol,
                      const char *name) {
    int ret;
    ac_solvers_reset();
    ac_parse_expression(src);
    ret = ac_solve_newton(x0, 100, 1e-12);
    if (ret == 1 && g_solve_error == 0 && g_root_count == 1 &&
        g_root_im[0] == 0.0 && approx(g_root_re[0], want, tol)) {
        s_pass++;
    } else {
        s_fail++;
        printf("FAIL [newton] %s: ret=%d err=%d root=%.17g want=%.17g\n",
               name, ret, g_solve_error, g_root_re[0], want);
    }
}

/* 牛顿失败路径：校验返回 0 + err=3 + 无根 */
static void newton_err(const char *src, double x0, int max_iter, double tol,
                       const char *name) {
    int ret;
    ac_solvers_reset();
    ac_parse_expression(src);
    ret = ac_solve_newton(x0, max_iter, tol);
    if (ret == 0 && g_solve_error == 3 && g_root_count == 0) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL [newton] %s: ret=%d err=%d count=%d\n",
               name, ret, g_solve_error, g_root_count);
    }
}

int test_newton_run(void) {
    /* ---- 五次方程（解析求解器域外） ---- */
    newton_ok("x^5-5x+3", 0.5, 0.6180339887498948, 1e-6,
              "quintic root 0.6180339887 (golden conjugate)");
    newton_ok("x^5-5x+3", -1.7, -1.6180339887498949, 1e-6,
              "quintic root -1.6180339887 (-phi)");
    newton_ok("x^5-5x+3", 1.3, 1.2756822036509849, 1e-6,
              "quintic root 1.2756822037");

    /* ---- 超越方程：x = cos(x)（RAD 模式） ---- */
    {
        g_angle_deg = 0;
        newton_ok("x-cos(x)", 1.0, 0.7390851332151607, 1e-6,
                  "transcendental x=cos(x) dottie");
        g_angle_deg = 1;
    }

    /* ---- 多项式（与解析路径交叉验证：三次的实根） ---- */
    newton_ok("x^3-6x^2+11x-6", 0.0, 1.0, 1e-6,
              "cubic newton from 0 -> root 1");

    /* ---- 失败路径 ---- */
    newton_err("x^2+1", 2.0, 100, 1e-12, "no real root: orbit wanders -> fail");
    newton_err("x^2", 0.0, 100, 1e-12, "stationary point: f'(0)=0 -> fail");
    newton_err("x^5-5x+3", (double)NAN, 100, 1e-12, "NaN x0 -> fail");
    newton_err("x^5-5x+3", 0.5, 0, 1e-12, "max_iter=0 -> fail");
    newton_err("x^5-5x+3", 0.5, 100, -1.0, "negative tol -> fail");
    newton_err("x^5-5x+3", 0.5, 2, 1e-12, "max_iter too small -> fail");

    /* ---- 成功后状态：单根落 0 槽、精确形式无效 ---- */
    {
        int ret;
        ac_solvers_reset();
        ac_parse_expression("x^5-5x+3");
        ret = ac_solve_newton(0.5, 100, 1e-12);
        expect_true(ret == 1 && g_root_count == 1 &&
                    g_root_im[0] == 0.0 && g_root_exact_valid[0] == 0,
                    "newton success state: slot 0, exact invalid");
    }

    printf("==== C99 newton: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
