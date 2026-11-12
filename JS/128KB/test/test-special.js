/* ApolloCalc · test-special.js — 高阶特殊函数测试

   被测模块：src/core_special.js（自包含，不依赖既有 core）
   运行：node test/test-special.js   （JS/128KB 目录下）
   全过输出：==== special: N pass, 0 fail ====

   期望值口径：erf/gamma/lgamma 的真值取自标准数学常数表；
   erf 容差 2e-7（A&S 26.2.17 的 1.5e-7 上限留余量），
   gamma/lgamma 容差按 Lanczos g=7 实测精度（~1e-13 相对）放宽到 1e-9。 */
/* 加载器（svd-format-v26.md §10 再授权）：单文件独立 eval——不装其余 core，
   被测模块意外引用其他 core 全局即 ReferenceError（自包含金丝雀保留）。
   不用 "use strict"，严格模式会把 eval 内 var 困在 eval 作用域 */
var fs = require("fs");
eval(fs.readFileSync(__dirname + "/../src/core_special.js", "utf8"));
var S = ApolloSpecial;

var pass = 0, fail = 0;
function approx(a, b, tol) {
  if (a === b) { return true; }
  if (typeof a !== "number" || typeof b !== "number") { return false; }
  if (a !== a || b !== b) { return false; }              /* NaN 不相等 */
  if (!isFinite(a) || !isFinite(b)) { return a === b; }
  return Math.abs(a - b) <= (tol === undefined ? 1e-9 : tol);
}
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}

/* erf（A&S 26.2.17，容差 2e-7） */
/* x=0 处 A&S 多项式 P(1) = 0.999999999，固有偏移 ~1e-9，在 1.5e-7 界内 */
ok("erf(0) ≈ 0（偏移 <1.5e-7）", approx(S.erf(0), 0, 2e-7), S.erf(0));
ok("erf(0.5) ≈ 0.5204998778", approx(S.erf(0.5), 0.5204998778130465, 2e-7), S.erf(0.5));
ok("erf(1) ≈ 0.8427007929", approx(S.erf(1), 0.8427007929497149, 2e-7), S.erf(1));
ok("erf(2) ≈ 0.9953222650", approx(S.erf(2), 0.9953222650189527, 2e-7), S.erf(2));
ok("erf(3) ≈ 0.9999779095", approx(S.erf(3), 0.9999779095030014, 2e-7), S.erf(3));
ok("erf(6) 饱和到 1", approx(S.erf(6), 1, 2e-7), S.erf(6));
ok("erf 奇对称 erf(-1) = -erf(1)", S.erf(-1) === -S.erf(1));
ok("erf(-6) 饱和到 -1", approx(S.erf(-6), -1, 2e-7), S.erf(-6));
ok("erf(+Inf) = 1", S.erf(Infinity) === 1);
ok("erf(-Inf) = -1", S.erf(-Infinity) === -1);
ok("erf(NaN) = NaN + err 1", S.erf(NaN) !== S.erf(NaN) && S.getError() === 1);

/* gamma（Lanczos g=7；容差 1e-9） */
ok("gamma(1) = 1", approx(S.gamma(1), 1, 1e-12), S.gamma(1));
ok("gamma(2) = 1", approx(S.gamma(2), 1, 1e-12), S.gamma(2));
ok("gamma(5) = 24", approx(S.gamma(5), 24, 1e-9), S.gamma(5));
ok("gamma(10) = 362880", approx(S.gamma(10), 362880, 1e-4), S.gamma(10));
ok("gamma(0.5) = √π", approx(S.gamma(0.5), 1.7724538509055159, 1e-12), S.gamma(0.5));
ok("gamma(-0.5) = -2√π", approx(S.gamma(-0.5), -3.5449077018110318, 1e-12), S.gamma(-0.5));
ok("gamma(0) = +Inf 极点", S.gamma(0) === Infinity);
ok("gamma(-2) = +Inf 极点", S.gamma(-2) === Infinity);
ok("gamma(NaN) = NaN + err 1", S.gamma(NaN) !== S.gamma(NaN) && S.getError() === 1);

/* 递推 Γ(z+1) = z·Γ(z)：双方都过 Lanczos 核，残差应达 1e-11 量级 */
var g45 = S.gamma(4.5);
var g35 = S.gamma(3.5);
ok("递推 gamma(4.5) = 3.5·gamma(3.5)", approx(g45, 3.5 * g35, 1e-11),
   g45 + " vs " + 3.5 * g35);
ok("gamma(4.5) 真值 11.631728396567448", approx(g45, 11.631728396567448, 1e-9), g45);

/* lgamma */
ok("lgamma(5) = ln 24", approx(S.lgamma(5), 3.1780538303479458, 1e-12), S.lgamma(5));
ok("lgamma(0.5) = ln √π", approx(S.lgamma(0.5), 0.5723649429247001, 1e-12), S.lgamma(0.5));
ok("lgamma(-0.5) = ln(2√π)", approx(S.lgamma(-0.5), 1.2655121234846454, 1e-12), S.lgamma(-0.5));
ok("lgamma(100) ≈ 359.1342053695754", approx(S.lgamma(100), 359.1342053695754, 1e-9), S.lgamma(100));
ok("lgamma(0) = +Inf 极点", S.lgamma(0) === Infinity);
ok("lgamma(-3) = +Inf 极点", S.lgamma(-3) === Infinity);
ok("lgamma(NaN) = NaN + err 1", S.lgamma(NaN) !== S.lgamma(NaN) && S.getError() === 1);
ok("lgamma 与 gamma 对数一致",
   approx(S.lgamma(2.5), Math.log(S.gamma(2.5)), 1e-12), S.lgamma(2.5));

/* 零 GC：无池模块，加载期与热循环分配均为 0 */
ok("加载期分配 = 0（纯标量函数）", S.allocCount() === 0, S.allocCount());
var before = S.allocCount();
var i, acc = 0;
for (i = 0; i < 10000; i++) {
  acc = acc + S.erf(i % 7) + S.gamma(0.5 + (i % 13)) + S.lgamma(1 + (i % 11));
}
ok("热循环零分配", S.allocCount() === before,
   "delta=" + (S.allocCount() - before));
ok("热循环结果非平凡（未塌缩成单一值）", acc !== 0 && acc === acc);

console.log("==== special: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
