/* ============================================================
 * ApolloCalc-CAS · C/128KB-ProTEXT/test/test_pro_sparse.c
 * ------------------------------------------------------------
 * 【职责 / Responsibility】
 *   v2.1.0 方向④「稀疏线性方程组」的单元测试：12 个用例、35 条测试，
 *   覆盖功能主干（乱序三元组压缩、同列重复合并、行交换 + perm、稠密
 *   退化 CSR 对照、n=32 稀疏带状系统）、全部四类熔断（n 超限 / nnz
 *   超限 / 主元下限 / 结构奇异）以及填充溢出、下标非法、阈值主元的
 *   稀疏偏好。骨架期的探针冒烟（1 项测试）已被本文件整体取代。
 *
 * 【期望值来源 / Where the constants come from —— 确定性】
 *   全部期望值用 node 离线核算后以字面常量写入，不含任何运行期随机：
 *   - 小型用例（SP-1/2/3/12）与 SP-5 的右端 b：整数/半整数精确算术，
 *     node 按 A·x_true 逐项算出 b 后以常量写入；
 *   - SP-4（随机稠密对照）：矩阵由**定种子 LCG**生成 ——
 *         st = (1664525·st + 1013904223) mod 2^32，st0 = 12345，
 *         v = (st mod 2001)/100 − 10，按行主序先填 A 的 16 行、每行后
 *         填 b_i —— node（BigInt 精确复现同一 LCG）解出参考解 x̂，
 *     16 个分量以全精度字面量写入（容差 1e-6）；C 侧运行期用同一 LCG
 *     复现同一矩阵，两次运行逐位一致，校验确定。
 *
 * 【接口约定 / Interface】
 *   照抄 test/test_pro_multi.c / test_pro_eigen.c：本文件定义自己的
 *   pro_tally_t 与 pcheck 助手，导出 6 个入口函数返回计数结构，runner
 *   只汇总、不感知内部类型。入口函数不带 static（external linkage）。
 *
 * 【零静态存储纪律 / Zero static storage】
 *   本文件不声明任何静态对象（pcheck / LCG 助手是 static **函数**，
 *   不占 .bss），链接进来后 runner 口径的 .bss 增量仍是 0。
 * ============================================================ */

#include <math.h>
#include <stdio.h>
#include <stdint.h>

#include "../src/ac_pro.h"

#if AC_PRO_ACTIVE

typedef struct {
    int checks;
    int failed;
} pro_tally_t;

static void pcheck(pro_tally_t *t, int cond, const char *name, const char *detail) {
    t->checks++;
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        t->failed++;
        printf("  [FAIL] %s  (%s)\n", name, detail);
    }
}

/* ---- 定种子 LCG（与 node 离线核算逐位一致，见文件头）---- */
static uint32_t sp_lcg_next(uint32_t *st) {
    *st = 1664525u * (*st) + 1013904223u;   /* uint32 溢出回绕 = mod 2^32 */
    return *st;
}

static double sp_lcg_val(uint32_t *st) {
    return (double)(sp_lcg_next(st) % 2001u) / 100.0 - 10.0;
}

