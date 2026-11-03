/* ApolloCalc · test-complex-ext.js — v2.5.0 F1 复变函数扩展测试（run-all 自动发现）

   期望值唯一出处：tests/complex-functions.json（117 条，mpmath 40dps 独立生成，
   契约 docs/contracts/complex-ext-v25.md §9）。比较口径按契约 §8：
     err = |a-b| / max(1, |a|, |b|) <= 1e-9（±0 不比符号）；错误码精确。
   另含：命名空间接线（setComplexA → ApolloComplexExt.cXxx → getComplexTmp*）、寄存器
   协议（仅 cpow 覆写 A）、刚好通过/刚好拒绝双侧边界、cgamma 实轴交叉回归锚
   （vs core_special.js ApolloSpecial.gamma）。

   本文件为新增测试文件（现有 test-*.js 红线一字不改）；run-all readdirSync 自动发现。
   自报行格式：==== complex-ext: N pass, M fail ====（run-all parseCounts 形态 1）。 */
var fs = require("fs");
var url = require("url");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_complex.js", "core_special.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

var pass = 0, fail = 0;
function approx(a, b, tol) { return Math.abs(a - b) <= (tol || 1e-9); }

/* 契约 §8 比较式 */
function cxNear(a, b) {
  var den = Math.max(1.0, Math.abs(a), Math.abs(b));
  return Math.abs(a - b) / den <= 1e-9;
}

function checkCond(name, cond) {
  if (cond) { pass++; } else { fail++; console.log("FAIL " + name); }
}

/* ---- 契约 JSON 全量行（117 条）经 complex 命名空间逐条断言 ---- */
var contract = JSON.parse(fs.readFileSync(__dirname + "/../../../tests/complex-functions.json", "utf8"));
var cases = contract.cases;

function runCase(c) {
  var x = c.z[0], y = c.z[1];
  var rc, re, im, wantErr, ok, name;
  _aRe = x;
  _aIm = y;
  if (c.fn === "cpow") {
    _bRe = c.w[0];
    _bIm = c.w[1];
    rc = ApolloComplexExt.cpow();
  } else if (c.fn === "cnthroot") {
    rc = ApolloComplexExt.cnthroot(c.k);
  } else {
    rc = ApolloComplexExt[c.fn]();
  }
  re = _tmpRe;
  im = _tmpIm;
  name = c.id + " " + c.fn + "(" + x + (y < 0 || Object.is(y, -0) ? "" : "+") + y + "i)" +
         (c.fn === "cnthroot" ? " k=" + c.k : "") +
         (c.fn === "cpow" ? " w=" + c.w[0] + "+" + c.w[1] + "i" : "");
  if (c.want.err !== undefined) {
    checkCond(name + " rc=" + c.want.err, rc === c.want.err);
    return;
  }
  if (c.band === "invariant") {
    /* 极点邻域行（契约 §8 r3）：跨 libm 1e-9 物理不可达，判不变量 */
    var large = Math.abs(re) > Math.abs(im) ? re : im;
    var small = Math.abs(re) > Math.abs(im) ? im : re;
    var wLarge = Math.abs(c.want.re) > Math.abs(c.want.im) ? c.want.re : c.want.im;
    var wSmall = Math.abs(c.want.re) > Math.abs(c.want.im) ? c.want.im : c.want.re;
    ok = (rc === 0) && Math.abs(large) > 1e15 && (large > 0) === (wLarge > 0) &&
         cxNear(small, wSmall);
    if (!ok) {
      console.log("FAIL " + name + " invariant rc=" + rc + " got=(" + re + ", " + im + ")");
    }
    pass += ok ? 1 : (fail++, 0);
    return;
  }
  ok = (rc === 0) && cxNear(re, c.want.re) && cxNear(im, c.want.im);
  if (!ok) {
    console.log("FAIL " + name + " rc=" + rc + " got=(" + re + ", " + im +
                ") want=(" + c.want.re + ", " + c.want.im + ")");
  }
  pass += ok ? 1 : (fail++, 0);
}

for (var ci = 0; ci < cases.length; ci++) { runCase(cases[ci]); }

/* ---- 命名空间与寄存器协议（源码层直查） ---- */

checkCond("namespace has exactly 15 contract methods",
  Object.keys(ApolloComplexExt).length === 15 &&
  ["csin","ccos","ctan","csinh","ccosh","ctanh","casin","cacos","catan",
   "casinh","cacosh","catanh","cpow","cnthroot","cgamma"].every(function (k) {
    return typeof ApolloComplexExt[k] === "function";
  }));

/* 直接函数层：csin(i) = i*sinh(1)（cx-csin-01 同款，走全局函数） */
_aRe = 0; _aIm = 1;
checkCond("cSin(i) rc=0 && imag=sinh(1)", cSin() === 0 && cxNear(_tmpIm, Math.sinh(1)) && cxNear(_tmpRe, 0));

