/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc · ac_pro_svd.c — m×n 实矩阵 one-sided Jacobi 奇异值分解(G2)

   v2.6.0 G2:m×n(m,n ≤ 12;m < n 内部走转置路径)实稠密矩阵的奇异值分解。
   算法:one-sided Jacobi(Hestenes,LAPACK dgesvj 同族)——A 的副本上固定
   循环序列正交化列对,旋转累计进 V;收敛后各列 2-范数降序稳定插入排序
   (ac_pro_schur.c:440-486 同款),U/V 列同步换序。行为边界由
   docs/contracts/svd-format-v26.md 逐字冻结:§3.4 的旋转公式表达式书写
   顺序、§3.7 的出口序列、§4 的符号规范化、§3.9 的错误码,本文件是唯一
   权威 C 实现(v2.7 JS 面 core_matrix.js proSvd* 逐字镜像本文件)。

   幂 2 缩放:先按 max|a_ij| 用 frexp 取 2^e 做精确缩放(逐元素除以 2 的
   幂 = 纯指数加减,零舍入;ac_pro_schur.c:342-369 同纪律),列范数平方
   项绝无溢出,奇异值出口 ×scale 精确还原。e ≥ 1024(scale 非有限)→
   -1 域界诚实拒绝,绝不在 scale = inf 下产出 NaN 谱。

   sweep 终止性(契约 §3.4 注记):正常列对按平方收敛(γ 每轮开方);
   数值秩亏列对(幽灵尘埃)的 γ² = αβ 精确成立,相对正交判据永不触发,
   由 PRO_SVD_ZERO_TOL(1e-12 相对)的数值零列对跳过判据冻结——没有它,
   尘埃对会在 ζ = ±inf → t = 0 的恒等旋转上永久空转(12×12 数值秩亏
   稠密阵实测 30 轮预算耗尽)。契约冻结 30 轮预算,全语料实测峰值 12 轮。

   出口(契约 §3.7 顺序冻结):排序 → 数值零 σ 精确置 0(≤ 1e-12·σ₁,
   被弃质量 ≪ 1e-9 闸门,方向无数学意义,确定性优于保留噪声谱尾)→
   零列 Gram-Schmidt 补全(候选 e_t 首个残量 ≥ 0.25 即收;Bessel 恒等式
   保证 m ≤ 12 必有候选)→ U/V 出口构造(m < n 走转置对偶)→ 符号规范化
   (U 每列最大 |·| 分量置正、并列取最小索引,V 同因子成对翻转)→
   双闸门回乘验证(‖A_s−UΣ_sVᵗ‖∞ ≤ 1e-9·(1+‖A_s‖∞) 与 ‖UᵗU−I‖∞ ≤ 1e-9,
   缩放域;不过闸返 -3 绝不硬凑)→ w ×scale 还原。

   视图 svd(3760 B ≤ 3848 吸收线,ac_pro_scratch.h;margin 88 B):
   a 工作副本/ U / V / w / work 双列排序临时+补全残量 / m_st,n_st,k_st
   accessor 停泊。调用方的 const a 在整个调用期间只读(闸门即时重算
   A_s = a/scale,幂 2 除法,零额外缓冲)。

   零 static 对象、零 malloc、零 VLA、零递归;错误码走返回值(ac_pro.h
   eig 节实测论证:一个 static = TU 净 +32 B .bss)。旋转常数、sweep
   计数全在栈;Jacobi 紧循环约 60 行(比 Francis 双移 QR 简单一档,
   ROADMAP §G2 预估兑现)。 */

#include <math.h>

#include "ac_pro_config.h"
#include "ac_pro.h"

#if AC_PRO_ACTIVE

#include "ac_pro_scratch.h"

/* 契约冻结常量(docs/contracts/svd-format-v26.md §3.1,可测可镜像) */
#define PRO_SVD_MAX_SWEEPS   30     /* sweep 预算封顶,超限返 -2          */
#define PRO_SVD_ORTHO_TOL    1e-14  /* 列对正交判据(相对 √(αβ))         */
#define PRO_SVD_ZERO_TOL     1e-12  /* 数值零列/零 σ 判据(相对)         */
#define PRO_SVD_COMPLETE_MIN 0.25   /* 零列补全残量验收线(Bessel 下界
                                     * √(1/12) ≈ 0.2887 > 0.25,m ≤ 12
                                     * 必有候选可收)                     */
#define PRO_SVD_RESID        1e-9   /* 出口双闸门容差                     */

