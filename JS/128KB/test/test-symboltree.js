/* ApolloCalc · test-symboltree.js — 符号微分引擎测试(v2.4.0 功能 6,JS 侧)

   被测模块:src/core_symboltree.js(依赖内核拼接作用域中的 parseExpression/
   toRPN/preCheckExpression/ratRootDescent/exactSetFraction/_coeffs,故按
   拼接序载入 parser→evaluator→solvers→ratroot→symboltree)。

   对齐锚:C/128KB/src/ac_symbolic.c(求导规则/dmake 化简/append-only)、
   C/128KB-ProTEXT/src/ac_pro_symbolic_ext.c(A6 隐函数,错误码 0..4 与
   test_pro_runner.c A6-1..A6-9 用例逐条镜像)、C ac_pro_integrate.c 的
   pro_int_emit(渲染括号化规则同款)。

   覆盖:建树(隐式乘法/一元负号/右结合幂)+ 渲染 + 17 函数求导规则 +
   dmake 化简 + preCheck 深度≤6 与树深联动 + factor 因式列表 + A6 隐函数
   + 数值对拍(evalDiff vs 中心差分)。

   运行:node test/test-symboltree.js   (JS/128KB 目录下)
   全过输出:==== symboltree: N pass, 0 fail ====
   注:不用 "use strict"——拼接体经 eval 装入(与 test-solvers.js 同款约定)。 */

var fs = require("fs");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_base.js", "core_solvers.js", "core_ratroot.js",
            "core_symboltree.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

angleMode = "RAD";
var pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}
function checkRender(name, src, want) {
  var rc = ApolloSymboltree.build(src);
  if (rc !== 0) { ok(name + "「" + src + "」build", false, "code=" + rc); return; }
  var s = ApolloSymboltree.toString();
  ok(name + "「" + src + "」", s === want, "got=「" + s + "」 want=「" + want + "」");
}
function checkDiff(name, src, want) {
  var rc = ApolloSymboltree.build(src);
  if (rc !== 0) { ok(name + "「d " + src + "」build", false, "code=" + rc); return; }
  rc = ApolloSymboltree.diff();
  if (rc !== 0) { ok(name + "「d " + src + "」diff", false, "code=" + rc); return; }
  var s = ApolloSymboltree.toString();
  ok(name + "「d/dx " + src + "」", s === want, "got=「" + s + "」 want=「" + want + "」");
}
function checkCode(name, src, wantCode) {
  var rc = ApolloSymboltree.build(src);
  ok(name + "「" + src + "」→ " + wantCode, rc === wantCode, "code=" + rc);
}
/* 数值对拍:导数树求值 vs 原树中心差分(容差 1e-4 相对) */
function checkNumeric(name, src, x) {
  var rc = ApolloSymboltree.build(src);
  if (rc !== 0) { ok(name + " build", false, "code=" + rc); return; }
  var f0 = ApolloSymboltree.evalTree(x);
  var h = 1e-5;
  var fp = ApolloSymboltree.evalTree(x + h);
  var fm = ApolloSymboltree.evalTree(x - h);
  if (f0 !== f0 || fp !== fp || fm !== fm) { ok(name, false, "eval NaN"); return; }
  var num = (fp - fm) / (2 * h);
  rc = ApolloSymboltree.diff();
  if (rc !== 0) { ok(name + " diff", false, "code=" + rc); return; }
  var dv = ApolloSymboltree.evalDiff(x);
  if (dv !== dv || num !== num) { ok(name, false, "NaN dv=" + dv + " num=" + num); return; }
  var scale = num < 0 ? -num : num;
  if (scale < 1) { scale = 1; }
  ok(name + "「d " + src + " @" + x + "」", Math.abs(dv - num) <= 1e-4 * scale,
     "got=" + dv + " numdiff=" + num);
}
function checkFactor(name, src, want) {
  var rc = ApolloSymboltree.build(src);
  if (rc !== 0) { ok(name + " build", false, "code=" + rc); return; }
  rc = ApolloSymboltree.factor();
  if (rc !== 0) { ok(name + " factor", false, "code=" + rc); return; }
  var s = ApolloSymboltree.factorToString();
  ok(name + "「factor " + src + "」", s === want, "got=「" + s + "」 want=「" + want + "」");
}
function checkFactorCode(name, src, wantCode) {
  var rc = ApolloSymboltree.build(src);
  if (rc !== 0) { ok(name + " build", false, "code=" + rc); return; }
  rc = ApolloSymboltree.factor();
  ok(name + "「factor " + src + "」→ " + wantCode, rc === wantCode, "code=" + rc);
}
function checkImp(name, expr, x, y, wantCode, wantDy) {
  ApolloSymboltree.reset();
  ApolloSymboltree.implicitDyDx("x^2+y^2-25", 9, 9);   /* 预置旧值,验证失败不改写 */
  var sentinel = ApolloSymboltree.getDyDx();
  var rc = ApolloSymboltree.implicitDyDx(expr, x, y);
  if (rc !== wantCode) { ok(name, false, "code=" + rc + " want=" + wantCode); return; }
  if (wantCode !== 0) {
    ok(name, ApolloSymboltree.getDyDx() === sentinel, "dydx 被改写(C:失败输出不动)");
    return;
  }
  ok(name, Math.abs(ApolloSymboltree.getDyDx() - wantDy) < 1e-12,
     "dydx=" + ApolloSymboltree.getDyDx() + " want=" + wantDy);
}

