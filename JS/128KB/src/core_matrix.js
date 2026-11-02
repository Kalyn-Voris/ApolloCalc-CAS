// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc-CAS · JS/128KB · core_matrix.js — 基础矩阵运算：乘法 / 转置 / 行列式 / 逆矩阵（≤3×3 闭式），自包含。
 * v2.3.0 功能 2：pro 层升级 12×12——部分主元 det/inv/rank、LU 分解
 * （P·A=L·U+piv 访问器，契约对齐 C/128KB-ProTEXT ac_pro.h A4）、
 * 幂法 + Hotelling deflation 实对称全特征对（对齐 ac_pro_eig_all 语义：
 * |λ| 降序 + 访问器 + 同款错误码）、QR 分解（对齐 ac_mat_qr 语义）。
 *
 *   红线测试 test-matrix.js 把既有寄存器 API 钉死在 ≤3 阶（setA(4,4,..)
 *   必须 err=1 拒绝、rBuffer().length===9、allocCount()===3），因此 12×12
 *   以 pro 层同模块叠加实现：既有寄存器层（A/B/R 3×3）逐字节保持原行为，
 *   pro 层用独立池与 pro* 前缀 API。两层共用 _err 错误通道与 reset()。
 *   allocCount() 报告既有寄存器层的 3 个池（红线断言口径不变）；
 *   proAllocCount() 报告 pro 层 7 个池；全模块加载期合计 10 个 TypedArray。
 *
 * proSetA(rows, cols, i, v) 写 12×12 输入寄存器 A
 *   （行主序）；结果一律落 pro 结果寄存器 R（proGetR 读取），特征对经
 *   proEigValue/proEigVector 读取。UI 调用序 proSetA → proXxx → proGetR*，
 *   与既有 setA → det → getR 契约同构。
 *
 * pro 层池账本（全部加载期一次分配，allocCount=7，合计 4944 B：
 * 4×1152 + 48 + 96 + 192；全模块 10 池 5160 B，F0 实测修回——原 5944 系笔误）
 *   _pa    Float64Array(144)  输入寄存器 A（12×12）      1152 B
 *   _pr    Float64Array(144)  结果寄存器 R                1152 B
 *   _luBuf Float64Array(144)  LU 打包 L\U / det·rank·inv
 *                             工作副本 / eig deflated A    1152 B
 *   _vBuf  Float64Array(144)  QR 的 Q / eig 特征向量 V     1152 B
 *   _piv   Int32Array(12)     LU 置换 / eig 零空间列主元    48 B
 *   _eigLam Float64Array(12)  特征值 λ（|λ| 降序）          96 B
 *   _work  Float64Array(24)   eig x/y 向量 / QR 列草稿     192 B
 *   池合计 ≈ 5.8 KiB（ROADMAP 预算 ~6 KB 内）；C 侧 eig 视图 2592 B
 *   （ac_pro.h:272）作上界参照，JS 以独立池替代 union 视图。
 *   共享草稿约定：LU/det/rank/inv/eig 依次复用 _luBuf，一次调用使上一次
 *   的草稿结果失效（C Pro union「一次调用只持有一个视图」同款语义）。
 *
 *   - 部分主元消元核 = C ac_matrix.c mat_echelon（主元阈值 1e-12、零列
 *     右移计秩、swap 奇偶定 det 符号）；rank 支持矩形 m×n。
 *   - LU = ac_pro_solvers_ext.c ac_mat_lu 的 Doolittle 部分主元逐字移植：
 *     P·A=L·U，piv[i]=第 i 行来自原矩阵的行号，L 单位下三角（访问器组装），
 *     U 上三角；任一主元 |·|<1e-12 → 奇异拒绝。
 *   - eig = ac_pro_eigen.c 逐字移植：带 Rayleigh 商的幂迭代，收敛判据盯
 *     残差（‖Dx−λx‖ ≤ 1e-13·‖D‖∞ 或 0.9 停滞比）而非 |Δλ|，Hotelling
 *     收缩 A ← A − λvvᵀ 逐轮重建，多初值（3 解析 + n 单位基）重试，候选过
 *     1e-8·(1+scale) 残差闸门，兜底零空间路径；非对称 → −2（收缩不保谱），
 *     零矩阵 → −4，不收敛 → −3；λ 按原始 A 的 Rayleigh 商复核。
 *   - QR = ac_matrix.c ac_mat_qr 的 Gram-Schmidt 逐字移植：Q m×n 正交列、
 *     R n×n 上三角；秩亏列 R[j][j]=0 且 q 零列（成文语义）。
 *
 * pro 层错误码（getError，与既有码共用通道）
 *   0 OK / 1 维度超界（>12 或 ≤0 或下标越界）/ 3 非方阵 / 4 奇异 /
 *   5 幂迭代不收敛 / 6 非对称矩阵 / 7 零矩阵（特征向量无法归一化）。
 *   proEig 返回值本身即 C 侧同款错误码：1 成功 / −1 维度 / −2 非对称 /
 *   −3 不收敛 / −4 零矩阵（0 内部错误，不应出现）。
 *
 * 零 GC：10 个 TypedArray 加载期一次分配，全路径无对象/数组字面量、
 * 无字符串操作、无递归。
 *
 * 导出三通道：Node require / 浏览器 <script> 顶层 var / 内核拼接 ESM。 */

