/* ============================================================
 * ApolloCalc-CAS · diff-smoke128.js — C 128KB vs JS 128KB 冒烟比对（第 11 轮）
 * 读 tests/bench/c128-smoke.csv 与 js128-smoke.csv（同一组用例、同一格式），
 * 按 stage 分派列、按 name 对齐逐行比对：
 *   solve:    err、count 精确相等；12 个数值 |差| ≤ 1e-9
 *   precheck: err 精确相等
 *   exact:    err、valid 与五分量（rat,rad_num,rad,den,exp）精确相等
 * 注意：精确形式为五分量（12 列），与 64KB 版 diff-smoke.js 的三分量（8 列）
 *   不同，故本脚本独立；不要与 diff-smoke.js 混用。
 * 生成 tests/bench/SMOKE-DIFF-128.md；全部通过退出码 0，否则 1。
 * 运行：node tests/bench/diff-smoke128.js
 * ============================================================ */
"use strict";
const fs = require("fs");
const path = require("path");

const DIR = __dirname;
const C_CSV = path.join(DIR, "c128-smoke.csv");
const J_CSV = path.join(DIR, "js128-smoke.csv");
const OUT = path.join(DIR, "SMOKE-DIFF-128.md");
const TOL = 1e-9;

function readRows(file) {
  const text = fs.readFileSync(file, "utf8");
  const rows = [];
  for (const line of text.split(/\r?\n/)) {
    if (line === "") { continue; }
    const parts = line.split(",");
    if (parts[1] === "name") { continue; }   /* 三段各自的表头行 */
    rows.push(parts);
  }
  return rows;
}

function key(row) { return row[0] + "/" + row[1]; }

/* 返回 [maxDiff, ok]：两侧均 NaN 视为一致 */
function cmpFloat(a, b) {
  const x = parseFloat(a);
  const y = parseFloat(b);
  if (Number.isNaN(x) && Number.isNaN(y)) { return [0, true]; }
  if (Number.isNaN(x) || Number.isNaN(y)) { return [Infinity, false]; }
  const d = Math.abs(x - y);
  return [d, d <= TOL];
}

const cRows = readRows(C_CSV);
const jRows = readRows(J_CSV);
const cMap = new Map(cRows.map((r) => [key(r), r]));
const jMap = new Map(jRows.map((r) => [key(r), r]));

const missing = [];
for (const k of cMap.keys()) { if (!jMap.has(k)) { missing.push("C-only: " + k); } }
for (const k of jMap.keys()) { if (!cMap.has(k)) { missing.push("JS-only: " + k); } }

const solveRows = [];   /* {name, jc, cc, maxDiff, ok} */
const preRows = [];     /* {name, je, ce, ok} */
const exactRows = [];   /* {name, jv, cv, ok} */

for (const r of cRows) {
  const stage = r[0];
  const name = r[1];
  const j = jMap.get(key(r));
  if (!j) { continue; }
  if (stage === "solve") {
    const je = parseInt(j[2], 10), ce = parseInt(r[2], 10);
    const jcnt = parseInt(j[3], 10), ccnt = parseInt(r[3], 10);
    let maxDiff = 0;
    let ok = (je === ce) && (jcnt === ccnt);
    for (let i = 0; i < 12; i++) {
      const [d, oki] = cmpFloat(j[4 + i], r[4 + i]);
      if (d > maxDiff) { maxDiff = d; }
      if (!oki) { ok = false; }
    }
    solveRows.push({ name, jc: jcnt, cc: ccnt, je, ce, maxDiff, ok });
  } else if (stage === "precheck") {
    const je = parseInt(j[2], 10), ce = parseInt(r[2], 10);
    preRows.push({ name, je, ce, ok: je === ce });
  } else if (stage === "exact") {
    const je = parseInt(j[2], 10), ce = parseInt(r[2], 10);
    let ok = (je === ce);
    /* 五分量：12 列（valid,rat,rad_num,rad,den,exp）× 2 槽 */
    for (let i = 0; i < 12; i++) {
      if (parseInt(j[3 + i], 10) !== parseInt(r[3 + i], 10)) { ok = false; }
    }
    exactRows.push({ name, jv: j[3] + "," + j[9], cv: r[3] + "," + r[9], ok });
  }
}

const allOk = missing.length === 0 &&
  solveRows.every((r) => r.ok) && preRows.every((r) => r.ok) && exactRows.every((r) => r.ok);
const total = solveRows.length + preRows.length + exactRows.length;
const passed = solveRows.filter((r) => r.ok).length +
  preRows.filter((r) => r.ok).length + exactRows.filter((r) => r.ok).length;
const overTol = solveRows.filter((r) => r.maxDiff > TOL).length;

function md(v) { return v ? "✅" : "❌"; }

let out = "# C 128KB vs JS 128KB 全量冒烟比对\n\n";
out += "- 用例来源：`tests/polynomials.json` / `tests/exact-forms.json` / `tests/edge-cases.json` 手工转录（50 例）\n";
out += "- C 侧：`C/128KB/test/test_smoke_csv.c` → `make smoke_runner` → `c128-smoke.csv`\n";
out += "- JS 侧：`tests/bench/js128-smoke.js`（动态 import `JS/128KB/src/core_kernel.js`，只读）→ `js128-smoke.csv`\n";
out += "- 数值容差 1e-9（绝对）；五分量精确形式与错误码为精确比对\n\n";
out += "## 求解用例（多项式 + 线性方程组）\n\n";
out += "| 用例 | JS 根数 | C 根数 | 最大差异 | 状态 |\n";
out += "|:---|---:|---:|---:|:---|\n";
for (const r of solveRows) {
  out += "| " + r.name + " | " + r.jc + " | " + r.cc + " | " +
    (r.maxDiff === 0 ? "0" : r.maxDiff.toExponential(2)) + " | " + md(r.ok) + " |\n";
}
out += "\n## 门禁用例（preCheck 拒绝码）\n\n";
out += "| 用例 | JS err | C err | 状态 |\n";
out += "|:---|---:|---:|:---|\n";
for (const r of preRows) {
  out += "| " + r.name + " | " + r.je + " | " + r.ce + " | " + md(r.ok) + " |\n";
}
out += "\n## 精确形式用例（五分量，根槽 0/1 的 valid 标志）\n\n";
out += "| 用例 | JS valid | C valid | 状态 |\n";
out += "|:---|:---|:---|:---|\n";
for (const r of exactRows) {
  out += "| " + r.name + " | " + r.jv + " | " + r.cv + " | " + md(r.ok) + " |\n";
}
if (missing.length > 0) {
  out += "\n## 对齐异常\n\n";
  for (const m of missing) { out += "- " + m + "\n"; }
}
out += "\n**统计**：" + passed + "/" + total + " 用例一致（含错误码与五分量精确比对），" +
  overTol + " 处数值差异 > 1e-9。\n\n";
out += allOk
  ? "**结论**：C 128KB 与 JS 128KB 在 50 个冒烟用例上行为完全一致。"
  : "**结论**：存在不一致，见上表 ❌ 行。";

fs.writeFileSync(OUT, out, "utf8");
console.log(out);
process.exit(allOk ? 0 : 1);
