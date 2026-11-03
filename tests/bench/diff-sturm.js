/* ApolloCalc · diff-sturm.js — F3 Sturm 序列与实根隔离 双端对拍驱动

   机制同 diff-smoke128.js:tests/sturm.json 三层用例分别喂给 C 探针
   (probe_sturm_csv.c,obj 缓存直链)与 JS 侧(core_ratroot.js sturm 扩段):
     dualCases:计数精确整数、隔离串逐字、错误码精确——零分叉区;
     cOnlyDEG:C 断言 cCount/cIsolate,JS 断言 EDEG(−2);
     cOnlyOVF:C 断言真实计数,JS 断言 ECOEF(−3)。
   既有 50 例 smoke 管线一行不动;全部通过退出码 0,否则 1。
   报告:tests/bench/SMOKE-DIFF-STURM.md。
   运行:node tests/bench/diff-sturm.js   (仓库根;需 mingw32-make/gcc) */
"use strict";
const fs = require("fs");
const path = require("path");
const cp = require("child_process");

const ROOT = path.join(__dirname, "..", "..");
const CDIR = path.join(ROOT, "C", "128KB-ProTEXT");
const OUT_MD = path.join(__dirname, "SMOKE-DIFF-STURM.md");
const J = JSON.parse(fs.readFileSync(path.join(ROOT, "tests", "sturm.json"), "utf8"));

/* ---- JS 侧:拼接 eval 加载(core_ratroot.js sturm 扩段) ---- */
const JS_FILES = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
  "core_evaluator.js", "core_base.js", "core_solvers.js", "core_ratroot.js"];
const jsCode = JS_FILES.map((f) => fs.readFileSync(path.join(ROOT, "JS", "128KB", "src", f), "utf8")).join("\n");
const jsMod = { exports: {} };
(0, eval)(jsCode);   /* 间接 eval:var 落 globalThis(严格模式下直接 eval 会自建作用域) */
jsMod.ApolloSqfree = globalThis.ApolloSqfree;

function jsCount(p, a, b) { return jsMod.ApolloSqfree.sturmCount(p, a, b); }
function jsIso(p) {
  const rc = jsMod.ApolloSqfree.sturmIsolate(p);
  return rc + "|" + jsMod.ApolloSqfree.getSturmString();
}

/* ---- C 侧:obj 缓存直链探针 ---- */
function sh(cmd) {
  return cp.spawnSync(cmd[0], cmd.slice(1), { encoding: "utf8", cwd: CDIR });
}
const mk = sh(["mingw32-make", "SHELL=cmd.exe", "pro_runner"]);
if (mk.status !== 0) {
  console.error("diff-sturm: mingw32-make pro_runner failed:\n" + (mk.stderr || mk.stdout));
  process.exit(1);
}
const probeSrc = path.join(CDIR, "test", "probe_sturm_csv.c");
const probeExe = path.join(CDIR, "probe_sturm.exe");
const objs = fs.readdirSync(path.join(CDIR, "obj", "src")).filter((f) => f.endsWith(".o"))
  .map((f) => path.join(CDIR, "obj", "src", f));
const cc = sh(["gcc", "-std=c99", "-O2", "-Isrc", "-I../../C/128KB/src", "-DAC_PRO=1",
  probeSrc].concat(objs).concat(["-o", probeExe, "-lm"]));
if (cc.status !== 0) {
  console.error("diff-sturm: probe compile failed:\n" + (cc.stderr || cc.stdout));
  process.exit(1);
}

/* 输入行:C|p|a|b 或 I|p;输出:count 行 / rc|out 行 */
const inputLines = [];
const plan = [];
for (const c of J.dualCases) {
  if (c.counts) {
    for (const ct of c.counts) {
      inputLines.push("C|" + c.p + "|" + ct.a + "|" + ct.b);
      plan.push({ kind: "count", id: c.id, a: ct.a, b: ct.b, want: (ct.want !== undefined ? ct.want : ct.err), layer: "dual" });
    }
  }
  if (c.err !== undefined) {
    if (c.counts === undefined) {
      inputLines.push("C|" + c.p + "|-3|3");
      plan.push({ kind: "count", id: c.id, a: "-3", b: "3", want: c.err, layer: "dual" });
    }
  }
  if (c.isolate !== undefined) {
    inputLines.push("I|" + c.p);
    plan.push({ kind: "isolate", id: c.id, want: c.isolate, layer: "dual" });
  }
}
for (const c of J.cOnlyDEG) {
  if (c.cCount) {
    inputLines.push("C|" + c.p + "|" + c.cCount.a + "|" + c.cCount.b);
    plan.push({ kind: "count", id: c.id, a: c.cCount.a, b: c.cCount.b, want: c.cCount.want, jsWant: c.jsErr, layer: "cOnlyDEG" });
  }
  if (c.cIsolate !== undefined) {
    inputLines.push("I|" + c.p);
    plan.push({ kind: "isolate", id: c.id, want: c.cIsolate, jsWant: c.jsErr, layer: "cOnlyDEG" });
  }
}
for (const c of J.cOnlyOVF) {
  inputLines.push("C|" + c.p + "|" + c.a + "|" + c.b);
  plan.push({ kind: "count", id: c.id, a: c.a, b: c.b, want: c.cCount, jsWant: c.jsExpect, layer: "cOnlyOVF" });
}

