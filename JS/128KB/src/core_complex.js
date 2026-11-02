// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · core_complex.js — 极简复数运算层（零分配），供文件 6 方程求解器使用。
 * 依赖文件 1 已声明的全局结果寄存器 _tmpRe / _tmpIm，此处不重复声明。 */

/* 文件 3 专属全局变量（唯一声明处） */
var _aRe = 0.0;
var _aIm = 0.0;
var _bRe = 0.0;
var _bIm = 0.0;

/* 模块一：基础四则运算 */

function cAdd() {
  /* 零分配：仅操作全局标量，无 GC */
  _tmpRe = _aRe + _bRe;
  _tmpIm = _aIm + _bIm;
}

function cSub() {
  /* 零分配：仅操作全局标量，无 GC */
  _tmpRe = _aRe - _bRe;
  _tmpIm = _aIm - _bIm;
}

function cMul() {
  /* 零分配：4 次浮点乘加原地完成，用局部 let 暂存避免读写序污染 */
  /* 零分配：局部标量存放于寄存器/JerryScript 值槽，不进堆 */
  let re = _aRe * _bRe - _aIm * _bIm;
  let im = _aRe * _bIm + _aIm * _bRe;
  _tmpRe = re;
  _tmpIm = im;
}

function cDiv() {
  /* 零分配：分母模平方 + 分子复数乘法展开，无临时对象 */
  let d = _bRe * _bRe + _bIm * _bIm;
  if (d === 0.0) {
    /* 分母为零：整体置 NaN，交由上层判定无效根 */
    _tmpRe = NaN;
    _tmpIm = NaN;
    return;
  }
  let re = (_aRe * _bRe + _aIm * _bIm) / d;
  let im = (_aIm * _bRe - _aRe * _bIm) / d;
  _tmpRe = re;
  _tmpIm = im;
}

/* 模块二：模长、辐角与共轭 */

function cAbs() {
  /* 返回普通 Number，零分配 */
  return Math.sqrt(_aRe * _aRe + _aIm * _aIm);
}

function cArg() {
  /* 返回普通 Number，零分配 */
  return Math.atan2(_aIm, _aRe);
}

function cConj() {
  /* 零分配：仅翻转虚部符号 */
  _tmpRe = _aRe;
  _tmpIm = -_aIm;
}

/* 模块三：复数开方（核心，精度敏感） */

function cSqrt() {
  /* 零分配：仅全局标量与局部 let，Math.* 走原生 C 底层 */
  let re = _aRe;
  let im = _aIm;

  /* 零元情形：0 + 0i 的开方仍为 0 + 0i */
  if (re === 0.0 && im === 0.0) {
    _tmpRe = 0.0;
    _tmpIm = 0.0;
    return;
  }

  /* 纯实数退化情形：虚部为 0 时直接开实部，绕开 atan2(0, x) 的精度损失 */
  if (im === 0.0) {
    if (re >= 0.0) {
      _tmpRe = Math.sqrt(re);
      _tmpIm = 0.0;
    } else {
      /* 负实数：根为纯虚数 ±√|re|·i，约定取正虚部分支 */
      _tmpRe = 0.0;
      _tmpIm = Math.sqrt(-re);
    }
    return;
  }

  /* 极坐标法：√r · (cos(θ/2) + i·sin(θ/2))，另一根取反（求解器按需取反） */
  let r = Math.sqrt(re * re + im * im);
  let theta = Math.atan2(im, re);
  let half = theta * 0.5;
  let sr = Math.sqrt(r);
  _tmpRe = sr * Math.cos(half);
  _tmpIm = sr * Math.sin(half);
}

/* 模块四：复数乘幂（快速幂，平方-乘法） */

