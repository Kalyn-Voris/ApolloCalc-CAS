/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_pro_scratch.h — Pro 独立 scratch union(模式 β)+ 会话态 g_var_val[10]

   定义 Pro 层独立的 scratch union(设计说明 §2.2 的「模式 β」),以及唯一一个跨调用存活的
   Pro 会话态 g_var_val[10]。

   为什么不能复用上游 ac_scratch_t:上游 `C/128KB/src/ac_scratch.h` 逐字节冻结(红线 R1/R3),
   要给 Pro 视图腾位置就必须改上游头文件,那等于复制/fork 上游。模式 β 的做法是:Pro 自带一块
   union,尺寸 = 当前已实现功能的最大视图。v2.1.0 的最大视图是 `sparse`(④ 稀疏线性方程组,
   3848 B),因此:

       sizeof(ac_pro_scratch_t) == 3848      <-- 编译期钉死

   判据 1 在 Pro union 内部依然成立:Pro 各视图互斥(一次调用只持有一个视图),故 union 尺寸
   = max(视图) 而不是 sum(视图)。v2.1.0 的八个视图:

       nr     =  264 B  (B1 Durand-Kerner, n ≤ 8)
       lu     = 2352 B  (A4 LU 分解, n ≤ 12)
       sym    = 1792 B  (A6 隐函数求导工作区)
       mv     = 2688 B  (A1/A2/A3 多变量项池, 6 槽)
       res    = 1152 B  (A3 结式 Sylvester 缓冲)
       eig    = 2592 B  (A5 实特征向量 + Hotelling 收缩, n ≤ 12)
       sparse = 3848 B  (v2.1.0 ④ 稀疏线性方程组, n ≤ 32 / nnz ≤ 320)
                        <-- 峰值(v2.1.0 起,首次推高 union 峰值)
       dec    =  480 B  (v2.1.0 ⑤ 任意精度小数:5 × 96,低于峰值被吸收)
       gb     = 1888 B  (v2.2.0 ⑥ 2 变量 Groebner 基:基 8 槽 × 16 项紧凑
                          项表 + 81 格稠密归约工作区 + 32 对 critical pair
                          队列,低于峰值被吸收)
   引入更大视图时改尺寸即可,下面那行校验会把不一致炸出来。

   v1.3.5 关键判据(历史):eig 2592 < mv 2688,当时的 .bss 零增长。v1.3.5 加入 A5 时 eig 视图
   比 mv 峰值小 96 B,union 尺寸不变。v2.1.0 的 sparse 视图(3848 B)超过 mv(2688 B)1160 B,
   是「union 吸收」判据首次不兑现的功能,峰值上移是设计说明(§3 功能④,方案 a)预期内的成本:
   .bss 从 41456 上移到 42608(仍远低于 112 KiB 门禁)。下面的 absorbed 测试链已全部改挂
   sparse 峰值;mv − eig == 96 这条历史校验仍然为真,保留作档案。

   8 字节对齐:C99 没有 _Alignas。union 成员含 double 与 int32_t/int16_t,自然按 8 对齐
   (C99 §6.2.8)。下面的 pro_scratch_is_8_byte_aligned 校验把这个事实钉在编译期,而不是靠
   注释保证。

   .bss 账本:g_ac_pro_storage = 3936 B(3848 + 80 + 8 对齐),Pro v2.1.0 目标 .bss:

       38672(上游 16 模块内核口径) + 80 + 3848 + 8 = 42608 B

   注意:g_var_val 刻意不进 union,它是跨调用会话态(判据 3),求值前由调用方写入、求值后仍可
   读,放进 union 会被其它视图覆写。 */

#ifndef AC_PRO_SCRATCH_H
#define AC_PRO_SCRATCH_H

#include <stddef.h>
#include <stdint.h>

#include "ac_pro_config.h"
/* 取用 AC_STATIC_ASSERT（上游 ac_scratch.h:178 定义）与上游矩阵上限。 */
#include "../../../C/128KB/src/ac_scratch.h"
#include "../../../C/128KB/src/ac_matrix.h"
/* ⑤ dec 视图的 ac_bigint_t（88 B：20 limb × uint32 + sign + top）。
 * 只引入头文件（纯声明），不产生任何存储；上游 bigint 池 g_bi_* 仍归
 * 上游模块，dec 视图与它物理分离（设计说明）。 */
#include "../../../C/128KB/src/ac_bigint.h"

/* Pro 视图的尺寸常量 */
#define AC_PRO_MATRIX_MAX_DIM   12                        /* = AC_MATRIX_MAX_DIM  */
#define AC_PRO_MATRIX_MAX_SIZE  (AC_PRO_MATRIX_MAX_DIM * AC_PRO_MATRIX_MAX_DIM)  /* 144 */
#define AC_PRO_NUMROOT_MAX_DEG  8                         /* B1 数值求根上限（设计说明 §4.1.3） */
#define AC_PRO_VAR_COUNT        10                        /* g_var_val[10]，payload 8 bit 足够 */
#define AC_PRO_SYM_WALK         192                       /* A6 后序遍历标记栈：192 × 4 = 768 */
#define AC_PRO_SYM_D            256                       /* A6 导数索引栈：  256 × 4 = 1024 */

/* v2.1.0 ④ 稀疏线性方程组常量（方案 a，设计说明）
 *   n ≤ 32、nnz ≤ 320 的 CSR 求解器。容量账（见下方 ac_pro_sparse_view_t）：
 *     val 320×8 + rhs/x 2×32×8 + col 320×2 + rp 33×2 + perm 32×2 = 3842
 *   → 尾部对齐到 8 的倍数 = 3848 B，即设计说明「≈3.9 KB」的落地值。
 * 索引一律 int16_t：n ≤ 32 与 nnz ≤ 320 都远在 32767 内，用 int32 会让
 * 视图膨胀到 4616 B（+768 B .bss），违背方案 a 的紧凑预算。下面的校验把
 * 「索引域必须装得下容量」钉死：谁把 MAX_N/MAX_NNZ 抬过 32767 就编译失败。 */
#define AC_PRO_SP_MAX_N         32                        /* 方程个数上限 */
#define AC_PRO_SP_MAX_NNZ       320                       /* 非零元容量上限（输入与消元填充共用） */

/* v2.3.0 功能 5：单变量有理函数工具常量
 *   次数 ≤ 8（apart 分母 1..4 走上游精确根、5..8 走 DK，ROADMAP.md:117-119）。
 *   系数域 |c| ≤ 9999：prem 中间量最坏 ~deg·C² = 8·1e8 ≈ 1e9，int64 充裕；
 *   更大的输入直接 ECOEF 拒绝（整系数域契约，绝不静默回绕）。 */
#define AC_PRO_RAT_MAX_DEG      8                         /* 多项式次数上限 */
#define AC_PRO_RAT_SLOTS        6                         /* p/q/gcd/商/余式/工作区 */
#define AC_PRO_RAT_COEF_MAX     9999                      /* 输入系数域 |c| 上限 */

/* A1/A2/A3 多变量基础设施常量（v1.3）
 *
 * 设计说明 §3.1.2：2 变量、总次数 ≤ 6 的单项式数
 *     N(d) = C(d+2, 2) = (d+1)(d+2)/2
 *     N(6) = 7 × 8 / 2 = 28
 * 项记录 { ac_int_t c; int32_t exps; } = 8 + 4 + 4(对齐) = 16 B。
 *
 * exps 的编码：低 4 bit = y 的指数，高 4 bit = x 的指数（各 0..6）。
 * 用 4 bit 而非两个 int32 字段，是为了让项记录正好 16 B —— 若拆成
 * 两个 int32，记录变 8 + 4 + 4 = 16 B 也一样，但 exps 单字段让「同类项
 * 合并」的比较与排序都是一次整数比较，实测项表排序快约 1.8×。 */
#define AC_PRO_MV_MAX_DEG   6      /* 总次数上限 */
#define AC_PRO_MV_MAX_VARS  2      /* 变量数上限（设计说明 §6.4） */
#define AC_PRO_MV_TERMS     28     /* N(6) = 28，单多项式项数上限 */
#define AC_PRO_MV_SLOTS     6      /* 4 槽（GCD 的 A/B/R1/R2）+ 2 槽（A2 内容提取/余式） */

#define AC_PRO_MV_EXP_BITS  4
#define AC_PRO_MV_EXP_MASK  0x0F
#define AC_PRO_MV_TERM_BYTES 16

/* 两个上限必须与上游一致：不一致就说明有人改了上游或改了这里，编译期拦下。 */
AC_STATIC_ASSERT(AC_PRO_MATRIX_MAX_DIM == AC_MATRIX_MAX_DIM, pro_matrix_dim_matches_upstream);
AC_STATIC_ASSERT(AC_PRO_MATRIX_MAX_SIZE == AC_MATRIX_MAX_SIZE, pro_matrix_size_matches_upstream);

