/* ApolloCalc · ac_pro_symbolic_ext.c — 隐函数求导 dy/dx = −F_x / F_y

   本项目最关键的一次「不改上游」:设计说明 §1.4 判据 2 发现
   ac_sym_node_t 的偏移 12..15 是填充洞:

         offsetof(type)  = 0    1 B
         offsetof(pad)   = 1    3 B   <-- 上游 sym_new 会清零 pad[0..2]
         offsetof(left)  = 4    4 B
         offsetof(right) = 8    4 B
         偏移 12..15:4 B 填充洞,sizeof=24 时 value 对齐到 16
         offsetof(value) = 16   8 B

   任务书预警的陷阱是:直接给结构体加 `int32_t var` 需要改
   ac_symbolic.h,而该头文件被上游 6 个 TU 包含,改完之后「Pro TU 与
   上游 TU 对 sizeof/布局的认知」会分叉,属于 ODR 式错配,是那种
   1000 条测试全绿也发现不了的静默错值。

   本实现的解法:结构体定义一个字都不改,用「字节偏移 + memcpy」
   访问那块洞,并用六条编译期断言把布局钉死:
         sizeof(ac_sym_node_t)          == 24
         offsetof(ac_sym_node_t, pad)   ==  1
         offsetof(ac_sym_node_t, right) ==  8
         offsetof(ac_sym_node_t, value) == 16
   于是 12..15 是洞这件事由编译器证明,而不是由注释声称。

   为什么 memcpy 是合法的:
     - 经 unsigned char* 访问任意对象的字节表示是标准明确允许的
       (C99 §6.2.6 / §7.21.2),无严格别名问题;
     - memcpy 对任意对齐的源/目的都合法,无对齐问题;
     - 该 4 字节从未被上游代码读写(上游只写 type/pad[0..2]/left/right/
       value),所以不存在与上游的读写竞争。

   会话态纪律:节点偏移 12..15 的 var 标签只在 type == NODE_VAR 时有意义。
   上游 ac_sym_new_var() 不写这块洞,所以本文件在每次建 VAR 节点后
   都显式写一次标签;非 VAR 节点残留旧标签无害(没人读)。
   本文件也不调用 ac_sym_from_poly,它会经 ac_sym_new_var() 造出
   带残留标签的 VAR 节点,语义上不属于 Pro 的变量体系。

   无 malloc、无 VLA、无递归。遍历工作区落 Pro union 的 sym 视图
   (1792 B,被 2688 B 的 mv 峰值吸收,.bss 零增长)。
   求值值栈是栈上标量数组(128 × 8 = 1024 B),不入 .bss。 */

#include <math.h>
#include <string.h>

#include "ac_pro.h"
#include "../../../C/128KB/src/ac_parser.h"
#include "../../../C/128KB/src/ac_evaluator.h"

#if AC_PRO_ACTIVE

/* 布局自证：填充洞真的在 12..15 */
AC_STATIC_ASSERT(sizeof(ac_sym_node_t) == 24, pro_sym_node_is_24_bytes);
AC_STATIC_ASSERT(offsetof(ac_sym_node_t, type)  ==  0, pro_sym_node_type_at_0);
AC_STATIC_ASSERT(offsetof(ac_sym_node_t, pad)   ==  1, pro_sym_node_pad_at_1);
AC_STATIC_ASSERT(offsetof(ac_sym_node_t, left)  ==  4, pro_sym_node_left_at_4);
AC_STATIC_ASSERT(offsetof(ac_sym_node_t, right) ==  8, pro_sym_node_right_at_8);
AC_STATIC_ASSERT(offsetof(ac_sym_node_t, value) == 16, pro_sym_node_value_at_16);
/* 由上面两条推出：12..15 是洞。这里再显式校验一次，让意图可 grep。 */
AC_STATIC_ASSERT(offsetof(ac_sym_node_t, right) +
                 (int)sizeof(((ac_sym_node_t *)0)->right) == 12,
                 pro_sym_node_hole_starts_at_12);
