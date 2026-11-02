/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_pro.h — ProTEXT 层唯一公开头文件,上游 C/128KB/src/ 一行不改

   Pro 的全部增量 API 都从这里对外。三层结构:上游包装层(src/ac_pro_<mod>.c)只做
   #include "../../../C/128KB/src/ac_<mod>.c",把上游 16 个模块各编译一次;唯一例外是
   ac_pro_solvers.c 里的符号重命名拦截(见下)。Pro 增量层(src/ac_pro_*_ext.c +
   ac_pro_extra_globals.c)是新 TU,只调用上游公开 API 与上游公开全局,不 fork 上游源码。
   存储层(src/ac_pro_scratch.h)是 Pro 独立 union(模式 β),尺寸由编译期断言钉死。

   拦截方式:ac_guard_degree 在上游 ac_solvers.c 内定义。Pro 不能改上游,也不能在链接期
   覆盖同名符号(会 multiple definition)。做法是在包装 TU 里用宏重命名把上游那份改成
   ac_pro_upstream_guard_degree,再由 Pro 增量层提供同签名的 ac_guard_degree:

       #if AC_PRO_ACTIVE
       #define ac_guard_degree ac_pro_upstream_guard_degree
       #endif
       #include "../../../C/128KB/src/ac_solvers.c"

   宏在 ac_solvers.h 的声明处同样生效,故重命名是自洽的(声明与定义一起改名,不会出现
   「声明的是 A、定义的是 B」)。上游源码零改动,AC_PRO_ACTIVE=0 的退化构建里宏不生效,
   符号与上游逐位一致。

   节点 var 字段(本项目最关键的一次「不改上游」):设计说明 §1.4 判据 2 发现 ac_sym_node_t
   的偏移 12..15 是填充洞(type 1 + pad 3 + left 4 + right 4 = 12,value 对齐到 16)。直接
   给结构体加字段需要改上游头文件,而该头文件被 6 个上游 TU 包含,改了就会出现「Pro TU
   认为 sizeof=24,上游 TU 仍认为 sizeof=24 但字段布局不同」的 ODR 式错配,这正是任务书
   要求上报的陷阱。

   本项目的解法:完全不碰结构体定义,用字节偏移 + memcpy 访问那块填充洞,并用三条编译期
   断言证明洞确实在 12..15:

       sizeof(ac_sym_node_t) == 24
       offsetof(ac_sym_node_t, value)  == 16
       offsetof(ac_sym_node_t, right)  ==  8

   12..15 未被任何上游代码读写(上游 sym_new 只清 pad[0..2],即 1..3),所以这是一块真正
   空闲且尺寸精确的 4 字节。memcpy 经 unsigned char* 是标准明确允许的访问方式,无严格
   别名问题、无对齐问题、无 UB。详见 ac_pro_symbolic_ext.c 的 pro_node_set_var /
   pro_node_get_var。 */

#ifndef AC_PRO_H
#define AC_PRO_H

#include "ac_pro_config.h"
#include "ac_pro_scratch.h"

#include "../../../C/128KB/src/ac_solvers.h"
#include "../../../C/128KB/src/ac_symbolic.h"
#include "../../../C/128KB/src/ac_matrix.h"

/* 阶段一①：内存水位门禁（修正上游死代码）
 * 上游 ac_check_memory_watermark 的两个硬伤（设计说明 §6.3）：
 *   硬伤一：估算上限 17320 B，阈值 98304 B，比值 5.68×，任何输入都不触发；
 *   硬伤二：漏算 g_ac_scratch（8224）与 g_sym_nodes（12288），低估 27496 B。
 * Pro 侧改为两段式：编译期真实静态量 + 运行期动态量。
 *
 * 返回值与上游同约定：0 = 通过；1 = 拒绝（g_solve_error = 4）。
 * extra_bytes 是测试注入用的假池字节数（正常调用传 0），
 * 这样「门禁到底会不会触发」可以被真正测出来，而不是靠改全局变量。 */
double ac_pro_memory_used(double extra_bytes);
int    ac_pro_check_memory_watermark_ex(double extra_bytes);
int    ac_pro_check_memory_watermark(void);

/* 阶段二 B1：五次及以上数值求根
 * Durand-Kerner（Aberth 初始化）。n ≤ 8（设计说明 §4.1.3）。
 * 系数按升序传入：coef[0] + coef[1]·x + ... + coef[n]·x^n。
 * 约束：coef 不得指向 g_ac_pro_scratch（内部会把它拷进 nr 视图）。
 * 返回 1 = 成功（g_root_count = n，g_solve_error = 0）；
 *      0 = 失败（g_solve_error = 3 不收敛 / 1 次数越界 / 2 前置拒绝）。 */
int ac_pro_numroots(const double *coef, int degree);

/* 采样提取 1..8 次多项式的系数（上游 ac_extract_coefficients 只支持 1..4）。
 * 复用上游 g_matrix_pool（156 double）做 (n+1)×(n+2) 增广矩阵，
 * 结果写入 nr 视图的 coef[0..degree]。返回 0 = 成功。 */
int ac_pro_extract_coefficients(int degree);

/* 端到端：从当前已解析的表达式走「提取 → 数值求根」。
 * degree 必须 5..8；1..4 请走上游精确路径。 */
int ac_pro_solve_numeric_from_expr(int degree);

/* 次数路由：0 = 精确路径(1..4)，1 = 数值路径(5..8)，-1 = 拒绝(>8 或 <1)。 */
int ac_pro_degree_route(int degree);

/* 阶段二 A4：LU 分解
 * 部分主元 Doolittle：P·A = L·U。
 *   piv[i] = 第 i 行来自原始矩阵的第 piv[i] 行，即 (P·A)[i][j] = A[piv[i]][j]。
 *   L 为单位下三角（对角恒 1，仍按满阵存），U 为上三角。
 * 结果落在 Pro union 的 lu 视图（2352 B）：ac_pro_lu_l() / _u() / _piv()。
 * n 必须 1..12。返回 1 = 成功；0 = 奇异（任一主元 |·| < 1e-12）。
 * 奇异时 L/U/piv 的内容未定义，调用方不得使用。 */
int            ac_mat_lu(const double *a, int n);
const double  *ac_pro_lu_l(void);
const double  *ac_pro_lu_u(void);
const int32_t *ac_pro_lu_piv(void);

