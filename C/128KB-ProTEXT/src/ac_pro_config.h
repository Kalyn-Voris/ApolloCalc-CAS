/* ============================================================
 * ApolloCalc-CAS · C/128KB-ProTEXT/src/ac_pro_config.h
 * ------------------------------------------------------------
 * 【为什么需要这个文件 / Why this file exists】
 *   Pro 层的每个包装文件都在第一行引入本头文件，从而保证「编译期
 *   开关」在**所有 TU 的第一行**就已经生效，不依赖任何 -I 或命令行
 *   顺序。上游 .c 内部一律 `#include "ac_xxx.h"`，这些头文件自带
 *   include guard，因此这里先引入 cfg 与 types 不会造成重复定义，
 *   也不会打乱上游的包含顺序（头文件是幂等的）。
 *
 * 【两级开关 / Two-level switch】
 *   AC_PRO          —— 传统开关，默认 1。只允许 0 或 1，其它值直接
 *                      #error（避免 -DAC_PRO= 这类空定义被当成 1）。
 *   AC_PRO_ACTIVE   —— **派生**开关 = AC_PRO && !AC_PRO_DEGRADED。
 *                      它的语义是「本 TU 是否真的启用 Pro 增量代码」。
 *                      所有 Pro 增量（新 TU、新全局、新视图）一律由它
 *                      守卫；AC_PRO 本身只表示「这是 Pro 目录的构建」。
 *
 *   为什么要拆成两级：Makefile 的包装清单由 wildcard 自动推导，一旦
 *   新增 `src/ac_pro_solvers_ext.c`，它会被自动编进**所有**构建。如果
 *   Pro 增量只看 AC_PRO，那么 `AC_PRO=0` 的「退化验证构建」也会把 Pro
 *   代码链进去，退化验证就失去意义。用 AC_PRO_ACTIVE 守卫后：
 *
 *       默认构建         AC_PRO=1                 -> Pro 增量全开
 *       AC_PRO=0         AC_PRO=0                 -> Pro 增量全关，只剩上游 16 TU
 *       AC_PRO_DEGRADED=1（AC_PRO 仍为 1）        -> Pro 增量全关，但构建仍走 Pro 目录
 *
 *   第三种正是「ProTEXT 的代码能完全退化为上游 v1.1.0 的行为」这条
 *   验收要求的直接实现：包装层照常把上游 16 个 .c 编成 16 个 TU，Pro
 *   增量一个字节都不参与，于是产物与上游逐位同构。
 *
 * 【路径深度的说明 / Why three levels】
 *   引号 include 以**包含者所在目录**为基准解析。本文件位于
 *   C/128KB-ProTEXT/src/，所以：
 *       ..            -> C/128KB-ProTEXT
 *       ../..         -> C
 *       ../../..      -> 仓库根 F:\ApolloCalc-CAS
 *       ../../../C/128KB/src -> 上游（正确）
 *   两级写法 ../../C/128KB/src 会解析成 C/C/128KB/src（不存在）。
 *   Makefile 里的 UPSTREAM_DIR 用两级是因为它相对于 Makefile 自身
 *   所在的 C/128KB-ProTEXT/，两者基准不同，不要混淆。
 *
 * 【设计边界 / Design boundary】
 *   本文件只做三件事：置宏、引入上游 cfg、编译期自证。
 *   它**不**定义任何存储、不引入 windows.h、不产生 .bss/.data。
 * ============================================================ */

#ifndef AC_PRO_CONFIG_H
#define AC_PRO_CONFIG_H

/* 编译期开关：Pro 层标识。允许 -DAC_PRO=1 覆盖（值相同，无副作用），
   也允许 -DAC_PRO=0 做「负向验证」——此时下面的 #error 会立刻拦下。 */
#ifndef AC_PRO
#define AC_PRO 1
#endif

/* 值域自证：只接受 0/1。`-DAC_PRO=` 这种空定义会让预处理把它当成 1，
   属于「看起来关了其实没关」的静默陷阱，这里显式拦下。 */
#if (AC_PRO != 0) && (AC_PRO != 1)
#error "AC_PRO must be exactly 0 or 1."
#endif

/* 退化开关：Pro 增量是否关闭。默认 0（即 Pro 增量启用）。 */
#ifndef AC_PRO_DEGRADED
#define AC_PRO_DEGRADED 0
#endif

#if (AC_PRO_DEGRADED != 0) && (AC_PRO_DEGRADED != 1)
#error "AC_PRO_DEGRADED must be exactly 0 or 1."
#endif

/* 派生开关：本 TU 是否启用 Pro 增量。 */
#if AC_PRO && !AC_PRO_DEGRADED
#define AC_PRO_ACTIVE 1
#else
#define AC_PRO_ACTIVE 0
#endif

