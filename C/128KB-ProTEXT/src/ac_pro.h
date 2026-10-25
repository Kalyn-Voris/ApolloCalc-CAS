/* ============================================================
 * ApolloCalc-CAS · C/128KB-ProTEXT/src/ac_pro.h
 * ------------------------------------------------------------
 * 【职责 / Responsibility】
 *   ProTEXT 层的**唯一**公开头文件。上游 `C/128KB/src/` 一行不改，
 *   Pro 的全部增量 API 都从这里对外。
 *
 * 【三层结构 / Three layers】
 *   ① 上游包装层（src/ac_pro_<mod>.c）
 *        只做 `#include "../../../C/128KB/src/ac_<mod>.c"`，把上游 16 个
 *        模块各编译一次。唯一例外是 ac_pro_solvers.c 里的**符号重命名**
 *        拦截（见下）。
 *   ② Pro 增量层（src/ac_pro_*_ext.c + ac_pro_extra_globals.c）
 *        新 TU，只调用上游公开 API 与上游公开全局，不 fork 上游源码。
 *   ③ 存储层（src/ac_pro_scratch.h）
 *        Pro 独立 union（模式 β），尺寸由编译期断言钉死。
 *
 * 【拦截方式 / Interception】
 *   `ac_guard_degree` 在上游 ac_solvers.c 内定义。Pro 不能改上游，也不
 *   能在链接期覆盖同名符号（会 multiple definition）。做法是在包装 TU
 *   里用**宏重命名**把上游那份改成 ac_pro_upstream_guard_degree，再由
 *   Pro 增量层提供同签名的 ac_guard_degree：
 *
 *       #if AC_PRO_ACTIVE
 *       #define ac_guard_degree ac_pro_upstream_guard_degree
 *       #endif
 *       #include "../../../C/128KB/src/ac_solvers.c"
 *
 *   宏在 ac_solvers.h 的声明处同样生效，故重命名是自洽的（声明与定义
 *   一起改名，不会出现「声明的是 A、定义的是 B」）。上游源码零改动，
 *   AC_PRO_ACTIVE=0 的退化构建里宏不生效，符号与上游逐位一致。
 *
 * 【节点 var 字段 / Node var tag —— 本项目最关键的一次「不改上游」】
 *   设计说明 §1.4 判据 2 发现 ac_sym_node_t 的偏移 12..15 是填充洞
 *   （type 1 + pad 3 + left 4 + right 4 = 12，value 对齐到 16）。
 *   直接给结构体加字段需要改上游头文件，而该头文件被 6 个上游 TU 包含，
 *   改了就会出现「Pro TU 认为 sizeof=24，上游 TU 仍认为 sizeof=24 但
 *   字段布局不同」的 ODR 式错配 —— 这正是任务书要求上报的陷阱。
 *
 *   本项目的解法：**完全不碰结构体定义**，用字节偏移 + memcpy 访问
 *   那块填充洞，并用三条编译期断言证明洞确实在 12..15：
 *       sizeof(ac_sym_node_t) == 24
 *       offsetof(ac_sym_node_t, value)  == 16
 *       offsetof(ac_sym_node_t, right)  ==  8
 *   12..15 未被任何上游代码读写（上游 sym_new 只清 pad[0..2]，即 1..3），
 *   所以这是一块**真正空闲且尺寸精确**的 4 字节。memcpy 经 unsigned char*
 *   是标准明确允许的访问方式，无严格别名问题、无对齐问题、无 UB。
 *   详见 ac_pro_symbolic_ext.c 的 pro_node_set_var / pro_node_get_var。
 * ============================================================ */

#ifndef AC_PRO_H
#define AC_PRO_H

#include "ac_pro_config.h"
#include "ac_pro_scratch.h"

#include "../../../C/128KB/src/ac_solvers.h"
#include "../../../C/128KB/src/ac_symbolic.h"
#include "../../../C/128KB/src/ac_matrix.h"

/* ==================== 阶段一①：内存水位门禁（修正上游死代码） ====================
 * 上游 ac_check_memory_watermark 的两个硬伤（设计说明 §6.3）：
 *   硬伤一：估算上限 17320 B，阈值 98304 B，比值 5.68×，任何输入都不触发；
 *   硬伤二：漏算 g_ac_scratch（8224）与 g_sym_nodes（12288），低估 27496 B。
 * Pro 侧改为两段式：编译期真实静态量 + 运行期动态量。
 *
 * 返回值与上游同约定：0 = 通过；1 = 拒绝（g_solve_error = 4）。
 * extra_bytes 是**测试注入用**的假池字节数（正常调用传 0），
 * 这样「门禁到底会不会触发」可以被真正测出来，而不是靠改全局变量。 */
double ac_pro_memory_used(double extra_bytes);
int    ac_pro_check_memory_watermark_ex(double extra_bytes);
int    ac_pro_check_memory_watermark(void);

/* ==================== 阶段二 B1：五次及以上数值求根 ====================
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

/* ==================== 阶段二 A4：LU 分解 ====================
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

/* ==================== 阶段二 A6：隐函数求导 ====================
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
 * 直接破坏 41456 的账本。详见 ac_pro_symbolic_ext.c 的文件头。 */