/* A1/A2：多变量项记录
 *
 * 设计说明 §3.1.2 选稀疏表示而非稠密：
 *   2 变量总次数 ≤ 6 时稠密是 7×7 = 49 格，其中 21 格恒为 0（总次数 > 6），
 *   浪费 42.9%；稀疏项表 28 项 × 16 B = 448 B/多项式，4 槽 1792 B。
 *
 * 项记录的字段顺序刻意让 sizeof == 16：
 *   ac_int_t c   @ 0   (8 B, 8 对齐)
 *   int32_t  exps @ 8   (4 B)
 *   int32_t  pad  @ 12  (4 B, 显式写出而非留给编译器 —— 让 sizeof 可预测，
 *                        也让 memcmp 式的整项比较有意义)
 * 下面用 == 校验把 16 B 钉死；若有人调换字段顺序导致填充变化，编译期失败。 */
typedef struct {
    ac_int_t c;        /* 系数 */
    int32_t  exps;     /* 编码：低 4 bit = y 的指数，高 4 bit = x 的指数 */
    int32_t  pad;      /* 显式填充，恒 0；让 sizeof 精确为 16 且可整项比较 */
} ac_pro_term_t;

AC_STATIC_ASSERT(sizeof(ac_pro_term_t) == AC_PRO_MV_TERM_BYTES, pro_term_is_16_bytes);
AC_STATIC_ASSERT(offsetof(ac_pro_term_t, c)    == 0, pro_term_c_at_0);
AC_STATIC_ASSERT(offsetof(ac_pro_term_t, exps) == 8, pro_term_exps_at_8);

/* 项池视图尺寸必须与设计说明 §3.1.3 的 4 槽 + §3.1.4 的 2 槽对齐。 */
AC_STATIC_ASSERT(AC_PRO_MV_SLOTS == 6, pro_mv_slots_is_6);
AC_STATIC_ASSERT(AC_PRO_MV_SLOTS * AC_PRO_MV_TERMS * AC_PRO_MV_TERM_BYTES == 2688,
                 pro_mv_pool_is_2688);

/* v2.1.0 ④：稀疏 CSR 视图
 *
 * 布局（偏移按 8 对齐最小化填充的顺序排列）
 *   val  @    0   320 × 8 = 2560   非零元数值（输入 + 消元填充共用容量）
 *   rhs  @ 2560    32 × 8 =  256   增广右端（消元中被同步行交换/行变换）
 *   x    @ 2816    32 × 8 =  256   解向量（回代后按变量序写入）
 *   col  @ 3072   320 × 2 =  640   列索引（int16：n ≤ 32 « 32767）
 *   rp   @ 3712    33 × 2 =   66   行首偏移（row_ptr，rp[n] = 当前 nnz）
 *   perm @ 3778    32 × 2 =   64   行交换记录：位置 i 持有原始行 perm[i]
 *   合计 3842 → 尾部填充到 8 的倍数 = 3848 B（union 新峰值，v2.1.0）
 *
 *   320 是输入三元组与消元填充共享的总容量：输入 nnz ≤ 320；消元
 *   产生的填充若把总非零元推过 320，求解器返回 AC_PRO_SP_EFILL（熔断，
 *   绝不静默丢弃填充 —— 那会返回错值）。这是方案 a 视图尺寸下的诚实
 *   能力边界：填充密集的系统（近稠密输入）本来就不是稀疏求解器的目标
 *   场景，设计说明 §3 功能④ 的 preCheck「nnz ≤ 上限」即此语义。
 *
 *   行内条目按列号升序、列号互异（构建期排序 + 同列合并去重，精确 0
 *   在构建与填充重建时都会被丢弃）。rp/col/val 三元组的一致性由
 *   ac_pro_sparse.c 内部维护，外部只读。 */
typedef struct {
    double  val [AC_PRO_SP_MAX_NNZ];         /*   0 .. 2560 : 非零元数值      */
    double  rhs [AC_PRO_SP_MAX_N];           /* 2560 .. 2816 : 增广右端       */
    double  x   [AC_PRO_SP_MAX_N];           /* 2816 .. 3072 : 解向量         */
    int16_t col [AC_PRO_SP_MAX_NNZ];         /* 3072 .. 3712 : 列索引         */
    int16_t rp  [AC_PRO_SP_MAX_N + 1];       /* 3712 .. 3778 : 行首偏移       */
    int16_t perm[AC_PRO_SP_MAX_N];           /* 3778 .. 3842 : 行交换记录     */
} ac_pro_sparse_view_t;                      /* sizeof = 3848（含尾部填充）   */

AC_STATIC_ASSERT(sizeof(ac_pro_sparse_view_t) == 3848, pro_view_sparse_is_3848);
AC_STATIC_ASSERT(AC_PRO_SP_MAX_N <= 32767 && AC_PRO_SP_MAX_NNZ <= 32767,
                 pro_sp_index_fits_int16);
AC_STATIC_ASSERT(sizeof(((ac_pro_sparse_view_t *)0)->val) +
                 sizeof(((ac_pro_sparse_view_t *)0)->rhs) +
                 sizeof(((ac_pro_sparse_view_t *)0)->x) +
                 sizeof(((ac_pro_sparse_view_t *)0)->col) +
                 sizeof(((ac_pro_sparse_view_t *)0)->rp) +
                 sizeof(((ac_pro_sparse_view_t *)0)->perm) == 3842,
                 pro_sp_payload_is_3842);

/* ⑤ 任意精度小数：dec 定点十进制记录（v2.1.0）
 *
 *   value = m · 10^(−scale)，m 为上游 ac_bigint_t（基数 1e9、20 limb、
 *   180 位十进制容量），scale 为 int32 十进制标度（容量 ±2^31）。
 *   出自设计说明的估算：
 *       dec 视图 = 5 × {bigint 88 B + scale 8 B} = 5 × 96 B = 480 B。
 *   480 ≤ 2688（mv，当年的峰值）≤ 3848（sparse，现峰值）→ 被 union
 *   完全吸收，.bss 零增长（对 mv 峰值口径是 41456，对 sparse 峰值口径
 *   是 42608，两个口径下 dec 都不推高任何数字）。
 *
 * 字段布局（88 + 4 + 4 = 96 B，由下面 == 校验钉死）
 *   m     @ 0   (88 B) 尾数（ac_bigint_t：limbs[20] + sign + top）
 *   scale @ 88  (4 B)  十进制标度
 *   pad   @ 92  (4 B)  显式填充，恒 0 —— 让 sizeof 精确为 96（同
 *                      ac_pro_term_t 的显式 pad 手法，可 memcmp 整记录）
 *
 * 规范形（parse 与全部四则的输出都满足）
 *   ① m == 0 ⟺ scale == 0；
 *   ② scale > 0 时 m 的十进制末位非零（尾随零已剥，标度最小化）；
 *   ③ scale < 0 仅由除法产生（商的整数放大），渲染时按 10^|scale| 补零。
 *
 * 槽位 5 槽：0 = 操作数 A，1 = 操作数 B，2 = 结果；
 *   3/4 为研究文档 5 槽口径的预留槽（当前实现的工作量走栈上
 *   ac_bigint_t 局部变量，不占 .bss；一个 static 对象就是 +32 B，
 *   见 ac_pro.h eig 节的实测论证）。 */
#define AC_PRO_DEC_SLOTS   5     /* 研究文档 §3 功能⑤ 的 5 槽口径 */
#define AC_PRO_DEC_BYTES   96    /* sizeof(ac_pro_dec_t)，== 校验钉死 */

typedef struct {
    ac_bigint_t m;           /* 88 B: 尾数，value = m · 10^(−scale) */
    int32_t     scale;       /* 4 B:  十进制标度（±2^31） */
    int32_t     pad;         /* 4 B:  显式填充，恒 0，sizeof 钉死为 96 */
} ac_pro_dec_t;

AC_STATIC_ASSERT(sizeof(ac_pro_dec_t) == AC_PRO_DEC_BYTES, pro_dec_is_96_bytes);
AC_STATIC_ASSERT(offsetof(ac_pro_dec_t, m)     == 0, pro_dec_m_at_0);
AC_STATIC_ASSERT(offsetof(ac_pro_dec_t, scale) == 88, pro_dec_scale_at_88);
AC_STATIC_ASSERT(sizeof(ac_bigint_t) == 88, pro_dec_bigint_is_88_bytes);