/* 寄存器协议：单操作数 API 不改写 A/B（契约 §3） */
_aRe = 0.3; _aIm = 0.4; _bRe = 9; _bIm = 8;
cSin();
checkCond("cSin preserves A", _aRe === 0.3 && _aIm === 0.4);
checkCond("cSin preserves B", _bRe === 9 && _bIm === 8);

/* cpow 覆写 A：调后 _aRe == 3*ln2（契约 §3 沿袭 cPowReal 约定） */
_aRe = 2; _aIm = 0; _bRe = 3; _bIm = 0;
checkCond("cPowC(2,3)=8 && A clobbered to 3*ln2",
  cPowC() === 0 && cxNear(_tmpRe, 8) && cxNear(_aRe, 3 * Math.log(2)) && cxNear(_aIm, 0));

/* 刚好通过/刚好拒绝：catanh 支点 vs 支点旁（JSON 行互为边界对） */
_aRe = 1; _aIm = 0;
checkCond("catanh(1+0i) rc=-1（刚好拒绝）", ApolloComplexExt.catanh() === -1);
_aRe = 0.999999999; _aIm = 0;
checkCond("catanh(0.999999999) rc=0（刚好通过）", ApolloComplexExt.catanh() === 0);

/* cnthroot 参数域：k=0/-2 → -3 */
_aRe = 8; _aIm = 0;
checkCond("cnthroot(0) rc=-3 && cnthroot(-2) rc=-3", cNthRoot(0) === -3 && cNthRoot(-2) === -3);

/* ctan 实极点大有限值（cx-ctan-05 契约锚） */
_aRe = Math.PI / 2; _aIm = 0;
checkCond("ctan(pi/2+0i) large finite ~1.633e16",
  cTan() === 0 && _tmpRe > 1e15 && cxNear(_tmpIm, 0));

/* 交叉回归锚：cgamma 实轴 == ApolloSpecial.gamma（契约 §8 cgamma 标定②） */
var xs = [0.5, 1.0, 2.5, 7.3, 0.1];
var crossOK = true;
for (var xi = 0; xi < xs.length; xi++) {
  _aRe = xs[xi]; _aIm = 0;
  cGamma();
  var g = ApolloSpecial.gamma(xs[xi]);
  if (!(cxNear(_tmpRe, g) && cxNear(_tmpIm, 0))) { crossOK = false; }
}
checkCond("cgamma real-axis cross-anchor vs special.gamma (5 pts) 1e-9", crossOK);

/* ---- ESM 导出接口接线（压缩产物调用序冒烟） ---- */

var fnFail0 = fail;
import(url.pathToFileURL(dir + "/core_kernel.js").href).then(function (mod) {
  var k = mod.default;
  checkCond("kernel.complex 命名空间接线", typeof k.complex === "object" &&
    typeof k.complex.csin === "function" && typeof k.complex.cgamma === "function");
  k.setComplexA(0, 1);
  checkCond("kernel setComplexA→ApolloComplexExt.csin→getComplexTmpIm=sinh(1)",
    k.complex.csin() === 0 && cxNear(k.getComplexTmpIm(), Math.sinh(1)) &&
    cxNear(k.getComplexTmpRe(), 0));
  k.setComplexA(2.718281828459045, 0);
  k.setComplexB(0, Math.PI);
  checkCond("kernel ApolloComplexExt.cpow(e,iπ)≈-1 欧拉锚",
    k.complex.cpow() === 0 && cxNear(k.getComplexTmpRe(), -1) && cxNear(k.getComplexTmpIm(), 0));
  k.setComplexA(0, 1);
  checkCond("kernel ApolloComplexExt.catan(i) rc=-1 经接口", k.complex.catan() === -1);
  k.setComplexA(1, 1);
  checkCond("kernel ApolloComplexExt.cgamma(1+i) 文献锚",
    k.complex.cgamma() === 0 && cxNear(k.getComplexTmpRe(), 0.49801566811835604) &&
    cxNear(k.getComplexTmpIm(), -0.15494982830181069));

  console.log("---- ESM 接线: " + (pass - (cases.length + 10)) + " passed, " +
              (fail - fnFail0) + " failed ----");
  if (fail === 0) {
    console.log("==== complex-ext: " + pass + " pass, " + fail + " fail ====");
  } else {
    console.log("==== complex-ext: " + pass + " pass, " + fail + " fail ====");
    process.exit(1);
  }
}, function (err) {
  console.log("FAIL 无法加载 core_kernel.js（请先运行 node test/build-kernel.js --minify）: " +
              (err && err.message ? err.message : err));
  process.exit(1);
});
