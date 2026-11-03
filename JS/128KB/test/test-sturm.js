/* ApolloCalc · test-sturm.js — F3 Sturm 序列与实根隔离(JS 镜像 deg ≤ 4)测试

   被测模块:src/core_ratroot.js 的 sturm 扩段(ApolloSqfree.sturmCount /
   sturmIsolate / getSturmString),与 C 侧 test_pro_sturm.c 互为镜像;
   期望值 = node BigInt 精确 oracle 离线核算(tests/sturm.json 三层:
   dualCases / cOnlyDEG / cOnlyOVF)。覆盖:计数闭区间端点语义、±∞ 与
   混合端点、a=b、重根 SFF 去重、负首项/内容、近根隔离、既约分数端点、
   cOnly-DEG(≥5 → EDEG)、cOnly-OVF(2^53 审计:大端点「刚好通过/刚好
   拒绝」边界对 9741/9742)、文法/端点负向全序、幂等。

   运行:node test/test-sturm.js   (JS/128KB 目录下)
   全过输出:==== sturm: N pass, 0 fail ==== */
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
function expectCount(name, p, a, b, want) {
  var rc = ApolloSqfree.sturmCount(p, a, b);
  ok(name, rc === want, "rc=" + rc + " want=" + want);
}
function expectIso(name, p, want) {
  var rc = ApolloSqfree.sturmIsolate(p);
  var s = ApolloSqfree.getSturmString();
  ok(name, rc === 0 && s === want, "rc=" + rc + " got=\"" + s + "\" want=\"" + want + "\"");
}

/* ---- dualCases:计数(契约 §2.3 闭区间语义) ---- */
expectCount("st-lin [0,2]", "2x-3", "0", "2", 1);
expectCount("st-lin [-inf,inf]", "2x-3", "-inf", "inf", 1);
expectCount("st-cubic [-2,2]", "x^3-x", "-2", "2", 3);
expectCount("st-cubic [-inf,inf]", "x^3-x", "-inf", "inf", 3);
expectCount("st-cubic [0,1] 端点根计入", "x^3-x", "0", "1", 2);
expectCount("st-cubic [-1,inf]", "x^3-x", "-1", "inf", 3);
expectCount("st-cubic [-inf,1]", "x^3-x", "-inf", "1", 3);
expectCount("st-cubic [-1,1] 闭区间全含", "x^3-x", "-1", "1", 3);
expectCount("st-x2p1 [-5,5]", "x^2+1", "-5", "5", 0);
expectCount("st-x2p1 [-inf,inf]", "x^2+1", "-inf", "inf", 0);
expectCount("st-x2p1 大端点", "x^2+1", "-999999", "999999", 0);
expectCount("st-x4p1 [-3,3]", "x^4+1", "-3", "3", 0);
expectCount("st-quartic4 [-inf,inf]", "x^4-5x^2+4", "-inf", "inf", 4);
expectCount("st-quartic4 [0,10]", "x^4-5x^2+4", "0", "10", 2);
expectCount("st-quartic4 [-99,99]", "x^4-5x^2+4", "-99", "99", 4);
expectCount("st-sff-dual 重根去重", "x^3-3x+2", "-10", "10", 2);
expectCount("st-endpoints [-2,3]", "x^2-4", "-2", "3", 2);
expectCount("st-endpoints [-3,2]", "x^2-4", "-3", "2", 2);
expectCount("st-endpoints [-2,2]", "x^2-4", "-2", "2", 2);
expectCount("st-endpoints [0,10]", "x^2-4", "0", "10", 1);
expectCount("st-endpoints [3,10]", "x^2-4", "3", "10", 0);
expectCount("st-endpoints [2,2] a=b", "x^2-4", "2", "2", 1);
expectCount("st-endpoints [0,0]", "x^2-4", "0", "0", 0);
expectCount("st-neglc 负首项", "-x^3+x", "-2", "2", 3);
expectCount("st-content 内容 6", "6x^2-6", "-2", "2", 2);
expectCount("st-const 常数 7", "7", "-3", "3", 0);
expectCount("st-close [0,1]", "999x^2-2", "0", "1", 1);
expectCount("st-close2 [0,100]", "x^2-999", "0", "100", 1);

