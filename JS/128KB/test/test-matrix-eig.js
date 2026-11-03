/* ApolloCalc · test-matrix-eig.js — 一般实矩阵特征值（v2.5.0 F4）测试

   被测模块：src/core_matrix.js 的 proSchur* API（ac_pro_schur.c 逐条移植，
   契约 docs/contracts/eigen-format-v25.md；既有 proEig/proLU/proQR 与 3×3
   寄存器层由红线 test-matrix.js / test-matrix-pro.js 覆盖，本文件不重复）。
   检查口径与 C test_pro_schur.c 同源：谱多重集 ±1e-9、拉开 (>1e-6) 的用例
   逐位断言契约全序 (|λ|,Re,Im) 降序、复共轭对相邻 +Im 在前、实特征向量
   ‖Av−λv‖∞ ≤ 1e-9·(1+‖A‖∞)（对原始 A 复核）、复特征值无向量、
   符号规范化不变式（每列最大 |·| 分量置正、并列取最小索引）。
   运行：node test/test-matrix-eig.js   （JS/128KB 目录下）
   全过输出：==== matrix-eig: N pass, 0 fail ==== */
"use strict";

var M = require("../src/core_matrix.js");

var pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}
function close(a, b, tol) {
  if (a === b) { return true; }
  if (typeof a !== "number" || typeof b !== "number" || a !== a || b !== b) { return false; }
  return Math.abs(a - b) <= (tol === undefined ? 1e-9 : tol);
}

/* 测试侧助手（测试期特权，不在被测零 GC 热路径内） */
function setPro(n, flat) {
  for (var i = 0; i < n * n; i++) { M.proSetA(n, n, i, flat[i]); }
}
function infNorm(n, flat) {
  var best = 0, i, j, s;
  for (i = 0; i < n; i++) {
    s = 0;
    for (j = 0; j < n; j++) { s += Math.abs(flat[i * n + j]); }
    if (s > best) { best = s; }
  }
  return best;
}
/* ‖A v − λ v‖∞（对原始 A；v 取自 proSchurVector） */
function vecResid(n, flat, k) {
  var worst = 0, i, j, s, d;
  for (i = 0; i < n; i++) {
    s = 0;
    for (j = 0; j < n; j++) { s += flat[i * n + j] * M.proSchurVector(k, j); }
    d = Math.abs(s - M.proSchurRe(k) * M.proSchurVector(k, i));
    if (d > worst) { worst = d; }
  }
  return worst;
}
function allRealResids(n, flat) {
  var worst = 0, k;
  for (k = 0; k < n; k++) {
    if (M.proSchurHasVec(k) === 1) {
      var r = vecResid(n, flat, k);
      if (r > worst) { worst = r; }
    }
  }
  return worst;
}
/* 符号规范化不变式：最大 |·| 分量为正且为首个最大 */
function vecNormOK(n, k) {
  var big = -1, imax = -1, i, a;
  for (i = 0; i < n; i++) {
    a = Math.abs(M.proSchurVector(k, i));
    if (a > big) { big = a; imax = i; }
  }
  return imax >= 0 && M.proSchurVector(k, imax) > 0;
}
/* 谱多重集匹配（C sch_spectrum_match 同思想） */
function spectrumMatch(n, want) {
  var used = [], i, j;
  for (i = 0; i < n; i++) { used.push(false); }
  if (M.proSchurCount() !== n) { return false; }
  for (i = 0; i < n; i++) {
    var found = -1;
    for (j = 0; j < n; j++) {
      if (used[j]) { continue; }
      if (close(M.proSchurRe(j), want[i][0]) && close(M.proSchurIm(j), want[i][1])) { found = j; break; }
    }
    if (found < 0) { return false; }
    used[found] = true;
  }
  return true;
}
/* 共轭对相邻 +Im 在前（结构不变式） */
function pairingOK(n) {
  for (var k = 0; k < n; k++) {
    if (M.proSchurIm(k) !== 0) {
      if (!(M.proSchurIm(k) > 0)) { return false; }
      if (k + 1 >= n) { return false; }
      if (M.proSchurIm(k + 1) >= 0) { return false; }
      if (M.proSchurRe(k) !== M.proSchurRe(k + 1)) { return false; }
      if (Math.abs(M.proSchurIm(k)) !== Math.abs(M.proSchurIm(k + 1))) { return false; }
      k++;
    }
  }
  return true;
}
var SQ2 = Math.sqrt(2), SQ3 = Math.sqrt(3);
var PHI = (1 + Math.sqrt(5)) / 2, PSI = (1 - Math.sqrt(5)) / 2;
var C60 = 0.8660254037844386;

