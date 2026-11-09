/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_gui_bridge.c — Windows 桌面 Demo 的 DLL 桥接

   物理隔离：本 TU 只被 c_bridge/build.bat 编进 ApolloCalc.dll，绝不进入
   核心库构建系统；核心库目录（C/128KB-ProTEXT/、C/128KB/）零改动。

   核心 API 通过 ProTEXT 层唯一公开头一行引入（其内部再按相对路径挂接
   上游 C/128KB/src/，无需任何 -I）。解析/求值调用序列与核心库自检
   （C/128KB-ProTEXT/src/ac_pro_smoke.c 的端到端路径）同款：

       ac_solvers_reset() -> ac_parse_expression() -> ac_eval_expression_at()

   每次调用完整重置：RPN 由 ac_eval_expression_at 内部逐次重建
   （ac_evaluator.c 的入口先置 g_eval_error = EVAL_OK 再 ac_to_rpn），
   故跨 x 采样无需额外清理。 */

#include "ac_gui_bridge.h"

#include <math.h>

#include "../../../C/128KB-ProTEXT/src/ac_pro.h"
#include "../../../C/128KB/src/ac_parser.h"     /* ac_parse_expression / ERR_NONE */
#include "../../../C/128KB/src/ac_evaluator.h"  /* ac_eval_expression_at / EVAL_OK */
#include "../../../C/128KB/src/ac_math.h"       /* g_angle_deg（角度制开关） */

AC_GUI_API double ac_gui_eval(const char *expr, double x)
{
    double y;

    if (expr == NULL || expr[0] == '\0') {
        return NAN;
    }
    ac_solvers_reset();
    if (ac_parse_expression(expr) != ERR_NONE) {
        return NAN;
    }
    y = ac_eval_expression_at(x);
    if (g_eval_error != EVAL_OK) {
        return NAN;
    }
    return y;
}

AC_GUI_API void ac_gui_set_angle_mode(int deg)
{
    g_angle_deg = (deg != 0) ? 1 : 0;
}
