/* ApolloCalc · ac_pro_integrate.c — 积分表匹配 + 固定两层分部积分

   本文件的文件级对象必须全是 static const:Pro 层的 .bss 账本是
   38672 + 80 + 2688 + 16 = 41456,没有余量。实测(gcc 8.1.0 / MinGW-w64 ld)
   任何一个非 const 的 static 对象都会让本 TU 产生 .bss 段,叠加链接期对齐
   后全程序 .bss 净涨 32 B,门禁直接失败。错误码走返回值,不设 static error 槽。
   自检:gcc ... -c src/ac_pro_integrate.c -o x.o && size x.o  -> bss 必须 0

   节点预算:上游 g_sym_nodes arena(512)被 6 个上游 TU 共享,本文件只读不建
   (不调用任何 ac_sym_new_*),对 arena 的写入量恒为 0。但预算仍按真实消耗查:
   表匹配路径 ≤ 256 节点;分部积分固定两层(v2.3.0 功能 4),每层额外 ≤ 96
   节点,即 256 + 2×96 = 448,用的是本模块实际占用的 IR 节点数 P.count,
   并同时校验 base + need ≤ 512。超限返回 AC_PRO_INT_EBUDGET。

   「固定两层」与禁递归红线(CONTRIBUTING.md:62)的关系:分部尝试的层数
   parts_left 是硬编码上界 2 的递减参数,外层调内层时严格 −1,到 0 关死;
   不存在同形自调用,也不存在层数随输入增长的通路,等价于把两层展开写成
   两个函数,只是共享同一份匹配体。深度另受 PRO_INT_MAX_DEPTH 双重封顶。 */

#include <string.h>

#include "ac_pro_config.h"
#include "ac_pro.h"

#if AC_PRO_ACTIVE

#define PRO_INT_BUDGET_TABLE   256
#define PRO_INT_BUDGET_PARTS    96   /* 每层分部的额外预算（× 层数） */
#define PRO_INT_PARTS_LAYERS     2   /* 固定两层，硬编码（v2.3.0 功能 4） */
#define PRO_INT_BUDGET_TOTAL   (PRO_INT_BUDGET_TABLE + \
                                PRO_INT_BUDGET_PARTS * PRO_INT_PARTS_LAYERS)

/* 预算值写错就编译失败。 */
AC_STATIC_ASSERT(AC_SYM_MAX_NODES == 512, pro_int_arena_is_512_nodes);
AC_STATIC_ASSERT(PRO_INT_BUDGET_TABLE == 256, pro_int_table_budget_is_256);
AC_STATIC_ASSERT(PRO_INT_BUDGET_PARTS == 96, pro_int_parts_budget_is_96);
AC_STATIC_ASSERT(PRO_INT_PARTS_LAYERS == 2, pro_int_parts_layers_is_2);
AC_STATIC_ASSERT(PRO_INT_BUDGET_TOTAL == 448, pro_int_total_budget_is_448);
AC_STATIC_ASSERT(PRO_INT_BUDGET_TOTAL <= AC_SYM_MAX_NODES,
                 pro_int_budgets_fit_arena);

/* 以下都是栈上局部量的尺寸上限，不落 .bss。 */
#define PRO_INT_POOL_NODES      512   /* > 448，够固定两层分部路径用 */
#define PRO_INT_MAX_DEPTH       128
#define PRO_INT_OUTBUF          768
#define PRO_INT_MAX_POW_IN       20   /* 超过就不走分部积分 */
#define PRO_INT_MAX_POW_PARSE    96   /* 输入指数超过即 EDEGREE */
#define PRO_INT_MAX_POW_BUILD    128  /* 模板的 n+1 要比 PARSE 留更多余量 */
#define PRO_INT_MAX_CONST   1000000L

#define PRO_INT_OP_CONST 0
#define PRO_INT_OP_VAR   1
#define PRO_INT_OP_ADD   2
#define PRO_INT_OP_SUB   3
#define PRO_INT_OP_MUL   4
#define PRO_INT_OP_DIV   5
#define PRO_INT_OP_POW   6
#define PRO_INT_OP_NEG   7
#define PRO_INT_OP_FUNC  8

/* 输入只允许 x；u/a/b/n/m 只出现在原函数模板里（a 线性系数、b 常数项、
 * n 幂次或常量值、m=n² 类伴生常量），由 pro_int_bind_t 代换。 */
#define PRO_INT_V_X  0
#define PRO_INT_V_U  1
#define PRO_INT_V_A  2
#define PRO_INT_V_B  3
#define PRO_INT_V_N  4
#define PRO_INT_V_M  5

/* 数值必须与上游 ac_types.h 的 F_* 一致。 */
#define PRO_INT_F_SIN   1
#define PRO_INT_F_COS   2
#define PRO_INT_F_TAN   3
#define PRO_INT_F_ASIN  4
#define PRO_INT_F_ACOS  5
#define PRO_INT_F_ATAN  6
#define PRO_INT_F_SQRT  8
#define PRO_INT_F_LN    9
#define PRO_INT_F_LOG   10
#define PRO_INT_F_EXP   11
#define PRO_INT_F_SINH  13
#define PRO_INT_F_COSH  14
#define PRO_INT_F_TANH  15
#define PRO_INT_F_ASINH 16
#define PRO_INT_F_ACOSH 17
#define PRO_INT_F_ATANH 18

typedef struct {
    signed char   op;
    signed char   pad0;
    short         pad1;
    int           a;
    int           b;
    long          ival;   /* 按 op 解释：CONST 值 / VAR id / FUNC id / POW 指数 */
} pro_int_node_t;

typedef struct {
    int  u;
    long a;
    long b;
    long n;
    long m;   /* v2.3.0：模板伴生常量（如 k² = n²），无伴生时恒 0 */
} pro_int_bind_t;

typedef struct {
    pro_int_node_t n[PRO_INT_POOL_NODES];
    int count;
    int xvar;             /* 所有 x 共用一个叶子；-1 = 尚未创建 */
    int overflow;         /* 池满，映射成 EBUDGET */
    int parts_used;       /* 走过分部积分，预算上限提到 352 */
    const pro_int_bind_t *bind;   /* 非 NULL 时按模板代换解析 */
} pro_int_pool_t;

static void pro_int_pool_init(pro_int_pool_t *P) {
    P->count = 0;
    P->xvar = -1;
    P->overflow = 0;
    P->parts_used = 0;
    P->bind = 0;
}

enum {
    PRO_INT_T_END = 0,
    PRO_INT_T_NUM,
    PRO_INT_T_VAR,
    PRO_INT_T_FUNC,
    PRO_INT_T_E,
    PRO_INT_T_ADD,
    PRO_INT_T_SUB,
    PRO_INT_T_MUL,
    PRO_INT_T_DIV,
    PRO_INT_T_POW,
    PRO_INT_T_LP,
    PRO_INT_T_RP
};

typedef struct {
    const char *s;
    int         i;
    int         tok;
    long        tok_val;
    int         tok_var;
    int         tok_func;
    int         err;        /* 0 = 正常，否则 AC_PRO_INT_* */
} pro_int_lex_t;

typedef struct {
    const char *name;
    int         id;
} pro_int_fn_t;

/* 必须按长度降序：否则 "atanh" 会被拆成 "atan" + "h"。 */
static const pro_int_fn_t s_pro_int_fns[] = {
    { "asinh", PRO_INT_F_ASINH },
    { "acosh", PRO_INT_F_ACOSH },
    { "atanh", PRO_INT_F_ATANH },
    { "asin",  PRO_INT_F_ASIN  },
    { "acos",  PRO_INT_F_ACOS  },
    { "atan",  PRO_INT_F_ATAN  },
    { "sinh",  PRO_INT_F_SINH  },
    { "cosh",  PRO_INT_F_COSH  },
    { "tanh",  PRO_INT_F_TANH  },
    { "sin",   PRO_INT_F_SIN   },
    { "cos",   PRO_INT_F_COS   },
    { "tan",   PRO_INT_F_TAN   },
    { "exp",   PRO_INT_F_EXP   },
    { "log",   PRO_INT_F_LOG   },
    { "ln",    PRO_INT_F_LN    },
    { "sqrt",  PRO_INT_F_SQRT  }
};

#define PRO_INT_FN_COUNT ((int)(sizeof(s_pro_int_fns) / sizeof(s_pro_int_fns[0])))

static const char *pro_int_fn_name(int fid) {
    int k;
    for (k = 0; k < PRO_INT_FN_COUNT; k++) {
        if (s_pro_int_fns[k].id == fid) { return s_pro_int_fns[k].name; }
    }
    return "?";
}

static int pro_int_var_id(int c) {
    if (c == 'x') { return PRO_INT_V_X; }
    if (c == 'u') { return PRO_INT_V_U; }
    if (c == 'a') { return PRO_INT_V_A; }
    if (c == 'b') { return PRO_INT_V_B; }
    if (c == 'n') { return PRO_INT_V_N; }
    if (c == 'm') { return PRO_INT_V_M; }
    return -1;
}

static void pro_int_next(pro_int_lex_t *L) {
    int c;
    int k;

    while (L->s[L->i] == ' ' || L->s[L->i] == '\t' ||
           L->s[L->i] == '\r' || L->s[L->i] == '\n') {
        L->i++;
    }
    c = (unsigned char)L->s[L->i];

    if (c == '\0') { L->tok = PRO_INT_T_END; return; }

    /* 只认整数；小数请写成 1/2 这类精确形式。 */
    if (c >= '0' && c <= '9') {
        long v = 0;
        while (L->s[L->i] >= '0' && L->s[L->i] <= '9') {
            v = v * 10 + (long)(L->s[L->i] - '0');
            if (v > PRO_INT_MAX_CONST) { L->err = AC_PRO_INT_EDEGREE; return; }
            L->i++;
        }
        if (L->s[L->i] == '.') { L->err = AC_PRO_INT_EPARSE; return; }
        L->tok = PRO_INT_T_NUM;
        L->tok_val = v;
        return;
    }

    /* 函数名必须先于变量匹配：atanh/asin/asinh 都以 'a' 开头。 */
    for (k = 0; k < PRO_INT_FN_COUNT; k++) {
        size_t len = strlen(s_pro_int_fns[k].name);
        if (strncmp(L->s + L->i, s_pro_int_fns[k].name, len) == 0) {
            L->i += (int)len;
            L->tok = PRO_INT_T_FUNC;
            L->tok_func = s_pro_int_fns[k].id;
            return;
        }
    }

    {
        int vid;
        /* e 只作 e^x 的底数，语法层翻成 exp；"exp" 已被函数表先匹配掉。 */
        if (c == 'e') {
            L->i++;
            L->tok = PRO_INT_T_E;
            return;
        }
        vid = pro_int_var_id(c);
        if (vid >= 0) {
            L->i++;
            L->tok = PRO_INT_T_VAR;
            L->tok_var = vid;
            return;
        }
    }

    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        L->err = AC_PRO_INT_EPARSE;
        return;
    }

    L->i++;
    switch (c) {
    case '+': L->tok = PRO_INT_T_ADD; return;
    case '-': L->tok = PRO_INT_T_SUB; return;
    case '*': L->tok = PRO_INT_T_MUL; return;
    case '/': L->tok = PRO_INT_T_DIV; return;
    case '^': L->tok = PRO_INT_T_POW; return;
    case '(': L->tok = PRO_INT_T_LP;  return;
    case ')': L->tok = PRO_INT_T_RP;  return;
    default:
        L->err = AC_PRO_INT_EPARSE;
        return;
    }
}

static int pro_int_is_const(const pro_int_pool_t *P, int i) {
    return (i >= 0 && i < P->count && P->n[i].op == PRO_INT_OP_CONST);
}

static int pro_int_put(pro_int_pool_t *P, int op, int a, int b, long ival) {
    int idx;
    if (P->overflow) { return -1; }
    if (P->count >= PRO_INT_POOL_NODES) { P->overflow = 1; return -1; }
    idx = P->count++;
    P->n[idx].op = (signed char)op;
    P->n[idx].pad0 = 0;
    P->n[idx].pad1 = 0;
    P->n[idx].a = a;
    P->n[idx].b = b;
    P->n[idx].ival = ival;
    return idx;
}

static int pro_int_const(pro_int_pool_t *P, long v) {
    return pro_int_put(P, PRO_INT_OP_CONST, -1, -1, v);
}

static int pro_int_var_x(pro_int_pool_t *P) {
    if (P->xvar >= 0) { return P->xvar; }
    P->xvar = pro_int_put(P, PRO_INT_OP_VAR, -1, -1, (long)PRO_INT_V_X);
    return P->xvar;
}

static int pro_int_add(pro_int_pool_t *P, int a, int b);
static int pro_int_sub(pro_int_pool_t *P, int a, int b);
static int pro_int_mul(pro_int_pool_t *P, int a, int b);
static int pro_int_div(pro_int_pool_t *P, int a, int b);
static int pro_int_neg(pro_int_pool_t *P, int a);
static int pro_int_pow(pro_int_pool_t *P, int base, long e);