/* ---- n=1 边界（3 例 × 2 检查） ---- */
setPro(1, [5]);
ok("n1[5] rc/count", M.proSchur() === 1 && M.proSchurCount() === 1);
ok("n1[5] lam/vec", M.proSchurRe(0) === 5 && M.proSchurIm(0) === 0 &&
  M.proSchurHasVec(0) === 1 && M.proSchurVector(0, 0) === 1);
setPro(1, [-3]);
ok("n1[-3] rc/lam", M.proSchur() === 1 && M.proSchurRe(0) === -3 && M.proSchurIm(0) === 0);
ok("n1[-3] vec +1", M.proSchurVector(0, 0) === 1);   /* 符号规范化：max|·| 置正 */
setPro(1, [0]);
ok("n1[0] rc/lam", M.proSchur() === 1 && M.proSchurRe(0) === 0 && M.proSchurIm(0) === 0);
ok("n1[0] vec unit", M.proSchurVector(0, 0) === 1 && vecResid(1, [0], 0) === 0);

/* ---- diag3：确定性 |λ| 降序 + 规范单位向量 ---- */
var d3 = [1, 0, 0, 0, 2, 0, 0, 0, 3];
setPro(3, d3);
ok("diag3 rc/count", M.proSchur() === 1 && M.proSchurCount() === 3);
ok("diag3 order 3,2,1", M.proSchurRe(0) === 3 && M.proSchurRe(1) === 2 && M.proSchurRe(2) === 1);
ok("diag3 im exact 0", M.proSchurIm(0) === 0 && M.proSchurIm(1) === 0 && M.proSchurIm(2) === 0);
ok("diag3 vec e2,e1,e0", M.proSchurVector(0, 2) === 1 && M.proSchurVector(1, 1) === 1 &&
  M.proSchurVector(2, 0) === 1);
ok("diag3 resid <= 1e-9", allRealResids(3, d3) <= 1e-9);
ok("diag3 vec norm inv", vecNormOK(3, 0) && vecNormOK(3, 1) && vecNormOK(3, 2));

/* ---- diag3_neg：|λ| 降序 = -3,-2,-1 ---- */
setPro(3, [-1, 0, 0, 0, -2, 0, 0, 0, -3]);
ok("diag3neg rc/order", M.proSchur() === 1 &&
  M.proSchurRe(0) === -3 && M.proSchurRe(1) === -2 && M.proSchurRe(2) === -1);
ok("diag3neg resid", allRealResids(3, [-1, 0, 0, 0, -2, 0, 0, 0, -3]) <= 1e-9);

/* ---- rotation90：纯虚共轭对，复向量不产出 ---- */
var rot = [0, -1, 1, 0];
setPro(2, rot);
ok("rot90 rc/count", M.proSchur() === 1 && M.proSchurCount() === 2);
ok("rot90 spectrum {+i,-i}", M.proSchurRe(0) === 0 && M.proSchurIm(0) === 1 &&
  M.proSchurRe(1) === 0 && M.proSchurIm(1) === -1);
ok("rot90 no vectors", M.proSchurHasVec(0) === 0 && M.proSchurHasVec(1) === 0 &&
  M.proSchurVector(0, 0) !== M.proSchurVector(0, 0));   /* NaN：绝不硬凑复向量 */
ok("rot90 pairing +Im first", pairingOK(2));

