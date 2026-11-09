/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_gui_bridge.h — Windows 桌面 Demo 的 C DLL 导出面

   本文件属于 demos/Desktop-Windows/（演示层），物理隔离于核心库构建系统：
   核心库（C/128KB-ProTEXT/ 与 C/128KB/）零改动，DLL 由 c_bridge/build.bat
   独立编译（见 ../README.md），产物 ApolloCalc.dll 也不入库（.gitignore）。 */

#ifndef AC_GUI_BRIDGE_H
#define AC_GUI_BRIDGE_H

/* 构建 DLL 时导出；消费方（C#）经 DllImport 按名加载，不使用导入库。 */
#ifdef AC_GUI_BUILDING_DLL
#define AC_GUI_API __declspec(dllexport)
#else
#define AC_GUI_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 解析 expr 并在 x 处求值。expr 为 ASCII 数学表达式（与 C# 侧
 * DllImport 的 CharSet.Ansi 对齐），例如 "sin(x)*exp(-x/5)"。
 *
 * 返回值：
 *   有限 double = f(x)
 *   NaN         = 表达式为空 / 解析失败 / 求值错误（除零、定义域、溢出）
 *
 * 线程约定：内核使用全局池（g_token_pool、g_rpn_token 等），本函数不做
 * 加锁 —— 与核心库既有单线程约定一致，C# 侧只在 UI 线程调用。 */
AC_GUI_API double ac_gui_eval(const char *expr, double x);

/* 三角/反三角函数的角度制开关（读写字内核公开全局 g_angle_deg，
 * 见 C/128KB/src/ac_math.h）。核心默认 DEG（镜像 JS angleMode="DEG"），
 * 本导出只是演示层的便捷开关，不改核心默认值。
 *   deg != 0 -> DEG；deg == 0 -> RAD。 */
AC_GUI_API void ac_gui_set_angle_mode(int deg);

#ifdef __cplusplus
}
#endif

#endif /* AC_GUI_BRIDGE_H */
