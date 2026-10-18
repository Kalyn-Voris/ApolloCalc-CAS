/* ============================================================
 * ApolloCalc-CAS · ac_evaluator.c — RPN 求值器（第 2 轮）
 * C99 64KB / 静态内存 / Zero-GC：禁 malloc、禁 VLA、禁递归
 * 对等基准：JS/64KB/src/core_evaluator.js（逐分支行为一致）
 * 错误码：0=OK 1=除零 2=域错误 3=溢出 4=内部错（与 JS _evalError 一致）
 * 第 5 轮起 applyFunction / g_angle_deg / 双精度常量迁至 ac_math（本文件经
 * ac_math.h 继续取用 AC_PI / AC_E 供 T_CONST 求值），求值行为逐位不变。
 * 历史差异（第 2 轮定档）：
 *   T_NUM 载荷越界 / 方程段长超 half 时 JS 依赖 Int32Array 越界
 *   写丢弃 + undefined 码元跳过语义，C 改为显式钳制（可观测输出一致）
 * ============================================================ */
#include "ac_parser.h"    /* 复用第 1 轮全局池：g_token_pool / g_number_pool / g_eval_stack */
#include "ac_evaluator.h"
#include "ac_math.h"      /* ac_apply_function / AC_PI / AC_E */

/* ==================== 全局内存池与状态（本文件唯一声明处） ==================== */
int32_t g_rpn_token[RPN_POOL_SIZE];
int     g_rpn_len = 0;
int32_t g_op_stack[OP_STACK_SIZE];
int     g_op_sp = 0;
int     g_eval_sp = 0;
double  g_eval_x = 0.0;
int     g_eval_error = EVAL_OK;
double  g_eval_result = 0.0;

int32_t g_lhs_token[TOKEN_POOL_SIZE / 2];
int32_t g_rhs_token[TOKEN_POOL_SIZE / 2];
int     g_lhs_count = 0;
int     g_rhs_count = 0;
double  g_lhs_result = 0.0;
double  g_rhs_result = 0.0;

/* ==================== 运算符优先级（整数化，避开浮点） ====================
 * +− = 2   ×÷ = 4   一元负号 = 5（高于乘除低于乘方）   ^ = 6
 * 返回 0 表示该 Token 不是可比较优先级的运算符（LP / FUNC 等）
 */
static int op_prec(int32_t word) {
    int t = ac_token_type(word);
    int p = ac_token_payload(word);
    if (t == T_OP) {
        if (p == OP_ADD || p == OP_SUB) { return 2; }
        if (p == OP_MUL || p == OP_DIV) { return 4; }
        return 6;                              /* OP_POW */
    }
    if (t == T_UMINUS) { return 5; }
    return 0;
}

/* ==================== 模块一：Shunting-yard 中缀转后缀 ====================
 * 读 src[0..src_count-1]，输出 dst[0..返回值-1]；运算符栈 g_op_stack 显式数组。
 * 一元负号无条件压栈（不参与优先级比较，保证 a^-b 不弹缺右操作数的 ^）；
 * ^ 右结合，其余左结合；T_EQ 必须先经 ac_eval_equation_at 分段，流入即内部错。
 * 副作用（对齐 JS toRPN）：写 g_rpn_len / g_op_sp / g_eval_error。
 */
