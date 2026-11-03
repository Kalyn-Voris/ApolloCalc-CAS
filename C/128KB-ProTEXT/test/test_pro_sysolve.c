/* ApolloCalc · test_pro_sysolve.c — F2 二元方程组 GB 消元数值全解(C 侧)验收测试

   比对 docs/contracts/sysolve-format-v25.md 冻结的期望串(成功解列表按 §5.2
   排序、fmt 三级渲染逐字:实根 fmt(re);复根 re≠0 写 "[re]±|im|i"、re≈0
   整段省略且 |im|≈1 只写 i)与错误码(§7 精确比较)。期望值独立核算:整数解
   解析可得;DK 路径用例按闭式值 + fmt 规则离线展开,并与 tests/systems.json
   (node 高精度独立核算 + 逐解残差复核)交叉锚定;差分驱动 diff-sysolve.js
   另行对拍。

   自带 sy_tally_t,不 include runner 内部头(与 test_pro_sqfree.c 同约定)。
   不声明静态对象:本 TU 的 .bss 必须是 0。 */

#include <stdio.h>
#include <string.h>

#include "../src/ac_pro.h"
#include "../../../C/128KB/src/ac_parser.h"    /* g_matrix_pool(线性系交叉验证) */
#include "../../../C/128KB/src/ac_solvers.h"

#if AC_PRO_ACTIVE

typedef struct {
    int checks;
    int failed;
} sy_tally_t;

static void sycheck(sy_tally_t *t, int cond, const char *name, const char *detail) {
    t->checks++;
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        t->failed++;
        printf("  [FAIL] %s  (%s)\n", name, detail);
    }
}

static void expect_sysolve(sy_tally_t *t, const char *f, const char *g, int want_rc,
                           const char *want, const char *name) {
    char out[2048];
    char detail[512];
    int rc;

    out[0] = '\0';
    rc = ac_pro_sysolve(f, g, out, (int)sizeof(out));
    if (rc == want_rc && (want == 0 || strcmp(out, want) == 0)) {
        sycheck(t, 1, name, "");
        return;
    }
    sprintf(detail, "rc=%d got=\"%.200s\" want rc=%d \"%s\"", rc, out, want_rc,
            (want == 0) ? "<any>" : want);
    sycheck(t, 0, name, detail);
}

/* 契约 sys-A:经典二次系 4 解,h=y^4-4y^2+1 走精确根式 */
void test_sysolve_quartic_system(sy_tally_t *t) {
    printf("[case SYSOLVE-1] contract sys-A: x^2+y^2-4; xy-1 (4 solutions)\n");
    expect_sysolve(t, "x^2+y^2-4", "xy-1", AC_PRO_SYS_OK,
                   "[(0.517638090205, 1.931851652578), (1.931851652578, 0.517638090205),"
                   " (-1.931851652578, -0.517638090205), (-0.517638090205, -1.931851652578)]",
                   "x^2+y^2-4; xy-1");
}

/* 契约 sys-B:2 实解 + 复数解路径(耦合代回 DK;x=±1.272i) */
void test_sysolve_complex_solutions(sy_tally_t *t) {
    printf("[case SYSOLVE-2] contract sys-B: x^2+y^2-1; x^2-y (complex branch)\n");
    expect_sysolve(t, "x^2+y^2-1", "x^2-y", AC_PRO_SYS_OK,
                   "[(0.786151377757, 0.61803398875), (-0.786151377757, 0.61803398875),"
                   " (1.272019649514i, -1.61803398875), (-1.272019649514i, -1.61803398875)]",
                   "x^2+y^2-1; x^2-y");
}

/* 契约 sys-C:GB={1} → 成功 0 解 "[]" */
void test_sysolve_empty_set(sy_tally_t *t) {
    printf("[case SYSOLVE-3] contract sys-C: x-1; x-2 (GB={1} -> [])\n");
    expect_sysolve(t, "x-1", "x-2", AC_PRO_SYS_OK, "[]", "x-1; x-2 inconsistent");
    /* <x-1, 2x-2> = <x-1>:无 y-单变元元 → 正维数(契约 §4.3) */
    expect_sysolve(t, "x-1", "2x-2", AC_PRO_SYS_EZDIM, 0, "x-1; 2x-2 = line (EZDIM)");
}

