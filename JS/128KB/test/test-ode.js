/* ApolloCalc · test-ode.js — ODE 通解测试(v2.3.0 功能 3,JS 侧)

   被测模块:src/core_ode.js(依赖内核拼接作用域中的 _coeffs 与
   solveQuadratic/Cubic/Quartic,故按拼接序载入求解层文件)。
   契约:docs/contracts/ode-format-v23.md —— §5.4 全部 11 行期望串逐字
   比对 + §6 错误码全序 + §7 拒绝情形总表 + 边界值(§9-4)。
   运行:node test/test-ode.js   (JS/128KB 目录下)
   全过输出:==== ode: N pass, 0 fail ====
   注:不用 "use strict"——拼接体经 eval 装入,严格模式会把 eval 内的
   var 声明困在 eval 作用域(与 test-solvers.js 同款约定)。 */

var fs = require("fs");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_base.js", "core_solvers.js", "core_ratroot.js",
            "core_ode.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

var pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}
function checkOut(name, src, want) {
  var r = ApolloOde.solveOde(src);
  if (r.code !== 0) { ok(name, false, "code=" + r.code + " (" + src + ")"); return; }
  ok(name + "「" + src + "」", r.out === want, "got=「" + r.out + "」 want=「" + want + "」");
}
function checkCode(name, src, wantCode) {
  var r = ApolloOde.solveOde(src);
  ok(name + "「" + src + "」→ " + wantCode, r.code === wantCode && r.out === "",
     "code=" + r.code + " out=「" + r.out + "」");
}

/* §5.4 全覆盖范例(期望串逐字) */
checkOut("契约§5.4-1", "y''+3y'+2y=0", "y = C₁*exp(-1*x) + C₂*exp(-2*x)");
checkOut("契约§5.4-2", "y''-3y'+2y=0", "y = C₁*exp(2*x) + C₂*exp(1*x)");
checkOut("契约§5.4-3", "y''+2y'+5y=0", "y = C₁*exp(-1*x)*cos(2*x) + C₂*exp(-1*x)*sin(2*x)");
checkOut("契约§5.4-4", "y''+4y=0", "y = C₁*cos(2*x) + C₂*sin(2*x)");
checkOut("契约§5.4-5", "y''-4y'=0", "y = C₁*exp(4*x) + C₂");
checkOut("契约§5.4-6", "y''-2y'+y=0", "y = C₁*exp(1*x) + C₂*x*exp(1*x)");
checkOut("契约§5.4-7", "y'''-y'=0", "y = C₁*exp(1*x) + C₂ + C₃*exp(-1*x)");
checkOut("契约§5.4-8", "y''''-y=0", "y = C₁*exp(1*x) + C₂*cos(1*x) + C₃*sin(1*x) + C₄*exp(-1*x)");
checkOut("契约§5.4-9", "y''''+2y''+y=0", "y = C₁*cos(1*x) + C₂*sin(1*x) + C₃*x*cos(1*x) + C₄*x*sin(1*x)");
checkOut("契约§5.4-10", "y'=0", "y = C₁");
checkOut("契约§5.4-11", "2y''+2y'+y=0", "y = C₁*exp(-1/2*x)*cos(1/2*x) + C₂*exp(-1/2*x)*sin(1/2*x)");

/* 模板与 fmt 的补充确定性用例 */
checkOut("实重根三阶", "y'''-3y''+3y'-y=0",
         "y = C₁*exp(1*x) + C₂*x*exp(1*x) + C₃*x^2*exp(1*x)");
checkOut("三重零根", "y'''=0", "y = C₁ + C₂*x + C₃*x^2");
checkOut("一阶非零根", "y'+2y=0", "y = C₁*exp(-2*x)");
checkOut("一阶有理根", "2y'+y=0", "y = C₁*exp(-1/2*x)");
checkOut("小正有理根(q=999 边界,v2.4.1 回归)", "999y''+998y'-y=0",
         "y = C₁*exp(1/999*x) + C₂*exp(-1*x)");
