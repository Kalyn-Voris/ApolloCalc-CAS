/* ApolloCalc · test-groebner.js — 结式 + 2 变量 Groebner 基测试(v2.4.0 功能 10,JS 侧)

   被测模块:src/core_groebner.js(ApolloGroebner)。
   对齐锚:C/128KB-ProTEXT/test/test_pro_groebner.c(GB-1..GB-12,期望串
   逐字搬运——reduced GB 在固定单项式序下唯一,node 独立核算锚定)与
   test_pro_multi.c 的 A3 结式用例(A3-1..A3-4)。
   GB-1..GB-10 与 A3-1..A3-4 即 C 侧回归集,两侧一致用例在此内嵌;
   tests/polynomials.json 收口归收尾组(tests/ 本版冻结)。
   系数域边界:JS 无 int64,Float64 安全整数 |v| > 2^53−1 → EOVER 熔断
   (实现文件头成文);经典用例幅值远低于熔断线,两侧同码同串。

   运行:node test/test-groebner.js   (JS/128KB 目录下)
   全过输出:==== groebner: N pass, 0 fail ====
   注:不用 "use strict"——拼接体经 eval 装入(test-solvers.js 同款约定)。 */
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
/* C gb_run 同款 */
function checkGB(name, f, g, want) {
  var rc = ApolloGroebner.groebner(f, g);
  if (rc !== 1) { ok(name + "「<" + f + "," + g + ">」rc", false, "rc=" + rc); return; }
  var s = ApolloGroebner.getString();
  ok(name + "「<" + f + "," + g + ">」", s === want, "got=「" + s + "」 want=「" + want + "」");
}
/* C gb_check_reduced 同款 */
function checkReduced(name) {
  var count = ApolloGroebner.count();
  var i = 0;
  var j = 0;
  var q = 0;
  for (i = 0; i < count; i++) {
    var ti = ApolloGroebner.terms(i);
    var lmi = ApolloGroebner.exps(i, 0);
    ok(name + " elem" + i + " 首项系数为正", ApolloGroebner.coef(i, 0) > 0,
       "lc=" + ApolloGroebner.coef(i, 0));
    for (j = 0; j < count; j++) {
      if (j === i) { continue; }
      var lmj = ApolloGroebner.exps(j, 0);
      var divides = (lmi >> 4) >= (lmj >> 4) && (lmi & 15) >= (lmj & 15);
      ok(name + " lm(elem" + j + ") 不整除 lm(elem" + i + ")", !divides,
         "lmi=" + lmi + " lmj=" + lmj);
    }
    for (q = 1; q < ti; q++) {
      var eq = ApolloGroebner.exps(i, q);
      var div2 = 0;
      for (j = 0; j < count; j++) {
        if (j === i) { continue; }
        var lm2 = ApolloGroebner.exps(j, 0);
        if ((eq >> 4) >= (lm2 >> 4) && (eq & 15) >= (lm2 & 15)) { div2 = 1; }
      }
      ok(name + " elem" + i + " 尾项" + q + " 不被其它首项整除", !div2, "eq=" + eq);
    }
  }
}

/* 经典主干(C GB-1..GB-5,期望串逐字) */
checkGB("GB-1 任务书钦定", "x^2+y^2-1", "x^2-y", "x^2 - y | y^2 + y - 1");
checkReduced("GB-1");
checkGB("GB-2 深归约", "x^2+y^2-1", "x*y-1", "x + y^3 - y | y^4 - y^2 + 1");
checkReduced("GB-2");
checkGB("GB-3 线性对", "x+y", "x-y", "x | y");
checkGB("GB-4 无交互变元", "x^2-1", "y^2-1", "x^2 - 1 | y^2 - 1");
checkGB("GB-5 interreduce 主打", "x^2+x*y", "x*y+y^2", "x^2 - y^2 | x*y + y^2");

/* 退化与消融(C GB-6..GB-10) */
checkGB("GB-6 冗余生成元", "x^2-y", "2*x^2-2*y", "x^2 - y");
checkGB("GB-7 不相容→全环", "x^2+y^2-1", "x^2+y^2-2", "1");
checkGB("GB-8 单变量公约数", "x^2-1", "x^3-1", "x - 1");
checkGB("GB-9a 常数生成元", "5", "x^2-y", "1");
checkGB("GB-9b 零生成元", "0", "x^2-y", "x^2 - y");
checkGB("GB-10 曲线交点形", "x^2-y", "y^2-x", "x - y^2 | y^4 - y");