/* 契约 sys-D/sys-M:正维数诚实拒绝 */
void test_sysolve_ezdim(sy_tally_t *t) {
    printf("[case SYSOLVE-4] contract sys-D/sys-M: positive dimension -> EZDIM\n");
    expect_sysolve(t, "x+y-1", "2x+2y-2", AC_PRO_SYS_EZDIM, 0, "no y-univariate element");
    expect_sysolve(t, "y^2", "xy", AC_PRO_SYS_EZDIM, 0, "all mixed vanish at y=0");
    expect_sysolve(t, "y^2+1", "0", AC_PRO_SYS_EZDIM, 0, "x unconstrained (g=0)");
}

/* 契约 sys-E/sys-K:解耦 GB 交叉积 */
void test_sysolve_decoupled(sy_tally_t *t) {
    printf("[case SYSOLVE-5] contract sys-E/sys-K: decoupled cross product\n");
    expect_sysolve(t, "x^2-1", "y-2", AC_PRO_SYS_OK, "[(1, 2), (-1, 2)]", "x^2-1; y-2");
    expect_sysolve(t, "x^2-1", "y^2-4", AC_PRO_SYS_OK,
                   "[(1, 2), (-1, 2), (1, -2), (-1, -2)]", "x^2-1; y^2-4");
}

/* 契约 sys-F:切相重根(h=(y-1)^2 经 sqfree 归一) */
void test_sysolve_tangent(sy_tally_t *t) {
    printf("[case SYSOLVE-6] contract sys-F: tangent (h=(y-1)^2)\n");
    expect_sysolve(t, "x^2+y^2-2", "x+y-2", AC_PRO_SYS_OK, "[(1, 1)]", "tangent circle+line");
}

/* 契约 sys-G:EDEG 可达失败(输入仅 3 次) */
void test_sysolve_edeg_reachable(sy_tally_t *t) {
    printf("[case SYSOLVE-7] contract sys-G: x^3+y^3-1; xy-1 -> EDEG\n");
    expect_sysolve(t, "x^3+y^3-1", "xy-1", AC_PRO_SYS_EDEG, 0, "GB degree fuse");
}

/* 契约 sys-H:h 次数 4 路由边界(精确根式路径) */
void test_sysolve_route4(sy_tally_t *t) {
    printf("[case SYSOLVE-8] contract sys-H: h deg 4 exact route\n");
    expect_sysolve(t, "y^4-5y^2+4", "x-y", AC_PRO_SYS_OK,
                   "[(2, 2), (1, 1), (-1, -1), (-2, -2)]", "y^4-5y^2+4; x-y");
}

/* 契约 sys-I:h 次数 5 路由边界(DK 路径;复数解) */
void test_sysolve_route5(sy_tally_t *t) {
    printf("[case SYSOLVE-9] contract sys-I: h deg 5 DK route (xy^2-1; x-y^3)\n");
    expect_sysolve(t, "xy^2-1", "x-y^3", AC_PRO_SYS_OK,
                   "[(1, 1),"
                   " ([-0.809016994375]-0.587785252292i, [0.309016994375]+0.951056516295i),"
                   " ([-0.809016994375]+0.587785252292i, [0.309016994375]-0.951056516295i),"
                   " ([0.309016994375]+0.951056516295i, [-0.809016994375]+0.587785252292i),"
                   " ([0.309016994375]-0.951056516295i, [-0.809016994375]-0.587785252292i)]",
                   "xy^2-1; x-y^3 (h=y^5-1)");
}

/* 契约 sys-J:线性系 + 上游 solveLinearSystem 交叉验证 */
void test_sysolve_linear_cross(sy_tally_t *t) {
    printf("[case SYSOLVE-10] contract sys-J: linear system cross-check\n");
    expect_sysolve(t, "x+y-3", "x-y-1", AC_PRO_SYS_OK, "[(2, 1)]", "x+y-3; x-y-1");
    /* 上游矩阵求解器独立重算同一系统:g_matrix_pool 行主序 n x (n+1) 增广 */
    g_matrix_pool[0] = 1.0; g_matrix_pool[1] = 1.0; g_matrix_pool[2] = 3.0;
    g_matrix_pool[3] = 1.0; g_matrix_pool[4] = -1.0; g_matrix_pool[5] = 1.0;  /* x-y=1, eq x-y-1=0 */
    ac_solve_linear_system(2);
    sycheck(t, g_root_count == 2 && g_root_re[0] == 2.0 && g_root_re[1] == 1.0,
            "upstream linear solver agrees (2,1)", "linear cross");
    ac_solvers_reset();
}

