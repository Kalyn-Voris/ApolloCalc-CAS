/* ============================================================
 * ApolloCalc-CAS · validate-pool.js — 池有效性校验（构建期诊断工具）
 *
 * 目的：确认 pool.json 的每一条在四个版本上真的走通了目标路径，
 *       而不是静默地测到错误短路。压测前跑一次，避免整份报告失真。
 *
 * 判定：
 *   parse   parseExpression == 0
 *   eval    parse == 0 且 evalExpressionAt 后 evalError == 0（无域错误）
 *   solve   parse == 0 且 extractCoefficients == 0 且 rootCount == degree
 *   exact   同 solve（bench 只测耗时，但路径必须真的跑通）
 *
 * 用法：node tests/bench/validate-pool.js
 * 退出码：0 = 全部通过；1 = 有条目未通过（打印明细）
 * ============================================================ */
"use strict";
const fs = require("fs");
const path = require("path");
const { pathToFileURL } = require("url");

const POOL = JSON.parse(fs.readFileSync(path.join(__dirname, "pool.json"), "utf8"));

function filterPool(items, versionTag) {
  return items.filter(function (item) {
    if (typeof item === "string") { return true; }
    if (!item.versions) { return true; }
    return item.versions.indexOf(versionTag) >= 0;
  });
}

function srcOf(item) { return typeof item === "string" ? item : item.src; }

async function checkVersion(tag, kernelPath) {
  const k = (await import(pathToFileURL(kernelPath).href)).default;
  const fails = [];
  let counts = { parse: 0, eval: 0, solve: 0, exact: 0 };

  /* ---- parse ---- */
  filterPool(POOL.parse, tag).forEach(function (item) {
    const src = srcOf(item);
    counts.parse++;
    const e = k.parseExpression(src);
    if (e !== 0) { fails.push("parse  [" + src + "] parseError=" + e); }
  });

  /* ---- eval ---- */
  filterPool(POOL.eval, tag).forEach(function (item) {
    counts.eval++;
    k.setAngleMode(item.angleMode || "DEG");
    const pe = k.parseExpression(item.src);
    if (pe !== 0) { fails.push("eval   [" + item.src + "] parseError=" + pe); return; }
    const v = k.evalExpressionAt(item.x);
    /* JS 内核不暴露 getEvalError，改判返回值：必须有限（域错误/除零会得 NaN 或 ±Inf） */
    if (!isFinite(v)) {
      fails.push("eval   [" + item.src + "@" + item.x + "] non-finite result=" + v);
    }
  });

  /* ---- solve / exact ---- */
  ["solve", "exact"].forEach(function (stage) {
    filterPool(POOL[stage], tag).forEach(function (item) {
      counts[stage]++;
      k.solversReset();
      const pe = k.parseExpression(item.src);
      if (pe !== 0) { fails.push(stage + "  [" + item.src + "] parseError=" + pe); return; }
      const xe = k.extractCoefficients(item.degree);
      if (xe !== 0) { fails.push(stage + "  [" + item.src + "] extract=" + xe); return; }
      if (item.degree === 2) { k.solveQuadratic(); }
      else if (item.degree === 3) { k.solveCubic(0); }
      else if (item.degree === 4) { k.solveQuartic(); }
      const rc = k.getRootCount();
      const se = k.getSolveError();
      if (se !== 0) { fails.push(stage + "  [" + item.src + "] solveError=" + se); return; }
      if (rc !== item.degree) {
        fails.push(stage + "  [" + item.src + "] roots=" + rc + " expected=" + item.degree);
      }
    });
  });

  return { tag: tag, counts: counts, fails: fails };
}

(async function main() {
  const versions = [
    ["64KB", path.join(__dirname, "..", "..", "JS", "64KB", "src", "core_kernel.js")],
    ["128KB", path.join(__dirname, "..", "..", "JS", "128KB", "src", "core_kernel.js")],
  ];
  let bad = 0;
  for (const [tag, kp] of versions) {
    const r = await checkVersion(tag, kp);
    const total = r.counts.parse + r.counts.eval + r.counts.solve + r.counts.exact;
    console.log("== " + tag + " ==  entries=" + total +
      " (parse " + r.counts.parse + " / eval " + r.counts.eval +
      " / solve " + r.counts.solve + " / exact " + r.counts.exact + ")");
    if (r.fails.length === 0) {
      console.log("   ALL PASS");
    } else {
      bad += r.fails.length;
      console.log("   FAILURES: " + r.fails.length);
      r.fails.slice(0, 40).forEach(function (f) { console.log("     " + f); });
      if (r.fails.length > 40) { console.log("     ... +" + (r.fails.length - 40) + " more"); }
    }
  }
  console.log(bad === 0 ? "\nPOOL VALIDATION: OK" : "\nPOOL VALIDATION: " + bad + " FAILURES");
  process.exit(bad === 0 ? 0 : 1);
})().catch(function (e) { console.error(e); process.exit(1); });
