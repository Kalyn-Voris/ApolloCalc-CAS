/* ============================================================
 * ApolloCalc-CAS · test_math_xlang.js — 数学函数层跨语言一致性驱动
 * 基准：JS/64KB/src（只读，严禁修改 JS 目录）
 * 用例 XL-M01…XL-M15 与 C/64KB/test/test_math.c 逐一对应，
 * 求值结果必须一致（容差 1e-9；libm 与 V8 有 ulp 级差异）。
 * 运行：node test/test_math_xlang.js
 * 输出：打印可粘贴进 test_math.c 的 C 表行（xl_math_row_t）。
 * ============================================================ */
/* 注意：本文件必须保持非严格模式（eval 展开的内核依赖隐式全局） */
var fs = require("fs");
var path = require("path");

var dir = path.join(__dirname, "..", "..", "..", "JS", "64KB", "src");
var code = ["core_parser.js", "core_math_functions.js", "core_evaluator.js"]
  .map(function (f) { return fs.readFileSync(path.join(dir, f), "utf8"); }).join("\n");
eval(code);

/* [expr, mode] —— mode: 1=DEG, 0=RAD（对应 C 侧 g_angle_deg） */
var CASES = [
  ["sin(30)",     1],   /* XL-M01 */
  ["cos(60)",     1],   /* XL-M02 */
  ["tan(45)",     1],   /* XL-M03 */
  ["asin(0.5)",   1],   /* XL-M04 */
  ["acos(0.5)",   1],   /* XL-M05 */
  ["atan(1)",     1],   /* XL-M06 */
  ["atan2(3,4)",  1],   /* XL-M07 */
  ["ln(2)",       1],   /* XL-M08 */
  ["log(100)",    1],   /* XL-M09 注意：两侧词法都只认 "log"，"log10(" 会被切成 log+10 */
  ["exp(1)",      1],   /* XL-M10 */
  ["sqrt(2)",     1],   /* XL-M11 */
  ["sin(pi/6)",   0],   /* XL-M12 */
  ["cos(pi/3)",   0],   /* XL-M13 */
  ["tan(pi/4)",   0],   /* XL-M14 */
  ["atan2(1,1)",  0]    /* XL-M15 */
];

console.log("/* ---- 由 test/test_math_xlang.js 输出转录（node test/test_math_xlang.js） ---- */");
console.log("static const xl_math_row_t XL_MATH[] = {");
for (var i = 0; i < CASES.length; i++) {
  angleMode = CASES[i][1] ? "DEG" : "RAD";
  var perr = parseExpression(CASES[i][0]);
  if (perr !== 0 || parseError !== 0) { throw new Error("parse failed: " + CASES[i][0] + " err=" + parseError); }
  var v = evalExpressionAt(0.0);
  var e = _evalError;
  if (e !== 0 || typeof v !== "number" || !isFinite(v)) {
    throw new Error("eval failed: " + CASES[i][0] + " err=" + e + " v=" + v);
  }
  var lit = v.toPrecision(17);
  if (lit.indexOf("e") < 0 && lit.indexOf("E") < 0 && lit.indexOf(".") < 0) { lit += ".0"; }
  var tag = "XL-M" + (i + 1 < 10 ? "0" : "") + (i + 1);   /* util.format 不支持 %02d 宽度标志 */
  console.log('    {"%s", %d, %s},   /* %s */',
              CASES[i][0], CASES[i][1], lit, tag);
}
console.log("};");
console.log("/* ---- XL-M 表结束（%d 条） ---- */", CASES.length);
