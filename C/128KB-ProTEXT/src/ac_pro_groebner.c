/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_pro_groebner.c — 2 变量 Groebner 基:lex 序,M4GB 逐对归约

   v2.2.0 方向⑥「2 变量 Groebner 基」的完整实现:lex 序(x > y),输入总次数
   ≤ 4,M4GB 逐对归约路线。见设计说明 §3 功能③(文献 P5:Makarim & Stevens,
   M4GB, ISSAC 2017,逐对处理 critical pair,避免 F4 大矩阵,正是小内存该走
   的路)。

   算法(全部显式循环,无递归 / malloc / VLA / static):
   解析两生成元进 gb 视图槽 0/1(自备解析器,语法与 A1/A2 多变量层一致;
   不借道 mv 视图,一次调用只持有一个视图)。

   critical pair 队列 = 当前活跃基的全对(Gebauer-Möller 意义下的重建式队列:
   每次基变更后整体重建,从而「终结集的每一对都在终结版本上处理过」,这是
   逐对处理正确性的关键不变式)。重建时跳过「首项互素」的对(Buchberger
   第一判据:lcm = 乘积 ⟹ S-多项式必归约到零,经典定理)。

   逐对弹出:S-多项式 = lc_j·(lcm/lt_i)·f_i − lc_i·(lcm/lt_j)·f_j 在稠密
   工作区合成(首项恰好整系数相消),随后稠密伪除归约:lex 降序扫 81 格,
   最大可除单项式 m 被
       dense ←= lc_g·dense − coef(m)·(m/lt_g)·g
   消去(伪除保持整系数;每步后提整数 content 控制膨胀,与 A1 prem 的
   primitive 归一同一纪律,实测 2 变量 ≤ 8 次规模下系数位宽远在 int64 内)。
   因为 m/lt_g·(g 的非首项)在 lex 下严格小于 m(单项式序平移不变性),消去
   只触碰 lex 更小的格子,单趟降序扫描即完成带余除法,被「跳过」的不可除
   首项留在余式。

   余式非零 → 规范化(content + 首项系数为正)→ 入基;余式为非零常数 →
   理想 = 全环,基坍缩为 {1}。

   每次入基后 interreduce:每个活跃元素用「被其余元素归约的余式」替换。
   该替换无条件保理想(f − r 落在其余元素生成的理想里),合法性不依赖
   「G 已是 GB」,这一点经 node 对照锚定:运行中途直接删除「首项被新元素
   整除」的旧元素(不经归约)会在 <x²+y²−1, xy−1> 上产生错误基(旧元素
   并非必然落在其余元素 + 新元素生成的理想里)。余式为零的元素判死,槽位
   回收。外层不动点循环保证终结时:首项两两互不整除(antichain)+ 尾项
   互不整除 + primitive 正首项系数 = reduced GB。

   队列空 → 输出。reduced GB 在固定单项式序下唯一,故与任何正确实现(含
   node 有理数独立核算,test 文件头)逐字一致。

   为什么归约走 81 格稠密而不是稀疏项表:单项式空间(总次数 ≤ 8)只有 81 格;
   稠密数组让「找最大可除单项式」成为两重降序循环(ex 降序 × ey 降序 =
   ep 降序 = lex 降序),无查找表,无排序,无合并逻辑,用 648 B 换掉一整类
   稀疏合并 bug。总次数 > 8 的格子恒为 0(两处写入前都有总次数熔断),扫描
   它们只是空转 81 格,规模下可忽略。

   次数上限的推导,为什么总次数 ≤ 8 是诚实的边界:输入总次数 ≤ 4 ⟹ 每变量
   指数 ≤ 4 ⟹ 两个首项的 lcm 每变量指数 ≤ 4(第一代 S-多项式每项总次数
   ≤ 8)。其后所有基元素都来自「总次数 ≤ 8 的余式」,归约只减不增。第二轮
   及以后 pair 的 lcm 理论上可达总次数 16(如 x⁸ 与 y⁸),此时总次数熔断
   AC_PRO_GB_EDEG 诚实中止,任务书划定的经典用例(node 实测 runMaxDeg ≤ 7)
   全部落在界内。

   溢出防护:所有系数 + − × 走上游 ac_safe_add / ac_safe_sub / ac_safe_mul,
   失败即返回 AC_PRO_GB_EOVER,绝不返回静默错值。INT64_MIN 取负在符号
   规范化处显式拒绝。

   守卫:TU 体整体包在 #if AC_PRO_ACTIVE 内;DEGRADED 构建下编成空对象,
   零 .bss,零 .text。错误码走返回值而非静态变量:任何静态对象 = .bss
   +32 B(16 B 段 + 16 B 链接对齐,实测钉死,见 ac_pro.h eig 节)。 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ac_pro.h"
