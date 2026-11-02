// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · core_solvers.js — 方程求解器（文件 6/6，内核最终层）
 * Lite Wearable / JerryScript / 堆上限 64KB / Zero-GC
 * 职责：二次/三次/四次多项式求根、线性方程组高斯消元、残差回验、
 *       OOM 安全防护（深度/函数数/变量数/次数/多元非线性/内存水位）
 * 依赖：文件 1 tokenPool/_coeffs/matrixPool 等全局池
 *       文件 3 cSqrt（双二次路径复数开方）
 *       文件 4 exactFromDiscriminant / exactSetFraction / exactSimplifyFraction
 *       文件 5 evalEquationAt（残差回验）
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象/数组字面量
 *       全部中间量为函数内 var 标量，求解过程不产生任何临时 JS 堆对象 */

/* 文件 6 专属全局变量（唯一声明处） */
var _rootCount = 0;
var _rootRe = new Float64Array(6);        /* 6 槽 = solveLinearSystem 的 n 上限 */
var _rootIm = new Float64Array(6);
var _rootExactNum = new Float64Array(6);
var _rootExactDen = new Float64Array(6);  /* 恒为正 */
var _rootExactRad = new Float64Array(6);  /* 无平方因子 */
var _rootExactValid = new Int32Array(6);  /* 1=支持 0=不支持 */
var _solveError = 0;                      /* 0=OK 1=次数过高 2=多元非线性拒绝 3=求解失败 4=内存水位过高 */
var _residualMax = 0.0;                   /* 残差回验最大相对误差（UI 层决定是否告警） */

/* 精确系统分量上限（2^53-1，与文件 4 保持一致） */
var SOLVER_INT_MAX = 9007199254740991;

/* 内联助手：带符号立方根
 * Math.cbrt 在 JerryScript 上不保证可用，故用 pow(1/3) 手工实现
 * 零分配：纯标量 */
function cbrtSigned(x) {
  if (x === 0.0) { return 0.0; }
  if (x > 0.0) { return Math.pow(x, 1.0 / 3.0); }
  return -Math.pow(-x, 1.0 / 3.0);
}

/* 模块一：二次方程求解器
 * 输入 _coeffs[0..2] = [c, b, a]，即 ax² + bx + c = 0
 * D ≥ 0：两实根，精确形式经 exactFromDiscriminant(D) 组装
 * D < 0：共轭复根，精确形式置无效
 * 退化 a=0：降为线性（单根，整数系数时保留精确分数）
 * 返回 _solveError（0 = 成功） */
