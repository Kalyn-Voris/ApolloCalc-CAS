/* ============================================================
 * run-all.js — JS/128KB 全量测试入口：顺序运行同目录全部 test-*.js（当前 12 个）
 * 运行：node test/run-all.js   （JS/128KB 目录下；亦可从仓库根以路径直呼）
 *
 * 规则：
 *   · 每个文件以 node 子进程顺序执行，exit 0 才算过
 *   · 每跑完一个打印一行「PASS/FAIL 文件名 (用例数)」
 *   · 末行打印 TOTAL: <n> pass, <m> fail —— n/m 为各测试文件自报用例数聚合（基线 603/0）
 *   · 任一文件非零退出（或其自报 fail>0 / 汇总行不可解析）→ 整体退出码 1
 *
 * 注：test-*.js 为红线文件不可改，其汇总行格式即契约，共三种形态（parseCounts 逐一解析，
 *     自末行向前取首个命中）：
 *       1) ==== <name>: N pass[, I improve], M fail ====   （9 个常规 + consistency）
 *       2) ---- N passed, M failed ----                    （test-evaluator.js）
 *       3) COMPLEX TESTS OK, all N passed                  （test-complex.js）
 * run-all.js 自身不在测试文件红线内。
 * ============================================================ */
var fs = require("fs");
var path = require("path");
var cp = require("child_process");

var files = fs.readdirSync(__dirname)
  .filter(function (f) { return /^test-.*\.js$/.test(f); })
  .sort();

/* 自末行向前解析测试文件自报的用例数；解析不到返回 pass=-1（fail-fast，不猜测） */
function parseCounts(stdout, exitCode) {
  var lines = stdout.split(/\r?\n/);
  var m, k;
  for (k = lines.length - 1; k >= 0; k--) {
    m = lines[k].match(/====[^=\n]*:\s*(\d+) pass(?:,\s*\d+ improve)?,\s*(\d+) fail\s*====/);
    if (m) { return { pass: +m[1], fail: +m[2] }; }
  }
  for (k = lines.length - 1; k >= 0; k--) {
    m = lines[k].match(/----\s*(\d+) passed,\s*(\d+) failed\s*----/);
    if (m) { return { pass: +m[1], fail: +m[2] }; }
  }
  for (k = lines.length - 1; k >= 0; k--) {
    m = lines[k].match(/all (\d+) passed/);
    if (m) { return { pass: +m[1], fail: exitCode === 0 ? 0 : -1 }; }
  }
  return { pass: -1, fail: -1 };
}

var totalPass = 0, totalFail = 0, badFiles = 0;
for (var ti = 0; ti < files.length; ti++) {
  var f = files[ti];
  var r = cp.spawnSync(process.execPath, [path.join(__dirname, f)], { encoding: "utf8" });
  var cnt = parseCounts(r.stdout || "", r.status);
  if (r.status === 0 && cnt.pass >= 0 && cnt.fail === 0) {
    totalPass += cnt.pass;
    totalFail += cnt.fail;
    console.log("PASS " + f + " (" + cnt.pass + " pass, " + cnt.fail + " fail)");
  } else {
    badFiles++;
    console.log("FAIL " + f + " (exit=" + r.status +
      (cnt.pass >= 0 ? ", " + cnt.pass + " pass, " + cnt.fail + " fail" : ", 汇总行不可解析") + ")");
    if (r.stdout) {
      console.log("---- " + f + " stdout 尾部 ----\n" + r.stdout.split(/\r?\n/).slice(-12).join("\n"));
    }
    if (r.stderr) {
      console.log("---- " + f + " stderr 尾部 ----\n" + r.stderr.split(/\r?\n/).slice(-12).join("\n"));
    }
  }
}

/* TOTAL 只聚合可解析用例数；文件级失败由上方 FAIL 行与下方附加行、以及非零退出码承载 */
console.log("TOTAL: " + totalPass + " pass, " + totalFail + " fail");
if (badFiles > 0) {
  console.log("RUN-ALL FAILED: " + badFiles + "/" + files.length + " 个测试文件未通过");
  process.exit(1);
}
console.log("RUN-ALL OK: " + files.length + "/" + files.length + " 个测试文件全部通过");
