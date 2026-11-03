/* ApolloCalc · test_pro_complex_ext.c — v2.5.0 功能 F1 复变函数扩展（15 API）验收测试

   期望值出处：docs/contracts/complex-ext-v25.md §9 实测锚 + tests/complex-functions.json
   （mpmath 40dps 独立生成，行号以 id 标注）。比较口径按契约 §8：
     err = |a-b| / max(1, |a|, |b|) <= 1e-9（相对-绝对混合，±0 不比符号）；
     错误码精确比较。

   覆盖：恒等式锚（csin(i)、cpow(e,iπ)、cgamma(1)）、四条割线两侧采样
   （casin/cacos/catanh 实轴割线、catan/casinh 虚轴割线）、极点契约
   （cgamma 非正整数、catan ±i、catanh ±1、cpow 零底）、溢出契约、
   「刚好通过/刚好拒绝」双侧边界（支点 vs 支点旁、0^w 约定边界）、
   cgamma 与 ac_special 实 Gamma 的实轴交叉回归锚（cx-cgamma 交叉）。

   本 TU 由 Makefile CX 锚点的 cx_runner 目标独立链接（契约 §11）：
   零 static 对象、零 malloc、零递归；计数器走 main() 栈。 */

#include <stdio.h>
#include <math.h>

#include "../src/ac_pro.h"
#include "../../../C/128KB/src/ac_complex.h"
#include "../../../C/128KB/src/ac_special.h"

#if AC_PRO_ACTIVE

typedef struct {
    int checks;
    int failed;
} cx_tally_t;

static void cxcheck(cx_tally_t *t, int cond, const char *name, const char *detail) {
    t->checks++;
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        t->failed++;
        printf("  [FAIL] %s  (%s)\n", name, detail);
    }
}

/* 契约 §8 比较式：err = |a-b| / max(1,|a|,|b|) <= 1e-9（±0 不比符号） */
static int cx_near(double a, double b) {
    double den = fabs(a);
    double num = fabs(a - b);
    if (fabs(b) > den) { den = fabs(b); }
    if (den < 1.0) { den = 1.0; }
    return num / den <= 1e-9;
}

/* 双分量比较：读 g_c_tmp_* 对 (wr, wi) */
static void cx_tmp(cx_tally_t *t, double wr, double wi, const char *name) {
    char detail[160];
    if (cx_near(g_c_tmp_re, wr) && cx_near(g_c_tmp_im, wi)) {
        cxcheck(t, 1, name, "");
        return;
    }
    sprintf(detail, "got=(%.17g, %.17g) want=(%.17g, %.17g)",
            g_c_tmp_re, g_c_tmp_im, wr, wi);
    cxcheck(t, 0, name, detail);
}

static void cx_code(cx_tally_t *t, int rc, int want, const char *name) {
    char detail[64];
    sprintf(detail, "rc=%d want=%d", rc, want);
    cxcheck(t, rc == want, name, detail);
}

static void cx_set(double re, double im) {
    g_c_a_re = re;
    g_c_a_im = im;
}

/* ==================== CX-1..CX-4：整函数族 ==================== */

/* 恒等式锚：csin(i)=i·sinh(1)、ccos(0)=1、csinh(i)=i·sin(1)、ccosh(i)=cos(1) */
static void test_cx_01(cx_tally_t *t) {
    printf("[case CX-1] identity anchors csin/ccos/csinh/ccosh\n");
    cx_set(0.0, 1.0);
    cx_code(t, ac_pro_csin(), AC_PRO_CX_OK, "csin(i) rc=OK");
    cx_tmp(t, 0.0, sinh(1.0), "csin(i)=i*sinh(1)");
    cx_set(0.0, 0.0);
    cx_code(t, ac_pro_ccos(), AC_PRO_CX_OK, "ccos(0) rc=OK");
    cx_tmp(t, 1.0, 0.0, "ccos(0)=1");
    cx_set(0.0, 1.0);
    cx_code(t, ac_pro_csinh(), AC_PRO_CX_OK, "csinh(i) rc=OK");
    cx_tmp(t, 0.0, sin(1.0), "csinh(i)=i*sin(1)");
    cx_set(0.0, 1.0);
    cx_code(t, ac_pro_ccosh(), AC_PRO_CX_OK, "ccosh(i) rc=OK");
    cx_tmp(t, cos(1.0), 0.0, "ccosh(i)=cos(1)");
}