static int pro_int_add(pro_int_pool_t *P, int a, int b) {
    if (a < 0 || b < 0) { return -1; }
    if (pro_int_is_const(P, a) && pro_int_is_const(P, b)) {
        return pro_int_const(P, P->n[a].ival + P->n[b].ival);
    }
    if (pro_int_is_const(P, a) && P->n[a].ival == 0) { return b; }
    if (pro_int_is_const(P, b) && P->n[b].ival == 0) { return a; }
    /* 右侧链折叠：add(a, b1±b2) 摊平成 (a±b1)±b2，配合 mul 的常数分配，
     * 让分部积分输出与积分表模板同为左结合扁平链。只动右侧；左侧本就是
     * 构建顺序的左结合链，天然扁平。每层折叠严格缩短右链，必然终止。 */
    if (P->n[b].op == PRO_INT_OP_NEG) { return pro_int_sub(P, a, P->n[b].a); }
    if (P->n[b].op == PRO_INT_OP_ADD) {
        int l = pro_int_add(P, a, P->n[b].a);
        return (l < 0) ? -1 : pro_int_add(P, l, P->n[b].b);
    }
    if (P->n[b].op == PRO_INT_OP_SUB) {
        int l = pro_int_add(P, a, P->n[b].a);
        return (l < 0) ? -1 : pro_int_sub(P, l, P->n[b].b);
    }
    return pro_int_put(P, PRO_INT_OP_ADD, a, b, 0);
}

static int pro_int_sub(pro_int_pool_t *P, int a, int b) {
    if (a < 0 || b < 0) { return -1; }
    if (pro_int_is_const(P, a) && pro_int_is_const(P, b)) {
        return pro_int_const(P, P->n[a].ival - P->n[b].ival);
    }
    if (pro_int_is_const(P, b) && P->n[b].ival == 0) { return a; }
    /* 右侧链折叠：a-(b1+b2) = (a-b1)-b2；a-(b1-b2) = (a-b1)+b2；
     * a-(-b1) = a+b1。同样是保持输出扁平的形态归一化。 */
    if (P->n[b].op == PRO_INT_OP_NEG) { return pro_int_add(P, a, P->n[b].a); }
    if (P->n[b].op == PRO_INT_OP_ADD) {
        int l = pro_int_sub(P, a, P->n[b].a);
        return (l < 0) ? -1 : pro_int_sub(P, l, P->n[b].b);
    }
    if (P->n[b].op == PRO_INT_OP_SUB) {
        int l = pro_int_sub(P, a, P->n[b].a);
        return (l < 0) ? -1 : pro_int_add(P, l, P->n[b].b);
    }
    return pro_int_put(P, PRO_INT_OP_SUB, a, b, 0);
}

static int pro_int_mul(pro_int_pool_t *P, int a, int b) {
    if (a < 0 || b < 0) { return -1; }
    if (pro_int_is_const(P, a) && pro_int_is_const(P, b)) {
        return pro_int_const(P, P->n[a].ival * P->n[b].ival);
    }
    if (pro_int_is_const(P, a)) {
        if (P->n[a].ival == 0) { return pro_int_const(P, 0); }
        if (P->n[a].ival == 1) { return b; }
    }
    if (pro_int_is_const(P, b)) {
        if (P->n[b].ival == 0) { return pro_int_const(P, 0); }
        if (P->n[b].ival == 1) { return a; }
    }
    /* 负号外提：输出 -2*cos(x) 而不是 2*(-cos(x))。 */
    if (P->n[a].op == PRO_INT_OP_NEG) {
        int inner = pro_int_mul(P, P->n[a].a, b);
        return (inner < 0) ? -1 : pro_int_neg(P, inner);
    }
    if (P->n[b].op == PRO_INT_OP_NEG) {
        int inner = pro_int_mul(P, a, P->n[b].a);
        return (inner < 0) ? -1 : pro_int_neg(P, inner);
    }
    /* 常数×常数头先折叠（必须先于下方的外提归一）：否则
     * mul(c1, MUL(c2, X)) 会被外提换成 mul(c2, mul(c1, X))，再被分配
     * 拆回，提升与分配互为逆操作，乒乓不休（实测 512 节点池溢出）。
     * 先折叠成 mul(c1·c2, X) 让变换单向汇聚。 */
    if (pro_int_is_const(P, a) && P->n[b].op == PRO_INT_OP_MUL &&
        pro_int_is_const(P, P->n[b].a)) {
        long c = P->n[a].ival * P->n[P->n[b].a].ival;
        if (c > PRO_INT_MAX_CONST || c < -PRO_INT_MAX_CONST) {
            return pro_int_put(P, PRO_INT_OP_MUL, a, b, 0);
        }
        return pro_int_mul(P, pro_int_const(P, c), P->n[b].b);
    }
    if (pro_int_is_const(P, b) && P->n[a].op == PRO_INT_OP_MUL &&
        pro_int_is_const(P, P->n[a].a)) {
        long c = P->n[b].ival * P->n[P->n[a].a].ival;
        if (c > PRO_INT_MAX_CONST || c < -PRO_INT_MAX_CONST) {
            return pro_int_put(P, PRO_INT_OP_MUL, a, b, 0);
        }
        return pro_int_mul(P, pro_int_const(P, c), P->n[a].b);
    }
    /* 常数因子归一到最外层：2*x*exp(x) 要变成 2*(x*exp(x))，否则常数倍
       规则和分部积分都看不见这个常数，整个乘积会被判成表未命中。 */
    if (P->n[a].op == PRO_INT_OP_MUL && pro_int_is_const(P, P->n[a].a)) {
        int inner = pro_int_mul(P, P->n[a].b, b);
        return (inner < 0) ? -1 : pro_int_mul(P, P->n[a].a, inner);
    }
    if (P->n[a].op == PRO_INT_OP_MUL && pro_int_is_const(P, P->n[a].b)) {
        int inner = pro_int_mul(P, P->n[a].a, b);
        return (inner < 0) ? -1 : pro_int_mul(P, P->n[a].b, inner);
    }
    if (P->n[b].op == PRO_INT_OP_MUL && pro_int_is_const(P, P->n[b].a)) {
        int inner = pro_int_mul(P, a, P->n[b].b);
        return (inner < 0) ? -1 : pro_int_mul(P, P->n[b].a, inner);
    }
    if (P->n[b].op == PRO_INT_OP_MUL && pro_int_is_const(P, P->n[b].b)) {
        int inner = pro_int_mul(P, a, P->n[b].a);
        return (inner < 0) ? -1 : pro_int_mul(P, P->n[b].b, inner);
    }
    /* 常数 × 和/差：分配展开。两层分部的系数乘法（n·∫v du 的 n·inner）
     * 必须保持与积分表模板一致的扁平形态，否则输出出现 n*(a+b-c) 嵌套，
     * 形态契约就变了。仅常数因子参与分配；exp(x)*(x^2-2*x+2) 这类
     * 非常数 × 和差不在此列，维持括号形态（既有测试锚定）。 */
    if (pro_int_is_const(P, a) &&
        (P->n[b].op == PRO_INT_OP_ADD || P->n[b].op == PRO_INT_OP_SUB)) {
        int l = pro_int_mul(P, a, P->n[b].a);
        int r = pro_int_mul(P, a, P->n[b].b);
        if (l < 0 || r < 0) { return -1; }
        return (P->n[b].op == PRO_INT_OP_ADD) ? pro_int_add(P, l, r)
                                              : pro_int_sub(P, l, r);
    }
    if (pro_int_is_const(P, b) &&
        (P->n[a].op == PRO_INT_OP_ADD || P->n[a].op == PRO_INT_OP_SUB)) {
        int l = pro_int_mul(P, P->n[a].a, b);
        int r = pro_int_mul(P, P->n[a].b, b);
        if (l < 0 || r < 0) { return -1; }
        return (P->n[a].op == PRO_INT_OP_ADD) ? pro_int_add(P, l, r)
                                              : pro_int_sub(P, l, r);
    }
    return pro_int_put(P, PRO_INT_OP_MUL, a, b, 0);
}

static int pro_int_div(pro_int_pool_t *P, int a, int b) {
    if (a < 0 || b < 0) { return -1; }
    if (pro_int_is_const(P, b)) {
        long d = P->n[b].ival;
        if (d == 0) { return -1; }
        if (d == 1) { return a; }
        if (pro_int_is_const(P, a) && (P->n[a].ival % d) == 0) {
            return pro_int_const(P, P->n[a].ival / d);
        }
        /* 避免输出 x^2/2/2 这种连除 */
        if (P->n[a].op == PRO_INT_OP_DIV && pro_int_is_const(P, P->n[a].b)) {
            long d1 = P->n[P->n[a].b].ival;
            if (d1 != 0 && d1 * d <= PRO_INT_MAX_CONST) {
                return pro_int_div(P, P->n[a].a, pro_int_const(P, d1 * d));
            }
        }
    }
    return pro_int_put(P, PRO_INT_OP_DIV, a, b, 0);
}

static int pro_int_neg(pro_int_pool_t *P, int a) {
    if (a < 0) { return -1; }
    if (pro_int_is_const(P, a)) { return pro_int_const(P, -P->n[a].ival); }
    if (P->n[a].op == PRO_INT_OP_NEG) { return P->n[a].a; }
    return pro_int_put(P, PRO_INT_OP_NEG, a, -1, 0);
}

static int pro_int_pow(pro_int_pool_t *P, int base, long e) {
    if (base < 0) { return -1; }
    if (e < 0 || e > PRO_INT_MAX_POW_BUILD) { return -1; }
    if (e == 0) { return pro_int_const(P, 1); }
    if (e == 1) { return base; }
    if (pro_int_is_const(P, base)) {
        long v = 1;
        long k;
        int ok = 1;
        for (k = 0; k < e; k++) {
            v *= P->n[base].ival;
            if (v > PRO_INT_MAX_CONST || v < -PRO_INT_MAX_CONST) { ok = 0; break; }
        }
        if (ok) { return pro_int_const(P, v); }
    }
    return pro_int_put(P, PRO_INT_OP_POW, base, -1, e);
}

static int pro_int_func(pro_int_pool_t *P, int fid, int arg) {
    if (arg < 0) { return -1; }
    return pro_int_put(P, PRO_INT_OP_FUNC, arg, -1, (long)fid);
}

static int pro_int_xpow(pro_int_pool_t *P, long e) {
    return pro_int_pow(P, pro_int_var_x(P), e);
}

static int pro_int_parse_expr(pro_int_lex_t *L, pro_int_pool_t *P, int depth);
static int pro_int_parse_pow(pro_int_lex_t *L, pro_int_pool_t *P, int depth);

static int pro_int_parse_primary(pro_int_lex_t *L, pro_int_pool_t *P, int depth) {
    int idx;

    if (depth > PRO_INT_MAX_DEPTH) { L->err = AC_PRO_INT_EPARSE; return -1; }

    if (L->tok == PRO_INT_T_NUM) {
        idx = pro_int_const(P, L->tok_val);
        pro_int_next(L);
        return idx;
    }
    if (L->tok == PRO_INT_T_VAR) {
        int vid = L->tok_var;
        pro_int_next(L);
        if (vid == PRO_INT_V_X) { return pro_int_var_x(P); }
        if (P->bind == 0) { L->err = AC_PRO_INT_EPARSE; return -1; }
        if (vid == PRO_INT_V_U) { return P->bind->u; }
        if (vid == PRO_INT_V_A) { return pro_int_const(P, P->bind->a); }
        if (vid == PRO_INT_V_B) { return pro_int_const(P, P->bind->b); }
        if (vid == PRO_INT_V_M) { return pro_int_const(P, P->bind->m); }
        return pro_int_const(P, P->bind->n);
    }
    if (L->tok == PRO_INT_T_LP) {
        pro_int_next(L);
        idx = pro_int_parse_expr(L, P, depth + 1);
        if (idx < 0) { return -1; }
        if (L->tok != PRO_INT_T_RP) { L->err = AC_PRO_INT_EPARSE; return -1; }
        pro_int_next(L);
        return idx;
    }
    if (L->tok == PRO_INT_T_E) {
        /* IR 里没有无理常量，e^u 一律翻成 exp(u)。 */
        int ex;
        pro_int_next(L);
        if (L->tok != PRO_INT_T_POW) { L->err = AC_PRO_INT_EPARSE; return -1; }
        pro_int_next(L);
        ex = pro_int_parse_pow(L, P, depth + 1);
        if (ex < 0) { return -1; }
        return pro_int_func(P, PRO_INT_F_EXP, ex);
    }
    if (L->tok == PRO_INT_T_FUNC) {
        int fid = L->tok_func;
        int arg;
        pro_int_next(L);
        if (L->tok != PRO_INT_T_LP) { L->err = AC_PRO_INT_EPARSE; return -1; }
        pro_int_next(L);
        arg = pro_int_parse_expr(L, P, depth + 1);
        if (arg < 0) { return -1; }
        if (L->tok != PRO_INT_T_RP) { L->err = AC_PRO_INT_EPARSE; return -1; }
        pro_int_next(L);
        return pro_int_func(P, fid, arg);
    }
    L->err = AC_PRO_INT_EPARSE;
    return -1;
}