int ac_to_rpn(const int32_t *src, int src_count, int32_t *dst) {
    int i;
    int32_t w = 0;
    int t = 0;
    int p = 0;
    int q = 0;
    int32_t cur = 0;
    int cur_p = 0;

    g_rpn_len = 0;
    g_op_sp = 0;

    for (i = 0; i < src_count; i++) {
        w = src[i];
        t = ac_token_type(w);
        p = ac_token_payload(w);

        /* ---- 操作数：直接输出 ---- */
        if (t == T_NUM || t == T_VAR || t == T_CONST) {
            if (g_rpn_len >= RPN_POOL_SIZE) { g_eval_error = EVAL_INTERNAL; return g_rpn_len; }
            dst[g_rpn_len] = w;
            g_rpn_len++;
            continue;
        }

        /* ---- 函数 / 左括号 / 一元负号：无条件压栈 ---- */
        if (t == T_FUNC || t == T_LP || t == T_UMINUS) {
            if (g_op_sp >= OP_STACK_SIZE) { g_eval_error = EVAL_INTERNAL; return g_rpn_len; }
            g_op_stack[g_op_sp] = w;
            g_op_sp++;
            continue;
        }

        /* ---- 右括号：弹到左括号；括号前若是函数则一并弹出 ---- */
        if (t == T_RP) {
            while (g_op_sp > 0) {
                g_op_sp--;
                t = ac_token_type(g_op_stack[g_op_sp]);
                if (t == T_LP) { break; }
                if (g_rpn_len >= RPN_POOL_SIZE) { g_eval_error = EVAL_INTERNAL; return g_rpn_len; }
                dst[g_rpn_len] = g_op_stack[g_op_sp];
                g_rpn_len++;
            }
            if (t != T_LP) { g_eval_error = EVAL_INTERNAL; return g_rpn_len; }  /* 无匹配左括号 */
            if (g_op_sp > 0 && ac_token_type(g_op_stack[g_op_sp - 1]) == T_FUNC) {
                g_op_sp--;
                if (g_rpn_len >= RPN_POOL_SIZE) { g_eval_error = EVAL_INTERNAL; return g_rpn_len; }
                dst[g_rpn_len] = g_op_stack[g_op_sp];
                g_rpn_len++;
            }
            continue;
        }

        /* ---- 逗号：弹到左括号（双参函数参数分隔，如 atan2） ---- */
        if (t == T_COMMA) {
            while (g_op_sp > 0 && ac_token_type(g_op_stack[g_op_sp - 1]) != T_LP) {
                g_op_sp--;
                if (g_rpn_len >= RPN_POOL_SIZE) { g_eval_error = EVAL_INTERNAL; return g_rpn_len; }
                dst[g_rpn_len] = g_op_stack[g_op_sp];
                g_rpn_len++;
            }
            if (g_op_sp == 0) { g_eval_error = EVAL_INTERNAL; return g_rpn_len; }
            continue;
        }

        /* ---- 二元运算符：按优先级弹栈后压栈 ---- */
        if (t == T_OP) {
            cur = w;
            cur_p = p;
            /* OP_POW 右结合，+−×÷ 左结合 */
            while (g_op_sp > 0) {
                w = g_op_stack[g_op_sp - 1];
                t = ac_token_type(w);
                if (t == T_LP || t == T_FUNC) { break; }        /* 函数/括号屏障 */
                q = op_prec(w);
                if (q > op_prec(cur) || (q == op_prec(cur) && cur_p != OP_POW)) {
                    g_op_sp--;
                    if (g_rpn_len >= RPN_POOL_SIZE) { g_eval_error = EVAL_INTERNAL; return g_rpn_len; }
                    dst[g_rpn_len] = w;
                    g_rpn_len++;
                } else {
                    break;
                }
            }
            if (g_op_sp >= OP_STACK_SIZE) { g_eval_error = EVAL_INTERNAL; return g_rpn_len; }
            g_op_stack[g_op_sp] = cur;
            g_op_sp++;
            continue;
        }

        /* ---- 等号不该流到这里：必须先经 ac_eval_equation_at 分段 ---- */
        if (t == T_EQ) { g_eval_error = EVAL_INTERNAL; return g_rpn_len; }
    }

    /* ---- 收尾：清空运算符栈；残余左括号 = 括号不匹配 ---- */
    while (g_op_sp > 0) {
        g_op_sp--;
        if (ac_token_type(g_op_stack[g_op_sp]) == T_LP) { g_eval_error = EVAL_INTERNAL; return g_rpn_len; }
        if (g_rpn_len >= RPN_POOL_SIZE) { g_eval_error = EVAL_INTERNAL; return g_rpn_len; }
        dst[g_rpn_len] = g_op_stack[g_op_sp];
        g_rpn_len++;
    }
    return g_rpn_len;
}

/* ==================== 模块二：RPN 求值器 ====================
 * g_eval_stack 显式数组 + g_eval_sp 栈式求值（不用系统栈）。
 * 结果写 g_eval_result 并返回；任何错误置 g_eval_error 后立即返回 NaN
 * （g_eval_result 保持旧值，对齐 JS 错误路径）。
 */