/* 毕达哥拉斯恒等式：csin(z)^2 + ccos(z)^2 = 1（z=0.7+1.3i，TMP 复用链） */
static void test_cx_02(cx_tally_t *t) {
    double sr, si, cr, ci;
    printf("[case CX-2] pythagorean identity csin^2+ccos^2=1 @0.7+1.3i\n");
    cx_set(0.7, 1.3);
    cx_code(t, ac_pro_csin(), AC_PRO_CX_OK, "csin(0.7+1.3i) rc=OK");
    sr = g_c_tmp_re;
    si = g_c_tmp_im;
    cx_code(t, ac_pro_ccos(), AC_PRO_CX_OK, "ccos(0.7+1.3i) rc=OK");
    cr = g_c_tmp_re;
    ci = g_c_tmp_im;
    /* 复平方和（手工展开，直接比较实部） */
    cxcheck(t, cx_near(sr * sr - si * si + cr * cr - ci * ci, 1.0),
            "Re(csin^2+ccos^2)=1", "pythagorean real part off");
    cxcheck(t, cx_near(2.0 * sr * si + 2.0 * cr * ci, 0.0),
            "Im(csin^2+ccos^2)=0", "pythagorean imag part off");
}

/* ctan 与 csin/ccos 的一致性 + 实轴退化 */
static void test_cx_03(cx_tally_t *t) {
    double sr, si, cr, ci, qr, qi, den;
    printf("[case CX-3] ctan consistency with csin/ccos @0.4-0.9i; real axis\n");
    cx_set(0.4, -0.9);
    cx_code(t, ac_pro_csin(), AC_PRO_CX_OK, "csin rc=OK");
    sr = g_c_tmp_re;
    si = g_c_tmp_im;
    cx_code(t, ac_pro_ccos(), AC_PRO_CX_OK, "ccos rc=OK");
    cr = g_c_tmp_re;
    ci = g_c_tmp_im;
    den = cr * cr + ci * ci;
    qr = (sr * cr + si * ci) / den;
    qi = (si * cr - sr * ci) / den;
    cx_code(t, ac_pro_ctan(), AC_PRO_CX_OK, "ctan rc=OK");
    cx_tmp(t, qr, qi, "ctan = csin/ccos");
    cx_set(1.0, 0.0);
    cx_code(t, ac_pro_ctan(), AC_PRO_CX_OK, "ctan(1) rc=OK");
    cx_tmp(t, tan(1.0), 0.0, "ctan(1+0i)=tan(1) 实轴");
}

/* ctan/ctanh 极点边界：大有限值而非错误（契约 §9 实测锚） */
static void test_cx_04(cx_tally_t *t) {
    printf("[case CX-4] ctan(pi/2+0i), ctanh(i*pi/2): large finite, not EDOM\n");
    cx_set(1.5707963267948966, 0.0);
    cx_code(t, ac_pro_ctan(), AC_PRO_CX_OK, "ctan(pi/2+0i) rc=OK");
    cxcheck(t, g_c_tmp_re > 1e15, "ctan real part ~1.63e16", "pole value too small");
    cxcheck(t, cx_near(g_c_tmp_im, 0.0), "ctan imag part = 0", "imag drift");
    cx_set(0.0, 1.5707963267948966);
    cx_code(t, ac_pro_ctanh(), AC_PRO_CX_OK, "ctanh(i*pi/2) rc=OK");
    cxcheck(t, g_c_tmp_im > 1e15, "ctanh imag ~1.63e16", "pole value too small");
    cxcheck(t, cx_near(g_c_tmp_re, 0.0), "ctanh real = 0", "real drift");
}

