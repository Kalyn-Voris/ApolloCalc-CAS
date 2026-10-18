/* ============================================================
 * ApolloCalc-CAS · ac_calculus.c — 数值微积分（C 128KB 第 3 轮）
 * 行为基准：JS/128KB/src/core_calculus.js（文件 7），逐函数逐分支对齐。
 * 复用是有意为之的性能优化：积分最坏 ~1.3 万次求值只做一次 toRPN，
 * 之后直接驱动 evaluator 的 g_rpn_token / g_eval_x / g_eval_error。
 * UI 契约：表达式变更后必须先 ac_calculus_reset() 再调 ac_calc_*，
 * 否则 s_rpn_ready 会复用旧表达式的缓存 RPN（测试有专测钉住）。
 * Zero-GC：无 malloc、无 VLA、无递归（差分表原地逐层覆盖，单缓冲）。
 * ============================================================ */
#include "ac_calculus.h"
#include "ac_parser.h"
#include "ac_evaluator.h"

/* ==================== 模块状态（唯一声明处） ==================== */

double g_calc_result = 0.0;
int    g_calc_error = 0;
int    g_calc_iterations = 0;

static int s_rpn_ready = 0;              /* 0=未缓存 1=已缓存（reset 清零） */

double g_taylor_coeffs[16];              /* 展开系数 c_k（最多 16 阶） */
int    g_taylor_order = 0;               /* 当前展开阶数 */
double g_taylor_center = 0.0;            /* 展开中心 x0 */

static double s_taylor_work[256];        /* 差分表工作区（2K+1 ≤ 33，用完即弃） */

/* ==================== 模块一：域检查与内部求值 ==================== */

/* 扫描 token 池是否含等号；含等号是方程不是函数 → 微积分域错误 */
static int calc_check_function(void) {
    int i;
    for (i = 0; i < g_token_count; i++) {
        if (ac_token_type(g_token_pool[i]) == T_EQ) { return 1; }
    }
    return 0;
}

/* 缓存 RPN（首次调用做 toRPN，后续微积分操作直接复用）。
   关键：toRPN 成功时不清全局 g_eval_error，须先清零再查
   （与 ac_eval_expression_at 契约一致），否则上游遗留错误码毒化缓存准备 */
static int calc_prepare_rpn(void) {
    if (s_rpn_ready) { return 0; }
    g_eval_error = 0;
    g_rpn_len = ac_to_rpn(g_token_pool, g_token_count, g_rpn_token);
    if (g_eval_error != 0) { return g_eval_error; }
    s_rpn_ready = 1;
    return 0;
}

/* 轻量求值：置 g_eval_x、清 g_eval_error、驱动缓存 RPN（JS 逐点同款） */
static double calc_eval(double x) {
    g_eval_x = x;
    g_eval_error = 0;
    return ac_eval_rpn(g_rpn_token, g_rpn_len);
}

/* ==================== 模块二：一阶导数（中心差分） ====================
 * f'(x) ≈ (f(x+h) - f(x-h)) / (2h)
 * h = 1e-5·max(|x|,1)：x 近 0 时 h=1e-5；x 大时 h 正比于 x，
 * 避免 x+h 与 x 因浮点精度无法区分 */

double ac_calc_derivative(double x) {
    double ax;
    double h;
    double y_plus;
    double y_minus;

    if (x != x || x == (double)INFINITY || x == -(double)INFINITY) {
        g_calc_error = 1; return (double)NAN;
    }
    if (calc_check_function() != 0) { g_calc_error = 1; return (double)NAN; }
    if (calc_prepare_rpn() != 0) { g_calc_error = 4; return (double)NAN; }

    ax = (x < 0.0) ? -x : x;
    h = 1e-5 * ((ax > 1.0) ? ax : 1.0);

    y_plus = calc_eval(x + h);
    if (g_eval_error != 0) { g_calc_error = 1; return (double)NAN; }
    y_minus = calc_eval(x - h);
    if (g_eval_error != 0) { g_calc_error = 1; return (double)NAN; }

    g_calc_iterations = 2;
    g_calc_result = (y_plus - y_minus) / (2.0 * h);
    if (g_calc_result != g_calc_result || g_calc_result == (double)INFINITY ||
        g_calc_result == -(double)INFINITY) {
        g_calc_error = 1; return (double)NAN;
    }
    g_calc_error = 0;
    return g_calc_result;
}