AC_STATIC_ASSERT(offsetof(ac_sym_node_t, value) - 12 == 4, pro_sym_node_hole_is_4_bytes);
/* 洞的 4 字节必须装得下 int32_t，这是 var 字段能零成本落位的充要条件。 */
AC_STATIC_ASSERT(sizeof(int32_t) == 4, pro_sym_var_tag_is_4_bytes);

#define AC_PRO_NODE_VAR_OFFSET 12

/* 填充洞读写 */
static void pro_node_set_var(int idx, int32_t var) {
    unsigned char *base = (unsigned char *)(void *)&g_sym_nodes[idx];
    memcpy(base + AC_PRO_NODE_VAR_OFFSET, &var, sizeof(var));
}

static int32_t pro_node_get_var(int idx) {
    int32_t var = 0;
    const unsigned char *base = (const unsigned char *)(const void *)&g_sym_nodes[idx];
    memcpy(&var, base + AC_PRO_NODE_VAR_OFFSET, sizeof(var));
    return var;
}

/* 求值值栈（栈上，不入 .bss） */
#define AC_PRO_SYM_VALSTK 128

/* Pro 层允许的 arena 占用上限。超过就拒绝，而不是让上游的 append-only
   预算在中途返回 -1、留下半构造的 arena（设计说明 §4.2.3 的同款教训）。 */
#define AC_PRO_SYM_NODE_CAP 256

/* 为什么错误码走返回值而不是 static 变量
 *   直觉写法是 `static int s_pro_imp_error;` + `ac_pro_implicit_error()`
 *   读取上一次的错误。实测（本仓库 gcc 8.1.0 / MinGW-w64 ld）这会多花
 *   32 B .bss：
 *       该 TU 因此产生一个 16 B 的 .bss 段（哪怕只装 4 B 的 int）
 *       + 链接期 16 B 对齐填充
 *   而 Pro 的 .bss 账本是 38672 + 80 + 2688 + 16 = 41456，没有给这个
 *   static 留任何余量，加进去就变成 41488，与设计说明 §7.2 不符。
 *
 *   所以错误码直接作为返回值：0 = 成功，1..4 = 具体失败原因（定义见下）。
 *   这比「读一个隐藏的全局」更好测（无需依赖调用顺序），也让 Pro 层
 *   的静态对象恰好只剩 g_ac_pro_storage 一个。
 *
 *   这正是设计说明判据 2 的另一面，「不在 union 的静态量
 *   100% 计入」，而计入的单位不是 4 B，是 32 B（段 + 对齐）。 */
#define AC_PRO_IMP_OK       0
#define AC_PRO_IMP_EPARSE   1   /* 解析失败 / 表达式不含 y / 传参非法 */
#define AC_PRO_IMP_EBUDGET  2   /* arena 预算不足（节点数 > 256 或余量不够） */
#define AC_PRO_IMP_EFLAT    3   /* F_y ≈ 0：隐函数不存在 / 切线水平 */
#define AC_PRO_IMP_EEVAL    4   /* 求值失败（非有限值 / 变量索引越界） */

/* 词法：表达式 -> g_token_pool
 * 只接受隐函数求导需要的子集：整数常量、变量 x/y/a..j、+ − * / ^、括号、
 * 一元负号。函数调用（sin 等）明确不支持，上游 NODE_* 只有五种节点，
 * 加 NODE_FUNC 会改上游语义，超出 v1.2 边界。
 *
 * 与上游 ac_parser.c 的差异：
 *   T_VAR 的 payload 是变量 ID（上游恒为 0，ac_parser.c:341）；
 *   支持 y / a..j（上游只认 x/X，其它字母直接 ERR_BADCHAR）；
 *   常量只接受整数（NODE_CONST 的 value 是 ac_int_t）。
 * 上游 ac_to_rpn 只读 token 的 type 与 payload 数值、不校验语义，
 * 所以这两套 token 流可以共用同一个 Shunting-Yard 实现。 */

