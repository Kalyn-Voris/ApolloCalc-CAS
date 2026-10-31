/* ApolloCalc-CAS · JS 端 100 万次压测脚本
 * 用法：  node --expose-gc bench-js.js <kernel.js> [--warmup=1000] [--runs=3]
 * 例：    node --expose-gc bench-js.js ../../JS/64KB/src/core_kernel.js
 * 输出：  CSV，列名固定 stage,name,iterations,total_ms,us_per_op（跨版本 diff 用）
 *         尾部 SUMMARY 行 = 全 stage 汇总；MEM 行 = 每个 stage 之后各测一次堆占用
 * 依赖：  仅 Node 内置 API；内核为 ESM，动态 import 加载（Windows 须走 pathToFileURL）
 *
 * 池：    pool.json（650 条，由 gen_pool.py 一次性生成，运行时只读不拼接）
 *         条目可为裸字符串（parse 池通用）或对象 {src, x, degree, angleMode, versions}
 *         versions 缺省 = 四版本通用；["128KB"] = 仅 128KB 两版（双曲函数等）
 *
 * 迭代：  每 stage 独立 100 万次（pool.iterations），按池大小摊到每条的循环遍数：
 *         loopsPerItem = ceil(totalIter / poolSize)，actualIter = loopsPerItem * poolSize
 *         因此实际迭代数会略高于 100 万（整遍对齐），CSV 的 iterations 列是真实值
 */

var fs = require("fs");
var path = require("path");
var url = require("url");

/* 加载内核 */
var kernelPath = process.argv[2];
if (!kernelPath) {
  console.error("usage: node bench-js.js <kernel.js> [--warmup=1000] [--runs=3]");
  process.exit(1);
}

/* 解析参数 */
var warmup = 1000;
var runs = 3;
process.argv.slice(3).forEach(function (a) {
  if (a.indexOf("--warmup=") === 0) { warmup = parseInt(a.slice(9), 10); }
  if (a.indexOf("--runs=") === 0) { runs = parseInt(a.slice(7), 10); }
});

/* 版本标签：由内核路径判定，决定 pool 的 versions 门控 */
function versionTagOf(p) {
  if (p.indexOf("128KB") >= 0) { return "128KB"; }
  if (p.indexOf("64KB") >= 0) { return "64KB"; }
  return "64KB";   /* 未知路径按最小能力集处理，避免误选 128KB 专属条目 */
}

/* 池过滤：裸字符串恒通用；对象按 versions 门控 */
function filterPool(items, versionTag) {
  return items.filter(function (item) {
    if (typeof item === "string") { return true; }
    if (!item.versions) { return true; }
    return item.versions.indexOf(versionTag) >= 0;
  });
}

function memUsedMB() {
  return process.memoryUsage().heapUsed / 1024 / 1024;
}

function gcNow() {
  if (global.gc) { global.gc(); }
}