/* ==================== CX-5..CX-8：casin/cacos/casinh/cacosh 割线 ==================== */

/* casin 割线上侧：casin(2+0i) = pi/2 + i*acosh(2)（cx-casin-05） */
static void test_cx_05(cx_tally_t *t) {
    printf("[case CX-5] casin cut upper side (cx-casin-05)\n");
    cx_set(2.0, 0.0);
    cx_code(t, ac_pro_casin(), AC_PRO_CX_OK, "casin(2+0i) rc=OK");
    cx_tmp(t, 1.5707963267948966, 1.3169578969248168, "casin(2+0i)=pi/2+i*acosh2");
    /* 割线端点 casin(1+0i)=pi/2（支点有限值，cx-casin-02） */
    cx_set(1.0, 0.0);
    cx_code(t, ac_pro_casin(), AC_PRO_CX_OK, "casin(1+0i) rc=OK");
    cx_tmp(t, 1.5707963267948966, 0.0, "casin(1+0i)=pi/2");
}

/* casin 割线下侧与左支：共轭/奇偶对称（cx-casin-06/07） */
static void test_cx_06(cx_tally_t *t) {
    printf("[case CX-6] casin cut lower/left sides (cx-casin-06/07)\n");
    cx_set(2.0, -0.0);
    cx_code(t, ac_pro_casin(), AC_PRO_CX_OK, "casin(2-0i) rc=OK");
    cx_tmp(t, 1.5707963267948966, -1.3169578969248168, "casin(2-0i) 共轭下侧");
    cx_set(-2.0, 0.0);
    cx_code(t, ac_pro_casin(), AC_PRO_CX_OK, "casin(-2+0i) rc=OK");
    cx_tmp(t, -1.5707963267948966, 1.3169578969248168, "casin(-2+0i)=-pi/2+i*acosh2");
    /* 实轴内点 cx-casin-03: asin(0.5)=pi/6 */
    cx_set(0.5, 0.0);
    cx_code(t, ac_pro_casin(), AC_PRO_CX_OK, "casin(0.5) rc=OK");
    cx_tmp(t, 0.5235987755982989, 0.0, "casin(0.5)=pi/6");
}

/* cacos 割线：acos(2+0i)=(0,-acosh2)、acos(-2-0i)=(pi,+acosh2)（cx-cacos-03/04） */
static void test_cx_07(cx_tally_t *t) {
    printf("[case CX-7] cacos cut sides (cx-cacos-03/04)\n");
    cx_set(2.0, 0.0);
    cx_code(t, ac_pro_cacos(), AC_PRO_CX_OK, "cacos(2+0i) rc=OK");
    cx_tmp(t, 0.0, -1.3169578969248168, "cacos(2+0i)=-i*acosh2");
    cx_set(-2.0, -0.0);
    cx_code(t, ac_pro_cacos(), AC_PRO_CX_OK, "cacos(-2-0i) rc=OK");
    cx_tmp(t, 3.141592653589793, 1.3169578969248168, "cacos(-2-0i)=pi+i*acosh2");
    /* 实轴内点 cacos(0.5)=pi/3（cx-cacos-05） */
    cx_set(0.5, 0.0);
    cx_code(t, ac_pro_cacos(), AC_PRO_CX_OK, "cacos(0.5) rc=OK");
    cx_tmp(t, 1.0471975511965979, 0.0, "cacos(0.5)=pi/3");
}

