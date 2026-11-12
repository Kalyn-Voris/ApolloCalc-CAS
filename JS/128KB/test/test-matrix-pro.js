/* ApolloCalc · test-matrix-pro.js — 矩阵 pro 层（12×12）测试（v2.3.0 功能 2）

   被测模块：src/core_matrix.js 的 pro* API（既有 3×3 寄存器层由红线
   test-matrix.js 覆盖，本文件只测 12×12 pro 层与两层共存不变式）。
   锚点：C/128KB ac_matrix.c / ac_pro_solvers_ext.c(A4) / ac_pro_eigen.c(A5)
   的成文语义；特征值用闭式谱（对角阵 / 2+2cos(kπ/5) 三对角 / 相似变换谱）。
   运行：node test/test-matrix-pro.js   （JS/128KB 目录下）
   全过输出：==== matrix-pro: N pass, 0 fail ==== */
/* 加载器（svd-format-v26.md §10 再授权）：单文件独立 eval——不装其余 core，
   被测模块意外引用其他 core 全局即 ReferenceError（自包含金丝雀保留）。
   不用 "use strict"，严格模式会把 eval 内 var 困在 eval 作用域 */
var fs = require("fs");
eval(fs.readFileSync(__dirname + "/../src/core_matrix.js", "utf8"));
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
/* 行主序矩阵乘（测试侧普通数组） */
function matMul(a, b, m, k, n) {
  var out = [];
  var i, j, p, s;
  for (i = 0; i < m; i++) {
    for (j = 0; j < n; j++) {
      s = 0;
      for (p = 0; p < k; p++) { s += a[i * k + p] * b[p * n + j]; }
      out.push(s);
    }
  }
  return out;
}
/* 整数矩阵行列式（测试侧余子式递归，仅 ≤5 阶用） */
function detRec(a, n) {
  if (n === 1) { return a[0]; }
  if (n === 2) { return a[0] * a[3] - a[1] * a[2]; }
  var sum = 0;
  var c, j, i2, j2;
  for (c = 0; c < n; c++) {
    var minor = [];
    for (i2 = 1; i2 < n; i2++) {
      for (j2 = 0; j2 < n; j2++) {
        if (j2 !== c) { minor.push(a[i2 * n + j2]); }
      }
    }
    var term = a[c] * detRec(minor, n - 1);
    sum += (c % 2 === 0) ? term : -term;
  }
  return sum;
}
/* 读回 pro 结果寄存器为普通数组 */
function readR(rows, cols) {
  var out = [];
  var i, j;
  for (i = 0; i < rows; i++) {
    for (j = 0; j < cols; j++) { out.push(M.proGetR(i, j)); }
  }
  return out;
}
/* A·X ≈ I 检查（A 为 rows×rows 普通数组，X 从 pro 结果寄存器读回） */
function checkInverse(name, a, n, tol) {
  setPro(n, n, a);
  var rc = M.proInv();
  if (rc !== 1) { ok(name, false, "proInv rc=" + rc + " err=" + M.getError()); return; }
  var x = readR(n, n);
  var prod = matMul(a, x, n, n, n);
  var good = true;
  var i, j;
  for (i = 0; i < n && good; i++) {
    for (j = 0; j < n && good; j++) {
      var want = (i === j) ? 1 : 0;
      if (!approx(prod[i * n + j], want, tol)) { good = false; }
    }
  }
  ok(name, good);
}

/* pro 输入寄存器与两层共存不变式 */
ok("PRO_MAX_DIM=12 且旧 MAX_DIM=3", M.PRO_MAX_DIM === 12 && M.MAX_DIM === 3);
ok("旧层拒绝 4 阶不变（红线口径）", M.setA(4, 4, 0, 1) === -1 && M.getError() === 1);
ok("旧层池仍 3×9", M.allocCount() === 3 && M.rBuffer().length === 9);
ok("pro 层池 allocCount=7", M.proAllocCount() === 7, M.proAllocCount());
ok("proSetA 13 阶拒绝", M.proSetA(13, 13, 0, 1) === -1 && M.getError() === 1);
ok("proSetA 0 阶拒绝", M.proSetA(0, 1, 0, 1) === -1 && M.getError() === 1);
ok("proSetA 下标越界拒绝", M.proSetA(2, 2, 4, 1) === -1 && M.getError() === 1);
ok("proSetA 12×12 接受", M.proSetA(12, 12, 143, 5) === 0 && M.getError() === 0);
ok("proGetR 越界 NaN", M.proGetR(12, 0) !== M.proGetR(12, 0));