/* 阶段二 A6：隐函数求导
 * dy/dx = −F_x / F_y。
 *
 * 变量命名：x -> 0，y -> 1，a..j -> 2..11 中的前 8 个（共 10 个槽）。
 * 支持：整数常量、变量、+ − * / ^、括号、一元负号。
 * 不支持：函数调用（上游 NODE_* 只有 CONST/VAR/ADD/SUB/MUL/POW 五种，
 *         加 NODE_FUNC 会改上游结构体语义，超出 v1.2 边界）。
 *
 * 节点 var 标签：写在 ac_sym_node_t 偏移 12..15 的填充洞里（见头注），
 * sizeof 仍为 24，g_sym_nodes 仍为 12288 B，.bss 零增长。 */
int    ac_pro_sym_parse(const char *src);            /* 返回根索引，失败 -1 */
int    ac_pro_sym_parse_for(const char *src, int target_var);
double ac_pro_sym_eval_vars(int root);               /* 读 g_var_val[] */

/* 高层入口：解析 expr，求 F_x / F_y，在 (x, y) 处算出 −F_x/F_y。
 * 返回值 = 错误码（0 = 成功；1 = 解析/无 y；2 = 预算；3 = F_y≈0；4 = 求值）。
 * 失败时 *dydx 不被改写。
 *
 * 为什么不是「bool + ac_pro_implicit_error()」：错误码若存 static，
 * 该 TU 会多出一个 16 B 的 .bss 段，叠加 16 B 链接对齐后 .bss 涨 32 B，
 * 直接破坏 42608 的账本。详见 ac_pro_symbolic_ext.c 的文件头。 */
int ac_pro_implicit_derivative(const char *expr, double x, double y,
                               double *dydx);

/* 错误码常量（与 ac_pro_implicit_derivative 的返回值同源） */
#define AC_PRO_IMP_OK       0
#define AC_PRO_IMP_EPARSE   1
#define AC_PRO_IMP_EBUDGET  2
#define AC_PRO_IMP_EFLAT    3
#define AC_PRO_IMP_EEVAL    4

/* v1.3 方向 A：多变量代数结构
 *
 * 共同约束（设计说明 §6.4 的 preCheck 参数矩阵）
 *   变量数 = 2（x 与 y）
 *   总次数 ≤ 6（AC_PRO_MV_DEGREE）
 *   项数 ≤ 28/多项式（AC_PRO_MV_TERMS = C(8,2)）
 *   系数为 int64，所有 + − × 走上游 ac_safe_mul/add/sub；
 *   溢出置 g_solve_error = 5 并失败，绝不返回错值。
 *
 * 多项式字符串语法
 *   项用 + / - 分隔；每项形如 [系数][*]x[^n][*]y[^m]，顺序可换，
 *   变量可缺省（纯常数项），系数缺省为 1，指数缺省为 1。
 *   例："x^2*y + 2*x - 3"、"2*x^2 - 8"、"x*y - 6"
 *   不支持括号、除法、负指数 —— 多变量层只处理多项式。
 */

/* A1：多变量 GCD（子结式 PRS）
 * 结果归一化为「原始部分 + 首项系数为正」，例如
 *     gcd(2x²−2y², 4x²−4xy) -> "x - y"（而非 2x−2y 或 x−y 的常数倍）
 * 返回：1 = 成功（out 已写入）；0 = 平凡（两者皆零）；-1 = 非法/溢出。
 * g_solve_error：0 正常 / 1 次数超限 / 2 语法错 / 5 系数溢出。 */
int ac_pro_multi_var_gcd(const char *fsrc, const char *gsrc, char *out, int out_len);

/* A2：多变量因式分解（内容提取 + 单变量因子）
 * 设计说明 §3.1.4：只做「内容提取 + 单变量因子」。
 * 若多项式退化为单变量（只含 x 或只含 y）且次数 ≤ 4，把系数搬到
 * 上游 g_coeffs，再调 ac_rat_root_descent 做有理根降次。
 *
 * 输出形如 "2 * (x - 1) * (x + 2) * 1"（内容 · 各因子 · 余式）。
 * 返回：1 = 成功；0 = 超出边界（真 2 变量 / 次数 > 4）；
 *      -1 = 非法/溢出。
 * *content 回填整数内容（可为 NULL）。 */
int ac_pro_multi_var_factor(const char *src, char *out, int out_len,
                            ac_int_t *content);

/* A3：结式 Resultant（数值路径，Sylvester + ac_mat_det）
 * 在 x = x0 处计算 Res_y(f(x0,y), g(x0,y))。
 * 阶数 = deg_y(f) + deg_y(g)，严格 ≤ 12（AC_MATRIX_MAX_DIM）。
 * 超过 12 直接拒绝（返回 0，*out 不被改写）—— 绝不截断。
 *
 * 返回：1 = 成功（*out 已写入，可能是 0 —— 0 表示 f、g 有公共根，
 *      这是有意义的结果而非错误）；0 = 拒绝；-1 = 内部错误。
 * g_solve_error：0 正常 / 1 阶数超限 / 2 输入非法 / 3 数值失败。 */
int ac_pro_resultant(const char *fsrc, const char *gsrc, double x0, double *out);

/* 构造 Sylvester 矩阵但不取行列式（供测试检查矩阵本体）。
 * 返回阶数 n（0 = 失败）；矩阵本体用 ac_pro_resultant_sylvester() 读。 */
int            ac_pro_resultant_build(const char *fsrc, const char *gsrc, double x0);
const double  *ac_pro_resultant_sylvester(void);

/* 窄接口：供 A3 解析并读回项表（A1 模块内部实现）
 * 这两个接口刻意只认槽号而不暴露 mv_poly_t，避免槽位纪律泄漏到
 * 多个 TU。正常调用方不需要它们。 */
int                  ac_pro_multi_parse_into(const char *src, int slot);
const ac_pro_term_t *ac_pro_multi_slot_terms(int slot);
int                  ac_pro_multi_slot_degree(int slot);

/* v2.0.0：多变量符号求导（d/dx、d/dy）
 * 对 A1/A2/A3 的多项式字符串做逐项求导，输出同一套归一化格式。
 * 实现在 src/ac_pro_diff_multi.c，复用 mv 视图的槽 0（输入）与
 * 槽 1（输出），调用期间不得嵌套任何其它 Pro 视图 API。
 *
 * var_index 语义：0 = 对 x 求导，1 = 对 y 求导；其他值直接拒绝。
 * 项规则：exp_var == 0 的项丢弃；否则新系数 = c * exp_var（走上游
 *   ac_safe_mul，溢出置 g_solve_error = 5 并失败，绝不返回错值），
 *   exp_var 减 1。
 * 输出格式：与 A1 的归一化格式一致："2*x*y + y^2"、指数 1 省略、
 *   零多项式写 "0"。
 * 返回约定：1 = 成功（含结果为 "0" 的情形）；-1 = 错误，且
 *   g_solve_error 置因：2 = var_index 非法 / 语法错 / 输出缓冲不足，
 *   5 = 系数溢出。
 *
 * 示例：
 *     char buf[64];
 *     ac_pro_diff_multi("x^2*y + x*y^2", 0, buf, (int)sizeof(buf));
 *         -> 返回 1，buf == "2*x*y + y^2"
 *     ac_pro_diff_multi("x^2*y + x*y^2", 1, buf, (int)sizeof(buf));
 *         -> 返回 1，buf == "x^2 + 2*x*y" */
