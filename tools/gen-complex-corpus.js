#!/usr/bin/env node
/* ApolloCalc · gen-complex-corpus.js — F1 复变差分语料生成器（契约
 * docs/contracts/complex-ext-v25.md §9；feat/math-ext 文件级归属 tools/）
 *
 * 生成 tests/bench/complex-corpus.csv（输入网格，无期望列）：
 *   列 id,fn,zre,zim,wre,wim,k
 * 网格设计（全部确定性算术，无随机源）：
 *   · |z| ∈ [0.1,10] 全象限固定网格（半径 × 辐角），15 函数族分层抽样；
 *   · 四条割线两侧采样：实轴 x∈[−10,−1]∪[1,10] 的 y=±0（casin/cacos/catanh/
 *     cacosh/cpow/cnthroot），虚轴 y∈[−10,−1]∪[1,10] 的 x=±0（catan/casinh）；
 *   · 极点/支点/溢出边界：cgamma 非正整数、catan ±i、catanh ±1、cpow 零底、
 *     ctan/ctanh 实/虚极点大有限值、csin/cpow/cgamma 溢出；
 *   · −0.0 边角：(±0,±0) 组合与割线 y=−0 侧。
 *   · 契约锚行：tests/complex-functions.json 的 117 条全部以原 id 收录，
 *     diff-complex.js 对这些行同时做 C/JS vs JSON want 三方校验。
 * 重生成：node tools/gen-complex-corpus.js（应与已冻结语料 diff 为空）。 */
"use strict";
const fs = require("fs");
const path = require("path");

const ROOT = path.join(__dirname, "..");
const OUT = path.join(ROOT, "tests", "bench", "complex-corpus.csv");
const CONTRACT = path.join(ROOT, "tests", "complex-functions.json");

const PI = Math.PI;
const HALF_PI = Math.PI / 2;

/* 全象限固定辐角（10 个，覆盖四象限与近轴/近割线角） */
const ANGLES = [
  PI / 12, PI / 4, 0.999 * HALF_PI, 3 * PI / 4, PI + PI / 6,
  5 * PI / 4, 0.999 * 3 * HALF_PI, 11 * PI / 6, 0.01, PI - 0.001
];
const RADII = [0.1, 0.2, 0.5, 1, 2, 5, 10];

function pol(r, th) { return [r * Math.cos(th), r * Math.sin(th)]; }

const rows = [];
const seen = new Set();
function emit(id, fn, z, w, k) {
  const key = id;
  if (seen.has(key)) { throw new Error("dup id " + id); }
  seen.add(key);
  const ww = w || [0, 0];
  const kk = (k === undefined) ? 0 : k;
  rows.push(`${id},${fn},${z[0]},${z[1]},${ww[0]},${ww[1]},${kk}`);
}

function fmt(v) {
  /* -0 的字面量：JSON/CSV 均写 "-0" 保持符号（parseFloat 保 −0） */
  return Object.is(v, -0) ? "-0" : String(v);
}
function emitRaw(id, fn, z, w, k) {
  const ww = w || [0, 0];
  const kk = (k === undefined) ? 0 : k;
  seen.add(id);
  rows.push(`${id},${fn},${fmt(z[0])},${fmt(z[1])},${fmt(ww[0])},${fmt(ww[1])},${kk}`);
}

/* ---- 1) 整函数族网格：csin/ccos/ctan/csinh/ccosh/ctanh（每族 35 点） ---- */
const ENTIRE = ["csin", "ccos", "ctan", "csinh", "ccosh", "ctanh"];
for (const fn of ENTIRE) {
  for (let ri = 0; ri < RADII.length; ri++) {
    for (let ai = 0; ai < 5; ai++) {
      const z = pol(RADII[ri], ANGLES[ai]);
      emit(`grid-${fn}-${ri}-${ai}`, fn, z);
    }
  }
}

/* ---- 2) 反函数族网格：casin/cacos/catan/casinh/cacosh/catanh（每族 25 点） ---- */
const INVERSE = ["casin", "cacos", "catan", "casinh", "cacosh", "catanh"];
for (const fn of INVERSE) {
  for (let ri = 1; ri < 6; ri++) {
    for (let ai = 0; ai < 5; ai++) {
      const z = pol(RADII[ri], ANGLES[ai]);
      emit(`grid-${fn}-${ri}-${ai}`, fn, z);
    }
  }
}