/* proDet：1..3 闭式 + 4..12 部分主元 */
setPro(1, 1, [7]);
ok("proDet 1×1 = 7", M.proDet() === 7);
setPro(2, 2, [1, 2, 3, 4]);
ok("proDet 2×2 = -2（旧层同式）", M.proDet() === -2);
setPro(3, 3, [6, 1, 1, 4, -2, 5, 2, 8, 7]);
ok("proDet 3×3 = -306（旧层同式）", M.proDet() === -306);
var m4 = [2, 0, 1, 3, 1, 1, 0, 2, 0, 1, 2, 1, 1, 2, 0, 1];
setPro(4, 4, m4);
ok("proDet 4×4 = 余子式锚点", approx(M.proDet(), detRec(m4, 4)), M.proDet() + "/" + detRec(m4, 4));
var m5 = [3, -1, 2, 0, 4, 1, 2, 0, 1, 1, 0, 1, 3, 2, 1, 2, 0, 1, 4, -2, 1, 3, 0, 1, 2];
setPro(5, 5, m5);
ok("proDet 5×5 = 余子式锚点", approx(M.proDet(), detRec(m5, 5), 1e-7), M.proDet() + "/" + detRec(m5, 5));
var diag12 = [];
var di2, dj5;
for (di2 = 0; di2 < 12; di2++) {
  for (dj5 = 0; dj5 < 12; dj5++) { diag12.push(di2 === dj5 ? di2 + 1 : 0); }
}
setPro(12, 12, diag12);
ok("proDet 12×12 对角 = 12!", M.proDet() === 479001600, M.proDet());
var eye12 = [];
for (di2 = 0; di2 < 144; di2++) { eye12.push(di2 % 13 === 0 ? 1 : 0); }
setPro(12, 12, eye12);
ok("proDet I₁₂ = 1", M.proDet() === 1);
var sing4 = [1, 2, 3, 4, 2, 4, 6, 8, 1, 0, 1, 0, 0, 0, 1, 1];
setPro(4, 4, sing4);
ok("proDet 奇异 4×4 = 0", M.proDet() === 0, M.proDet());
/* 三对角 Toeplitz 12×12（对角 2、次对角 1）：D_n = 2D_{n−1} − D_{n−2} → D_12 = 13 */
var tri12 = [];
for (di2 = 0; di2 < 12; di2++) {
  var dj;
  for (dj = 0; dj < 12; dj++) {
    tri12.push((di2 === dj) ? 2 : (Math.abs(di2 - dj) === 1 ? 1 : 0));
  }
}
setPro(12, 12, tri12);
ok("proDet 三对角 12×12 = 13", approx(M.proDet(), 13, 1e-9), M.proDet());
var anti11 = [];
for (di2 = 0; di2 < 11; di2++) {
  var dj3;
  for (dj3 = 0; dj3 < 11; dj3++) { anti11.push(di2 + dj3 === 10 ? 1 : 0); }
}
setPro(11, 11, anti11);
ok("proDet 反对角 11×11 = -1（55 次行换奇性）", M.proDet() === -1, M.proDet());
setPro(2, 3, [1, 2, 3, 4, 5, 6]);
ok("proDet 非方阵 → NaN + err 3", M.proDet() !== M.proDet() && M.getError() === 3);
ok("proDet 13 阶拒绝", M.proSetA(13, 13, 0, 1) === -1 || (M.proDet() !== M.proDet() && M.getError() === 1));