#include "../../../C/128KB/src/ac_exact.h"

#if AC_PRO_ACTIVE

/* 单项式编码
 * ep = (ex << 4) | ey，ex/ey ∈ [0, AC_PRO_GB_MAX_DEG]（≤ 8，见
 * ac_pro_scratch.h 的 nibble 校验）。ep 的无符号比较 = lex x>y 比较
 * （ex 在高半字节）。稠密下标 idx = ex*9 + ey（行 = ex）。 */
#define GB_IDX(ex, ey) ((ex) * (AC_PRO_GB_MAX_DEG + 1) + (ey))

static int gb_ex_of(int ep) { return (ep >> 4) & 0x0F; }
static int gb_ey_of(int ep) { return ep & 0x0F; }
static int gb_mk_ep(int ex, int ey) { return (ex << 4) | ey; }

/* 安全算术包装（失败 = EOVER） */
static int gb_mul(int64_t a, int64_t b, int64_t *o) {
    if (!ac_safe_mul(a, b, o)) { return AC_PRO_GB_EOVER; }
    return 1;
}
static int gb_add(int64_t a, int64_t b, int64_t *o) {
    if (!ac_safe_add(a, b, o)) { return AC_PRO_GB_EOVER; }
    return 1;
}
static int gb_sub(int64_t a, int64_t b, int64_t *o) {
    if (!ac_safe_sub(a, b, o)) { return AC_PRO_GB_EOVER; }
    return 1;
}

/* 槽位读取 */
/* 首项 ep；空槽返回 -1（调用方保证只在活槽上调用） */
static int gb_lm(int s) {
    if (s_pro_gb_cnt(s) <= 0) { return -1; }
    return s_pro_gb_ep(s, 0);
}

static int gb_live(int s) {
    return s >= 0 && s < AC_PRO_GB_BASIS && !s_pro_gb_dead(s) && s_pro_gb_cnt(s) > 0;
}

/* 解析（语法与 A1/A2 多变量层一致）
 * 项用 + / - 分隔；每项形如 [系数][*]x[^n][*]y[^m]，变量顺序可换、
 * 可缺省，系数缺省 1，指数缺省 1。不支持括号 / 除法 / 负指数。
 * 与 mv 层解析器的差异：这里在词法层直接执行任务书的次数语义
 * （单变量指数 > 8 或项总次数 > 4 → EDEG），因为 gb 的输入边界就是
 * 语义边界，无需 mv 层的「词法 / 语义」两级拆分。
 *
 * 返回 1 = 成功；其余 = AC_PRO_GB_* 错误码。 */
static int gb_parse_term(const char **ps, int *out_neg, int64_t *out_c,
                         int *out_nx, int *out_ny) {
    const char *s = *ps;
    /* 系数必须从 0 起累加（c = c*10 + digit）：从 1 起会把 "1" 算成 11。
       隐式系数 1 由下方 saw_coef 单独负责,两件事不能混（A1 层实测
       踩过的坑，见 ac_pro_multi.c:1887）。 */
    int64_t c = 0;
    int nx = 0;
    int ny = 0;
    int saw_coef = 0;
    int saw_x = 0;
    int saw_y = 0;
    int neg = 0;

    while (*s == ' ' || *s == '\t') { s++; }
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    /* 符号后再跳一次空白："- 1" 不跳会把系数循环卡在空格上（A1 层实测坑） */
    while (*s == ' ' || *s == '\t') { s++; }

    while (*s >= '0' && *s <= '9') {
        if (c > (INT64_MAX - 9) / 10) { return AC_PRO_GB_EOVER; }
        c = c * 10 + (*s - '0');
        saw_coef = 1;
        s++;
    }
    if (!saw_coef) { c = 1; }
    while (*s == ' ' || *s == '\t') { s++; }
    if (*s == '*') { s++; }
    while (*s == ' ' || *s == '\t') { s++; }

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
            while (*s >= '0' && *s <= '9') {
                if (exp > 1000) { return AC_PRO_GB_EDEG; }   /* 指数离谱：早退 */
                exp = exp * 10 + (*s - '0');
                s++;
            }
        }
        if (exp > AC_PRO_GB_MAX_DEG) { return AC_PRO_GB_EDEG; }
        if (var == 0) { if (saw_x) { return AC_PRO_GB_EARG; } nx = exp; saw_x = 1; }
        else          { if (saw_y) { return AC_PRO_GB_EARG; } ny = exp; saw_y = 1; }
        while (*s == ' ' || *s == '\t') { s++; }
        if (*s == '*') { s++; }
        while (*s == ' ' || *s == '\t') { s++; }
    }

    if (nx + ny > AC_PRO_GB_IN_DEG) { return AC_PRO_GB_EDEG; }
    *out_neg = neg;
    *out_c = c;
    *out_nx = nx;
    *out_ny = ny;
    *ps = s;
    return 1;
}

