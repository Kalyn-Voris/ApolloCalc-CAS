/* ApolloCalc · test-taylor.js — 泰勒展开测试

   Node 下从 ../src 按序拼接文件 1-6（与 build-kernel.js 前段一致）后执行
   覆盖：sin@0 六阶系数（验证目标 4）、Horner 求值、exp/ln(1+x)/cos 中心偏移、
         阶数超限/非整数阶拒绝、方程输入拒绝、0 阶、calculusReset 失效
   运行：node test/test-taylor.js   （JS/128KB 目录下）
   全过输出：==== taylor smoke: N pass, 0 fail ==== */
var fs = require("fs");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_calculus.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

var pass = 0, fail = 0;
function approx(a, b, tol) { return Math.abs(a - b) <= (tol || 1e-9); }
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}

function taylorPrepare(src, x0, order) {
  calculusReset();
  parseExpression(src);
  return calcTaylor(x0, order);
}

function coeffsOK(want, n, tol) {
  var k;
  for (k = 0; k < n; k++) {
    if (!approx(_taylorCoeffs[k], want[k], tol)) { return false; }
  }
  return true;
}

angleMode = "RAD";

/* sin @ 0 展开 6 阶（验证目标 4） */
ok("sin@0 6阶展开", taylorPrepare("sin(x)", 0, 6) === 0, "ret/_calcError=" + _calcError);
ok("sin@0 系数 [0,1,0,-1/6,0,1/120,0]",
   coeffsOK([0, 1, 0, -1 / 6, 0, 1 / 120, 0], 7, 1e-4),
   "c1=" + _taylorCoeffs[1] + " c3=" + _taylorCoeffs[3] + " c5=" + _taylorCoeffs[5] + " c6=" + _taylorCoeffs[6]);
ok("_taylorCenter/_taylorOrder", _taylorCenter === 0 && _taylorOrder === 6);
ok("taylorEval(0.1)≈sin(0.1)", approx(taylorEval(0.1), Math.sin(0.1), 1e-5),
   "got=" + taylorEval(0.1) + " want=" + Math.sin(0.1));
ok("taylorEval(0.5)≈sin(0.5)", approx(taylorEval(0.5), Math.sin(0.5), 1e-4));
ok("taylorEval(0)=c0=0", taylorEval(0) === 0);
ok("迭代计数非零", _calcIterations >= 19, "iter=" + _calcIterations);

/* x² @ 0 展开 2 阶（低次精确路径） */
ok("x^2@0 2阶展开", taylorPrepare("x^2", 0, 2) === 0);
ok("x^2@0 系数 [0,0,1]", coeffsOK([0, 0, 1], 3, 1e-9));
ok("taylorEval(3)=9", approx(taylorEval(3), 9, 1e-9));

/* e^x @ 0 展开 4 阶 */
ok("e^x@0 4阶展开", taylorPrepare("e^x", 0, 4) === 0, "_calcError=" + _calcError);
ok("e^x@0 系数 [1,1,1/2,1/6,1/24]", coeffsOK([1, 1, 0.5, 1 / 6, 1 / 24], 5, 1e-4),
   "c3=" + _taylorCoeffs[3] + " c4=" + _taylorCoeffs[4]);
ok("taylorEval(0.5)≈e^0.5", approx(taylorEval(0.5), Math.exp(0.5), 1e-3));

/* ln(1+x) @ 0 展开 4 阶（交错级数系数） */
ok("ln(1+x)@0 4阶展开", taylorPrepare("ln(1+x)", 0, 4) === 0, "_calcError=" + _calcError);
ok("ln(1+x)@0 系数 [0,1,-1/2,1/3,-1/4]", coeffsOK([0, 1, -0.5, 1 / 3, -0.25], 5, 1e-4),
   "c2=" + _taylorCoeffs[2] + " c3=" + _taylorCoeffs[3] + " c4=" + _taylorCoeffs[4]);

/* 中心偏移：cos @ π/3 展开 4 阶 */
var PI3 = Math.PI / 3;
ok("cos@π/3 4阶展开", taylorPrepare("cos(x)", PI3, 4) === 0);
ok("cos@π/3 系数 [1/2,-√3/2,-1/4,√3/12,1/48]",
   coeffsOK([0.5, -Math.sqrt(3) / 2, -0.25, Math.sqrt(3) / 12, 1 / 48], 5, 1e-4),
   "c0=" + _taylorCoeffs[0] + " c1=" + _taylorCoeffs[1] + " c3=" + _taylorCoeffs[3]);
ok("taylorEval 中心偏移求值", approx(taylorEval(PI3 + 0.1), Math.cos(PI3 + 0.1), 1e-5));

/* 边界与拒绝 */
ok("0 阶展开", taylorPrepare("sin(x)", 0, 0) === 0 && _taylorOrder === 0 && _taylorCoeffs[0] === 0);
ok("taylorEval 0 阶恒 c0", taylorEval(100) === 0);
ok("17 阶拒绝", taylorPrepare("sin(x)", 0, 17) === -1 && _calcError === 3);
ok("非整数阶拒绝", taylorPrepare("sin(x)", 0, 2.5) === -1 && _calcError === 3);
ok("负阶拒绝", taylorPrepare("sin(x)", 0, -1) === -1 && _calcError === 3);
calculusReset();
parseExpression("x=1");
ok("方程输入拒绝", calcTaylor(0, 2) === -1 && _calcError === 1);
ok("非中心点展开", taylorPrepare("sin(x)", 1.0, 3) === 0 &&
   approx(_taylorCoeffs[0], Math.sin(1), 1e-9) && approx(_taylorCoeffs[1], Math.cos(1), 1e-7));

/* calculusReset 使泰勒缓存失效 */
taylorPrepare("sin(x)", 0, 6);
calculusReset();
ok("复位清 _taylorOrder/_taylorCoeffs[0]", _taylorOrder === 0 && _taylorCoeffs[0] === 0 && taylorEval(5) === 0);

console.log("==== taylor smoke: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