int ac_pro_diff_multi(const char *src, int var_index, char *out, int out_len);

/* 多变量多项式积分（逐项、精确整除）
 * 对 f(x,y) = Σ c·x^a·y^b 按指定变量做形式积分：
 *     ∫ c·x^a·y^b dx = (c/(a+1)) · x^(a+1) · y^b
 *     ∫ c·x^a·y^b dy = (c/(b+1)) · x^a  · y^(b+1)
 *
 * var_index 语义：0 = 对 x 积分；1 = 对 y 积分；其它值 = 拒绝
 * （返回 -1 且 g_solve_error = 2）。
 *
 * 整除约束：ac_pro_term_t 无分母字段，系数必须能被 (指数+1) 精确
 *   整除：c % (a+1) != 0（对 dy 则 c % (b+1) != 0）的项按
 *   AC_PRO_MVI_ENONEXACT 拒绝 —— 绝不返回近似值、绝不截断。
 *   INT64_MIN 系数按溢出处理拒绝（g_solve_error = 5）。
 *
 * 次数约束：结果总次数 (指数+1)+另一变量指数 ≤ AC_PRO_MV_DEGREE(6)，
 *   超限返回 -1 且 g_solve_error = 1（如 ∫x^6 dx、∫x^3*y^3 dx）。
 *   该门禁同时蕴含 exps 4-bit 域必然可写（每指数 ≤ 6 < 15）。
 *
 * 超越函数 = 解析期拒绝：本层复用纯多项式解析器
 *   （见上方「多项式字符串语法」：不支持括号/除法/负指数），
 *   sin/exp 等记号在解析期即语法错误。因此单变量 B2 的
 *   AC_PRO_INT_ETABLE 语义在本层自然表现为：返回 -1 且
 *   g_solve_error = 2。这是同一「不支持」语义在纯多项式层上的
 *   诚实表现，没有伪造的 ETABLE 路径。
 *
 * 缓冲：out/out_len 由调用方提供；out 为 NULL、out_len < 2 或
 *   内容写不下时返回 -1 且 g_solve_error = 2。失败时 out 内容未定义。
 *
 *   1                        = 成功（out 已写入；∫0 的结果 "0" 也是成功）
 *   -1                       = 一般错误，粗因看 g_solve_error（与
 *                              ac_pro_multi_var_gcd 同表）：
 *                                1 = 结果次数超限
 *                                2 = 语法 / var_index / 非整除粗因 /
 *                                    缓冲不足 / 超越函数解析期拒绝
 *                                5 = 系数溢出（INT64_MIN）
 *   AC_PRO_MVI_ENONEXACT(-6) = 非精确整除 —— 唯一的细粒度负码，
 *                              直接作为返回值，同时 g_solve_error = 2。
 *
 *   为什么 -6 不走「专用 getter 查询」：那需要 static 保存「上一次
 *   错误」，实测（见下方 ac_pro_eig_all 的注释）一个 static 就让 TU
 *   多出 16 B .bss 段并叠加 16 B 链接对齐，全程序 .bss 净涨 32 B，
 *   直接破坏 42608 门禁。返回值通道零存储、不依赖调用顺序，更易测。
 *
 * 视图：只占用 mv 视图的槽 0（解析输入）与槽 1（积分输出），
 *   不申请新视图；ac_pro_scratch.h 一行未改，.bss 增量为 0 —— 连
 *   任务卡预留的 frac 视图（480 B）都不需要（整除约束把分数挡在
 *   门外，见 ac_pro_integrate_multi.c 文件头的「边界」小节）。 */
int ac_pro_integrate_multi(const char *src, int var_index, char *out, int out_len);

/* v1.3.5 A5：实特征向量 + Hotelling 收缩
 *
 *   ① 主特征对：对 A 做带位移的幂迭代（Rayleigh 商），得到 |λ_1| 最大的
 *      特征对 (λ_1, x_1)。
 *   ② 特征向量求解的第二条路径（也是测试对拍的依据）：解
 *      (A − λI)v = 0 的零空间 —— 把 A − λI 做行阶梯消元（复用
 *      ac_mat_rank 同款 mat_echelon 逻辑），自由变量取 1、回代出 v。
 *      幂迭代给出 x_1 后，用零空间解再求一次并比对（两者必须平行），
 *      这是「特征向量确实满足 (A−λI)v=0」的直接证据，而不是只验证
 *      「Av ≈ λv」。
 *   ③ Deflation：Hotelling 收缩 A ← A − λ_1 · x_1 · x_1ᵀ（x_1 已归一化），
 *      收缩后的矩阵把 λ_1 压成 0，其余特征值不变；再对收缩矩阵求主
 *      特征对，重复 n 轮即得全部 n 个特征对。
 *
 *   n 必须 1..12（AC_PRO_MATRIX_MAX_DIM）。矩阵行主序、n×n、稠密。
 *   要求 A 为实对称矩阵（Hotelling 收缩在非对称矩阵上不保谱，
 *   这是 A5 的适用范围声明，不是实现缺陷）；非对称输入返回 -2。
 *
 * 视图：eig（2592 B：V 144 + A 副本 144 + λ 12 + work 24）。
 *   一次调用只持有 eig 视图，调用期间不得嵌套任何其它 Pro 视图 API。
 *   调用方矩阵 `a` 在整个调用期间必须有效且只读（内部不写它）。
 *
 *   特征值：ac_pro_eig_value(k)，按 |λ| 降序，k = 0..n−1。
 *   特征向量：ac_pro_eig_vector(k) 返回列主序第 k 列的起始指针，
 *            即 v[i] 位于 ac_pro_eig_vector(k)[i]，i = 0..n−1。
 *             两者都返回内部视图指针，跨 API 调用后失效。
 *
 *   1 = 成功（n 个特征对全部求出，视图已写入）；
 *   ≤ 0 = 失败，且错误码就是返回值本身：
 *        0  = 内部错误（不应出现）
 *       -1  = n 越界（必须 1..12）
 *       -2  = 非对称矩阵（A5 适用范围外）
 *       -3  = 幂迭代不收敛（max_iter 内未达容差）
 *       -4  = 零向量 / 退化为零矩阵（特征向量无法归一化）
 *   失败时视图内容未定义，调用方不得读取。
 *
 *   本仓库实测（gcc 8.1.0 / MinGW-w64 ld，见 ac_pro_symbolic_ext.c:89-103）：
 *   一个 `static int` 会让该 TU 产生 16 B 的 .bss 段，叠加 16 B 链接对齐，
 *   全程序 .bss 净涨 32 B（42608 → 42640），直接破坏任务书的门禁。
 *   错误码作为返回值不需要任何静态存储，且更易测（不依赖调用顺序）。
 *   这是判据 2 的直接推论：「不在 union 里的静态量按 32 B 计，不是 4 B」。 */
