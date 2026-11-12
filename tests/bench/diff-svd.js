/* ApolloCalc · diff-svd.js — C 128KB vs JS 128KB 奇异值分解对拍（v2.7.0 G2）

   读 tests/bench/svd-corpus.csv（输入矩阵，tools/gen-svd-corpus.js 种子 20261104
   生成，52 anchor + 183 gen）与 tests/bench/svd128-c.csv（C 探针 svd_runner 导出
   金标准，随探针提交），按 id 对齐驱动 JS 内核（动态 import JS/128KB/src/
   core_kernel.js 的 matrix.proSvd），逐例比对（契约 docs/contracts/svd-format-v26.md
   §5/§6；diff-eigen.js 同款流程）：
     rc/count:      精确相等（8 个拒绝用例双侧一致 -1；域内 rc ∉ {-2,-3} 反向断言）
     w:             混合误差 err = |a-b| / max(1,|b|) ≤ tol（per-case，缺省 1e-9）；
                    逐位一致（===）另计统计（探针 %.17g 往返应逐位相同）
     结构不变式:    探针内独立重算（不信任被测代码闸门）：‖UᵗU−I‖∞ ≤ 1e-9、
                    ‖VᵗV−I‖∞ ≤ 1e-9、重构 max|A−UΣVᵗ| ≤ 1e-9·max(1,‖A‖∞)、
                    符号规范化（U 每列首个最大 |·| 分量 > 0）、w 降序
     uv/expect:     uvCompare=true 的 anchor：JS U/V 与 C 金标准 U/V（行主序，
                    比对时转置为列访问）逐分量 ≤ tol；expectW/expectU/expectV
                    对 C 与 JS 双侧自证（防「同错不异」）
   生成 tests/bench/SVD-DIFF.md；全部通过退出码 0，否则 1。
   运行：node tests/bench/diff-svd.js   （需先 node JS/128KB/test/build-kernel.js --minify 重建内核） */
"use strict";
const fs = require("fs");
const path = require("path");
const { pathToFileURL } = require("url");

const DIR = __dirname;
const ROOT = path.join(DIR, "..", "..");
const CORPUS = path.join(DIR, "svd-corpus.csv");
const GOLD = path.join(DIR, "svd128-c.csv");
const CASES = path.join(ROOT, "tests", "svd-cases.json");
const KERNEL = path.join(ROOT, "JS", "128KB", "src", "core_kernel.js");
const OUT = path.join(DIR, "SVD-DIFF.md");

function readCsvRows(file) {
  const text = fs.readFileSync(file, "utf8");
  const rows = [];
  for (const line of text.split(/\r?\n/)) {
    if (line === "") { continue; }
    rows.push(line.split(","));
  }
  return rows;
}

/* 非有限编码（契约 §5/§6：JSON 用 "NaN"/"Infinity"/"-Infinity"，语料 CSV 用
 * "nan"/"inf"/"-inf"；C strtod 同值） */
function parseVal(s) {
  if (s === "nan" || s === "NaN") { return NaN; }
  if (s === "inf" || s === "Infinity") { return Infinity; }
  if (s === "-inf" || s === "-Infinity") { return -Infinity; }
  return parseFloat(s);
}

/* ---- 语料：id → {m, n, kind, tol, expect, mat} ---- */
function collectCorpus(rows) {
  const map = new Map();
  for (const r of rows) {
    const id = r[0];
    const m = parseInt(r[1], 10), n = parseInt(r[2], 10);
    const kind = r[3];
    const tol = r[4] === "" ? 1e-9 : parseFloat(r[4]);
    const expect = r[5];
    const mat = [];
    for (let i = 6; i < r.length; i++) { mat.push(parseVal(r[i])); }
    map.set(id, { m, n, kind, tol, expect, mat });
  }
  return map;
}

/* ---- C 金标准：id → {rc, count, w, U(行主序 m×k), V(行主序 n×k)} ---- */
function collectGold(rows, corpus) {
  const map = new Map();
  for (const r of rows) {
    if (r[0] !== "svd") { continue; }
    const id = r[1];
    const rc = parseInt(r[2], 10);
    const e = { rc, count: 0, w: [], U: [], V: [] };
    if (rc === 1) {
      const k = parseInt(r[3], 10);
      const ce = corpus.get(id);
      e.count = k;
      e.w = r.slice(4, 4 + k).map(parseFloat);
      e.U = r.slice(4 + k, 4 + k + ce.m * k).map(parseFloat);
      e.V = r.slice(4 + k + ce.m * k, 4 + k + ce.m * k + ce.n * k).map(parseFloat);
      if (e.w.length !== k || e.U.length !== ce.m * k || e.V.length !== ce.n * k) {
        throw new Error("gold 行字段数不符: " + id);
      }
    }
    map.set(id, e);
  }
  return map;
}

