/* ApolloCalc · ac_pro_math.c — 用 const 预计算表替换上游 sin/cos 可变 LUT

   ProTEXT 包装层:上游 C/128KB/src/ac_math.c 仍作为唯一 TU 经本文件编译(不
   复制、不改上游源码)。本文件在宏层面把上游的 sin/cos 查表从「.bss 可变表 +
   运行时填充」替换为「const 预计算表(.rdata)」:

     上游原样(AC_MATH_USE_LUT 开启时):
         static double s_lut_sin[1024];  s_lut_cos[1024];   -> .bss 16388 B
         ac_math_lut_init()  运行时 sin()/cos() 填充
         ac_math_sin/cos     先查 s_lut_ready 再 ac_lut_lookup

     本包装(AC_PRO_MATH_LUT_CONST=1,默认):
         #define sin(x) ac_pro_lut_sin_fn(x)                (仅本 TU 生效)
         #define cos(x) ac_pro_lut_cos_fn(x)
         上游 ac_math_sin/cos 里的 return sin(rad)/cos(rad) 被重定向到本文件的
         const 表线性插值;同时 #undef AC_MATH_USE_LUT 阻断上游可变表编译(否则
         .bss +16388 B)。
         ac_math_lut_init 退化为空操作、ac_math_lut_ready 恒 1(表编译期已就绪),
         保持 ABI 兼容。

   关键顺序:
   1) <math.h> 必须在宏劫持之前引入:math.h 的 sin/cos 函数声明先被处理;其后
      经 include guard 再入的 <math.h>(上游头文件链)不会重复展开,函数式宏不
      会污染任何声明。
   2) 函数式宏只命中整 token sin(/cos( —— ac_math_sin、ac_math_cos、acos( 等
      标识符不受影响。上游 TU 内仅 ac_math_sin/ac_math_cos 的 return
      sin(rad)/cos(rad) 两处(AC_MATH_USE_LUT 关闭后)命中。
   3) 本文件的查表函数是纯表算术(乘加/归一化),绝不调用 sin/cos,避免宏自指。
   4) DEGRADED / AC_PRO=0 构建:整个劫持段被 #if 编译掉,本 TU 与上游逐位一致
      (AC_PRO_ACTIVE=0 时只多一次幂等的 <math.h>)。

   精度:const 表与上游运行时表同式同序生成(t = 2*PI*i/1024,17 位有效数字字
   面量,double 读回无损);线性插值最坏误差 h^2/8 ~ 4.7e-6。 */
#include <math.h>            /* 必须在宏劫持之前:先处理 sin/cos 声明 */
#include "ac_pro_config.h"

#if AC_PRO_ACTIVE && AC_PRO_MATH_LUT_CONST

#include "ac_pro_lut_tables.h"

/* 前置声明:先于宏劫持,定义放在文件尾(上游代码之后)。 */
static double ac_pro_lut_sin_fn(double rad);
static double ac_pro_lut_cos_fn(double rad);

/* 宏劫持:上游 TU 内的裸 sin(/cos( 调用重定向到 const 表插值。
   作用域仅限本 TU(后续 #include 的上游 ac_math.c)。 */
#define sin(x) ac_pro_lut_sin_fn(x)
#define cos(x) ac_pro_lut_cos_fn(x)

/* 阻断上游 Part B(可变 LUT)编译:s_lut_sin/s_lut_cos/s_lut_ready 若放行会给
   .bss 增加 16388 B,与 const 表目标直接冲突。
   const 路径下 AC_MATH_USE_LUT 被有意忽略(含命令行传入的情形)。 */
#ifdef AC_MATH_USE_LUT
#undef AC_MATH_USE_LUT
#endif

#endif /* AC_PRO_ACTIVE && AC_PRO_MATH_LUT_CONST */

#include "../../../C/128KB/src/ac_math.c"

#if AC_PRO_ACTIVE && AC_PRO_MATH_LUT_CONST

/* [English] Linear interpolation lookup — verbatim port of upstream
   ac_lut_lookup (C/128KB/src/ac_math.c, Part B), with AC_LUT_SIZE
   renamed to AC_PRO_LUT_SIZE. Non-finite guard first: libm
   sin/cos(NaN/±Inf) = NaN, but the range-reduction loops below would never
   terminate on ±Inf and would invoke UB on the (int)NaN cast. */
static double ac_pro_lut_lookup(const double *table, double rad) {
    double t = rad;
    double two_pi = 2.0 * AC_PI;
    double fi;
    int i0;
    int i1;
    double frac;

    if (t != t || t == (double)INFINITY || t == -(double)INFINITY) {
        return (double)NAN;
    }
    /* 归一化到 [0, 2π) */
    while (t < 0.0) { t += two_pi; }
    while (t >= two_pi) { t -= two_pi; }
    /* 线性插值 */
    fi = t * AC_PRO_LUT_SIZE / two_pi;
    i0 = (int)fi;
    if (i0 >= AC_PRO_LUT_SIZE) { i0 = AC_PRO_LUT_SIZE - 1; }
    i1 = i0 + 1;
    if (i1 >= AC_PRO_LUT_SIZE) { i1 = 0; }
    frac = fi - (double)i0;
    return table[i0] * (1.0 - frac) + table[i1] * frac;
}

/* 查表入口:入参已是弧度(DEG/RAD 转换仍由上游 ac_math_sin/cos 完成)。
   纯表算术,不得调用 sin/cos(宏仍在生效)。 */
static double ac_pro_lut_sin_fn(double rad) {
    return ac_pro_lut_lookup(ac_pro_lut_sin, rad);
}

static double ac_pro_lut_cos_fn(double rad) {
    return ac_pro_lut_lookup(ac_pro_lut_cos, rad);
}

/* ABI 兼容层:const 表编译期已定值,init 退化为空操作、ready 恒 1。
   上游声明在 AC_MATH_USE_LUT 之下(本路径已 #undef),故此处定义无原型冲突。 */
void ac_math_lut_init(void) {
    /* const 表:预计算,无需初始化 */
}

int ac_math_lut_ready(void) {
    return 1;
}

#endif /* AC_PRO_ACTIVE && AC_PRO_MATH_LUT_CONST */