/* 契约 sys-L:h 次数 6(DK;2 实解 + 4 复解) */
void test_sysolve_deg6(sy_tally_t *t) {
    printf("[case SYSOLVE-11] contract sys-L: h deg 6 (y^6+y^2-2)\n");
    expect_sysolve(t, "y^3-x", "x^2+y^2-2", AC_PRO_SYS_OK,
                   "[(1, 1),"
                   " ([-1.632241882312]+0.405232726187i, [0.676096724727]+0.978318343479i),"
                   " ([-1.632241882312]-0.405232726187i, [0.676096724727]-0.978318343479i),"
                   " ([1.632241882312]+0.405232726187i, [-0.676096724727]+0.978318343479i),"
                   " ([1.632241882312]-0.405232726187i, [-0.676096724727]-0.978318343479i),"
                   " (-1, -1)]",
                   "y^3-x; x^2+y^2-2");
}

/* 契约 sys-N/sys-O:h 次数 7 / 8(DK 全功能路由上限) */
void test_sysolve_deg78(sy_tally_t *t) {
    printf("[case SYSOLVE-12] contract sys-N/sys-O: h deg 7 and 8\n");
    expect_sysolve(t, "xy^3-1", "x-y^4", AC_PRO_SYS_OK,
                   "[(1, 1),"
                   " ([-0.900968867902]-0.433883739118i, [0.623489801859]+0.781831482468i),"
                   " ([-0.900968867902]+0.433883739118i, [0.623489801859]-0.781831482468i),"
                   " ([0.623489801859]+0.781831482468i, [-0.222520933956]+0.974927912182i),"
                   " ([0.623489801859]-0.781831482468i, [-0.222520933956]-0.974927912182i),"
                   " ([-0.222520933956]-0.974927912182i, [-0.900968867902]+0.433883739118i),"
                   " ([-0.222520933956]+0.974927912182i, [-0.900968867902]-0.433883739118i)]",
                   "xy^3-1; x-y^4 (deg 7)");
    expect_sysolve(t, "x^2-1", "x-y^4", AC_PRO_SYS_OK,
                   "[(1, 1), (-1, [0.707106781187]+0.707106781187i),"
                   " (-1, [0.707106781187]-0.707106781187i), (1, i), (1, -i),"
                   " (-1, [-0.707106781187]+0.707106781187i),"
                   " (-1, [-0.707106781187]-0.707106781187i), (1, -1)]",
                   "x^2-1; x-y^4 (deg 8)");
}

/* 契约 sys-P:16 解 = Bézout 界封顶「刚好通过」 */
void test_sysolve_bezout_cap(sy_tally_t *t) {
    printf("[case SYSOLVE-13] contract sys-P: 16 solutions (Bezout cap)\n");
    expect_sysolve(t, "x^4-1", "y^4-1", AC_PRO_SYS_OK,
                   "[(1, 1), (i, 1), (-i, 1), (-1, 1),"
                   " (1, i), (i, i), (-i, i), (-1, i),"
                   " (1, -i), (i, -i), (-i, -i), (-1, -i),"
                   " (1, -1), (i, -1), (-i, -1), (-1, -1)]",
                   "x^4-1; y^4-1");
}

/* 契约 sys-S:候选去重(混合元 (x-1)^2 二重根) */
void test_sysolve_dedup(sy_tally_t *t) {
    printf("[case SYSOLVE-14] contract sys-S: candidate dedup\n");
    expect_sysolve(t, "x^2-2x+1", "y-1", AC_PRO_SYS_OK, "[(1, 1)]", "(x-1)^2; y-1");
}

/* 契约 §7 错误码:EARG / EDEG / EOVER */
void test_sysolve_errors(sy_tally_t *t) {
    char out[2048];
    int rc;
    printf("[case SYSOLVE-15] contract section 7: error codes\n");
    expect_sysolve(t, "", "x-2", AC_PRO_SYS_EARG, 0, "empty f (sys-T)");
    expect_sysolve(t, "x^5+y", "x-1", AC_PRO_SYS_EDEG, 0, "input total degree 5 (sys-U)");
    expect_sysolve(t, "x^9+y", "x-1", AC_PRO_SYS_EDEG, 0, "exponent 9 (sys-V)");
    expect_sysolve(t, "x^5+y^5", "xy", AC_PRO_SYS_EDEG, 0, "two deg-5 inputs");
    expect_sysolve(t, "99999999999999999999999x+y", "x-1", AC_PRO_SYS_EOVER, 0,
                   "coefficient beyond int64 accumulation");
    rc = ac_pro_sysolve(0, "x-1", out, (int)sizeof(out));
    sycheck(t, rc == AC_PRO_SYS_EARG, "null f -> EARG", "null arg");
    rc = ac_pro_sysolve("x-1", "x-2", 0, 16);
    sycheck(t, rc == AC_PRO_SYS_EARG, "null out -> EARG", "null out");
}

