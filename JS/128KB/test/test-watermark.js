/* ApolloCalc · test-watermark.js — F0 全池水位门禁自检（feat/js-gate-f0）

   被测行为：checkMemoryWatermark（core_solvers.js）F0 全池口径改造——
   ①全池静态线 178496 B（170304 基线 + 8192 封顶，含动态游标显式计入）；
   ②堆线 98304 B（不变）；③模块池加载期自报机制（_modulePoolBytes，
   core_parser.js 声明 + 各池模块 typeof 守卫自报）；④账本完整性
   （tests/js-pool-ledger.json 逐文件 pools/bytes 与加载期实测一致）。

   方法：全 17 文件拼接 eval（run-all 环境等价），eval 期间以 TypedArray
   子类包装 Float64Array/Int32Array 记录每次分配的 byteLength 与 stack 行号，
   按拼接体行号归属到源文件，与账本 JSON 逐行比对；随后对真实门禁做
   「刚好通过/刚好拒绝」双侧边界（178496/178497、98304/98305）与懒分配 0 校验。
   测试侧包装/记录本身允许分配（测试期特权，不在被测零 GC 热路径内）。

   运行：node test/test-watermark.js   （JS/128KB 目录下；run-all 自动发现）
   全过输出：==== watermark: N pass, 0 fail ==== */

var fs = require("fs");
var path = require("path");

var wmDir = path.join(__dirname, "..", "src");
var wmLedgerPath = path.join(__dirname, "..", "..", "..", "tests", "js-pool-ledger.json");
var wmFiles = [
  "core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
  "core_evaluator.js", "core_calculus.js", "core_base.js", "core_solvers.js",
  "core_ratroot.js", "core_symboltree.js", "core_integrate.js", "core_multivar.js",
  "core_matrix.js", "core_special.js", "core_numtheory.js", "core_ode.js", "core_groebner.js"
];

var pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}

var wmLedger = JSON.parse(fs.readFileSync(wmLedgerPath, "utf8"));
var wmBodies = wmFiles.map(function (f) { return fs.readFileSync(path.join(wmDir, f), "utf8"); });
/* 拼接方式与 build-kernel.js 一致（\n join），stack 行号 1:1 映射回源文件 */
var wmLineMap = [];
wmBodies.forEach(function (b, i) {
  var n = b.split("\n").length, k = 0;
  for (k = 0; k < n; k++) { wmLineMap.push(wmFiles[i]); }
});
var wmBody = wmBodies.join("\n");

/* 分配记录器：byteLength + 行号归属（throw 必须在构造器体内，
 * 使 stack 帧 [2] 恰为内核分配点；经辅助函数会多一帧导致归属失效） */
var wmRec = [];
var wmRealF64 = Float64Array;
var wmRealI32 = Int32Array;
var wmRecF64 = class extends wmRealF64 {
  constructor() {
    super(...arguments);
    var line = -1;
    try { throw new Error("wm"); } catch (e) {
      var st = e.stack.split("\n")[2] || "";
      var m = st.match(/<anonymous>:(\d+):/);
      if (m) { line = +m[1]; }
    }
    wmRec.push([(line >= 1 && line <= wmLineMap.length) ? wmLineMap[line - 1] : "?", this.byteLength]);
  }
};
var wmRecI32 = class extends wmRealI32 {
  constructor() {
    super(...arguments);
    var line = -1;
    try { throw new Error("wm"); } catch (e) {
      var st = e.stack.split("\n")[2] || "";
      var m = st.match(/<anonymous>:(\d+):/);
      if (m) { line = +m[1]; }
    }
    wmRec.push([(line >= 1 && line <= wmLineMap.length) ? wmLineMap[line - 1] : "?", this.byteLength]);
  }
};
globalThis.Float64Array = wmRecF64;
globalThis.Int32Array = wmRecI32;
eval(wmBody);
globalThis.Float64Array = wmRealF64;
globalThis.Int32Array = wmRealI32;

/* ---- 实测汇总 ---- */
var wmPerB = {}, wmPerN = {}, wmTotal = 0, wmBad = 0, i;
for (i = 0; i < wmRec.length; i++) {
  var f = wmRec[i][0];
  if (f === "?") { wmBad++; continue; }
  wmPerB[f] = (wmPerB[f] || 0) + wmRec[i][1];
  wmPerN[f] = (wmPerN[f] || 0) + 1;
  wmTotal += wmRec[i][1];
}

ok("分配归属完整（无未归属行）", wmBad === 0, "unattributed=" + wmBad);
ok("实测总字节 = 账本基线", wmTotal === wmLedger.baseline.totalBytes,
   "measured=" + wmTotal + " ledger=" + wmLedger.baseline.totalBytes);
ok("实测分配组数 = 账本", wmRec.length === wmLedger.baseline.allocations,
   "measured=" + wmRec.length + " ledger=" + wmLedger.baseline.allocations);
for (i = 0; i < wmFiles.length; i++) {
  var fi = wmFiles[i];
  var ent = null;
  for (var k = 0; k < wmLedger.perFile.length; k++) {
    if (wmLedger.perFile[k].file === fi) { ent = wmLedger.perFile[k]; break; }
  }
  ok("逐文件账本一致 " + fi, ent !== null &&
     (wmPerB[fi] || 0) === ent.bytes && (wmPerN[fi] || 0) === ent.pools,
     "measured=" + (wmPerB[fi] || 0) + "B/" + (wmPerN[fi] || 0) + "池 ledger=" +
     (ent ? ent.bytes + "B/" + ent.pools + "池" : "缺行"));
}

