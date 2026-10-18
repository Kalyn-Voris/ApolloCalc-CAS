/* ============================================================
 * build-kernel.js — ApolloCalc-CAS JS/64KB 内核拼接脚本（构建工具，非设备交付物）
 * 运行：node test/build-kernel.js   （JS/64KB 目录下，Node 环境）
 *
 * 功能：
 *   1. 按序读取 common/ 下 6 个 core 文件，用 \n 拼接
 *   2. 末尾追加 export 块（index.js 唯一访问点）
 *   3. 写出 ../src/core_kernel.js
 *   4. node --check 语法校验（ESM export 语法按模块解析校验）
 *   5. eval() 拼接体（不含 export 块——export 仅 ESM 合法，eval 中会语法错误），
 *      复用 _test_evaluator.js(41 项) + _test_solvers.js(39 项) 断言逻辑，共 80 项
 *   6. 静态扫描 export 块引用的 34 个标识符是否在文件 1-6 中声明过 + eval 运行时存在性复核
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
  "core_solvers.js"
];

/* export 块：内容与交付约定逐字一致，禁止在此改动（改了会与 index.js 契约漂移） */
var EXPORT_BLOCK = `/* ============================================================
 * ApolloCalc · core_kernel.js — 内核统一入口
 * 由 _build_kernel.js 自动拼接，禁止手动编辑
 * 拼接顺序：parser → math_functions → complex → exact → evaluator → solvers
 * ============================================================ */

/* 供 index.js 引用的内核接口（UI 层唯一访问点） */
export default {
  /* ---- 解析层 ---- */
  parseExpression: parseExpression,
  dumpTokenStream: dumpTokenStream,

  /* ---- 数学执行层 ---- */
  applyFunction: applyFunction,

  /* ---- 精确数值系统 ---- */
  exactReset: exactReset,
  exactIsValid: function () { return exactIsValid; },
  getExactNum: function () { return exactNum; },
  getExactDen: function () { return exactDen; },
  getExactRad: function () { return exactRad; },
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
  getRootExactNum: function (i) { return _rootExactNum[i]; },
  getRootExactDen: function (i) { return _rootExactDen[i]; },
  getRootExactRad: function (i) { return _rootExactRad[i]; },
  getRootExactValid: function (i) { return _rootExactValid[i]; },
  getSolveError: function () { return _solveError; },
  getResidualMax: function () { return _residualMax; },

  /* ---- 全局状态切换 ---- */
  setAngleMode: function (mode) { angleMode = mode; },
  getAngleMode: function () { return angleMode; },

  /* ---- 矩阵池写入（UI 层给线性方程组输入系数用） ---- */
  setMatrixElement: function (i, v) { matrixPool[i] = v; }
};`;

/* export 块引用的全部内核标识符（34 个；函数形参 i/v/mode 除外） */
var EXPORTED_NAMES = [
  "parseExpression", "dumpTokenStream",
  "applyFunction",
  "exactReset", "exactIsValid", "exactNum", "exactDen", "exactRad", "approxValue",
  "evalExpressionAt", "evalEquationAt", "extractCoefficients", "evaluatorReset",
  "solveQuadratic", "solveCubic", "solveQuartic", "solveLinearSystem", "verifyRoots", "solversReset",
  "preCheckExpression", "guardDegree", "guardMultivariateNonlinear", "checkMemoryWatermark",
  "_rootCount", "_rootRe", "_rootIm", "_rootExactNum", "_rootExactDen", "_rootExactRad",
  "_rootExactValid", "_solveError", "_residualMax",
  "angleMode", "matrixPool"
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
console.log("[1/5] 已拼接 6 个源文件 -> " + OUT + " (" + Buffer.byteLength(kernelFull, "utf8") + " bytes)");

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

/* ========== 以下断言逻辑复用自 _test_solvers.js（39 项） ========== */
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
    approx(_rootExactNum[0] / _rootExactDen[0], 3, 1e-12) &&
    approx(_rootExactNum[1] / _rootExactDen[1], 2, 1e-12) &&
    _rootExactRad[0] === 1 && _rootExactRad[1] === 1) { pass++; }
else { fail++; console.log("FAIL quad exact integer roots: v=" + _rootExactValid[0] + _rootExactValid[1] +
  " n=" + _rootExactNum[0] + "/" + _rootExactDen[0] + " r=" + _rootExactRad[0]); }

solvePoly("x^2-x-1=0", 2);
checkRoots("quad golden", [(1 + Math.sqrt(5)) / 2, (1 - Math.sqrt(5)) / 2], 1e-9);
if (_rootExactValid[0] === 0 && _rootExactValid[1] === 0) { pass++; }  /* 混合形式置无效 */
else { fail++; console.log("FAIL golden exact should be invalid"); }

solvePoly("x^2-2=0", 2);
checkRoots("quad sqrt2", [Math.SQRT2, -Math.SQRT2], 1e-9);
if (_rootExactValid[0] === 1 && _rootExactNum[0] === 1 && _rootExactDen[0] === 1 && _rootExactRad[0] === 2 &&
    _rootExactValid[1] === 1 && _rootExactNum[1] === -1) { pass++; }
