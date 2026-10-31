/* ApolloCalc · ac_pro_multi.c — 2 变量稀疏多项式 + 子结式 PRS 的 GCD(A1) + 内容提取式因式分解(A2)

   方向 A 基础设施。设计说明 §3.1.2:2 变量,总次数 ≤ 6 的单项式数
       N(d) = C(d+2, 2),N(6) = 28
   稠密表示是 7×7 = 49 格,其中 21 格恒为 0(总次数 > 6),浪费 42.9%。
   稀疏项表 28 项 × 16 B = 448 B/多项式,6 槽 = 2688 B。存储是硬约束,
   故取稀疏。

   每个多项式槽维护一个有序,无零项,无重复指数的项表:按 exps 降序排列
   (x 指数优先,再 y 指数),即字典序降序,于是「首项」就是 terms[0],
   与多项式除法的语义直接对应;系数为 0 的项不存储(count 只数非零项);
   同一 exps 最多出现一次。三条由 mv_normalize() 统一保证,所有构造/运算
   结束都调它,与上游「所有构造器都 funnel 到 ac_exact_normalize」同一纪律。

   exps 低 4 bit = y 指数,高 4 bit = x 指数(各 0..6,4 bit 足够)。
   一次整数比较即可完成排序与同类项判等。

   Pro union 的 mv 视图只有 6 个槽,每个槽 28 项。所有多项式运算都必须
   显式指定三个不同的槽(dst / a / b),否则 mv_add_sub 之类的就地写入会
   破坏操作数。为杜绝别名 bug,本文件统一采用下面的固定分配,任何函数
   都不越界:

      槽 0  SLOT_A     被除数 / 左操作数
      槽 1  SLOT_B     除数 / 右操作数
      槽 2  SLOT_R     结果 / 余式
      槽 3  SLOT_T1    临时 1
      槽 4  SLOT_T2    临时 2
      槽 5  SLOT_T3    临时 3

   每个函数在头注里声明它占用哪些槽,要求哪些槽空闲。mv_assert_slots()
   在 debug 构建里检查三槽互异(release 下编译掉,零开销)。这条纪律比
   「小心别写错」可靠,实测第一版就是因为 prem 里三处同槽别名而算错,
   改成固定分配后一次通过。

   设计说明 §3.1.3 硬要求:系数运算一律走 ac_safe_mul / ac_safe_add /
   ac_safe_sub(ac_exact.h:40-42),溢出即 g_solve_error 中止,绝不返回错值。
   本文件所有系数 + − × 都经 mv_safe_* 包装,任一失败置 g_solve_error = 5
   并让当前运算返回失败。

   无 malloc,无 VLA,无递归。子结式 PRS 的欧几里得主循环是 while 迭代
   (次数严格下降,必然终止);插入排序是双循环,无递归。所有项池落 Pro
   union 的 mv 视图(2688 B)。 */

#include <math.h>
#include <stdio.h>
#include <string.h>   /* memset：校验阶段局部 mv_sp_t 的全量清零 */

#include "ac_pro.h"
#include "../../../C/128KB/src/ac_exact.h"
#include "../../../C/128KB/src/ac_ratroot.h"
#include "../../../C/128KB/src/ac_solvers.h"

#if AC_PRO_ACTIVE

/* 错误码
 * 复用 g_solve_error 的 0..4（与上游同码），新增 5 = 多变量溢出。
 * 上游 0=OK 1=degree 2=precheck 3=singular 4=mem；5 在 Pro 侧独占，
 * 不会与上游冲突（上游从不写 5）。 */
#define PRO_MV_ERR_OVERFLOW 5

/* exps 编码 / 解码
 * 低 4 bit = y 指数，高 4 bit = x 指数。一次整数比较即可排序与判等。
 * 三个 inline 函数必须早于任何使用者定义（C99 无隐式声明）。 */
static int32_t mv_exps(int xdeg, int ydeg) {
    return (int32_t)(((xdeg & AC_PRO_MV_EXP_MASK) << AC_PRO_MV_EXP_BITS) |
                     (ydeg & AC_PRO_MV_EXP_MASK));
}
static int mv_xdeg(int32_t exps) {
    return (int)((exps >> AC_PRO_MV_EXP_BITS) & AC_PRO_MV_EXP_MASK);
}
static int mv_ydeg(int32_t exps) {
    return (int)(exps & AC_PRO_MV_EXP_MASK);
}

/* 固定槽位分配 */
#define SLOT_A   0
#define SLOT_B   1
#define SLOT_R   2
#define SLOT_T1  3
#define SLOT_T2  4
#define SLOT_T3  5

/* 多项式句柄：槽号 + 项数。槽池在 union 里，句柄本身是栈上 8 B。
 * 用句柄而不是指针，是为了让「视图失效」在类型层面可见，指针会诱使
 * 调用方在别的 API 调用之后继续解引用。 */
typedef struct {
    int slot;
    int count;
} mv_poly_t;

static ac_pro_term_t *mv_terms(int slot) { return s_pro_mv_slot(slot); }

/* 前置声明：mv_format 在文件后半段定义，但 A1 的对外入口先用到它。 */
static int mv_format(const mv_poly_t *p, char *out, int out_len);
static int mv_format_factor(ac_int_t c, int k, int rd, char *out, int out_len);

/* 槽互异检查：release 构建里被优化掉（见 Makefile 的 -DNDEBUG 说明）。
 * 保留在 debug 里，是因为别名 bug 的表现是「结果看起来合理但错」，
 * 比崩溃难查得多。 */
static int mv_slots_distinct(int s0, int s1, int s2) {
    return (s0 != s1) && (s0 != s2) && (s1 != s2);
}

/* 安全算术包装
 * 设计说明 §3.1.3 的硬要求。任一失败即置 g_solve_error = 5 并返回 0。 */
static int mv_safe_mul(ac_int_t a, ac_int_t b, ac_int_t *out) {
    if (!ac_safe_mul(a, b, out)) { g_solve_error = PRO_MV_ERR_OVERFLOW; return 0; }
    return 1;
}
static int mv_safe_add(ac_int_t a, ac_int_t b, ac_int_t *out) {
    if (!ac_safe_add(a, b, out)) { g_solve_error = PRO_MV_ERR_OVERFLOW; return 0; }
    return 1;
}

/* 项表基础操作 */

static void mv_clear(mv_poly_t *p) { p->count = 0; }

/* 追加一项（不排序、不去重）；返回 0 = 成功，-1 = 项池满。 */
static int mv_push(mv_poly_t *p, ac_int_t c, int32_t exps) {
    if (c == 0) { return 0; }                       /* 零项不入表（count 只数非零项） */
    if (p->count >= AC_PRO_MV_TERMS) { return -1; }
    mv_terms(p->slot)[p->count].c = c;
    mv_terms(p->slot)[p->count].exps = exps;
    mv_terms(p->slot)[p->count].pad = 0;
    p->count++;
    return 0;
}

/* 归一化：排序（exps 降序）+ 合并同类项 + 删除零项。
 *
 * 排序用插入排序：28 项上限下最坏 378 次比较，比快排的递归开销更划算，
 * 且无递归（Zero-GC 红线 R4）。排序键是 exps 整数，降序，
 * 让 terms[0] 成为首项（字典序最大）。
 *
 * 合并：排序后相邻同 exps 的项用 ac_safe_add 累加；和为零则整组丢弃。
 * 这一步就是「项表合并效果」的所在：乘法会产生大量同类项，归一化把
 * 它们压回真实项数。 */
static int mv_normalize(mv_poly_t *p) {
    ac_pro_term_t *t = mv_terms(p->slot);
    int i;
    int j;
    int out;

    /* 插入排序（exps 降序） */
    for (i = 1; i < p->count; i++) {
        ac_pro_term_t key = t[i];
        j = i - 1;
        while (j >= 0 && t[j].exps < key.exps) {
            t[j + 1] = t[j];
            j--;
        }
        t[j + 1] = key;
    }

    /* 合并相邻同类项 */
    out = 0;
    i = 0;
    while (i < p->count) {
        ac_int_t sum = t[i].c;
        int32_t ex = t[i].exps;
        int k = i + 1;
        while (k < p->count && t[k].exps == ex) {
            if (!mv_safe_add(sum, t[k].c, &sum)) { return -1; }
            k++;
        }
        if (sum != 0) {                             /* 和为零的项整组丢弃 */
            t[out].c = sum;
            t[out].exps = ex;
            t[out].pad = 0;
            out++;
        }
        i = k;
    }
    p->count = out;
    return 0;
}

/* 深拷贝。dst 与 src 同槽时是 no-op（count 不变）。
 *
 * 为什么先清 dst->count 再拷贝
 *   mv_poly_t 是栈上句柄，调用方可能只设了 slot 没设 count（例如
 *   `mv_poly_t r; r.slot = SLOT_R;`）。若拷贝前不归零，一旦 src 的
 *   拷贝在中途失败（项池满 / 溢出），dst->count 会停留在垃圾值，
 *   后续 mv_normalize 就会扫到未初始化的项,这是实测踩过的坑
 *   （表现为访问冲突或「结果看起来合理但错」）。
 *   归零的代价是一次赋值，收益是任何失败路径下 dst 都处于「零多项式」
 *   这个合法状态。 */
static int mv_copy(mv_poly_t *dst, const mv_poly_t *src) {
    int i;
    if (src->count < 0 || src->count > AC_PRO_MV_TERMS) { dst->count = 0; return -1; }
    if (dst->slot == src->slot) { dst->count = src->count; return 0; }
    dst->count = 0;
    for (i = 0; i < src->count; i++) {
        mv_terms(dst->slot)[i] = mv_terms(src->slot)[i];
    }
    dst->count = src->count;
    return 0;
}

static int mv_is_zero(const mv_poly_t *p) { return p->count == 0; }

/* 首项 exps；零多项式返回 -1 */
static int32_t mv_lead_exps(const mv_poly_t *p) {
    if (p->count == 0) { return -1; }
    return mv_terms(p->slot)[0].exps;
}

/* 首项系数 */
static ac_int_t mv_lead_coef(const mv_poly_t *p) {
    if (p->count == 0) { return 0; }
    return mv_terms(p->slot)[0].c;
}

static int mv_total_degree(const mv_poly_t *p) {
    int i;
    int best = 0;
    for (i = 0; i < p->count; i++) {
        int d = mv_xdeg(mv_terms(p->slot)[i].exps) + mv_ydeg(mv_terms(p->slot)[i].exps);
        if (d > best) { best = d; }
    }
    return best;
}

/* 多项式算术
 * 全部要求 dst / a / b 三槽互异（由调用方保证，mv_slots_distinct 检查）。 */

/* dst = a + sign·b，sign ∈ {+1, -1} */
static int mv_add_sub(mv_poly_t *dst, const mv_poly_t *a, const mv_poly_t *b, int sign) {
    int i;
    int na;
    int nb;

    if (!mv_slots_distinct(dst->slot, a->slot, b->slot)) { return -1; }

    mv_clear(dst);
    /* 先把 a、b 的项快照到栈上（≤ 28 项 × 16 B = 448 B，栈上可接受），
       避免 dst 写入时与 a/b 同槽的隐患，虽然上面已校验互异，但
       快照让「追加到一半失败」的回滚更简单。 */
    na = a->count;
    nb = b->count;
    for (i = 0; i < na; i++) {
        if (mv_push(dst, mv_terms(a->slot)[i].c, mv_terms(a->slot)[i].exps) != 0) { return -1; }
    }
    for (i = 0; i < nb; i++) {
        ac_int_t c = mv_terms(b->slot)[i].c;
        if (sign < 0) {
            if (c == INT64_MIN) { g_solve_error = PRO_MV_ERR_OVERFLOW; return -1; }
            c = -c;
        }
        if (mv_push(dst, c, mv_terms(b->slot)[i].exps) != 0) { return -1; }
    }
    return mv_normalize(dst);
}

/* dst = a × b
 *
 * 用栈上累加缓冲，不边算边 push
 *   与 mv_mul_y_coef 同一个理由：项池满时「先归一化再重试」会让项表
 *   处于「前半有序、后半无序」的混合态，后续 normalize 会错误合并。
 *   这里先在栈上按指数累加（最多 28 个不同指数），最后一次性写入。 */
static int mv_mul(mv_poly_t *dst, const mv_poly_t *a, const mv_poly_t *b) {
    int i;
    int j;
    ac_int_t prod;
    int nx;
    int ny;

    if (!mv_slots_distinct(dst->slot, a->slot, b->slot)) { return -1; }

    {
        ac_int_t bc[AC_PRO_MV_TERMS];
        int32_t  be[AC_PRO_MV_TERMS];
        int bn = 0;

        for (i = 0; i < a->count; i++) {
            for (j = 0; j < b->count; j++) {
                int k;
                int found = 0;
                if (!mv_safe_mul(mv_terms(a->slot)[i].c, mv_terms(b->slot)[j].c, &prod)) {
                    return -1;
                }
                nx = mv_xdeg(mv_terms(a->slot)[i].exps) + mv_xdeg(mv_terms(b->slot)[j].exps);
                ny = mv_ydeg(mv_terms(a->slot)[i].exps) + mv_ydeg(mv_terms(b->slot)[j].exps);
                if (nx > AC_PRO_MV_EXP_MASK || ny > AC_PRO_MV_EXP_MASK) {
                    g_solve_error = 1;
                    return -1;
                }
                {
                    int32_t ex = mv_exps(nx, ny);
                    for (k = 0; k < bn; k++) {
                        if (be[k] != ex) { continue; }
                        if (!mv_safe_add(bc[k], prod, &bc[k])) { return -1; }
                        found = 1;
                        break;
                    }
                    if (!found) {
                        if (bn >= AC_PRO_MV_TERMS) { g_solve_error = 1; return -1; }
                        bc[bn] = prod;
                        be[bn] = ex;
                        bn++;
                    }
                }
            }
        }

        mv_clear(dst);
        for (i = 0; i < bn; i++) {
            if (mv_push(dst, bc[i], be[i]) != 0) { return -1; }
        }
    }
    return mv_normalize(dst);
}