function cPowInt(n) {
  /* 零分配：累加器全部为局部 let 标量，平方-乘法无对象产生 */
  let baseRe = _aRe;
  let baseIm = _aIm;
  let exp = n;

  /* 负指数：先取倒数，再按正指数求幂 */
  if (exp < 0) {
    let d = baseRe * baseRe + baseIm * baseIm;
    if (d === 0.0) {
      /* 0 的负次幂无定义：置 NaN 交由上层处理 */
      _tmpRe = NaN;
      _tmpIm = NaN;
      return;
    }
    let invRe = baseRe / d;
    let invIm = -baseIm / d;
    baseRe = invRe;
    baseIm = invIm;
    exp = -exp;
  }

  /* 结果累加器初始化为 1 + 0i */
  let accRe = 1.0;
  let accIm = 0.0;

  /* 平方-乘法：从最低位起逐位消费指数 */
  let e = exp;
  for (;;) {
    if ((e & 1) === 1) {
      /* acc = acc * base（4 次浮点乘加，局部标量暂存） */
      let nRe = accRe * baseRe - accIm * baseIm;
      let nIm = accRe * baseIm + accIm * baseRe;
      accRe = nRe;
      accIm = nIm;
    }
    e = e >> 1;
    if (e === 0) {
      break;
    }
    /* base = base * base（原地平方） */
    let sqRe = baseRe * baseRe - baseIm * baseIm;
    let sqIm = 2.0 * baseRe * baseIm;
    baseRe = sqRe;
    baseIm = sqIm;
  }

  _tmpRe = accRe;
  _tmpIm = accIm;
}

/* 模块五：复数指数 / 对数 / 实数幂（128KB 扩展） */

function cExp() {
  /* 零分配：e^(a+bi) = e^a · (cos b + i sin b)，仅局部标量 */
  var ea = Math.exp(_aRe);
  var re = ea * Math.cos(_aIm);
  var im = ea * Math.sin(_aIm);
  _tmpRe = re;
  _tmpIm = im;
}

function cLn() {
  /* 零分配：主值对数 ln(z) = ln|z| + i·arg(z)
     零元时 ln(0) = -Infinity 交上层判错 */
  var re = _aRe;
  var im = _aIm;
  if (re === 0 && im === 0) {
    /* ln(0) 无定义：返回 -Infinity + 0i，上层据 isFinite 判错 */
    _tmpRe = -Infinity;
    _tmpIm = 0;
    return;
  }
  var lnR = 0.5 * Math.log(re * re + im * im);   /* = ln|z|，避免先开方再取对数 */
  var arg = Math.atan2(im, re);
  _tmpRe = lnR;
  _tmpIm = arg;
}

function cPowReal(w) {
  /* 零分配：z^w = e^(w·ln z)，w 为实数
     内部临时复用 _aRe/_aIm 与 cLn/cExp */
  var re = _aRe;
  var im = _aIm;
  /* z = 0 且 w > 0 → 0；z = 0 且 w <= 0 → 无定义 */
  if (re === 0 && im === 0) {
    if (w > 0) { _tmpRe = 0; _tmpIm = 0; }
    else { _tmpRe = NaN; _tmpIm = NaN; }
    return;
  }
  /* 保存 z，用 _aRe/_aIm 暂存 w·ln z */
  cLn();                         /* _tmpRe = ln|z|, _tmpIm = arg z */
  var lnRe = _tmpRe;
  var lnIm = _tmpIm;
  _aRe = w * lnRe;
  _aIm = w * lnIm;
  cExp();                        /* 结果回 _tmpRe/_tmpIm */
}

/* 模块六：复位辅助 */

function cReset() {
  /* 零分配：六寄存器整体清零，供方程求解器每轮迭代前调用 */
  _aRe = 0.0;
  _aIm = 0.0;
  _bRe = 0.0;
  _bIm = 0.0;
  _tmpRe = 0.0;
  _tmpIm = 0.0;
}

