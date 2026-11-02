/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_pro_diff_multi.c — 2 变量符号求导 d/dx、d/dy + mv_format 同款渲染

   v2.0.0:多变量符号求导(d/dx、d/dy)。对 A1/A2/A3 的多项式字符串语法逐项
   求导,输出同一套归一化字符串。

   签名偏离任务卡的原因:任务卡原签名是 `ac_pro_diff_multi(const mv_poly_t*,
   ...)`,但 mv_poly_t 是 ac_pro_multi.c 的私有类型(见 ac_pro.h「窄接口」
   注释:公开槽号而非句柄,是为了不让槽位纪律泄漏到多个 TU)。本文件与 A3
   同一条纪律:入口收字符串,内部走 ac_pro_multi_parse_into(src, slot) 窄
   接口,与 ac_pro_multi_var_gcd 的入口风格完全一致。

   视图复用,只用槽 0 和槽 1:不申请新视图、不改 ac_pro_scratch.h,
       槽 0  输入多项式(ac_pro_multi_parse_into 写入)
       槽 1  求导结果(本文件直接写 s_pro_mv_slot(1))
   结果项表无需再归一化:槽 0 的项表按 exps 降序且指数互异(解析器的不变式),
   求导把每个保留项的对应变量指数减同一个常数(d/dx 减 1<<4,d/dy 减 1),
   保序、单射,所以槽 1 自然有序、无重复指数;系数经 ac_safe_mul 非零,无零项。

   零 .bss:无 malloc、无 VLA、无递归;本文件没有任何静态存储对象
   (static 函数不占 .bss),所以 kernel 口径 .bss 仍是 42608、runner 口径
   仍是 42640(实测见 make bss-check)。

   溢出防护:与 A1/A2/A3 同一条设计说明 §3.1.3 硬要求,新系数 = c × exp 一律
   走上游 ac_safe_mul(ac_exact.h),溢出置 g_solve_error = 5
   (ac_pro_multi.c 的 PRO_MV_ERR_OVERFLOW 同码)并失败,绝不返回错值。 */

#include <stdio.h>

#include "ac_pro.h"
#include "../../../C/128KB/src/ac_exact.h"

#if AC_PRO_ACTIVE

/* 与 ac_pro_multi.c 同码：5 = 多变量系数溢出（上游只用 0..4）。 */
#define PRO_MV_ERR_OVERFLOW 5

/* exps 编码 / 解码
 * 从 ac_pro_multi.c 顶部的 mv_exps / mv_xdeg / mv_ydeg 原样抄录。
 * 它们是该文件的 static 私有实现（刻意不进公开头），本文件作为
 * mv 视图的第二个使用者，把同一编码规则复制一份而不是破坏封装：
 *   低 4 bit = y 指数，高 4 bit = x 指数（各 0..15，业务上限 6）。 */
static int32_t diff_exps(int xdeg, int ydeg) {
    return (int32_t)(((xdeg & AC_PRO_MV_EXP_MASK) << AC_PRO_MV_EXP_BITS) |
                     (ydeg & AC_PRO_MV_EXP_MASK));
}
static int diff_xdeg(int32_t exps) {
    return (int)((exps >> AC_PRO_MV_EXP_BITS) & AC_PRO_MV_EXP_MASK);
}
static int diff_ydeg(int32_t exps) {
    return (int)(exps & AC_PRO_MV_EXP_MASK);
}

/* 格式化（仿 ac_pro_multi.c 的 mv_format）
 * 把槽 1 的前 count 项写成规范字符串，规则与 mv_format 逐条一致：
 *   项按 exps 降序（槽 1 已自然有序，见头注）；
 *   系数 |mag| != 1 时写 "2*x*y"，=1 时省系数（写 "x" 而非 "1*x"）；
 *   指数 1 省略（写 "x" 而非 "x^1"）；指数 0 省略变量；
 *   项之间用 " + " / " - " 连接（负系数转成减号 + 绝对值）；
 *   零多项式写 "0"。
 * 缓冲不足返回 -1（调用方置 g_solve_error = 2）。无静态存储。 */