/* ---- mixed_block3：实 3 与 ±i 块 ---- */
var mb3 = [0, -1, 0, 1, 0, 0, 0, 0, 3];
setPro(3, mb3);
ok("mixed3 rc/order", M.proSchur() === 1 && M.proSchurCount() === 3 && M.proSchurRe(0) === 3);
ok("mixed3 spectrum", spectrumMatch(3, [[3, 0], [0, 1], [0, -1]]));
ok("mixed3 real vec (0,0,1)", M.proSchurVector(0, 2) === 1 && vecResid(3, mb3, 0) <= 1e-9);
ok("mixed3 pairing", pairingOK(3));

/* ---- 友矩阵 x^3-1：1 与 (-1±i√3)/2 ---- */
var c3 = [0, 0, 1, 1, 0, 0, 0, 1, 0];
setPro(3, c3);
ok("c3 rc/count", M.proSchur() === 1 && M.proSchurCount() === 3);
ok("c3 spectrum (roots of unity)", spectrumMatch(3, [[1, 0], [-0.5, SQ3 / 2], [-0.5, -SQ3 / 2]]));
ok("c3 pairing", pairingOK(3));
ok("c3 real vec resid", allRealResids(3, c3) <= 1e-9);

/* ---- 友矩阵 x^3-6x^2+11x-6：谱 {1,2,3}（求解层交叉验证） ---- */
var cp123 = [0, 0, 6, 1, 0, -11, 0, 1, 6];
setPro(3, cp123);
ok("comp123 rc/order", M.proSchur() === 1 && close(M.proSchurRe(0), 3, 1e-9) &&
  close(M.proSchurRe(1), 2, 1e-9) && close(M.proSchurRe(2), 1, 1e-9));
ok("comp123 vec resid", allRealResids(3, cp123) <= 1e-9);
ok("comp123 norm inv", vecNormOK(3, 0) && vecNormOK(3, 1) && vecNormOK(3, 2));

/* ---- fib 友矩阵 x^2-x-1：φ 与 ψ ---- */
var fib = [0, 1, 1, 1];
setPro(2, fib);
ok("fib2 spectrum phi/psi", M.proSchur() === 1 && close(M.proSchurRe(0), PHI) &&
  close(M.proSchurRe(1), PSI) && M.proSchurIm(0) === 0 && M.proSchurIm(1) === 0);
ok("fib2 vec resid", allRealResids(2, fib) <= 1e-9);

/* ---- Jordan 块 [[2,1],[0,2]]：亏损谱，两向量同向（诚实，不修饰） ---- */
var j2 = [2, 1, 0, 2];
setPro(2, j2);
ok("jordan2 rc/lams", M.proSchur() === 1 && close(M.proSchurRe(0), 2) && close(M.proSchurRe(1), 2));
ok("jordan2 vecs collinear", M.proSchurHasVec(0) === 1 && M.proSchurHasVec(1) === 1 &&
  close(Math.abs(M.proSchurVector(0, 0) * M.proSchurVector(1, 0) +
                 M.proSchurVector(0, 1) * M.proSchurVector(1, 1)), 1.0, 1e-9));
ok("jordan2 vec resid", allRealResids(2, j2) <= 1e-9);

/* ---- 简并 diag(2,2,2)：重特征值，残差不变量口径 ---- */
var dg = [2, 0, 0, 0, 2, 0, 0, 0, 2];
setPro(3, dg);
ok("degen222 rc/lams", M.proSchur() === 1 && M.proSchurRe(0) === 2 &&
  M.proSchurRe(1) === 2 && M.proSchurRe(2) === 2);
ok("degen222 resid", allRealResids(3, dg) <= 1e-9);

/* ---- 4×4 两个共轭对（e^{±i60°}, e^{±i90°}） ---- */
var tp = [0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 0.5, -C60, 0, 0, C60, 0.5];
setPro(4, tp);
ok("2pairs rc/spectrum", M.proSchur() === 1 && spectrumMatch(4,
  [[0.5, C60], [0.5, -C60], [0, 1], [0, -1]]));
ok("2pairs all no vec", M.proSchurHasVec(0) === 0 && M.proSchurHasVec(1) === 0 &&
  M.proSchurHasVec(2) === 0 && M.proSchurHasVec(3) === 0);
ok("2pairs pairing", pairingOK(4));

