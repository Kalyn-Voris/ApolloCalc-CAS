/* ============================================================
 * ApolloCalc-CAS · C/128KB-ProTEXT/src/ac_pro_scratch.h
 * ------------------------------------------------------------
 * 【职责 / Responsibility】
 *   定义 Pro 层**独立**的 scratch union —— 设计说明 §2.2 的「模式 β」，
 *   以及唯一一个跨调用存活的 Pro 会话态 g_var_val[10]。
 *
 * 【为什么不能复用上游 ac_scratch_t / Why a separate union】
 *   上游 `C/128KB/src/ac_scratch.h` 逐字节冻结（红线 R1/R3）。要给 Pro
 *   视图腾位置就必须改上游头文件 —— 那等于复制/fork 上游。模式 β 的
 *   做法是：Pro 自带一块 union，尺寸 = 当前已实现功能的最大视图。
 *   v2.1.0 的最大视图是 `sparse`（④ 稀疏线性方程组，3848 B），因此：
 *
 *       sizeof(ac_pro_scratch_t) == 3848      <-- 编译期钉死
 *
 * 【判据 1 在 Pro union 内部依然成立】
 *   Pro 各视图互斥（一次调用只持有一个视图），故 union 尺寸 = max(视图)
 *   而不是 sum(视图)。v2.1.0 的七个视图：
 *       nr     =  264 B  （B1 Durand-Kerner，n ≤ 8）
 *       lu     = 2352 B  （A4 LU 分解，n ≤ 12）
 *       sym    = 1792 B  （A6 隐函数求导工作区）
 *       mv     = 2688 B  （A1/A2/A3 多变量项池，6 槽）
 *       res    = 1152 B  （A3 结式 Sylvester 缓冲）
 *       eig    = 2592 B  （A5 实特征向量 + Hotelling 收缩，n ≤ 12）
 *       sparse = 3848 B  （v2.1.0 ④ 稀疏线性方程组，n ≤ 32 / nnz ≤ 320）
 *                        <-- 峰值（v2.1.0 起，首次推高 union 峰值）
 *   引入更大视图时改尺寸即可，下面那行校验会把不一致炸出来。
 *
 *   【v1.3.5 关键判据（历史）：eig 2592 < mv 2688 —— 当时的 .bss 零增长】
 *     v1.3.5 加入 A5 时 eig 视图比 mv 峰值小 96 B，union 尺寸不变。
 *     v2.1.0 的 sparse 视图（3848 B）**超过** mv（2688 B）1160 B，是
 *     「union 吸收」判据首次**不**兑现的功能 —— 峰值上移是设计说明
 *     （§3 功能④，方案 a）预期内的
 *     成本：.bss 从 41456 上移到 42608（仍远低于 112 KiB 门禁）。下面的
 *     absorbed 测试链已全部改挂 sparse 峰值；mv − eig == 96 这条历史
 *     校验仍然为真，保留作档案。
 *
 * 【8 字节对齐 / 8-byte alignment】
 *   C99 没有 _Alignas。union 成员含 double 与 int32_t/int16_t，自然按 8
 *   对齐（C99 §6.2.8）。下面的 pro_scratch_is_8_byte_aligned 校验把这个
 *   事实钉在编译期，而不是靠注释保证。
 *
 * 【.bss 账本 / Ledger】
 *   g_ac_pro_storage = 3936 B（3848 + 80 + 8 对齐），Pro v2.1.0 目标 .bss：
 *       38672（上游 16 模块内核口径） + 80 + 3848 + 8 = 42608 B
 *   注意：g_var_val 刻意**不**进 union —— 它是跨调用会话态（判据 3），
 *   求值前由调用方写入、求值后仍可读，放进 union 会被其它视图覆写。
 * ============================================================ */

#ifndef AC_PRO_SCRATCH_H
#define AC_PRO_SCRATCH_H

#include <stddef.h>
#include <stdint.h>

#include "ac_pro_config.h"
/* 取用 AC_STATIC_ASSERT（上游 ac_scratch.h:178 定义）与上游矩阵上限。 */
#include "../../../C/128KB/src/ac_scratch.h"
#include "../../../C/128KB/src/ac_matrix.h"

