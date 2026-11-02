// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ============================================================
 * ApolloCalc · core_evaluator.js — RPN 求值器（文件 5/6）
 * Lite Wearable / JerryScript / 堆上限 64KB / Zero-GC
 * 职责：Shunting-yard 中缀→后缀（显式栈迭代）、RPN 栈式求值、
 *       方程 f(x)=g(x) → f(x)-g(x)、数值采样法提取多项式系数
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象/数组字面量
 * ============================================================ */

/* 文件 5 专属全局变量（唯一声明处） */
var _rpnLen = 0;
var _rpnToken = new Int32Array(256);      /* 逆波兰 Token 流（与 tokenPool 同编码） */
var _evalX = 0.0;
var _evalError = 0;                       /* 0=OK 1=除零 2=域错误 3=溢出 4=内部错 */
var _evalResult = 0.0;
var _evalSp = 0;

var _opStack = new Int32Array(64);        /* Shunting-yard 运算符栈（存完整 Token word） */
var _opSp = 0;

var _lhsToken = new Int32Array(128);
var _rhsToken = new Int32Array(128);
var _lhsCount = 0;
var _rhsCount = 0;
var _lhsResult = 0.0;
var _rhsResult = 0.0;

var _coeffs = new Float64Array(5);        /* 多项式系数 [常数项..最高次项] */

/* 运算符优先级（整数化 ×2，避开 2.5；+− = 2  ×÷ = 4  一元负号 = 5  ^ = 6）
 * 右结合：^ 与 T_UMINUS；其余左结合
 * 返回 0 表示该 Token 不是可比较优先级的运算符（LP / FUNC 等） */
function opPrec(word) {
  var t = word >> 8;
  var p = word & 255;
  if (t === T_OP) {
    if (p === OP_ADD || p === OP_SUB) { return 2; }
    if (p === OP_MUL || p === OP_DIV) { return 4; }
    return 6;                              /* OP_POW */
  }
  if (t === T_UMINUS) { return 5; }
  return 0;
}

/* 模块一：Shunting-yard 中缀转后缀
 * 读 srcPool[0..srcCount-1]，写 dstPool[0.._rpnLen-1]，返回 _rpnLen
 * 一元负号入栈时无条件压栈：保证 a^-b 不会把 ^ 弹走导致缺右操作数
 * 方程等号必须先由 evalEquationAt 分段后传入 */