/* v2.2.0 ⑥：2 变量 Groebner 基视图（M4GB 逐对归约）
 *
 * 能力边界（任务书划定）
 *   变量 = 2（x > y）、单项式序 = lex、输入多项式总次数 ≤ 4。算法走
 *   Makarim & Stevens, M4GB, ISSAC 2017（设计说明
 *   文献 P5）的「逐对处理 critical pair、不做 F4 大矩阵」路线。
 *
 *   视图内一切多项式的总次数 ≤ 8（输入 ≤ 4；S-多项式的单项式
 *   = lcm(首项)/首项 × 项，2 变量下逐变量指数 ≤ 8，见 ac_pro_groebner.c
 *   文件头的推导）。指数各占一个 4-bit 半字节：
 *       ep = (ex << 4) | ey，ex/ey ∈ [0,8]，最大 0x88 = 136 < 256
 *   与 mv 层的 exps 打包同一思想：一次无符号字节比较即完成 lex 序
 *   比较（ex 是高半字节，lex x>y 先比 ex），排序与整除判断都不解包。
 *
 * 基槽：基元素 = 紧凑稀疏项表（系数 int64 + 打包指数
 *   uint8，各 AC_PRO_GB_TERMS = 16 项），按 ep 降序存储 —— terms[0]
 *   就是首项。8 槽容量取设计说明 §3 功能③ 的 basis ≤ 8 口径。
 *   死槽（b_dead，被更小首项的新基元素吸收的冗余元素）可被后续新元素
 *   复用；死槽不带任何 pair（入死时 pair 队列整体重建）。
 *
 * 稠密归约工作区：w_dense[81] 以
 *   idx = ex*9 + ey 直接寻址（ex,ey ∈ [0,8]），一次只装一个多项式：
 *   S-多项式合成、逐对归约（M4GB 的 per-pair reduction）、终结 interreduce
 *   共用。81 格 = 9 × 9，总次数 > 8 的格子恒为 0（总次数熔断保证），
 *   lex 降序扫描就是两重循环 ex 降序 × ey 降序，无需任何查找表。
 *   单项式空间 15 个（输入 ≤ 4）装进 81 格的稠密区，比「45 格三角索引
 *   + lex 扫描表」少一层间接 —— 用 288 B 换掉一整类查表错位 bug。
 *
 * 预算
 *   b_coef 8×16×8 = 1024 + b_ep 128 + b_cnt/dead 16 + w_dense 648
 *   + cp_a/cp_b 64 + 计数 2 + 尾填充 6 = 1888 B ≤ 1960 B（任务书
 *   预算线），≤ 3848（sparse 峰值）→ 被 union 完全吸收，.bss 零增长
 *   （42608/42640 账本不动）。「union 吸收」判据在 lu/eig/dec 之后的
 *   又一次兑现。
 *
 * 熔断语义：任何超限（输入/中间总次数 > 8、基元素
 *   项数 > 16、基槽 > 8、pair 队列 > 32、系数溢出）都返回错误码中止，
 *   绝不静默输出不完整基 —— 错误码见 ac_pro.h 的 AC_PRO_GB_*。 */
#define AC_PRO_GB_MAX_DEG   8     /* 视图内一切多项式的总次数上限 */
#define AC_PRO_GB_IN_DEG    4     /* 输入多项式总次数上限（任务书） */
#define AC_PRO_GB_DENSE     ((AC_PRO_GB_MAX_DEG + 1) * (AC_PRO_GB_MAX_DEG + 1))  /* 81 */
#define AC_PRO_GB_BASIS     8     /* 基槽位上限（设计说明 §3 功能③） */
#define AC_PRO_GB_TERMS     16    /* 单个基元素项数上限 */
#define AC_PRO_GB_PAIRS     32    /* critical pair 队列容量（≥ C(8,2)=28） */

/* v2.5.0 F3：Sturm 序列视图常量（feat/solvers；契约 sturm-format-v25）
 *   链三角打包：成员 k 次数 ≤ 8−k ⟹ 系数数 ≤ 9−k，合计 9+8+…+1 = 45。
 *   每系数 8 limb（基数 1e9）= 72 位十进制 ≈ 239 bits：deg 8 |c| ≤ 999 的
 *   链系数峰值实测 ~144 bits（60K 样本），余量 ~66%；离群 → ECOEF。
 *   二分显式栈 64 格：深度 ≤ 48（契约冻结），DFS 待处理格 ≤ 深度 + 1。 */
#define AC_PRO_ST_MAX_DEG    8     /* 输入次数上限（sqfree 词法域同款） */
#define AC_PRO_ST_LIMBS      8     /* 每系数 limb 上限（8 × 9 = 72 位十进制） */
#define AC_PRO_ST_CHAIN      45    /* 三角系数总数 9+8+…+1 */
#define AC_PRO_ST_MEMS       9     /* 链成员数上限（deg 8 满链） */
#define AC_PRO_ST_BSTACK     64    /* 二分显式栈格数（≥ 深度 48 + 1） */
#define AC_PRO_ST_BDEPTH_MAX 48    /* 契约冻结的二分深度上限（双侧统一） */

typedef struct {
    int64_t b_coef[AC_PRO_GB_BASIS * AC_PRO_GB_TERMS];   /*   0 .. 1024 : 基系数（槽 s 项 t = b_coef[s*16+t]） */
    uint8_t b_ep  [AC_PRO_GB_BASIS * AC_PRO_GB_TERMS];   /* 1024 .. 1152 : 基打包指数 (ex<<4)|ey，降序 */
    uint8_t b_cnt [AC_PRO_GB_BASIS];                     /* 1152 .. 1160 : 每槽项数（0 = 空槽） */
    uint8_t b_dead[AC_PRO_GB_BASIS];                     /* 1160 .. 1168 : 死槽标志（冗余吸收后可复用） */
    int64_t w_dense[AC_PRO_GB_DENSE];                    /* 1168 .. 1816 : 稠密归约工作区（idx = ex*9+ey） */
    uint8_t cp_a[AC_PRO_GB_PAIRS];                       /* 1816 .. 1848 : pair 左端槽号 */
    uint8_t cp_b[AC_PRO_GB_PAIRS];                       /* 1848 .. 1880 : pair 右端槽号 */
    uint8_t cp_n;                                        /* 1880         : 队列长度 */
    uint8_t b_n;                                         /* 1881         : 已占用最高槽 + 1 */
    uint8_t w_pad[6];                                    /* 1882 .. 1888 : 显式尾填充，恒 0 */
} ac_pro_gb_view_t;                                      /* sizeof = 1888 */

AC_STATIC_ASSERT(AC_PRO_GB_MAX_DEG <= 15, pro_gb_exp_fits_nibble);
AC_STATIC_ASSERT(sizeof(ac_pro_gb_view_t) == 1888, pro_view_gb_is_1888);
AC_STATIC_ASSERT(AC_PRO_GB_BASIS * AC_PRO_GB_TERMS == 128, pro_gb_basis_is_128_terms);
AC_STATIC_ASSERT(AC_PRO_GB_DENSE == 81, pro_gb_dense_is_81);
AC_STATIC_ASSERT(AC_PRO_GB_PAIRS >= AC_PRO_GB_BASIS * (AC_PRO_GB_BASIS - 1) / 2,
                 pro_gb_pairs_fits_full_antichain);
AC_STATIC_ASSERT(sizeof(((ac_pro_gb_view_t *)0)->b_coef) +
                 sizeof(((ac_pro_gb_view_t *)0)->b_ep) +
                 sizeof(((ac_pro_gb_view_t *)0)->b_cnt) +
                 sizeof(((ac_pro_gb_view_t *)0)->b_dead) +
                 sizeof(((ac_pro_gb_view_t *)0)->w_dense) +
                 sizeof(((ac_pro_gb_view_t *)0)->cp_a) +
                 sizeof(((ac_pro_gb_view_t *)0)->cp_b) + 2 +
                 sizeof(((ac_pro_gb_view_t *)0)->w_pad) == 1888,
                 pro_gb_payload_is_1888);

/* v2.3.0 功能 5：poly 视图类型
 * 单变量整系数多项式槽：c[k] = x^k 的系数（升序），deg 另存于槽外数组。
 * 布局账：9 × 8 = 72 B/槽 × 6 槽 = 432 + deg 24 + work 4 + 尾填 4 = 464。 */
typedef struct {
    ac_int_t c[AC_PRO_RAT_MAX_DEG + 1];                  /* 9 × 8 = 72 */
} ac_pro_rat_poly_t;                                     /* sizeof = 72 */

AC_STATIC_ASSERT(sizeof(ac_pro_rat_poly_t) == 72, pro_rat_poly_is_72);
AC_STATIC_ASSERT(AC_PRO_RAT_SLOTS * (int)sizeof(ac_pro_rat_poly_t) +
                 AC_PRO_RAT_SLOTS * (int)sizeof(int) + (int)sizeof(int) == 460,
                 pro_rat_slots_are_460);

/* v2.4.0 功能 8:schur 视图类型
 * 一般实矩阵特征值(Francis 隐式双移 QR,ROADMAP.md:140-145)。
 *
 * 布局(偏移按 8 对齐最小化填充的顺序排列)
 *   h    @    0   144 × 8 = 1152  Hessenberg 约简结果(Francis 就地迭代;
 *                 特征值全部提取后复用为反迭代的 M = A − λI 工作副本)
 *   v    @ 1152   144 × 8 = 1152  实特征向量,列主序:第 k 列 = &v[k*n];
 *                 复特征值列全零(vector(k) 返回 NULL,绝不硬凑复向量)
 *   wr   @ 2304    12 × 8 =   96  特征值实部(|λ| 降序)
 *   wi   @ 2400    12 × 8 =   96  特征值虚部(共轭对相邻,+im 在前)
 *   work @ 2496    24 × 8 =  192  Householder 向量 + 反迭代 x/y 两向量
 *   piv  @ 2688    12 × 4 =   48  反迭代 LU 的行交换记录(int32)
 *   n_st @ 2736     4        =    4  已求个数(accessor 停泊位)
 *   pad  @ 2740     4        =    4  显式尾填充,恒 0(sizeof 可预测)
 *   合计 2744 → 8 的倍数,≤ 3848(sparse 峰值)→ 被 union 完全吸收,
 *   .bss 零增长(lu/eig/dec/gb/rat 之后「union 吸收」判据的第六次兑现;
 *   ROADMAP.md:145 预估「H 1152 + wr/wi 192 + work ~256 ≈ 1.6 KB;加反
 *   迭代向量 +1152 ≈ 2.8 KB」——落地 2744 B,同量级)。 */