/* ==================== SP-1/2/3：主干功能（压缩/合并/交换） ==================== */
pro_tally_t test_sparse_basic(void) {
    pro_tally_t t;
    const double *x;
    const int16_t *pm;
    int rc, i;

    t.checks = 0;
    t.failed = 0;

    /* ---- [case SP-1] 乱序三元组 → CSR 压缩 → 求解 ----
     * A = [[2,1,1],[1,3,2],[1,0,0]]，x_true = [5,3,8]
     * b = [21,30,5]（node 离线核算 A·x_true）。 */
    printf("[case SP-1] 3x3 unsorted triplets -> CSR compress -> solve (x_true = [5,3,8])\n");
    {
        static const int32_t R[7] = {2, 0, 1, 0, 1, 0, 1};
        static const int32_t C[7] = {0, 2, 1, 0, 2, 1, 0};
        static const double  V[7] = {1, 1, 3, 2, 2, 1, 1};
        static const double  B[3] = {21, 30, 5};
        static const double  XT[3] = {5, 3, 8};
        rc = ac_pro_sparse_solve(3, 7, R, C, V, B);
        pcheck(&t, rc == AC_PRO_SP_OK, "rc == AC_PRO_SP_OK", "unexpected return");
        x = ac_pro_sparse_x();
        for (i = 0; i < 3; i++) {
            char name[96];
            sprintf(name, "x[%d] == %.17g (exact integer solution)", i, XT[i]);
            pcheck(&t, fabs(x[i] - XT[i]) < 1e-9, name, "component mismatch");
        }
        printf("         x = [%.4f, %.4f, %.4f]\n", x[0], x[1], x[2]);
    }

    /* ---- [case SP-2] 同坐标重复三元组求和合并：(0,0)=1+(0,0)=2 -> 3 ----
     * A = [[3,1],[0,4]]，x_true = [1,2]，b = [5,8]。 */
    printf("[case SP-2] duplicate triplets are summed: (0,0)=1+(0,0)=2 -> A[0][0]=3\n");
    {
        static const int32_t R[4] = {0, 0, 0, 1};
        static const int32_t C[4] = {0, 0, 1, 1};
        static const double  V[4] = {1, 2, 1, 4};
        static const double  B[2] = {5, 8};
        rc = ac_pro_sparse_solve(2, 4, R, C, V, B);
        pcheck(&t, rc == AC_PRO_SP_OK, "rc == AC_PRO_SP_OK", "unexpected return");
        x = ac_pro_sparse_x();
        pcheck(&t, fabs(x[0] - 1.0) < 1e-12, "x[0] == 1 (needs A[0][0] == 3)", "duplicate merge broken");
        pcheck(&t, fabs(x[1] - 2.0) < 1e-12, "x[1] == 2", "component mismatch");
    }

    /* ---- [case SP-3] 列 k 对角为零 → 必须行交换 ----
     * A = [[0,1],[1,0]]，b = [3,7] -> x = [7,3]；perm = [1,0]
     * （位置 0 持有原始行 1，与 A4 lu.piv 同语义）。 */
    printf("[case SP-3] zero-diagonal swap: A=[[0,1],[1,0]] -> x=[7,3], perm=[1,0]\n");
    {
        static const int32_t R[2] = {0, 1};
        static const int32_t C[2] = {1, 0};
        static const double  V[2] = {1, 1};
        static const double  B[2] = {3, 7};
        rc = ac_pro_sparse_solve(2, 2, R, C, V, B);
        pcheck(&t, rc == AC_PRO_SP_OK, "rc == AC_PRO_SP_OK", "unexpected return");
        x = ac_pro_sparse_x();
        pm = ac_pro_sparse_perm();
        pcheck(&t, fabs(x[0] - 7.0) < 1e-12, "x[0] == 7", "component mismatch");
        pcheck(&t, fabs(x[1] - 3.0) < 1e-12, "x[1] == 3", "component mismatch");
        pcheck(&t, pm[0] == 1 && pm[1] == 0, "perm == [1,0] (row swap recorded)", "perm wrong");
    }

    return t;
}

/* ==================== SP-4：随机稠密对照（退化 CSR） ====================
 * n=16 的稠密矩阵按全非零三元组（nnz=256）喂入 —— CSR 的退化形态。
 * 矩阵与右端由定种子 LCG 生成（node 离线核算参考解 x̂，常量见下）。 */