/* casinh/cacosh 割线与支点（cx-casinh-03/04、cx-cacosh-02/04） */
static void test_cx_08(cx_tally_t *t) {
    printf("[case CX-8] casinh/cacosh cut lines (cx-casinh/cacosh)\n");
    cx_set(0.0, 1.0);
    cx_code(t, ac_pro_casinh(), AC_PRO_CX_OK, "casinh(i) rc=OK");
    cx_tmp(t, 0.0, 1.5707963267948966, "casinh(i)=i*pi/2（支点有限）");
    cx_set(0.0, 2.0);
    cx_code(t, ac_pro_casinh(), AC_PRO_CX_OK, "casinh(2i) rc=OK");
    cx_tmp(t, 1.3169578969248168, 1.5707963267948966, "casinh(2i)=acosh2+i*pi/2");
    cx_set(-2.0, 0.0);
    cx_code(t, ac_pro_cacosh(), AC_PRO_CX_OK, "cacosh(-2+0i) rc=OK");
    cx_tmp(t, 1.3169578969248168, 3.141592653589793, "cacosh(-2+0i)=acosh2+i*pi");
    cx_set(0.0, 0.0);
    cx_code(t, ac_pro_cacosh(), AC_PRO_CX_OK, "cacosh(0+0i) rc=OK");
    cx_tmp(t, 0.0, 1.5707963267948966, "cacosh(0+0i)=i*pi/2");
    cx_set(2.0, 0.0);
    cx_code(t, ac_pro_cacosh(), AC_PRO_CX_OK, "cacosh(2) rc=OK");
    cx_tmp(t, 1.3169578969248168, 0.0, "cacosh(2+0i)=acosh2");
}

/* ==================== CX-9..CX-10：catan/catanh 支点双侧边界 ==================== */

/* catan：虚割线值 + 支点 EDOM + 支点旁 OK（刚好通过/刚好拒绝） */
static void test_cx_09(cx_tally_t *t) {
    printf("[case CX-9] catan cut value, branch point reject/accept boundary\n");
    cx_set(0.0, 2.0);
    cx_code(t, ac_pro_catan(), AC_PRO_CX_OK, "catan(2i) rc=OK");
    cx_tmp(t, 1.5707963267948966, 0.5493061443340549, "catan(2i)=pi/2+i*0.25ln9");
    cx_set(0.0, 1.0);
    cx_code(t, ac_pro_catan(), AC_PRO_CX_EDOM, "catan(+i) rc=EDOM（刚好拒绝）");
    cx_set(0.0, -1.0);
    cx_code(t, ac_pro_catan(), AC_PRO_CX_EDOM, "catan(-i) rc=EDOM");
    cx_set(1.0, 1.0);
    cx_code(t, ac_pro_catan(), AC_PRO_CX_OK, "catan(1+i) rc=OK（割线旁刚好通过）");
    /* 实轴 cx-catan-02: atan(1)=pi/4 */
    cx_set(1.0, 0.0);
    cx_code(t, ac_pro_catan(), AC_PRO_CX_OK, "catan(1) rc=OK");
    cx_tmp(t, 0.7853981633974483, 0.0, "catan(1+0i)=pi/4");
}

/* catanh：支点 ±1 EDOM 双侧 + 割线值 + 内点 */
static void test_cx_10(cx_tally_t *t) {
    printf("[case CX-10] catanh branch points +-1: EDOM both sides (cx-catanh-08/09/03/04)\n");
    cx_set(1.0, 0.0);
    cx_code(t, ac_pro_catanh(), AC_PRO_CX_EDOM, "catanh(+1+0i) rc=EDOM");
    cx_set(-1.0, 0.0);
    cx_code(t, ac_pro_catanh(), AC_PRO_CX_EDOM, "catanh(-1+0i) rc=EDOM");
    cx_set(-1.0, -0.0);
    cx_code(t, ac_pro_catanh(), AC_PRO_CX_EDOM, "catanh(-1-0i) rc=EDOM（-0 侧）");
    cx_set(0.999999999, 0.0);
    cxcheck(t, ac_pro_catanh() == AC_PRO_CX_OK,
            "catanh(0.999999999) rc=OK（刚好通过）", "near-branch rejected");
    cx_set(2.0, 0.0);
    cx_code(t, ac_pro_catanh(), AC_PRO_CX_OK, "catanh(2+0i) rc=OK");
    cx_tmp(t, 0.5493061443340549, 1.5707963267948966, "catanh(2+0i)=0.5ln3+i*pi/2");
    cx_set(2.0, -0.0);
    cx_code(t, ac_pro_catanh(), AC_PRO_CX_OK, "catanh(2-0i) rc=OK");
    cx_tmp(t, 0.5493061443340549, -1.5707963267948966, "catanh(2-0i) 共轭下侧");
}

