/* ApolloCalc · diff-sysolve.js — F2 二元方程组 GB 消元数值全解 双端对拍驱动

   机制同 diff-smoke128.js:同一组用例分别喂给 C 探针(probe_sysolve_csv.c,
   用与内核一致的 obj 缓存直链编译运行)与 JS 侧(core_groebner.js sysolve
   扩段,拼接 eval 加载),逐用例比对:
     错误码精确相等;成功时解列表字符串解析出的 (x,y) 四分量按序 |Δ| ≤ 1e-9
     (契约 sysolve-format-v25 §5.2 排序冻结,两侧同序逐对比)。
   既有 50 例 smoke 管线一行不动;全部通过退出码 0,否则 1。
   报告:tests/bench/SMOKE-DIFF-SYSOLVE.md。
   运行:node tests/bench/diff-sysolve.js   (仓库根;需 mingw32-make/gcc) */
"use strict";
const fs = require("fs");
const path = require("path");
const cp = require("child_process");

const ROOT = path.join(__dirname, "..", "..");
const CDIR = path.join(ROOT, "C", "128KB-ProTEXT");
const OUT_MD = path.join(__dirname, "SMOKE-DIFF-SYSOLVE.md");
const TOL = 1e-9;

const CASES = JSON.parse(fs.readFileSync(path.join(ROOT, "tests", "systems.json"), "utf8")).cases;

/* ---- JS 侧:拼接 eval 加载(core_groebner.js sysolve 扩段) ---- */
const JS_FILES = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
  "core_evaluator.js", "core_base.js", "core_solvers.js", "core_ratroot.js",
  "core_symboltree.js", "core_integrate.js", "core_multivar.js", "core_matrix.js",
  "core_special.js", "core_numtheory.js", "core_ode.js", "core_groebner.js"];
const jsCode = JS_FILES.map((f) => fs.readFileSync(path.join(ROOT, "JS", "128KB", "src", f), "utf8")).join("\n");
const jsMod = { exports: {} };
(0, eval)(jsCode);   /* 间接 eval:var 落 globalThis(严格模式下直接 eval 会自建作用域) */
jsMod.ApolloGroebner = globalThis.ApolloGroebner;

function jsRun(f, g) {
  const rc = jsMod.ApolloGroebner.sysolve(f, g);
  return rc + "|" + jsMod.ApolloGroebner.getSysolveString();
}

/* ---- C 侧:obj 缓存直链探针(mingw32-make 先保证 obj 齐) ---- */
function sh(cmd, opts) {
  return cp.spawnSync(cmd[0], cmd.slice(1), Object.assign({ encoding: "utf8", cwd: CDIR }, opts || {}));
}
const mk = sh(["mingw32-make", "SHELL=cmd.exe", "pro_runner"]);
if (mk.status !== 0) {
  console.error("diff-sysolve: mingw32-make pro_runner failed:\n" + (mk.stderr || mk.stdout));
  process.exit(1);
}
const probeSrc = path.join(CDIR, "test", "probe_sysolve_csv.c");
const probeExe = path.join(CDIR, "probe_sysolve.exe");
const objs = fs.readdirSync(path.join(CDIR, "obj", "src")).filter((f) => f.endsWith(".o"))
  .map((f) => path.join(CDIR, "obj", "src", f));
const cc = sh(["gcc", "-std=c99", "-O2", "-Isrc", "-I../../C/128KB/src", "-DAC_PRO=1",
  probeSrc].concat(objs).concat(["-o", probeExe, "-lm"]));
if (cc.status !== 0) {
  console.error("diff-sysolve: probe compile failed:\n" + (cc.stderr || cc.stdout));
  process.exit(1);
}

const input = CASES.map((c) => c.f + "|" + c.g).join("\n") + "\n";
const run = cp.spawnSync(probeExe, { input, encoding: "utf8", maxBuffer: 16 * 1024 * 1024 });
if (run.status !== 0) {
  console.error("diff-sysolve: probe run failed:\n" + (run.stderr || ""));
  process.exit(1);
}
const cLines = run.stdout.split(/\r?\n/).filter((l) => l.length > 0);
if (cLines.length !== CASES.length) {
  console.error("diff-sysolve: probe line count mismatch: " + cLines.length + " vs " + CASES.length);
  process.exit(1);
}