int ac_pro_implicit_derivative(const char *expr, double x, double y,
                               double *dydx);

/* 错误码常量（与 ac_pro_implicit_derivative 的返回值同源） */
#define AC_PRO_IMP_OK       0
#define AC_PRO_IMP_EPARSE   1
#define AC_PRO_IMP_EBUDGET  2
#define AC_PRO_IMP_EFLAT    3
#define AC_PRO_IMP_EEVAL    4

/* ==================== v1.3 方向 A：多变量代数结构 ====================
 *
 * 【共同约束】（设计说明 §6.4 的 preCheck 参数矩阵）
 *   变量数 = 2（x 与 y）
 *   总次数 ≤ 6（AC_PRO_MV_DEGREE）
 *   项数 ≤ 28/多项式（AC_PRO_MV_TERMS = C(8,2)）
 *   系数为 int64，所有 + − × 走上游 ac_safe_mul/add/sub；
 *   溢出置 g_solve_error = 5 并失败，**绝不返回错值**。
 *
 * 【多项式字符串语法】
 *   项用 + / - 分隔；每项形如 [系数][*]x[^n][*]y[^m]，顺序可换，
 *   变量可缺省（纯常数项），系数缺省为 1，指数缺省为 1。
 *   例："x^2*y + 2*x - 3"、"2*x^2 - 8"、"x*y - 6"
 *   不支持括号、除法、负指数 —— 多变量层只处理多项式。
 */

/* ---- A1：多变量 GCD（子结式 PRS）----
 * 结果归一化为「原始部分 + 首项系数为正」，例如
 *     gcd(2x²−2y², 4x²−4xy) -> "x - y"（而非 2x−2y 或 x−y 的常数倍）
 * 返回：1 = 成功（out 已写入）；0 = 平凡（两者皆零）；-1 = 非法/溢出。
 * g_solve_error：0 正常 / 1 次数超限 / 2 语法错 / 5 系数溢出。 */
int ac_pro_multi_var_gcd(const char *fsrc, const char *gsrc, char *out, int out_len);

/* ---- A2：多变量因式分解（内容提取 + 单变量因子）----
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

/* ---- A3：结式 Resultant（数值路径，Sylvester + ac_mat_det）----
 * 在 x = x0 处计算 Res_y(f(x0,y), g(x0,y))。
 * 阶数 = deg_y(f) + deg_y(g)，严格 ≤ 12（AC_MATRIX_MAX_DIM）。
 * 超过 12 直接拒绝（返回 0，*out 不被改写）—— **绝不截断**。
 *
 * 返回：1 = 成功（*out 已写入，可能是 0 —— 0 表示 f、g 有公共根，
 *      这是有意义的结果而非错误）；0 = 拒绝；-1 = 内部错误。
 * g_solve_error：0 正常 / 1 阶数超限 / 2 输入非法 / 3 数值失败。 */
int ac_pro_resultant(const char *fsrc, const char *gsrc, double x0, double *out);

/* 构造 Sylvester 矩阵但不取行列式（供测试检查矩阵本体）。
 * 返回阶数 n（0 = 失败）；矩阵本体用 ac_pro_resultant_sylvester() 读。 */
int            ac_pro_resultant_build(const char *fsrc, const char *gsrc, double x0);
const double  *ac_pro_resultant_sylvester(void);

/* ---- 窄接口：供 A3 解析并读回项表（A1 模块内部实现）----
 * 这两个接口刻意只认**槽号**而不暴露 mv_poly_t，避免槽位纪律泄漏到
 * 多个 TU。正常调用方不需要它们。 */
int                  ac_pro_multi_parse_into(const char *src, int slot);
const ac_pro_term_t *ac_pro_multi_slot_terms(int slot);
int                  ac_pro_multi_slot_degree(int slot);