double ac_eval_rpn(const int32_t *rpn, int rpn_len) {
    int i;
    int32_t w = 0;
    int t = 0;
    int p = 0;
    double a = 0.0;
    double b = 0.0;
    double r = 0.0;

    g_eval_sp = 0;

    for (i = 0; i < rpn_len; i++) {
        w = rpn[i];
        t = ac_token_type(w);
        p = ac_token_payload(w);

        /* ---- 压栈类操作数 ---- */
        if (t == T_NUM) {
            if (g_eval_sp >= EVAL_STACK_SIZE) { g_eval_error = EVAL_OVERFLOW; return NAN; }
            if (p >= NUMBER_POOL_SIZE) {         /* JS 越界读 undefined→NaN，C 显式判内部错 */
                g_eval_error = EVAL_INTERNAL;
                return NAN;
            }
            g_eval_stack[g_eval_sp] = g_number_pool[p];
            g_eval_sp++;
            continue;
        }
        if (t == T_VAR) {
            if (g_eval_sp >= EVAL_STACK_SIZE) { g_eval_error = EVAL_OVERFLOW; return NAN; }
            g_eval_stack[g_eval_sp] = g_eval_x;
            g_eval_sp++;
            continue;
        }
        if (t == T_CONST) {
            if (g_eval_sp >= EVAL_STACK_SIZE) { g_eval_error = EVAL_OVERFLOW; return NAN; }
            g_eval_stack[g_eval_sp] = (p == C_PI) ? (double)AC_PI : (double)AC_E;
            g_eval_sp++;
            continue;
        }

        /* ---- 一元负号：原地取负，栈深不变 ---- */
        if (t == T_UMINUS) {
            if (g_eval_sp < 1) { g_eval_error = EVAL_INTERNAL; return NAN; }   /* 栈下溢 */
            g_eval_stack[g_eval_sp - 1] = -g_eval_stack[g_eval_sp - 1];
            continue;
        }

        /* ---- 双参函数 atan2(y, x)：先弹 x 后弹 y ---- */
        if (t == T_FUNC && p == F_ATAN2) {
            if (g_eval_sp < 2) { g_eval_error = EVAL_INTERNAL; return NAN; }
            b = g_eval_stack[g_eval_sp - 1]; g_eval_sp--;                      /* x */
            a = g_eval_stack[g_eval_sp - 1]; g_eval_sp--;                      /* y */
            r = ac_apply_function(F_ATAN2, a, b);
            if (isnan(r)) { g_eval_error = EVAL_DOMAIN; return NAN; }          /* 域错误 */
            if (g_eval_sp >= EVAL_STACK_SIZE) { g_eval_error = EVAL_OVERFLOW; return NAN; }
            g_eval_stack[g_eval_sp] = r;
            g_eval_sp++;
            continue;
        }

        /* ---- 单参函数 ---- */
        if (t == T_FUNC) {
            if (g_eval_sp < 1) { g_eval_error = EVAL_INTERNAL; return NAN; }
            a = g_eval_stack[g_eval_sp - 1]; g_eval_sp--;
            r = ac_apply_function(p, a, 0.0);
            if (isnan(r) && !isnan(a)) { g_eval_error = EVAL_DOMAIN; return NAN; }  /* 域错误 */
            if (isinf(r)) { g_eval_error = EVAL_OVERFLOW; return NAN; }             /* 溢出 */
            if (g_eval_sp >= EVAL_STACK_SIZE) { g_eval_error = EVAL_OVERFLOW; return NAN; }
            g_eval_stack[g_eval_sp] = r;
            g_eval_sp++;
            continue;
        }

        /* ---- 二元运算符：右操作数先弹 ---- */
        if (t == T_OP) {
            if (g_eval_sp < 2) { g_eval_error = EVAL_INTERNAL; return NAN; }
            b = g_eval_stack[g_eval_sp - 1]; g_eval_sp--;
            a = g_eval_stack[g_eval_sp - 1]; g_eval_sp--;

            if (p == OP_ADD) { r = a + b; }
            else if (p == OP_SUB) { r = a - b; }
            else if (p == OP_MUL) { r = a * b; }
            else if (p == OP_DIV) {
                if (b == 0.0) { g_eval_error = EVAL_DIV_ZERO; return NAN; }    /* 除零 */
                r = a / b;
            } else {                                                           /* OP_POW */
                if (a == 0.0 && b < 0.0) { g_eval_error = EVAL_DOMAIN; return NAN; }
                r = pow(a, b);
            }
            if (isinf(r)) { g_eval_error = EVAL_OVERFLOW; return NAN; }        /* 溢出 */
            if (g_eval_sp >= EVAL_STACK_SIZE) { g_eval_error = EVAL_OVERFLOW; return NAN; }
            g_eval_stack[g_eval_sp] = r;
            g_eval_sp++;
            continue;
        }
    }

    /* ---- 收尾：栈上必须恰好剩一个结果 ---- */
    if (g_eval_sp != 1) { g_eval_error = EVAL_INTERNAL; return NAN; }
    g_eval_result = g_eval_stack[0];
    return g_eval_result;
}