/* 访问器结构(C GB-11) */
(function () {
  var rc = ApolloGroebner.groebner("x^2+y^2-1", "x*y-1");
  ok("GB-11 rc == 1", rc === 1, "rc=" + rc);
  ok("GB-11 count == 2", ApolloGroebner.count() === 2, "count=" + ApolloGroebner.count());
  ok("GB-11 terms(0) == 3", ApolloGroebner.terms(0) === 3, "t=" + ApolloGroebner.terms(0));
  ok("GB-11 exps(0,0) == 0x10", ApolloGroebner.exps(0, 0) === 16,
     "e=" + ApolloGroebner.exps(0, 0));
  ok("GB-11 exps(0,1) == 0x03", ApolloGroebner.exps(0, 1) === 3,
     "e=" + ApolloGroebner.exps(0, 1));
  ok("GB-11 exps(0,2) == 0x01", ApolloGroebner.exps(0, 2) === 1,
     "e=" + ApolloGroebner.exps(0, 2));
  ok("GB-11 coefs(0) == 1,1,-1", ApolloGroebner.coef(0, 0) === 1 &&
     ApolloGroebner.coef(0, 1) === 1 && ApolloGroebner.coef(0, 2) === -1,
     "c=" + ApolloGroebner.coef(0, 0) + "," + ApolloGroebner.coef(0, 1) + "," + ApolloGroebner.coef(0, 2));
  ok("GB-11 exps(1) == 04,02,00", ApolloGroebner.exps(1, 0) === 4 &&
     ApolloGroebner.exps(1, 1) === 2 && ApolloGroebner.exps(1, 2) === 0, "");
  ok("GB-11 coefs(1) == 1,-1,1", ApolloGroebner.coef(1, 0) === 1 &&
     ApolloGroebner.coef(1, 1) === -1 && ApolloGroebner.coef(1, 2) === 1, "");
  ok("GB-11 越界访问器钝化", ApolloGroebner.coef(2, 0) === 0 &&
     ApolloGroebner.exps(2, 0) === -1 && ApolloGroebner.terms(-1) === -1 &&
     ApolloGroebner.count() === 2, "");
})();

/* 熔断与幂等(C GB-12) */
ok("GB-12 x^5 → EDEG(-2)", ApolloGroebner.groebner("x^5", "y") === -2, "");
ok("GB-12 x^3*y^3 → EDEG(-2)", ApolloGroebner.groebner("x^3*y^3", "y") === -2, "");
ok("GB-12 尾随垃圾 → EARG(-1)", ApolloGroebner.groebner("x^2;", "y") === -1, "");
ok("GB-12 项内重复变量 → EARG(-1)", ApolloGroebner.groebner("x*x", "y") === -1, "");
ok("GB-12 空输入 → EARG(-1)", ApolloGroebner.groebner("", "y") === -1, "");
ok("GB-12 非字符串 → EARG(-1)", ApolloGroebner.groebner(null, "y") === -1, "");
(function () {
  ApolloGroebner.groebner("x^2+y^2-1", "x*y-1");
  ApolloGroebner.groebner("x^2+y^2-1", "x^2-y");
  var rc = ApolloGroebner.groebner("x^2+y^2-1", "x*y-1");
  ok("GB-12 幂等:跨调用无残留", rc === 1 &&
     ApolloGroebner.getString() === "x + y^3 - y | y^4 - y^2 + 1",
     ApolloGroebner.getString());
})();
(function () {
  var rc = ApolloGroebner.groebner("0", "0");
  ok("GB-12 两零输入 → 空基 \"0\"", rc === 1 && ApolloGroebner.getString() === "0" &&
     ApolloGroebner.count() === 0, "out=" + ApolloGroebner.getString());
})();

