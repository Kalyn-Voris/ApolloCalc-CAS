/* ApolloCalc · test_pro_svd.c — v2.6.0 G2(one-sided Jacobi SVD)验收测试

   比对 docs/contracts/svd-format-v26.md 冻结的语义:降序稳定排序、
   符号规范化(U 每列最大 |·| 分量置正、并列取最小索引)、退化谱语义
   (重 σ/σ=0 只断言 w + 重构/正交不变量,良分离谱才逐向量)、错误码
   1/-1/-2/-3。期望值出处:契约 tests/svd-cases.json(52 例,闭式谱
   手算锚 + 开工前 JS oracle 全量验证),本文件按组取关键锚。
   双闸门残差与正交性在测试侧对 accessor 独立重算(不信任被测闸门)。

   覆盖五组:basic 方阵 thin / rectangular 矩形(含 m<n 转置路径) /
   degenerate 秩亏+重 σ / scale 极端幂 2 / reject 全拒绝码,含
   「刚好通过/刚好拒绝」双侧边界(12 阶 vs 13 阶、正交判据 1e-15 跳过
   vs 1e-13 旋转)。

   纪律:accessor 读数必须在下一次 ac_pro_svd_all 之前完成(视图持有
   纪律③:跨 API 调用即失效)。自带 svd_tally_t,不 include runner 内部
   头(与 test_pro_schur.c 同约定)。不声明静态对象:本 TU 的 .bss 是 0。 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../src/ac_pro.h"

#if AC_PRO_ACTIVE

typedef struct {
    int checks;
    int failed;
} svd_tally_t;

static void svcheck(svd_tally_t *t, int cond, const char *name, const char *detail) {
    t->checks++;
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        t->failed++;
        printf("  [FAIL] %s  (%s)\n", name, detail);
    }
}

/* 契约混合相对-绝对比较:err = |a-b| / max(1,|b|) <= tol */
static int sv_near(double a, double b, double tol) {
    double den = fabs(b);
    if (den < 1.0) { den = 1.0; }
    return fabs(a - b) <= tol * den;
}

/* 纯相对比较:|a-b| <= tol*|b|(极端 幂 2 尺度的逐位级断言用) */
static int sv_near_rel(double a, double b, double tol) {
    return fabs(a - b) <= tol * fabs(b);
}

/* ‖A − UΣVᵗ‖∞ / (1 + ‖A‖∞):对调用方原始 A 与 accessor 独立重算 */
static double sv_recon_rel(const double *a, int m, int n) {
    int k = ac_pro_svd_count();
    int i, j, l;
    double anorm = 0.0, r1 = 0.0;
    if (k == 0) { return -1.0; }
    for (i = 0; i < m; i++) {
        double rs = 0.0;
        for (l = 0; l < n; l++) { rs += fabs(a[i * n + l]); }
        if (rs > anorm) { anorm = rs; }
    }
    for (i = 0; i < m; i++) {
        double rs = 0.0;
        for (l = 0; l < n; l++) {
            double acc = 0.0;
            for (j = 0; j < k; j++) {
                acc += ac_pro_svd_u(j)[i] * ac_pro_svd_value(j) * ac_pro_svd_v(j)[l];
            }
            rs += fabs(a[i * n + l] - acc);
        }
        if (rs > r1) { r1 = rs; }
    }
    return r1 / (1.0 + anorm);
}

/* ‖UᵗU − I‖∞ 与 ‖VᵗV − I‖∞(需要 m/n,accessor 步长的契约语义) */
static void sv_ortho_dev(int m, int n, double *ru, double *rv) {
    int k = ac_pro_svd_count();
    int i, p, q;
    *ru = 0.0;
    *rv = 0.0;
    for (p = 0; p < k; p++) {
        for (q = 0; q < k; q++) {
            double du = 0.0, dv = 0.0;
            for (i = 0; i < m; i++) { du += ac_pro_svd_u(p)[i] * ac_pro_svd_u(q)[i]; }
            for (i = 0; i < n; i++) { dv += ac_pro_svd_v(p)[i] * ac_pro_svd_v(q)[i]; }
            {
                double eu = fabs(((p == q) ? 1.0 : 0.0) - du);
                double ev = fabs(((p == q) ? 1.0 : 0.0) - dv);
                if (eu > *ru) { *ru = eu; }
                if (ev > *rv) { *rv = ev; }
            }
        }
    }
}

/* 符号规范化不变量:U 每列首个最大 |·| 分量严格为正(契约 §4) */
static int sv_sign_norm_ok(int m) {
    int k = ac_pro_svd_count();
    int j, i;
    for (j = 0; j < k; j++) {
        const double *u = ac_pro_svd_u(j);
        int idx = 0;
        double mx = fabs(u[0]);
        for (i = 1; i < m; i++) {
            if (fabs(u[i]) > mx) { mx = fabs(u[i]); idx = i; }
        }
        if (!(u[idx] > 0.0)) { return 0; }
    }
    return 1;
}

