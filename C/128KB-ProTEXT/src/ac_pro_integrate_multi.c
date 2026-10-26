/* ============================================================
 * ApolloCalc-CAS · C/128KB-ProTEXT/src/ac_pro_integrate_multi.c
 * ------------------------------------------------------------
 * 【职责 / Responsibility】
 *   多变量多项式积分（逐项、精确整除）：对 2 变量稀疏多项式
 *   f(x,y) = Σ c_i · x^(a_i) · y^(b_i) 按指定变量做形式积分，
 *
 *       ∫ c·x^a·y^b dx = (c/(a+1)) · x^(a+1) · y^b
 *       ∫ c·x^a·y^b dy = (c/(b+1)) · x^a · y^(b+1)
 *
 *   并把结果按 mv_format 同款约定格式化到调用方缓冲。
 *
 * 【为什么只做整系数 / Why integer coefficients only —— 边界】
 *   ac_pro_term_t 只有系数字段 c（int64），**没有分母字段**。因此
 *   c % (divisor) != 0 的项（如 ∫x·y^2 dx = x^2·y^2/2）无法在现有
 *   项记录里表示，按 AC_PRO_MVI_ENONEXACT 拒绝，绝不返回近似值、
 *   绝不截断。若未来要支持分数系数，需要扩充项记录（例如复用 pad
 *   字段存分母，或把项记录扩成 24 B）—— 那会连带改动
 *   ac_pro_scratch.h 的 16 B sizeof 校验与 2688 B 视图账本，属于
 *   另一次有预算审批的变更，本文件刻意不预支。
 *
 * 【Zero-GC / 零 .bss 增量】
 *   本文件**没有任何静态存储对象**（连一个 static int 都没有），
 *   无 malloc、无 VLA、无递归 —— 积分主循环是 for 迭代，格式化是
 *   双层顺序追加。因此本 TU 不产生 .bss 段，全程序 .bss 增量为 0。
 *
 *   任务卡曾为本功能预留了一个 frac 视图（480 B）用于分数系数；
 *   实测**连它都不需要**：整除约束把所有不可整除的项挡在门外后，
 *   中间状态只有「槽 0 的输入项表 + 槽 1 的输出项表」，全部落在
 *   既有的 mv 视图（2688 B，峰值视图）里。ac_pro_scratch.h 一行
 *   未改，.bss 增量为 0，优于 480 B 预算。
 *
 * 【槽位使用 / Slot discipline】
 *   只用 mv 视图的槽 0 与槽 1：
 *       槽 0 = ac_pro_multi_parse_into(src, 0) 的解析结果（已归一化）
 *       槽 1 = 逐项积分的输出项表（本函数直接写 s_pro_mv_slot(1)）
 *   不申请新视图、不改 ac_pro_scratch.h。读写分属两个槽，无别名。
 *
 * 【免归一化论证 / Why no normalize pass on the result】
 *   输入项表由 mv_normalize 保证：exps 严格降序、无重复、无零项。
 *   积分映射 (a,b) -> (a+1,b) 或 (a,b+1) 是单射且保序（次数门禁
 *   保证 b+1 ≤ 6，不会从 y 字段进位到 x 字段），且 c ≠ 0 且整除
 *   时新系数 c/d ≠ 0。故输出项表自动满足三条不变式，无需再排序。
 *
 * 【超越函数的映射 / Transcendental inputs】
 *   mv 解析器是**纯多项式**解析器（ac_pro.h「多项式字符串语法」
 *   一节明言不支持括号/除法/负指数）：sin、exp 等记号在解析期就是
 *   语法错误 —— mv_parse_term 读到 's' 既非数字也非变量即失败。
 *   所以「遇超越函数返回 ETABLE」（单变量 B2 的行为）在本层**自然
 *   表现为解析期拒绝**：返回 -1 且 g_solve_error = 2。这是同一
 *   语义族在纯多项式层上的诚实表现，不是造假路径。
 * ============================================================ */

#include <stdio.h>    /* snprintf */
#include <stdint.h>   /* INT64_MIN / INT64_MAX */

#include "ac_pro.h"

#if AC_PRO_ACTIVE

/* ==================== 错误码 ====================
 * 与 ac_pro_multi.c 的 PRO_MV_ERR_OVERFLOW 同值同义：5 = 多变量系数
 * 溢出，Pro 侧独占，不与上游 0..4 冲突。 */
#define MVI_ERR_OVERFLOW 5

/* ==================== exps 编码 / 解码 ====================
 * 从 ac_pro_multi.c 抄写（那边是 static，跨 TU 不可链接）：
 * 低 4 bit = y 指数，高 4 bit = x 指数。一次整数比较即可排序与判等。 */
