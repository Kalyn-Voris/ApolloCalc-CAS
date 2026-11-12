/* ApolloCalc · test-svd.js — m×n 实矩阵奇异值分解（one-sided Jacobi，v2.7.0 G2）

   被测模块：src/core_matrix.js 的 proSvd* API（C/128KB-ProTEXT/src/ac_pro_svd.c
   逐字转录）。锚点：docs/contracts/svd-format-v26.md（§3 算法/错误码、§4 符号
   规范化、§5 关键用例族）；黄金值来自 C 侧输出回填。c1..c14 编号对应勘察用例。
   运行：node test/test-svd.js   （JS/128KB 目录下）
   全过输出：==== svd: N pass, 0 fail ==== */
var fs = require("fs");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_base.js", "core_solvers.js", "core_ratroot.js",
            "core_matrix.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);  /* test-matrix-pro 同款拼接加载：不用 "use strict"，严格模式会把 eval 内 var 困在 eval 作用域 */
var M = ApolloMatrix;

var pass = 0, fail = 0;
function approx(a, b, tol) {
  if (a === b) { return true; }
  if (typeof a !== "number" || typeof b !== "number") { return false; }
  if (a !== a || b !== b) { return false; }
  return Math.abs(a - b) <= (tol === undefined ? 1e-9 : tol);
}
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}

/* 测试侧助手（测试期特权，不在被测零 GC 热路径内） */
function setPro(rows, cols, data) {
  var i;
  for (i = 0; i < rows * cols; i++) {
    if (M.proSetA(rows, cols, i, data[i]) !== 0) { return -1; }
  }
  return 0;
}

/* 契约不变量全套（§5/§6）：rc=1 时 UᵗU=I、VᵗV=I、USVᵗ=A、w 降序、
 * 符号规范化（U 每列首个最大 |·| 分量 > 0）。U/V 经 getter 读（列主序）。 */
function checkSvd(name, m, n, a, tol) {
  setPro(m, n, a);
  var rc = M.proSvd();
  if (rc !== 1 || rc === -2 || rc === -3) {
    ok(name, false, "rc=" + rc + " err=" + M.getError());
    return 0;
  }
  var k = M.proSvdCount();
  if (k !== (m < n ? m : n)) { ok(name, false, "count=" + k); return 0; }
  var good = true;
  var why = "";
  var i, j, l, t;
  var worstU = 0, worstV = 0, worstR = 0;
  /* w 降序（非增） */
  for (j = 1; j < k && good; j++) {
    if (!(M.proSvdValue(j - 1) >= M.proSvdValue(j))) { good = false; why = "w 非降序 @" + j; }
  }
  /* UᵗU = I / VᵗV = I（k 列） */
  for (i = 0; i < k && good; i++) {
    for (j = 0; j < k && good; j++) {
      var du = 0, dv = 0;
      for (l = 0; l < m; l++) { du += M.proSvdU(i, l) * M.proSvdU(j, l); }
      for (l = 0; l < n; l++) { dv += M.proSvdV(i, l) * M.proSvdV(j, l); }
      var tu = Math.abs((i === j ? 1 : 0) - du);
      var tv = Math.abs((i === j ? 1 : 0) - dv);
      if (tu > worstU) { worstU = tu; }
      if (tv > worstV) { worstV = tv; }
      if (tu > tol) { good = false; why = "UᵗU[" + i + "][" + j + "]=" + tu; }
      if (tv > tol) { good = false; why = "VᵗV[" + i + "][" + j + "]=" + tv; }
    }
  }
  /* 重构 USVᵗ = A（原始未缩放域） */
  for (i = 0; i < m && good; i++) {
    for (l = 0; l < n && good; l++) {
      var acc = 0;
      for (j = 0; j < k; j++) { acc += M.proSvdU(j, i) * M.proSvdValue(j) * M.proSvdV(j, l); }
      var rec = Math.abs(a[i * n + l] - acc);
      if (rec > worstR) { worstR = rec; }
      if (rec > tol) { good = false; why = "USVᵗ[" + i + "][" + l + "]=" + rec; }
    }
  }
  /* 符号规范化不变式（契约 §4）：U 每列首个最大 |·| 分量 > 0 */
  for (j = 0; j < k && good; j++) {
    var mx = -1, im = -1;
    for (i = 0; i < m; i++) {
      var av = Math.abs(M.proSvdU(j, i));
      if (av > mx) { mx = av; im = i; }
    }
    if (!(M.proSvdU(j, im) > 0)) { good = false; why = "U 列 " + j + " 规范化破坏"; }
  }
  ok(name, good, why + "  worst UᵗU=" + worstU.toExponential(2) + " VᵗV=" + worstV.toExponential(2) +
     " 重构=" + worstR.toExponential(2));
  return 1;
}