function solveQuadratic() {
  _rootCount = 0;
  _solveError = 0;

  var c = _coeffs[0];
  var b = _coeffs[1];
  var a = _coeffs[2];
  if (!isFinite(a) || !isFinite(b) || !isFinite(c)) { _solveError = 3; return _solveError; }

  /* 退化：a = 0 → 线性方程 bx + c = 0 */
  if (a === 0.0) {
    if (b === 0.0) {
      if (c === 0.0) { _solveError = 3; return _solveError; }  /* 恒等式：无穷多根无法枚举 */
      _rootCount = 0;                                          /* 矛盾式：无根，非错误 */
      return 0;
    }
    _rootRe[0] = -c / b;
    _rootIm[0] = 0.0;
    _rootCount = 1;
    /* 内存策略：精确形式仅整数系数可组装，复用文件 4 全局 exactNum/exactDen/exactRad */
    if (b === Math.floor(b) && c === Math.floor(c) &&
        b <= SOLVER_INT_MAX && b >= -SOLVER_INT_MAX &&
        c <= SOLVER_INT_MAX && c >= -SOLVER_INT_MAX) {
      exactSetFraction(-c, b);
    } else {
      exactInvalidate();
    }
    _rootExactValid[0] = exactIsValid ? 1 : 0;
    _rootExactNum[0] = exactNum;
    _rootExactDen[0] = exactDen;
    _rootExactRad[0] = exactRad;
    return 0;
  }

  var D = b * b - 4.0 * a * c;
  if (!isFinite(D)) { _solveError = 3; return _solveError; }
  var twoA = 2.0 * a;

  if (D >= 0.0) {
    /* 两实根：x = (-b ± √D) / 2a */
    var sq = Math.sqrt(D);
    _rootRe[0] = (-b + sq) / twoA;
    _rootRe[1] = (-b - sq) / twoA;
    _rootIm[0] = 0.0;
    _rootIm[1] = 0.0;
    _rootCount = 2;

    /* 精确形式组装
     * √D = (n/d)·√r（d>0，来自 exactFromDiscriminant）
     * 内存策略：r=1（D 为有理数平方）→ 纯分数 (−b·d ± n)/(2a·d)；
     *          b=0 → 纯根式 (±n)/(2a·d)·√r；
     *          其余（有理部分+根式混合，如 (1+√5)/2）三分量模型无法
     *          表达 → 置无效，保留近似值 */
    exactFromDiscriminant(D);
    if (!exactIsValid) {
      _rootExactValid[0] = 0;
      _rootExactValid[1] = 0;
    } else {
      var n = exactNum;
      var d = exactDen;
      var rad = exactRad;
      if (rad === 1) {
        if (a === Math.floor(a) && b === Math.floor(b) &&
            a <= SOLVER_INT_MAX && a >= -SOLVER_INT_MAX &&
            b <= SOLVER_INT_MAX && b >= -SOLVER_INT_MAX &&
            Math.abs(b) <= SOLVER_INT_MAX / d &&
            Math.abs(a) <= SOLVER_INT_MAX / (2.0 * d)) {
          var base = -b * d;                     /* |b·d| ≤ 2^53-1，精确 */
          var den = 2.0 * a * d;                 /* |2a·d| ≤ 2^53-1，精确 */
          var ok0 = Math.abs(base) <= SOLVER_INT_MAX - (n < 0 ? -n : n);
          if (ok0) {
            /* 根 0：(base + n)/den，根 1：(base − n)/den，逐个借全局约分 */
            exactNum = base + n;
            exactDen = den;
            exactRad = 1;
            exactSimplifyFraction();
            exactUpdateApprox();
            _rootExactNum[0] = exactNum;
            _rootExactDen[0] = exactDen;
            _rootExactRad[0] = exactRad;
            _rootExactValid[0] = exactIsValid ? 1 : 0;
            exactNum = base - n;
            exactDen = den;
            exactRad = 1;
            exactSimplifyFraction();
            exactUpdateApprox();
            _rootExactNum[1] = exactNum;
            _rootExactDen[1] = exactDen;
            _rootExactRad[1] = exactRad;
            _rootExactValid[1] = exactIsValid ? 1 : 0;
          } else {
            _rootExactValid[0] = 0;
            _rootExactValid[1] = 0;
          }
        } else {
          _rootExactValid[0] = 0;
          _rootExactValid[1] = 0;
        }
      } else if (b === 0.0) {
        if (a === Math.floor(a) && a <= SOLVER_INT_MAX && a >= -SOLVER_INT_MAX &&
            Math.abs(a) <= SOLVER_INT_MAX / (2.0 * d)) {
          var den2 = 2.0 * a * d;                /* 分母归正：符号翻进分子 */
          var sgn = 1;
          if (den2 < 0.0) { sgn = -1; den2 = -den2; }
          exactNum = sgn * n;
          exactDen = den2;
          exactRad = rad;
          exactSimplifyFraction();
          exactUpdateApprox();
          _rootExactNum[0] = exactNum;
          _rootExactDen[0] = exactDen;
          _rootExactRad[0] = exactRad;
          _rootExactValid[0] = exactIsValid ? 1 : 0;
          _rootExactNum[1] = -exactNum;          /* 共轭：仅分子变号 */
          _rootExactDen[1] = exactDen;
          _rootExactRad[1] = exactRad;
          _rootExactValid[1] = _rootExactValid[0];
        } else {
          _rootExactValid[0] = 0;
          _rootExactValid[1] = 0;
        }
      } else {
        _rootExactValid[0] = 0;                  /* 有理+根式混合：三Fields 模型不可表达 */
        _rootExactValid[1] = 0;
      }
    }
  } else {
    /* 共轭复根：x = (-b ± i√|D|) / 2a */
    var sqI = Math.sqrt(-D);
    var reC = -b / twoA;
    var imC = sqI / twoA;
    _rootRe[0] = reC;
    _rootIm[0] = imC;
    _rootRe[1] = reC;
    _rootIm[1] = -imC;
    _rootCount = 2;
    _rootExactValid[0] = 0;
    _rootExactValid[1] = 0;
  }
  return 0;
}