/* ============================================================
 * 模块七：v2.5 F1 复变函数扩展（15 API）
 * 契约 docs/contracts/complex-ext-v25.md §4 公式表逐字镜像（C 侧
 * ac_pro_complex_ext.c 同构）；误差带 §8（1e-9 相对-绝对混合）。
 * 错误码走返回值：0=OK / -1=EDOM（奇点或非有限输入）/ -2=EOVER
 * （计算非有限）/ -3=EPARAM（cnthroot k<=0）。
 * 零 GC：纯标量局部变量 + 模块级工作槽（加载期声明一次），无对象/
 * 数组字面量、无递归、无热路径字符串拼接；仅 cpow 覆写 A 寄存器
 * （契约 §3，沿袭 cPowReal 约定），其余 14 API 不改写 A/B。
 * ============================================================ */

/* 辅助工作槽（加载期一次性声明，热路径零分配） */
var _cxRre = 0.0;
var _cxRim = 0.0;
var _cxGre = 0.0;
var _cxGim = 0.0;

/* H1 cln(p,q)：仅 (p,q) != (0,0) 可调（各 API 调用前已排除，契约 §4） */
function cxLn(p, q) {
  _cxRre = 0.5 * Math.log(p * p + q * q);
  _cxRim = Math.atan2(q, p);
}

/* H2 csqrt(p,q)：符号零敏感；1/q 区分 ±0（与 C 无 signbit 依赖同构） */
function cxSqrt(p, q) {
  var r, t, s;
  if (p === 0.0 && q === 0.0) {
    _cxRre = 0.0;
    _cxRim = q;                     /* 保 ±0：csqrt(±0±i0) = 0 侧共轭成对 */
    return;
  }
  r = Math.sqrt(p * p + q * q);
  if (p >= 0.0) {
    t = Math.sqrt((r + p) / 2.0);
    _cxRre = t;
    _cxRim = q / (2.0 * t);
  } else {
    t = Math.sqrt((r - p) / 2.0);
    s = (1.0 / q < 0.0) ? -t : t;
    _cxRre = Math.abs(q) / (2.0 * t);
    _cxRim = s;
  }
}

/* H3 cexp(p,q) */
function cxExp(p, q) {
  var ea = Math.exp(p);
  _cxRre = ea * Math.cos(q);
  _cxRim = ea * Math.sin(q);
}

/* H4 cdiv(a,b)/(c,d)：den = c²+d²（域上限见契约 §6） */
function cxDiv(ar, ai, br, bi) {
  var den = br * br + bi * bi;
  _cxRre = (ar * br + ai * bi) / den;
  _cxRim = (ai * br - ar * bi) / den;
}

/* casin 静态因子（契约 §4 #7）：casin/casinh 共用，无自调用 */
function cxAsinCore(x, y) {
  var tre = 1.0 - x * x + y * y;
  var tim = -2.0 * x * y;
  var wr, wi, uz, vi, lr, li;
  cxSqrt(tre, tim);
  wr = _cxRre;
  wi = _cxRim;
  uz = -y + wr;
  vi = x + wi;
  cxLn(uz, vi);
  lr = _cxRre;
  li = _cxRim;
  _tmpRe = li;
  _tmpIm = -lr;
}

/* catan 静态因子（契约 §4 #9）：catan/catanh 共用；支点判据在函数内 */
function cxAtanCore(x, y) {
  var num = x * x + (y + 1.0) * (y + 1.0);
  var den = x * x + (y - 1.0) * (y - 1.0);
  if (x === 0.0 && (y === 1.0 || y === -1.0)) {
    return -1;                      /* 支点 ±i → EDOM（契约 §5） */
  }
  _tmpRe = 0.5 * Math.atan2(2.0 * x, 1.0 - x * x - y * y);
  _tmpIm = 0.25 * (Math.log(num) - Math.log(den));
  return (isFinite(_tmpRe) && isFinite(_tmpIm)) ? 0 : -2;
}

/* 输入非有限 → EDOM（契约 §5 首条） */
function cxInputFin(x, y) {
  return isFinite(x) && isFinite(y);
}

/* 结果终检（契约 §6）：TMP 全有限 → 0，否则 -2 */
function cxTmpFin() {
  return (isFinite(_tmpRe) && isFinite(_tmpIm)) ? 0 : -2;
}

