/* ApolloCalc · ac_pro_eigen.c — A5 实对称矩阵的全部特征对(n ≤ 12)

   幂迭代 + Rayleigh 商 + Hotelling 收缩 deflation。

   只用两个 n×n 缓冲,是因为 `const double *a` 在调用期间一直有效:「当前
   deflated 矩阵」不必常驻,每轮从 a 重拷进 s_pro_eig_a 再扣 Σλ_jv_jvᵀ。
   三个缓冲要 3456 B,超出 eig 视图 2592 B 的预算。

   收敛判据盯残差而不是 |Δλ|:Rayleigh 商二阶精确(λ(x) = λ + O(θ²)),相邻
   |Δλ| 会先塌到 1e-16,而残差 ‖Dx − λx‖ 还在 1e-7,用 |Δλ| 判停会提前两三
   个数量级收工,实测 3×3 用例残差卡在 4.6e-8 过不了 1e-9 闸门。残差才是角度
   误差的直接度量(‖r‖ = gap·sinθ)。

   特征向量一律单位 2-范数:λ_jv_jv_jᵀ 只有在 ‖v_j‖ = 1 时才是谱投影,否则
   会把 ‖v_j‖² 的比例因子混进后续特征值。

   λ 用原始 A 的 Rayleigh 商复核,不用 deflated 矩阵的商,deflation 的舍入误差
   只该影响求向量,不该污染报特征值。每个候选特征对还要过残差闸门,不过闸就
   换初值重试,绝不把混合态当特征对返回。

   初值用 3 个解析向量 + n 个单位基依次试(mode 0..2+n)。幂迭代初值若落在已求
   向量的张成空间里,正交化后会归零,所以要换下一个。

   不声明任何 static 变量或数组。工作存储只有 eig 视图那四个数组;视图没有
   「已求个数」的专用槽(a 被 n² 用满,lam 在 n = 12 时也用满),所以 count
   停泊在 s_pro_eig_a[0]:这块缓冲每轮重建,ac_pro_eig_all 返回后不再有语义,
   拿它停一个整数不增加一字节静态存储。读取端把 [0,12] 之外的值一律报 0,
   失败调用残留的算术垃圾不会被误读成有效计数。
   (实测一个 static int 会让全程序 .bss 从 41456 涨到 41488,直接打穿门禁。) */

#include <math.h>
#include <stddef.h>

#include "ac_pro_config.h"
#include "ac_pro.h"

#if AC_PRO_ACTIVE

#include "ac_pro_scratch.h"

/* PRO_EIG_MAX_ITER：最坏情况 r = |λ2|/|λ1| ≈ 0.84（4×4 用例第 2 轮），
 * 残差从 O(1) 降到 1e-14 要 ln(1e-14)/ln(0.84) ≈ 185 次，取 400 留一倍余量。
 * 单轮代价 400 × n² = 57600 次乘加（n = 12），全矩阵 12 轮仍 < 1 ms。 */
#define PRO_EIG_MAX_ITER     400
#define PRO_EIG_RESID        1e-13   /* 迭代内残差早退（相对 ‖D‖∞） */
#define PRO_EIG_SLOWDOWN     0.9     /* 残差下降停滞比：res > 0.9·res_prev 即收敛 */
#define PRO_EIG_ACCEPT       1e-8    /* 特征对验收残差（相对 1+max|a_ij|） */
#define PRO_EIG_START_COLL   1e-10   /* 初值被已求向量吃掉后的判零阈值 */
#define PRO_EIG_SYM_REL      1e-12   /* 对称性相对容差 */
#define PRO_EIG_RANK_REL     1e-12   /* 阶梯消元的秩判定相对容差 */

static double eig_norm2(const double *v, int n) {
    double s = 0.0;
    int i;
    for (i = 0; i < n; ++i) s += v[i] * v[i];
    return sqrt(s);
}

static double eig_dot(const double *x, const double *y, int n) {
    double s = 0.0;
    int i;
    for (i = 0; i < n; ++i) s += x[i] * y[i];
    return s;
}

/* y 与 x 必须不别名。 */
static void eig_matvec(const double *m, const double *x, double *y, int n) {
    int i, j;
    for (i = 0; i < n; ++i) {
        double s = 0.0;
        for (j = 0; j < n; ++j) s += m[i * n + j] * x[j];
        y[i] = s;
    }
}