else { fail++; console.log("FAIL sqrt2 exact: n=" + _rootExactNum[0] + "/" + _rootExactDen[0] +
  " rad=" + _rootExactRad[0] + " v=" + _rootExactValid[0] + _rootExactValid[1] +
  " n1=" + _rootExactNum[1]); }

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
checkRoots("cubic (x-1)(x-2)(x-3) trig", [1, 2, 3], 1e-9);
if (_rootExactValid[0] === 0 && _rootExactValid[1] === 0 && _rootExactValid[2] === 0) { pass++; }
else { fail++; console.log("FAIL cubic exact should be all invalid"); }

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
if (_rootExactValid[0] === 0 && _rootExactValid[3] === 0) { pass++; }
else { fail++; console.log("FAIL quartic exact should be invalid"); }

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

/* ==================== 模块六：门禁 ==================== */
solversReset();
parseExpression("((((x))))");
if (preCheckExpression() === 1 && _solveError === 2) { pass++; }
else { fail++; console.log("FAIL preCheck depth4: ret/_solveError"); }

parseExpression("sin(x)+cos(x)");
if (preCheckExpression() === 0) { pass++; }
else { fail++; console.log("FAIL preCheck 2 funcs should pass"); }

parseExpression("sin(x)+cos(x)+tan(x)");
if (preCheckExpression() === 1 && _solveError === 2) { pass++; }
else { fail++; console.log("FAIL preCheck 3 funcs"); }

parseExpression("x+x+x+x+x+x");
if (preCheckExpression() === 1 && _solveError === 2) { pass++; }
else { fail++; console.log("FAIL preCheck 6 vars"); }

parseExpression("x+x+x+x+x");
if (preCheckExpression() === 0) { pass++; }
else { fail++; console.log("FAIL preCheck 5 vars should pass"); }

if (guardDegree(5) === 1 && _solveError === 1 && guardDegree(4) === 0) { pass++; }
else { fail++; console.log("FAIL guardDegree"); }

if (guardMultivariateNonlinear() === 0) { pass++; }   /* 单变量解析器：恒通过 */
else { fail++; console.log("FAIL guardMultivariate"); }

if (checkMemoryWatermark() === 0) { pass++; }
else { fail++; console.log("FAIL watermark baseline: used>48KB?"); }

/* ==================== 模块七：复位 ==================== */
solvePoly("x^2-5x+6=0", 2);
solversReset();
if (_rootCount === 0 && _solveError === 0 && _residualMax === 0 &&
    _rootExactDen[0] === 1 && _rootExactRad[3] === 1 && _rootExactValid[2] === 0) { pass++; }
else { fail++; console.log("FAIL solversReset"); }

var solverPass = pass - evalPass, solverFail = fail - evalFail;
console.log("---- solvers: " + solverPass + " passed, " + solverFail + " failed ----");

if (pass !== 80 || fail !== 0) {
  die("BUILD FAILED: 回归测试期望 80/80 (evaluator 41 + solvers 39)，实际 pass=" + pass + " fail=" + fail);
}
console.log("[3/5] 静态声明扫描 34/34 + 运行时存在性 34/34 通过");
console.log("[4/5] eval 拼接体回归: evaluator " + evalPass + "/41 + solvers " + solverPass + "/39 = " + pass + "/80 全部通过");

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
  ek("getRootExact 整根 3/1", k.getRootExactValid(0) === 1 && k.getRootExactNum(0) === 3 &&
    k.getRootExactDen(0) === 1 && k.getRootExactRad(0) === 1);
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
  ek("exactReset+精确量 getter", k.getExactNum() === 0 && k.getExactDen() === 1 && k.getExactRad() === 1);
  ek("exactIsValid getter", typeof k.exactIsValid() === "boolean");
  ek("getApproxValue fn", typeof k.getApproxValue === "function");

  k.parseExpression("x+x+x+x+x+x");
  ek("preCheckExpression 6 vars 拦截", k.preCheckExpression() === 1);
  k.solversReset();
  ek("solversReset 经接口生效", k.getRootCount() === 0 && k.getSolveError() === 0);
  ek("guardDegree", k.guardDegree(5) === 1 && k.guardDegree(4) === 0);
  ek("guardMultivariateNonlinear", k.guardMultivariateNonlinear() === 0);
  ek("checkMemoryWatermark", k.checkMemoryWatermark() === 0);
  ek("dumpTokenStream/applyFunction 接线", typeof k.dumpTokenStream === "function" && typeof k.applyFunction === "function");
  ek("evaluatorReset 接线", typeof k.evaluatorReset === "function");

  if (esmProblems.length > 0) {
    die("BUILD FAILED: ESM(严格模式)导出接口冒烟失败 " + esmProblems.length + " 项:\n  - " + esmProblems.join("\n  - "));
  }
  console.log("[5/5] ESM 严格模式 import 冒烟: 导出接口链路 19/19 通过（含 getter/矩阵池写入/角度模式切换）");
  console.log("BUILD OK, all tests passed");
}, function (err) {
  die("BUILD FAILED: core_kernel.js 无法以 ES Module 加载（严格模式兼容性问题）:\n" + (err && err.stack ? err.stack : String(err)));
});