/* 主入口:契约 §3.3-§3.7 逐字实现。 */
int ac_pro_svd_all(const double *a, int m, int n) {
    double scale = 1.0;
    double mmax = 0.0;
    int k, p, tr, i, j, l, pp, q, qq, tt, sweep, rotated = 0;
    int idx, completed, nonfinite = 0;

    s_pro_svd_m = 0;              /* 失败路径的 accessor 归零 */
    s_pro_svd_n = 0;
    s_pro_svd_k = 0;

    /* -1 参数拒绝:NULL / 维度 / 非有限元素(契约 §3.9) */
    if (a == 0 || m < 1 || n < 1 ||
        m > AC_PRO_MATRIX_MAX_DIM || n > AC_PRO_MATRIX_MAX_DIM) {
        return -1;
    }
    for (i = 0; i < m * n; i++) {
        if (isnan(a[i]) || isinf(a[i])) { nonfinite = 1; break; }
        if (fabs(a[i]) > mmax) { mmax = fabs(a[i]); }
    }
    if (nonfinite) { return -1; }

    /* 幂 2 缩放(契约 §3.3):scale = 2^e,e 由 frexp 给出;
       e ≥ 1024 ⟹ scale 非有限 → 域界诚实拒绝 */
    if (mmax > 0.0) {
        int e = 0;
        (void)frexp(mmax, &e);
        if (e >= 1024) { return -1; }
        scale = ldexp(1.0, e);
    }

    k = (m < n) ? m : n;
    tr = (m < n) ? 1 : 0;
    p = tr ? n : m;

    /* 工作副本 B(p×k 行主序,缩放域;m < n 时 B = Aᵗ;契约 §3.2) */
    for (i = 0; i < p; i++) {
        for (j = 0; j < k; j++) {
            double e = tr ? a[j * n + i] : a[i * n + j];
            s_pro_svd_a[i * k + j] = e / scale;
        }
    }

    /* V ← I_k(列主序 v[j*k+i]) */
    for (j = 0; j < k * k; j++) { s_pro_svd_v[j] = 0.0; }
    for (j = 0; j < k; j++) { s_pro_svd_v[j * k + j] = 1.0; }

    /* one-sided Jacobi sweep(契约 §3.4/§3.5:表达式书写顺序逐字冻结) */
    for (sweep = 0; sweep < PRO_SVD_MAX_SWEEPS; sweep++) {
        rotated = 0;
        for (pp = 0; pp < k - 1; pp++) {
            for (q = pp + 1; q < k; q++) {
                double alpha = 0.0;
                double beta = 0.0;
                double gamma = 0.0;
                double smin, smax, zeta, t, c, s;
                for (i = 0; i < p; i++) {
                    alpha += s_pro_svd_a[i * k + pp] * s_pro_svd_a[i * k + pp];
                }
                for (i = 0; i < p; i++) {
                    beta += s_pro_svd_a[i * k + q] * s_pro_svd_a[i * k + q];
                }
                for (i = 0; i < p; i++) {
                    gamma += s_pro_svd_a[i * k + pp] * s_pro_svd_a[i * k + q];
                }
                if (gamma == 0.0) { continue; }               /* 精确正交/零列 */
                smin = sqrt((alpha < beta) ? alpha : beta);
                smax = sqrt((alpha > beta) ? alpha : beta);
                if (smin <= PRO_SVD_ZERO_TOL * smax) { continue; }  /* 数值零列对 */
                if (fabs(gamma) <= PRO_SVD_ORTHO_TOL * sqrt(alpha * beta)) {
                    continue;                                  /* 已正交 */
                }
                zeta = (beta - alpha) / (2.0 * gamma);
                t = ((zeta >= 0.0) ? 1.0 : -1.0)
                    / (fabs(zeta) + sqrt(1.0 + zeta * zeta));
                c = 1.0 / sqrt(1.0 + t * t);
                s = c * t;
                for (i = 0; i < p; i++) {
                    double bp = s_pro_svd_a[i * k + pp];
                    double bq = s_pro_svd_a[i * k + q];
                    s_pro_svd_a[i * k + pp] = c * bp - s * bq;
                    s_pro_svd_a[i * k + q] = s * bp + c * bq;
                }
                for (i = 0; i < k; i++) {
                    double vp = s_pro_svd_v[pp * k + i];
                    double vq = s_pro_svd_v[q * k + i];
                    s_pro_svd_v[pp * k + i] = c * vp - s * vq;
                    s_pro_svd_v[q * k + i] = s * vp + c * vq;
                }
                rotated++;
            }
        }
        if (rotated == 0) { break; }
    }
    if (rotated != 0) { return -2; }   /* 30 sweep 预算耗尽,诚实失败 */

    /* 奇异值 = 列 2-范数(求和按 i 升序,契约 §3.6) */
    for (j = 0; j < k; j++) {
        double s2 = 0.0;
        for (i = 0; i < p; i++) {
            s2 += s_pro_svd_a[i * k + j] * s_pro_svd_a[i * k + j];
        }
        s_pro_svd_w[j] = sqrt(s2);
    }

    /* 降序稳定插入排序(schur 同款:严格 < 才前移,并列保持提取序),
       b 列(work[0..11])与 V 列(work[12..23])同步换序(契约 §3.6) */
    for (i = 1; i < k; i++) {
        double key = s_pro_svd_w[i];
        l = i - 1;
        for (j = 0; j < p; j++) { s_pro_svd_work[j] = s_pro_svd_a[j * k + i]; }
        for (j = 0; j < k; j++) {
            s_pro_svd_work[AC_PRO_MATRIX_MAX_DIM + j] = s_pro_svd_v[i * k + j];
        }
        while (l >= 0 && s_pro_svd_w[l] < key) {
            s_pro_svd_w[l + 1] = s_pro_svd_w[l];
            for (j = 0; j < p; j++) { s_pro_svd_a[j * k + l + 1] = s_pro_svd_a[j * k + l]; }
            for (j = 0; j < k; j++) { s_pro_svd_v[(l + 1) * k + j] = s_pro_svd_v[l * k + j]; }
            l--;
        }
        s_pro_svd_w[l + 1] = key;
        for (j = 0; j < p; j++) { s_pro_svd_a[j * k + l + 1] = s_pro_svd_work[j]; }
        for (j = 0; j < k; j++) {
            s_pro_svd_v[(l + 1) * k + j] = s_pro_svd_work[AC_PRO_MATRIX_MAX_DIM + j];
        }
    }

    /* 数值零 σ 判定(契约 §3.7a2):w_j ≤ 1e-12·w_0 → 精确置 0 */
    for (j = 0; j < k; j++) {
        if (s_pro_svd_w[j] <= PRO_SVD_ZERO_TOL * s_pro_svd_w[0]) {
            s_pro_svd_w[j] = 0.0;
        }
    }
    /* 数据列归一化(w_j > 0,缩放域除法;契约 §3.7b) */
    for (j = 0; j < k; j++) {
        if (s_pro_svd_w[j] > 0.0) {
            for (i = 0; i < p; i++) { s_pro_svd_a[i * k + j] /= s_pro_svd_w[j]; }
        }
    }
    /* 零 σ 列 Gram-Schmidt 补全(契约 §3.7c:候选 e_0..e_{p-1} 首个可收即停;
       投影求和 j' = 0..k−1 升序跳 j;残量复用 work[12..23]) */
    for (j = 0; j < k; j++) {
        if (s_pro_svd_w[j] != 0.0) { continue; }
        completed = 0;
        for (tt = 0; tt < p && !completed; tt++) {
            double nr = 0.0;
            for (i = 0; i < p; i++) {
                s_pro_svd_work[AC_PRO_MATRIX_MAX_DIM + i] = (i == tt) ? 1.0 : 0.0;
            }
            for (qq = 0; qq < k; qq++) {
                double dot = 0.0;
                if (qq == j) { continue; }
                for (i = 0; i < p; i++) {
                    dot += s_pro_svd_a[i * k + qq]
                         * s_pro_svd_work[AC_PRO_MATRIX_MAX_DIM + i];
                }
                for (i = 0; i < p; i++) {
                    s_pro_svd_work[AC_PRO_MATRIX_MAX_DIM + i] -=
                        dot * s_pro_svd_a[i * k + qq];
                }
            }
            for (i = 0; i < p; i++) {
                nr += s_pro_svd_work[AC_PRO_MATRIX_MAX_DIM + i]
                    * s_pro_svd_work[AC_PRO_MATRIX_MAX_DIM + i];
            }
            nr = sqrt(nr);
            if (nr >= PRO_SVD_COMPLETE_MIN) {
                for (i = 0; i < p; i++) {
                    s_pro_svd_a[i * k + j] =
                        s_pro_svd_work[AC_PRO_MATRIX_MAX_DIM + i] / nr;
                }
                completed = 1;
            }
        }
        if (!completed) { return -3; }   /* Bessel 保证不发生;诚实路径 */
    }

    /* 出口构造(契约 §3.7d;纯数据搬移零舍入) */
    if (!tr) {
        /* m ≥ n:U ← b 转置重排(k = n,V 缓冲列主序 k×k 已是最终 V) */
        for (j = 0; j < k; j++) {
            for (i = 0; i < m; i++) {
                s_pro_svd_u[j * m + i] = s_pro_svd_a[i * k + j];
            }
        }
    } else {
        /* m < n:U ← V'(旋转累计阵,m×m 正交即最终 U);V ← b 转置重排。
           先拷 U 再覆写 V(写区间 j·n+m 高于读区间 j·k+m,倒序覆盖) */
        for (j = 0; j < k; j++) {
            for (i = 0; i < m; i++) {
                s_pro_svd_u[j * m + i] = s_pro_svd_v[j * k + i];
            }
        }
        for (j = 0; j < k; j++) {
            for (i = 0; i < n; i++) {
                s_pro_svd_v[j * n + i] = s_pro_svd_a[i * k + j];
            }
        }
    }

    /* 符号规范化(契约 §4:U 每列最大 |·| 分量置正、并列取最小索引;
       V 同因子成对翻转) */
    for (j = 0; j < k; j++) {
        double mx = fabs(s_pro_svd_u[j * m]);
        idx = 0;
        for (i = 1; i < m; i++) {
            double av = fabs(s_pro_svd_u[j * m + i]);
            if (av > mx) { mx = av; idx = i; }
        }
        if (s_pro_svd_u[j * m + idx] < 0.0) {
            for (i = 0; i < m; i++) { s_pro_svd_u[j * m + i] = -s_pro_svd_u[j * m + i]; }
            for (i = 0; i < n; i++) { s_pro_svd_v[j * n + i] = -s_pro_svd_v[j * n + i]; }
        }
    }

    /* 双闸门回乘验证(契约 §3.8,缩放域,A_s 即时重算,零额外缓冲) */
    {
        double anorm = 0.0;
        double r1 = 0.0;
        double r2 = 0.0;
        for (i = 0; i < m; i++) {
            double rs = 0.0;
            for (l = 0; l < n; l++) { rs += fabs(a[i * n + l] / scale); }
            if (rs > anorm) { anorm = rs; }
        }
        /* R1:‖A_s − UΣ_sVᵗ‖∞ ≤ RESID·(1+‖A_s‖∞);acc 乘法结合序 (U·w)·V */
        for (i = 0; i < m; i++) {
            double rs = 0.0;
            for (l = 0; l < n; l++) {
                double acc = 0.0;
                for (j = 0; j < k; j++) {
                    acc += (s_pro_svd_u[j * m + i] * s_pro_svd_w[j])
                         * s_pro_svd_v[j * n + l];
                }
                rs += fabs(a[i * n + l] / scale - acc);
            }
            if (rs > r1) { r1 = rs; }
        }
        if (!(r1 <= PRO_SVD_RESID * (1.0 + anorm))) { return -3; }
        /* R2:‖UᵗU − I‖∞ ≤ RESID(绝对) */
        for (pp = 0; pp < k; pp++) {
            for (q = 0; q < k; q++) {
                double dot = 0.0;
                double d;
                for (i = 0; i < m; i++) {
                    dot += s_pro_svd_u[i + pp * m] * s_pro_svd_u[i + q * m];
                }
                d = fabs(((pp == q) ? 1.0 : 0.0) - dot);
                if (d > r2) { r2 = d; }
            }
        }
        if (!(r2 <= PRO_SVD_RESID)) { return -3; }
    }

    /* 缩放还原(乘幂 2,精确)+ accessor 停泊(契约 §3.7g) */
    for (j = 0; j < k; j++) { s_pro_svd_w[j] *= scale; }
    s_pro_svd_m = m;
    s_pro_svd_n = n;
    s_pro_svd_k = k;
    return 1;
}

/* accessor:契约 §3.10。越界安全;返回视图内部指针,跨 API 调用后失效。 */
double ac_pro_svd_value(int k) {
    if (k < 0 || k >= s_pro_svd_k) { return 0.0; }
    return s_pro_svd_w[k];
}

const double *ac_pro_svd_u(int k) {
    if (k < 0 || k >= s_pro_svd_k) { return 0; }
    return &s_pro_svd_u[k * s_pro_svd_m];
}

const double *ac_pro_svd_v(int k) {
    if (k < 0 || k >= s_pro_svd_k) { return 0; }
    return &s_pro_svd_v[k * s_pro_svd_n];
}

int ac_pro_svd_count(void) {
    return s_pro_svd_k;
}

#endif /* AC_PRO_ACTIVE */
