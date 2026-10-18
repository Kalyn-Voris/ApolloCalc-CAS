/* ============================================================
 * ApolloCalc-CAS · ac_ratroot.c — 有理根检验与降次（C 128KB 第 5 轮）
 * 行为基准：JS/128KB/src/core_ratroot.js（文件 9），逐函数逐分支对齐。
 * 降次用显式栈（根栈 0..8），循环不变式：每轮要么消去一个根
 * （零根剥离 / 候选命中 / 线性吸收），要么 break 交还原求解器——
 * 无递归、无函数自调用。
 * Zero-GC：无 malloc、无 VLA、无递归；中间量全部为预分配 static 数组。
 * ============================================================ */
#include "ac_ratroot.h"
#include "ac_exact.h"
#include "ac_solvers.h"

/* ==================== 模块状态（唯一声明处） ==================== */

static int32_t s_rr_cand[64];        /* 候选根缓存：p/q 交错存放（至多 32 组） */
static int     s_rr_count = 0;       /* 最近一次枚举生成的候选数 */
static double  s_rr_work[16];        /* 降次工作区：当前整系数多项式（升幂） */
static double  s_rr_trial[16];       /* 综合除法试除区（失败即弃，不污染 work） */
static int     s_rr_divs[128];       /* 常数项 |a0| 的因数表（试除到 √） */

double   g_rat_root_values[8];       /* 根值（发现顺序，栈） */
ac_int_t g_rat_root_num[8];          /* 精确形式分子 p */
ac_int_t g_rat_root_den[8];          /* 精确形式分母 q（>0，已约简） */
int      g_rat_root_found = 0;       /* 实际找到的根数（显式栈深度，0..8） */

/* 整数判定与枚举上限：采样误差容差 1e-6（相对）；|an|/|a0| 超过 1e7 跳过枚举 */
#define RR_INT_TOL    1e-6
#define RR_ENUM_MAX   10000000

/* ==================== 模块一：基础工具 ==================== */