static int pro_int_parse_pow(pro_int_lex_t *L, pro_int_pool_t *P, int depth) {
    int base;
    if (depth > PRO_INT_MAX_DEPTH) { L->err = AC_PRO_INT_EPARSE; return -1; }
    /* 一元负号优先级低于 ^：-x^2 读作 -(x^2)。 */
    if (L->tok == PRO_INT_T_SUB) {
        int arg;
        pro_int_next(L);
        arg = pro_int_parse_pow(L, P, depth + 1);
        if (arg < 0) { return -1; }
        return pro_int_neg(P, arg);
    }
    if (L->tok == PRO_INT_T_ADD) {
        pro_int_next(L);
        return pro_int_parse_pow(L, P, depth + 1);
    }
    base = pro_int_parse_primary(L, P, depth + 1);
    if (base < 0) { return -1; }
    if (L->tok == PRO_INT_T_POW) {
        int e;
        long ev;
        pro_int_next(L);
        /* 指数必须折叠成非负整数字面量；模板里的 "(n+1)" 也算。 */
        e = pro_int_parse_primary(L, P, depth + 1);
        if (e < 0) { return -1; }
        if (!pro_int_is_const(P, e)) { L->err = AC_PRO_INT_EPARSE; return -1; }
        ev = P->n[e].ival;
        if (ev < 0) { L->err = AC_PRO_INT_EPARSE; return -1; }
        if (ev > PRO_INT_MAX_POW_PARSE && P->bind == 0) {
            L->err = AC_PRO_INT_EDEGREE;
            return -1;
        }
        return pro_int_pow(P, base, ev);
    }
    return base;
}

static int pro_int_parse_term(pro_int_lex_t *L, pro_int_pool_t *P, int depth) {
    int left;
    if (depth > PRO_INT_MAX_DEPTH) { L->err = AC_PRO_INT_EPARSE; return -1; }
    left = pro_int_parse_pow(L, P, depth + 1);
    if (left < 0) { return -1; }
    for (;;) {
        int is_div;
        int right;
        if (L->tok == PRO_INT_T_MUL || L->tok == PRO_INT_T_DIV) {
            is_div = (L->tok == PRO_INT_T_DIV);
            pro_int_next(L);
        } else if (L->tok == PRO_INT_T_VAR || L->tok == PRO_INT_T_FUNC ||
                   L->tok == PRO_INT_T_E || L->tok == PRO_INT_T_LP) {
            /* 隐式乘法；NUM 后面紧跟 NUM 仍是语法错误。 */
            is_div = 0;
        } else {
            break;
        }
        right = pro_int_parse_pow(L, P, depth + 1);
        if (right < 0) { return -1; }
        left = is_div ? pro_int_div(P, left, right) : pro_int_mul(P, left, right);
        if (left < 0) { L->err = AC_PRO_INT_EBUDGET; return -1; }
    }
    return left;
}

static int pro_int_parse_expr(pro_int_lex_t *L, pro_int_pool_t *P, int depth) {
    int left;
    if (depth > PRO_INT_MAX_DEPTH) { L->err = AC_PRO_INT_EPARSE; return -1; }
    left = pro_int_parse_term(L, P, depth + 1);
    if (left < 0) { return -1; }
    while (L->tok == PRO_INT_T_ADD || L->tok == PRO_INT_T_SUB) {
        int is_sub = (L->tok == PRO_INT_T_SUB);
        int right;
        pro_int_next(L);
        right = pro_int_parse_term(L, P, depth + 1);
        if (right < 0) { return -1; }
        left = is_sub ? pro_int_sub(P, left, right) : pro_int_add(P, left, right);
        if (left < 0) { L->err = AC_PRO_INT_EBUDGET; return -1; }
    }
    return left;
}

static int pro_int_parse_string(const char *src, pro_int_pool_t *P, int *err) {
    pro_int_lex_t L;
    int root;

    *err = 0;
    L.s = src;
    L.i = 0;
    L.tok = PRO_INT_T_END;
    L.tok_val = 0;
    L.tok_var = PRO_INT_V_X;
    L.tok_func = 0;
    L.err = 0;

    pro_int_next(&L);
    if (L.err != 0) { *err = L.err; return -1; }
    if (L.tok == PRO_INT_T_END) { *err = AC_PRO_INT_EPARSE; return -1; }

    root = pro_int_parse_expr(&L, P, 0);
    if (L.err != 0) { *err = L.err; return -1; }
    if (root < 0) { *err = P->overflow ? AC_PRO_INT_EBUDGET : AC_PRO_INT_EPARSE; return -1; }
    if (L.tok != PRO_INT_T_END) { *err = AC_PRO_INT_EPARSE; return -1; }
    return root;
}

static int pro_int_prec(const pro_int_pool_t *P, int r) {
    switch (P->n[r].op) {
    case PRO_INT_OP_ADD:
    case PRO_INT_OP_SUB:  return 1;
    case PRO_INT_OP_MUL:
    case PRO_INT_OP_DIV:
    case PRO_INT_OP_NEG:  return 2;
    case PRO_INT_OP_POW:  return 3;
    default:              return 4;
    }
}

static int pro_int_putc(char *buf, int cap, int *pos, char c) {
    if (*pos + 1 >= cap) { return AC_PRO_INT_EOVER; }
    buf[*pos] = c;
    (*pos)++;
    buf[*pos] = '\0';
    return 0;
}

static int pro_int_puts(char *buf, int cap, int *pos, const char *s) {
    while (*s != '\0') {
        int rc = pro_int_putc(buf, cap, pos, *s);
        if (rc != 0) { return rc; }
        s++;
    }
    return 0;
}

static int pro_int_putn(char *buf, int cap, int *pos, long v) {
    char tmp[24];
    int n = 0;
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) { tmp[n++] = '0'; }
    while (v > 0) {
        tmp[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    }
    if (neg) {
        int rc = pro_int_putc(buf, cap, pos, '-');
        if (rc != 0) { return rc; }
    }
    while (n > 0) {
        int rc = pro_int_putc(buf, cap, pos, tmp[--n]);
        if (rc != 0) { return rc; }
    }
    return 0;
}

static int pro_int_emit(pro_int_pool_t *P, int r, char *buf, int cap, int *pos,
                        int minprec) {
    int paren;
    int rc;

    if (r < 0 || r >= P->count) { return AC_PRO_INT_EPARSE; }
    paren = (pro_int_prec(P, r) < minprec);
    if (paren) {
        rc = pro_int_putc(buf, cap, pos, '(');
        if (rc != 0) { return rc; }
    }

    switch (P->n[r].op) {
    case PRO_INT_OP_CONST:
        rc = pro_int_putn(buf, cap, pos, P->n[r].ival);
        break;
    case PRO_INT_OP_VAR:
        rc = pro_int_putc(buf, cap, pos, 'x');
        break;
    case PRO_INT_OP_NEG: {
        int child = P->n[r].a;
        rc = pro_int_putc(buf, cap, pos, '-');
        /* -a*b 与 -(a*b) 同值，省掉一层括号；只有 -(a+b) 才需要括号。 */
        if (rc == 0) { rc = pro_int_emit(P, child, buf, cap, pos, 2); }
        break;
    }
    case PRO_INT_OP_ADD:
    case PRO_INT_OP_SUB: {
        int right = P->n[r].b;
        int neg_right = 0;
        if (P->n[r].op == PRO_INT_OP_ADD && right >= 0 &&
            P->n[right].op == PRO_INT_OP_NEG) {
            neg_right = 1;
            right = P->n[right].a;
        }
        rc = pro_int_emit(P, P->n[r].a, buf, cap, pos, 1);
        if (rc != 0) { break; }
        rc = pro_int_putc(buf, cap, pos,
                          (P->n[r].op == PRO_INT_OP_SUB || neg_right) ? '-' : '+');
        if (rc != 0) { break; }
        rc = pro_int_emit(P, right, buf, cap, pos, 2);
        break;
    }
    case PRO_INT_OP_MUL: {
        int right = P->n[r].b;
        int rp = (right >= 0 && P->n[right].op == PRO_INT_OP_MUL) ? 2 : 3;
        rc = pro_int_emit(P, P->n[r].a, buf, cap, pos, 2);
        if (rc != 0) { break; }
        rc = pro_int_putc(buf, cap, pos, '*');
        if (rc != 0) { break; }
        /* a*(b/c) 摊平成 a*b/c：2*(x^3/3) -> 2*x^3/3 */
        if (right >= 0 && P->n[right].op == PRO_INT_OP_DIV) {
            rc = pro_int_emit(P, P->n[right].a, buf, cap, pos, 2);
            if (rc != 0) { break; }
            rc = pro_int_putc(buf, cap, pos, '/');
            if (rc != 0) { break; }
            rc = pro_int_emit(P, P->n[right].b, buf, cap, pos, 3);
            break;
        }
        rc = pro_int_emit(P, right, buf, cap, pos, rp);
        break;
    }
    case PRO_INT_OP_DIV:
        rc = pro_int_emit(P, P->n[r].a, buf, cap, pos, 2);
        if (rc != 0) { break; }
        rc = pro_int_putc(buf, cap, pos, '/');
        if (rc != 0) { break; }
        rc = pro_int_emit(P, P->n[r].b, buf, cap, pos, 3);
        break;
    case PRO_INT_OP_POW:
        rc = pro_int_emit(P, P->n[r].a, buf, cap, pos, 4);
        if (rc != 0) { break; }
        rc = pro_int_putc(buf, cap, pos, '^');
        if (rc != 0) { break; }
        rc = pro_int_putn(buf, cap, pos, P->n[r].ival);
        break;
    case PRO_INT_OP_FUNC:
        rc = pro_int_puts(buf, cap, pos, pro_int_fn_name((int)P->n[r].ival));
        if (rc != 0) { break; }
        rc = pro_int_putc(buf, cap, pos, '(');
        if (rc != 0) { break; }
        rc = pro_int_emit(P, P->n[r].a, buf, cap, pos, 1);
        if (rc != 0) { break; }
        rc = pro_int_putc(buf, cap, pos, ')');
        break;
    default:
        rc = AC_PRO_INT_EPARSE;
        break;
    }

    if (rc != 0) { return rc; }
    if (paren) { return pro_int_putc(buf, cap, pos, ')'); }
    return 0;
}

/* 64 项标准积分表。原函数写成模板串，由 pro_int_parse_string 按 bind 代换
 * 解析出来,代码里没有第二份原函数知识。
 * 占位符：u 内层表达式；a/b 是把 u 视为 a·x+b 时的斜率与常数项；
 *         n 捕获的幂次（K_XN）或常量值（K_CONST）。
 * static const，落 .rdata，不占 .bss。 */
#define PRO_INT_K_CONST      0   /* 常数 c */
#define PRO_INT_K_XN         1   /* x^n */
#define PRO_INT_K_INV        2   /* 1/(a*x+b) */
#define PRO_INT_K_INV2       3   /* 1/x^2 */
#define PRO_INT_K_ATAN_KER   4   /* 1/(1+x^2) */
#define PRO_INT_K_ASIN_KER   5   /* 1/sqrt(1-x^2) */
#define PRO_INT_K_ASINH_KER  6   /* 1/sqrt(1+x^2) */
#define PRO_INT_K_ACOSH_KER  7   /* 1/sqrt(x^2-1) */
#define PRO_INT_K_ATANH_KER  8   /* 1/(1-x^2) */
#define PRO_INT_K_CIRCLE     9   /* sqrt(1-x^2) */
#define PRO_INT_K_HYPER     10   /* sqrt(1+x^2) */
#define PRO_INT_K_FUNC      11   /* f(u)，u 为 x 的一次式 */
#define PRO_INT_K_FUNC_POW  12   /* f(u)^n */
#define PRO_INT_K_XN_FUNC   13   /* x^n · f(x) */
/* v2.3.0 功能 4：表 64 → 128 的新 kind
 * QUAD_L/QUAD_K 的 pow 字段编码子形态（0..7/0..8，见各 case 注释）；
 * FUNC_DIV/FUNC_FUNC/SECTAN/FUNCOVERFP 的 pow 字段编码第二个函数 id。 */