/* ‖M‖∞，用来把残差归一化成相对量。 */
static double eig_infnorm(const double *m, int n) {
    double best = 0.0;
    int i, j;
    for (i = 0; i < n; ++i) {
        double s = 0.0;
        for (j = 0; j < n; ++j) s += fabs(m[i * n + j]);
        if (s > best) best = s;
    }
    return best;
}

/* 每轮都从调用方的 const a 重拷，绝不在 a 上就地操作。 */
static void eig_build_deflated(const double *a, int n, int k) {
    int i, j, t;
    for (i = 0; i < n; ++i)
        for (j = 0; j < n; ++j)
            s_pro_eig_a[i * n + j] = a[i * n + j];
    for (t = 0; t < k; ++t) {
        const double *vj = s_pro_eig_v + t * n;
        double lj = s_pro_eig_lam[t];
        for (i = 0; i < n; ++i) {
            double li = lj * vj[i];
            for (j = 0; j < n; ++j)
                s_pro_eig_a[i * n + j] -= li * vj[j];
        }
    }
}

/* 在 m 上求主特征对。返回 -2 表示初值被已求向量吃光，调用方换下一个 mode。 */
static int eig_power(const double *m, int n, int mode, int kprev,
                     double *v, double *lam) {
    double *x = s_pro_eig_work;         /* 前 n 个 */
    double *y = s_pro_eig_work + n;     /* 后 n 个（n ≤ 12 → 最多用满 24） */
    double scale, ny, res, res_prev = 1e300, d;
    int i, j, t, pass;
    int converged = 0;

    scale = eig_infnorm(m, n);
    if (scale <= 0.0) scale = 1.0;      /* 全零矩阵交给退化分支 */

    if (mode == 0) {
        for (i = 0; i < n; ++i) x[i] = 1.0;
    } else if (mode == 1) {
        for (i = 0; i < n; ++i) x[i] = 1.0 + 0.5 * (double)i;
    } else if (mode == 2) {
        for (i = 0; i < n; ++i)
            x[i] = ((i & 1) ? -1.0 : 1.0) * (1.0 + 0.25 * (double)i);
    } else {
        int e = mode - 3;
        for (i = 0; i < n; ++i) x[i] = (i == e) ? 1.0 : 0.0;
    }

    /* 两遍 MGS：单遍的正交性会随 kprev 退化。 */
    for (pass = 0; pass < 2; ++pass) {
        for (j = 0; j < kprev; ++j) {
            const double *vj = s_pro_eig_v + j * n;
            double c = 0.0;
            for (i = 0; i < n; ++i) c += vj[i] * x[i];
            for (i = 0; i < n; ++i) x[i] -= c * vj[i];
        }
    }
    ny = eig_norm2(x, n);
    if (ny <= PRO_EIG_START_COLL) return -2;
    for (i = 0; i < n; ++i) x[i] /= ny;

    for (t = 0; t < PRO_EIG_MAX_ITER; ++t) {
        eig_matvec(m, x, y, n);
        *lam = eig_dot(x, y, n);        /* x 单位 → Rayleigh 商 */
        ny = eig_norm2(y, n);

        /* 退化：D·x = 0。x 是零空间向量，且（由 D 对称 + Dv_j = 0）
           自动与已求向量正交，正好是 λ = 0 的特征向量。 */
        if (ny <= PRO_EIG_RESID * scale) {
            for (i = 0; i < n; ++i) v[i] = x[i];
            *lam = 0.0;
            return 0;
        }

        /* 迭代内残差 r = y − λx，不额外做矩阵乘。 */
        res = 0.0;
        for (i = 0; i < n; ++i) {
            d = y[i] - *lam * x[i];
            res += d * d;
        }
        res = sqrt(res);
        if (res <= PRO_EIG_RESID * scale) { converged = 1; break; }

        for (i = 0; i < n; ++i) x[i] = y[i] / ny;

        /* 停滞判据必须盯残差而不是 |Δλ|：Rayleigh 商二阶精确，相邻 |Δλ|
         * 会先于角度误差塌到 1e-16，此时残差还在 1e-7 量级，用 |Δλ| 判停
         * 会提前 2~3 个数量级收工，实测 3×3 用例残差卡在 4.6e-8 过不了 1e-9。
         * 残差按 (λ2/λ1)^k 几何下降，掉不动了就是到了机器精度。 */
        if (t >= 1 && res > 0.9 * res_prev) { converged = 1; break; }
        res_prev = res;
    }

    /* 收尾在最终 x 上重算 Rayleigh 商，使 (v, λ) 同步。 */
    eig_matvec(m, x, y, n);
    *lam = eig_dot(x, y, n);
    for (i = 0; i < n; ++i) v[i] = x[i];
    return converged ? 1 : -1;
}