/* 混合误差口径（契约 §5）：err = |a−b| / max(1,|b|)。全部数值比对（C↔JS 与
 * expect 自证）统一用此口径——大尺度谱（2^±40 等）闭式期望与计算值的绝对差
 * 可达末位量级，混合口径下 ≈ 相对误差 ≪ tol */
function mixErr(a, b) {
  return Math.abs(a - b) / Math.max(1, Math.abs(b));
}

function worstErr(got, want) {
  let w = 0;
  for (let i = 0; i < want.length; i++) {
    const e = mixErr(got[i], want[i]);
    if (e > w) { w = e; }
  }
  return w;
}

/* 符号规范化不变式（契约 §4）：U 每列首个最大 |·| 分量 > 0（U 经 getter 列访问） */
function normInvariantU(M, k, m) {
  for (let j = 0; j < k; j++) {
    let big = -1, im = -1;
    for (let i = 0; i < m; i++) {
      const a = Math.abs(M.proSvdU(j, i));
      if (a > big) { big = a; im = i; }
    }
    if (!(M.proSvdU(j, im) > 0)) { return false; }
  }
  return true;
}

async function main() {
  const doc = JSON.parse(fs.readFileSync(CASES, "utf8"));
  const anchorById = new Map();
  for (const c of doc.cases) { anchorById.set(c.id, c); }
  const corpus = collectCorpus(readCsvRows(CORPUS));
  const gold = collectGold(readCsvRows(GOLD), corpus);
  const kernel = await import(pathToFileURL(KERNEL).href);
  const M = kernel.default.matrix;
  if (!M || typeof M.proSvd !== "function") {
    console.log("FAIL: kernel.matrix.proSvd 不可用（请先 node JS/128KB/test/build-kernel.js --minify 重建内核）");
    process.exit(1);
  }

  const problems = [];
  const reportRows = [];
  let compared = 0, bitEqualW = 0, wRows = 0, uvRows = 0;

  for (const [id, ce] of corpus) {
    const ge = gold.get(id);
    if (!ge) { problems.push(id + ": C 金标准缺该用例"); continue; }
    const tol = ce.tol;
    const anchor = anchorById.get(id);

    /* --- JS 侧运行（每例前 reset，隔离共享池状态；维度拒绝经 proSetA 域闸） --- */
    M.reset();
    let jrc;
    if (ce.m < 1 || ce.n < 1 || ce.m > 12 || ce.n > 12) {
      M.proSetA(ce.m, ce.n, 0, 1.0);   /* 预期被拒（err=1），维度寄存保持 0 → proSvd −1 */
      jrc = M.proSvd();
    } else {
      let setOK = true;
      for (let i = 0; i < ce.m * ce.n && setOK; i++) {
        if (M.proSetA(ce.m, ce.n, i, ce.mat[i]) !== 0) { setOK = false; }
      }
      if (!setOK) { problems.push(id + ": proSetA 拒绝域内写入"); continue; }
      jrc = M.proSvd();
    }
    compared++;

    const row = { id, jrc, crc: ge.rc, k: ge.count, ok: true, why: [], stats: "" };
    if (ce.rc !== undefined && parseInt(ce.expect, 10) !== ge.rc) {
      row.ok = false; row.why.push("语料期望 rc=" + ce.expect + " 与 C=" + ge.rc + " 不符");
    }

    /* rc / count 精确 */
    if (ge.rc !== jrc) { row.ok = false; row.why.push("rc JS=" + jrc + " C=" + ge.rc); }
    if (ge.count !== M.proSvdCount()) {
      row.ok = false; row.why.push("count JS=" + M.proSvdCount() + " C=" + ge.count);
    }
    /* 域内反向断言（契约 §3.9） */
    if (ge.rc === 1 && (jrc === -2 || jrc === -3)) {
      row.ok = false; row.why.push("域内 rc=" + jrc + " ∈ {-2,-3}");
    }

    if (ge.rc === 1 && jrc === 1) {
      const k = ge.count;
      /* --- w 对拍（混合误差 ≤ tol；逐位一致另计） --- */
      const jw = [];
      for (let j = 0; j < k; j++) { jw.push(M.proSvdValue(j)); }
      const wErr = worstErr(jw, ge.w);
      wRows++;
      let bits = true;
      for (let j = 0; j < k; j++) { if (jw[j] !== ge.w[j]) { bits = false; } }
      if (bits) { bitEqualW++; }
      if (wErr > tol) { row.ok = false; row.why.push("w 最大混合误差 " + wErr.toExponential(2)); }
      for (let j = 1; j < k; j++) {
        if (!(ge.w[j - 1] >= ge.w[j])) { row.ok = false; row.why.push("C w 非降序 @" + j); }
        if (!(jw[j - 1] >= jw[j])) { row.ok = false; row.why.push("JS w 非降序 @" + j); }
      }
      row.stats = "wErr=" + wErr.toExponential(1) + (bits ? " bit-eq" : "");

      /* --- 探针式独立不变式（不信任被测闸门；缩放原始域） --- */
      let uo = 0, vo = 0, ro = 0, anorm = 0;
      for (let i = 0; i < k; i++) {
        for (let j = 0; j < k; j++) {
          let du = 0, dv = 0;
          for (let l = 0; l < ce.m; l++) { du += M.proSvdU(i, l) * M.proSvdU(j, l); }
          for (let l = 0; l < ce.n; l++) { dv += M.proSvdV(i, l) * M.proSvdV(j, l); }
          uo = Math.max(uo, Math.abs((i === j ? 1 : 0) - du));
          vo = Math.max(vo, Math.abs((i === j ? 1 : 0) - dv));
        }
      }
      for (let i = 0; i < ce.m; i++) {
        let rs = 0;
        for (let l = 0; l < ce.n; l++) { rs += Math.abs(ce.mat[i * ce.n + l]); }
        if (rs > anorm) { anorm = rs; }
      }
      for (let i = 0; i < ce.m; i++) {
        for (let l = 0; l < ce.n; l++) {
          let acc = 0;
          for (let j = 0; j < k; j++) { acc += M.proSvdU(j, i) * jw[j] * M.proSvdV(j, l); }
          ro = Math.max(ro, Math.abs(ce.mat[i * ce.n + l] - acc));
        }
      }
      if (uo > 1e-9) { row.ok = false; row.why.push("‖UᵗU−I‖=" + uo.toExponential(2)); }
      if (vo > 1e-9) { row.ok = false; row.why.push("‖VᵗV−I‖=" + vo.toExponential(2)); }
      if (ro > 1e-9 * Math.max(1, anorm)) {
        row.ok = false; row.why.push("重构残差 " + ro.toExponential(2) + " > 1e-9·max(1,‖A‖)");
      }
      if (!normInvariantU(M, k, ce.m)) { row.ok = false; row.why.push("JS U 规范化不变式破坏"); }
      /* C 侧符号规范化独立复核（行主序 U） */
      for (let j = 0; j < k; j++) {
        let big = -1, im = -1;
        for (let i = 0; i < ce.m; i++) {
          const a = Math.abs(ge.U[i * k + j]);
          if (a > big) { big = a; im = i; }
        }
        if (!(ge.U[im * k + j] > 0)) { row.ok = false; row.why.push("C U 规范化不变式破坏 @" + j); }
      }

      /* --- uvCompare anchor：JS U/V vs C 金标准（行主序→列访问转置）逐分量 --- */
      if (anchor && anchor.uvCompare) {
        uvRows++;
        const cu = [], cv = [];
        for (let i = 0; i < ce.m; i++) { for (let j = 0; j < k; j++) { cu.push(M.proSvdU(j, i)); } }
        for (let i = 0; i < ce.n; i++) { for (let j = 0; j < k; j++) { cv.push(M.proSvdV(j, i)); } }
        const eu = worstErr(cu, ge.U), ev = worstErr(cv, ge.V);
        if (eu > tol) { row.ok = false; row.why.push("U 对 C 最大混合误差 " + eu.toExponential(2)); }
        if (ev > tol) { row.ok = false; row.why.push("V 对 C 最大混合误差 " + ev.toExponential(2)); }
        /* 双侧自证：C 与 JS 各自对契约 expectU/expectV（行主序） */
        if (anchor.expectU) {
          if (worstErr(ge.U, anchor.expectU) > tol) { row.ok = false; row.why.push("C U 偏离契约 expectU"); }
          if (worstErr(cu, anchor.expectU) > tol) { row.ok = false; row.why.push("JS U 偏离契约 expectU"); }
        }
        if (anchor.expectV) {
          if (worstErr(ge.V, anchor.expectV) > tol) { row.ok = false; row.why.push("C V 偏离契约 expectV"); }
          if (worstErr(cv, anchor.expectV) > tol) { row.ok = false; row.why.push("JS V 偏离契约 expectV"); }
        }
      }
      /* --- expectW 双侧自证 --- */
      if (anchor && anchor.expectW) {
        if (worstErr(ge.w, anchor.expectW) > tol) { row.ok = false; row.why.push("C w 偏离契约 expectW"); }
        if (worstErr(jw, anchor.expectW) > tol) { row.ok = false; row.why.push("JS w 偏离契约 expectW"); }
      }
    } else if (ge.rc === 1 || jrc === 1) {
      row.ok = false; row.why.push("单侧成功（C=" + ge.rc + " JS=" + jrc + "）");
    }

    if (!row.ok) { problems.push(id + ": " + row.why.join("; ")); }
    reportRows.push(row);
  }

  /* 金标准有而语料无的 id（防语料重生成后金标准陈旧） */
  for (const id of gold.keys()) {
    if (!corpus.has(id)) { problems.push("gold-only case: " + id); }
  }
  /* 契约 52 anchor 全部在语料内（gen-svd-corpus 的 1:1 锚） */
  for (const c of doc.cases) {
    if (!corpus.has(c.id)) { problems.push("契约 anchor 缺语料: " + c.id); }
  }

  /* ---- 报告 ---- */
  const passed = reportRows.filter(r => r.ok).length;
  let md = "# C 128KB vs JS 128KB 奇异值分解对拍（v2.7.0 G2 one-sided Jacobi）\n\n";
  md += "- 用例来源：`tests/bench/svd-corpus.csv`（" + corpus.size + " 例 = 52 anchor + " +
    (corpus.size - anchorById.size) + " gen，种子 20261104）\n";
  md += "- C 侧：`C/128KB-ProTEXT/test/svd_runner.c` → `tests/bench/svd128-c.csv`（随探针提交）\n";
  md += "- JS 侧：动态 import `JS/128KB/src/core_kernel.js` → `matrix.proSvd`\n";
  md += "- 数值容差 per-case（缺省 1e-9，混合口径 err=|a−b|/max(1,|b|)）；rc/count 精确比对；\n";
  md += "  不变量探针独立重算 ≤1e-9；输出契约见 `docs/contracts/svd-format-v26.md`\n\n";
  md += "| 用例 | JS rc | C rc | 状态 |\n|:---|---:|---:|:---|\n";
  for (const r of reportRows) {
    md += "| " + r.id + " | " + r.jrc + " | " + r.crc + " | " +
      (r.ok ? "✅ " + r.stats : "❌ " + r.why.join("; ")) + " |\n";
  }
  md += "\n**" + passed + "/" + reportRows.length + " 通过**；w 逐位一致 " + bitEqualW + "/" + wRows +
    "；uv 逐分量对拍 " + uvRows + " 例" + (problems.length === 0 ? "，零分叉\n" : "\n");
  fs.writeFileSync(OUT, md);

  for (const p of problems) { console.log("FAIL " + p); }
  console.log("---- svd diff: " + passed + "/" + reportRows.length + " cases passed ----");
  console.log("---- w 逐位一致 " + bitEqualW + "/" + wRows + "；uvCompare 对拍 " + uvRows + " 例 ----");
  if (problems.length > 0) {
    console.log("DIFF-SVD FAILED: " + problems.length + " 个问题；详见 tests/bench/SVD-DIFF.md");
    process.exit(1);
  }
  console.log("DIFF-SVD OK: " + compared + " 例全语料对拍（含不变式探针独立重算与 expect 双侧自证）");
  process.exit(0);
}

main().catch(function (e) {
  console.log("DIFF-SVD FAILED: " + (e && e.stack ? e.stack : String(e)));
  process.exit(1);
});