/* ==================== 模块三：求值入口 ==================== */

/* 单表达式入口：g_token_pool / g_token_count 已由 ac_parse_expression 填充（无等号） */
double ac_eval_expression_at(double x) {
    g_eval_x = x;
    g_eval_error = EVAL_OK;
    g_rpn_len = ac_to_rpn(g_token_pool, g_token_count, g_rpn_token);
    if (g_eval_error != EVAL_OK) { g_eval_result = NAN; return NAN; }
    return ac_eval_rpn(g_rpn_token, g_rpn_len);
}

/* 方程入口：f(x)=g(x) → f(x)-g(x)；无等号时退化为 ac_eval_expression_at */
double ac_eval_equation_at(double x) {
    int eq_pos = -1;
    int i = 0;
    int lhs_n = 0;
    int rhs_n = 0;
    int half = TOKEN_POOL_SIZE / 2;

    g_eval_x = x;
    g_eval_error = EVAL_OK;

    /* 定位等号（内存策略：扫描在原 g_token_pool 上进行，不复制） */
    for (i = 0; i < g_token_count; i++) {
        if (ac_token_type(g_token_pool[i]) == T_EQ) { eq_pos = i; break; }
    }

    if (eq_pos < 0) { return ac_eval_expression_at(x); }

    /* 等号两段拷入预分配区（零新分配，for 循环原地写定长数组）。
       段长 > half 时钳制拷贝与转换长度：JS Int32Array 越界写被丢弃、
       toRPN 读越界得 undefined 码元被跳过，可观测输出与本钳制一致 */
    g_lhs_count = eq_pos;
    g_rhs_count = g_token_count - eq_pos - 1;
    if (g_lhs_count == 0 || g_rhs_count == 0) {
        g_eval_error = EVAL_INTERNAL;
        g_eval_result = NAN;
        return NAN;
    }
    lhs_n = (g_lhs_count < half) ? g_lhs_count : half;
    rhs_n = (g_rhs_count < half) ? g_rhs_count : half;
    for (i = 0; i < lhs_n; i++) { g_lhs_token[i] = g_token_pool[i]; }
    for (i = 0; i < rhs_n; i++) { g_rhs_token[i] = g_token_pool[eq_pos + 1 + i]; }

    /* 左侧 f(x) */
    g_rpn_len = ac_to_rpn(g_lhs_token, lhs_n, g_rpn_token);
    if (g_eval_error != EVAL_OK) { g_eval_result = NAN; return NAN; }
    g_lhs_result = ac_eval_rpn(g_rpn_token, g_rpn_len);
    if (g_eval_error != EVAL_OK) { g_eval_result = NAN; return NAN; }

    /* 右侧 g(x) */
    g_rpn_len = ac_to_rpn(g_rhs_token, rhs_n, g_rpn_token);
    if (g_eval_error != EVAL_OK) { g_eval_result = NAN; return NAN; }
    g_rhs_result = ac_eval_rpn(g_rpn_token, g_rpn_len);
    if (g_eval_error != EVAL_OK) { g_eval_result = NAN; return NAN; }

    g_eval_result = g_lhs_result - g_rhs_result;
    return g_eval_result;
}

/* ==================== 模块四：复位 ====================
 * 只清标量（对齐 JS evaluatorReset；数组内容随 g_eval_sp/g_op_sp 归零失效） */
void ac_evaluator_reset(void) {
    g_rpn_len = 0;
    g_eval_sp = 0;
    g_op_sp = 0;
    g_eval_error = EVAL_OK;
    g_eval_x = 0.0;
    g_eval_result = 0.0;
    g_lhs_count = 0;
    g_rhs_count = 0;
    g_lhs_result = 0.0;
    g_rhs_result = 0.0;
}