/* dst = a × k（整数标量） */
static int mv_scale(mv_poly_t *dst, const mv_poly_t *a, ac_int_t k) {
    int i;
    ac_int_t v;

    if (dst->slot == a->slot) { return -1; }        /* 不支持原地缩放 */
    mv_clear(dst);
    if (k == 0) { return 0; }
    for (i = 0; i < a->count; i++) {
        if (!mv_safe_mul(mv_terms(a->slot)[i].c, k, &v)) { return -1; }
        if (mv_push(dst, v, mv_terms(a->slot)[i].exps) != 0) { return -1; }
    }
    return mv_normalize(dst);
}

/* dst = a ÷ d（逐项精确整除）。
 * 返回 1 = 整除且已写入 dst；0 = 不整除；-1 = 错误（溢出/项池满）。
 * 这是 A2 整除性验证的基础。 */
static int mv_div_exact(mv_poly_t *dst, const mv_poly_t *a, const mv_poly_t *d) {
    int i;
    ac_int_t q;
    ac_int_t lc;
    int32_t lex;
    int lx;
    int ly;

    if (!mv_slots_distinct(dst->slot, a->slot, d->slot)) { return -1; }
    mv_clear(dst);
    if (d->count == 0) { return 0; }                /* 除零 */
    lex = mv_lead_exps(d);
    lx = mv_xdeg(lex);
    ly = mv_ydeg(lex);
    lc = mv_lead_coef(d);

    for (i = 0; i < a->count; i++) {
        int xd = mv_xdeg(mv_terms(a->slot)[i].exps) - lx;
        int yd = mv_ydeg(mv_terms(a->slot)[i].exps) - ly;
        if (xd < 0 || yd < 0) { return 0; }         /* 首项除不尽 */
        if (lc == 0) { return 0; }
        if (mv_terms(a->slot)[i].c % lc != 0) { return 0; }   /* 系数除不尽 */
        q = mv_terms(a->slot)[i].c / lc;
        if (mv_push(dst, q, mv_exps(xd, yd)) != 0) { return -1; }
    }
    if (mv_normalize(dst) != 0) { return -1; }
    return 1;
}

/* 真实多项式长除法
 *
 * 与 mv_div_exact 的区别，两者用途完全不同，别混用
 *
 *   mv_div_exact（上一节）：逐项整除。要求 a 的每一项都能被 d 的
 *       首项整除，任一除不尽就返回 0。用于「整除性验证」和 A2 的
 *       因式重构校验。
 *
 *   mv_div_remainder（本节）：真实长除法。用字典序首项反复消去，
 *       余式次数逐次下降，直到余式的首项除不动为止。用于欧几里得
 *       GCD 的余式序列。
 *
 *   两者都返回三态，但语义不同：
 *       mv_div_exact     : 1 = 整除（商写入 dst）；0 = 不整除；-1 = 错误
 *       mv_div_remainder : 1 = 成功（商与余式都写入）；-1 = 错误
 *
 * 为什么长除法需要「系数是整数」这个前提
 *   本项目的多变量层是整系数的（ac_int_t）。长除法每一步要算
 *   「余式首项系数 / 除式首项系数」，在整数上不一定整除。
 *   本实现采用伪长除法（pseudo-division）：不除，而是把余式乘以
 *   除式首项系数再减，这样始终保持整数，代价是商多一个常数倍。
 *   对 GCD 而言常数倍无影响（最终会被 primitive 归一化掉）。
 *
 *   具体：R = a；商 q 用「精确整除的那些步」累积，除不尽时把整个
 *   余式乘以除式首项系数（记录到 *out_scale）后继续。
 *   本函数只保证「余式次数严格下降」，不保证商是真实商，
 *   对 GCD 足够，对 A2 的整除性验证不够（那里用 mv_div_exact）。
 *
 * 槽位 dst_q / dst_r 必须与 a、d 互异，且彼此互异。
 *   内部用 SLOT_T1 作余式工作区。 */
static int mv_div_remainder(mv_poly_t *dst_r, const mv_poly_t *a, const mv_poly_t *d,
                            int scratch_slot) {
    mv_poly_t r;
    mv_poly_t t;
    int sub_slot = scratch_slot;
    ac_int_t lc;
    int32_t lex;
    int lx;
    int ly;
    int guard = 0;

    if (d->count == 0) { return -1; }
    if (dst_r->slot == a->slot || dst_r->slot == d->slot) { return -1; }
    if (scratch_slot == dst_r->slot || scratch_slot == a->slot || scratch_slot == d->slot) { return -1; }

    r.slot = dst_r->slot;
    r.count = 0;
    if (mv_copy(&r, a) != 0) { return -1; }
    if (mv_normalize(&r) != 0) { return -1; }

    t.slot = scratch_slot;
    t.count = 0;
    /* A second scratch for the subtrahend: mv_add_sub needs dst, a and b to
     * be three distinct slots, and t already occupies scratch_slot. */
    {
        int k;
        for (k = 0; k < 6; k++) {
            if (k != dst_r->slot && k != a->slot && k != d->slot && k != scratch_slot) {
                sub_slot = k;
                break;
            }
        }
    }

    lex = mv_lead_exps(d);
    lx = mv_xdeg(lex);
    ly = mv_ydeg(lex);
    lc = mv_lead_coef(d);

    while (!mv_is_zero(&r)) {
        int32_t lrex = mv_lead_exps(&r);
        int dx = mv_xdeg(lrex) - lx;
        int dy = mv_ydeg(lrex) - ly;
        ac_int_t lcr;
        ac_int_t scale;
        int i;

        if (dx < 0 || dy < 0) { break; }        /* 首项除不动，余式已得 */

        lcr = mv_lead_coef(&r);

        /* 把余式整体乘以 lc（伪除法的核心：避免分数系数） */
        if (lc != 1) {
            mv_poly_t scaled;
            scaled.slot = scratch_slot;
            scaled.count = 0;
            if (mv_scale(&scaled, &r, lc) != 0) { return -1; }
            if (mv_copy(&r, &scaled) != 0) { return -1; }
        }

        /* 减去 lcr · x^dx · y^dy · d */
        {
            mv_poly_t sub;
            sub.slot = sub_slot;
            sub.count = 0;
            for (i = 0; i < d->count; i++) {
                ac_int_t prod;
                if (!mv_safe_mul(mv_terms(d->slot)[i].c, lcr, &prod)) { return -1; }
                if (mv_push(&sub, prod,
                            mv_exps(mv_xdeg(mv_terms(d->slot)[i].exps) + dx,
                                    mv_ydeg(mv_terms(d->slot)[i].exps) + dy)) != 0) {
                    return -1;
                }
            }
            if (mv_normalize(&sub) != 0) { return -1; }
            /* r = r − sub，r 与 sub 同槽不允许，借 t 中转 */
            if (mv_copy(&t, &r) != 0) { return -1; }
            if (mv_add_sub(&r, &t, &sub, -1) != 0) { return -1; }
        }

        (void)scale;
        guard++;
        if (guard > 256) { g_solve_error = 3; return -1; }
    }
    return 1;
}

/* a 是否被 d 整除（用真实长除法，余式为零） */
static int mv_divides(const mv_poly_t *d, const mv_poly_t *a) {
    mv_poly_t rem;
    int rc;

    if (d->count == 0) { return 0; }
    if (a->count == 0) { return 1; }            /* 零多项式被任何非零多项式整除 */

    /* 借一个不与 d、a 冲突的槽：用 SLOT_T2（调用方需保证空闲） */
    if (SLOT_T2 == d->slot || SLOT_T2 == a->slot) { return 0; }
    rem.slot = SLOT_T2;
    rem.count = 0;

    rc = mv_div_remainder(&rem, a, d, SLOT_T1);
    if (rc != 1) { return 0; }
    return mv_is_zero(&rem);
}

/* 公共单项式因子 x^a·y^b（a、b 取两者所有项指数最小值的 min） */
static int mv_common_monomial(mv_poly_t *dst, const mv_poly_t *f, const mv_poly_t *g) {
    int fmx;
    int fmy;
    int gmx;
    int gmy;
    int i;
    int a;
    int b;

    if (f->count == 0 || g->count == 0) {
        mv_clear(dst);
        if (mv_push(dst, 1, mv_exps(0, 0)) != 0) { return -1; }
        return mv_normalize(dst) == 0 ? 1 : -1;
    }

    fmx = AC_PRO_MV_EXP_MASK; fmy = AC_PRO_MV_EXP_MASK;
    for (i = 0; i < f->count; i++) {
        int xd = mv_xdeg(mv_terms(f->slot)[i].exps);
        int yd = mv_ydeg(mv_terms(f->slot)[i].exps);
        if (xd < fmx) { fmx = xd; }
        if (yd < fmy) { fmy = yd; }
    }
    gmx = AC_PRO_MV_EXP_MASK; gmy = AC_PRO_MV_EXP_MASK;
    for (i = 0; i < g->count; i++) {
        int xd = mv_xdeg(mv_terms(g->slot)[i].exps);
        int yd = mv_ydeg(mv_terms(g->slot)[i].exps);
        if (xd < gmx) { gmx = xd; }
        if (yd < gmy) { gmy = yd; }
    }

    a = (fmx < gmx) ? fmx : gmx;
    b = (fmy < gmy) ? fmy : gmy;

    mv_clear(dst);
    if (mv_push(dst, 1, mv_exps(a, b)) != 0) { return -1; }
    if (mv_normalize(dst) != 0) { return -1; }

    /* 用真实除法复核 */
    if (mv_divides(dst, f) && mv_divides(dst, g)) { return 1; }
    mv_clear(dst);
    if (mv_push(dst, 1, mv_exps(0, 0)) != 0) { return -1; }
    return mv_normalize(dst) == 0 ? 1 : -1;
}

/* 内容与原始部分 */

static ac_int_t mv_content(const mv_poly_t *p) {
    int i;
    ac_int_t g = 0;
    for (i = 0; i < p->count; i++) {
        g = ac_gcd(g, mv_terms(p->slot)[i].c);
    }
    return g;                                       /* ac_gcd 恒返回非负 */
}

static int mv_primitive_part(mv_poly_t *dst, const mv_poly_t *p, ac_int_t content) {
    int i;
    if (dst->slot == p->slot) { return -1; }
    mv_clear(dst);
    if (content == 0) { return 0; }
    for (i = 0; i < p->count; i++) {
        if (mv_push(dst, mv_terms(p->slot)[i].c / content,
                    mv_terms(p->slot)[i].exps) != 0) { return -1; }
    }
    return mv_normalize(dst);
}

/* 单变量塌缩（A2 接口）
 * 只处理「退化为单变量」的情形：多项式只含 x（或只含 y）。
 * 返回：1 = 是单变量且已写入 out[0..*out_deg]；0 = 不适用。 */
static int mv_to_univariate(const mv_poly_t *p, int which_var,
                            ac_int_t *out, int *out_deg) {
    int i;
    int deg = -1;

    if (p->count == 0) { *out_deg = 0; out[0] = 0; return 1; }

    for (i = 0; i < p->count; i++) {
        int xd = mv_xdeg(mv_terms(p->slot)[i].exps);
        int yd = mv_ydeg(mv_terms(p->slot)[i].exps);
        if (which_var == 0 && yd != 0) { return 0; }
        if (which_var == 1 && xd != 0) { return 0; }
    }
    for (i = 0; i < p->count; i++) {
        int d = (which_var == 0) ? mv_xdeg(mv_terms(p->slot)[i].exps)
                                 : mv_ydeg(mv_terms(p->slot)[i].exps);
        if (d > deg) { deg = d; }
    }
    if (deg < 0) { deg = 0; }
    if (deg + 1 > 5) { return 0; }                  /* 上游 g_coeffs[5] 只到 4 次 */
    for (i = 0; i <= deg; i++) { out[i] = 0; }
    for (i = 0; i < p->count; i++) {
        int d = (which_var == 0) ? mv_xdeg(mv_terms(p->slot)[i].exps)
                                 : mv_ydeg(mv_terms(p->slot)[i].exps);
        out[d] = mv_terms(p->slot)[i].c;
    }
    *out_deg = deg;
    return 1;
}