/* Uᵗ·A·V 逐元素（左=U 列 i、右=V 列 j 的双线性型），返回 k×k 行主序数组 */
function utav(m, n, a, k) {
  var out = [];
  var i, j, l, q;
  for (i = 0; i < k; i++) {
    for (j = 0; j < k; j++) {
      var s = 0;
      for (l = 0; l < m; l++) {
        for (q = 0; q < n; q++) { s += M.proSvdU(i, l) * a[l * n + q] * M.proSvdV(j, q); }
      }
      out.push(s);
    }
  }
  return out;
}

/* ---- 边界与 getter 语义 ---- */
ok("PRO_MAX_DIM=12 且旧 MAX_DIM=3（共存不变式）", M.PRO_MAX_DIM === 12 && M.MAX_DIM === 3);
ok("pro 层池 allocCount=7（SVD 零新池红线）", M.proAllocCount() === 7, M.proAllocCount());
ok("SVD getter 初始全 NaN/0（count=0 视图不得读取）",
   M.proSvdCount() === 0 && M.proSvdValue(0) === 0 &&
   M.proSvdU(0, 0) !== M.proSvdU(0, 0) && M.proSvdV(0, 0) !== M.proSvdV(0, 0));
ok("reset 清 SVD 状态后 getter 仍死", (M.reset(), M.proSvdCount() === 0));

/* ---- c13：1×1 [−5] → w=[5]、U=[1]、V=[−1]（§4 成对翻转钉死） ---- */
setPro(1, 1, [-5]);
ok("c13 1×1 [−5] rc=1 count=1", M.proSvd() === 1 && M.proSvdCount() === 1);
ok("c13 w=[5] 精确", M.proSvdValue(0) === 5, M.proSvdValue(0));
ok("c13 U=[1] V=[−1]（§4 符号规范化）", M.proSvdU(0, 0) === 1 && M.proSvdV(0, 0) === -1);
checkSvd("c13 1×1 不变量", 1, 1, [-5], 1e-15);

/* ---- c1：[[2,1],[1,2]] → 闭式谱 {3,1}（良分离谱 uv 逐位） ---- */
var c1 = [2, 1, 1, 2];
setPro(2, 2, c1);
ok("c1 [[2,1],[1,2]] rc=1 count=2", M.proSvd() === 1 && M.proSvdCount() === 2);
ok("c1 w 逐位 = [2.9999999999999996, 0.99999999999999989]",
   M.proSvdValue(0) === 2.9999999999999996 && M.proSvdValue(1) === 0.99999999999999989,
   M.proSvdValue(0) + "," + M.proSvdValue(1));
/* U 列主序 [0.70710678118654757, 0.70710678118654757, 0.70710678118654757, −0.70710678118654757] */
ok("c1 U 列主序逐位", M.proSvdU(0, 0) === 0.70710678118654757 &&
   M.proSvdU(0, 1) === 0.70710678118654757 &&
   M.proSvdU(1, 0) === 0.70710678118654757 &&
   M.proSvdU(1, 1) === -0.70710678118654757,
   M.proSvdU(0, 0) + "," + M.proSvdU(0, 1) + "," + M.proSvdU(1, 0) + "," + M.proSvdU(1, 1));
ok("c1 V 列主序逐位（旋转累计末位差 1ulp）", M.proSvdV(0, 0) === 0.70710678118654746 &&
   M.proSvdV(0, 1) === 0.70710678118654746 &&
   M.proSvdV(1, 0) === 0.70710678118654746 &&
   M.proSvdV(1, 1) === -0.70710678118654746,
   M.proSvdV(0, 0) + "," + M.proSvdV(0, 1) + "," + M.proSvdV(1, 0) + "," + M.proSvdV(1, 1));