(async function main() {
  var mod = await import(url.pathToFileURL(path.resolve(kernelPath)).href);
  var k = mod.default || mod;
  if (typeof k.parseExpression !== "function") {
    console.error("error: " + kernelPath + " 未通过 default export 暴露内核（缺 parseExpression）");
    process.exit(1);
  }

  var versionTag = versionTagOf(kernelPath);

  /* 加载池 */
  var pool = JSON.parse(fs.readFileSync(__dirname + "/pool.json", "utf8"));
  var iterBudget = pool.iterations;

  /* 静态池占用（如果内核暴露了接口就输出，否则跳过） */
  if (typeof k.getStaticPoolBytes === "function") {
    console.log("STATIC,total_bytes," + k.getStaticPoolBytes());
  }

  /* CSV 头（列名严格固定） */
  console.log("stage,name,iterations,total_ms,us_per_op");

  /* 汇总累加器 */
  var sumOps = 0;
  var sumMs = 0;

  /* 池式迭代：遍历 items 凑到 totalIter，runs 轮取中位数 */
  function benchPool(stage, items, totalIter, perIterFn) {
    var poolSize = items.length;
    if (poolSize === 0) {
      console.error("warn: " + stage + " 池在 " + versionTag + " 下为空，跳过");
      return;
    }
    var loopsPerItem = Math.ceil(totalIter / poolSize);
    var actualIter = loopsPerItem * poolSize;

    /* 预热：按池顺序循环，覆盖全部条目 */
    var w;
    for (w = 0; w < warmup; w++) { perIterFn(items[w % poolSize]); }

    var times = [];
    for (var r = 0; r < runs; r++) {
      var t0 = process.hrtime.bigint();
      for (var l = 0; l < loopsPerItem; l++) {
        for (var i = 0; i < poolSize; i++) {
          perIterFn(items[i]);
        }
      }
      var t1 = process.hrtime.bigint();
      times.push(Number(t1 - t0) / 1e6);
    }
    times.sort(function (a, b) { return a - b; });
    var med = times[Math.floor(times.length / 2)];

    sumOps += actualIter;
    sumMs += med;

    console.log(stage + ",pool" + poolSize + "," + actualIter + "," +
                med.toFixed(3) + "," + (med / actualIter * 1000).toFixed(4));

    /* 每个 stage 之后各测一次堆占用（避免跨 stage 累积污染） */
    gcNow();
    console.log("MEM,after_" + stage + ",heap_MB," + memUsedMB().toFixed(3));
  }

  /* 求解器调用签名适配
   * JS128 的 solveCubic(skipRational) 带参、JS64 无参（跨版本分叉）。
   * 在环外一次性解析，避免每轮迭代多做一次分支判断。 */
  var cubicFn = (k.solveCubic.length > 0)
    ? function () { k.solveCubic(0); }
    : function () { k.solveCubic(); };

  /* parse */
  gcNow();
  console.log("MEM,baseline,heap_MB," + memUsedMB().toFixed(3));
  var parseItems = filterPool(pool.parse, versionTag);
  benchPool("parse", parseItems, iterBudget.parse, function (item) {
    k.parseExpression(typeof item === "string" ? item : item.src);
  });

  /* eval
   * 池内每条表达式不同 → 环内必须重设 angleMode + parse，再求值。
   * 因此 eval 段测得的是「解析 + 求值」的合计成本（与旧 cases.json 版
   * 只在环内测 evalExpressionAt 不同，见 BENCH-1M.md 口径说明）。 */
  var evalItems = filterPool(pool.eval, versionTag);
  benchPool("eval", evalItems, iterBudget.eval, function (item) {
    k.setAngleMode(item.angleMode || "DEG");
    k.parseExpression(item.src);
    k.evalExpressionAt(item.x);
  });

  /* solve
   * 规范链路（与 test-solvers / test_smoke_csv 一致）：
   *   solversReset → parseExpression → extractCoefficients → solveXxx */
  function solveOnce(item) {
    k.solversReset();
    k.parseExpression(item.src);
    k.extractCoefficients(item.degree);
    if (item.degree === 2) { k.solveQuadratic(); }
    else if (item.degree === 3) { cubicFn(); }
    else if (item.degree === 4) { k.solveQuartic(); }
  }

  var solveItems = filterPool(pool.solve, versionTag);
  benchPool("solve", solveItems, iterBudget.solve, solveOnce);

  /* exact
   * 只测耗时（不校验精确形式），池偏向精确形式路径：
   * 纯分数根 / 纯根式根 / 五分量混合形式。 */
  var exactItems = filterPool(pool.exact, versionTag);
  benchPool("exact", exactItems, iterBudget.exact, solveOnce);

  /* 汇总行 */
  console.log("SUMMARY,total_ops," + sumOps + ",total_ms," + sumMs.toFixed(3) +
              ",avg_us_per_op," + (sumMs / sumOps * 1000).toFixed(4));

  /* 运行时堆增量：10 万次 parse 前后各 gc 一次再测 heapUsed */
  gcNow();
  var memBefore = memUsedMB();
  for (var i = 0; i < 100000; i++) { k.parseExpression("x^2-5x+6=0"); }
  gcNow();
  var memAfter = memUsedMB();
  console.log("MEM,,gc_enabled," + (global.gc ? 1 : 0));
  console.log("MEM,,heap_before_MB," + memBefore.toFixed(3));
  console.log("MEM,,heap_after_MB," + memAfter.toFixed(3));
  console.log("MEM,,delta_KB," + ((memAfter - memBefore) * 1024).toFixed(2));
})().catch(function (e) { console.error(e); process.exit(1); });
