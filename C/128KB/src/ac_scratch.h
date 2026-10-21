#ifndef AC_SCRATCH_H
#define AC_SCRATCH_H

/* ============================================================
 * ApolloCalc-CAS · ac_scratch.h — 编译期互斥 scratch 存储池
 * ------------------------------------------------------------
 * 设计要点：
 *  1) C99 没有 _Alignas。用「含 double / uint64_t 成员的 union」自然
 *     获得 8 字节对齐（C99 §6.2.8 对齐要求；§6.7.2.1p14 每个非位域
 *     成员自偏移 0 开始）。成员里出现 double 即保证整体 8 对齐。
 *  2) 所有 scratch 池并入一个顶层 union。union 的每个成员都从偏移 0
 *     开始 → 标准层面的「编译期 section overlay」，零运行时开销。
 *  3) 互斥性由「同一 API 调用路径上不会同时活跃」保证。这是本项目
 *     零递归 + 模块边界清晰的结构性事实，非运行时检查。
 *
 * 回退设计（重要）：
 *   本头文件**不**在 #else 分支里定义 static 数组。原因：若把
 *   `static double s_sym_stack[...]` 写进头文件，则每个包含它的 .c
 *   都会生成一份自己的副本（8 个模块 → 8 份），.bss 反而暴涨。
 *   正确做法是让各 .c 文件用同一个开关守卫其原有定义 —— 见下方
 *   「使用方式」。回退时把对应 AC_SCRATCH_xxx 置 0 即可，源码
 *   一行都不用改回。
 *
 * 使用方式（在 .c 文件里）：
 *      #include "ac_scratch.h"
 *      #if !(AC_SCRATCH_UNION && AC_SCRATCH_SYM)
 *      static int32_t s_sym_stack[AC_SYM_MAX_NODES * 2 + 8];
 *      ...                                  // 原有定义原样保留
 *      #endif
 * ============================================================ */

/* 开关必须最先引入：下面的模块头文件会用到这些宏 */
#include "ac_scratch_cfg.h"

#include "ac_types.h"
#include "ac_symbolic.h"   /* AC_SYM_MAX_NODES    = 512   */
#include "ac_matrix.h"     /* AC_MATRIX_MAX_SIZE  = 144   */
#include "ac_bigint.h"     /* AC_BIGINT_LIMBS     = 20    */

/* ==================== 共享存储 ==================== */

typedef union {
    /* ---- 符号模块：deriv / eval / equal 三组严格互斥 ----
       ac_sym_derivative 只用 {stack, dmap}；
       ac_sym_eval       只用 {stack, val}；
       ac_sym_equal      只用 {pa, pb}。
       三者永不并发，故复用同一块内存。 */
    struct {                                    /* ac_sym_derivative */
        int32_t stack[AC_SYM_MAX_NODES * 2 + 8];    /* 1032 × 4 = 4128 */
        int32_t dmap [AC_SYM_MAX_NODES];            /*  512 × 4 = 2048 */
    } sym_deriv;                                       /* 6176 */

    struct {                                    /* ac_sym_eval       */
        int32_t stack[AC_SYM_MAX_NODES * 2 + 8];    /* 1032 × 4 = 4128 */
        double  val  [AC_SYM_MAX_NODES];            /*  512 × 8 = 4096 */
    } sym_eval;                                        /* 8224  <-- 峰值 */

    struct {                                    /* ac_sym_equal      */
        int32_t pa[AC_SYM_MAX_NODES];               /* 2048 */
        int32_t pb[AC_SYM_MAX_NODES];               /* 2048 */
    } sym_equal;                                       /* 4096 */

    /* ---- 矩阵模块：四个缓冲串联存放（实测 aug@+0, mul@+3456）----
       aug/mul/tr/qr 之间互不嵌套，但这里**不**把它们再互叠：
       整个 mat 只有 5760 B < sym_eval 的 8224 B，自然被吸收；
       再叠加一分钱也省不下，只会引入额外别名风险。 */
    struct {
        double aug[AC_MATRIX_MAX_SIZE * 2];         /* 288 × 8 = 2304 */
        double mul[AC_MATRIX_MAX_SIZE];             /* 144 × 8 = 1152 */
        double tr [AC_MATRIX_MAX_SIZE];             /* 144 × 8 = 1152 */
        double qr [AC_MATRIX_MAX_SIZE];             /* 144 × 8 = 1152 */
    } mat;                                          /* 合计 5760 */

    /* ---- 微积分：泰勒差分表 ---- */
    struct {
        double taylor[256];                         /* 256 × 8 = 2048 */
    } calculus;

    /* ---- 有理根：候选缓存 + 降次工作区 ---- */
    struct {
        int32_t divs [128];                         /* 512 */
        int32_t cand [ 64];                         /* 256 */
        double  work [ 16];                         /* 128 */
        double  trial[ 16];                         /* 128 */
    } ratroot;                                      /* 1024 */

    /* ---- 大整数：divmod 组 与 乘法原始积 互斥 ---- */
    union {
        struct {
            ac_bigint_t r, da, db;                  /* 3 × 88 = 264 */
            uint32_t t2[AC_BIGINT_LIMBS + 1];       /* 21 × 4 =  84 */
            uint32_t q [AC_BIGINT_LIMBS];           /* 20 × 4 =  80 */
        } divmod;                                   /* 428  <-- 峰值 */
        struct {
            uint32_t prod[AC_BIGINT_LIMBS * 2];     /* 40 × 4 = 160 */
        } mul;
    } bigint;

    /* ---- ac_util 叶函数查表：延迟构建，不与任何池同时活跃 ---- */
    struct {
        uint32_t crc[256];                          /* 1024 */
        int8_t   b64[256];                          /*  256 */
    } util;                                         /* 1280 */

} ac_scratch_t;