/* 建树与规范渲染(隐式乘法/一元负号/右结合幂) */
checkRender("渲染 1", "2x", "2*x");
checkRender("渲染 2", "2x^2+3x-5", "2*x^2+3*x-5");
checkRender("渲染 3", "-x", "-x");
checkRender("渲染 4", "--4", "4");
checkRender("渲染 5", "2^3^2", "2^(3^2)");
checkRender("渲染 6", "-2^2", "-2^2");
checkRender("渲染 7", "pi*x+e", "pi*x+e");
checkRender("渲染 8", "(x+1)(x-1)", "(x+1)*(x-1)");
checkRender("渲染 9", "3/2x", "3/2*x");
checkRender("渲染 10", "sqrt(2)x", "sqrt(2)*x");
checkRender("渲染 11", "x^2/2", "x^2/2");
checkRender("渲染 12", "e^x", "e^x");
checkRender("渲染 13", "2pi x", "2*pi*x");
checkRender("渲染 14", "1/2x^2", "1/2*x^2");

/* 求导:幂/四则与常数折叠 */
checkDiff("求导 1", "x", "1");
checkDiff("求导 2", "5", "0");
checkDiff("求导 3", "x^3", "3*x^2");
checkDiff("求导 4", "x^-2", "-2*x^-3");
checkDiff("求导 5", "0.5x^2", "x");
checkDiff("求导 6", "3x^2+5x", "6*x+5");
checkDiff("求导 7", "(x^2+1)*x", "2*x*x+x^2+1");
checkDiff("求导 8", "5-x", "-1");
checkDiff("求导 9", "3-x^2", "-2*x");
checkDiff("求导 10", "x^2/2", "4*x/2^2");
checkDiff("求导 11", "2^3^2", "0");
checkDiff("求导 12", "x^1", "1");
checkDiff("求导 13", "x^0", "0");
checkDiff("求导 14", "pi*x", "pi");

/* 求导:17 种单参函数规则 */
checkDiff("函数 1", "sin(x)", "cos(x)");
checkDiff("函数 2", "cos(x)", "-sin(x)");
checkDiff("函数 3", "tan(x)", "1/cos(x)^2");
checkDiff("函数 4", "exp(x)", "exp(x)");
checkDiff("函数 5", "ln(x)", "1/x");
checkDiff("函数 6", "log(x)", "1/(x*2.302585092994046)");
checkDiff("函数 7", "sqrt(x)", "1/(2*sqrt(x))");
checkDiff("函数 8", "abs(x)", "x/abs(x)");
checkDiff("函数 9", "sinh(x)", "cosh(x)");
checkDiff("函数 10", "cosh(x)", "sinh(x)");
checkDiff("函数 11", "tanh(x)", "1/cosh(x)^2");
checkDiff("函数 12", "asin(x)", "1/sqrt(1-x^2)");
checkDiff("函数 13", "acos(x)", "-1/sqrt(1-x^2)");
checkDiff("函数 14", "atan(x)", "1/(1+x^2)");
checkDiff("函数 15", "asinh(x)", "1/sqrt(x^2+1)");
checkDiff("函数 16", "acosh(x)", "1/sqrt(x^2-1)");
checkDiff("函数 17", "atanh(x)", "1/(1-x^2)");

