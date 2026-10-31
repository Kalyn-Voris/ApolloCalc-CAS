/* 功能 9 C 侧(square-free 分解与重根重数)的验收测试。比对的是
 * docs/contracts/sqfree-format-v24.md 冻结的期望串(§6.2 范例逐字,
 * 含 d 标注的 DK 数值路径)与错误码(§7/§8 逐条,精确比较)。
 * d 标注用例的期望串已按契约 §4/§5 规则离线核算(√2/2、cos/sin 72°
 * 等 12 位定点展开),并经本套件 C 侧实跑核对(契约 §10-2)。
 * 自带 pro_tally_t,不 include runner 的内部头(与 test_pro_ode.c 同
 * 约定)。不声明静态对象:本 TU 的 .bss 必须是 0。 */

#include <stdio.h>
#include <string.h>

#include "../src/ac_pro.h"

#if AC_PRO_ACTIVE

typedef struct {
    int checks;
    int failed;
} sq_tally_t;

static void sqcheck(sq_tally_t *t, int cond, const char *name, const char *detail) {
    t->checks++;
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        t->failed++;
        printf("  [FAIL] %s  (%s)\n", name, detail);
    }
}

static void expect_sqfree(sq_tally_t *t, const char *in, int want_rc,
                          const char *want, const char *name) {
    char out[512];
    char detail[384];
    int rc;

    out[0] = '\0';
    rc = ac_pro_sqfree(in, out, (int)sizeof(out));
    if (rc == want_rc && (want == 0 || strcmp(out, want) == 0)) {
        sqcheck(t, 1, name, "");
        return;
    }
    sprintf(detail, "rc=%d got=\"%.80s\" want rc=%d \"%s\"", rc, out, want_rc,
            (want == 0) ? "<any>" : want);
    sqcheck(t, 0, name, detail);
}

/* ---- [case SQFREE-1] 契约 §6.2 行 1..3(单根 / 实重根 / 混合)---- */
void test_sqfree_basic(sq_tally_t *t) {
    printf("[case SQFREE-1] contract 6.2 rows 1-3\n");
    expect_sqfree(t, "x^2-5x+6", AC_PRO_SQF_OK, "[3@e1, 2@e1]",
                  "x^2-5x+6");
    expect_sqfree(t, "x^2-2x+1", AC_PRO_SQF_OK, "[1@e2]",
                  "x^2-2x+1(重根只出一次)");
    expect_sqfree(t, "x^3+x^2-5x+3", AC_PRO_SQF_OK, "[1@e2, -3@e1]",
                  "x^3+x^2-5x+3(Re 降序:1 先于 -3)");
}

/* ---- [case SQFREE-2] 契约 §6.2 行 4..6(纯虚对 / 本原化 / (x^2+1)^2)---- */
void test_sqfree_complex(sq_tally_t *t) {
    printf("[case SQFREE-2] contract 6.2 rows 4-6\n");
    expect_sqfree(t, "x^2+1", AC_PRO_SQF_OK, "[i@e1, -i@e1]",
                  "x^2+1(±i,+Im 在前)");
    expect_sqfree(t, "2x^2-2", AC_PRO_SQF_OK, "[1@e1, -1@e1]",
                  "2x^2-2(容度不进输出)");
    expect_sqfree(t, "x^4+2x^2+1", AC_PRO_SQF_OK, "[i@e2, -i@e2]",
                  "x^4+2x^2+1(q1=1 层跳过)");
}

/* ---- [case SQFREE-3] 契约 §6.2 行 7..8(按因子次数路由 / 重数 8)---- */
void test_sqfree_route(sq_tally_t *t) {
    printf("[case SQFREE-3] contract 6.2 rows 7-8 (per-factor routing)\n");
    expect_sqfree(t, "x^6-4x^5+7x^4-8x^3+7x^2-4x+1", AC_PRO_SQF_OK,
                  "[1@e4, i@e1, -i@e1]",
                  "(x-1)^4*(x^2+1):整题 6 次但两因子 <=4 全 e");
    expect_sqfree(t, "x^8-8x^7+28x^6-56x^5+70x^4-56x^3+28x^2-8x+1",
                  AC_PRO_SQF_OK, "[1@e8]",
                  "(x-1)^8:重数 8、因子 1 次 -> e");
}