/* 解析 src 进槽 s 并归一化（排序 / 合并 / 去零）。 */
static int gb_parse_into(const char *src, int s) {
    const char *p = src;
    int n = 0;

    while (*p == ' ' || *p == '\t') { p++; }
    if (*p == '\0') { return AC_PRO_GB_EARG; }           /* 空输入 */

    for (;;) {
        int rc;
        int neg = 0;
        int64_t c = 0;
        int nx = 0;
        int ny = 0;

        rc = gb_parse_term(&p, &neg, &c, &nx, &ny);
        if (rc != 1) { return rc; }
        if (c != 0) {
            if (neg) {
                if (c == INT64_MIN) { return AC_PRO_GB_EOVER; }
                c = -c;
            }
            if (n >= AC_PRO_GB_TERMS) { return AC_PRO_GB_ETERM; }
            s_pro_gb_coef(s, n) = c;
            s_pro_gb_ep(s, n) = (uint8_t)gb_mk_ep(nx, ny);
            n++;
        }
        while (*p == ' ' || *p == '\t') { p++; }
        if (*p == '\0') { break; }
        if (*p != '+' && *p != '-') { return AC_PRO_GB_EARG; }
        /* 不前进：让 gb_parse_term 处理符号 */
    }

    /* 归一化：按 ep 降序插入排序 + 同类项合并（safe add）+ 去零 */
    {
        int i;
        int j;
        for (i = 1; i < n; i++) {
            int64_t kc = s_pro_gb_coef(s, i);
            int ke = s_pro_gb_ep(s, i);
            j = i - 1;
            while (j >= 0 && s_pro_gb_ep(s, j) < ke) {
                s_pro_gb_coef(s, j + 1) = s_pro_gb_coef(s, j);
                s_pro_gb_ep(s, j + 1) = s_pro_gb_ep(s, j);
                j--;
            }
            s_pro_gb_coef(s, j + 1) = kc;
            s_pro_gb_ep(s, j + 1) = (uint8_t)ke;
        }
    }
    {
        int out = 0;
        int i = 0;
        while (i < n) {
            int64_t sum = s_pro_gb_coef(s, i);
            int e = s_pro_gb_ep(s, i);
            int k = i + 1;
            int rc;
            while (k < n && s_pro_gb_ep(s, k) == e) {
                rc = gb_add(sum, s_pro_gb_coef(s, k), &sum);
                if (rc != 1) { return rc; }
                k++;
            }
            if (sum != 0) {
                s_pro_gb_coef(s, out) = sum;
                s_pro_gb_ep(s, out) = (uint8_t)e;
                out++;
            }
            i = k;
        }
        s_pro_gb_cnt(s) = (uint8_t)out;
    }
    return 1;
}

/* 稠密工作区基础操作 */

static void gb_dense_clear(void) {
    int i;
    for (i = 0; i < AC_PRO_GB_DENSE; i++) { s_pro_gb_dense[i] = 0; }
}

/* 槽 s 散射进稠密区。槽内所有项都在存储时被总次数熔断约束过
   （总次数 ≤ 8 ⟹ 每变量 ≤ 8 ⟹ idx 在界内），防御性检查兜底。 */
static int gb_dense_scatter(int s) {
    int t;
    gb_dense_clear();
    for (t = 0; t < s_pro_gb_cnt(s); t++) {
        int e = s_pro_gb_ep(s, t);
        int ex = gb_ex_of(e);
        int ey = gb_ey_of(e);
        if (ex + ey > AC_PRO_GB_MAX_DEG) { return AC_PRO_GB_EINTERNAL; }
        s_pro_gb_dense[GB_IDX(ex, ey)] = s_pro_gb_coef(s, t);
    }
    return 1;
}

static int gb_dense_is_zero(void) {
    int i;
    for (i = 0; i < AC_PRO_GB_DENSE; i++) {
        if (s_pro_gb_dense[i] != 0) { return 0; }
    }
    return 1;
}

/* 非零常数：只有 (0,0) 格非零 */
static int gb_dense_is_const(void) {
    int i;
    if (s_pro_gb_dense[0] == 0) { return 0; }
    for (i = 1; i < AC_PRO_GB_DENSE; i++) {
        if (s_pro_gb_dense[i] != 0) { return 0; }
    }
    return 1;
}

