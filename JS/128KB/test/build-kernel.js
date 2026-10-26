/* ============================================================
 * build-kernel.js — ApolloCalc-CAS JS/128KB 内核拼接脚本（构建工具，非设备交付物）
 * 运行：node test/build-kernel.js   （JS/128KB 目录下，Node 环境）
 *
 * 功能：
 *   1. 按序读取 common/ 下 9 个 core 文件，用 \n 拼接
 *   2. 末尾追加 export 块（index.js 唯一访问点）
 *   3. 写出 ../src/core_kernel.js
 *   4. node --check 语法校验（ESM export 语法按模块解析校验）
 *   5. eval() 拼接体（不含 export 块——export 仅 ESM 合法，eval 中会语法错误），
 *      复用 _test_evaluator.js(41 项) + _test_solvers.js(42 项) 断言逻辑，共 83 项
 *   6. 静态扫描 export 块引用的 78 个标识符是否在文件 1-12 中声明过 + eval 运行时存在性复核
 *   7. 动态 import() 生成的 core_kernel.js（ESM/严格模式），走一遍导出接口全链路冒烟
 *
 * 全部通过输出：BUILD OK, all tests passed；否则输出失败详情并退出码 1
 * ============================================================ */
var fs = require("fs");
var path = require("path");
var os = require("os");
var url = require("url");
var cp = require("child_process");

var ROOT = __dirname;
var COMMON = path.join(ROOT, "..", "src");
var OUT = path.join(COMMON, "core_kernel.js");

var FILES = [
  "core_parser.js",
  "core_math_functions.js",
  "core_complex.js",
  "core_exact.js",
  "core_evaluator.js",
  "core_calculus.js",
  "core_base.js",
  "core_solvers.js",
  "core_ratroot.js",
  "core_multivar.js",
  "core_matrix.js",
  "core_special.js"
];

/* export 块：内容与交付约定逐字一致，禁止在此改动（改了会与 index.js 契约漂移） */
var EXPORT_BLOCK = `/* ============================================================
 * ApolloCalc · core_kernel.js — 内核统一入口
 * 由 build-kernel.js 自动拼接，禁止手动编辑
 * 拼接顺序：parser → math_functions → complex → exact → evaluator → calculus → base → solvers → ratroot → multivar → matrix → special（12 文件）
 * ============================================================ */

/* 供 index.js 引用的内核接口（UI 层唯一访问点） */
export default {
  /* ---- 解析层 ---- */
  parseExpression: parseExpression,
  dumpTokenStream: dumpTokenStream,

  /* ---- 数学执行层 ---- */
  applyFunction: applyFunction,

  /* ---- 精确数值系统（五分量） ---- */
  exactReset: exactReset,
  exactIsValid: function () { return exactIsValid; },
  getExactRat: function () { return exactRat; },
  getExactRadNum: function () { return exactRadNum; },
  getExactDen: function () { return exactDen; },
  getExactRad: function () { return exactRad; },
  getExactExp: function () { return exactExp; },
  getApproxValue: function () { return approxValue; },

  /* ---- 求值层 ---- */
  evalExpressionAt: evalExpressionAt,
  evalEquationAt: evalEquationAt,
  extractCoefficients: extractCoefficients,
  evaluatorReset: evaluatorReset,

  /* ---- 求解层 ---- */
  solveQuadratic: solveQuadratic,
  solveCubic: solveCubic,
  solveQuartic: solveQuartic,
  solveLinearSystem: solveLinearSystem,
  verifyRoots: verifyRoots,
  solversReset: solversReset,

  /* ---- 安全防护层 ---- */
  preCheckExpression: preCheckExpression,
  guardDegree: guardDegree,
  guardMultivariateNonlinear: guardMultivariateNonlinear,
  checkMemoryWatermark: checkMemoryWatermark,

  /* ---- 求解结果（getter 形式，避免跨作用域赋值） ---- */
  getRootCount: function () { return _rootCount; },
  getRootRe: function (i) { return _rootRe[i]; },
  getRootIm: function (i) { return _rootIm[i]; },
  getRootExactRat: function (i) { return _rootExactRat[i]; },
  getRootExactRadNum: function (i) { return _rootExactRadNum[i]; },
  getRootExactRad: function (i) { return _rootExactRad[i]; },
  getRootExactDen: function (i) { return _rootExactDen[i]; },
  getRootExactExp: function (i) { return _rootExactExp[i]; },
  getRootExactValid: function (i) { return _rootExactValid[i]; },
  getSolveError: function () { return _solveError; },
  getResidualMax: function () { return _residualMax; },

  /* ---- 全局状态切换 ---- */
  setAngleMode: function (mode) { angleMode = mode; },
  getAngleMode: function () { return angleMode; },

  /* ---- 矩阵池写入（UI 层给线性方程组输入系数用，matrixPool 156 = 12×13） ---- */
  setMatrixElement: function (i, v) { matrixPool[i] = v; },

  /* ---- 复数域函数与寄存器访问器（128KB 扩展：UI 层调用序 setComplexA → cXxx → getComplexTmp*） ---- */
  setComplexA: function (re, im) { _aRe = re; _aIm = im; },
  setComplexB: function (re, im) { _bRe = re; _bIm = im; },
  getComplexTmpRe: function () { return _tmpRe; },
  getComplexTmpIm: function () { return _tmpIm; },
  cExp: function () { cExp(); },
  cLn: function () { cLn(); },
  cPowReal: function (w) { cPowReal(w); },

  /* ---- 数值微积分（128KB 扩展：UI 层先 parseExpression 再调 calcXxx，表达式变更前须 calculusReset） ---- */
  calcDerivative: function (x) { return calcDerivative(x); },
  calcDerivative2: function (x) { return calcDerivative2(x); },
  calcIntegral: function (a, b) { return calcIntegral(a, b); },
  calcLimit: function (a, side) { return calcLimit(a, side); },
  calculusReset: function () { calculusReset(); },
  getCalcError: function () { return _calcError; },
  getCalcIterations: function () { return _calcIterations; },

  /* ---- 泰勒展开（128KB 扩展：先 parseExpression，calcTaylor 缓存系数后 taylorEval 求值） ---- */
  calcTaylor: function (x0, order) { return calcTaylor(x0, order); },
  taylorEval: function (x) { return taylorEval(x); },
  getTaylorCoeff: function (i) { return _taylorCoeffs[i]; },
  getTaylorOrder: function () { return _taylorOrder; },

  /* ---- 进制转换与 SI 前缀（128KB 扩展：先 decToXxx/siNormalize 再 toString 出口） ---- */
  decToBin: function (n) { return decToBin(n); },
  decToOct: function (n) { return decToOct(n); },
  decToHex: function (n) { return decToHex(n); },
  baseToString: function () { return baseToString(); },
  siNormalize: function (v) { return siNormalize(v); },
  siToString: function () { return siToString(); },
  getSiMantissa: function () { return _siMantissa; },
  getSiPrefixExp: function () { return _siPrefixExp; },
  getSiValid: function () { return _siValid; },
  getBaseLen: function () { return _baseLen; },
  baseReset: function () { baseReset(); },

  /* ---- v2.0.0 自包含三模块（IIFE 单命名空间，UI 层经 multivar.* / matrix.* / special.* 访问） ---- */
  multivar: ApolloMultivar,
  matrix: ApolloMatrix,
  special: ApolloSpecial
};`;