/* ---- [case SQFREE-4] 契约 §6.2 行 9..10(常数边界 / DK 路径)---- */
void test_sqfree_const_dk(sq_tally_t *t) {
    printf("[case SQFREE-4] contract 6.2 rows 9-10 (f'=0 edge, DK path)\n");
    expect_sqfree(t, "7", AC_PRO_SQF_OK, "[]",
                  "7:f'=0 常数边界 -> 空列表");
    expect_sqfree(t, "x^5-15x^4+85x^3-225x^2+274x-120", AC_PRO_SQF_OK,
                  "[5@d1, 4@d1, 3@d1, 2@d1, 1@d1]",
                  "Pi(x-k),k=1..5:deg5 因子整题走 DK,整根整判命中");
}

/* ---- [case SQFREE-5] 契约 §6.2 行 11(x^8-1 的 DK 单位圆谱,逐字)---- */
void test_sqfree_x8(sq_tally_t *t) {
    printf("[case SQFREE-5] contract 6.2 row 11 (x^8-1)\n");
    expect_sqfree(t, "x^8-1", AC_PRO_SQF_OK,
                  "[1@d1, 0.707106781187+0.707106781187i@d1, "
                  "0.707106781187-0.707106781187i@d1, i@d1, -i@d1, "
                  "-0.707106781187+0.707106781187i@d1, "
                  "-0.707106781187-0.707106781187i@d1, -1@d1]",
                  "x^8-1:DK 根的 fmt 12 位定点(√2/2)");
}

/* ---- [case SQFREE-6] fmt 三级(有理重建 / 定点 12 位 / q=999 边界)---- */
void test_sqfree_fmt(sq_tally_t *t) {
    printf("[case SQFREE-6] fmt three tiers\n");
    expect_sqfree(t, "2x^2-2x+1", AC_PRO_SQF_OK,
                  "[1/2+1/2i@e1, 1/2-1/2i@e1]",
                  "(1±i)/2:有理重建 p/q 形态");
    expect_sqfree(t, "x^2-2", AC_PRO_SQF_OK,
                  "[1.414213562373@e1, -1.414213562373@e1]",
                  "√2:定点 12 位半连舍");
    expect_sqfree(t, "999*x^2+1", AC_PRO_SQF_OK,
                  "[0.031638599858i@e1, -0.031638599858i@e1]",
                  "系数 999 刚好通过;根 ±√(1/999)i,定点 12 位");
}

/* ---- [case SQFREE-7] DK 五次单位圆根(x^5-1,d 标注实跑核对)----
 * 期望串按契约 §10-2 机制冻结:C 侧实跑核对后写死。sin72° 分量的
 * DK 收敛特征(与真值差 ~5.7e-11,12 位定点为 …295)是共享 B1 管线
 * (PRO_NR_TOL 步长判据)在本配置下的确定性表现 —— d 标注正是
 * 「DK 来源」的诚实标注;冻结后任何一侧输出与之不符 = 实现 bug。 */
void test_sqfree_x5(sq_tally_t *t) {
    printf("[case SQFREE-7] x^5-1 (DK, 5th roots of unity)\n");
    expect_sqfree(t, "x^5-1", AC_PRO_SQF_OK,
                  "[1@d1, 0.309016994375+0.951056516295i@d1, "
                  "0.309016994375-0.951056516295i@d1, "
                  "-0.809016994375+0.587785252292i@d1, "
                  "-0.809016994375-0.587785252292i@d1]",
                  "x^5-1:q1=全式(deg5 -> DK),cos/sin 72° 12 位");
}