int            ac_pro_eig_all(const double *a, int n);
double         ac_pro_eig_value(int k);
const double  *ac_pro_eig_vector(int k);
int            ac_pro_eig_count(void);

/* v1.3.5 B2：符号积分（表匹配 + 分部积分）
 * v2.3.0 功能 4 修订：表 64 → 128 项（二次核族 / 倒数函数族 / 函数幂与
 * x^n·f(x) 扩充）；分部积分从「只做一层」改为「固定两层」。
 *
 *   ① 表匹配：128 项标准积分表（static const，落 .rdata，不占 .bss）。
 *      输入表达式先做线性替换归一化，再按「函数头 + 线性内层」的模式
 *      逐项匹配，命中即返回原函数。
 *   ② 分部积分：∫u dv = u·v − ∫v du，固定两层（v2.3.0 功能 4）。
 *      层数是硬编码上界 2 的递减参数：外层把被积函数拆成 u·dv，对
 *      ∫v du 允许再做一次分部，之后只查表/多项式，到 0 关死——无同形
 *      自调用、无层数随输入增长的通路，绝不无限展开。
 *
 *   复用上游 g_sym_nodes arena（AC_SYM_MAX_NODES = 512，全局共享）。
 *   进入前必须检查剩余预算：
 *       表匹配路径     ≤ 256 节点
 *       分部积分路径   ≤ 2×96 节点（每层额外，合计 448 ≤ 512）
 *   预算不足时返回 AC_PRO_INT_EBUDGET，绝不越界写 arena。
 *   这条检查不是防御性编程的客套 —— g_sym_nodes 是上游 6 个 TU 共享的
 *   单一 arena，越界写会静默破坏其它模块的表达式树。
 *
 *   变量固定为 x。支持：整数/小正常量、x、+ − * / ^、括号、一元负号，
 *   以及 sin/cos/tan/exp/ln/log/sqrt/asin/acos/atan/sinh/cosh/tanh。
 *   输出写到调用方缓冲（out/out_len），不产生静态存储。
 *
 * 返回：0 = 成功（out 已写入）；负值 = 错误码（见下）。
 *   失败时 out 内容未定义，调用方不得使用。 */
int ac_pro_integrate(const char *expr, char *out, int out_len);

#define AC_PRO_INT_OK        0    /* 成功 */
#define AC_PRO_INT_EPARSE   (-1)  /* 语法错误 / 空输入 / 非法字符 */
#define AC_PRO_INT_ETABLE   (-2)  /* 128 项积分表未命中（且两层分部也未命中） */
#define AC_PRO_INT_EBUDGET  (-3)  /* g_sym_nodes 预算不足（表 256 / 分部 2×96） */
#define AC_PRO_INT_EOVER    (-4)  /* 输出缓冲不足 */
#define AC_PRO_INT_EDEGREE  (-5)  /* 次数/指数超限（如 x^1000） */

/* 积分表规模（供测试核对表项数与设计说明一致；v2.3.0 功能 4 起 128）。 */
#define AC_PRO_INT_TABLE_SIZE 128

/* 多变量多项式积分的错误码（续 AC_PRO_INT_* 编号区）
 * 通道选型：失败直接以错误码为返回值（细粒度通道），同时把粗因
 * 写入 g_solve_error（多变量族通道，见 ac_pro_integrate_multi 的
 * 注释）。刻意不设 AC_PRO_MVI_EPARSE/EOVER/...：那些语义与本文件
 * 上方 AC_PRO_INT_* 一一对应，复用同值即可，避免两套平行码漂移；
 * 只有本层独有的失败模式（非精确整除）才值得一个新码。 */
#define AC_PRO_MVI_ENONEXACT (-6) /* 系数不能被 (指数+1) 整除：原函数非整系数 */

/* v2.3.0 功能 5：单变量有理函数工具
 * （GCD / cancel 约分 / apart 部分分式；实现于 src/ac_pro_rational.c）
 *
 *   单变量 x、整系数域：次数 ≤ 8（AC_PRO_RAT_MAX_DEG）、输入系数
 *   |c| ≤ 9999（AC_PRO_RAT_COEF_MAX）。中间量（prem/Euclid 的伪除系数）
 *   溢出即拒绝，绝不回绕 —— GCD/cancel 只在整系数域良定（A1 prem 同纪律）。
 *   视图：Pro union 的 rat 槽（464 B，v2.3.0 起，低于 sparse 峰值被吸收）。
 *
 *   多项式串：项 = [符号] [系数] [x[^n]]，隐式系数 1、隐式次数 1、`*` 可选，
 *   空白任意；与前游 gb 解析器同风格（ac_pro_groebner.c:106）。没有括号、
 *   分数、除法、多变量 —— 有理式一律写成 "p/q"（恰一个顶层 '/'），
 *   gcd 走两个独立参数。
 *
 *   ac_pro_rat_arith(f, op, g, out,n) 多项式四则的 + - *（op ∈ '+','-','*'；
 *                                     除法走伪除 pdivmod 支撑 gcd/cancel，
 *                                     不单设——商带余式，字符串语义不唯一）。
 *   ac_pro_rat_gcd(f, g, out, n)     最大公因式：本原化（容度 1、首项为正）
 *                                    的本原最大公因式；互素输出 "1"。
 *                                    gcd(0,g)=primitive(g)；gcd(0,0) 拒绝。
 *   ac_pro_rat_cancel(f, out, n)     f = "p/q"：约分到 (P)/(Q)，Q 已本原化且
 *                                    首项为正，P/Q 的整数容差也约尽；Q=1 时
 *                                    直接输出 P；P=0 输出 "0"。
 *   ac_pro_rat_apart(f, out, n)      f = "p/q"：部分分式。前置：真分式
 *                                    (deg p < deg q ≥ 1)、q 的根互异且全实
 *                                    （deg q ≤ 4 走上游精确根、5..8 走 DK）。
 *                                    A_i = p(r_i)/q'(r_i)（互异单根闭式），
 *                                    重根/复根/假分式 → EDOM。回乘验证：
 *                                    Σ A_i·Π_{j≠i}(x−r_j) 与 p 在 dq 个采样点
 *                                    的数值残差 ≤ 1e-9·max(1,|p|)，不过即
 *                                    EDOM —— 绝不输出未验证的分解。
 *
 *   多项式：降幂 "3*x^2-x-1"（±1 折叠、负号并项）。cancel 恒 "(P)/(Q)"。
 *   apart：项 = A_i/(x ∓ r_i)（r>0 "x - R"、r<0 "x + M"、r=0 "x"），按 r
 *   降序 " + " 连接；A_i 与 r 的数值串走三级 fmt（整判 1e-9 / 有理重建
 *   q≤1000 / 定点 12 位），与 ODE 契约 §5.1 同算法。
 *
 *   失败时 out 内容未定义。 */
