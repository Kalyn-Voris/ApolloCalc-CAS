/* ApolloCalc-CAS · JS/64KB 求解层冒烟测试：Node 下从 ../src 按序拼接文件 1-6 后执行 */
var fs = require("fs");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_solvers.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

var pass = 0, fail = 0;
function approx(a, b, tol) { return Math.abs(a - b) <= (tol || 1e-9); }

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

/* ==================== 降次链：四次 a=0 → 三次委托 ====================
 * extractCoefficients(3) 后 _coeffs[4] 恒为 0，solveQuartic 应降次到三次 */
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

matrixPool[0] = 1; matrixPool[1] = 1; matrixPool[2] = 1; matrixPool[3] = 6;
matrixPool[4] = 1; matrixPool[5] = 2; matrixPool[6] = 3; matrixPool[7] = 14;
matrixPool[8] = 2; matrixPool[9] = 3; matrixPool[10] = -1; matrixPool[11] = 5;
solveLinearSystem(3);
if (_solveError === 0 && approx(_rootRe[0], 1, 1e-9) && approx(_rootRe[1], 2, 1e-9) && approx(_rootRe[2], 3, 1e-9)) { pass++; }
else { fail++; console.log("FAIL lin3: err=" + _solveError + " [" + _rootRe[0] + " " + _rootRe[1] + " " + _rootRe[2] + "]"); }

/* 6 元线性方程组：对角矩阵，解 x_i = i（根池 6 槽上限，回归 4 槽越界 bug） */
solversReset();
matrixPool[0] = 1; matrixPool[1] = 0; matrixPool[2] = 0; matrixPool[3] = 0; matrixPool[4] = 0; matrixPool[5] = 0; matrixPool[6] = 1;
matrixPool[7] = 0; matrixPool[8] = 1; matrixPool[9] = 0; matrixPool[10] = 0; matrixPool[11] = 0; matrixPool[12] = 0; matrixPool[13] = 2;
matrixPool[14] = 0; matrixPool[15] = 0; matrixPool[16] = 1; matrixPool[17] = 0; matrixPool[18] = 0; matrixPool[19] = 0; matrixPool[20] = 3;
matrixPool[21] = 0; matrixPool[22] = 0; matrixPool[23] = 0; matrixPool[24] = 1; matrixPool[25] = 0; matrixPool[26] = 0; matrixPool[27] = 4;
matrixPool[28] = 0; matrixPool[29] = 0; matrixPool[30] = 0; matrixPool[31] = 0; matrixPool[32] = 1; matrixPool[33] = 0; matrixPool[34] = 5;
matrixPool[35] = 0; matrixPool[36] = 0; matrixPool[37] = 0; matrixPool[38] = 0; matrixPool[39] = 0; matrixPool[40] = 1; matrixPool[41] = 6;
solveLinearSystem(6);
if (_solveError === 0 && _rootCount === 6 &&
    approx(_rootRe[0], 1, 1e-9) && approx(_rootRe[1], 2, 1e-9) &&
    approx(_rootRe[2], 3, 1e-9) && approx(_rootRe[3], 4, 1e-9) &&
    approx(_rootRe[4], 5, 1e-9) && approx(_rootRe[5], 6, 1e-9)) { pass++; }
else { fail++; console.log("FAIL lin6: err=" + _solveError + " count=" + _rootCount +
  " roots=[" + _rootRe[0] + "," + _rootRe[1] + "," + _rootRe[2] + "," +
  _rootRe[3] + "," + _rootRe[4] + "," + _rootRe[5] + "]"); }

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

console.log("==== solvers smoke: " + pass + " pass, " + fail + " fail ====");
