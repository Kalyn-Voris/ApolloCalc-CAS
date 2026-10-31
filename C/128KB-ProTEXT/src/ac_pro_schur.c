/* ============================================================
 * ApolloCalc-CAS · C/128KB-ProTEXT/src/ac_pro_schur.c
 * ------------------------------------------------------------
 * 【职责 / Responsibility】
 *   v2.4.0 功能 8:一般实矩阵的全部特征值(实/复共轭对)+ 可选实特征
 *   向量(反迭代)。算法:Householder 相似变换化 Hessenberg → Francis
 *   隐式双移 QR 迭代(bulge chasing)→ 2×2 块闭式解。非对称输入合法
 *   ——这正是本功能与 A5(ac_pro_eig.c,实对称 + Hotelling)的分界:
 *   ROADMAP.md:144 点名的「非对称返 -2」缺口(ac_pro.h:283-284)就此
 *   关闭;本文件的 -2 是「QR 迭代不收敛」,与 A5 的 -2 语义不同。
 *
 * 【算法边界 / Algorithm envelope】
 *   ① 幂 2 缩放:先按 max|a_ij| 用 frexp 取 2^e 做**精确**缩放(逐元素
 *      除以 2 的幂 = 纯指数加减,零舍入),把全部条目压进 [≈0.5, 1]。
 *      Francis 步中的 H² 项(条目平方)因此绝无溢出;特征值最后乘回。
 *   ② 次对角可忽略判据:|h[i][i-1]| ≤ 1e-12·(|h[i-1][i-1]|+|h[i][i]|),
 *      分母为 0 时退到 ‖H‖∞(再为 0 则次对角必为 0,直接 deflation)。
 *   ③ 每个特征值最多 50 轮 Francis 步,每 10 轮不 deflation 注入一次
 *      EISPACK 同款异常位移(s = 1.5·s0,t = −0.4375·s0²);50 轮仍不
 *      deflation → 返回 -2,**绝不返回未收敛的垃圾特征值**。
 *   ④ 实特征向量:对每个实特征值 λ 做 M = A − λI 的部分主元 LU +
 *      两步反迭代(零主元替换为 tiny,数值recipes 同款),残差
 *      ‖Av − λv‖∞ ≤ 1e-9·(1+‖A‖) 才接受;6 组 (扰动 × 初值) 全失败
 *      → 返回 -3。复特征值不产出向量(v 列全零,vector(k) 返回 NULL)
 *      —— 绝不硬凑复向量。亏损(Jordan)特征值按几何重数重复给出同
 *      一条特征向量,不做正交化修饰(那会产出非特征向量)。
 *
 * 【视图 / View】schur(2744 B ≤ 3848 吸收线,ac_pro_scratch.h)。
 *   调用方的 const a 在整个调用期间保持有效且只读:M = A − λI 每次从
 *   const 源重拷(与 A5 的 eig_build_deflated 同一技巧),省一个 n×n 缓冲。
 *
 * 【零存储纪律 / Zero-storage】
 *   零 static 对象、零 malloc、零 VLA、零递归;错误码走返回值
 *   (ac_pro.h eig 节论证:一个 static = TU 净 +32 B .bss)。Householder
 *   向量、2×2 位移标量全在栈/视图 work 槽。
 * ============================================================ */

#include <math.h>

#include "ac_pro_config.h"
#include "ac_pro.h"

#if AC_PRO_ACTIVE

#include "ac_pro_scratch.h"

/* 收敛/预算常量(全部写死,可测) */
#define PRO_SCHUR_TOL        1e-12  /* 次对角可忽略的相对阈值 */
#define PRO_SCHUR_MAX_EPOCH  50     /* 单特征值 Francis 步预算(每 10 步异常位移) */
#define PRO_SCHUR_VEC_RESID  1e-9   /* 实特征向量验收残差(相对 1+‖A‖) */
#define PRO_SCHUR_VEC_TRIES  6      /* 反迭代尝试组数(扰动 × 初值) */

/* ---- 单位 2-范数 Householder(3 元):u 单位向量,P = I − 2uuᵀ ----
 * P(x,y,z)ᵀ = (±‖·‖, 0, 0)ᵀ;全零向量返回 β = 0(调用方跳过变换)。
 * a = −sign(x)·‖·‖ 使 v₀ = x − a 无相消(LAPACK 同款)。 */