/* ==================== CX-11..CX-13：cpow / cnthroot ==================== */

/* cpow 恒等式锚（cx-cpow-01/02/03）+ 零底约定（cx-cpow-05/06） */
static void test_cx_11(cx_tally_t *t) {
    printf("[case CX-11] cpow anchors + zero-base conventions (cx-cpow-01/02/03/05/06)\n");
    cx_set(2.718281828459045, 0.0);
    g_c_b_re = 0.0;
    g_c_b_im = 3.141592653589793;
    cx_code(t, ac_pro_cpow(), AC_PRO_CX_OK, "cpow(e,i*pi) rc=OK");
    cx_tmp(t, -1.0, 0.0, "cpow(e,i*pi)=-1 欧拉锚");
    cx_set(2.0, 0.0);
    g_c_b_re = 3.0;
    g_c_b_im = 0.0;
    cx_code(t, ac_pro_cpow(), AC_PRO_CX_OK, "cpow(2,3) rc=OK");
    cx_tmp(t, 8.0, 0.0, "cpow(2,3+0i)=8");
    cx_set(0.0, 1.0);
    g_c_b_re = 0.0;
    g_c_b_im = 1.0;
    cx_code(t, ac_pro_cpow(), AC_PRO_CX_OK, "cpow(i,i) rc=OK");
    cx_tmp(t, 0.20787957635076193, 0.0, "cpow(i,i)=e^(-pi/2) 实正值");
    cx_set(0.0, 0.0);
    g_c_b_re = 0.0;
    g_c_b_im = 0.0;
    cx_code(t, ac_pro_cpow(), AC_PRO_CX_OK, "cpow(0,0) rc=OK");
    cx_tmp(t, 1.0, 0.0, "cpow(0,0)=1（契约约定）");
    cx_set(0.0, 0.0);
    g_c_b_re = 2.0;
    g_c_b_im = 0.0;
    cx_code(t, ac_pro_cpow(), AC_PRO_CX_OK, "cpow(0,2) rc=OK");
    cx_tmp(t, 0.0, 0.0, "cpow(0,2)=0");
}

/* cpow 拒绝/溢出 + A 覆写语义（契约 §3） */
static void test_cx_12(cx_tally_t *t) {
    printf("[case CX-12] cpow rejections, overflow, A-clobber semantics (cx-cpow-07/08/11)\n");
    cx_set(0.0, 0.0);
    g_c_b_re = -1.0;
    g_c_b_im = 0.0;
    cx_code(t, ac_pro_cpow(), AC_PRO_CX_EDOM, "cpow(0,-1) rc=EDOM");
    cx_set(0.0, 0.0);
    g_c_b_re = 0.0;
    g_c_b_im = 1.0;
    cx_code(t, ac_pro_cpow(), AC_PRO_CX_EDOM, "cpow(0,i) rc=EDOM");
    cx_set(10.0, 0.0);
    g_c_b_re = 400.0;
    g_c_b_im = 0.0;
    cx_code(t, ac_pro_cpow(), AC_PRO_CX_EOVER, "cpow(10,400) rc=EOVER");
    /* A 覆写：cpow(2,3) 后 A == 3*ln2（+0i） */
    cx_set(2.0, 0.0);
    g_c_b_re = 3.0;
    g_c_b_im = 0.0;
    cx_code(t, ac_pro_cpow(), AC_PRO_CX_OK, "cpow(2,3) rc=OK (clobber probe)");
    cxcheck(t, cx_near(g_c_a_re, 3.0 * log(2.0)) && cx_near(g_c_a_im, 0.0),
            "A clobbered to w*ln z = 3*ln2", "A register not clobbered");
}

