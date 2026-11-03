/* ApolloCalc · diff-complex.js — F1 复变函数扩展 C/JS 差分对拍驱动（契约
 * docs/contracts/complex-ext-v25.md §9/§10；机制同 diff-smoke128.js）
 *
 * 语料：tests/bench/complex-corpus.csv（tools/gen-complex-corpus.js 生成，789 行）。
 * C 侧：以 gcc（-std=c99 -Wall -Wextra -Werror -O2 -DAC_PRO=1）把
 *   tests/bench/complex-probe.c + C/128KB-ProTEXT/src/ac_pro_complex_ext.c +
 *   C/128KB/src/ac_complex.c 链成临时探针（上游 C/128KB 源码零改动，只读编译），
 *   同语料逐行输出 id\tfn\trc\tre\tim。
 * JS 侧：按 test-complex.js 同款源码 eval（core_parser + core_complex），同语料
 *   逐行计算。
 * 判定（契约 §8）：错误码精确相等；值行 err = |a-b| / max(1,|a|,|b|) <= 1e-9
 *   （NaN==NaN 视为一致，同 diff-smoke128 约定；±0 不比符号）。
 * 契约锚行（id 命中 tests/complex-functions.json）：C 与 JS 各自再对 want 做
 *   三方校验（C↔JSON、JS↔JSON），防止两侧同错。
 * 产物：tests/bench/COMPLEX-DIFF.md 报告；全部一致退出码 0，否则 1。
 * 运行：node tests/bench/diff-complex.js */
"use strict";
const fs = require("fs");
const path = require("path");
const os = require("os");
const cp = require("child_process");

const ROOT = path.join(__dirname, "..", "..");
const CORPUS = path.join(__dirname, "complex-corpus.csv");
const CONTRACT = path.join(ROOT, "tests", "complex-functions.json");
const OUTMD = path.join(__dirname, "COMPLEX-DIFF.md");
const PROBE_C = path.join(__dirname, "complex-probe.c");
const EXT_C = path.join(ROOT, "C", "128KB-ProTEXT", "src", "ac_pro_complex_ext.c");
const CPLX_C = path.join(ROOT, "C", "128KB", "src", "ac_complex.c");
const TOL = 1e-9;

function readCorpus() {
  const text = fs.readFileSync(CORPUS, "utf8");
  const rows = [];
  for (const line of text.split(/\r?\n/)) {
    if (line === "" || line.startsWith("#")) { continue; }
    const p = line.split(",");
    rows.push({ id: p[0], fn: p[1], zre: parseFloat(p[2]), zim: parseFloat(p[3]),
                wre: parseFloat(p[4]), wim: parseFloat(p[5]), k: parseInt(p[6], 10) });
  }
  return rows;
}