setPro(2, 2, c1);
M.proSvd();
var c1s = utav(2, 2, c1, 2);
ok("c1 UᵗAV = diag(3,1) 精确", c1s[0] === 3 && Math.abs(c1s[1]) <= 1e-15 &&
   Math.abs(c1s[2]) <= 1e-15 && c1s[3] === 1,
   c1s.join(","));
checkSvd("c1 不变量", 2, 2, c1, 1e-14);

/* ---- c2：diag(3,−4,0) → w=[4,3,0]、U/V 全精确（§4 成对翻转：V 列0=−e1） ---- */
var c2 = [3, 0, 0, 0, -4, 0, 0, 0, 0];
setPro(3, 3, c2);
ok("c2 diag(3,−4,0) rc=1 count=3", M.proSvd() === 1 && M.proSvdCount() === 3);
ok("c2 w=[4,3,0] 精确", M.proSvdValue(0) === 4 && M.proSvdValue(1) === 3 && M.proSvdValue(2) === 0,
   M.proSvdValue(0) + "," + M.proSvdValue(1) + "," + M.proSvdValue(2));
ok("c2 U 列主序 = [0,1,0, 1,0,0, 0,0,1] 精确",
   M.proSvdU(0, 0) === 0 && M.proSvdU(0, 1) === 1 && M.proSvdU(0, 2) === 0 &&
   M.proSvdU(1, 0) === 1 && M.proSvdU(1, 1) === 0 && M.proSvdU(1, 2) === 0 &&
   M.proSvdU(2, 0) === 0 && M.proSvdU(2, 1) === 0 && M.proSvdU(2, 2) === 1);
ok("c2 V 列主序 = [0,−1,0, 1,0,0, 0,0,1] 精确（成对翻转）",
   M.proSvdV(0, 0) === 0 && M.proSvdV(0, 1) === -1 && M.proSvdV(0, 2) === 0 &&
   M.proSvdV(1, 0) === 1 && M.proSvdV(1, 1) === 0 && M.proSvdV(1, 2) === 0 &&
   M.proSvdV(2, 0) === 0 && M.proSvdV(2, 1) === 0 && M.proSvdV(2, 2) === 1);
setPro(3, 3, c2);
M.proSvd();
var c2s = utav(3, 3, c2, 3);
ok("c2 UᵗAV = diag(4,3,0) 精确", c2s[0] === 4 && c2s[4] === 3 && c2s[8] === 0 &&
   Math.abs(c2s[1]) + Math.abs(c2s[2]) + Math.abs(c2s[3]) + Math.abs(c2s[5]) +
   Math.abs(c2s[6]) + Math.abs(c2s[7]) === 0, c2s.join(","));
checkSvd("c2 不变量", 3, 3, c2, 0);

/* ---- c3：rank-1 [[1,1],[1,1]] → w=[1.9999999999999998, 0]（零 σ 补全列） ---- */
var c3 = [1, 1, 1, 1];
setPro(2, 2, c3);
ok("c3 rank-1 rc=1 count=2", M.proSvd() === 1 && M.proSvdCount() === 2);
ok("c3 w=[1.9999999999999998, 0] 精确", M.proSvdValue(0) === 1.9999999999999998 &&
   M.proSvdValue(1) === 0, M.proSvdValue(0) + "," + M.proSvdValue(1));
checkSvd("c3 不变量（补全列正交）", 2, 2, c3, 1e-14);

/* ---- c9：符号置换×diag(4,3,2)×置换ᵗ 合成 A=[[0,0,3],[0,−2,0],[4,0,0]] ---- */
var c9 = [0, 0, 3, 0, -2, 0, 4, 0, 0];
setPro(3, 3, c9);
ok("c9 合成阵 rc=1 count=3", M.proSvd() === 1 && M.proSvdCount() === 3);
ok("c9 w=[4,3,2] 精确", M.proSvdValue(0) === 4 && M.proSvdValue(1) === 3 && M.proSvdValue(2) === 2,
   M.proSvdValue(0) + "," + M.proSvdValue(1) + "," + M.proSvdValue(2));
ok("c9 U 列主序 = [0,0,1, 1,0,0, 0,1,0] 精确",
   M.proSvdU(0, 0) === 0 && M.proSvdU(0, 1) === 0 && M.proSvdU(0, 2) === 1 &&
   M.proSvdU(1, 0) === 1 && M.proSvdU(1, 1) === 0 && M.proSvdU(1, 2) === 0 &&
   M.proSvdU(2, 0) === 0 && M.proSvdU(2, 1) === 1 && M.proSvdU(2, 2) === 0);