/* proInv：1..3 闭式 + 4..12 Gauss-Jordan */
setPro(2, 2, [4, 7, 2, 6]);
ok("proInv 2×2 旧层同式 R00=0.6", M.proInv() === 1 && approx(M.proGetR(0, 0), 0.6));
setPro(3, 3, [2, 0, 0, 0, 4, 0, 0, 0, 8]);
M.proInv();
ok("proInv 3×3 对角 R11=0.25", approx(M.proGetR(1, 1), 0.25) && approx(M.proGetR(2, 2), 0.125));
checkInverse("proInv 4×4 A·A⁻¹≈I", m4, 4, 1e-9);
checkInverse("proInv 5×5 A·A⁻¹≈I", m5, 5, 1e-9);
checkInverse("proInv 12×12 对角 A·A⁻¹≈I", diag12, 12, 1e-9);
var g12 = [];
for (di2 = 0; di2 < 12; di2++) {
  var dj4;
  for (dj4 = 0; dj4 < 12; dj4++) {
    g12.push(1 + ((di2 * 7 + dj4 * 5) % 13) + (di2 === dj4 ? 12 : 0));
  }
}
checkInverse("proInv 12×12 一般整阵 A·A⁻¹≈I", g12, 12, 1e-8);
setPro(12, 12, g12);
var detG12 = M.proDet();
setPro(12, 12, g12);
M.proInv();
ok("proInv 记账 det 与 proDet 一致", approx(M.proGetLastDet(), detG12, Math.abs(detG12) * 1e-9),
   M.proGetLastDet() + "/" + detG12);
var singInv = [1, 2, 3, 4, 2, 4, 6, 8, 1, 0, 1, 0, 3, 1, 4, 2];
setPro(4, 4, singInv);
ok("proInv 奇异拒绝", M.proInv() === -1 && M.getError() === 4);
setPro(2, 3, [1, 2, 3, 4, 5, 6]);
ok("proInv 非方阵拒绝", M.proInv() === -1 && M.getError() === 3);

/* proRank：矩形部分主元阶梯 */
setPro(12, 12, eye12);
ok("rank(I₁₂)=12", M.proRank() === 12);
setPro(3, 3, [0, 0, 0, 0, 0, 0, 0, 0, 0]);
ok("rank(0₃)=0", M.proRank() === 0);
setPro(4, 4, [1, 2, 3, 4, 2, 4, 6, 8, 1, 0, 0, 0, 0, 1, 1, 1]);
ok("rank 4×4=3（第2行=第1行×2）", M.proRank() === 3, M.proRank());
setPro(3, 5, [1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 1, 1]);
ok("rank 3×5=3", M.proRank() === 3, M.proRank());
setPro(5, 3, [1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 2, 1, 1]);
ok("rank 5×3=3", M.proRank() === 3, M.proRank());
var outer12 = [];
for (di2 = 0; di2 < 12; di2++) {
  for (dj4 = 0; dj4 < 12; dj4++) { outer12.push((di2 + 1) * (dj4 + 2)); }
}
setPro(12, 12, outer12);
ok("rank 秩1外积阵=1", M.proRank() === 1, M.proRank());
var rank7 = [];
for (di2 = 0; di2 < 12; di2++) {
  for (dj4 = 0; dj4 < 12; dj4++) {
    rank7.push(di2 < 7 ? (di2 === dj4 ? di2 + 1 : (dj4 + di2) % 3) : 0);
  }
}
setPro(12, 12, rank7);
ok("rank 12×12 带 5 零行=7", M.proRank() === 7, M.proRank());
ok("rank 0 阶拒绝", M.proSetA(0, 0, 0, 0) === -1 || (M.proRank() === -1 && M.getError() === 1));