static int pro_var_id_of(int c) {
    if (c == 'x') { return 0; }
    if (c == 'y') { return 1; }
    if (c >= 'a' && c <= 'j') { return 2 + (c - 'a'); }
    return -1;
}

static int pro_emit(int32_t w) {
    if (g_token_count >= TOKEN_POOL_SIZE) { return -1; }
    g_token_pool[g_token_count] = w;
    g_token_count++;
    return 0;
}

static int pro_lex(const char *src) {
    int i = 0;
    int prev_type = -1;

    g_token_count = 0;
    g_number_count = 0;

    if (src == (const char *)0) { return -1; }

    while (src[i] != '\0') {
        int c = (unsigned char)src[i];

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { i++; continue; }

        /* 数字：只接受整数 */
        if (c >= '0' && c <= '9') {
            double v = 0.0;
            if (g_number_count >= NUMBER_POOL_SIZE) { return -1; }
            while (src[i] >= '0' && src[i] <= '9') {
                v = v * 10.0 + (double)(src[i] - '0');
                i++;
            }
            if (src[i] == '.') { return -1; }   /* 小数超出符号层边界 */
            g_number_pool[g_number_count] = v;
            if (pro_emit(ac_token_make(T_NUM, g_number_count)) != 0) { return -1; }
            g_number_count++;
            prev_type = T_NUM;
            continue;
        }

        /* 变量 */
        if (pro_var_id_of(c) >= 0) {
            int vid = pro_var_id_of(c);
            if (vid >= AC_PRO_VAR_COUNT) { return -1; }   /* k..z 越界 */
            if (prev_type == T_NUM || prev_type == T_VAR ||
                prev_type == T_CONST || prev_type == T_RP) {
                if (pro_emit(ac_token_make(T_OP, OP_MUL)) != 0) { return -1; }
            }
            if (pro_emit(ac_token_make(T_VAR, vid)) != 0) { return -1; }
            prev_type = T_VAR;
            i++;
            continue;
        }

        if (c == '(') {
            if (prev_type == T_NUM || prev_type == T_VAR || prev_type == T_RP) {
                if (pro_emit(ac_token_make(T_OP, OP_MUL)) != 0) { return -1; }
            }
            if (pro_emit(ac_token_make(T_LP, 0)) != 0) { return -1; }
            prev_type = T_LP;
            i++;
            continue;
        }
        if (c == ')') {
            if (pro_emit(ac_token_make(T_RP, 0)) != 0) { return -1; }
            prev_type = T_RP;
            i++;
            continue;
        }

        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^') {
            /* 一元负号：表达式开头 / 左括号后 / 运算符后 */
            if (c == '-' && (prev_type == -1 || prev_type == T_LP ||
                             prev_type == T_OP || prev_type == T_UMINUS)) {
                if (pro_emit(ac_token_make(T_UMINUS, 0)) != 0) { return -1; }
                prev_type = T_UMINUS;
                i++;
                continue;
            }
            if (c == '+') { if (pro_emit(ac_token_make(T_OP, OP_ADD)) != 0) { return -1; } }
            else if (c == '-') { if (pro_emit(ac_token_make(T_OP, OP_SUB)) != 0) { return -1; } }
            else if (c == '*') { if (pro_emit(ac_token_make(T_OP, OP_MUL)) != 0) { return -1; } }
            else if (c == '/') { if (pro_emit(ac_token_make(T_OP, OP_DIV)) != 0) { return -1; } }
            else { if (pro_emit(ac_token_make(T_OP, OP_POW)) != 0) { return -1; } }
            prev_type = T_OP;
            i++;
            continue;
        }

        return -1;      /* 非法字符 */
    }

    return (g_token_count > 0) ? g_token_count : -1;
}

