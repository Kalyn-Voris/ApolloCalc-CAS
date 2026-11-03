/* ApolloCalc · svd_runner.c — v2.6.0 G2 SVD 差分对拍 C 侧探针(独立可执行,
 * 由 Makefile SVD 锚点的 svd_runner 目标构建,不进 TEST_SRC/runner tally)。
 *
 * 职责:读语料 CSV(tests/bench/svd-corpus.csv,235 行 = 52 契约锚 + 183 结构谱)
 * 逐条跑 C 侧 ac_pro_svd_all,并在探针内**独立重算**不变量断言(不信任
 * 被测代码的闸门):rc 期望精确、期望谱逐位(混合容差)、w 降序非负、
 * 重构残差 ≤ 1e-9·max(1,‖A‖∞)、‖UᵗU−I‖∞ ≤ 1e-9、‖VᵗV−I‖∞ ≤ 1e-9、
 * 符号规范化(U 每列首个最大 |·| 分量 > 0)。任何一条失败即计数,收尾
 * 汇总,非零退出。**exit 0 为验收线**。
 *
 * 输出 tests/bench/svd128-c.csv(随探针提交,v2.7 JS 面对拍金标准):
 *   svd,<id>,<rc>,<k>,<w…k>,<U 行主序 m×k>,<V 行主序 n×k>   (%.17g,往返逐位一致)
 *   拒绝用例只写 svd,<id>,<rc>。
 *
 * 用法:svd_runner.exe <corpus.csv> <out.csv>
 * 语料行格式(契约 §6 冻结):id,m,n,kind,tol,expect,a00,a01,…
 *   expect = ""(不变量) | "-1"(期望参数拒绝) | 降序 w 列表 ';'-分隔;
 *   tol 空 = 1e-9;非有限条目字面量 nan/inf/-inf(C strtod 同值)。
 * 零 malloc、零 static 对象、零递归(与内核同款纪律);缓冲全在栈。 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../src/ac_pro.h"

#if AC_PRO_ACTIVE

#define PROBE_MAX_FIELDS  192      /* 6 + 13×13 余量 */
#define PROBE_MAX_VALS    169      /* 13×13 */
#define PROBE_LINE        8192
#define PROBE_TOL         1e-9

/* 手工字段切分:保留空字段(strtok 会吞掉连续逗号,语料 expect 常为空) */
static int probe_split(char *line, char **fields, int maxf) {
    int nf = 1;
    char *p = line;
    fields[0] = line;
    while (*p != '\0') {
        if (*p == ',') {
            *p = '\0';
            if (nf >= maxf) { return -1; }
            fields[nf++] = p + 1;
        }
        p++;
    }
    return nf;
}

static int probe_near(double a, double b, double tol) {
    double den = fabs(b);
    if (den < 1.0) { den = 1.0; }
    return fabs(a - b) <= tol * den;
}

/* 不变量:重构残差(原始标度,对调用方 A 与 accessor 独立重算) */
static int probe_check_recon(const double *a, int m, int n, double tol) {
    int k = ac_pro_svd_count();
    int i, j, l;
    double anorm = 0.0, r1 = 0.0;
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
    return r1 <= tol * ((anorm > 1.0) ? anorm : 1.0);
}

static int probe_check_ortho(int m, int n, double tol) {
    int k = ac_pro_svd_count();
    int i, p, q;
    for (p = 0; p < k; p++) {
        for (q = 0; q < k; q++) {
            double du = 0.0, dv = 0.0;
            for (i = 0; i < m; i++) { du += ac_pro_svd_u(p)[i] * ac_pro_svd_u(q)[i]; }
            for (i = 0; i < n; i++) { dv += ac_pro_svd_v(p)[i] * ac_pro_svd_v(q)[i]; }
            if (fabs((((p == q) ? 1.0 : 0.0)) - du) > tol) { return 0; }
            if (fabs((((p == q) ? 1.0 : 0.0)) - dv) > tol) { return 0; }
        }
    }
    return 1;
}

