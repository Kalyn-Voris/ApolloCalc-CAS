/* ApolloCalc · diff-eigen.js — C 128KB vs JS 128KB 一般实矩阵特征值对拍（F4）

   读 tests/bench/eig128-c.csv（C 探针 test/eig_csv_probe.c 的导出，已随探针
   提交）与 tests/eigen-cases.json（共享契约，47 例 dual 全量零分叉），驱动
   JS 内核（动态 import JS/128KB/src/core_kernel.js 的 matrix.proSchur），
   按 id 对齐逐例比对：
     rc/count:      精确相等（含 perm4_fail3 的双侧一致 -3 与 rej_n0/rej_n13 的 -1）
     mat:           C 探针矩阵与契约 JSON 逐字精确相等（防转录漂移）
     val:           特征值多重集 |差| ≤ tol（per-case，缺省 1e-9）；
                    order=true 的用例额外按契约全序 (|λ|,Re,Im) 降序逐位断言
     hasvec:        逐 k 精确相等（复特征值两侧都不产出向量）
     vec:           vecCompare=true 的用例，符号规范化后的实特征向量逐分量 ≤ tol
     结构不变式:    共轭对相邻 +Im 在前（两侧各自检查）；
                    符号规范化不变式（最大 |·| 分量 > 0，首达最大）
     expect:        有 expectRe/expectIm 的用例，C 与 JS 双侧都对闭式谱自证
                    （防「同错不异」）；expectRc 精确
   生成 tests/bench/EIGEN-DIFF.md；全部通过退出码 0，否则 1。
   运行：node tests/bench/diff-eigen.js
*/
"use strict";
const fs = require("fs");
const path = require("path");
const { pathToFileURL } = require("url");

const DIR = __dirname;
const ROOT = path.join(DIR, "..", "..");
const C_CSV = path.join(DIR, "eig128-c.csv");
const CASES = path.join(ROOT, "tests", "eigen-cases.json");
const KERNEL = path.join(ROOT, "JS", "128KB", "src", "core_kernel.js");
const OUT = path.join(DIR, "EIGEN-DIFF.md");

function readCsvRows(file) {
  const text = fs.readFileSync(file, "utf8");
  const rows = [];
  for (const line of text.split(/\r?\n/)) {
    if (line === "") { continue; }
    rows.push(line.split(","));
  }
  return rows;
}

/* ---- 组织 C 侧行：{ id: {mat, rc, count, vals:[{re,im,hasvec}], vecs:{k:[...]}} } ---- */
function collectC(rows) {
  const map = new Map();
  for (const r of rows) {
    const kind = r[0], id = r[1];
    if (!map.has(id)) { map.set(id, { vals: [], vecs: new Map() }); }
    const e = map.get(id);
    if (kind === "mat") {
      e.matN = parseInt(r[2], 10);
      e.mat = r.slice(3).map(parseFloat);
    } else if (kind === "eig") {
      e.rc = parseInt(r[2], 10);
      e.count = parseInt(r[3], 10);
    } else if (kind === "val") {
      e.vals[parseInt(r[2], 10)] = {
        k: parseInt(r[2], 10), re: parseFloat(r[3]), im: parseFloat(r[4]), hasvec: parseInt(r[5], 10)
      };
    } else if (kind === "vec") {
      e.vecs.set(parseInt(r[2], 10), r.slice(3).map(parseFloat));
    }
  }
  return map;
}

/* 多重集匹配：每个期望 (re,im) 在未用槽位里找容差内匹配（C test_pro_schur.c
 * sch_spectrum_match 同思想）。全配成功且计数一致 → null，否则返回描述串。 */
function spectrumMatch(got, expRe, expIm, tol) {
  if (got.length !== expRe.length) { return "count " + got.length + " != " + expRe.length; }
  const used = new Array(got.length).fill(false);
  for (let i = 0; i < expRe.length; i++) {
    let found = -1;
    for (let j = 0; j < got.length; j++) {
      if (used[j]) { continue; }
      if (Math.abs(got[j].re - expRe[i]) <= tol && Math.abs(got[j].im - expIm[i]) <= tol) { found = j; break; }
    }
    if (found < 0) {
      return "want (" + expRe[i] + "," + expIm[i] + ") unmatched";
    }
    used[found] = true;
  }
  return null;
}

function approxVec(a, b, tol) {
  if (a.length !== b.length) { return false; }
  for (let i = 0; i < a.length; i++) {
    if (!(Math.abs(a[i] - b[i]) <= tol)) { return false; }
  }
  return true;
}

/* 符号规范化不变式：最大 |·| 分量为正且是首个最大（首达最大 = 并列取最小索引） */
function normInvariant(v) {
  let big = -1, imax = -1;
  for (let i = 0; i < v.length; i++) {
    const a = Math.abs(v[i]);
    if (a > big) { big = a; imax = i; }
  }
  return imax >= 0 && v[imax] > 0;
}

