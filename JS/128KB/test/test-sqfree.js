/* ============================================================
 * ApolloCalc-CAS · JS/128KB sqfree 单变量多项式 square-free 分解测试
 * —— v2.4.0 功能 9(JS 侧)
 * 被测模块:src/core_ratroot.js 的 ApolloSqfree 扩展段。
 * 契约:docs/contracts/sqfree-format-v24.md(冻结)——
 *   §6.2 全覆盖范例逐字比对(JS 适用行 = 整题 ≤4;≥5 行断言 −2)、
 *   §7 错误码表 + 判定全序交叉例、§8 拒绝情形总表、§10-4 边界值
 *   (系数 999/1000、次数 4/5、重数、常数/全零、长度 128/129)。
 * 期望串逐字 = 契约 §6.2(C/JS 双侧逐字契约;JS 全走 @e 精确管线)。
 * 运行:node test/test-sqfree.js   (JS/128KB 目录下)
 * 全过输出:==== sqfree: N pass, 0 fail ====
 * 注:不用 "use strict"——拼接体经 eval 装入(test-solvers.js 同款约定)。
 * ============================================================ */
var fs = require("fs");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_base.js", "core_solvers.js", "core_ratroot.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

var pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}
/* 契约 §6.2 逐字比对(rc=0 且 out === want) */
function checkOut(name, src, want) {
  var r = ApolloSqfree.sqfree(src);
  ok(name + "「" + src + "」", r.code === 0 && r.out === want,
     "code=" + r.code + " out=「" + r.out + "」 want=「" + want + "」");
}
/* 错误码比对(失败 out 恒空,§9) */
function checkCode(name, src, wantCode) {
  var r = ApolloSqfree.sqfree(src);
  ok(name + "「" + src + "」→ " + wantCode, r.code === wantCode && r.out === "",
     "code=" + r.code + " out=「" + r.out + "」");
}

/* ==================== 一、契约 §6.2 全覆盖范例(JS 适用行,期望逐字) ==================== */
checkOut("契约§6.2-1", "x^2-5x+6", "[3@e1, 2@e1]");
checkOut("契约§6.2-2", "x^2-2x+1", "[1@e2]");
checkOut("契约§6.2-3", "x^3+x^2-5x+3", "[1@e2, -3@e1]");
checkOut("契约§6.2-4", "x^2+1", "[i@e1, -i@e1]");
checkOut("契约§6.2-5 本原化", "2x^2-2", "[1@e1, -1@e1]");
checkOut("契约§6.2-6", "x^4+2x^2+1", "[i@e2, -i@e2]");
checkOut("契约§6.2-9 常数", "7", "[]");

/* §6.2-7/8/10/11(整题 5..8 次):C 侧 @e/@d 输出,JS 侧 §5.1 成文例外断言 −2 */
checkCode("契约§6.2-7 整题6次(JS拒)", "x^6-4x^5+7x^4-8x^3+7x^2-4x+1", -2);
checkCode("契约§6.2-8 整题8次(JS拒)", "x^8-8x^7+28x^6-56x^5+70x^4-56x^3+28x^2-8x+1", -2);
checkCode("契约§6.2-10 整题5次(JS拒)", "x^5-15x^4+85x^3-225x^2+274x-120", -2);
checkCode("契约§6.2-11 x^8-1(JS拒)", "x^8-1", -2);

/* ==================== 二、补充确定性用例(重数/排序/复根/fmt) ==================== */
checkOut("重数3", "x^3-3x^2+3x-1", "[1@e3]");
checkOut("重数4(整题4次界内)", "x^4-4x^3+6x^2-4x+1", "[1@e4]");
checkOut("§5.3 Re降序", "x^2-9", "[3@e1, -3@e1]");
checkOut("§5.3 复根共轭序", "x^2+2x+5", "[-1+2i@e1, -1-2i@e1]");
checkOut("§5.3 四根 Re 降序", "x^4-1", "[1@e1, i@e1, -i@e1, -1@e1]");
checkOut("§6.1 复根 |im|=1 只写 i", "x^2+1", "[i@e1, -i@e1]");
checkOut("§6.1 有理根 1/2", "2x-1", "[1/2@e1]");
checkOut("§5.4 定点12位", "3x^2-1", "[0.57735026919@e1, -0.57735026919@e1]");
checkOut("§5.4 定点12位(999x⁴=1)", "999*x^4-1",
         "[0.177872425796@e1, 0.177872425796i@e1, -0.177872425796i@e1, -0.177872425796@e1]");
