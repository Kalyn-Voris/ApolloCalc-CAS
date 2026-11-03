/* ApolloCalc · test-sysolve.js — F2 二元方程组 GB 消元数值全解(JS 侧)测试

   被测模块:src/core_groebner.js 的 sysolve 扩段(ApolloGroebner.sysolve /
   getSysolveString),与 C 侧 test_pro_sysolve.c 互为镜像;期望串逐字搬运
   (契约 docs/contracts/sysolve-format-v25.md §5/§7;tests/systems.json
   双侧对拍)。覆盖:经典系/复数解路径/GB={1}/正维数/解耦交叉积/切相重根/
   EDEG 可达失败/h 次数 4..8 路由/16 解 Bézout 封顶/去重/错误码全序/
   「刚好通过/刚好拒绝」双侧边界/确定性。

   运行:node test/test-sysolve.js   (JS/128KB 目录下)
   全过输出:==== sysolve: N pass, 0 fail ====
   注:不用 "use strict"——拼接体经 eval 装入(test-groebner.js 同款约定)。 */
var fs = require("fs");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_base.js", "core_solvers.js", "core_ratroot.js",
            "core_symboltree.js", "core_integrate.js", "core_multivar.js", "core_matrix.js",
            "core_special.js", "core_numtheory.js", "core_ode.js", "core_groebner.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

var pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}
function expectSysolve(name, f, g, wantRc, wantStr) {
  var rc = ApolloGroebner.sysolve(f, g);
  var s = ApolloGroebner.getSysolveString();
  var good = (rc === wantRc) && (wantStr === null || s === wantStr);
  ok(name, good, "rc=" + rc + " got=\"" + s.slice(0, 80) + "\" wantRc=" + wantRc);
}

/* ---- SYSOLVE-1..2 对应:经典系 + 复数解路径 ---- */
expectSysolve("sys-A classic 4 solutions", "x^2+y^2-4", "xy-1", 0,
  "[(0.517638090205, 1.931851652578), (1.931851652578, 0.517638090205), (-1.931851652578, -0.517638090205), (-0.517638090205, -1.931851652578)]");
expectSysolve("sys-B complex branch", "x^2+y^2-1", "x^2-y", 0,
  "[(0.786151377757, 0.61803398875), (-0.786151377757, 0.61803398875), (1.272019649514i, -1.61803398875), (-1.272019649514i, -1.61803398875)]");

/* ---- SYSOLVE-3 对应:GB={1} → 成功 0 解 ---- */
expectSysolve("sys-C GB={1} -> []", "x-1", "x-2", 0, "[]");
expectSysolve("sys-C2 line (no y-univ)", "x-1", "2x-2", -8, null);

/* ---- SYSOLVE-4 对应:正维数 ---- */
expectSysolve("sys-D positive dimension", "x+y-1", "2x+2y-2", -8, null);
expectSysolve("sys-M all mixed vanish", "y^2", "xy", -8, null);
expectSysolve("sys-G0 x unconstrained (g=0)", "y^2+1", "0", -8, null);

/* ---- SYSOLVE-5 对应:解耦交叉积 ---- */
expectSysolve("sys-E decoupled pair", "x^2-1", "y-2", 0, "[(1, 2), (-1, 2)]");
expectSysolve("sys-K decoupled 4", "x^2-1", "y^2-4", 0,
  "[(1, 2), (-1, 2), (1, -2), (-1, -2)]");

/* ---- SYSOLVE-6 对应:切相重根 ---- */
expectSysolve("sys-F tangent (h=(y-1)^2)", "x^2+y^2-2", "x+y-2", 0, "[(1, 1)]");

/* ---- SYSOLVE-7 对应:EDEG 可达失败 ---- */
expectSysolve("sys-G GB degree fuse", "x^3+y^3-1", "xy-1", -2, null);

/* ---- SYSOLVE-8/9 对应:h 次数路由 4 / 5 ---- */
expectSysolve("sys-H h deg 4 exact route", "y^4-5y^2+4", "x-y", 0,
  "[(2, 2), (1, 1), (-1, -1), (-2, -2)]");
expectSysolve("sys-I h deg 5 DK route", "xy^2-1", "x-y^3", 0,
  "[(1, 1), ([-0.809016994375]-0.587785252292i, [0.309016994375]+0.951056516295i), ([-0.809016994375]+0.587785252292i, [0.309016994375]-0.951056516295i), ([0.309016994375]+0.951056516295i, [-0.809016994375]+0.587785252292i), ([0.309016994375]-0.951056516295i, [-0.809016994375]-0.587785252292i)]");

/* ---- SYSOLVE-11/12 对应:h 次数 6 / 7 / 8 ---- */
expectSysolve("sys-L h deg 6", "y^3-x", "x^2+y^2-2", 0,
  "[(1, 1), ([-1.632241882312]+0.405232726187i, [0.676096724727]+0.978318343479i), ([-1.632241882312]-0.405232726187i, [0.676096724727]-0.978318343479i), ([1.632241882312]+0.405232726187i, [-0.676096724727]+0.978318343479i), ([1.632241882312]-0.405232726187i, [-0.676096724727]-0.978318343479i), (-1, -1)]");