/* cnthroot：主值/共轭/零/参数域（cx-cnth-01/02/03/06/09/10） */
static void test_cx_13(cx_tally_t *t) {
    printf("[case CX-13] cnthroot principal values + param gate (cx-cnth)\n");
    cx_set(8.0, 0.0);
    cx_code(t, ac_pro_cnthroot(3), AC_PRO_CX_OK, "nthroot(8,3) rc=OK");
    cx_tmp(t, 2.0, 0.0, "nthroot(8,3)=2");
    cx_set(-8.0, 0.0);
    cx_code(t, ac_pro_cnthroot(3), AC_PRO_CX_OK, "nthroot(-8+0i,3) rc=OK");
    cx_tmp(t, 1.0, 1.7320508075688772, "nthroot(-8+0i,3)=1+i*sqrt3");
    cx_set(-8.0, -0.0);
    cx_code(t, ac_pro_cnthroot(3), AC_PRO_CX_OK, "nthroot(-8-0i,3) rc=OK");
    cx_tmp(t, 1.0, -1.7320508075688772, "nthroot(-8-0i,3)=1-i*sqrt3 共轭");
    cx_set(0.0, 0.0);
    cx_code(t, ac_pro_cnthroot(5), AC_PRO_CX_OK, "nthroot(0,5) rc=OK");
    cx_tmp(t, 0.0, 0.0, "nthroot(0,5)=0");
    cx_set(8.0, 0.0);
    cx_code(t, ac_pro_cnthroot(0), AC_PRO_CX_EPARAM, "nthroot(.,0) rc=EPARAM");
    cx_code(t, ac_pro_cnthroot(-2), AC_PRO_CX_EPARAM, "nthroot(.,-2) rc=EPARAM");
    cx_set(0.0, 1.0);
    cx_code(t, ac_pro_cnthroot(2), AC_PRO_CX_OK, "nthroot(i,2) rc=OK");
    cx_tmp(t, 0.7071067811865476, 0.7071067811865476, "nthroot(i,2)=(1+i)/sqrt2");
}

/* ==================== CX-14..CX-17：cgamma ==================== */

/* cgamma 整数/半整数锚（cx-cgamma-01/02/03） */
static void test_cx_14(cx_tally_t *t) {
    printf("[case CX-14] cgamma integer/half-integer anchors (cx-cgamma-01/02/03)\n");
    cx_set(1.0, 0.0);
    cx_code(t, ac_pro_cgamma(), AC_PRO_CX_OK, "cgamma(1) rc=OK");
    cx_tmp(t, 1.0, 0.0, "cgamma(1)=1");
    cx_set(5.0, 0.0);
    cx_code(t, ac_pro_cgamma(), AC_PRO_CX_OK, "cgamma(5) rc=OK");
    cx_tmp(t, 24.0, 0.0, "cgamma(5)=24");
    cx_set(0.5, 0.0);
    cx_code(t, ac_pro_cgamma(), AC_PRO_CX_OK, "cgamma(0.5) rc=OK");
    cx_tmp(t, 1.7724538509055159, 0.0, "cgamma(0.5)=sqrt(pi)");
}