/* w 降序(含精确并列) */
static int sv_desc_ok(void) {
    int k = ac_pro_svd_count();
    int j;
    for (j = 1; j < k; j++) {
        if (ac_pro_svd_value(j - 1) < ac_pro_svd_value(j)) { return 0; }
    }
    return 1;
}

/* 不变量打包:rc==1 后通用五连(降序/重构/正交/符号/域内码) */
static void sv_invariants(svd_tally_t *t, const double *a, int m, int n,
                          double tol, const char *tag) {
    char name[96];
    double ru, rv, rr;
    sprintf(name, "%s: w descending", tag);
    svcheck(t, sv_desc_ok(), name, "");
    rr = sv_recon_rel(a, m, n);
    sprintf(name, "%s: reconstruction rel <= tol", tag);
    {
        char detail[64];
        sprintf(detail, "res=%.3g tol=%g", rr, tol);
        svcheck(t, rr <= tol, name, detail);
    }
    sv_ortho_dev(m, n, &ru, &rv);
    sprintf(name, "%s: U^T U ~ I", tag);
    {
        char detail[64];
        sprintf(detail, "dev=%.3g", ru);
        svcheck(t, ru <= 1e-9, name, detail);
    }
    sprintf(name, "%s: V^T V ~ I", tag);
    {
        char detail[64];
        sprintf(detail, "dev=%.3g", rv);
        svcheck(t, rv <= 1e-9, name, detail);
    }
    sprintf(name, "%s: sign-normalized U", tag);
    svcheck(t, sv_sign_norm_ok(m), name, "");
}

/* 期望 w 逐位(长度 k,混合容差) */
static void sv_expect_w(svd_tally_t *t, const double *want, const char *tag) {
    int k = ac_pro_svd_count();
    int j;
    char name[96];
    for (j = 0; j < k; j++) {
        double got = ac_pro_svd_value(j);
        if (!sv_near(got, want[j], 1e-9)) {
            char detail[128];
            sprintf(name, "%s: w[%d] == %.15g", tag, j, want[j]);
            sprintf(detail, "got %.17g", got);
            svcheck(t, 0, name, detail);
            return;
        }
    }
    sprintf(name, "%s: w spectrum (%d values)", tag, k);
    svcheck(t, 1, name, "");
}

/* 期望向量列(sv_near 混合容差;仅在良分离谱/精确结构用例调用) */
static void sv_expect_col(svd_tally_t *t, int is_u, int col,
                          const double *want, int len, const char *tag) {
    const double *got = is_u ? ac_pro_svd_u(col) : ac_pro_svd_v(col);
    char name[96];
    int i;
    if (got == 0) {
        sprintf(name, "%s: %s col %d present", tag, is_u ? "U" : "V", col);
        svcheck(t, 0, name, "accessor NULL");
        return;
    }
    for (i = 0; i < len; i++) {
        if (!sv_near(got[i], want[i], 1e-9)) {
            char detail[160];
            sprintf(name, "%s: %s[%d][%d] == %.15g", tag, is_u ? "U" : "V", col, i, want[i]);
            sprintf(detail, "got %.17g", got[i]);
            svcheck(t, 0, name, detail);
            return;
        }
    }
    sprintf(name, "%s: %s col %d exact", tag, is_u ? "U" : "V", col);
    svcheck(t, 1, name, "");
}

static void sv_expect_rc(svd_tally_t *t, int rc, int want, const char *tag) {
    char name[96], detail[64];
    sprintf(name, "%s: rc == %d", tag, want);
    sprintf(detail, "got %d", rc);
    svcheck(t, rc == want, name, detail);
}

/* ---- Group 1: basic 方阵 thin ---- */