#define PRO_INT_K_QUAD_L    14   /* 二次核（k=1）+ 线性内层 u=a*x+b */
#define PRO_INT_K_QUAD_K    15   /* 二次核（k=√m≥2 完全平方）+ 线性内层 */
#define PRO_INT_K_XQUAD     16   /* u/(u²±m)、u/(m−u²) */
#define PRO_INT_K_FUNC_INV  17   /* 1/f(u)：sec/csc */
#define PRO_INT_K_FUNC_DIV  18   /* f(u)/g(u)：cot/coth */
#define PRO_INT_K_FUNC_INVP 19   /* 1/f(u)^2：sec²/csc²/cot²/sech²/csch²/coth² */
#define PRO_INT_K_FUNC_FUNC 20   /* f(u)·g(u)（同内层）：sin·cos、sinh·cosh */
#define PRO_INT_K_SECTAN    21   /* (1/f(u))·g(u)：sec·tan、csc·cot */
#define PRO_INT_K_FUNCOVERFP 22  /* f(u)/g(u)^2：sec·tan、csc·cot 的另一拼法 */
#define PRO_INT_K_EXP_POW   23   /* exp(u)^n */
#define PRO_INT_K_SIN2COS2  24   /* sin(u)^2·cos(u)^2 */
#define PRO_INT_K_LN_OVER_X 25   /* ln(u)/u */
#define PRO_INT_K_ONE_OVER_ULN 26 /* 1/(u·ln(u)) */

typedef struct {
    unsigned char kind;
    unsigned char func;    /* PRO_INT_F_*，无关时为 0 */
    signed char   pow;     /* >= 0 精确幂次，-1 通配 */
    const char   *anti;    /* 原函数模板 */
} pro_int_entry_t;