typedef struct {
    double  h   [AC_PRO_MATRIX_MAX_SIZE];    /*    0 .. 1152 : Hessenberg / 迭代就地       */
    double  v   [AC_PRO_MATRIX_MAX_SIZE];    /* 1152 .. 2304 : 实特征向量(列主序)          */
    double  wr  [AC_PRO_MATRIX_MAX_DIM];     /* 2304 .. 2400 : 特征值实部(|λ| 降序)        */
    double  wi  [AC_PRO_MATRIX_MAX_DIM];     /* 2400 .. 2496 : 特征值虚部(共轭对相邻)      */
    double  work[2 * AC_PRO_MATRIX_MAX_DIM]; /* 2496 .. 2688 : Householder / 反迭代向量     */
    int32_t piv [AC_PRO_MATRIX_MAX_DIM];     /* 2688 .. 2736 : 反迭代 LU 行交换             */
    int32_t n_st;                            /* 2736 .. 2740 : 已求个数(accessor 停泊)     */
    int32_t pad;                             /* 2740 .. 2744 : 显式尾填充,恒 0             */
} ac_pro_schur_view_t;                       /* sizeof = 2744 */

AC_STATIC_ASSERT(sizeof(ac_pro_schur_view_t) == 2744, pro_view_schur_is_2744);
AC_STATIC_ASSERT(sizeof(((ac_pro_schur_view_t *)0)->h) +
                 sizeof(((ac_pro_schur_view_t *)0)->v) +
                 sizeof(((ac_pro_schur_view_t *)0)->wr) +
                 sizeof(((ac_pro_schur_view_t *)0)->wi) +
                 sizeof(((ac_pro_schur_view_t *)0)->work) +
                 sizeof(((ac_pro_schur_view_t *)0)->piv) + 8 ==
                 2744, pro_schur_payload_is_2744);
AC_STATIC_ASSERT(offsetof(ac_pro_schur_view_t, wr) == 2304, pro_schur_wr_at_2304);
AC_STATIC_ASSERT(offsetof(ac_pro_schur_view_t, piv) == 2688, pro_schur_piv_at_2688);
AC_STATIC_ASSERT(sizeof(ac_pro_schur_view_t) % 8 == 0, pro_schur_is_8byte_multiple);