/* [case SVD-1] 1×1 形状:正/负/零(补全列 + 符号规范化钉死) */
void test_svd_n1(svd_tally_t *t) {
    double mp[1] = { 5.0 };
    double mn[1] = { -3.0 };
    double mz[1] = { 0.0 };
    double wn[1] = { 3.0 };                 /* sigma = |a| */
    double up[1] = { 1.0 }, vp[1] = { 1.0 };
    double un[1] = { 1.0 }, vn[1] = { -1.0 };
    int rc;
    printf("[case SVD-1] 1x1 shapes (pos/neg/zero, sign normalization)\n");
    rc = ac_pro_svd_all(mp, 1, 1);
    sv_expect_rc(t, rc, 1, "n1 pos");
    svcheck(t, ac_pro_svd_count() == 1, "n1 pos: count == 1", "");
    sv_expect_w(t, mp, "n1 pos");           /* w = [5] */
    sv_expect_col(t, 1, 0, up, 1, "n1 pos");
    sv_expect_col(t, 0, 0, vp, 1, "n1 pos");
    rc = ac_pro_svd_all(mn, 1, 1);
    sv_expect_rc(t, rc, 1, "n1 neg");
    sv_expect_w(t, wn, "n1 neg");           /* w = [3] */
    sv_expect_col(t, 1, 0, un, 1, "n1 neg");    /* U = +1(符号规范化) */
    sv_expect_col(t, 0, 0, vn, 1, "n1 neg");    /* V = -1(成对翻转) */
    rc = ac_pro_svd_all(mz, 1, 1);
    sv_expect_rc(t, rc, 1, "n1 zero");
    svcheck(t, ac_pro_svd_value(0) == 0.0, "n1 zero: w == 0 exactly", "");
    sv_expect_col(t, 1, 0, up, 1, "n1 zero");   /* 补全列 = e0 */
    sv_expect_col(t, 0, 0, vp, 1, "n1 zero");
}

/* [case SVD-2] diag3:w = |diag|、U 列 = +e_i(排序换序 + 符号规范化) */
void test_svd_diag3(svd_tally_t *t) {
    double m[9] = { 1, 0, 0, 0, 2, 0, 0, 0, 3 };
    double u0[3] = { 0, 0, 1 }, u1[3] = { 0, 1, 0 }, u2[3] = { 1, 0, 0 };
    double w[3] = { 3, 2, 1 };
    int rc = ac_pro_svd_all(m, 3, 3);
    printf("[case SVD-2] diag(1,2,3): w=|diag|, U cols = +e_i\n");
    sv_expect_rc(t, rc, 1, "diag3");
    sv_expect_w(t, w, "diag3");
    sv_expect_col(t, 1, 0, u0, 3, "diag3");
    sv_expect_col(t, 1, 1, u1, 3, "diag3");
    sv_expect_col(t, 1, 2, u2, 3, "diag3");
    sv_expect_col(t, 0, 0, u0, 3, "diag3");     /* V col0 = e2(正对角) */
    sv_invariants(t, m, 3, 3, 1e-9, "diag3");
}

/* [case SVD-3] diag 符号语义:mixed/neg 的 V 成对翻转 */
void test_svd_diag_signs(svd_tally_t *t) {
    double mm[9] = { 3, 0, 0, 0, -2, 0, 0, 0, 1 };
    double mn[9] = { -1, 0, 0, 0, -2, 0, 0, 0, -3 };
    double ucol[3] = { 0, 1, 0 };           /* U col1 = +e1(符号规范化) */
    double vcol[3] = { 0, -1, 0 };          /* V col1 = -e1(成对翻转)   */
    double uabc[9] = { 0, 0, 1, 0, 1, 0, 1, 0, 0 };
    double vnabc[9] = { 0, 0, -1, 0, -1, 0, -1, 0, 0 };
    double wmix[3] = { 3, 2, 1 };
    int rc = ac_pro_svd_all(mm, 3, 3);
    printf("[case SVD-3] diag signs: paired U/V flips\n");
    sv_expect_rc(t, rc, 1, "mixed diag");
    sv_expect_w(t, wmix, "mixed diag");
    sv_expect_col(t, 1, 1, ucol, 3, "mixed diag");
    sv_expect_col(t, 0, 1, vcol, 3, "mixed diag");
    rc = ac_pro_svd_all(mn, 3, 3);
    sv_expect_rc(t, rc, 1, "neg diag");
    sv_expect_col(t, 1, 0, uabc, 3, "neg diag");
    sv_expect_col(t, 0, 0, vnabc, 3, "neg diag");
    sv_invariants(t, mn, 3, 3, 1e-9, "neg diag");
}

/* [case SVD-4] [[2,1],[1,2]] 闭式谱 {3,1},良分离谱逐向量 */
void test_svd_sym2(svd_tally_t *t) {
    double m[4] = { 2, 1, 1, 2 };
    double s = 0.70710678118654752;
    double w[2] = { 3, 1 };
    double u0[2] = { s, s }, u1[2] = { s, -s };
    int rc = ac_pro_svd_all(m, 2, 2);
    printf("[case SVD-4] [[2,1],[1,2]]: closed spectrum {3,1}, strict uv\n");
    sv_expect_rc(t, rc, 1, "sym2");
    sv_expect_w(t, w, "sym2");
    sv_expect_col(t, 1, 0, u0, 2, "sym2");
    sv_expect_col(t, 1, 1, u1, 2, "sym2");
    sv_expect_col(t, 0, 0, u0, 2, "sym2");
    sv_expect_col(t, 0, 1, u1, 2, "sym2");
    sv_invariants(t, m, 2, 2, 1e-9, "sym2");
}