/* ---- Pro 视图的尺寸常量 ---- */
#define AC_PRO_MATRIX_MAX_DIM   12                        /* = AC_MATRIX_MAX_DIM  */
#define AC_PRO_MATRIX_MAX_SIZE  (AC_PRO_MATRIX_MAX_DIM * AC_PRO_MATRIX_MAX_DIM)  /* 144 */
#define AC_PRO_NUMROOT_MAX_DEG  8                         /* B1 数值求根上限（设计说明 §4.1.3） */
#define AC_PRO_VAR_COUNT        10                        /* g_var_val[10]，payload 8 bit 足够 */
#define AC_PRO_SYM_WALK         192                       /* A6 后序遍历标记栈：192 × 4 = 768 */
#define AC_PRO_SYM_D            256                       /* A6 导数索引栈：  256 × 4 = 1024 */

/* ---- v2.1.0 ④ 稀疏线性方程组常量（方案 a，设计说明）----
 *   n ≤ 32、nnz ≤ 320 的 CSR 求解器。容量账（见下方 ac_pro_sparse_view_t）：
 *     val 320×8 + rhs/x 2×32×8 + col 320×2 + rp 33×2 + perm 32×2 = 3842
 *   → 尾部对齐到 8 的倍数 = 3848 B，即设计说明「≈3.9 KB」的落地值。
 * 索引一律 int16_t：n ≤ 32 与 nnz ≤ 320 都远在 32767 内，用 int32 会让
 * 视图膨胀到 4616 B（+768 B .bss），违背方案 a 的紧凑预算。下面的校验把
 * 「索引域必须装得下容量」钉死：谁把 MAX_N/MAX_NNZ 抬过 32767 就编译失败。 */
#define AC_PRO_SP_MAX_N         32                        /* 方程个数上限 */
#define AC_PRO_SP_MAX_NNZ       320                       /* 非零元容量上限（输入与消元填充共用） */