/* proLU：P·A=L·U+piv（C A4 契约） */
/* 重建 P·A 与 L·U 比对 */
function checkLU(name, a, n, tol) {
  setPro(n, n, a);
  if (M.proLU() !== 1) { ok(name, false, "proLU rc=" + M.getError()); return; }
  var good = true;
  var i, j, k;
  for (i = 0; i < n && good; i++) {
    for (j = 0; j < n && good; j++) {
      var s = 0;
      for (k = 0; k < n; k++) {
        s += M.proLUGetL(i, k) * M.proLUGetU(k, j);
      }
      /* (P·A)[i][j] = A[piv[i]][j] */
      if (!approx(s, a[M.proLUGetPiv(i) * n + j], tol)) { good = false; }
    }
  }
  ok(name, good);
}
function checkLUShape(name, n) {
  var good = true;
  var i, j;
  for (i = 0; i < n && good; i++) {
    if (M.proLUGetL(i, i) !== 1) { good = false; }
    for (j = 0; j < n && good; j++) {
      if (j > i && M.proLUGetL(i, j) !== 0) { good = false; }   /* L 上三角为 0 */
      if (j < i && M.proLUGetU(i, j) !== 0) { good = false; }   /* U 下三角为 0 */
    }
  }
  ok(name, good);
}
function checkPivPerm(name, n) {
  var seen = [];
  var i, p;
  for (i = 0; i < n; i++) { seen.push(0); }
  var good = true;
  for (i = 0; i < n; i++) {
    p = M.proLUGetPiv(i);
    if (p < 0 || p >= n || seen[p] !== 0) { good = false; }
    else { seen[p] = 1; }
  }
  ok(name, good);
}
var a3 = [6, 1, 1, 4, -2, 5, 2, 8, 7];
setPro(3, 3, a3);
ok("proLU 3×3 成功", M.proLU() === 1 && M.proLUN() === 3);
checkLU("proLU 3×3 P·A=L·U", a3, 3, 1e-9);
checkLUShape("proLU 3×3 L 单位下三角/U 上三角", 3);
checkPivPerm("proLU 3×3 piv 是置换", 3);
checkLU("proLU 12×12 P·A=L·U", g12, 12, 1e-8);
checkLUShape("proLU 12×12 形状", 12);
checkPivPerm("proLU 12×12 piv 是置换", 12);
checkLU("proLU 5×5 P·A=L·U", m5, 5, 1e-9);
setPro(1, 1, [5]);
ok("proLU 1×1：L=[1] U=[5]", M.proLU() === 1 && M.proLUGetL(0, 0) === 1 && M.proLUGetU(0, 0) === 5);
setPro(4, 4, sing4);
ok("proLU 奇异拒绝 err4", M.proLU() === -1 && M.getError() === 4 && M.proLUN() === 0);
/* U 对角乘积 × 置换符号 == det（一致性锚） */
setPro(12, 12, tri12);
M.proLU();
var luDet = 1;
var swapParity = 0;
var ii, jj;
for (ii = 0; ii < 12; ii++) {
  luDet *= M.proLUGetU(ii, ii);
  for (jj = 0; jj < ii; jj++) {
    if (M.proLUGetPiv(ii) !== M.proLUGetPiv(jj) && M.proLUGetPiv(ii) > M.proLUGetPiv(jj)) { /* noop */ }
  }
}
for (ii = 0; ii < 12; ii++) {
  for (jj = ii + 1; jj < 12; jj++) {
    if (M.proLUGetPiv(ii) > M.proLUGetPiv(jj)) { swapParity++; }
  }
}
setPro(12, 12, tri12);
var detTri = M.proDet();
ok("proLU U 对角积×符号 = proDet", approx(luDet * (swapParity % 2 === 0 ? 1 : -1), detTri, 1e-8),
   luDet + "/" + detTri);

/* proEig：幂法 + Hotelling（C A5 语义） */
function checkEigResidual(name, a, n, tol) {
  setPro(n, n, a);
  var rc = M.proEig();
  if (rc !== 1) { ok(name, false, "proEig rc=" + rc + " err=" + M.getError()); return 0; }
  var good = true;
  var k, i, j;
  for (k = 0; k < n && good; k++) {
    var lam = M.proEigValue(k);
    for (i = 0; i < n && good; i++) {
      var s = 0;
      for (j = 0; j < n; j++) { s += a[i * n + j] * M.proEigVector(k, j); }
      if (Math.abs(s - lam * M.proEigVector(k, i)) > tol) { good = false; }
    }
  }
  ok(name, good);
  return 1;
}
function eigVal(k) { return M.proEigValue(k); }

