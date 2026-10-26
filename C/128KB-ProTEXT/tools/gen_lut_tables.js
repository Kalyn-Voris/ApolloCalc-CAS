#!/usr/bin/env node
/* ============================================================
 * ApolloCalc-CAS · C/128KB-ProTEXT/tools/gen_lut_tables.js
 * ------------------------------------------------------------
 * 【用途 / Purpose】
 *   生成 src/ac_pro_lut_tables.h —— sin/cos 的 1024 项 const 预计算
 *   查找表（.rdata，零 .bss 成本）。Pro 包装层用它把上游 ac_math.c 的
 *   「运行时填充可变 LUT」替换为「编译期定值 const 表」：
 *     - 上游 ac_math_lut_init 需要 2×8192 B .bss + 运行时填充；
 *     - const 表把这 16 KiB 从 RAM 预算挪进只读段，且免去 init。
 *
 * 【逐位一致性 / Bit-exactness contract】
 *   1) 角度步长与上游 ac_math.c 完全同式同序：
 *          t = 2.0 * AC_PI * i / AC_LUT_SIZE      （C 左结合）
 *          t = 2.0 * PI * i / SIZE                （JS 同为左结合）
 *      两边都是 IEEE-754 double 三次运算（(2.0*PI)*i)/1024，逐位一致。
 *   2) AC_PI 用与上游 ac_math.h 相同的字面量 3.141592653589793 ——
 *      C 与 JS 解析该十进制字面量得到同一个最近邻 double。
 *   3) 数值用 toPrecision(17) 输出：17 位有效十进制保证 IEEE-754
 *      double 无损往返（等价于 C 的 %.17g），C 编译器读回得到与
 *      Math.sin/Math.cos 相同的 double 位型。
 *   JS Math.sin 与目标 libm sin 至多相差末位 1 ulp，远小于线性插值
 *   本身的 h^2/8 ~ 4.7e-6 误差，不影响精度结论。
 *
 * 【再生成 / Regenerate】（在 C/128KB-ProTEXT/ 目录下执行）
 *      node tools/gen_lut_tables.js
 *   生成物已提交；仅在改表尺寸/精度或上游 AC_PI 变化时重跑。
 *
 * 【边界 / Constraints】
 *   生成物是纯数据头文件：static const double 数组 + 守卫，无代码、
 *   无 malloc、无 VLA；仅被 src/ac_pro_math.c 这一个 TU 包含一次。
 * ============================================================ */
'use strict';

const AC_PI = 3.141592653589793; /* 与上游 ac_math.h 的 #define AC_PI 逐位一致 */
const AC_LUT_SIZE = 1024;        /* 与上游 #define AC_LUT_SIZE 一致 */
const PER_LINE = 4;              /* 每行字面量个数 */
const FS = require('fs');
const PATH = require('path');

const OUT = PATH.join(__dirname, '..', 'src', 'ac_pro_lut_tables.h');

function fmt(x) {
    if (!Number.isFinite(x)) {
        throw new Error('non-finite table entry: ' + x);
    }
    /* toPrecision(17)：17 位有效数字，double 无损往返（同 C %.17g） */
    return x.toPrecision(17);
}

function emitTable(name, fn) {
    const lines = [];
    lines.push('static const double ' + name + '[AC_PRO_LUT_SIZE] = {');
    for (let i = 0; i < AC_LUT_SIZE; i += PER_LINE) {
        const vals = [];
        for (let j = i; j < i + PER_LINE && j < AC_LUT_SIZE; j++) {
            /* 与上游 ac_math_lut_init 同式同序：((2.0*PI)*i)/SIZE */
            const t = 2.0 * AC_PI * j / AC_LUT_SIZE;
            vals.push(fmt(fn(t)));
        }
        const last = (i + PER_LINE >= AC_LUT_SIZE);
        lines.push('    ' + vals.join(', ') + (last ? '' : ','));
    }
    lines.push('};');
    return lines.join('\n');
}

const header = [
    '/* ============================================================',
    ' * ApolloCalc-CAS · C/128KB-ProTEXT/src/ac_pro_lut_tables.h',
    ' * ------------------------------------------------------------',
    ' * 【生成物，勿手编 / GENERATED FILE — DO NOT EDIT BY HAND】',
    ' * 再生成命令（在 C/128KB-ProTEXT/ 目录下）:',
    ' *     node tools/gen_lut_tables.js',
    ' *',
    ' * sin/cos 1024 项 const 预计算查找表，覆盖 [0, 2*pi)，供',
    ' * src/ac_pro_math.c 的 ac_pro_lut_lookup 线性插值使用。',
    ' * 生成参数（与上游 C/128KB/src/ac_math.c 的运行时填充逐位同式）:',
    ' *     t_i = 2.0 * 3.141592653589793 * i / 1024    (i = 0..1023)',
    ' *     sin 表 = Math.sin(t_i)   cos 表 = Math.cos(t_i)',
    ' * 字面量以 17 位有效数字输出（同 C %.17g），double 读回无损。',
    ' * 最坏插值误差 h^2/8 ~ 4.7e-6（h = 2*pi/1024）。',
    ' *',
    ' * 存储口径: static const -> .rdata（只读段），.bss 零占用。',
    ' * ============================================================ */',
    '#ifndef AC_PRO_LUT_TABLES_H',
    '#define AC_PRO_LUT_TABLES_H',
    '',
    '#define AC_PRO_LUT_SIZE 1024',
    ''
].join('\n');

const body = [
    emitTable('ac_pro_lut_sin', Math.sin),
    '',
    emitTable('ac_pro_lut_cos', Math.cos),
    ''
].join('\n');

FS.writeFileSync(OUT, header + '\n' + body + '#endif /* AC_PRO_LUT_TABLES_H */\n', 'utf8');
console.log('wrote ' + OUT + ' (' + AC_LUT_SIZE + ' entries x2, ' + PER_LINE + '/line)');
