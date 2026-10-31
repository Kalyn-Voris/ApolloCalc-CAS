/* ApolloCalc-CAS · JS/128KB 符号积分测试 —— v2.4.0 功能 7(JS 侧)
 * 被测模块:src/core_integrate.js(依赖内核拼接作用域中的 parseExpression/
 * toRPN,按拼接序载入 parser→evaluator→solvers→ratroot→symboltree→integrate;
 * symboltree 先载入以复现内核真实拼接序)。
 * 对齐锚:C/128KB-ProTEXT/src/ac_pro_integrate.c(128 项积分表 + 固定两层
 * 分部 + pro_int_emit 渲染)与 C/128KB-ProTEXT/test/test_pro_integrate.c。
 * 【与 C 侧逐项对拍】下方标注「C B2-n」的用例与 C 侧 test_pro_integrate.c
 * 的 expect_int 断言一一对应(同输入、同期望串、同错误码),共用用例即
 * C 侧回归集,期望串逐字搬运。
 * 运行:node test/test-integrate.js   (JS/128KB 目录下)
 * 全过输出:==== integrate: N pass, 0 fail ====
 * 注:不用 "use strict"——拼接体经 eval 装入(与 test-solvers.js 同款约定)。
 */

var fs = require("fs");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_base.js", "core_solvers.js", "core_ratroot.js",
            "core_symboltree.js", "core_integrate.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

var pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}
/* C B2 同款:期望串逐字比对(rc=0 且 out === want) */
function checkInt(name, expr, want) {
  var rc = ApolloIntegrate.integrate(expr);
  if (rc !== 0) { ok(name + "「" + expr + "」", false, "rc=" + rc); return; }
  var s = ApolloIntegrate.getString();
  ok(name + "「" + expr + "」", s === want, "got=「" + s + "」 want=「" + want + "」");
}
/* 错误码比对(失败串恒空) */
function checkCode(name, expr, wantCode) {
  var rc = ApolloIntegrate.integrate(expr);
  ok(name + "「" + expr + "」→ " + wantCode, rc === wantCode && ApolloIntegrate.getString() === "",
     "rc=" + rc + " out=「" + ApolloIntegrate.getString() + "」");
}
/* 跨模块数值对拍:∫f 的结果串经 symboltree 求导,与 f 在多点数值相等 */
function checkDeriv(name, integrand, anti) {
  var rc = ApolloIntegrate.integrate(integrand);
  if (rc !== 0) { ok(name + " integrate", false, "rc=" + rc); return; }
  if (ApolloSymboltree.build(ApolloIntegrate.getString()) !== 0 ||
      ApolloSymboltree.diff() !== 0) {
    ok(name + " symboltree", false, "build/diff 失败");
    return;
  }
  if (ApolloSymboltree.build(integrand) !== 0) {
    ok(name + " 原函数 build", false, "integrand 不可建树");
    return;
  }
  var xs = [0.3, 0.7, 1.1, 1.9];
  var good = 1;
  var i = 0;
  var df = 0;
  var f0 = 0;
  for (i = 0; i < xs.length; i++) {
    if (ApolloSymboltree.build(anti) !== 0 || ApolloSymboltree.diff() !== 0) { good = 0; break; }
    df = ApolloSymboltree.evalDiff(xs[i]);
    if (ApolloSymboltree.build(integrand) !== 0) { good = 0; break; }
    f0 = ApolloSymboltree.evalTree(xs[i]);
    if (df !== df || f0 !== f0 || Math.abs(df - f0) > 1e-6 * (1 + Math.abs(f0))) { good = 0; break; }
  }
  ok(name + "「d/dx∫" + integrand + "」", good === 1,
     "df=" + df + " f=" + f0 + " @" + xs[i]);
}

/* ==================== 一、多项式/线性(C B2-1) ==================== */
checkInt("B2-1 幂", "x^2", "x^3/3");
checkInt("B2-1 x", "x", "x^2/2");
checkInt("B2-1 常数", "1", "x");
checkInt("B2-1 线性", "2x+3", "x^2+3*x");
checkInt("B2-1 混合", "x^3+2x^2-5x+1", "x^4/4+2*x^3/3-5*x^2/2+x");
checkInt("B2-1 展开幂", "(x+1)^2", "x^3/3+x^2+x");