/* export 块引用的全部内核标识符（78 个；函数形参 i/v/mode/re/im/w/n/x/a/b/side/order/x0 除外） */
var EXPORTED_NAMES = [
  "parseExpression", "dumpTokenStream",
  "applyFunction",
  "exactReset", "exactIsValid", "exactRat", "exactRadNum", "exactRad", "exactDen", "exactExp", "approxValue",
  "evalExpressionAt", "evalEquationAt", "extractCoefficients", "evaluatorReset",
  "solveQuadratic", "solveCubic", "solveQuartic", "solveLinearSystem", "verifyRoots", "solversReset",
  "preCheckExpression", "guardDegree", "guardMultivariateNonlinear", "checkMemoryWatermark",
  "_rootCount", "_rootRe", "_rootIm", "_rootExactRat", "_rootExactRadNum", "_rootExactRad",
  "_rootExactDen", "_rootExactExp", "_rootExactValid", "_solveError", "_residualMax",
  "angleMode", "matrixPool",
  "cExp", "cLn", "cPowReal",
  "_aRe", "_aIm", "_bRe", "_bIm", "_tmpRe", "_tmpIm",
  "calcDerivative", "calcDerivative2", "calcIntegral", "calcLimit", "calculusReset",
  "_calcError", "_calcIterations", "_calcResult", "_calcRpnReady",
  "calcTaylor", "taylorEval", "_taylorCoeffs", "_taylorOrder",
  "decToBase", "decToBin", "decToOct", "decToHex", "baseToString",
  "siPrefixCode", "siNormalize", "siToString", "baseReset",
  "_baseBuf", "_baseLen", "_siMantissa", "_siPrefixCode", "_siPrefixExp", "_siValid",
  "ApolloMultivar", "ApolloMatrix", "ApolloSpecial"
];

function die(msg) {
  console.log(msg);
  process.exit(1);
}

/* ---- [1/5] 按序读取 + \n 拼接 + 追加 export 块 + 写出 ---- */
var parts = [];
for (var fi = 0; fi < FILES.length; fi++) {
  var srcPath = path.join(COMMON, FILES[fi]);
  if (!fs.existsSync(srcPath)) { die("BUILD FAILED: 缺少源文件 " + srcPath); }
  parts.push(fs.readFileSync(srcPath, "utf8"));
}
var kernelBody = parts.join("\n");
var kernelFull = kernelBody + "\n" + EXPORT_BLOCK + "\n";
fs.writeFileSync(OUT, kernelFull);
console.log("[1/5] 已拼接 " + FILES.length + " 个源文件 -> " + OUT + " (" + Buffer.byteLength(kernelFull, "utf8") + " bytes)");

/* ---- [2/5] node --check 语法校验 ---- */
var syntaxOK = false, syntaxDetail = "";
var r1 = cp.spawnSync(process.execPath, ["--check", OUT], { encoding: "utf8" });
if (!r1.error && r1.status === 0) {
  syntaxOK = true;
  console.log("[2/5] node --check 通过（Node " + process.version + "，export 块按 ES Module 语法解析）");
} else {
  /* 旧版 Node 将 .js 按 CommonJS 解析会拒绝 export；用 .mjs 临时副本按 ESM 复核 */
  var tmpMjs = path.join(os.tmpdir(), "core_kernel_syntax_check.mjs");
  fs.writeFileSync(tmpMjs, kernelFull);
  var r2 = cp.spawnSync(process.execPath, ["--check", tmpMjs], { encoding: "utf8" });
  try { fs.unlinkSync(tmpMjs); } catch (eUnlink) { /* 临时文件，忽略 */ }
  if (!r2.error && r2.status === 0) {
    syntaxOK = true;
    console.log("[2/5] node --check 通过（经 .mjs 副本按 ES Module 校验；CJS 直检报错属预期）");
  } else {
    syntaxDetail = (r1.error ? String(r1.error) : (r1.stderr || "")) + "\n" + (r2.error ? String(r2.error) : (r2.stderr || ""));
  }
}
if (!syntaxOK) { die("BUILD FAILED: node --check 语法校验未通过\n" + syntaxDetail); }