int ac_pro_rat_arith(const char *f, char op, const char *g,
                     char *out, int out_len);
int ac_pro_rat_gcd(const char *f, const char *g, char *out, int out_len);
int ac_pro_rat_cancel(const char *f, char *out, int out_len);
int ac_pro_rat_apart(const char *f, char *out, int out_len);

#define AC_PRO_RAT_OK       0    /* 成功 */
#define AC_PRO_RAT_EPARSE  (-1)  /* 语法错误 / 空串 / 非法字符 / 缺 '/' */
#define AC_PRO_RAT_EDEG    (-2)  /* 次数超限（>8）或 apart 分母次数 < 1 */
#define AC_PRO_RAT_ECOEF   (-3)  /* 系数超域（|c| > 9999）或中间整数溢出 */
#define AC_PRO_RAT_EDOM    (-4)  /* apart 语义拒绝：假分式 / 重根 / 复根 / 回乘验证不过 */
#define AC_PRO_RAT_EOVER   (-5)  /* 输出缓冲不足 */

/* v2.3.0 功能 3（C 侧）：常系数线性齐次 ODE
 * 行为边界由 docs/contracts/ode-format-v23.md 逐字冻结（本节是该契约
 * 的镜像，ac_pro_ode.c 头注同；契约变更须 C/JS 双组确认后先改契约）。
 *
 *   未知函数固定 y、自变量固定 x（小写），阶数 n ∈ [1, 4]；整系数
 *   |a_k| ≤ 999；ASCII 输入 ≤ 256 字节。非齐次（右端非 "0"）/ 变系数
 *   （左端出现 x）/ 阶数 ≥ 5 / 恒等式 → 对应错误码，不硬解。
 *
 *   项 := [符号][系数][x 因子...]y{撇号 0..4}；系数可缺省为 1、`*` 可选、
 *   同阶项禁止重复、y 记号后只允许 空白/+/-/=/串尾；右端去空白后必须
 *   恰为单个 '0'。判定全序（§6）：
 *   EPARSE → EDEG（撇号计到 5 当场）→ EVARCOEF（x 标志延迟）→
 *   ENONHOMO → ECOEF → [求根渲染期] EOVER / EINTERNAL。
 *
 *   "y = " 前缀 + 逐项和，项 = C_i [*x^j] [*exp(fmt(λ或α)*x)]
 *   [*cos/sin(fmt(β)*x)]；唯二特例：j=0 省 x 因子、指数整判 0 时整个
 *   exp 因子省略。根序 = Re 降序、Re 相等（1e-9）Im 降序；共轭对先 cos
 *   后 sin、重数按 x 幂层展开；任意常数 C₁..C₄（U+2081..2084）按渲染
 *   顺序连续分配，总数 = n。fmt 三级：整判 1e-9 / 有理重建 q≤1000 /
 *   定点 12 位。
 *
 *   int ac_pro_ode(const char *src, char *out, int out_len);
 *   out 由调用方提供，契约冻结 out_len ≥ 256；不足 → EOVER 且不写部分
 *   内容。无根访问器、无解析态访问器（特征多项式/根数组是内部量）。
 *   返回 §6 错误码；失败时 out 内容未定义。 */
int ac_pro_ode(const char *src, char *out, int out_len);

#define AC_PRO_ODE_OK        0    /* 成功 */
#define AC_PRO_ODE_EPARSE   (-1)  /* 词法/结构错误（契约 §6） */
#define AC_PRO_ODE_EDEG     (-2)  /* 导数阶 ≥ 5 */
#define AC_PRO_ODE_EVARCOEF (-3)  /* 变系数：左端出现 x 记号 */
#define AC_PRO_ODE_ENONHOMO (-4)  /* 非齐次：右端非单个 '0' */
#define AC_PRO_ODE_ECOEF    (-5)  /* 系数域：|a_k|>999 / 全零 / 无导数项 */
#define AC_PRO_ODE_EOVER    (-6)  /* 输出缓冲不足 */
#define AC_PRO_ODE_EINTERNAL (-7) /* 聚类失败 / 求根失败 / fmt 越域（绝不硬凑） */

/* v2.4.0 功能 8:一般实矩阵特征值(schur)
 * Francis 隐式双移 QR(实现于 src/ac_pro_schur.c)。
 *
 *   n ≤ 12(AC_PRO_MATRIX_MAX_DIM)、行主序 n×n 稠密实矩阵,
 *   对称性不限 —— 非对称输入合法(与 A5 ac_pro_eig_all 的分界:
 *   A5 要求实对称、非对称返 -2;本功能就是为关闭该缺口而立项,
 *   ROADMAP.md:144。本文件的 -2 是「QR 迭代不收敛」,语义不同)。
 *   视图:Pro union 的 schur 槽(2744 B ≤ 3848 吸收线,.bss 零增长)。
 *
 *   幂 2 精确缩放(条目压进 [0.5,1],Francis 的 H² 项绝无溢出)→
 *   Householder 化 Hessenberg → Francis 隐式双移 QR(bulge chasing,
 *   全显式循环零递归)→ 1×1/2×2 deflation;2×2 走闭式(Vieta 稳定形)。
 *   每 10 步不 deflation 注入 EISPACK 同款异常位移,单特征值 50 步
 *   预算,超限返回 -2 —— 绝不返回未收敛的垃圾特征值。
 *   实特征向量:实特征值逐个做 M = A − λI 部分主元 LU + 两步反迭代,
 *   残差 ‖Av − λv‖∞ ≤ 1e-9·(1+‖A‖) 才接受,6 组(扰动 × 初值)全败
 *   返回 -3;复特征值不产出向量(v 列全零,vector(k) 返回 NULL,
 *   绝不硬凑复向量)。亏损(Jordan)特征值按几何重数重复给出同一条
 *   特征向量,不做正交化修饰。
 *
 *   键 = (|λ|, Re, Im) 降序的稳定排序;|λ| = sqrt(Re²+Im²) 按值比较,
 *   精确并列保持提取序 → 确定性。复共轭对相邻且 +Im 在前。
 *
 *   1  = 成功(特征值 + 实特征向量全部就绪,视图可读)
 *   -1 = 空指针 / n < 1 或 n > 12
 *   -2 = QR 迭代不收敛(单特征值 50 步预算耗尽)
 *   -3 = 实特征向量反迭代 6 组尝试全败(此时特征值已正确但一并放弃
 *        —— 失败时视图内容未定义,调用方不得读取)
 *   调用期间不得嵌套任何其它 Pro 视图 API;调用方矩阵 a 全程只读。 */