/* ==================== 二、基本函数(C B2-2) ==================== */
checkInt("B2-2 sin", "sin(x)", "-cos(x)");
checkInt("B2-2 cos", "cos(x)", "sin(x)");
checkInt("B2-2 exp", "exp(x)", "exp(x)");
checkInt("B2-2 倒数", "1/x", "ln(x)");
checkInt("B2-2 tan", "tan(x)", "-ln(cos(x))");
checkInt("补充 e^ 底数", "e^x", "exp(x)");
checkInt("补充 线性内层", "sin(2x+1)", "-cos(2*x+1)/2");
checkInt("补充 sqrt", "sqrt(x)", "2*x*sqrt(x)/3");
checkInt("补充 log", "log(x)", "x*log(x)-x/ln(10)");

/* ==================== 三、分部一层(C B2-3) ==================== */
checkInt("B2-3 x·e^x", "x*e^x", "exp(x)*(x-1)");
checkInt("B2-3 x·sin", "x*sin(x)", "-x*cos(x)+sin(x)");
checkInt("B2-3 cos·x", "cos(x)*x", "x*sin(x)+cos(x)");
checkInt("B2-3 x²·exp", "x^2*exp(x)", "exp(x)*(x^2-2*x+2)");

/* ==================== 四、128 项表命中(C B2-4) ==================== */
checkInt("B2-4 ln", "ln(x)", "x*ln(x)-x");
checkInt("B2-4 atan 核", "1/(1+x^2)", "atan(x)");
checkInt("B2-4 1/x²", "1/x^2", "-1/x");
checkInt("补充 双曲", "sinh(x)", "cosh(x)");
checkInt("补充 cosh", "cosh(x)", "sinh(x)");
checkInt("补充 tanh", "tanh(x)", "ln(cosh(x))");

/* ==================== 五、二次核族(C B2-7) ==================== */
checkInt("B2-7 平移", "1/(1+(2*x+1)^2)", "atan(2*x+1)/2");
checkInt("B2-7 k=2", "1/(x^2+4)", "atan(x/2)/2");
checkInt("B2-7 k=3", "1/(x^2-9)", "ln((x-3)/(x+3))/6");
checkInt("B2-7 9−x²", "1/(9-x^2)", "atanh(x/3)/3");
checkInt("B2-7 sqrt(x²+4)", "sqrt(x^2+4)", "(x*sqrt(x^2+4)+4*asinh(x/2))/2");
checkInt("B2-7 x/(x²+1)", "x/(x^2+1)", "ln(x^2+1)/2");
checkCode("B2-7 8 非完全平方", "1/(x^2+8)", -2);

/* ==================== 六、倒数函数族(C B2-8) ==================== */
checkInt("B2-8 sec", "1/cos(x)", "ln(1/cos(x)+sin(x)/cos(x))");
checkInt("B2-8 cot", "cos(x)/sin(x)", "ln(sin(x))");
checkInt("B2-8 sec²", "1/cos(x)^2", "sin(x)/cos(x)");
checkInt("B2-8 csc²", "1/sin(x)^2", "-cos(x)/sin(x)");
checkInt("B2-8 coth", "sinh(x)/cosh(x)", "ln(sinh(x))");
checkInt("B2-8 sin·cos", "sin(x)*cos(x)", "sin(x)^2/2");
checkInt("B2-8 sec·tan", "1/cos(x)*tan(x)", "1/cos(x)");
checkInt("B2-8 ln/x", "ln(x)/x", "ln(x)^2/2");
checkInt("B2-8 1/(x·ln)", "1/(x*ln(x))", "ln(ln(x))");

/* ==================== 七、函数幂(C B2-9) ==================== */
checkInt("B2-9 exp²", "exp(x)^2", "exp(2*x)/2");
checkInt("B2-9 sin⁴", "sin(x)^4", "3*x/8-sin(2*x)/4+sin(4*x)/32");
checkInt("B2-9 tan³", "tan(x)^3", "tan(x)^2/2+ln(cos(x))");
checkInt("B2-9 sinh³", "sinh(x)^3", "cosh(x)^3/3-cosh(x)");

/* ==================== 八、x^n·f(x) 与固定两层分部(C B2-10) ==================== */
checkInt("B2-10 x⁴·sin", "x^4*sin(x)",
         "-x^4*cos(x)+4*x^3*sin(x)+12*x^2*cos(x)-24*x*sin(x)-24*cos(x)");