/* 模块二：三次方程求解器（卡尔达诺 + 三角切换）
 * 输入 _coeffs[0..3] = [d, c, b, a]，即 ax³ + bx² + cx + d = 0
 * 消去二次项：x = y − b/(3a) → y³ + py + q = 0
 * Δ > 0：一实根 + 共轭复对（卡尔达诺，全实标量路径——Δ>0 时 u³/v³ 皆为实数，
 *        不产生复数中间量，cSqrt/cPowInt 在本路径无调用点）
 * Δ = 0：三实根（含重根）
 * Δ < 0：不可约情形，必须切换三角法（卡尔达诺会因复数中间量丢失精度）
 * 三次方程不支持精确形式，_rootExactValid 全 0 */
function solveCubic() {
  _rootCount = 0;
  _solveError = 0;

  var d = _coeffs[0];
  var c = _coeffs[1];
  var b = _coeffs[2];
  var a = _coeffs[3];
  if (!isFinite(a) || !isFinite(b) || !isFinite(c) || !isFinite(d)) { _solveError = 3; return _solveError; }

  /* 退化 a = 0 → 降次为二次（_coeffs[0..2] 恰为二次布局，直接委托） */
  if (a === 0.0) { return solveQuadratic(); }

  var a2 = a * a;
  var a3 = a2 * a;
  var p = (3.0 * a * c - b * b) / (3.0 * a2);
  var q = (2.0 * b * b * b - 9.0 * a * b * c + 27.0 * a2 * d) / (27.0 * a3);
  if (!isFinite(p) || !isFinite(q)) { _solveError = 3; return _solveError; }
  var off = -b / (3.0 * a);                /* x = y + off */

  var qh = q * 0.5;
  var p3 = p / 3.0;
  var p3c = p3 * p3 * p3;
  var dlt = qh * qh + p3c;
  if (!isFinite(dlt)) { _solveError = 3; return _solveError; }

  /* Δ 零判定容差（相对量级），避免浮点抖动把重根误判为三根/一根 */
  var tolD = 1e-14 * (Math.abs(qh * qh) + Math.abs(p3c));

  if (dlt > tolD) {
    /* Δ > 0：卡尔达诺
     * 内存策略：u/v 为带符号实立方根标量，无复数对象 */
    var sD = Math.sqrt(dlt);
    var u = cbrtSigned(-qh + sD);
    var v = cbrtSigned(-qh - sD);
    _rootRe[0] = u + v + off;
    _rootIm[0] = 0.0;
    var re2 = -(u + v) * 0.5 + off;
    var imS = 0.8660254037844386 * (u - v);          /* √3/2 */
    _rootRe[1] = re2;
    _rootIm[1] = imS;
    _rootRe[2] = re2;
    _rootIm[2] = -imS;
    _rootCount = 3;
  } else if (dlt >= -tolD) {
    /* Δ = 0：三实根（y1 = 2∛(−q/2) 单根，y2 = y3 = −∛(−q/2) 二重根） */
    var t0 = cbrtSigned(-qh);
    _rootRe[0] = 2.0 * t0 + off;
    _rootRe[1] = -t0 + off;
    _rootRe[2] = -t0 + off;
    _rootIm[0] = 0.0;
    _rootIm[1] = 0.0;
    _rootIm[2] = 0.0;
    _rootCount = 3;
  } else {
    /* Δ < 0 不可约：三角法
     * y_k = 2√(−p/3)·cos(θ − 2πk/3)，θ = acos((3q)/(2p)·√(−3/p))/3
     * 内存策略：纯标量，acos 入参钳位到 [−1,1] 防浮点越界 */
    var mm = 2.0 * Math.sqrt(-p3);
    var arg = (3.0 * q) / (2.0 * p) * Math.sqrt(-3.0 / p);
    if (arg > 1.0) { arg = 1.0; }
    else if (arg < -1.0) { arg = -1.0; }
    var th = Math.acos(arg) / 3.0;
    var TWO_PI_3 = 2.0943951023931953;               /* 2π/3 */
    _rootRe[0] = mm * Math.cos(th) + off;
    _rootRe[1] = mm * Math.cos(th - TWO_PI_3) + off;
    _rootRe[2] = mm * Math.cos(th - 2.0 * TWO_PI_3) + off;
    _rootIm[0] = 0.0;
    _rootIm[1] = 0.0;
    _rootIm[2] = 0.0;
    _rootCount = 3;
  }

  /* 三次方程不支持精确形式 */
  _rootExactValid[0] = 0;
  _rootExactValid[1] = 0;
  _rootExactValid[2] = 0;
  return 0;
}