checkOut("§4.1 容度+符号", "-2x^2+2", "[1@e1, -1@e1]");
checkOut("§3.2 显式乘号", "3*x^2-3", "[1@e1, -1@e1]");
checkOut("§3.2 前导零指数", "x^02-1", "[1@e1, -1@e1]");
checkOut("§3.2 前导零系数", "03x^2-3", "[1@e1, -1@e1]");
checkOut("§3.3 项首符号消费", "+-x^2+1", "[1@e1, -1@e1]");
checkOut("§3.1 空白穿插(单元间)", " x^2 - 1 ", "[1@e1, -1@e1]");
checkCode("§3.1 ^ 与次数间空白拒", "x ^ 2 - 1", -1);
checkOut("§5.3 零根排序", "x^3-x", "[1@e1, 0@e1, -1@e1]");

/* ==================== 三、§8 拒绝情形总表(逐条 = §7 码) ==================== */
checkCode("§8 非整系数·小数", "0.5*x^2+x", -1);
checkCode("§8 非整系数·分数", "1/2*x^2", -1);
checkCode("§8 方程形式", "x^2-1=0", -1);
checkCode("§8 括号", "(x-1)^2", -1);
checkCode("§8 多变量", "x*y^2", -1);
checkCode("§8 大写 X", "X^2-1", -1);
checkCode("§8 UTF-8 ²", "x²-1", -1);
checkCode("§8 次数 ≥9(§7 当场)", "x^9+1", -2);
checkCode("§8 次数 ≥9 无系数", "x^12", -2);
checkCode("§8 次数 ≥5(JS 侧)", "x^5-1", -2);
checkCode("§8 全零多项式", "0", -3);
checkCode("§8 系数越界 1000x²", "1000*x^2+1", -3);
checkCode("§8 系数越界 -1000x", "-1000x", -3);
checkCode("§8 同次项重复", "x^2+2x^2", -1);
checkCode("§8 同次重复(带系数)", "x^2+3x^2", -1);
checkCode("§8 常数项重复", "1+2", -1);
checkCode("§8 悬空 3*", "3*", -1);
checkCode("§8 悬空 *x", "*x", -1);
checkCode("§8 x*3", "x*3", -1);
checkCode("§8 裸符号", "+", -1);
checkCode("§8 悬空 ^", "x^", -1);
checkCode("§8 x^0", "x^0", -1);
checkCode("§8 ^ 后夹空白", "x^ 2", -1);
checkCode("§8 空串", "", -1);
checkCode("§8 纯空白", "   ", -1);
checkCode("§8 x 后 *", "x*3y", -1);
checkCode("§8 大写变量 Y", "y^2-1", -1);

/* ==================== 四、§7 判定全序交叉例 ==================== */
checkCode("§7 全序:0.5+x^9 → EPARSE(. 先命中)", "0.5+x^9", -1);
checkCode("§7 全序:x^9+0.5 → EDEG(当场)", "x^9+0.5", -2);
checkCode("§7 全序:1000*x^2+1 → ECOEF", "1000*x^2+1", -3);

/* ==================== 五、§10-4 边界值(刚好通过/刚好拒绝) ==================== */
checkOut("边界 系数999(合法)", "999x^2-1", "[0.031638599858@e1, -0.031638599858@e1]");
checkCode("边界 系数1000 拒", "1000x^2-1", -3);
checkOut("边界 次数4 过", "x^4-16", "[2@e1, 2i@e1, -2i@e1, -2@e1]");
checkCode("边界 次数5 拒", "x^5-16", -2);
checkOut("边界 长度128 过(空白填充)", "x^2-1" + new Array(124).join(" "), "[1@e1, -1@e1]");
checkCode("边界 长度129 拒(空白填充)", "x^2-1" + new Array(125).join(" "), -1);
checkOut("边界 本原化 999 容度", "999x^2-999", "[1@e1, -1@e1]");
checkOut("边界 重根+单根混合", "x^3-x^2-x+1", "[1@e2, -1@e1]");

/* ==================== 六、会话与审计 ==================== */
(function () {
  ApolloSqfree.reset();
  ok("会话 1(reset 后串空)", ApolloSqfree.sqfree("x^2-1").out !== "");
  var r = ApolloSqfree.sqfree("x^2-5x+6");
  ok("会话 2(连续调用一致)", r.code === 0 && r.out === "[3@e1, 2@e1]", r.out);
  var r2 = ApolloSqfree.sqfree("x^2-2x+1");
  ok("会话 3(跨输入无残留)", r2.code === 0 && r2.out === "[1@e2]", r2.out);
  ok("审计 allocCount=18", ApolloSqfree.allocCount() === 18, "alloc=" + ApolloSqfree.allocCount());
})();

console.log("==== sqfree: " + pass + " pass, " + fail + " fail ====");
process.exit(fail > 0 ? 1 : 0);