/* ---- [case SQFREE-8] 拒绝情形(契约 §8 逐条)---- */
void test_sqfree_reject(sq_tally_t *t) {
    char longbuf[132];
    int i;

    printf("[case SQFREE-8] rejections (contract section 8)\n");
    expect_sqfree(t, "0.5*x^2+x", AC_PRO_SQF_EPARSE, "", "小数系数 -> -1");
    expect_sqfree(t, "1/2*x^2", AC_PRO_SQF_EPARSE, "", "分数/有理式 -> -1");
    expect_sqfree(t, "x^2-1=0", AC_PRO_SQF_EPARSE, "", "方程形式 -> -1");
    expect_sqfree(t, "(x-1)^2", AC_PRO_SQF_EPARSE, "", "括号 -> -1");
    expect_sqfree(t, "x*y^2", AC_PRO_SQF_EPARSE, "", "多变量 -> -1");
    expect_sqfree(t, "X^2-1", AC_PRO_SQF_EPARSE, "", "大写 X -> -1");
    expect_sqfree(t, "x^9+1", AC_PRO_SQF_EDEG, "", "次数 9 -> -2(当场)");
    expect_sqfree(t, "x^12", AC_PRO_SQF_EDEG, "", "次数 12 -> -2");
    expect_sqfree(t, "0", AC_PRO_SQF_ECOEF, "", "全零多项式 -> -3");
    expect_sqfree(t, "1000*x^2+1", AC_PRO_SQF_ECOEF, "", "系数 1000 -> -3");
    expect_sqfree(t, "x^2+2x^2", AC_PRO_SQF_EPARSE, "", "同次项重复 -> -1");
    expect_sqfree(t, "3*", AC_PRO_SQF_EPARSE, "", "悬空 3* -> -1");
    expect_sqfree(t, "*x", AC_PRO_SQF_EPARSE, "", "悬空 *x -> -1");
    expect_sqfree(t, "x*3", AC_PRO_SQF_EPARSE, "", "x 后 '*' -> -1");
    expect_sqfree(t, "x^", AC_PRO_SQF_EPARSE, "", "^ 后非数字 -> -1");
    expect_sqfree(t, "x^0", AC_PRO_SQF_EPARSE, "", "x^0 -> -1");
    expect_sqfree(t, "x^ 2", AC_PRO_SQF_EPARSE, "", "^ 与次数间空白 -> -1");
    expect_sqfree(t, "", AC_PRO_SQF_EPARSE, "", "空串 -> -1");
    for (i = 0; i < 131; i++) { longbuf[i] = 'x'; }
    longbuf[131] = '\0';
    expect_sqfree(t, longbuf, AC_PRO_SQF_EPARSE, "",
                  "129 字节超长 -> -1");
}

/* ---- [case SQFREE-9] 判定全序交叉例(契约 §7)---- */
void test_sqfree_order(sq_tally_t *t) {
    printf("[case SQFREE-9] error-precedence cross cases\n");
    expect_sqfree(t, "0.5+x^9", AC_PRO_SQF_EPARSE, "",
                  "EPARSE 先于 EDEG(从左到右 '.' 先命中)");
    expect_sqfree(t, "x^9+0.5", AC_PRO_SQF_EDEG, "",
                  "EDEG 词法当场,先于后面一切");
    expect_sqfree(t, "1000*x^9", AC_PRO_SQF_ECOEF, "",
                  "从左到右:系数 1000 先命中 ECOEF(全序)");
    expect_sqfree(t, "x^9+1000", AC_PRO_SQF_EDEG, "",
                  "x^9 词法当场,先于后面系数域判定");
}

/* ---- [case SQFREE-10] 边界:EOVER / 出缓冲恰好足够 ---- */
void test_sqfree_bounds(sq_tally_t *t) {
    char tiny[8];
    char detail[64];
    int rc;

    printf("[case SQFREE-10] boundary values\n");
    tiny[0] = '\0';
    rc = ac_pro_sqfree("x^2-1", tiny, (int)sizeof(tiny));
    sprintf(detail, "rc=%d want %d", rc, AC_PRO_SQF_EOVER);
    sqcheck(t, rc == AC_PRO_SQF_EOVER, "tiny buffer -> EOVER", detail);
    expect_sqfree(t, "5", AC_PRO_SQF_OK, "[]", "常数 5 -> [](§4.4)");
}

#endif /* AC_PRO_ACTIVE */