int            ac_pro_schur_all(const double *a, int n);
int            ac_pro_schur_count(void);
double         ac_pro_schur_re(int k);
double         ac_pro_schur_im(int k);
const double  *ac_pro_schur_vector(int k);

/* v2.4.0 功能 9(C 侧):square-free 分解与重根重数
 * 行为边界由 docs/contracts/sqfree-format-v24.md 逐字冻结(本节是该
 * 契约的镜像,ac_pro_sqfree.c 头注同;契约变更须先改契约再改码)。
 *
 *   单变量 x、整系数域(char 0):次数 ≤ 8、|c| ≤ 999、ASCII 输入
 *   ≤ 128 字节(契约 §3)。非零常数 → 成功、空列表 "[]";全零多项式
 *   → ECOEF(契约 §4.4,刻意偏离 ODE 契约)。非整系数( '/' '.' 等)
 *   在文法层 EPARSE;次数 n ≥ 9 词法层当场 EDEG;JS 侧整题 ≥ 5 拒绝
 *   是契约 §5.1 成文的双侧能力边界,C 侧 5..8 走 DK。
 *
 *   "[根@e|d重数, ...]"(根 = fmt 数值串;e = 精确根式管线 / d = DK,
 *   按无重因子次数路由:≤4 → e、5..8 → d);排序 Re 降序、并列(1e-9)
 *   Im 降序稳定插入;复根 [re]("+"/"-")[|im|]"i",re≈0 省略、|im|≈1
 *   只写 i;重根只出一次,重数由整系数 GCD 的 SFF 层指标精确决定,
 *   浮点永不参与合并(契约 §4.3)。
 *
 *   int ac_pro_sqfree(const char *src, char *out, int out_len);
 *   out 由调用方提供,契约冻结 out_len ≥ 512(§6.1 最坏估算 344 B);
 *   不足容纳实际输出 → EOVER 且不写部分内容。返回 §7 错误码;失败时
 *   out 内容未定义。无根访问器、无 SFF 中间态访问器。视图:仅解析期
 *   持 rat 槽,整系数代数与渲染全在栈上(deg 5..8 的 numroots 写 nr
 *   视图不冲突);.bss 零增长。 */
int ac_pro_sqfree(const char *src, char *out, int out_len);

#define AC_PRO_SQF_OK        0    /* 成功(out 已写入,含 "[]") */
#define AC_PRO_SQF_EPARSE   (-1)  /* 词法/结构错误(契约 §3/§7) */
#define AC_PRO_SQF_EDEG     (-2)  /* 次数域:n ≥ 9 当场 / 整题 > 8(JS 侧 ≥5) */
#define AC_PRO_SQF_ECOEF    (-3)  /* |c| > 999 / 全零多项式 / SFF 中间溢出 */
#define AC_PRO_SQF_EOVER    (-4)  /* 输出缓冲不足(JS 侧不产生) */
#define AC_PRO_SQF_EINTERNAL (-5) /* 整除余式非零 / 求根失败 / 共轭残缺 /
                                    账本不平 / fmt 越域(绝不硬凑) */


/* v2.1.0 方向④：稀疏线性方程组（方案 a）
 *
 *   n ≤ 32（AC_PRO_SP_MAX_N）、非零元 nnz ≤ 320（AC_PRO_SP_MAX_NNZ）。
 *   实现于 src/ac_pro_sparse.c；静态视图挂在 Pro union 的 sparse 成员
 *   （3848 B，v2.1.0 起的峰值）。见
 *   设计说明 §3 功能④ 方案 a。
 *
 *   调用方提供三元组（坐标格式的行号/列号/数值三个平行数组，顺序
 *   任意，允许乱序与同坐标重复 —— 重复项求和合并，精确 0 丢弃）与右端
 *   向量；求解器内部完成 CSR 压缩、阈值主元消元（τ = 0.1，候选中取行
 *   非零元最少者，数值上主元 ≥ τ·列最大模 且 ≥ 主元下限）与回代。
 *   全程显式循环：无递归、无 malloc、无 VLA、无静态对象（Zero-GC）。
 *
 *   1  = 成功（AC_PRO_SP_OK，x/perm/rhs 视图可读）
 *   -1 = AC_PRO_SP_EN      n 超限（n < 1 或 n > 32）
 *   -2 = AC_PRO_SP_ENNZ    nnz 超限（nnz < 1 或 nnz > 320）
 *   -3 = AC_PRO_SP_EPIVOT  主元低于下限（某步列最大模 ∈ (0, 1e-12)）
 *   -4 = AC_PRO_SP_ESING   结构奇异（某步列在待消行中无非零元）
 *   -5 = AC_PRO_SP_EFILL   消元填充溢出（总非零元将超过 320 —— 320 为
 *                           输入与填充共享的总容量，绝不静默丢填充）
 *   -6 = AC_PRO_SP_EINDEX  三元组非法（下标越界或指针为空）
 *   为什么错误码走返回值而不是静态「上一次错误」：一个 static 就让 TU
 *   净增 32 B .bss（见下方 eig 节的实测论证），直接破坏 42608/42640 门禁。
 *   失败时视图内容未定义，调用方不得读取。
 *
 *   ac_pro_sparse_x()      解向量（n 项，按变量序 —— 行交换不换未知量）
 *   ac_pro_sparse_perm()   行交换记录：perm[i] = 位置 i 上的原始行号
 *                          （int16_t，与 A4 的 lu.piv 同语义）
 *   ac_pro_sparse_rhs()    消元后的增广右端（上三角系统的 c 向量）
 *   三者都返回 sparse 视图内部指针，跨 Pro 视图 API 调用后失效。
 *   调用期间不得嵌套任何其它 Pro 视图 API（视图持有纪律）。 */