static int32_t mvi_exps(int xdeg, int ydeg) {
    return (int32_t)(((xdeg & AC_PRO_MV_EXP_MASK) << AC_PRO_MV_EXP_BITS) |
                     (ydeg & AC_PRO_MV_EXP_MASK));
}
static int mvi_xdeg(int32_t exps) {
    return (int)((exps >> AC_PRO_MV_EXP_BITS) & AC_PRO_MV_EXP_MASK);
}
static int mvi_ydeg(int32_t exps) {
    return (int)(exps & AC_PRO_MV_EXP_MASK);
}

/* ==================== 格式化（仿 mv_format） ====================
 * 规则与 ac_pro_multi.c 的 mv_format 逐条一致：
 *   ① 项按 exps 降序（免归一化论证保证输出自然有序）；
 *   ② 系数 ±1 且带变量时省略系数（写 "x" 而非 "1*x"）；
 *   ③ 指数 1 省略（写 "x" 而非 "x^1"）；指数 0 省略变量；
 *   ④ 项之间用 " + " / " - " 连接（负系数转成减号 + 绝对值）；
 *   ⑤ 零多项式写 "0"。
 * 与 mv_format 的唯一差别：入参是「项表指针 + 项数」而不是私有类型
 * mv_poly_t —— 本文件与 ac_pro_multi.c 之间只共享 ac_pro.h 的窄接口
 * 与槽号约定，不共享槽位纪律之外的任何类型。
 * 返回 0 = 成功；-1 = 缓冲不足 / INT64_MIN（不可到达的防御分支）。 */
static int mvi_format(const ac_pro_term_t *t, int count, char *out, int out_len) {
    int i;
    int used = 0;

    if (count == 0) {
        if (out_len < 2) { return -1; }
        out[0] = '0';
        out[1] = '\0';
        return 0;
    }

    for (i = 0; i < count; i++) {
        ac_int_t c = t[i].c;
        int xd = mvi_xdeg(t[i].exps);
        int yd = mvi_ydeg(t[i].exps);
        int neg = (c < 0);
        ac_int_t mag;
        int n;

        if (c == INT64_MIN) { return -1; }           /* 取绝对值会溢出 */
        mag = neg ? -c : c;

        /* 符号 */
        if (i == 0) {
            if (neg) {
                n = snprintf(out + used, (size_t)(out_len - used), "-");
                if (n < 0 || used + n >= out_len) { return -1; }
                used += n;
            }
        } else {
            n = snprintf(out + used, (size_t)(out_len - used), neg ? " - " : " + ");
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
        }

        /* 系数：只有在「无变量」或「系数绝对值 != 1」时才写 */
        if (xd == 0 && yd == 0) {
            n = snprintf(out + used, (size_t)(out_len - used), "%ld", (long)mag);
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
        } else if (mag != 1) {
            n = snprintf(out + used, (size_t)(out_len - used), "%ld", (long)mag);
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
            n = snprintf(out + used, (size_t)(out_len - used), "*");
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
        }

        /* 变量 */
        if (xd > 0) {
            if (xd == 1) { n = snprintf(out + used, (size_t)(out_len - used), "x"); }
            else { n = snprintf(out + used, (size_t)(out_len - used), "x^%d", xd); }
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
            if (yd > 0) {
                n = snprintf(out + used, (size_t)(out_len - used), "*");
                if (n < 0 || used + n >= out_len) { return -1; }
                used += n;
            }
        }
        if (yd > 0) {
            if (yd == 1) { n = snprintf(out + used, (size_t)(out_len - used), "y"); }
            else { n = snprintf(out + used, (size_t)(out_len - used), "y^%d", yd); }
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
        }
    }
    return 0;
}