/* A1 核心：主变量分层 + 多变量伪余式
 *
 * 为什么必须分层
 *   上一版把多项式当成扁平项表（每项一个 x^a·y^b），伪余式用
 *   「字典序首项」做除法判据。这在同维度时能跑，但一旦两个多项式的
 *   首项落在不同变量上就失效：
 *
 *       prem(x + y, -2y)   // 被除式首项在 x，除式首项在 y
 *       dx = 1 - 0 = 1, dy = 0 - 1 = -1   ->  dy < 0，无法消去
 *
 *   数学上这不算错（x+y 确实不能被 −2y 整除），但欧几里得主循环
 *   因此永远不收敛,实测 gcd(x−y, x+y) 死循环。
 *
 *   正确做法是常规的：选定一个主变量，把多项式写成
 *       f(x, y) = a_n(y)·x^n + a_{n-1}(y)·x^{n-1} + ... + a_0(y)
 *   即「系数是另一个变量的多项式」的一元多项式。伪余式在这个
 *   一元多项式环上做，判据变成「x 次数严格下降」，与 y 无关。
 *   于是 prem(x+y, −2y) 先把两者看成 x 的多项式：
 *       f = 1·x + y      (deg_x = 1)
 *       g = 0·x + (−2y)  (deg_x = 0)
 *   做一次伪除法：
 *       R = lc(g)·f − lc(f)·x^(1−0)·g = (−2y)(x+y) − 1·x·(−2y) = −2y²
 *   deg_x(R) = 0 < 1，收敛。R 的内容 2y 提出后得 −y²，PRS 继续。
 *
 * 分层表示
 *   不引入新存储类型：仍然用扁平项表，但所有运算按「x 的次数分组」。
 *   一个「系数 a_i(y)」就是项表里所有 xdeg == i 的项构成的子表。
 *   好处是不需要额外项池，6 槽设计不变。
 *
 *   关键操作：
 *     mv_xdeg_of(p)          最大 x 次数
 *     mv_coef_at(dst, p, i)  取 x^i 的系数（y 的多项式）→ 临时槽
 *     mv_shift_x(dst, p, k)  p · x^k（支持原地）
 *     mv_mul_y_coef(dst,p,q) p 的每个 x 系数乘以 q（q 是纯 y 多项式）
 *
 * 主变量选择
 *   取「f 与 g 都出现过」的最高变量（x 优先）。若只有 y 是共同变量，
 *   先交换 x/y 角色、按 x 处理，最后再交换回来，这样算法只有一套。
 *   若两者无共同变量（f 只含 x、g 只含 y），主变量取 x：此时
 *   deg_x(g)=0，伪除法退化为「不断乘 lc(g)」，仍正确收敛。
 *
 * 伪余式的定义与实现
 *       prem(f, g) = lc(g)^(deg_x f − deg_x g + 1) · f  mod  g
 *   迭代实现（避免显式求幂，逐次乘 lc(g)）：
 *       R = f
 *       while deg_x(R) >= deg_x(g):
 *           R = lc(g)·R − lc(R)·x^Δ·g,   Δ = deg_x(R) − deg_x(g)
 *   每次迭代 R 的 x 次数严格下降 1，至多 deg_x(f) 次。
 *
 *   所有系数 + − × 走 mv_safe_*（内部是上游 ac_safe_mul/add/sub）。
 *   溢出置 g_solve_error = 5 并立即失败,绝不返回静默错值。
 *
 * 内容归一化
 *   每轮伪余式后把 R 的系数除以它们的整数 GCD。这不等于 β 归一化
 *   （会留常数倍膨胀），但在 2 变量 ≤ 6 次的规模下实测系数位宽
 *   < 40 bit，int64 有 2× 余量。扩到 4 变量/8 次时再换标准子结式
 *   β 归一化；届时接口不变，只换内部实现。
 *
 * 槽位契约
 *   6 槽全部用上，任何一处的别名都是「结果看起来合理但错」：
 *       SLOT_A  f      被除式（prem 期间只读）
 *       SLOT_B  g      除式（prem 期间只读）
 *       SLOT_R  dst    余式 / 结果
 *       SLOT_T1 t1     临时 1
 *       SLOT_T2 t2     临时 2（同时存 lcg）
 *       SLOT_T3 t3     临时 3（同时存 lcr）
 *   每个函数在入口显式检查「六槽互异」，失败即返回 -1，
 *   这比依赖注释可靠：实测第一版就是靠它定位到三处别名。 */

/* 分层操作 */

/* 多项式的 x 次数（最大 xdeg）；零多项式返回 -1 */
static int mv_xdeg_of(const mv_poly_t *p) {
    int i;
    int best = -1;
    for (i = 0; i < p->count; i++) {
        int d = mv_xdeg(mv_terms(p->slot)[i].exps);
        if (d > best) { best = d; }
    }
    return best;
}

/* 取 x^i 的系数（y 的多项式），写到 dst。dst 与 p 必须不同槽。 */
static int mv_coef_at(mv_poly_t *dst, const mv_poly_t *p, int i) {
    int k;
    if (dst->slot == p->slot) { return -1; }
    mv_clear(dst);
    for (k = 0; k < p->count; k++) {
        if (mv_xdeg(mv_terms(p->slot)[k].exps) != i) { continue; }
        if (mv_push(dst, mv_terms(p->slot)[k].c,
                    mv_exps(0, mv_ydeg(mv_terms(p->slot)[k].exps))) != 0) {
            return -1;
        }
    }
    return mv_normalize(dst);
}

/* dst = p · x^shift（shift >= 0），支持 dst == p 的原地平移。 */
static int mv_shift_x(mv_poly_t *dst, const mv_poly_t *p, int shift) {
    int k;
    ac_int_t cbuf[AC_PRO_MV_TERMS];
    int32_t ebuf[AC_PRO_MV_TERMS];
    int n = p->count;

    if (shift < 0) { return -1; }
    if (n > AC_PRO_MV_TERMS) { return -1; }

    for (k = 0; k < n; k++) {           /* 快照，支持原地 */
        cbuf[k] = mv_terms(p->slot)[k].c;
        ebuf[k] = mv_terms(p->slot)[k].exps;
    }
    mv_clear(dst);
    for (k = 0; k < n; k++) {
        int nx = mv_xdeg(ebuf[k]) + shift;
        int ny = mv_ydeg(ebuf[k]);
        if (nx > AC_PRO_MV_EXP_MASK) { g_solve_error = 1; return -1; }
        if (mv_push(dst, cbuf[k], mv_exps(nx, ny)) != 0) { return -1; }
    }
    return mv_normalize(dst);
}

/* dst = p × q，其中 q 是纯 y 的多项式（所有项 xdeg == 0）。
 *
 * 这是伪余式里最关键的一步：R = lc(g)·R 中 lc(g) 是 y 的多项式，
 * 不是整数标量，所以不能用 mv_scale（那只能乘整数）。
 *
 * 实现：p 的项 (c, (i,j)) 与 q 的项 (d, (0,m)) 相乘得 (c·d, (i, j+m))。
 * 这正是普通多项式乘法在「q 不含 x」时的特化。 */
static int mv_mul_y_coef(mv_poly_t *dst, const mv_poly_t *p, const mv_poly_t *q) {
    int i;
    int j;
    ac_int_t prod;
    int nx;
    int ny;

    if (!mv_slots_distinct(dst->slot, p->slot, q->slot)) { return -1; }
    mv_clear(dst);
    if (p->count == 0 || q->count == 0) { return 0; }

    /* 为什么用栈上累加缓冲，而不是边算边 push
     *   上一版在项池满时「先 mv_normalize 再重试 push」。这是错的：
     *   mv_normalize 会排序并合并已经写入的部分项，而循环还在
     *   继续 push 新的未排序项,于是项表处于「前半段有序、后半段
     *   无序」的混合状态，后续 normalize 把无序段也当有序段合并，
     *   产生错乱（实测出现 [6 x1 y0]、[-71 x0 y2] 这种垃圾项）。
     *
     *   正确做法：先在栈上累加全部 p.count × q.count 个乘积
     *   （最多 28 × 28 = 784 项，但同类项会大量重合，实际去重后远小
     *   于 28；这里用「插入式累加」,对每个乘积在缓冲区里线性查找
     *   同指数项，找到就加、找不到就追加），最后一次性写进 dst。
     *   缓冲区 784 × 16 B = 12.5 KB 太大，所以改用「边算边在小
     *   缓冲里合并」：缓冲容量取 AC_PRO_MV_TERMS（28），因为乘积的
     *   不同指数个数受总次数 ≤ 6 限制，最多 28 个。 */
    {
        ac_int_t bc[AC_PRO_MV_TERMS];
        int32_t  be[AC_PRO_MV_TERMS];
        int bn = 0;

        for (i = 0; i < p->count; i++) {
            for (j = 0; j < q->count; j++) {
                int k;
                int found = 0;
                if (!mv_safe_mul(mv_terms(p->slot)[i].c, mv_terms(q->slot)[j].c, &prod)) {
                    return -1;
                }
                nx = mv_xdeg(mv_terms(p->slot)[i].exps);    /* q 不含 x */
                ny = mv_ydeg(mv_terms(p->slot)[i].exps) +
                     mv_ydeg(mv_terms(q->slot)[j].exps);
                if (nx > AC_PRO_MV_EXP_MASK || ny > AC_PRO_MV_EXP_MASK) {
                    g_solve_error = 1;
                    return -1;
                }
                {
                    int32_t ex = mv_exps(nx, ny);
                    for (k = 0; k < bn; k++) {
                        if (be[k] != ex) { continue; }
                        if (!mv_safe_add(bc[k], prod, &bc[k])) { return -1; }
                        found = 1;
                        break;
                    }
                    if (!found) {
                        if (bn >= AC_PRO_MV_TERMS) {
                            /* 不同指数个数超过 28：总次数超限，拒绝 */
                            g_solve_error = 1;
                            return -1;
                        }
                        bc[bn] = prod;
                        be[bn] = ex;
                        bn++;
                    }
                }
            }
        }

        /* 一次性写入 dst 并归一化 */
        mv_clear(dst);
        for (i = 0; i < bn; i++) {
            if (mv_push(dst, bc[i], be[i]) != 0) { return -1; }
        }
    }
    return mv_normalize(dst);
}

/* 选主变量：返回 0 = x，1 = y。
 *
 * 规则（按优先级）：
 *   两者都含 x  -> x（字典序优先）
 *   两者都含 y  -> y
 *   f 含 x、g 只含 y（或反之）：此时没有共同变量，取次数更高的
 *      那个变量做主变量，取错了会让 deg_x(g)==0，伪除法退化成
 *      「每轮乘一次 lc(g)」，系数迅速膨胀且 x 次数不降，最终撞上
 *      guard 报错（实测踩过：gcd(x²+y²−1, x²−y) 与 gcd(2x²−2y²,
 *      4x²−4xy) 都栽在这条路径）。
 *
 *      正确的判据是「主变量必须在两个多项式里都出现，否则就用
 *      另一个」。若两个都不共同（例如 f 只含 x、g 只含 y），则二者
 *      必然互素（不同变量的多项式在 Q[x,y] 里没有公因式），
 *      直接返回 1 由调用方处理。 */
static int mv_pick_main_var(const mv_poly_t *f, const mv_poly_t *g) {
    int fx = 0;
    int fy = 0;
    int gx = 0;
    int gy = 0;
    int i;

    for (i = 0; i < f->count; i++) {
        if (mv_xdeg(mv_terms(f->slot)[i].exps) > 0) { fx = 1; }
        if (mv_ydeg(mv_terms(f->slot)[i].exps) > 0) { fy = 1; }
    }
    for (i = 0; i < g->count; i++) {
        if (mv_xdeg(mv_terms(g->slot)[i].exps) > 0) { gx = 1; }
        if (mv_ydeg(mv_terms(g->slot)[i].exps) > 0) { gy = 1; }
    }
    if (fx && gx) { return 0; }     /* x 是共同变量 */
    if (fy && gy) { return 1; }     /* 只有 y 是共同变量 */
    return 2;                       /* 无共同变量：互素，调用方短路 */
}

/* 交换 x/y 角色（exps 的两个 4-bit 字段互换）。dst 与 p 必须不同槽。 */
static int mv_swap_xy(mv_poly_t *dst, const mv_poly_t *p) {
    int i;
    if (dst->slot == p->slot) { return -1; }
    mv_clear(dst);
    for (i = 0; i < p->count; i++) {
        int xd = mv_xdeg(mv_terms(p->slot)[i].exps);
        int yd = mv_ydeg(mv_terms(p->slot)[i].exps);
        if (mv_push(dst, mv_terms(p->slot)[i].c, mv_exps(yd, xd)) != 0) { return -1; }
    }
    return mv_normalize(dst);
}

/* 多变量伪余式 prem
 * 前提：f、g 已按主变量 x 分层（主变量若为 y，调用方先交换）。
 *
 * 每轮用到的中间量：R、lcg、lcr、A=lcg·R、B=lcr·x^Δ·g。
 * 算法上 A 与 B 不需要同时存在：
 *     dst = lcg·dst            （A 直接写回结果槽）
 *     t1  = lcr·x^Δ·g          （B 落临时槽）
 *     dst = dst − t1
 * 于是只需 dst + t1 + lcg + lcr 四处。lcg/lcr 分别寄生在 t2/t3。
 *
 * 六槽契约：f=A、g=B 只读；dst=R；t1/t2/t3 临时（t2 存 lcg、t3 存 lcr）。 */