function cSin() {
  var x = _aRe, y = _aIm;
  if (!cxInputFin(x, y)) { return -1; }
  _tmpRe = Math.sin(x) * Math.cosh(y);
  _tmpIm = Math.cos(x) * Math.sinh(y);
  return cxTmpFin();
}

function cCos() {
  var x = _aRe, y = _aIm;
  if (!cxInputFin(x, y)) { return -1; }
  _tmpRe = Math.cos(x) * Math.cosh(y);
  _tmpIm = -Math.sin(x) * Math.sinh(y);
  return cxTmpFin();
}

function cTan() {
  /* 契约 §4 #3 混合式（r2 因式分解形），阈值 1.0 冻结，与 C 逐字同构 */
  var x = _aRe, y = _aIm;
  var sr, si, cr, ci, sech, c, den;
  if (!cxInputFin(x, y)) { return -1; }
  if (Math.abs(y) <= 1.0) {
    sr = Math.sin(x) * Math.cosh(y);
    si = Math.cos(x) * Math.sinh(y);
    cr = Math.cos(x) * Math.cosh(y);
    ci = -Math.sin(x) * Math.sinh(y);
    cxDiv(sr, si, cr, ci);
    _tmpRe = _cxRre;
    _tmpIm = _cxRim;
  } else {
    sech = 1.0 / Math.cosh(2.0 * y);
    c = Math.cos(2.0 * x) * sech;
    den = 1.0 + c;
    _tmpRe = Math.sin(2.0 * x) * sech / den;
    _tmpIm = Math.tanh(2.0 * y) / den;
  }
  return cxTmpFin();
}

function cSinh() {
  var x = _aRe, y = _aIm;
  if (!cxInputFin(x, y)) { return -1; }
  _tmpRe = Math.sinh(x) * Math.cos(y);
  _tmpIm = Math.cosh(x) * Math.sin(y);
  return cxTmpFin();
}

function cCosh() {
  var x = _aRe, y = _aIm;
  if (!cxInputFin(x, y)) { return -1; }
  _tmpRe = Math.cosh(x) * Math.cos(y);
  _tmpIm = Math.sinh(x) * Math.sin(y);
  return cxTmpFin();
}

function cTanh() {
  /* 契约 §4 #6：与 ctan 对称的混合式 + cosh(2x) 因式分解（r2） */
  var x = _aRe, y = _aIm;
  var sr, si, cr, ci, sech, c, den;
  if (!cxInputFin(x, y)) { return -1; }
  if (Math.abs(x) <= 1.0) {
    sr = Math.sinh(x) * Math.cos(y);
    si = Math.cosh(x) * Math.sin(y);
    cr = Math.cosh(x) * Math.cos(y);
    ci = Math.sinh(x) * Math.sin(y);
    cxDiv(sr, si, cr, ci);
    _tmpRe = _cxRre;
    _tmpIm = _cxRim;
  } else {
    sech = 1.0 / Math.cosh(2.0 * x);
    c = Math.cos(2.0 * y) * sech;
    den = 1.0 + c;
    _tmpRe = Math.tanh(2.0 * x) / den;
    _tmpIm = Math.sin(2.0 * y) * sech / den;
  }
  return cxTmpFin();
}

function cAsin() {
  var x = _aRe, y = _aIm;
  if (!cxInputFin(x, y)) { return -1; }
  cxAsinCore(x, y);
  return cxTmpFin();
}

function cAcos() {
  var x = _aRe, y = _aIm;
  var ur, ui;
  if (!cxInputFin(x, y)) { return -1; }
  cxAsinCore(x, y);
  ur = _tmpRe;
  ui = _tmpIm;
  _tmpRe = 1.5707963267948966 - ur;
  _tmpIm = -ui;
  return cxTmpFin();
}

function cAtan() {
  var x = _aRe, y = _aIm;
  if (!cxInputFin(x, y)) { return -1; }
  return cxAtanCore(x, y);
}