/* ---- [3/5] export 块引用标识符声明扫描（静态 grep 语义 + eval 后运行时复核） ---- */
var undeclaredStatic = [];
for (var si = 0; si < EXPORTED_NAMES.length; si++) {
  var n0 = EXPORTED_NAMES[si];
  /* 匹配 var n / function n / 逗号声明符 ", n" 三种声明形态 */
  var reDecl = new RegExp("(?:^|[^\\w$.])(?:var|function|,)\\s+" + n0 + "\\b");
  if (!reDecl.test(kernelBody)) { undeclaredStatic.push(n0); }
}
if (undeclaredStatic.length > 0) { die("BUILD FAILED: export 块引用了未声明的全局变量（静态扫描）: " + undeclaredStatic.join(", ")); }

/* ---- [4/5] eval 拼接体（不含 export 块）+ 运行时存在性复核 + 80 项测试回归 ---- */
eval(kernelBody);  /* 直接 eval：var/function 声明进入本模块作用域，测试代码同作用域访问 */

var missingRuntime = [];
for (var ri = 0; ri < EXPORTED_NAMES.length; ri++) {
  var n1 = EXPORTED_NAMES[ri];
  try { eval("(" + n1 + " !== undefined)"); } catch (eRef) { missingRuntime.push(n1); }
}
if (missingRuntime.length > 0) { die("BUILD FAILED: eval 后以下标识符不可访问（运行时复核）: " + missingRuntime.join(", ")); }

var pass = 0, fail = 0;
function approx(a, b, tol) { return Math.abs(a - b) <= (tol || 1e-9); }
function check(name, got, want, tol) {
  if (approx(got, want, tol)) { pass++; }
  else { fail++; console.log("FAIL " + name + " got=" + got + " want=" + want + " err=" + _evalError); }
}

/* ========== 以下断言逻辑复用自 _test_evaluator.js（41 项） ========== */
angleMode = "RAD";

/* ---- 基础表达式 ---- */
check("2+3*4", (parseExpression("2+3*4"), evalExpressionAt(0)), 14);
check("(2+3)*4", (parseExpression("(2+3)*4"), evalExpressionAt(0)), 20);
check("2^3^2", (parseExpression("2^3^2"), evalExpressionAt(0)), 512);
check("-2^2", (parseExpression("-2^2"), evalExpressionAt(0)), -4);
check("2^-1", (parseExpression("2^-1"), evalExpressionAt(0)), 0.5);
check("--4", (parseExpression("--4"), evalExpressionAt(0)), 4);
check("10/4", (parseExpression("10/4"), evalExpressionAt(0)), 2.5);
check("2pi", (parseExpression("2pi"), evalExpressionAt(0)), 2 * Math.PI);
check("e^2", (parseExpression("e^2"), evalExpressionAt(0)), Math.E * Math.E);
check("sin(pi/2)", (parseExpression("sin(pi/2)"), evalExpressionAt(0)), 1, 1e-9);
check("sqrt(2)x at x=4", (parseExpression("sqrt(2)x"), evalExpressionAt(4)), 4 * Math.sqrt(2), 1e-9);
check("abs(-7)", (parseExpression("abs(-7)"), evalExpressionAt(0)), 7);
check("ln(e)", (parseExpression("ln(e)"), evalExpressionAt(0)), 1);
check("x^2+3x-5 @x=2", (parseExpression("x^2+3x-5"), evalExpressionAt(2)), 5);
check("atan2(1,1)", (parseExpression("atan2(1,1)"), evalExpressionAt(0)), Math.PI / 4, 1e-9);
check("3x^2 @x=-2", (parseExpression("3x^2"), evalExpressionAt(-2)), 12);
check("2x^2-8 @x=3", (parseExpression("2x^2-8"), evalExpressionAt(3)), 10);

/* ---- 错误码 ---- */
parseExpression("1/0"); evalExpressionAt(0);
console.log(_evalError === 1 ? "PASS 1/0 err=1" : "FAIL 1/0 err=" + _evalError), _evalError === 1 ? pass++ : fail++;
parseExpression("sqrt(-1)"); evalExpressionAt(0);
if (_evalError === 2) { pass++; } else { fail++; console.log("FAIL sqrt(-1) err=" + _evalError); }
parseExpression("ln(0)"); evalExpressionAt(0);
if (_evalError === 2 || _evalError === 3) { pass++; }  /* ln(0)→-Inf，按溢出/域错误均视为失败 */
else { fail++; console.log("FAIL ln(0) err=" + _evalError); }

/* ---- 方程做差 ---- */
check("eq x^2-4=0 @x=2", (parseExpression("x^2-4=0"), evalEquationAt(2)), 0);
check("eq x^2-4=0 @x=3", (parseExpression("x^2-4=0"), evalEquationAt(3)), 5);
check("eq 2x+1=x+5 @x=1", (parseExpression("2x+1=x+5"), evalEquationAt(1)), -3);  /* 3-6=-3 */
check("eq 2x+1=x+5 @x=4", (parseExpression("2x+1=x+5"), evalEquationAt(4)), 0);

/* ---- 系数提取 ---- */
parseExpression("x^2-5x+6=0");
var rc = extractCoefficients(2);
check("coeff c0", _coeffs[0], 6, 1e-7);
check("coeff c1", _coeffs[1], -5, 1e-7);
check("coeff c2", _coeffs[2], 1, 1e-7);

parseExpression("2x^3-3x^2+4x-1=0");
extractCoefficients(3);
check("cubic c0", _coeffs[0], -1, 1e-7);
check("cubic c1", _coeffs[1], 4, 1e-7);
check("cubic c2", _coeffs[2], -3, 1e-7);
check("cubic c3", _coeffs[3], 2, 1e-7);

