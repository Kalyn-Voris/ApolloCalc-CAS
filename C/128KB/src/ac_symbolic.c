/* ============================================================
 * ApolloCalc-CAS · ac_symbolic.c — 符号求导 + 因式分解
 * （C 128KB 第 10 轮，C 独有）
 * 表达式树：扁平数组 + 索引寻址（无指针追踪、无 malloc）。
 * 求导/求值/相等比较：全部显式栈后序遍历（标记编码 nd + MAX_NODES），
 * 零递归。求导 append-only：原树 [0..count) 不变（测试钉住）。
 * 幂法则：d(u^n) = n·u^(n−1)·du —— u^(n−1) 用原始基节点索引，
 * du 用子树导数（首轮实现误用 du 于 u 槽，被 d(x³)=3x³ 检出）。
 * 化简：ac_sym_new_pow 做 u^1→u、u^0→1（含 0^0=1，与 IEEE pow(0,0) 一致）；
 * 导数节点构造再做 0+x→x、x−0→x、0·x→0、1·x→x。幂法则经 ac_sym_new_pow
 * 建 u^(n−1)，故 d(x^1)→CONST 1、d(x^0)→CONST 0 直接坍缩，不残留 POW(x,0)。
 * Zero-GC：无 malloc、无 VLA、无递归；栈/dmap/值表全 static 预分配。
 * ============================================================ */
#include "ac_symbolic.h"
#include "ac_solvers.h"
#include "ac_ratroot.h"
#include "ac_exact.h"
#include <math.h>

/* ==================== 全局状态 + 模块工作区 ==================== */

ac_sym_node_t g_sym_nodes[AC_SYM_MAX_NODES];
int           g_sym_count = 0;

static int32_t s_sym_stack[AC_SYM_MAX_NODES * 2 + 8];  /* 遍历栈（节点压两次） */
static int32_t s_sym_dmap[AC_SYM_MAX_NODES];           /* 原节点 → 导数节点 */
static double  s_sym_val[AC_SYM_MAX_NODES];            /* 求值结果表 */
static int32_t s_sym_pa[AC_SYM_MAX_NODES];             /* equal 比较栈 A 侧 */
static int32_t s_sym_pb[AC_SYM_MAX_NODES];             /* equal 比较栈 B 侧 */

int    g_sym_factor_count = 0;
ac_int_t g_sym_factor_rat[8];
ac_int_t g_sym_factor_rad_num[8];
ac_int_t g_sym_factor_rad[8];
ac_int_t g_sym_factor_den[8];
int      g_sym_factor_exp[8];
int    g_sym_remainder_degree = 0;
double g_sym_remainder_coeffs[8];

/* ==================== 节点构造 ==================== */

void ac_sym_reset(void) {
    g_sym_count = 0;
}

static int sym_new(int8_t type, int32_t left, int32_t right, ac_int_t value) {
    if (g_sym_count >= AC_SYM_MAX_NODES) { return -1; }
    g_sym_nodes[g_sym_count].type = type;
    g_sym_nodes[g_sym_count].pad[0] = 0;
    g_sym_nodes[g_sym_count].pad[1] = 0;
    g_sym_nodes[g_sym_count].pad[2] = 0;
    g_sym_nodes[g_sym_count].left = left;
    g_sym_nodes[g_sym_count].right = right;
    g_sym_nodes[g_sym_count].value = value;
    return g_sym_count++;
}

int ac_sym_new_const(ac_int_t v) { return sym_new(NODE_CONST, -1, -1, v); }
int ac_sym_new_var(void)         { return sym_new(NODE_VAR, -1, -1, 0); }
int ac_sym_new_add(int l, int r) { return sym_new(NODE_ADD, l, r, 0); }
int ac_sym_new_sub(int l, int r) { return sym_new(NODE_SUB, l, r, 0); }
int ac_sym_new_mul(int l, int r) { return sym_new(NODE_MUL, l, r, 0); }