/* ==================== 对外：多变量多项式积分 ====================
 * 契约（写进 ac_pro.h）：
 *   var_index：0 = 对 x 积分，1 = 对 y 积分，其它值 = 拒绝。
 *   逐项核：∫ c·x^a·y^b d(var) = (c/(指数+1)) · 指数+1 的单项式。
 *
 * 【错误通道选型 —— 二选一的取舍，在此写明】
 *   返回值 1 = 成功（out 已写入；∫0 = "0" 也是成功）；
 *   返回值 -1 = 一般错误，粗因看 g_solve_error（多变量族约定，
 *   与 ac_pro_multi_var_gcd 同表：1 次数 / 2 语法·参数·缓冲 / 5 溢出）；
 *   返回值 AC_PRO_MVI_ENONEXACT(-6) = 非精确整除 —— 这是**唯一**的
 *   细粒度负码，直接作为返回值（任务卡二选一中的「直接作为返回值」），
 *   同时 g_solve_error = 2 保持粗因通道一致。
 *
 *   为什么不用「专用 getter 查 -6」：那需要 static 保存「上一次错误」，
 *   实测（ac_pro.h 的 eig 注释、gcc 8.1.0 / MinGW-w64 ld）一个 static
 *   就让 TU 多出 16 B .bss 段并叠加 16 B 链接对齐，全程序 .bss 净涨
 *   32 B，直接破坏 41456 门禁。返回值通道零存储且更易测（不依赖调用
 *   顺序）。
 *
 * 【检查顺序 / Why degree before exactness】
 *   ∫x^6 dx 同时触犯两条（7 > 6 且 6 % 7 != 0），任务卡钉死它必须报
 *   g_solve_error = 1，故次数门禁先于整除检查。
 *
 * 【4-bit exps 域 / Why no separate exps-overflow check】
 *   「exps 写不进 4 bit」被次数门禁**蕴含**：结果总次数 ≤ 6 ⇒ 每个指
 *   数 ≤ 6 < 15（4-bit 上限）。∫x^15 dx 这类输入在门禁处即被拒
 *   （(15+1)+0 = 16 > 6），无需第二条检查。 */
int ac_pro_integrate_multi(const char *src, int var_index, char *out, int out_len) {
    const ac_pro_term_t *tin;
    ac_pro_term_t *tout;
    int n_in;
    int n_out;
    int i;

    g_solve_error = 0;

    if (var_index != 0 && var_index != 1) { g_solve_error = 2; return -1; }
    if (out == (char *)0 || out_len < 2) { g_solve_error = 2; return -1; }
    if (src == (const char *)0) { g_solve_error = 2; return -1; }

    /* ---- 解析进槽 0（窄接口，返回项数 ≥ 0 或 -1）----
     * 超越函数（sin 等）在这里就被拒：纯多项式解析器把它们当语法错。 */
    n_in = ac_pro_multi_parse_into(src, 0);
    if (n_in < 0) { g_solve_error = 2; return -1; }

    tin = ac_pro_multi_slot_terms(0);
    tout = s_pro_mv_slot(1);   /* 直接写槽 1：读写分属两槽，无别名 */
    n_out = 0;

    for (i = 0; i < n_in; i++) {
        ac_int_t c = tin[i].c;
        int a = mvi_xdeg(tin[i].exps);
        int b = mvi_ydeg(tin[i].exps);
        int na;
        int nb;
        int div;
        ac_int_t q;

        if (var_index == 0) { na = a + 1; nb = b; }
        else                { na = a;     nb = b + 1; }
        div = (var_index == 0) ? (a + 1) : (b + 1);

        /* ---- 次数门禁（先于整除，见上）----
         * 输入单项式指数可到 15（词法上限），语义上限在这里拦：
         * 结果总次数 > AC_PRO_MV_DEGREE(6) 一律拒绝，绝不截断。 */
        if (na + nb > AC_PRO_MV_DEGREE) { g_solve_error = 1; return -1; }

        /* ---- INT64_MIN：按溢出处理拒绝 ----
         * |INT64_MIN| = 2^63 无法用 int64 表示（格式化取绝对值也会溢出）。
         * 解析器数字累加封顶 INT64_MAX，今日不可达；核保持全函数防御。 */
        if (c == INT64_MIN) { g_solve_error = MVI_ERR_OVERFLOW; return -1; }

        /* ---- 精确整除约束 ----
         * c % div != 0 ⇒ 原函数系数非整（如 x^2*y^2/2），项记录无处放
         * 分母 —— 按 -6 拒绝（见 ac_pro.h 的通道选型注释）。 */
        if (c % div != 0) { g_solve_error = 2; return AC_PRO_MVI_ENONEXACT; }
        q = c / div;

        /* ---- 写槽 1 ----
         * 防御性容量检查：归一化输入 ≤ 28 个非零项且映射为 1:1，本分支
         * 不可达；保留它使写入无前提可依赖。 */
        if (n_out >= AC_PRO_MV_TERMS) { g_solve_error = MVI_ERR_OVERFLOW; return -1; }
        tout[n_out].c = q;
        tout[n_out].exps = mvi_exps(na, nb);
        tout[n_out].pad = 0;
        n_out++;
    }

    if (mvi_format(tout, n_out, out, out_len) != 0) {
        g_solve_error = 2;    /* 缓冲不足：按约定走 -1 / g_solve_error = 2 */
        return -1;
    }
    return 1;
}

#endif /* AC_PRO_ACTIVE */
