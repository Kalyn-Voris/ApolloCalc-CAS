/* ============================================================
 * ApolloCalc-CAS · test_complex_xlang.js — 复数基础层跨语言一致性驱动
 * 基准：JS/64KB/src/core_complex.js（只读，严禁修改 JS 目录）
 * 用例 XL-C01…XL-C12 与 C/64KB/test/test_complex.c 逐一对应，
 * 结果必须一致（容差 1e-9；libm 与 V8 有 ulp 级差异）。
 * 运行：node test/test_complex_xlang.js
 * 输出：打印可粘贴进 test_complex.c 的 C 表行（xl_crow_t）。
 * ============================================================ */
/* 注意：本文件必须保持非严格模式（eval 展开的内核依赖隐式全局） */
var fs = require("fs");
var path = require("path");

/* core_complex.js 依赖文件 1 已声明的结果寄存器，此处先行注入 */
var _tmpRe = 0.0;
var _tmpIm = 0.0;

var dir = path.join(__dirname, "..", "..", "..", "JS", "64KB", "src");
var code = fs.readFileSync(path.join(dir, "core_complex.js"), "utf8");
eval(code);

/* [op, aRe, aIm, bRe, bIm, n] —— n 仅 POW 使用，其余填 0 */
var CASES = [
  ["ADD",  3,  4,  1, -2,  0],   /* XL-C01 (3+4i)+(1-2i) = 4+2i */
  ["MUL",  3,  4,  1, -2,  0],   /* XL-C02 (3+4i)*(1-2i) = 11-2i */
  ["DIV",  1,  2,  3, -4,  0],   /* XL-C03 (1+2i)/(3-4i) = -0.2+0.4i */
  ["ABS",  3,  4,  0,  0,  0],   /* XL-C04 |3+4i| = 5 */
  ["ARG",  1,  1,  0,  0,  0],   /* XL-C05 arg(1+i) = π/4 */
  ["CONJ", 3,  4,  0,  0,  0],   /* XL-C06 conj(3+4i) = 3-4i */
  ["SQRT", 3,  4,  0,  0,  0],   /* XL-C07 sqrt(3+4i) = 2+i */
  ["SQRT", -4, 0,  0,  0,  0],   /* XL-C08 sqrt(-4) = 2i（正虚部分支） */
  ["POW",  1,  1,  0,  0,  3],   /* XL-C09 (1+i)^3 = -2+2i */
  ["POW",  1,  1,  0,  0, 10],   /* XL-C10 (1+i)^10 = 32i */
  ["POW",  2,  0,  0,  0, -2],   /* XL-C11 2^-2 = 0.25 */
  ["POW",  5,  7,  0,  0,  0]    /* XL-C12 (5+7i)^0 = 1+0i */
];

/* 按 CASES 行驱动 JS 64KB 内核，返回 [结果实部, 结果虚部]
 * （ABS/ARG 为标量返回，虚部位补 0，与 C 表行约定一致） */
function run(c) {
  _aRe = c[1]; _aIm = c[2]; _bRe = c[3]; _bIm = c[4];
  switch (c[0]) {
    case "ADD":  cAdd();  return [_tmpRe, _tmpIm];
    case "SUB":  cSub();  return [_tmpRe, _tmpIm];
    case "MUL":  cMul();  return [_tmpRe, _tmpIm];
    case "DIV":  cDiv();  return [_tmpRe, _tmpIm];
    case "ABS":  return [cAbs(), 0];
    case "ARG":  return [cArg(), 0];
    case "CONJ": cConj(); return [_tmpRe, _tmpIm];
    case "SQRT": cSqrt(); return [_tmpRe, _tmpIm];
    case "POW":  cPowInt(c[5]); return [_tmpRe, _tmpIm];
  }
  throw new Error("unknown op " + c[0]);
}

/* 17 位有效数字字面量，C 侧按同字面量解析 → 位级一致 */
function lit(v) {
  var s = v.toPrecision(17);
  if (s.indexOf("e") < 0 && s.indexOf("E") < 0 && s.indexOf(".") < 0) { s += ".0"; }
  return s;
}

console.log("/* ---- 由 test/test_complex_xlang.js 输出转录（node test/test_complex_xlang.js） ---- */");
console.log("static const xl_crow_t XL_COMPLEX[] = {");
for (var i = 0; i < CASES.length; i++) {
  var c = CASES[i];
  _aRe = 0; _aIm = 0; _bRe = 0; _bIm = 0;
  var r = run(c);
  if (typeof r[0] !== "number" || !isFinite(r[0]) ||
      typeof r[1] !== "number" || !isFinite(r[1])) {
    throw new Error("non-finite result: " + c[0] + " -> " + r);
  }
  var tag = "XL-C" + (i + 1 < 10 ? "0" : "") + (i + 1);   /* util.format 不支持 %02d 宽度标志 */
  console.log('    {%s, %s, %s, %s, XLC_%s, %d, %s, %s},   /* %s */',
              lit(c[1]), lit(c[2]), lit(c[3]), lit(c[4]), c[0], c[5],
              lit(r[0]), lit(r[1]), tag);
}
console.log("};");
console.log("/* ---- XL-C 表结束（%d 条） ---- */", CASES.length);