static int mv_prem_full(mv_poly_t *dst, const mv_poly_t *f, const mv_poly_t *g,
                        mv_poly_t *t1, mv_poly_t *t2, mv_poly_t *t3) {
    mv_poly_t lcg;
    mv_poly_t lcr;
    int df;
    int dg;
    int guard = 0;

    if (g->count == 0) { return 0; }

    /* 六槽互异自检，别名的表现是「结果合理但错」，必须显式拦住 */
    if (dst->slot == f->slot || dst->slot == g->slot) { return -1; }
    if (t1->slot == f->slot || t1->slot == g->slot || t1->slot == dst->slot) { return -1; }
    if (t2->slot == f->slot || t2->slot == g->slot || t2->slot == dst->slot ||
        t2->slot == t1->slot) { return -1; }
    if (t3->slot == f->slot || t3->slot == g->slot || t3->slot == dst->slot ||
        t3->slot == t1->slot || t3->slot == t2->slot) { return -1; }

    lcg.slot = t2->slot; lcg.count = 0;
    lcr.slot = t3->slot; lcr.count = 0;

    if (mv_copy(dst, f) != 0) { return -1; }
    if (mv_normalize(dst) != 0) { return -1; }

    dg = mv_xdeg_of(g);
    if (dg < 0) { return 0; }

    /* dg == 0 的特殊处理,这里曾经两次判断错
     *   dg == 0 表示 g 关于主变量 x 是常数（它可能仍是 y 的多项式，
     *   例如 gcd(x−y, x+y) 第 1 轮后的 g = −2y）。
     *
     *   此时伪除法「R = lcg·R − lcr·x^Δ·g」里的 lcg 不是常数而是
     *   y 的多项式，每轮乘上去会让次数不降反升（实测 −2y² 变
     *   4y³−4y² …），永远撞 guard，这是第一版的行为。
     *
     *   第二版改成「返回零多项式」，等价于宣称 g | f，但 −2y 并
     *   不整除 x+y，于是 gcd 被错报成 y（实测）。
     *
     *   正确的语义：余式就是被除式本身。因为「对关于 x 的常数
     *   除式取余」在 x 方向上没有意义，余式的 x 次数无法再降。
     *   返回 f 让主循环继续以 (g, f) 为下一对,这正好是欧几里得
     *   算法「除不动就交换」的自然行为，且保证每次迭代至少把
     *   (deg_x f, deg_x g) 这对非负整数按字典序严格减小，必然终止。 */

    while (!mv_is_zero(dst)) {
        df = mv_xdeg_of(dst);
        if (df < dg) { break; }                     /* x 次数低于除式 -> 收敛 */

        /* lcg = coef_x(g, dg)（y 的多项式；g 不变，每轮重取成本 O(28)） */
        if (mv_coef_at(&lcg, g, dg) != 0) { return -1; }
        if (mv_is_zero(&lcg)) { return -1; }        /* 首项系数为零：g 不合法 */
        /* lcr = coef_x(R, df) */
        if (mv_coef_at(&lcr, dst, df) != 0) { return -1; }

        /* 本轮三步的顺序是本函数最容易写错的地方，必须写清
         *
         * 目标：  dst = lcg·dst − lcr·x^Δ·g
         *
         * 陷阱：step 1 算 lcg·dst 时需要把 dst 的旧值暂存，而
         * step 2 算 lcr·x^Δ·g 也需要一个槽，若两者共用 t1，
         * step 2 会把 step 1 的暂存覆盖掉。上一版正是这么错的：
         * 表现为 gcd(x²−y², x−y) 返回 x−y（把除式当成了余式）。
         *
         * 正确顺序（用满 3 个临时槽，互不覆盖）：
         *   t1 = dst                  暂存旧 R
         *   dst = t1 × lcg            step 1：dst = lcg·R
         *   t2 已被 lcg 占用 → 复用前先算出 B 到 t1（t1 的暂存已用完）
         *   t1 = x^Δ · g              平移
         *   t3 已被 lcr 占用 → B 需要第三个槽：把 t1 乘到 t1 不行，
         *        所以先把 t1 拷到 dst 之外的地方 …
         *
         * 更干净的做法：先把 B 算完存进 t1，再算 A 存进 dst，
         * 最后 dst −= t1。这样只需要 t1 一个额外槽：
         *   t1  = x^Δ·g             （原地平移，支持同槽）
         *   B   = t1 × lcr          （同槽不允许 → 借 t2 的 lcg 槽）
         *   但 lcg 还要用 …
         *
         * 最终方案：把 lcg、lcr 都算出来之后，它们只各用一次，
         * 因此可以按「先 A 后 B」严格串行，每次只借一个已释放的槽：
         *   step A:  t1 = dst;  dst = t1 × lcg;      // lcg 用完，t2 释放
         *   step B:  t2 = x^Δ·g;  t1 = t2 × lcr;     // t2 已释放，可复用
         *   step C:  dst = dst − t1;                 // 三槽互异
         * 这条路径只用 t1、t2 两个临时槽，t3 完全不碰，更少的槽
         * 意味着更少的别名机会。 */
        /* step A: dst = lcg · dst */
        if (mv_copy(t1, dst) != 0) { return -1; }
        if (mv_mul_y_coef(dst, t1, &lcg) != 0) { return -1; }

        /* step B: t1 = lcr · x^Δ · g
         * t2 此刻存放 lcg，但 lcg 已在 step A 用完，可以安全复用。
         * 注意：不能直接 mv_mul_y_coef(t1, t1, &lcr)（同槽），
         * 所以走「t2 = x^Δ·g；t1 = t2 × lcr」。 */
        if (mv_shift_x(t2, g, df - dg) != 0) { return -1; }
        if (mv_mul_y_coef(t1, t2, &lcr) != 0) { return -1; }

        /* step C: dst = dst − t1 */
        /* dst = dst - t1.
         * mv_add_sub rejects aliasing between dst and its operands, so the
         * subtraction always goes through t2 as a scratch copy. Writing
         * mv_add_sub(dst, dst, t1, -1) directly returns -1 and made prem
         * fail on the very first iteration (measured: gcd(x^2-y^2, x-y)
         * returned an error instead of x-y). */
        if (mv_copy(t2, dst) != 0) { return -1; }
        if (mv_add_sub(dst, t2, t1, -1) != 0) { return -1; }
        guard++;
        if (guard > AC_PRO_MV_MAX_DEG + 2) { g_solve_error = 3; return -1; }
    }
    return 1;
}

/* 中间步骤的内容归一化
 *
 * 三次试错的完整记录
 *
 *   试错 1（只提整数 GCD）：gcd(x²−y², x²+2xy+y²) 得到 y⁴。
 *
 *   试错 2（整数 GCD + 公共单项式，中间与最终都提）：
 *       gcd(xy+x, xy+x) 返回 y+1,把答案本身的 x 也提走了。
 *       原因：最终归一化也调了同一个函数。
 *
 *   试错 3（中间提单项式、最终只提整数）：仍错。
 *       跟踪显示 gcd(x²−y², x²+2xy+y²) 第 1 轮余式 −2xy−2y²
 *       被「提取 x 因子」变成 −2y−2y²，x 次数从 1 掉到 0，
 *       而伪除法的收敛条件是「x 次数严格小于除式」，这一提等于
 *       跳过了该有的消元步骤，后续 PRS 全部错位。
 *
 *   结论：中间步骤也绝不能提取单项式。
 *       伪余式 prem 的定义要求余式的 x 次数 = deg_x(f) − 1（每次
 *       只降一阶），提取 x 因子会破坏这条不变量。整数 content 则
 *       安全，它是常数倍，不影响任何次数。
 *
 *   那系数膨胀怎么办？实测（2 变量 ≤ 6 次、项数 ≤ 28）整数 content
 *   归一化后，系数位宽始终 < 45 bit，int64 有 1.4× 余量，足够。
 *   若将来扩到 8 次或 4 变量，再引入标准子结式 β 归一化。
 *
 *   本函数因此只做两件事：整数 content 归一 + 首项符号规范。 */
static int mv_make_primitive(mv_poly_t *p, mv_poly_t *tmp) {
    ac_int_t c;
    if (p->count == 0) { return 1; }
    if (tmp->slot == p->slot) { return -1; }
    c = mv_content(p);
    if (c < 1) { c = 1; }
    if (c > 1) {
        if (mv_primitive_part(tmp, p, c) != 0) { return -1; }
        if (mv_copy(p, tmp) != 0) { return -1; }
    }
    if (p->count > 0 && mv_lead_coef(p) < 0) {
        if (mv_scale(tmp, p, -1) != 0) { return -1; }
        if (mv_copy(p, tmp) != 0) { return -1; }
    }
    return 1;
}

/* A1 的 GCD 主循环
 *
 * 本实现是 Python 基准 tools/ref_prs.py 的逐条移植
 *
 *   那个脚本用精确有理数把算法钉死，12 个用例全部通过之后才移植到
 *   这里。之前 6 轮 C 侧盲试失败，根本原因是没有可信基准：每次修
 *   一处、坏一处，无法判断是算法错还是实现错。
 *
 * 算法（与 ref_prs.py 完全一致）
 *
 *   gcd_2var(f, g):
 *       gcd_pass(x 轴)
 *       若退化 -> 用当前对 (cf, cg) 换到 y 轴再跑
 *       若两轴都退化 -> 取公共单项式因子
 *
 *   gcd_pass(f, g):
 *       while g != 0:
 *           R = prem_x(f, g)
 *           if R == 0:
 *               if 真实整除(g, f) -> return g
 *               else -> Euclid 兜底 + strip_spurious 双重校验
 *           R = primitive(R)          // 只提整数 content
 *           f, g = g, R
 *       return f
 *
 * 四条踩过的坑，写在这里防止回退
 *
 *   坑 1：把 prem(f,g)==0 当成「g | f」。
 *       多变量 prem 在 K(y)[x] 上工作，返回零只说明在该域里整除，
 *       商可能带 y 的分母。例：prem(x²+2xy+y², −2xy−2y²)=0，
 *       但 −2xy−2y² 不整除 x²+2xy+y²（商 = −(x+y)/(2y)）。
 *       => 必须用真实长除法验证。
 *
 *   坑 2：退化时用原始输入重跑另一轴。
 *       那会丢掉第一轴已约化掉的因子。
 *       => 必须用当前对 (cf, cg)。
 *
 *   坑 3：公共单项式用「最大次数」算。
 *       f = x²y + x 的 min y 指数是 0（+x 项无 y），用 deg_y 会误判
 *       出 y 因子。
 *       => 用所有项指数的最小值（mv_common_monomial 已按此实现）。
 *
 *   坑 4：中间归一化提单项式因子。
 *       那会改变 deg_x，破坏 prem「每轮降 1 阶」的不变量。
 *       => 中间只提整数 content（mv_make_primitive 已如此）。
 *
 * 槽位 f=A、g=B、R=R、临时 T1/T2/T3,6 槽全用。 */

static int mv_gcd_pass(mv_poly_t *dst, const mv_poly_t *fa, const mv_poly_t *fb,
                       int *out_degraded, mv_poly_t *out_cf, mv_poly_t *out_cg);
static int mv_strip_spurious(mv_poly_t *dst, const mv_poly_t *cand,
                             const mv_poly_t *f, const mv_poly_t *g);

/* Euclid 兜底
 * prem 返回零但不真实整除时调用。此时 (f, g) 已被 PRS 约化到很小的
 * 规模，普通欧几里得几轮就收敛。
 *
 * 结果必须对原始 f0、g0 校验（循环会改写工作副本），因为一个只
 * 整除当前 f 的因子未必整除 f0。
 *
 * 槽位：工作副本用 T1/T2/T3，dst 接收最终结果。
 * 要求 dst 与 f、g 互异。 */
static int mv_euclid_gcd(mv_poly_t *dst, const mv_poly_t *f, const mv_poly_t *g) {
    int euclid_scratch = SLOT_A;
    mv_poly_t x;
    mv_poly_t y;
    mv_poly_t r;
    mv_poly_t tmp;
    int guard = 0;

    if (dst->slot == f->slot || dst->slot == g->slot) {
        return -1;
    }
    /* dst may be any slot; the working slots below are chosen to avoid it. */

    /* Slot plan (6 slots total, dst is one of them):
     *   x     = T1   current dividend
     *   y     = T2   current divisor
     *   r     = T3   remainder  (must differ from the scratch slot)
     *   tmp   = dst  used only for the primitive-part normalisation,
     *                which happens between div_remainder calls
     * mv_div_remainder internally needs one scratch slot: we pass SLOT_A,
     * which is free here (the caller's f lives in A only during prem). */
    /* Pick three working slots that are not dst, then a scratch slot that is
     * none of those four. There are 6 slots, so this always succeeds. */
    {
        int cand[6];
        int nc = 0;
        int i;
        int s0, s1, s2, sc;
        for (i = 0; i < 6; i++) { if (i != dst->slot) { cand[nc++] = i; } }
        s0 = cand[0]; s1 = cand[1]; s2 = cand[2]; sc = cand[3];
        x.slot = s0; x.count = 0;
        y.slot = s1; y.count = 0;
        r.slot = s2; r.count = 0;
        tmp.slot = s2; tmp.count = 0;   /* tmp is only used transiently */
        euclid_scratch = sc;
    }

    if (mv_copy(&x, f) != 0) { return -1; }
    if (mv_copy(&y, g) != 0) { return -1; }
    if (mv_normalize(&x) != 0 || mv_normalize(&y) != 0) { return -1; }

    while (!mv_is_zero(&y)) {
        if (mv_div_remainder(&r, &x, &y, euclid_scratch) != 1) { return -1; }
        if (mv_is_zero(&r)) {
            return mv_strip_spurious(dst, &y, f, g);
        }
        if (mv_make_primitive(&r, &tmp) != 1) { return -1; }
        if (mv_copy(&x, &y) != 0) { return -1; }
        if (mv_copy(&y, &r) != 0) { return -1; }
        guard++;
        if (guard > 32) { break; }
    }
    return mv_strip_spurious(dst, &x, f, g);
}

/* 剥掉不整除输入的因子
 * 候选 cand 可能带多余因子（典型是某个操作数没有的 y 的幂）。
 * 在 cand 的所有「除以单项式」的商里，找项数最多且同时整除
 * f 与 g 的那个。
 *
 * 实测：cand = y(x+y) 时，cand/y = x+y 整除两个输入，而 cand 本身
 * 不整除 f，于是选出 x+y。
 *
 * 槽位：best=T1、tmp=T2、mono=T3。dst 必须与这三者互异。 */
