/* ApolloCalc · ac_pro_sparse.c — 稀疏线性方程组 CSR 直接求解器(n ≤ 32,nnz ≤ 320)

   v2.1.0 方向④「稀疏线性方程组(n > 12)」方案 a 的完整实现:阈值主元 CSR
   高斯消元,全部显式循环。见设计说明 §3 功能④(方案 a)。骨架期的探针占位
   (ac_pro_sparse_probe / AC_PRO_SPARSE_ENIMPL)已由真实 API 整体取代。

   三元组 → CSR:计数排序按行分桶(用 perm[] 当逐行游标,不另占存储),行内
   按列号插入排序,同列重复项求和合并,精确 0 丢弃。

   阈值主元消元(right-looking):第 k 步在行 k..n-1 中扫描列 k,记列最大模
   pmax。pmax == 0 是结构奇异 AC_PRO_SP_ESING;pmax < 主元下限是
   AC_PRO_SP_EPIVOT;否则取「|a_ik| ≥ τ·pmax 且 ≥ 主元下限」的候选中行非零
   元最少者为主元行(同数取 |a_ik| 更大者,再同取行号最小者,全部确定性
   tie-break),τ = 0.1。这是 Duff 意义下阈值主元的最简形态:数值上被 τ 夹住
   (主元 ≥ τ·pmax,增长因子 ≤ 1/τ 每步),结构上偏向稀疏行以抑制填充。行
   交换在 CSR 内原位完成(两行内容经定长栈暂存,中段整体平移,增广右端与
   perm 同步交换,perm 与 A4 lu.piv 同语义)。

   行更新经 32 格稠密散射行(栈上定长数组):行 i 的列 > k 部分散射进 dr[],
   减去 m×主元行,再按列号升序收回 CSR,精确 0(相消结果)丢弃;行段变长时
   尾部整体平移,总非零元超过 320 即熔断 AC_PRO_SP_EFILL(绝不静默丢填充,
   那会返回错值)。320 是输入与填充共享的总容量,这是方案 a 视图尺寸下的
   诚实能力边界。回代:上三角因子仍存于同一 CSR,按行读对角与右上部分。

   无 malloc、无 VLA、无递归、无静态对象。全部工作态在 Pro union 的 sparse
   视图(3848 B,v2.1.0 起的峰值)+ 栈上定长小数组(≤ 32 格,固定尺寸,非
   VLA)。错误码走返回值而非静态变量:任何静态对象 = .bss +32 B(16 B 段 +
   16 B 链接对齐,实测钉死,见 ac_pro.h eig 节)。

   TU 体整体包在 #if AC_PRO_ACTIVE 内;DEGRADED 构建下编成空对象,零 .bss、
   零 .text(与 ac_pro_multi.c 等既有 Pro 增量 TU 同纪律)。 */

#include <math.h>

#include "ac_pro.h"

#if AC_PRO_ACTIVE

/* 算法参数(TU 内部约定,不进公开头文件,宏不占任何存储)
 *   AC_PRO_SP_PIVOT_FLOOR 主元绝对下限:某步列最大模低于它 → EPIVOT。
 *     与 A4(LU)的奇异判据 1e-12 同值,两层口径一致。
 *   AC_PRO_SP_PIVOT_TAU   阈值主元的 τ:候选主元须 |a| ≥ τ·列最大模。
 *     τ = 0.1 是稀疏直接法(Duff/MA48 一系)的常用缺省;τ ≤ 1 保证
 *     列最大模自身恒为候选,选择循环无空转路径。 */
#define AC_PRO_SP_PIVOT_FLOOR 1e-12
#define AC_PRO_SP_PIVOT_TAU   0.1

/* 对外入口:稀疏线性方程组求解
 * 参数与返回契约见 ac_pro.h 的声明处(唯一公开头文件)。 */