/* ---- 12×12 友矩阵 (x^12-1)：单位圆 12 根，2 实 + 10 复 ---- */
var c12 = [], ci;
for (ci = 0; ci < 144; ci++) { c12.push(0); }
for (ci = 0; ci < 11; ci++) { c12[(ci + 1) * 12 + ci] = 1; }
c12[11] = 1;
setPro(12, c12);
ok("c12 rc/count", M.proSchur() === 1 && M.proSchurCount() === 12);
(function () {
  var nreal = 0, ncplx = 0, maxerr = 0, k;
  for (k = 0; k < 12; k++) {
    var mod = Math.sqrt(M.proSchurRe(k) * M.proSchurRe(k) + M.proSchurIm(k) * M.proSchurIm(k));
    if (Math.abs(mod - 1) > maxerr) { maxerr = Math.abs(mod - 1); }
    if (M.proSchurIm(k) === 0) { nreal++; } else { ncplx++; }
  }
  ok("c12 2 real + 10 complex, |lam|≈1", nreal === 2 && ncplx === 10 && maxerr <= 1e-9,
     "nreal=" + nreal + " ncplx=" + ncplx + " maxerr=" + maxerr);
})();
ok("c12 spectrum multiset", spectrumMatch(12, (function () {
  var w = [], k;
  for (k = 0; k < 12; k++) { w.push([Math.cos(2 * Math.PI * k / 12), Math.sin(2 * Math.PI * k / 12)]); }
  return w;
})()));
ok("c12 pairing + real resid", pairingOK(12) && allRealResids(12, c12) <= 1e-8);

/* ---- 对称三对角闭式谱：2+2cos(kπ/(n+1)) ---- */
var t3 = [2, 1, 0, 1, 2, 1, 0, 1, 2];
setPro(3, t3);
ok("tri3 order closed-form", M.proSchur() === 1 &&
  close(M.proSchurRe(0), 2 + SQ2) && close(M.proSchurRe(1), 2) && close(M.proSchurRe(2), 2 - SQ2));
ok("tri3 resid + norm inv", allRealResids(3, t3) <= 1e-9 && vecNormOK(3, 0) && vecNormOK(3, 2));

(function () {
  var n = 4, m = [], k, i;
  for (i = 0; i < 16; i++) { m.push(0); }
  for (i = 0; i < n; i++) { m[i * (n + 1)] = 2; if (i < n - 1) { m[i * n + i + 1] = 1; m[(i + 1) * n + i] = 1; } }
  setPro(n, m);
  var want = [];
  for (k = 1; k <= n; k++) { want.push(2 + 2 * Math.cos(k * Math.PI / 5)); }
  ok("tri4 order closed-form", M.proSchur() === 1 &&
    close(M.proSchurRe(0), want[0]) && close(M.proSchurRe(1), want[1]) &&
    close(M.proSchurRe(2), want[2]) && close(M.proSchurRe(3), want[3]));
  ok("tri4 resid", allRealResids(n, m) <= 1e-9);
})();

(function () {
  var n = 12, m = [], k, i;
  for (i = 0; i < 144; i++) { m.push(0); }
  for (i = 0; i < n; i++) { m[i * 13] = 2; if (i < n - 1) { m[i * 12 + i + 1] = 1; m[(i + 1) * 12 + i] = 1; } }
  setPro(n, m);
  var good = true;
  var worst = 0;
  M.proSchur();
  for (k = 0; k < n; k++) {
    var want = 2 + 2 * Math.cos((k + 1) * Math.PI / 13);
    var d = Math.abs(M.proSchurRe(k) - want);
    if (d > 1e-9) { good = false; }
    if (d > worst) { worst = d; }
  }
  ok("tri12 order closed-form (12 vals)", good, "worst=" + worst);
  ok("tri12 resid", allRealResids(n, m) <= 1e-9);
})();