setPro(3, 3, [-5, 0, 0, 0, 3, 0, 0, 0, -1]);
ok("eig diag(-5,3,-1) 成功 count=3", M.proEig() === 1 && M.proEigCount() === 3);
ok("eig |λ| 降序 = -5,3,-1",
   approx(eigVal(0), -5, 1e-8) && approx(eigVal(1), 3, 1e-8) && approx(eigVal(2), -1, 1e-8),
   eigVal(0) + "," + eigVal(1) + "," + eigVal(2));
checkEigResidual("eig diag 残差 Av=λv", [-5, 0, 0, 0, 3, 0, 0, 0, -1], 3, 1e-8);
setPro(2, 2, [2, 1, 1, 2]);
ok("eig [[2,1],[1,2]] = 3,1", M.proEig() === 1 &&
   approx(M.proEigValue(0), 3, 1e-8) && approx(M.proEigValue(1), 1, 1e-8));
checkEigResidual("eig [[2,1],[1,2]] 残差", [2, 1, 1, 2], 2, 1e-8);
setPro(2, 2, [0, 1, 1, 0]);
ok("eig [[0,1],[1,0]] = 1,-1（符号保留）", M.proEig() === 1 &&
   approx(M.proEigValue(0), 1, 1e-8) && approx(M.proEigValue(1), -1, 1e-8),
   M.proEigValue(0) + "," + M.proEigValue(1));
setPro(2, 2, [1, 1, 1, 1]);
ok("eig 奇异对称 = 2,0", M.proEig() === 1 &&
   approx(M.proEigValue(0), 2, 1e-8) && Math.abs(M.proEigValue(1)) < 1e-7,
   M.proEigValue(0) + "," + M.proEigValue(1));
/* 三对角 Toeplitz 4×4：λ_k = 2 + 2cos(kπ/5)，谱比 0.724 < 0.9 可收敛 */
var tri4 = [2, 1, 0, 0, 1, 2, 1, 0, 0, 1, 2, 1, 0, 0, 1, 2];
var tri4lam = [2 + 2 * Math.cos(Math.PI / 5), 2 + 2 * Math.cos(2 * Math.PI / 5),
               2 + 2 * Math.cos(3 * Math.PI / 5), 2 + 2 * Math.cos(4 * Math.PI / 5)];
setPro(4, 4, tri4);
ok("eig 三对角 4×4 成功", M.proEig() === 1);
ok("eig 三对角 4×4 闭式谱 |λ| 降序",
   approx(M.proEigValue(0), tri4lam[0], 1e-6) && approx(M.proEigValue(1), tri4lam[1], 1e-6) &&
   approx(M.proEigValue(2), tri4lam[2], 1e-6) && approx(M.proEigValue(3), tri4lam[3], 1e-6),
   M.proEigValue(0) + "," + M.proEigValue(1) + "," + M.proEigValue(2) + "," + M.proEigValue(3));
