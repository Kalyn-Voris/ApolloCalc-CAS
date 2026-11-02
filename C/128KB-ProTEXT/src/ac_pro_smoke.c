/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_pro_smoke.c — Pro 层自检:上游代码编进 Pro TU、API 可用、宏生效

   Pro 层自检:证明「上游代码确实被编进 Pro TU、且 API 可用、且 AC_PRO 宏在
   编译期真的生效」。

   .bss 零增长:本文件绝不声明任何静态数组、静态缓冲或全局对象。阶段一的核
   心验证点就是「.bss 与上游 16 模块基线逐字节一致(38672 B)」,任何 static 大
   数组都会破坏这条门禁。所以自检只用:局部标量、sizeof 编译期常量、宏校验。
   注意:sizeof(g_ac_scratch) 是编译期常量,不产生存储。

   为什么不用 assert() / 不打印:assert 会引入 __assert_fail 依赖并在失败时
   abort,破坏「返回错误码」的可测性;打印交给 test/test_pro_runner.c 统一做,
   本文件保持纯函数 + 错误码,便于单测与后续复用。

   头文件路径:本文件在 src/ 下,上游头文件位于 ../../../C/128KB/src/;本目录
   的 ac_pro_config.h 用引号直取(同目录)。 */

#include <math.h>
#include <stddef.h>

#include "ac_pro_config.h"
#include "../../../C/128KB/src/ac_scratch.h"
#include "../../../C/128KB/src/ac_parser.h"
#include "../../../C/128KB/src/ac_solvers.h"
#include "../../../C/128KB/src/ac_evaluator.h"

/* 自检错误码(0 = 通过;非 0 逐位定位,便于报告) */
#define AC_PRO_SMOKE_OK              0
#define AC_PRO_SMOKE_ERR_MACRO       1   /* AC_PRO 宏值不为 1 */
#define AC_PRO_SMOKE_ERR_SCRATCH     2   /* scratch union 尺寸低于承诺 */
#define AC_PRO_SMOKE_ERR_PARSE       3   /* 表达式解析失败 */
#define AC_PRO_SMOKE_ERR_COEFF       4   /* 系数提取失败 */
#define AC_PRO_SMOKE_ERR_ROOTS       5   /* 求根结果与期望不符 */

/* 容差与上游测试口径一致 */
#define AC_PRO_SMOKE_TOL 1e-9

/* 判断根集合里是否同时含期望的两个实根(允许乱序) */
static int smoke_has_two_real_roots(double want0, double want1) {
    int i;
    int hit0 = 0;
    int hit1 = 0;
    if (g_solve_error != 0) { return 0; }
    if (g_root_count != 2) { return 0; }
    for (i = 0; i < g_root_count; i++) {
        if (fabs(g_root_im[i]) > AC_PRO_SMOKE_TOL) { continue; }
        if (fabs(g_root_re[i] - want0) <= AC_PRO_SMOKE_TOL) { hit0 = 1; }
        if (fabs(g_root_re[i] - want1) <= AC_PRO_SMOKE_TOL) { hit1 = 1; }
    }
    return hit0 && hit1;
}

int ac_pro_smoke_run(void) {
    /* 1) 编译期宏自证:AC_PRO 必须是 1(负向验证见 ac_pro_config.h) */
    if (AC_PRO != 1) { return AC_PRO_SMOKE_ERR_MACRO; }

    /* 2) scratch union 覆盖承诺:上游头文件里的 8224 B 峰值
       这是编译期常量比较,无运行时开销,也不产生任何静态存储。 */
    if (sizeof(ac_scratch_t) < 8224u) { return AC_PRO_SMOKE_ERR_SCRATCH; }

    /* 3) 端到端:x^2 - 5x + 6 = 0 -> 根 2 与 3
       流程与上游 test_solvers.c 的 solve_poly 完全一致:
       reset -> parse -> extract(deg) -> solve_quadratic */
    ac_solvers_reset();
    if (ac_parse_expression("x^2-5x+6=0") != ERR_NONE) {
        return AC_PRO_SMOKE_ERR_PARSE;
    }
    if (ac_extract_coefficients(2) != EVAL_OK) {
        return AC_PRO_SMOKE_ERR_COEFF;
    }
    ac_solve_quadratic();
    if (!smoke_has_two_real_roots(2.0, 3.0)) {
        return AC_PRO_SMOKE_ERR_ROOTS;
    }

    /* 自检结束,恢复干净状态供调用方继续使用 */
    ac_solvers_reset();
    return AC_PRO_SMOKE_OK;
}