/* 在 m 上求 m·v = 0 的非零解，m 会被破坏。
 * 自由变量取最后一个自由列，自下而上回代。 */
static int eig_null(double *m, int n, double *v) {
    int piv[AC_PRO_MATRIX_MAX_DIM];
    int row, col, i, j, pc, free_col;
    double scale = 0.0, tol, best, f, s;

    for (i = 0; i < n; ++i)
        for (j = 0; j < n; ++j)
            if (fabs(m[i * n + j]) > scale) scale = fabs(m[i * n + j]);

    if (scale <= 0.0) {
        for (i = 0; i < n; ++i) v[i] = (i == 0) ? 1.0 : 0.0;
        return 1;
    }
    tol = PRO_EIG_RANK_REL * scale;

    row = 0;
    for (col = 0; col < n && row < n; ++col) {
        int p = row;
        best = fabs(m[row * n + col]);
        for (i = row + 1; i < n; ++i) {
            f = fabs(m[i * n + col]);
            if (f > best) { best = f; p = i; }
        }
        if (best <= tol) {
            piv[col] = -1;
            continue;
        }
        if (p != row) {
            for (j = 0; j < n; ++j) {
                double t = m[row * n + j];
                m[row * n + j] = m[p * n + j];
                m[p * n + j] = t;
            }
        }
        for (i = row + 1; i < n; ++i) {
            f = m[i * n + col] / m[row * n + col];
            if (f == 0.0) continue;
            for (j = col; j < n; ++j) m[i * n + j] -= f * m[row * n + j];
            m[i * n + col] = 0.0;
        }
        piv[col] = row;
        ++row;
    }

    if (row == n) return 0;

    free_col = -1;
    for (col = n - 1; col >= 0; --col)
        if (piv[col] < 0) { free_col = col; break; }
    if (free_col < 0) return 0;

    for (i = 0; i < n; ++i) v[i] = 0.0;
    v[free_col] = 1.0;

    for (i = row - 1; i >= 0; --i) {
        pc = -1;
        for (col = 0; col < n; ++col)
            if (piv[col] == i) { pc = col; break; }
        if (pc < 0) continue;
        s = 0.0;
        for (j = pc + 1; j < n; ++j) s += m[i * n + j] * v[j];
        v[pc] = -s / m[i * n + pc];
    }
    return 1;
}

/* 对 V 的前 k 列再正交化，然后用原始 A 复核 λ 与残差；不过闸返回 0。 */
static int eig_accept(const double *a, int n, int k, double *v,
                      double *lam, double scale) {
    double ny, resid, d;
    int i, j, pass;

    for (pass = 0; pass < 2; ++pass) {
        for (j = 0; j < k; ++j) {
            const double *vj = s_pro_eig_v + j * n;
            double c = 0.0;
            for (i = 0; i < n; ++i) c += vj[i] * v[i];
            for (i = 0; i < n; ++i) v[i] -= c * vj[i];
        }
    }
    ny = eig_norm2(v, n);
    if (ny <= PRO_EIG_START_COLL) return 0;
    for (i = 0; i < n; ++i) v[i] /= ny;

    /* 借 work 做 Av，此时 eig 视图空闲。 */
    eig_matvec(a, v, s_pro_eig_work, n);
    *lam = eig_dot(v, s_pro_eig_work, n);
    resid = 0.0;
    for (i = 0; i < n; ++i) {
        d = fabs(s_pro_eig_work[i] - *lam * v[i]);
        if (d > resid) resid = d;
    }
    if (resid > PRO_EIG_ACCEPT * (1.0 + scale)) return 0;
    return 1;
}