/* ==================== 模块三：二阶导数（五点公式） ====================
 * f''(x) ≈ (-f(x+2h) + 16f(x+h) - 30f(x) + 16f(x-h) - f(x-2h)) / (12h²)
 * h = 1e-4·max(|x|,1)（二阶差分舍入更敏感，h 略大于一阶） */

double ac_calc_derivative2(double x) {
    double ax;
    double h;
    double h2;
    double fp2;
    double fp1;
    double f0;
    double fm1;
    double fm2;

    if (x != x || x == (double)INFINITY || x == -(double)INFINITY) {
        g_calc_error = 1; return (double)NAN;
    }
    if (calc_check_function() != 0) { g_calc_error = 1; return (double)NAN; }
    if (calc_prepare_rpn() != 0) { g_calc_error = 4; return (double)NAN; }

    ax = (x < 0.0) ? -x : x;
    h = 1e-4 * ((ax > 1.0) ? ax : 1.0);
    h2 = h * h;

    fp2 = calc_eval(x + 2.0 * h);
    if (g_eval_error != 0) { g_calc_error = 1; return (double)NAN; }
    fp1 = calc_eval(x + h);
    if (g_eval_error != 0) { g_calc_error = 1; return (double)NAN; }
    f0 = calc_eval(x);
    if (g_eval_error != 0) { g_calc_error = 1; return (double)NAN; }
    fm1 = calc_eval(x - h);
    if (g_eval_error != 0) { g_calc_error = 1; return (double)NAN; }
    fm2 = calc_eval(x - 2.0 * h);
    if (g_eval_error != 0) { g_calc_error = 1; return (double)NAN; }

    g_calc_iterations = 5;
    g_calc_result = (-fp2 + 16.0 * fp1 - 30.0 * f0 + 16.0 * fm1 - fm2) / (12.0 * h2);
    if (g_calc_result != g_calc_result || g_calc_result == (double)INFINITY ||
        g_calc_result == -(double)INFINITY) {
        g_calc_error = 1; return (double)NAN;
    }
    g_calc_error = 0;
    return g_calc_result;
}

/* ==================== 模块四：定积分（自适应复合辛普森） ====================
 * ∫[a,b] f dx ≈ (h/3)[f(a) + 4Σf(奇内点) + 2Σf(偶内点) + f(b)]
 * 自适应：n 从 100 起每轮加倍（上限 10000），前后两轮差 < 1e-8 即停；
 * 每轮单次遍历累加 sumOdd/sumEven，不缓存采样点（辛普森节点随 n 变化不可复用）；
 * 采样点 x = a + i·h 用乘法重算，不做 x += h 累积（避免浮点误差漂移）；
 * n 加倍到上限仍未收敛时返回当前结果且 g_calc_error=0，由 UI 层自行判断 */

double ac_calc_integral(double a, double b) {
    double fa;
    double fb;
    double h;
    double x;
    double y;
    double sum_odd;
    double sum_even;
    double prev;
    double curr;
    double diff;
    int i;
    int n;
    int iter_total = 0;

    if (a != a || a == (double)INFINITY || a == -(double)INFINITY ||
        b != b || b == (double)INFINITY || b == -(double)INFINITY) {
        g_calc_error = 1; return (double)NAN;
    }
    if (a == b) { g_calc_result = 0.0; g_calc_error = 0; return 0.0; }
    if (calc_check_function() != 0) { g_calc_error = 1; return (double)NAN; }
    if (calc_prepare_rpn() != 0) { g_calc_error = 4; return (double)NAN; }

    /* 端点 f(a)、f(b) */
    fa = calc_eval(a);
    if (g_eval_error != 0) { g_calc_error = 1; return (double)NAN; }
    fb = calc_eval(b);
    if (g_eval_error != 0) { g_calc_error = 1; return (double)NAN; }
    iter_total = 2;

    n = 100;
    prev = 0.0;
    curr = 0.0;
    while (n <= 10000) {
        h = (b - a) / (double)n;
        sum_odd = 0.0;
        sum_even = 0.0;
        for (i = 1; i < n; i++) {
            x = a + (double)i * h;
            y = calc_eval(x);
            if (g_eval_error != 0) { g_calc_error = 1; return (double)NAN; }
            iter_total++;
            if (i % 2 == 1) { sum_odd += y; }
            else { sum_even += y; }
        }
        curr = (h / 3.0) * (fa + 4.0 * sum_odd + 2.0 * sum_even + fb);
        if (curr != curr || curr == (double)INFINITY || curr == -(double)INFINITY) {
            g_calc_error = 1; return (double)NAN;
        }

        if (n > 100) {
            diff = curr - prev;
            if (diff < 0.0) { diff = -diff; }
            if (diff < 1e-8) { break; }
        }
        prev = curr;
        n = n * 2;
    }

    g_calc_iterations = iter_total;
    g_calc_result = curr;
    g_calc_error = 0;
    return g_calc_result;
}