/* 全区提整数 content（gcd > 1 时就地除尽,gcd 由构造保证整除） */
static int gb_dense_strip(void) {
    int64_t g = 0;
    int i;
    for (i = 0; i < AC_PRO_GB_DENSE; i++) {
        if (s_pro_gb_dense[i] != 0) { g = ac_gcd(g, s_pro_gb_dense[i]); }
    }
    if (g > 1) {
        for (i = 0; i < AC_PRO_GB_DENSE; i++) {
            if (s_pro_gb_dense[i] != 0) { s_pro_gb_dense[i] /= g; }
        }
    }
    return 1;
}

/* 稠密伪除归约（M4GB 逐对路线的核心）
 * lex 降序扫 81 格；最大可除单项式 m 被
 *     dense ← lc_g·dense − coef(m)·(m/lt_g)·g
 * 消去。m/lt_g·(g 的非首项) 在 lex 下严格小于 m（单项式序平移不变性），
 * 故消去只触碰 lex 更小的格子，单趟扫描即带余除法，不可除首项留在
 * 余式。每步后提 content 控制伪除系数膨胀。exclude ≥ 0 时跳过该槽
 * （interreduce 用；-1 = 对全部活槽归约）。 */
static int gb_dense_reduce(int exclude) {
    int ex;
    int ey;

    for (ex = AC_PRO_GB_MAX_DEG; ex >= 0; ex--) {
        for (ey = AC_PRO_GB_MAX_DEG; ey >= 0; ey--) {
            int idx = GB_IDX(ex, ey);
            int64_t c = s_pro_gb_dense[idx];
            int s;
            int hit = 0;

            if (c == 0) { continue; }

            for (s = 0; s < AC_PRO_GB_BASIS && !hit; s++) {
                int lme;
                int lx;
                int ly;
                int64_t lcg;
                int rx;
                int ry;
                int t;
                int p;
                int rc;

                if (s == exclude || !gb_live(s)) { continue; }
                lme = gb_lm(s);
                lx = gb_ex_of(lme);
                ly = gb_ey_of(lme);
                if (ex < lx || ey < ly) { continue; }        /* lt_g ∤ m */

                /* 伪除：整区乘 lc_g，再减 coef(m)·(m/lt_g)·g */
                lcg = s_pro_gb_coef(s, 0);
                for (p = 0; p < AC_PRO_GB_DENSE; p++) {
                    if (s_pro_gb_dense[p] != 0) {
                        int64_t v;
                        rc = gb_mul(s_pro_gb_dense[p], lcg, &v);
                        if (rc != 1) { return rc; }
                        s_pro_gb_dense[p] = v;
                    }
                }
                rx = ex - lx;
                ry = ey - ly;
                for (t = 0; t < s_pro_gb_cnt(s); t++) {
                    int te = s_pro_gb_ep(s, t);
                    int tx = gb_ex_of(te) + rx;
                    int ty = gb_ey_of(te) + ry;
                    int64_t prod;
                    int64_t cur;
                    if (tx + ty > AC_PRO_GB_MAX_DEG) { return AC_PRO_GB_EDEG; }
                    rc = gb_mul(c, s_pro_gb_coef(s, t), &prod);
                    if (rc != 1) { return rc; }
                    cur = s_pro_gb_dense[GB_IDX(tx, ty)];
                    rc = gb_sub(cur, prod, &s_pro_gb_dense[GB_IDX(tx, ty)]);
                    if (rc != 1) { return rc; }
                }
                /* (ex,ey) 格：c·lc_g − c·lc_g = 0 精确归零 */
                rc = gb_dense_strip();
                if (rc != 1) { return rc; }
                hit = 1;
            }
            /* 不可除：该单项式留在余式，继续向 lex 更小扫描 */
        }
    }
    return 1;
}

/* 稠密区 → 槽 s（lex 降序收集 + content/符号规范化）。
 * 返回 1 = 已写入非零多项式；0 = 零多项式（槽清空）；<0 = 错误码。 */