pro_tally_t test_sparse_dense_control(void) {
    pro_tally_t t;
    int32_t tr[256], tc[256];
    double tv[256], b[16], res;
    uint32_t st = 12345u;
    const double *x;
    int rc, i, j, k;
    double maxdiff;

    /* node 离线核算的参考解（同一定种子 LCG 矩阵，全精度字面量） */
    static const double XHAT[16] = {
        1.2094877087180609, -3.902092639230997,  0.08495316205708504,
        0.2866029578495151,  4.485319542078071, -3.1160583090211618,
        1.1545608614829896, -4.557985425056517,  1.54913389475994,
        3.037825879740886,  -1.7407680514468644, 2.235312493027844,
       -3.294254057106672,   1.540114368937085, -0.8463530667540689,
       -1.9381395241641821
    };

    t.checks = 0;
    t.failed = 0;

    printf("[case SP-4] dense 16x16 random control (degenerate CSR, nnz=256, LCG seed 12345)\n");
    k = 0;
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j++) {
            double v = sp_lcg_val(&st);
            tr[k] = (int32_t)i;
            tc[k] = (int32_t)j;
            tv[k] = v;
            k++;
        }
        b[i] = sp_lcg_val(&st);
    }
    rc = ac_pro_sparse_solve(16, 256, tr, tc, tv, b);
    pcheck(&t, rc == AC_PRO_SP_OK, "rc == AC_PRO_SP_OK (dense control solves)", "unexpected return");
    x = ac_pro_sparse_x();

    maxdiff = 0.0;
    for (i = 0; i < 16; i++) {
        double d = fabs(x[i] - XHAT[i]);
        if (d > maxdiff) maxdiff = d;
    }
    pcheck(&t, maxdiff < 1e-6, "max |x - xhat(node)| < 1e-6", "differs from node reference");

    res = 0.0;
    for (i = 0; i < 16; i++) {
        double s = 0.0;
        for (j = 0; j < 16; j++) s += tv[(size_t)i * 16 + j] * x[j];
        if (fabs(s - b[i]) > res) res = fabs(s - b[i]);
    }
    pcheck(&t, res < 1e-8, "residual max |Ax - b| < 1e-8", "residual too large");
    printf("         max|x-xhat| = %.3e, residual = %.3e\n", maxdiff, res);
    return t;
}

/* ==================== SP-5：n=32 稀疏带状系统（n > 12 的主打能力） ====================
 * 对角 5、次对角 ±1/−2、两角各一个远端非零元，nnz_in = 96。
 * x_true[i] = ((7i) mod 13) − 6（精确整数），b 由 node 离线算出（常量）。 */
pro_tally_t test_sparse_banded32(void) {
    pro_tally_t t;
    int32_t tr[96], tc[96];
    double tv[96], res;
    const double *x;
    int rc, i, j, nnz = 0;
    double maxdiff;

    static const double B[32] = {
       -29.5, 22, -29, 24, -27, 26, -25, 28, -23, 30, -21, 32,  -6, -31, 22, -29,
        24, -27, 26, -25, 28, -23, 30, -21, 32,  -6, -31, 22, -29, 24, -27, 21.5
    };

    t.checks = 0;
    t.failed = 0;

    printf("[case SP-5] banded n=32 sparse system (nnz_in=96, exact integer x_true)\n");
    for (i = 0; i < 32; i++) {
        for (j = 0; j < 32; j++) {
            double v = 0.0;
            if (i == j) v = 5.0;
            else if (j == i + 1) v = -1.0;
            else if (j == i - 1) v = -2.0;
            else if (i == 0 && j == 31) v = 0.5;
            else if (i == 31 && j == 0) v = 0.25;
            if (v != 0.0) {
                tr[nnz] = (int32_t)i;
                tc[nnz] = (int32_t)j;
                tv[nnz] = v;
                nnz++;
            }
        }
    }
    rc = ac_pro_sparse_solve(32, nnz, tr, tc, tv, B);
    pcheck(&t, rc == AC_PRO_SP_OK, "rc == AC_PRO_SP_OK (n=32 > dense-solver limit)", "unexpected return");
    x = ac_pro_sparse_x();

    maxdiff = 0.0;
    for (i = 0; i < 32; i++) {
        double xt = (double)(((i * 7) % 13) - 6);
        double d = fabs(x[i] - xt);
        if (d > maxdiff) maxdiff = d;
    }
    pcheck(&t, maxdiff < 1e-9, "max |x - x_true| < 1e-9 (exact integer solution)", "component mismatch");

    res = 0.0;
    for (i = 0; i < 32; i++) {
        double s = 0.0;
        for (j = 0; j < 32; j++) {
            int m;
            for (m = 0; m < nnz; m++) {
                if (tr[m] == i && tc[m] == j) { s += tv[m] * x[j]; break; }
            }
        }
        if (fabs(s - B[i]) > res) res = fabs(s - B[i]);
    }
    pcheck(&t, res < 1e-8, "residual max |Ax - b| < 1e-8", "residual too large");
    printf("         max|x-x_true| = %.3e, residual = %.3e\n", maxdiff, res);
    return t;
}