function toRPN(srcPool, srcCount, dstPool) {
  _rpnLen = 0;
  _opSp = 0;
  var w = 0;
  var t = 0;
  var p = 0;
  var q = 0;
  var cur = 0;
  var i = 0;

  for (i = 0; i < srcCount; i++) {
    w = srcPool[i];
    t = w >> 8;
    p = w & 255;

    /* 操作数：直接输出 */
    if (t === T_NUM || t === T_VAR || t === T_CONST) {
      if (_rpnLen >= 256) { _evalError = 4; return _rpnLen; }
      dstPool[_rpnLen] = w;
      _rpnLen++;
      continue;
    }

    /* 函数 / 左括号 / 一元负号：无条件压栈 */
    if (t === T_FUNC || t === T_LP || t === T_UMINUS) {
      if (_opSp >= 64) { _evalError = 4; return _rpnLen; }
      _opStack[_opSp] = w;
      _opSp++;
      continue;
    }

    /* 右括号：弹到左括号；括号前若是函数则一并弹出 */
    if (t === T_RP) {
      while (_opSp > 0) {
        _opSp--;
        t = _opStack[_opSp] >> 8;
        if (t === T_LP) { break; }
        if (_rpnLen >= 256) { _evalError = 4; return _rpnLen; }
        dstPool[_rpnLen] = _opStack[_opSp];
        _rpnLen++;
      }
      if (t !== T_LP) { _evalError = 4; return _rpnLen; }   /* 无匹配左括号 */
      if (_opSp > 0 && (_opStack[_opSp - 1] >> 8) === T_FUNC) {
        _opSp--;
        if (_rpnLen >= 256) { _evalError = 4; return _rpnLen; }
        dstPool[_rpnLen] = _opStack[_opSp];
        _rpnLen++;
      }
      continue;
    }

    /* 逗号：弹到左括号（双参函数参数分隔，如 atan2） */
    if (t === T_COMMA) {
      while (_opSp > 0 && (_opStack[_opSp - 1] >> 8) !== T_LP) {
        _opSp--;
        if (_rpnLen >= 256) { _evalError = 4; return _rpnLen; }
        dstPool[_rpnLen] = _opStack[_opSp];
        _rpnLen++;
      }
      if (_opSp === 0) { _evalError = 4; return _rpnLen; }  /* 逗号不在函数括号内 */
      continue;
    }

    /* 二元运算符：按优先级弹栈后压栈 */
    if (t === T_OP) {
      cur = w;
      /* OP_POW 右结合，+−×÷ 左结合 */
      while (_opSp > 0) {
        w = _opStack[_opSp - 1];
        t = w >> 8;
        if (t === T_LP || t === T_FUNC) { break; }          /* 函数/括号屏障 */
        q = opPrec(w);
        if (q > opPrec(cur) || (q === opPrec(cur) && p !== OP_POW)) {
          _opSp--;
          if (_rpnLen >= 256) { _evalError = 4; return _rpnLen; }
          dstPool[_rpnLen] = w;
          _rpnLen++;
        } else {
          break;
        }
      }
      if (_opSp >= 64) { _evalError = 4; return _rpnLen; }
      _opStack[_opSp] = cur;
      _opSp++;
      continue;
    }

    /* 等号不该流到这里：必须先经 evalEquationAt 分段 */
    if (t === T_EQ) { _evalError = 4; return _rpnLen; }
  }

  /* 收尾：清空运算符栈；残余左括号 = 括号不匹配 */
  while (_opSp > 0) {
    _opSp--;
    if ((_opStack[_opSp] >> 8) === T_LP) { _evalError = 4; return _rpnLen; }
    if (_rpnLen >= 256) { _evalError = 4; return _rpnLen; }
    dstPool[_rpnLen] = _opStack[_opSp];
    _rpnLen++;
  }
  return _rpnLen;
}

/* 模块二：RPN 求值器
 * 读 rpnPool[0..rpnLen-1]，用 evalStack + _evalSp 栈式求值
 * 结果写 _evalResult 并返回；任何错误置 _evalError 后立即返回 NaN */