/* 模块三：四次方程求解器（笛卡尔分解法）
 * 输入 _coeffs[0..4] = [e, d, c, b, a]，即 ax⁴ + bx³ + cx² + dx + e = 0
 * 消去三次项：x = y − b/(4a) → y⁴ + py² + qy + r = 0
 * q ≈ 0 退化：双二次 z² + pz + r = 0（z = y²），逐 z 用 cSqrt 开方
 * 一般情形：预解三次 8m³ + 8pm² + (2p²−8r)m − q² = 0（系数已推导核验：
 *   设 (y²+αy+β)(y²−αy+γ)，比对系数得 β+γ=p+2m、α(γ−β)=q、βγ=r，
 *   消去 βγ 得 2m(p+2m)²−q²=8mr，展开即上式），取最大实根 m（f(0)=−q²≤0
 *   保证存在非负实根），α=√(2m)，β=m+p/2−q/(2α)，γ=m+p/2+q/(2α)，
 *   两个二次因式的 4 个 y 根合成后平移回 x
 * 注意：预解/因式二次复用 _coeffs 传参（原系数已缓存为局部标量），
 *       求解完成后 _coeffs 不再是原四次系数
 * 四次方程不支持精确形式，_rootExactValid 全 0 */
function solveQuartic() {
  _rootCount = 0;
  _solveError = 0;

  var e = _coeffs[0];
  var d = _coeffs[1];
  var c = _coeffs[2];
  var b = _coeffs[3];
  var a = _coeffs[4];
  if (!isFinite(a) || !isFinite(b) || !isFinite(c) || !isFinite(d) || !isFinite(e)) {
    _solveError = 3; return _solveError;
  }

  /* 退化 a = 0 → 降次为三次 */
  if (a === 0.0) { return solveCubic(); }

  var a2 = a * a;
  var a3 = a2 * a;
  var a4 = a2 * a2;
  var b2 = b * b;
  var p = (8.0 * a * c - 3.0 * b2) / (8.0 * a2);
  var q = (b * b2 - 4.0 * a * b * c + 8.0 * a2 * d) / (8.0 * a3);
  var r = (-3.0 * b * b2 * b + 256.0 * a3 * e - 64.0 * a2 * b * d + 16.0 * a * b2 * c) / (256.0 * a4);
  if (!isFinite(p) || !isFinite(q) || !isFinite(r)) { _solveError = 3; return _solveError; }
  var off = -b / (4.0 * a);                /* x = y + off */

  var qTol = 1e-12 * (1.0 + Math.abs(p) + Math.abs(r));
  var i = 0;

  if (Math.abs(q) < qTol) {
    /* 双二次退化路径：z² + pz + r = 0，z = y²
     * 内存策略：z 根先缓存为局部标量再展开写回，避免读写区重叠；
     * 每个 z 用 cSqrt 开方（负 z 自动落到正虚部分支），正负两根就地取反 */
    _coeffs[0] = r;
    _coeffs[1] = p;
    _coeffs[2] = 1.0;
    solveQuadratic();
    if (_solveError !== 0) { return _solveError; }
    var z0Re = _rootRe[0];
    var z0Im = _rootIm[0];
    var z1Re = _rootRe[1];
    var z1Im = _rootIm[1];
    _aRe = z0Re;
    _aIm = z0Im;
    cSqrt();
    _rootRe[0] = _tmpRe;
    _rootIm[0] = _tmpIm;
    _rootRe[1] = -_tmpRe;
    _rootIm[1] = -_tmpIm;
    _aRe = z1Re;
    _aIm = z1Im;
    cSqrt();
    _rootRe[2] = _tmpRe;
    _rootIm[2] = _tmpIm;
    _rootRe[3] = -_tmpRe;
    _rootIm[3] = -_tmpIm;
    _rootCount = 4;
  } else {
    /* 一般路径：预解三次 → 待定系数分解
     * 内存策略：m/α/β/γ 全为标量；两因式根用 _rootRe[0..3] 就地接力
     * （因式1 根暂存 [2..3]，因式2 根落 [0..1]），零临时数组 */
    _coeffs[0] = -q * q / 8.0;
    _coeffs[1] = p * p * 0.25 - r;
    _coeffs[2] = p;
    _coeffs[3] = 1.0;
    solveCubic();
    if (_solveError !== 0) { return _solveError; }

    var m = -1e308;
    var found = 0;
    for (i = 0; i < _rootCount; i++) {
      if (_rootIm[i] === 0.0 && _rootRe[i] > m) { m = _rootRe[i]; found = 1; }
    }
    if (found === 0 || m <= 0.0 || !isFinite(m)) {
      _solveError = 3;
      _rootCount = 0;
      return _solveError;
    }

    var alpha = Math.sqrt(2.0 * m);
    var halfP = m + p * 0.5;
    var beta = halfP - q / (2.0 * alpha);
    var gamma = halfP + q / (2.0 * alpha);

    /* 因式 1：y² + αy + β = 0 → 根暂存 _rootRe[2..3] */
    _coeffs[0] = beta;
    _coeffs[1] = alpha;
    _coeffs[2] = 1.0;
    solveQuadratic();
    if (_solveError !== 0) { return _solveError; }
    _rootRe[2] = _rootRe[0];
    _rootIm[2] = _rootIm[0];
    _rootRe[3] = _rootRe[1];
    _rootIm[3] = _rootIm[1];

    /* 因式 2：y² − αy + γ = 0 → 根落 _rootRe[0..1] */
    _coeffs[0] = gamma;
    _coeffs[1] = -alpha;
    _coeffs[2] = 1.0;
    solveQuadratic();
    if (_solveError !== 0) { return _solveError; }
    _rootCount = 4;
  }

  /* 平移回 x = y + off；四次方程不支持精确形式 */
  for (i = 0; i < 4; i++) {
    _rootRe[i] = _rootRe[i] + off;
    _rootExactValid[i] = 0;
  }
  return 0;
}