/* 建树：RPN -> 符号树 */
static int pro_new_var(int32_t var) {
    int idx = ac_sym_new_var();
    if (idx < 0) { return -1; }
    pro_node_set_var(idx, var);
    return idx;
}

/* RPN -> 树。操作数栈复用 sym 视图的 walk 数组（int32 × 192）。
 * 返回根索引，失败 -1。 */
static int pro_build_tree(const int32_t *rpn, int rpn_len) {
    int sp = 0;
    int i;

    for (i = 0; i < rpn_len; i++) {
        int32_t w = rpn[i];
        int t = ac_token_type(w);
        int p = ac_token_payload(w);

        if (t == T_NUM) {
            double d;
            ac_int_t iv;
            int cn;
            if (p < 0 || p >= g_number_count) { return -1; }
            d = g_number_pool[p];
            iv = (ac_int_t)d;
            if ((double)iv != d) { return -1; }     /* 非整数常量：符号层不支持 */
            if (sp >= AC_PRO_SYM_WALK) { return -1; }
            cn = ac_sym_new_const(iv);
            if (cn < 0) { return -1; }
            s_pro_sym_walk[sp++] = cn;
            continue;
        }
        if (t == T_VAR) {
            int vn;
            if (sp >= AC_PRO_SYM_WALK) { return -1; }
            vn = pro_new_var((int32_t)p);
            if (vn < 0) { return -1; }
            s_pro_sym_walk[sp++] = vn;
            continue;
        }
        if (t == T_UMINUS) {
            int a;
            int z;
            int neg;
            if (sp < 1) { return -1; }
            a = s_pro_sym_walk[--sp];
            z = ac_sym_new_const(0);
            if (z < 0) { return -1; }
            neg = ac_sym_new_sub(z, a);
            if (neg < 0) { return -1; }
            if (sp >= AC_PRO_SYM_WALK) { return -1; }
            s_pro_sym_walk[sp++] = neg;
            continue;
        }
        if (t == T_OP) {
            int b;
            int a;
            int r;
            if (sp < 2) { return -1; }
            b = s_pro_sym_walk[--sp];
            a = s_pro_sym_walk[--sp];
            if (p == OP_ADD) { r = ac_sym_new_add(a, b); }
            else if (p == OP_SUB) { r = ac_sym_new_sub(a, b); }
            else if (p == OP_MUL) { r = ac_sym_new_mul(a, b); }
            else if (p == OP_POW) {
                /* 指数必须是整数常量（NODE_POW 的 value 是 ac_int_t 快照） */
                ac_int_t nv;
                if (g_sym_nodes[b].type != NODE_CONST) { return -1; }
                nv = g_sym_nodes[b].value;
                if (nv < 0) { return -1; }          /* 负指数超出契约 */
                r = ac_sym_new_pow(a, nv);
            } else {
                return -1;                          /* '/' 不支持：符号层无除法节点 */
            }
            if (r < 0) { return -1; }
            if (sp >= AC_PRO_SYM_WALK) { return -1; }
            s_pro_sym_walk[sp++] = r;
            continue;
        }
        return -1;                                  /* T_FUNC / T_CONST 等不支持 */
    }
    if (sp != 1) { return -1; }
    return s_pro_sym_walk[0];
}

/* 公共：解析 */
int ac_pro_sym_parse(const char *src) {
    int n;
    int rpn_len;
    int root;

    ac_sym_reset();
    g_eval_error = EVAL_OK;

    n = pro_lex(src);
    if (n < 0) { return -1; }

    rpn_len = ac_to_rpn(g_token_pool, g_token_count, g_rpn_token);
    if (g_eval_error != EVAL_OK || rpn_len <= 0) { return -1; }

    root = pro_build_tree(g_rpn_token, rpn_len);
    if (root < 0) { return -1; }
    return root;
}

