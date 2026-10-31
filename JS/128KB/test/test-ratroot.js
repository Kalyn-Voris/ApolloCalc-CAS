/* ApolloCalc · test-ratroot.js — 有理根检验 / 多项式因式分解测试

   Node 下从 ../src 按序拼接文件 1-5,7,8,9（与 build-kernel.js 顺序一致）后执行
   覆盖：整数根精确化（三次/四次）、分数根、重根、零根剥离、部分有理降次委托、
   无有理根回落（Cardano/三角/笛卡尔）、非整系数回落

   运行：node test/test-ratroot.js   （JS/128KB 目录下）
   全过输出：==== ratroot smoke: N pass, 0 fail ==== */
var fs = require("fs");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_base.js", "core_solvers.js", "core_ratroot.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

var pass = 0, fail = 0;
function approx(a, b, tol) { return Math.abs(a - b) <= (tol || 1e-9); }
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}

/* 求解管线：parse → extract → solveXxx（与 UI 真实顺序一致） */
function solvePoly(src, deg) {
  solversReset();
  parseExpression(src);
  extractCoefficients(deg);
  if (deg === 2) { solveQuadratic(); }
  else if (deg === 3) { solveCubic(); }
  else if (deg === 4) { solveQuartic(); }
}

/* 期望 (re,im) 根多重集合是否全部命中（乱序、一对一消耗） */
function rootsMatch(wantPairs, tol) {
  if (_rootCount !== wantPairs.length) { return false; }
  var used = [0, 0, 0, 0];
  var i, w, hit;
  for (i = 0; i < _rootCount; i++) { used[i] = 0; }
  for (w = 0; w < wantPairs.length; w++) {
    hit = -1;
    for (i = 0; i < _rootCount; i++) {
      if (used[i] === 0 &&
          Math.abs(_rootRe[i] - wantPairs[w][0]) <= tol &&
          Math.abs(_rootIm[i] - wantPairs[w][1]) <= tol) { hit = i; break; }
    }
    if (hit < 0) { return false; }
    used[hit] = 1;
  }
  return true;
}

/* 值为 val 的实根是否带纯分数精确形式 p/den（无序查找） */
function exactHit(val, p, den, tol) {
  var i;
  for (i = 0; i < _rootCount; i++) {
    if (approx(_rootRe[i], val, tol) && _rootIm[i] === 0 && _rootExactValid[i] === 1 &&
        _rootExactRat[i] === p && _rootExactDen[i] === den &&
        _rootExactRadNum[i] === 0 && _rootExactRad[i] === 1 && _rootExactExp[i] === 0) {
      return 1;
    }
  }
  return 0;
}

/* 值为 val 的实根是否带纯根式精确形式 radNum·√rad/den（无序查找） */
function exactHitRad(val, radNum, rad, den, tol) {
  var i;
  for (i = 0; i < _rootCount; i++) {
    if (approx(_rootRe[i], val, tol) && _rootIm[i] === 0 && _rootExactValid[i] === 1 &&
        _rootExactRat[i] === 0 && _rootExactRadNum[i] === radNum &&
        _rootExactRad[i] === rad && _rootExactDen[i] === den && _rootExactExp[i] === 0) {
      return 1;
    }
  }
  return 0;
}

angleMode = "RAD";

/* 整数根精确化（验证目标 1/3） */
solvePoly("x^3-6x^2+11x-6=0", 3);
ok("cubic 1,2,3 数值根", _solveError === 0 && rootsMatch([[1, 0], [2, 0], [3, 0]], 1e-9),
   "err=" + _solveError + " count=" + _rootCount);
ok("cubic 根 1 精确 1/1", exactHit(1, 1, 1, 1e-9) === 1);
ok("cubic 根 2 精确 2/1", exactHit(2, 2, 1, 1e-9) === 1);
ok("cubic 根 3 精确 3/1", exactHit(3, 3, 1, 1e-9) === 1);

solvePoly("x^4-5x^2+4=0", 4);
ok("quartic ±1,±2 数值根", _solveError === 0 && rootsMatch([[1, 0], [-1, 0], [2, 0], [-2, 0]], 1e-9),
   "err=" + _solveError + " count=" + _rootCount);
ok("quartic 根 1 精确", exactHit(1, 1, 1, 1e-9) === 1);
ok("quartic 根 -1 精确", exactHit(-1, -1, 1, 1e-9) === 1);
ok("quartic 根 2 精确", exactHit(2, 2, 1, 1e-9) === 1);
ok("quartic 根 -2 精确", exactHit(-2, -2, 1, 1e-9) === 1);

/* 验证目标 2：分数候选 + 降次委托 */
solvePoly("2x^3-x-1=0", 3);
ok("2x^3-x-1 数值根", _solveError === 0 &&
   rootsMatch([[1, 0], [-0.5, 0.5], [-0.5, -0.5]], 1e-9),
   "err=" + _solveError + " count=" + _rootCount);
ok("2x^3-x-1 有理根 1 精确", exactHit(1, 1, 1, 1e-9) === 1);
var complexInvalid = 1;
var i;
for (i = 0; i < _rootCount; i++) {
  if (_rootIm[i] !== 0 && _rootExactValid[i] !== 0) { complexInvalid = 0; }
}
ok("2x^3-x-1 复根精确置无效", complexInvalid === 1);