static int gb_dense_store(int s) {
    int ex;
    int ey;
    int n = 0;
    int64_t g = 0;
    int i;

    for (ex = AC_PRO_GB_MAX_DEG; ex >= 0; ex--) {
        for (ey = AC_PRO_GB_MAX_DEG; ey >= 0; ey--) {
            int64_t c = s_pro_gb_dense[GB_IDX(ex, ey)];
            if (c == 0) { continue; }
            if (n >= AC_PRO_GB_TERMS) { return AC_PRO_GB_ETERM; }
            s_pro_gb_coef(s, n) = c;
            s_pro_gb_ep(s, n) = (uint8_t)gb_mk_ep(ex, ey);
            n++;
        }
    }
    s_pro_gb_cnt(s) = (uint8_t)n;
    if (n == 0) { return 0; }

    for (i = 0; i < n; i++) { g = ac_gcd(g, s_pro_gb_coef(s, i)); }
    if (g > 1) {
        for (i = 0; i < n; i++) { s_pro_gb_coef(s, i) /= g; }
    }
    if (s_pro_gb_coef(s, 0) < 0) {
        for (i = 0; i < n; i++) {
            if (s_pro_gb_coef(s, i) == INT64_MIN) { return AC_PRO_GB_EOVER; }
            s_pro_gb_coef(s, i) = -s_pro_gb_coef(s, i);
        }
    }
    return 1;
}

/* S-多项式合成（稠密区）
 * S = lc_j·(lcm/lt_i)·f_i − lc_i·(lcm/lt_j)·f_j,首项在 lcm 处
 * 精确相消（lc_j·lc_i − lc_i·lc_j = 0）。每个乘积目标先做总次数
 * 熔断再写入（总次数 > 8 ⟹ EDEG）。 */
static int gb_spoly(int i, int j) {
    int li = gb_lm(i);
    int lj = gb_lm(j);
    int lx = gb_ex_of(li);
    int ly = gb_ey_of(li);
    int jx = gb_ex_of(lj);
    int jy = gb_ey_of(lj);
    int lcx = (lx > jx) ? lx : jx;
    int lcy = (ly > jy) ? ly : jy;
    int64_t lci = s_pro_gb_coef(i, 0);
    int64_t lcj = s_pro_gb_coef(j, 0);
    int t;
    int rc;

    gb_dense_clear();

    for (t = 0; t < s_pro_gb_cnt(i); t++) {
        int te = s_pro_gb_ep(i, t);
        int tx = gb_ex_of(te) + (lcx - lx);
        int ty = gb_ey_of(te) + (lcy - ly);
        int64_t prod;
        int64_t cur;
        if (tx + ty > AC_PRO_GB_MAX_DEG) { return AC_PRO_GB_EDEG; }
        rc = gb_mul(lcj, s_pro_gb_coef(i, t), &prod);
        if (rc != 1) { return rc; }
        cur = s_pro_gb_dense[GB_IDX(tx, ty)];
        rc = gb_add(cur, prod, &s_pro_gb_dense[GB_IDX(tx, ty)]);
        if (rc != 1) { return rc; }
    }
    for (t = 0; t < s_pro_gb_cnt(j); t++) {
        int te = s_pro_gb_ep(j, t);
        int tx = gb_ex_of(te) + (lcx - jx);
        int ty = gb_ey_of(te) + (lcy - jy);
        int64_t prod;
        int64_t cur;
        if (tx + ty > AC_PRO_GB_MAX_DEG) { return AC_PRO_GB_EDEG; }
        rc = gb_mul(lci, s_pro_gb_coef(j, t), &prod);
        if (rc != 1) { return rc; }
        cur = s_pro_gb_dense[GB_IDX(tx, ty)];
        rc = gb_sub(cur, prod, &s_pro_gb_dense[GB_IDX(tx, ty)]);
        if (rc != 1) { return rc; }
    }
    return 1;
}

/* 基变更：interreduce（替换式，无条件保理想）
 * 对每个活槽：散射 → 对「其余活槽」稠密归约 → 收集回原槽（零 → 判死，
 * 槽位回收）。替换保理想不依赖「G 已是 GB」；外层不动点循环消化
 * 「后面的槽收缩使前面槽的尾项变得可除」的级联（每轮要么死槽要么
 * 首项 lex 严格缩小，必终止）。终结时基是 reduced GB。 */
static int gb_interreduce_all(void) {
    int round;
    for (round = 0; round < 16; round++) {
        int changed = 0;
        int s;
        for (s = 0; s < AC_PRO_GB_BASIS; s++) {
            int rc;
            int64_t snap_c[AC_PRO_GB_TERMS];
            uint8_t snap_e[AC_PRO_GB_TERMS];
            int snap_n;
            int same;
            int t;
            if (!gb_live(s)) { continue; }
            /* 全项快照：不动点检测必须比较整张项表，只比项数 + 首项
               系数会漏掉「尾项变了但计数与首项未变」的改动，让循环
               提前退出、留下未约化干净的基。 */
            snap_n = s_pro_gb_cnt(s);
            for (t = 0; t < snap_n; t++) {
                snap_c[t] = s_pro_gb_coef(s, t);
                snap_e[t] = s_pro_gb_ep(s, t);
            }
            rc = gb_dense_scatter(s);
            if (rc != 1) { return rc; }
            rc = gb_dense_reduce(s);
            if (rc != 1) { return rc; }
            rc = gb_dense_store(s);
            if (rc < 0) { return rc; }
            if (rc == 0) {
                s_pro_gb_dead(s) = 1;          /* 被其余元素吸收：判死回收 */
                changed = 1;
            } else {
                same = (s_pro_gb_cnt(s) == snap_n);
                for (t = 0; same && t < snap_n; t++) {
                    if (s_pro_gb_coef(s, t) != snap_c[t] ||
                        s_pro_gb_ep(s, t) != snap_e[t]) {
                        same = 0;
                    }
                }
                if (!same) { changed = 1; }
            }
        }
        if (!changed) { return 1; }
    }
    return AC_PRO_GB_EINTERNAL;                /* 不动点 16 轮未收敛：防御熔断 */
}