/* EBUF 边界:「刚好通过/刚好拒绝」(实际输出长度 ±1) */
void test_sysolve_buffer_boundary(sy_tally_t *t) {
    char big[2048];
    char slim[256];
    int need;
    int rc;
    printf("[case SYSOLVE-16] buffer boundary (exact fit passes, len-1 rejects)\n");
    rc = ac_pro_sysolve("x^2+y^2-4", "xy-1", big, (int)sizeof(big));
    sycheck(t, rc == AC_PRO_SYS_OK, "reference output computed", "ref rc");
    need = (int)strlen(big);
    rc = ac_pro_sysolve("x^2+y^2-4", "xy-1", slim, need + 1);
    sycheck(t, rc == AC_PRO_SYS_OK && strcmp(slim, big) == 0,
            "out_len = len+1 -> exact fit passes", "fit rc");
    rc = ac_pro_sysolve("x^2+y^2-4", "xy-1", slim, need);
    sycheck(t, rc == AC_PRO_SYS_EOVER, "out_len = len -> EOVER (just-reject)", "tight rc");
}

/* gb 文法镜像验收:隐式乘、变量换序、空白、系数 1 缺省 */
void test_sysolve_syntax(sy_tally_t *t) {
    printf("[case SYSOLVE-17] gb syntax mirror (implicit product, var order, spaces)\n");
    expect_sysolve(t, "x y - 2", "x + y - 3", AC_PRO_SYS_OK, "[(1, 2), (2, 1)]",
                   "x y - 2; x + y - 3 (implicit product)");
    expect_sysolve(t, "y*x-6", "x+y-5", AC_PRO_SYS_OK, "[(2, 3), (3, 2)]",
                   "yx-6; x+y-5 (var order swapped)");
    expect_sysolve(t, "  x^2 + y^2 - 25 ", " x + y - 1 ", AC_PRO_SYS_OK,
                   "[(-3, 4), (4, -3)]", "whitespace tolerance (y Re desc: 4 first)");
}

/* 确定性:换序不变 + 幂等(跨调用无状态残留) */
void test_sysolve_determinism(sy_tally_t *t) {
    char a[2048];
    char b[2048];
    int rc1;
    int rc2;
    printf("[case SYSOLVE-18] determinism (swap f/g, repeated call)\n");
    rc1 = ac_pro_sysolve("x^2+y^2-4", "xy-1", a, (int)sizeof(a));
    rc2 = ac_pro_sysolve("xy-1", "x^2+y^2-4", b, (int)sizeof(b));
    sycheck(t, rc1 == AC_PRO_SYS_OK && rc2 == AC_PRO_SYS_OK && strcmp(a, b) == 0,
            "f/g swap -> same solution list", "swap");
    rc2 = ac_pro_sysolve("x^2+y^2-4", "xy-1", b, (int)sizeof(b));
    sycheck(t, rc2 == AC_PRO_SYS_OK && strcmp(a, b) == 0, "repeat call idempotent", "repeat");
}

/* 残差回验在管线内的健全性:整数系端到端抽查 */
void test_sysolve_residual_sanity(sy_tally_t *t) {
    printf("[case SYSOLVE-19] end-to-end sanity (x^2+y^2-13; x+y-5)\n");
    expect_sysolve(t, "x^2+y^2-13", "x+y-5", AC_PRO_SYS_OK, "[(2, 3), (3, 2)]",
                   "circle + line, integer solutions (y Re desc)");
}

/* 非本原输入 / 负首项(GB 自行归一) */
void test_sysolve_nonprimitive(sy_tally_t *t) {
    printf("[case SYSOLVE-20] non-primitive inputs (GB normalizes)\n");
    expect_sysolve(t, "2x+2y-6", "3x-3y-3", AC_PRO_SYS_OK, "[(2, 1)]",
                   "scaled linear system");
    expect_sysolve(t, "-x^2-y^2+4", "-xy+1", AC_PRO_SYS_OK,
                   "[(0.517638090205, 1.931851652578), (1.931851652578, 0.517638090205),"
                   " (-1.931851652578, -0.517638090205), (-0.517638090205, -1.931851652578)]",
                   "sign-flipped classic system");
}

#endif /* AC_PRO_ACTIVE */