/* ==================== 模块五：极限（左右逼近） ====================
 * side: -1 左极限 / +1 右极限 / 0 双侧（左右都收敛且相差 < 1e-6 才返回均值）
 *
 * 单侧逼近：eps 从 1e-1 逐级缩到 1e-8（8 个采样点），相邻两级函数值差
 * < 1e-6 判收敛。收敛阈值取 1e-6 而非 1e-8 —— 一阶逼近型极限
 * （如 (x²-1)/(x-1)→2，x=1±eps 时值 ≈ 2±eps）相邻采样差 ≈ 9·10⁻ᵏ，
 * 6 步方案（eps≥1e-6）下最小差 9e-6，1e-8 阈值恒不收敛。8 步 + 1e-6
 * 下 sin(x)/x 类仍达 ~1e-9 精度（JS 128KB 最终版同款参数）。 */

/* 单侧逼近内核（仅供 ac_calc_limit 调用，非递归） */
static double calc_limit_one_side(double a, int side) {
    double eps = 0.1;
    double prev = (double)NAN;
    double curr = 0.0;
    double x;
    double d;
    int step;
    int iter_total = 0;

    for (step = 0; step < 8; step++) {
        x = a + (double)side * eps;
        curr = calc_eval(x);
        if (g_eval_error != 0) { g_calc_error = 1; return (double)NAN; }
        if (curr != curr) { g_calc_error = 1; return (double)NAN; }  /* 采样点落出域 */
        iter_total++;

        if (step > 0 && prev == prev) {
            d = curr - prev;
            if (d < 0.0) { d = -d; }
            if (d < 1e-6) {
                g_calc_iterations = iter_total;
                g_calc_result = curr;
                g_calc_error = 0;
                return curr;
            }
        }
        prev = curr;
        eps = eps * 0.1;
    }
    g_calc_iterations = iter_total;
    g_calc_error = 2;
    return (double)NAN;
}

double ac_calc_limit(double a, int side) {
    double lim_l;
    double lim_r;
    double dl;

    if (a != a || a == (double)INFINITY || a == -(double)INFINITY) {
        g_calc_error = 1; return (double)NAN;
    }
    if (calc_check_function() != 0) { g_calc_error = 1; return (double)NAN; }
    if (calc_prepare_rpn() != 0) { g_calc_error = 4; return (double)NAN; }

    if (side == 0) {
        lim_l = calc_limit_one_side(a, -1);
        if (g_calc_error != 0) { return (double)NAN; }
        lim_r = calc_limit_one_side(a, 1);
        if (g_calc_error != 0) { return (double)NAN; }
        dl = lim_l - lim_r;
        if (dl < 0.0) { dl = -dl; }
        if (dl > 1e-6) { g_calc_error = 2; return (double)NAN; }  /* 左右极限不一致 */
        g_calc_result = (lim_l + lim_r) / 2.0;
        g_calc_error = 0;
        return g_calc_result;
    }
    return calc_limit_one_side(a, side);
}

/* ==================== 模块六：复位 ====================
 * UI 层在每次表达式变更后的首个微积分操作前调用（零分配；
 * 泰勒系数属于旧表达式，必须一并失效） */

void ac_calculus_reset(void) {
    g_calc_result = 0.0;
    g_calc_error = 0;
    g_calc_iterations = 0;
    s_rpn_ready = 0;
    g_taylor_order = 0;
    g_taylor_coeffs[0] = 0.0;
}

