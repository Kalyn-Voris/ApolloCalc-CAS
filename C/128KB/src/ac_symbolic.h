#ifndef AC_SYMBOLIC_H
#define AC_SYMBOLIC_H

#include "ac_types.h"

/* ============================================================
 * ApolloCalc-CAS · ac_symbolic.h — 符号求导 + 因式分解
 * （C 128KB 第 10 轮，C 独有；依赖第 1 轮五分量 + 第 5 轮有理根）
 * ----
 * 表达式树：扁平数组 + 索引（无指针、无 malloc）。求导向 g_sym_nodes
 * 追加新节点（原树 [0..count) 保持不变——append-only 契约，测试钉住）。
 * 求导与求值均为显式栈后序遍历（标记法），零递归。
 * 化简：ac_sym_new_pow 做 u^1→u、u^0→1（含 0^0=1，与 IEEE pow(0,0) 一致）；
 *   导数节点构造再做 0+x→x、0·x→0、1·x→x（x−0→x）。
 *   幂法则经 ac_sym_new_pow 建 u^(n-1)，故 d(x^1)→CONST 1、d(x^0)→CONST 0
 *   直接坍缩，不残留 POW(x,0)。
 * 幂法则用原始基节点：d(u^n) = n·u^(n-1)·du（u 取原索引，du 取子树导数）。
 * ----
 * 因式分解：复用第 5 轮 ac_rat_root_descent（有理根定理 + 综合除法 +
 * 显式栈降次）。注意：会改写 ac_solvers 的 g_coeffs（降次提交副作用）。
 * ============================================================ */

#define NODE_CONST 0
#define NODE_VAR   1
#define NODE_ADD   2
#define NODE_SUB   3
#define NODE_MUL   4
#define NODE_POW   5

#define AC_SYM_MAX_NODES 512

typedef struct {
    int8_t   type;     /* NODE_* */
    int8_t   pad[3];
    int32_t  left;     /* 左子节点索引，叶子为 -1 */
    int32_t  right;    /* 右子节点索引（NODE_POW 指向常量节点），叶子 -1 */
    ac_int_t value;    /* 叶子常量值 / POW 的指数快照 */
} ac_sym_node_t;

extern ac_sym_node_t g_sym_nodes[AC_SYM_MAX_NODES];
extern int           g_sym_count;   /* 当前节点数 */

/* [English] Factorization output (per rational root, with multiplicity;
   five-component exact form per root — rational roots have rad_num = 0,
   rad = 1, exp = 0). Remainder = depressed integer polynomial after all
   rational roots are stripped; fully-factored input reports degree 0
   with coeffs[0] = 1. */
extern int    g_sym_factor_count;
extern ac_int_t g_sym_factor_rat[8];
extern ac_int_t g_sym_factor_rad_num[8];
extern ac_int_t g_sym_factor_rad[8];
extern ac_int_t g_sym_factor_den[8];
extern int      g_sym_factor_exp[8];
extern int    g_sym_remainder_degree;
extern double g_sym_remainder_coeffs[8];

/* [English] Reset the node arena (count = 0). */
void ac_sym_reset(void);

/* [English] Node constructors. Return node index, or -1 when the arena
   is full. ac_sym_new_pow builds base^n with an internal const node, after
   the spec simplifications u^1 -> u (returns base unchanged) and
   u^0 -> 1 (returns a fresh CONST 1 node; 0^0 = 1, matching IEEE pow). */
int ac_sym_new_const(ac_int_t v);
int ac_sym_new_var(void);
int ac_sym_new_add(int l, int r);
int ac_sym_new_sub(int l, int r);
int ac_sym_new_mul(int l, int r);
int ac_sym_new_pow(int base, ac_int_t n);

/* [English] Build Σ coeffs[i]·x^i (ascending, zero coefficients skipped;
   all-zero builds CONST 0). degree <= 32 recommended (arena budget).
   Returns root index or -1. */
int ac_sym_from_poly(const ac_int_t *coeffs, int degree);

/* [English] Symbolic derivative. Appends the derivative tree to the arena
   (original tree untouched) and returns its root index, or -1 on
   overflow / invalid root. Explicit-stack post-order, no recursion. */
int ac_sym_derivative(int root);

/* [English] Evaluate the tree at x (explicit stack, no recursion). */
double ac_sym_eval(int root, double x);

/* [English] Structural equality (type-recursive, iterative). */
int ac_sym_equal(int a, int b);

/* [English] Factor an integer polynomial (ascending coeffs[0..degree])
 * into rational factors + depressed remainder, reusing ac_ratroot.
 * degree must be 1..4. CLOBBERS ac_solvers' g_coeffs (descent commits).
 * Returns the number of rational factors (0 = irreducible over Q),
 * or -1 on invalid degree. */
int ac_sym_factor_poly(const double *coeffs, int degree);

#endif /* AC_SYMBOLIC_H */