int ac_pro_sparse_solve(int n, int nnz,
                        const int32_t *tri_r, const int32_t *tri_c,
                        const double *tri_v, const double *rhs) {
    double *const val = s_pro_sp_val;
    int16_t *const col = s_pro_sp_col;
    int16_t *const rp = s_pro_sp_rp;
    int16_t *const perm = s_pro_sp_perm;
    double dr[AC_PRO_SP_MAX_N];          /* 消元散射行(栈上定长,非 VLA) */
    int i, j, k, c;

    /* preCheck:n 超限 / nnz 超限 / 三元组非法 */
    if (n < 1 || n > AC_PRO_SP_MAX_N) return AC_PRO_SP_EN;
    if (nnz < 1 || nnz > AC_PRO_SP_MAX_NNZ) return AC_PRO_SP_ENNZ;
    if (tri_r == 0 || tri_c == 0 || tri_v == 0 || rhs == 0) return AC_PRO_SP_EINDEX;
    for (k = 0; k < nnz; k++) {
        if (tri_r[k] < 0 || tri_r[k] >= n || tri_c[k] < 0 || tri_c[k] >= n)
            return AC_PRO_SP_EINDEX;
    }

    /* 三元组 → CSR:计数排序分桶 */
    for (i = 0; i <= n; i++) rp[i] = 0;
    for (k = 0; k < nnz; k++) rp[tri_r[k] + 1] = (int16_t)(rp[tri_r[k] + 1] + 1);
    for (i = 0; i < n; i++) rp[i + 1] = (int16_t)(rp[i + 1] + rp[i]);   /* 前缀和 → 行首 */
    for (i = 0; i < n; i++) perm[i] = rp[i];             /* perm 暂当逐行写入游标 */
    for (k = 0; k < nnz; k++) {
        int p = perm[tri_r[k]];
        perm[tri_r[k]] = (int16_t)(p + 1);
        col[p] = (int16_t)tri_c[k];
        val[p] = tri_v[k];
    }

    /* 续:行内按列排序 + 同列合并 + 精确 0 丢弃 + 原位压实
     * 写游标 used 恒 ≤ 当前行起点(已处理行的压实长度 ≤ 原始长度),
     * 故写永不越过未读输入。合并后的同列求和若恰为 0 也回撤丢弃。 */
    {
        int used = 0;
        int beg = 0;
        for (i = 0; i < n; i++) {
            int end = rp[i + 1];
            int rowStart = used;
            int a, b;
            for (a = beg + 1; a < end; a++) {           /* 插入排序(行 ≤ 32 项) */
                int cv = col[a];
                double vv = val[a];
                b = a - 1;
                while (b >= beg && col[b] > cv) {
                    col[b + 1] = col[b];
                    val[b + 1] = val[b];
                    b--;
                }
                col[b + 1] = (int16_t)cv;
                val[b + 1] = vv;
            }
            rp[i] = (int16_t)rowStart;
            for (j = beg; j < end; j++) {
                if (val[j] == 0.0) continue;            /* 输入精确 0:结构零,丢弃 */
                if (used > rowStart && col[used - 1] == col[j]) {
                    val[used - 1] += val[j];
                    if (val[used - 1] == 0.0) used--;   /* 相消归零:回撤 */
                } else {
                    col[used] = col[j];
                    val[used] = val[j];
                    used++;
                }
            }
            beg = end;
        }
        rp[n] = (int16_t)used;
    }

    /* 增广右端入视图,perm 置恒等 */
    for (i = 0; i < n; i++) s_pro_sp_rhs[i] = rhs[i];
    for (i = 0; i < n; i++) perm[i] = (int16_t)i;

    /* 阈值主元消元 */
    {
        int total = rp[n];                               /* 当前 CSR 总非零元 */
        for (k = 0; k < n; k++) {
            double pmax = 0.0;
            double thresh;
            int best = -1;
            double bestA = 0.0;
            int bestN = 0;
            double piv;

            /* 列 k(行 k..n-1)最大模。行内列号升序,越过 k 即剪枝。 */
            for (i = k; i < n; i++) {
                for (j = rp[i]; j < rp[i + 1]; j++) {
                    c = col[j];
                    if (c == k) {
                        double a = fabs(val[j]);
                        if (a > pmax) pmax = a;
                        break;
                    }
                    if (c > k) break;
                }
            }
            if (pmax == 0.0) return AC_PRO_SP_ESING;     /* 列空 → 结构奇异 */
            if (pmax < AC_PRO_SP_PIVOT_FLOOR) return AC_PRO_SP_EPIVOT;

            /* 阈值主元选择:候选须 |a| ≥ τ·pmax 且 ≥ 主元下限;
             * 行非零元最少者优先(抑制填充),同数取 |a| 大者,再同取行号小者。
             * pmax 自身必为候选(τ ≤ 1 且 pmax ≥ 下限),best 恒 ≥ 0。 */
            thresh = AC_PRO_SP_PIVOT_TAU * pmax;
            for (i = k; i < n; i++) {
                int rnnz = rp[i + 1] - rp[i];
                for (j = rp[i]; j < rp[i + 1]; j++) {
                    c = col[j];
                    if (c == k) {
                        double a = fabs(val[j]);
                        if (a >= thresh && a >= AC_PRO_SP_PIVOT_FLOOR) {
                            if (best < 0 || rnnz < bestN ||
                                (rnnz == bestN && a > bestA)) {
                                best = i;
                                bestA = a;
                                bestN = rnnz;
                            }
                        }
                        break;
                    }
                    if (c > k) break;
                }
            }

            /* 行交换(best → k):两行内容经栈上定长暂存,中段整体平移。
             * 平移总 span 不变(交换不增减非零元),无容量问题。 */
            if (best != k) {
                int a0 = rp[k], a1 = rp[k + 1], b0 = rp[best], b1 = rp[best + 1];
                int lenK = a1 - a0, lenB = b1 - b0;
                int delta = lenB - lenK;
                double kv[AC_PRO_SP_MAX_N], bv[AC_PRO_SP_MAX_N];
                int16_t kc[AC_PRO_SP_MAX_N], bc[AC_PRO_SP_MAX_N];
                for (j = 0; j < lenK; j++) { kv[j] = val[a0 + j]; kc[j] = col[a0 + j]; }
                for (j = 0; j < lenB; j++) { bv[j] = val[b0 + j]; bc[j] = col[b0 + j]; }
                if (delta > 0) {
                    for (j = b0 - 1; j >= a1; j--) {
                        val[j + delta] = val[j];
                        col[j + delta] = col[j];
                    }
                } else if (delta < 0) {
                    for (j = a1; j < b0; j++) {
                        val[j + delta] = val[j];
                        col[j + delta] = col[j];
                    }
                }
                for (j = 0; j < lenB; j++) { val[a0 + j] = bv[j]; col[a0 + j] = bc[j]; }
                for (j = 0; j < lenK; j++) { val[b1 - lenK + j] = kv[j]; col[b1 - lenK + j] = kc[j]; }
                for (j = k + 2; j <= best; j++) rp[j] = (int16_t)(rp[j] + delta);
                rp[k + 1] = (int16_t)(a0 + lenB);
                {
                    double tb = s_pro_sp_rhs[k];
                    s_pro_sp_rhs[k] = s_pro_sp_rhs[best];
                    s_pro_sp_rhs[best] = tb;
                }
                {
                    int16_t tp = perm[k];
                    perm[k] = perm[best];
                    perm[best] = tp;
                }
            }

            /* 主元 (k,k) 值 */
            piv = 0.0;
            for (j = rp[k]; j < rp[k + 1]; j++) {
                if (col[j] == k) { piv = val[j]; break; }
            }

            /* 消元:行 i = k+1..n-1,经稠密散射行做稀疏 axpy + 原位重建 */
            for (i = k + 1; i < n; i++) {
                int pik = -1;
                double aik = 0.0;
                double m;
                int cnt, nKeep, olen, nlen, delta2, pos;
                for (j = rp[i]; j < rp[i + 1]; j++) {
                    c = col[j];
                    if (c == k) { pik = j; aik = val[j]; break; }
                    if (c > k) break;
                }
                if (pik < 0) continue;                   /* a_ik = 0:无事可做 */
                m = aik / piv;

                for (c = k + 1; c < n; c++) dr[c] = 0.0;
                for (j = rp[i]; j < rp[i + 1]; j++) {
                    if (col[j] > k) dr[col[j]] = val[j]; /* 散射行 i(列 > k) */
                }
                for (j = rp[k]; j < rp[k + 1]; j++) {    /* 减 m × 主元行 */
                    c = col[j];
                    if (c > k) dr[c] -= m * val[j];
                }

                cnt = 0;
                for (c = k + 1; c < n; c++) {
                    if (dr[c] != 0.0) cnt++;             /* 精确 0(含相消)不回写 */
                }
                nKeep = pik - rp[i];                     /* 行内列 < k 的前缀不动 */
                olen = rp[i + 1] - rp[i];
                nlen = nKeep + cnt;
                delta2 = nlen - olen;

                if (delta2 > 0) {                        /* 行变长:容量熔断 + 尾部右移 */
                    if (total + delta2 > AC_PRO_SP_MAX_NNZ) return AC_PRO_SP_EFILL;
                    for (j = total - 1; j >= rp[i + 1]; j--) {
                        val[j + delta2] = val[j];
                        col[j + delta2] = col[j];
                    }
                } else if (delta2 < 0) {                 /* 行变短:尾部左移 */
                    for (j = rp[i + 1]; j < total; j++) {
                        val[j + delta2] = val[j];
                        col[j + delta2] = col[j];
                    }
                }
                pos = rp[i] + nKeep;
                for (c = k + 1; c < n; c++) {
                    if (dr[c] != 0.0) {
                        val[pos] = dr[c];
                        col[pos] = (int16_t)c;
                        pos++;
                    }
                }
                for (j = i + 1; j <= n; j++) rp[j] = (int16_t)(rp[j] + delta2);
                total += delta2;
                s_pro_sp_rhs[i] -= m * s_pro_sp_rhs[k];
            }
        }
    }

    /* 回代:上三角因子仍在同一 CSR,行 i 的列 ≥ i */
    for (i = n - 1; i >= 0; i--) {
        double sum = 0.0;
        double diag = 0.0;
        for (j = rp[i]; j < rp[i + 1]; j++) {
            c = col[j];
            if (c == i) diag = val[j];
            else if (c > i) sum += val[j] * s_pro_sp_x[c];
        }
        s_pro_sp_x[i] = (s_pro_sp_rhs[i] - sum) / diag;
    }

    return AC_PRO_SP_OK;
}

/* 结果读取(视图访问器)
 * 均返回 sparse 视图内部指针:成功调用后、下一次任何 Pro 视图 API 之前
 * 有效(视图持有纪律见 ac_pro_scratch.h)。失败调用的内容未定义。 */
const double *ac_pro_sparse_x(void) {
    return s_pro_sp_x;
}

const double *ac_pro_sparse_rhs(void) {
    return s_pro_sp_rhs;    /* 消元后的增广右端(上三角系统的 c 向量) */
}

const int16_t *ac_pro_sparse_perm(void) {
    return s_pro_sp_perm;   /* perm[i] = 位置 i 上的原始行号(同 lu.piv 语义) */
}

#endif /* AC_PRO_ACTIVE */
