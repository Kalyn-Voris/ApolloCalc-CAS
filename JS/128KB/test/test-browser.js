/* ApolloCalc-CAS · JS/128KB 浏览器环境模拟测试
 * 用 vm 沙箱模拟 <script> 经典脚本环境（无 require / module / exports），
 * 验证三个自包含模块的浏览器加载通道 + 功能探针。
 *
 * core_kernel.js 是 ESM（含 export 语句），浏览器经
 * <script type="module"> 加载，其导出链路由 build-kernel.js 第 5 步
 * （动态 import + 43 项冒烟）覆盖，此处不重复。
 *
 * 运行：node test/test-browser.js   （JS/128KB 目录下）
 * 全过输出：==== browser-sim: N pass, 0 fail ====
 */
"use strict";

var fs = require("fs");
var path = require("path");
var vm = require("vm");

var pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}

/* 经典脚本沙箱：只有 console，刻意不给 require/module/exports。
 * runInContext 的经典脚本中顶层 var 声明会落到沙箱全局对象上 ——
 * 与浏览器 <script> 的行为一致，这正是三个模块的第三导出通道。 */
function loadInBrowser(file) {
  var sandbox = { console: console };
  vm.createContext(sandbox);
  vm.runInContext(fs.readFileSync(path.join(__dirname, "..", "src", file), "utf8"),
                  sandbox, { filename: file });
  return sandbox;
}

/* 一、multivar */
var sb1 = loadInBrowser("core_multivar.js");
ok("浏览器全局 ApolloMultivar 挂载", typeof sb1.ApolloMultivar === "object" && sb1.ApolloMultivar !== null);
var bm = sb1.ApolloMultivar;
ok("multivar parse 经典脚本可用", bm.parse("3x^2y + 2xy - 5", 0) === 3);
ok("multivar evalSlot 经典脚本可用", bm.evalSlot(0, 2, 3) === 43);
bm.parse("x^2y + xy^2", 1);
bm.diff(1, 0, 2);
ok("multivar diff 经典脚本可用", bm.polyToString(2) === "2xy + y^2", bm.polyToString(2));

/* 二、matrix */
var sb2 = loadInBrowser("core_matrix.js");
ok("浏览器全局 ApolloMatrix 挂载", typeof sb2.ApolloMatrix === "object" && sb2.ApolloMatrix !== null);
var bx = sb2.ApolloMatrix;
bx.setA(3, 3, 0, 6); bx.setA(3, 3, 1, 1); bx.setA(3, 3, 2, 1);
bx.setA(3, 3, 3, 4); bx.setA(3, 3, 4, -2); bx.setA(3, 3, 5, 5);
bx.setA(3, 3, 6, 2); bx.setA(3, 3, 7, 8); bx.setA(3, 3, 8, 7);
ok("matrix det 经典脚本可用", bx.det() === -306, bx.det());

/* 三、special */
var sb3 = loadInBrowser("core_special.js");
ok("浏览器全局 ApolloSpecial 挂载", typeof sb3.ApolloSpecial === "object" && sb3.ApolloSpecial !== null);
var bs = sb3.ApolloSpecial;
ok("special erf 经典脚本可用", Math.abs(bs.erf(1) - 0.8427007929497149) < 2e-7, bs.erf(1));
ok("special gamma 经典脚本可用", Math.abs(bs.gamma(5) - 24) < 1e-9, bs.gamma(5));

/* 四、沙箱隔离：模块不偷用宿主的 require/module（加载期零访问即可挂载已证明） */
ok("沙箱内无 module 注入仍正常工作", typeof sb1.module === "undefined" &&
   typeof sb1.require === "undefined");

console.log("==== browser-sim: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