function evalRPN(rpnPool, rpnLen) {
  _evalSp = 0;
  var w = 0;
  var t = 0;
  var p = 0;
  var a = 0.0;
  var b = 0.0;
  var r = 0.0;
  var i = 0;

  for (i = 0; i < rpnLen; i++) {
    w = rpnPool[i];
    t = w >> 8;
    p = w & 255;

    /* 压栈类操作数 */
    if (t === T_NUM) {
      if (_evalSp >= 64) { _evalError = 3; return NaN; }    /* 栈溢出 */
      evalStack[_evalSp] = numberPool[p];
      _evalSp++;
      continue;
    }
    if (t === T_VAR) {
      if (_evalSp >= 64) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = _evalX;
      _evalSp++;
      continue;
    }
    if (t === T_CONST) {
      if (_evalSp >= 64) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = (p === C_PI) ? Math.PI : Math.E;
      _evalSp++;
      continue;
    }

    /* 一元负号 */
    if (t === T_UMINUS) {
      if (_evalSp < 1) { _evalError = 4; return NaN; }      /* 栈下溢 */
      evalStack[_evalSp - 1] = -evalStack[_evalSp - 1];     /* 原地取负，栈深不变 */
      continue;
    }

    /* 双参函数 atan2(y, x)：先弹 x 后弹 y */
    if (t === T_FUNC && p === F_ATAN2) {
      if (_evalSp < 2) { _evalError = 4; return NaN; }
      b = evalStack[--_evalSp];                              /* x */
      a = evalStack[--_evalSp];                              /* y */
      r = applyFunction(F_ATAN2, a, b);
      if (r !== r) { _evalError = 2; return NaN; }           /* 域错误 */
      if (_evalSp >= 64) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = r;
      _evalSp++;
      continue;
    }

    /* 单参函数 */
    if (t === T_FUNC) {
      if (_evalSp < 1) { _evalError = 4; return NaN; }
      a = evalStack[--_evalSp];
      r = applyFunction(p, a, 0);
      if (r !== r && a === a) { _evalError = 2; return NaN; }   /* 域错误，如 sqrt(-1) ln(-1) */
      if (r === Infinity || r === -Infinity) { _evalError = 3; return NaN; }  /* 溢出 */
      if (_evalSp >= 64) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = r;
      _evalSp++;
      continue;
    }

    /* 二元运算符：右操作数先弹 */
    if (t === T_OP) {
      if (_evalSp < 2) { _evalError = 4; return NaN; }
      b = evalStack[--_evalSp];
      a = evalStack[--_evalSp];

      if (p === OP_ADD) { r = a + b; }
      else if (p === OP_SUB) { r = a - b; }
      else if (p === OP_MUL) { r = a * b; }
      else if (p === OP_DIV) {
        if (b === 0) { _evalError = 1; return NaN; }         /* 除零 */
        r = a / b;
      } else {                                               /* OP_POW */
        if (a === 0 && b < 0) { _evalError = 2; return NaN; } /* 0^负数 = 域错误 */
        r = Math.pow(a, b);
      }
      if (r === Infinity || r === -Infinity) { _evalError = 3; return NaN; }  /* 溢出 */
      if (_evalSp >= 64) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = r;
      _evalSp++;
      continue;
    }
  }

  /* 收尾：栈上必须恰好剩一个结果 */
  if (_evalSp !== 1) { _evalError = 4; return NaN; }
  _evalResult = evalStack[0];
  return _evalResult;
}

/* 模块三：求值入口 */

/* 单表达式入口：tokenPool / tokenCount 已由 parseExpression 填充（无等号） */
function evalExpressionAt(x) {
  _evalX = x;
  _evalError = 0;
  _rpnLen = toRPN(tokenPool, tokenCount, _rpnToken);
  if (_evalError !== 0) { _evalResult = NaN; return NaN; }
  return evalRPN(_rpnToken, _rpnLen);
}

/* 方程入口：f(x)=g(x) → f(x)-g(x)；无等号时退化为 evalExpressionAt */
function evalEquationAt(x) {
  _evalX = x;
  _evalError = 0;
  var eqPos = -1;
  var i = 0;

  /* 定位等号（内存策略：扫描在原 tokenPool 上进行，不复制） */
  for (i = 0; i < tokenCount; i++) {
    if ((tokenPool[i] >> 8) === T_EQ) { eqPos = i; break; }
  }

  if (eqPos < 0) { return evalExpressionAt(x); }

  /* 等号两段拷入预分配区（内存策略：零新分配，Int32Array 定长拷贝） */
  _lhsCount = eqPos;
  _rhsCount = tokenCount - eqPos - 1;
  if (_lhsCount === 0 || _rhsCount === 0) { _evalError = 4; _evalResult = NaN; return NaN; }
  for (i = 0; i < _lhsCount; i++) { _lhsToken[i] = tokenPool[i]; }
  for (i = 0; i < _rhsCount; i++) { _rhsToken[i] = tokenPool[eqPos + 1 + i]; }

  _rpnLen = toRPN(_lhsToken, _lhsCount, _rpnToken);
  if (_evalError !== 0) { _evalResult = NaN; return NaN; }
  _lhsResult = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _evalResult = NaN; return NaN; }

  _rpnLen = toRPN(_rhsToken, _rhsCount, _rpnToken);
  if (_evalError !== 0) { _evalResult = NaN; return NaN; }
  _rhsResult = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _evalResult = NaN; return NaN; }

  _evalResult = _lhsResult - _rhsResult;
  return _evalResult;
}