int ac_sym_new_pow(int base, ac_int_t n) {
    int cn;
    int p;
    /* 基础化简（规格）：u^1 → u、u^0 → 1。
       指数在本 API 中恒为常量（按值传入），故不存在"变量指数不可化简"分支。
       n == 0 无条件返回 CONST 1（含 0^0 = 1，与 IEEE pow(0,0) 一致）。
       不删除已有节点，保持 append-only。 */
    if (n == 1) { return base; }
    if (n == 0) { return sym_new(NODE_CONST, -1, -1, 1); }
    cn = sym_new(NODE_CONST, -1, -1, n);
    if (cn < 0) { return -1; }
    p = sym_new(NODE_POW, base, cn, n);   /* value 快照指数（求导免二次读子） */
    return p;
}

/* ==================== 多项式 → 树 ==================== */

int ac_sym_from_poly(const ac_int_t *coeffs, int degree) {
    int t;
    int i;
    if (degree < 0) { return -1; }
    /* 从最高次向最低次折叠 ADD；零系数跳过（全零 → CONST 0） */
    t = -1;
    for (i = degree; i >= 0; i--) {
        int term;
        if (coeffs[i] == 0) { continue; }
        if (i == 0) {
            term = sym_new(NODE_CONST, -1, -1, coeffs[0]);
        } else {
            int pw = (i == 1) ? ac_sym_new_var() : ac_sym_new_pow(ac_sym_new_var(), i);
            if (pw < 0) { return -1; }
            if (coeffs[i] == 1) { term = pw; }
            else {
                int c = sym_new(NODE_CONST, -1, -1, coeffs[i]);
                if (c < 0) { return -1; }
                term = sym_new(NODE_MUL, c, pw, 0);
            }
            if (term < 0) { return -1; }
        }
        if (term < 0) { return -1; }
        t = (t < 0) ? term : sym_new(NODE_ADD, t, term, 0);
        if (t < 0) { return -1; }
    }
    if (t < 0) { t = sym_new(NODE_CONST, -1, -1, 0); }
    return t;
}

/* ==================== 符号求导（显式栈后序） ==================== */

/* 导数节点构造（带基础化简）；子节点导数索引已就绪 */
static int sym_dmake_add(int l, int r) {
    if (g_sym_nodes[l].type == NODE_CONST && g_sym_nodes[l].value == 0) { return r; }
    if (g_sym_nodes[r].type == NODE_CONST && g_sym_nodes[r].value == 0) { return l; }
    return sym_new(NODE_ADD, l, r, 0);
}

static int sym_dmake_sub(int l, int r) {
    if (g_sym_nodes[r].type == NODE_CONST && g_sym_nodes[r].value == 0) { return l; }
    return sym_new(NODE_SUB, l, r, 0);
}

static int sym_dmake_mul(int l, int r) {
    if (g_sym_nodes[l].type == NODE_CONST && g_sym_nodes[l].value == 0) {
        return sym_new(NODE_CONST, -1, -1, 0);
    }
    if (g_sym_nodes[r].type == NODE_CONST && g_sym_nodes[r].value == 0) {
        return sym_new(NODE_CONST, -1, -1, 0);
    }
    if (g_sym_nodes[l].type == NODE_CONST && g_sym_nodes[l].value == 1) { return r; }
    if (g_sym_nodes[r].type == NODE_CONST && g_sym_nodes[r].value == 1) { return l; }
    return sym_new(NODE_MUL, l, r, 0);
}