/* 求导:链式/乘积/三分支幂 */
checkDiff("链式 1", "sin(x^2)", "2*x*cos(x^2)");
checkDiff("链式 2", "exp(2x)", "2*exp(2*x)");
checkDiff("链式 3", "ln(sin(x))", "cos(x)/sin(x)");
checkDiff("链式 4", "sqrt(x^2+1)", "2*x/(2*sqrt(x^2+1))");
checkDiff("链式 5", "x^x", "x^x*(ln(x)+x/x)");
checkDiff("链式 6", "e^x", "e^x");
checkDiff("链式 7", "2^x", "2^x*0.6931471805599453");
checkDiff("链式 8", "sin(x)*cos(x)", "cos(x)*cos(x)-sin(x)*sin(x)");
checkDiff("链式 9", "sin(x)/x", "(cos(x)*x-sin(x))/x^2");

/* dmake 化简规则(C sym_dmake_* 同款) */
checkDiff("化简 1", "x+0", "1");
checkDiff("化简 2", "1*x", "1");
checkDiff("化简 3", "0*x+x", "1");
checkDiff("化简 4", "0-x", "-1");

/* preCheck 深度≤6 与树深联动(先定义后写码) */
/* 定义:preCheck 管括号嵌套(≤6)/函数(≤4)/变量(≤10);树侧独立管
   节点(≤100)与树深(≤48)——括号嵌套不增加树深,运算链不受 preCheck 约束。 */
checkCode("联动 1(6 层括号过 preCheck)", "((((((x))))))", 0);
checkCode("联动 2(7 层括号 preCheck 拒)", "(((((((x)))))))", 2);
checkCode("联动 3(11 变量 preCheck 拒)", "x+x+x+x+x+x+x+x+x+x+x", 2);
checkCode("联动 4(5 函数 preCheck 拒)", "sin(x)+cos(x)+tan(x)+sin(x)+cos(x)", 2);
(function () {
  var s = "1";
  var i;
  for (i = 0; i < 49; i++) { s = s + "+1"; }         /* 50 项:节点 99<100,树深 49>48 */
  checkCode("联动 5(纯数字链树深 49 → EDEPTH)", s, 3);
})();
(function () {
  var blk = "((1+1)*(1+1))*((1+1)*(1+1))";            /* 每块 15 节点,深 3 */
  var s = blk;
  var i;
  for (i = 0; i < 6; i++) { s = s + "+" + blk; }      /* 7 块:111 节点 > 100 */
  checkCode("联动 6(111 节点 → EBUDGET)", s, 4);
})();
(function () {
  var rc = ApolloSymboltree.build("((((x))))+x^2");
  ok("联动 7(括号透明:树深 2)", rc === 0 && ApolloSymboltree.getDepth() === 2,
     "code=" + rc + " depth=" + ApolloSymboltree.getDepth());
})();
(function () {
  var rc = ApolloSymboltree.build("x^2^3+x");
  ok("联动 8(树深可观测:3)", rc === 0 && ApolloSymboltree.getDepth() === 3,
     "code=" + rc + " depth=" + ApolloSymboltree.getDepth());
})();
(function () {
  /* 树 = SUB(ADD(MUL(2,POW(x,CONST2)),MUL(3,x)),CONST5):4 常量+1 变量
     +1 幂+2 乘+1 加+1 减 = 10 */
  var rc = ApolloSymboltree.build("2x^2+3x-5");
  ok("联动 9(节点数可观测:10)", rc === 0 && ApolloSymboltree.getSrcNodes() === 10,
     "code=" + rc + " nodes=" + ApolloSymboltree.getSrcNodes());
})();

