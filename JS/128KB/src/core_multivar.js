// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · core_multivar.js — 二元稀疏多项式（x, y）+ 高阶三角/复数补充。
   依赖 core_complex.js 的 cSqrt/cExp/cLn/cPowInt（拼接序在前，经统一测试加载器与内核拼接通道提供）。
   exps 编码与 C 版 ac_pro_term_t 一致：低 4 bit = y，高 4 bit = x；
   MAX_TOTAL_DEG = 6 时不同单项式正好 N(6) = C(8,2) = 28。

   不变式（_normalizeInPlace 保证）：exps 降序、零系数不存、同 exps 唯一。
   零 GC：6 个 TypedArray 加载期一次分配，之后求值/解析/运算/格式化全路径不再分配。
   槽位：二元运算先算进 _tmpC/_tmpE 再提交 dst，所以 dst 可与 a/b 同槽。
   错误码（getLastError）：0 OK / 1 次数溢出 / 2 语法错 / 3 槽号非法 / 4 项表溢出。

   导出三通道（v2.0.0 内核拼接起生效）：
     Node require（module.exports 赋值）/ 浏览器 <script>（顶层 var 即全局）/
     内核拼接与 ESM（var 进模块作用域，export 块引用 ApolloMultivar）。
     三通道互斥触发：typeof module 守卫在 ESM/浏览器经典脚本下短路。
 */