parseExpression("x^4-2x^2+1=0");
extractCoefficients(4);
check("quartic c0", _coeffs[0], 1, 1e-7);
check("quartic c1", _coeffs[1], 0, 1e-7);
check("quartic c2", _coeffs[2], -2, 1e-7);
check("quartic c3", _coeffs[3], 0, 1e-7);
check("quartic c4", _coeffs[4], 1, 1e-7);

/* 非多项式验证"采样点求值失败"路径：sqrt(x) 在负采样点域错误 → 系数清零 */
parseExpression("sqrt(x)=0");
var rc2 = extractCoefficients(2);
if (rc2 === 2 && _coeffs[0] === 0 && _coeffs[1] === 0 && _coeffs[2] === 0) { pass++; }
else { fail++; console.log("FAIL domain coeffs rc=" + rc2 + " c=" + _coeffs[0] + "," + _coeffs[1] + "," + _coeffs[2]); }

/* 多项式但右侧含 x → x^2=2x+3 应提取出 c=[-3,-2,1] */
parseExpression("x^2=2x+3");
extractCoefficients(2);
check("x^2=2x+3 c0", _coeffs[0], -3, 1e-7);
check("x^2=2x+3 c1", _coeffs[1], -2, 1e-7);
check("x^2=2x+3 c2", _coeffs[2], 1, 1e-7);

/* ---- 复位 ---- */
evaluatorReset();
if (_rpnLen === 0 && _evalSp === 0 && _evalError === 0 && _evalResult === 0 && _coeffs[3] === 0) { pass++; }
else { fail++; console.log("FAIL reset"); }

var evalPass = pass, evalFail = fail;
console.log("---- evaluator: " + evalPass + " passed, " + evalFail + " failed ----");

/* ========== 以下断言逻辑复用自 _test_solvers.js（42 项） ========== */
/* 解方程辅助：parse → extractCoefficients(deg) → solveXxx() */
function solvePoly(src, deg) {
  solversReset();
  parseExpression(src);
  extractCoefficients(deg);
  if (deg === 2) { solveQuadratic(); }
  else if (deg === 3) { solveCubic(); }
  else if (deg === 4) { solveQuartic(); }
}

/* 检查期望实根集合是否全部命中（允许乱序） */
function checkRoots(name, wantArr, tol) {
  var ok = (_solveError === 0);
  if (ok) {
    for (var w = 0; w < wantArr.length; w++) {
      var hit = 0;
      for (var i = 0; i < _rootCount; i++) {
        if (approx(_rootRe[i], wantArr[w], tol) && Math.abs(_rootIm[i]) <= (tol || 1e-9)) { hit = 1; break; }
      }
      if (hit === 0) { ok = 0; break; }
    }
  }
  if (ok) { pass++; }
  else {
    fail++;
    var s = name + " err=" + _solveError + " roots=[";
    for (var j = 0; j < _rootCount; j++) { s += _rootRe[j] + (_rootIm[j] === 0 ? "" : "+" + _rootIm[j] + "i") + " "; }
    console.log("FAIL " + s + "] want=[" + wantArr + "]");
  }
}

/* 检查期望 (re,im) 根对集合是否全部命中（允许乱序，用于复根） */
function checkRootsC(name, wantPairs, tol) {
  var ok = (_solveError === 0 && _rootCount === wantPairs.length);
  if (ok) {
    for (var w = 0; w < wantPairs.length; w++) {
      var hit = 0;
      for (var i = 0; i < _rootCount; i++) {
        if (approx(_rootRe[i], wantPairs[w][0], tol) && approx(_rootIm[i], wantPairs[w][1], tol)) { hit = 1; break; }
      }
      if (hit === 0) { ok = 0; break; }
    }
  }
  if (ok) { pass++; }
  else {
    fail++;
    var s = name + " err=" + _solveError + " roots=[";
    for (var j = 0; j < _rootCount; j++) { s += _rootRe[j] + (_rootIm[j] === 0 ? "" : "+" + _rootIm[j] + "i") + " "; }
    console.log("FAIL " + s + "] want=" + JSON.stringify(wantPairs));
  }
}

angleMode = "RAD";

/* ==================== 模块一：二次 ==================== */
solvePoly("x^2-5x+6=0", 2);
checkRoots("quad (x-3)(x-2)", [3, 2], 1e-9);
if (_rootExactValid[0] === 1 && _rootExactValid[1] === 1 &&
    _rootExactRat[0] === 3 && _rootExactRadNum[0] === 0 && _rootExactRad[0] === 1 &&
    _rootExactDen[0] === 1 && _rootExactExp[0] === 0 &&
    _rootExactRat[1] === 2 && _rootExactRadNum[1] === 0) { pass++; }
else { fail++; console.log("FAIL quad exact integer roots: v=" + _rootExactValid[0] + _rootExactValid[1] +
  " rat=" + _rootExactRat[0] + "/" + _rootExactDen[0] + " rn=" + _rootExactRadNum[0] + " r=" + _rootExactRad[0]); }

solvePoly("x^2-x-1=0", 2);
checkRoots("quad golden", [(1 + Math.sqrt(5)) / 2, (1 - Math.sqrt(5)) / 2], 1e-9);
if (_rootExactValid[0] === 1 && _rootExactValid[1] === 1 &&
    _rootExactRat[0] === 1 && _rootExactRadNum[0] === 1 && _rootExactRad[0] === 5 &&
    _rootExactDen[0] === 2 && _rootExactExp[0] === 0 &&
    _rootExactRat[1] === 1 && _rootExactRadNum[1] === -1 && _rootExactRad[1] === 5 &&
    _rootExactDen[1] === 2) { pass++; }  /* 混合形式 (1±√5)/2 五分量可表达 */