/* ==================== v1.3.5 A5：实特征向量 + Hotelling 收缩 ====================
 *
 * 【算法 / Algorithm】
 *   ① 主特征对：对 A 做带位移的幂迭代（Rayleigh 商），得到 |λ_1| 最大的
 *      特征对 (λ_1, x_1)。
 *   ② 特征向量求解的**第二条路径**（也是测试对拍的依据）：解
 *      (A − λI)v = 0 的零空间 —— 把 A − λI 做行阶梯消元（复用
 *      ac_mat_rank 同款 mat_echelon 逻辑），自由变量取 1、回代出 v。
 *      幂迭代给出 x_1 后，用零空间解再求一次并比对（两者必须平行），
 *      这是「特征向量确实满足 (A−λI)v=0」的直接证据，而不是只验证
 *      「Av ≈ λv」。
 *   ③ Deflation：Hotelling 收缩 A ← A − λ_1 · x_1 · x_1ᵀ（x_1 已归一化），
 *      收缩后的矩阵把 λ_1 压成 0，其余特征值不变；再对收缩矩阵求主
 *      特征对，重复 n 轮即得全部 n 个特征对。
 *
 * 【约束 / Constraints】
 *   n 必须 1..12（AC_PRO_MATRIX_MAX_DIM）。矩阵**行主序**、n×n、稠密。
 *   要求 A 为**实对称**矩阵（Hotelling 收缩在非对称矩阵上不保谱，
 *   这是 A5 的适用范围声明，不是实现缺陷）；非对称输入返回 -2。
 *
 * 【视图 / View】eig（2592 B：V 144 + A 副本 144 + λ 12 + work 24）。
 *   **一次调用只持有 eig 视图**，调用期间不得嵌套任何其它 Pro 视图 API。
 *   调用方矩阵 `a` 在整个调用期间必须有效且只读（内部不写它）。
 *
 * 【结果读取 / Result accessors】
 *   特征值：ac_pro_eig_value(k)，按 |λ| 降序，k = 0..n−1。
 *   特征向量：ac_pro_eig_vector(k) 返回**列主序**第 k 列的起始指针，
 *            即 v[i] 位于 ac_pro_eig_vector(k)[i]，i = 0..n−1。
 *             两者都返回内部视图指针，跨 API 调用后失效。
 *
 * 【返回 / Return】
 *   1 = 成功（n 个特征对全部求出，视图已写入）；
 *   ≤ 0 = 失败，且**错误码就是返回值本身**：
 *        0  = 内部错误（不应出现）
 *       -1  = n 越界（必须 1..12）
 *       -2  = 非对称矩阵（A5 适用范围外）
 *       -3  = 幂迭代不收敛（max_iter 内未达容差）
 *       -4  = 零向量 / 退化为零矩阵（特征向量无法归一化）
 *   失败时视图内容未定义，调用方不得读取。
 *
 *   【为什么错误码走返回值而不是 ac_pro_eig_error() / Why no error getter】
 *   本仓库实测（gcc 8.1.0 / MinGW-w64 ld，见 ac_pro_symbolic_ext.c:97-111）：
 *   一个 `static int` 会让该 TU 产生 16 B 的 .bss 段，叠加 16 B 链接对齐，
 *   全程序 .bss 净涨 **32 B**（41456 → 41488），直接破坏任务书的门禁。
 *   错误码作为返回值不需要任何静态存储，且更易测（不依赖调用顺序）。
 *   这是判据 2 的直接推论：「不在 union 里的静态量按 32 B 计，不是 4 B」。 */
int            ac_pro_eig_all(const double *a, int n);
double         ac_pro_eig_value(int k);
const double  *ac_pro_eig_vector(int k);
int            ac_pro_eig_count(void);

/* ==================== v1.3.5 B2：符号积分（表匹配 + 一层分部积分）====================
 *
 * 【算法 / Algorithm】
 *   ① 表匹配：64 项标准积分表（static const，落 .rdata，**不占 .bss**）。
 *      输入表达式先做线性替换归一化，再按「函数头 + 线性内层」的模式
 *      逐项匹配，命中即返回原函数。
 *   ② 分部积分：∫u dv = u·v − ∫v du，**只做一层**。当表匹配失败时，
 *      把被积函数拆成 u · dv 两部分，对 ∫v du 再试一次表匹配；仍失败
 *      则直接返回错误码（绝不递归、绝不无限展开）。
 *
 * 【节点预算 / Node budget —— 硬约束】
 *   复用上游 g_sym_nodes arena（AC_SYM_MAX_NODES = 512，全局共享）。
 *   进入前必须检查剩余预算：
 *       表匹配路径   ≤ 256 节点
 *       分部积分路径 ≤  96 节点（额外）
 *   预算不足时返回 AC_PRO_INT_EBUDGET，**绝不越界写 arena**。
 *   这条检查不是防御性编程的客套 —— g_sym_nodes 是上游 6 个 TU 共享的
 *   单一 arena，越界写会静默破坏其它模块的表达式树。
 *
 * 【输入语法 / Input syntax】
 *   变量固定为 x。支持：整数/小正常量、x、+ − * / ^、括号、一元负号，
 *   以及 sin/cos/tan/exp/ln/log/sqrt/asin/acos/atan/sinh/cosh/tanh。
 *   输出写到调用方缓冲（out/out_len），不产生静态存储。
 *
 * 【返回 / Return】0 = 成功（out 已写入）；负值 = 错误码（见下）。
 *   失败时 out 内容未定义，调用方不得使用。 */
int ac_pro_integrate(const char *expr, char *out, int out_len);

#define AC_PRO_INT_OK        0    /* 成功 */
#define AC_PRO_INT_EPARSE   (-1)  /* 语法错误 / 空输入 / 非法字符 */
#define AC_PRO_INT_ETABLE   (-2)  /* 64 项积分表未命中（且分部积分也未命中） */
#define AC_PRO_INT_EBUDGET  (-3)  /* g_sym_nodes 预算不足（表 256 / 分部 96） */
#define AC_PRO_INT_EOVER    (-4)  /* 输出缓冲不足 */
#define AC_PRO_INT_EDEGREE  (-5)  /* 次数/指数超限（如 x^1000） */

/* 积分表规模（供测试核对表项数与设计说明一致）。 */
#define AC_PRO_INT_TABLE_SIZE 64

#endif /* AC_PRO_H */