var ApolloMultivar = (function () {
  "use strict";

  var EXP_BITS = 4;                 /* 每个变量的指数位宽 */
  var EXP_MASK = 15;                /* 0x0F */
  var MAX_EXP = 6;                  /* 单变量指数上限，同 AC_PRO_MV_MAX_DEG */
  var MAX_TOTAL_DEG = 6;
  var MAX_TERMS = 28;               /* N(6) = C(8,2) */
  var SLOTS = 16;
  var STR_BUF_LEN = 1024;
  var TMP_CAP = MAX_TERMS * 2;      /* 加减法暂存可能到 56 项 */

  /* 项池扁平排布，槽 s 的区间是 [s*MAX_TERMS, ...) */
  var _coef = new Float64Array(SLOTS * MAX_TERMS);
  var _exps = new Int32Array(SLOTS * MAX_TERMS);
  var _cnt = new Int32Array(SLOTS);

  var _strBuf = new Int32Array(STR_BUF_LEN);

  var _tmpC = new Float64Array(TMP_CAP);
  var _tmpE = new Int32Array(TMP_CAP);

  /* F0 全池水位账本：模块池加载期自报（docs/contracts/f0-governance-v25.md §3） */
  if (typeof _modulePoolSum === "function") {
    _modulePoolSum(_coef, _exps, _cnt, _strBuf, _tmpC, _tmpE);
  }

  /* 6 次 new 之后不再增长；用普通 number 而非对象，测试可直接校验。 */
  var _allocCount = 6;

  var _opCount = 0;                 /* 热路径调用次数 */
  var _err = 0;                     /* 最近一次操作的错误码 */
  var _strLen = 0;

  function encodeExps(xdeg, ydeg) {
    return ((xdeg & EXP_MASK) << EXP_BITS) | (ydeg & EXP_MASK);
  }

  function decodeX(exps) {
    return (exps >> EXP_BITS) & EXP_MASK;
  }

  function decodeY(exps) {
    return exps & EXP_MASK;
  }

  function _validSlot(s) {
    /* (s|0)===s 顺带挡掉 NaN / 小数 / 字符串 */
    return s >= 0 && s < SLOTS && (s | 0) === s;
  }

  function _isSpace(ch) {
    return ch === 32 || ch === 9 || ch === 10 || ch === 13;
  }

  /* 就地归一化任意 (arrC, arrE, n, base) 视图，槽位与暂存区共用这一份实现。
   * 插入排序而非快排：28 项上限下最坏 378 次比较，且无递归（零 GC 红线）。
   * 返回归一化后的项数。 */
  function _normalizeInPlace(arrC, arrE, n, base) {
    var i, j, kc, ke;

    for (i = 1; i < n; i++) {
      kc = arrC[base + i];
      ke = arrE[base + i];
      j = i - 1;
      while (j >= 0 && arrE[base + j] < ke) {
        arrC[base + j + 1] = arrC[base + j];
        arrE[base + j + 1] = arrE[base + j];
        j--;
      }
      arrC[base + j + 1] = kc;
      arrE[base + j + 1] = ke;
    }

    /* 排序后同 exps 必然相邻 */
    var out = 0;
    i = 0;
    while (i < n) {
      var sum = arrC[base + i];
      var ex = arrE[base + i];
      var k = i + 1;
      while (k < n && arrE[base + k] === ex) {
        sum += arrC[base + k];
        k++;
      }
      if (sum !== 0) {
        arrC[base + out] = sum;
        arrE[base + out] = ex;
        out++;
      }
      i = k;
    }
    return out;
  }

  function _commitTmp(slot, n) {
    var b = slot * MAX_TERMS;
    var i;
    for (i = 0; i < n; i++) {
      _coef[b + i] = _tmpC[i];
      _exps[b + i] = _tmpE[i];
    }
    _cnt[slot] = n;
    return n;
  }

  /* 追加一项，不去重。满 56 项就先归一化腾空间，仍满则 _err = 4。 */
  function _tmpPush(m, c, ex) {
    if (m >= TMP_CAP) {
      m = _normalizeInPlace(_tmpC, _tmpE, m, 0);
      if (m >= TMP_CAP) { _err = 4; return -1; }
    }
    _tmpC[m] = c;
    _tmpE[m] = ex;
    return m + 1;
  }

  function _emitChar(code) {
    if (_strLen < STR_BUF_LEN) { _strBuf[_strLen] = code; _strLen++; }
  }

  /* 无符号整数 → 字符码：逆序写入后原地反转，不借助数组。 */
  function _emitUint(v) {
    if (!(v >= 1)) {                 /* 0 / NaN / 负数兜底 */
      _emitChar(48);                 /* '0' */
      return;
    }
    var start = _strLen;
    while (v >= 1) {
      _emitChar(48 + (v % 10));
      v = Math.floor(v / 10);
    }
    var a = start;
    var b = _strLen - 1;
    while (a < b) {
      var t = _strBuf[a];
      _strBuf[a] = _strBuf[b];
      _strBuf[b] = t;
      a++;
      b--;
    }
  }

  /* double → 字符码。小数取 12 位后裁掉尾零：0.1+0.2 这类二进制噪声
   * 在 1e-12 处被裁掉，输出仍是 "0.3"。 */
  function _emitNumber(v) {
    if (v !== v) {                   /* NaN */
      _emitChar(78); _emitChar(97); _emitChar(78);
      return;
    }
    if (v === Infinity) { _emitChar(73); _emitChar(110); _emitChar(102); return; }
    if (v === -Infinity) { _emitChar(45); _emitChar(73); _emitChar(110); _emitChar(102); return; }

    if (v < 0) { _emitChar(45); v = -v; }
    var ip = Math.floor(v);
    _emitUint(ip);

    var frac = v - ip;
    if (frac > 0) {
      var scaled = Math.round(frac * 1e12);
      if (scaled >= 1e12) { scaled = 1e12 - 1; }   /* 极端进位夹断 */
      var digits = 12;
      while (digits > 0 && scaled % 10 === 0) {
        scaled = scaled / 10;
        digits--;
      }
      if (digits > 0) {
        _emitChar(46);
        var div = 1;
        var t;
        for (t = 1; t < digits; t++) { div = div * 10; }
        while (div >= 1) {
          _emitChar(48 + (Math.floor(scaled / div) % 10));
          div = div / 10;
        }
      }
    }
  }

  function clear(slot) {
    _err = 0;
    if (!_validSlot(slot)) { _err = 3; return -1; }
    _cnt[slot] = 0;
    return 0;
  }

  function normalize(slot) {
    if (!_validSlot(slot)) { _err = 3; return -1; }
    _opCount++;
    var n = _normalizeInPlace(_coef, _exps, _cnt[slot], slot * MAX_TERMS);
    _cnt[slot] = n;
    return n;
  }

  /* 只排序不合并，区别于 normalize。 */
  function sortTerms(slot) {
    _err = 0;
    if (!_validSlot(slot)) { _err = 3; return -1; }
    _opCount++;
    var b = slot * MAX_TERMS;
    var n = _cnt[slot];
    var i, j, kc, ke;
    for (i = 1; i < n; i++) {
      kc = _coef[b + i];
      ke = _exps[b + i];
      j = i - 1;
      while (j >= 0 && _exps[b + j] < ke) {
        _coef[b + j + 1] = _coef[b + j];
        _exps[b + j + 1] = _exps[b + j];
        j--;
      }
      _coef[b + j + 1] = kc;
      _exps[b + j + 1] = ke;
    }
    return n;
  }

  function termCount(slot) {
    if (!_validSlot(slot)) { return -1; }
    return _cnt[slot];
  }

  function getCoef(slot, i) {
    if (!_validSlot(slot) || i < 0 || i >= _cnt[slot]) { return NaN; }
    return _coef[slot * MAX_TERMS + i];
  }

  function getExps(slot, i) {
    if (!_validSlot(slot) || i < 0 || i >= _cnt[slot]) { return -1; }
    return _exps[slot * MAX_TERMS + i];
  }

  function setTerm(slot, i, c, ex) {
    _err = 0;
    if (!_validSlot(slot) || i < 0 || i >= MAX_TERMS) { _err = 3; return -1; }
    var xd = decodeX(ex);
    var yd = decodeY(ex);
    if (xd > MAX_EXP || yd > MAX_EXP || xd + yd > MAX_TOTAL_DEG) { _err = 1; return -1; }
    _coef[slot * MAX_TERMS + i] = c;
    _exps[slot * MAX_TERMS + i] = ex;
    if (i + 1 > _cnt[slot]) { _cnt[slot] = i + 1; }
    return 0;
  }

  /* 零多项式返回 -1，对应数学上的 -∞。 */
  function totalDegree(slot) {
    if (!_validSlot(slot)) { return -1; }
    var b = slot * MAX_TERMS;
    var n = _cnt[slot];
    var d = -1;
    var i;
    for (i = 0; i < n; i++) {
      var e = _exps[b + i];
      var t = decodeX(e) + decodeY(e);
      if (t > d) { d = t; }
    }
    return d;
  }

  function degreeX(slot) {
    if (!_validSlot(slot)) { return -1; }
    var b = slot * MAX_TERMS;
    var n = _cnt[slot];
    var d = -1;
    var i;
    for (i = 0; i < n; i++) {
      var x = decodeX(_exps[b + i]);
      if (x > d) { d = x; }
    }
    return d;
  }

  function degreeY(slot) {
    if (!_validSlot(slot)) { return -1; }
    var b = slot * MAX_TERMS;
    var n = _cnt[slot];
    var d = -1;
    var i;
    for (i = 0; i < n; i++) {
      var y = decodeY(_exps[b + i]);
      if (y > d) { d = y; }
    }
    return d;
  }

  /* src === dst 时是 no-op，不归一化。 */
  function copy(src, dst) {
    _err = 0;
    if (!_validSlot(src) || !_validSlot(dst)) { _err = 3; return -1; }
    var bs = src * MAX_TERMS;
    var bd = dst * MAX_TERMS;
    var n = _cnt[src];
    var i;
    if (src !== dst) {
      for (i = 0; i < n; i++) {
        _coef[bd + i] = _coef[bs + i];
        _exps[bd + i] = _exps[bs + i];
      }
      _cnt[dst] = n;
    }
    return _cnt[dst];
  }

  /* 文法（2 变量、整数指数、十进制系数、隐式乘法）：
   *   poly     := [sign] monomial { ('+'|'-') monomial }
   *   monomial := [number] { '*'? (number | var ['^' uint]) }
   *   var      := 'x' | 'y'
   * 全程线性扫描无递归，写暂存区后统一归一化再提交，
   * 因此解析失败时目标槽位保持为空，不留半成品。
   *
   * 下面几个 _parse* 用模块级变量回传结果（_pcVal / _peVal / _pm*），
   * 是为了不构造返回对象。 */

  var _pcVal = 0;
  function _parseCoeff(s, i) {
    var n = s.length;
    var mant = 0;
    var dec = 0;
    var inFrac = 0;
    var seen = 0;
    while (i < n) {
      var ch = s.charCodeAt(i);
      if (ch >= 48 && ch <= 57) {
        mant = mant * 10 + (ch - 48);
        if (inFrac) { dec++; }
        seen = 1;
        i++;
      } else if (ch === 46 && inFrac === 0) {
        inFrac = 1;
        i++;
      } else {
        break;
      }
    }
    if (!seen) { return -1; }
    if (dec > 0) {
      var p = 1;
      var k;
      for (k = 0; k < dec; k++) { p = p * 10; }
      _pcVal = mant / p;
    } else {
      _pcVal = mant;
    }
    return i;
  }

  var _peVal = 1;
  function _parsePowOpt(s, i) {
    var n = s.length;
    var j = i;
    while (j < n && _isSpace(s.charCodeAt(j))) { j++; }
    if (j >= n || s.charCodeAt(j) !== 94) {   /* 94 = '^' */
      _peVal = 1;
      return i;                                /* 未消费空格，外层会跳过 */
    }
    j++;
    while (j < n && _isSpace(s.charCodeAt(j))) { j++; }
    if (j >= n) { return -1; }
    var d = s.charCodeAt(j);
    if (d < 48 || d > 57) { return -1; }
    var v = 0;
    while (j < n) {
      d = s.charCodeAt(j);
      if (d < 48 || d > 57) { break; }
      v = v * 10 + (d - 48);
      j++;
    }
    _peVal = v;
    return j;
  }

  var _pmCoef = 0;
  var _pmX = 0;
  var _pmY = 0;
  function _parseMonomial(s, i) {
    var n = s.length;
    var c = 1;
    var xd = 0;
    var yd = 0;
    var got = 0;

    /* 前导系数可省：x 等价于 1x */
    if (i < n) {
      var ch = s.charCodeAt(i);
      if ((ch >= 48 && ch <= 57) || ch === 46) {
        i = _parseCoeff(s, i);
        if (i < 0) { _err = 2; return -1; }
        c = _pcVal;
        got = 1;
      }
    }

    for (;;) {
      var j = i;
      while (j < n && _isSpace(s.charCodeAt(j))) { j++; }

      if (j < n && s.charCodeAt(j) === 42) {   /* 42 = '*' */
        j++;
        while (j < n && _isSpace(s.charCodeAt(j))) { j++; }
        if (j >= n) { _err = 2; return -1; }
        var chm = s.charCodeAt(j);
        if ((chm >= 48 && chm <= 57) || chm === 46) {
          /* x*2 这类数字因子，累乘进系数 */
          i = _parseCoeff(s, j);
          if (i < 0) { _err = 2; return -1; }
          c = c * _pcVal;
          got = 1;
          continue;
        }
        i = j;
      } else {
        i = j;
      }

      if (i >= n) { break; }
      var chv = s.charCodeAt(i);
      if (chv !== 120 && chv !== 121) { break; }   /* 120='x', 121='y' */

      i++;
      i = _parsePowOpt(s, i);
      if (i < 0) { _err = 2; return -1; }
      var e = _peVal;
      if (e > MAX_EXP) { _err = 1; return -1; }
      if (chv === 120) { xd += e; } else { yd += e; }
      if (xd > MAX_EXP || yd > MAX_EXP) { _err = 1; return -1; }
      got = 1;
    }

    if (!got) { _err = 2; return -1; }
    if (xd + yd > MAX_TOTAL_DEG) { _err = 1; return -1; }
    _pmCoef = c;
    _pmX = xd;
    _pmY = yd;
    return i;
  }

  function parse(src, slot) {
    _err = 0;
    if (!_validSlot(slot)) { _err = 3; return -1; }
    _cnt[slot] = 0;                              /* 失败时目标槽位保持空 */
    if (typeof src !== "string" || src.length === 0) { _err = 2; return -1; }

    var n = src.length;
    var i = 0;
    var m = 0;
    var first = 1;
    var sawTerm = 0;

    for (;;) {
      while (i < n && _isSpace(src.charCodeAt(i))) { i++; }
      if (i >= n) { break; }

      var sign = 1;
      var ch = src.charCodeAt(i);
      if (ch === 43) {                           /* '+' */
        i++;
      } else if (ch === 45) {                    /* '-' */
        sign = -1;
        i++;
      } else if (!first) {
        _err = 2; return -1;                     /* 缺运算符 */
      }
      while (i < n && _isSpace(src.charCodeAt(i))) { i++; }
      if (i >= n) { _err = 2; return -1; }       /* 悬空符号 */

      i = _parseMonomial(src, i);
      if (i < 0) { return -1; }                  /* _err 已由子解析器置位 */

      m = _tmpPush(m, sign * _pmCoef, encodeExps(_pmX, _pmY));
      if (m < 0) { return -1; }
      sawTerm = 1;
      first = 0;

      var k = i;
      while (k < n && _isSpace(src.charCodeAt(k))) { k++; }
      if (k < n) {
        var chn = src.charCodeAt(k);
        if (chn !== 43 && chn !== 45) { _err = 2; return -1; }
      }
      i = k;
    }

    if (!sawTerm) { _err = 2; return -1; }

    var out = _normalizeInPlace(_tmpC, _tmpE, m, 0);
    if (out > MAX_TERMS) { _err = 4; return -1; }
    return _commitTmp(slot, out);
  }

  function evalSlot(slot, x, y) {
    if (!_validSlot(slot)) { _err = 3; return NaN; }
    _opCount++;
    var b = slot * MAX_TERMS;
    var n = _cnt[slot];
    var sum = 0;
    var i, k;
    for (i = 0; i < n; i++) {
      var c = _coef[b + i];
      if (c === 0) { continue; }                 /* 不变式保证不存在，防御性跳过 */
      var e = _exps[b + i];
      var xd = (e >> EXP_BITS) & EXP_MASK;
      var yd = e & EXP_MASK;
      var px = 1;
      for (k = 0; k < xd; k++) { px = px * x; }
      var py = 1;
      for (k = 0; k < yd; k++) { py = py * y; }
      sum += c * px * py;
    }
    return sum;
  }

  /* 项表拆成 coefs / exps 两个平行数组传入，避免为了一个 terms 对象
   * 而临时构造对象（零 GC 禁止）。 */
  function evalTerms(coefs, exps, n, x, y) {
    _opCount++;
    var sum = 0;
    var i, k;
    for (i = 0; i < n; i++) {
      var c = coefs[i];
      if (c === 0) { continue; }
      var e = exps[i];
      var xd = (e >> EXP_BITS) & EXP_MASK;
      var yd = e & EXP_MASK;
      var px = 1;
      for (k = 0; k < xd; k++) { px = px * x; }
      var py = 1;
      for (k = 0; k < yd; k++) { py = py * y; }
      sum += c * px * py;
    }
    return sum;
  }

  /* 3 参 = 槽位形态，5 参 = 项表形态。按 arguments.length 分派，不构造对象。 */
  function evalPoly(a, b, c, d, e) {
    if (typeof a === "number" && arguments.length === 3) {
      return evalSlot(a, b, c);
    }
    return evalTerms(a, b, c, d, e);
  }

  function _addSub(a, b, dst, bsign) {
    _err = 0;
    if (!_validSlot(a) || !_validSlot(b) || !_validSlot(dst)) { _err = 3; return -1; }
    _opCount++;
    var m = 0;
    var ba = a * MAX_TERMS;
    var bb = b * MAX_TERMS;
    var i;
    var na = _cnt[a];
    var nb = _cnt[b];
    for (i = 0; i < na; i++) { _tmpC[m] = _coef[ba + i]; _tmpE[m] = _exps[ba + i]; m++; }
    for (i = 0; i < nb; i++) { _tmpC[m] = bsign * _coef[bb + i]; _tmpE[m] = _exps[bb + i]; m++; }
    var out = _normalizeInPlace(_tmpC, _tmpE, m, 0);
    if (out > MAX_TERMS) { _err = 4; return -1; }
    return _commitTmp(dst, out);
  }

  function add(a, b, dst) { return _addSub(a, b, dst, 1); }
  function sub(a, b, dst) { return _addSub(a, b, dst, -1); }

  /* 卷积 + 边算边合并，同 C 版 mv_mul 的策略。 */
  function mul(a, b, dst) {
    _err = 0;
    if (!_validSlot(a) || !_validSlot(b) || !_validSlot(dst)) { _err = 3; return -1; }
    _opCount++;
    var ba = a * MAX_TERMS;
    var bb = b * MAX_TERMS;
    var na = _cnt[a];
    var nb = _cnt[b];
    var m = 0;
    var i, j, k;

    for (i = 0; i < na; i++) {
      var ca = _coef[ba + i];
      var ea = _exps[ba + i];
      var ax = (ea >> EXP_BITS) & EXP_MASK;
      var ay = ea & EXP_MASK;
      for (j = 0; j < nb; j++) {
        var cb = _coef[bb + j];
        var eb = _exps[bb + j];
        var nx = ax + ((eb >> EXP_BITS) & EXP_MASK);
        var ny = ay + (eb & EXP_MASK);
        if (nx > MAX_EXP || ny > MAX_EXP || nx + ny > MAX_TOTAL_DEG) {
          _err = 1; return -1;
        }
        var prod = ca * cb;
        var ex = (nx << EXP_BITS) | ny;
        var found = 0;
        for (k = 0; k < m; k++) {
          if (_tmpE[k] === ex) { _tmpC[k] += prod; found = 1; break; }
        }
        if (!found) {
          if (m >= TMP_CAP) { _err = 4; return -1; }
          _tmpC[m] = prod;
          _tmpE[m] = ex;
          m++;
        }
      }
    }

    var out = _normalizeInPlace(_tmpC, _tmpE, m, 0);
    if (out > MAX_TERMS) { _err = 4; return -1; }
    return _commitTmp(dst, out);
  }

  /* dst = a × (c · x^xd · y^yd)，一次遍历，无卷积。 */
  function mulMonomial(a, c, xd, yd, dst) {
    _err = 0;
    if (!_validSlot(a) || !_validSlot(dst)) { _err = 3; return -1; }
    if (xd < 0 || yd < 0 || xd > MAX_EXP || yd > MAX_EXP || xd + yd > MAX_TOTAL_DEG) {
      _err = 1; return -1;
    }
    _opCount++;
    var ba = a * MAX_TERMS;
    var n = _cnt[a];
    var m = 0;
    var i;
    for (i = 0; i < n; i++) {
      var ea = _exps[ba + i];
      var nx = ((ea >> EXP_BITS) & EXP_MASK) + xd;
      var ny = (ea & EXP_MASK) + yd;
      if (nx > MAX_EXP || ny > MAX_EXP || nx + ny > MAX_TOTAL_DEG) {
        _err = 1; return -1;
      }
      m = _tmpPush(m, _coef[ba + i] * c, (nx << EXP_BITS) | ny);
      if (m < 0) { return -1; }
    }
    var out = _normalizeInPlace(_tmpC, _tmpE, m, 0);
    if (out > MAX_TERMS) { _err = 4; return -1; }
    return _commitTmp(dst, out);
  }

  function scale(a, k, dst) {
    _err = 0;
    if (!_validSlot(a) || !_validSlot(dst)) { _err = 3; return -1; }
    _opCount++;
    var ba = a * MAX_TERMS;
    var bd = dst * MAX_TERMS;
    var n = _cnt[a];
    var i;
    var out = 0;
    for (i = 0; i < n; i++) {
      var v = _coef[ba + i] * k;
      if (v !== 0) {
        _coef[bd + out] = v;
        _exps[bd + out] = _exps[ba + i];
        out++;
      }
    }
    _cnt[dst] = out;
    return out;
  }

  function neg(a, dst) { return scale(a, -1, dst); }

  /* 多变量符号求导（v2.0.0，对齐 C 侧 ac_pro_diff_multi）
   * 逐项幂法则：d/dx(c·x^a·y^b) = (c·a)·x^(a-1)·y^b，a === 0 的项整体丢弃（d/dy 同理）。
   * 结果总次数严格下降 → 不会触发项表溢出；系数乘指数 ≤ 6 倍，double 无溢出。
   * 有序性保持：被求导项 exps 同减 1<<位宽（保序保唯一），未求导项全部丢弃，
   * 故结果仍降序且同 exps 唯一，_normalizeInPlace 在此只是防御性过一遍。
   * 槽位纪律同二元运算：先算进 _tmpC/_tmpE 再提交 dst，dst 可与 a 同槽。
   * varIndex：0 = 对 x 求导，1 = 对 y 求导；其他值 _err = 2（语法/参数错类）。
   * 返回 dst 项数（常数的导数 = 0 项 = "0" 多项式），参数/槽位错误返回 -1。 */
  function diff(a, varIndex, dst) {
    _err = 0;
    if (varIndex !== 0 && varIndex !== 1) { _err = 2; return -1; }
    if (!_validSlot(a) || !_validSlot(dst)) { _err = 3; return -1; }
    _opCount++;
    var ba = a * MAX_TERMS;
    var n = _cnt[a];
    var m = 0;
    var shift = varIndex === 0 ? EXP_BITS : 0;
    var i;
    for (i = 0; i < n; i++) {
      var e = _exps[ba + i];
      var d = (e >> shift) & EXP_MASK;
      if (d === 0) { continue; }
      m = _tmpPush(m, _coef[ba + i] * d, e - (1 << shift));
      if (m < 0) { return -1; }
    }
    return _commitTmp(dst, _normalizeInPlace(_tmpC, _tmpE, m, 0));
  }

  /* 写 _strBuf 并返回字符数，不返回字符串。输出格式是外部契约：
   * 降幂、系数 1 省略、项间 " + " / " - "、空多项式输出 "0"。 */
  function toString(slot) {
    _err = 0;
    _strLen = 0;
    if (!_validSlot(slot)) { _err = 3; return 0; }
    _opCount++;
    var b = slot * MAX_TERMS;
    var n = _cnt[slot];
    if (n === 0) { _emitChar(48); return _strLen; }   /* '0' */

    var i;
    for (i = 0; i < n; i++) {
      var c = _coef[b + i];
      var e = _exps[b + i];
      var xd = (e >> EXP_BITS) & EXP_MASK;
      var yd = e & EXP_MASK;
      var isNeg = c < 0;

      if (i === 0) {
        if (isNeg) { _emitChar(45); }
      } else {
        _emitChar(32);
        _emitChar(isNeg ? 45 : 43);
        _emitChar(32);
      }

      var a = isNeg ? -c : c;
      if (a !== 1 || (xd === 0 && yd === 0)) { _emitNumber(a); }
      if (xd > 0) {
        _emitChar(120);
        if (xd > 1) { _emitChar(94); _emitUint(xd); }
      }
      if (yd > 0) {
        _emitChar(121);
        if (yd > 1) { _emitChar(94); _emitUint(yd); }
      }
    }
    return _strLen;
  }

  /* 出口才拼串，UI 层调用一次，不在热路径内。 */
  function getString(len) {
    var n = len;
    if (n === undefined || n < 0 || n > _strLen) { n = _strLen; }
    var s = "";
    var i;
    for (i = 0; i < n; i++) { s = s + String.fromCharCode(_strBuf[i]); }
    return s;
  }

  function polyToString(slot) {
    return getString(toString(slot));
  }

  function charAt(i) {
    if (i < 0 || i >= _strLen) { return -1; }
    return _strBuf[i];
  }

  /* 返回单字符字符串，供测试直观比对；不在热路径内。 */
  function charAtStr(i) {
    if (i < 0 || i >= _strLen) { return ""; }
    return String.fromCharCode(_strBuf[i]);
  }

  function sec(x) { return 1 / Math.cos(x); }
  function csc(x) { return 1 / Math.sin(x); }
  function cot(x) { return 1 / Math.tan(x); }

  function asec(x) {
    if (x > -1 && x < 1) { return NaN; }
    return Math.acos(1 / x);
  }

  function acsc(x) {
    if (x > -1 && x < 1) { return NaN; }
    return Math.asin(1 / x);
  }

  /* 值域 (0, π)。用 atan2(1, x) 一次取到正确分支，
   * 避免 atan(1/x) 在 x<0 时落到 (-π/2, 0) 的错误区间。 */
  function acot(x) { return Math.atan2(1, x); }

  function sech(x) { return 1 / Math.cosh(x); }
  function csch(x) { return 1 / Math.sinh(x); }
  function coth(x) { return 1 / Math.tanh(x); }

  /* 自实现，不依赖 Math.asinh 等新 API。x<0 时借对称性保精度。 */
  function asinh(x) {
    var ax = x < 0 ? -x : x;
    var r = Math.log(ax + Math.sqrt(ax * ax + 1));
    return x < 0 ? -r : r;
  }

  function acosh(x) {
    if (x < 1) { return NaN; }
    return Math.log(x + Math.sqrt(x * x - 1));
  }

  function atanh(x) {
    if (x <= -1 || x >= 1) {
      if (x === 1) { return Infinity; }
      if (x === -1) { return -Infinity; }
      return NaN;
    }
    return 0.5 * Math.log((1 + x) / (1 - x));
  }

  /* 复数域操作数在 _zRe/_zIm、结果在 _rRe/_rIm（零分配，寄存器传递）。
   * 实现委托 core_complex 全局原语；经全局 A 寄存器借用，
   * UI 契约 set-then-call 保证借用不跨调用存活。 */

  var _zRe = 0, _zIm = 0;
  var _rRe = 0, _rIm = 0;

  function setZ(re, im) { _zRe = re; _zIm = im; }
  /* 把结果寄存器搬回操作数寄存器，供链式调用（cExp 后再 cLn）用。 */
  function setZFromResult() { _zRe = _rRe; _zIm = _rIm; }
  function getZRe() { return _zRe; }
  function getZIm() { return _zIm; }
  function getRRe() { return _rRe; }
  function getRIm() { return _rIm; }

  /* 薄委托：_z 装入全局 A 寄存器 → 全局原语 → 结果拷回 _r。
   * cPowInt 保留非整数守卫（全局版 ToInt32 截断语义不可暴露）。 */
  function _mvLoad() { _aRe = _zRe; _aIm = _zIm; }
  function _mvCsqrt() { _mvLoad(); cSqrt(); _rRe = _tmpRe; _rIm = _tmpIm; }
  function _mvCexp() { _mvLoad(); cExp(); _rRe = _tmpRe; _rIm = _tmpIm; }
  function _mvCln() { _mvLoad(); cLn(); _rRe = _tmpRe; _rIm = _tmpIm; }
  function _mvCpowint(n) {
    if (n !== (n | 0)) { _rRe = NaN; _rIm = NaN; return; }
    _mvLoad();
    cPowInt(n);
    _rRe = _tmpRe;
    _rIm = _tmpIm;
  }
  function _mvCsqrtAt(re, im) { _aRe = re; _aIm = im; cSqrt(); }
  function _mvClnAt(re, im) { _aRe = re; _aIm = im; cLn(); }

  function cAsinh() {
    var re = _zRe;
    var im = _zIm;
    var z2re = re * re - im * im;
    var z2im = 2 * re * im;
    _mvCsqrtAt(z2re + 1, z2im);
    var sr = _tmpRe;
    var si = _tmpIm;
    _mvClnAt(re + sr, im + si);
    _rRe = _tmpRe;
    _rIm = _tmpIm;
  }

  function cAcosh() {
    var re = _zRe;
    var im = _zIm;
    var z2re = re * re - im * im;
    var z2im = 2 * re * im;
    _mvCsqrtAt(z2re - 1, z2im);
    var sr = _tmpRe;
    var si = _tmpIm;
    _mvClnAt(re + sr, im + si);
    _rRe = _tmpRe;
    _rIm = _tmpIm;
  }

  /* z=1 是极点，返回 ±Infinity 由上层判错。 */
  function cAtanh() {
    var re = _zRe;
    var im = _zIm;
    var ar = 1 + re;
    var ai = im;
    var br = 1 - re;
    var bi = -im;
    var d = br * br + bi * bi;
    if (d === 0) { _rRe = Infinity; _rIm = 0; return; }
    var qr = (ar * br + ai * bi) / d;
    var qi = (ai * br - ar * bi) / d;
    _mvClnAt(qr, qi);
    _rRe = 0.5 * _tmpRe;
    _rIm = 0.5 * _tmpIm;
  }

  function cAbs() { return Math.sqrt(_zRe * _zRe + _zIm * _zIm); }

  /* 加载后恒为 6，热路径前后不变，测试据此确认零 GC。 */
  function allocCount() { return _allocCount; }

  function stats() { return _allocCount; }

  function getOpCount() { return _opCount; }

  function getLastError() { return _err; }

  /* 返回已有 TypedArray 引用，不分配，供测试确认已预分配。 */
  function coefBuffer() { return _coef; }
  function expsBuffer() { return _exps; }

  /* 不复位 _allocCount，分配确实发生过。 */
  function reset() {
    var i;
    for (i = 0; i < SLOTS; i++) { _cnt[i] = 0; }
    _err = 0;
    _opCount = 0;
    _strLen = 0;
    _zRe = 0; _zIm = 0; _rRe = 0; _rIm = 0;
  }

  return {
    EXP_BITS: EXP_BITS,
    EXP_MASK: EXP_MASK,
    MAX_EXP: MAX_EXP,
    MAX_TOTAL_DEG: MAX_TOTAL_DEG,
    MAX_TERMS: MAX_TERMS,
    SLOTS: SLOTS,

    encodeExps: encodeExps,
    decodeX: decodeX,
    decodeY: decodeY,

    parse: parse,
    evalPoly: evalPoly,
    evalSlot: evalSlot,
    evalTerms: evalTerms,

    clear: clear,
    normalize: normalize,
    sortTerms: sortTerms,
    termCount: termCount,
    totalDegree: totalDegree,
    degreeX: degreeX,
    degreeY: degreeY,
    getCoef: getCoef,
    getExps: getExps,
    setTerm: setTerm,
    copy: copy,

    add: add,
    sub: sub,
    mul: mul,
    mulMonomial: mulMonomial,
    scale: scale,
    neg: neg,
    diff: diff,

    toString: toString,
    getString: getString,
    polyToString: polyToString,
    charAt: charAt,
    charAtStr: charAtStr,

    sec: sec, csc: csc, cot: cot,
    asec: asec, acsc: acsc, acot: acot,
    sech: sech, csch: csch, coth: coth,
    asinh: asinh, acosh: acosh, atanh: atanh,

    setZ: setZ,
    setZFromResult: setZFromResult,
    getZRe: getZRe,
    getZIm: getZIm,
    getRRe: getRRe,
    getRIm: getRIm,
    cSqrt: _mvCsqrt,
    cExp: _mvCexp,
    cLn: _mvCln,
    cPowInt: _mvCpowint,
    cAsinh: cAsinh,
    cAcosh: cAcosh,
    cAtanh: cAtanh,
    cAbs: cAbs,

    allocCount: allocCount,
    stats: stats,
    getOpCount: getOpCount,
    getLastError: getLastError,
    coefBuffer: coefBuffer,
    expsBuffer: expsBuffer,
    reset: reset
  };
})();