else { fail++; console.log("FAIL golden exact fields: v=" + _rootExactValid[0] + _rootExactValid[1] +
  " rat=" + _rootExactRat[0] + "," + _rootExactRat[1] + " rn=" + _rootExactRadNum[0] + "," + _rootExactRadNum[1] +
  " rad=" + _rootExactRad[0] + " den=" + _rootExactDen[0] + "," + _rootExactDen[1]); }

solvePoly("x^2-2=0", 2);
checkRoots("quad sqrt2", [Math.SQRT2, -Math.SQRT2], 1e-9);
if (_rootExactValid[0] === 1 && _rootExactRat[0] === 0 && _rootExactRadNum[0] === 1 &&
    _rootExactDen[0] === 1 && _rootExactRad[0] === 2 &&
    _rootExactValid[1] === 1 && _rootExactRadNum[1] === -1) { pass++; }
else { fail++; console.log("FAIL sqrt2 exact: rat=" + _rootExactRat[0] + " rn=" + _rootExactRadNum[0] + "/" + _rootExactDen[0] +
  " rad=" + _rootExactRad[0] + " v=" + _rootExactValid[0] + _rootExactValid[1] +
  " rn1=" + _rootExactRadNum[1]); }

solvePoly("x^2+1=0", 2);
if (_rootCount === 2 && approx(_rootRe[0], 0, 1e-12) && approx(_rootIm[0], 1, 1e-9) &&
    approx(_rootIm[1], -1, 1e-9) && _rootExactValid[0] === 0) { pass++; }
else { fail++; console.log("FAIL quad complex: " + _rootRe[0] + "+" + _rootIm[0] + "i"); }

solvePoly("2x^2+3x-2=0", 2);
checkRoots("quad 2x^2+3x-2", [0.5, -2], 1e-9);

solvePoly("x^2-4x+4=0", 2);
checkRoots("quad double root", [2, 2], 1e-9);
if (_rootExactValid[0] === 1 && _rootExactValid[1] === 1) { pass++; }  /* D=0 仍精确 */
else { fail++; console.log("FAIL double root exact validity"); }

/* ==================== 模块二：三次 ==================== */
solvePoly("x^3-6x^2+11x-6=0", 3);
checkRoots("cubic (x-1)(x-2)(x-3) ratroot", [1, 2, 3], 1e-9);
/* 有理根快路径：三个整数根全部带精确形式（此前三次方程一律无精确形式） */
if (_rootExactValid[0] === 1 && _rootExactValid[1] === 1 && _rootExactValid[2] === 1) { pass++; }
else { fail++; console.log("FAIL cubic rational exact flags: v=" + _rootExactValid[0] +
  _rootExactValid[1] + _rootExactValid[2]); }

solvePoly("x^3-1=0", 3);
if (_solveError === 0 && _rootCount === 3 && approx(_rootRe[0], 1, 1e-9) && _rootIm[0] === 0 &&
    approx(_rootRe[1], -0.5, 1e-9) && approx(_rootIm[1], Math.sqrt(3) / 2, 1e-9) &&
    approx(_rootIm[2], -Math.sqrt(3) / 2, 1e-9)) { pass++; }
else { fail++; console.log("FAIL cubic cardano x^3-1: r0=" + _rootRe[0] + " r1=" + _rootRe[1] + " i1=" + _rootIm[1]); }

solvePoly("x^3-6x^2+12x-8=0", 3);
checkRoots("cubic triple root", [2, 2, 2], 1e-9);

solvePoly("x^3-3x=0", 3);
checkRoots("cubic irreducible x^3-3x", [Math.sqrt(3), 0, -Math.sqrt(3)], 1e-9);

/* 非首一项系数：2x^3-x-1 = (x-1)(2x^2+2x+1) → 1 与 -0.5±0.5i */
solvePoly("2x^3-x-1=0", 3);
checkRootsC("cubic 2x^3-x-1", [[1, 0], [-0.5, 0.5], [-0.5, -0.5]], 1e-9);

/* ==================== 模块三：四次 ==================== */
solvePoly("x^4-5x^2+4=0", 4);
checkRoots("quartic biquad (x^2-1)(x^2-4)", [1, -1, 2, -2], 1e-9);
/* 有理根快路径：四个整数根全部带精确形式 */
if (_rootExactValid[0] === 1 && _rootExactValid[1] === 1 &&
    _rootExactValid[2] === 1 && _rootExactValid[3] === 1) { pass++; }
else { fail++; console.log("FAIL quartic rational exact flags: v=" + _rootExactValid[0] +
  _rootExactValid[1] + _rootExactValid[2] + _rootExactValid[3]); }

solvePoly("x^4-1=0", 4);
checkRootsC("quartic x^4-1 biquad", [[1, 0], [-1, 0], [0, 1], [0, -1]], 1e-9);

solvePoly("x^4+5x^2+4=0", 4);
if (_solveError === 0 && _rootCount === 4 &&
    approx(Math.abs(_rootIm[0]), 1, 1e-9) && approx(Math.abs(_rootIm[2]), 2, 1e-9) &&
    approx(_rootRe[0], 0, 1e-9) && approx(_rootRe[2], 0, 1e-9)) { pass++; }
else { fail++; console.log("FAIL quartic complex biquad x^4+5x^2+4: " + _rootRe[0] + "+" + _rootIm[0] + "i " + _rootRe[2] + "+" + _rootIm[2] + "i"); }

solvePoly("x^4-10x^3+35x^2-50x+24=0", 4);
checkRoots("quartic general (1,2,3,4)", [1, 2, 3, 4], 1e-8);

solvePoly("x^4-3x^3+3x^2-3x+2=0", 4);
checkRoots("quartic mixed real+complex", [1, 2], 1e-8);