/* 共享存储的唯一定义处（ac_scratch.c） */
extern ac_scratch_t g_ac_scratch;

/* ==================== 操作符栈 / 求值栈别名 ====================
 * ac_to_rpn（Shunting-Yard）用 g_op_stack（int32 × 128）；
 * ac_eval_rpn（栈式求值）用 g_eval_stack（double × 128）。
 * 二者从不并发 —— 中间的交接物是 g_rpn_token 数组。
 * 因二者原分属不同 TU，union 必须放在共享头里由单一 TU 定义。 */
union ac_stk_u {
    int32_t op[OP_STACK_SIZE];      /* 128 × 4 =  512 —— ac_to_rpn   */
    double  ev[EVAL_STACK_SIZE];    /* 128 × 8 = 1024 —— ac_eval_rpn ← 峰值 */
};

extern union ac_stk_u g_ac_stk;

/* ==================== 视图宏（仅在被迁移的模块里生效） ==================== */

/* ---- 符号模块 ---- */
#if AC_SCRATCH_UNION && AC_SCRATCH_SYM
#define s_sym_stack  (g_ac_scratch.sym_eval.stack)
#define s_sym_val    (g_ac_scratch.sym_eval.val)
#define s_sym_dmap   (g_ac_scratch.sym_deriv.dmap)
#define s_sym_pa     (g_ac_scratch.sym_equal.pa)
#define s_sym_pb     (g_ac_scratch.sym_equal.pb)
#endif

/* ---- 矩阵模块 ---- */
#if AC_SCRATCH_UNION && AC_SCRATCH_MAT
#define s_mat_aug    (g_ac_scratch.mat.aug)
#define s_mat_mul    (g_ac_scratch.mat.mul)
#define s_mat_tr     (g_ac_scratch.mat.tr)
#define s_qr_a       (g_ac_scratch.mat.qr)
#endif

/* ---- 微积分 ---- */
#if AC_SCRATCH_UNION && AC_SCRATCH_CALC
#define s_taylor_work (g_ac_scratch.calculus.taylor)
#endif

/* ---- 有理根 ---- */
#if AC_SCRATCH_UNION && AC_SCRATCH_RATROOT
#define s_rr_divs     (g_ac_scratch.ratroot.divs)
#define s_rr_cand     (g_ac_scratch.ratroot.cand)
#define s_rr_work     (g_ac_scratch.ratroot.work)
#define s_rr_trial    (g_ac_scratch.ratroot.trial)
#endif

/* ---- 大整数 ---- */
#if AC_SCRATCH_UNION && AC_SCRATCH_BIGINT
#define s_bi_r        (g_ac_scratch.bigint.divmod.r)
#define s_bi_da       (g_ac_scratch.bigint.divmod.da)
#define s_bi_db       (g_ac_scratch.bigint.divmod.db)
#define s_bi_t2       (g_ac_scratch.bigint.divmod.t2)
#define s_bi_q        (g_ac_scratch.bigint.divmod.q)
#define s_bi_prod     (g_ac_scratch.bigint.mul.prod)
#endif

/* ---- 工具层 ---- */
#if AC_SCRATCH_UNION && AC_SCRATCH_UTIL
#define s_crc_table   (g_ac_scratch.util.crc)
#define s_b64_rev     (g_ac_scratch.util.b64)
#endif

/* ---- 栈别名 ---- */
#if AC_SCRATCH_UNION && AC_SCRATCH_STK
#define g_op_stack    (g_ac_stk.op)
#define g_eval_stack  (g_ac_stk.ev)
#endif

/* ==================== 编译期断言（C99 无 _Static_assert） ==================== */

#define AC_STATIC_ASSERT(cond, name) typedef char ac_sa_##name[(cond) ? 1 : -1]

/* union 必须至少覆盖符号求值侧的并发峰值 8224 B */
AC_STATIC_ASSERT(sizeof(ac_scratch_t) >= 8224, scratch_covers_sym_eval_peak);

/* 操作符栈与求值栈的别名不能小于两者中较大者 */
AC_STATIC_ASSERT(sizeof(union ac_stk_u) >= sizeof(double) * EVAL_STACK_SIZE,
                 stk_union_covers_eval_stack);

/* 矩阵侧必须能容纳增广矩阵 288 个 double */
AC_STATIC_ASSERT(sizeof(((ac_scratch_t *)0)->mat) >= sizeof(double) * 288,
                 mat_covers_augmented);

#endif /* AC_SCRATCH_H */
