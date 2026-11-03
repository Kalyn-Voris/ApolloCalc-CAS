#!/usr/bin/env node
/* ApolloCalc · gen-svd-corpus.js — v2.6.0 G2 SVD 差分语料生成器（契约
 * docs/contracts/svd-format-v26.md §6；feat/matrix-advanced 文件级归属 tools/）
 *
 * 生成 tests/bench/svd-corpus.csv（输入语料 + 期望谱列，无期望向量列；
 * 落 tests/bench/ 与 complex-corpus.csv 同款——.gitignore 的 `*.csv` +
 * `!tests/bench/*.csv` 双规则只把该目录作 tracked 基线数据集）：
 *   列 id,m,n,kind,tol,expect,a00,a01,…   （kind ∈ {anchor, gen}）
 *   expect = ""（仅不变量断言）| "-1"（参数拒绝）| 降序 w 列表 %.17g 分号分隔
 * 结构谱（全部确定性算术 + 固定种子 mulberry32(20261104)，无其他随机源）：
 *   · 对角 / 符号置换正交×对角×符号置换ᵗ 合成（σ = |对角|，全链精确）；
 *   · 已知谱对称三对角（2+2cos(kπ/(n+1))，n = 2..12）；
 *   · 随机稠密（全形状组合 1..12 × 1..12 分层抽样）；
 *   · 秩亏（随机 m×r · r×n 合成 + 重复列结构）；
 *   · 近重 σ（diag(d, d(1+1e-7), …)）；
 *   · Hilbert 病态（n = 2..12）；
 *   · 1e±150 幂 2 极端尺度（±2^40/2^80/2^150 整体系数与谱内跨档）；
 *   · 拒绝类（m=0 / n=0 / m=13 / n=13 / NaN / ±Inf，CSV 字面量 nan/inf/-inf，
 *     C strtod 与 JS Number 同值）。
 * 契约锚：tests/svd-cases.json 全部用例以原 id、原期望收录（缺一即 fail，
 * 重生成应与已冻结语料 diff 为空）。期望谱取自契约 JSON（闭式谱手算锚 +
 * 开工前 JS oracle 全量验证），本脚本不做任何 SVD 数值计算。
 * 重生成：node tools/gen-svd-corpus.js */
"use strict";
const fs = require("fs");
const path = require("path");

const ROOT = path.join(__dirname, "..");
const OUT = path.join(ROOT, "tests", "bench", "svd-corpus.csv");
const CONTRACT = JSON.parse(fs.readFileSync(path.join(ROOT, "tests", "svd-cases.json"), "utf8"));
const SEED = 20261104;

let s = SEED >>> 0;
function rnd() {
  s |= 0; s = (s + 0x6D2B79F5) | 0;
  let t = Math.imul(s ^ (s >>> 15), 1 | s);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}
function ri(lo, hi) { return lo + Math.floor(rnd() * (hi - lo + 1)); }

function fmt(v) {
  if (typeof v === "string") {
    return { "NaN": "nan", "nan": "nan", "Infinity": "inf", "inf": "inf",
             "-Infinity": "-inf", "-inf": "-inf" }[v];
  }
  if (Object.is(v, -0)) { return "-0"; }
  return String(v);
}

const rows = [];
const seen = new Set();
function emit(id, m, n, kind, tol, expect, mat) {
  if (seen.has(id)) { throw new Error("dup id " + id); }
  seen.add(id);
  const cells = [id, m, n, kind, tol, expect];
  if (mat) { for (const v of mat) { cells.push(fmt(v)); } }
  rows.push(cells.join(","));
}

/* ---- 1) 契约锚：tests/svd-cases.json 全量收录 ---- */
const anchorIds = [];
for (const c of CONTRACT.cases) {
  let expect = "";
  if (c.expectRc === -1) { expect = "-1"; }
  else if (c.expectW) { expect = c.expectW.map(fmt).join(";"); }
  const tol = (c.tol === undefined) ? "" : String(c.tol);
  emit(c.id, c.m, c.n, "anchor", tol, expect, c.matrix);
  anchorIds.push(c.id);
}
if (anchorIds.length !== CONTRACT.count) { throw new Error("anchor count mismatch"); }

/* ---- 2) 对角 / 符号置换×对角×置换ᵗ（结构谱，σ 精确已知） ---- */
function signedPerm(n) {
  const p = [];
  for (let i = 0; i < n; i++) { p.push(i); }
  for (let i = n - 1; i > 0; i--) { const j = ri(0, i); const t = p[i]; p[i] = p[j]; p[j] = t; }
  const m = new Array(n * n).fill(0);
  for (let i = 0; i < n; i++) { m[p[i] * n + i] = rnd() < 0.5 ? 1 : -1; }
  return { m, p };
}
function diagMat(d) { const m = new Array(d.length * d.length).fill(0); for (let i = 0; i < d.length; i++) { m[i * d.length + i] = d[i]; } return m; }

for (let n = 1; n <= 12; n++) {
  const d = []; for (let i = 0; i < n; i++) { d.push(ri(1, 9) * (rnd() < 0.4 ? -1 : 1)); }
  emit("g-diag-" + n, n, n, "gen", "", "", diagMat(d));
}
for (let n = 2; n <= 12; n++) {
  const d = []; for (let i = 0; i < n; i++) { d.push(ri(1, 9)); }
  const P1 = signedPerm(n), P2 = signedPerm(n);
  const D = diagMat(d);
  const T = new Array(n * n).fill(0);
  /* A = P1 · D · P2^t：行置换 × 对角 × 列置换（全为精确 ±1/0 数据搬移） */
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      let acc = 0;
      for (let t = 0; t < n; t++) { acc += P1.m[i * n + t] * D[t * n + j]; }
      T[i * n + j] = acc;
    }
  }
  const A = new Array(n * n).fill(0);
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      let acc = 0;
      for (let t = 0; t < n; t++) { acc += T[i * n + t] * P2.m[j * n + t]; }
      A[i * n + j] = acc;
    }
  }
  emit("g-orthdiag-" + n, n, n, "gen", "", "", A);
}