/* ---- Hilbert 4：病态对称，trace/det 双不变量 ---- */
(function () {
  var n = 4, m = [], i, j;
  for (i = 0; i < n; i++) { for (j = 0; j < n; j++) { m.push(1 / (i + j + 1)); } }
  setPro(n, m);
  var rc = M.proSchur();
  var sum = 0, prod = 1, k;
  for (k = 0; k < n; k++) { sum += M.proSchurRe(k); prod *= M.proSchurRe(k); }
  var trace = 0, det = 1;
  for (i = 0; i < n; i++) { trace += m[i * 5]; }
  /* det(H4) = 1/6048000（精确有理） */
  det = 1 / 6048000;
  ok("hilbert4 rc + desc order", rc === 1 &&
    M.proSchurRe(0) > M.proSchurRe(1) && M.proSchurRe(1) > M.proSchurRe(2) && M.proSchurRe(2) > M.proSchurRe(3));
  ok("hilbert4 trace/det invariants", close(sum, trace, 1e-12) && close(prod, det, 1e-16 * 10),
     "sum=" + sum + " prod=" + prod);
  ok("hilbert4 resid", allRealResids(n, m) <= 1e-9);
})();

/* ---- 幂 2 极端尺度（缩放还原精确） ---- */
var P40 = Math.pow(2, 40), M40 = Math.pow(2, -40);
var p2b = [P40, 0, 0, M40];
setPro(2, p2b);
ok("p2big exact lams", M.proSchur() === 1 && M.proSchurRe(0) === P40 && M.proSchurRe(1) === M40);
ok("p2big vec resid", allRealResids(2, p2b) <= 1e-30);

var p2s = [1e-150, 0, 0, -2e-150];
setPro(2, p2s);
ok("p2small order/exact", M.proSchur() === 1 && M.proSchurRe(0) === -2e-150 &&
  M.proSchurRe(1) === 1e-150 && M.proSchurIm(0) === 0);

var P60 = Math.pow(2, 60), M60 = Math.pow(2, -60), P40v = Math.pow(2, 40);
var p2m = [P60, 1, 0, 0, 0, M60, 0, 0, 0, 0, 0, -P40v, 0, 0, P40v, 0];
setPro(4, p2m);
ok("p2mixed4 spectrum", M.proSchur() === 1 && M.proSchurRe(0) === P60 &&
  M.proSchurIm(1) === P40v && M.proSchurIm(2) === -P40v && M.proSchurRe(3) === M60 &&
  M.proSchurIm(0) === 0 && M.proSchurIm(3) === 0);
ok("p2mixed4 pairing + vec", pairingOK(4) && allRealResids(4, p2m) <= 1e-6);

/* ---- deflation 判据边界对：1e-13 刚好通过（立即 deflation），
   1e-11 刚好拒绝（真迭代），谱相同（输出零分叉） ---- */
var tp1 = [1, 0, 1e-13, 2], tp2 = [1, 0, 1e-11, 2];
setPro(2, tp1);
ok("tol_pass rc/order", M.proSchur() === 1 && M.proSchurRe(0) === 2 && M.proSchurRe(1) === 1);
setPro(2, tp2);
ok("tol_iterate rc/order", M.proSchur() === 1 && M.proSchurRe(0) === 2 && M.proSchurRe(1) === 1);
ok("tol pair vec resid", allRealResids(2, tp1) <= 1e-9 && allRealResids(2, tp2) <= 1e-9);

/* ---- 近亏损 [[1,1],[1e-9,1]]：disc=4e-9，双实根 1±3.16e-5 ---- */
setPro(2, [1, 1, 1e-9, 1]);
ok("neardef2 rc/spectrum", M.proSchur() === 1 &&
  close(M.proSchurRe(0), 1.0000316227766017, 1e-9) && close(M.proSchurRe(1), 0.9999683772238398, 1e-9));

/* ---- perm4：近亏损谱下反迭代 6 组全败，双侧一致 -3（契约 §5） ---- */
var perm4 = [0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0];
setPro(4, perm4);
ok("perm4 honest -3", M.proSchur() === -3 && M.proSchurCount() === 0);
ok("perm4 accessors dead", M.proSchurRe(0) === 0 && M.proSchurIm(0) === 0 &&
  M.proSchurHasVec(0) === 0 && M.proSchurVector(0, 0) !== M.proSchurVector(0, 0));