int ac_sym_derivative(int root) {
    int sp = 0;
    int budget;

    if (root < 0 || root >= g_sym_count) { return -1; }
    /* 容量预算：导数节点数 ≤ 4×原树；不足即早退（免中途 -1 撕裂半棵树） */
    budget = AC_SYM_MAX_NODES - g_sym_count;
    if (budget < (g_sym_count - root) * 4 + 8) { return -1; }

    s_sym_stack[sp++] = root;
    while (sp > 0) {
        int nd = s_sym_stack[--sp];
        if (nd >= AC_SYM_MAX_NODES) {
            /* ---- 后序标记：子节点导数已就绪，构造本节点导数 ---- */
            int n = nd - AC_SYM_MAX_NODES;
            int d;
            switch (g_sym_nodes[n].type) {
            case NODE_CONST:
                d = sym_new(NODE_CONST, -1, -1, 0);
                break;
            case NODE_VAR:
                d = sym_new(NODE_CONST, -1, -1, 1);
                break;
            case NODE_ADD:
                d = sym_dmake_add(s_sym_dmap[g_sym_nodes[n].left],
                                  s_sym_dmap[g_sym_nodes[n].right]);
                break;
            case NODE_SUB:
                d = sym_dmake_sub(s_sym_dmap[g_sym_nodes[n].left],
                                  s_sym_dmap[g_sym_nodes[n].right]);
                break;
            case NODE_MUL:
                d = sym_dmake_add(
                    sym_dmake_mul(s_sym_dmap[g_sym_nodes[n].left],
                                  g_sym_nodes[n].right),
                    sym_dmake_mul(g_sym_nodes[n].left,
                                  s_sym_dmap[g_sym_nodes[n].right]));
                break;
            case NODE_POW: {
                /* d(u^n) = n·u^(n−1)·du（u = 原始基节点索引）
                   u^(n−1) 走 ac_sym_new_pow，继承 u^1→u / u^0→1 化简：
                   d(x^1) 与 d(x^0) 因此坍缩为常量，不残留 POW(x,0)。 */
                ac_int_t nv = g_sym_nodes[n].value;
                int dl = s_sym_dmap[g_sym_nodes[n].left];
                int pw;
                int inner;
                int cn;
                if (nv < 0) { return -1; }   /* 负指数不在契约内 */
                pw = ac_sym_new_pow(g_sym_nodes[n].left, nv - 1);
                if (pw < 0) { return -1; }
                inner = sym_dmake_mul(pw, dl);
                if (inner < 0) { return -1; }
                cn = sym_new(NODE_CONST, -1, -1, nv);
                if (cn < 0) { return -1; }
                d = sym_dmake_mul(cn, inner);
                break;
            }
            default:
                return -1;
            }
            if (d < 0) { return -1; }
            s_sym_dmap[n] = d;
        } else {
            /* ---- 先序：压后序标记 + 子节点 ---- */
            int t = g_sym_nodes[nd].type;
            s_sym_stack[sp++] = nd + AC_SYM_MAX_NODES;
            if (t == NODE_ADD || t == NODE_SUB || t == NODE_MUL) {
                s_sym_stack[sp++] = g_sym_nodes[nd].left;
                s_sym_stack[sp++] = g_sym_nodes[nd].right;
            } else if (t == NODE_POW) {
                s_sym_stack[sp++] = g_sym_nodes[nd].left;   /* 只导基；指数是常量 */
            }
        }
    }
    return s_sym_dmap[root];
}

/* ==================== 求值（显式栈后序） ==================== */

double ac_sym_eval(int root, double x) {
    int sp = 0;
    if (root < 0 || root >= g_sym_count) { return (double)NAN; }
    s_sym_stack[sp++] = root;
    while (sp > 0) {
        int nd = s_sym_stack[--sp];
        if (nd >= AC_SYM_MAX_NODES) {
            int n = nd - AC_SYM_MAX_NODES;
            switch (g_sym_nodes[n].type) {
            case NODE_CONST:
                s_sym_val[n] = (double)g_sym_nodes[n].value;
                break;
            case NODE_VAR:
                s_sym_val[n] = x;
                break;
            case NODE_ADD:
                s_sym_val[n] = s_sym_val[g_sym_nodes[n].left] +
                               s_sym_val[g_sym_nodes[n].right];
                break;
            case NODE_SUB:
                s_sym_val[n] = s_sym_val[g_sym_nodes[n].left] -
                               s_sym_val[g_sym_nodes[n].right];
                break;
            case NODE_MUL:
                s_sym_val[n] = s_sym_val[g_sym_nodes[n].left] *
                               s_sym_val[g_sym_nodes[n].right];
                break;
            case NODE_POW:
                s_sym_val[n] = pow(s_sym_val[g_sym_nodes[n].left],
                                   (double)g_sym_nodes[n].value);
                break;
            default:
                s_sym_val[n] = (double)NAN;
                break;
            }
        } else {
            int t = g_sym_nodes[nd].type;
            s_sym_stack[sp++] = nd + AC_SYM_MAX_NODES;
            if (t == NODE_ADD || t == NODE_SUB || t == NODE_MUL) {
                s_sym_stack[sp++] = g_sym_nodes[nd].left;
                s_sym_stack[sp++] = g_sym_nodes[nd].right;
            } else if (t == NODE_POW) {
                s_sym_stack[sp++] = g_sym_nodes[nd].left;
                s_sym_stack[sp++] = g_sym_nodes[nd].right;   /* 求值需要指数节点值 */
            }
        }
    }
    return s_sym_val[root];
}