static double sch_house3(double x, double y, double z, double *u) {
    double n = sqrt(x * x + y * y + z * z);
    double a, v0, vv;
    if (n == 0.0) { u[0] = 1.0; u[1] = 0.0; u[2] = 0.0; return 0.0; }
    a = (x >= 0.0) ? -n : n;
    v0 = x - a;
    vv = sqrt(v0 * v0 + y * y + z * z);
    if (vv == 0.0) { u[0] = 1.0; u[1] = 0.0; u[2] = 0.0; return 0.0; }
    u[0] = v0 / vv;
    u[1] = y / vv;
    u[2] = z / vv;
    return 2.0;
}

static double sch_house2(double x, double y, double *u) {
    double n = sqrt(x * x + y * y);
    double a, v0, vv;
    if (n == 0.0) { u[0] = 1.0; u[1] = 0.0; return 0.0; }
    a = (x >= 0.0) ? -n : n;
    v0 = x - a;
    vv = sqrt(v0 * v0 + y * y);
    if (vv == 0.0) { u[0] = 1.0; u[1] = 0.0; return 0.0; }
    u[0] = v0 / vv;
    u[1] = y / vv;
    return 2.0;
}

/* ---- 左乘 P:行 i0..i0+w-1,列 jlo..n-1,H ← H − β·u(uᵀH) ---- */
static void sch_apply_left(double *h, int n, int i0, int w, int jlo,
                           double beta, const double *u) {
    int j, t;
    if (beta == 0.0) { return; }
    for (j = jlo; j < n; j++) {
        double s = 0.0;
        for (t = 0; t < w; t++) { s += u[t] * h[(i0 + t) * n + j]; }
        if (s != 0.0) {
            for (t = 0; t < w; t++) { h[(i0 + t) * n + j] -= beta * u[t] * s; }
        }
    }
}

/* ---- 右乘 P:行 ilo..ihi,列 i0..i0+w-1,H ← H − β·(Hu)uᵀ ---- */
static void sch_apply_right(double *h, int n, int i0, int w, int ilo, int ihi,
                            double beta, const double *u) {
    int i, t;
    if (beta == 0.0) { return; }
    for (i = ilo; i <= ihi; i++) {
        double s = 0.0;
        for (t = 0; t < w; t++) { s += h[i * n + i0 + t] * u[t]; }
        if (s != 0.0) {
            for (t = 0; t < w; t++) { h[i * n + i0 + t] -= beta * s * u[t]; }
        }
    }
}

/* ---- Householder 化 Hessenberg:逐列 k = 0..n-3,反射器作用于
 * 行 k+1..n-1(左,列 k..n-1)与列 k+1..n-1(右,行 0..n-1)。
 * 变换后把该列显式置成 (a, 0, ..., 0),舍入残渣不留巢。 ---- */
static void sch_hessenberg(double *h, int n, double *u) {
    int k, i;
    for (k = 0; k + 2 < n; k++) {
        double sq = 0.0;
        double nrm, a, v0, beta;
        for (i = k + 1; i < n; i++) { sq += h[i * n + k] * h[i * n + k]; }
        nrm = sqrt(sq);
        if (nrm == 0.0) { continue; }
        a = (h[(k + 1) * n + k] >= 0.0) ? -nrm : nrm;
        v0 = h[(k + 1) * n + k] - a;
        sq = v0 * v0;
        for (i = k + 2; i < n; i++) { sq += h[i * n + k] * h[i * n + k]; }
        nrm = sqrt(sq);
        if (nrm == 0.0) { continue; }
        u[0] = v0 / nrm;
        /* 行 k+1+t ↔ u[t]:行 k+2 的分量写 u[1](不是 u[i-k],那是
           off-by-one —— u[1] 会留成 work 槽的陈旧垃圾,首例碰巧全零
           才蒙混过关)。 */
        for (i = k + 2; i < n; i++) { u[i - k - 1] = h[i * n + k] / nrm; }
        beta = 2.0;
        sch_apply_left(h, n, k + 1, n - 1 - k, k, beta, u);
        sch_apply_right(h, n, k + 1, n - 1 - k, 0, n - 1, beta, u);
        h[(k + 1) * n + k] = a;
        for (i = k + 2; i < n; i++) { h[i * n + k] = 0.0; }
    }
}