/* ---- sin/cos const LUT 开关（v1.3.6 / feat/lut-const）----
 * 背景：上游 ac_math.c 的 sin/cos 查表（AC_MATH_USE_LUT）是 .bss
 * 可选项——2×1024×8 B 表 + 4 B ready 标志 = 16388 B RAM，且需运行时
 * ac_math_lut_init 填充。本开关开启时，src/ac_pro_math.c 在包装层用
 * 宏劫持把查表改走 src/ac_pro_lut_tables.h 的 const 预计算表：
 *     - LUT 存储从「+16388 B .bss 的可选项」变成零 RAM 成本项
 *       （16392 B 进 .rdata 只读段，.bss 门禁 42608/42640 不动）；
 *     - 无需 ac_math_lut_init（退化为空操作，ac_math_lut_ready 恒 1）；
 *     - 旧开关 AC_MATH_USE_LUT 在 const 路径下被有意忽略（包装层
 *       #undef）：命令行 -DAC_MATH_USE_LUT=1 不会带回 .bss 可变表；
 *     - 精度：与上游可变表同式同序生成（t = 2*PI*i/1024，17 位有效
 *       数字字面量），线性插值最坏误差同为 h^2/8 ~ 4.7e-6。
 * 置 0 可关回原行为（上游 libm 直通；AC_MATH_USE_LUT 恢复话语权）。
 * DEGRADED / AC_PRO=0 构建不受影响（AC_PRO_ACTIVE=0 时整个劫持段
 * 编译为空）。 */
#ifndef AC_PRO_MATH_LUT_CONST
#define AC_PRO_MATH_LUT_CONST 1
#endif

#if (AC_PRO_MATH_LUT_CONST != 0) && (AC_PRO_MATH_LUT_CONST != 1)
#error "AC_PRO_MATH_LUT_CONST must be exactly 0 or 1."
#endif

/* 引入上游 cfg：scratch 复用开关必须先于任何模块头文件（上游约定，
   见 ac_scratch_cfg.h 头注「开关必须最先引入」），随后是基础类型。 */
#include "../../../C/128KB/src/ac_scratch_cfg.h"
#include "../../../C/128KB/src/ac_types.h"

/* 编译期自证：宏控制必须真的生效，而不是「写在文件里没人看」。
   负向验证命令（应编译失败）：
       gcc -DAC_PRO=2 -std=c99 -c src/ac_pro_parser.c -o out.o
   期望输出：error: "AC_PRO must be exactly 0 or 1." */
#if !AC_PRO && !AC_PRO_DEGRADED
#error "AC_PRO=0 requires AC_PRO_DEGRADED=1: the ProTEXT wrapper TUs are only meaningful in one of the two modes."
#endif

/* Pro 层版本标签：与上游 v1.1.0 组合口径（仅字符串，不占存储） */
#define AC_PRO_LAYER_NAME "ProTEXT"
#define AC_PRO_LAYER_VERSION "1.3.5"

/* ---- 多变量基础设施上限（v1.3 A1/A2/A3）----
 * 设计说明 §6.4 的 preCheck 参数矩阵：
 *   A1 多元 GCD      不同变量数 ≤ 2、总次数 ≤ 6、项数 ≤ 28/多项式
 *   A2 多元因式分解  同 A1；降维后 degree ≤ 4（上游 ratroot 的硬限）
 *   A3 结式          deg_y(f) + deg_y(g) ≤ 12（AC_MATRIX_MAX_DIM）
 * 这些数字不是注释，而是被 ac_pro_multi.c / ac_pro_resultant.c 里的
 * 校验与循环边界直接使用 —— 改这里就是改行为。 */
#define AC_PRO_MAX_VARS       10    /* g_var_val[10] 的容量（payload 8 bit 足够） */
#define AC_PRO_MV_VARS         2    /* 多变量算法只支持 2 变量（设计说明 §6.4） */
#define AC_PRO_MV_DEGREE       6    /* 总次数上限 */
#define AC_PRO_MV_TERMS       28    /* N(6) = C(8,2) = 28 */

/* ---- .bss 三层账本常量（设计说明 §2.3 / §7.2，v2.1.0 起由 sparse 视图定峰） ----
 *   AC_STATIC_FOOTPRINT  上游 16 模块的内核口径基线，`size` 实测值。
 *   AC_PRO_STATIC_EXTRA  Pro 增量：g_var_val[10] 80 + g_ac_pro_scratch。
 *   AC_PRO_STATIC_FOOTPRINT = 两者之和 = Pro 构建的真实静态足迹。
 * 这三个数不是手抄的注释，而是被 ac_pro_solvers_ext.c 里的
 * AC_STATIC_ASSERT 与 sizeof(g_ac_pro_storage) 硬绑定：谁改错一个字节，
 * 编译期立刻失败。 */
#define AC_STATIC_FOOTPRINT          38672   /* 16 上游模块 + trivial main，size 实测 */
#define AC_PRO_VAR_VAL_BYTES         80      /* double g_var_val[10] */
#define AC_PRO_ALIGN_PAD_BYTES       8       /* double align_pad[1]: pad to a 32-byte multiple */
#define AC_PRO_SCRATCH_BYTES         3848    /* sizeof(ac_pro_scratch_t)，峰值 sparse（v2.1.0 ④） */
#define AC_PRO_STATIC_EXTRA          (AC_PRO_VAR_VAL_BYTES + AC_PRO_SCRATCH_BYTES + AC_PRO_ALIGN_PAD_BYTES)
#define AC_PRO_STATIC_FOOTPRINT      (AC_STATIC_FOOTPRINT + AC_PRO_STATIC_EXTRA)
#define AC_PRO_UPSTREAM_BASELINE_BSS AC_STATIC_FOOTPRINT
#define AC_PRO_TARGET_BSS            42608   /* 38672 + 80 + 3848 + 8，v2.1.0（sparse 峰值落地） */
#define AC_PRO_GATE_BSS              114688  /* 112 KiB = 128 KiB × 87.5% */

#endif /* AC_PRO_CONFIG_H */