/* ---- C 探针编译与运行 ---- */
function runC(rows) {
  const exe = path.join(os.tmpdir(), "cx_probe_" + process.pid + ".exe");
  const gcc = cp.spawnSync("gcc",
    ["-std=c99", "-Wall", "-Wextra", "-Werror", "-O2", "-DAC_PRO=1",
     "-o", exe, PROBE_C, EXT_C, CPLX_C, "-lm"],
    { encoding: "utf8" });
  if (gcc.error || gcc.status !== 0) {
    console.error("C probe compile failed:\n" + (gcc.stderr || String(gcc.error)));
    process.exit(2);
  }
  const run = cp.spawnSync(exe, [CORPUS], { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
  try { fs.unlinkSync(exe); } catch (e) { /* 临时文件，忽略 */ }
  if (run.error || run.status !== 0) {
    console.error("C probe run failed:\n" + (run.stderr || String(run.error)));
    process.exit(2);
  }
  const map = new Map();
  for (const line of run.stdout.split(/\r?\n/)) {
    if (line === "") { continue; }
    const p = line.split("\t");
    map.set(p[0], { rc: parseInt(p[2], 10), re: parseFloat(p[3]), im: parseFloat(p[4]) });
  }
  if (map.size !== rows.length) {
    console.error(`C probe rows ${map.size} != corpus ${rows.length}`);
    process.exit(2);
  }
  return map;
}

/* ---- JS 侧（源码间接 eval：sloppy 源码 var 声明落全局，寄存器经 globalThis 读写；
 * test-complex.js 的直接 eval 方案要求同作用域引用，驱动脚本以间接 eval 等价达成） ---- */
const JS_CODE = ["core_parser.js", "core_complex.js"]
  .map((f) => fs.readFileSync(path.join(ROOT, "JS", "128KB", "src", f), "utf8")).join("\n");
(0, eval)(JS_CODE);

function runJs(rows) {
  const ns = globalThis.ApolloComplexExt;
  const map = new Map();
  for (const r of rows) {
    globalThis._aRe = r.zre;
    globalThis._aIm = r.zim;
    globalThis._bRe = r.wre;
    globalThis._bIm = r.wim;
    let rc;
    if (r.fn === "cpow") { rc = ns.cpow(); }
    else if (r.fn === "cnthroot") { rc = ns.cnthroot(r.k); }
    else { rc = ns[r.fn](); }
    map.set(r.id, { rc: rc, re: globalThis._tmpRe, im: globalThis._tmpIm });
  }
  return map;
}

/* 值比较（契约 §8）：NaN==NaN 视为一致；±0 不比符号 */
function cmpVal(a, b) {
  if (Number.isNaN(a) && Number.isNaN(b)) { return 0; }
  if (Number.isNaN(a) || Number.isNaN(b)) { return Infinity; }
  return Math.abs(a - b) / Math.max(1, Math.abs(a), Math.abs(b));
}
function rowErr(x, y) { return Math.max(cmpVal(x.re, y.re), cmpVal(x.im, y.im)); }

const rows = readCorpus();
const want = new Map();
for (const c of JSON.parse(fs.readFileSync(CONTRACT, "utf8")).cases) {
  want.set(c.id, c);
}
const cMap = runC(rows);
const jMap = runJs(rows);

/* 极点邻域不变量判定（契约 §8 r3）：双方 rc=0、大分量 |v|>1e15 且同号、
 * 对侧分量按 §8 混合带相等。 */
function invariantOK(a, b) {
  if (a.rc !== 0 || b.rc !== 0) { return false; }
  const aL = Math.abs(a.re) > Math.abs(a.im) ? a.re : a.im;
  const bL = Math.abs(b.re) > Math.abs(b.im) ? b.re : b.im;
  if (!(Math.abs(aL) > 1e15) || !(Math.abs(bL) > 1e15)) { return false; }
  if ((aL > 0) !== (bL > 0)) { return false; }
  const aS = Math.abs(a.re) > Math.abs(a.im) ? a.im : a.re;
  const bS = Math.abs(b.re) > Math.abs(b.im) ? b.im : b.re;
  return cmpVal(aS, bS) <= TOL;
}
function isInvariantRow(id, wantRow) {
  if (wantRow && wantRow.band === "invariant") { return true; }
  return id.startsWith("pole-ctan-halfpi") || id.startsWith("pole-ctanh-halfpi");
}

let bad = 0;
let maxErr = 0;
let maxErrId = "";
let jsonBad = 0;
const badRows = [];
for (const r of rows) {
  const cv = cMap.get(r.id);
  const jv = jMap.get(r.id);
  const inv = isInvariantRow(r.id, want.get(r.id));
  let e = rowErr(cv, jv);
  /* 错误行 TMP 为非有限自然产物（契约 §6 不可信），只比错误码 */
  let ok = (cv.rc === jv.rc) && (cv.rc !== 0 || (inv ? invariantOK(cv, jv) : e <= TOL));
  if (!ok) {
    bad++;
    badRows.push(`${r.id} (${r.fn}): C rc=${cv.rc} (${cv.re}, ${cv.im}) | JS rc=${jv.rc} (${jv.re}, ${jv.im}) err=${e}`);
  }
  if (cv.rc === 0 && jv.rc === 0 && !inv && e > maxErr) { maxErr = e; maxErrId = r.id; }
  const w = want.get(r.id);
  if (w) {
    if (w.want.err !== undefined) {
      if (cv.rc !== w.want.err || jv.rc !== w.want.err) {
        jsonBad++;
        badRows.push(`${r.id}: JSON want err=${w.want.err} | C rc=${cv.rc} | JS rc=${jv.rc}`);
        bad++;
      }
    } else if (w.band === "invariant") {
      if (!invariantOK(cv, jv)) {
        jsonBad++;
        badRows.push(`${r.id}: invariant check failed | C (${cv.re}, ${cv.im}) | JS (${jv.re}, ${jv.im})`);
        bad++;
      }
    } else {
      const cE = Math.max(cmpVal(cv.re, w.want.re), cmpVal(cv.im, w.want.im));
      const jE = Math.max(cmpVal(jv.re, w.want.re), cmpVal(jv.im, w.want.im));
      if (cv.rc !== 0 || jv.rc !== 0 || cE > TOL || jE > TOL) {
        jsonBad++;
        badRows.push(`${r.id}: JSON want (${w.want.re}, ${w.want.im}) | C err=${cE} rc=${cv.rc} | JS err=${jE} rc=${jv.rc}`);
        bad++;
      }
    }
  }
}

const lines = [];
lines.push("# COMPLEX-DIFF — F1 复变函数扩展 C/JS 差分报告");
lines.push("");
lines.push(`- 语料：complex-corpus.csv ${rows.length} 行（含契约锚 ${want.size} 行）`);
lines.push(`- 判定：错误码精确（错误行 TMP 不比，契约 §6）+ err = |a-b|/max(1,|a|,|b|) <= 1e-9（NaN==NaN、±0 不比符号）；极点邻域行按 §8 r3 不变量判定`);
lines.push(`- C/JS 行间最大 err（值行，不含不变量行）：${maxErr.toExponential(3)}（@${maxErrId}）`);
lines.push(`- 不一致行：${bad}（其中三方校验失败 ${jsonBad}）`);
lines.push("");
lines.push(`RESULT: ${bad === 0 ? "PASS (all rows identical within contract band)" : "FAIL"}`);
lines.push("");
if (badRows.length > 0) {
  lines.push("## 不一致明细（前 50 行）");
  for (const b of badRows.slice(0, 50)) { lines.push("- " + b); }
}
fs.writeFileSync(OUTMD, lines.join("\n") + "\n", "utf8");
console.log(lines.slice(1, 6).join("\n"));
console.log(bad === 0 ? "COMPLEX DIFF OK, exit 0" : "COMPLEX DIFF FAILED");
process.exit(bad === 0 ? 0 : 1);