/* pair 队列重建
 * 队列 = 当前活跃基的全对，跳过「首项互素」的对（Buchberger 第一判据：
 * lcm(lt_i, lt_j) = lt_i·lt_j ⟹ S-多项式归约到零,经典定理）。
 * 每次基变更后整体重建：终结集的每一对都在终结版本上被处理过。 */
static int gb_rebuild_pairs(void) {
    int i;
    int j;
    s_pro_gb_cp_n = 0;
    for (i = 0; i < AC_PRO_GB_BASIS; i++) {
        if (!gb_live(i)) { continue; }
        for (j = i + 1; j < AC_PRO_GB_BASIS; j++) {
            int li = gb_lm(i);
            int lj = gb_lm(j);
            if (!gb_live(j)) { continue; }
            /* 互素：min(ex) == 0 且 min(ey) == 0 */
            if ((gb_ex_of(li) < gb_ex_of(lj) ? gb_ex_of(li) : gb_ex_of(lj)) == 0 &&
                (gb_ey_of(li) < gb_ey_of(lj) ? gb_ey_of(li) : gb_ey_of(lj)) == 0) {
                continue;
            }
            if (s_pro_gb_cp_n >= AC_PRO_GB_PAIRS) { return AC_PRO_GB_EPAIR; }
            s_pro_gb_cp_a[s_pro_gb_cp_n] = (uint8_t)i;
            s_pro_gb_cp_b[s_pro_gb_cp_n] = (uint8_t)j;
            s_pro_gb_cp_n++;
        }
    }
    return 1;
}

/* 理想 = 全环：基坍缩为 {1}（占用槽 0，其余判死，队列清空） */
static void gb_collapse_one(void) {
    int s;
    for (s = 0; s < AC_PRO_GB_BASIS; s++) {
        s_pro_gb_dead(s) = (s != 0) ? 1 : 0;
        s_pro_gb_cnt(s) = 0;
    }
    s_pro_gb_coef(0, 0) = 1;
    s_pro_gb_ep(0, 0) = 0;
    s_pro_gb_cnt(0) = 1;
    s_pro_gb_n = 1;
    s_pro_gb_cp_n = 0;
}

/* Buchberger 主循环 */
static int gb_solve(void) {
    int guard = 0;
    int rc;
    int s;

    /* 常数输入短路：任一非零常数生成元 ⟹ 理想 = 全环 */
    for (s = 0; s < AC_PRO_GB_BASIS; s++) {
        if (gb_live(s) && s_pro_gb_cnt(s) == 1 && s_pro_gb_ep(s, 0) == 0) {
            gb_collapse_one();
            return 1;
        }
    }

    /* 两输入皆零（解析后全为空槽）：空基 */
    {
        int any = 0;
        for (s = 0; s < AC_PRO_GB_BASIS; s++) {
            if (gb_live(s)) { any = 1; }
        }
        if (!any) { return 1; }
    }

    rc = gb_interreduce_all();                 /* 归一化输入 + 清理同首项冗余 */
    if (rc != 1) { return rc; }
    rc = gb_rebuild_pairs();
    if (rc != 1) { return rc; }

    for (;;) {
        int i;
        int j;
        int k;
        int slot;

        if (s_pro_gb_cp_n == 0) { break; }
        if (++guard > 512) { return AC_PRO_GB_EINTERNAL; }

        /* 队首弹出（FIFO，整体前移，队列 ≤ 32 格） */
        i = s_pro_gb_cp_a[0];
        j = s_pro_gb_cp_b[0];
        for (k = 1; k < s_pro_gb_cp_n; k++) {
            s_pro_gb_cp_a[k - 1] = s_pro_gb_cp_a[k];
            s_pro_gb_cp_b[k - 1] = s_pro_gb_cp_b[k];
        }
        s_pro_gb_cp_n--;

        if (!gb_live(i) || !gb_live(j)) { continue; }   /* 防御：重建后不应出现 */

        rc = gb_spoly(i, j);
        if (rc != 1) { return rc; }
        rc = gb_dense_reduce(-1);
        if (rc != 1) { return rc; }

        if (gb_dense_is_zero()) { continue; }
        if (gb_dense_is_const()) { gb_collapse_one(); break; }

        /* 槽位分配：先回收死槽，否则开新槽（> 8 熔断） */
        slot = -1;
        for (s = 0; s < AC_PRO_GB_BASIS; s++) {
            if (s_pro_gb_dead(s)) { slot = s; break; }
        }
        if (slot < 0) {
            if (s_pro_gb_n >= AC_PRO_GB_BASIS) { return AC_PRO_GB_ESLOT; }
            slot = s_pro_gb_n;
            s_pro_gb_n++;
        }
        s_pro_gb_dead(slot) = 0;
        rc = gb_dense_store(slot);
        if (rc != 1) { return rc; }

        rc = gb_interreduce_all();
        if (rc != 1) { return rc; }
        rc = gb_rebuild_pairs();
        if (rc != 1) { return rc; }
    }

    /* 终结保证：无论主循环是否跑过，收尾再跑一遍不动点 interreduce */
    rc = gb_interreduce_all();
    return rc;
}