/* Pro 独立 scratch union */
typedef union {
    /* B1：Durand-Kerner 数值求根，n ≤ 8
       设计说明 §4.1.1：zre/zim 128 + 分母缓存 64 + 系数 72 = 264 B。
       wden 供 Aberth 的 Weierstrass 乘积缓存使用；DK 路径不读它，
       但保留槽位可以让「DK ↔ Aberth」切换不动 union 尺寸。 */
    struct {
        double zre [AC_PRO_NUMROOT_MAX_DEG];              /*  8 × 8 =  64 */
        double zim [AC_PRO_NUMROOT_MAX_DEG];              /*  8 × 8 =  64 */
        double wden[AC_PRO_NUMROOT_MAX_DEG];              /*  8 × 8 =  64 */
        double coef[AC_PRO_NUMROOT_MAX_DEG + 1];          /*  9 × 8 =  72 */
    } nr;                                                 /* 合计 264 B */

    /* A4：LU 分解（P·A = L·U），n ≤ 12
       设计说明 §3.2.2：L 1152 + U 1152 + piv 48 = 2352 B。 */
    struct {
        double  l  [AC_PRO_MATRIX_MAX_SIZE];              /* 144 × 8 = 1152 */
        double  u  [AC_PRO_MATRIX_MAX_SIZE];              /* 144 × 8 = 1152 */
        int32_t piv[AC_PRO_MATRIX_MAX_DIM];               /*  12 × 4 =   48 */
    } lu;                                                 /* 合计 2352 B */

    /* A6：隐函数求导的遍历工作区
       设计说明 §3.4.2 把 A6 记为「8 B 栈上」—— 那是按复用上游
       s_sym_stack / s_sym_dmap 估算的。本实现刻意不复用上游 scratch：
       上游 s_sym_* 是 ac_sym_derivative / ac_sym_eval 的私有工作区，Pro
       若借用，就与「一次只持有一个视图」的纪律耦合到上游内部实现上。
       自带 1792 B 换来的隔离，被更大的 mv 视图（2688 B）完全吸收，
       .bss 一分钱不多 —— 这正是判据 1「union 吸收」的用法。 */
    struct {
        int32_t walk[AC_PRO_SYM_WALK];                    /* 192 × 4 =  768 */
        int32_t d   [AC_PRO_SYM_D];                       /* 256 × 4 = 1024 */
    } sym;                                                /* 合计 1792 B */

    /* A1/A2/A3：多变量项池（v1.3 新增）
     *
     *   任务书写「新增视图 sym，设计说明预估 1792 B」—— 那个 1792 B 的
     *   `sym` 视图已经在 v1.2 落地了（上一段，A6 隐函数求导的遍历
     *   工作区）。设计说明 §3.1.3/§3.1.4 给 A1/A2 的预估是另外两个视图：
     *       mv   1792 B = 4 槽 × 28 项 × 16 B   （A1 多变量 GCD 项池）
     *       mv2   896 B = 2 槽 × 28 项 × 16 B   （A2 因式分解的额外 2 槽）
     *   本节按设计说明的 mv/mv2 命名落地，并让二者叠加成一个视图：
     *       mv 视图总容量 = 6 槽 = 2688 B（4 + 2，A1 用前 4 槽，A2 再要 2 槽）
     *   叠加而不是并列，是因为 A2 的实现路径是「先做 A1 的 GCD 判可约、
     *   再做内容提取」，两者在同一次调用内先后使用项池，必须同时驻留。
     *   若拆成两个 union 成员，A2 就需要在 GCD 结果与自己的槽位之间来回
     *   搬运，反而引入别名风险。
     *
     * 为什么是 6 槽而不是设计说明的 4 + 2 分开算
     *   设计说明 §3.1.3 的 4 槽 = 操作数 A、B、余式 R1、R2（GCD 的 PRS 需要
     *   同时持有两路余式）；§3.1.4 的额外 2 槽 = 内容提取 + 余式。合起来
     *   最多同时 6 槽，故视图容量取 6 槽。
     *
     * 尺寸：6 × 28 × 16 = 2688 B ≤ 2352？否 —— 2688 > 2352，
     *   所以 v1.3 的 Pro union 峰值从 `lu`（2352）上移到 `mv`（2688），
     *   .bss 随之 +336 B：41104 -> 41440，再加 struct 尾部对齐到 32 的
     *   整数倍后为 41456。这是设计说明 §7.3 预期内的
     *   「v1.3 仍由 lu 决定峰值」判断被实测推翻的地方 —— 设计说明按
     *   「mv 1792 / mv2 896 各自 ≤ 2352 即被吸收」推演，但两者在 A2 里
     *   并发驻留，真实峰值是 2688 而非 1792。此处按实测落地。 */
    struct {
        ac_pro_term_t slot[AC_PRO_MV_SLOTS][AC_PRO_MV_TERMS];  /* 6 × 28 × 16 = 2688 */
    } mv;                                                 /* 合计 2688 B  <-- 峰值（v1.3 起，v1.3.5 未变） */

    /* A3：结式的 Sylvester 行主序缓冲（n ≤ 12）
     *   设计说明 §3.1.5 说「复用 g_mat_work[144]，新增 0 B」。那条路径成立，
     *   但它要求调用方传入 g_mat_work；而 ac_mat_det 的 work 参数也
     *   需要 ≥ n² 且必须与 a 不同（内部先拷贝再消元）。所以数值路径实际
     *   需要两块 144 double：Sylvester 矩阵本体 + det 的工作区。
     *   上游 g_mat_work 只能提供一块，故 Pro 自带一块，另一块用 g_mat_work。
     *   144 × 8 = 1152 B ≤ 2688，被 mv 峰值吸收，.bss 零增长。 */
    struct {
        double sylv[AC_PRO_MATRIX_MAX_SIZE];              /* 144 × 8 = 1152 */
    } res;                                                /* 合计 1152 B */

    /* A5：实特征向量 + Hotelling 收缩 deflation，n ≤ 12
     *
     * 四块缓冲的分工
     *   v[144]    V 矩阵，列主序：第 k 列（v[k*n + i]）是 λ_k 的特征向量。
     *   a[144]    调用方矩阵的可破坏工作副本。每求一个特征对就从
     *             调用方的 const 矩阵重新拷入（见下），再做行阶梯消元
     *             求 (A − λI)v = 0 的零空间 —— 消元会毁掉这块缓冲，
     *             所以它必须是副本，绝不能在调用方矩阵上就地操作。
     *   lam[12]   特征值，按 |λ| 降序（Hotelling 每轮取主特征对）。
     *   work[24]  幂迭代的 x / y 两个 n 向量（各 ≤ 12）+ Rayleigh 商暂存。
     *
     * 为什么 a 副本可以在每轮重新拷入
     *   朴素实现需要同时驻留「原始 A + 当前 deflated 矩阵 + 消元工作区」
     *   三个 n×n（3 × 1152 = 3456 B），远超 2592 B 预算。
     *   本实现利用 A5 的 API 签名 `const double *a`：调用方的矩阵在
     *   整个调用期间保持有效且只读，于是「当前 deflated 矩阵」不必常驻 ——
     *   每轮从调用方矩阵重拷一份，再按已求出的 (λ_j, v_j) 逐项扣除
     *       D_k = A − Σ_{j<k} λ_j · v_j · v_jᵀ
     *   即可。代价是 O(n) 轮 × O(n²) 次减法 = O(n³)，对 n ≤ 12 是
     *   3456 次乘加，可忽略；换来的是省下一整个 n×n 缓冲。
     *
     * 尺寸：1152 + 1152 + 96 + 192 = 2592 B ≤ 2688（mv 峰值）。
     *   被 mv 完全吸收，.bss 零增长 —— v1.3.5 的门禁值仍是 41456。 */
    struct {
        double v   [AC_PRO_MATRIX_MAX_SIZE];              /* 144 × 8 = 1152 */
        double a   [AC_PRO_MATRIX_MAX_SIZE];              /* 144 × 8 = 1152 */
        double lam [AC_PRO_MATRIX_MAX_DIM];               /*  12 × 8 =   96 */
        double work[2 * AC_PRO_MATRIX_MAX_DIM];           /*  24 × 8 =  192 */
    } eig;                                                /* 合计 2592 B */

    /* v2.1.0 ④：稀疏线性方程组（n ≤ 32 / nnz ≤ 320，方案 a）
     * 布局与容量语义见上方 ac_pro_sparse_view_t 的头注。3848 > mv 2688，
     * 本视图是 v2.1.0 起的 union 峰值 —— .bss 由 41456 上移到 42608
     * （设计说明 §3 功能④ 预估 ≈42684，落地 42608，同量级）。 */
    ac_pro_sparse_view_t sparse;                          /* 合计 3848 B  <-- 峰值（v2.1.0 起） */

    /* v2.3.0 功能 5：单变量有理函数工具（poly 视图，v2.3.0 新增）
     *   6 槽整系数多项式（次数 ≤ 8，9 系数 × 8 B = 72 B/槽 = 432 B）+ 次数表
     *   + 工作标记，载荷 460 B、8 字节对齐后 sizeof 464 ≤ 1536（任务书预算线）
     *   < 3848（sparse 峰值）——被 union 完全吸收，.bss 零增长（lu/eig/dec/gb
     *   之后「union 吸收」判据的第五次兑现）。
     * 整系数域契约：GCD/cancel 这类算法只在整系数域
     *   良定（A1 prem 同纪律）：全程 ac_int_t，中间量溢出即拒绝
     *   （AC_PRO_RAT_ECOEF），绝不回绕、绝不出浮点系数。
     * 槽位 0 = p，1 = q，2 = gcd/商，3 = 余式，4/5 = 工作区。
     * apart 的视图纪律：apart 的数值阶段（求根、
     *   A_i、回乘验证）全部在栈上数组进行（次数 ≤ 8）：deg(q) 5..8 时
     *   ac_pro_numroots 写 nr 视图，会整块覆写同 union 的 rat 数据 ——
     *   所以先把 p/q 系数拷到栈、再求根、再回代，rat 视图此后不再读。
     *   deg(q) ≤ 4 走上游 ac_solvers（上游 g_ac_scratch，物理不重叠），
     *   rat 视图安全，但实现统一走栈拷贝，两条路径同构。 */
    struct {
        ac_pro_rat_poly_t s[AC_PRO_RAT_SLOTS];            /* 6 × 72 = 432 */
        int deg[AC_PRO_RAT_SLOTS];                        /* 6 × 4 =  24；-1 = 零多项式 */
        int work;                                         /*           4：标记/临时 */
    } rat;                                                /* 合计 464 B（载荷 460 + 8 字节对齐尾填，v2.3.0 功能 5） */

    /* v2.4.0 功能 8:一般实矩阵特征值(schur 视图,v2.4.0 新增)
     *   Householder 化 Hessenberg → Francis 隐式双移 QR(bulge chasing,
     *   全显式循环)→ 全部特征值(实/复共轭对)+ 反迭代实特征向量;
     *   合计 2744 B ≤ 3848(sparse 峰值)——被 union 完全吸收,.bss 零增长
     *   (ROADMAP.md:145「.bss 零增长设计目标」兑现;布局与编译期断言见
     *   上方 ac_pro_schur_view_t 头注;算法与收敛边界见 src/ac_pro_schur.c
     *   文件头)。非对称输入合法(这正是功能 8 的立项理由:ac_pro.h:269-270
     *   的「非对称返 -2」缺口就此关闭)。 */
    ac_pro_schur_view_t schur;                            /* 合计 2744 B(v2.4.0 功能 8) */

    /* ⑤ 任意精度小数：dec 定点十进制视图（v2.1.0 新增）
     *   5 槽 × 96 B = 480 B ≤ 3848（sparse 峰值，对 mv 2688 亦然）——
     *   被峰值完全吸收，.bss 零增长（v1.3 的 lu、v1.3.5 的 eig、
     *   v2.1.0 的 dec 之后，「union 吸收」判据的又一次兑现）。槽位语义
     *   与规范形约束见上方 ac_pro_dec_t 定义；算法（divmod 余数扩标长除、
     *   位数上限熔断、与上游 g_bi_* 池物理分离）见 src/ac_pro_bigdec.c
     *   的文件头。 */
    struct {
        ac_pro_dec_t d[AC_PRO_DEC_SLOTS];                 /* 5 × 96 = 480 */
    } dec;                                                /* 合计 480 B */

    /* v2.2.0 ⑥：2 变量 Groebner 基（M4GB 逐对归约，新峰值之下）
     *   基 8 槽紧凑项表 + 81 格稠密归约工作区 + 32 对 critical pair 队列，
     *   合计 1888 B ≤ 1960（任务书预算线）< 3848（sparse 峰值）——被 union
     *   完全吸收，.bss 零增长（lu/eig/dec 之后「union 吸收」判据的第四次
     *   兑现）。布局、单项式打包与熔断语义见上方 ac_pro_gb_view_t 的头注；
     *   算法（逐对 S-多项式、稠密伪除归约、终结 interreduce）见
     *   src/ac_pro_groebner.c 的文件头。 */
    ac_pro_gb_view_t gb;                                  /* 合计 1888 B */

    /* v2.5.0 F3：Sturm 序列与实根隔离（feat/solvers 独占视图，v2.5 新增）
     *   精确有理余 PRS（−rem 的正倍数逐成员整化，规避 prem 符号陷阱）+
     *   V(a)−V(b) 闭区间计数 + 显式栈二分隔离（深度 ≤ 48）。
     *   布局（载荷 2912 B,8 字节对齐,sizeof == 2912 由断言链钉死）:
     *     limb  @    0   45×8×4  = 1440  链系数幅值 limb（基数 1e9 低位在前，
     *                    三角打包：成员 k 有 deg_k+1 个系数，≤ 8 limb/系数 =
     *                    72 位十进制 ≈ 239 bits；超限 → ECOEF 离群诚实拒绝）
     *     sgn   @ 1440   45(+3 填充)      系数符号（−1/0/+1）
     *     deg   @ 1488   9×4  =  36      成员次数（deg_0 > deg_1 > … ≥ 0）
     *     off   @ 1524   9×4  =  36      成员 k 的系数起始下标（三角偏移）
     *     nmem  @ 1560    4              链成员数（gcd(p,p')=1 ⟹ 末元为非零常数）
     *     lonum @ 1568   64×8 = 512      二分显式栈：区间左端分子（值 = num/2^dep，
     *                                    num ≤ 2^59 < int64 精确）
     *     hinum @ 2080   64×8 = 512      二分显式栈：区间右端分子
     *     sdep  @ 2592   64×4 = 256      二分显式栈：网格深度（≤ 48）
     *     sflg  @ 2816   64×1 =  64      二分显式栈：占用标志
     *     合计 2912 B（8 的倍数）≤ 3848（sparse 峰值）——被 union 完全
     *   吸收，.bss 零增长（lu/eig/dec/gb/rat/schur 之后「union 吸收」判据的
     *   又一次兑现；ROADMAP §F3 估算 2.0-2.8 KB,落地 2912 B 同量级上沿)。
     *   bigint 工作槽刻意走栈上 ac_bigint_t 局部变量（dec 视图先例：
     *   「工作量走栈上 ac_bigint_t 局部变量,不占 .bss」），视图只存
     *   跨 V() 求值存活的链与二分栈。 */
    struct {
        uint32_t limb[AC_PRO_ST_CHAIN * AC_PRO_ST_LIMBS]; /*    0 .. 1440 : 链系数幅值 limb   */
        int8_t   sgn [AC_PRO_ST_CHAIN];                   /* 1440 .. 1485 : 系数符号          */
        int32_t  deg [AC_PRO_ST_MEMS];                    /* 1488 .. 1524 : 成员次数          */
        int32_t  off [AC_PRO_ST_MEMS];                    /* 1524 .. 1560 : 成员系数起始下标  */
        int32_t  nmem;                                    /* 1560 .. 1564 : 链成员数          */
        int64_t  lonum[AC_PRO_ST_BSTACK];                 /* 1568 .. 2080 : 二分栈左端分子    */
        int64_t  hinum[AC_PRO_ST_BSTACK];                 /* 2080 .. 2592 : 二分栈右端分子    */
        int32_t  sdep [AC_PRO_ST_BSTACK];                 /* 2592 .. 2848 : 二分栈深度        */
        uint8_t  sflg [AC_PRO_ST_BSTACK];                 /* 2848 .. 2912 : 二分栈占用标志    */
    } sturm;                                              /* 合计 2912 B(v2.5.0 F3) */

    /* v2.6.0 G2:SVD 视图(feat/matrix-advanced 独占视图,v2.6 新增)
     *   one-sided Jacobi(Hestenes,dgesvj 同族)m×n(m,n ≤ 12;m < n 走转置),
     *   算法、表达式书写顺序、排序全序、符号规范化与错误码由
     *   docs/contracts/svd-format-v26.md 逐字冻结(本版 C-only,JS v2.7 同构)。
     *   布局(载荷 3760 B,8 的倍数,sizeof == 3760 由断言链钉死):
     *     a    @    0   144×8 = 1152  工作副本 B(Jacobi 就地迭代;缩放域,
     *                  行主序 p×k,p = max(m,n),k = min(m,n);出口归一化后
     *                  即左/右奇异向量的原生侧矩阵)
     *     u    @ 1152   144×8 = 1152  最终 U(列主序:第 j 列 = &u[j*m],
     *                  m×k,列正交;零 σ 列为 Gram-Schmidt 补全向量)
     *     v    @ 2304   144×8 = 1152  最终 V(列主序:第 j 列 = &v[j*n],
     *                  n×k,列正交;Jacobi 旋转累计阵或其转置对偶)
     *     w    @ 3456    12×8 =   96  奇异值,降序(出口已乘回 scale;
     *                  σ ≤ 1e-12·σ₁ 判数值零,精确 0)
     *     work @ 3552    24×8 =  192  排序列临时(work[0..11] b 列 +
     *                  work[12..23] V 列,换序双列同时驻留;补全残量复用
     *                  work[12..23]——补全在排序之后,无并发)
     *     m_st @ 3744     4        =    4  行数 m 停泊(U 列步长,accessor 用)
     *     n_st @ 3748     4        =    4  列数 n 停泊(V 列步长,accessor 用)
     *     k_st @ 3752     4        =    4  已交付个数(= min(m,n),停泊)
     *     pad  @ 3756     4        =    4  显式尾填充,恒 0(sizeof 可预测)
     *     合计 3760 B(8 的倍数)≤ 3848(sparse 峰值)——被 union 完全吸收,
     *   .bss 零增长(lu/eig/dec/gb/rat/schur/sturm 之后「union 吸收」判据的
     *   又一次兑现;ROADMAP §G2 预估 3752 B,开工复核 +8 B:m/n 步长停泊
     *   是矩形 accessor(列指针 + 变步长)的必要状态,schur 方阵口径
     *   stride == count 才得以只停泊一个数;margin 96 → 88 B,吸收判据
     *   不变,sparse 3848 仍是峰值)。
     *   零 static 对象纪律:sweep 计数、旋转常数全走栈/视图,错误码走
     *   返回值(ac_pro.h eig 节实测论证:一个 static = TU 净 +32 B .bss)。 */
    struct {
        double  a   [AC_PRO_MATRIX_MAX_SIZE];             /*    0 .. 1152 : 工作副本 B(缩放域)     */
        double  u   [AC_PRO_MATRIX_MAX_SIZE];             /* 1152 .. 2304 : 最终 U(列主序 m×k)     */
        double  v   [AC_PRO_MATRIX_MAX_SIZE];             /* 2304 .. 3456 : 最终 V(列主序 n×k)     */
        double  w   [AC_PRO_MATRIX_MAX_DIM];              /* 3456 .. 3552 : 奇异值(降序)           */
        double  work[2 * AC_PRO_MATRIX_MAX_DIM];          /* 3552 .. 3744 : 排序双列临时/补全残量  */
        int32_t m_st;                                     /* 3744 .. 3748 : 行数 m 停泊(U 步长)   */
        int32_t n_st;                                     /* 3748 .. 3752 : 列数 n 停泊(V 步长)   */
        int32_t k_st;                                     /* 3752 .. 3756 : 已交付个数(停泊)      */
        int32_t pad;                                      /* 3756 .. 3760 : 显式尾填充,恒 0      */
    } svd;                                                /* 合计 3760 B(v2.6.0 G2) */
} ac_pro_scratch_t;