/* ---- A1/A2/A3 多变量基础设施常量（v1.3）----
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

/* ==================== A1/A2：多变量项记录 ====================
 *
 * 设计说明 §3.1.2 选**稀疏**表示而非稠密：
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

/* ==================== v2.1.0 ④：稀疏 CSR 视图 ====================
 *
 * 【布局 / Layout】（偏移按 8 对齐最小化填充的顺序排列）
 *   val  @    0   320 × 8 = 2560   非零元数值（输入 + 消元填充共用容量）
 *   rhs  @ 2560    32 × 8 =  256   增广右端（消元中被同步行交换/行变换）
 *   x    @ 2816    32 × 8 =  256   解向量（回代后按变量序写入）
 *   col  @ 3072   320 × 2 =  640   列索引（int16：n ≤ 32 « 32767）
 *   rp   @ 3712    33 × 2 =   66   行首偏移（row_ptr，rp[n] = 当前 nnz）
 *   perm @ 3778    32 × 2 =   64   行交换记录：位置 i 持有原始行 perm[i]
 *   合计 3842 → 尾部填充到 8 的倍数 = 3848 B（union 新峰值，v2.1.0）
 *
 * 【容量语义 / Capacity semantics】
 *   320 是**输入三元组与消元填充共享**的总容量：输入 nnz ≤ 320；消元
 *   产生的填充若把总非零元推过 320，求解器返回 AC_PRO_SP_EFILL（熔断，
 *   绝不静默丢弃填充 —— 那会返回错值）。这是方案 a 视图尺寸下的诚实
 *   能力边界：填充密集的系统（近稠密输入）本来就不是稀疏求解器的目标
 *   场景，设计说明 §3 功能④ 的 preCheck「nnz ≤ 上限」即此语义。
 *
 * 【字段纪律 / Field discipline】
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

/* ==================== Pro 独立 scratch union ==================== */
typedef union {
    /* ---- B1：Durand-Kerner 数值求根，n ≤ 8 ----
       设计说明 §4.1.1：zre/zim 128 + 分母缓存 64 + 系数 72 = 264 B。
       wden 供 Aberth 的 Weierstrass 乘积缓存使用；DK 路径不读它，
       但保留槽位可以让「DK ↔ Aberth」切换不动 union 尺寸。 */
    struct {
        double zre [AC_PRO_NUMROOT_MAX_DEG];              /*  8 × 8 =  64 */
        double zim [AC_PRO_NUMROOT_MAX_DEG];              /*  8 × 8 =  64 */
        double wden[AC_PRO_NUMROOT_MAX_DEG];              /*  8 × 8 =  64 */
        double coef[AC_PRO_NUMROOT_MAX_DEG + 1];          /*  9 × 8 =  72 */
    } nr;                                                 /* 合计 264 B */

    /* ---- A4：LU 分解（P·A = L·U），n ≤ 12 ----
       设计说明 §3.2.2：L 1152 + U 1152 + piv 48 = 2352 B。 */
    struct {
        double  l  [AC_PRO_MATRIX_MAX_SIZE];              /* 144 × 8 = 1152 */
        double  u  [AC_PRO_MATRIX_MAX_SIZE];              /* 144 × 8 = 1152 */
        int32_t piv[AC_PRO_MATRIX_MAX_DIM];               /*  12 × 4 =   48 */
    } lu;                                                 /* 合计 2352 B */

    /* ---- A6：隐函数求导的遍历工作区 ----
       设计说明 §3.4.2 把 A6 记为「8 B 栈上」—— 那是**按复用上游
       s_sym_stack / s_sym_dmap 估算**的。本实现刻意不复用上游 scratch：
       上游 s_sym_* 是 ac_sym_derivative / ac_sym_eval 的私有工作区，Pro
       若借用，就与「一次只持有一个视图」的纪律耦合到上游内部实现上。
       自带 1792 B 换来的隔离，被更大的 mv 视图（2688 B）完全吸收，
       .bss 一分钱不多 —— 这正是判据 1「union 吸收」的用法。 */
    struct {
        int32_t walk[AC_PRO_SYM_WALK];                    /* 192 × 4 =  768 */
        int32_t d   [AC_PRO_SYM_D];                       /* 256 × 4 = 1024 */
    } sym;                                                /* 合计 1792 B */

    /* ---- A1/A2/A3：多变量项池（v1.3 新增）----
     *
     * 【命名澄清 / Naming】
     *   任务书写「新增视图 sym，设计说明预估 1792 B」—— 那个 1792 B 的
     *   `sym` 视图**已经在 v1.2 落地了**（上一段，A6 隐函数求导的遍历
     *   工作区）。设计说明 §3.1.3/§3.1.4 给 A1/A2 的预估是另外两个视图：
     *       mv   1792 B = 4 槽 × 28 项 × 16 B   （A1 多变量 GCD 项池）
     *       mv2   896 B = 2 槽 × 28 项 × 16 B   （A2 因式分解的额外 2 槽）
     *   本节按设计说明的 mv/mv2 命名落地，并让二者**叠加**成一个视图：
     *       mv 视图总容量 = 6 槽 = 2688 B（4 + 2，A1 用前 4 槽，A2 再要 2 槽）
     *   叠加而不是并列，是因为 A2 的实现路径是「先做 A1 的 GCD 判可约、
     *   再做内容提取」，两者在**同一次调用内**先后使用项池，必须同时驻留。
     *   若拆成两个 union 成员，A2 就需要在 GCD 结果与自己的槽位之间来回
     *   搬运，反而引入别名风险。
     *
     * 【为什么是 6 槽而不是设计说明的 4 + 2 分开算】
     *   设计说明 §3.1.3 的 4 槽 = 操作数 A、B、余式 R1、R2（GCD 的 PRS 需要
     *   同时持有两路余式）；§3.1.4 的额外 2 槽 = 内容提取 + 余式。合起来
     *   最多同时 6 槽，故视图容量取 6 槽。
     *
     * 【尺寸】6 × 28 × 16 = 2688 B ≤ 2352？**否** —— 2688 > 2352，
     *   所以 v1.3 的 Pro union 峰值从 `lu`（2352）**上移到 `mv`（2688）**，
     *   .bss 随之 +336 B：41104 -> 41440，再加 struct 尾部对齐到 32 的
     *   整数倍后为 41456。这是设计说明 §7.3 预期内的
     *   「v1.3 仍由 lu 决定峰值」判断被实测推翻的地方 —— 设计说明按
     *   「mv 1792 / mv2 896 各自 ≤ 2352 即被吸收」推演，但两者在 A2 里
     *   **并发驻留**，真实峰值是 2688 而非 1792。此处按实测落地。 */
    struct {
        ac_pro_term_t slot[AC_PRO_MV_SLOTS][AC_PRO_MV_TERMS];  /* 6 × 28 × 16 = 2688 */
    } mv;                                                 /* 合计 2688 B  <-- 峰值（v1.3 起，v1.3.5 未变） */

    /* ---- A3：结式的 Sylvester 行主序缓冲（n ≤ 12）----
     *   设计说明 §3.1.5 说「复用 g_mat_work[144]，新增 0 B」。那条路径成立，
     *   但它要求调用方**传入** g_mat_work；而 ac_mat_det 的 work 参数也
     *   需要 ≥ n² 且必须与 a 不同（内部先拷贝再消元）。所以数值路径实际
     *   需要两块 144 double：Sylvester 矩阵本体 + det 的工作区。
     *   上游 g_mat_work 只能提供一块，故 Pro 自带一块，另一块用 g_mat_work。
     *   144 × 8 = 1152 B ≤ 2688，被 mv 峰值吸收，.bss 零增长。 */
    struct {
        double sylv[AC_PRO_MATRIX_MAX_SIZE];              /* 144 × 8 = 1152 */
    } res;                                                /* 合计 1152 B */

    /* ---- A5：实特征向量 + Hotelling 收缩 deflation，n ≤ 12 ----
     *
     * 【四块缓冲的分工 / Four buffers】
     *   v[144]    V 矩阵，列主序：第 k 列（v[k*n + i]）是 λ_k 的特征向量。
     *   a[144]    调用方矩阵的**可破坏**工作副本。每求一个特征对就从
     *             调用方的 const 矩阵重新拷入（见下），再做行阶梯消元
     *             求 (A − λI)v = 0 的零空间 —— 消元会毁掉这块缓冲，
     *             所以它必须是副本，绝不能在调用方矩阵上就地操作。
     *   lam[12]   特征值，按 |λ| 降序（Hotelling 每轮取主特征对）。
     *   work[24]  幂迭代的 x / y 两个 n 向量（各 ≤ 12）+ Rayleigh 商暂存。
     *
     * 【为什么 a 副本可以在每轮重新拷入 / Why re-copy each round】
     *   朴素实现需要同时驻留「原始 A + 当前 deflated 矩阵 + 消元工作区」
     *   三个 n×n（3 × 1152 = 3456 B），远超 2592 B 预算。
     *   本实现利用 A5 的 API 签名 `const double *a`：**调用方的矩阵在
     *   整个调用期间保持有效且只读**，于是「当前 deflated 矩阵」不必常驻 ——
     *   每轮从调用方矩阵重拷一份，再按已求出的 (λ_j, v_j) 逐项扣除
     *       D_k = A − Σ_{j<k} λ_j · v_j · v_jᵀ
     *   即可。代价是 O(n) 轮 × O(n²) 次减法 = O(n³)，对 n ≤ 12 是
     *   3456 次乘加，可忽略；换来的是省下一整个 n×n 缓冲。
     *
     * 【尺寸 / Size】1152 + 1152 + 96 + 192 = 2592 B ≤ 2688（mv 峰值）。
     *   被 mv 完全吸收，.bss 零增长 —— v1.3.5 的门禁值仍是 41456。 */
    struct {
        double v   [AC_PRO_MATRIX_MAX_SIZE];              /* 144 × 8 = 1152 */
        double a   [AC_PRO_MATRIX_MAX_SIZE];              /* 144 × 8 = 1152 */
        double lam [AC_PRO_MATRIX_MAX_DIM];               /*  12 × 8 =   96 */
        double work[2 * AC_PRO_MATRIX_MAX_DIM];           /*  24 × 8 =  192 */
    } eig;                                                /* 合计 2592 B */

    /* ---- v2.1.0 ④：稀疏线性方程组（n ≤ 32 / nnz ≤ 320，方案 a）----
     * 布局与容量语义见上方 ac_pro_sparse_view_t 的头注。3848 > mv 2688，
     * 本视图是 v2.1.0 起的 union 峰值 —— .bss 由 41456 上移到 42608
     * （设计说明 §3 功能④ 预估 ≈42684，落地 42608，同量级）。 */
    ac_pro_sparse_view_t sparse;                          /* 合计 3848 B  <-- 峰值（v2.1.0 起） */
} ac_pro_scratch_t;