/* cgamma 文献复值锚 Γ(1+i) + 反射半整数（cx-cgamma-06/04/05） */
static void test_cx_15(cx_tally_t *t) {
    printf("[case CX-15] cgamma literature Gamma(1+i) + reflection (cx-cgamma-06/04/05)\n");
    cx_set(1.0, 1.0);
    cx_code(t, ac_pro_cgamma(), AC_PRO_CX_OK, "cgamma(1+i) rc=OK");
    cx_tmp(t, 0.49801566811835604, -0.15494982830181069, "cgamma(1+i) 文献值");
    cx_set(-0.5, 0.0);
    cx_code(t, ac_pro_cgamma(), AC_PRO_CX_OK, "cgamma(-0.5) rc=OK");
    cx_tmp(t, -3.544907701811032, 0.0, "cgamma(-0.5)=-2*sqrt(pi)");
    cx_set(-1.5, 0.0);
    cx_code(t, ac_pro_cgamma(), AC_PRO_CX_OK, "cgamma(-1.5) rc=OK");
    cx_tmp(t, 2.3632718012073548, 0.0, "cgamma(-1.5)=4*sqrt(pi)/3");
    cx_set(0.5, 2.0);
    cx_code(t, ac_pro_cgamma(), AC_PRO_CX_OK, "cgamma(0.5+2i) rc=OK");
    cxcheck(t, isfinite(g_c_tmp_re) && isfinite(g_c_tmp_im),
            "cgamma(0.5+2i) finite", "non-finite");
}

/* cgamma 极点契约（cx-cgamma-10/11）+ 溢出契约（cx-cgamma-12） */
static void test_cx_16(cx_tally_t *t) {
    printf("[case CX-16] cgamma poles EDOM + overflow EOVER (cx-cgamma-10/11/12)\n");
    cx_set(0.0, 0.0);
    cx_code(t, ac_pro_cgamma(), AC_PRO_CX_EDOM, "cgamma(0) rc=EDOM");
    cx_set(-2.0, 0.0);
    cx_code(t, ac_pro_cgamma(), AC_PRO_CX_EDOM, "cgamma(-2) rc=EDOM");
    cx_set(-3.0, -0.0);
    cx_code(t, ac_pro_cgamma(), AC_PRO_CX_EDOM, "cgamma(-3-0i) rc=EDOM");
    cx_set(-0.0, 0.0);
    cxcheck(t, ac_pro_cgamma() == AC_PRO_CX_EDOM,
            "cgamma(-0+0i) rc=EDOM（-0 极点判定）", "-0 pole missed");
    cx_set(-0.5, 0.0);
    cxcheck(t, ac_pro_cgamma() == AC_PRO_CX_OK,
            "cgamma(-0.5) rc=OK（非整数，刚好通过）", "near-pole rejected");
    cx_set(200.0, 0.0);
    cx_code(t, ac_pro_cgamma(), AC_PRO_CX_EOVER, "cgamma(200) rc=EOVER");
}

/* cgamma 实轴交叉回归锚：与既有 ac_special_gamma 逐点一致（契约 §8） */
static void test_cx_17(cx_tally_t *t) {
    static const double xs[5] = {0.5, 1.0, 2.5, 7.3, 0.1};
    int i;
    printf("[case CX-17] cgamma real-axis cross-anchor vs ac_special_gamma\n");
    for (i = 0; i < 5; i++) {
        double want = ac_special_gamma(xs[i]);
        char name[96];
        cx_set(xs[i], 0.0);
        cx_code(t, ac_pro_cgamma(), AC_PRO_CX_OK, "cgamma(x) rc=OK");
        sprintf(name, "cgamma(%.2g+0i)==special_gamma(%.2g) 1e-9", xs[i], xs[i]);
        cxcheck(t, cx_near(g_c_tmp_re, want) && cx_near(g_c_tmp_im, 0.0),
                name, "cross-anchor mismatch");
    }
}

/* ==================== CX-18..CX-20：溢出/非有限统一语义 ==================== */