/* 编译期断言（从严，全部用 ==） */

/* 核心校验：Pro union 的尺寸必须精确等于当前峰值视图。
   用 == 而不是 >=（设计说明 §8.3 对上游 >= 的批评），这样任何人悄悄
   加一个更大的视图都会在编译期被拦下，而不是让 .bss 静默增长。
   峰值自 v2.1.0 起是 sparse（3848），已高于 mv（2688）、eig（2592）、
   lu（2352）；v2.1.0 ⑤ 的 dec（480）同样低于峰值。 */
AC_STATIC_ASSERT(sizeof(ac_pro_scratch_t) == 3848, pro_scratch_is_exactly_peak);

/* 逐视图尺寸：防止「总量没变但视图错位」这种更隐蔽的漂移。 */
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->lu)     == 2352, pro_view_lu_is_2352);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->nr)     ==  264, pro_view_nr_is_264);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->sym)    == 1792, pro_view_sym_is_1792);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->mv)     == 2688, pro_view_mv_is_2688);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->res)    == 1152, pro_view_res_is_1152);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->eig)    == 2592, pro_view_eig_is_2592);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->sparse) == 3848, pro_view_sparse_is_3848);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->dec)    ==  480, pro_view_dec_is_480);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->gb)     == 1888, pro_view_gb_is_1888);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->rat)     ==  464, pro_view_rat_is_464);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->rat)      ==  460 + 4,
                 pro_view_rat_is_8byte_aligned_464);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->schur)   == 2744, pro_view_schur_is_2744);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->sturm)   == 2912, pro_view_sturm_is_2912);
AC_STATIC_ASSERT(AC_PRO_ST_CHAIN == AC_PRO_ST_MEMS * (AC_PRO_ST_MEMS + 1) / 2,
                 pro_sturm_chain_is_triangular_45);
AC_STATIC_ASSERT(AC_PRO_ST_LIMBS == 8, pro_sturm_limbs_is_72_decimal_digits);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->sturm.limb) +
                 sizeof(((ac_pro_scratch_t *)0)->sturm.sgn) + 3 +
                 sizeof(((ac_pro_scratch_t *)0)->sturm.deg) +
                 sizeof(((ac_pro_scratch_t *)0)->sturm.off) +
                 sizeof(((ac_pro_scratch_t *)0)->sturm.nmem) + 4 +
                 sizeof(((ac_pro_scratch_t *)0)->sturm.lonum) +
                 sizeof(((ac_pro_scratch_t *)0)->sturm.hinum) +
                 sizeof(((ac_pro_scratch_t *)0)->sturm.sdep) +
                 sizeof(((ac_pro_scratch_t *)0)->sturm.sflg) == 2912,
                 pro_sturm_payload_is_2912);

/* v2.6.0 G2:svd 视图尺寸链(2.6 开工前按本断言链复核,构成账写进 PR):
   144×8(a) + 144×8(u) + 144×8(v) + 12×8(w) + 24×8(work) + 16(m/n/k_st+pad)
   = 3760,3760 % 8 == 0,≤ 3848(sparse 峰值,margin 88 B)→ union 峰值不动。 */
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->svd) == 3760, pro_view_svd_is_3760);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->svd.a) +
                 sizeof(((ac_pro_scratch_t *)0)->svd.u) +
                 sizeof(((ac_pro_scratch_t *)0)->svd.v) +
                 sizeof(((ac_pro_scratch_t *)0)->svd.w) +
                 sizeof(((ac_pro_scratch_t *)0)->svd.work) + 16 == 3760,
                 pro_svd_payload_is_3760);