checkEigResidual("eig 三对角 4×4 残差", tri4, 4, 1e-7);
/* 12×12：良 gap 谱经正交相似变换（Householder 两反射构造 Q） */
function houseQ(n, x) {
  /* 构造把 e0 映到 x/‖x‖ 的 Householder Q（测试侧） */
  var norm = 0;
  var i, j;
  for (i = 0; i < n; i++) { norm += x[i] * x[i]; }
  norm = Math.sqrt(norm);
  var v = [];
  for (i = 0; i < n; i++) { v.push(i === 0 ? x[i] + norm : x[i]); }
  var vn = 0;
  for (i = 0; i < n; i++) { vn += v[i] * v[i]; }
  vn = Math.sqrt(vn);
  var q = [];
  for (i = 0; i < n; i++) {
    for (j = 0; j < n; j++) {
      var e = (i === j) ? 1 : 0;
      q.push(e - 2 * v[i] * v[j] / (vn * vn));
    }
  }
  return q;
}
function qT(a, n) {
  var out = [];
  var i, j;
  for (i = 0; i < n; i++) {
    for (j = 0; j < n; j++) { out.push(a[j * n + i]); }
  }
  return out;
}
var lamSpec = [1000000, 10000, 100, 50, 25, 12, 6, 3, 1.5, 0.7, 0.3, 0.1];
var lamDiag = [];
var si, sj;
for (si = 0; si < 12; si++) {
  for (sj = 0; sj < 12; sj++) { lamDiag.push(si === sj ? lamSpec[si] : 0); }
}
var qA = houseQ(12, [3, -1, 4, 1, 5, -2, 6, 0, 2, 7, -3, 1]);
var symA = matMul(matMul(qA, lamDiag, 12, 12, 12), qT(qA, 12), 12, 12, 12);
setPro(12, 12, symA);
ok("eig 12×12 相似谱 成功", M.proEig() === 1, "rc=" + M.proEig() + " err=" + M.getError());
var eigOK = true;
for (si = 0; si < 12; si++) {
  if (Math.abs(M.proEigValue(si) - lamSpec[si]) > 1e-6 * Math.abs(lamSpec[si]) + 1e-9) { eigOK = false; }
}
ok("eig 12×12 谱逐点(|λ|降序)", eigOK, M.proEigValue(0) + ".." + M.proEigValue(11));
checkEigResidual("eig 12×12 残差 Av=λv", symA, 12, 1e-6);
/* 正交性：v_i·v_j ≈ δij */
var ortho = true;
for (si = 0; si < 12 && ortho; si++) {
  for (sj = si; sj < 12 && ortho; sj++) {
    var dot = 0;
    for (var sk = 0; sk < 12; sk++) { dot += M.proEigVector(si, sk) * M.proEigVector(sj, sk); }
    if (Math.abs(dot - (si === sj ? 1 : 0)) > 1e-6) { ortho = false; }
  }
}
ok("eig 12×12 向量正交归一", ortho);
setPro(12, 12, eye12);
ok("eig I₁₂ 全 1（退化即时收敛）", M.proEig() === 1 &&
   approx(M.proEigValue(0), 1, 1e-12) && approx(M.proEigValue(11), 1, 1e-12));
var rep4 = [2, 0, 0, 0, 0, 2, 0, 0, 0, 0, -2, 0, 0, 0, 0, -2];
setPro(4, 4, rep4);
var rcRep = M.proEig();
/* |λ| 相等的并列次序是实现序（C 选择排序 strict > 同款），按值排序断多重集 */
var repVals = [M.proEigValue(0), M.proEigValue(1), M.proEigValue(2), M.proEigValue(3)];
repVals.sort(function (x, y) { return x - y; });
ok("eig ±重根谱成功且多重集 {−2,−2,2,2}", rcRep === 1 &&
   approx(repVals[0], -2, 1e-7) && approx(repVals[1], -2, 1e-7) &&
   approx(repVals[2], 2, 1e-7) && approx(repVals[3], 2, 1e-7),
   "rc=" + rcRep + " " + repVals.join(","));
checkEigResidual("eig ±重根残差", rep4, 4, 1e-7);
setPro(3, 3, [0, 1, 0, 1, 0, 2, 0, 0, 0]);
ok("eig 非对称 → -2 err6", M.proEig() === -2 && M.getError() === 6);
setPro(2, 2, [0, 0, 0, 0]);
ok("eig 零矩阵 → -4 err7", M.proEig() === -4 && M.getError() === 7 && M.proEigCount() === 0);
ok("eig 13 阶写入拒绝（proEig 的 −1 分支为防御位：dims 经 proSetA 只能落在 1..12）",
   M.proSetA(13, 13, 0, 1) === -1 && M.getError() === 1);
ok("eigValue 越界返 0 / eigVector 越界 NaN",
   M.proEigValue(12) === 0 && M.proEigVector(0, 12) !== M.proEigVector(0, 12));

