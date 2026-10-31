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