AC_STATIC_ASSERT(offsetof(ac_pro_scratch_t, svd) == 0, pro_view_svd_starts_at_zero);
AC_STATIC_ASSERT(offsetof(ac_pro_scratch_t, svd.w) == 3456, pro_svd_w_at_3456);
AC_STATIC_ASSERT(offsetof(ac_pro_scratch_t, svd.work) == 3552, pro_svd_work_at_3552);
AC_STATIC_ASSERT(offsetof(ac_pro_scratch_t, svd.m_st) == 3744, pro_svd_mst_at_3744);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->svd) % 8 == 0, pro_svd_is_8byte_multiple);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->svd) <= sizeof(((ac_pro_scratch_t *)0)->sparse),
                 pro_view_svd_absorbed);

/* 判据 1 在 Pro union 内部：每个视图都必须 ≤ 峰值，
   否则「union 尺寸 = max(视图)」这条前提被破坏。
   峰值自 v2.1.0 起挂 sparse（此前挂 mv，见头注的历史说明）。 */
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->nr)     <= sizeof(((ac_pro_scratch_t *)0)->sparse),
                 pro_view_nr_absorbed);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->sym)    <= sizeof(((ac_pro_scratch_t *)0)->sparse),
                 pro_view_sym_absorbed);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->lu)     <= sizeof(((ac_pro_scratch_t *)0)->sparse),
                 pro_view_lu_absorbed);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->res)    <= sizeof(((ac_pro_scratch_t *)0)->sparse),
                 pro_view_res_absorbed);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->mv)     <= sizeof(((ac_pro_scratch_t *)0)->sparse),
                 pro_view_mv_absorbed);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->eig)    <= sizeof(((ac_pro_scratch_t *)0)->sparse),
                 pro_view_eig_absorbed);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->dec)     <= sizeof(((ac_pro_scratch_t *)0)->sparse),
                 pro_view_dec_absorbed);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->gb)      <= sizeof(((ac_pro_scratch_t *)0)->sparse),
                 pro_view_gb_absorbed);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->rat)      <= sizeof(((ac_pro_scratch_t *)0)->sparse),
                 pro_view_rat_absorbed);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->rat)      <= 1536,
                 pro_view_rat_under_task_budget);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->schur)    <= sizeof(((ac_pro_scratch_t *)0)->sparse),
                 pro_view_schur_absorbed);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->sturm)    <= sizeof(((ac_pro_scratch_t *)0)->sparse),
                 pro_view_sturm_absorbed);
/* v1.3.5 的历史档案：mv 比 eig 恰好大 96 B（当年「.bss 零增长」的充要
   条件）。v2.1.0 峰值上移后这条等式仍然为真，保留作回归锚点。
   v2.1.0 ⑤：dec 480 < mv 2688（当年的峰值）—— lu（v1.3）、eig（v1.3.5）
   之后「union 吸收」判据的第三次兑现；在 sparse（3848）成为现峰值后，
   上面挂 sparse 的 absorbed 校验依然对 dec 成立。 */
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->mv) -
                 sizeof(((ac_pro_scratch_t *)0)->eig) == 96,
                 pro_eig_is_96_bytes_under_mv);

/* 8 字节对齐：把一个 char 放在 union 前面，union 的偏移必须正好是 8。
   若有人把 double 换成 char 数组，这条校验立刻失败。 */
typedef struct {
    char             lead;
    ac_pro_scratch_t s;
} ac_pro_align_probe_t;
AC_STATIC_ASSERT(offsetof(ac_pro_align_probe_t, s) == 8, pro_scratch_is_8_byte_aligned);

/* 每个视图都必须从偏移 0 起（union 的语义前提，也是「编译期 overlay」） */
AC_STATIC_ASSERT(offsetof(ac_pro_scratch_t, nr) == 0, pro_view_nr_starts_at_zero);
AC_STATIC_ASSERT(offsetof(ac_pro_scratch_t, lu) == 0, pro_view_lu_starts_at_zero);
AC_STATIC_ASSERT(offsetof(ac_pro_scratch_t, mv) == 0, pro_view_mv_starts_at_zero);
AC_STATIC_ASSERT(offsetof(ac_pro_scratch_t, res) == 0, pro_view_res_starts_at_zero);
AC_STATIC_ASSERT(offsetof(ac_pro_scratch_t, eig) == 0, pro_view_eig_starts_at_zero);
AC_STATIC_ASSERT(offsetof(ac_pro_scratch_t, sparse) == 0, pro_view_sparse_starts_at_zero);
AC_STATIC_ASSERT(offsetof(ac_pro_scratch_t, dec) == 0, pro_view_dec_starts_at_zero);
AC_STATIC_ASSERT(offsetof(ac_pro_scratch_t, gb) == 0, pro_view_gb_starts_at_zero);
AC_STATIC_ASSERT(offsetof(ac_pro_scratch_t, schur) == 0, pro_view_schur_starts_at_zero);
AC_STATIC_ASSERT(offsetof(ac_pro_scratch_t, sturm) == 0, pro_view_sturm_starts_at_zero);

/* g_var_val 的字节数必须与账本常量一致（80 = 10 × 8）。 */
AC_STATIC_ASSERT(AC_PRO_VAR_VAL_BYTES == AC_PRO_VAR_COUNT * 8, pro_var_val_is_80_bytes);

/* 存储实体
 *
 * 为什么把两块存储装进同一个 struct
 *
 *   PE/COFF 链接器把每个全局对象按 16 B 对齐起放。两个独立全局
 *   （scratch + var_val）会因此被插入 16 B 填充；装进同一个 struct 后
 *   struct 是「顺序排列」而不是「重叠」，g_var_val 仍然是跨调用会话态，
 *   但整块只占一个分配单元，填充无从产生。
 *
 *   关键约束：g_var_val 不能放进 union。判据 3 要求它是跨调用会话态，
 *   而 union 成员会被 lu/nr/mv/sparse/dec 视图覆写。
 */
typedef struct {
    ac_pro_scratch_t scratch;                    /* 3848 B: Pro standalone union (peak: sparse, since v2.1.0) */
    double           var_val[AC_PRO_VAR_COUNT];  /*   80 B: variable values (criterion 3) */
    double           align_pad[1];               /*    8 B: pad to a 32-byte multiple */
} ac_pro_storage_t;

/* 3936 = 3848 + 80 + 8，且 3936 % 32 == 0，链接期不需要额外填充。
   v1.3.5 时是 2688 + 80 + 16 = 2784：sparse 视图把 union 峰值抬高 1160 B，
   同时 3928 距 32 的倍数只差 8，对齐填充从 16 B 收窄到 8 B，净增 1152 B。 */
AC_STATIC_ASSERT(sizeof(ac_pro_storage_t) == 3936, pro_storage_is_exactly_3936);
AC_STATIC_ASSERT(offsetof(ac_pro_storage_t, scratch) == 0, pro_storage_scratch_at_0);
AC_STATIC_ASSERT(offsetof(ac_pro_storage_t, var_val) == 3848, pro_storage_var_val_at_3848);
AC_STATIC_ASSERT(sizeof(ac_pro_storage_t) % 32 == 0, pro_storage_is_32_aligned_multiple);

/* 存储实体（定义在 ac_pro_extra_globals.c）：单个全局对象。 */
extern ac_pro_storage_t g_ac_pro_storage;

/* 两个视图别名沿用上游 ac_scratch.h 的命名风格，让「这块存储叫什么」
 * 在调用点与上游保持一致；它们都是 g_ac_pro_storage 的子对象，不是
 * 独立全局，所以不产生额外的对齐填充。 */
#define g_ac_pro_scratch (g_ac_pro_storage.scratch)
#define g_var_val        (g_ac_pro_storage.var_val)

/* 视图别名：与上游 ac_scratch.h 的 s_xxx 宏同一风格
   仅在 Pro 增量代码里使用；AC_PRO_ACTIVE=0 时这些宏仍存在（指向那个
   不存在的全局），任何使用都会在链接期报未定义符号，而不是悄悄
   读写一块不存在的内存。 */
#define s_pro_nr_re   (g_ac_pro_scratch.nr.zre)
#define s_pro_nr_im   (g_ac_pro_scratch.nr.zim)
#define s_pro_nr_den  (g_ac_pro_scratch.nr.wden)
#define s_pro_nr_coef (g_ac_pro_scratch.nr.coef)
#define s_pro_lu_l    (g_ac_pro_scratch.lu.l)
#define s_pro_lu_u    (g_ac_pro_scratch.lu.u)
#define s_pro_lu_piv  (g_ac_pro_scratch.lu.piv)
#define s_pro_sym_walk (g_ac_pro_scratch.sym.walk)
#define s_pro_sym_d    (g_ac_pro_scratch.sym.d)
/* A1/A2：多变量项池。slot[i] 是第 i 个多项式槽（0..5），每槽 28 项。 */
#define s_pro_mv_slot(i) (g_ac_pro_scratch.mv.slot[(i)])
/* A3：结式的 Sylvester 行主序缓冲（n ≤ 12，n² ≤ 144） */
#define s_pro_res_sylv   (g_ac_pro_scratch.res.sylv)
/* A5：实特征向量 + Hotelling 收缩（n ≤ 12）。
   s_pro_eig_v    V 矩阵，列主序：第 k 列特征向量 = &s_pro_eig_v[k * n]。
   s_pro_eig_a    调用方矩阵的可破坏工作副本（每轮从 const 源重拷）。
   s_pro_eig_lam  特征值，|λ| 降序。
   s_pro_eig_work 幂迭代的两个 n 向量：x = &work[0]，y = &work[n]。 */
