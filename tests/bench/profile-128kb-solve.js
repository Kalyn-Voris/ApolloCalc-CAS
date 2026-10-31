/* ApolloCalc-CAS · 128KB solve 慢 1.5-2.2x 根因剖析
 * 用法：node tests/bench/profile-128kb-solve.js
 * 性质：诊断工具，非正式测试（不改内核，只读）
 * 方法：每项 setup（solversReset + parseExpression）在计时环外；
 *       solve 计时环内逐轮 extractCoefficients 恢复系数 —— solveQuartic 会
 *       原地改写 _coeffs（降次 + 预解式，core_solvers.js:323/358），必须恢复，
 *       extract 开销两版完全一致，不污染比值。
 * 输出：每项 µs/op = 7 轮中位数；128KB 全项 + 64KB 同项对照 + 比值。
 * 判读：
 *   c_rat vs c_skip（同为 x^3-6x^2+11x-6）      → ratRoot 钩子成本
 *   c_skip vs 64KB c_irr（同为 x^3-2x-5）       → 三次核心算法基线差
 *   q_irr vs q_cpx                              → 五分量精确形式成本
 *   q_* / c_* 各项 128:64 比值                  → 归因到具体路径
 */

var path = require("path");
var url = require("url");

var WARMUP = 2000;
var ROUNDS = 7;

function median(arr) {
  var s = arr.slice().sort(function (a, b) { return a - b; });
  return s[Math.floor(s.length / 2)];
}

/* 用例表：name, 表达式, 次数, solve 入参, 计时环迭代数 */
var ITEMS = [
  { name: "reset      (solversReset only)",      poly: null,                 deg: 0, args: [],    iter: 20000 },
  { name: "extract    (extractCoefficients(2))", poly: "x^2-5x+6=0",         deg: 2, args: [],    iter: 20000, extractOnly: true },
  { name: "quad  x^2-5x+6=0   (有理精确根)",      poly: "x^2-5x+6=0",         deg: 2, args: [],    iter: 20000 },
  { name: "quad  x^2-2=0      (无理精确根)",      poly: "x^2-2=0",            deg: 2, args: [],    iter: 20000 },
  { name: "quad  x^2+3x+7=0   (复根,无精确式)",   poly: "x^2+3x+7=0",         deg: 2, args: [],    iter: 20000 },
  { name: "cubic x^3-6x^2+11x-6 (有理根)",        poly: "x^3-6x^2+11x-6=0",   deg: 3, args: [],    iter: 10000 },
  { name: "cubic x^3-2x-5=0   (无有理根)",        poly: "x^3-2x-5=0",         deg: 3, args: [],    iter: 10000 },
  { name: "cubic skipRational=1 (同上,跳钩子)",    poly: "x^3-2x-5=0",         deg: 3, args: [1],   iter: 10000, only128: true },
  { name: "quart  x^4-5x^2+4=0 (有理根 ±1±2)",    poly: "x^4-5x^2+4=0",       deg: 4, args: [],    iter: 10000 }
];

function timeIt(fn, n) {
  var t0 = process.hrtime.bigint();
  for (var i = 0; i < n; i++) { fn(); }
  var t1 = process.hrtime.bigint();
  return Number(t1 - t0) / 1e6;   /* ms */
}

/* 对一个内核测一项，返回 µs/op（中位数） */
function measure(k, item) {
  if (item.poly !== null) {
    k.solversReset();
    k.parseExpression(item.poly);
  }
  /* reset 项：op 即 solversReset；extract 项：op 即 extractCoefficients；
   * solve 项：每轮先 extractCoefficients 恢复 _coeffs（quartic 会原地改写）再 solve */
  function op() {
    if (item.name.indexOf("reset") === 0) { k.solversReset(); return; }
    if (item.extractOnly) { k.extractCoefficients(item.deg); return; }
    k.extractCoefficients(item.deg);
    if (item.args.length > 0) { k.solveCubic(item.args[0]); }
    else if (item.deg === 2) { k.solveQuadratic(); }
    else if (item.deg === 3) { k.solveCubic(); }
    else if (item.deg === 4) { k.solveQuartic(); }
  }
  var i;
  for (i = 0; i < WARMUP; i++) { op(); }
  var times = [];
  for (i = 0; i < ROUNDS; i++) { times.push(timeIt(op, item.iter)); }
  var med = median(times);
  return med / item.iter * 1000;   /* µs per op */
}

/* 正确性哨兵：确认计时路径真的是在解这个方程（防重复求解退化） */
function sanity(k, tag, poly, deg) {
  k.solversReset();
  k.parseExpression(poly);
  k.extractCoefficients(deg);
  if (deg === 2) { k.solveQuadratic(); }
  else if (deg === 3) { k.solveCubic(); }
  else if (deg === 4) { k.solveQuartic(); }
  console.log("  [check " + tag + "] " + poly + "  roots=" + k.getRootCount() + " err=" + k.getSolveError() +
    " r0=" + (k.getRootCount() > 0 ? k.getRootRe(0).toPrecision(8) : "-"));
}

(async function main() {
  var kernels = {};
  var order = ["128KB", "64KB"];
  for (var oi = 0; oi < order.length; oi++) {
    var p = path.join(__dirname, "..", "..", "JS", order[oi], "src", "core_kernel.js");
    var mod = await import(url.pathToFileURL(p).href);
    kernels[order[oi]] = mod.default || mod;
  }

  console.log("== solve 路径微剖析  (µs/op, " + ROUNDS + " 轮中位数, 预热 " + WARMUP + ") ==\n");

  var k128 = kernels["128KB"];
  var k64 = kernels["64KB"];

  var res = {};
  for (var i = 0; i < ITEMS.length; i++) {
    var item = ITEMS[i];
    if (i === ITEMS.length - 1) {
      console.log("-- 正确性哨兵（确认计时的是真解路径） --");
      sanity(k128, "128", "x^2-5x+6=0", 2);
      sanity(k128, "128", "x^3-6x^2+11x-6=0", 3);
      sanity(k128, "128", "x^4-5x^2+4=0", 4);
      sanity(k64, "64", "x^4-5x^2+4=0", 4);
      console.log("");
    }
    res[item.name] = { u128: measure(k128, item) };
    if (!item.only128) { res[item.name].u64 = measure(k64, item); }
  }

  console.log("item                                       128KB µs/op    64KB µs/op    128/64");
  for (var i = 0; i < ITEMS.length; i++) {
    var item = ITEMS[i];
    var r = res[item.name];
    var line = item.name.padEnd(42) + r.u128.toFixed(3).padStart(10);
    if (r.u64 !== undefined) {
      line += r.u64.toFixed(3).padStart(14) + (r.u128 / r.u64).toFixed(2).padStart(10);
    } else {
      line += "       (only)";
    }
    console.log(line);
  }
})().catch(function (e) { console.error(e); process.exit(1); });