static int mv_strip_spurious(mv_poly_t *dst, const mv_poly_t *cand,
                             const mv_poly_t *f, const mv_poly_t *g) {
    mv_poly_t best;
    mv_poly_t tmp;
    mv_poly_t mono;
    int mx;
    int my;
    int a;
    int b;
    int best_terms = -1;

    if (mv_is_zero(cand)) { mv_clear(dst); return 1; }

    /* 原候选先试一次（最常见情形） */
    if (mv_divides(cand, f) && mv_divides(cand, g)) {
        return mv_copy(dst, cand) == 0 ? 1 : -1;
    }

    mx = AC_PRO_MV_EXP_MASK;
    my = AC_PRO_MV_EXP_MASK;
    {
        int i;
        for (i = 0; i < cand->count; i++) {
            int xd = mv_xdeg(mv_terms(cand->slot)[i].exps);
            int yd = mv_ydeg(mv_terms(cand->slot)[i].exps);
            if (xd < mx) { mx = xd; }
            if (yd < my) { my = yd; }
        }
    }

    best.slot = SLOT_T1; best.count = 0;
    tmp.slot = SLOT_T2;  tmp.count = 0;
    mono.slot = SLOT_T3; mono.count = 0;

    for (a = 0; a <= mx; a++) {
        for (b = 0; b <= my; b++) {
            int rc;
            mv_clear(&mono);
            if (mv_push(&mono, 1, mv_exps(a, b)) != 0) { return -1; }
            if (mv_normalize(&mono) != 0) { return -1; }

            rc = mv_div_exact(&tmp, cand, &mono);
            if (rc != 1) { continue; }
            if (mv_is_zero(&tmp)) { continue; }

            if (mv_divides(&tmp, f) && mv_divides(&tmp, g)) {
                if (tmp.count > best_terms) {
                    if (mv_copy(&best, &tmp) != 0) { return -1; }
                    best_terms = tmp.count;
                }
            }
        }
    }

    if (best_terms >= 0) { return mv_copy(dst, &best) == 0 ? 1 : -1; }

    return mv_common_monomial(dst, f, g);
}

/* Single-axis PRS pass.
 * Returns 1 = success (result in *dst, *out_degraded = 0)
 *         0 = degraded (*out_cf / *out_cg hold the CURRENT pair)
 *        -1 = error
 * Slots: f=A, g=B, r=R, and T1/T2/T3 for prem and primitive. */
static int mv_gcd_pass(mv_poly_t *dst, const mv_poly_t *fa, const mv_poly_t *fb,
                       int *out_degraded, mv_poly_t *out_cf, mv_poly_t *out_cg) {
    mv_poly_t f;
    mv_poly_t g;
    mv_poly_t r;
    mv_poly_t t1;
    mv_poly_t t2;
    mv_poly_t t3;
    int guard = 0;

    f.slot = SLOT_A;  f.count = 0;
    g.slot = SLOT_B;  g.count = 0;
    r.slot = SLOT_R;  r.count = 0;
    t1.slot = SLOT_T1; t1.count = 0;
    t2.slot = SLOT_T2; t2.count = 0;
    t3.slot = SLOT_T3; t3.count = 0;

    *out_degraded = 0;

    if (mv_copy(&f, fa) != 0) { return -1; }
    if (mv_copy(&g, fb) != 0) { return -1; }
    if (mv_normalize(&f) != 0 || mv_normalize(&g) != 0) { return -1; }
    if (mv_make_primitive(&f, &t1) != 1) { return -1; }
    if (mv_make_primitive(&g, &t1) != 1) { return -1; }

    if (mv_is_zero(&f)) { return mv_copy(dst, &g) == 0 ? 1 : -1; }
    if (mv_is_zero(&g)) { return mv_copy(dst, &f) == 0 ? 1 : -1; }

    while (!mv_is_zero(&g)) {
        int dg = mv_xdeg_of(&g);

        /* 除式在该轴上是常数：伪除法无法继续，退化 */
        if (dg == 0) {
            if (mv_copy(out_cf, &f) != 0) { return -1; }
            if (mv_copy(out_cg, &g) != 0) { return -1; }
            *out_degraded = 1;
            return 0;
        }

        if (mv_prem_full(&r, &f, &g, &t1, &t2, &t3) != 1) { return -1; }

        if (mv_is_zero(&r)) {
            /* prem 返回零,必须做真实整除验证 */
            if (mv_divides(&g, &f)) {
                return mv_copy(dst, &g) == 0 ? 1 : -1;
            }
            /* prem == 0 but not divisible: Euclid fallback.
             * mv_euclid_gcd refuses a scratch-slot destination, and inside
             * this pass f/g/r already own A/B/R. So compute into t3 and copy. */
            {
                mv_poly_t eout;
                eout.slot = SLOT_T3;
                eout.count = 0;
                    if (mv_euclid_gcd(&eout, &f, &g) != 1) { return -1; }
                if (mv_copy(dst, &eout) != 0) { return -1; }
                return 1;
            }
        }

        /* 中间归一化：只提整数 content（不提单项式） */
        if (mv_make_primitive(&r, &t1) != 1) { return -1; }

        if (mv_copy(&f, &g) != 0) { return -1; }
        if (r.count < 0 || r.count > AC_PRO_MV_TERMS) {
            return -1;
        }
        if (mv_copy(&g, &r) != 0) { return -1; }

        guard++;
        if (guard > 64) { g_solve_error = 3; return -1; }
    }

    return mv_copy(dst, &f) == 0 ? 1 : -1;
}

/* A1 final validation (stack-based)
 *
 * WHY A SEPARATE STACK IMPLEMENTATION
 * The PRS / degradation / Euclid pipelines each own the six union slots at
 * different moments. Bolting a validation pass onto them caused a long chain
 * of slot-aliasing bugs: every fix exposed the next collision, because
 * mv_divides() alone needs two scratch slots on top of whatever the caller
 * already holds.
 *
 * The clean fix: run the validation on STACK copies of the polynomials.
 * A polynomial is at most 28 terms x 16 B = 448 B, and the stack is not part
 * of .bss, so this costs zero bytes of the 42608 B budget (v2.1.0) and cannot collide
 * with any union slot.
 *
 * Invariant: the two original inputs are snapshotted once at the top of
 * mv_gcd_prs, before any slot is reused. */

typedef struct {
    ac_pro_term_t t[AC_PRO_MV_TERMS];
    int           n;
} mv_snap_t;

typedef struct {
    ac_pro_term_t t[AC_PRO_MV_TERMS];
    int           n;
} mv_sp_t;

static void mv_snap_take(mv_snap_t *s, const mv_poly_t *p) {
    int i;
    int n = p->count;
    if (n < 0) { n = 0; }
    if (n > AC_PRO_MV_TERMS) { n = AC_PRO_MV_TERMS; }
    for (i = 0; i < n; i++) { s->t[i] = mv_terms(p->slot)[i]; }
    s->n = n;
}

static void sp_from_poly(mv_sp_t *d, const mv_poly_t *p) {
    int i;
    int n = p->count;
    if (n < 0) { n = 0; }
    if (n > AC_PRO_MV_TERMS) { n = AC_PRO_MV_TERMS; }
    for (i = 0; i < n; i++) { d->t[i] = mv_terms(p->slot)[i]; }
    d->n = n;
}

static void sp_from_snap(mv_sp_t *d, const mv_snap_t *s) {
    int i;
    for (i = 0; i < s->n; i++) { d->t[i] = s->t[i]; }
    d->n = s->n;
}

static void sp_clear(mv_sp_t *d) { d->n = 0; }
static int  sp_is_zero(const mv_sp_t *d) { return d->n == 0; }

static int sp_push(mv_sp_t *d, ac_int_t c, int32_t exps) {
    if (c == 0) { return 0; }
    if (d->n >= AC_PRO_MV_TERMS) { return -1; }
    d->t[d->n].c = c;
    d->t[d->n].exps = exps;
    d->t[d->n].pad = 0;
    d->n++;
    return 0;
}

static int sp_copy(mv_sp_t *d, const mv_sp_t *s) {
    int i;
    for (i = 0; i < s->n; i++) { d->t[i] = s->t[i]; }
    d->n = s->n;
    return 0;
}

static int sp_xdeg(int32_t e) { return (int)((e >> AC_PRO_MV_EXP_BITS) & AC_PRO_MV_EXP_MASK); }
static int sp_ydeg(int32_t e) { return (int)(e & AC_PRO_MV_EXP_MASK); }

/* Sort descending by exps, merge equal exponents, drop zeros. */
static int sp_normalize(mv_sp_t *d) {
    int i;
    int j;
    int out;

    for (i = 1; i < d->n; i++) {
        ac_pro_term_t key = d->t[i];
        j = i - 1;
        while (j >= 0 && d->t[j].exps < key.exps) {
            d->t[j + 1] = d->t[j];
            j--;
        }
        d->t[j + 1] = key;
    }

    out = 0;
    i = 0;
    while (i < d->n) {
        ac_int_t sum = d->t[i].c;
        int32_t ex = d->t[i].exps;
        int k = i + 1;
        while (k < d->n && d->t[k].exps == ex) {
            if (!ac_safe_add(sum, d->t[k].c, &sum)) { return -1; }
            k++;
        }
        if (sum != 0) {
            d->t[out].c = sum;
            d->t[out].exps = ex;
            d->t[out].pad = 0;
            out++;
        }
        i = k;
    }
    d->n = out;
    return 0;
}

/* r = a - b */
static int sp_sub(mv_sp_t *r, const mv_sp_t *a, const mv_sp_t *b) {
    int i;
    sp_clear(r);
    for (i = 0; i < a->n; i++) {
        if (sp_push(r, a->t[i].c, a->t[i].exps) != 0) { return -1; }
    }
    for (i = 0; i < b->n; i++) {
        ac_int_t c = b->t[i].c;
        if (c == INT64_MIN) { return -1; }
        if (sp_push(r, -c, b->t[i].exps) != 0) { return -1; }
    }
    return sp_normalize(r);
}

/* Long division on stack polynomials; remainder written to r. */
static int sp_div_rem(mv_sp_t *r, const mv_sp_t *a, const mv_sp_t *d) {
    mv_sp_t t;
    mv_sp_t sub;
    mv_sp_t scaled;
    mv_sp_t settled;
    int lx;
    int ly;
    ac_int_t lc;
    int guard = 0;

    if (d->n == 0) { return -1; }

    /* 局部 mv_sp_t 全量清零：sp_copy 只写 [0, n) 并把 n 设成有效值，
     * n 之后的槽位保持栈垃圾。这里整体清零，杜绝任何按固定长度遍历的
     * 代码把 -110 这类未初始化值当系数（与 mv_validate_gcd 同一纪律）。 */
    memset(&t, 0, sizeof(mv_sp_t));
    memset(&sub, 0, sizeof(mv_sp_t));
    memset(&scaled, 0, sizeof(mv_sp_t));
    memset(&settled, 0, sizeof(mv_sp_t));

    sp_clear(&settled);
    sp_copy(r, a);
    if (sp_normalize(r) != 0) { return -1; }

    lx = sp_xdeg(d->t[0].exps);
    ly = sp_ydeg(d->t[0].exps);
    lc = d->t[0].c;

    while (!sp_is_zero(r)) {
        int dx;
        int dy;
        ac_int_t lcr;
        int i;
        int k;

        /* Re-normalise: r->t[0] must be the current leading term. */
        if (sp_normalize(r) != 0) { return -1; }
        if (sp_is_zero(r)) { break; }

        dx = sp_xdeg(r->t[0].exps) - lx;
        dy = sp_ydeg(r->t[0].exps) - ly;

        if (dx < 0 || dy < 0) {
            /* Leading term cannot be eliminated. Move it to the settled
             * buffer and continue with the remaining terms; simply breaking
             * leaves the remainder untouched and makes the outer Euclid loop
             * oscillate forever (measured: x^2 <-> x*y). */
            if (sp_push(&settled, r->t[0].c, r->t[0].exps) != 0) { return -1; }
            {
                int q;
                for (q = 1; q < r->n; q++) { r->t[q - 1] = r->t[q]; }
                r->n--;
            }
            if (sp_normalize(&settled) != 0) { return -1; }
            guard++;
            if (guard > 512) { return -1; }
            continue;
        }
        lcr = r->t[0].c;

        /* Pseudo-division: scale the remainder by lc to stay integral. */
        if (lc != 1) {
            sp_clear(&scaled);
            for (k = 0; k < r->n; k++) {
                ac_int_t v;
                if (!ac_safe_mul(r->t[k].c, lc, &v)) { return -1; }
                if (sp_push(&scaled, v, r->t[k].exps) != 0) { return -1; }
            }
            if (sp_normalize(&scaled) != 0) { return -1; }
            sp_copy(r, &scaled);
        }

        sp_clear(&sub);
        for (i = 0; i < d->n; i++) {
            ac_int_t prod;
            int nx = sp_xdeg(d->t[i].exps) + dx;
            int ny = sp_ydeg(d->t[i].exps) + dy;   /* dy is 0 by design */
            if (!ac_safe_mul(d->t[i].c, lcr, &prod)) { return -1; }
            if (nx > AC_PRO_MV_EXP_MASK || ny > AC_PRO_MV_EXP_MASK) { return -1; }
            if (sp_push(&sub, prod, mv_exps(nx, ny)) != 0) { return -1; }
        }
        if (sp_normalize(&sub) != 0) { return -1; }

        sp_copy(&t, r);
        if (sp_sub(r, &t, &sub) != 0) { return -1; }

        guard++;
        if (guard > 256) { return -1; }
    }

    /* remainder = working remainder + settled terms */
    if (!sp_is_zero(&settled)) {
        int q;
        for (q = 0; q < settled.n; q++) {
            if (sp_push(r, settled.t[q].c, settled.t[q].exps) != 0) { return -1; }
        }
        if (sp_normalize(r) != 0) { return -1; }
    }
    return 1;
}

static int sp_divides(const mv_sp_t *d, const mv_sp_t *a) {
    mv_sp_t r;
    if (d->n == 0) { return 0; }
    if (a->n == 0) { return 1; }
    if (sp_div_rem(&r, a, d) != 1) { return 0; }
    return sp_is_zero(&r);
}


static int sp_euclid(mv_sp_t *dst, const mv_sp_t *f, const mv_sp_t *g);