/* 整函数溢出：cosh(800) 上溢 → EOVER（契约 §6），TMP 只可信错误码 */
static void test_cx_18(cx_tally_t *t) {
    printf("[case CX-18] entire-function overflow EOVER (contract section 6)\n");
    cx_set(0.0, 800.0);
    cx_code(t, ac_pro_csin(), AC_PRO_CX_EOVER, "csin(0,800) rc=EOVER");
    cx_set(800.0, 0.0);
    cx_code(t, ac_pro_csinh(), AC_PRO_CX_EOVER, "csinh(800,0) rc=EOVER");
    cx_set(0.0, 400.0);
    cxcheck(t, ac_pro_ctan() == AC_PRO_CX_OK,
            "ctan(0,400) rc=OK（和式形无中间溢出）", "sum-form overflowed");
}

/* 输入非有限 → EDOM（契约 §5 首条） */
static void test_cx_19(cx_tally_t *t) {
    printf("[case CX-19] non-finite input -> EDOM (contract section 5)\n");
    cx_set(NAN, 0.0);
    cx_code(t, ac_pro_csin(), AC_PRO_CX_EDOM, "csin(NaN,0) rc=EDOM");
    cx_set(INFINITY, 1.0);
    cx_code(t, ac_pro_catan(), AC_PRO_CX_EDOM, "catan(Inf,i) rc=EDOM");
    cx_set(1.0, 1.0);
    g_c_b_re = NAN;
    g_c_b_im = 0.0;
    cx_code(t, ac_pro_cpow(), AC_PRO_CX_EDOM, "cpow(1,NaN) rc=EDOM");
    cx_set(2.0, 3.0);
    cx_code(t, ac_pro_cnthroot(2), AC_PRO_CX_OK, "finite input sanity rc=OK");
}

/* 寄存器协议：单操作数 API 不改写 A/B（契约 §3） */
static void test_cx_20(cx_tally_t *t) {
    printf("[case CX-20] register protocol: single-operand APIs preserve A/B\n");
    cx_set(0.3, 0.4);
    g_c_b_re = 9.0;
    g_c_b_im = 8.0;
    cx_code(t, ac_pro_csin(), AC_PRO_CX_OK, "csin rc=OK");
    cxcheck(t, cx_near(g_c_a_re, 0.3) && cx_near(g_c_a_im, 0.4),
            "A preserved by csin", "A clobbered");
    cxcheck(t, cx_near(g_c_b_re, 9.0) && cx_near(g_c_b_im, 8.0),
            "B preserved by csin", "B clobbered");
    cx_code(t, ac_pro_cgamma(), AC_PRO_CX_OK, "cgamma rc=OK");
    cxcheck(t, cx_near(g_c_a_re, 0.3) && cx_near(g_c_a_im, 0.4),
            "A preserved by cgamma", "A clobbered");
}

int main(void) {
    cx_tally_t t;

    t.checks = 0;
    t.failed = 0;

    printf("=== ApolloCalc-CAS cx_runner (v2.5 F1 complex extension suite) ===\n");

    test_cx_01(&t);
    test_cx_02(&t);
    test_cx_03(&t);
    test_cx_04(&t);
    test_cx_05(&t);
    test_cx_06(&t);
    test_cx_07(&t);
    test_cx_08(&t);
    test_cx_09(&t);
    test_cx_10(&t);
    test_cx_11(&t);
    test_cx_12(&t);
    test_cx_13(&t);
    test_cx_14(&t);
    test_cx_15(&t);
    test_cx_16(&t);
    test_cx_17(&t);
    test_cx_18(&t);
    test_cx_19(&t);
    test_cx_20(&t);

    printf("--- summary: %d checks, %d failed ---\n", t.checks, t.failed);
    if (t.failed == 0) {
        printf("RESULT: PASS\n");
        return 0;
    }
    printf("RESULT: FAIL\n");
    return 1;
}

#else /* !AC_PRO_ACTIVE */

/* 退化构建：本 TU 整体为空（零 .bss），cx_runner 目标亦不适用。 */
int main(void) {
    printf("cx_runner: AC_PRO_DEGRADED build, suite not applicable\n");
    return 0;
}

#endif /* AC_PRO_ACTIVE */