/* 模块四：多项式系数提取（数值采样 + 高斯消元）
 * 在 maxDegree+1 个采样点上求值当前方程 f(x)=0（f(x)=g(x) 已转差），
 * 解范德蒙德方程组得幂基系数，写入 _coeffs[0..maxDegree]（常数项→最高次）
 * 内存策略：增广矩阵复用 matrixPool 前 30 元素（5x6，行主序，步长 maxDegree+2）
 * 返回 0 = 成功；否则返回错误码且 _coeffs 已清零 */
function extractCoefficients(maxDegree) {
  var n = maxDegree + 1;               /* 采样点数 = 方程组元数 */
  var stride = n + 1;                  /* 增广矩阵行宽 */
  var i = 0;
  var j = 0;
  var k = 0;
  var xk = 0.0;
  var pw = 0.0;
  var base = 0;
  var y = 0.0;

  for (i = 0; i < 5; i++) { _coeffs[i] = 0.0; }
  if (maxDegree < 1 || maxDegree > 4) { _evalError = 4; return _evalError; }

  /* 采样并构造增广矩阵：第 k 行 = [1, xk, xk², .., xkⁿ⁻¹ | y_k]
   * 幂用循环累乘（内存策略：仅标量累加器，避免 Math.pow 调用开销） */
  for (k = 0; k < n; k++) {
    xk = k - maxDegree / 2;            /* 小整数/半整数采样点，如 4 次取 −2..2 */
    base = k * stride;
    pw = 1.0;
    for (j = 0; j < n; j++) {
      matrixPool[base + j] = pw;
      pw = pw * xk;
    }
    y = evalEquationAt(xk);
    if (_evalError !== 0) {
      for (i = 0; i < 5; i++) { _coeffs[i] = 0.0; }
      return _evalError;
    }
    matrixPool[base + maxDegree + 1] = y;
  }

  /* 高斯消元（列主元部分选主，显式循环，零递归） */
  var col = 0;
  var row = 0;
  var piv = 0;
  var maxAbs = 0.0;
  var v = 0.0;
  var f = 0.0;
  var c = 0;

  for (col = 0; col < n; col++) {
    piv = col;
    maxAbs = matrixPool[col * stride + col];
    if (maxAbs < 0.0) { maxAbs = -maxAbs; }
    for (row = col + 1; row < n; row++) {
      v = matrixPool[row * stride + col];
      if (v < 0.0) { v = -v; }
      if (v > maxAbs) { maxAbs = v; piv = row; }
    }
    if (maxAbs < 1e-12) {              /* 奇异：方程含非多项式成分或次数超限 */
      _evalError = 4;
      return _evalError;
    }
    if (piv !== col) {                 
      for (c = 0; c < stride; c++) {
        v = matrixPool[col * stride + c];
        matrixPool[col * stride + c] = matrixPool[piv * stride + c];
        matrixPool[piv * stride + c] = v;
      }
    }
    for (row = col + 1; row < n; row++) {
      f = matrixPool[row * stride + col] / matrixPool[col * stride + col];
      if (f !== 0.0) {
        for (c = col; c < stride; c++) {
          matrixPool[row * stride + c] = matrixPool[row * stride + c] - f * matrixPool[col * stride + c];
        }
      }
    }
  }

  /* 回代（内存策略：结果直接落 _coeffs，不建临时数组） */
  var s = 0.0;
  for (i = n - 1; i >= 0; i--) {
    s = matrixPool[i * stride + n];
    for (j = i + 1; j < n; j++) {
      s = s - matrixPool[i * stride + j] * _coeffs[j];
    }
    _coeffs[i] = s / matrixPool[i * stride + i];
  }

  _evalError = 0;
  return 0;
}

/* 模块五：复位 — 求解器每轮求解前调用（只清标量与预分配数组，零分配） */
function evaluatorReset() {
  _rpnLen = 0;
  _evalSp = 0;
  _opSp = 0;
  _evalError = 0;
  _evalX = 0.0;
  _evalResult = 0.0;
  _lhsCount = 0;
  _rhsCount = 0;
  _lhsResult = 0.0;
  _rhsResult = 0.0;
  _coeffs[0] = 0.0;
  _coeffs[1] = 0.0;
  _coeffs[2] = 0.0;
  _coeffs[3] = 0.0;
  _coeffs[4] = 0.0;
}