/* ---- 比对:错误码精确;解列表按序逐对 |Δ| ≤ 1e-9 ---- */
function parseSolutions(s) {
  /* "[(x, y), ...]" → [[xre, xim, yre, yim], ...];复根 [re]±|im|i / i / -i */
  const out = [];
  if (s === "[]") { return out; }
  const body = s.slice(1, -1);
  const rePair = /\(([^()]*)\)/g;
  let m;
  function coord(tok) {
    tok = tok.trim();
    const mi = tok.match(/^(\[[-0-9./]+])([+-])([0-9./]+)i$/);
    if (mi) {
      const re = mi[1].slice(1, -1);
      const im = (mi[2] === "-" ? -1 : 1) * parseFloat(mi[3]);
      return [parseFloat(re), im];
    }
    const mi2 = tok.match(/^([+-]?)([0-9./]*)i$/);
    if (mi2) {
      const mag = mi2[2] === "" ? 1 : parseFloat(mi2[2]);
      return [0, (mi2[1] === "-" ? -1 : 1) * mag];
    }
    return [parseFloat(tok), 0];
  }
  while ((m = rePair.exec(body)) !== null) {
    const parts = m[1].split(", ");
    const x = coord(parts[0]);
    const y = coord(parts[1]);
    out.push([x[0], x[1], y[0], y[1]]);
  }
  return out;
}
function cmpSolutions(a, b) {
  if (a.length !== b.length) { return false; }
  for (let i = 0; i < a.length; i++) {
    for (let k = 0; k < 4; k++) {
      if (Math.abs(a[i][k] - b[i][k]) > TOL) { return false; }
    }
  }
  return true;
}

const rows = [];
let bad = 0;
for (let i = 0; i < CASES.length; i++) {
  const c = CASES[i];
  const cLine = cLines[i];
  const jLine = jsRun(c.f, c.g);
  const sep = cLine.indexOf("|");
  const cRc = parseInt(cLine.slice(0, sep), 10);
  const cOut = cLine.slice(sep + 1);
  const jSep = jLine.indexOf("|");
  const jRc = parseInt(jLine.slice(0, jSep), 10);
  const jOut = jLine.slice(jSep + 1);
  const okRc = cRc === jRc && cRc === c.expect;
  /* 三方对齐:C vs JS(逐字解析)与 C vs JSON 期望解(数值 ≤1e-9) */
  const cSol = parseSolutions(cOut);
  const jSol = parseSolutions(jOut);
  const wantSol = Array.isArray(c.solutions) ? c.solutions : [];
  const okSol = okRc && (cRc !== 0 || (cmpSolutions(cSol, jSol) && cmpSolutions(cSol, wantSol)));
  const ok = okRc && okSol;
  if (!ok) { bad++; }
  rows.push({ id: c.id, ok, cRc, jRc, cOut, jOut });
}

/* ---- 报告 ---- */
let md = "# SMOKE-DIFF-SYSOLVE (v2.5.0 F2)\n\nC (ac_pro_sysolve) vs JS (core_groebner.sysolve) — " +
  CASES.length + " cases, tolerance " + TOL + "\n\n| case | verdict | C rc | JS rc | C out | JS out |\n|---|---|---|---|---|---|\n";
for (const r of rows) {
  md += "| " + r.id + " | " + (r.ok ? "OK" : "MISMATCH") + " | " + r.cRc + " | " + r.jRc + " | `" +
    r.cOut.slice(0, 60) + "` | `" + r.jOut.slice(0, 60) + "` |\n";
}
md += "\n**" + (CASES.length - bad) + "/" + CASES.length + " aligned, exit " + (bad === 0 ? 0 : 1) + "**\n";
fs.writeFileSync(OUT_MD, md);
console.log("diff-sysolve: " + (CASES.length - bad) + "/" + CASES.length + " aligned -> " + OUT_MD);
process.exit(bad === 0 ? 0 : 1);