/* ==================== 模块七：泰勒展开 ====================
 * f(x) 在 x0 点的 order 阶泰勒展开，系数 c_k = f^(k)(x0)/k! 缓存于
 * g_taylor_coeffs：
 *   k=0   直接求值 f(x0)
 *   k=1   复用一阶中心差分（ac_calc_derivative，h=1e-5·max(|x0|,1)）
 *   k=2   复用二阶五点公式（ac_calc_derivative2，h=1e-4·max(|x0|,1)）
 *   k≥3   中心差分表：网格 x_j = x0+(j-K)h（j=0..2K，K=阶数），
 *         逐层前向差分（原地覆盖，单缓冲），偶数阶取中心表元素
 *         Δ^k f_{K-k/2}，奇数阶取跨中心两表元素平均——均为 O(h²) 精度；
 *         步长 h = eps^(1/(K+2))·max(|x0|,1)（eps=2.22e-16，逐阶舍入/截断折中）
 * 数值边界：k≥10 的高阶导数差分舍入放大不可避免，高阶系数精度随阶数
 *   衰减是数值求导的固有极限，UI 层按需取低阶
 * 返回 0 = 成功；-1 = 参数/域错误（g_calc_error：1=域错误 3=阶数超限 4=内部错） */

int ac_calc_taylor(double x0, int order) {
    double scale;
    double h;
    double f0;
    double d1;
    double d2;
    double der;
    double fact;
    double yj;
    int K;
    int k;
    int j;
    int t;
    int s;
    int iter_total = 0;

    g_calc_error = 0;
    if (x0 != x0 || x0 == (double)INFINITY || x0 == -(double)INFINITY) {
        g_calc_error = 1; return -1;
    }
    if (order < 0 || order > 16) { g_calc_error = 3; return -1; }
    if (calc_check_function() != 0) { g_calc_error = 1; return -1; }
    if (calc_prepare_rpn() != 0) { g_calc_error = 4; return -1; }

    K = order;
    scale = (x0 < 0.0) ? -x0 : x0;
    if (scale < 1.0) { scale = 1.0; }

    g_taylor_center = x0;
    g_taylor_order = K;

    /* k=0：f(x0) */
    f0 = calc_eval(x0);
    iter_total++;
    if (g_eval_error != 0 || f0 != f0) { g_calc_error = 1; return -1; }
    g_taylor_coeffs[0] = f0;
    g_calc_result = f0;

    /* k=1,2：复用一阶/二阶专用差分公式（c_k = f^(k)/k!） */
    if (K >= 1) {
        d1 = ac_calc_derivative(x0);
        iter_total = iter_total + g_calc_iterations;
        if (d1 != d1) { return -1; }           /* g_calc_error 已由内部置位 */
        g_taylor_coeffs[1] = d1;
    }
    if (K >= 2) {
        d2 = ac_calc_derivative2(x0);
        iter_total = iter_total + g_calc_iterations;
        if (d2 != d2) { return -1; }
        g_taylor_coeffs[2] = d2 * 0.5;
    }

    /* k≥3：中心差分表（原地逐层前向差分，取中心/跨中心元素） */
    if (K >= 3) {
        h = pow(2.22e-16, 1.0 / (double)(K + 2)) * scale;
        for (j = 0; j <= 2 * K; j++) {
            yj = calc_eval(x0 + (double)(j - K) * h);
            iter_total++;
            if (g_eval_error != 0 || yj != yj) { g_calc_error = 1; return -1; }
            s_taylor_work[j] = yj;
        }
        fact = 2.0;                            /* 进入 k=3 时乘 3 得 3! */
        for (k = 1; k <= K; k++) {
            for (t = 0; t <= 2 * K - k; t++) {
                s_taylor_work[t] = s_taylor_work[t + 1] - s_taylor_work[t];
            }
            if (k >= 3) {
                if (k % 2 == 0) {
                    der = s_taylor_work[K - k / 2];         /* Δ^k f 的中点恰为 x0 */
                } else {
                    s = K - (k + 1) / 2;                    /* 相邻两中点 x0∓h/2，平均回 x0 */
                    der = (s_taylor_work[s] + s_taylor_work[s + 1]) * 0.5;
                }
                fact = fact * (double)k;
                g_taylor_coeffs[k] = der / (pow(h, (double)k) * fact);
            }
        }
    }

    g_calc_iterations = iter_total;
    g_calc_error = 0;
    return 0;
}

/* 用缓存系数在 x 处求泰勒多项式值（Horner 法，纯标量） */
double ac_taylor_eval(double x) {
    double t = x - g_taylor_center;
    double sum = g_taylor_coeffs[g_taylor_order];
    int k;
    for (k = g_taylor_order - 1; k >= 0; k--) {
        sum = sum * t + g_taylor_coeffs[k];
    }
    return sum;
}