/* 首系数非 1：(2x-1)(x-3)(x+2)(x+1) = 2x^4-x^3-14x^2-5x+6 */
solvePoly("2x^4-x^3-14x^2-5x+6=0", 4);
checkRoots("quartic frac leading (0.5,3,-2,-1)", [0.5, 3, -2, -1], 1e-7);

/* ==================== 降次链：四次 a=0 → 三次委托 ==================== */
solversReset();
parseExpression("x^3-6x^2+11x-6=0");
extractCoefficients(3);
solveQuartic();
checkRoots("quartic degenerate → cubic", [1, 2, 3], 1e-9);

/* ==================== 模块四：线性方程组 ==================== */
solversReset();
matrixPool[0] = 2; matrixPool[1] = 3; matrixPool[2] = 8;
matrixPool[3] = 1; matrixPool[4] = -1; matrixPool[5] = -1;
solveLinearSystem(2);
if (_solveError === 0 && _rootCount === 2 && approx(_rootRe[0], 1, 1e-9) && approx(_rootRe[1], 2, 1e-9) &&
    _rootIm[0] === 0 && _rootIm[1] === 0) { pass++; }
else { fail++; console.log("FAIL lin2: err=" + _solveError + " x=" + _rootRe[0] + " y=" + _rootRe[1]); }

solversReset();
matrixPool[0] = 1; matrixPool[1] = 1; matrixPool[2] = 1; matrixPool[3] = 6;
matrixPool[4] = 1; matrixPool[5] = 2; matrixPool[6] = 3; matrixPool[7] = 14;
matrixPool[8] = 2; matrixPool[9] = 3; matrixPool[10] = -1; matrixPool[11] = 5;
solveLinearSystem(3);
if (_solveError === 0 && approx(_rootRe[0], 1, 1e-9) && approx(_rootRe[1], 2, 1e-9) && approx(_rootRe[2], 3, 1e-9)) { pass++; }
else { fail++; console.log("FAIL lin3: err=" + _solveError + " [" + _rootRe[0] + " " + _rootRe[1] + " " + _rootRe[2] + "]"); }

solversReset();
matrixPool[0] = 1; matrixPool[1] = 1; matrixPool[2] = 1;
matrixPool[3] = 1; matrixPool[4] = 1; matrixPool[5] = 2;
solveLinearSystem(2);
if (_solveError === 3 && _rootCount === 0) { pass++; }
else { fail++; console.log("FAIL lin singular: err=" + _solveError + " count=" + _rootCount); }

/* ==================== 模块五：残差回验 ==================== */
solvePoly("x^2-5x+6=0", 2);
verifyRoots();
if (_residualMax < 1e-6) { pass++; }
else { fail++; console.log("FAIL residual clean: " + _residualMax); }

/* 坏根注入：根偏移 0.001 → 相对残差应远超 1e-6 */
solvePoly("x^2-5x+6=0", 2);
_rootRe[0] = _rootRe[0] + 0.001;
verifyRoots();
if (_residualMax > 1e-6) { pass++; }
else { fail++; console.log("FAIL residual bad root: " + _residualMax); }

/* 复根跳过：x^2+1 无实根 → residual 0 */
solvePoly("x^2+1=0", 2);
verifyRoots();
if (_residualMax === 0) { pass++; }
else { fail++; console.log("FAIL residual complex skip: " + _residualMax); }

/* ==================== 模块六：门禁（128KB 阈值：深度≤6 函数≤4 变量≤10） ==================== */
solversReset();
parseExpression("(((((x)))))");                                  /* 5 层：通过 */
if (preCheckExpression() === 0) { pass++; }
else { fail++; console.log("FAIL preCheck depth5 should pass"); }

parseExpression("((((((x))))))");                                /* 6 层边界：通过 */
if (preCheckExpression() === 0) { pass++; }
else { fail++; console.log("FAIL preCheck depth6 should pass"); }

parseExpression("(((((((x)))))))");                              /* 7 层：拒绝 */
if (preCheckExpression() === 1 && _solveError === 2) { pass++; }
else { fail++; console.log("FAIL preCheck depth7: ret/_solveError"); }

parseExpression("sin(x)+cos(x)");                                /* 2 函数：通过 */
if (preCheckExpression() === 0) { pass++; }
else { fail++; console.log("FAIL preCheck 2 funcs should pass"); }

parseExpression("sin(x)+cos(x)+tan(x)+sin(x)");                  /* 4 函数边界：通过 */
if (preCheckExpression() === 0) { pass++; }
else { fail++; console.log("FAIL preCheck 4 funcs should pass"); }

parseExpression("sin(x)+cos(x)+tan(x)+sin(x)+cos(x)");           /* 5 函数：拒绝 */
if (preCheckExpression() === 1 && _solveError === 2) { pass++; }
else { fail++; console.log("FAIL preCheck 5 funcs"); }

parseExpression("x+x+x+x+x+x+x+x+x+x");                          /* 10 变量边界：通过 */
if (preCheckExpression() === 0) { pass++; }
else { fail++; console.log("FAIL preCheck 10 vars should pass"); }

parseExpression("x+x+x+x+x+x+x+x+x+x+x");                        /* 11 变量：拒绝 */
if (preCheckExpression() === 1 && _solveError === 2) { pass++; }
else { fail++; console.log("FAIL preCheck 11 vars"); }

if (guardDegree(5) === 1 && _solveError === 1 && guardDegree(4) === 0) { pass++; }
else { fail++; console.log("FAIL guardDegree"); }

if (guardMultivariateNonlinear() === 0) { pass++; }   /* 单变量解析器：恒通过 */
else { fail++; console.log("FAIL guardMultivariate"); }

if (checkMemoryWatermark() === 0) { pass++; }
else { fail++; console.log("FAIL watermark baseline: used>96KB?"); }

