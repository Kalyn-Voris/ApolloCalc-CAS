/* ApolloCalc · core_calculus.js — 数值微积分（文件 7）
 * Lite Wearable / JerryScript / 堆上限 128KB / Zero-GC
 * 职责：一阶导数（中心差分）、二阶导数（五点公式）、
 *       定积分（自适应复合辛普森）、数值极限（左右逼近）、
 *       泰勒展开（逐阶数值导数 + 中心差分表，系数缓存 Horner 求值）
 * 依赖：文件 1（全局池 tokenPool / tokenCount）、
 *       文件 5（evalRPN / toRPN / _rpnToken / _evalX / _evalError 全局状态，
 *       复用是有意为之的性能优化：积分最坏 ~1.3 万次求值只做一次 toRPN）
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象/数组字面量
 * UI 契约：表达式变更后必须先 calculusReset() 再 calcXxx，
 *          否则 _calcRpnReady 会复用旧表达式的缓存 RPN */

/* 文件 7 专属全局变量（唯一声明处） */
var _calcResult = 0.0;
var _calcError = 0;             /* 0=OK 1=域错误 2=不收敛 3=迭代超限 4=内部错 */
var _calcIterations = 0;        /* 诊断用 */
var _calcRpnReady = 0;          /* 0=未缓存 1=已缓存（表达式变更前必须经 calculusReset 清零） */

/* 模块一：域检查与内部求值 */

/* 扫描 tokenPool 是否含等号；含等号表示这是方程而不是函数 → 微积分域错误 */
function calcCheckFunction() {
  var i = 0;
  for (i = 0; i < tokenCount; i++) {
    if ((tokenPool[i] >> 8) === T_EQ) { return 1; }
  }
  return 0;
}

/* 缓存 RPN 并求值一次（首次调用做 toRPN，后续微积分操作直接复用） */
function calcPrepareRPN() {
  if (_calcRpnReady === 1) { return 0; }
  _evalError = 0;   /* toRPN 成功时不清全局 _evalError，须先清零再查（与 evalExpressionAt 契约一致），否则上游遗留的域错误码会毒化缓存准备 */
  _rpnLen = toRPN(tokenPool, tokenCount, _rpnToken);
  if (_evalError !== 0) { return _evalError; }
  _calcRpnReady = 1;
  return 0;
}

/* 轻量复位：只清 RPN 缓存与诊断量，不清 _calcResult */
function calcResetRPN() {
  _calcRpnReady = 0;
  _calcIterations = 0;
  _calcError = 0;
}

/* 模块二：一阶导数（中心差分）
 * f'(x) ≈ (f(x+h) − f(x−h)) / (2h)
 * h = 1e-5 · max(|x|, 1)：x 近 0 时 h=1e-5；x 大时 h 正比于 x，
 * 避免 x+h 与 x 因浮点精度无法区分 */
function calcDerivative(x) {
  if (!isFinite(x)) { _calcError = 1; return NaN; }
  if (calcCheckFunction() !== 0) { _calcError = 1; return NaN; }
  if (calcPrepareRPN() !== 0) { _calcError = 4; return NaN; }

  var ax = x < 0 ? -x : x;
  var h = 1e-5 * (ax > 1 ? ax : 1);

  _evalX = x + h;
  _evalError = 0;
  var yPlus = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }

  _evalX = x - h;
  _evalError = 0;
  var yMinus = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }

  _calcIterations = 2;
  _calcResult = (yPlus - yMinus) / (2 * h);
  if (!isFinite(_calcResult)) { _calcError = 1; return NaN; }
  _calcError = 0;
  return _calcResult;
}

/* 模块三：二阶导数（五点公式）
 * f''(x) ≈ (−f(x+2h) + 16f(x+h) − 30f(x) + 16f(x−h) − f(x−2h)) / (12h²)
 * h = 1e-4 · max(|x|, 1)（二阶差分舍入更敏感，h 略大于一阶） */