int ac_pro_sym_parse_for(const char *src, int target_var) {
    int root;
    int i;
    if (target_var < 0 || target_var >= AC_PRO_VAR_COUNT) { return -1; }
    root = ac_pro_sym_parse(src);
    if (root < 0) { return -1; }
    /* 校验目标变量确实出现在树里（否则 F_y ≡ 0，隐函数定理不适用） */
    for (i = 0; i < g_sym_count; i++) {
        if (g_sym_nodes[i].type == NODE_VAR && pro_node_get_var(i) == target_var) {
            return root;
        }
    }
    return -1;
}

/* 求值：读 g_var_val[]
 * 与上游 ac_sym_eval 的区别只有一处：NODE_VAR 取 g_var_val[var] 而不是
 * 参数 x。值栈是栈上数组而不是按节点索引落值表，后者要
 * AC_SYM_MAX_NODES 个 double（4096 B），是上游 union 峰值的来源。
 *
 * 压栈顺序必须让左子树先出栈：push(marker) -> push(right) -> push(left)，
 * 弹出顺序为 left、right、marker，此时值栈顶两格正是 [a=left, b=right]。
 * 顺序反了会让减法/幂运算左右颠倒，这是最容易写错、也最难被
 * 「x²+y²」这类对称用例发现的一处，故在此显式写明。 */
static int pro_eval_vars(int root, double *out) {
    int sp = 0;
    int vsp = 0;
    double stack_val[AC_PRO_SYM_VALSTK];

    if (root < 0 || root >= g_sym_count) { return 0; }
    if (sp >= AC_PRO_SYM_WALK) { return 0; }
    s_pro_sym_walk[sp++] = root;

    while (sp > 0) {
        int nd = s_pro_sym_walk[--sp];
        if (nd >= AC_SYM_MAX_NODES) {
            int nidx = nd - AC_SYM_MAX_NODES;
            int t = g_sym_nodes[nidx].type;
            double v = 0.0;
            if (t == NODE_CONST) {
                v = (double)g_sym_nodes[nidx].value;
            } else if (t == NODE_VAR) {
                int32_t vid = pro_node_get_var(nidx);
                if (vid < 0 || vid >= AC_PRO_VAR_COUNT) { return 0; }
                v = g_var_val[vid];
            } else if (t == NODE_ADD || t == NODE_SUB ||
                       t == NODE_MUL || t == NODE_POW) {
                double b;
                double a;
                if (vsp < 2) { return 0; }
                b = stack_val[--vsp];
                a = stack_val[--vsp];
                if (t == NODE_ADD) { v = a + b; }
                else if (t == NODE_SUB) { v = a - b; }
                else if (t == NODE_MUL) { v = a * b; }
                else {
                    v = pow(a, b);
                    if (!isfinite(v)) { return 0; }
                }
            } else {
                return 0;
            }
            if (vsp >= AC_PRO_SYM_VALSTK) { return 0; }
            stack_val[vsp++] = v;
        } else {
            int t = g_sym_nodes[nd].type;
            if (sp + 3 >= AC_PRO_SYM_WALK) { return 0; }
            s_pro_sym_walk[sp++] = nd + AC_SYM_MAX_NODES;
            if (t == NODE_ADD || t == NODE_SUB ||
                t == NODE_MUL || t == NODE_POW) {
                s_pro_sym_walk[sp++] = g_sym_nodes[nd].right;
                s_pro_sym_walk[sp++] = g_sym_nodes[nd].left;
            }
        }
    }
    if (vsp != 1) { return 0; }
    *out = stack_val[0];
    return 1;
}

double ac_pro_sym_eval_vars(int root) {
    double v = 0.0;
    if (!pro_eval_vars(root, &v)) { return (double)NAN; }
    return v;
}

