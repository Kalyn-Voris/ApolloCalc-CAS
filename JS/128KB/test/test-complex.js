/* ApolloCalc · test-complex.js — 复数域函数测试：cExp / cLn / cPowReal

   Node 下从 ../src 按序拼接文件 1-6 后执行；末尾经 core_kernel.js 导出接口
   冒烟 UI 层调用序 setComplexA → cXxx → getComplexTmp*。 */
var fs = require("fs");
var url = require("url");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_solvers.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

var pass = 0, fail = 0;
function approx(a, b, tol) { return Math.abs(a - b) <= (tol || 1e-9); }

function checkC(name, gotRe, gotIm, wantRe, wantIm, tol) {
  if (approx(gotRe, wantRe, tol) && approx(gotIm, wantIm, tol)) { pass++; }
  else { fail++; console.log("FAIL " + name + " got=(" + gotRe + ", " + gotIm + ") want=(" + wantRe + ", " + wantIm + ")"); }
}

function checkCond(name, cond) {
  if (cond) { pass++; } else { fail++; console.log("FAIL " + name); }
}

angleMode = "RAD";

/* 模块一：cExp */

_aRe = 0; _aIm = Math.PI;
cExp();
checkC("cExp e^(iπ) = -1+0i", _tmpRe, _tmpIm, -1, 0, 1e-12);
checkCond("cExp e^(iπ) 虚部 < 1e-15", Math.abs(_tmpIm) < 1e-15);

_aRe = 0; _aIm = Math.PI / 2;
cExp();
checkC("cExp e^(iπ/2) = 0+1i", _tmpRe, _tmpIm, 0, 1, 1e-15);

_aRe = 0; _aIm = 0;
cExp();
checkC("cExp e^0 = 1+0i", _tmpRe, _tmpIm, 1, 0, 0);

_aRe = 1; _aIm = 2;
cExp();
checkC("cExp |e^(1+2i)|=e 且 arg=2",
       Math.sqrt(_tmpRe * _tmpRe + _tmpIm * _tmpIm), Math.atan2(_tmpIm, _tmpRe),
       Math.E, 2, 1e-12);

/* 模块二：cLn */

_aRe = 1; _aIm = 0;
cLn();
checkC("cLn 1 = 0+0i", _tmpRe, _tmpIm, 0, 0, 0);

_aRe = -1; _aIm = 0;
cLn();
checkC("cLn (-1) = 0+iπ", _tmpRe, _tmpIm, 0, Math.PI, 1e-15);

_aRe = 0; _aIm = 1;
cLn();
checkC("cLn i = 0+iπ/2", _tmpRe, _tmpIm, 0, Math.PI / 2, 1e-15);

/* ln(0) 返回 -Infinity+0i 是交上层 isFinite 判错的哨兵值 */
_aRe = 0; _aIm = 0;
cLn();
checkCond("cLn 0 = -Infinity+0i 哨兵", _tmpRe === -Infinity && _tmpIm === 0);

_aRe = Math.exp(3); _aIm = 0;
cLn();
checkC("cLn e^3 = 3+0i", _tmpRe, _tmpIm, 3, 0, 1e-9);

_aRe = 0; _aIm = Math.PI;
cExp();
_aRe = _tmpRe; _aIm = _tmpIm;
cLn();
checkC("cExp→cLn 回环 e^(iπ)", _tmpRe, _tmpIm, 0, Math.PI, 1e-12);

/* 模块三：cPowReal */

/* 2^3 走 ln/exp 路径，虚部恒为 0 */
_aRe = 2; _aIm = 0;
cPowReal(3);
checkC("cPowReal 2^3 = 8+0i", _tmpRe, _tmpIm, 8, 0, 1e-12);
checkCond("cPowReal 2^3 虚部 === 0", _tmpIm === 0);

_aRe = -1; _aIm = 0;
cPowReal(0.5);
checkC("cPowReal (-1)^0.5 = 0+1i", _tmpRe, _tmpIm, 0, 1, 1e-15);

_aRe = 2; _aIm = 0;
cPowReal(-1);
checkC("cPowReal 2^(-1) = 0.5+0i", _tmpRe, _tmpIm, 0.5, 0, 1e-12);

_aRe = 0; _aIm = 0;
cPowReal(0);
checkCond("cPowReal 0^0 = NaN", isNaN(_tmpRe) && isNaN(_tmpIm));

_aRe = 0; _aIm = 0;
cPowReal(2);
checkC("cPowReal 0^2 = 0+0i", _tmpRe, _tmpIm, 0, 0, 0);

_aRe = 0; _aIm = 0;
cPowReal(-1);
checkCond("cPowReal 0^(-1) = NaN", isNaN(_tmpRe) && isNaN(_tmpIm));

/* 1^7 走 ln 1 = 0 路径，结果精确 */
_aRe = 1; _aIm = 0;
cPowReal(7);
checkC("cPowReal 1^7 = 1+0i", _tmpRe, _tmpIm, 1, 0, 0);

_aRe = 2; _aIm = 3;
cPowInt(2);
var intRe = _tmpRe, intIm = _tmpIm;
_aRe = 2; _aIm = 3;
cPowReal(2);
checkC("cPowReal 与 cPowInt 一致 (2+3i)^2", _tmpRe, _tmpIm, intRe, intIm, 1e-12);

var fnPass = pass, fnFail = fail;
console.log("---- 复数域函数: " + fnPass + " passed, " + fnFail + " failed ----");

/* ESM 接线冒烟：core_kernel.js 导出接口走 UI 调用序 */
import(url.pathToFileURL(dir + "/core_kernel.js").href).then(function (mod) {
  var k = mod.default;

  k.setComplexA(0, Math.PI);
  k.cExp();
  checkC("kernel.cExp 欧拉恒等式", k.getComplexTmpRe(), k.getComplexTmpIm(), -1, 0, 1e-12);

  k.setComplexA(-1, 0);
  k.cLn();
  checkC("kernel.cLn (-1) = iπ", k.getComplexTmpRe(), k.getComplexTmpIm(), 0, Math.PI, 1e-12);

  k.setComplexA(2, 0);
  k.cPowReal(3);
  checkC("kernel.cPowReal 2^3 = 8", k.getComplexTmpRe(), k.getComplexTmpIm(), 8, 0, 1e-12);

  console.log("---- ESM 接线: 3 passed, " + (fail - fnFail) + " failed ----");
  if (fail === 0) {
    console.log("COMPLEX TESTS OK, all " + pass + " passed");
  } else {
    console.log("COMPLEX TESTS FAILED: pass=" + pass + " fail=" + fail);
    process.exit(1);
  }
}, function (err) {
  console.log("FAIL 无法加载 core_kernel.js（请先运行 node test/build-kernel.js）: " +
              (err && err.message ? err.message : err));
  process.exit(1);
});