function calcDerivative2(x) {
  if (!isFinite(x)) { _calcError = 1; return NaN; }
  if (calcCheckFunction() !== 0) { _calcError = 1; return NaN; }
  if (calcPrepareRPN() !== 0) { _calcError = 4; return NaN; }

  var ax = x < 0 ? -x : x;
  var h = 1e-4 * (ax > 1 ? ax : 1);
  var h2 = h * h;

  _evalX = x + 2 * h; _evalError = 0; var fp2 = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }
  _evalX = x + h;     _evalError = 0; var fp1 = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }
  _evalX = x;         _evalError = 0; var f0 = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }
  _evalX = x - h;     _evalError = 0; var fm1 = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }
  _evalX = x - 2 * h; _evalError = 0; var fm2 = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }

  _calcIterations = 5;
  _calcResult = (-fp2 + 16 * fp1 - 30 * f0 + 16 * fm1 - fm2) / (12 * h2);
  if (!isFinite(_calcResult)) { _calcError = 1; return NaN; }
  _calcError = 0;
  return _calcResult;
}

/* 模块四：定积分（自适应复合辛普森）
 * ∫[a,b] f dx ≈ (h/3)[f(a) + 4Σf(奇内点) + 2Σf(偶内点) + f(b)]
 * 自适应：n 从 100 起每轮加倍（上限 10000），前后两轮差 < 1e-8 即停；
 * 每轮单次遍历累加 sumOdd/sumEven，不缓存采样点（辛普森节点随 n 变化不可复用）；
 * 采样点 x = a + i·h 用乘法重算，不做 x += h 累积（避免浮点误差漂移）；
 * n 加倍到上限仍未收敛时返回当前结果且 _calcError=0，由 UI 层自行判断 */
function calcIntegral(a, b) {
  if (!isFinite(a) || !isFinite(b)) { _calcError = 1; return NaN; }
  if (a === b) { _calcResult = 0; _calcError = 0; return 0; }
  if (calcCheckFunction() !== 0) { _calcError = 1; return NaN; }
  if (calcPrepareRPN() !== 0) { _calcError = 4; return NaN; }

  var fa = 0.0;
  var fb = 0.0;
  var i = 0;
  var n = 0;
  var h = 0.0;
  var x = 0.0;
  var y = 0.0;
  var sumOdd = 0.0;
  var sumEven = 0.0;
  var prev = 0.0;
  var curr = 0.0;
  var diff = 0.0;
  var iterTotal = 0;

  /* 端点 f(a)、f(b) */
  _evalX = a; _evalError = 0;
  fa = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }
  _evalX = b; _evalError = 0;
  fb = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }
  iterTotal = 2;

  n = 100;
  prev = 0.0;
  curr = 0.0;
  while (n <= 10000) {
    h = (b - a) / n;
    sumOdd = 0.0;
    sumEven = 0.0;
    for (i = 1; i < n; i++) {
      x = a + i * h;
      _evalX = x;
      _evalError = 0;
      y = evalRPN(_rpnToken, _rpnLen);
      if (_evalError !== 0) { _calcError = 1; return NaN; }
      iterTotal++;
      if (i % 2 === 1) { sumOdd += y; }
      else { sumEven += y; }
    }
    curr = (h / 3) * (fa + 4 * sumOdd + 2 * sumEven + fb);
    if (!isFinite(curr)) { _calcError = 1; return NaN; }

    if (n > 100) {
      diff = curr - prev;
      if (diff < 0) { diff = -diff; }
      if (diff < 1e-8) { break; }
    }
    prev = curr;
    n = n * 2;
  }

  _calcIterations = iterTotal;
  _calcResult = curr;
  _calcError = 0;
  return _calcResult;
}