/* ---- 2×2 实块的闭式特征值(压缩映射稳定的 Vieta 形式) ---- */
static void sch_eig2x2(double a, double b, double c, double d,
                       double *r1, double *i1, double *r2, double *i2) {
    double tr = a + d;
    double det = a * d - b * c;
    double disc = tr * tr - 4.0 * det;
    if (disc >= 0.0) {
        double sq = sqrt(disc);
        double l1 = (tr >= 0.0) ? (tr + sq) / 2.0 : (tr - sq) / 2.0;
        double l2 = (l1 != 0.0) ? det / l1 : tr - l1;
        *r1 = l1; *i1 = 0.0;
        *r2 = l2; *i2 = 0.0;
    } else {
        double re = tr / 2.0;
        double im = sqrt(-disc) / 2.0;
        *r1 = re; *i1 = im;
        *r2 = re; *i2 = -im;
    }
}

/* ---- Francis 隐式双移一步(窗口 [l..q],位移和 s、位移积 t 显式传入,
 * 异常位移由调用方改写 s/t)。
 * 左乘列域:jlo = max(l, k−1)(k = l 时含 l−1,保持相似性对次对角
 * 残条的精确缩放);右乘行域 0..min(k+3, q)(窗口下方耦合条已被
 * deflation 显式清零,乘它恒等)。k ≥ l+1 的左乘之后把被零化的
 * 凸出条显式清零,不让舍入残渣存活。 ---- */
static void sch_francis(double *h, int n, int l, int q,
                        double s, double t, double *u) {
    double x = h[l * n + l] * h[l * n + l]
             + h[l * n + l + 1] * h[(l + 1) * n + l]
             - s * h[l * n + l] + t;
    double y = h[(l + 1) * n + l] * (h[l * n + l] + h[(l + 1) * n + l + 1] - s);
    double z = h[(l + 1) * n + l] * h[(l + 2) * n + l + 1];
    int k;
    /* k = l..q-2:k = l 是隐式起步(P₀ 从 M 第一列构造),k ≥ l+1 逐列
       追赶(k 的反射器零化列 k−1 的凸出条);k = q−2 时 z 读出界 → 0,
       3 元反射器退化为「2 条凸出 + 1 条次对角」仍成立。窗口尺寸 3 时
       循环恰跑一轮(满窗 P₀),与 GvL n=3 的步态一致。 */
    for (k = l; k <= q - 2; k++) {
        double beta = sch_house3(x, y, z, u);
        if (beta != 0.0) {
            int jlo = (k > l) ? (k - 1) : ((l > 0) ? (l - 1) : 0);
            sch_apply_left(h, n, k, 3, jlo, beta, u);
            sch_apply_right(h, n, k, 3, 0, (k + 3 < q) ? (k + 3) : q, beta, u);
        }
        if (k > l) {
            h[(k + 1) * n + (k - 1)] = 0.0;
            h[(k + 2) * n + (k - 1)] = 0.0;
        }
        x = h[(k + 1) * n + k];
        y = h[(k + 2) * n + k];
        z = (k + 3 <= q) ? h[(k + 3) * n + k] : 0.0;
    }
    /* 末步:2 行反射器消 H[q][q-2] 残余凸出 */
    {
        double beta = sch_house2(h[(q - 1) * n + (q - 2)], h[q * n + (q - 2)], u);
        if (beta != 0.0) {
            sch_apply_left(h, n, q - 1, 2, q - 2, beta, u);
            sch_apply_right(h, n, q - 1, 2, 0, q, beta, u);
        }
        h[q * n + (q - 2)] = 0.0;
    }
}

/* ---- 反迭代:对实特征值 lam(缩放域)求 M = A_s − (lam+eps)I 的
 * 最小奇异方向。M 就地部分主元 LU(行交换记 piv),两步反迭代,
 * 残差过闸才接受。返回 1 = 成功(v 已归一化),0 = 本组失败。
 * h/piv/work 全是 schur 视图槽:特征值已全部提取,h 此时空闲。 ---- */