/* ---- 3) 已知谱对称三对角 2+2cos(kπ/(n+1))（全部 σ = 特征值 ≥ 0） ---- */
for (let n = 2; n <= 12; n++) {
  const m = new Array(n * n).fill(0);
  for (let i = 0; i < n; i++) { m[i * n + i] = 2; }
  for (let i = 0; i + 1 < n; i++) { m[i * n + i + 1] = 1; m[(i + 1) * n + i] = 1; }
  emit("g-tri-" + n, n, n, "gen", "", "", m);
}

/* ---- 4) 随机稠密全形状 ---- */
for (let t = 0; t < 60; t++) {
  const m = ri(1, 12), n = ri(1, 12);
  const mat = []; for (let i = 0; i < m * n; i++) { mat.push(ri(-5, 5)); }
  emit("g-dense-" + t + "-" + m + "x" + n, m, n, "gen", "", "", mat);
}

/* ---- 5) 秩亏（随机 m×r · r×n 合成 + 重复列） ---- */
for (let t = 0; t < 30; t++) {
  const m = ri(2, 12), n = ri(2, 12), r = ri(1, Math.min(m, n) - (Math.min(m, n) > 1 ? 1 : 0));
  const L = [], R = [];
  for (let i = 0; i < m * r; i++) { L.push(ri(-3, 3)); }
  for (let i = 0; i < r * n; i++) { R.push(ri(-3, 3)); }
  const mat = new Array(m * n).fill(0);
  for (let i = 0; i < m; i++) {
    for (let j = 0; j < n; j++) {
      let acc = 0;
      for (let q = 0; q < r; q++) { acc += L[i * r + q] * R[q * n + j]; }
      mat[i * n + j] = acc;
    }
  }
  emit("g-rankdef-" + t + "-" + m + "x" + n + "r" + r, m, n, "gen", "", "", mat);
}
for (let t = 0; t < 8; t++) {
  const m = ri(2, 6), n = ri(3, 8);
  const mat = new Array(m * n).fill(0);
  for (let i = 0; i < m; i++) { for (let j = 0; j < n; j++) { mat[i * n + j] = ri(-4, 4) * ((j % 2) + (i % 3 === 0 ? 1 : 0)); } }
  emit("g-dupcol-" + t, m, n, "gen", "", "", mat);
}

/* ---- 6) 近重 σ ---- */
for (let t = 0; t < 12; t++) {
  const n = ri(2, 6);
  const d0 = ri(2, 9);
  const d = []; for (let i = 0; i < n; i++) { d.push(i === 0 ? d0 : d0 * (1 + 1e-7 * (rnd() < 0.5 ? 1 : -1))); }
  emit("g-nearrep-" + t, n, n, "gen", "", "", diagMat(d));
}

/* ---- 7) Hilbert 病态 ---- */
for (let n = 2; n <= 12; n++) {
  const m = []; for (let i = 0; i < n; i++) { for (let j = 0; j < n; j++) { m.push(1 / (i + j + 1)); } }
  emit("g-hilbert-" + n, n, n, "gen", "", "", m);
}

/* ---- 8) 1e±150 幂 2 极端尺度 ---- */
const SCALES = [Math.pow(2, 40), Math.pow(2, -40), Math.pow(2, 80), Math.pow(2, -80), Math.pow(2, 150), Math.pow(2, -150)];
for (let t = 0; t < 12; t++) {
  const m = ri(1, 6), n = ri(1, 6);
  const sc = SCALES[t % SCALES.length];
  const mat = []; for (let i = 0; i < m * n; i++) { mat.push(ri(-4, 4) * sc); }
  emit("g-pow2-" + t + "-" + m + "x" + n, m, n, "gen", "", "", mat);
}
for (let t = 0; t < 8; t++) { /* 谱内跨档：对角跨 2^±150 */
  const n = ri(2, 6);
  const d = []; for (let i = 0; i < n; i++) { d.push(Math.pow(2, ri(-150, 150))); }
  emit("g-pow2spread-" + t, n, n, "gen", "", "", diagMat(d));
}

/* ---- 9) 拒绝类（CSV 字面量 nan/inf/-inf；C strtod 同值） ---- */
emit("g-rej-m0", 0, 3, "gen", "", "-1", [1, 2, 3]);
emit("g-rej-n0", 3, 0, "gen", "", "-1", [1, 2, 3]);
emit("g-rej-m13", 13, 4, "gen", "", "-1", []);
emit("g-rej-n13", 4, 13, "gen", "", "-1", []);
emit("g-rej-nan", 2, 2, "gen", "", "-1", [1, "nan", 0, 1]);
emit("g-rej-inf", 2, 2, "gen", "", "-1", ["inf", 0, 0, 1]);
emit("g-rej-neginf", 2, 2, "gen", "", "-1", ["-inf", 2, 3, 4]);
emit("g-rej-nanbig", 3, 3, "gen", "", "-1", [1, 2, 3, 4, "nan", 6, 7, 8, 9]);

fs.writeFileSync(OUT, rows.join("\n") + "\n");
console.log("written " + OUT + ": " + rows.length + " rows (" + anchorIds.length + " anchors + " + (rows.length - anchorIds.length) + " gen)");
if (rows.length < 150 || rows.length > 400) { throw new Error("row count out of contract band 150-400: " + rows.length); }