int ac_pro_eig_all(const double *a, int n) {
    int i, j, k, t, rc, got;
    double scale, lam, best_lam;
    double *vk;

    s_pro_eig_a[0] = 0.0;               /* count 停泊位先清零（失败时 count() 必须为 0） */

    if (a == NULL) return -1;
    if (n < 1 || n > AC_PRO_MATRIX_MAX_DIM) return -1;

    /* 全零矩阵没有可归一化的特征向量 → -4 */
    scale = 0.0;
    for (i = 0; i < n; ++i)
        for (j = 0; j < n; ++j)
            if (fabs(a[i * n + j]) > scale) scale = fabs(a[i * n + j]);
    if (scale <= 0.0) return -4;

    /* 非对称 → -2：Hotelling 收缩在非对称矩阵上不保谱，返回错值比返回错误码更糟。 */
    for (i = 0; i < n; ++i)
        for (j = i + 1; j < n; ++j)
            if (fabs(a[i * n + j] - a[j * n + i]) >
                PRO_EIG_SYM_REL * (scale > 1.0 ? scale : 1.0))
                return -2;

    for (k = 0; k < n; ++k) {
        vk = s_pro_eig_v + k * n;
        eig_build_deflated(a, n, k);
        got = 0;
        best_lam = 0.0;

        /* 多初值重试，取第一个过残差闸门的候选。 */
        for (t = 0; t < 3 + n && !got; ++t) {
            rc = eig_power(s_pro_eig_a, n, t, k, vk, &lam);
            if (rc == -2) continue;     /* 初值落在已求张成空间里，换一个 */
            if (rc == 0) best_lam = 0.0;
            else best_lam = lam;
            if (eig_accept(a, n, k, vk, &lam, scale)) {
                s_pro_eig_lam[k] = lam;
                got = 1;
            }
        }

        /* 兜底走零空间路径：先用最后一个候选 λ 做位移，再用 λ = 0
         * （D_k 自身的零空间）。 */
        for (t = 0; t < 2 && !got; ++t) {
            double shift = (t == 0) ? best_lam : 0.0;
            eig_build_deflated(a, n, k);
            for (i = 0; i < n; ++i) s_pro_eig_a[i * n + i] -= shift;
            if (eig_null(s_pro_eig_a, n, vk) != 1) continue;
            if (eig_accept(a, n, k, vk, &lam, scale)) {
                s_pro_eig_lam[k] = lam;
                got = 1;
            }
        }

        if (!got) {
            s_pro_eig_a[0] = 0.0;
            return -3;
        }
    }

    for (i = 0; i < n; ++i) {
        int m = i;
        for (j = i + 1; j < n; ++j)
            if (fabs(s_pro_eig_lam[j]) > fabs(s_pro_eig_lam[m])) m = j;
        if (m != i) {
            double tl = s_pro_eig_lam[i];
            s_pro_eig_lam[i] = s_pro_eig_lam[m];
            s_pro_eig_lam[m] = tl;
            for (t = 0; t < n; ++t) {
                double tv = s_pro_eig_v[i * n + t];
                s_pro_eig_v[i * n + t] = s_pro_eig_v[m * n + t];
                s_pro_eig_v[m * n + t] = tv;
            }
        }
    }

    s_pro_eig_a[0] = (double)n;
    return 1;
}

double ac_pro_eig_value(int k) {
    int cnt = ac_pro_eig_count();
    if (k < 0 || k >= cnt) return 0.0;
    return s_pro_eig_lam[k];
}

const double *ac_pro_eig_vector(int k) {
    int cnt = ac_pro_eig_count();
    if (k < 0 || k >= cnt) return (const double *)0;
    return (const double *)(s_pro_eig_v + k * cnt);
}

int ac_pro_eig_count(void) {
    double c = s_pro_eig_a[0];
    int n;
    if (!(c >= 0.0)) return 0;          /* 写成 !(c >= 0.0) 是为了连 NaN 一起挡掉 */
    if (c > (double)AC_PRO_MATRIX_MAX_DIM) return 0;
    n = (int)(c + 0.5);
    if (n < 0 || n > AC_PRO_MATRIX_MAX_DIM) return 0;
    return n;
}

#endif /* AC_PRO_ACTIVE */