static int sch_inviter(const double *a, int n, int scale_exp, double lam_s,
                       int mode, double *v) {
    double *h = s_pro_schur_h;    /* M = A − λI 的就地 LU 工作副本(视图槽) */
    double anorm = 0.0;
    double *b = s_pro_schur_work;
    double *w = s_pro_schur_work + AC_PRO_MATRIX_MAX_DIM;
    int i, j, c, r, t, attempt;
    int got = 0;

    /* ‖A_scaled‖∞(缩放域):残差闸门的归一尺度 */
    for (i = 0; i < n; i++) {
        double s = 0.0;
        for (j = 0; j < n; j++) {
            s += fabs(a[i * n + j]) / ldexp(1.0, scale_exp);
        }
        if (s > anorm) { anorm = s; }
    }

    for (attempt = 0; attempt < PRO_SCHUR_VEC_TRIES && !got; attempt++) {
        double eps = (attempt % 2 == 1) ? anorm * 1e-13 : 0.0;
        double tiny = (anorm > 0.0) ? anorm * 1e-30 : 1e-30;
        double nrm;

        /* M = A_s − (lam_s + eps)·I(每次从 const 源重拷,精确幂 2 缩放) */
        for (i = 0; i < n; i++) {
            for (j = 0; j < n; j++) {
                h[i * n + j] = a[i * n + j] / ldexp(1.0, scale_exp);
            }
            h[i * n + i] -= lam_s + eps;
        }

        /* 部分主元 LU(就地),零主元替换为 tiny */
        for (c = 0; c < n; c++) {
            int p = c;
            double best = fabs(h[c * n + c]);
            double f;
            for (r = c + 1; r < n; r++) {
                double m = fabs(h[r * n + c]);
                if (m > best) { best = m; p = r; }
            }
            if (best < tiny) {
                h[p * n + c] = (h[p * n + c] < 0.0) ? -tiny : tiny;
            }
            if (p != c) {
                for (j = 0; j < n; j++) {
                    double tmp = h[c * n + j];
                    h[c * n + j] = h[p * n + j];
                    h[p * n + j] = tmp;
                }
            }
            s_pro_schur_piv[c] = p;
            for (r = c + 1; r < n; r++) {
                f = h[r * n + c] / h[c * n + c];
                h[r * n + c] = f;
                if (f != 0.0) {
                    for (j = c + 1; j < n; j++) { h[r * n + j] -= f * h[c * n + j]; }
                }
            }
        }

        /* 初值(确定性,与 A5 的 mode 族同风格) */
        if (mode == 0) {
            for (i = 0; i < n; i++) { b[i] = 1.0; }
        } else if (mode == 1) {
            for (i = 0; i < n; i++) { b[i] = 1.0 + 0.5 * (double)i; }
        } else if (mode == 2) {
            for (i = 0; i < n; i++) { b[i] = ((i & 1) ? -1.0 : 1.0) * (1.0 + 0.25 * (double)i); }
        } else {
            for (i = 0; i < n; i++) { b[i] = (i == mode - 3) ? 1.0 : 0.0; }
        }

        /* 两步反迭代:w = M⁻¹b,归一;v = M⁻¹w,归一 */
        for (t = 0; t < 2; t++) {
            const double *rhs = (t == 0) ? b : w;
            for (i = 0; i < n; i++) { w[i] = rhs[i]; }
            for (i = 0; i < n; i++) {
                int p = s_pro_schur_piv[i];
                if (p != i) {
                    double tmp = w[i]; w[i] = w[p]; w[p] = tmp;
                }
            }
            for (i = 1; i < n; i++) {
                double s = 0.0;
                for (j = 0; j < i; j++) { s += h[i * n + j] * w[j]; }
                w[i] -= s;
            }
            for (i = n - 1; i >= 0; i--) {
                double s = 0.0;
                for (j = i + 1; j < n; j++) { s += h[i * n + j] * w[j]; }
                w[i] = (w[i] - s) / h[i * n + i];
            }
            nrm = 0.0;
            for (i = 0; i < n; i++) { nrm += w[i] * w[i]; }
            nrm = sqrt(nrm);
            if (!(nrm > 0.0) || !isfinite(nrm)) { break; }   /* 归一化失败,换组 */
            for (i = 0; i < n; i++) { w[i] /= nrm; }
        }
        if (t < 2) { continue; }   /* 内层 break:本组失败 */

        /* 残差闸门:‖A_s v − lam_s v‖∞ ≤ 1e-9·(1 + ‖A_s‖保守界) */
        {
            double resid = 0.0;
            for (i = 0; i < n; i++) {
                double s = 0.0;
                double d;
                for (j = 0; j < n; j++) { s += (a[i * n + j] / ldexp(1.0, scale_exp)) * w[j]; }
                d = fabs(s - lam_s * w[i]);
                if (d > resid) { resid = d; }
            }
            if (resid <= PRO_SCHUR_VEC_RESID * (1.0 + anorm)) {
                for (i = 0; i < n; i++) { v[i] = w[i]; }
                got = 1;
            }
        }
    }
    return got;
}