/* 输出顺序：按首项 lex 降序的活槽映射
 * 每次调用重算（≤ 8 槽插入排序，栈上完成，无静态对象）。 */
static int gb_kth_slot(int k) {
    int order[AC_PRO_GB_BASIS];
    int n = 0;
    int i;
    int t;

    for (i = 0; i < AC_PRO_GB_BASIS; i++) {
        if (gb_live(i)) { order[n++] = i; }
    }
    /* 插入排序：按首项 ep 降序 */
    for (i = 1; i < n; i++) {
        int key = order[i];
        t = i - 1;
        while (t >= 0 && gb_lm(order[t]) < gb_lm(key)) {
            order[t + 1] = order[t];
            t--;
        }
        order[t + 1] = key;
    }
    if (k < 0 || k >= n) { return -1; }
    return order[k];
}

/* int64 → 十进制（无符号幅值法）。为什么不用 %lld：MinGW 的格式检查
 * （ms_printf 原型）不认识 ll 长度修饰符，-Werror 下直接编译失败；而
 * %ld 在 Win64 上是 32 位 long，截断风险真实存在。手写渲染同时规避
 * 两者，并对 INT64_MIN 显式安全（先 +1 再取负再补 1，绝不直接取负）。 */
static int gb_fmt_i64(char *dst, int64_t v) {
    uint64_t m;
    char tmp[24];
    int n = 0;
    int d = 0;
    int i;
    if (v < 0) {
        dst[0] = '-';
        n = 1;
        m = (uint64_t)(-(v + 1)) + 1u;
    } else {
        m = (uint64_t)v;
    }
    if (m == 0) { tmp[d++] = '0'; }
    while (m > 0) {
        tmp[d++] = (char)('0' + (int)(m % 10u));
        m /= 10u;
    }
    for (i = d - 1; i >= 0; i--) { dst[n++] = tmp[i]; }
    dst[n] = '\0';
    return n;
}

/* 格式化（与 A1 归一化格式一致）
 * 元素间 " | "；系数 1/-1 带变量时省略；指数 1 省略；负系数转 " - "。 */