checkInt("B2-10 x⁴·ln", "x^4*ln(x)", "x^5*ln(x)/5-x^5/25");
checkInt("B2-10 x²·atan", "x^2*atan(x)", "((2*x^3-x^2-1)*atan(x)+x)/6");
checkInt("B2-10 x⁶·sin(两层)", "x^6*sin(x)",
         "-x^6*cos(x)+6*x^5*sin(x)+30*x^4*cos(x)-120*x^3*sin(x)"
         + "-360*x^2*cos(x)+720*x*sin(x)+720*cos(x)");
checkInt("B2-10 x⁷·exp(两层)", "x^7*exp(x)",
         "x^7*exp(x)-7*x^6*exp(x)"
         + "+42*exp(x)*(x^5-5*x^4+20*x^3-60*x^2+120*x-120)");
checkCode("B2-10 x⁷·sin → ETABLE", "x^7*sin(x)", -2);
checkCode("B2-10 x⁶·ln → ETABLE", "x^6*ln(x)", -2);

/* ==================== 九、错误码(AC_PRO_INT_* 全序,C B2-6 同表) ==================== */
checkCode("B2-6 空串", "", -1);
checkCode("B2-6 纯空白", "   ", -1);
checkCode("B2-6 未知变量", "z + 1", -1);
checkCode("B2-6 悬空 ^", "x^", -1);
checkCode("B2-6 指数 >96", "x^1000", -5);
checkCode("补充 小数常量(C 文法拒)", "0.5x", -1);
checkCode("补充 π(C 无此记号)", "pi*x", -1);
checkCode("补充 abs(C 表外函数)", "abs(x)", -1);
checkCode("补充 负指数(C EPARSE 同款)", "x^-1", -1);
checkCode("补充 常量 >1e6", "9999999x", -5);
checkCode("补充 指数 97 越界", "2^97", -5);
checkInt("补充 常量 1e6 边界内", "x^2*1000000", "1000000*x^3/3");

/* ==================== 十、预算(EBUDGET;C B2-5 的 JS 对应物) ==================== */
/* C B2-5 用共享 arena 的 g_sym_count 挤压触发;JS 池私有,改以真实
 * 耗尽触发:常数×长和差分配展开把 256 节点预算打爆。 */
(function () {
  var s = "2*(x";
  var i;
  for (i = 0; i < 149; i++) { s = s + "+x"; }
  s = s + ")";
  var rc = ApolloIntegrate.integrate(s);
  ok("预算 1(分配展开耗尽 → EBUDGET)", rc === -3, "rc=" + rc);
  rc = ApolloIntegrate.integrate("x^2");
  ok("预算 2(门禁非粘滞)", rc === 0 && ApolloIntegrate.getString() === "x^3/3",
     "rc=" + rc + " out=" + ApolloIntegrate.getString());
})();

/* ==================== 十一、跨模块数值对拍(结果串 → symboltree 求导 = 原函数) ==================== */
checkDeriv("数值 1 多项式", "x^3+2x^2-5x+1", "x^4/4+2*x^3/3-5*x^2/2+x");
checkDeriv("数值 2 sin", "sin(x)", "-cos(x)");
checkDeriv("数值 3 x·exp(分部)", "x^2*exp(x)", "exp(x)*(x^2-2*x+2)");
checkDeriv("数值 4 atan 核", "1/(1+x^2)", "atan(x)");
checkDeriv("数值 5 ln(表)", "ln(x)", "x*ln(x)-x");
/* 注:x⁴·sin 的 anti 串含 5 个函数调用,被 build 的 preCheck 联动
 * (函数 ≤4)拒绝 —— 该包络的诚实表现;数值对拍改用 4 函数以内的
 * x³·sin 两层结果(∫x³sin = −x³cos+3x²sin+6xcos−6sin)。 */
checkDeriv("数值 6 x³·sin(两层)", "x^3*sin(x)",
           "-x^3*cos(x)+3*x^2*sin(x)+6*x*cos(x)-6*sin(x)");

/* ==================== 十二、会话与审计 ==================== */
(function () {
  ApolloIntegrate.reset();
  ok("会话 1(reset 后串空)", ApolloIntegrate.getString() === "", "out 非空");
  var rc = ApolloIntegrate.integrate("x^2");
  ok("会话 2(复位后可复用)", rc === 0 && ApolloIntegrate.getString() === "x^3/3", "rc=" + rc);
  ok("会话 3(allocCount=28)", ApolloIntegrate.allocCount() === 28,
     "alloc=" + ApolloIntegrate.allocCount());
})();

console.log("==== integrate: " + pass + " pass, " + fail + " fail ====");
process.exit(fail > 0 ? 1 : 0);