/* ---- dualCases:隔离(既约分数端点) ---- */
expectIso("st-lin isolate", "2x-3", "[[-4, 4]]");
expectIso("st-cubic isolate", "x^3-x", "[[-2, -1], [-1, 0], [0, 2]]");
expectIso("st-x2p1 isolate", "x^2+1", "[]");
expectIso("st-x4p1 isolate", "x^4+1", "[]");
expectIso("st-quartic4 isolate", "x^4-5x^2+4", "[[-4, -2], [-2, 0], [0, 1], [1, 2]]");
expectIso("st-sff-dual isolate", "x^3-3x+2", "[[-4, 0], [0, 4]]");
expectIso("st-content isolate", "6x^2-6", "[[-2, 0], [0, 2]]");
expectIso("st-const isolate", "7", "[]");
expectIso("st-close isolate", "999x^2-2", "[[-2, 0], [0, 2]]");
expectIso("st-close2 isolate", "x^2-999", "[[-1024, 0], [0, 1024]]");
expectIso("st-mignotte-iso isolate", "999x^4-999x^3+1", "[[0, 1/2], [1/2, 1]]");

/* ---- 文法与端点负向(契约 §3/§6 全序) ---- */
expectCount("st-eparam a>b", "x^2-4", "5", "3", -4);
expectCount("st-eparam 非法端点", "x^2-4", "abc", "1", -4);
expectCount("st-eparam 越域 1000000", "x^2-4", "1000000", "inf", -4);
expectCount("st-eparam 小数端点", "x^2-4", "1.5", "2", -4);
expectCount("st-eparse 大写 X", "X^2-4", "-3", "3", -1);
expectCount("st-eparse '='", "x^2=4", "-3", "3", -1);
expectCount("st-eparse 非整系数", "x^2/2", "-3", "3", -1);
expectCount("st-eparse 空串", "", "-3", "3", -1);
expectCount("st-eparse 同次重复", "x^2+x^2", "-3", "3", -1);
expectCount("st-edeg9 次数 9", "x^9+1", "-3", "3", -2);
expectCount("st-ecoef 全零", "0", "-3", "3", -3);
expectCount("st-ecoef 1000 刚好拒绝", "1000x", "-3", "3", -3);
expectCount("st-ecoef 999 刚好通过对照", "999x", "-3", "3", 1);

/* ---- cOnly-DEG(JS ≥5 → EDEG 成文分叉) ---- */
expectCount("st-deg5 JS EDEG", "x^5-1", "-2", "2", -2);
expectCount("st-deg5rep JS EDEG", "x^5+x^4-5x^3-x^2+8x-4", "-10", "10", -2);
expectCount("st-deg8 JS EDEG", "x^8+1", "-3", "3", -2);
expectCount("st-dense8 JS EDEG", "9x^8+8x^7+7x^6+6x^5+5x^4+4x^3+3x^2+2x+1", "-2", "2", -2);

/* ---- cOnly-OVF(JS 2^53 审计;「刚好通过/刚好拒绝」边界对) ---- */
expectCount("st-ovf-pass 9741 (t^4+1 <= 2^53-1)", "x^4+1", "-9741", "9741", 0);
expectCount("st-ovf-reject 9742 (t^4+1 > 2^53-1)", "x^4+1", "-9742", "9742", -3);
expectCount("st-ovf-mignotte-endpoint", "999x^4-999x^3+1", "-999999", "999999", -3);
expectCount("st-ovf-cubic-endpoint", "x^3-2", "-999999", "999999", -3);
/* 隔离轨道在 OVF 域的行为:大系数+深端点 → ECOEF(浮点界不可判定/审计) */
ok("st-ovf isolate x^4+1 shallow dual", ApolloSqfree.sturmIsolate("x^4+1") === 0 &&
   ApolloSqfree.getSturmString() === "[]");

/* ---- 空白容忍与幂等 ---- */
expectCount("空白容忍 count", "x^3 - x ", "-2", "2", 3);   /* 注:符号后空格("- x^3")为 sqfree JS lex 既有从严禁例,C 侧接受——既有双侧差异,不在本功能域 */
expectIso("空白容忍 isolate", " 6x^2 - 6 ", "[[-2, 0], [0, 2]]");
ApolloSqfree.sturmIsolate("x^3-x");
var s1 = ApolloSqfree.getSturmString();
ApolloSqfree.sturmIsolate("x^3-x");
ok("幂等(跨调用无残留)", ApolloSqfree.getSturmString() === s1);

/* ---- sqfree 面零回归(同文件既有锚) ---- */
ok("sqfree face no regression", ApolloSqfree.sqfree("x^2-5x+6").code === 0 &&
   ApolloSqfree.sqfree("x^2-5x+6").out === "[3@e1, 2@e1]");
ok("sturmAllocCount audited", ApolloSqfree.sturmAllocCount() === 15);

/* ---- 边界:端点 6 位上限「刚好通过/刚好拒绝」 ---- */
expectCount("端点 999999 刚好通过", "x^2+1", "-999999", "999999", 0);
expectCount("端点 1000000 刚好拒绝", "x^2+1", "1000000", "inf", -4);

console.log("==== sturm: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