/* ==================== 编译期断言（从严，全部用 ==） ==================== */

/* 核心校验：Pro union 的尺寸必须**精确**等于当前峰值视图。
   用 == 而不是 >=（设计说明 §8.3 对上游 >= 的批评），这样任何人悄悄
   加一个更大的视图都会在编译期被拦下，而不是让 .bss 静默增长。
   峰值自 v2.1.0 起是 sparse（3848），已高于 mv（2688）、eig（2592）、
   lu（2352）。 */
AC_STATIC_ASSERT(sizeof(ac_pro_scratch_t) == 3848, pro_scratch_is_exactly_peak);

/* 逐视图尺寸：防止「总量没变但视图错位」这种更隐蔽的漂移。 */
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->lu)     == 2352, pro_view_lu_is_2352);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->nr)     ==  264, pro_view_nr_is_264);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->sym)    == 1792, pro_view_sym_is_1792);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->mv)     == 2688, pro_view_mv_is_2688);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->res)    == 1152, pro_view_res_is_1152);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->eig)    == 2592, pro_view_eig_is_2592);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->sparse) == 3848, pro_view_sparse_is_3848);

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
/* v1.3.5 的历史档案：mv 比 eig 恰好大 96 B（当年「.bss 零增长」的充要
   条件）。v2.1.0 峰值上移后这条等式仍然为真，保留作回归锚点。 */
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