/* ==================== 模块七：复位 ==================== */
solvePoly("x^2-5x+6=0", 2);
solversReset();
if (_rootCount === 0 && _solveError === 0 && _residualMax === 0 &&
    _rootExactDen[0] === 1 && _rootExactRad[3] === 1 && _rootExactValid[2] === 0) { pass++; }
else { fail++; console.log("FAIL solversReset"); }

var solverPass = pass - evalPass, solverFail = fail - evalFail;
console.log("---- solvers: " + solverPass + " passed, " + solverFail + " failed ----");

if (pass !== 83 || fail !== 0) {
  die("BUILD FAILED: 回归测试期望 83/83 (evaluator 41 + solvers 42)，实际 pass=" + pass + " fail=" + fail);
}
console.log("[3/5] 静态声明扫描 " + EXPORTED_NAMES.length + "/" + EXPORTED_NAMES.length +
            " + 运行时存在性 " + EXPORTED_NAMES.length + "/" + EXPORTED_NAMES.length + " 通过");
console.log("[4/5] eval 拼接体回归: evaluator " + evalPass + "/41 + solvers " + solverPass + "/42 = " + pass + "/83 全部通过");

/* ---- 可选微基准（--bench）：parseExpression 性能对比 ----
 * 用途：核对 .length 自跟随池方案相对硬编码常量无显著解析开销；
 * 若此处耗时比常量方案慢超过 20%，再考虑改用文件顶部常量 */
if (process.argv.indexOf("--bench") >= 0) {
  var N = 10000;
  var src = "x^2-5x+6=0";
  var t0 = Date.now();
  for (var i = 0; i < N; i++) {
    parseExpression(src);
  }
  var t1 = Date.now();
  console.log("[bench] parseExpression x" + N + " = " + (t1 - t0) + "ms");
}

/* ---- [5/5] 动态 import() 生成的 core_kernel.js（ESM/严格模式）+ 导出接口全链路冒烟 ----
 * DevEco 工具链（babel/webpack 处理 harmony 模块）会以严格模式执行内核，
 * 此步提前暴露任何仅松弛模式下成立的写法，并验证 export 块 getter 接线。 */
var esmProblems = [];
function ek(name, cond) { if (!cond) { esmProblems.push(name); } }

