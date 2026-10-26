/* ApolloCalc-CAS · JS/128KB · core_matrix.js
 * 基础矩阵运算：乘法 / 转置 / 行列式 / 逆矩阵（≤3×3 闭式），自包含。
 *
 * 寄存器模型：A、B 两个输入寄存器 + R 结果寄存器，各 3×3 Float64Array 行主序。
 * UI 调用序 setA/setB → mul/transpose/det/inv → getR*（与复数域
 * setComplexA → cXxx → getComplexTmp* 的契约同构）。
 *
 * 边界：行列式与逆只支持 1..3 阶方阵 —— Sarrus 法则与伴随矩阵法闭式，
 * 无消元、无递归、无选主元。12 元线性方程组的稠密消元是 C/128KB
 * ac_matrix（C 独有）的能力；本模块是浏览器侧的基础块，不是对等实现。
 *
 * 零 GC：3 个 TypedArray 加载期一次分配（3 × 72 = 216 B），全路径无
 * 对象/数组字面量、无字符串操作、无递归。
 * 错误码（getError）：0 OK / 1 维度超界（>3 或 ≤0 或下标越界）/
 * 2 乘法内维不匹配 / 3 非方阵 / 4 奇异（|det| ≤ 1e-12）。
 *
 * 导出三通道：Node require / 浏览器 <script> 顶层 var / 内核拼接 ESM。
 */

var ApolloMatrix = (function () {
  "use strict";

  var MAX_DIM = 3;
  var CAP = MAX_DIM * MAX_DIM;      /* 9 */
  var SINGULAR_TOL = 1e-12;         /* 与 C/128KB ac_matrix 的主元阈值同值 */

  var _a = new Float64Array(CAP);   /* 输入寄存器 A（m×k） */
  var _b = new Float64Array(CAP);   /* 输入寄存器 B（k×n） */
  var _r = new Float64Array(CAP);   /* 结果寄存器 R */

  var _rowsA = 0, _colsA = 0;
  var _rowsB = 0, _colsB = 0;
  var _rowsR = 0, _colsR = 0;

  var _err = 0;
  var _opCount = 0;
  var _allocCount = 3;
  var _det = 0;                     /* det()/inv() 最近一次的行列式值 */

  function allocCount() { return _allocCount; }
  function getOpCount() { return _opCount; }
  function getError() { return _err; }

  function aBuffer() { return _a; }
  function bBuffer() { return _b; }
  function rBuffer() { return _r; }

  function _dimsOK(r, c) {
    return r >= 1 && r <= MAX_DIM && c >= 1 && c <= MAX_DIM;
  }

  /* 写输入寄存器：dims + 行主序下标一次给定，越界拒绝不写。
   * 重复调用覆盖旧值 —— 换矩阵前不要求 reset。 */
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
    /* 3×3 Sarrus：a(ei − fh) − b(di − fg) + c(dh − eg) */
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

  return {
    MAX_DIM: MAX_DIM,

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