/* 模块四：线性方程组求解器（列主元高斯消元）
 * 输入 matrixPool 前 n·(n+1) 个元素：增广矩阵（行主序，1 ≤ n ≤ 6）
 * 列主元选取 → 行交换 → 消元 → 回代，结果写 _rootRe[0..n-1]
 * 主元 |·| < 1e-12 → 奇异，_solveError = 3
 * 内存策略：原地作用于 matrixPool，零复制零分配 */
function solveLinearSystem(n) {
  _rootCount = 0;
  _solveError = 0;
  if (n < 1 || n > 6) { _solveError = 3; return _solveError; }

  var stride = n + 1;
  var col = 0;
  var row = 0;
  var k = 0;
  var piv = 0;
  var maxAbs = 0.0;
  var v = 0.0;
  var f = 0.0;

  for (col = 0; col < n; col++) {
    /* 列主元：当前列 |·| 最大的行换到对角位 */
    piv = col;
    maxAbs = matrixPool[col * stride + col];
    if (maxAbs < 0.0) { maxAbs = -maxAbs; }
    for (row = col + 1; row < n; row++) {
      v = matrixPool[row * stride + col];
      if (v < 0.0) { v = -v; }
      if (v > maxAbs) { maxAbs = v; piv = row; }
    }
    if (maxAbs < 1e-12) {
      _solveError = 3;                       /* 奇异矩阵 */
      _rootCount = 0;
      return _solveError;
    }
    if (piv !== col) {
      for (k = 0; k < stride; k++) {
        v = matrixPool[col * stride + k];
        matrixPool[col * stride + k] = matrixPool[piv * stride + k];
        matrixPool[piv * stride + k] = v;
      }
    }
    for (row = col + 1; row < n; row++) {
      f = matrixPool[row * stride + col] / matrixPool[col * stride + col];
      if (f !== 0.0) {
        for (k = col; k < stride; k++) {
          matrixPool[row * stride + k] = matrixPool[row * stride + k] - f * matrixPool[col * stride + k];
        }
      }
    }
  }

  /* 回代：解直接落 _rootRe，不建临时数组 */
  var s = 0.0;
  for (row = n - 1; row >= 0; row--) {
    s = matrixPool[row * stride + n];
    for (k = row + 1; k < n; k++) {
      s = s - matrixPool[row * stride + k] * _rootRe[k];
    }
    _rootRe[row] = s / matrixPool[row * stride + row];
  }
  for (row = 0; row < n; row++) {
    _rootIm[row] = 0.0;
    _rootExactValid[row] = 0;
  }
  _rootCount = n;
  return 0;
}