static const pro_int_entry_t s_pro_int_table[AC_PRO_INT_TABLE_SIZE] = {
    /* 1..10 常数与幂 */
    { PRO_INT_K_CONST,   0,            0, "n*u" },
    { PRO_INT_K_XN,      0,            1, "u^2/2" },
    { PRO_INT_K_XN,      0,            2, "u^3/3" },
    { PRO_INT_K_XN,      0,            3, "u^4/4" },
    { PRO_INT_K_XN,      0,            4, "u^5/5" },
    { PRO_INT_K_XN,      0,            5, "u^6/6" },
    { PRO_INT_K_XN,      0,            6, "u^7/7" },
    { PRO_INT_K_XN,      0,            7, "u^8/8" },
    { PRO_INT_K_XN,      0,            8, "u^9/9" },
    { PRO_INT_K_XN,      0,           -1, "u^(n+1)/(n+1)" },
    /* 11..19 倒数与代数核 */
    { PRO_INT_K_INV,       0,          0, "ln(u)/a" },
    { PRO_INT_K_INV2,      0,          0, "-1/u" },
    { PRO_INT_K_ATAN_KER,  0,          0, "atan(u)" },
    { PRO_INT_K_ASIN_KER,  0,          0, "asin(u)" },
    { PRO_INT_K_ASINH_KER, 0,          0, "asinh(u)" },
    { PRO_INT_K_ACOSH_KER, 0,          0, "acosh(u)" },
    { PRO_INT_K_ATANH_KER, 0,          0, "atanh(u)" },
    { PRO_INT_K_CIRCLE,    0,          0, "(u*sqrt(1-u^2)+asin(u))/2" },
    { PRO_INT_K_HYPER,     0,          0, "(u*sqrt(u^2+1)+asinh(u))/2" },
    /* 20..35 基本函数，线性内层自动带 1/a */
    { PRO_INT_K_FUNC, PRO_INT_F_SIN,   0, "-cos(u)/a" },
    { PRO_INT_K_FUNC, PRO_INT_F_COS,   0, "sin(u)/a" },
    { PRO_INT_K_FUNC, PRO_INT_F_TAN,   0, "-ln(cos(u))/a" },
    { PRO_INT_K_FUNC, PRO_INT_F_EXP,   0, "exp(u)/a" },
    { PRO_INT_K_FUNC, PRO_INT_F_LN,    0, "(u*ln(u)-u)/a" },
    { PRO_INT_K_FUNC, PRO_INT_F_LOG,   0, "(u*log(u)-u/ln(10))/a" },
    { PRO_INT_K_FUNC, PRO_INT_F_SQRT,  0, "2*u*sqrt(u)/(3*a)" },
    { PRO_INT_K_FUNC, PRO_INT_F_SINH,  0, "cosh(u)/a" },
    { PRO_INT_K_FUNC, PRO_INT_F_COSH,  0, "sinh(u)/a" },
    { PRO_INT_K_FUNC, PRO_INT_F_TANH,  0, "ln(cosh(u))/a" },
    { PRO_INT_K_FUNC, PRO_INT_F_ASIN,  0, "(u*asin(u)+sqrt(1-u^2))/a" },
    { PRO_INT_K_FUNC, PRO_INT_F_ACOS,  0, "(u*acos(u)-sqrt(1-u^2))/a" },
    { PRO_INT_K_FUNC, PRO_INT_F_ATAN,  0, "(u*atan(u)-ln(1+u^2)/2)/a" },
    { PRO_INT_K_FUNC, PRO_INT_F_ASINH, 0, "(u*asinh(u)-sqrt(1+u^2))/a" },
    { PRO_INT_K_FUNC, PRO_INT_F_ACOSH, 0, "(u*acosh(u)-sqrt(u^2-1))/a" },
    { PRO_INT_K_FUNC, PRO_INT_F_ATANH, 0, "(u*atanh(u)+ln(1-u^2)/2)/a" },
    /* 36..43 函数幂 */
    { PRO_INT_K_FUNC_POW, PRO_INT_F_SIN,  2, "u/(2*a)-sin(2*u)/(4*a)" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_COS,  2, "u/(2*a)+sin(2*u)/(4*a)" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_TAN,  2, "tan(u)/a-u/a" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_SIN,  3, "(-cos(u)+cos(u)^3/3)/a" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_COS,  3, "(sin(u)-sin(u)^3/3)/a" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_SINH, 2, "sinh(2*u)/(4*a)-u/(2*a)" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_COSH, 2, "sinh(2*u)/(4*a)+u/(2*a)" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_TANH, 2, "u/a-tanh(u)/a" },
    /* 44..59 x^n·f(x) */
    { PRO_INT_K_XN_FUNC, PRO_INT_F_SIN,   1, "-x*cos(x)+sin(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_COS,   1, "x*sin(x)+cos(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_EXP,   1, "exp(x)*(x-1)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_LN,    1, "x^2*ln(x)/2-x^2/4" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_SINH,  1, "x*cosh(x)-sinh(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_COSH,  1, "x*sinh(x)-cosh(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_ATAN,  1, "((x^2+1)*atan(x)-x)/2" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_SQRT,  1, "2*x^2*sqrt(x)/5" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_SIN,   2, "-x^2*cos(x)+2*x*sin(x)+2*cos(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_COS,   2, "x^2*sin(x)+2*x*cos(x)-2*sin(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_EXP,   2, "exp(x)*(x^2-2*x+2)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_LN,    2, "x^3*ln(x)/3-x^3/9" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_SINH,  2, "x^2*cosh(x)-2*x*sinh(x)+2*cosh(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_COSH,  2, "x^2*sinh(x)-2*x*cosh(x)+2*sinh(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_EXP,   3, "exp(x)*(x^3-3*x^2+6*x-6)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_EXP,   4, "exp(x)*(x^4-4*x^3+12*x^2-24*x+24)" },
    /* 60..64 高阶三角/双曲，补足 64 */
    { PRO_INT_K_XN_FUNC, PRO_INT_F_SIN,   3, "-x^3*cos(x)+3*x^2*sin(x)+6*x*cos(x)-6*sin(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_COS,   3, "x^3*sin(x)+3*x^2*cos(x)-6*x*sin(x)-6*cos(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_LN,    3, "x^4*ln(x)/4-x^4/16" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_SINH,  3, "x^3*cosh(x)-3*x^2*sinh(x)+6*x*cosh(x)-6*sinh(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_COSH,  3, "x^3*sinh(x)-3*x^2*cosh(x)+6*x*sinh(x)-6*cosh(x)" },
    /* v2.3.0 功能 4：65..128（表 64 → 128）
     * 形态接续原有 kind：二次核/倒数函数族/x^n·f(x) 扩充/函数幂扩充。
     * QUAD_L/QUAD_K/XQUAD 的 pow 字段 = 子形态码；FUNC_DIV/FUNC_FUNC/
     * SECTAN/FUNCOVERFP 的 pow 字段 = 第二个函数 id。模板变量 m = n² 类
     * 伴生常量（QUAD_K/XQUAD 用）。 */

    /* 65..72 二次核 k=1，线性内层（QUAD_L，pow = 形态码） */
    { PRO_INT_K_QUAD_L, 0, 0, "atan(u)/a" },                               /* 1/(1+u²) */
    { PRO_INT_K_QUAD_L, 0, 1, "atanh(u)/a" },                              /* 1/(1-u²) */
    { PRO_INT_K_QUAD_L, 0, 2, "asin(u)/a" },                               /* 1/sqrt(1-u²) */
    { PRO_INT_K_QUAD_L, 0, 3, "asinh(u)/a" },                              /* 1/sqrt(1+u²) */
    { PRO_INT_K_QUAD_L, 0, 4, "acosh(u)/a" },                              /* 1/sqrt(u²-1) */
    { PRO_INT_K_QUAD_L, 0, 5, "(u*sqrt(1-u^2)+asin(u))/(2*a)" },           /* sqrt(1-u²) */
    { PRO_INT_K_QUAD_L, 0, 6, "(u*sqrt(1+u^2)+asinh(u))/(2*a)" },          /* sqrt(1+u²) */
    { PRO_INT_K_QUAD_L, 0, 7, "(u*sqrt(u^2-1)-acosh(u))/(2*a)" },          /* sqrt(u²-1) */

    /* 73..81 二次核 k=√m≥2 完全平方，线性内层（QUAD_K） */
    { PRO_INT_K_QUAD_K, 0, 0, "atan(u/n)/(a*n)" },                         /* 1/(n²+u²) */
    { PRO_INT_K_QUAD_K, 0, 1, "atanh(u/n)/(a*n)" },                        /* 1/(n²-u²) */
    { PRO_INT_K_QUAD_K, 0, 2, "asin(u/n)/a" },                             /* 1/sqrt(n²-u²) */
    { PRO_INT_K_QUAD_K, 0, 3, "asinh(u/n)/a" },                            /* 1/sqrt(u²+n²) */
    { PRO_INT_K_QUAD_K, 0, 4, "acosh(u/n)/a" },                            /* 1/sqrt(u²-n²) */
    { PRO_INT_K_QUAD_K, 0, 5, "(u*sqrt(m-u^2)+m*asin(u/n))/(2*a)" },       /* sqrt(n²-u²) */
    { PRO_INT_K_QUAD_K, 0, 6, "(u*sqrt(u^2+m)+m*asinh(u/n))/(2*a)" },      /* sqrt(u²+n²) */
    { PRO_INT_K_QUAD_K, 0, 7, "(u*sqrt(u^2-m)-m*acosh(u/n))/(2*a)" },      /* sqrt(u²-n²) */
    { PRO_INT_K_QUAD_K, 0, 8, "ln((u-n)/(u+n))/(2*a*n)" },                 /* 1/(u²-n²) */

    /* 82..84 u/(u²±m)、u/(m-u²)（XQUAD，m≥1 任意整数） */
    { PRO_INT_K_XQUAD, 0, 0, "ln(u^2+m)/(2*a)" },                          /* u/(u²+m) */
    { PRO_INT_K_XQUAD, 0, 1, "ln(u^2-m)/(2*a)" },                          /* u/(u²-m) */
    { PRO_INT_K_XQUAD, 0, 2, "-ln(m-u^2)/(2*a)" },                         /* u/(m-u²) */

    /* 85..96 倒数函数族：sec/csc/cot/coth 及其平方 */
    { PRO_INT_K_FUNC_INV,  PRO_INT_F_COS, 0, "ln(1/cos(u)+sin(u)/cos(u))/a" },
    { PRO_INT_K_FUNC_INV,  PRO_INT_F_SIN, 0, "-ln(1/sin(u)+cos(u)/sin(u))/a" },
    { PRO_INT_K_FUNC_DIV,  PRO_INT_F_COS, PRO_INT_F_SIN,  "ln(sin(u))/a" },
    { PRO_INT_K_FUNC_DIV,  PRO_INT_F_SINH, PRO_INT_F_COSH, "ln(sinh(u))/a" },
    { PRO_INT_K_FUNC_INVP, PRO_INT_F_COS, 2, "sin(u)/cos(u)/a" },          /* sec² */
    { PRO_INT_K_FUNC_INVP, PRO_INT_F_SIN, 2, "-cos(u)/sin(u)/a" },         /* csc² */
    { PRO_INT_K_FUNC_INVP, PRO_INT_F_TAN, 2, "-cos(u)/sin(u)/a-u/a" },     /* cot² */
    { PRO_INT_K_FUNC_INVP, PRO_INT_F_COSH, 2, "sinh(u)/cosh(u)/a" },       /* sech² */
    { PRO_INT_K_FUNC_INVP, PRO_INT_F_SINH, 2, "-cosh(u)/sinh(u)/a" },      /* csch² */
    { PRO_INT_K_FUNC_INVP, PRO_INT_F_TANH, 2, "u/a-cosh(u)/sinh(u)/a" },   /* coth² */
    { PRO_INT_K_FUNC_FUNC, PRO_INT_F_SIN, PRO_INT_F_COS,  "sin(u)^2/(2*a)" },
    { PRO_INT_K_FUNC_FUNC, PRO_INT_F_SINH, PRO_INT_F_COSH, "sinh(u)^2/(2*a)" },

    /* 97..107 乘积恒等式：sec·tan、csc·cot 与 ln 核 */
    { PRO_INT_K_SECTAN,      PRO_INT_F_COS, PRO_INT_F_TAN,  "1/cos(u)/a" },
    { PRO_INT_K_SECTAN,      PRO_INT_F_SIN, PRO_INT_F_COS,  "-1/sin(u)/a" },
    { PRO_INT_K_FUNCOVERFP,  PRO_INT_F_SIN, PRO_INT_F_COS,  "1/cos(u)/a" },
    { PRO_INT_K_FUNCOVERFP,  PRO_INT_F_COS, PRO_INT_F_SIN,  "-1/sin(u)/a" },
    { PRO_INT_K_FUNCOVERFP,  PRO_INT_F_SINH, PRO_INT_F_COSH, "-1/cosh(u)/a" },
    { PRO_INT_K_FUNCOVERFP,  PRO_INT_F_COSH, PRO_INT_F_SINH, "-1/sinh(u)/a" },
    { PRO_INT_K_EXP_POW,     PRO_INT_F_EXP, 2, "exp(n*u)/(n*a)" },
    { PRO_INT_K_SIN2COS2,    PRO_INT_F_SIN, PRO_INT_F_COS,
      "u/(8*a)-sin(4*u)/(32*a)" },
    { PRO_INT_K_LN_OVER_X,   PRO_INT_F_LN, 0, "ln(u)^2/(2*a)" },
    { PRO_INT_K_ONE_OVER_ULN, PRO_INT_F_LN, 0, "ln(ln(u))/a" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_TAN,  3, "tan(u)^2/(2*a)+ln(cos(u))/a" },

    /* 108..117 函数幂扩充（FUNC_POW 复用） */
    { PRO_INT_K_FUNC_POW, PRO_INT_F_TANH, 3, "tanh(u)^2/(2*a)-ln(cosh(u))/a" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_SIN,  4, "3*u/(8*a)-sin(2*u)/(4*a)+sin(4*u)/(32*a)" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_COS,  4, "3*u/(8*a)+sin(2*u)/(4*a)+sin(4*u)/(32*a)" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_SIN,  5, "-cos(u)/a+2*cos(u)^3/(3*a)-cos(u)^5/(5*a)" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_COS,  5, "sin(u)/a-2*sin(u)^3/(3*a)+sin(u)^5/(5*a)" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_SINH, 3, "cosh(u)^3/(3*a)-cosh(u)/a" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_COSH, 3, "sinh(u)/a+sinh(u)^3/(3*a)" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_SINH, 4, "sinh(4*u)/(32*a)-sinh(2*u)/(4*a)+3*u/(8*a)" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_COSH, 4, "sinh(4*u)/(32*a)+sinh(2*u)/(4*a)+3*u/(8*a)" },
    { PRO_INT_K_FUNC_POW, PRO_INT_F_LN,   2, "(u*ln(u)^2-2*u*ln(u)+2*u)/a" },

    /* 118..127 x^n·f(x) 扩充 */
    { PRO_INT_K_XN_FUNC, PRO_INT_F_SIN,  4, "-x^4*cos(x)+4*x^3*sin(x)+12*x^2*cos(x)-24*x*sin(x)-24*cos(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_COS,  4, "x^4*sin(x)+4*x^3*cos(x)-12*x^2*sin(x)-24*x*cos(x)+24*sin(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_EXP,  5, "exp(x)*(x^5-5*x^4+20*x^3-60*x^2+120*x-120)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_LN,   4, "x^5*ln(x)/5-x^5/25" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_LN,   5, "x^6*ln(x)/6-x^6/36" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_SINH, 4, "x^4*cosh(x)-4*x^3*sinh(x)+12*x^2*cosh(x)-24*x*sinh(x)+24*cosh(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_COSH, 4, "x^4*sinh(x)-4*x^3*cosh(x)+12*x^2*sinh(x)-24*x*cosh(x)+24*sinh(x)" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_ATAN, 2, "((2*x^3-x^2-1)*atan(x)+x)/6" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_ATAN, 3, "((6*x^4-2*x^3+x^2+1)*atan(x)-x)/24" },
    { PRO_INT_K_XN_FUNC, PRO_INT_F_SQRT, 2, "2*x^3*sqrt(x)/7" },

    /* 128 收尾：x^3·sqrt(x) */
    { PRO_INT_K_XN_FUNC, PRO_INT_F_SQRT, 3, "2*x^4*sqrt(x)/9" }
};

AC_STATIC_ASSERT(sizeof(s_pro_int_table) / sizeof(s_pro_int_table[0]) ==
                 AC_PRO_INT_TABLE_SIZE, pro_int_table_has_128_entries);

static int pro_int_is_one(const pro_int_pool_t *P, int i) {
    return pro_int_is_const(P, i) && P->n[i].ival == 1;
}

static int pro_int_is_xpow(const pro_int_pool_t *P, int i, long *pn) {
    if (i < 0 || i >= P->count) { return 0; }
    if (P->n[i].op == PRO_INT_OP_VAR && P->n[i].ival == PRO_INT_V_X) {
        *pn = 1;
        return 1;
    }
    if (P->n[i].op == PRO_INT_OP_POW) {
        int base = P->n[i].a;
        if (base >= 0 && P->n[base].op == PRO_INT_OP_VAR &&
            P->n[base].ival == PRO_INT_V_X) {
            *pn = P->n[i].ival;
            return 1;
        }
    }
    return 0;
}

static int pro_int_is_x2(const pro_int_pool_t *P, int i) {
    long n = 0;
    return pro_int_is_xpow(P, i, &n) && n == 2;
}

static int pro_int_is_sqrt_of(const pro_int_pool_t *P, int i, int *parg) {
    if (i < 0 || i >= P->count) { return 0; }
    if (P->n[i].op != PRO_INT_OP_FUNC) { return 0; }
    if (P->n[i].ival != PRO_INT_F_SQRT) { return 0; }
    *parg = P->n[i].a;
    return 1;
}

static int pro_int_is_lin_func(const pro_int_pool_t *P, int i, int *pu,
                               long *pa, long *pb, int *pfid);

static int pro_int_linear(const pro_int_pool_t *P, int r, long *pa, long *pb,
                          int depth) {
    long a2;
    long b2;

    if (r < 0 || r >= P->count || depth > PRO_INT_MAX_DEPTH) { return 0; }

    switch (P->n[r].op) {
    case PRO_INT_OP_CONST:
        *pa = 0;
        *pb = P->n[r].ival;
        return 1;
    case PRO_INT_OP_VAR:
        if (P->n[r].ival != PRO_INT_V_X) { return 0; }
        *pa = 1;
        *pb = 0;
        return 1;
    case PRO_INT_OP_NEG:
        if (!pro_int_linear(P, P->n[r].a, pa, pb, depth + 1)) { return 0; }
        *pa = -*pa;
        *pb = -*pb;
        return 1;
    case PRO_INT_OP_ADD:
    case PRO_INT_OP_SUB: {
        long sgn = (P->n[r].op == PRO_INT_OP_ADD) ? 1 : -1;
        if (!pro_int_linear(P, P->n[r].a, pa, pb, depth + 1)) { return 0; }
        if (!pro_int_linear(P, P->n[r].b, &a2, &b2, depth + 1)) { return 0; }
        *pa += sgn * a2;
        *pb += sgn * b2;
        return 1;
    }
    case PRO_INT_OP_MUL:
        if (pro_int_is_const(P, P->n[r].a)) {
            if (!pro_int_linear(P, P->n[r].b, &a2, &b2, depth + 1)) { return 0; }
            *pa = P->n[P->n[r].a].ival * a2;
            *pb = P->n[P->n[r].a].ival * b2;
            return 1;
        }
        if (pro_int_is_const(P, P->n[r].b)) {
            if (!pro_int_linear(P, P->n[r].a, &a2, &b2, depth + 1)) { return 0; }
            *pa = P->n[P->n[r].b].ival * a2;
            *pb = P->n[P->n[r].b].ival * b2;
            return 1;
        }
        return 0;
    case PRO_INT_OP_DIV: {
        long d;
        if (!pro_int_is_const(P, P->n[r].b)) { return 0; }
        d = P->n[P->n[r].b].ival;
        if (d == 0) { return 0; }
        if (!pro_int_linear(P, P->n[r].a, &a2, &b2, depth + 1)) { return 0; }
        if ((a2 % d) != 0 || (b2 % d) != 0) { return 0; }
        *pa = a2 / d;
        *pb = b2 / d;
        return 1;
    }
    default:
        return 0;
    }
}

static int pro_int_is_lin_func(const pro_int_pool_t *P, int i, int *pu,
                               long *pa, long *pb, int *pfid) {
    long a = 0;
    long b = 0;
    if (i < 0 || i >= P->count) { return 0; }
    if (P->n[i].op != PRO_INT_OP_FUNC) { return 0; }
    if (!pro_int_linear(P, P->n[i].a, &a, &b, 0)) { return 0; }
    if (a == 0) { return 0; }
    *pu = P->n[i].a;
    *pa = a;
    *pb = b;
    *pfid = (int)P->n[i].ival;
    return 1;
}

/* 命中返回 1（*bd 已填），未命中 0，池满 -1。 */

/* v2.3.0 功能 4：新 kind 的共用匹配辅助
 * 全部只读池节点 + 写调用方 bind，零存储。 */

/* 完全平方判定：m = k² 且 k ≥ 2 时返回 k，否则 0（k=1 的形态归老表管）。 */
static long pro_int_sqrt_perfect(long m) {
    long k;
    if (m < 4) { return 0; }
    for (k = 2; k * k <= m; k++) {
        if (k * k == m) { return k; }
    }
    return 0;
}

/* u² 判定：node 是 POW(lin,2) 且 lin 斜率非零（u 允许 a*x+b）。 */
static int pro_int_is_usq(pro_int_pool_t *P, int i, int *pu, long *pa, long *pb) {
    if (i < 0 || i >= P->count) { return 0; }
    if (P->n[i].op != PRO_INT_OP_POW || P->n[i].ival != 2) { return 0; }
    if (!pro_int_linear(P, P->n[i].a, pa, pb, 0)) { return 0; }
    if (*pa == 0) { return 0; }
    *pu = P->n[i].a;
    return 1;
}

/* 二次二项 Q = s·u² + c 的分解（s = ±1，c 已带符号、非零）。
 * Q 必须是 ADD/SUB：一侧 u²、另一侧非零常量，两侧顺序不限。 */
static int pro_int_decomp_quad(pro_int_pool_t *P, int q, int *pu, long *pa,
                               long *pb, long *pc, int *pu2sign) {
    long c = 0;
    if (q < 0 || q >= P->count) { return 0; }
    if (P->n[q].op != PRO_INT_OP_ADD && P->n[q].op != PRO_INT_OP_SUB) { return 0; }
    if (pro_int_is_usq(P, P->n[q].a, pu, pa, pb)) {
        if (!pro_int_is_const(P, P->n[q].b)) { return 0; }
        c = P->n[P->n[q].b].ival;
        if (c == 0) { return 0; }
        *pu2sign = 1;
        *pc = (P->n[q].op == PRO_INT_OP_ADD) ? c : -c;
        return 1;
    }
    if (!pro_int_is_usq(P, P->n[q].b, pu, pa, pb)) { return 0; }
    if (!pro_int_is_const(P, P->n[q].a)) { return 0; }
    c = P->n[P->n[q].a].ival;
    if (c == 0) { return 0; }
    *pu2sign = (P->n[q].op == PRO_INT_OP_ADD) ? 1 : -1;
    *pc = c;
    return 1;
}

static int pro_int_match_entry(pro_int_pool_t *P, const pro_int_entry_t *e,
                               int r, pro_int_bind_t *bd) {
    long n = 0;
    long a = 0;
    long b = 0;
    int u = -1;
    int fid = 0;

    bd->u = -1;
    bd->a = 1;
    bd->b = 0;
    bd->n = 0;

    if (r < 0 || r >= P->count) { return 0; }

    switch (e->kind) {
    case PRO_INT_K_CONST:
        if (P->n[r].op != PRO_INT_OP_CONST) { return 0; }
        u = pro_int_var_x(P);
        if (u < 0) { return -1; }
        bd->u = u;
        bd->n = P->n[r].ival;
        return 1;

    case PRO_INT_K_XN:
        if (!pro_int_is_xpow(P, r, &n)) { return 0; }
        if (e->pow >= 0 && n != (long)e->pow) { return 0; }
        u = pro_int_var_x(P);
        if (u < 0) { return -1; }
        bd->u = u;
        bd->n = n;
        return 1;

    case PRO_INT_K_INV:
        if (P->n[r].op != PRO_INT_OP_DIV) { return 0; }
        if (!pro_int_is_one(P, P->n[r].a)) { return 0; }
        if (!pro_int_linear(P, P->n[r].b, &a, &b, 0)) { return 0; }
        if (a == 0) { return 0; }
        bd->u = P->n[r].b;
        bd->a = a;
        bd->b = b;
        return 1;

    case PRO_INT_K_INV2: {
        int den;
        if (P->n[r].op != PRO_INT_OP_DIV) { return 0; }
        if (!pro_int_is_one(P, P->n[r].a)) { return 0; }
        den = P->n[r].b;
        if (P->n[den].op != PRO_INT_OP_POW) { return 0; }
        if (P->n[den].ival != 2) { return 0; }
        if (!pro_int_linear(P, P->n[den].a, &a, &b, 0)) { return 0; }
        if (a != 1 || b != 0) { return 0; }   /* 只认 1/x^2 */
        bd->u = P->n[den].a;
        return 1;
    }

    case PRO_INT_K_ATAN_KER:
    case PRO_INT_K_ATANH_KER: {
        int den;
        int k1;
        if (P->n[r].op != PRO_INT_OP_DIV) { return 0; }
        if (!pro_int_is_one(P, P->n[r].a)) { return 0; }
        den = P->n[r].b;
        if (P->n[den].op != PRO_INT_OP_ADD && P->n[den].op != PRO_INT_OP_SUB) {
            return 0;
        }
        k1 = (P->n[den].op == PRO_INT_OP_ADD);
        if (e->kind == PRO_INT_K_ATAN_KER && !k1) { return 0; }
        if (e->kind == PRO_INT_K_ATANH_KER && k1) { return 0; }
        if (!pro_int_is_one(P, P->n[den].a)) { return 0; }
        if (!pro_int_is_x2(P, P->n[den].b)) { return 0; }
        u = pro_int_var_x(P);
        if (u < 0) { return -1; }
        bd->u = u;
        return 1;
    }

    case PRO_INT_K_ASIN_KER:
    case PRO_INT_K_ASINH_KER:
    case PRO_INT_K_ACOSH_KER: {
        int den;
        int arg;
        int k1;
        if (P->n[r].op != PRO_INT_OP_DIV) { return 0; }
        if (!pro_int_is_one(P, P->n[r].a)) { return 0; }
        den = P->n[r].b;
        if (!pro_int_is_sqrt_of(P, den, &arg)) { return 0; }
        if (P->n[arg].op != PRO_INT_OP_ADD && P->n[arg].op != PRO_INT_OP_SUB) {
            return 0;
        }
        k1 = (P->n[arg].op == PRO_INT_OP_ADD);
            if (e->kind != PRO_INT_K_ACOSH_KER) {
            if (!pro_int_is_one(P, P->n[arg].a)) { return 0; }
            if (!pro_int_is_x2(P, P->n[arg].b)) { return 0; }
            if (e->kind == PRO_INT_K_ASIN_KER && k1) { return 0; }
            if (e->kind == PRO_INT_K_ASINH_KER && !k1) { return 0; }
        } else {
                    if (k1) { return 0; }
            if (!pro_int_is_x2(P, P->n[arg].a)) { return 0; }
            if (!pro_int_is_one(P, P->n[arg].b)) { return 0; }
        }
        u = pro_int_var_x(P);
        if (u < 0) { return -1; }
        bd->u = u;
        return 1;
    }

    case PRO_INT_K_CIRCLE:
    case PRO_INT_K_HYPER: {
        int arg;
        int k1;
        if (!pro_int_is_sqrt_of(P, r, &arg)) { return 0; }
        if (P->n[arg].op != PRO_INT_OP_ADD && P->n[arg].op != PRO_INT_OP_SUB) {
            return 0;
        }
        k1 = (P->n[arg].op == PRO_INT_OP_ADD);
        if (e->kind == PRO_INT_K_CIRCLE && k1) { return 0; }
        if (e->kind == PRO_INT_K_HYPER && !k1) { return 0; }
        if (!pro_int_is_one(P, P->n[arg].a)) { return 0; }
        if (!pro_int_is_x2(P, P->n[arg].b)) { return 0; }
        u = pro_int_var_x(P);
        if (u < 0) { return -1; }
        bd->u = u;
        return 1;
    }

    case PRO_INT_K_FUNC:
        if (!pro_int_is_lin_func(P, r, &u, &a, &b, &fid)) { return 0; }
        if (fid != (int)e->func) { return 0; }
        bd->u = u;
        bd->a = a;
        bd->b = b;
        return 1;

    case PRO_INT_K_FUNC_POW: {
        int base;
        if (P->n[r].op != PRO_INT_OP_POW) { return 0; }
        if (P->n[r].ival != (long)e->pow) { return 0; }
        base = P->n[r].a;
        if (!pro_int_is_lin_func(P, base, &u, &a, &b, &fid)) { return 0; }
        if (fid != (int)e->func) { return 0; }
        bd->u = u;
        bd->a = a;
        bd->b = b;
        bd->n = P->n[r].ival;
        return 1;
    }

    case PRO_INT_K_XN_FUNC: {
        int fnode;
        int xnode;
        long m = 0;
        if (P->n[r].op != PRO_INT_OP_MUL) { return 0; }
        if (P->n[P->n[r].a].op == PRO_INT_OP_FUNC) {
            fnode = P->n[r].a;
            xnode = P->n[r].b;
        } else if (P->n[P->n[r].b].op == PRO_INT_OP_FUNC) {
            fnode = P->n[r].b;
            xnode = P->n[r].a;
        } else {
            return 0;
        }
        if (P->n[fnode].ival != (long)e->func) { return 0; }
        /* 本组不支持线性内层，实参必须就是 x */
        if (P->n[P->n[fnode].a].op != PRO_INT_OP_VAR) { return 0; }
        if (P->n[P->n[fnode].a].ival != PRO_INT_V_X) { return 0; }
        if (!pro_int_is_xpow(P, xnode, &m)) { return 0; }
        if (e->pow >= 0 && m != (long)e->pow) { return 0; }
        bd->u = P->n[fnode].a;
        bd->n = m;
        return 1;
    }

    /* v2.3.0 功能 4：QUAD_L / QUAD_K
     * 三种外层形式：r = 1/Q（form 0）、r = 1/sqrt(Q)（form 1）、
     * r = sqrt(Q)（form 2）。形态码见表注释。 */
    case PRO_INT_K_QUAD_L:
    case PRO_INT_K_QUAD_K: {
        int shape = (int)e->pow;
        int form;
        int q = -1;
        int u2 = -1;
        int u2s = 0;
        int ok = 0;
        long a = 0;
        long b = 0;
        long c = 0;
        if (e->kind == PRO_INT_K_QUAD_L) {
            if (shape < 0 || shape > 7) { return 0; }
            form = (shape <= 1) ? 0 : (shape <= 4) ? 1 : 2;
        } else {
            if (shape < 0 || shape > 8) { return 0; }
            form = (shape == 8) ? 0 : (shape <= 1) ? 0
                 : (shape <= 4) ? 1 : 2;
        }
        if (form == 0) {
            if (P->n[r].op != PRO_INT_OP_DIV) { return 0; }
            if (!pro_int_is_one(P, P->n[r].a)) { return 0; }
            q = P->n[r].b;
        } else if (form == 1) {
            if (P->n[r].op != PRO_INT_OP_DIV) { return 0; }
            if (!pro_int_is_one(P, P->n[r].a)) { return 0; }
            if (!pro_int_is_sqrt_of(P, P->n[r].b, &q)) { return 0; }
        } else {
            if (!pro_int_is_sqrt_of(P, r, &q)) { return 0; }
        }
        if (!pro_int_decomp_quad(P, q, &u2, &a, &b, &c, &u2s)) { return 0; }
        if (e->kind == PRO_INT_K_QUAD_L) {
            /* k=1：常量必须是 ±1（c 已带 Q 的符号） */
            switch (shape) {
            case 0: ok = (u2s > 0 && c == 1);  break;   /* 1+u² */
            case 1: ok = (u2s < 0 && c == 1);  break;   /* 1-u² */
            case 2: ok = (u2s < 0 && c == 1);  break;   /* sqrt(1-u²) */
            case 3: ok = (u2s > 0 && c == 1);  break;   /* sqrt(1+u²) */
            case 4: ok = (u2s > 0 && c == -1); break;   /* sqrt(u²-1) */
            case 5: ok = (u2s < 0 && c == 1);  break;
            case 6: ok = (u2s > 0 && c == 1);  break;
            case 7: ok = (u2s > 0 && c == -1); break;
            default: ok = 0; break;
            }
            if (!ok) { return 0; }
            bd->u = u2;
            bd->a = a;
            bd->b = b;
            return 1;
        }
        {
            long k = pro_int_sqrt_perfect(c > 0 ? c : -c);
            if (k == 0) { return 0; }   /* 常量非完全平方：诚实拒绝 */
            switch (shape) {
            case 0: ok = (u2s > 0 && c > 0);  break;   /* k²+u² */
            case 1: ok = (u2s < 0 && c > 0);  break;   /* k²-u² */
            case 2: ok = (u2s < 0 && c > 0);  break;   /* 1/sqrt(k²-u²) */
            case 3: ok = (u2s > 0 && c > 0);  break;   /* 1/sqrt(u²+k²) */
            case 4: ok = (u2s > 0 && c < 0);  break;   /* 1/sqrt(u²-k²) */
            case 5: ok = (u2s < 0 && c > 0);  break;   /* sqrt(k²-u²) */
            case 6: ok = (u2s > 0 && c > 0);  break;   /* sqrt(u²+k²) */
            case 7: ok = (u2s > 0 && c < 0);  break;   /* sqrt(u²-k²) */
            case 8: ok = (u2s > 0 && c < 0);  break;   /* 1/(u²-k²) */
            default: ok = 0; break;
            }
            if (!ok) { return 0; }
            bd->u = u2;
            bd->a = a;
            bd->b = b;
            bd->n = k;
            bd->m = k * k;
            return 1;
        }
    }

    /* u/(u²±m)、u/(m-u²)：分子与 u² 的底是同一线性式 */
    case PRO_INT_K_XQUAD: {
        int shape = (int)e->pow;
        int u2 = -1;
        int u2s = 0;
        long a = 0;
        long b = 0;
        long c = 0;
        long a2 = 0;
        long b2 = 0;
        if (shape < 0 || shape > 2) { return 0; }
        if (P->n[r].op != PRO_INT_OP_DIV) { return 0; }
        if (!pro_int_decomp_quad(P, P->n[r].b, &u2, &a, &b, &c, &u2s)) { return 0; }
        if (!pro_int_linear(P, P->n[r].a, &a2, &b2, 0)) { return 0; }
        if (a2 == 0 || a2 != a || b2 != b) { return 0; }
        if (shape == 0 && !(u2s > 0 && c > 0)) { return 0; }
        if (shape == 1 && !(u2s > 0 && c < 0)) { return 0; }
        if (shape == 2 && !(u2s < 0 && c > 0)) { return 0; }
        bd->u = P->n[r].a;
        bd->a = a;
        bd->b = b;
        bd->m = (c > 0) ? c : -c;
        return 1;
    }

    /* 1/f(u)：sec / csc */
    case PRO_INT_K_FUNC_INV: {
        long a = 0;
        long b = 0;
        int fid = 0;
        if (P->n[r].op != PRO_INT_OP_DIV) { return 0; }
        if (!pro_int_is_one(P, P->n[r].a)) { return 0; }
        if (!pro_int_is_lin_func(P, P->n[r].b, &u, &a, &b, &fid)) { return 0; }
        if (fid != (int)e->func) { return 0; }
        bd->u = u;
        bd->a = a;
        bd->b = b;
        return 1;
    }

    /* 1/f(u)^2：sec²/csc²/cot²/sech²/csch²/coth² */
    case PRO_INT_K_FUNC_INVP: {
        int base = -1;
        long a = 0;
        long b = 0;
        int fid = 0;
        if (P->n[r].op != PRO_INT_OP_DIV) { return 0; }
        if (!pro_int_is_one(P, P->n[r].a)) { return 0; }
        base = P->n[r].b;
        if (base < 0 || base >= P->count) { return 0; }
        if (P->n[base].op != PRO_INT_OP_POW || P->n[base].ival != 2) { return 0; }
        if (!pro_int_is_lin_func(P, P->n[base].a, &u, &a, &b, &fid)) { return 0; }
        if (fid != (int)e->func) { return 0; }
        bd->u = u;
        bd->a = a;
        bd->b = b;
        return 1;
    }

    /* f(u)/g(u)（同内层）：cot / coth */
    case PRO_INT_K_FUNC_DIV: {
        int u1 = -1;
        int u2 = -1;
        int f1 = 0;
        int f2 = 0;
        long a1 = 0;
        long b1 = 0;
        long a2 = 0;
        long b2 = 0;
        if (P->n[r].op != PRO_INT_OP_DIV) { return 0; }
        if (!pro_int_is_lin_func(P, P->n[r].a, &u1, &a1, &b1, &f1)) { return 0; }
        if (!pro_int_is_lin_func(P, P->n[r].b, &u2, &a2, &b2, &f2)) { return 0; }
        if (f1 != (int)e->func || f2 != (int)e->pow) { return 0; }
        if (a1 != a2 || b1 != b2) { return 0; }
        bd->u = u1;
        bd->a = a1;
        bd->b = b1;
        return 1;
    }

    /* f(u)·g(u)（同内层）：sin·cos、sinh·cosh（顺序不限） */
    case PRO_INT_K_FUNC_FUNC: {
        int u1 = -1;
        int u2 = -1;
        int f1 = 0;
        int f2 = 0;
        long a1 = 0;
        long b1 = 0;
        long a2 = 0;
        long b2 = 0;
        if (P->n[r].op != PRO_INT_OP_MUL) { return 0; }
        if (!pro_int_is_lin_func(P, P->n[r].a, &u1, &a1, &b1, &f1) ||
            !pro_int_is_lin_func(P, P->n[r].b, &u2, &a2, &b2, &f2)) {
            return 0;
        }
        if (a1 != a2 || b1 != b2) { return 0; }
        if (f1 == (int)e->func && f2 == (int)e->pow) {
            bd->u = u1;
        } else if (f1 == (int)e->pow && f2 == (int)e->func) {
            bd->u = u2;
        } else {
            return 0;
        }
        bd->a = a1;
        bd->b = b1;
        return 1;
    }

    /* (1/f(u))·g(u)：sec·tan、csc·cot（MUL 两侧顺序不限） */
    case PRO_INT_K_SECTAN: {
        int inv = -1;
        int gnode = -1;
        int u1 = -1;
        int u2 = -1;
        int f1 = 0;
        int f2 = 0;
        long a1 = 0;
        long b1 = 0;
        long a2 = 0;
        long b2 = 0;
        if (P->n[r].op != PRO_INT_OP_MUL) { return 0; }
        if (P->n[P->n[r].a].op == PRO_INT_OP_DIV) {
            inv = P->n[r].a;
            gnode = P->n[r].b;
        } else if (P->n[P->n[r].b].op == PRO_INT_OP_DIV) {
            inv = P->n[r].b;
            gnode = P->n[r].a;
        } else {
            return 0;
        }
        if (!pro_int_is_one(P, P->n[inv].a)) { return 0; }
        if (!pro_int_is_lin_func(P, P->n[inv].b, &u1, &a1, &b1, &f1)) { return 0; }
        if (f1 != (int)e->func) { return 0; }
        if (!pro_int_is_lin_func(P, gnode, &u2, &a2, &b2, &f2)) { return 0; }
        if (f2 != (int)e->pow) { return 0; }
        if (a1 != a2 || b1 != b2) { return 0; }
        bd->u = u1;
        bd->a = a1;
        bd->b = b1;
        return 1;
    }

    /* f(u)/g(u)^2：sec·tan、csc·cot 的除式拼法，及 -sech、-csch */
    case PRO_INT_K_FUNCOVERFP: {
        int u1 = -1;
        int u2 = -1;
        int f1 = 0;
        int f2 = 0;
        long a1 = 0;
        long b1 = 0;
        long a2 = 0;
        long b2 = 0;
        int base = -1;
        if (P->n[r].op != PRO_INT_OP_DIV) { return 0; }
        if (!pro_int_is_lin_func(P, P->n[r].a, &u1, &a1, &b1, &f1)) { return 0; }
        base = P->n[r].b;
        if (base < 0 || base >= P->count) { return 0; }
        if (P->n[base].op != PRO_INT_OP_POW || P->n[base].ival != 2) { return 0; }
        if (!pro_int_is_lin_func(P, P->n[base].a, &u2, &a2, &b2, &f2)) { return 0; }
        if (f1 != (int)e->func || f2 != (int)e->pow) { return 0; }
        if (a1 != a2 || b1 != b2) { return 0; }
        bd->u = u1;
        bd->a = a1;
        bd->b = b1;
        return 1;
    }

    /* exp(u)^n（n≥2） */
    case PRO_INT_K_EXP_POW: {
        long a = 0;
        long b = 0;
        int fid = 0;
        if (P->n[r].op != PRO_INT_OP_POW) { return 0; }
        if (P->n[r].ival < 2) { return 0; }
        if (!pro_int_is_lin_func(P, P->n[r].a, &u, &a, &b, &fid)) { return 0; }
        if (fid != PRO_INT_F_EXP) { return 0; }
        bd->u = u;
        bd->a = a;
        bd->b = b;
        bd->n = P->n[r].ival;
        return 1;
    }

    /* sin(u)^2·cos(u)^2（顺序不限） */
    case PRO_INT_K_SIN2COS2: {
        int u1 = -1;
        int u2 = -1;
        int f1 = 0;
        int f2 = 0;
        long a1 = 0;
        long b1 = 0;
        long a2 = 0;
        long b2 = 0;
        int p1 = -1;
        int p2 = -1;
        if (P->n[r].op != PRO_INT_OP_MUL) { return 0; }
        p1 = P->n[r].a;
        p2 = P->n[r].b;
        if (P->n[p1].op != PRO_INT_OP_POW || P->n[p1].ival != 2) { return 0; }
        if (P->n[p2].op != PRO_INT_OP_POW || P->n[p2].ival != 2) { return 0; }
        if (!pro_int_is_lin_func(P, P->n[p1].a, &u1, &a1, &b1, &f1)) { return 0; }
        if (!pro_int_is_lin_func(P, P->n[p2].a, &u2, &a2, &b2, &f2)) { return 0; }
        if (a1 != a2 || b1 != b2) { return 0; }
        if (f1 == PRO_INT_F_SIN && f2 == PRO_INT_F_COS) { bd->u = u1; }
        else if (f1 == PRO_INT_F_COS && f2 == PRO_INT_F_SIN) { bd->u = u2; }
        else { return 0; }
        bd->a = a1;
        bd->b = b1;
        return 1;
    }

    /* ln(u)/u：分子 ln 与分母是同一线性式 */
    case PRO_INT_K_LN_OVER_X: {
        long a1 = 0;
        long b1 = 0;
        long a2 = 0;
        long b2 = 0;
        int fid = 0;
        if (P->n[r].op != PRO_INT_OP_DIV) { return 0; }
        if (!pro_int_is_lin_func(P, P->n[r].a, &u, &a1, &b1, &fid)) { return 0; }
        if (fid != PRO_INT_F_LN) { return 0; }
        if (!pro_int_linear(P, P->n[r].b, &a2, &b2, 0)) { return 0; }
        if (a1 != a2 || b1 != b2) { return 0; }
        bd->u = u;
        bd->a = a1;
        bd->b = b1;
        return 1;
    }

    /* 1/(u·ln(u))：MUL 两侧 = 同一线性式与 ln(该式) */
    case PRO_INT_K_ONE_OVER_ULN: {
        int mulnode = -1;
        int s = -1;
        int t = -1;
        int ulin = -1;
        int u2 = -1;
        int fid = 0;
        long a1 = 0;
        long b1 = 0;
        long a2 = 0;
        long b2 = 0;
        if (P->n[r].op != PRO_INT_OP_DIV) { return 0; }
        if (!pro_int_is_one(P, P->n[r].a)) { return 0; }
        mulnode = P->n[r].b;
        if (mulnode < 0 || mulnode >= P->count) { return 0; }
        if (P->n[mulnode].op != PRO_INT_OP_MUL) { return 0; }
        s = P->n[mulnode].a;
        t = P->n[mulnode].b;
        if (pro_int_is_lin_func(P, t, &u2, &a2, &b2, &fid) && fid == PRO_INT_F_LN &&
            pro_int_linear(P, s, &a1, &b1, 0) && a1 == a2 && b1 == b2) {
            ulin = s;
        } else if (pro_int_is_lin_func(P, s, &u2, &a2, &b2, &fid) && fid == PRO_INT_F_LN &&
                   pro_int_linear(P, t, &a1, &b1, 0) && a1 == a2 && b1 == b2) {
            ulin = t;
        } else {
            return 0;
        }
        bd->u = ulin;
        bd->a = a1;
        bd->b = b1;
        return 1;
    }

    default:
        return 0;
    }
}

/* 表匹配只看形状，看不见 (x+1)^2 展开后的样子。这里做一次有界的多项式
 * 归一化：只收纯多项式（无 FUNC 节点、次数 <= PRO_INT_POLY_MAX），
 * 系数用栈上数组。 */
#define PRO_INT_POLY_MAX 16

static int pro_int_poly_fetch(const pro_int_pool_t *P, int r, long *c, int depth) {
    long t1[PRO_INT_POLY_MAX + 1];
    long t2[PRO_INT_POLY_MAX + 1];
    int i;
    int j;

    if (r < 0 || r >= P->count || depth > PRO_INT_MAX_DEPTH) { return 0; }
    for (i = 0; i <= PRO_INT_POLY_MAX; i++) { c[i] = 0; }

    switch (P->n[r].op) {
    case PRO_INT_OP_CONST:
        c[0] = P->n[r].ival;
        return 1;
    case PRO_INT_OP_VAR:
        if (P->n[r].ival != PRO_INT_V_X) { return 0; }
        c[1] = 1;
        return 1;
    case PRO_INT_OP_NEG:
        if (!pro_int_poly_fetch(P, P->n[r].a, c, depth + 1)) { return 0; }
        for (i = 0; i <= PRO_INT_POLY_MAX; i++) { c[i] = -c[i]; }
        return 1;
    case PRO_INT_OP_ADD:
    case PRO_INT_OP_SUB: {
        long sgn = (P->n[r].op == PRO_INT_OP_ADD) ? 1 : -1;
        if (!pro_int_poly_fetch(P, P->n[r].a, c, depth + 1)) { return 0; }
        if (!pro_int_poly_fetch(P, P->n[r].b, t1, depth + 1)) { return 0; }
        for (i = 0; i <= PRO_INT_POLY_MAX; i++) { c[i] += sgn * t1[i]; }
        return 1;
    }
    case PRO_INT_OP_DIV: {
        long d;
        if (!pro_int_is_const(P, P->n[r].b)) { return 0; }
        d = P->n[P->n[r].b].ival;
        if (d == 0) { return 0; }
        if (!pro_int_poly_fetch(P, P->n[r].a, c, depth + 1)) { return 0; }
        for (i = 0; i <= PRO_INT_POLY_MAX; i++) {
            if ((c[i] % d) != 0) { return 0; }
            c[i] /= d;
        }
        return 1;
    }
    case PRO_INT_OP_MUL: {
        if (!pro_int_poly_fetch(P, P->n[r].a, t1, depth + 1)) { return 0; }
        if (!pro_int_poly_fetch(P, P->n[r].b, t2, depth + 1)) { return 0; }
        for (i = 0; i <= PRO_INT_POLY_MAX; i++) { c[i] = 0; }
        for (i = 0; i <= PRO_INT_POLY_MAX; i++) {
            if (t1[i] == 0) { continue; }
            for (j = 0; i + j <= PRO_INT_POLY_MAX; j++) {
                if (t2[j] == 0) { continue; }
                c[i + j] += t1[i] * t2[j];
                if (c[i + j] > PRO_INT_MAX_CONST || c[i + j] < -PRO_INT_MAX_CONST) {
                    return 0;
                }
            }
        }
        return 1;
    }
    case PRO_INT_OP_POW: {
        long e = P->n[r].ival;
        long base[PRO_INT_POLY_MAX + 1];
        long acc[PRO_INT_POLY_MAX + 1];
        long k;
        if (e < 0 || e > PRO_INT_POLY_MAX) { return 0; }
        if (!pro_int_poly_fetch(P, P->n[r].a, base, depth + 1)) { return 0; }
        for (i = 0; i <= PRO_INT_POLY_MAX; i++) { acc[i] = 0; }
        acc[0] = 1;
        for (k = 0; k < e; k++) {
            for (i = 0; i <= PRO_INT_POLY_MAX; i++) { c[i] = 0; }
            for (i = 0; i <= PRO_INT_POLY_MAX; i++) {
                if (acc[i] == 0) { continue; }
                for (j = 0; i + j <= PRO_INT_POLY_MAX; j++) {
                    if (base[j] == 0) { continue; }
                    c[i + j] += acc[i] * base[j];
                    if (c[i + j] > PRO_INT_MAX_CONST ||
                        c[i + j] < -PRO_INT_MAX_CONST) {
                        return 0;
                    }
                }
            }
            for (i = 0; i <= PRO_INT_POLY_MAX; i++) { acc[i] = c[i]; }
        }
        for (i = 0; i <= PRO_INT_POLY_MAX; i++) { c[i] = acc[i]; }
        return 1;
    }
    default:
        return 0;
    }
}

/* 逐项积分；1 = 命中，0 = 非多项式。 */
static int pro_int_poly_integrate(pro_int_pool_t *P, int r, int *out) {
    long c[PRO_INT_POLY_MAX + 1];
    int sum = -1;
    int k;

    if (!pro_int_poly_fetch(P, r, c, 0)) { return 0; }
    for (k = PRO_INT_POLY_MAX; k >= 0; k--) {
        long coef = c[k];
        long d = (long)k + 1;
        int term;
        int pw;
        if (coef == 0) { continue; }
        pw = pro_int_xpow(P, d);
        if (pw < 0) { return 0; }
        if ((coef % d) == 0) {
            term = pro_int_mul(P, pro_int_const(P, coef / d), pw);
        } else {
            int num = pro_int_mul(P, pro_int_const(P, coef), pw);
            if (num < 0) { return 0; }
            term = pro_int_div(P, num, pro_int_const(P, d));
        }
        if (term < 0) { return 0; }
        sum = (sum < 0) ? term : pro_int_add(P, sum, term);
        if (sum < 0) { return 0; }
    }
    if (sum < 0) { sum = pro_int_const(P, 0); }
    if (sum < 0) { return 0; }
    *out = sum;
    return 1;
}

/* 进入前确认共享 arena 还容得下表匹配路径的 256 节点。 */
static int pro_int_budget_enter(int base) {
    int remain;
    if (base < 0 || base > AC_SYM_MAX_NODES) { return 0; }
    remain = AC_SYM_MAX_NODES - base;
    return (remain >= PRO_INT_BUDGET_TABLE);
}

/* need 是真实消耗的节点数，cap 是该路径上限；同时查 arena 不越界。 */
static int pro_int_budget_ok(int base, int need, int cap) {
    if (base < 0 || need < 0 || cap <= 0) { return 0; }
    if (cap > AC_SYM_MAX_NODES) { return 0; }
    if (need > cap) { return 0; }
    if (base + need > AC_SYM_MAX_NODES) { return 0; }
    return 1;
}

static int pro_int_integrate_rec(pro_int_pool_t *P, int r, int depth,
                                 int parts_left, int *out);

/* 分部积分。parts_left ∈ {1,2}：本层用掉一次，内层带 parts_left-1 收尾；
 * 到 0 后内层只查表/多项式，绝不展开，固定两层，硬编码上界。
 * sin 的 v 取 -cos，所以这一支是 -x^n·cos + n·∫x^(n-1)·cos。 */
static int pro_int_try_parts(pro_int_pool_t *P, int r, int depth,
                             int parts_left, int *out) {
    int fnode;
    int xnode;
    long n = 0;
    int fid;
    int vsign;
    int f_anti_id;
    int inner_expr;
    int inner_anti = -1;
    int term1;
    int term2;
    int res;
    int rc;

    if (depth > PRO_INT_MAX_DEPTH) { return AC_PRO_INT_ETABLE; }
    if (r < 0 || P->n[r].op != PRO_INT_OP_MUL) { return 0; }
    if (P->n[P->n[r].a].op == PRO_INT_OP_FUNC) {
        fnode = P->n[r].a;
        xnode = P->n[r].b;
    } else if (P->n[P->n[r].b].op == PRO_INT_OP_FUNC) {
        fnode = P->n[r].b;
        xnode = P->n[r].a;
    } else {
        return 0;
    }
    if (P->n[P->n[fnode].a].op != PRO_INT_OP_VAR) { return 0; }
    if (P->n[P->n[fnode].a].ival != PRO_INT_V_X) { return 0; }
    if (!pro_int_is_xpow(P, xnode, &n)) { return 0; }
    if (n < 1 || n > PRO_INT_MAX_POW_IN) { return 0; }

    fid = (int)P->n[fnode].ival;
    switch (fid) {
    case PRO_INT_F_EXP:  vsign = 1;  f_anti_id = PRO_INT_F_EXP;  break;
    case PRO_INT_F_COS:  vsign = 1;  f_anti_id = PRO_INT_F_SIN;  break;
    case PRO_INT_F_SIN:  vsign = -1; f_anti_id = PRO_INT_F_COS;  break;
    case PRO_INT_F_SINH: vsign = 1;  f_anti_id = PRO_INT_F_COSH; break;
    case PRO_INT_F_COSH: vsign = 1;  f_anti_id = PRO_INT_F_SINH; break;
    default: return 0;
    }

    P->parts_used = 1;

    inner_expr = pro_int_mul(P, pro_int_xpow(P, n - 1),
                             pro_int_func(P, f_anti_id, pro_int_var_x(P)));
    if (inner_expr < 0) { return AC_PRO_INT_EBUDGET; }

    rc = pro_int_integrate_rec(P, inner_expr, depth + 1, parts_left - 1,
                               &inner_anti);
    if (rc != 0) {
        /* 层数纪律：内层（含剩余层）失败就整体未命中 */
        return (rc == AC_PRO_INT_EBUDGET) ? rc : 0;
    }

    term1 = pro_int_mul(P, pro_int_xpow(P, n),
                        pro_int_func(P, f_anti_id, pro_int_var_x(P)));
    term2 = pro_int_mul(P, pro_int_const(P, n), inner_anti);
    if (term1 < 0 || term2 < 0) { return AC_PRO_INT_EBUDGET; }

    if (vsign < 0) {
        res = pro_int_add(P, pro_int_neg(P, term1), term2);
    } else {
        res = pro_int_sub(P, term1, term2);
    }
    if (res < 0) { return AC_PRO_INT_EBUDGET; }
    *out = res;
    return 0;
}

static int pro_int_integrate_rec(pro_int_pool_t *P, int r, int depth,
                                 int parts_left, int *out) {
    int k;
    int rc;

    if (depth > PRO_INT_MAX_DEPTH) { return AC_PRO_INT_ETABLE; }
    if (r < 0 || r >= P->count) { return AC_PRO_INT_EPARSE; }
    if (P->overflow) { return AC_PRO_INT_EBUDGET; }

    if (P->n[r].op == PRO_INT_OP_ADD || P->n[r].op == PRO_INT_OP_SUB) {
        int la = -1;
        int ra = -1;
        int res;
        rc = pro_int_integrate_rec(P, P->n[r].a, depth + 1, parts_left, &la);
        if (rc != 0) { return rc; }
        rc = pro_int_integrate_rec(P, P->n[r].b, depth + 1, parts_left, &ra);
        if (rc != 0) { return rc; }
        res = (P->n[r].op == PRO_INT_OP_ADD) ? pro_int_add(P, la, ra)
                                             : pro_int_sub(P, la, ra);
        if (res < 0) { return AC_PRO_INT_EBUDGET; }
        *out = res;
        return 0;
    }

    if (P->n[r].op == PRO_INT_OP_NEG) {
        int inner = -1;
        rc = pro_int_integrate_rec(P, P->n[r].a, depth + 1, parts_left, &inner);
        if (rc != 0) { return rc; }
        *out = pro_int_neg(P, inner);
        return (*out < 0) ? AC_PRO_INT_EBUDGET : 0;
    }

    /* 常数倍 c·f */
    if (P->n[r].op == PRO_INT_OP_MUL) {
        int cnode = -1;
        int other = -1;
        if (pro_int_is_const(P, P->n[r].a)) {
            cnode = P->n[r].a;
            other = P->n[r].b;
        } else if (pro_int_is_const(P, P->n[r].b)) {
            cnode = P->n[r].b;
            other = P->n[r].a;
        }
        if (cnode >= 0) {
            int inner = -1;
            long c = P->n[cnode].ival;
            int res;
            rc = pro_int_integrate_rec(P, other, depth + 1, parts_left, &inner);
            if (rc != 0) { return rc; }
            /* (c/d)·f：3·(x^3/3) 直接约成 x^3 */
            if (P->n[inner].op == PRO_INT_OP_DIV &&
                pro_int_is_const(P, P->n[inner].b)) {
                long d = P->n[P->n[inner].b].ival;
                if (d != 0 && (c % d) == 0) {
                    res = pro_int_mul(P, pro_int_const(P, c / d), P->n[inner].a);
                    *out = res;
                    return (res < 0) ? AC_PRO_INT_EBUDGET : 0;
                }
            }
            res = pro_int_mul(P, pro_int_const(P, c), inner);
            *out = res;
            return (res < 0) ? AC_PRO_INT_EBUDGET : 0;
        }
    }

    /* 除以常数 */
    if (P->n[r].op == PRO_INT_OP_DIV && pro_int_is_const(P, P->n[r].b)) {
        int inner = -1;
        long d = P->n[P->n[r].b].ival;
        if (d == 0) { return AC_PRO_INT_EPARSE; }
        rc = pro_int_integrate_rec(P, P->n[r].a, depth + 1, parts_left, &inner);
        if (rc != 0) { return rc; }
        *out = pro_int_div(P, inner, pro_int_const(P, d));
        return (*out < 0) ? AC_PRO_INT_EBUDGET : 0;
    }

    /* 64 项表匹配 */
    for (k = 0; k < AC_PRO_INT_TABLE_SIZE; k++) {
        pro_int_bind_t bd;
        int hit = pro_int_match_entry(P, &s_pro_int_table[k], r, &bd);
        if (hit < 0) { return AC_PRO_INT_EBUDGET; }
        if (hit == 1) {
            int saved_count = P->count;
            int saved_xvar = P->xvar;
            int saved_bind_set = 0;
            int root;
            int err = 0;
            (void)saved_bind_set;
            P->bind = &bd;
            root = pro_int_parse_string(s_pro_int_table[k].anti, P, &err);
            P->bind = 0;
            if (root < 0) {
                P->count = saved_count;      /* 回滚半构造的节点 */
                P->xvar = saved_xvar;
                P->overflow = 0;
                return (err == AC_PRO_INT_EBUDGET) ? AC_PRO_INT_EBUDGET
                                                   : AC_PRO_INT_EPARSE;
            }
            *out = root;
            return 0;
        }
    }

    /* 固定两层分部积分：parts_left 由顶层 2 逐层递减，0 即关死 */
    if (parts_left > 0) {
        int parts_out = -1;
        rc = pro_int_try_parts(P, r, depth, parts_left, &parts_out);
        if (rc != 0) { return rc; }
        if (parts_out >= 0) { *out = parts_out; return 0; }
    }

    /* 多项式展开 */
    if (pro_int_poly_integrate(P, r, out)) { return 0; }

    return AC_PRO_INT_ETABLE;
}

int ac_pro_integrate(const char *expr, char *out, int out_len) {
    pro_int_pool_t P;
    int pos = 0;
    int root;
    int anti = -1;
    int base;
    int err = 0;
    int rc;
    int cap;
    char tmp[PRO_INT_OUTBUF];

    if (out == 0 || out_len <= 0) { return AC_PRO_INT_EOVER; }
    out[0] = '\0';
    if (expr == 0) { return AC_PRO_INT_EPARSE; }

    /* 先看共享 arena 的余量 */
    base = g_sym_count;
    if (!pro_int_budget_enter(base)) {
        return AC_PRO_INT_EBUDGET;
    }

    pro_int_pool_init(&P);

    root = pro_int_parse_string(expr, &P, &err);
    if (root < 0) {
        if (err != 0) { return err; }
        return P.overflow ? AC_PRO_INT_EBUDGET : AC_PRO_INT_EPARSE;
    }

    rc = pro_int_integrate_rec(&P, root, 0, PRO_INT_PARTS_LAYERS, &anti);
    if (rc != 0) { return rc; }
    if (anti < 0) { return AC_PRO_INT_ETABLE; }

    cap = P.parts_used ? PRO_INT_BUDGET_TOTAL : PRO_INT_BUDGET_TABLE;
    if (!pro_int_budget_ok(base, P.count, cap)) { return AC_PRO_INT_EBUDGET; }

    tmp[0] = '\0';
    pos = 0;
    rc = pro_int_emit(&P, anti, tmp, PRO_INT_OUTBUF, &pos, 1);
    if (rc != 0) { return rc; }
    if (pos >= out_len) { return AC_PRO_INT_EOVER; }
    memcpy(out, tmp, (size_t)pos + 1);
    return AC_PRO_INT_OK;
}

#endif /* AC_PRO_ACTIVE */