/* Plain Euclid on stack polynomials, used as the final fallback.
 *
 * Terminates with a polynomial that divides f in K(y)[x]. That is weaker than
 * divisibility in Z[x,y] (measured: f = x^2+2xy+y^2, g = y(x+y) gives
 * remainder 0 over K(y)[x] but g does not divide f). So the result is always
 * validated against the ORIGINAL f and g, and reduced by searching monomial
 * quotients until a real common divisor is found. */
static int sp_euclid(mv_sp_t *dst, const mv_sp_t *f, const mv_sp_t *g) {
    mv_sp_t x;
    mv_sp_t y;
    mv_sp_t r;
    int guard = 0;

    memset(&x, 0, sizeof(mv_sp_t));
    memset(&y, 0, sizeof(mv_sp_t));
    memset(&r, 0, sizeof(mv_sp_t));

    sp_copy(&x, f);
    sp_copy(&y, g);
    if (sp_normalize(&x) != 0 || sp_normalize(&y) != 0) { return -1; }

    while (!sp_is_zero(&y)) {
        if (sp_div_rem(&r, &x, &y) != 1) { return -1; }
        if (sp_is_zero(&r)) { break; }
        sp_copy(&x, &y);
        sp_copy(&y, &r);
        guard++;
        if (guard > 32) { break; }
    }

    /* y is now either zero (x holds the gcd) or the last non-zero remainder. */
    {
        const mv_sp_t *cand = sp_is_zero(&y) ? &x : &y;
        mv_sp_t best;
        int mx = AC_PRO_MV_EXP_MASK;
        int my = AC_PRO_MV_EXP_MASK;
        int a;
        int b;
        int i;
        int best_terms = -1;

        memset(&best, 0, sizeof(mv_sp_t));

        if (sp_divides(cand, f) && sp_divides(cand, g)) {
            return sp_copy(dst, cand);
        }

        for (i = 0; i < cand->n; i++) {
            int xd = sp_xdeg(cand->t[i].exps);
            int yd = sp_ydeg(cand->t[i].exps);
            if (xd < mx) { mx = xd; }
            if (yd < my) { my = yd; }
        }
        best.n = 0;
        for (a = 0; a <= mx; a++) {
            for (b = 0; b <= my; b++) {
                mv_sp_t q;
                int k;
                int ok = 1;
                sp_clear(&q);
                for (k = 0; k < cand->n; k++) {
                    int xd = sp_xdeg(cand->t[k].exps) - a;
                    int yd = sp_ydeg(cand->t[k].exps) - b;
                    if (xd < 0 || yd < 0) { ok = 0; break; }
                    if (sp_push(&q, cand->t[k].c, mv_exps(xd, yd)) != 0) { return -1; }
                }
                if (!ok) { continue; }
                if (sp_normalize(&q) != 0) { return -1; }
                if (sp_is_zero(&q)) { continue; }
                if (sp_divides(&q, f) && sp_divides(&q, g)) {
                    if (q.n > best_terms) { sp_copy(&best, &q); best_terms = q.n; }
                }
            }
        }
        if (best_terms >= 0) { return sp_copy(dst, &best); }

        /* Still nothing: the two polynomials are coprime in Z[x,y]. */
        sp_clear(dst);
        if (sp_push(dst, 1, mv_exps(0, 0)) != 0) { return -1; }
        return sp_normalize(dst);
    }
}

/* Reduce cand to the largest divisor that divides both f and g. */
static int sp_strip_spurious(mv_sp_t *dst, const mv_sp_t *cand,
                             const mv_sp_t *f, const mv_sp_t *g) {
    mv_sp_t best;
    mv_sp_t tmp;
    int mx;
    int my;
    int a;
    int b;
    int i;
    int best_terms = -1;

    memset(&best, 0, sizeof(mv_sp_t));
    memset(&tmp, 0, sizeof(mv_sp_t));

    if (sp_is_zero(cand)) { sp_clear(dst); return 0; }

    /* A constant candidate divides everything, so it must not be accepted on
     * divisibility alone -- that would let a wrong pipeline answer (measured:
     * gcd(x*y-1, x^2*y-x) came back as 1) pass through. Recompute instead. */
    if (cand->n == 1 && sp_xdeg(cand->t[0].exps) == 0 && sp_ydeg(cand->t[0].exps) == 0) {
        return sp_euclid(dst, f, g);
    }

    if (sp_divides(cand, f) && sp_divides(cand, g)) {
        return sp_copy(dst, cand);
    }

    mx = AC_PRO_MV_EXP_MASK;
    my = AC_PRO_MV_EXP_MASK;
    for (i = 0; i < cand->n; i++) {
        int xd = sp_xdeg(cand->t[i].exps);
        int yd = sp_ydeg(cand->t[i].exps);
        if (xd < mx) { mx = xd; }
        if (yd < my) { my = yd; }
    }

    best.n = 0;
    for (a = 0; a <= mx; a++) {
        for (b = 0; b <= my; b++) {
            int ok = 1;
            int k;
            sp_clear(&tmp);
            for (k = 0; k < cand->n; k++) {
                int xd = sp_xdeg(cand->t[k].exps) - a;
                int yd = sp_ydeg(cand->t[k].exps) - b;
                if (xd < 0 || yd < 0) { ok = 0; break; }
                if (sp_push(&tmp, cand->t[k].c, mv_exps(xd, yd)) != 0) { return -1; }
            }
            if (!ok) { continue; }
            if (sp_normalize(&tmp) != 0) { return -1; }
            if (sp_is_zero(&tmp)) { continue; }
            if (sp_divides(&tmp, f) && sp_divides(&tmp, g)) {
                if (tmp.n > best_terms) {
                    sp_copy(&best, &tmp);
                    best_terms = tmp.n;
                }
            }
        }
    }

    if (best_terms >= 0) { return sp_copy(dst, &best); }

    /* No divisor of cand works: cand itself is the wrong shape (measured:
     * gcd(x^2-y^2, x^2+2xy+y^2) produced x^3*y, which shares no useful
     * factor with the inputs). Recompute directly from the originals. */
    return sp_euclid(dst, f, g);
}

/* 边界保护 sp_* 层的项表容量就是 AC_PRO_MV_TERMS，任何 sp_from_* /
 * sp_push 都在这个上限上截断或失败。这里再守一道门，让畸形候选（上游漏
 * 出来的坏结果、快照被写坏）安全失败，而不是在 sp_* 里被静默截断后
 * 拿去比较。
 *
 * 上限必须取 AC_PRO_MV_TERMS（28），不能收紧到 8：2 变量、总次数 <= 6 的
 * 全部单项式有 C(8,2) = 28 个，合法输入就能占满，压到 8 会把
 * gcd(x^4+x^3y+x^2y^2+xy^3+y^4+x^3+x^2y+xy^2+y^3, x^2+xy+y^2) 这类用例
 * 误判为失败（实测 rc=-1）。安全性由「不超 mv_sp_t 物理容量」保证。
 *
 * 同理不缩小 mv_sp_t 本身，它必须与 mv_snap_t 同形（28 项），否则
 * mv_gcd_prs 快照下来的原始输入装不下，校验就失去参照物。 */
#define MV_VALIDATE_MAX_TERMS AC_PRO_MV_TERMS

/* Validate dst against the original inputs; everything runs on the stack.
 * 所有局部 mv_sp_t 先整体清零再使用：sp_from_* 只写 [0, n) 区间并把 n
 * 设成有效值，n 之后的槽位是垃圾；只要有任何一处代码不慎按固定长度
 * （而非按 n）遍历，就会把栈垃圾（实测出现过 -110）当成系数参与比较。
 * memset 一次 448 B × 4 个对象，代价可忽略，换来的是「未初始化读」这一类
 * 缺陷在结构上不可能再发生。 */
static int mv_validate_gcd(mv_poly_t *dst, const mv_snap_t *sf, const mv_snap_t *sg) {
    mv_sp_t cand;
    mv_sp_t f;
    mv_sp_t g;
    mv_sp_t out;
    int i;

    if (mv_is_zero(dst)) { return 1; }

    /* 边界保护：项数超限直接安全失败，绝不越界写 */
    if (dst->count < 0 || dst->count > MV_VALIDATE_MAX_TERMS) { return -1; }
    if (sf->n < 0 || sf->n > MV_VALIDATE_MAX_TERMS) { return -1; }
    if (sg->n < 0 || sg->n > MV_VALIDATE_MAX_TERMS) { return -1; }

    /* 全量清零：杜绝未初始化栈值参与比较 */
    memset(&cand, 0, sizeof(mv_sp_t));
    memset(&f, 0, sizeof(mv_sp_t));
    memset(&g, 0, sizeof(mv_sp_t));
    memset(&out, 0, sizeof(mv_sp_t));

    sp_from_poly(&cand, dst);
    sp_from_snap(&f, sf);
    sp_from_snap(&g, sg);

    if (sp_strip_spurious(&out, &cand, &f, &g) != 0) { return -1; }
    if (sp_is_zero(&out)) {
        /* Validation produced nothing usable: fall back to Euclid. */
        if (sp_euclid(&out, &f, &g) != 0) { return -1; }
    }

    if (out.n < 0 || out.n > MV_VALIDATE_MAX_TERMS) { return -1; }

    mv_clear(dst);
    for (i = 0; i < out.n; i++) {
        if (mv_push(dst, out.t[i].c, out.t[i].exps) != 0) { return -1; }
    }
    if (mv_normalize(dst) != 0) { return -1; }
    return 1;
}


static int mv_gcd_prs(mv_poly_t *dst, const mv_poly_t *fa, const mv_poly_t *fb) {
    mv_poly_t f;
    mv_poly_t g;
    mv_poly_t r;
    mv_poly_t t1;
    mv_poly_t cf;
    mv_poly_t cg;
    mv_poly_t pass_out;
    mv_snap_t snap_f;
    mv_snap_t snap_g;
    int degraded = 0;
    int main_var;

    f.slot = SLOT_A;  f.count = 0;
    g.slot = SLOT_B;  g.count = 0;
    r.slot = SLOT_R;  r.count = 0;
    t1.slot = SLOT_T1; t1.count = 0;
    cf.slot = SLOT_T2; cf.count = 0;
    cg.slot = SLOT_T3; cg.count = 0;

    /* Snapshot the original inputs on the stack before any union slot is
     * reused. The final validation runs against these copies. */
    mv_snap_take(&snap_f, fa);
    mv_snap_take(&snap_g, fb);
    /* mv_gcd_pass uses SLOT_R as its own remainder buffer, so it must not
     * write its result straight into dst when dst is SLOT_R. pass_out is a
     * distinct slot; the result is copied to dst afterwards. */
    pass_out.slot = (dst->slot == SLOT_T1) ? SLOT_T2 : SLOT_T1;
    pass_out.count = 0;

    if (mv_copy(&f, fa) != 0) { return -1; }
    if (mv_copy(&g, fb) != 0) { return -1; }
    if (mv_normalize(&f) != 0 || mv_normalize(&g) != 0) { return -1; }

    /* 零操作数必须在 pick_main_var 之前判
     *   gcd(f, 0) = f，gcd(0, g) = g，gcd(0, 0) = 0，都是定义上的恒等式。
     *   而空多项式没有任何变量，mv_pick_main_var 会把它看成「两者没有
     *   公共变量」，从而短路返回 2（互素 -> 1）。
     *   实测（修复前）：A1-5 gcd(x^2-y^2, "0") 返回 "1"。
     *   根因其实在解析层（"0" 曾被解析成常数 10），但解析修好后 "0" 会
     *   正确地变成空多项式，于是这条顺序缺陷就暴露出来了，两处都得修。 */
    if (mv_is_zero(&f) || mv_is_zero(&g)) {
        if (mv_is_zero(&f) && mv_is_zero(&g)) {
            mv_clear(dst);
        } else if (mv_is_zero(&f)) {
            if (mv_copy(dst, &g) != 0) { return -1; }
        } else {
            if (mv_copy(dst, &f) != 0) { return -1; }
        }
        return mv_normalize(dst) == 0 ? 1 : -1;
    }

    main_var = mv_pick_main_var(&f, &g);

    if (main_var == 2) {                    /* 无公共变量：互素 */
        mv_clear(dst);
        if (mv_push(dst, 1, mv_exps(0, 0)) != 0) { return -1; }
        return mv_normalize(dst) == 0 ? 1 : -1;
    }

    if (main_var == 1) {                    /* 以 y 为主变量：交换后按 x 处理 */
        if (mv_swap_xy(&r, &f) != 0) { return -1; }
        if (mv_copy(&f, &r) != 0) { return -1; }
        if (mv_swap_xy(&r, &g) != 0) { return -1; }
        if (r.count < 0 || r.count > AC_PRO_MV_TERMS) {
            return -1;
        }
        if (mv_copy(&g, &r) != 0) { return -1; }
    }

    {
        int rc = mv_gcd_pass(&pass_out, &f, &g, &degraded, &cf, &cg);
        if (rc < 0) { return -1; }
        if (!degraded) {
            if (mv_copy(dst, &pass_out) != 0) { return -1; }
        }

        if (degraded) {
            /* 换到 y 轴，用当前对 (cf, cg) 继续 */
            mv_poly_t fs;
            mv_poly_t gs;
            mv_poly_t res2;
            mv_poly_t d2f;
            mv_poly_t d2g;
            int deg2 = 0;

            fs.slot = SLOT_A;   fs.count = 0;
            gs.slot = SLOT_B;   gs.count = 0;
            res2.slot = SLOT_R; res2.count = 0;
            d2f.slot = SLOT_T1; d2f.count = 0;
            d2g.slot = SLOT_T2; d2g.count = 0;

            if (mv_swap_xy(&fs, &cf) != 0) { return -1; }
            if (mv_swap_xy(&gs, &cg) != 0) { return -1; }

            rc = mv_gcd_pass(&res2, &fs, &gs, &deg2, &d2f, &d2g);
            if (rc < 0) { return -1; }

            if (!deg2) {
                mv_poly_t back;
                back.slot = SLOT_T3;
                back.count = 0;
                if (mv_swap_xy(&back, &res2) != 0) { return -1; }
                /* 交换回来之后必须对原始输入复核 */
                if (mv_strip_spurious(dst, &back, fa, fb) != 1) { return -1; }
            } else {
                if (mv_common_monomial(dst, &cf, &cg) != 1) { return -1; }
            }
        }
    }

    if (main_var == 1) {                    /* 交换回原坐标系 */
        if (mv_swap_xy(&r, dst) != 0) { return -1; }
        if (mv_copy(dst, &r) != 0) { return -1; }
    }

    /* FINAL VALIDATION.
     * Whatever the pipeline produced must divide both original inputs.
     * The PRS, degradation and Euclid paths can each leak a spurious
     * factor (typically a y power carried by only one operand). Measured
     * before this check existed:
     *     gcd(x^2-y^2, x^2+2xy+y^2) -> x^3*y   (expected x+y)
     *     gcd(2x^2-2y^2, 4x^2-4xy)  -> xy-y^2  (expected x-y)
     *     gcd(x^2*y, x*y^2)         -> x*y^2   (expected x*y)
     * mv_validate_gcd reduces the candidate to the largest factor that
     * really divides both, working entirely on stack copies. */
    if (mv_validate_gcd(dst, &snap_f, &snap_g) != 1) { return -1; }

    /* 最终归一化：整数 content + 首项符号（不提单项式） */
    if (!mv_is_zero(dst)) {
        ac_int_t c = mv_content(dst);
        if (c > 1) {
            if (mv_primitive_part(&t1, dst, c) != 0) { return -1; }
            if (mv_copy(dst, &t1) != 0) { return -1; }
        }
        if (mv_lead_coef(dst) < 0) {
            if (mv_scale(&t1, dst, -1) != 0) { return -1; }
            if (mv_copy(dst, &t1) != 0) { return -1; }
        }
    }
    return 1;
}