/* 模块五：残差回验
 * 对每个实根（_rootIm[i] === 0）代回原方程 f(x)（evalEquationAt，基于 tokenPool）
 * 相对残差 = |f(root)| / max(|f(root−δ)|, |f(root+δ)|, 1e-300)，
 * δ = |root| > 10 时取 0.1·|root| 否则 1 —— 邻域值自然反映系数整体量级，
 * 采样拟合失真（如 sin(x)=0 被拟合成二次）时邻域值 O(1) 而残差 O(1)，比值近 1；
 * 真 根时残差为浮点噪声，比值 ~1e-12
 * 只记录 _residualMax 不拦截，UI 层据 > 1e-6 决定是否提示"结果可能不准确" */
function verifyRoots() {
  _residualMax = 0.0;
  var i = 0;
  var x = 0.0;
  var f0 = 0.0;
  var f1 = 0.0;
  var f2 = 0.0;
  var del = 1.0;
  var scale = 0.0;
  var rel = 0.0;
  var ax = 0.0;

  for (i = 0; i < _rootCount; i++) {
    if (_rootIm[i] !== 0.0) { continue; }
    x = _rootRe[i];
    f0 = evalEquationAt(x);
    if (_evalError !== 0 || !isFinite(f0)) {
      rel = 1.0;                             /* 求值失败：视作不可信 */
    } else {
      ax = x < 0.0 ? -x : x;
      del = ax > 10.0 ? 0.1 * ax : 1.0;
      f1 = evalEquationAt(x - del);
      f2 = evalEquationAt(x + del);
      scale = f1 < 0.0 ? -f1 : f1;
      if (f2 > scale) { scale = f2; }
      else if (-f2 > scale) { scale = -f2; }
      if (scale < 1e-300) { scale = 1e-300; }
      rel = f0 < 0.0 ? -f0 : f0;
      if (rel < 1e-300) { rel = 0.0; }
      else { rel = rel / scale; }
    }
    if (rel > _residualMax) { _residualMax = rel; }
  }
}

/* 模块六：OOM 安全防护与异常拦截
 * 求解前置门禁：全部只读扫描 tokenPool，零分配 */

/* Token 流体检：括号嵌套 ≤ 3、函数调用 ≤ 2、变量出现 ≤ 5
 * 返回 0 = 通过；1 = 拒绝（_solveError = 2） */
function preCheckExpression() {
  _solveError = 0;
  var depth = 0;
  var funcCount = 0;
  var varCount = 0;
  var i = 0;
  var w = 0;
  var t = 0;

  for (i = 0; i < tokenCount; i++) {
    w = tokenPool[i];
    t = w >> 8;
    if (t === T_LP) {
      depth++;
      if (depth > 3) { _solveError = 2; return 1; }
    } else if (t === T_RP) {
      depth--;
    } else if (t === T_FUNC) {
      funcCount++;
      if (funcCount > 2) { _solveError = 2; return 1; }
    } else if (t === T_VAR) {
      varCount++;
      if (varCount > 5) { _solveError = 2; return 1; }
    }
  }
  return 0;
}