/* g_var_val 的字节数必须与账本常量一致（80 = 10 × 8）。 */
AC_STATIC_ASSERT(AC_PRO_VAR_VAL_BYTES == AC_PRO_VAR_COUNT * 8, pro_var_val_is_80_bytes);

/* ==================== 存储实体 ====================
 *
 * 【为什么把两块存储装进同一个 struct / Why one enclosing object】
 *
 *   PE/COFF 链接器把**每个**全局对象按 16 B 对齐起放。两个独立全局
 *   （scratch + var_val）会因此被插入 16 B 填充；装进同一个 struct 后
 *   struct 是「顺序排列」而不是「重叠」，g_var_val 仍然是跨调用会话态，
 *   但整块只占一个分配单元，填充无从产生。
 *
 *   关键约束：g_var_val **不能**放进 union。判据 3 要求它是跨调用会话态，
 *   而 union 成员会被 lu/nr/mv/sparse 视图覆写。
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

/* ---- 视图别名：与上游 ac_scratch.h 的 s_xxx 宏同一风格 ----
   仅在 Pro 增量代码里使用；AC_PRO_ACTIVE=0 时这些宏仍存在（指向那个
   不存在的全局），任何**使用**都会在链接期报未定义符号，而不是悄悄
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
   s_pro_eig_v    V 矩阵，**列主序**：第 k 列特征向量 = &s_pro_eig_v[k * n]。
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

/* ---- 视图持有纪律（写进头注，而不是靠调用方自觉） ----
   ① 一次 API 调用只持有一个视图。ac_pro_numroots 用 nr，ac_mat_lu 用 lu，
      ac_pro_multi_var_gcd 用 mv，ac_pro_resultant 用 res，
      ac_pro_eig_all 用 eig，ac_pro_sparse_solve 用 sparse —— 任意两者都
      不得嵌套调用（它们共享同一块存储，后进者会整块覆写先前者）。
   ② g_var_val 不参与 overlay：它是跨调用的会话态，任何函数都不得把它
      当草稿写。
   ③ 视图里的数据在**跨 API 调用后即失效**；需要跨调用保留的结果必须
      落到调用方提供的缓冲或 Pro 的结果池。
   ④ **A2 的 mv ↔ ratroot 纪律（设计说明 §3.1.4 的关键约束）**：
      ac_pro_multi_var_factor 在调用上游 ac_rat_root_descent 之前，必须
      先把降维后的单变量系数**搬到 g_coeffs**（独立全局，不在任何 union
      里），再释放 mv 视图。上游 ratroot 会占用上游 ac_scratch 的
      `ratroot` 视图 —— 那是**另一块** union（g_ac_scratch），与 Pro 的
      g_ac_pro_scratch 物理上不重叠，所以严格说并不冲突；但「一次只持有
      一个视图」的纪律仍然要求我们把 mv 的数据先落盘到 g_coeffs，
      避免调用方在 ratroot 返回后误以为 mv 里还是自己的数据。 */

#endif /* AC_PRO_SCRATCH_H */