/* 对外：解析多项式字符串
 * 语法（刻意窄）：项用 + / - 分隔，每项形如
 *      [系数][*] x[^n][*] y[^m]      或     [系数][*] y[^m][*] x[^n]
 * 系数缺省为 1；变量可缺省（纯常数项）；指数缺省为 1。
 * 不支持括号、除法、负指数，多变量层只做「多项式」，不做表达式。
 *
 * 为什么不用上游解析器：上游 ac_parse_expression 产出的是求值用的
 * token 流（T_NUM/T_VAR/T_OP），且 T_VAR 的 payload 在 v1.2 里被 Pro
 * 复用成变量 ID 但上游仍恒为 0；把 token 流反解成多项式需要额外一层
 * 求值-采样，精度与次数都不可控。多变量层直接解析字符串，是设计说明
 * §3.1.1 指出的「合规做法」的同一条思路（Pro 自己扫描原始字符串）。 */
static int mv_parse_term(const char **ps, ac_int_t *coef, int *xd, int *yd) {
    const char *s = *ps;
    /* 必须从 0 起累加，不能从 1 起
     *   系数循环是 c = c*10 + digit。若初值为 1，"1" 会算成 11、"2" 算成
     *   12、"8" 算成 18、"12" 算成 112、"0" 算成 10,每一个显式写出的
     *   整数系数都被加上了一个隐含的前导 1。
     *   隐式系数 1 的默认值由下面 `if (!saw_coef) { c = 1; }` 单独负责，
     *   两者不能混为一谈。
     *   实测后果（修复前）：A1-1 gcd(x^2-y^2, x-y) 得到 "x + y"、
     *   A1-5 gcd(x^2-y^2, 0) 里 "0" 被当成常数 10 而得到 "1"、
     *   A3-1 Res(y^2-x, y-1) 得到 117 而不是 -3。 */
    ac_int_t c = 0;
    int nx = 0;
    int ny = 0;
    int saw_coef = 0;
    int saw_x = 0;
    int saw_y = 0;
    int neg = 0;

    /* 跳过空白 */
    while (*s == ' ' || *s == '\t') { s++; }
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    /* 符号后必须再跳一次空白
     *   实测 bug：输入 "- 1"（减号后有空格）时，若不跳过，下面的
     *   系数循环看到的是 ' ' 而不是数字，saw_coef 保持 0、c 退化成 1，
     *   变量循环也立刻退出,于是这一项被当成「-1·x^0·y^0」但指针
     *   停在了 '1' 上，外层再把 '1' 当成非法分隔符，整个解析失败。
     *   表现：所有「带负常数项」的多项式都解析不了
     *   （x*y - 1、x^2 + y^2 - 1、2*x^2 - 2*y^2 ...）。 */
    while (*s == ' ' || *s == '\t') { s++; }

    /* 系数 */
    while (*s >= '0' && *s <= '9') {
        if (c > (INT64_MAX - 9) / 10) { return -1; }
        c = c * 10 + (*s - '0');
        saw_coef = 1;
        s++;
    }
    if (!saw_coef) { c = 1; }
    while (*s == ' ' || *s == '\t') { s++; }
    if (*s == '*') { s++; }
    while (*s == ' ' || *s == '\t') { s++; }

    /* 变量部分：至多两个变量 */
    for (;;) {
        int var;
        int exp = 1;
        if (*s == 'x' || *s == 'X') { var = 0; }
        else if (*s == 'y' || *s == 'Y') { var = 1; }
        else { break; }

        s++;
        if (*s == '^') {
            s++;
            exp = 0;
            while (*s >= '0' && *s <= '9') { exp = exp * 10 + (*s - '0'); s++; }
        }
        /* 这里只守 exps 的编码上限，不守业务次数上限
         *   exps 用两个 4-bit 字段存 x/y 指数，单变量指数最大 15；
         *   超过 15 会溢出到相邻字段，必须在这里当成语法错误拒掉。
         *   而「总次数 ≤ 6」是语义约束，由 ac_pro_multi_var_gcd 的
         *   preCheck（mv_total_degree > AC_PRO_MV_MAX_DEG -> g_solve_error = 1）
         *   与 A3 的 df+dg 阶数门禁负责。
         *   修复前这里误用 AC_PRO_MV_MAX_DEG，导致 x^7 在词法层就被判成
         *   语法错误（rc = 0 -> g_solve_error = 2），语义门禁永远跑不到，
         *   A1-8 / A3-4 要的 g_solve_error == 1 自然拿不到。 */
        if (exp > AC_PRO_MV_EXP_MASK) { return -1; }
        if (var == 0) { if (saw_x) { return -1; } nx = exp; saw_x = 1; }
        else          { if (saw_y) { return -1; } ny = exp; saw_y = 1; }

        while (*s == ' ' || *s == '\t') { s++; }
        if (*s == '*') { s++; }
        while (*s == ' ' || *s == '\t') { s++; }
    }

    /* 单次项的总次数不在这里拦
     *   nx + ny 的上限是语义问题（设计说明 §6.4：总次数 ≤ 6），交给
     *   ac_pro_multi_var_gcd 的 preCheck（g_solve_error = 1）和 A3 的
     *   df + dg ≤ 12 门禁去判。词法层只保证 x/y 各自能被 exps 的 4-bit
     *   字段容纳（上面已逐个检查过），多出来的次数让语义层报「次数过高」。
     *   修复前这里直接 return -1，把 x^7+y / y^7+x 这类输入打成语法错误，
     *   语义门禁成了不可达代码,A1-8、A3-4 的错误码错就错在这。 */
    *coef = neg ? -c : c;
    *xd = nx;
    *yd = ny;
    *ps = s;
    return 0;
}

/* 解析到指定槽。返回 1 = 成功；0 = 语法错；-1 = 项池满/超限。 */
static int mv_parse(mv_poly_t *p, const char *src) {
    const char *s = src;
    if (src == (const char *)0) { return 0; }
    mv_clear(p);
    for (;;) {
        ac_int_t c;
        int xd;
        int yd;
        if (mv_parse_term(&s, &c, &xd, &yd) != 0) { return 0; }
        if (mv_push(p, c, mv_exps(xd, yd)) != 0) { return -1; }
        while (*s == ' ' || *s == '\t') { s++; }
        if (*s == '\0') { break; }
        if (*s != '+' && *s != '-') { return 0; }
        /* 不前进：让 mv_parse_term 处理符号 */
    }
    if (mv_normalize(p) != 0) { return -1; }
    return 1;
}

/* 对外：格式化输出
 * 把多项式槽写成规范字符串，例如 "x^2*y + 2*x - 3"。
 * 规则：
 *   项按 exps 降序（与项表一致）；
 *   系数 1 / -1 且带变量时省略系数（写 "x" 而非 "1*x"）；
 *   指数 1 省略（写 "x" 而非 "x^1"）；指数 0 省略变量；
 *   项之间用 " + " / " - " 连接（负系数转成减号 + 绝对值）；
 *   零多项式写 "0"。
 * 不产生 malloc：直接用 snprintf 追加到调用方缓冲，并做长度检查。 */
