/* ApolloCalc-CAS · JS/64KB 求值层冒烟测试：Node 下从 ../src 按序拼接文件 1-5 后执行 */
var fs = require("fs");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js", "core_evaluator.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

var pass = 0, fail = 0;
function approx(a, b, tol) { return Math.abs(a - b) <= (tol || 1e-9); }
function check(name, got, want, tol) {
  if (approx(got, want, tol)) { pass++; }
  else { fail++; console.log("FAIL " + name + " got=" + got + " want=" + want + " err=" + _evalError); }
}

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

/* 非多项式 → 三点采样可能准确拟合，须用残差更大的用例验证清零路径：
 * sin(x) 在 ±1.5 附近与二次曲线可拟合，改用 x^5（对二次欠拟合不奇异但系数错）不可行；
 * 这里验证"采样点求值失败"路径：sqrt(x) 在负采样点域错误 → 系数清零 */
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

console.log("---- " + pass + " passed, " + fail + " failed ----");