/* ---- 3) 割线两侧采样（实轴 y=±0；虚轴 x=±0） ---- */
const CUTX = [10, 5, 2, 1.0000001, 1, 0.1, -0.1, -1, -1.0000001, -2, -5, -10];
const REAL_CUT_FNS = ["casin", "cacos", "catanh", "cacosh", "cpow", "cnthroot"];
for (const fn of REAL_CUT_FNS) {
  for (const x of CUTX) {
    for (const s of [0, -0]) {
      emitRaw(`cut-${fn}-${x}-${Object.is(s, -0) ? "m" : "p"}`, fn, [x, s],
              fn === "cpow" ? [0.5, 0] : [0, 0], fn === "cnthroot" ? 3 : undefined);
    }
  }
}
const CUTY = [10, 3, 1.0000001, 1, -1, -1.0000001, -3, -10];
for (const fn of ["catan", "casinh"]) {
  for (const y of CUTY) {
    for (const s of [0, -0]) {
      emit(`cut-${fn}-${y}-${Object.is(s, -0) ? "m" : "p"}`, fn, [s, y]);
    }
  }
}

/* ---- 4) 极点/支点/溢出边界 ---- */
for (const x of [0, -1, -2, -7]) {
  for (const s of [0, -0]) { emit(`pole-cgamma-${x}-${Object.is(s, -0) ? "m" : "p"}`, "cgamma", [x, s]); }
}
emit("pole-catan-p", "catan", [0, 1]);
emit("pole-catan-m", "catan", [0, -1]);
for (const x of [1, -1]) {
  for (const s of [0, -0]) { emit(`pole-catanh-${x}-${Object.is(s, -0) ? "m" : "p"}`, "catanh", [x, s]); }
}
emit("pole-ctan-halfpi-p", "ctan", [HALF_PI, 0]);
emit("pole-ctan-halfpi-m", "ctan", [HALF_PI, -0]);
emit("pole-ctanh-halfpi", "ctanh", [0, HALF_PI]);
emit("ovf-csin-800i", "csin", [0, 800]);
emit("ovf-csinh-800", "csinh", [800, 0]);
emit("ovf-cgamma-200", "cgamma", [200, 0]);
emitRaw("ovf-cpow-10-400", "cpow", [10, 0], [400, 0]);

/* ---- 5) cpow / cnthroot 参数网格 ---- */
const POW_BASES = [[0.5, 0.5], [2, 0], [3, -4], [-2, 0], [-2, -0], [0.1, 9], [10, 0]];
const POW_EXPS = [[0.5, 0], [3, 0], [-1.5, 0], [0, 1], [1, 1], [-2, -0.5], [0, 0], [2, 0]];
for (let bi = 0; bi < POW_BASES.length; bi++) {
  for (let ei = 0; ei < POW_EXPS.length; ei++) {
    emit(`cpow-${bi}-${ei}`, "cpow", POW_BASES[bi], POW_EXPS[ei]);
  }
}
const NTH_ZS = [[8, 0], [-8, 0], [-8, -0], [0.5, 0.5], [1, 1], [10, -10], [0, 0]];
const NTH_KS = [1, 2, 3, 4, 7];
for (let zi = 0; zi < NTH_ZS.length; zi++) {
  for (const k of NTH_KS) {
    emit(`cnth-${zi}-${k}`, "cnthroot", NTH_ZS[zi], undefined, k);
  }
}
for (const k of [0, -1, -5]) { emit(`cnth-eparam-${k}`, "cnthroot", [8, 0], undefined, k); }

/* ---- 6) −0 边角组合 ---- */
for (const fn of ["csin", "ccos", "ctan", "csinh", "ccosh", "ctanh", "cgamma"]) {
  emitRaw(`zero-${fn}-mm`, fn, [-0, -0]);
  emitRaw(`zero-${fn}-pm`, fn, [0, -0]);
  emitRaw(`zero-${fn}-mp`, fn, [-0, 0]);
}

/* ---- 7) 契约锚行（同 id 收录，diff 三方校验） ---- */
const contract = JSON.parse(fs.readFileSync(CONTRACT, "utf8"));
for (const c of contract.cases) {
  emitRaw(c.id, c.fn, c.z, c.w, c.k);
}

fs.writeFileSync(OUT, "# ApolloCalc complex-ext differential corpus (gen-complex-corpus.js; contract complex-ext-v25.md section 9)\n" +
                 rows.join("\n") + "\n", "utf8");
console.log(`wrote ${OUT}: ${rows.length} rows`);