checkOut("小正有理根(1/3,v2.4.1 回归)", "3y''+2y'-y=0",
         "y = C₁*exp(1/3*x) + C₂*exp(-1*x)");
checkOut("小正有理根(1/100,v2.4.1 回归)", "100y''+99y'-y=0",
         "y = C₁*exp(1/100*x) + C₂*exp(-1*x)");
checkOut("项首双符号 +-(v2.4.2 差分收口)", "+-3y''+3y'=0",
         "y = C₁*exp(1*x) + C₂");
checkOut("项首双负号 --(后一符号覆盖,v2.4.2)", "--y''+y=0",
         "y = C₁*exp(1*x) + C₂*exp(-1*x)");
checkOut("复根正实部", "y''-2y'+5y=0",
         "y = C₁*exp(1*x)*cos(2*x) + C₂*exp(1*x)*sin(2*x)");
checkOut("纯虚根 3", "y''+9y=0", "y = C₁*cos(3*x) + C₂*sin(3*x)");
checkOut("无理根走定点12位(√2)", "y''-2y=0",
         "y = C₁*exp(1.414213562373*x) + C₂*exp(-1.414213562373*x)");
checkOut("显式乘号等价", "3*y''+3y'=0", "y = C₁ + C₂*exp(-1*x)");
checkOut("前导零系数", "03y''+3y'=0", "y = C₁ + C₂*exp(-1*x)");
checkOut("系数999过界内", "999y''=0", "y = C₁ + C₂*x");

/* 错误码:EPARSE(−1) */
checkCode("空串", "", -1);
checkCode("缺 y(2=0)", "2=0", -1);
checkCode("缺 y(0=0)", "0=0", -1);
checkCode("缺 =(y''+y)", "y''+y", -1);
checkCode("右端空(y''=)", "y''=", -1);
checkCode("右端文法外字符(y''=;)", "y''=;", -1);
checkCode("大写 Y", "Y''=0", -1);
checkCode("^ 记号", "y^2+y=0", -1);
checkCode("y 后 *", "y''*3+y=0", -1);
checkCode("撇号间空白", "y ''=0", -1);
checkCode("同阶重复", "y''+y''=0", -1);
checkCode("同阶重复(带系数)", "y'+3y'=0", -1);
checkCode("非法字符 $", "x$+y''=0", -1);
checkCode("UTF-8 字符", "y''+√2y=0", -1);
checkOut("星号隐式系数 1(gb 先例)", "*y''=0", "y = C₁ + C₂*x");
checkCode("双星号", "3**y''=0", -1);
checkCode("左端无任何项(x=0)", "x=0", -1);
checkCode("纯右端(=0)", "=0", -1);
checkCode("系数单元被空白打断", "3 5y''=0", -1);
checkCode("y 后接 y", "yy=0", -1);
checkCode("尾随分隔符", "y''+=0", -1);
checkCode("导数记号后尾随 x(y''x=0,§3.3-1 项尾硬校验,v2.4.1 裁决)", "y''x=0", -1);
checkCode("项中 +(x+y''=0,v2.4.1 链 v2.4.2 钉死)", "x+y''=0", -1);
checkCode("每项至多一个 *(x*x*y''=0,§3.2)", "x*x*y''=0", -1);
checkCode("右端第二个 =(y''=x=0,§3.3-5)", "y''=x=0", -1);
checkCode("右端第二个 =(y''=0=0,§3.3-5)", "y''=0=0", -1);
/* 257 字节(超长,§9-4 边界) */
var long257 = "y''";
var li;
for (li = 0; li < 84; li++) { long257 += "+y'"; }
long257 += "+y";
checkCode("257 字节超长", long257, -1);
/* 256 字节(边界内:结构本身合法) */
var long256 = "y''";
for (li = 0; li < 84; li++) { long256 += "+y'"; }
long256 += "+";
checkCode("256 字节但尾随分隔符结构错(长度先行通过)", long256, -1);
checkOut("256 字节内合法形态", "y'=0", "y = C₁");