/* 欧几里得 gcd（迭代，无递归；入参非负） */
static int rr_gcd(int a, int b) {
    int t;
    while (b != 0) {
        t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* 显式栈入栈：根值 p/q（自动约简、分母翻正）；栈满（8）丢弃 */
static void rr_push(int p, int q) {
    int g;
    if (g_rat_root_found >= 8) { return; }
    if (q < 0) { p = -p; q = -q; }
    g = rr_gcd(p < 0 ? -p : p, q);
    if (g > 1) { p = p / g; q = q / g; }
    g_rat_root_values[g_rat_root_found] = (double)p / (double)q;
    g_rat_root_num[g_rat_root_found] = (ac_int_t)p;
    g_rat_root_den[g_rat_root_found] = (ac_int_t)q;
    g_rat_root_found++;
}

/* 工作区尺度：max |work[0..cd]|（整系数快照保证 ≥ 1） */
static double rr_scale(int cd) {
    double mx = 1.0;
    int k;
    for (k = 0; k <= cd; k++) {
        double a = s_rr_work[k] < 0.0 ? -s_rr_work[k] : s_rr_work[k];
        if (a > mx) { mx = a; }
    }
    return mx;
}

/* ==================== 模块二：整系数快照与提交 ==================== */

/* 快照：g_coeffs[0..cd] 逐项近整判定后舍入存入 work；
 * 任一系数非近整（非整系数多项式）返回 -1，调用方放弃降次走原路径 */
static int rr_snap(int cd) {
    int k;
    for (k = 0; k <= cd; k++) {
        double c = g_coeffs[k];
        double r;
        double diff;
        double sc;
        if (c != c || c == (double)INFINITY || c == -(double)INFINITY) { return -1; }
        r = floor(c + 0.5);   /* 正负对称的 Math.round（0.5 远离零） */
        diff = c - r;
        if (diff < 0.0) { diff = -diff; }
        sc = c < 0.0 ? -c : c;
        if (sc < 1.0) { sc = 1.0; }
        if (diff > RR_INT_TOL * sc) { return -1; }
        s_rr_work[k] = r;
    }
    return 0;
}

/* 提交：work[0..cd]（近整）舍入写回 g_coeffs[0..cd]，其余槽清零
 * （g_coeffs 共 5 槽：多项式最高 4 次；降次后的尾槽清零防低次求解器误读） */
static void rr_commit(int cd) {
    int k;
    for (k = 0; k <= cd; k++) {
        g_coeffs[k] = floor(s_rr_work[k] + 0.5);
    }
    for (k = cd + 1; k < 5; k++) {
        g_coeffs[k] = 0.0;
    }
}

/* ==================== 模块三：候选枚举 ==================== */

/* 因数表：|m| 的全部因数存入 divs（试除到 √m），返回个数；表满返回 -1 */
static int rr_divisors(int m) {
    int d = m < 0 ? -m : m;
    int cnt = 0;
    int i = 1;
    while (i * i <= d) {           /* d ≤ 1e7 → i ≤ 3163，i*i 不溢出 int */
        if (d % i == 0) {
            if (cnt >= 128) { return -1; }
            s_rr_divs[cnt++] = i;
            if (i != d / i) {
                if (cnt >= 128) { return -1; }
                s_rr_divs[cnt++] = d / i;
            }
        }
        i++;
    }
    return cnt;
}

/* 候选入表（约简 + 线性去重）；候选满 32 组即止 */
static void rr_try_add(int p, int q) {
    int g;
    int ci;
    if (q < 0) { p = -p; q = -q; }
    g = rr_gcd(p < 0 ? -p : p, q);
    if (g > 1) { p = p / g; q = q / g; }
    for (ci = 0; ci < s_rr_count; ci++) {
        if (s_rr_cand[ci * 2] == p && s_rr_cand[ci * 2 + 1] == q) { return; }
    }
    if (s_rr_count >= 32) { return; }
    s_rr_cand[s_rr_count * 2] = p;
    s_rr_cand[s_rr_count * 2 + 1] = q;
    s_rr_count++;
}

/* 单个 q 的候选全组：±p/q（p 遍历因数表） */
static void rr_add_cands(int q, int dc) {
    int di;
    for (di = 0; di < dc; di++) {
        rr_try_add(s_rr_divs[di], q);
        rr_try_add(-s_rr_divs[di], q);
    }
}

/* 枚举 an、a0 的全部因数组合 p/q（p|a0、q|an），填候选表；
 * 超上限（|an|/|a0| > 1e7 或因数表溢出）返回 0 = 放弃本轮枚举 */
static int rr_enumerate(int an, int a0) {
    int n = an < 0 ? -an : an;
    int m = a0 < 0 ? -a0 : a0;
    int dc;
    int i = 1;
    s_rr_count = 0;
    if (n < 1 || m < 1 || n > RR_ENUM_MAX || m > RR_ENUM_MAX) { return 0; }
    dc = rr_divisors(m);
    if (dc < 0) { return 0; }
    while (i * i <= n) {
        if (n % i == 0) {
            rr_add_cands(i, dc);
            if (n / i != i) { rr_add_cands(n / i, dc); }
        }
        i++;
    }
    return s_rr_count;
}

/* ==================== 模块四：综合除法（升幂布局） ====================
 * P(x) = (x−r)·Q(x) + rem，Q 升幂落 trial[1..cd]：
 *   q_{cd-1} = c_cd；q_{k-1} = c_k + r·q_k（k = cd−1..1）；rem = c_0 + r·q_0
 * 试除区失败即弃（work 不动），余项返回给调用方判定 */
static double rr_trial_divide(int cd, double r) {
    int k;
    for (k = 0; k <= cd; k++) {
        s_rr_trial[k] = s_rr_work[k];
    }
    for (k = cd - 1; k >= 1; k--) {
        s_rr_trial[k] = s_rr_trial[k] + r * s_rr_trial[k + 1];
    }
    return s_rr_trial[0] + r * s_rr_trial[1];
}

/* ==================== 模块五：主入口（迭代降次，显式栈） ==================== */

int ac_rat_root_descent(int degree) {
    int cd;
    int guard = 0;

    g_rat_root_found = 0;
    s_rr_count = 0;
    if (degree < 1 || degree > 4) { return 0; }
    cd = degree;
    while (cd >= 1 && guard < 8) {
        int nc;
        double scale;
        int hit = 0;
        int ci;
        guard++;
        if (rr_snap(cd) != 0) { break; }          /* 非整系数：交还原求解器 */
        if (s_rr_work[cd] == 0.0) { break; }      /* 首项舍入为零：次数实低，不处理 */

        if (s_rr_work[0] == 0.0) {
            /* 零根剥离：x=0 是根，整体下移一次降次 */
            int s;
            rr_push(0, 1);
            for (s = 0; s < cd; s++) { s_rr_work[s] = s_rr_work[s + 1]; }
            cd--;
            rr_commit(cd);
            continue;
        }
        if (cd == 1) {
            /* 线性吸收：root = −c0/c1（整系数下必为有理根，分数精确） */
            rr_push((int)(-s_rr_work[0]), (int)s_rr_work[1]);
            cd = 0;
            rr_commit(0);
            continue;                             /* cd=0 → 循环退出 */
        }

        /* 候选枚举 + 综合除法逐个验证（首个命中即提交、重启枚举） */
        nc = rr_enumerate((int)s_rr_work[cd], (int)s_rr_work[0]);
        scale = rr_scale(cd);
        for (ci = 0; ci < nc && hit == 0; ci++) {
            int p = s_rr_cand[ci * 2];
            int q = s_rr_cand[ci * 2 + 1];
            double r = (double)p / (double)q;
            double rem = rr_trial_divide(cd, r);
            double arem = rem < 0.0 ? -rem : rem;
            if (arem > RR_INT_TOL * scale) { continue; }   /* 余项判定：非根 */
            /* 商近整复核（高斯引理；防裕度内假阳性根污染系数） */
            {
                int ok = 1;
                int k;
                for (k = 1; k <= cd; k++) {
                    double c = s_rr_trial[k];
                    double rr = floor(c + 0.5);
                    double df = c - rr;
                    double scc = c < 0.0 ? -c : c;
                    if (df < 0.0) { df = -df; }
                    if (scc < 1.0) { scc = 1.0; }
                    if (df > RR_INT_TOL * scc) { ok = 0; break; }
                }
                if (ok == 0) { continue; }
            }
            /* 接受：商左移入工作区，降次提交 */
            rr_push(p, q);
            {
                int k;
                for (k = 0; k < cd; k++) { s_rr_work[k] = s_rr_trial[k + 1]; }
            }
            cd--;
            rr_commit(cd);
            hit = 1;
        }
        if (hit == 0) { break; }                  /* 候选全部落空：剩余部分无有理根 */
    }
    return g_rat_root_found;
}

/* ==================== 模块六：收尾合并（供 solveCubic/solveQuartic 调用） ====================
 * 栈中 k 个有理根排在结果前段（exactSetFraction 五分量精确形式），
 * 剩余 dd = degree−k 次多项式（已在 g_coeffs）交低次求解器，
 * 其根整体右移 k 位拼接（倒序复制防覆盖）：
 *   dd=0：全有理，直接填栈；dd≤2：二次求解器（含 dd=1 防御：首项零退化线性）；
 *   dd=3：三次核（skipRational=1，降次循环已证明无剩余有理根） */

int ac_rat_root_finish(int k, int degree) {
    int dd = degree - k;
    int m;
    int i;

    if (dd >= 1 && dd <= 2) {
        ac_solve_quadratic();
        if (g_solve_error != 0) { return g_solve_error; }
    } else if (dd == 3) {
        ac_solve_cubic(1);
        if (g_solve_error != 0) { return g_solve_error; }
    }

    m = g_root_count;                             /* 子求解器根数（dd=0 时为 0） */
    /* 子求解器根整体右移 k 位（倒序复制防覆盖），有理根（栈序）占前段 */
    for (i = m - 1; i >= 0; i--) {
        g_root_re[i + k] = g_root_re[i];
        g_root_im[i + k] = g_root_im[i];
        g_root_exact_rat[i + k] = g_root_exact_rat[i];
        g_root_exact_rad_num[i + k] = g_root_exact_rad_num[i];
        g_root_exact_rad[i + k] = g_root_exact_rad[i];
        g_root_exact_den[i + k] = g_root_exact_den[i];
        g_root_exact_exp[i + k] = g_root_exact_exp[i];
        g_root_exact_valid[i + k] = g_root_exact_valid[i];
    }
    for (i = 0; i < k; i++) {
        g_root_re[i] = g_rat_root_values[i];
        g_root_im[i] = 0.0;
        ac_exact_set_fraction(g_rat_root_num[i], g_rat_root_den[i]);
        g_root_exact_valid[i] = g_exact_valid;
        g_root_exact_rat[i] = g_exact_rat;
        g_root_exact_rad_num[i] = g_exact_rad_num;
        g_root_exact_rad[i] = g_exact_rad;
        g_root_exact_den[i] = g_exact_den;
        g_root_exact_exp[i] = g_exact_exp;
    }
    g_root_count = m + k;
    return 0;
}