/* ==================== 主入口 ==================== */
int ac_pro_schur_all(const double *a, int n) {
    double u3[3];                 /* Francis 3 元反射器(栈) */
    double ure[AC_PRO_MATRIX_MAX_DIM];
    double uim[AC_PRO_MATRIX_MAX_DIM];
    double hnorm = 0.0;
    double scale;
    int scale_exp = 0;
    int i, j, k, q, l, its, nu;
    int maxabs_zero = 1;

    s_pro_schur_n = 0;            /* 失败路径的 accessor 归零 */

    if (a == 0 || n < 1 || n > AC_PRO_MATRIX_MAX_DIM) { return -1; }

    /* ---- 幂 2 缩放:条目压进 [0.5, 1],Francis 的 H² 项绝无溢出 ---- */
    for (i = 0; i < n && maxabs_zero; i++) {
        for (j = 0; j < n; j++) {
            if (a[i * n + j] != 0.0) { maxabs_zero = 0; break; }
        }
    }
    if (!maxabs_zero) {
        double m = 0.0;
        int e;
        for (i = 0; i < n; i++) {
            for (j = 0; j < n; j++) {
                double v = fabs(a[i * n + j]);
                if (v > m) { m = v; }
            }
        }
        frexp(m, &e);             /* m = f·2^e,f ∈ [0.5,1) → 2^e ≥ m */
        scale_exp = e;
        scale = ldexp(1.0, e);
    } else {
        scale = 1.0;
    }
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            double e = a[i * n + j] / scale;
            s_pro_schur_h[i * n + j] = e;
            if (fabs(e) > hnorm) { hnorm = fabs(e); }
        }
    }

    /* ---- Householder 化 Hessenberg ---- */
    sch_hessenberg(s_pro_schur_h, n, s_pro_schur_work);

    /* ---- Francis 隐式双移 QR(bulge chasing,自底向上 deflation)----
     * 零矩阵:hnorm = 0,次对角全 0,判定式 0 ≤ 0 成立 → 逐位 1×1
     * deflation,特征值全 0,无需特例。 */
    q = n - 1;
    its = 0;
    while (q >= 0) {
        if (q == 0) {
            s_pro_schur_wr[0] = s_pro_schur_h[0];
            s_pro_schur_wi[0] = 0.0;
            q = -1;
            break;
        }
        /* 找窗口起点 l:自底向上找第一条可忽略次对角(找到即清零) */
        l = q;
        while (l > 0) {
            double below = fabs(s_pro_schur_h[l * n + (l - 1)]);
            double un = fabs(s_pro_schur_h[(l - 1) * n + (l - 1)])
                      + fabs(s_pro_schur_h[l * n + l]);
            double tol = PRO_SCHUR_TOL * ((un > 0.0) ? un : hnorm);
            if (below <= tol) {
                s_pro_schur_h[l * n + (l - 1)] = 0.0;
                break;
            }
            l--;
        }
        if (l == q) {
            /* 1×1 deflation */
            s_pro_schur_wr[q] = s_pro_schur_h[q * n + q];
            s_pro_schur_wi[q] = 0.0;
            s_pro_schur_h[q * n + (q - 1)] = 0.0;
            q--;
            its = 0;
            continue;
        }
        if (l == q - 1) {
            /* 2×2 块闭式解(共轭对相邻,+im 在前) */
            sch_eig2x2(s_pro_schur_h[(q - 1) * n + (q - 1)],
                       s_pro_schur_h[(q - 1) * n + q],
                       s_pro_schur_h[q * n + (q - 1)],
                       s_pro_schur_h[q * n + q],
                       &s_pro_schur_wr[q - 1], &s_pro_schur_wi[q - 1],
                       &s_pro_schur_wr[q], &s_pro_schur_wi[q]);
            s_pro_schur_h[q * n + (q - 1)] = 0.0;
            q -= 2;
            its = 0;
            continue;
        }
        /* Francis 步:位移取窗口尾 2×2;每 10 步不 deflation 注入
           EISPACK 同款异常位移,50 步仍不收敛 → -2 绝不硬凑 */
        its++;
        if (its > PRO_SCHUR_MAX_EPOCH) { return -2; }
        if (its % 10 == 0) {
            double s0 = fabs(s_pro_schur_h[q * n + (q - 1)])
                      + fabs(s_pro_schur_h[(q - 1) * n + (q - 2)]);
            sch_francis(s_pro_schur_h, n, l, q, 1.5 * s0, -0.4375 * s0 * s0, u3);
        } else {
            double s = s_pro_schur_h[(q - 1) * n + (q - 1)] + s_pro_schur_h[q * n + q];
            double t = s_pro_schur_h[(q - 1) * n + (q - 1)] * s_pro_schur_h[q * n + q]
                     - s_pro_schur_h[(q - 1) * n + q] * s_pro_schur_h[q * n + (q - 1)];
            sch_francis(s_pro_schur_h, n, l, q, s, t, u3);
        }
    }

    /* ---- 单元化 + 排序:实值自成单元,复共轭对(相邻,结构保证)合成
     * 一个单元(代表值 im ≥ 0);键 = (|λ|, re, im) 降序,稳定插入
     * (精确并列保持提取序 → 确定性)。展开回 wr/wi:共轭对相邻,+im 在前。 */
    nu = 0;
    for (k = 0; k < n; ) {
        if (s_pro_schur_wi[k] == 0.0) {
            ure[nu] = s_pro_schur_wr[k];
            uim[nu] = 0.0;
            nu++;
            k++;
        } else {
            ure[nu] = s_pro_schur_wr[k];
            uim[nu] = (s_pro_schur_wi[k] > 0.0) ? s_pro_schur_wi[k]
                                                : -s_pro_schur_wi[k];
            nu++;
            k += 2;
        }
    }
    for (i = 1; i < nu; i++) {
        double kre = ure[i];
        double kim = uim[i];
        double kmod = sqrt(kre * kre + kim * kim);
        j = i - 1;
        while (j >= 0) {
            double m = sqrt(ure[j] * ure[j] + uim[j] * uim[j]);
            if (m < kmod ||
                (m == kmod && ure[j] < kre) ||
                (m == kmod && ure[j] == kre && uim[j] < kim)) {
                ure[j + 1] = ure[j];
                uim[j + 1] = uim[j];
                j--;
            } else {
                break;
            }
        }
        ure[j + 1] = kre;
        uim[j + 1] = kim;
    }
    k = 0;
    for (i = 0; i < nu; i++) {
        s_pro_schur_wr[k] = ure[i];
        s_pro_schur_wi[k] = uim[i];
        if (uim[i] != 0.0) {
            s_pro_schur_wr[k + 1] = ure[i];
            s_pro_schur_wi[k + 1] = -uim[i];
            k += 2;
        } else {
            k += 1;
        }
    }

    /* ---- 缩放还原(乘幂 2,精确) ---- */
    for (k = 0; k < n; k++) {
        s_pro_schur_wr[k] *= scale;
        s_pro_schur_wi[k] *= scale;
    }

    /* ---- 实特征向量:反迭代(缩放域;向量与缩放无关)。
     * 复特征值列显式全零 —— 绝不硬凑复向量。 ---- */
    for (k = 0; k < n; k++) {
        if (s_pro_schur_wi[k] != 0.0) {
            for (i = 0; i < n; i++) { s_pro_schur_v[k * n + i] = 0.0; }
            continue;
        }
        {
            double lam_s = s_pro_schur_wr[k] / scale;   /* 幂 2 除法,精确 */
            int mode = k % 3;
            if (!sch_inviter(a, n, scale_exp, lam_s, mode,
                             s_pro_schur_v + k * n)) {
                return -3;    /* 6 组尝试全败:诚实失败,绝不硬凑 */
            }
        }
    }

    s_pro_schur_n = n;
    return 1;
}

/* ==================== 结果读取 ==================== */
int ac_pro_schur_count(void) {
    int c = s_pro_schur_n;
    if (c < 0 || c > AC_PRO_MATRIX_MAX_DIM) { return 0; }
    return c;
}

double ac_pro_schur_re(int k) {
    if (k < 0 || k >= ac_pro_schur_count()) { return 0.0; }
    return s_pro_schur_wr[k];
}

double ac_pro_schur_im(int k) {
    if (k < 0 || k >= ac_pro_schur_count()) { return 0.0; }
    return s_pro_schur_wi[k];
}

/* 实特征值返回列主序向量指针;复特征值(与越界)返回 NULL。 */
const double *ac_pro_schur_vector(int k) {
    int c = ac_pro_schur_count();
    if (k < 0 || k >= c) { return 0; }
    if (s_pro_schur_wi[k] != 0.0) { return 0; }
    return (const double *)(s_pro_schur_v + k * c);
}

#endif /* AC_PRO_ACTIVE */