import(url.pathToFileURL(OUT).href).then(function (mod) {
  var k = mod.default;
  ek("default 导出为对象", k && typeof k === "object");

  k.parseExpression("2+3*4");
  ek("evalExpressionAt 2+3*4=14", Math.abs(k.evalExpressionAt(0) - 14) < 1e-9);

  k.parseExpression("x^2-5x+6=0");
  k.extractCoefficients(2);
  k.solveQuadratic();
  ek("getRootCount=2", k.getRootCount() === 2);
  ek("getRootRe 3,2", Math.abs(k.getRootRe(0) - 3) < 1e-9 && Math.abs(k.getRootRe(1) - 2) < 1e-9);
  ek("getRootExact 整根 3/1", k.getRootExactValid(0) === 1 && k.getRootExactRat(0) === 3 &&
    k.getRootExactRadNum(0) === 0 && k.getRootExactDen(0) === 1 &&
    k.getRootExactRad(0) === 1 && k.getRootExactExp(0) === 0);
  ek("getSolveError=0", k.getSolveError() === 0);
  k.verifyRoots();
  ek("getResidualMax<1e-6", k.getResidualMax() < 1e-6);

  k.setAngleMode("DEG");
  ek("setAngleMode/getAngleMode", k.getAngleMode() === "DEG");
  k.setAngleMode("RAD");

  var M = [2, 3, 8, 1, -1, -1];
  for (var mi = 0; mi < 6; mi++) { k.setMatrixElement(mi, M[mi]); }
  k.solveLinearSystem(2);
  ek("setMatrixElement+solveLinearSystem", k.getRootCount() === 2 &&
    Math.abs(k.getRootRe(0) - 1) < 1e-9 && Math.abs(k.getRootRe(1) - 2) < 1e-9);

  k.exactReset();
  ek("exactReset+精确量 getter", k.getExactRat() === 0 && k.getExactRadNum() === 0 &&
    k.getExactDen() === 1 && k.getExactRad() === 1 && k.getExactExp() === 0);
  ek("exactIsValid getter", typeof k.exactIsValid() === "boolean");
  ek("getApproxValue fn", typeof k.getApproxValue === "function");

  k.parseExpression("x+x+x+x+x+x+x+x+x+x+x");
  ek("preCheckExpression 11 vars 拦截", k.preCheckExpression() === 1);
  k.solversReset();
  ek("solversReset 经接口生效", k.getRootCount() === 0 && k.getSolveError() === 0);
  ek("guardDegree", k.guardDegree(5) === 1 && k.guardDegree(4) === 0);
  ek("guardMultivariateNonlinear", k.guardMultivariateNonlinear() === 0);
  ek("checkMemoryWatermark", k.checkMemoryWatermark() === 0);
  ek("dumpTokenStream/applyFunction 接线", typeof k.dumpTokenStream === "function" && typeof k.applyFunction === "function");
  ek("evaluatorReset 接线", typeof k.evaluatorReset === "function");

  /* 复数域接口接线：setComplexA → cXxx → getComplexTmp* 调用序 */
  k.setComplexA(0, Math.PI);
  k.cExp();
  ek("cExp 欧拉恒等式", Math.abs(k.getComplexTmpRe() + 1) < 1e-12 && Math.abs(k.getComplexTmpIm()) < 1e-15);
  k.setComplexA(-1, 0);
  k.cLn();
  ek("cLn(-1)=iπ", Math.abs(k.getComplexTmpRe()) < 1e-12 && Math.abs(k.getComplexTmpIm() - Math.PI) < 1e-12);
  k.setComplexA(2, 0);
  k.cPowReal(3);
  ek("cPowReal 2^3=8", Math.abs(k.getComplexTmpRe() - 8) < 1e-12 && k.getComplexTmpIm() === 0);

  /* 数值微积分接口接线：calculusReset → parseExpression → calcXxx 调用序 */
  k.calculusReset();
  k.parseExpression("x^2");
  ek("calcDerivative x^2@3=6", Math.abs(k.calcDerivative(3) - 6) < 1e-6 && k.getCalcError() === 0);
  ek("getCalcIterations d1=2", k.getCalcIterations() === 2);
  ek("calcIntegral x^2[0,1]=1/3", Math.abs(k.calcIntegral(0, 1) - 1 / 3) < 1e-7);
  k.calculusReset();
  k.parseExpression("sin(x)/x");
  ek("calcLimit sin(x)/x→1", Math.abs(k.calcLimit(0, 0) - 1) < 1e-6);
  k.calculusReset();
  ek("calculusReset 经接口生效", k.getCalcError() === 0 && k.getCalcIterations() === 0);

  /* 泰勒展开接口接线：calculusReset → parseExpression → calcTaylor → taylorEval 调用序 */
  k.calculusReset();
  k.parseExpression("sin(x)");
  ek("calcTaylor sin@0 6 阶", k.calcTaylor(0, 6) === 0 && k.getTaylorOrder() === 6 &&
    Math.abs(k.getTaylorCoeff(1) - 1) < 1e-4 &&
    Math.abs(k.getTaylorCoeff(3) + 1 / 6) < 1e-4 &&
    Math.abs(k.getTaylorCoeff(5) - 1 / 120) < 1e-4);
  ek("taylorEval(0.1)≈sin(0.1)", Math.abs(k.taylorEval(0.1) - Math.sin(0.1)) < 1e-5);
  ek("calcTaylor 超阶拒绝", k.calcTaylor(0, 17) === -1 && k.getCalcError() === 3);
  k.calculusReset();

  /* 进制转换与 SI 前缀接口接线：decToXxx → baseToString；siNormalize → getter/siToString */
  ek("decToHex 255→FF", k.decToHex(255) === 1 && k.getBaseLen() === 2 && k.baseToString() === "FF");
  ek("decToBin 255→11111111", k.decToBin(255) === 1 && k.baseToString() === "11111111");
  ek("decToHex -42→-2A 负号", k.decToHex(-42) === 1 && k.baseToString() === "-2A");
  ek("siNormalize 12345→12.345k", k.siNormalize(12345) === 1 && Math.abs(k.getSiMantissa() - 12.345) < 1e-12 &&
    k.getSiPrefixExp() === 3 && k.siToString() === "12.345 k");
  ek("siNormalize 0 无效+空串", k.siNormalize(0) === 0 && k.getSiValid() === 0 && k.siToString() === "");
  ek("baseReset 经接口生效", (k.baseReset(), k.getSiValid() === 0 && k.getBaseLen() === 0));

  /* ---- v2.0.0：自包含三模块接线（multivar / matrix / special） ---- */
  k.multivar.parse("3x^2y + 2xy - 5", 0);
  ek("multivar parse+evalSlot 2,3→43", k.multivar.evalSlot(0, 2, 3) === 43);
  k.multivar.parse("x^2y + xy^2", 1);
  k.multivar.diff(1, 0, 2);
  ek("multivar diff d/dx = 2xy + y^2", k.multivar.polyToString(2) === "2xy + y^2");
  ek("multivar toString 隐式乘契约", k.multivar.parse("3x^2y", 3) === 1 &&
    k.multivar.polyToString(3) === "3x^2y");

  k.matrix.setA(3, 3, 0, 6); k.matrix.setA(3, 3, 1, 1); k.matrix.setA(3, 3, 2, 1);
  k.matrix.setA(3, 3, 3, 4); k.matrix.setA(3, 3, 4, -2); k.matrix.setA(3, 3, 5, 5);
  k.matrix.setA(3, 3, 6, 2); k.matrix.setA(3, 3, 7, 8); k.matrix.setA(3, 3, 8, 7);
  ek("matrix det 3x3 = -306", k.matrix.det() === -306);
  k.matrix.setA(2, 2, 0, 4); k.matrix.setA(2, 2, 1, 7);
  k.matrix.setA(2, 2, 2, 2); k.matrix.setA(2, 2, 3, 6);
  ek("matrix inv 2x2 R[0][0]=0.6", k.matrix.inv() === 1 &&
    Math.abs(k.matrix.getR(0, 0) - 0.6) < 1e-12);

  ek("special erf(1) ≈ 0.8427007929", Math.abs(k.special.erf(1) - 0.8427007929497149) < 2e-7);
  ek("special gamma(5) = 24", Math.abs(k.special.gamma(5) - 24) < 1e-9);
  ek("special lgamma(5) = ln 24", Math.abs(k.special.lgamma(5) - 3.1780538303479458) < 1e-12);

  if (esmProblems.length > 0) {
    die("BUILD FAILED: ESM(严格模式)导出接口冒烟失败 " + esmProblems.length + " 项:\n  - " + esmProblems.join("\n  - "));
  }
  console.log("[5/5] ESM 严格模式 import 冒烟: 导出接口链路 43/43 通过（含 getter/矩阵池写入/角度模式切换/复数域接线/微积分接线/泰勒接线/进制 SI 接线/多变量·矩阵·特殊函数三模块接线）");
  console.log("BUILD OK, all tests passed");
}, function (err) {
  die("BUILD FAILED: core_kernel.js 无法以 ES Module 加载（严格模式兼容性问题）:\n" + (err && err.stack ? err.stack : String(err)));
});