/* 共轭对相邻 +Im 在前（结构不变式，按 hasvec=0 识别复特征值） */
function pairingInvariant(vals, count) {
  for (let k = 0; k < count; k++) {
    const v = vals[k];
    if (!v) { return "missing val k=" + k; }
    if (v.hasvec === 0) {
      /* 复特征值：必须处于相邻对且本槽 +Im */
      if (!(v.im > 0)) { return "k=" + k + " complex slot im=" + v.im + " (want +Im first)"; }
      const w = vals[k + 1];
      if (!w || w.hasvec !== 0 || !(w.im < 0) || w.re !== v.re || Math.abs(w.im) !== Math.abs(v.im)) {
        return "k=" + k + " conjugate pair not adjacent/+Im-first";
      }
      k++;   /* 跳过配对另一半 */
    }
  }
  return null;
}

async function main() {
  const doc = JSON.parse(fs.readFileSync(CASES, "utf8"));
  const cMap = collectC(readCsvRows(C_CSV));
  const kernel = await import(pathToFileURL(KERNEL).href);
  const M = kernel.default.matrix;
  if (!M || typeof M.proSchur !== "function") {
    console.log("FAIL: kernel.matrix.proSchur 不可用（请先 node JS/128KB/test/build-kernel.js --minify 重建内核）");
    process.exit(1);
  }

  const problems = [];
  const reportRows = [];
  let compared = 0;

  for (const c of doc.cases) {
    const id = c.id;
    const tol = (c.tol !== undefined) ? c.tol : doc.defaultTol;
    const ce = cMap.get(id);
    if (!ce) { problems.push(id + ": C CSV 缺该用例"); continue; }

    /* --- JS 侧运行（每例前 reset，隔离共享池状态） --- */
    M.reset();
    let jrc;
    if (c.matrix) {
      for (let i = 0; i < c.n * c.n; i++) {
        if (M.proSetA(c.n, c.n, i, c.matrix[i]) !== 0) {
          problems.push(id + ": proSetA 拒绝 i=" + i); break;
        }
      }
      jrc = M.proSchur();
    } else if (c.n === 13) {
      M.proSetA(13, 13, 0, 1.0);   /* 预期被域闸拒绝（err=1），输入保持未置 */
      jrc = M.proSchur();
    } else {
      jrc = M.proSchur();          /* n=0：未置输入 → -1 */
    }

    const row = { id, jrc, crc: ce.rc, ok: true, why: [] };
    compared++;

    /* rc / count 精确 */
    if (ce.rc !== jrc) { row.ok = false; row.why.push("rc JS=" + jrc + " C=" + ce.rc); }
    if (ce.count !== M.proSchurCount()) { row.ok = false; row.why.push("count JS=" + M.proSchurCount() + " C=" + ce.count); }

    /* expectRc（双侧） */
    if (c.expectRc !== undefined) {
      if (c.expectRc !== ce.rc) { row.ok = false; row.why.push("C rc=" + ce.rc + " want " + c.expectRc); }
      if (c.expectRc !== jrc) { row.ok = false; row.why.push("JS rc=" + jrc + " want " + c.expectRc); }
    }

    /* mat 精确（C 探针矩阵 vs 契约 JSON，防转录漂移） */
    if (c.matrix) {
      if (!ce.mat || ce.matN !== c.n || ce.mat.length !== c.matrix.length) {
        row.ok = false; row.why.push("C mat 行缺失/长度不符");
      } else {
        for (let i = 0; i < c.matrix.length; i++) {
          if (ce.mat[i] !== c.matrix[i]) { row.ok = false; row.why.push("mat[" + i + "] C=" + ce.mat[i] + " JSON=" + c.matrix[i]); break; }
        }
      }
    }

    /* rc=1 用例：val/vec/结构/expect 全查 */
    if (ce.rc === 1 && jrc === 1) {
      const cvals = [];
      for (let k = 0; k < c.n; k++) {
        if (!ce.vals[k]) { row.ok = false; row.why.push("C val k=" + k + " 缺行"); continue; }
        cvals.push(ce.vals[k]);
      }
      const jvals = [];
      for (let k = 0; k < c.n; k++) {
        jvals.push({ k, re: M.proSchurRe(k), im: M.proSchurIm(k), hasvec: M.proSchurHasVec(k) });
      }

      /* 多重集（order=false）或契约全序逐位（order=true） */
      const mismatch = c.order
        ? (function () {
            for (let k = 0; k < c.n; k++) {
              if (Math.abs(cvals[k].re - jvals[k].re) > tol || Math.abs(cvals[k].im - jvals[k].im) > tol) {
                return "positional k=" + k + " C=(" + cvals[k].re + "," + cvals[k].im + ") JS=(" + jvals[k].re + "," + jvals[k].im + ")";
              }
            }
            return null;
          })()
        : spectrumMatch(jvals, cvals.map(v => v.re), cvals.map(v => v.im), tol);
      if (mismatch) { row.ok = false; row.why.push("C-JS " + mismatch); }

      /* 闭式谱自证（双侧独立对 expect；防同错） */
      if (c.expectRe) {
        for (const [tag, vv] of [["C", cvals], ["JS", jvals]]) {
          const em = c.order
            ? (function () {
                for (let k = 0; k < c.n; k++) {
                  if (Math.abs(vv[k].re - c.expectRe[k]) > tol || Math.abs(vv[k].im - c.expectIm[k]) > tol) {
                    return "k=" + k + " got=(" + vv[k].re + "," + vv[k].im + ")";
                  }
                }
                return null;
              })()
            : spectrumMatch(vv, c.expectRe, c.expectIm, tol);
          if (em) { row.ok = false; row.why.push("expect[" + tag + "] " + em); }
        }
      }

      /* hasvec 逐 k 精确 */
      for (let k = 0; k < c.n; k++) {
        if (cvals[k].hasvec !== jvals[k].hasvec) {
          row.ok = false; row.why.push("hasvec k=" + k + " C=" + cvals[k].hasvec + " JS=" + jvals[k].hasvec);
        }
      }

      /* 结构不变式（两侧各自） */
      const pc = pairingInvariant(cvals, c.n);
      if (pc) { row.ok = false; row.why.push("C pairing: " + pc); }
      const pj = pairingInvariant(jvals, c.n);
      if (pj) { row.ok = false; row.why.push("JS pairing: " + pj); }

      /* 向量比对（vecCompare=true）+ 规范化不变式（两侧全部实向量） */
      for (let k = 0; k < c.n; k++) {
        if (jvals[k].hasvec !== 1) { continue; }
        const jv = [];
        for (let i = 0; i < c.n; i++) { jv.push(M.proSchurVector(k, i)); }
        if (!normInvariant(jv)) { row.ok = false; row.why.push("JS vec k=" + k + " 规范化不变式破坏"); }
        const cv = ce.vecs.get(k);
        if (!cv) { row.ok = false; row.why.push("C vec k=" + k + " 缺行"); continue; }
        if (!normInvariant(cv)) { row.ok = false; row.why.push("C vec k=" + k + " 规范化不变式破坏"); }
        if (c.vecCompare) {
          if (!approxVec(jv, cv, tol)) {
            let worst = 0;
            for (let i = 0; i < c.n; i++) { worst = Math.max(worst, Math.abs(jv[i] - cv[i])); }
            row.ok = false; row.why.push("vec k=" + k + " 最大差 " + worst.toExponential(2));
          }
        }
      }
    } else if (ce.rc === 1 || jrc === 1) {
      row.ok = false; row.why.push("单侧成功（C=" + ce.rc + " JS=" + jrc + "）");
    }

    if (!row.ok) { problems.push(id + ": " + row.why.join("; ")); }
    reportRows.push(row);
  }

  /* C CSV 有而 JSON 无的 id（防契约删例后 CSV 陈旧） */
  for (const id of cMap.keys()) {
    if (!doc.cases.some(c => c.id === id)) { problems.push("C-only case: " + id); }
  }

  /* ---- 报告 ---- */
  const passed = reportRows.filter(r => r.ok).length;
  let md = "# C 128KB vs JS 128KB 特征值对拍（F4 Francis QR）\n\n";
  md += "- 用例来源：`tests/eigen-cases.json`（" + doc.cases.length + " 例，dual 全量零分叉）\n";
  md += "- C 侧：`C/128KB-ProTEXT/test/eig_csv_probe.c` → `tests/bench/eig128-c.csv`（随探针提交）\n";
  md += "- JS 侧：动态 import `JS/128KB/src/core_kernel.js` → `matrix.proSchur`\n";
  md += "- 数值容差 per-case（缺省 1e-9）；rc/count/hasvec/mat 精确比对；输出契约见 `docs/contracts/eigen-format-v25.md`\n\n";
  md += "| 用例 | JS rc | C rc | 状态 |\n|:---|---:|---:|:---|\n";
  for (const r of reportRows) {
    md += "| " + r.id + " | " + r.jrc + " | " + r.crc + " | " + (r.ok ? "✅" : "❌ " + r.why.join("; ")) + " |\n";
  }
  md += "\n**" + passed + "/" + reportRows.length + " 通过**" + (problems.length === 0 ? " —— 零分叉\n" : "\n");
  fs.writeFileSync(OUT, md);

  for (const p of problems) { console.log("FAIL " + p); }
  console.log("---- eigen diff: " + passed + "/" + reportRows.length + " cases passed ----");
  if (problems.length > 0) {
    console.log("DIFF-EIGEN FAILED: " + problems.length + " 个问题；详见 tests/bench/EIGEN-DIFF.md");
    process.exit(1);
  }
  console.log("DIFF-EIGEN OK: " + compared + " 例 dual 全量零分叉（含 expect 双侧自证与结构不变式）");
  process.exit(0);
}

main().catch(function (e) {
  console.log("DIFF-EIGEN FAILED: " + (e && e.stack ? e.stack : String(e)));
  process.exit(1);
});