/* 错误码(解析/RPN/建树) */
checkCode("错误 1(空串)", "", 1);
checkCode("错误 2(悬空 ^)", "x^", 1);
checkCode("错误 3(atan2 双参)", "atan2(1,2)", 1);
checkCode("错误 4(尾随 +)", "x+", 1);
checkCode("错误 5(括号不匹配)", "(x+1", 1);

/* factor:多项式子树 → 因式列表(镜像 ac_sym_factor_poly) */
checkFactor("因式 1", "x^2-5x+6", "(x-2)*(x-3)");
checkFactor("因式 2", "x^2+1", "(x^2+1)");
checkFactor("因式 3", "x^2-2x+1", "(x-1)*(x-1)");
checkFactor("因式 4", "2x^2-3x+1", "(x-1)*(x-1/2)");
checkFactor("因式 5", "x^3-6x^2+11x-6", "(x-1)*(x-2)*(x-3)");
checkFactor("因式 6", "x^4-1", "(x-1)*(x+1)*(x^2+1)");
checkFactor("因式 7", "x^2/2-3x/2+1", "(0.5*x^2-1.5*x+1)");
checkFactor("因式 8(常数项 6 捕捉)", "x^2-9", "(x-3)*(x+3)");
checkFactorCode("因式 9(非多项式)", "sin(x)", 1);
checkFactorCode("因式 10(次数 5 越域)", "x^5", 1);
checkFactorCode("因式 11(x 在分母)", "1/x", 1);
(function () {
  var rc = ApolloSymboltree.build("x^4-1");
  if (rc !== 0) { ok("因式 12 getters", false, "build=" + rc); return; }
  rc = ApolloSymboltree.factor();
  var g = rc === 0 && ApolloSymboltree.getFactorCount() === 2 &&
    ApolloSymboltree.getRemainderDegree() === 2 &&
    ApolloSymboltree.getRemainderCoeff(2) === 1 &&
    ApolloSymboltree.getRemainderCoeff(1) === 0 &&
    ApolloSymboltree.getRemainderCoeff(0) === 1 &&
    ApolloSymboltree.getFactorRat(0) === 1 && ApolloSymboltree.getFactorDen(0) === 1 &&
    ApolloSymboltree.getFactorRadNum(0) === 0 && ApolloSymboltree.getFactorRad(0) === 1 &&
    ApolloSymboltree.getFactorExp(0) === 0 && ApolloSymboltree.getFactorRat(1) === -1;
  ok("因式 12(getter 五分量+剩余)", g,
     "k=" + ApolloSymboltree.getFactorCount() + " rd=" + ApolloSymboltree.getRemainderDegree());
})();

/* A6 隐函数 dy/dx = −F_x/F_y(C test_pro_runner A6-1..A6-8 逐条镜像) */
checkImp("A6-1 圆 @(3,4)", "x^2+y^2-25", 3.0, 4.0, 0, -0.75);
checkImp("A6-2 圆 @(-3,4)", "x^2+y^2-25", -3.0, 4.0, 0, 0.75);
checkImp("A6-3 双曲线 xy=6 @(2,3)", "x*y-6", 2.0, 3.0, 0, -1.5);
checkImp("A6-4 三次 x³+y³=9 @(1,2)", "x^3+y^3-9", 1.0, 2.0, 0, -0.25);
checkImp("A6-6 F_y=0 竖直切线 @(5,0)", "x^2+y^2-25", 5.0, 0.0, 3, 0);
checkImp("A6-6b F_y=0 @(2,0)", "x^2+y^2-4", 2.0, 0.0, 3, 0);
checkImp("A6-7 不含 y", "x^2-4", 2.0, 1.0, 1, 0);
checkImp("A6-8 函数调用越界", "sin(x)+y", 0.0, 1.0, 1, 0);
checkImp("A6 扩展 y²=x @(1,1)", "y^2-x", 1.0, 1.0, 0, 0.5);
checkImp("A6 扩展 除法拒绝(C 无除法节点)", "x/y-1", 1.0, 2.0, 1, 0);
checkImp("A6 扩展 负指数拒绝", "x^-1+y", 1.0, 2.0, 1, 0);
checkImp("A6 扩展 空串", "", 1.0, 1.0, 1, 0);
checkImp("A6 扩展 尾随运算符", "x+", 1.0, 1.0, 1, 0);
(function () {
  /* A6-5 数值对拍:圆上 (3,4) 的 dy/dx vs y=sqrt(25−x²) 中心差分 */
  ApolloSymboltree.reset();
  var rc = ApolloSymboltree.implicitDyDx("x^2+y^2-25", 3.0, 4.0);
  var h = 1e-6;
  var yp = Math.sqrt(25.0 - (3.0 + h) * (3.0 + h));
  var ym = Math.sqrt(25.0 - (3.0 - h) * (3.0 - h));
  var num = (yp - ym) / (2 * h);
  ok("A6-5 符号 vs 数值", rc === 0 && Math.abs(ApolloSymboltree.getDyDx() - num) < 1e-6,
     "sym=" + ApolloSymboltree.getDyDx() + " num=" + num);
})();

