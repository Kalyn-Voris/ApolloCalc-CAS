/* ApolloCalc · test-calculus.js — 数值微积分冒烟测试：导数 / 积分 / 极限
   Node 下从 ../src 按序拼接文件 1-7（与 build-kernel.js 顺序一致）后执行 */
var fs = require("fs");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_calculus.js", "core_solvers.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

var pass = 0, fail = 0;
function approx(a, b, tol) { return Math.abs(a - b) <= (tol || 1e-9); }

function calcOf(src) {
  calculusReset();
  parseExpression(src);
}

function expectCalc(name, got, want, tol, errWant) {
  if (approx(got, want, tol) && _calcError === errWant) { pass++; }
  else { fail++; console.log("FAIL " + name + " got=" + got + " want=" + want + " err=" + _calcError + " (want err=" + errWant + ")"); }
}

function expectCalcErr(name, got, errWant) {
  if (got !== got && _calcError === errWant) { pass++; }
  else { fail++; console.log("FAIL " + name + " got=" + got + " err=" + _calcError + " (want NaN, err=" + errWant + ")"); }
}

angleMode = "RAD";

/* 模块一：一阶导数（中心差分） */
calcOf("x^2");
expectCalc("d1 x^2@3", calcDerivative(3), 6, 1e-6, 0);

calcOf("x^3");
expectCalc("d1 x^3@2", calcDerivative(2), 12, 1e-6, 0);

calcOf("sin(x)");
expectCalc("d1 sin@0", calcDerivative(0), 1, 1e-6, 0);

calcOf("exp(x)");
expectCalc("d1 exp@1", calcDerivative(1), Math.E, 1e-6, 0);

calcOf("x^2");
expectCalc("d1 x^2@0 边界", calcDerivative(0), 0, 1e-6, 0);

/* 模块二：二阶导数（五点公式） */
calcOf("x^3");
expectCalc("d2 x^3@2", calcDerivative2(2), 12, 1e-5, 0);

calcOf("sin(x)");
expectCalc("d2 sin@0", calcDerivative2(0), 0, 1e-5, 0);

calcOf("x^4");
expectCalc("d2 x^4@1", calcDerivative2(1), 12, 1e-5, 0);

/* 模块三：定积分（自适应复合辛普森） */
calcOf("x");
expectCalc("int x [0,1]", calcIntegral(0, 1), 0.5, 1e-7, 0);

calcOf("x^2");
expectCalc("int x^2 [0,1]", calcIntegral(0, 1), 1 / 3, 1e-7, 0);

calcOf("x^2");
expectCalc("int x^2 [-1,1]", calcIntegral(-1, 1), 2 / 3, 1e-7, 0);

calcOf("sin(x)");
expectCalc("int sin [0,pi]", calcIntegral(0, Math.PI), 2, 1e-7, 0);

calcOf("exp(x)");
expectCalc("int exp [0,1]", calcIntegral(0, 1), Math.E - 1, 1e-7, 0);

calcOf("1");
expectCalc("int 1 [0,5]", calcIntegral(0, 5), 5, 1e-7, 0);

calcOf("x^2");
expectCalc("int a===b [3,3]", calcIntegral(3, 3), 0, 1e-12, 0);

/* 模块四：极限（左右逼近） */
calcOf("sin(x)/x");
expectCalc("lim sin(x)/x @0 双侧", calcLimit(0, 0), 1, 1e-6, 0);

calcOf("sin(x)/x");
expectCalc("lim sin(x)/x @0 右", calcLimit(0, 1), 1, 1e-6, 0);

calcOf("(x^2-1)/(x-1)");
expectCalc("lim (x^2-1)/(x-1) @1 双侧", calcLimit(1, 0), 2, 1e-6, 0);

/* 模块五：错误路径 */
/* 方程含等号：微积分只对函数表达式有定义 */
calcOf("x^2=0");
expectCalcErr("方程拦截 d1", calcDerivative(1), 1);

/* sqrt(x) 在负采样点域错 */
calcOf("sqrt(x)");
expectCalcErr("int 域错 [-1,1] sqrt", calcIntegral(-1, 1), 1);

/* 1/x 在 x→0 两侧无有限极限 */
calcOf("1/x");
expectCalcErr("lim 发散 1/x @0", calcLimit(0, 0), 2);

calcOf("x^2");
expectCalcErr("d1 非有限 x=∞", calcDerivative(Infinity), 1);

calcOf("x^2");
expectCalcErr("int 非有限 b=NaN", calcIntegral(0, NaN), 1);

/* 模块六：RPN 缓存复用与诊断量
   同一表达式连续多次微积分操作（不复位）只做一次 toRPN，结果不受影响 */
calcOf("x^2");
expectCalc("缓存复用 d1 x^2@3", calcDerivative(3), 6, 1e-6, 0);
if (_calcRpnReady === 1 && _calcIterations === 2 && _calcError === 0) { pass++; }
else { fail++; console.log("FAIL 缓存诊断 _calcRpnReady=" + _calcRpnReady + " iter=" + _calcIterations + " err=" + _calcError); }

expectCalc("缓存复用 int 同表达式", calcIntegral(0, 1), 1 / 3, 1e-7, 0);
if (_calcIterations > 100) { pass++; }   /* 积分求值数应达百级（n=100..200 收敛） */
else { fail++; console.log("FAIL 积分迭代数=" + _calcIterations); }

/* 模块七：复位 */
calcOf("x^2");
calcDerivative(3);
calculusReset();
if (_calcResult === 0 && _calcError === 0 && _calcIterations === 0 && _calcRpnReady === 0) { pass++; }
else { fail++; console.log("FAIL calculusReset 复位 result=" + _calcResult + " err=" + _calcError +
  " iter=" + _calcIterations + " rpn=" + _calcRpnReady); }

console.log("==== calculus smoke: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exitCode = 1; }