/* ---- 门禁线算术与登记机制 ---- */
var wmGate = wmLedger.gate;
ok("门禁线推导闭合 174624+3872=178496（封顶固定，F2/F3 登记后剩余预算）",
   wmGate.staticLineBytes === wmLedger.baseline.totalBytes + 3872 &&
   wmGate.staticLineBytes === 178496,
   "staticLineBytes=" + wmGate.staticLineBytes);
ok("模块池自报合计 = 账本", _modulePoolBytes === wmGate.moduleRegisteredBytes,
   "_modulePoolBytes=" + _modulePoolBytes + " ledger=" + wmGate.moduleRegisteredBytes);
var wmBasicLive = (wmPerB["core_parser.js"] || 0) + (wmPerB["core_evaluator.js"] || 0) +
                  (wmPerB["core_base.js"] || 0) + (wmPerB["core_solvers.js"] || 0);
ok("基础池 live 合计 = 账本", wmBasicLive === wmGate.basicLiveBytes,
   "measured=" + wmBasicLive + " ledger=" + wmGate.basicLiveBytes);
ok("checkMemoryWatermark 在导出契约（typeof function）",
   typeof checkMemoryWatermark === "function");

/* ---- 门禁基线与全池静态线双侧边界 ---- */
tokenCount = 0; numberCount = 0;
ok("基线全池口径通过（idle → 0）", checkMemoryWatermark() === 0);
ok("通过时 _solveError = 0", _solveError === 0);

var wmKeepPool = _modulePoolBytes;
/* 门禁确实消费 _modulePoolBytes：置 0 后仍通过（仅基础池 + 游标） */
_modulePoolBytes = 0;
ok("门禁读取自报标量（置 0 仍通过）", checkMemoryWatermark() === 0);
_modulePoolBytes = wmKeepPool;

/* 刚好通过：reading == 178496（≤ 线） */
_modulePoolBytes = 178496 - wmBasicLive;
ok("刚好通过：全池读数 = 178496 → 0", checkMemoryWatermark() === 0 &&
   _solveError === 0);
/* 刚好拒绝：reading == 178497（> 线） */
_modulePoolBytes = 178496 - wmBasicLive + 1;
ok("刚好拒绝：全池读数 = 178497 → 1", checkMemoryWatermark() === 1);
ok("全池拒绝置 _solveError = 4", _solveError === 4);
_modulePoolBytes = wmKeepPool;
ok("恢复自报合计后回到通过", checkMemoryWatermark() === 0 && _solveError === 0);

/* ---- 堆线双侧边界（98304 / 98305，performance 打桩） ---- */
var wmPerfDesc = Object.getOwnPropertyDescriptor(globalThis, "performance");
var wmPerfActive = true;
function wmSetHeap(bytes) {
  Object.defineProperty(globalThis, "performance", {
    value: { memory: { usedJSHeapSize: bytes } },
    writable: true, configurable: true
  });
}
wmSetHeap(98304);
ok("堆线刚好通过：98304 → 0", checkMemoryWatermark() === 0 && _solveError === 0);
wmSetHeap(98305);
ok("堆线刚好拒绝：98305 → 1", checkMemoryWatermark() === 1 && _solveError === 4);
if (wmPerfDesc) { Object.defineProperty(globalThis, "performance", wmPerfDesc); }
else { delete globalThis.performance; }
wmPerfActive = false;
ok("堆打桩恢复后回到通过", checkMemoryWatermark() === 0);

/* ---- 懒分配 0（代表性工作负载，重挂记录器） ---- */
wmRec = [];
globalThis.Float64Array = wmRecF64;
globalThis.Int32Array = wmRecI32;
function wmStep(name, fn) { try { fn(); } catch (e) { console.log("NOTE workload " + name + ": " + e.message); } }
wmStep("solve", function () {
  parseExpression("x^4-10x^3+35x^2-50x+24=0"); extractCoefficients(4); solveQuartic(); solversReset();
});
wmStep("ratroot", function () {
  parseExpression("x^3-6x^2+11x-6=0"); extractCoefficients(3); solveCubic(); solversReset();
});
wmStep("symboltree", function () { ApolloSymboltree.build("x^3+x^2+x"); ApolloSymboltree.diff(); });
wmStep("integrate", function () { ApolloIntegrate.integrate("x*e^x"); });
wmStep("multivar", function () {
  ApolloMultivar.parse("3x^2y+2xy-5", 0); ApolloMultivar.evalSlot(0, 2, 3);
  ApolloMultivar.polyToString(0);
});
wmStep("matrix", function () {
  var j;
  for (j = 0; j < 9; j++) { ApolloMatrix.setA(3, 3, j, j + 1); }
  ApolloMatrix.det(); ApolloMatrix.inv();
});
wmStep("special", function () { ApolloSpecial.erf(1); ApolloSpecial.gamma(5); });
wmStep("numtheory", function () { ApolloNumtheory.isPrime(97); ApolloNumtheory.factorize(360); });
wmStep("ode", function () { ApolloOde.solveOde("y''+3y'+2y=0"); });
wmStep("gb", function () { ApolloGroebner.groebner("x^2+y^2-1", "x^2-y"); });
wmStep("sqfree", function () { ApolloSqfree.sqfree("x^3-2x^2+x"); });
globalThis.Float64Array = wmRealF64;
globalThis.Int32Array = wmRealI32;
ok("懒分配 0（工作负载前后分配组数不变）", wmRec.length === 0,
   "lazy=" + wmRec.length + (wmRec.length ? " 首例=" + wmRec[0][0] + " " + wmRec[0][1] + "B" : ""));

/* ---- 工作负载后（游标非零）门禁不误拒 ---- */
ok("求解工作负载后门禁通过（游标计入）", checkMemoryWatermark() === 0 && _solveError === 0);
tokenCount = 0; numberCount = 0;

console.log("==== watermark: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