/* 数值对拍(evalDiff vs 中心差分,容差 1e-4) */
checkNumeric("数值 1", "x^x", 1.5);
checkNumeric("数值 2", "sin(x^2)", 0.7);
checkNumeric("数值 3", "tan(x)", 0.3);
checkNumeric("数值 4", "x^2/2", 2.0);
checkNumeric("数值 5", "exp(2x)", 0.1);
checkNumeric("数值 6", "sqrt(x^2+1)", 1.2);
checkNumeric("数值 7", "2^x", 0.5);
checkNumeric("数值 8", "ln(sin(x))", 0.5);
checkNumeric("数值 9", "abs(x)", 1.5);
checkNumeric("数值 10", "asin(x)", 0.3);
checkNumeric("数值 11", "atanh(x)", 0.3);
checkNumeric("数值 12", "x^2/2+x^-2", 1.3);

/* evalTree 与会话 */
(function () {
  var rc = ApolloSymboltree.build("2x^2+3x-5");
  ok("求值 1(2x²+3x−5 @2 = 9)", rc === 0 && Math.abs(ApolloSymboltree.evalTree(2) - 9) < 1e-12,
     "v=" + ApolloSymboltree.evalTree(2));
})();
(function () {
  ApolloSymboltree.build("sqrt(2)x");
  ok("求值 2(sqrt(2)x @4)", Math.abs(ApolloSymboltree.evalTree(4) - 4 * Math.sqrt(2)) < 1e-12,
     "v=" + ApolloSymboltree.evalTree(4));
})();
(function () {
  ApolloSymboltree.build("x^2");
  ApolloSymboltree.reset();
  ok("会话 1(reset 后 toString 空)", ApolloSymboltree.toString() === "" && ApolloSymboltree.getError() === 1,
     "s=「" + ApolloSymboltree.toString() + "」err=" + ApolloSymboltree.getError());
})();
(function () {
  ApolloSymboltree.build("x^2");
  var n1 = ApolloSymboltree.getNodeCount();
  ApolloSymboltree.build("x");
  ok("会话 2(重建复用池)", n1 >= 2 && ApolloSymboltree.getNodeCount() === 1 && ApolloSymboltree.getSrcNodes() === 1,
     "n1=" + n1);
})();
(function () {
  /* 导数可再求值:diff 后 evalDiff 与 evalTree 并存 */
  ApolloSymboltree.build("x^3");
  ApolloSymboltree.diff();
  ok("会话 3(diff 后原树仍在)", Math.abs(ApolloSymboltree.evalTree(2) - 8) < 1e-12 &&
    Math.abs(ApolloSymboltree.evalDiff(2) - 12) < 1e-12,
     "f=" + ApolloSymboltree.evalTree(2) + " d=" + ApolloSymboltree.evalDiff(2));
})();
(function () {
  /* allocCount 可审计 */
  ok("会话 4(allocCount=20)", ApolloSymboltree.allocCount() === 20,
     "alloc=" + ApolloSymboltree.allocCount());
})();

console.log("==== symboltree: " + pass + " pass, " + fail + " fail ====");
process.exit(fail > 0 ? 1 : 0);