ok("c9 V 列主序 = [1,0,0, 0,0,1, 0,−1,0] 精确",
   M.proSvdV(0, 0) === 1 && M.proSvdV(0, 1) === 0 && M.proSvdV(0, 2) === 0 &&
   M.proSvdV(1, 0) === 0 && M.proSvdV(1, 1) === 0 && M.proSvdV(1, 2) === 1 &&
   M.proSvdV(2, 0) === 0 && M.proSvdV(2, 1) === -1 && M.proSvdV(2, 2) === 0);
setPro(3, 3, c9);
M.proSvd();
var c9s = utav(3, 3, c9, 3);
ok("c9 UᵗAV = diag(4,3,2) 精确", c9s[0] === 4 && c9s[4] === 3 && c9s[8] === 2 &&
   Math.abs(c9s[1]) + Math.abs(c9s[2]) + Math.abs(c9s[3]) + Math.abs(c9s[5]) +
   Math.abs(c9s[6]) + Math.abs(c9s[7]) === 0, c9s.join(","));
checkSvd("c9 不变量", 3, 3, c9, 0);

/* ---- c8：2^±150 幂 2 极端尺度（2^−150 被数值零判据精确置 0） ---- */
var p150 = Math.pow(2, 150);
var c8 = [p150, 0, 0, Math.pow(2, -150)];
setPro(2, 2, c8);
ok("c8 2^±150 rc=1 count=2", M.proSvd() === 1 && M.proSvdCount() === 2);
ok("c8 w=[2^150, 0] 精确（×scale 幂 2 还原）", M.proSvdValue(0) === p150 && M.proSvdValue(1) === 0,
   M.proSvdValue(0) + "," + M.proSvdValue(1));
ok("c8 U=V=I 精确", M.proSvdU(0, 0) === 1 && M.proSvdU(0, 1) === 0 &&
   M.proSvdU(1, 0) === 0 && M.proSvdU(1, 1) === 1 &&
   M.proSvdV(0, 0) === 1 && M.proSvdV(0, 1) === 0 &&
   M.proSvdV(1, 0) === 0 && M.proSvdV(1, 1) === 1);
checkSvd("c8 不变量", 2, 2, c8, 1e-14);

/* ---- 缩放指数双侧边界：2^1023 拒绝（e=1024）/ 2^1022 通过 ---- */
var hi = Math.pow(2, 1023);
setPro(2, 2, [hi, 0, 0, 1]);
ok("缩放指数 e=1024 → −1 域界拒绝（C 同查）", M.proSvd() === -1 && M.proSvdCount() === 0,
   "rc=" + M.proSvd());
var hi2 = Math.pow(2, 1022);
setPro(2, 2, [hi2, 0, 0, 1]);
ok("缩放指数 e=1023 → 通过且 w0=2^1022 精确还原",
   M.proSvd() === 1 && M.proSvdValue(0) === hi2, M.proSvdValue(0));

/* ---- c10/c11：正交判据双侧（1e−15 跳过 → V 恒等；1e−13 旋转 → V 非恒等） ---- */
setPro(2, 2, [1, 1e-15, 1e-15, 1]);
ok("c10 1e−15 刚好跳过 rc=1", M.proSvd() === 1 && M.proSvdCount() === 2);
ok("c10 V 逐位恒等", M.proSvdV(0, 0) === 1 && M.proSvdV(0, 1) === 0 &&
   M.proSvdV(1, 0) === 0 && M.proSvdV(1, 1) === 1,
   M.proSvdV(0, 0) + "," + M.proSvdV(0, 1) + "," + M.proSvdV(1, 0) + "," + M.proSvdV(1, 1));
ok("c10 w=[1,1]", M.proSvdValue(0) === 1 && M.proSvdValue(1) === 1);
checkSvd("c10 不变量", 2, 2, [1, 1e-15, 1e-15, 1], 1e-14);
setPro(2, 2, [1, 1e-13, 1e-13, 1]);
ok("c11 1e−13 刚好旋转 rc=1", M.proSvd() === 1 && M.proSvdCount() === 2);
ok("c11 V 非恒等（旋转发生）", M.proSvdV(0, 1) !== 0 || M.proSvdV(1, 0) !== 0);
ok("c11 w 逐位 = [1.0000000000000999, 0.99999999999989986]",
   M.proSvdValue(0) === 1.0000000000000999 && M.proSvdValue(1) === 0.99999999999989986,
   M.proSvdValue(0) + "," + M.proSvdValue(1));