/* [case SVD-5] 已知闭式谱对称阵:三对角 2+2cos(kπ/5) 与 2±√2 */
void test_svd_tri(svd_tally_t *t) {
    double t4[16] = { 2,1,0,0, 1,2,1,0, 0,1,2,1, 0,0,1,2 };
    double t3[9] = { 2,1,0, 1,2,1, 0,1,2 };
    double w4[4] = { 3.618033988749895, 2.618033988749895, 1.381966011250105, 0.381966011250105 };
    double w3[3] = { 3.414213562373095, 2, 0.585786437626905 };
    int rc = ac_pro_svd_all(t4, 4, 4);
    printf("[case SVD-5] known closed spectra (2+2cos(k*pi/5), 2+-sqrt2)\n");
    sv_expect_rc(t, rc, 1, "tri4");
    sv_expect_w(t, w4, "tri4");
    sv_invariants(t, t4, 4, 4, 1e-9, "tri4");
    rc = ac_pro_svd_all(t3, 3, 3);
    sv_expect_rc(t, rc, 1, "tri3");
    sv_expect_w(t, w3, "tri3");
    sv_invariants(t, t3, 3, 3, 1e-9, "tri3");
}

/* ---- Group 2: rectangular 矩形 ---- */

/* [case SVD-6] 恒等/置换:重 σ 谱 w 精确 + 不变量(契约 §5 语义实证) */
void test_svd_eye_perm(svd_tally_t *t) {
    double eye[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    double perm[9] = { 0,0,1, 1,0,0, 0,1,0 };
    double w4[4] = { 1, 1, 1, 1 };
    double w3[3] = { 1, 1, 1 };
    int rc = ac_pro_svd_all(eye, 4, 4);
    printf("[case SVD-6] identity/cyclic permutation: repeated sigma\n");
    sv_expect_rc(t, rc, 1, "eye4");
    sv_expect_w(t, w4, "eye4");
    sv_invariants(t, eye, 4, 4, 1e-9, "eye4");
    rc = ac_pro_svd_all(perm, 3, 3);
    sv_expect_rc(t, rc, 1, "perm3");
    sv_expect_w(t, w3, "perm3");
    sv_invariants(t, perm, 3, 3, 1e-9, "perm3");
}

/* [case SVD-7] 符号置换×对角×置换ᵗ 合成:全链精确可验 */
void test_svd_permcombo(svd_tally_t *t) {
    double m[16] = { 0,16,0,0, 0,0,0,1, 9,0,0,0, 0,0,4,0 };
    double w[4] = { 16, 9, 4, 1 };
    double u0[4] = { 1,0,0,0 }, u1[4] = { 0,0,1,0 }, u2[4] = { 0,0,0,1 }, u3[4] = { 0,1,0,0 };
    double v0[4] = { 0,1,0,0 }, v1[4] = { 1,0,0,0 }, v2[4] = { 0,0,1,0 }, v3[4] = { 0,0,0,1 };
    int rc = ac_pro_svd_all(m, 4, 4);
    printf("[case SVD-7] signed-perm x diag x perm^T: exact uv chain\n");
    sv_expect_rc(t, rc, 1, "permcombo");
    sv_expect_w(t, w, "permcombo");
    sv_expect_col(t, 1, 0, u0, 4, "permcombo");
    sv_expect_col(t, 1, 1, u1, 4, "permcombo");
    sv_expect_col(t, 1, 2, u2, 4, "permcombo");
    sv_expect_col(t, 1, 3, u3, 4, "permcombo");
    sv_expect_col(t, 0, 0, v0, 4, "permcombo");
    sv_expect_col(t, 0, 1, v1, 4, "permcombo");
    sv_expect_col(t, 0, 2, v2, 4, "permcombo");
    sv_expect_col(t, 0, 3, v3, 4, "permcombo");
    sv_invariants(t, m, 4, 4, 1e-9, "permcombo");
}

/* [case SVD-8] 3×2 与 2×3(m<n 转置路径)同谱对偶,全链精确 */
void test_svd_rect_pair(svd_tally_t *t) {
    double a32[6] = { 3, 0, 0, 0, 0, 4 };
    double a23[6] = { 3, 0, 0, 0, 0, 4 };
    double w[2] = { 4, 3 };
    double u32_0[3] = { 0, 0, 1 }, u32_1[3] = { 1, 0, 0 };
    double v32_0[2] = { 0, 1 }, v32_1[2] = { 1, 0 };
    double u23_0[2] = { 0, 1 }, u23_1[2] = { 1, 0 };
    double v23_0[3] = { 0, 0, 1 }, v23_1[3] = { 1, 0, 0 };
    int rc = ac_pro_svd_all(a32, 3, 2);
    printf("[case SVD-8] 3x2 / 2x3 transpose-path dual pair, exact uv\n");
    sv_expect_rc(t, rc, 1, "r32");
    sv_expect_w(t, w, "r32");
    sv_expect_col(t, 1, 0, u32_0, 3, "r32");
    sv_expect_col(t, 1, 1, u32_1, 3, "r32");
    sv_expect_col(t, 0, 0, v32_0, 2, "r32");
    sv_expect_col(t, 0, 1, v32_1, 2, "r32");
    sv_invariants(t, a32, 3, 2, 1e-9, "r32");
    rc = ac_pro_svd_all(a23, 2, 3);
    sv_expect_rc(t, rc, 1, "r23");
    sv_expect_w(t, w, "r23");
    sv_expect_col(t, 1, 0, u23_0, 2, "r23");
    sv_expect_col(t, 1, 1, u23_1, 2, "r23");
    sv_expect_col(t, 0, 0, v23_0, 3, "r23");
    sv_expect_col(t, 0, 1, v23_1, 3, "r23");
    sv_invariants(t, a23, 2, 3, 1e-9, "r23");
}

/* [case SVD-9] 1×n / n×1 形状边界(3,−4,0 → σ=5) */
void test_svd_row_col(svd_tally_t *t) {
    double r13[3] = { 3, -4, 0 };
    double r31[3] = { 3, -4, 0 };
    double r12[2] = { 3, 4 };
    double r21[2] = { 3, 4 };
    double w5[1] = { 5 };
    double u13[1] = { 1 };
    double v13[3] = { 0.6, -0.8, 0 };
    double u31[3] = { -0.6, 0.8, 0 };
    double v31[1] = { -1 };
    double v12[2] = { 0.6, 0.8 };
    double u21[2] = { 0.6, 0.8 };
    double v21[1] = { 1.0 };                /* U max 分量 0.8@idx1 已正,无翻转 */
    int rc;
    printf("[case SVD-9] 1xn / nx1 shape edges\n");
    rc = ac_pro_svd_all(r13, 1, 3);
    sv_expect_rc(t, rc, 1, "1x3");
    sv_expect_w(t, w5, "1x3");
    sv_expect_col(t, 1, 0, u13, 1, "1x3");
    sv_expect_col(t, 0, 0, v13, 3, "1x3");
    sv_invariants(t, r13, 1, 3, 1e-9, "1x3");
    rc = ac_pro_svd_all(r31, 3, 1);
    sv_expect_rc(t, rc, 1, "3x1");
    sv_expect_col(t, 1, 0, u31, 3, "3x1");
    sv_expect_col(t, 0, 0, v31, 1, "3x1");
    sv_invariants(t, r31, 3, 1, 1e-9, "3x1");
    rc = ac_pro_svd_all(r12, 1, 2);
    sv_expect_rc(t, rc, 1, "1x2");
    sv_expect_col(t, 1, 0, u13, 1, "1x2");
    sv_expect_col(t, 0, 0, v12, 2, "1x2");
    rc = ac_pro_svd_all(r21, 2, 1);
    sv_expect_rc(t, rc, 1, "2x1");
    sv_expect_col(t, 1, 0, u21, 2, "2x1");
    sv_expect_col(t, 0, 0, v21, 1, "2x1");
}

/* ---- Group 3: degenerate 秩亏 + 重 σ ---- */

/* [case SVD-10] rank-1 与零矩阵:补全列正交不变量(契约 §5) */
void test_svd_rank_deficient(svd_tally_t *t) {
    double r1[4] = { 1, 1, 1, 1 };
    double z32[6] = { 0, 0, 0, 0, 0, 0 };
    double z23[6] = { 0, 0, 0, 0, 0, 0 };
    double w10[2] = { 2, 0 };
    double wz[2] = { 0, 0 };
    int rc = ac_pro_svd_all(r1, 2, 2);
    printf("[case SVD-10] rank-1 / zero matrices: completion invariants\n");
    sv_expect_rc(t, rc, 1, "rank1");
    sv_expect_w(t, w10, "rank1");
    svcheck(t, ac_pro_svd_value(1) == 0.0, "rank1: w[1] == 0 exactly", "");
    sv_invariants(t, r1, 2, 2, 1e-9, "rank1");
    rc = ac_pro_svd_all(z32, 3, 2);
    sv_expect_rc(t, rc, 1, "zero32");
    sv_expect_w(t, wz, "zero32");
    sv_invariants(t, z32, 3, 2, 1e-9, "zero32");
    rc = ac_pro_svd_all(z23, 2, 3);
    sv_expect_rc(t, rc, 1, "zero23");
    sv_expect_w(t, wz, "zero23");
    sv_invariants(t, z23, 2, 3, 1e-9, "zero23");
}

/* [case SVD-11] 重 σ / 近重 σ:invariants-only 语义实证 */
void test_svd_repeated(svd_tally_t *t) {
    double rp[4] = { 2, 0, 0, 2 };
    double rs[9] = { 2, 1, 1, 1, 2, 1, 1, 1, 2 };
    double nr[4] = { 2, 0, 0, 2.0000002 };
    double wrp[2] = { 2, 2 };
    double wrs[3] = { 4, 1, 1 };
    double wnr[2] = { 2.0000002, 2 };
    int rc = ac_pro_svd_all(rp, 2, 2);
    printf("[case SVD-11] repeated / near-repeated sigma (invariants-only)\n");
    sv_expect_rc(t, rc, 1, "rep2");
    sv_expect_w(t, wrp, "rep2");
    sv_invariants(t, rp, 2, 2, 1e-9, "rep2");
    rc = ac_pro_svd_all(rs, 3, 3);
    sv_expect_rc(t, rc, 1, "sym3rep");
    sv_expect_w(t, wrs, "sym3rep");
    sv_invariants(t, rs, 3, 3, 1e-9, "sym3rep");
    rc = ac_pro_svd_all(nr, 2, 2);
    sv_expect_rc(t, rc, 1, "nearrep");
    sv_expect_w(t, wnr, "nearrep");
    sv_invariants(t, nr, 2, 2, 1e-9, "nearrep");
}

/* ---- Group 4: scale 极端幂 2 ---- */

/* [case SVD-12] 2^±150 对角与近贴 1e-150 谱:w 精确 + 缩放域不变量 */
void test_svd_pow2(svd_tally_t *t) {
    double big = ldexp(1.0, 150);
    double tiny = ldexp(1.0, -150);
    double mx[4] = { 0, 0, 0, 0 };
    double mn[4] = { 0, 0, 0, 0 };
    double ts[4] = { 1e-150, 0, 0, 2e-150 };
    double i2[4] = { 1, 0, 0, 1 };
    double x2[4] = { 0, 1, 1, 0 };
    int rc;
    printf("[case SVD-12] 2^+-150 diagonal extremes\n");
    mx[0] = big; mx[3] = tiny;
    mn[0] = 2 * tiny; mn[3] = tiny;
    rc = ac_pro_svd_all(mx, 2, 2);
    sv_expect_rc(t, rc, 1, "pow2mix");
    svcheck(t, sv_near_rel(ac_pro_svd_value(0), big, 1e-14),
            "pow2mix: w[0] == 2^150 (rel 1e-14, sqrt-rounding slack)", "");
    svcheck(t, ac_pro_svd_value(1) == 0.0, "pow2mix: w[1] zeroed by 1e-12 rel rule",
            "sigma ratio 2^-300 < zero-tol");
    sv_invariants(t, mx, 2, 2, 1e-9, "pow2mix");
    rc = ac_pro_svd_all(mn, 2, 2);
    sv_expect_rc(t, rc, 1, "pow2dn150");
    svcheck(t, sv_near_rel(ac_pro_svd_value(0), 2 * tiny, 1e-14) &&
               sv_near_rel(ac_pro_svd_value(1), tiny, 1e-14),
            "pow2dn150: w exact to 1e-14 rel", "");
    sv_invariants(t, mn, 2, 2, 1e-9, "pow2dn150");
    rc = ac_pro_svd_all(ts, 2, 2);
    sv_expect_rc(t, rc, 1, "tinyspread");
    svcheck(t, sv_near_rel(ac_pro_svd_value(0), 2e-150, 1e-14) &&
               sv_near_rel(ac_pro_svd_value(1), 1e-150, 1e-14),
            "tinyspread: w exact to 1e-14 rel (scaled domain normal-range)", "");
    sv_invariants(t, ts, 2, 2, 1e-9, "tinyspread");
    /* 符号规范化在极端尺度下仍成立:恒等排序路径 */
    rc = ac_pro_svd_all(i2, 2, 2);
    sv_expect_col(t, 1, 0, i2, 2, "eye2 scale");
    rc = ac_pro_svd_all(x2, 2, 2);
    svcheck(t, rc == 1, "swap2 scale rc", "");
}

/* [case SVD-13] 2^40 往返:w 逐位 ×2^40(幂 2 缩放精确性直接证据) */
void test_svd_roundtrip(svd_tally_t *t) {
    double base[4] = { 1, 2, 3, 4 };
    double sym[4] = { 2, 1, 1, 2 };
    double sc[4];
    double s40 = ldexp(1.0, 40);
    double w0[2], w1[2];
    double u0[4], u1[4], v0[4], v1[4];
    int i, rc;
    printf("[case SVD-13] 2^40 roundtrip: bitwise-exact rescale\n");
    rc = ac_pro_svd_all(base, 2, 2);
    sv_expect_rc(t, rc, 1, "roundtrip base");
    for (i = 0; i < 2; i++) { w0[i] = ac_pro_svd_value(i); }
    for (i = 0; i < 4; i++) { u0[i] = ac_pro_svd_u(i / 2)[i % 2]; v0[i] = ac_pro_svd_v(i / 2)[i % 2]; }
    for (i = 0; i < 4; i++) { sc[i] = base[i] * s40; }
    rc = ac_pro_svd_all(sc, 2, 2);
    sv_expect_rc(t, rc, 1, "roundtrip scaled");
    for (i = 0; i < 2; i++) { w1[i] = ac_pro_svd_value(i); }
    for (i = 0; i < 4; i++) { u1[i] = ac_pro_svd_u(i / 2)[i % 2]; v1[i] = ac_pro_svd_v(i / 2)[i % 2]; }
    svcheck(t, w1[0] == w0[0] * s40 && w1[1] == w0[1] * s40,
            "roundtrip: w == w0 * 2^40 bitwise", "");
    svcheck(t, memcmp(u0, u1, sizeof(u0)) == 0 && memcmp(v0, v1, sizeof(v0)) == 0,
            "roundtrip: U/V bitwise identical (power-2 invariance)", "");
    for (i = 0; i < 4; i++) { sc[i] = sym[i] * s40; }
    rc = ac_pro_svd_all(sc, 2, 2);
    sv_expect_rc(t, rc, 1, "roundtrip sym scaled");
    svcheck(t, sv_near_rel(ac_pro_svd_value(0), 3 * s40, 1e-14) &&
               sv_near_rel(ac_pro_svd_value(1), s40, 1e-14),
            "roundtrip sym: w == {3,1} * 2^40 (rel 1e-14)", "");
}

/* [case SVD-14] Hilbert 12×12:刚好通过维度边界 + 病态谱不变量 */
void test_svd_hilbert12(svd_tally_t *t) {
    double m[144];
    double dense[144];
    int i, j, rc;
    printf("[case SVD-14] Hilbert 12x12 (conditioning ~1e16) + dense 12x12\n");
    for (i = 0; i < 12; i++) {
        for (j = 0; j < 12; j++) {
            m[i * 12 + j] = 1.0 / (double)(i + j + 1);
            dense[i * 12 + j] = (double)(((i * 7 + j * 3) % 11) - 5);
        }
    }
    rc = ac_pro_svd_all(m, 12, 12);
    sv_expect_rc(t, rc, 1, "hilbert12");
    sv_invariants(t, m, 12, 12, 1e-9, "hilbert12");
    svcheck(t, ac_pro_svd_value(0) >= 1.0, "hilbert12: sigma1 ~ 1.9", "");
    rc = ac_pro_svd_all(dense, 12, 12);
    sv_expect_rc(t, rc, 1, "dense12");
    sv_invariants(t, dense, 12, 12, 1e-9, "dense12");
}

/* ---- Group 5: reject 全拒绝码 ---- */

/* [case SVD-15] 参数拒绝全集 + accessor 失败安全(契约 §3.9/-1) */
void test_svd_reject_params(svd_tally_t *t) {
    double m[4] = { 1, 0, 0, 1 };
    double nan_m[4] = { 1, 0, 0, 0 };
    double inf_m[4] = { 1, 0, 0, 1 };
    double ninf_m[4] = { 1, 0, 0, 1 };
    nan_m[3] = 0.0 / 0.0;          /* NaN */
    inf_m[0] = 1.0 / 0.0;          /* +Inf */
    ninf_m[0] = -1.0 / 0.0;        /* -Inf */
    printf("[case SVD-15] parameter rejects + accessor safety\n");
    sv_expect_rc(t, ac_pro_svd_all(0, 2, 2), -1, "rej null");
    sv_expect_rc(t, ac_pro_svd_all(m, 0, 2), -1, "rej m=0");
    sv_expect_rc(t, ac_pro_svd_all(m, 2, 0), -1, "rej n=0");
    sv_expect_rc(t, ac_pro_svd_all(m, -1, 2), -1, "rej m=-1");
    sv_expect_rc(t, ac_pro_svd_all(m, 13, 2), -1, "rej m=13 (刚好拒绝侧)");
    sv_expect_rc(t, ac_pro_svd_all(m, 2, 13), -1, "rej n=13");
    sv_expect_rc(t, ac_pro_svd_all(nan_m, 2, 2), -1, "rej NaN");
    sv_expect_rc(t, ac_pro_svd_all(inf_m, 2, 2), -1, "rej +Inf");
    sv_expect_rc(t, ac_pro_svd_all(ninf_m, 2, 2), -1, "rej -Inf");
    svcheck(t, ac_pro_svd_count() == 0, "rej: count == 0", "");
    svcheck(t, ac_pro_svd_value(0) == 0.0, "rej: value(0) == 0.0", "");
    svcheck(t, ac_pro_svd_u(0) == 0, "rej: u(0) == NULL", "");
    svcheck(t, ac_pro_svd_v(0) == 0, "rej: v(0) == NULL", "");
    svcheck(t, ac_pro_svd_value(-1) == 0.0, "rej: value(-1) == 0.0", "");
    svcheck(t, ac_pro_svd_u(-1) == 0 && ac_pro_svd_v(1) == 0, "rej: u(-1)/v(1) NULL", "");
}

/* [case SVD-16] 正交判据双侧边界 + 域内 rc ∉ {−2,−3} 反向断言 */
void test_svd_boundary(svd_tally_t *t) {
    double skip_m[4] = { 1, 1e-15, 0, 1 };
    double rot_m[4] = { 1, 1e-13, 0, 1 };
    double battery[12 * 12];        /* 域内反向覆盖批(栈上,零 static) */
    int i, j, rc, viol = 0;
    const double *v0;
    double v01;
    int rc1, rc2;
    printf("[case SVD-16] orthogonality threshold double-sided + domain rc sweep\n");
    rc1 = ac_pro_svd_all(skip_m, 2, 2);
    v0 = ac_pro_svd_v(0);
    v01 = v0[1];
    {
        double ws0 = ac_pro_svd_value(0), ws1 = ac_pro_svd_value(1);
        rc2 = ac_pro_svd_all(rot_m, 2, 2);
        sv_expect_rc(t, rc1, 1, "skip case");
        sv_expect_rc(t, rc2, 1, "rot case");
        svcheck(t, v01 == 0.0, "1e-15 off-diag: skipped, V bitwise identity (刚好通过)",
                "V01 != 0");
        svcheck(t, fabs(ac_pro_svd_v(0)[1]) > 0.0,
                "1e-13 off-diag: rotated, V non-identity (刚好拒绝侧)", "V01 == 0");
        svcheck(t, ws0 == 1.0 && ws1 == 1.0,
                "skip case: w == {1,1} exactly (norm sqrt round-trips)", "");
    }
    /* 域内反向覆盖:全形状 12×12 内抽样矩阵,rc ∈ {1}(合法有限输入),
       契约域内 -2/-3 不可触发的实证断言 */
    for (i = 0; i < 12; i++) {
        for (j = 0; j < 12; j++) {
            battery[i * 12 + j] = (double)(((i * 5 + j * 3) % 9) - 4);
        }
    }
    {
        int dims[8][2] = { {1,1},{1,12},{12,1},{2,12},{12,2},{3,12},{12,3},{12,12} };
        int d;
        for (d = 0; d < 8; d++) {
            rc = ac_pro_svd_all(battery, dims[d][0], dims[d][1]);
            if (rc == -2 || rc == -3) { viol++; }
            if (rc != 1) { viol++; }
        }
    }
    /* 12 阶刚好通过(12×12 已在 dims 内 rc==1)+ 失败码语义静态断言 */
    svcheck(t, viol == 0, "domain sweep: 8 shapes all rc==1 (no -2/-3)", "");
    svcheck(t, AC_PRO_SVD_ENOCONV == -2 && AC_PRO_SVD_EVERIFY == -3,
            "error code values frozen (-2/-3)", "");
}

/* 注册表:16 例五组(test_pro_runner.c SVD 锚点声明,main 注册块调用) */
void test_svd_all_cases(svd_tally_t *t) {
    test_svd_n1(t);
    test_svd_diag3(t);
    test_svd_diag_signs(t);
    test_svd_sym2(t);
    test_svd_tri(t);
    test_svd_eye_perm(t);
    test_svd_permcombo(t);
    test_svd_rect_pair(t);
    test_svd_row_col(t);
    test_svd_rank_deficient(t);
    test_svd_repeated(t);
    test_svd_pow2(t);
    test_svd_roundtrip(t);
    test_svd_hilbert12(t);
    test_svd_reject_params(t);
    test_svd_boundary(t);
}

#endif /* AC_PRO_ACTIVE */
