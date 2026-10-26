/* ApolloCalc-CAS · JS/128KB · core_special.js
 * 高阶特殊函数：误差函数 erf、伽马函数 gamma、对数伽马 lgamma。自包含。
 *
 * 纯标量数值函数：无池、无寄存器、无递归；加载期与热路径分配均为 0
 * （Lanczos 级数与 A&S 系数全部手工展开为标量常量，无数组字面量）。
 *
 * 精度口径：
 *   erf        A&S 26.2.17 有理逼近，|ε| ≤ 1.5e-7，全域（大 |x| 自然饱和到 ±1）
 *   gamma      Lanczos g=7 / n=9，x ≥ 0.5 相对误差 ~1e-13；x < 0.5 反射公式
 *              Γ(x) = π / (sin(πx)·Γ(1−x))，手工展开、无自调用
 *              （与 C/128KB src/ac_special.c 同一条款，系数同表）
 *   lgamma     同一 Lanczos 核取对数形态，反射走 ln π − ln|sin πx| − lgamma(1−x)
 *
 * 极点契约：gamma/lgamma 在 0 与负整数处返回 +Infinity（对数伽马在极点
 * 附近本号趋向 +∞，不是 −∞，故统一 +Infinity 而非 NaN）。
 * 非有限输入：NaN 输入返回 NaN 且 _err = 1；±Infinity 有定义值不设错。
 *
 * 与 C 版的数值对齐口径：erf 双方实现路径不同（C 正项级数 / JS 有理逼近），
 * 对齐容差按各自精度下限取 2e-7；gamma/lgamma 双方同表 Lanczos，容差 1e-9。
 *
 * 导出三通道：Node require / 浏览器 <script> 顶层 var / 内核拼接 ESM。
 */

var ApolloSpecial = (function () {
  "use strict";

  var _opCount = 0;
  var _err = 0;
  var _allocCount = 0;              /* 无池：加载期零分配 */

  /* Lanczos g=7, n=9 系数（与 C/128KB ac_special.c 同表）。
   * p[0] 前置因子之外，级数项为 c_i / (z + i)，i = 1..8。 */
  var LC0 = 0.99999999999980993;
  var LC1 = 676.5203681218851;
  var LC2 = -1259.1392167224028;
  var LC3 = 771.32342877765313;
  var LC4 = -176.61502916214059;
  var LC5 = 12.507343278686905;
  var LC6 = -0.13857109526572012;
  var LC7 = 9.9843695780195716e-6;
  var LC8 = 1.5056327351493116e-7;

  var SQRT_2PI = 2.5066282746310002;      /* √(2π) */
  var LN_SQRT_2PI = 0.9189385332046727;   /* ln √(2π) */
  var LN_PI = 1.1447298858494002;         /* ln π */
  var LANCZOS_G_HALF = 7.5;               /* g + 0.5，级数位移项 z + 7.5 */

  function allocCount() { return _allocCount; }
  function getOpCount() { return _opCount; }
  function getError() { return _err; }

  function reset() {
    _opCount = 0;
    _err = 0;
    return 0;
  }

  /* A&S 26.2.17：t = 1/(1 + p·|x|)，erf = 1 − poly(t)·e^(−x²)，|ε| ≤ 1.5e-7。
   * 奇对称手工分支：erf(−x) = −erf(x)。x ≥ 6 时 poly(t)·e^(−x²) 已低于
   * 1 的半个 ulp，结果自然饱和到 ±1，无需专门分支。 */
  function erf(x) {
    _err = 0;
    _opCount++;
    if (x !== x) { _err = 1; return NaN; }
    if (x === Infinity) { return 1; }
    if (x === -Infinity) { return -1; }
    var neg = x < 0;
    var ax = neg ? -x : x;
    var t = 1 / (1 + 0.3275911 * ax);
    var y = t * (0.254829592 + t * (-0.284496736 + t * (1.421413741 +
           t * (-1.453152027 + t * 1.061405429)))) * Math.exp(-ax * ax);
    var v = 1 - y;
    return neg ? -v : v;
  }

  /* Lanczos 级数 A(z) 的对数，z = x − 1，x ≥ 0.5 保证分母全为正。 */
  function _lanczosLogA(z) {
    var a = LC0
          + LC1 / (z + 1) + LC2 / (z + 2) + LC3 / (z + 3) + LC4 / (z + 4)
          + LC5 / (z + 5) + LC6 / (z + 6) + LC7 / (z + 7) + LC8 / (z + 8);
    return Math.log(a);
  }

  /* ln Γ(x)，x ≥ 0.5。Stirling 核：ln Γ = ln √(2π) + (z+½)ln(z+g+½) − (z+g+½) + ln A(z) */
  function _lgammaHalf(x) {
    var z = x - 1;
    var t = z + LANCZOS_G_HALF;
    return LN_SQRT_2PI + (z + 0.5) * Math.log(t) - t + _lanczosLogA(z);
  }

  /* Γ(x)，x ≥ 0.5。与 _lgammaHalf 同核取指数形态（先算对数再 exp，
   * 大参数下比直接乘积形态少一次上溢风险）。 */
  function _gammaHalf(x) {
    return Math.exp(_lgammaHalf(x));
  }

  function lgamma(x) {
    _err = 0;
    _opCount++;
    if (x !== x) { _err = 1; return NaN; }
    if (x === Infinity) { return Infinity; }
    if (x === -Infinity) { _err = 1; return NaN; }
    if (x <= 0 && x === Math.floor(x)) { return Infinity; }     /* 极点 */
    if (x < 0.5) {
      /* 反射：ln|Γ(x)| = ln π − ln|sin πx| − ln Γ(1−x) */
      return LN_PI - Math.log(Math.abs(Math.sin(Math.PI * x))) - _lgammaHalf(1 - x);
    }
    return _lgammaHalf(x);
  }

  function gamma(x) {
    _err = 0;
    _opCount++;
    if (x !== x) { _err = 1; return NaN; }
    if (x === Infinity) { return Infinity; }
    if (x === -Infinity) { _err = 1; return NaN; }
    if (x <= 0 && x === Math.floor(x)) { return Infinity; }     /* 极点 */
    if (x < 0.5) {
      /* 反射：Γ(x) = π / (sin(πx)·Γ(1−x))，手工展开，无自调用 */
      return Math.PI / (Math.sin(Math.PI * x) * _gammaHalf(1 - x));
    }
    return _gammaHalf(x);
  }

  return {
    erf: erf,
    gamma: gamma,
    lgamma: lgamma,

    reset: reset,
    getError: getError,
    getOpCount: getOpCount,
    allocCount: allocCount
  };
})();

if (typeof module === "object" && module && module.exports) {
  module.exports = ApolloSpecial;
}
