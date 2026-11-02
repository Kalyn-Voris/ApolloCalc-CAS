// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · core_complex.js — 极简复数运算层（零分配），供文件 6 方程求解器使用。
 * 依赖文件 1 已声明的全局结果寄存器 _tmpRe / _tmpIm，此处不重复声明 */

/* 文件 3 专属全局变量（唯一声明处） */
var _aRe = 0.0;
var _aIm = 0.0;
var _bRe = 0.0;
var _bIm = 0.0;

/* 模块一：基础四则运算 */

function cAdd() {
  _tmpRe = _aRe + _bRe;
  _tmpIm = _aIm + _bIm;
}

function cSub() {
  _tmpRe = _aRe - _bRe;
  _tmpIm = _aIm - _bIm;
}

function cMul() {
  /* 局部 let 暂存：直接写 _tmpRe 会污染后用的 _aRe/_bRe */
  let re = _aRe * _bRe - _aIm * _bIm;
  let im = _aRe * _bIm + _aIm * _bRe;
  _tmpRe = re;
  _tmpIm = im;
}

function cDiv() {
  let d = _bRe * _bRe + _bIm * _bIm;
  if (d === 0.0) {
    /* 分母为零：置 NaN，由上层判无效根 */
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
  return Math.sqrt(_aRe * _aRe + _aIm * _aIm);
}

function cArg() {
  return Math.atan2(_aIm, _aRe);
}

function cConj() {
  _tmpRe = _aRe;
  _tmpIm = -_aIm;
}

/* 模块三：复数开方（核心，精度敏感） */

function cSqrt() {
  let re = _aRe;
  let im = _aIm;

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
  let baseRe = _aRe;
  let baseIm = _aIm;
  let exp = n;

  /* 负指数：先取倒数转为正指数幂 */
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

  let accRe = 1.0;
  let accIm = 0.0;

  /* 平方-乘法：从最低位起逐位消费指数 */
  let e = exp;
  for (;;) {
    if ((e & 1) === 1) {
      let nRe = accRe * baseRe - accIm * baseIm;
      let nIm = accRe * baseIm + accIm * baseRe;
      accRe = nRe;
      accIm = nIm;
    }
    e = e >> 1;
    if (e === 0) {
      break;
    }
    let sqRe = baseRe * baseRe - baseIm * baseIm;
    let sqIm = 2.0 * baseRe * baseIm;
    baseRe = sqRe;
    baseIm = sqIm;
  }

  _tmpRe = accRe;
  _tmpIm = accIm;
}

/* 模块五：复位辅助 */

function cReset() {
  /* 六寄存器整体清零；求解器每轮迭代前调用 */
  _aRe = 0.0;
  _aIm = 0.0;
  _bRe = 0.0;
  _bIm = 0.0;
  _tmpRe = 0.0;
  _tmpIm = 0.0;
}