checkSvd("c11 不变量", 2, 2, [1, 1e-13, 1e-13, 1], 1e-14);

/* ---- c7：零矩阵 3×2 → w=[0,0]、U Gram-Schmidt 补全正交 ---- */
var c7 = [0, 0, 0, 0, 0, 0];
setPro(3, 2, c7);
ok("c7 零矩阵 3×2 rc=1 count=2", M.proSvd() === 1 && M.proSvdCount() === 2);
ok("c7 w=[0,0] 精确", M.proSvdValue(0) === 0 && M.proSvdValue(1) === 0);
ok("c7 U 补全列 = [e0, e1] 精确", M.proSvdU(0, 0) === 1 && M.proSvdU(0, 1) === 0 &&
   M.proSvdU(0, 2) === 0 && M.proSvdU(1, 0) === 0 && M.proSvdU(1, 1) === 1 && M.proSvdU(1, 2) === 0);
ok("c7 V=I₂ 精确", M.proSvdV(0, 0) === 1 && M.proSvdV(0, 1) === 0 &&
   M.proSvdV(1, 0) === 0 && M.proSvdV(1, 1) === 1);
checkSvd("c7 不变量", 3, 2, c7, 0);

/* ---- c4/c5：矩形两向（3×2 / 2×3 转置对偶路径） ---- */
var c4 = [1, 2, 3, 4, 5, 6];
var w4 = [9.5255180915651056, 0.51430058065864426];  /* Verified against C implementation */
setPro(3, 2, c4);
ok("c4 3×2 rc=1 count=2", M.proSvd() === 1 && M.proSvdCount() === 2);
ok("c4 w 逐位", M.proSvdValue(0) === w4[0] && M.proSvdValue(1) === w4[1],
   M.proSvdValue(0) + "," + M.proSvdValue(1));
checkSvd("c4 不变量", 3, 2, c4, 1e-14);
var c5 = [1, 2, 3, 4, 5, 6];
var w5 = [9.5080320006957244, 0.77286963567348443];  /* Verified against C implementation */
setPro(2, 3, c5);
ok("c5 2×3（m<n 转置路径）rc=1 count=2", M.proSvd() === 1 && M.proSvdCount() === 2);
ok("c5 w 逐位", M.proSvdValue(0) === w5[0] && M.proSvdValue(1) === w5[1],
   M.proSvdValue(0) + "," + M.proSvdValue(1));
checkSvd("c5 不变量", 2, 3, c5, 1e-14);

/* ---- c12：Hilbert 5×12 病态谱 ---- */
var h5 = [];
var hi3, hj3;
for (hi3 = 0; hi3 < 5; hi3++) { for (hj3 = 0; hj3 < 5; hj3++) { h5.push(1 / (hi3 + hj3 + 1)); } }
var hw = [1.5670506910982300, 0.20853421861101326, 0.011407491623419792,
          0.00030589804015119615, 0.0000032879287721570129];
setPro(5, 5, h5);
ok("c12 Hilbert5 rc=1 count=5", M.proSvd() === 1 && M.proSvdCount() === 5);
var hOK = true;
for (hi3 = 0; hi3 < 5; hi3++) { if (!approx(M.proSvdValue(hi3), hw[hi3], 1e-15)) { hOK = false; } }
ok("c12 w 逐位（tol 1e−15）", hOK,
   M.proSvdValue(0) + "," + M.proSvdValue(1) + "," + M.proSvdValue(2) + "," +
   M.proSvdValue(3) + "," + M.proSvdValue(4));
checkSvd("c12 不变量", 5, 5, h5, 1e-14);

/* ---- c14：12×12 恒等（维度上界 + 重 σ 谱 w 精确） ---- */
var e12 = [];
for (hi3 = 0; hi3 < 144; hi3++) { e12.push(hi3 % 13 === 0 ? 1 : 0); }
setPro(12, 12, e12);
ok("c14 I₁₂ rc=1 count=12", M.proSvd() === 1 && M.proSvdCount() === 12);
var eOK = true;
for (hi3 = 0; hi3 < 12; hi3++) { if (M.proSvdValue(hi3) !== 1) { eOK = false; } }
ok("c14 w 全 1 精确", eOK);
checkSvd("c14 不变量", 12, 12, e12, 0);