/* 错误码:EDEG(−2) */
checkCode("五撇号当场", "y'''''+y=0", -2);
checkCode("五撇号+变系数标志(EDEG 先于一切标志)", "y'''''+x=0", -2);

/* 错误码:EVARCOEF(−3) */
checkCode("变系数(x*y''+y)", "x*y''+y=0", -3);
checkCode("变系数(y''+x*y)", "y''+x*y=0", -3);
checkCode("变系数先于非齐次(x*y''=1)", "x*y''=1", -3);
checkCode("变系数 x 幂(x^2*y=0,多项式语义,v2.4.1 裁决)", "x^2*y=0", -3);
checkCode("x 幂值可空(x^y''=0,镜像 C 侧)", "x^y''=0", -3);

/* 错误码:ENONHOMO(−4) */
checkCode("非齐次常数", "y''+y=1", -4);
checkCode("非齐次 x(y''=x)", "y''=x", -4);
checkCode("非齐次 y(y''=y)", "y''=y", -4);
checkCode("右端非单个 0(y''=00)", "y''=00", -4);
checkCode("右端带号(y''=+0)", "y''=+0", -4);
checkCode("右端 y 残迹(y''=y')", "y''=y'", -4);

/* 错误码:ECOEF(−5) */
checkCode("无导数项(y=0)", "y=0", -5);
checkCode("无导数项(3y=0)", "3y=0", -5);
checkCode("恒等式(0y=0)", "0y=0", -5);
checkCode("恒等式(0y''+0y=0)", "0y''+0y=0", -5);
checkCode("系数越界 1000", "1000y''+y=0", -5);
checkCode("系数越界 1000 于低阶", "y''+1000y=0", -5);
checkOut("系数 999 边界内(λ=0 二重)", "999y''=0", "y = C₁ + C₂*x");

/* 判定全序交叉(§9-3) */
checkCode("结构错优先于 x 标志(x$+y''=0)", "x$+y''=0", -1);
checkCode("变系数先于非齐次(x*y''=1)", "x*y''=1", -3);
checkCode("EDEG 词法当场先于标志(y'''''+x=0)", "y'''''+x=0", -2);

/* 接口形态与零 GC */
var r1 = ApolloOde.solveOde("y''+4y=0");
var r2 = ApolloOde.solveOde("y'=0");
ok("solveOde 返回 {code,out} 且为单例(头注声明的别名语义)",
   r1 === r2 && typeof r2.code === "number" && typeof r2.out === "string" &&
   r2.code === 0 && r2.out === "y = C₁");
ok("失败 out 恒空串", ApolloOde.solveOde("y''=1").out === "" &&
   ApolloOde.solveOde("y''=1").code === -4);
ok("加载期分配 = 11", ApolloOde.allocCount() === 11, ApolloOde.allocCount());
ApolloOde.reset();
ok("reset 后仍可解", ApolloOde.solveOde("y''+3y'+2y=0").out ===
   "y = C₁*exp(-1*x) + C₂*exp(-2*x)");
/* EOVER(−6)JS 侧不产生:任何成功/失败码都不应是 −6 */
var codes = [0, -1, -2, -3, -4, -5, -7];
var probeOK = true;
var probes = ["y'=0", "", "y'''''+y=0", "x*y''+y=0", "y''=1", "y=0", "y''-3x=0"];
for (li = 0; li < probes.length; li++) {
  var cval = ApolloOde.solveOde(probes[li]).code;
  var known = false;
  for (var ck = 0; ck < codes.length; ck++) { if (cval === codes[ck]) { known = true; } }
  if (!known) { probeOK = false; }
}
ok("错误码域内(−6 EOVER JS 侧不产生)", probeOK);

console.log("==== ode: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