/* 结式(C A3-1..A3-4 逐条镜像) */
(function () {
  /* A3-1:Res_y(y²−x, y−1) @x=4 = f(1) = 1−4 = −3 */
  ApolloGroebner.reset();
  var rc = ApolloGroebner.resultant("y^2 - x", "y - 1", 4.0);
  ok("A3-1 returns 1", rc === 1, "rc=" + rc);
  ok("A3-1 Res == -3", Math.abs(ApolloGroebner.getResultant() - (-3.0)) < 1e-10,
     "Res=" + ApolloGroebner.getResultant());
  /* A3-2:Sylvester 3×3 逐格 */
  var n = ApolloGroebner.buildSylvester("y^2 - x", "y - 1", 4.0);
  ok("A3-2 order == 3", n === 3, "n=" + n);
  ok("A3-2 row0 == [1,0,-4]", Math.abs(ApolloGroebner.getSylvester(0) - 1) < 1e-12 &&
     Math.abs(ApolloGroebner.getSylvester(1)) < 1e-12 &&
     Math.abs(ApolloGroebner.getSylvester(2) - (-4)) < 1e-12, "");
  ok("A3-2 row1 == [1,-1,0]", Math.abs(ApolloGroebner.getSylvester(3) - 1) < 1e-12 &&
     Math.abs(ApolloGroebner.getSylvester(4) - (-1)) < 1e-12 &&
     Math.abs(ApolloGroebner.getSylvester(5)) < 1e-12, "");
  ok("A3-2 row2 == [0,1,-1]", Math.abs(ApolloGroebner.getSylvester(6)) < 1e-12 &&
     Math.abs(ApolloGroebner.getSylvester(7) - 1) < 1e-12 &&
     Math.abs(ApolloGroebner.getSylvester(8) - (-1)) < 1e-12, "");
  /* A3-3:公共根 → Res = 0 */
  rc = ApolloGroebner.resultant("y^2 - x", "y^2 - 4", 4.0);
  ok("A3-3 公共根 Res == 0", rc === 1 && Math.abs(ApolloGroebner.getResultant()) < 1e-10,
     "Res=" + ApolloGroebner.getResultant());
  /* A3-4:阶数 13 > 12 → 拒绝,error=1,值不被改写 */
  ApolloGroebner.reset();
  ApolloGroebner.resultant("y^2 - x", "y - 1", 4.0);   /* 预置结果,验证不被改写 */
  rc = ApolloGroebner.resultant("y^7 + x", "y^6 + x", 1.0);
  ok("A3-4 阶数13 → 拒绝(rc=0)", rc === 0, "rc=" + rc);
  ok("A3-4 error == 1", ApolloGroebner.getResultantError() === 1,
     "err=" + ApolloGroebner.getResultantError());
  ok("A3-4 结果未被改写", Math.abs(ApolloGroebner.getResultant() - (-3.0)) < 1e-12,
     "val=" + ApolloGroebner.getResultant());
  /* 补充:Res(y²−1, y−2) @0 = f(2) = 3 */
  rc = ApolloGroebner.resultant("y^2 - 1", "y - 2", 0);
  ok("补充 Res(y²−1, y−2)@0 == 3", rc === 1 &&
     Math.abs(ApolloGroebner.getResultant() - 3.0) < 1e-10,
     "Res=" + ApolloGroebner.getResultant());
  /* 补充:解析失败 → rc=0, error=2 */
  rc = ApolloGroebner.resultant("y^2 +", "y - 1", 0);
  ok("补充 语法错 → rc=0/error=2", rc === 0 && ApolloGroebner.getResultantError() === 2, "");
  /* 补充:某方不含 y → rc=0, error=2 */
  rc = ApolloGroebner.resultant("x^2 - 1", "y - 1", 0);
  ok("补充 不含 y → rc=0/error=2", rc === 0 && ApolloGroebner.getResultantError() === 2, "");
})();

/* 会话与审计 */
(function () {
  ApolloGroebner.reset();
  ok("会话 1(reset 后串空)", ApolloGroebner.getString() === "", "");
  var rc = ApolloGroebner.groebner("x+y", "x-y");
  ok("会话 2(复位后可复用)", rc === 1 && ApolloGroebner.getString() === "x | y",
     ApolloGroebner.getString());
  ok("审计 allocCount=18", ApolloGroebner.allocCount() === 18,
     "alloc=" + ApolloGroebner.allocCount());
})();

console.log("==== groebner: " + pass + " pass, " + fail + " fail ====");
process.exit(fail > 0 ? 1 : 0);