static int gb_format_elem(int s, char *out, int out_len, int *used) {
    int t;
    for (t = 0; t < s_pro_gb_cnt(s); t++) {
        int64_t c = s_pro_gb_coef(s, t);
        int e = s_pro_gb_ep(s, t);
        int ex = gb_ex_of(e);
        int ey = gb_ey_of(e);
        int neg = (c < 0);
        int64_t mag;
        int n;

        if (c == INT64_MIN) { return AC_PRO_GB_EOVER; }
        mag = neg ? -c : c;

        if (t == 0) {
            if (neg) {
                n = snprintf(out + *used, (size_t)(out_len - *used), "-");
                if (n < 0 || *used + n >= out_len) { return AC_PRO_GB_EBUF; }
                *used += n;
            }
        } else {
            n = snprintf(out + *used, (size_t)(out_len - *used), neg ? " - " : " + ");
            if (n < 0 || *used + n >= out_len) { return AC_PRO_GB_EBUF; }
            *used += n;
        }

        if (ex == 0 && ey == 0) {
            char num[24];
            n = gb_fmt_i64(num, mag);
            if (*used + n >= out_len) { return AC_PRO_GB_EBUF; }
            out[*used] = '\0';
            strcat(out + *used, num);
            *used += n;
        } else if (mag != 1) {
            char num[24];
            n = gb_fmt_i64(num, mag);
            if (*used + n + 1 >= out_len) { return AC_PRO_GB_EBUF; }
            out[*used] = '\0';
            strcat(out + *used, num);
            out[*used + n] = '*';
            out[*used + n + 1] = '\0';
            *used += n + 1;
        }
        if (ex > 0) {
            if (ex == 1) { n = snprintf(out + *used, (size_t)(out_len - *used), "x"); }
            else { n = snprintf(out + *used, (size_t)(out_len - *used), "x^%d", ex); }
            if (n < 0 || *used + n >= out_len) { return AC_PRO_GB_EBUF; }
            *used += n;
            if (ey > 0) {
                n = snprintf(out + *used, (size_t)(out_len - *used), "*");
                if (n < 0 || *used + n >= out_len) { return AC_PRO_GB_EBUF; }
                *used += n;
            }
        }
        if (ey > 0) {
            if (ey == 1) { n = snprintf(out + *used, (size_t)(out_len - *used), "y"); }
            else { n = snprintf(out + *used, (size_t)(out_len - *used), "y^%d", ey); }
            if (n < 0 || *used + n >= out_len) { return AC_PRO_GB_EBUF; }
            *used += n;
        }
    }
    return 1;
}

static int gb_format(char *out, int out_len) {
    int used = 0;
    int k;
    int cnt = 0;
    int rc;
    int i;

    for (i = 0; i < AC_PRO_GB_BASIS; i++) {
        if (gb_live(i)) { cnt++; }
    }
    if (cnt == 0) {
        if (out_len < 2) { return AC_PRO_GB_EBUF; }
        out[0] = '0';
        out[1] = '\0';
        return AC_PRO_GB_OK;
    }

    for (k = 0; k < cnt; k++) {
        int s = gb_kth_slot(k);
        if (s < 0) { return AC_PRO_GB_EINTERNAL; }
        if (k > 0) {
            int n = snprintf(out + used, (size_t)(out_len - used), " | ");
            if (n < 0 || used + n >= out_len) { return AC_PRO_GB_EBUF; }
            used += n;
        }
        rc = gb_format_elem(s, out, out_len, &used);
        if (rc != 1) { return rc; }
    }
    return AC_PRO_GB_OK;
}

/* 对外入口
 * 参数与返回契约见 ac_pro.h 的声明处（唯一公开头文件）。 */
int ac_pro_groebner(const char *fsrc, const char *gsrc, char *out, int out_len) {
    int rc;

    if (out == 0 || out_len < 2) { return AC_PRO_GB_EARG; }
    if (fsrc == 0 || gsrc == 0) { return AC_PRO_GB_EARG; }

    /* 整块清零：跨调用无残留（b_dead / 队列 / 计数全部归零） */
    memset(&g_ac_pro_scratch.gb, 0, sizeof(ac_pro_gb_view_t));

    rc = gb_parse_into(fsrc, 0);
    if (rc != 1) { return rc; }
    rc = gb_parse_into(gsrc, 1);
    if (rc != 1) { return rc; }
    s_pro_gb_n = 2;                            /* 槽 0/1 已解析（可能为空 = 零多项式） */

    rc = gb_solve();
    if (rc != 1) { return rc; }

    return gb_format(out, out_len);
}

/* 结果读取（视图访问器）
 * 均按输出顺序（首项 lex 降序）取第 k 个元素，与 gb_format 的排列
 * 一致；返回 gb 视图数据，成功调用后、下一次任何 Pro 视图 API 之前
 * 有效（视图持有纪律见 ac_pro_scratch.h）。 */
int ac_pro_gb_count(void) {
    int i;
    int n = 0;
    for (i = 0; i < AC_PRO_GB_BASIS; i++) {
        if (gb_live(i)) { n++; }
    }
    return n;
}

int ac_pro_gb_terms(int k) {
    int s = gb_kth_slot(k);
    if (s < 0) { return -1; }
    return s_pro_gb_cnt(s);
}

ac_int_t ac_pro_gb_coef(int k, int t) {
    int s = gb_kth_slot(k);
    if (s < 0 || t < 0 || t >= s_pro_gb_cnt(s)) { return 0; }
    return s_pro_gb_coef(s, t);
}

int ac_pro_gb_exps(int k, int t) {
    int s = gb_kth_slot(k);
    if (s < 0 || t < 0 || t >= s_pro_gb_cnt(s)) { return -1; }
    return s_pro_gb_ep(s, t);
}

#endif /* AC_PRO_ACTIVE */