function cAsinh() {
  /* asinh(z) = −i·asin(iz)，iw = (−y, x) 直取保 ±0（契约 §4 #10） */
  var x = _aRe, y = _aIm;
  var ur, ui;
  if (!cxInputFin(x, y)) { return -1; }
  cxAsinCore(-y, x);
  ur = _tmpRe;
  ui = _tmpIm;
  _tmpRe = ui;
  _tmpIm = -ur;
  return cxTmpFin();
}

function cAcosh() {
  /* acosh(z) = cln(z + csqrt(z−1)·csqrt(z+1))（契约 §4 #11） */
  var x = _aRe, y = _aIm;
  var p, q, m, n, pr, pi, lr, li;
  if (!cxInputFin(x, y)) { return -1; }
  cxSqrt(x - 1.0, y);
  p = _cxRre;
  q = _cxRim;
  cxSqrt(x + 1.0, y);
  m = _cxRre;
  n = _cxRim;
  pr = p * m - q * n;
  pi = p * n + q * m;
  cxLn(x + pr, y + pi);
  lr = _cxRre;
  li = _cxRim;
  _tmpRe = lr;
  _tmpIm = li;
  return cxTmpFin();
}

function cAtanh() {
  /* atanh(z) = −i·atan(iz)；z=±1 支点经 iw=±i 落入 cxAtanCore 判据 */
  var x = _aRe, y = _aIm;
  var rc, ur, ui;
  if (!cxInputFin(x, y)) { return -1; }
  rc = cxAtanCore(-y, x);
  if (rc !== 0) { return rc; }
  ur = _tmpRe;
  ui = _tmpIm;
  _tmpRe = ui;
  _tmpIm = -ur;
  return 0;
}

function cPowC() {
  /* cpow(z,w) = cexp(w·cln z)；A 覆写（契约 §3/§4 #13） */
  var x = _aRe, y = _aIm;
  var u = _bRe, v = _bIm;
  var lr, li;
  if (!cxInputFin(x, y) || !cxInputFin(u, v)) { return -1; }
  if (x === 0.0 && y === 0.0) {
    /* 契约 §5 零底约定：0^0:=1；0^正实:=0；其余 EDOM */
    if (u === 0.0 && v === 0.0) {
      _tmpRe = 1.0;
      _tmpIm = 0.0;
      return 0;
    }
    if (u > 0.0 && v === 0.0) {
      _tmpRe = 0.0;
      _tmpIm = 0.0;
      return 0;
    }
    return -1;
  }
  cxLn(x, y);
  lr = _cxRre;
  li = _cxRim;
  /* A 覆写：A := B·cln(z) */
  _aRe = u * lr - v * li;
  _aIm = u * li + v * lr;
  cxExp(_aRe, _aIm);
  _tmpRe = _cxRre;
  _tmpIm = _cxRim;
  return cxTmpFin();
}

function cNthRoot(k) {
  /* de Moivre 主值根（契约 §4 #14）；k ≤ 0 → EPARAM */
  var x = _aRe, y = _aIm;
  var r, th, base;
  if (k <= 0) { return -3; }
  if (!cxInputFin(x, y)) { return -1; }
  if (x === 0.0 && y === 0.0) {
    _tmpRe = 0.0;
    _tmpIm = 0.0;
    return 0;
  }
  r = Math.sqrt(x * x + y * y);
  th = Math.atan2(y, x) / k;
  base = Math.pow(r, 1.0 / k);
  _tmpRe = base * Math.cos(th);
  _tmpIm = base * Math.sin(th);
  return cxTmpFin();
}

/* cgamma 直算支（契约 §4 #15，x ≥ 0.5），写 _cxGre/_cxGim */
function cxGammaDirect(x, y) {
  var ar = 0.99999999999980993;
  var ai = 0.0;
  var k, dr, lr, li, m1r, m1i, lgr, lgi;
  for (k = 1; k < 9; k++) {
    dr = (x - 1.0) + k;
    cxDiv(CX_LANCZOS_K(k), 0.0, dr, y);
    ar = ar + _cxRre;
    ai = ai + _cxRim;
  }
  cxLn(x + 6.5, y);
  lr = _cxRre;
  li = _cxRim;
  m1r = (x - 0.5) * lr - y * li;
  m1i = (x - 0.5) * li + y * lr;
  lgr = 0.9189385332046727 + m1r - (x + 6.5) + 0.5 * Math.log(ar * ar + ai * ai);
  lgi = m1i - y + Math.atan2(ai, ar);
  cxExp(lgr, lgi);
  _cxGre = _cxRre;
  _cxGim = _cxRim;
}