/* 偏导（显式栈后序，零递归）
 * 与上游 ac_sym_derivative 的差异：NODE_VAR 的导数取决于节点 var 标签
 * 是否等于目标变量，而不是「一律 1」。
 *
 * 索引栈存的是节点索引而不是值：后序遍历中父节点需要的正是子节点
 * 导数的节点索引，所以用索引栈就够了（上游那张 2048 B 的 dmap 表因此
 * 也不需要）。
 *
 * 简化规则（与上游 ac_symbolic.c:125-146 同款，Pro 侧重写十余行）：
 *   0 + x -> x,  x + 0 -> x,  x - 0 -> x,  0 * x -> 0,  1 * x -> x
 * 这不是 fork 上游模块，而是「用上游公开构造器做必要化简」，
 * 不化简的话导数树会指数膨胀，512 节点 arena 撑不住。 */

static int pro_is_const(int idx, ac_int_t v) {
    if (idx < 0 || idx >= g_sym_count) { return 0; }
    return g_sym_nodes[idx].type == NODE_CONST && g_sym_nodes[idx].value == v;
}

static int pro_dmk_add(int l, int r) {
    if (l < 0 || r < 0) { return -1; }
    if (pro_is_const(l, 0)) { return r; }
    if (pro_is_const(r, 0)) { return l; }
    return ac_sym_new_add(l, r);
}

static int pro_dmk_sub(int l, int r) {
    if (l < 0 || r < 0) { return -1; }
    if (pro_is_const(r, 0)) { return l; }
    return ac_sym_new_sub(l, r);
}

static int pro_dmk_mul(int l, int r) {
    if (l < 0 || r < 0) { return -1; }
    if (pro_is_const(l, 0) || pro_is_const(r, 0)) { return ac_sym_new_const(0); }
    if (pro_is_const(l, 1)) { return r; }
    if (pro_is_const(r, 1)) { return l; }
    return ac_sym_new_mul(l, r);
}

/* 上游 ac_sym_derivative 的预算公式（ac_symbolic.c:154-155）：
 *     budget = MAX_NODES - g_sym_count
 *     if (budget < (g_sym_count - root) * 4 + 8) return -1;
 * Pro 侧前置做同样的检查（设计说明 §3.4.3：必须前置拒绝，而不是中途
 * 失败留下半构造的 arena）。 */
static int pro_budget_ok(int root) {
    int budget = AC_SYM_MAX_NODES - g_sym_count;
    if (g_sym_count > AC_PRO_SYM_NODE_CAP) { return 0; }
    if (budget < (g_sym_count - root) * 4 + 8) { return 0; }
    return 1;
}