#define AC_PRO_SP_OK       1
#define AC_PRO_SP_EN       (-1)   /* n 超限 */
#define AC_PRO_SP_ENNZ     (-2)   /* nnz 超限 */
#define AC_PRO_SP_EPIVOT   (-3)   /* 主元低于下限 */
#define AC_PRO_SP_ESING    (-4)   /* 结构奇异 */
#define AC_PRO_SP_EFILL    (-5)   /* 消元填充溢出（nnz 总容量超限） */
#define AC_PRO_SP_EINDEX   (-6)   /* 三元组下标/指针非法 */

int ac_pro_sparse_solve(int n, int nnz,
                        const int32_t *tri_r, const int32_t *tri_c,
                        const double *tri_v, const double *rhs);
const double  *ac_pro_sparse_x(void);
const double  *ac_pro_sparse_rhs(void);
const int16_t *ac_pro_sparse_perm(void);

/* v2.1.0 骨架探针的归宿（方向④/⑤，tools/scaffold_v2_1_0.py 生成）
 *
 *   方向④「稀疏线性方程组」与方向⑤「任意精度定点十进制」的真实 API
 *   均已落地，并按骨架文件的既定计划整体取代了各自的骨架探针：
 *     - ac_pro_sparse_probe / AC_PRO_SPARSE_ENIMPL 已移除，由本文件
 *       上方方向④ 的真实 API（ac_pro_sparse_solve 等）取代；
 *     - ac_pro_bigdec_probe / AC_PRO_BIGDEC_ENIMPL 已移除，由下方
 *       方向⑤ 的真实 API（ac_pro_dec_* 等）取代。
 *   冒烟测试（test/test_pro_sparse.c、test/test_pro_bigdec.c）随之改为
 *   对真实 API 的校验。见设计说明。
 *
 *   与本文件上方 eig 节的论证一致：任何静态对象都会让所在 TU 净增
 *   32 B .bss（16 B 段 + 16 B 链接对齐），直接破坏 42608/42640 门禁。 */

/* v2.1.0 方向⑤：任意精度定点十进制（dec）
 *
 *   value = m · 10^(−scale)：m 是上游 ac_bigint_t（基数 1e9、20 limb、
 *   180 位十进制容量），scale 是 int32 十进制标度（±2^31）。dec 视图
 *   5 × 96 B = 480 B 挂在 ac_pro_scratch.h 的 Pro union 里，低于现峰值
 *   sparse（3848 B，对 mv 2688 亦然）→ .bss 维持 42608 零增长
 *   （设计说明的估算兑现）。
 *
 *   加减先把两操作数对齐到 max(scale)（对齐溢出 = EOVER，绝不截断）；
 *   乘法走上游 alias-safe ac_bi_mul（容量 = 尾数位数之和 ≤ 180）；
 *   除法走「divmod 余数扩标长除」：N = m_a·10^e（e = 180 − 位数(m_a)，
 *   N 恰好占满 180 位），Q/R = divmod(N, m_b)，结果 = Q·10^(−(e+s_a−s_b))，
 *   R ≠ 0 为截断近似（返回 AC_PRO_DEC_INEXACT）；Q == 0 且 R ≠ 0 时
 *   连 1 位有效数字都凑不出 → EOVER 熔断。乘法尾数位数之和 > 180、
 *   解析超过 180 有效位同样熔断（AC_PRO_DEC_EDIGITS / EOVER）。
 *
 *   只调用上游公开的 ac_bi_add/sub/mul/divmod/cmp/from_int64/to_string/
 *   zero/copy；dec 的值全部存放在 Pro dec 视图与栈上 ac_bigint_t，
 *   不触碰上游 g_bi_a/b/tmp/work1/work2 池，也不调 factorial/fibonacci，
 *   与上游 g_bi_* 草稿契约（work1/work2 的遗留内容约定）物理无关。
 *   零递归、零 malloc、零 VLA、零 static 对象（错误码走返回值）。
 *
 *   解析接受 [+-]?digits[.digits]（".5"/"5." 也合法），无指数记法、
 *   无空白、无千分位；多余字符一律 EPARSE。前导零不计有效位，
 *   小数尾部零按标度剥离（"1.50" 解析为 15·10^−1）。输出一律规范形：
 *   无尾随小数零、零无符号、整数无小数点、scale<0 时补整数百零。
 *
 *   AC_PRO_DEC_OK(1)      = 成功且精确（结果已写入 out）
 *   AC_PRO_DEC_INEXACT(2) = 成功但除法在位数上限处截断（结果已写入）
 *   负值                  = 错误（out 不被改写）：
 *     AC_PRO_DEC_EPARSE   语法错误
 *     AC_PRO_DEC_EDIGITS  解析超 180 有效位（位数上限熔断）
 *     AC_PRO_DEC_EDIV0    除数为零
 *     AC_PRO_DEC_EOVER    对齐/乘积/标度超出容量（精确值无法在
 *                         180 位尾数 + int32 标度内表示，绝不返回错值）
 *     AC_PRO_DEC_EBUF     out/out_len 不足（需 = 渲染长度 + 1）
 *     AC_PRO_DEC_ESLOT    槽号越界（仅 narrow 接口）
 *   错误码走返回值而非静态变量：任何静态对象 = .bss +32 B（见上）。 */
#define AC_PRO_DEC_MAX_DIGITS 180      /* 20 limb × 9 = 180 位十进制容量 */
#define AC_PRO_DEC_SLOTS      5        /* dec 视图槽位（ac_pro_scratch.h） */

#define AC_PRO_DEC_OK       1
#define AC_PRO_DEC_INEXACT  2
#define AC_PRO_DEC_EPARSE   (-1010)
#define AC_PRO_DEC_EDIGITS  (-1011)
#define AC_PRO_DEC_EDIV0    (-1012)
#define AC_PRO_DEC_EOVER    (-1013)
#define AC_PRO_DEC_EBUF     (-1014)
#define AC_PRO_DEC_ESLOT    (-1015)

/* 窄接口：字符串 ↔ dec 槽（0..AC_PRO_DEC_SLOTS-1）
 * parse 把十进制字符串解析并规范化进指定槽；render 把槽内容渲染成
 * 规范形十进制字符串。供字符串往返与单独检查使用；四则入口内部
 * 走同一对函数（槽 0 = A、槽 1 = B、槽 2 = 结果）。 */
int ac_pro_dec_parse(const char *src, int slot);
int ac_pro_dec_render(int slot, char *out, int out_len);

/* 四则入口：解析 a/b → 运算 → 渲染规范形到 out
 * out/out_len 由调用方提供；失败时 out 内容不被改写。 */