function cGamma() {
  /* Γ(z)，Lanczos g=7/n=9（与 ac_special.c / core_special.js 同表）+ 反射式；
   * 非正整数极点 → EDOM。反射支经 cxGammaDirect 直算组合，无自调用。 */
  var x = _aRe, y = _aIm;
  var sx, sy, sr, si, gr, gi, dr2, di2;
  if (!cxInputFin(x, y)) { return -1; }
  if (y === 0.0 && x <= 0.0 && x === Math.floor(x)) {
    return -1;                      /* 非正整数极点（契约 §5） */
  }
  if (x >= 0.5) {
    cxGammaDirect(x, y);
    _tmpRe = _cxGre;
    _tmpIm = _cxGim;
  } else {
    sx = 3.141592653589793 * x;
    sy = 3.141592653589793 * y;
    sr = Math.sin(sx) * Math.cosh(sy);
    si = Math.cos(sx) * Math.sinh(sy);
    cxGammaDirect(1.0 - x, -y);     /* 1−z 的虚部取 −y（conj） */
    gr = _cxGre;
    gi = _cxGim;
    dr2 = sr * gr - si * gi;
    di2 = sr * gi + si * gr;
    cxDiv(3.141592653589793, 0.0, dr2, di2);
    _tmpRe = _cxRre;
    _tmpIm = _cxRim;
  }
  return cxTmpFin();
}

/* Lanczos 系数表（契约 §4，与 ac_special.c / core_special.js 同表；
 * 标量 var 加载期声明，同 core_special.js 手工展开先例——索引数组属
 * 热路径字面量禁用，此处以 9 个标量 + switch 展开见上/下） */
var CX_LANCZOS_K_1 = 676.5203681218851;
var CX_LANCZOS_K_2 = -1259.1392167224028;
var CX_LANCZOS_K_3 = 771.32342877765313;
var CX_LANCZOS_K_4 = -176.61502916214059;
var CX_LANCZOS_K_5 = 12.507343278686905;
var CX_LANCZOS_K_6 = -0.13857109526572012;
var CX_LANCZOS_K_7 = 9.9843695780195716e-6;
var CX_LANCZOS_K_8 = 1.5056327351493116e-7;

function CX_LANCZOS_K(k) {
  /* 系数取值（1..8）：标量展开，避免数组字面量（零 GC 纪律） */
  if (k === 1) { return CX_LANCZOS_K_1; }
  if (k === 2) { return CX_LANCZOS_K_2; }
  if (k === 3) { return CX_LANCZOS_K_3; }
  if (k === 4) { return CX_LANCZOS_K_4; }
  if (k === 5) { return CX_LANCZOS_K_5; }
  if (k === 6) { return CX_LANCZOS_K_6; }
  if (k === 7) { return CX_LANCZOS_K_7; }
  return CX_LANCZOS_K_8;
}

/* v2.5 F1 导出命名空间：方法名 = 契约 §4 行名；UI 调用序
 * setComplexA（双操作数再 setComplexB）→ complex.cXxx → getComplexTmp*。
 * 加载期对象字面量一次分配（ApolloSpecial 同款先例），热路径零分配。 */
var ApolloComplexExt = {
  csin: cSin,
  ccos: cCos,
  ctan: cTan,
  csinh: cSinh,
  ccosh: cCosh,
  ctanh: cTanh,
  casin: cAsin,
  cacos: cAcos,
  catan: cAtan,
  casinh: cAsinh,
  cacosh: cAcosh,
  catanh: cAtanh,
  cpow: cPowC,
  cnthroot: cNthRoot,
  cgamma: cGamma
};