/* 返回 d(root) 的节点索引，失败 -1。 */
static int pro_partial(int root, int32_t target_var) {
    int sp = 0;
    int dsp = 0;

    if (root < 0 || root >= g_sym_count) { return -1; }
    if (!pro_budget_ok(root)) { return -1; }

    s_pro_sym_walk[sp++] = root;
    while (sp > 0) {
        int nd = s_pro_sym_walk[--sp];
        if (nd >= AC_SYM_MAX_NODES) {
            int nidx = nd - AC_SYM_MAX_NODES;
            int t = g_sym_nodes[nidx].type;
            int d = -1;

            if (t == NODE_CONST) {
                d = ac_sym_new_const(0);
            } else if (t == NODE_VAR) {
                d = ac_sym_new_const(pro_node_get_var(nidx) == target_var ? 1 : 0);
            } else if (t == NODE_ADD || t == NODE_SUB || t == NODE_MUL) {
                int dl;
                int dr;
                if (dsp < 2) { return -1; }
                dr = s_pro_sym_d[--dsp];
                dl = s_pro_sym_d[--dsp];
                if (t == NODE_ADD) { d = pro_dmk_add(dl, dr); }
                else if (t == NODE_SUB) { d = pro_dmk_sub(dl, dr); }
                else {
                    int t1 = pro_dmk_mul(dl, g_sym_nodes[nidx].right);
                    int t2 = pro_dmk_mul(g_sym_nodes[nidx].left, dr);
                    if (t1 < 0 || t2 < 0) { return -1; }
                    d = pro_dmk_add(t1, t2);
                }
            } else if (t == NODE_POW) {
                /* d(u^n) = n · u^(n−1) · du（u 取原始基节点索引） */
                ac_int_t nv = g_sym_nodes[nidx].value;
                int dl;
                int pw;
                int inner;
                int cn;
                if (dsp < 1) { return -1; }
                dl = s_pro_sym_d[--dsp];
                if (nv < 0) { return -1; }
                pw = ac_sym_new_pow(g_sym_nodes[nidx].left, nv - 1);
                if (pw < 0) { return -1; }
                inner = pro_dmk_mul(pw, dl);
                if (inner < 0) { return -1; }
                cn = ac_sym_new_const(nv);
                if (cn < 0) { return -1; }
                d = pro_dmk_mul(cn, inner);
            } else {
                return -1;
            }
            if (d < 0) { return -1; }
            if (dsp >= AC_PRO_SYM_D) { return -1; }
            s_pro_sym_d[dsp++] = d;
        } else {
            int t = g_sym_nodes[nd].type;
            if (sp + 3 >= AC_PRO_SYM_WALK) { return -1; }
            s_pro_sym_walk[sp++] = nd + AC_SYM_MAX_NODES;
            if (t == NODE_ADD || t == NODE_SUB || t == NODE_MUL) {
                s_pro_sym_walk[sp++] = g_sym_nodes[nd].right;
                s_pro_sym_walk[sp++] = g_sym_nodes[nd].left;
            } else if (t == NODE_POW) {
                s_pro_sym_walk[sp++] = g_sym_nodes[nd].left;
            }
        }
    }
    if (dsp != 1) { return -1; }
    return s_pro_sym_d[0];
}

/* 高层入口：−F_x / F_y
 * 返回值即错误码（见文件头「为什么错误码走返回值」）：
 *     0 = 成功（*dydx 已写入）
 *     1 = 解析失败 / 表达式不含 y / 传参非法
 *     2 = arena 预算不足
 *     3 = F_y ≈ 0（隐函数不存在 / 切线水平）
 *     4 = 求值失败
 * 失败时 *dydx 保持调用方传入的原值，不被改写。 */
int ac_pro_implicit_derivative(const char *expr, double x, double y,
                               double *dydx) {
    int root;
    int fx;
    int fy;
    double vx;
    double vy;

    if (dydx == (double *)0) { return AC_PRO_IMP_EPARSE; }
    if (!isfinite(x) || !isfinite(y)) { return AC_PRO_IMP_EEVAL; }

    /* 变量值向量：判据 3 的会话态，求值前写入，求值后可读 */
    g_var_val[0] = x;
    g_var_val[1] = y;

    root = ac_pro_sym_parse_for(expr, 1);      /* 必须真的含 y */
    if (root < 0) { return AC_PRO_IMP_EPARSE; }

    /* F_x */
    fx = pro_partial(root, 0);
    if (fx < 0) { return AC_PRO_IMP_EBUDGET; }

    /* F_y */
    fy = pro_partial(root, 1);
    if (fy < 0) { return AC_PRO_IMP_EBUDGET; }

    /* 在 (x, y) 处求值 */
    if (!pro_eval_vars(fx, &vx)) { return AC_PRO_IMP_EEVAL; }
    if (!pro_eval_vars(fy, &vy)) { return AC_PRO_IMP_EEVAL; }
    if (!isfinite(vx) || !isfinite(vy)) { return AC_PRO_IMP_EEVAL; }

    /* F_y ≈ 0：隐函数定理不适用（切线水平 / 隐函数不存在）。
       阈值取相对量级，避免大数相消把 1e-18 判成「非零」。 */
    if (fabs(vy) < 1e-12 * (1.0 + fabs(vx))) {
        return AC_PRO_IMP_EFLAT;
    }

    *dydx = -vx / vy;
    return AC_PRO_IMP_OK;
}

#endif /* AC_PRO_ACTIVE */