/* ==================== SP-6/7/8：超限熔断 ==================== */
pro_tally_t test_sparse_limits(void) {
    pro_tally_t t;
    static const int32_t R1[1] = {0};
    static const int32_t C1[1] = {0};
    static const double  V1[1] = {1};
    static const double  B1[1] = {1};
    int rc, k;

    t.checks = 0;
    t.failed = 0;

    /* ---- [case SP-6] n 超限：n=33 与 n=0 都拒绝 ---- */
    printf("[case SP-6] n over limit: n=33 and n=0 -> AC_PRO_SP_EN\n");
    rc = ac_pro_sparse_solve(33, 1, R1, C1, V1, B1);
    pcheck(&t, rc == AC_PRO_SP_EN, "n=33 -> AC_PRO_SP_EN", "wrong code");
    rc = ac_pro_sparse_solve(0, 1, R1, C1, V1, B1);
    pcheck(&t, rc == AC_PRO_SP_EN, "n=0 -> AC_PRO_SP_EN", "wrong code");

    /* ---- [case SP-7] nnz 超限：nnz=321 与 nnz=0 都拒绝 ---- */
    printf("[case SP-7] nnz over limit: nnz=321 and nnz=0 -> AC_PRO_SP_ENNZ\n");
    {
        int32_t R[321], C[321];          /* 栈上自动数组（禁可变 static 对象） */
        double V[321];
        double B2[2];
        for (k = 0; k < 321; k++) { R[k] = (int32_t)(k % 2); C[k] = (int32_t)(k % 2); V[k] = 1.0; }
        B2[0] = 1.0; B2[1] = 1.0;
        rc = ac_pro_sparse_solve(2, 321, R, C, V, B2);
        pcheck(&t, rc == AC_PRO_SP_ENNZ, "nnz=321 -> AC_PRO_SP_ENNZ", "wrong code");
        rc = ac_pro_sparse_solve(2, 0, R, C, V, B2);
        pcheck(&t, rc == AC_PRO_SP_ENNZ, "nnz=0 -> AC_PRO_SP_ENNZ", "wrong code");
    }

    /* ---- [case SP-8] 三元组下标越界：col=5 在 n=2 中非法 ---- */
    printf("[case SP-8] triplet index out of range: col=5 with n=2 -> AC_PRO_SP_EINDEX\n");
    {
        static const int32_t R2[1] = {0};
        static const int32_t C2[1] = {5};
        rc = ac_pro_sparse_solve(2, 1, R2, C2, V1, B1);
        pcheck(&t, rc == AC_PRO_SP_EINDEX, "col=5 >= n=2 -> AC_PRO_SP_EINDEX", "wrong code");
    }
    return t;
}