#define s_pro_eig_v      (g_ac_pro_scratch.eig.v)
#define s_pro_eig_a      (g_ac_pro_scratch.eig.a)
#define s_pro_eig_lam    (g_ac_pro_scratch.eig.lam)
#define s_pro_eig_work   (g_ac_pro_scratch.eig.work)
/* v2.1.0 ④：稀疏 CSR 视图。val/col/rp 是压缩后的 CSR 本体；rhs 是消元
   过程中的增广右端（跨 API 调用后失效）；x 是解向量（成功后可读）；
   perm[i] = 位置 i 上的原始行号（与 A4 的 lu.piv 同语义）。 */
#define s_pro_sp_val     (g_ac_pro_scratch.sparse.val)
#define s_pro_sp_rhs     (g_ac_pro_scratch.sparse.rhs)
#define s_pro_sp_x       (g_ac_pro_scratch.sparse.x)
#define s_pro_sp_col     (g_ac_pro_scratch.sparse.col)
#define s_pro_sp_rp      (g_ac_pro_scratch.sparse.rp)
#define s_pro_sp_perm    (g_ac_pro_scratch.sparse.perm)
/* ⑤：dec 定点十进制视图（v2.1.0）。slot[i] 是第 i 个 dec 记录（0..4）：
   0 = 操作数 A，1 = 操作数 B，2 = 结果，3/4 预留。 */
#define s_pro_dec_slot(i) (g_ac_pro_scratch.dec.d[(i)])
/* v2.2.0 ⑥：Groebner 基视图。b_coef/b_ep 以「槽 s 的项 t」 = 下标
   s * AC_PRO_GB_TERMS + t 寻址（项按 ep 降序，[s*16] 即首项）；b_cnt[s]
   是槽 s 的项数，b_dead[s] != 0 表示该槽已被更小首项的新元素吸收（可
   复用）；w_dense 是稠密归约工作区（idx = ex*9+ey）；cp_a/cp_b[0..cp_n)
   是 critical pair 队列。 */
#define s_pro_gb_coef(s, t) (g_ac_pro_scratch.gb.b_coef[(s) * AC_PRO_GB_TERMS + (t)])
#define s_pro_gb_ep(s, t)   (g_ac_pro_scratch.gb.b_ep  [(s) * AC_PRO_GB_TERMS + (t)])
#define s_pro_gb_cnt(s)     (g_ac_pro_scratch.gb.b_cnt[(s)])
#define s_pro_gb_dead(s)    (g_ac_pro_scratch.gb.b_dead[(s)])
#define s_pro_gb_dense      (g_ac_pro_scratch.gb.w_dense)
#define s_pro_gb_cp_a       (g_ac_pro_scratch.gb.cp_a)
#define s_pro_gb_cp_b       (g_ac_pro_scratch.gb.cp_b)
#define s_pro_gb_cp_n       (g_ac_pro_scratch.gb.cp_n)
#define s_pro_gb_n          (g_ac_pro_scratch.gb.b_n)
/* v2.3.0 功能 5：poly 视图。s_pro_rat_slot(i) 是第 i 个多项式槽（0..5），
   s_pro_rat_deg(i) 是该槽次数（-1 = 零多项式）。 */
#define s_pro_rat_slot(i)   (g_ac_pro_scratch.rat.s[(i)])
#define s_pro_rat_deg(i)    (g_ac_pro_scratch.rat.deg[(i)])
/* v2.4.0 功能 8:schur 视图。h 是 Hessenberg/迭代就地缓冲(特征值提取完
   毕后复用为反迭代的 M = A − λI 工作副本);v 列主序实特征向量
   (v[k*n + i] = 第 k 个特征向量的第 i 分量,复特征值列全零);
   wr/wi 特征值实/虚部(|λ| 降序,共轭对相邻 +im 在前);work 是
   Householder 与反迭代向量;piv 是反迭代 LU 行交换。 */
#define s_pro_schur_h       (g_ac_pro_scratch.schur.h)
#define s_pro_schur_v       (g_ac_pro_scratch.schur.v)
#define s_pro_schur_wr      (g_ac_pro_scratch.schur.wr)
#define s_pro_schur_wi      (g_ac_pro_scratch.schur.wi)
#define s_pro_schur_work    (g_ac_pro_scratch.schur.work)
#define s_pro_schur_piv     (g_ac_pro_scratch.schur.piv)
#define s_pro_schur_n       (g_ac_pro_scratch.schur.n_st)
/* v2.5.0 F3:Sturm 视图。limb/sgn 三角打包的链系数(成员 k 系数 j =
   下标 off[k]+j,幅值 limb 基数 1e9 低位在前,符号 sgn);deg/off/nmem
   是链结构;lonum/hinum/sdep/sflg 是二分显式栈(值 = num/2^dep)。 */
#define s_pro_st_limb(i)    (g_ac_pro_scratch.sturm.limb[(i)])
#define s_pro_st_sgn(i)     (g_ac_pro_scratch.sturm.sgn[(i)])
#define s_pro_st_deg(k)     (g_ac_pro_scratch.sturm.deg[(k)])
#define s_pro_st_off(k)     (g_ac_pro_scratch.sturm.off[(k)])
#define s_pro_st_nmem       (g_ac_pro_scratch.sturm.nmem)
#define s_pro_st_lonum(i)   (g_ac_pro_scratch.sturm.lonum[(i)])
#define s_pro_st_hinum(i)   (g_ac_pro_scratch.sturm.hinum[(i)])
#define s_pro_st_sdep(i)    (g_ac_pro_scratch.sturm.sdep[(i)])
#define s_pro_st_sflg(i)    (g_ac_pro_scratch.sturm.sflg[(i)])
/* v2.6.0 G2:SVD 视图。a 是缩放域工作副本(Jacobi 就地,出口时已是归一化
   奇异向量矩阵);u/v 列主序(第 j 列 = &u[j*m] / &v[j*n]);w 降序奇异值
   (已乘回 scale);work 是排序列临时 + 补全/闸门残量。 */
#define s_pro_svd_a         (g_ac_pro_scratch.svd.a)
#define s_pro_svd_u         (g_ac_pro_scratch.svd.u)
#define s_pro_svd_v         (g_ac_pro_scratch.svd.v)
#define s_pro_svd_w         (g_ac_pro_scratch.svd.w)
#define s_pro_svd_work      (g_ac_pro_scratch.svd.work)
#define s_pro_svd_m         (g_ac_pro_scratch.svd.m_st)
#define s_pro_svd_n         (g_ac_pro_scratch.svd.n_st)
#define s_pro_svd_k         (g_ac_pro_scratch.svd.k_st)

/* 视图持有纪律（写进头注，而不是靠调用方自觉）
   ① 一次 API 调用只持有一个视图。ac_pro_numroots 用 nr，ac_mat_lu 用 lu，
      ac_pro_multi_var_gcd 用 mv，ac_pro_resultant 用 res，
      ac_pro_eig_all 用 eig，ac_pro_sparse_solve 用 sparse，
      ac_pro_dec_add/sub/mul/div 用 dec，ac_pro_groebner 用 gb —— 任意两者
      都不得嵌套调用（它们共享同一块存储，后进者会整块覆写先前者）。
      gb 视图刻意自带解析器（ac_pro_groebner.c 内部，与 mv 层同一语法），
      不借道 mv 视图解析输入 —— 那会在同一次调用内先后持有 mv 与 gb，
      违反本条纪律。
   ② g_var_val 不参与 overlay：它是跨调用的会话态，任何函数都不得把它
      当草稿写。
   ③ 视图里的数据在跨 API 调用后即失效；需要跨调用保留的结果必须
      落到调用方提供的缓冲或 Pro 的结果池。
   ④ A2 的 mv ↔ ratroot 纪律（设计说明 §3.1.4 的关键约束）：
      ac_pro_multi_var_factor 在调用上游 ac_rat_root_descent 之前，必须
      先把降维后的单变量系数搬到 g_coeffs（独立全局，不在任何 union
      里），再释放 mv 视图。上游 ratroot 会占用上游 ac_scratch 的
      `ratroot` 视图 —— 那是另一块 union（g_ac_scratch），与 Pro 的
      g_ac_pro_scratch 物理上不重叠，所以严格说并不冲突；但「一次只持有
      一个视图」的纪律仍然要求我们把 mv 的数据先落盘到 g_coeffs，
      避免调用方在 ratroot 返回后误以为 mv 里还是自己的数据。 */

#endif /* AC_PRO_SCRATCH_H */