/* ---- 零矩阵：逐位 deflation，谱全 0 ---- */
setPro(2, [0, 0, 0, 0]);
ok("zero2 rc/lams", M.proSchur() === 1 && M.proSchurRe(0) === 0 && M.proSchurRe(1) === 0);
ok("zero2 vec resid 0", M.proSchurHasVec(0) === 1 && vecResid(2, [0, 0, 0, 0], 0) === 0);

/* ---- 非对称三角 / 非正规块 / 亏损 3 阶 / 简并 5,5,1,1 / 幂零 / skew3 ---- */
setPro(2, [1, 1, 0, 2]);
ok("asym_tri2 order + resid", M.proSchur() === 1 && M.proSchurRe(0) === 2 &&
  M.proSchurRe(1) === 1 && allRealResids(2, [1, 1, 0, 2]) <= 1e-9);

setPro(2, [3, 1, 0.5, 1]);
ok("nonnorm2 spectrum", M.proSchur() === 1 &&
  close(M.proSchurRe(0), 3.224744871391589) && close(M.proSchurRe(1), 0.7752551286084108));

setPro(3, [2, 1, 0, 0, 2, 1, 0, 0, 2]);
ok("jordan3 triple 2 + resid", M.proSchur() === 1 && close(M.proSchurRe(0), 2) &&
  close(M.proSchurRe(1), 2) && close(M.proSchurRe(2), 2) && allRealResids(3, [2, 1, 0, 0, 2, 1, 0, 0, 2]) <= 1e-9);