/* 模块五：极限（左右逼近）
 * side: -1 左极限 / +1 右极限 / 0 双侧（左右都收敛且相差 < 1e-6 才返回均值）
 *
 * 单侧逼近：ε 从 1e-1 逐级缩到 1e-8（8 个采样点），相邻两级函数值差 < 1e-6 判收敛。
 * 注：收敛阈值取 1e-6 而非 1e-8 —— 一阶逼近型极限（如 (x²−1)/(x−1)→2，
 * x=1±ε 时值 ≈ 2±ε）相邻采样差 ≈ 9·10⁻ᵏ，6 步方案（ε≥1e-6）下最小差 9e-6，
 * 1e-8 阈值恒不收敛；ε 再往下压时 (x²−1) 的消去误差 ~1e-16/ε 又会淹没信号。
 * 8 步 + 1e-6 下 sin(x)/x 类仍达 ~1e-9 精度，(x²−1)/(x−1) 稳定收敛。 */
function calcLimit(a, side) {
  if (!isFinite(a)) { _calcError = 1; return NaN; }
  if (calcCheckFunction() !== 0) { _calcError = 1; return NaN; }
  if (calcPrepareRPN() !== 0) { _calcError = 4; return NaN; }

  if (side === 0) {
    var limL = _calcLimitOneSide(a, -1);
    if (_calcError !== 0) { return NaN; }
    var limR = _calcLimitOneSide(a, +1);
    if (_calcError !== 0) { return NaN; }
    var dl = limL - limR;
    if (dl < 0) { dl = -dl; }
    if (dl > 1e-6) { _calcError = 2; return NaN; }   /* 左右极限不一致 */
    _calcResult = (limL + limR) / 2;
    _calcError = 0;
    return _calcResult;
  }
  return _calcLimitOneSide(a, side);
}

/* 单侧逼近内核（仅供 calcLimit 调用，非递归） */
function _calcLimitOneSide(a, side) {
  var eps = 0.1;
  var prev = NaN;
  var curr = 0.0;
  var x = 0.0;
  var step = 0;
  var iterTotal = 0;

  for (step = 0; step < 8; step++) {
    x = a + side * eps;
    _evalX = x;
    _evalError = 0;
    curr = evalRPN(_rpnToken, _rpnLen);
    if (_evalError !== 0) { _calcError = 1; return NaN; }
    if (curr !== curr) { _calcError = 1; return NaN; }   /* 采样点落出域（如 0^负数经 pow 产生 NaN） */
    iterTotal++;

    if (step > 0 && prev === prev) {
      var d = curr - prev;
      if (d < 0) { d = -d; }
      if (d < 1e-6) {
        _calcIterations = iterTotal;
        _calcResult = curr;
        _calcError = 0;
        return curr;
      }
    }
    prev = curr;
    eps = eps * 0.1;
  }
  _calcIterations = iterTotal;
  _calcError = 2;
  return NaN;
}

/* 模块六：复位
 * UI 层在每次表达式变更后的首个微积分操作前调用（零分配；
 * 泰勒系数属于旧表达式，必须一并失效） */
function calculusReset() {
  _calcResult = 0.0;
  _calcError = 0;
  _calcIterations = 0;
  _calcRpnReady = 0;
  _taylorOrder = 0;
  _taylorCoeffs[0] = 0.0;
}

/* 模块七：泰勒展开
 * f(x) 在 a 点的 n 阶泰勒展开，系数 c_k = f⁽ᵏ⁾(a)/k! 缓存于 _taylorCoeffs：
 *   k=0   直接求值 f(a)
 *   k=1   复用一阶中心差分（calcDerivative，h=1e-5·max(|a|,1)）
 *   k=2   复用二阶五点公式（calcDerivative2，h=1e-4·max(|a|,1)）
 *   k≥3   中心差分表：网格 x_j = a+(j−K)h（j=0..2K，K=阶数），
 *         逐层前向差分（原地覆盖，单缓冲），偶数阶取中心表元素
 *         Δᵏf_{K−k/2}，奇数阶取跨中心两表元素平均——均为 O(h²) 精度；
 *         步长 h = ε^(1/(K+2))·max(|a|,1)（ε=2.22e-16，逐阶舍入/截断折中）
 * 数值边界：k≥10 的高阶导数差分舍入放大（~ε·2ᵏ/hᵏ）不可避免，
 *   高阶系数精度随阶数衰减是数值求导的固有极限，UI 层按需取低阶
 * 返回 0 = 成功；-1 = 参数/域错误（_calcError：1=域错误 3=阶数超限 4=内部错） */