/* 分数根（q|an）与重根、零根剥离 */
solvePoly("2x^4-x^3-14x^2-5x+6=0", 4);
ok("首系数 2 四次数值根", _solveError === 0 &&
   rootsMatch([[0.5, 0], [3, 0], [-2, 0], [-1, 0]], 1e-7),
   "err=" + _solveError + " count=" + _rootCount);
ok("分数根 1/2 精确 1/2", exactHit(0.5, 1, 2, 1e-9) === 1);
ok("整数根 3/-2/-1 精确", exactHit(3, 3, 1, 1e-9) === 1 && exactHit(-2, -2, 1, 1e-9) === 1 &&
   exactHit(-1, -1, 1, 1e-9) === 1);

solvePoly("x^4-4x^3+6x^2-4x+1=0", 4);
ok("(x-1)^4 四重根", _solveError === 0 && rootsMatch([[1, 0], [1, 0], [1, 0], [1, 0]], 1e-9));
ok("四重根全精确", exactHit(1, 1, 1, 1e-9) === 1);

solvePoly("x^3-3x=0", 3);
ok("x^3-3x 零根剥离+降次", _solveError === 0 &&
   rootsMatch([[Math.sqrt(3), 0], [0, 0], [-Math.sqrt(3), 0]], 1e-9),
   "err=" + _solveError);
ok("零根精确 0/1", exactHit(0, 0, 1, 1e-12) === 1);
ok("√3 纯根式精确", exactHitRad(Math.sqrt(3), 1, 3, 1, 1e-9) === 1 &&
   exactHitRad(-Math.sqrt(3), -1, 3, 1, 1e-9) === 1);

/* 无有理根回落：Cardano / 三角 / 笛卡尔 */
solvePoly("x^3-2x-5=0", 3);
ok("x^3-2x-5 Cardano 回落", _solveError === 0 && _rootCount === 3, "err=" + _solveError);
/* 复根由实根自推（韦达：和=0、|z|²=5/r0），自校验期望值 */
var r0 = _rootRe[0];
var cre = _rootRe[1];
var cim = _rootIm[1];
ok("x^3-2x-5 实根", approx(r0, 2.0945514815423265, 1e-9), "r0=" + r0);
ok("x^3-2x-5 复对共轭", approx(cre, -r0 / 2, 1e-9) && approx(cim * cim + cre * cre, 5 / r0, 1e-6) &&
   approx(_rootRe[2], cre, 1e-9) && approx(_rootIm[2], -cim, 1e-9) && cim !== 0,
   "re=" + cre + " im=" + cim);
ok("x^3-2x-5 实根无精确形式", _rootExactValid[0] === 0);

solvePoly("x^3-3x-1=0", 3);
ok("x^3-3x-1 三角法回落", _solveError === 0 &&
   rootsMatch([[1.8793852416, 0], [-1.5320888862, 0], [-0.3472963553, 0]], 1e-7),
   "err=" + _solveError + " r0=" + _rootRe[0]);
var triInvalid = 1;
for (i = 0; i < _rootCount; i++) {
  if (_rootExactValid[i] !== 0) { triInvalid = 0; }
}
ok("x^3-3x-1 全部无精确形式", triInvalid === 1);

solvePoly("x^4+x+1=0", 4);
ok("x^4+x+1 笛卡尔回落", _solveError === 0 && _rootCount === 4,
   "err=" + _solveError + " count=" + _rootCount);
verifyRoots();
ok("x^4+x+1 残差回验", _residualMax < 1e-6, "residual=" + _residualMax);

solvePoly("x^4+5x^2+4=0", 4);
ok("x^4+5x^2+4 双二次回落", _solveError === 0 &&
   rootsMatch([[0, 1], [0, -1], [0, 2], [0, -2]], 1e-9));

/* 非整系数回落（采样系数不在整数容差内） */
solvePoly("0.5x^3-x-0.5=0", 3);
ok("非整系数 Cardano 回落", _solveError === 0 &&
   rootsMatch([[-1, 0], [1.618033988749895, 0], [-0.6180339887498949, 0]], 1e-7),
   "err=" + _solveError + " count=" + _rootCount);

/* 残差回验（有理根路径整链） */
solvePoly("x^3-6x^2+11x-6=0", 3);
verifyRoots();
ok("三次有理根残差", _residualMax < 1e-6, "residual=" + _residualMax);

solvePoly("x^4-10x^3+35x^2-50x+24=0", 4);
verifyRoots();
ok("四次全有理残差", _residualMax < 1e-6, "residual=" + _residualMax);

/* 复位覆盖（12 槽根池无残留） */
solvePoly("x^4-5x^2+4=0", 4);
solveLinearSystem(12);
solversReset();
var resetClean = _rootCount === 0 && _solveError === 0;
for (i = 0; i < 12; i++) {
  if (_rootRe[i] !== 0 || _rootIm[i] !== 0 || _rootExactValid[i] !== 0 ||
      _rootExactRad[i] !== 1 || _rootExactDen[i] !== 1) { resetClean = false; }
}
ok("12 槽复位干净", resetClean);

console.log("==== ratroot smoke: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