expectSysolve("sys-N h deg 7", "xy^3-1", "x-y^4", 0,
  "[(1, 1), ([-0.900968867902]-0.433883739118i, [0.623489801859]+0.781831482468i), ([-0.900968867902]+0.433883739118i, [0.623489801859]-0.781831482468i), ([0.623489801859]+0.781831482468i, [-0.222520933956]+0.974927912182i), ([0.623489801859]-0.781831482468i, [-0.222520933956]-0.974927912182i), ([-0.222520933956]-0.974927912182i, [-0.900968867902]+0.433883739118i), ([-0.222520933956]+0.974927912182i, [-0.900968867902]-0.433883739118i)]");
expectSysolve("sys-O h deg 8", "x^2-1", "x-y^4", 0,
  "[(1, 1), (-1, [0.707106781187]+0.707106781187i), (-1, [0.707106781187]-0.707106781187i), (1, i), (1, -i), (-1, [-0.707106781187]+0.707106781187i), (-1, [-0.707106781187]-0.707106781187i), (1, -1)]");

/* ---- SYSOLVE-13 对应:16 解 Bézout 封顶「刚好通过」 ---- */
expectSysolve("sys-P 16 solutions (Bezout cap)", "x^4-1", "y^4-1", 0,
  "[(1, 1), (i, 1), (-i, 1), (-1, 1), (1, i), (i, i), (-i, i), (-1, i), (1, -i), (i, -i), (-i, -i), (-1, -i), (1, -1), (i, -1), (-i, -1), (-1, -1)]");

/* ---- SYSOLVE-14 对应:候选去重 ---- */
expectSysolve("sys-S dedup double x-root", "x^2-2x+1", "y-1", 0, "[(1, 1)]");

/* ---- SYSOLVE-15 对应:错误码 ---- */
expectSysolve("sys-T empty f", "", "x-2", -1, null);
expectSysolve("sys-U input degree 5", "x^5+y", "x-1", -2, null);
expectSysolve("sys-V exponent 9", "x^9+y", "x-1", -2, null);
expectSysolve("err two deg-5 inputs", "x^5+y^5", "xy", -2, null);
expectSysolve("err huge coefficient", "99999999999999999999999x+y", "x-1", -6, null);
ok("err null f", ApolloGroebner.sysolve(null, "x-1") === -1);
ok("err non-string g", ApolloGroebner.sysolve("x-1", 42) === -1);

/* ---- SYSOLVE-17 对应:gb 文法镜像 ---- */
expectSysolve("syntax implicit product", "x y - 2", "x + y - 3", 0, "[(1, 2), (2, 1)]");
expectSysolve("syntax var order swapped", "y*x-6", "x+y-5", 0, "[(2, 3), (3, 2)]");
expectSysolve("syntax whitespace", "  x^2 + y^2 - 25 ", " x + y - 1 ", 0, "[(-3, 4), (4, -3)]");

/* ---- SYSOLVE-19/20 对应:端到端 + 非本原 ---- */
expectSysolve("sanity circle+line", "x^2+y^2-13", "x+y-5", 0, "[(2, 3), (3, 2)]");
expectSysolve("nonprimitive scaled linear", "2x+2y-6", "3x-3y-3", 0, "[(2, 1)]");
expectSysolve("nonprimitive sign-flipped", "-x^2-y^2+4", "-xy+1", 0,
  "[(0.517638090205, 1.931851652578), (1.931851652578, 0.517638090205), (-1.931851652578, -0.517638090205), (-0.517638090205, -1.931851652578)]");

/* ---- 求解层交叉验证(JS solveLinearSystem 同款) ---- */
matrixPool[0] = 1; matrixPool[1] = 1; matrixPool[2] = 3;
matrixPool[3] = 1; matrixPool[4] = -1; matrixPool[5] = 1;
solveLinearSystem(2);
ok("linear cross-check (2,1)", _rootCount === 2 && _rootRe[0] === 2 && _rootRe[1] === 1,
   "cnt=" + _rootCount);
solversReset();

/* ---- 确定性:换序不变 + 幂等 ---- */
var rcA = ApolloGroebner.sysolve("x^2+y^2-4", "xy-1");
var sA = ApolloGroebner.getSysolveString();
var rcB = ApolloGroebner.sysolve("xy-1", "x^2+y^2-4");
var sB = ApolloGroebner.getSysolveString();
ok("swap f/g same list", rcA === 0 && rcB === 0 && sA === sB);
ApolloGroebner.sysolve("x^2+y^2-4", "xy-1");
ok("repeat idempotent", ApolloGroebner.getSysolveString() === sA);

/* ---- 与 GB 面零回归(A3/GB 既有锚) ---- */
ok("gb face no regression", ApolloGroebner.groebner("x^2+y^2-1", "x^2-y") === 1 &&
   ApolloGroebner.getString() === "x^2 - y | y^2 + y - 1");
ok("resultant face no regression", ApolloGroebner.resultant("y^2 - x", "y - 1", 4.0) === 1 &&
   Math.abs(ApolloGroebner.getResultant() - (-3)) < 1e-10);
ok("sysolveAllocCount audited", ApolloGroebner.sysolveAllocCount() === 30);

/* ---- 「刚好通过/刚好拒绝」边界(系数域 2^53;容量 16) ---- */
expectSysolve("boundary 999999x ok", "999999x-1", "y-1", 0, "[(0.000001000001, 1)]");
expectSysolve("boundary 2^53-scale reject", "9007199254740992x-1", "y-1", -6, null);
ok("boundary alloc after runs", ApolloGroebner.sysolveAllocCount() === 30);

console.log("==== sysolve: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