/* ==================== SP-9/10/11：填充溢出 / 奇异 / 主元下限 ==================== */
pro_tally_t test_sparse_robustness(void) {
    pro_tally_t t;
    int rc, i;

    t.checks = 0;
    t.failed = 0;

    /* ---- [case SP-9] 填充溢出 ----
     * 首行近稠密（a_00=1000 压住阈值选择）+ 30 个星型行（每行仅
     * {col0, col31}）：第 0 步以稠密首行为主元，星型行全部被填充成
     * 31 列满行；node 离线核证：总非零元在第 8 行更新时 296+29=325
     * > 320 → AC_PRO_SP_EFILL。输入 nnz=93 ≤ 320，合法输入、容量熔断。 */
    printf("[case SP-9] fill overflow: dense pivot row floods star rows -> AC_PRO_SP_EFILL\n");
    {
        int32_t R[93], C[93];
        double V[93], B[32];
        int nnz = 0;
        R[nnz] = 0; C[nnz] = 0; V[nnz] = 1000.0; nnz++;
        for (i = 1; i < 32; i++) { R[nnz] = 0; C[nnz] = i; V[nnz] = 1.0; nnz++; }
        for (i = 1; i <= 30; i++) {
            R[nnz] = i; C[nnz] = 0;  V[nnz] = 1.0; nnz++;
            R[nnz] = i; C[nnz] = 31; V[nnz] = 1.0; nnz++;
        }
        R[nnz] = 31; C[nnz] = 31; V[nnz] = 1.0; nnz++;
        for (i = 0; i < 32; i++) B[i] = (double)(i + 1);
        rc = ac_pro_sparse_solve(32, nnz, R, C, V, B);
        pcheck(&t, rc == AC_PRO_SP_EFILL, "fill > 320 -> AC_PRO_SP_EFILL (no silent drop)", "wrong code");
    }

    /* ---- [case SP-10] 结构奇异 ---- */
    printf("[case SP-10] structurally singular: empty column / cancellation -> AC_PRO_SP_ESING\n");
    {
        static const int32_t RA[2] = {0, 1};   /* 列 1 无任何非零元 */
        static const int32_t CA[2] = {0, 0};
        static const double  VA[2] = {2, 1};
        static const double  BA[2] = {1, 1};
        rc = ac_pro_sparse_solve(2, 2, RA, CA, VA, BA);
        pcheck(&t, rc == AC_PRO_SP_ESING, "empty column -> AC_PRO_SP_ESING", "wrong code");
    }
    {
        static const int32_t RB[4] = {0, 0, 1, 1};   /* [[1,2],[2,4]] 秩 1 */
        static const int32_t CB[4] = {0, 1, 0, 1};
        static const double  VB[4] = {1, 2, 2, 4};
        static const double  BB[2] = {3, 6};
        rc = ac_pro_sparse_solve(2, 4, RB, CB, VB, BB);
        pcheck(&t, rc == AC_PRO_SP_ESING, "rank-1 cancellation -> AC_PRO_SP_ESING", "wrong code");
    }

    /* ---- [case SP-11] 主元低于下限：列最大模 1e-14 ∈ (0, 1e-12) ---- */
    printf("[case SP-11] pivot below floor: column max 1e-14 -> AC_PRO_SP_EPIVOT\n");
    {
        static const int32_t R[3] = {0, 1, 1};
        static const int32_t C[3] = {0, 0, 1};
        static const double  V[3] = {2, 1, 1e-14};
        static const double  B[2] = {1, 1};
        rc = ac_pro_sparse_solve(2, 3, R, C, V, B);
        pcheck(&t, rc == AC_PRO_SP_EPIVOT, "column max 1e-14 < floor 1e-12 -> AC_PRO_SP_EPIVOT", "wrong code");
    }
    return t;
}

/* ==================== SP-12：阈值主元的稀疏偏好 ====================
 * 第 0 步列 0 有三个等模 2 的候选：行 0（4 非零元）、行 1/2（各 2 个）。
 * 纯部分主元（同模取行号最小）会选行 0；阈值主元按「行非零元最少」
 * 选行 1 —— perm[0] == 1 是阈值行为存在的直接证据。
 * x_true = [1,2,3,4]，b = [11,10,14,32]（node 离线核算）。 */
pro_tally_t test_sparse_threshold(void) {
    pro_tally_t t;
    static const int32_t R[9] = {0, 0, 0, 0, 1, 1, 2, 2, 3};
    static const int32_t C[9] = {0, 1, 2, 3, 0, 1, 0, 2, 3};
    static const double  V[9] = {2, 1, 1, 1, 2, 4, 2, 4, 8};
    static const double  B[4] = {11, 10, 14, 32};
    static const double  XT[4] = {1, 2, 3, 4};
    static const int16_t PM[4] = {1, 2, 0, 3};
    const double *x;
    const int16_t *pm;
    int rc, i;

    t.checks = 0;
    t.failed = 0;

    printf("[case SP-12] threshold pivoting prefers sparse pivot row: perm == [1,2,0,3], x == [1,2,3,4]\n");
    rc = ac_pro_sparse_solve(4, 9, R, C, V, B);
    pcheck(&t, rc == AC_PRO_SP_OK, "rc == AC_PRO_SP_OK", "unexpected return");
    x = ac_pro_sparse_x();
    pm = ac_pro_sparse_perm();
    for (i = 0; i < 4; i++) {
        char name[96];
        sprintf(name, "x[%d] == %.17g", i, XT[i]);
        pcheck(&t, fabs(x[i] - XT[i]) < 1e-12, name, "component mismatch");
    }
    for (i = 0; i < 4; i++) {
        char name[96];
        sprintf(name, "perm[%d] == %d (threshold chose the sparser row at step 0)", i, PM[i]);
        pcheck(&t, pm[i] == PM[i], name, "perm mismatch");
    }
    printf("         x = [%.4f, %.4f, %.4f, %.4f], perm = [%d,%d,%d,%d]\n",
           x[0], x[1], x[2], x[3], pm[0], pm[1], pm[2], pm[3]);
    return t;
}

#endif /* AC_PRO_ACTIVE */