static int diff_format(int count, char *out, int out_len) {
    const ac_pro_term_t *t = s_pro_mv_slot(1);
    int i;
    int used = 0;
    int n;

    if (count == 0) {
        if (out_len < 2) { return -1; }
        out[0] = '0';
        out[1] = '\0';
        return 0;
    }

    for (i = 0; i < count; i++) {
        ac_int_t c = t[i].c;
        int xd = diff_xdeg(t[i].exps);
        int yd = diff_ydeg(t[i].exps);
        int neg = (c < 0);
        ac_int_t mag;

        if (c == INT64_MIN) { return -1; }    /* 取绝对值会溢出（同 mv_format） */
        mag = neg ? -c : c;

        /* 符号 */
        if (i == 0) {
            if (neg) {
                n = snprintf(out + used, (size_t)(out_len - used), "-");
                if (n < 0 || used + n >= out_len) { return -1; }
                used += n;
            }
        } else {
            n = snprintf(out + used, (size_t)(out_len - used), neg ? " - " : " + ");
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
        }

        /* 系数：只有在「无变量」或「系数绝对值 != 1」时才写 */
        if (xd == 0 && yd == 0) {
            n = snprintf(out + used, (size_t)(out_len - used), "%ld", (long)mag);
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
        } else if (mag != 1) {
            n = snprintf(out + used, (size_t)(out_len - used), "%ld", (long)mag);
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
            /* 系数与变量之间用 '*'：与解析器的输入格式一致 */
            n = snprintf(out + used, (size_t)(out_len - used), "*");
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
        }

        /* 变量 */
        if (xd > 0) {
            if (xd == 1) { n = snprintf(out + used, (size_t)(out_len - used), "x"); }
            else { n = snprintf(out + used, (size_t)(out_len - used), "x^%d", xd); }
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
            if (yd > 0) {
                n = snprintf(out + used, (size_t)(out_len - used), "*");
                if (n < 0 || used + n >= out_len) { return -1; }
                used += n;
            }
        }
        if (yd > 0) {
            if (yd == 1) { n = snprintf(out + used, (size_t)(out_len - used), "y"); }
            else { n = snprintf(out + used, (size_t)(out_len - used), "y^%d", yd); }
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
        }
    }
    return 0;
}

/* 对外：多变量符号求导
 * 见 ac_pro.h 的完整契约。占用 mv 视图槽 0（输入）与槽 1（输出），
 * 调用期间不得嵌套任何其它 Pro 视图 API。 */
int ac_pro_diff_multi(const char *src, int var_index, char *out, int out_len) {
    const ac_pro_term_t *in;
    ac_pro_term_t *dst;
    int n;
    int i;
    int w = 0;
    int rc;

    /* var_index：0 = x，1 = y；其他值一律拒绝（上游 2 = precheck/语法错）。 */
    if (var_index != 0 && var_index != 1) {
        g_solve_error = 2;
        return -1;
    }
    if (out == (char *)0 || out_len <= 0) {
        g_solve_error = 2;
        return -1;
    }

    /* 解析进槽 0。parse_into 不清 g_solve_error（由调用方负责，见其注释）：
     * 语法错时它保持 0，这里补置 2；若归一化期间已置 5（合并同类项加法
     * 溢出），则保留 5，不吞掉更严重的错误码。 */
    g_solve_error = 0;
    n = ac_pro_multi_parse_into(src, 0);
    if (n < 0) {
        if (g_solve_error == 0) { g_solve_error = 2; }
        return -1;
    }

    /* 逐项求导写槽 1。exp 为 0 的项对本变量是常数 -> 丢弃；
     * 否则新系数 = c * exp（ac_safe_mul，溢出即 5），指数减 1。 */
    in = ac_pro_multi_slot_terms(0);
    dst = s_pro_mv_slot(1);
    for (i = 0; i < n; i++) {
        int32_t e = in[i].exps;
        int32_t ne;
        ac_int_t c = in[i].c;
        ac_int_t nc;
        int d = (var_index == 0) ? diff_xdeg(e) : diff_ydeg(e);

        if (d == 0) { continue; }
        if (!ac_safe_mul(c, (ac_int_t)d, &nc)) {
            g_solve_error = PRO_MV_ERR_OVERFLOW;
            return -1;
        }
        if (var_index == 0) {
            ne = diff_exps(diff_xdeg(e) - 1, diff_ydeg(e));
        } else {
            ne = diff_exps(diff_xdeg(e), diff_ydeg(e) - 1);
        }
        dst[w].c = nc;
        dst[w].exps = ne;
        dst[w].pad = 0;
        w++;
    }

    /* 槽 1 自然有序（见头注），直接格式化。缓冲不足 -> -1 且错误码 2。 */
    rc = diff_format(w, out, out_len);
    if (rc != 0) {
        g_solve_error = 2;
        return -1;
    }
    return 1;   /* 成功；结果为 "0"（全常数输入/零多项式）也算成功 */
}

#endif /* AC_PRO_ACTIVE */
