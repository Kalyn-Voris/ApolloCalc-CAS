/* odefuzz.js — JS 侧 ODE 差分探针:stdin 每行一个输入,输出 "code\tout"
 * 固化自 .auto/odefuzz.js（v2.4.2 会话资产,F0 固化进 git,R7）;
 * 唯一改动:src 路径由 CWD 相对（"JS/128KB/src/"）改为脚本位置相对,
 * 任意工作目录可调用。用法:
 *   node tools/odefuzz.js < tests/ode-fuzz-corpus.txt > js-out.txt
 * 与 C 侧探针（tools/odefuzz.c 编译）同语料逐行 code\tout 对拍。 */
const fs = require("fs");
const path = require("path");
const code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
              "core_evaluator.js", "core_base.js", "core_solvers.js", "core_ratroot.js",
              "core_ode.js"]
  .map(f => fs.readFileSync(path.join(__dirname, "..", "JS", "128KB", "src", f), "utf8")).join("\n");
eval(code);
const lines = fs.readFileSync(0, "utf8").split(/\r?\n/);
for (const line of lines) {
  if (line === "") { continue; }
  const r = ApolloOde.solveOde(line);
  console.log(r.code === 0 ? "0\t" + r.out : String(r.code) + "\t");
}