const run = cp.spawnSync(probeExe, { input: inputLines.join("\n") + "\n", encoding: "utf8", maxBuffer: 16 * 1024 * 1024 });
if (run.status !== 0) {
  console.error("diff-sturm: probe run failed:\n" + (run.stderr || ""));
  process.exit(1);
}
const cLines = run.stdout.split(/\r?\n/).filter((l) => l.length > 0);
if (cLines.length !== plan.length) {
  console.error("diff-sturm: probe line count mismatch: " + cLines.length + " vs " + plan.length);
  process.exit(1);
}

/* ---- 比对 ---- */
const rows = [];
let bad = 0;
for (let i = 0; i < plan.length; i++) {
  const p = plan[i];
  const cLine = cLines[i];
  let ok = false;
  let detail = "";
  if (p.kind === "count") {
    const cVal = parseInt(cLine, 10);
    const jVal = jsCount(p.id2p || "", "", "");
    if (p.layer === "dual") {
      ok = cVal === p.want && jsValOk();
      function jsValOk() { return true; }
    }
    /* 统一在下面按层重算(JS 值按用例参数现算) */
    const psrc = findPoly(p);
    const jv = jsCount(psrc, p.a, p.b);
    if (p.layer === "dual") {
      ok = (cVal === p.want) && (jv === p.want);
      detail = "C=" + cVal + " JS=" + jv + " want=" + p.want;
    } else if (p.layer === "cOnlyDEG") {
      ok = (cVal === p.want) && (jv === p.jsWant);
      detail = "C=" + cVal + " JS=" + jv + " (C want " + p.want + ", JS want " + p.jsWant + ")";
    } else {
      ok = (cVal === p.want) && (jv === p.jsWant);
      detail = "C=" + cVal + " JS=" + jv + " (C want " + p.want + ", JS want " + p.jsWant + ")";
    }
  } else {
    const sep = cLine.indexOf("|");
    const cRc = parseInt(cLine.slice(0, sep), 10);
    const cOut = cLine.slice(sep + 1);
    const psrc = findPoly(p);
    const jLine = jsIso(psrc);
    const jSep = jLine.indexOf("|");
    const jRc = parseInt(jLine.slice(0, jSep), 10);
    const jOut = jLine.slice(jSep + 1);
    const wantStr = p.want.constructor === Array
      ? (p.want.length === 0 ? "[]" : "[[" + p.want.map((x) => x.join(", ")).join("], [") + "]]")
      : p.want;
    if (p.layer === "dual") {
      ok = cRc === 0 && jRc === 0 && cOut === wantStr && jOut === wantStr;
      detail = "C=\"" + cOut + "\" JS=\"" + jOut + "\" want=\"" + wantStr + "\"";
    } else {
      ok = cRc === 0 && cOut === wantStr && jRc === p.jsWant;
      detail = "C=\"" + cOut + "\" JSrc=" + jRc;
    }
  }
  if (!ok) { bad++; }
  rows.push({ id: p.id + " [" + p.layer + " " + p.kind + "]", ok, detail });
}

/* 用例多态参数:plan 未携带 p——从 sturm.json 重查 */
function findPoly(p) {
  for (const c of J.dualCases) { if (c.id === p.id) { return c.p; } }
  for (const c of J.cOnlyDEG) { if (c.id === p.id) { return c.p; } }
  for (const c of J.cOnlyOVF) { if (c.id === p.id) { return c.p; } }
  return "";
}

let md = "# SMOKE-DIFF-STURM (v2.5.0 F3)\n\nC (ac_pro_sturm) vs JS (core_ratroot sturm 扩段) — " +
  plan.length + " checks\n\n| case | verdict | detail |\n|---|---|---|\n";
for (const r of rows) {
  md += "| " + r.id + " | " + (r.ok ? "OK" : "MISMATCH") + " | " + r.detail + " |\n";
}
md += "\n**" + (plan.length - bad) + "/" + plan.length + " aligned, exit " + (bad === 0 ? 0 : 1) + "**\n";
fs.writeFileSync(OUT_MD, md);
console.log("diff-sturm: " + (plan.length - bad) + "/" + plan.length + " aligned -> " + OUT_MD);
process.exit(bad === 0 ? 0 : 1);