/* 符号规范化不变量:U 每列首个最大 |·| 分量严格为正 */
static int probe_check_signnorm(int m) {
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

int main(int argc, char **argv) {
    FILE *in, *out;
    char line[PROBE_LINE];
    char *fields[PROBE_MAX_FIELDS];
    double vals[PROBE_MAX_VALS];
    double wout[AC_PRO_MATRIX_MAX_DIM];
    double uout[AC_PRO_MATRIX_MAX_SIZE];
    double vout[AC_PRO_MATRIX_MAX_SIZE];
    long total = 0, failed = 0;

    if (argc != 3) {
        fprintf(stderr, "usage: svd_runner <corpus.csv> <out.csv>\n");
        return 2;
    }
    in = fopen(argv[1], "r");
    if (in == 0) {
        fprintf(stderr, "svd_runner: cannot open corpus %s\n", argv[1]);
        return 2;
    }
    out = fopen(argv[2], "w");
    if (out == 0) {
        fclose(in);
        fprintf(stderr, "svd_runner: cannot open output %s\n", argv[2]);
        return 2;
    }

    while (fgets(line, (int)sizeof(line), in) != 0) {
        size_t len = strlen(line);
        int nf, m, n, i, nv, rc, ok = 1;
        double tol = PROBE_TOL;
        const char *id, *kind, *expect;
        char *wlist;
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) { continue; }
        nf = probe_split(line, fields, PROBE_MAX_FIELDS);
        if (nf < 6) {
            fprintf(stderr, "svd_runner: bad row (fields=%d)\n", nf);
            failed++;
            total++;
            continue;
        }
        id = fields[0];
        m = atoi(fields[1]);
        n = atoi(fields[2]);
        kind = fields[3];
        if (fields[4][0] != '\0') { tol = strtod(fields[4], 0); }
        expect = fields[5];
        nv = 0;
        for (i = 6; i < nf; i++) {
            if (nv >= PROBE_MAX_VALS) { break; }
            vals[nv++] = strtod(fields[i], 0);
        }
        total++;

        rc = ac_pro_svd_all(vals, m, n);
        /* 维度拒绝行允许无矩阵域(impl 先查维度后读元素);域内行必须带满
           m×n 个值,否则是语料缺陷,按探针失败计 */
        if (!(m >= 1 && n >= 1 && m <= 12 && n <= 12) && strcmp(expect, "-1") == 0) {
            ok = (rc == -1);
            fprintf(out, "svd,%s,%d\n", id, rc);
        } else if (strcmp(expect, "-1") == 0) {
            ok = (rc == -1);
            fprintf(out, "svd,%s,%d\n", id, rc);
        } else {
            if (nv < m * n) {
                fprintf(stderr, "svd_runner: row %s: %d values < m*n = %d\n",
                        id, nv, m * n);
                ok = 0;
            }
            if (rc != 1) { ok = 0; }
            if (rc == 1) {
                int k = ac_pro_svd_count();
                if (k != ((m < n) ? m : n)) { ok = 0; }
                /* w 降序非负 */
                for (i = 0; i < k; i++) {
                    wout[i] = ac_pro_svd_value(i);
                    if (wout[i] < 0.0 || isnan(wout[i])) { ok = 0; }
                    if (i > 0 && wout[i - 1] < wout[i]) { ok = 0; }
                }
                /* 期望谱逐位(混合容差) */
                if (expect[0] != '\0') {
                    wlist = (char *)expect;
                    for (i = 0; i < k; i++) {
                        char *semi = strchr(wlist, ';');
                        double want;
                        if (semi == 0 && i + 1 < k) { ok = 0; break; }
                        if (semi != 0) { *semi = '\0'; }
                        want = strtod(wlist, 0);
                        if (!probe_near(wout[i], want, tol)) { ok = 0; }
                        if (semi == 0) { break; }
                        wlist = semi + 1;
                    }
                }
                /* 不变量(独立重算) */
                if (!probe_check_recon(vals, m, n, PROBE_TOL)) { ok = 0; }
                if (!probe_check_ortho(m, n, PROBE_TOL)) { ok = 0; }
                if (!probe_check_signnorm(m)) { ok = 0; }
                /* 输出金标准 CSV:U/V 行主序 */
                fprintf(out, "svd,%s,%d,%d", id, rc, k);
                for (i = 0; i < k; i++) { fprintf(out, ",%.17g", wout[i]); }
                {
                    int j;
                    for (j = 0; j < k; j++) {
                        const double *u = ac_pro_svd_u(j);
                        for (i = 0; i < m; i++) { uout[i * k + j] = u[i]; }
                    }
                    for (j = 0; j < k; j++) {
                        const double *v = ac_pro_svd_v(j);
                        for (i = 0; i < n; i++) { vout[i * k + j] = v[i]; }
                    }
                }
                for (i = 0; i < m * k; i++) { fprintf(out, ",%.17g", uout[i]); }
                for (i = 0; i < n * k; i++) { fprintf(out, ",%.17g", vout[i]); }
                fprintf(out, "\n");
            } else {
                fprintf(out, "svd,%s,%d\n", id, rc);
            }
        }
        if (!ok) {
            failed++;
            fprintf(stderr, "svd_runner: FAIL %s (rc=%d)\n", id, rc);
        }
        (void)kind;
    }
    fclose(in);
    fclose(out);
    printf("svd_runner: %ld rows, %ld failed (%s)\n", total, failed,
           (failed == 0) ? "PASS" : "FAIL");
    return (failed == 0) ? 0 : 1;
}

#endif /* AC_PRO_ACTIVE */
