#ifndef AC_SCRATCH_CFG_H
#define AC_SCRATCH_CFG_H

/* ============================================================
 * ApolloCalc-CAS · ac_scratch_cfg.h — scratch 复用的编译期开关
 * ------------------------------------------------------------
 * 为什么把开关单独拆出来：
 *   模块头文件（ac_parser.h 等）需要知道 AC_SCRATCH_STK 是否开启，
 *   才能决定要不要声明 extern 数组。但它们不能包含 ac_scratch.h
 *   本身 —— 那会形成循环依赖（ac_scratch.h 已经包含 ac_matrix.h /
 *   ac_symbolic.h 等）。本文件只放 #define，无任何依赖，可被安全地
 *   先于任何模块头文件包含。
 *
 * 回退方式：把 AC_SCRATCH_UNION 置 0（或某个分组置 0），
 * 重建即可，源码无需改动。
 * ============================================================ */

/* 全局总开关 */
#ifndef AC_SCRATCH_UNION
#define AC_SCRATCH_UNION 1
#endif

/* 分组开关（每迁移完一个模块翻一个） */
#ifndef AC_SCRATCH_SYM
#define AC_SCRATCH_SYM 1          /* 符号  s_sym_stack/dmap/val/pa/pb  */
#endif
#ifndef AC_SCRATCH_MAT
#define AC_SCRATCH_MAT 1          /* 矩阵  s_mat_aug/mul/tr/qr_a        */
#endif
#ifndef AC_SCRATCH_CALC
#define AC_SCRATCH_CALC 1         /* 微积分 s_taylor_work               */
#endif
#ifndef AC_SCRATCH_RATROOT
#define AC_SCRATCH_RATROOT 1      /* 有理根 s_rr_divs/cand/work/trial   */
#endif
#ifndef AC_SCRATCH_BIGINT
#define AC_SCRATCH_BIGINT 1       /* 大整数 s_bi_r/da/db/t2/q/prod      */
#endif
#ifndef AC_SCRATCH_UTIL
#define AC_SCRATCH_UTIL 0         /* 工具层 —— 已证伪，保持关闭          */
#endif
#ifndef AC_SCRATCH_STK
#define AC_SCRATCH_STK 1          /* 栈别名 g_op_stack / g_eval_stack   */
#endif
#ifndef AC_SCRATCH_LRHS
#define AC_SCRATCH_LRHS 1         /* 冗余 token 副本（Step 4b-2）        */
#endif

#endif /* AC_SCRATCH_CFG_H */