int ac_pro_dec_add(const char *a, const char *b, char *out, int out_len);
int ac_pro_dec_sub(const char *a, const char *b, char *out, int out_len);
int ac_pro_dec_mul(const char *a, const char *b, char *out, int out_len);
int ac_pro_dec_div(const char *a, const char *b, char *out, int out_len);

/* v2.2.0 方向⑥：2 变量 Groebner 基（M4GB 逐对归约）
 *
 *   变量 = 2（x > y）、单项式序 = lex、输入多项式总次数 ≤ 4
 *   （AC_PRO_GB_IN_DEG）、基 ≤ 8 槽、基元素 ≤ 16 项、视图内一切多项式
 *   总次数 ≤ 8（AC_PRO_GB_MAX_DEG）。整系数（int64），全部系数 + − ×
 *   走上游 ac_safe_add/sub/mul，溢出熔断，绝不返回错值。
 *   实现于 src/ac_pro_groebner.c；静态视图挂在 Pro union 的 gb 成员
 *   （1888 B ≤ 任务书预算 1960 B，< sparse 峰值 3848 → union 完全吸收，
 *   .bss 维持 42608/42640 零增长）。
 *
 * 算法（Makarim & Stevens, M4GB, ISSAC 2017 —— 设计说明
 *   文献 P5 的「逐对处理
 *   critical pair、避免 F4 大矩阵」路线，全部显式循环）
 *   ① 解析两生成元进 gb 视图的槽 0/1（自备解析器，语法与 A1/A2 多变量层
 *      完全一致；不借道 mv 视图 —— 一次调用只持有一个视图）。
 *   ② critical pair 队列 = 当前活跃基的全对（每次基变更后整体重建，
 *      保证「终结集的每一对都在终结版本上处理过」）。逐对弹出：
 *      S-多项式 = lc_j·(lcm/lt_i)·f_i − lc_i·(lcm/lt_j)·f_j 在稠密工作区
 *      合成，随后做稠密伪除归约：lex 降序扫 81 格，最大可除单项式
 *      m 被 lc_g·(−)·(m/lt_g)·g 消去（每步后提整数 content 控制膨胀，
 *      与 A1 prem 的 primitive 归一同一纪律），扫完即余式。
 *   ③ 余式非零 → 规范化（primitive + 首项系数为正）→ 入基；余式为
 *      非零常数 → 理想 = 全环，基坍缩为 {1}。
 *   ④ 每次入基后跑一遍 interreduce：每个活跃元素用「被其余元素归约的
 *      余式」替换（该替换无条件保理想，合法性不依赖 G 已是 GB ——
 *      运行中途删除冗余元素则不然，实测 node 对照锚定了这一点），
 *      余式为零的元素判死、槽位回收。
 *   ⑤ 队列空 → 终结 interreduce 已在 ④ 中完成，按首项 lex 降序输出。
 *      终结集是 reduced GB（首项互不整除 + 尾项互不整除 + primitive
 *      正首项系数），由 reduced GB 在固定单项式序下的唯一性保证与
 *      任何正确实现逐字一致。
 *
 *   基元素按首项 lex 降序排列，元素间用 " | " 连接；每元素与 A1 的
 *   归一化格式一致（"x^2 - y"：系数 1 省略、指数 1 省略、零多项式 "0"）。
 *   例：<x^2+y^2-1, x^2-y> → "x^2 - y | y^2 + y - 1"。
 *   空基（两输入皆 0）输出 "0"；理想 = 全环输出 "1"。
 *
 *   1  = AC_PRO_GB_OK        成功（out 已写入，视图可读）
 *   -1 = AC_PRO_GB_EARG      空指针 / 语法错 / out_len < 2
 *   -2 = AC_PRO_GB_EDEG      输入总次数 > 4，或中间单项式总次数 > 8
 *   -3 = AC_PRO_GB_ETERM     基元素项数超 16（膨胀熔断）
 *   -4 = AC_PRO_GB_ESLOT     基槽位超 8（膨胀熔断）
 *   -5 = AC_PRO_GB_EPAIR     pair 队列超 32（膨胀熔断）
 *   -6 = AC_PRO_GB_EOVER     系数溢出（ac_safe_* 失败）
 *   -7 = AC_PRO_GB_EBUF      out 缓冲不足以容纳完整基
 *   -8 = AC_PRO_GB_EINTERNAL 内部不变式被破坏（防御性，不应出现）
 *   任何失败都不写 out、视图内容视为未定义 —— 超限一律熔断，
 *   绝不静默输出不完整基。为什么错误码走返回值而不是静态「上一次
 *   错误」：一个 static 就让 TU 净增 32 B .bss（见上方 eig 节的实测
 *   论证），直接破坏 42608/42640 门禁。
 *
 *   ac_pro_gb_count()          基元素个数（0 = 空基）
 *   ac_pro_gb_terms(k)         第 k 个元素的项数
 *   ac_pro_gb_coef(k, t)       第 k 个元素第 t 项的系数（项按首项 lex 降序）
 *   ac_pro_gb_exps(k, t)       第 k 个元素第 t 项的打包指数 (ex<<4)|ey，
 *                              即 x 指数 × 16 + y 指数；越界返回 -1
 *   全部返回 gb 视图数据：成功调用后、下一次任何 Pro 视图 API 之前
 *   有效；调用期间不得嵌套任何其它 Pro 视图 API。 */
#define AC_PRO_GB_OK        1
#define AC_PRO_GB_EARG   (-1)
#define AC_PRO_GB_EDEG   (-2)
#define AC_PRO_GB_ETERM  (-3)
#define AC_PRO_GB_ESLOT  (-4)
#define AC_PRO_GB_EPAIR  (-5)
#define AC_PRO_GB_EOVER  (-6)
#define AC_PRO_GB_EBUF   (-7)
#define AC_PRO_GB_EINTERNAL (-8)

int      ac_pro_groebner(const char *fsrc, const char *gsrc, char *out, int out_len);
int      ac_pro_gb_count(void);
int      ac_pro_gb_terms(int k);
ac_int_t ac_pro_gb_coef(int k, int t);
int      ac_pro_gb_exps(int k, int t);

/* ==== v2.5 ANCHOR: CX (feat/math-ext) ==== */

/* ==== END ANCHOR CX ==== */

/* ==== v2.5 ANCHOR: SYSOLVE (feat/solvers) ==== */

/* ==== END ANCHOR SYSOLVE ==== */

/* ==== v2.5 ANCHOR: STURM (feat/solvers) ==== */

/* ==== END ANCHOR STURM ==== */

#endif /* AC_PRO_H */