/* 次数门禁：degree > 4 拒绝（阿贝尔-鲁菲尼定理，五次及以上无根式解）
 * 返回 0 = 通过；1 = 拒绝（_solveError = 1） */
function guardDegree(degree) {
  if (degree > 4) { _solveError = 1; return 1; }
  return 0;
}

/* 多元非线性门禁：≥ 2 个不同变量且含非线性运算（^ 或函数调用）即拒绝
 * 变量区分按 T_VAR payload（当前解析器仅 x=0，接口为多变量扩展预留）
 * 线性多元组（如 x+y=3）不拦截，交 solveLinearSystem
 * 返回 0 = 通过；1 = 拒绝（_solveError = 2）
 * 内存策略：已见变量 payload 用 6 个标量寄存，不用数组 */
function guardMultivariateNonlinear() {
  _solveError = 0;
  var seen0 = -1;
  var seen1 = -1;
  var seen2 = -1;
  var seen3 = -1;
  var seen4 = -1;
  var seen5 = -1;
  var distinct = 0;
  var nonlinear = 0;
  var i = 0;
  var w = 0;
  var t = 0;
  var p = 0;

  for (i = 0; i < tokenCount; i++) {
    w = tokenPool[i];
    t = w >> 8;
    p = w & 255;
    if (t === T_VAR) {
      if (p !== seen0 && p !== seen1 && p !== seen2 && p !== seen3 && p !== seen4 && p !== seen5) {
        if (seen0 < 0) { seen0 = p; }
        else if (seen1 < 0) { seen1 = p; }
        else if (seen2 < 0) { seen2 = p; }
        else if (seen3 < 0) { seen3 = p; }
        else if (seen4 < 0) { seen4 = p; }
        else if (seen5 < 0) { seen5 = p; }
        distinct++;
      }
    } else if (t === T_OP && p === OP_POW) {
      nonlinear = 1;
    } else if (t === T_FUNC) {
      nonlinear = 1;
    }
  }
  if (distinct >= 2 && nonlinear === 1) { _solveError = 2; return 1; }
  return 0;
}

/* 内存水位门禁：堆使用 > 48KB（64KB × 75%）拒绝求解
 * 优先平台 Heap API（performance.memory.usedJSHeapSize），
 * 不可用时降级为静态池 byteLength + 动态游标估算（含 2KB 运行时冗余）
 * 返回 0 = 通过；1 = 拒绝（_solveError = 4） */
function checkMemoryWatermark() {
  _solveError = 0;
  var used = 0;

  if (typeof performance !== "undefined") {
    if (performance.memory && performance.memory.usedJSHeapSize) {
      used = performance.memory.usedJSHeapSize;
    }
  }
  if (used === 0) {
    /* 降级估算：全部预分配 TypedArray 之 byteLength + 已用游标 + 冗余 */
    used = tokenPool.byteLength + numberPool.byteLength + evalStack.byteLength +
           matrixPool.byteLength +
           _rpnToken.byteLength + _opStack.byteLength +
           _lhsToken.byteLength + _rhsToken.byteLength + _coeffs.byteLength +
           _rootRe.byteLength + _rootIm.byteLength +
           _rootExactNum.byteLength + _rootExactDen.byteLength +
           _rootExactRad.byteLength + _rootExactValid.byteLength +
           (tokenCount << 3) + (numberCount << 3) + 2048;
  }
  if (used > 49152) { _solveError = 4; return 1; }
  return 0;
}

/* 模块七：复位
 * 每轮求解前调用（内存策略：只清标量与预分配数组，零分配） */
function solversReset() {
  _rootCount = 0;
  var i = 0;
  for (i = 0; i < 6; i++) {
    _rootRe[i] = 0.0;
    _rootIm[i] = 0.0;
    _rootExactNum[i] = 0.0;
    _rootExactDen[i] = 1.0;
    _rootExactRad[i] = 1.0;
    _rootExactValid[i] = 0;
  }
  _solveError = 0;
  _residualMax = 0.0;
}