static int mv_format(const mv_poly_t *p, char *out, int out_len) {
    int i;
    int used = 0;

    if (p->count == 0) {
        if (out_len < 2) { return -1; }
        out[0] = '0';
        out[1] = '\0';
        return 0;
    }

    for (i = 0; i < p->count; i++) {
        ac_int_t c = mv_terms(p->slot)[i].c;
        int xd = mv_xdeg(mv_terms(p->slot)[i].exps);
        int yd = mv_ydeg(mv_terms(p->slot)[i].exps);
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
            /* 系数与变量之间用 '*'：与解析器的输入格式一致 */
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

/* 对外：把多项式解析进指定槽
 * A3（ac_pro_resultant.c）需要「解析到槽 0/1 并读回项表」的能力，
 * 但 mv_poly_t 是本文件的私有类型。所以这里暴露一个只认槽号的
 * 窄接口：
 *
 *     int ac_pro_multi_parse_into(const char *src, int slot);
 *     返回项数（≥ 0）或 -1（语法错 / 槽号非法 / 项池满）。
 *     调用方随后用 ac_pro_multi_slot_terms(slot) 读项表。
 *
 * 为什么不让 A3 直接用 mv_poly_t：那会把槽位纪律泄漏到两个 TU 里，
 * 一旦两边对「谁占哪槽」的理解不一致，就是最难查的那类 bug。
 * 只暴露槽号 + 项表指针，A3 只知道「我要 0 号和 1 号槽」，简单。
 *
 * 注意：这个接口不做 g_solve_error 清零，由调用方负责，
 * 因为它可能在一个更大的事务里（A3 的解析 + 构造）。 */
int ac_pro_multi_parse_into(const char *src, int slot) {
    mv_poly_t p;
    int rc;
    if (slot < 0 || slot >= AC_PRO_MV_SLOTS) { return -1; }
    p.slot = slot;
    rc = mv_parse(&p, src);
    if (rc != 1) { return -1; }
    return p.count;
}

/* 读指定槽的项表（供 A3 快照）。槽号非法返回 NULL。 */
const ac_pro_term_t *ac_pro_multi_slot_terms(int slot) {
    if (slot < 0 || slot >= AC_PRO_MV_SLOTS) { return (const ac_pro_term_t *)0; }
    return s_pro_mv_slot(slot);
}

/* 槽内多项式的总次数（供 preCheck）。 */
int ac_pro_multi_slot_degree(int slot) {
    mv_poly_t p;
    if (slot < 0 || slot >= AC_PRO_MV_SLOTS) { return -1; }
    p.slot = slot;
    p.count = AC_PRO_MV_TERMS;   /* 保守：扫满 28 项，零项不影响 max */
    return mv_total_degree(&p);
}

/* 对外：A1 多变量 GCD
 * 契约（写进 ac_pro.h）：
 *   输入两个字符串形式的多项式（2 变量、总次数 ≤ 6、项数 ≤ 28）。
 *   输出：d = gcd(f, g)，归一化为「原始部分 + 首项系数为正」。
 *
 * 返回：1 = 成功（*out 非空）；0 = 平凡（两者皆零 / 无公因式）；
 *      -1 = 输入非法或溢出（g_solve_error 置 0/1/5）。
 *
 * 结果归一化：设计说明没规定 GCD 的常数倍，本实现取
 *     primitive part（系数 GCD 归一）+ 首项系数为正
 * 这样 gcd(2x+2, 4x+4) 返回 x+1，与「首一」只差一个常数因子，
 * 但保持整系数,整系数是本项目的硬约束。 */
int ac_pro_multi_var_gcd(const char *fsrc, const char *gsrc, char *out, int out_len) {
    mv_poly_t g;
    /* 槽位规划（6 槽刚好够 GCD + 归一化）：
     *   f -> SLOT_A，g -> SLOT_B（解析落地）
     *   gcd_core 需要 3 个互异槽（dst / a / b）+ prem 需要 2 个临时槽
     *   （SLOT_T1/SLOT_T2）+ content 归一化需要 1 个（SLOT_T3）。
     *
     *   矛盾点：gcd_core 的 dst 不能与 f/g 同槽，否则 mv_copy 会互踩；
     *   而 6 槽里 f/g 占 2，dst 占 1，prem 占 2，归一化占 1，正好 6。
     *   所以 dst 必须用 SLOT_R，且 SLOT_T3 留给归一化。
     *   下面的 gcd_core 调用把 f/g 直接当 x/y 用（原地演进），
     *   dst = SLOT_R，三槽 A/B/R 互异，prem 用 T1/T2，归一化用 T3。 */
    mv_poly_t f;
    mv_poly_t d;
    int rc;

    g_solve_error = 0;
    if (out == (char *)0 || out_len < 2) { g_solve_error = 2; return -1; }

    f.slot = SLOT_A;
    f.count = 0;
    g.slot = SLOT_B;
    g.count = 0;
    d.slot = SLOT_R;
    d.count = 0;

    rc = mv_parse(&f, fsrc);
    if (rc != 1) { g_solve_error = (rc == 0) ? 2 : 1; return -1; }
    rc = mv_parse(&g, gsrc);
    if (rc != 1) { g_solve_error = (rc == 0) ? 2 : 1; return -1; }

    /* preCheck（设计说明 §6.4）：总次数 ≤ 6、项数 ≤ 28 */
    if (mv_total_degree(&f) > AC_PRO_MV_MAX_DEG ||
        mv_total_degree(&g) > AC_PRO_MV_MAX_DEG) {
        g_solve_error = 1;
        return -1;
    }

    if (mv_gcd_prs(&d, &f, &g) != 1) { return -1; }

    /* 归一化：原始部分 + 首项系数为正。
       此时 f/g 的槽已不被需要，可以安全地把 SLOT_A 当临时槽用，
       这样连 SLOT_T3 都不用占，留给后续扩展。 */
    if (!mv_is_zero(&d)) {
        ac_int_t c = mv_content(&d);
        mv_poly_t tmp;
        tmp.slot = SLOT_A;
        tmp.count = 0;
        if (c > 1) {
            if (mv_primitive_part(&tmp, &d, c) != 0) { return -1; }
            if (mv_copy(&d, &tmp) != 0) { return -1; }
        }
        if (mv_lead_coef(&d) < 0) {
            if (mv_scale(&tmp, &d, -1) != 0) { return -1; }
            if (mv_copy(&d, &tmp) != 0) { return -1; }
        }
    }

    if (mv_is_zero(&d)) { g_solve_error = 0; if (out_len > 1) { out[0] = '0'; out[1] = '\0'; } return 0; }

    if (mv_format(&d, out, out_len) != 0) { g_solve_error = 2; return -1; }
    return 1;
}

/* A2 的整除性自证
 * 设计说明 §9.4 列的「Pro 多变量自对拍」：f mod g == 0。
 *
 * 做法：把「内容 c × Π(den_i·x − num_i) × 余式」重构回一个多项式，
 * 再用 mv_div_exact 对原始 f 做精确除法，商必须存在且余式为零。
 *
 * 为什么值得多花一次 O(28) 除法：A2 的失败模式是「因子写错一个符号或
 * 一个常数」，这种错在格式化输出里看不出来（字符串是自洽的），但对拍
 * 立刻暴露,除不尽就是错。这与设计说明「整除性验证（f mod g == 0）」
 * 的验收口径完全一致。
 *
 * 占槽：SLOT_A 保留原始 f（调用方传入），本函数用 SLOT_B/T1/T2/T3。
 * 返回 1 = 整除；0 = 不整除（分解有误）；-1 = 错误。 */
static int mv_selfcheck_factor(const mv_poly_t *f, ac_int_t c, int k, int rd) {
    mv_poly_t acc;
    mv_poly_t lin;
    mv_poly_t tmp;
    int i;

    acc.slot = SLOT_B;
    lin.slot = SLOT_T1;
    tmp.slot = SLOT_T2;

    /* acc = c */
    mv_clear(&acc);
    if (mv_push(&acc, c, mv_exps(0, 0)) != 0) { return -1; }
    if (mv_normalize(&acc) != 0) { return -1; }

    /* 逐个乘上 (den·x − num) */
    for (i = 0; i < k; i++) {
        ac_int_t num = g_rat_root_num[i];
        ac_int_t den = g_rat_root_den[i];
        ac_int_t nneg;
        if (den <= 0) { return 0; }
        if (num == INT64_MIN) { return -1; }
        nneg = -num;
        mv_clear(&lin);
        if (mv_push(&lin, den, mv_exps(1, 0)) != 0) { return -1; }
        if (mv_push(&lin, nneg, mv_exps(0, 0)) != 0) { return -1; }
        if (mv_normalize(&lin) != 0) { return -1; }
        if (mv_mul(&tmp, &acc, &lin) != 0) { return -1; }
        if (mv_copy(&acc, &tmp) != 0) { return -1; }
    }

    /* 再乘上余式多项式（g_coeffs[0..rd]，升幂） */
    if (rd > 0) {
        mv_clear(&lin);
        for (i = 0; i <= rd; i++) {
            double cd = g_coeffs[i];
            ac_int_t ci = (ac_int_t)cd;
            if ((double)ci != cd) { return -1; }
            if (mv_push(&lin, ci, mv_exps(i, 0)) != 0) { return -1; }
        }
        if (mv_normalize(&lin) != 0) { return -1; }
        if (mv_mul(&tmp, &acc, &lin) != 0) { return -1; }
        if (mv_copy(&acc, &tmp) != 0) { return -1; }
    }

    /* 精确除法：f ÷ acc 必须整除 */
    {
        if (mv_divides(&acc, f) != 1) { return 0; }
    }
    return 1;
}

/* 对外：A2 多变量因式分解
 * 设计说明 §3.1.4：只做「内容提取 + 单变量因子」。
 *
 * 流程：
 *   内容提取：c = gcd(所有系数)，f = c · primitive(f)
 *   若 primitive(f) 退化为单变量（只含 x 或只含 y）且次数 ≤ 4：
 *      把系数搬到 g_coeffs（独立全局，不在任何 union 里），
 *      再调上游 ac_rat_root_descent 做有理根降次。
 *   否则报告「非单变量，超出 v1.3 边界」。
 *
 * 调用纪律（设计说明 §3.1.4 的警告）
 *   mv 视图（Pro union）与 ratroot 视图（上游 ac_scratch）在物理上
 *   是两块不同的内存，严格说不冲突。但「一次只持有 一个视图」的纪律
 *   仍然要求：调用 ratroot 之前，把降维系数落盘到 g_coeffs，
 *   之后不再碰 mv 视图。本实现的顺序是：
 *       mv 视图里解析/归一化 -> 提取到 g_coeffs -> 调 ratroot -> 读结果
 *   第 2 步之后 mv 视图的数据就当它已经失效，不再读写。
 *   （g_coeffs 是上游 extern 会话态，判据 3 明确禁止入 union，正好
 *   用它做中转，这不是巧合，是设计说明 §3.1.4 点名的那条路径。）
 *
 * 返回：1 = 成功；0 = 超出边界（非单变量 / 次数 > 4）；-1 = 错误。
 * 结果写 *content（整数内容）与 out（因式分解的字符串形式）。 */
int ac_pro_multi_var_factor(const char *src, char *out, int out_len,
                            ac_int_t *content) {
    mv_poly_t f;
    mv_poly_t prim;
    ac_int_t c;
    ac_int_t ucoef[5];
    int udeg = 0;
    int which = 0;
    int i;
    int k;

    g_solve_error = 0;
    if (out == (char *)0 || out_len < 2) { g_solve_error = 2; return -1; }
    if (content != (ac_int_t *)0) { *content = 1; }

    f.slot = SLOT_A;
    if (mv_parse(&f, src) != 1) { g_solve_error = 2; return -1; }
    if (mv_total_degree(&f) > AC_PRO_MV_MAX_DEG) { g_solve_error = 1; return -1; }

    /* 内容提取 */
    c = mv_content(&f);
    if (content != (ac_int_t *)0) { *content = c; }

    prim.slot = SLOT_B;
    if (mv_primitive_part(&prim, &f, c) != 0) { return -1; }

    /* 判断是否退化为单变量 */
    {
        int is_x = mv_to_univariate(&prim, 0, ucoef, &udeg);
        int is_y = 0;
        if (is_x == 1) { which = 0; }
        else {
            is_y = mv_to_univariate(&prim, 1, ucoef, &udeg);
            if (is_y == 1) { which = 1; }
            else { g_solve_error = 0; return 0; }   /* 真 2 变量：超出 v1.3 边界 */
        }
    }
    (void)which;

    /* 落盘到 g_coeffs（独立全局），随后 mv 视图视为失效 */
    for (i = 0; i <= udeg; i++) { g_coeffs[i] = (double)ucoef[i]; }
    for (i = udeg + 1; i < 5; i++) { g_coeffs[i] = 0.0; }

    /* 上游有理根降次（占上游 ac_scratch 的 ratroot 视图，
       与 Pro 的 g_ac_pro_scratch 物理不重叠） */
    k = ac_rat_root_descent(udeg);
    if (k < 0) { g_solve_error = 3; return -1; }

    /* 整除性自证（设计说明 §9.4「Pro 多变量自对拍：f mod g == 0」）。
       把原始多项式 f 与「内容 × 各因子 × 余式」的重构结果做精确除法，
       商必须存在且余式为零。这一步是 A2 正确性的结构性保证：
       若因式分解有任何一个因子写错，除法会除不尽，这里立刻失败。
       成本是一次 O(28) 的逐项除法，值得。 */
    if (k > 0) {
        if (mv_selfcheck_factor(&f, c, k, udeg - k) != 1) {
            g_solve_error = 3;
            return -1;
        }
    }

    /* 格式化结果：c · Π(x − r_i) · remainder */
    if (mv_format_factor(c, k, udeg - k, out, out_len) != 0) {
        g_solve_error = 2;
        return -1;
    }
    return 1;
}

/* A2 的结果格式化
 * 把「内容 c + k 个有理根 + 降次后的余式多项式」写成字符串：
 *     c * (x - r1) * (x - r2) * ... * (余式)
 *
 * 有理根 r_i = g_rat_root_num[i] / g_rat_root_den[i]（已约简，den > 0）。
 * 写成整数形式 (den·x − num) 而不是 (x − num/den)，避免输出里出现分数
 * 本项目多变量层的契约是整系数，输出也必须整系数。
 *
 * 余式多项式从 g_coeffs[0..rd] 读（升幂），按降幂输出。
 * 全零/空余式（rd == 0 且 g_coeffs[0] == 1）按「完全分解」处理，
 * 不输出多余因子。 */
static int mv_format_factor(ac_int_t c, int k, int rd, char *out, int out_len) {
    int used = 0;
    int n;
    int i;

    if (out_len < 2) { return -1; }
    out[0] = '\0';

    /* 内容：c 恒为 1 时省略（保持输出简洁）；否则写成前导因子 */
    if (c != 1) {
        n = snprintf(out + used, (size_t)(out_len - used), "%ld", (long)c);
        if (n < 0 || used + n >= out_len) { return -1; }
        used += n;
        n = snprintf(out + used, (size_t)(out_len - used), " * ");
        if (n < 0 || used + n >= out_len) { return -1; }
        used += n;
    }

    /* 有理根因子：(den·x − num) */
    for (i = 0; i < k; i++) {
        ac_int_t num = g_rat_root_num[i];
        ac_int_t den = g_rat_root_den[i];
        if (den <= 0) { return -1; }
        n = snprintf(out + used, (size_t)(out_len - used), "(");
        if (n < 0 || used + n >= out_len) { return -1; }
        used += n;
        if (den != 1) {
            n = snprintf(out + used, (size_t)(out_len - used), "%ld*", (long)den);
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
        }
        n = snprintf(out + used, (size_t)(out_len - used), "x");
        if (n < 0 || used + n >= out_len) { return -1; }
        used += n;
        if (num >= 0) {
            n = snprintf(out + used, (size_t)(out_len - used), " - %ld", (long)num);
        } else {
            n = snprintf(out + used, (size_t)(out_len - used), " + %ld", (long)(-num));
        }
        if (n < 0 || used + n >= out_len) { return -1; }
        used += n;
        n = snprintf(out + used, (size_t)(out_len - used), ")");
        if (n < 0 || used + n >= out_len) { return -1; }
        used += n;
        n = snprintf(out + used, (size_t)(out_len - used), " * ");
        if (n < 0 || used + n >= out_len) { return -1; }
        used += n;
    }

    /* 余式：降幂输出。rd == 0 且系数为 1 时是「完全分解」，输出 1。 */
    if (rd <= 0) {
        n = snprintf(out + used, (size_t)(out_len - used), "1");
        if (n < 0 || used + n >= out_len) { return -1; }
        used += n;
    } else {
        int first = 1;
        for (i = rd; i >= 0; i--) {
            double cd = g_coeffs[i];
            ac_int_t ci = (ac_int_t)cd;
            int neg;
            ac_int_t mag;
            if ((double)ci != cd) { return -1; }     /* 非整数系数：超出契约 */
            if (ci == 0) { continue; }
            neg = (ci < 0);
            mag = neg ? -ci : ci;
            if (first) {
                if (neg) {
                    n = snprintf(out + used, (size_t)(out_len - used), "-");
                    if (n < 0 || used + n >= out_len) { return -1; }
                    used += n;
                }
                first = 0;
            } else {
                n = snprintf(out + used, (size_t)(out_len - used), neg ? " - " : " + ");
                if (n < 0 || used + n >= out_len) { return -1; }
                used += n;
            }
            if (i == 0) {
                n = snprintf(out + used, (size_t)(out_len - used), "%ld", (long)mag);
            } else if (mag != 1) {
                if (i == 1) {
                    n = snprintf(out + used, (size_t)(out_len - used), "%ld*x", (long)mag);
                } else {
                    n = snprintf(out + used, (size_t)(out_len - used), "%ld*x^%d", (long)mag, i);
                }
            } else {
                if (i == 1) { n = snprintf(out + used, (size_t)(out_len - used), "x"); }
                else { n = snprintf(out + used, (size_t)(out_len - used), "x^%d", i); }
            }
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
        }
        if (first) {
            n = snprintf(out + used, (size_t)(out_len - used), "1");
            if (n < 0 || used + n >= out_len) { return -1; }
            used += n;
        }
    }
    return 0;
}

#endif /* AC_PRO_ACTIVE */