var ApolloMatrix = (function () {
  "use strict";

  var MAX_DIM = 3;
  var CAP = MAX_DIM * MAX_DIM;
  var SINGULAR_TOL = 1e-12;         /* 与 C/128KB ac_matrix 的主元阈值同值 */

  var _a = new Float64Array(CAP);
  var _b = new Float64Array(CAP);
  var _r = new Float64Array(CAP);

  var _rowsA = 0, _colsA = 0;
  var _rowsB = 0, _colsB = 0;
  var _rowsR = 0, _colsR = 0;

  var _err = 0;
  var _opCount = 0;
  var _allocCount = 3;
  var _det = 0;                     /* det()/inv() 最近一次的行列式值 */

  /* v2.3.0 pro 层：12×12 */

  var PRO_MAX_DIM = 12;
  var PRO_CAP = PRO_MAX_DIM * PRO_MAX_DIM;

  var _pa = new Float64Array(PRO_CAP);
  var _pr = new Float64Array(PRO_CAP);
  var _luBuf = new Float64Array(PRO_CAP);    /* LU 打包 / 工作副本 / deflated A */
  var _vBuf = new Float64Array(PRO_CAP);     /* QR 的 Q / eig 的 V（行存） */
  var _piv = new Int32Array(PRO_MAX_DIM);    /* LU 置换 / eig 零空间列主元 */
  var _eigLam = new Float64Array(PRO_MAX_DIM);
  var _work = new Float64Array(2 * PRO_MAX_DIM);  /* eig x/y / QR 列草稿 */

  /* F0 全池水位账本：模块池加载期自报（寄存器层 3 池 + pro 层 7 池共 10 池，
   * docs/contracts/f0-governance-v25.md §3） */
  if (typeof _modulePoolSum === "function") {
    _modulePoolSum(_a, _b, _r, _pa, _pr, _luBuf, _vBuf, _piv, _eigLam, _work);
  }

  var _pRowsA = 0, _pColsA = 0;
  var _pRowsR = 0, _pColsR = 0;
  var _pN = 0;                      /* 最近一次方阵口径的阶（det/inv/LU/eig） */
  var _qrM = 0, _qrN = 0;           /* 最近一次 QR 的输入形状（Q 为 m×n） */
  var _luN = 0;                     /* 最近一次成功 LU 的阶 */
  var _eigCount = 0;                /* 最近一次成功 eig 的特征对个数 */
  var _swaps = 0;                   /* echelon 行交换奇偶计数（标量寄存） */
  var _allocProCount = 7;

  function allocCount() { return _allocCount; }
  function getOpCount() { return _opCount; }
  function getError() { return _err; }

  function aBuffer() { return _a; }
  function bBuffer() { return _b; }
  function rBuffer() { return _r; }

  /* 既有寄存器层（≤3×3，行为保持 v2.2.0 逐字节不变） */

  function _dimsOK(r, c) {
    return r >= 1 && r <= MAX_DIM && c >= 1 && c <= MAX_DIM;
  }

  /* 重复调用覆盖旧值 —— 换矩阵前不要求 reset。 */
  function _setReg(arr, rr, cc, rows, cols, i, v) {
    _err = 0;
    if (!_dimsOK(rows, cols) || i < 0 || i >= rows * cols) { _err = 1; return -1; }
    rr(rows);
    cc(cols);
    arr[i] = v;
    return 0;
  }

  var _rowsASet = function (v) { _rowsA = v; };
  var _colsASet = function (v) { _colsA = v; };
  var _rowsBSet = function (v) { _rowsB = v; };
  var _colsBSet = function (v) { _colsB = v; };

  function setA(rows, cols, i, v) { return _setReg(_a, _rowsASet, _colsASet, rows, cols, i, v); }
  function setB(rows, cols, i, v) { return _setReg(_b, _rowsBSet, _colsBSet, rows, cols, i, v); }

  function getR(r, c) {
    if (r < 0 || c < 0 || r >= _rowsR || c >= _colsR) { return NaN; }
    return _r[r * _colsR + c];
  }
  function getRRows() { return _rowsR; }
  function getRCols() { return _colsR; }
  function getLastDet() { return _det; }

  function reset() {
    _rowsA = 0; _colsA = 0;
    _rowsB = 0; _colsB = 0;
    _rowsR = 0; _colsR = 0;
    _err = 0;
    _opCount = 0;
    _det = 0;
    _pRowsA = 0; _pColsA = 0;
    _pRowsR = 0; _pColsR = 0;
    _pN = 0; _luN = 0; _eigCount = 0;
    _qrM = 0; _qrN = 0;
    return 0;
  }

  /* R = A × B，m×k · k×n → m×n。三重循环无递归。 */
  function mul() {
    _err = 0;
    if (!_dimsOK(_rowsA, _colsA) || !_dimsOK(_rowsB, _colsB)) { _err = 1; return -1; }
    if (_colsA !== _rowsB) { _err = 2; return -1; }
    _opCount++;
    var m = _rowsA;
    var k = _colsA;
    var n = _colsB;
    var i, j, p, s;
    for (i = 0; i < m; i++) {
      for (j = 0; j < n; j++) {
        s = 0;
        for (p = 0; p < k; p++) {
          s = s + _a[i * k + p] * _b[p * n + j];
        }
        _r[i * n + j] = s;
      }
    }
    _rowsR = m;
    _colsR = n;
    return 1;
  }

  /* R = Aᵀ，m×n → n×m。读写不相交（_a 只读，_r 只写）。 */
  function transpose() {
    _err = 0;
    if (!_dimsOK(_rowsA, _colsA)) { _err = 1; return -1; }
    _opCount++;
    var m = _rowsA;
    var n = _colsA;
    var i, j;
    for (i = 0; i < m; i++) {
      for (j = 0; j < n; j++) {
        _r[j * m + i] = _a[i * n + j];
      }
    }
    _rowsR = n;
    _colsR = m;
    return 1;
  }

  /* det(A)。行列式返回值本身可为 0（奇异是合法结果不是错误）；
   * 维度错误返回 NaN 且 _err 置因。 */
  function det() {
    _err = 0;
    if (!_dimsOK(_rowsA, _colsA)) { _err = 1; return NaN; }
    if (_rowsA !== _colsA) { _err = 3; return NaN; }
    _opCount++;
    var n = _rowsA;
    if (n === 1) { _det = _a[0]; return _det; }
    if (n === 2) { _det = _a[0] * _a[3] - _a[1] * _a[2]; return _det; }
    /* 3×3 Sarrus 展开 */
    _det = _a[0] * (_a[4] * _a[8] - _a[5] * _a[7])
         - _a[1] * (_a[3] * _a[8] - _a[5] * _a[6])
         + _a[2] * (_a[3] * _a[7] - _a[4] * _a[6]);
    return _det;
  }

  /* R = A⁻¹，伴随矩阵法闭式。|det| ≤ 1e-12 视为奇异拒绝（不返回近似解）。
   * 结果经 getR 读取；det 值经 getLastDet 复查。 */
  function inv() {
    _err = 0;
    if (!_dimsOK(_rowsA, _colsA)) { _err = 1; return -1; }
    if (_rowsA !== _colsA) { _err = 3; return -1; }
    _opCount++;
    var n = _rowsA;
    var d;
    var i;
    if (n === 1) {
      d = _a[0];
      if (d > -SINGULAR_TOL && d < SINGULAR_TOL) { _err = 4; return -1; }
      _det = d;
      _r[0] = 1 / d;
      _rowsR = 1;
      _colsR = 1;
      return 1;
    }
    if (n === 2) {
      d = _a[0] * _a[3] - _a[1] * _a[2];
      if (d > -SINGULAR_TOL && d < SINGULAR_TOL) { _err = 4; return -1; }
      _det = d;
      _r[0] = _a[3] / d;
      _r[1] = -_a[1] / d;
      _r[2] = -_a[2] / d;
      _r[3] = _a[0] / d;
      _rowsR = 2;
      _colsR = 2;
      return 1;
    }
    /* 3×3 代数余子式 C_ij（i 行 j 列），det = a·C00 + b·C01 + c·C02，
     * R[j][i] = C_ij / det（伴随矩阵转置关系，行主序下标 R[j*3+i]）。 */
    var c00 = _a[4] * _a[8] - _a[5] * _a[7];
    var c01 = -(_a[3] * _a[8] - _a[5] * _a[6]);
    var c02 = _a[3] * _a[7] - _a[4] * _a[6];
    var c10 = -(_a[1] * _a[8] - _a[2] * _a[7]);
    var c11 = _a[0] * _a[8] - _a[2] * _a[6];
    var c12 = -(_a[0] * _a[7] - _a[1] * _a[6]);
    var c20 = _a[1] * _a[5] - _a[2] * _a[4];
    var c21 = -(_a[0] * _a[5] - _a[2] * _a[3]);
    var c22 = _a[0] * _a[4] - _a[1] * _a[3];
    d = _a[0] * c00 + _a[1] * c01 + _a[2] * c02;
    if (d > -SINGULAR_TOL && d < SINGULAR_TOL) { _err = 4; return -1; }
    _det = d;
    _r[0] = c00 / d;
    _r[1] = c10 / d;
    _r[2] = c20 / d;
    _r[3] = c01 / d;
    _r[4] = c11 / d;
    _r[5] = c21 / d;
    _r[6] = c02 / d;
    _r[7] = c12 / d;
    _r[8] = c22 / d;
    _rowsR = 3;
    _colsR = 3;
    return 1;
  }

  /* pro 层：输入寄存器与结果读取 */

  function _pDimsOK(r, c) {
    return r >= 1 && r <= PRO_MAX_DIM && c >= 1 && c <= PRO_MAX_DIM;
  }

  function proSetA(rows, cols, i, v) {
    _err = 0;
    if (!_pDimsOK(rows, cols) || i < 0 || i >= rows * cols) { _err = 1; return -1; }
    _pRowsA = rows;
    _pColsA = cols;
    _pa[i] = v;
    return 0;
  }

  function proGetR(r, c) {
    if (r < 0 || c < 0 || r >= _pRowsR || c >= _pColsR) { return NaN; }
    return _pr[r * _pColsR + c];
  }
  function proGetRRows() { return _pRowsR; }
  function proGetRCols() { return _pColsR; }
  function proGetLastDet() { return _det; }
  function proAllocCount() { return _allocProCount; }

  /* 方阵口径守卫：1..12 阶；非方阵 → err 3 */
  function _pSquareOK() {
    if (!_pDimsOK(_pRowsA, _pColsA)) { _err = 1; return 0; }
    if (_pRowsA !== _pColsA) { _err = 3; return 0; }
    _pN = _pRowsA;
    return 1;
  }

  /* pro 层：部分主元消元核（C mat_echelon 同款）
   * 在 _luBuf[0..m·n) 上原地行梯形消元（部分主元），返回秩并把交换次数
   * 累入 _swaps。主元阈值 1e-12；零列右移继续找秩（矩形支持）。 */
  function _echelon(m, n) {
    var rank = 0;
    var col = 0;
    var row, piv, k, c2;
    var maxAbs, f, v;
    _swaps = 0;
    while (rank < m && col < n) {
      piv = rank;
      maxAbs = _luBuf[rank * n + col];
      if (maxAbs < 0.0) { maxAbs = -maxAbs; }
      for (row = rank + 1; row < m; row++) {
        v = _luBuf[row * n + col];
        if (v < 0.0) { v = -v; }
        if (v > maxAbs) { maxAbs = v; piv = row; }
      }
      if (maxAbs < 1e-12) {
        col++;                        /* 该列余下全零：右移继续找秩 */
        continue;
      }
      if (piv !== rank) {
        for (c2 = 0; c2 < n; c2++) {
          var t = _luBuf[rank * n + c2];
          _luBuf[rank * n + c2] = _luBuf[piv * n + c2];
          _luBuf[piv * n + c2] = t;
        }
        _swaps++;
      }
      for (row = rank + 1; row < m; row++) {
        f = _luBuf[row * n + col] / _luBuf[rank * n + col];
        if (f === 0.0) { continue; }
        for (k = col; k < n; k++) {
          _luBuf[row * n + k] -= f * _luBuf[rank * n + k];
        }
      }
      rank++;
      col++;
    }
    return rank;
  }

  /* pro det：1..3 闭式快路径 + 4..12 部分主元 LU */

  function proDet() {
    _err = 0;
    if (!_pDimsOK(_pRowsA, _pColsA)) { _err = 1; return NaN; }
    if (_pRowsA !== _pColsA) { _err = 3; return NaN; }
    _opCount++;
    var n = _pRowsA;
    _pN = n;
    var i, j;
    if (n === 1) { _det = _pa[0]; return _det; }
    if (n === 2) { _det = _pa[0] * _pa[3] - _pa[1] * _pa[2]; return _det; }
    if (n === 3) {
      _det = _pa[0] * (_pa[4] * _pa[8] - _pa[5] * _pa[7])
           - _pa[1] * (_pa[3] * _pa[8] - _pa[5] * _pa[6])
           + _pa[2] * (_pa[3] * _pa[7] - _pa[4] * _pa[6]);
      return _det;
    }
    for (i = 0; i < n * n; i++) { _luBuf[i] = _pa[i]; }
    var rank = _echelon(n, n);
    if (rank < n) { _det = 0.0; return _det; }   /* 奇异含真 0，与 C det 合并约定 */
    var d = 1.0;
    for (i = 0; i < n; i++) { d *= _luBuf[i * n + i]; }
    _det = (_swaps % 2 === 0) ? d : -d;
    return _det;
  }

  /* pro inv：1..3 闭式快路径 + 4..12 Gauss-Jordan 部分主元
   * 工作副本在 _luBuf、单位阵累加器在 _pr，逐列归一主元并消散至全列；
   * 主元选择只看 A 部分，行操作两缓冲同步 —— 与 C 增广 [A|I] 单行流水
   * 逐运算同序，舍入路径一致。结果 R = A⁻¹（proGetR 读取）。 */
  function proInv() {
    _err = 0;
    if (!_pDimsOK(_pRowsA, _pColsA)) { _err = 1; return -1; }
    if (_pRowsA !== _pColsA) { _err = 3; return -1; }
    _opCount++;
    var n = _pRowsA;
    _pN = n;
    var i, j, col, row, piv, c2;
    var maxAbs, v, f;
    if (n === 1) {
      var d1 = _pa[0];
      if (d1 > -SINGULAR_TOL && d1 < SINGULAR_TOL) { _err = 4; return -1; }
      _det = d1;
      _pr[0] = 1 / d1;
      _pRowsR = 1; _pColsR = 1;
      return 1;
    }
    if (n === 2) {
      var d2 = _pa[0] * _pa[3] - _pa[1] * _pa[2];
      if (d2 > -SINGULAR_TOL && d2 < SINGULAR_TOL) { _err = 4; return -1; }
      _det = d2;
      _pr[0] = _pa[3] / d2;
      _pr[1] = -_pa[1] / d2;
      _pr[2] = -_pa[2] / d2;
      _pr[3] = _pa[0] / d2;
      _pRowsR = 2; _pColsR = 2;
      return 1;
    }
    if (n === 3) {
      var c00 = _pa[4] * _pa[8] - _pa[5] * _pa[7];
      var c01 = -(_pa[3] * _pa[8] - _pa[5] * _pa[6]);
      var c02 = _pa[3] * _pa[7] - _pa[4] * _pa[6];
      var c10 = -(_pa[1] * _pa[8] - _pa[2] * _pa[7]);
      var c11 = _pa[0] * _pa[8] - _pa[2] * _pa[6];
      var c12 = -(_pa[0] * _pa[7] - _pa[1] * _pa[6]);
      var c20 = _pa[1] * _pa[5] - _pa[2] * _pa[4];
      var c21 = -(_pa[0] * _pa[5] - _pa[2] * _pa[3]);
      var c22 = _pa[0] * _pa[4] - _pa[1] * _pa[3];
      var d3 = _pa[0] * c00 + _pa[1] * c01 + _pa[2] * c02;
      if (d3 > -SINGULAR_TOL && d3 < SINGULAR_TOL) { _err = 4; return -1; }
      _det = d3;
      _pr[0] = c00 / d3; _pr[1] = c10 / d3; _pr[2] = c20 / d3;
      _pr[3] = c01 / d3; _pr[4] = c11 / d3; _pr[5] = c21 / d3;
      _pr[6] = c02 / d3; _pr[7] = c12 / d3; _pr[8] = c22 / d3;
      _pRowsR = 3; _pColsR = 3;
      return 1;
    }
    /* 4..12：增广语义的 in-place Gauss-Jordan
     * det 同步记账：每列主元 f 累乘、行交换计奇偶，det = (−1)^swaps·∏f
     * （行归一化不改变行列式，LU 型乘积即精确记账）。 */
    for (i = 0; i < n * n; i++) { _luBuf[i] = _pa[i]; }
    for (i = 0; i < n; i++) {
      for (j = 0; j < n; j++) { _pr[i * n + j] = (i === j) ? 1.0 : 0.0; }
    }
    var detAcc = 1.0;
    var swapCnt = 0;
    for (col = 0; col < n; col++) {
      piv = col;
      maxAbs = _luBuf[col * n + col];
      if (maxAbs < 0.0) { maxAbs = -maxAbs; }
      for (row = col + 1; row < n; row++) {
        v = _luBuf[row * n + col];
        if (v < 0.0) { v = -v; }
        if (v > maxAbs) { maxAbs = v; piv = row; }
      }
      if (maxAbs < 1e-12) { _err = 4; return -1; }
      if (piv !== col) {
        for (c2 = 0; c2 < n; c2++) {
          var ta = _luBuf[col * n + c2];
          _luBuf[col * n + c2] = _luBuf[piv * n + c2];
          _luBuf[piv * n + c2] = ta;
          var tb = _pr[col * n + c2];
          _pr[col * n + c2] = _pr[piv * n + c2];
          _pr[piv * n + c2] = tb;
        }
        swapCnt++;
      }
      f = _luBuf[col * n + col];
      detAcc *= f;
      for (c2 = 0; c2 < n; c2++) {
        _luBuf[col * n + c2] /= f;
        _pr[col * n + c2] /= f;
      }
      for (row = 0; row < n; row++) {
        if (row === col) { continue; }
        f = _luBuf[row * n + col];
        if (f === 0.0) { continue; }
        for (c2 = 0; c2 < n; c2++) {
          _luBuf[row * n + c2] -= f * _luBuf[col * n + c2];
          _pr[row * n + c2] -= f * _pr[col * n + c2];
        }
      }
    }
    _det = (swapCnt % 2 === 0) ? detAcc : -detAcc;
    _pRowsR = n; _pColsR = n;
    return 1;
  }

  /* pro rank：矩形 m×n 部分主元阶梯消元 */

  function proRank() {
    _err = 0;
    if (!_pDimsOK(_pRowsA, _pColsA)) { _err = 1; return -1; }
    _opCount++;
    var m = _pRowsA;
    var n = _pColsA;
    var i;
    for (i = 0; i < m * n; i++) { _luBuf[i] = _pa[i]; }
    return _echelon(m, n);
  }

  /* pro LU：P·A = L·U + piv（C ac_mat_lu 逐字移植）
   * 打包存储：_luBuf 对角及以上 = U，对角以下 = L 的乘数；L 单位下三角
   * 由 proLUGetL 组装（对角恒 1）。piv[i] = 第 i 行来自原矩阵的行号。
   * 奇异（任一主元 |·| < 1e-12）→ −1 + err 4，L/U/piv 内容不得使用。 */
  function proLU() {
    _err = 0;
    if (!_pSquareOK()) { return -1; }
    _opCount++;
    var n = _pN;
    var i, j, k, pivRow, t;
    var maxAbs, v, f;
    for (i = 0; i < n; i++) {
      _piv[i] = i;
      for (j = 0; j < n; j++) {
        _luBuf[i * n + j] = _pa[i * n + j];
      }
    }
    for (k = 0; k < n; k++) {
      pivRow = k;
      maxAbs = _luBuf[k * n + k];
      if (maxAbs < 0.0) { maxAbs = -maxAbs; }
      for (i = k + 1; i < n; i++) {
        v = _luBuf[i * n + k];
        if (v < 0.0) { v = -v; }
        if (v > maxAbs) { maxAbs = v; pivRow = i; }
      }
      if (maxAbs < 1e-12) { _err = 4; _luN = 0; return -1; }
      if (pivRow !== k) {
        /* 打包行整行交换即同时完成 C 侧 U 整行与 L 前缀两处交换 */
        for (j = 0; j < n; j++) {
          v = _luBuf[k * n + j];
          _luBuf[k * n + j] = _luBuf[pivRow * n + j];
          _luBuf[pivRow * n + j] = v;
        }
        t = _piv[k]; _piv[k] = _piv[pivRow]; _piv[pivRow] = t;
      }
      for (i = k + 1; i < n; i++) {
        f = _luBuf[i * n + k] / _luBuf[k * n + k];
        _luBuf[i * n + k] = f;              /* 乘数存 L（下三角） */
        if (f === 0.0) { continue; }
        for (j = k; j < n; j++) {
          _luBuf[i * n + j] -= f * _luBuf[k * n + j];
        }
        _luBuf[i * n + k] = f;              /* 显式回写，清掉 −0.0/残渣（C 同款） */
      }
    }
    _luN = n;
    return 1;
  }

  function proLUGetL(i, j) {
    if (i < 0 || j < 0 || i >= _luN || j >= _luN) { return NaN; }
    if (j < i) { return _luBuf[i * _luN + j]; }
    if (j === i) { return 1.0; }
    return 0.0;
  }
  function proLUGetU(i, j) {
    if (i < 0 || j < 0 || i >= _luN || j >= _luN) { return NaN; }
    if (j >= i) { return _luBuf[i * _luN + j]; }
    return 0.0;
  }
  function proLUGetPiv(i) {
    if (i < 0 || i >= _luN) { return -1; }
    return _piv[i];
  }
  function proLUN() { return _luN; }

  /* pro eig：幂法 + Hotelling deflation（ac_pro_eigen.c 逐字移植）
   * 返回值 = C 侧同款错误码：1 成功 / 0 内部错误 / −1 维度越界 /
   * −2 非对称 / −3 不收敛 / −4 零矩阵。结果：λ 在 _eigLam（|λ| 降序），
   * 特征向量第 k 个的分量 i 在 _vBuf[k·n + i]（C 列主序访问器同布局）。
   * 常量与 C 侧逐字同值（ac_pro_eigen.c:44-50）。 */
  function _eigNorm2(voff, n) {
    var s = 0.0;
    var i;
    for (i = 0; i < n; i++) { s += _vBuf[voff + i] * _vBuf[voff + i]; }
    return Math.sqrt(s);
  }

  function _eigDot(xoff, yoff, n) {
    var s = 0.0;
    var i;
    for (i = 0; i < n; i++) { s += _work[xoff + i] * _work[yoff + i]; }
    return s;
  }

  /* y = M·x（M 行主序 n×n 在 buf 内），x/y 位于 _work，不别名 */
  function _eigMatvec(buf, xoff, yoff, n) {
    var i, j;
    for (i = 0; i < n; i++) {
      var s = 0.0;
      for (j = 0; j < n; j++) { s += buf[i * n + j] * _work[xoff + j]; }
      _work[yoff + i] = s;
    }
  }

  function _eigInfnorm(n) {
    var best = 0.0;
    var i, j;
    for (i = 0; i < n; i++) {
      var s = 0.0;
      for (j = 0; j < n; j++) {
        var v = _luBuf[i * n + j];
        if (v < 0.0) { v = -v; }
        s += v;
      }
      if (s > best) { best = s; }
    }
    return best;
  }

  /* 每轮从原始 _pa 重拷 deflated 矩阵进 _luBuf，再扣 Σ λ_j·v_j·v_jᵀ */
  function _eigBuildDeflated(n, k) {
    var i, j, t;
    for (i = 0; i < n; i++) {
      for (j = 0; j < n; j++) { _luBuf[i * n + j] = _pa[i * n + j]; }
    }
    for (t = 0; t < k; t++) {
      var lj = _eigLam[t];
      var i2, j2;
      for (i2 = 0; i2 < n; i2++) {
        var li = lj * _vBuf[t * n + i2];
        for (j2 = 0; j2 < n; j2++) {
          _luBuf[i2 * n + j2] -= li * _vBuf[t * n + j2];
        }
      }
    }
  }

  /* 在 _luBuf 的矩阵上求主特征对。返回 −2 表示初值被已求向量吃光；
   * λ 经 _eigLamTmp 标量出参带回。 */
  function _eigPower(n, mode, kprev, voff) {
    var x = 0;                  /* _work[0..n) */
    var y = PRO_MAX_DIM;        /* _work[12..24)（n ≤ 12 不重叠） */
    var scale, ny, res, resPrev, d;
    var i, j, t, pass;
    var converged = 0;
    resPrev = 1e300;
    var lam = 0.0;

    scale = _eigInfnorm(n);
    if (scale <= 0.0) { scale = 1.0; }

    if (mode === 0) {
      for (i = 0; i < n; i++) { _work[x + i] = 1.0; }
    } else if (mode === 1) {
      for (i = 0; i < n; i++) { _work[x + i] = 1.0 + 0.5 * i; }
    } else if (mode === 2) {
      for (i = 0; i < n; i++) { _work[x + i] = ((i & 1) !== 0 ? -1.0 : 1.0) * (1.0 + 0.25 * i); }
    } else {
      var e = mode - 3;
      for (i = 0; i < n; i++) { _work[x + i] = (i === e) ? 1.0 : 0.0; }
    }

    /* 两遍 MGS：单遍的正交性会随 kprev 退化（C 同款） */
    for (pass = 0; pass < 2; pass++) {
      for (j = 0; j < kprev; j++) {
        var c = 0.0;
        for (i = 0; i < n; i++) { c += _vBuf[j * n + i] * _work[x + i]; }
        for (i = 0; i < n; i++) { _work[x + i] -= c * _vBuf[j * n + i]; }
      }
    }
    ny = 0.0;
    for (i = 0; i < n; i++) { ny += _work[x + i] * _work[x + i]; }
    ny = Math.sqrt(ny);
    if (ny <= 1e-10) { return -2; }             /* PRO_EIG_START_COLL */
    for (i = 0; i < n; i++) { _work[x + i] /= ny; }

    for (t = 0; t < 400; t++) {                 /* PRO_EIG_MAX_ITER */
      _eigMatvec(_luBuf, x, y, n);
      lam = _eigDot(x, y, n);                   /* x 单位 → Rayleigh 商 */
      ny = 0.0;
      for (i = 0; i < n; i++) { ny += _work[y + i] * _work[y + i]; }
      ny = Math.sqrt(ny);

      if (ny <= 1e-13 * scale) {
        /* 退化：D·x = 0，x 是 λ=0 的零空间向量（自动 ⊥ 已求向量） */
        for (i = 0; i < n; i++) { _vBuf[voff + i] = _work[x + i]; }
        _eigLamTmp = 0.0;
        return 0;
      }

      res = 0.0;
      for (i = 0; i < n; i++) {
        d = _work[y + i] - lam * _work[x + i];
        res += d * d;
      }
      res = Math.sqrt(res);
      if (res <= 1e-13 * scale) { converged = 1; break; }

      for (i = 0; i < n; i++) { _work[x + i] = _work[y + i] / ny; }

      /* 停滞判据盯残差：Rayleigh 商二阶精确，|Δλ| 不作判据（C 同款注释） */
      if (t >= 1 && res > 0.9 * resPrev) { converged = 1; break; }
      resPrev = res;
    }

    /* 收尾在最终 x 上重算 Rayleigh 商，使 (v, λ) 同步 */
    _eigMatvec(_luBuf, x, y, n);
    lam = _eigDot(x, y, n);
    for (i = 0; i < n; i++) { _vBuf[voff + i] = _work[x + i]; }
    _eigLamTmp = lam;
    return converged === 1 ? 1 : -1;
  }

  var _eigLamTmp = 0.0;         /* _eigPower 的 λ 出参（标量寄存，免指针） */

  /* 在（已位移的）_luBuf 矩阵上求 M·v = 0 的非零解；破坏 _luBuf。
   * 自由变量取最后一个自由列，自下而上回代（C eig_null 同款）。 */
  function _eigNull(n, voff) {
    var row, col, i, j, pc, freeCol;
    var scale = 0.0, tol, best, f, s;
    for (i = 0; i < n * n; i++) {
      var v = _luBuf[i];
      if (v < 0.0) { v = -v; }
      if (v > scale) { scale = v; }
    }
    if (scale <= 0.0) {
      for (i = 0; i < n; i++) { _vBuf[voff + i] = (i === 0) ? 1.0 : 0.0; }
      return 1;
    }
    tol = 1e-12 * scale;        /* PRO_EIG_RANK_REL */

    row = 0;
    for (col = 0; col < n && row < n; col++) {
      var p = row;
      best = _luBuf[row * n + col];
      if (best < 0.0) { best = -best; }
      for (i = row + 1; i < n; i++) {
        f = _luBuf[i * n + col];
        if (f < 0.0) { f = -f; }
        if (f > best) { best = f; p = i; }
      }
      if (best <= tol) { _piv[col] = -1; continue; }
      if (p !== row) {
        for (j = 0; j < n; j++) {
          var t = _luBuf[row * n + j];
          _luBuf[row * n + j] = _luBuf[p * n + j];
          _luBuf[p * n + j] = t;
        }
      }
      for (i = row + 1; i < n; i++) {
        f = _luBuf[i * n + col] / _luBuf[row * n + col];
        if (f === 0.0) { continue; }
        for (j = col; j < n; j++) { _luBuf[i * n + j] -= f * _luBuf[row * n + j]; }
        _luBuf[i * n + col] = 0.0;
      }
      _piv[col] = row;
      row++;
    }

    if (row === n) { return 0; }

    freeCol = -1;
    for (col = n - 1; col >= 0; col--) {
      if (_piv[col] < 0) { freeCol = col; break; }
    }
    if (freeCol < 0) { return 0; }

    for (i = 0; i < n; i++) { _vBuf[voff + i] = 0.0; }
    _vBuf[voff + freeCol] = 1.0;

    for (i = row - 1; i >= 0; i--) {
      pc = -1;
      for (col = 0; col < n; col++) {
        if (_piv[col] === i) { pc = col; break; }
      }
      if (pc < 0) { continue; }
      s = 0.0;
      for (j = pc + 1; j < n; j++) { s += _luBuf[i * n + j] * _vBuf[voff + j]; }
      _vBuf[voff + pc] = -s / _luBuf[i * n + pc];
    }
    return 1;
  }

  /* 对 V 的前 k 个向量再正交化，用原始 A 复核 λ 与残差；不过闸返回 0 */
  function _eigAccept(n, k, voff, scale) {
    var ny, resid, d;
    var i, j, pass;
    for (pass = 0; pass < 2; pass++) {
      for (j = 0; j < k; j++) {
        var c = 0.0;
        for (i = 0; i < n; i++) { c += _vBuf[j * n + i] * _vBuf[voff + i]; }
        for (i = 0; i < n; i++) { _vBuf[voff + i] -= c * _vBuf[j * n + i]; }
      }
    }
    ny = _eigNorm2(voff, n);
    if (ny <= 1e-10) { return 0; }
    for (i = 0; i < n; i++) { _vBuf[voff + i] /= ny; }

    /* A·v 进 _work[0..n)（v 在 _vBuf 行 voff，A 只读） */
    for (i = 0; i < n; i++) {
      var s = 0.0;
      for (j = 0; j < n; j++) { s += _pa[i * n + j] * _vBuf[voff + j]; }
      _work[i] = s;
    }
    var lam = 0.0;
    for (i = 0; i < n; i++) { lam += _vBuf[voff + i] * _work[i]; }
    resid = 0.0;
    for (i = 0; i < n; i++) {
      d = _work[i] - lam * _vBuf[voff + i];
      if (d < 0.0) { d = -d; }
      if (d > resid) { resid = d; }
    }
    _eigLamTmp = lam;
    if (resid > 1e-8 * (1.0 + scale)) { return 0; }   /* PRO_EIG_ACCEPT */
    return 1;
  }

  function proEig() {
    _err = 0;
    if (!_pSquareOK()) { _eigCount = 0; return -1; }
    _opCount++;
    var n = _pN;
    var i, j, k, t, rc, got;
    var scale = 0.0;

    _eigCount = 0;
    if (n > PRO_MAX_DIM) { _err = 1; return -1; }

    for (i = 0; i < n * n; i++) {
      var v = _pa[i];
      if (v < 0.0) { v = -v; }
      if (v > scale) { scale = v; }
    }
    if (scale <= 0.0) { _err = 7; return -4; }   /* 零矩阵：无可归一化特征向量 */

    /* 非对称 → −2：Hotelling 收缩在非对称矩阵上不保谱（C 同款） */
    var symMax = scale > 1.0 ? scale : 1.0;
    for (i = 0; i < n; i++) {
      for (j = i + 1; j < n; j++) {
        var df = _pa[i * n + j] - _pa[j * n + i];
        if (df < 0.0) { df = -df; }
        if (df > 1e-12 * symMax) { _err = 6; return -2; }
      }
    }

    for (k = 0; k < n; k++) {
      var voff = k * n;
      _eigBuildDeflated(n, k);
      got = 0;
      var bestLam = 0.0;

      /* 多初值重试（3 解析 + n 单位基），取第一个过残差闸门的候选 */
      for (t = 0; t < 3 + n && got === 0; t++) {
        rc = _eigPower(n, t, k, voff);
        if (rc === -2) { continue; }            /* 初值落在已求张成空间，换下一个 */
        bestLam = _eigLamTmp;
        if (_eigAccept(n, k, voff, scale) === 1) {
          _eigLam[k] = _eigLamTmp;
          got = 1;
        }
      }

      /* 兜底零空间路径：先最后候选 λ 位移，再 λ = 0 */
      for (t = 0; t < 2 && got === 0; t++) {
        var shift = (t === 0) ? bestLam : 0.0;
        _eigBuildDeflated(n, k);
        for (i = 0; i < n; i++) { _luBuf[i * n + i] -= shift; }
        if (_eigNull(n, voff) !== 1) { continue; }
        if (_eigAccept(n, k, voff, scale) === 1) {
          _eigLam[k] = _eigLamTmp;
          got = 1;
        }
      }

      if (got === 0) {
        _eigCount = 0;
        _err = 5;
        return -3;
      }
    }

    /* |λ| 降序选择排序，向量随行交换（C 同款） */
    for (i = 0; i < n; i++) {
      var m = i;
      for (j = i + 1; j < n; j++) {
        if (Math.abs(_eigLam[j]) > Math.abs(_eigLam[m])) { m = j; }
      }
      if (m !== i) {
        var tl = _eigLam[i]; _eigLam[i] = _eigLam[m]; _eigLam[m] = tl;
        for (t = 0; t < n; t++) {
          var tv = _vBuf[i * n + t];
          _vBuf[i * n + t] = _vBuf[m * n + t];
          _vBuf[m * n + t] = tv;
        }
      }
    }

    _eigCount = n;
    return 1;
  }

  function proEigValue(k) {
    if (k < 0 || k >= _eigCount) { return 0.0; }
    return _eigLam[k];
  }
  function proEigVector(k, i) {
    if (k < 0 || i < 0 || k >= _eigCount || i >= _eigCount) { return NaN; }
    return _vBuf[k * _eigCount + i];
  }
  function proEigCount() { return _eigCount; }

  /* pro QR：Gram-Schmidt（C ac_mat_qr 逐字移植）
   * A（m×n，proSetA 装入）→ Q m×n 正交列（_vBuf，proQRGetQ 读取）+
   * R n×n 上三角（结果寄存器 _pr，proGetR 读取）。秩亏列 → R[j][j]=0
   * 且 q 零列（C 成文语义）。_pa 只读，无别名问题（C 需中转拷贝的
   * 原因是 q/r 可别名 a，本实现输出在独立池）。 */
  function proQR() {
    _err = 0;
    if (!_pDimsOK(_pRowsA, _pColsA)) { _err = 1; return -1; }
    _opCount++;
    var m = _pRowsA;
    var n = _pColsA;
    var i, j, k;
    for (i = 0; i < n * n; i++) { _pr[i] = 0.0; }   /* R 全清零（下三角保持 0） */

    for (j = 0; j < n; j++) {
      /* v = A[:, j]（_work[0..m)） */
      for (i = 0; i < m; i++) { _work[i] = _pa[i * n + j]; }
      /* 减去在前各列 q_i 上的投影：R[i][j] = q_i·v，v −= R[i][j]·q_i */
      for (i = 0; i < j; i++) {
        var rij = 0.0;
        for (k = 0; k < m; k++) { rij += _vBuf[k * n + i] * _work[k]; }
        _pr[i * n + j] = rij;
        for (k = 0; k < m; k++) { _work[k] -= rij * _vBuf[k * n + i]; }
      }
      /* R[j][j] = ‖v‖；归一化得 q_j（退化列 → 零列 + R[j][j] = 0） */
      var nj = 0.0;
      for (k = 0; k < m; k++) { nj += _work[k] * _work[k]; }
      nj = Math.sqrt(nj);
      _pr[j * n + j] = nj;
      for (k = 0; k < m; k++) {
        _vBuf[k * n + j] = (nj > 1e-12) ? _work[k] / nj : 0.0;
      }
    }
    _qrM = m; _qrN = n;
    _pRowsR = n; _pColsR = n;
    return 1;
  }

  function proQRGetQ(r, c) {
    if (r < 0 || c < 0 || r >= _qrM || c >= _qrN) { return NaN; }
    return _vBuf[r * _qrN + c];
  }
  function proQRRows() { return _qrM; }
  function proQRCols() { return _qrN; }

  return {
    MAX_DIM: MAX_DIM,
    PRO_MAX_DIM: PRO_MAX_DIM,

    setA: setA,
    setB: setB,
    getR: getR,
    getRRows: getRRows,
    getRCols: getRCols,
    getLastDet: getLastDet,

    mul: mul,
    transpose: transpose,
    det: det,
    inv: inv,

    proSetA: proSetA,
    proGetR: proGetR,
    proGetRRows: proGetRRows,
    proGetRCols: proGetRCols,
    proGetLastDet: proGetLastDet,
    proAllocCount: proAllocCount,

    proDet: proDet,
    proInv: proInv,
    proRank: proRank,

    proLU: proLU,
    proLUGetL: proLUGetL,
    proLUGetU: proLUGetU,
    proLUGetPiv: proLUGetPiv,
    proLUN: proLUN,

    proEig: proEig,
    proEigValue: proEigValue,
    proEigVector: proEigVector,
    proEigCount: proEigCount,

    proQR: proQR,
    proQRGetQ: proQRGetQ,
    proQRRows: proQRRows,
    proQRCols: proQRCols,

    reset: reset,
    getError: getError,
    getOpCount: getOpCount,
    allocCount: allocCount,
    aBuffer: aBuffer,
    bBuffer: bBuffer,
    rBuffer: rBuffer
  };
})();

if (typeof module === "object" && module && module.exports) {
  module.exports = ApolloMatrix;
}