/* proQR：Gram-Schmidt（C ac_mat_qr 语义） */
function checkQR(name, a, m, n, tol) {
  setPro(m, n, a);
  if (M.proQR() !== 1) { ok(name, false, "proQR rc=" + M.getError()); return; }
  var good = true;
  var i, j, k;
  /* A = Q·R */
  for (i = 0; i < m && good; i++) {
    for (j = 0; j < n && good; j++) {
      var s = 0;
      for (k = 0; k < n; k++) {
        s += M.proQRGetQ(i, k) * M.proGetR(k, j);
      }
      if (!approx(s, a[i * n + j], tol)) { good = false; }
    }
  }
  /* QᵀQ = I 仅对前 min(m,n) 个正交列成立；宽矩阵尾列秩亏 → q 零列 + R[j][j]≈0 */
  var qc = m < n ? m : n;
  for (i = 0; i < qc && good; i++) {
    for (j = 0; j < qc && good; j++) {
      var d = 0;
      for (k = 0; k < m; k++) { d += M.proQRGetQ(k, i) * M.proQRGetQ(k, j); }
      if (!approx(d, i === j ? 1 : 0, tol)) { good = false; }
    }
  }
  for (j = m; j < n && good; j++) {
    if (Math.abs(M.proGetR(j, j)) > 1e-12) { good = false; }
    for (k = 0; k < m && good; k++) {
      if (M.proQRGetQ(k, j) !== 0) { good = false; }
    }
  }
  /* R 上三角 */
  for (i = 0; i < n && good; i++) {
    for (j = 0; j < i && good; j++) {
      if (M.proGetR(i, j) !== 0) { good = false; }
    }
  }
  ok(name, good);
}
var qr3 = [12, -51, 4, 6, 167, -68, -4, 24, -41];
setPro(3, 3, qr3);
ok("QR 3×3 成功形状", M.proQR() === 1 && M.proQRRows() === 3 && M.proQRCols() === 3 &&
   M.proGetRRows() === 3 && M.proGetRCols() === 3);
checkQR("QR 3×3 A=QR·QᵀQ=I·R 上三角", qr3, 3, 3, 1e-9);
var qr53 = [1, 0, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0, 1, 1, 1];
checkQR("QR 5×3 矩形", qr53, 5, 3, 1e-9);
var qr35 = [1, 0, 2, 0, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1];
checkQR("QR 3×5 宽矩阵（秩3，尾2列零列语义）", qr35, 3, 5, 1e-9);
checkQR("QR 12×12 一般整阵", g12, 12, 12, 1e-8);
setPro(2, 2, [1, 1, 1, 1]);
ok("QR 秩亏列 |R11|≤1e-12 且 q1 零列（C 语义：R[j][j]=nj 残量）", M.proQR() === 1 &&
   Math.abs(M.proGetR(1, 1)) <= 1e-12 && M.proQRGetQ(0, 1) === 0 && M.proQRGetQ(1, 1) === 0);
var qrIdent = [];
for (si = 0; si < 144; si++) { qrIdent.push(si % 13 === 0 ? 1 : 0); }
setPro(12, 12, qrIdent);
ok("QR I₁₂ → Q=I R=I", M.proQR() === 1 && M.proQRGetQ(5, 5) === 1 && M.proGetR(7, 7) === 1 &&
   M.proQRGetQ(0, 1) === 0 && M.proGetR(1, 0) === 0);
ok("QR 13 阶写入拒绝", M.proSetA(13, 1, 0, 1) === -1 && M.getError() === 1);

/* 零 GC 与复位 */
var allocBefore = M.proAllocCount();
var hi;
for (hi = 0; hi < 2000; hi++) {
  setPro(4, 4, m4);
  M.proDet();
  M.proInv();
  M.proLU();
  M.proQR();
  M.proRank();
}
ok("pro 热循环零分配", M.proAllocCount() === allocBefore,
   "delta=" + (M.proAllocCount() - allocBefore));
var eigBefore = M.proAllocCount();
for (hi = 0; hi < 100; hi++) {
  setPro(3, 3, a3);
  M.proEig();
}
ok("eig 热循环零分配", M.proAllocCount() === eigBefore);
M.reset();
ok("reset 清 pro 维度", M.proGetRRows() === 0 && M.proLUN() === 0 && M.proEigCount() === 0 &&
   M.proQRRows() === 0 && M.getError() === 0);
ok("reset 后 proDet 拒绝", M.proDet() !== M.proDet() && M.getError() === 1);

console.log("==== matrix-pro: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