/* ==================== 结构相等（显式栈成对比较） ==================== */

int ac_sym_equal(int a, int b) {
    int sp = 0;
    if (a < 0 || a >= g_sym_count || b < 0 || b >= g_sym_count) { return 0; }
    s_sym_pa[sp] = a;
    s_sym_pb[sp] = b;
    sp++;
    while (sp > 0) {
        sp--;
        a = s_sym_pa[sp];
        b = s_sym_pb[sp];
        if (a < 0 || b < 0) { return (a < 0) && (b < 0) ? 1 : 0; }
        if (g_sym_nodes[a].type != g_sym_nodes[b].type) { return 0; }
        if (g_sym_nodes[a].type == NODE_CONST &&
            g_sym_nodes[a].value != g_sym_nodes[b].value) { return 0; }
        if (g_sym_nodes[a].type >= NODE_ADD) {
            if (sp + 2 > AC_SYM_MAX_NODES) { return 0; }
            s_sym_pa[sp] = g_sym_nodes[a].left;
            s_sym_pb[sp] = g_sym_nodes[b].left;
            sp++;
            s_sym_pa[sp] = g_sym_nodes[a].right;
            s_sym_pb[sp] = g_sym_nodes[b].right;
            sp++;
        }
    }
    return 1;
}

/* ==================== 因式分解（复用 ac_ratroot） ==================== */

int ac_sym_factor_poly(const double *coeffs, int degree) {
    int k;
    int i;
    int dd;

    g_sym_factor_count = 0;
    g_sym_remainder_degree = 0;
    for (i = 0; i < 8; i++) {
        g_sym_factor_rat[i] = 0;
        g_sym_factor_rad_num[i] = 0;
        g_sym_factor_rad[i] = 1;
        g_sym_factor_den[i] = 1;
        g_sym_factor_exp[i] = 0;
        g_sym_remainder_coeffs[i] = 0.0;
    }
    if (degree < 1 || degree > 4) { return -1; }

    /* 系数进 g_coeffs（descent 的读写场）；ac_ratroot 逐根降次提交 */
    for (i = 0; i <= degree; i++) { g_coeffs[i] = coeffs[i]; }
    for (i = degree + 1; i < 5; i++) { g_coeffs[i] = 0.0; }

    k = ac_rat_root_descent(degree);
    dd = degree - k;
    g_sym_factor_count = k;

    /* 有理根 → 五分量精确形式（经 ac_exact_set_fraction 规范化） */
    for (i = 0; i < k; i++) {
        ac_exact_set_fraction(g_rat_root_num[i], g_rat_root_den[i]);
        g_sym_factor_rat[i] = g_exact_rat;
        g_sym_factor_rad_num[i] = g_exact_rad_num;
        g_sym_factor_rad[i] = g_exact_rad;
        g_sym_factor_den[i] = g_exact_den;
        g_sym_factor_exp[i] = g_exact_exp;
    }

    /* 剩余多项式：dd = 0 → 常量 1（完全分解约定）；否则取降次提交结果 */
    g_sym_remainder_degree = dd;
    if (dd == 0) {
        g_sym_remainder_coeffs[0] = 1.0;
    } else {
        for (i = 0; i <= dd; i++) { g_sym_remainder_coeffs[i] = g_coeffs[i]; }
    }
    return k;
}