/* ---- 拒绝路径：NaN/±Inf（域闸对称，C ac_pro_svd.c:76-80 同查） ---- */
setPro(2, 2, [1, NaN, 0, 1]);
ok("NaN → −1 且 count=0", M.proSvd() === -1 && M.getError() === 1 && M.proSvdCount() === 0);
ok("NaN 失败后 getter 全死", M.proSvdValue(0) === 0 &&
   M.proSvdU(0, 0) !== M.proSvdU(0, 0) && M.proSvdV(0, 0) !== M.proSvdV(0, 0));
setPro(2, 2, [1, 0, Infinity, 1]);
ok("+Inf → −1", M.proSvd() === -1 && M.proSvdCount() === 0);
setPro(2, 2, [1, 0, -Infinity, 1]);
ok("−Inf → −1", M.proSvd() === -1 && M.proSvdCount() === 0);
ok("13 阶写入拒绝（SVD 维度守卫经 proSetA）", M.proSetA(13, 13, 0, 1) === -1 && M.getError() === 1);

/* ---- getter 越界边界（成功视图上） ---- */
setPro(3, 2, c7);
M.proSvd();
ok("getter 越界：value 0 / U,V NaN（proEigValue/proEigVector 同款）",
   M.proSvdValue(-1) === 0 && M.proSvdValue(2) === 0 &&
   M.proSvdU(-1, 0) !== M.proSvdU(-1, 0) && M.proSvdU(2, 0) !== M.proSvdU(2, 0) &&
   M.proSvdU(0, -1) !== M.proSvdU(0, -1) && M.proSvdU(0, 3) !== M.proSvdU(0, 3) &&
   M.proSvdV(2, 0) !== M.proSvdV(2, 0) && M.proSvdV(0, 2) !== M.proSvdV(0, 2));
ok("SVD 不写 proGetR 维度（借寄存器纪律）",
   M.proGetRRows() === 0 && M.proGetRCols() === 0);

/* ---- 域内反向断言（契约 §3.9）：全用例 rc ∉ {−2,−3} ---- */
var domCases = [
  [2, 2, c1], [3, 3, c2], [2, 2, c3], [3, 3, c9], [2, 2, c8],
  [5, 5, h5], [12, 12, e12], [3, 2, c4], [2, 3, c5], [3, 2, c7],
  [2, 2, [1, 1e-15, 1e-15, 1]], [2, 2, [1, 1e-13, 1e-13, 1]]
];
var domOK = true;
var domRc = "";
for (hi3 = 0; hi3 < domCases.length; hi3++) {
  var dm = domCases[hi3][0], dn2 = domCases[hi3][1];
  setPro(dm, dn2, domCases[hi3][2]);
  var drc = M.proSvd();
  if (drc === -2 || drc === -3) { domOK = false; domRc += " case#" + hi3 + " rc=" + drc; }
}
ok("域内 12 例 rc ∉ {−2,−3}（−2/−3 契约域内不可触发的反向覆盖）", domOK, domRc);

/* ---- 零 GC 热循环与两层共存 ---- */
var allocBefore = M.proAllocCount();
var hi4;
for (hi4 = 0; hi4 < 200; hi4++) {
  setPro(3, 3, c9);
  M.proSvd();
  setPro(2, 3, c5);
  M.proSvd();
}
ok("SVD 热循环零分配（proAllocCount 恒 7）", M.proAllocCount() === allocBefore,
   "delta=" + (M.proAllocCount() - allocBefore));
setPro(2, 2, [4, 7, 2, 6]);
ok("SVD 后既有 pro 层不受染（proInv 正常）",
   M.proInv() === 1 && approx(M.proGetR(0, 0), 0.6));
setPro(3, 3, [6, 1, 1, 4, -2, 5, 2, 8, 7]);
ok("SVD 后 proLU 正常（union 失效语义：视图数据被覆盖；_svdK 停泊同 C m_st/k_st 纪律）",
   M.proLU() === 1 && M.proLUN() === 3);
M.reset();
ok("reset 清 SVD 状态", M.proSvdCount() === 0 &&
   M.proSvdU(0, 0) !== M.proSvdU(0, 0) && M.getError() === 0);

console.log("==== svd: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