setPro(4, [5, 0, 0, 0, 0, 5, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
ok("degen5511 multiset + resid", M.proSchur() === 1 && spectrumMatch(4, [[5, 0], [5, 0], [1, 0], [1, 0]]) &&
  allRealResids(4, [5, 0, 0, 0, 0, 5, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]) <= 1e-9);

setPro(4, [0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0]);
ok("nilpotent4 all zero + resid", M.proSchur() === 1 && M.proSchurRe(0) === 0 &&
  M.proSchurRe(1) === 0 && M.proSchurRe(2) === 0 && M.proSchurRe(3) === 0 &&
  allRealResids(4, [0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0]) <= 1e-9);

var sk3 = [0, -1, 0, 1, 0, 0, 0, 0, 0];
setPro(3, sk3);
ok("skew3 pair+zero order", M.proSchur() === 1 && M.proSchurIm(0) === 1 &&
  M.proSchurIm(1) === -1 && M.proSchurRe(2) === 0 && M.proSchurHasVec(2) === 1);
ok("skew3 zero-vec resid + pairing", vecResid(3, sk3, 2) <= 1e-9 && pairingOK(3));

/* ---- skew：±1.5i、±0.5i（模长间距充分 → 全序逐位） ---- */
var sk4 = [0, -1.5, 0, 0, 1.5, 0, 0, 0, 0, 0, 0, -0.5, 0, 0, 0.5, 0];
setPro(4, sk4);
ok("skew4 order spectrum", M.proSchur() === 1 && M.proSchurIm(0) === 1.5 &&
  M.proSchurIm(1) === -1.5 && M.proSchurIm(2) === 0.5 && M.proSchurIm(3) === -0.5);
ok("skew4 pairing", pairingOK(4));

/* ---- 稠密非对称（Hessenberg 化真实工作）+ trace/det 不变量 ---- */
var dn3 = [2, 1, 3, 4, 5, 6, 7, 8, 9];
setPro(3, dn3);
ok("dense3 rc", M.proSchur() === 1);
(function () {
  /* 谱不变量对任意实/复混合成立：trace = Σ re_k；det = ∏实 · ∏(re²+im²)对 */
  var sum = 0, prod = 1, k;
  for (k = 0; k < 3; k++) { sum += M.proSchurRe(k); }
  for (k = 0; k < 3; k++) {
    if (M.proSchurIm(k) === 0) { prod *= M.proSchurRe(k); }
    else { prod *= (M.proSchurRe(k) * M.proSchurRe(k) + M.proSchurIm(k) * M.proSchurIm(k)); k++; }
  }
  ok("dense3 trace invariant", close(sum, 16, 1e-9), "sum=" + sum);
  ok("dense3 det invariant", close(prod, -9, 1e-6), "prod=" + prod);
})();
ok("dense3 pairing", pairingOK(3));

/* ---- 参数拒绝（双侧边界：12 阶刚好通过在 c12 已覆盖，13 阶刚好拒绝） ---- */
M.reset();
ok("unset input -1", M.proSchur() === -1 && M.getError() === 1 && M.proSchurCount() === 0);
setPro(2, [1, 2, 3, 4]);
ok("2x3 rejected", (M.proSetA(2, 3, 0, 9) === 0) && M.proSchur() === -1 && M.getError() === 3);
M.reset();
ok("proSetA(13) rejected", M.proSetA(13, 13, 0, 1) === -1 && M.getError() === 1);
ok("n=13 schur -1", M.proSchur() === -1 && M.proSchurCount() === 0);
setPro(2, [1, NaN, 0, 2]);
ok("NaN input -1", M.proSchur() === -1 && M.getError() === 1 && M.proSchurCount() === 0);
setPro(2, [1, Infinity, 0, 2]);
ok("Inf input -1", M.proSchur() === -1 && M.getError() === 1);
setPro(2, [1, 0, 0, 2]);
ok("recovers after reject", M.proSchur() === 1 && M.proSchurCount() === 2);

/* ---- 访问器边界 ---- */
ok("accessor bounds", M.proSchurRe(-1) === 0 && M.proSchurRe(99) === 0 &&
  M.proSchurIm(-1) === 0 && M.proSchurIm(99) === 0 &&
  M.proSchurHasVec(-1) === 0 && M.proSchurHasVec(99) === 0 &&
  M.proSchurVector(-1, 0) !== M.proSchurVector(-1, 0) &&
  M.proSchurVector(0, -1) !== M.proSchurVector(0, -1) &&
  M.proSchurVector(0, 99) !== M.proSchurVector(0, 99));

/* ---- 确定性：同输入两次逐位一致 ---- */
setPro(3, mb3);
M.proSchur();
var first = [], same = true, k2;
for (k2 = 0; k2 < 3; k2++) { first.push(M.proSchurRe(k2), M.proSchurIm(k2)); }
M.proSchur();
for (k2 = 0; k2 < 3; k2++) {
  if (M.proSchurRe(k2) !== first[2 * k2] || M.proSchurIm(k2) !== first[2 * k2 + 1]) { same = false; }
}
ok("determinism bitwise", same);

/* ---- 共享池互操作：proLU/proEig 草稿失效语义 + 对称阵与 A5 交叉对拍 ---- */
setPro(3, t3);
ok("schur after proLU", M.proLU() === 1 && M.proSchur() === 1 && close(M.proSchurRe(0), 2 + SQ2, 1e-9));
var schurLams = [M.proSchurRe(0), M.proSchurRe(1), M.proSchurRe(2)];
ok("proEig after schur", M.proEig() === 1 &&
  close(M.proEigValue(0), schurLams[0], 1e-6) &&
  close(M.proEigValue(1), schurLams[1], 1e-6) &&
  close(M.proEigValue(2), schurLams[2], 1e-6));
ok("schur after proEig (vBuf clobber ok)", M.proSchur() === 1 && close(M.proSchurRe(0), 2 + SQ2, 1e-9));

/* ---- 零 GC 审计：池计数红线钉死 7，热循环零分配 ---- */
ok("proAllocCount pinned 7", M.proAllocCount() === 7, M.proAllocCount());
(function () {
  var before = M.proAllocCount(), i;
  var mm = [], j;
  for (j = 0; j < 144; j++) { mm.push(0); }
  for (j = 0; j < 11; j++) { mm[(j + 1) * 12 + j] = 1; }
  mm[11] = 1;
  for (i = 0; i < 100; i++) { setPro(12, mm); M.proSchur(); }
  ok("100x proSchur zero-alloc", M.proAllocCount() === before, "delta=" + (M.proAllocCount() - before));
})();

/* ---- reset 语义 ---- */
M.reset();
ok("reset clears schur state", M.proSchurCount() === 0);

console.log("==== matrix-eig: " + pass + " pass, " + fail + " fail ====");
if (fail !== 0) { process.exit(1); }