var _taylorCoeffs = new Float64Array(16);   /* 最多 16 阶 */
var _taylorOrder = 0;
var _taylorCenter = 0.0;
var _taylorWork = new Float64Array(256);    /* 2K+1 ≤ 33，用完即弃 */

function calcTaylor(x0, order) {
  _calcError = 0;
  if (!isFinite(x0)) { _calcError = 1; return -1; }
  if (order < 0 || order > 16 || order !== Math.floor(order)) { _calcError = 3; return -1; }
  if (calcCheckFunction() !== 0) { _calcError = 1; return -1; }
  if (calcPrepareRPN() !== 0) { _calcError = 4; return -1; }

  var K = order;
  var k = 0;
  var j = 0;
  var iterTotal = 0;
  var scale = x0 < 0 ? -x0 : x0;
  if (scale < 1.0) { scale = 1.0; }

  _taylorCenter = x0;
  _taylorOrder = K;

  /* k=0：f(x0) */
  _evalX = x0;
  _evalError = 0;
  var f0 = evalRPN(_rpnToken, _rpnLen);
  iterTotal++;
  if (_evalError !== 0 || f0 !== f0) { _calcError = 1; return -1; }
  _taylorCoeffs[0] = f0;
  _calcResult = f0;

  /* k=1,2：复用一阶/二阶专用差分公式（c_k = f⁽ᵏ⁾/k!） */
  if (K >= 1) {
    var d1 = calcDerivative(x0);
    iterTotal = iterTotal + _calcIterations;
    if (d1 !== d1) { return -1; }              /* _calcError 已由 calcDerivative 置位 */
    _taylorCoeffs[1] = d1;
  }
  if (K >= 2) {
    var d2 = calcDerivative2(x0);
    iterTotal = iterTotal + _calcIterations;
    if (d2 !== d2) { return -1; }
    _taylorCoeffs[2] = d2 * 0.5;
  }

  /* k≥3：中心差分表（原地逐层前向差分，取中心/跨中心元素） */
  if (K >= 3) {
    var h = Math.pow(2.22e-16, 1.0 / (K + 2)) * scale;
    for (j = 0; j <= 2 * K; j++) {
      _evalX = x0 + (j - K) * h;
      _evalError = 0;
      var yj = evalRPN(_rpnToken, _rpnLen);
      iterTotal++;
      if (_evalError !== 0 || yj !== yj) { _calcError = 1; return -1; }
      _taylorWork[j] = yj;
    }
    var fact = 2.0;                            /* 进入 k=3 时乘 3 得 3! */
    for (k = 1; k <= K; k++) {
      var t = 0;
      for (t = 0; t <= 2 * K - k; t++) {
        _taylorWork[t] = _taylorWork[t + 1] - _taylorWork[t];
      }
      if (k >= 3) {
        var der = 0.0;
        if (k % 2 === 0) {
          der = _taylorWork[K - k / 2];        /* Δᵏf 的中点恰为 x0 */
        } else {
          var s = K - (k + 1) / 2;             /* 相邻两中点 x0∓h/2，平均回到 x0 */
          der = (_taylorWork[s] + _taylorWork[s + 1]) * 0.5;
        }
        fact = fact * k;
        _taylorCoeffs[k] = der / (Math.pow(h, k) * fact);
      }
    }
  }

  _calcIterations = iterTotal;
  _calcError = 0;
  return 0;
}

/* 用 _taylorCoeffs 在 x 处求泰勒多项式值（Horner 法，纯标量） */
function taylorEval(x) {
  var t = x - _taylorCenter;
  var sum = _taylorCoeffs[_taylorOrder];
  var k = 0;
  for (k = _taylorOrder - 1; k >= 0; k--) {
    sum = sum * t + _taylorCoeffs[k];
  }
  return sum;
}
