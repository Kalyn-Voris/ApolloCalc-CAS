// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · core_groebner.js — 结式 + 2 变量 Groebner 基
   (v2.4.0 功能 10,JS 侧;对齐 C 侧 ac_pro_resultant.c A3 + ac_pro_groebner.c GB)

   结式(A3,数值路径镜像)
   Res_y(f, g) 在 x = x0 处:f(x0,y)、g(x0,y) 视为 y 的一元多项式,
   构造 Sylvester 矩阵((df+dg)×(df+dg),df/dg 为 y 次数)取行列式;
   阶数严格 ≤ 12(复用 v2.3.0 功能 2 的 pro 层 12×12 部分主元 det:
   ApolloMatrix.proSetA + proDet——依赖关系即 ROADMAP 功能 10 所指)。
   行为对齐 C:解析失败/不含 y/参数非法 → 0 且 error=2;阶数 >12 → 0
   且 error=1(绝不截断);det 非有限 → 0 且 error=3;成功返回 1
   (结果可为 0——公共根判据,是意义不是错误)。
   词法:2 变量项语法同 gb(系数可省/变量序可换/指数可省);每变量
   指数 ≤15(C mv 层 4-bit 域,超限 → 拒绝),不设总次数上限——与 C
   parse_into 同域(总次数 6 的 preCheck 是 A1/GCD 调用方语义,A3 无)。

   Groebner(GB,M4GB 逐对归约,1:1 移植)
   lex 序(x>y,ep=(ex<<4)|ey 无符号比较)、输入总次数 ≤4、基 8 槽/
   16 项/pair 32 熔断;critical pair 全对重建(跳过首项互素——Buchberger
   第一判据);S-多项式稠密合成(首项精确相消);稠密伪除归约(81 格
   lex 降序单趟,每步提 content);interreduce 替换式不动点(≤16 轮,
   全项快照判稳);余式非零常数 → 基坍缩 {1};输出按首项 lex 降序,
   元素 " | " 分隔——与 C 侧逐字一致(reduced GB 在固定单项式序下唯一)。

   系数域收窄(JS 无 int64,如实成文)
   C 侧系数域 int64 + ac_safe_*;JS 收窄为 Float64 安全整数:
   所有系数加减乘结果 |v| > 2^53−1 即 AC_PRO_GB_EOVER(−6) 熔断,
   绝不返回静默错值。经典用例(C GB-1..GB-12)系数幅值 ≤ 数千,距
   熔断线多个数量级;两侧能力边界因此不完全对等(ROADMAP 功能 10
   评审修订所要求的披露)。

   错误码(= AC_PRO_GB_*,返回值即错误码)
   1 OK / −1 EARG(空指针/语法错/空输入)/ −2 EDEG(输入总次数 >4 或
   中间单项式总次数 >8)/ −3 ETERM(基元素项数 >16)/ −4 ESLOT(基槽
   >8)/ −5 EPAIR(pair >32)/ −6 EOVER(系数溢出)/ −7 EBUF(渲染 512
   字符上限,防御位)/ −8 EINTERNAL(不变式破坏)。失败串 getString()
   恒 ""(sqfree 同款 JS 失败语义)。

   零 GC:18 组 TypedArray 加载期一次分配(allocCount=18);全路径无
   对象/数组字面量、无递归(归一化/归约/interreduce/排序全显式循环)、
   热路径零字符串操作;渲染 charCode 缓冲 + fromCharCode 出口。
   依赖:ApolloMatrix.proSetA/proDet(12×12 det,内核拼接序在其后)。
   导出三通道:Node require / 浏览器 <script> 顶层 var / 内核拼接 ESM。
 */

var ApolloGroebner = (function () {
  "use strict";

  /* 常量(C 同值) */
  var GB_MAX_DEG = 8;            /* 视图内一切多项式总次数上限 */
  var GB_IN_DEG = 4;             /* 输入多项式总次数上限 */
  var GB_DENSE = 81;             /* (8+1)² 稠密格 */
  var GB_BASIS = 8;              /* 基槽位上限 */
  var GB_TERMS = 16;             /* 单个基元素项数上限 */
  var GB_PAIRS = 32;             /* critical pair 队列容量 */
  var GB_RES_MAX_DIM = 12;       /* Sylvester 阶数上限(= matrix pro 层) */
  var GB_RES_TERMS = 28;         /* 结式输入项数上限(C AC_PRO_MV_TERMS) */
  var GB_RES_EXPMAX = 15;        /* 每变量指数上限(C mv 4-bit 域) */
  var GB_GUARD = 512;            /* 主循环轮数防御 */
  var GB_OUT_CAP = 512;          /* 渲染缓冲(字符) */

  /* 错误码(AC_PRO_GB_* 同值) */
  var GB_OK = 1;
  var GB_EARG = -1;
  var GB_EDEG = -2;
  var GB_ETERM = -3;
  var GB_ESLOT = -4;
  var GB_EPAIR = -5;
  var GB_EOVER = -6;
  var GB_EBUF = -7;
  var GB_EINTERNAL = -8;

  var GB_SAFE = 9007199254740991; /* 2^53−1(JS 系数域熔断线,见文件头) */

  /* 池(加载期一次分配;allocCount = 18) */
  var _coef = new Float64Array(GB_BASIS * GB_TERMS);   /* 基系数(槽 s 项 t = [s*16+t]) */
  var _ep = new Int32Array(GB_BASIS * GB_TERMS);       /* 基打包指数 (ex<<4)|ey,降序 */
  var _cnt = new Int32Array(GB_BASIS);                 /* 每槽项数(0 = 空) */
  var _dead = new Int32Array(GB_BASIS);                /* 死槽标记 */
  var _dense = new Float64Array(GB_DENSE);             /* 稠密工作区 81 格 */
  var _cpA = new Int32Array(GB_PAIRS);                 /* pair 队列 a */
  var _cpB = new Int32Array(GB_PAIRS);                 /* pair 队列 b */
  var _snapC = new Float64Array(GB_TERMS);             /* interreduce 快照系数 */
  var _snapE = new Int32Array(GB_TERMS);               /* interreduce 快照指数 */
  var _order = new Int32Array(GB_BASIS);               /* 输出序槽映射 */
  var _outBuf = new Int32Array(GB_OUT_CAP);            /* 渲染缓冲(charCode) */
  var _digits = new Int32Array(24);                    /* 整数位数字垫 */
  var _resF = new Float64Array(GB_RES_MAX_DIM + 1);    /* 结式 f 的 y 系数(升幂) */
  var _resG = new Float64Array(GB_RES_MAX_DIM + 1);    /* 结式 g 的 y 系数(升幂) */
  var _sylv = new Float64Array(GB_RES_MAX_DIM * GB_RES_MAX_DIM);  /* Sylvester 矩阵 */
  var _tCoef = new Float64Array(GB_TERMS);             /* 解析项系数暂存 */
  var _tEp = new Int32Array(GB_TERMS);                 /* 解析项指数暂存 */
  var _resParseC = new Float64Array(GB_RES_TERMS);     /* 结式解析项系数 */

  /* F0 全池水位账本：模块池加载期自报（docs/contracts/f0-governance-v25.md §3） */
  if (typeof _modulePoolSum === "function") {
    _modulePoolSum(_coef, _ep, _cnt, _dead, _dense, _cpA, _cpB, _snapC, _snapE,
                   _order, _outBuf, _digits, _resF, _resG, _sylv, _tCoef, _tEp,
                   _resParseC);
  }

  var _allocCount = 18;
  var _gbN = 0;              /* 已开基槽数 */
  var _cpN = 0;              /* pair 队列长度 */
  var _outLen = 0;
  var _result = "";          /* 基串(成功后有效) */
  var _resDet = 0.0;         /* 结式结果 */
  var _resErr = 0;           /* 结式 error 通道(C g_solve_error 同义:1/2/3) */
  var _resN = 0;             /* 最近一次 buildSylvester 的阶数 */

  function allocCount() { return _allocCount; }
  function getString() { return _result; }

  /* 单项式编码(ep = (ex<<4)|ey) */

  function _exOf(e) { return (e >> 4) & 15; }
  function _eyOf(e) { return e & 15; }
  function _mkEp(ex, ey) { return (ex << 4) | ey; }
  function _idx(ex, ey) { return ex * (GB_MAX_DEG + 1) + ey; }

  /* 安全算术(|结果| > 2^53−1 → EOVER,见文件头) */

  /* 溢出哨兵用 NaN:有限系数的和/差/积永不 NaN,而 −6 等合法中间值会与
   * 错误码 GB_EOVER 撞车(v2.5.0 F2 实测踩坑:伪除中合法的 −6 系数被误判
   * 溢出,GB 误拒 <xy-6, x+y-5> 一类小系数系统)。调用方以 isNaN 判溢出。 */
  function _safeAdd(a, b) {
    var r = a + b;
    if (r > GB_SAFE || r < -GB_SAFE) { return NaN; }
    return r;
  }
  function _safeSub(a, b) {
    var r = a - b;
    if (r > GB_SAFE || r < -GB_SAFE) { return NaN; }
    return r;
  }
  function _safeMul(a, b) {
    var r = a * b;
    if (r > GB_SAFE || r < -GB_SAFE) { return NaN; }
    return r;
  }
  function _igcd(a, b) {
    var x = a;
    var y = b;
    var t = 0;
    while (y !== 0) { t = x % y; x = y; y = t; }
    return x;
  }

  /* 槽位读取 */

  function _lm(s) {
    if (_cnt[s] <= 0) { return -1; }
    return _ep[s * GB_TERMS];
  }
  function _live(s) {
    return s >= 0 && s < GB_BASIS && _dead[s] === 0 && _cnt[s] > 0;
  }

  /* 解析(语法与 A1/A2 多变量层一致,C gb_parse_* 1:1) */

  /* 词法出参 */
  var _ptNeg = 0;
  var _ptC = 0;
  var _ptNx = 0;
  var _ptNy = 0;
  var _ptNext = 0;

  function gbParseTerm(src, i, n) {
    var c = 0;
    var nx = 0;
    var ny = 0;
    var sawCoef = 0;
    var sawX = 0;
    var sawY = 0;
    var neg = 0;
    var cc = 0;
    var v = 0;
    var vIsX = 0;

    while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }
    if (i < n) {
      cc = src.charCodeAt(i);
      if (cc === 45) { neg = 1; i++; }
      else if (cc === 43) { i++; }
    }
    while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }

    while (i < n) {
      cc = src.charCodeAt(i);
      if (cc < 48 || cc > 57) { break; }
      if (c > (GB_SAFE - 9) / 10) { return GB_EOVER; }
      c = c * 10 + (cc - 48);
      sawCoef = 1;
      i++;
    }
    if (sawCoef === 0) { c = 1; }
    while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }
    if (i < n && src.charCodeAt(i) === 42) { i++; }       /* '*' */
    while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }

    for (;;) {
      if (i >= n) { break; }
      cc = src.charCodeAt(i);
      if (cc === 120 || cc === 88) { v = 0; vIsX = 1; }         /* x/X */
      else if (cc === 121 || cc === 89) { v = 1; vIsX = 1; }    /* y/Y */
      else { break; }
      i++;
      var exp = 1;
      if (i < n && src.charCodeAt(i) === 94) {                  /* '^' */
        i++;
        exp = 0;
        while (i < n) {
          cc = src.charCodeAt(i);
          if (cc < 48 || cc > 57) { break; }
          if (exp > 1000) { return GB_EDEG; }                   /* 指数离谱:早退 */
          exp = exp * 10 + (cc - 48);
          i++;
        }
      }
      if (exp > GB_MAX_DEG) { return GB_EDEG; }
      if (v === 0) {
        if (sawX === 1) { return GB_EARG; }
        nx = exp; sawX = 1;
      } else {
        if (sawY === 1) { return GB_EARG; }
        ny = exp; sawY = 1;
      }
      while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }
      if (i < n && src.charCodeAt(i) === 42) { i++; }
      while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }
      vIsX = 0;
    }

    if (nx + ny > GB_IN_DEG) { return GB_EDEG; }
    _ptNeg = neg; _ptC = c; _ptNx = nx; _ptNy = ny; _ptNext = i;
    return GB_OK;
  }

  /* 解析 src 进槽 s 并归一化(排序/合并/去零);返回 1 = 成功,其余 = 错误码 */
  function gbParseInto(src, s) {
    var i = 0;
    var n = src.length;
    var cnt = 0;
    var rc = 0;
    var cc = 0;
    var c = 0;
    var k = 0;
    var kc = 0;
    var ke = 0;
    var j = 0;
    var out = 0;
    var sum = 0;
    var e = 0;

    while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }
    if (i >= n) { return GB_EARG; }                        /* 空输入 */

    for (;;) {
      rc = gbParseTerm(src, i, n);
      if (rc !== GB_OK) { return rc; }
      i = _ptNext;
      if (_ptC !== 0) {
        c = _ptNeg === 1 ? -_ptC : _ptC;
        if (cnt >= GB_TERMS) { return GB_ETERM; }
        _coef[s * GB_TERMS + cnt] = c;
        _ep[s * GB_TERMS + cnt] = _mkEp(_ptNx, _ptNy);
        cnt++;
      }
      while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }
      if (i >= n) { break; }
      cc = src.charCodeAt(i);
      if (cc !== 43 && cc !== 45) { return GB_EARG; }      /* "x^2;" 尾随垃圾 */
      /* 不前进:让 gbParseTerm 处理符号 */
    }
    _cnt[s] = cnt;

    /* 归一化:按 ep 降序插入排序 */
    for (k = 1; k < cnt; k++) {
      kc = _coef[s * GB_TERMS + k];
      ke = _ep[s * GB_TERMS + k];
      j = k - 1;
      while (j >= 0 && _ep[s * GB_TERMS + j] < ke) {
        _coef[s * GB_TERMS + j + 1] = _coef[s * GB_TERMS + j];
        _ep[s * GB_TERMS + j + 1] = _ep[s * GB_TERMS + j];
        j--;
      }
      _coef[s * GB_TERMS + j + 1] = kc;
      _ep[s * GB_TERMS + j + 1] = ke;
    }
    /* 同类项合并(safe add)+ 去零 */
    out = 0;
    k = 0;
    while (k < cnt) {
      sum = _coef[s * GB_TERMS + k];
      e = _ep[s * GB_TERMS + k];
      j = k + 1;
      while (j < cnt && _ep[s * GB_TERMS + j] === e) {
        sum = _safeAdd(sum, _coef[s * GB_TERMS + j]);
        if (isNaN(sum)) { return GB_EOVER; }
        j++;
      }
      if (sum !== 0) {
        _coef[s * GB_TERMS + out] = sum;
        _ep[s * GB_TERMS + out] = e;
        out++;
      }
      k = j;
    }
    _cnt[s] = out;
    return GB_OK;
  }

  /* 稠密工作区 */

  function denseClear() {
    var i = 0;
    for (i = 0; i < GB_DENSE; i++) { _dense[i] = 0; }
  }
  function denseScatter(s) {
    var t = 0;
    var e = 0;
    var ex = 0;
    var ey = 0;
    denseClear();
    for (t = 0; t < _cnt[s]; t++) {
      e = _ep[s * GB_TERMS + t];
      ex = _exOf(e);
      ey = _eyOf(e);
      if (ex + ey > GB_MAX_DEG) { return GB_EINTERNAL; }
      _dense[_idx(ex, ey)] = _coef[s * GB_TERMS + t];
    }
    return GB_OK;
  }
  function denseIsZero() {
    var i = 0;
    for (i = 0; i < GB_DENSE; i++) { if (_dense[i] !== 0) { return 0; } }
    return 1;
  }
  function denseIsConst() {
    var i = 0;
    if (_dense[0] === 0) { return 0; }
    for (i = 1; i < GB_DENSE; i++) { if (_dense[i] !== 0) { return 0; } }
    return 1;
  }
  function denseStrip() {
    var g = 0;
    var i = 0;
    for (i = 0; i < GB_DENSE; i++) {
      if (_dense[i] !== 0) { g = _igcd(g, _dense[i] < 0 ? -_dense[i] : _dense[i]); }
    }
    if (g > 1) {
      for (i = 0; i < GB_DENSE; i++) {
        if (_dense[i] !== 0) { _dense[i] = _dense[i] / g; }
      }
    }
    return GB_OK;
  }

  /* 稠密伪除归约(M4GB 核心,C gb_dense_reduce 1:1) */

  function denseReduce(exclude) {
    var ex = 0;
    var ey = 0;
    var idx = 0;
    var c = 0;
    var s = 0;
    var hit = 0;
    var lme = 0;
    var lx = 0;
    var ly = 0;
    var lcg = 0;
    var rx = 0;
    var ry = 0;
    var t = 0;
    var p = 0;
    var prod = 0;
    var v = 0;

    for (ex = GB_MAX_DEG; ex >= 0; ex--) {
      for (ey = GB_MAX_DEG; ey >= 0; ey--) {
        idx = _idx(ex, ey);
        c = _dense[idx];
        if (c === 0) { continue; }
        hit = 0;
        for (s = 0; s < GB_BASIS && hit === 0; s++) {
          if (s === exclude || !_live(s)) { continue; }
          lme = _lm(s);
          lx = _exOf(lme);
          ly = _eyOf(lme);
          if (ex < lx || ey < ly) { continue; }            /* lt_g ∤ m */
          /* 伪除:整区乘 lc_g,再减 coef(m)·(m/lt_g)·g */
          lcg = _coef[s * GB_TERMS];
          for (p = 0; p < GB_DENSE; p++) {
            if (_dense[p] !== 0) {
              v = _safeMul(_dense[p], lcg);
              if (isNaN(v)) { return GB_EOVER; }
              _dense[p] = v;
            }
          }
          rx = ex - lx;
          ry = ey - ly;
          for (t = 0; t < _cnt[s]; t++) {
            var te = _ep[s * GB_TERMS + t];
            var tx = _exOf(te) + rx;
            var ty = _eyOf(te) + ry;
            if (tx + ty > GB_MAX_DEG) { return GB_EDEG; }
            prod = _safeMul(c, _coef[s * GB_TERMS + t]);
            if (isNaN(prod)) { return GB_EOVER; }
            var cur = _dense[_idx(tx, ty)];
            var sub = _safeSub(cur, prod);
            if (isNaN(sub)) { return GB_EOVER; }
            _dense[_idx(tx, ty)] = sub;
          }
          var rc = denseStrip();
          if (rc !== GB_OK) { return rc; }
          hit = 1;
        }
        /* 不可除:留作余式项 */
      }
    }
    return GB_OK;
  }

  /* 稠密区 → 槽 s(lex 降序收集 + content/符号规范化);1 = 非零,0 = 零,<0 = 错误 */
  function denseStore(s) {
    var ex = 0;
    var ey = 0;
    var n = 0;
    var g = 0;
    var i = 0;
    for (ex = GB_MAX_DEG; ex >= 0; ex--) {
      for (ey = GB_MAX_DEG; ey >= 0; ey--) {
        var c = _dense[_idx(ex, ey)];
        if (c === 0) { continue; }
        if (n >= GB_TERMS) { return GB_ETERM; }
        _coef[s * GB_TERMS + n] = c;
        _ep[s * GB_TERMS + n] = _mkEp(ex, ey);
        n++;
      }
    }
    _cnt[s] = n;
    if (n === 0) { return 0; }
    for (i = 0; i < n; i++) { g = _igcd(g, _coef[s * GB_TERMS + i] < 0 ? -_coef[s * GB_TERMS + i] : _coef[s * GB_TERMS + i]); }
    if (g > 1) {
      for (i = 0; i < n; i++) { _coef[s * GB_TERMS + i] = _coef[s * GB_TERMS + i] / g; }
    }
    if (_coef[s * GB_TERMS] < 0) {
      for (i = 0; i < n; i++) { _coef[s * GB_TERMS + i] = -_coef[s * GB_TERMS + i]; }
    }
    return 1;
  }

  /* S-多项式合成(C gb_spoly 1:1) */

  function spoly(i, j) {
    var li = _lm(i);
    var lj = _lm(j);
    var lx = _exOf(li);
    var ly = _eyOf(li);
    var jx = _exOf(lj);
    var jy = _eyOf(lj);
    var lcx = lx > jx ? lx : jx;
    var lcy = ly > jy ? ly : jy;
    var lci = _coef[i * GB_TERMS];
    var lcj = _coef[j * GB_TERMS];
    var t = 0;
    var tx = 0;
    var ty = 0;
    var prod = 0;
    var cur = 0;
    var sub = 0;
    var sum = 0;

    denseClear();
    for (t = 0; t < _cnt[i]; t++) {
      var tei = _ep[i * GB_TERMS + t];
      tx = _exOf(tei) + (lcx - lx);
      ty = _eyOf(tei) + (lcy - ly);
      if (tx + ty > GB_MAX_DEG) { return GB_EDEG; }
      prod = _safeMul(lcj, _coef[i * GB_TERMS + t]);
      if (isNaN(prod)) { return GB_EOVER; }
      cur = _dense[_idx(tx, ty)];
      sum = _safeAdd(cur, prod);
      if (isNaN(sum)) { return GB_EOVER; }
      _dense[_idx(tx, ty)] = sum;
    }
    for (t = 0; t < _cnt[j]; t++) {
      var tej = _ep[j * GB_TERMS + t];
      tx = _exOf(tej) + (lcx - jx);
      ty = _eyOf(tej) + (lcy - jy);
      if (tx + ty > GB_MAX_DEG) { return GB_EDEG; }
      prod = _safeMul(lci, _coef[j * GB_TERMS + t]);
      if (isNaN(prod)) { return GB_EOVER; }
      cur = _dense[_idx(tx, ty)];
      sub = _safeSub(cur, prod);
      if (isNaN(sub)) { return GB_EOVER; }
      _dense[_idx(tx, ty)] = sub;
    }
    return GB_OK;
  }

  /* interreduce(替换式不动点,C 1:1) */

  function interreduceAll() {
    var round = 0;
    for (round = 0; round < 16; round++) {
      var changed = 0;
      var s = 0;
      for (s = 0; s < GB_BASIS; s++) {
        var snapN = 0;
        var same = 0;
        var t = 0;
        if (!_live(s)) { continue; }
        snapN = _cnt[s];
        for (t = 0; t < snapN; t++) {                    /* 全项快照(不动点判稳) */
          _snapC[t] = _coef[s * GB_TERMS + t];
          _snapE[t] = _ep[s * GB_TERMS + t];
        }
        var rc = denseScatter(s);
        if (rc !== GB_OK) { return rc; }
        rc = denseReduce(s);
        if (rc !== GB_OK) { return rc; }
        rc = denseStore(s);
        if (rc < 0) { return rc; }
        if (rc === 0) {
          _dead[s] = 1;                                  /* 被其余元素吸收:判死 */
          changed = 1;
        } else {
          same = _cnt[s] === snapN ? 1 : 0;
          for (t = 0; same === 1 && t < snapN; t++) {
            if (_coef[s * GB_TERMS + t] !== _snapC[t] || _ep[s * GB_TERMS + t] !== _snapE[t]) {
              same = 0;
            }
          }
          if (same === 0) { changed = 1; }
        }
      }
      if (changed === 0) { return GB_OK; }
    }
    return GB_EINTERNAL;                                 /* 不动点 16 轮未收敛:防御 */
  }

  /* pair 队列重建(跳过首项互素对) */

  function rebuildPairs() {
    var i = 0;
    var j = 0;
    var li = 0;
    var lj = 0;
    _cpN = 0;
    for (i = 0; i < GB_BASIS; i++) {
      if (!_live(i)) { continue; }
      for (j = i + 1; j < GB_BASIS; j++) {
        if (!_live(j)) { continue; }
        li = _lm(i);
        lj = _lm(j);
        /* 互素:min(ex)==0 且 min(ey)==0 */
        if ((_exOf(li) < _exOf(lj) ? _exOf(li) : _exOf(lj)) === 0 &&
            (_eyOf(li) < _eyOf(lj) ? _eyOf(li) : _eyOf(lj)) === 0) {
          continue;
        }
        if (_cpN >= GB_PAIRS) { return GB_EPAIR; }
        _cpA[_cpN] = i;
        _cpB[_cpN] = j;
        _cpN++;
      }
    }
    return GB_OK;
  }

  /* 理想 = 全环:基坍缩 {1} */
  function collapseOne() {
    var s = 0;
    for (s = 0; s < GB_BASIS; s++) {
      _dead[s] = s !== 0 ? 1 : 0;
      _cnt[s] = 0;
    }
    _coef[0] = 1;
    _ep[0] = 0;
    _cnt[0] = 1;
    _gbN = 1;
    _cpN = 0;
  }

  /* Buchberger 主循环(C gb_solve 1:1) */

  function gbSolve() {
    var guard = 0;
    var s = 0;
    var rc = 0;
    var i = 0;
    var j = 0;
    var k = 0;
    var slot = 0;
    var any = 0;

    for (s = 0; s < GB_BASIS; s++) {                     /* 常数生成元短路 */
      if (_live(s) && _cnt[s] === 1 && _ep[s * GB_TERMS] === 0) {
        collapseOne();
        return GB_OK;
      }
    }
    any = 0;
    for (s = 0; s < GB_BASIS; s++) { if (_live(s)) { any = 1; } }
    if (any === 0) { return GB_OK; }                     /* 两输入皆零:空基 */

    rc = interreduceAll();
    if (rc !== GB_OK) { return rc; }
    rc = rebuildPairs();
    if (rc !== GB_OK) { return rc; }

    for (;;) {
      if (_cpN === 0) { break; }
      guard++;
      if (guard > GB_GUARD) { return GB_EINTERNAL; }

      i = _cpA[0];                                       /* FIFO 弹出 */
      j = _cpB[0];
      for (k = 1; k < _cpN; k++) {
        _cpA[k - 1] = _cpA[k];
        _cpB[k - 1] = _cpB[k];
      }
      _cpN--;

      if (!_live(i) || !_live(j)) { continue; }          /* 防御 */

      rc = spoly(i, j);
      if (rc !== GB_OK) { return rc; }
      rc = denseReduce(-1);
      if (rc !== GB_OK) { return rc; }

      if (denseIsZero() === 1) { continue; }
      if (denseIsConst() === 1) { collapseOne(); break; }

      slot = -1;
      for (s = 0; s < GB_BASIS; s++) {
        if (_dead[s] === 1) { slot = s; break; }
      }
      if (slot < 0) {
        if (_gbN >= GB_BASIS) { return GB_ESLOT; }
        slot = _gbN;
        _gbN++;
      }
      _dead[slot] = 0;
      rc = denseStore(slot);
      if (rc !== GB_OK) { return rc; }

      rc = interreduceAll();
      if (rc !== GB_OK) { return rc; }
      rc = rebuildPairs();
      if (rc !== GB_OK) { return rc; }
    }

    return interreduceAll();                             /* 终结不动点 */
  }

  /* 输出序与格式化(C 1:1) */

  function kthSlot(k) {
    var n = 0;
    var i = 0;
    var t = 0;
    for (i = 0; i < GB_BASIS; i++) {
      if (_live(i)) { _order[n] = i; n++; }
    }
    for (i = 1; i < n; i++) {                            /* 按首项 ep 降序 */
      var key = _order[i];
      t = i - 1;
      while (t >= 0 && _lm(_order[t]) < _lm(key)) {
        _order[t + 1] = _order[t];
        t--;
      }
      _order[t + 1] = key;
    }
    if (k < 0 || k >= n) { return -1; }
    return _order[k];
  }

  function pushCC(ch) {
    if (_outLen < GB_OUT_CAP) { _outBuf[_outLen] = ch; }
    _outLen++;
  }
  function pushInt(v) {
    var nd = 0;
    var m = v;
    if (m < 0) { pushCC(45); m = -m; }
    if (m === 0) { _digits[0] = 0; nd = 1; }
    else {
      while (m > 0) {
        _digits[nd] = m % 10;
        nd++;
        m = (m - (m % 10)) / 10;
      }
    }
    while (nd > 0) { nd--; pushCC(48 + _digits[nd]); }
  }

  function formatElem(s) {
    var t = 0;
    for (t = 0; t < _cnt[s]; t++) {
      var c = _coef[s * GB_TERMS + t];
      var e = _ep[s * GB_TERMS + t];
      var ex = _exOf(e);
      var ey = _eyOf(e);
      var neg = c < 0 ? 1 : 0;
      var mag = neg === 1 ? -c : c;
      if (t === 0) {
        if (neg === 1) { pushCC(45); }                   /* '-' */
      } else {
        pushCC(neg === 1 ? 32 : 32);                     /* ' ' */
        pushCC(neg === 1 ? 45 : 43);                     /* '-' / '+' */
        pushCC(32);
      }
      if (ex === 0 && ey === 0) {
        pushInt(mag);
      } else if (mag !== 1) {
        pushInt(mag);
        pushCC(42);                                      /* '*' */
      }
      if (ex > 0) {
        pushCC(120);                                     /* 'x' */
        if (ex > 1) { pushCC(94); pushInt(ex); }         /* '^' */
        if (ey > 0) { pushCC(42); }                      /* '*' */
      }
      if (ey > 0) {
        pushCC(121);                                     /* 'y' */
        if (ey > 1) { pushCC(94); pushInt(ey); }         /* '^' */
      }
    }
    return GB_OK;
  }

  function gbFormat() {
    var cnt = 0;
    var i = 0;
    var k = 0;
    for (i = 0; i < GB_BASIS; i++) { if (_live(i)) { cnt++; } }
    if (cnt === 0) {
      pushCC(48);                                        /* "0"(空基) */
      return GB_OK;
    }
    for (k = 0; k < cnt; k++) {
      var s = kthSlot(k);
      if (s < 0) { return GB_EINTERNAL; }
      if (k > 0) { pushCC(32); pushCC(124); pushCC(32); }  /* " | " */
      var rc = formatElem(s);
      if (rc !== GB_OK) { return rc; }
    }
    return GB_OK;
  }

  /* 对外:Groebner */

  function groebner(fsrc, gsrc) {
    var rc = 0;
    var s = "";
    var i = 0;

    _result = "";
    if (typeof fsrc !== "string" || typeof gsrc !== "string") { return GB_EARG; }

    for (i = 0; i < GB_BASIS; i++) {                     /* 整块清零(C memset 同义) */
      _cnt[i] = 0;
      _dead[i] = 0;
    }
    _cpN = 0;
    _gbN = 0;
    _outLen = 0;

    rc = gbParseInto(fsrc, 0);
    if (rc !== GB_OK) { return rc; }
    rc = gbParseInto(gsrc, 1);
    if (rc !== GB_OK) { return rc; }
    _gbN = 2;                                            /* 槽 0/1 已解析(可空) */

    rc = gbSolve();
    if (rc !== GB_OK) { return rc; }

    _outLen = 0;
    rc = gbFormat();
    if (rc !== GB_OK) { return rc; }
    if (_outLen > GB_OUT_CAP) { return GB_EBUF; }
    for (i = 0; i < _outLen; i++) { s = s + String.fromCharCode(_outBuf[i]); }
    _result = s;
    return GB_OK;
  }

  /* 访问器(输出序 = 首项 lex 降序) */

  function gbCount() {
    var i = 0;
    var n = 0;
    for (i = 0; i < GB_BASIS; i++) { if (_live(i)) { n++; } }
    return n;
  }
  function gbTerms(k) {
    var s = kthSlot(k);
    if (s < 0) { return -1; }
    return _cnt[s];
  }
  function gbCoef(k, t) {
    var s = kthSlot(k);
    if (s < 0 || t < 0 || t >= _cnt[s]) { return 0; }
    return _coef[s * GB_TERMS + t];
  }
  function gbExps(k, t) {
    var s = kthSlot(k);
    if (s < 0 || t < 0 || t >= _cnt[s]) { return -1; }
    return _ep[s * GB_TERMS + t];
  }

  /* 结式(A3 数值路径,C ac_pro_resultant.c 镜像) */

  /* 2 变量项解析(语法同 gb;每变量指数 ≤15,无总次数上限——C parse_into 同域)。
   * 产出 _resParseC/_tEp(系数与 (xd<<4)|yd),返回项数;失败 −1。 */
  function resParse(src) {
    var i = 0;
    var n = src.length;
    var cnt = 0;
    var cc = 0;
    var c = 0;
    var sawCoef = 0;
    var nx = 0;
    var ny = 0;
    var sawX = 0;
    var sawY = 0;
    var neg = 0;
    var exp = 0;
    var v = 0;

    while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }
    if (i >= n) { return -1; }

    for (;;) {
      neg = 0;
      while (i < n) {
        cc = src.charCodeAt(i);
        if (cc === 45) { neg = 1 - neg; i++; }
        else if (cc === 43) { i++; }
        else { break; }
      }
      while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }
      c = 0;
      sawCoef = 0;
      while (i < n) {
        cc = src.charCodeAt(i);
        if (cc < 48 || cc > 57) { break; }
        if (c > (GB_SAFE - 9) / 10) { return -1; }
        c = c * 10 + (cc - 48);
        sawCoef = 1;
        i++;
      }
      if (sawCoef === 0) { c = 1; }
      while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }
      if (i < n && src.charCodeAt(i) === 42) { i++; }
      while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }

      nx = 0; ny = 0; sawX = 0; sawY = 0;
      for (;;) {
        if (i >= n) { break; }
        cc = src.charCodeAt(i);
        if (cc === 120 || cc === 88) { v = 0; }
        else if (cc === 121 || cc === 89) { v = 1; }
        else { break; }
        i++;
        exp = 1;
        if (i < n && src.charCodeAt(i) === 94) {
          i++;
          exp = 0;
          while (i < n) {
            cc = src.charCodeAt(i);
            if (cc < 48 || cc > 57) { break; }
            if (exp > 1000) { return -1; }
            exp = exp * 10 + (cc - 48);
            i++;
          }
        }
        if (exp > GB_RES_EXPMAX) { return -1; }          /* C mv 4-bit 域 */
        if (v === 0) {
          if (sawX === 1) { return -1; }
          nx = exp; sawX = 1;
        } else {
          if (sawY === 1) { return -1; }
          ny = exp; sawY = 1;
        }
        while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }
        if (i < n && src.charCodeAt(i) === 42) { i++; }
        while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }
      }
      /* 纯系数项:mv 层合法,按 (0,0) 常数项收(C mv_parse 同域);
       * 但裸符号(无系数数字又无变量,如尾随 "+")→ 拒 */
      if (sawX === 0 && sawY === 0 && sawCoef === 0) { return -1; }
      if (cnt >= GB_RES_TERMS) { return -1; }
      _resParseC[cnt] = neg === 1 ? -c : c;
      _tEp[cnt] = _mkEp(nx, ny);
      cnt++;

      while (i < n && (src.charCodeAt(i) === 32 || src.charCodeAt(i) === 9)) { i++; }
      if (i >= n) { break; }
      cc = src.charCodeAt(i);
      if (cc !== 43 && cc !== 45) { return -1; }
    }
    return cnt;
  }

  /* 在 x0 处提取 y 系数(升幂;Horner 累乘),返回 y 次数 */
  function resYCoeffsAt(cnt, x0, out) {
    var i = 0;
    var deg = -1;
    var yd = 0;
    var xd = 0;
    var xp = 0;
    var k = 0;
    for (i = 0; i <= GB_RES_MAX_DIM; i++) { out[i] = 0; }
    for (i = 0; i < cnt; i++) {
      yd = _eyOf(_tEp[i]);
      xd = _exOf(_tEp[i]);
      xp = 1.0;
      for (k = 0; k < xd; k++) { xp = xp * x0; }
      out[yd] = out[yd] + _resParseC[i] * xp;
      if (yd > deg) { deg = yd; }
    }
    return deg;
  }

  /* Sylvester 矩阵构造(C res_build_sylvester 1:1):n = df+dg,
   * 前 dg 行 = f 降幂平移,后 df 行 = g 降幂平移。写入 _sylv,返回 n。 */
  function buildSylvesterM(fc, df, gc, dg) {
    var n = df + dg;
    var i = 0;
    var j = 0;
    var k = 0;
    if (df < 0 || dg < 0) { return -1; }
    if (n < 1 || n > GB_RES_MAX_DIM) { return -1; }
    for (i = 0; i < n * n; i++) { _sylv[i] = 0; }
    for (i = 0; i < dg; i++) {
      for (k = 0; k <= df; k++) {
        _sylv[i * n + i + k] = fc[df - k];               /* 降幂 = 升幂反序 */
      }
    }
    for (j = 0; j < df; j++) {
      for (k = 0; k <= dg; k++) {
        _sylv[(dg + j) * n + j + k] = gc[dg - k];
      }
    }
    return n;
  }

  /* 对外:构造 Sylvester 并暴露(测试 A3-2 同款);返回阶数,失败 0 */
  function buildSylvester(fsrc, gsrc, x0) {
    _resErr = 0;
    if (typeof fsrc !== "string" || typeof gsrc !== "string") { _resErr = 2; return 0; }
    if (x0 !== x0 || x0 === Infinity || x0 === -Infinity) { _resErr = 2; return 0; }
    var rc = resRun(fsrc, gsrc, x0, 1);
    if (rc !== 1) { return 0; }
    return _resN;
  }
  function getSylvester(i) {
    if (i < 0 || i >= _resN * _resN) { return NaN; }
    return _sylv[i];
  }

  /* 对外:结式主入口;返回 1 成功(值可 0)/ 0 拒绝 / −1 内部错误 */
  function resultant(fsrc, gsrc, x0) {
    _resErr = 0;
    _resN = 0;
    if (typeof fsrc !== "string" || typeof gsrc !== "string") { _resErr = 2; return 0; }
    if (x0 !== x0 || x0 === Infinity || x0 === -Infinity) { _resErr = 2; return 0; }
    return resRun(fsrc, gsrc, x0, 0);
  }

  function resRun(fsrc, gsrc, x0, buildOnly) {
    var nf = resParse(fsrc);
    if (nf < 0) { _resErr = 2; return 0; }
    /* C 同款纪律:解析完立刻把 y 系数提取到系数槽——随后 g 的解析
     * 会覆写项暂存区(res 视图与 mv 视图共享存储的 JS 对应物) */
    var df = resYCoeffsAtFrom(nf, x0, _resF);
    if (df < 0) { _resErr = 2; return 0; }

    var ng = resParse(gsrc);
    if (ng < 0) { _resErr = 2; return 0; }
    var dg = resYCoeffsAtFrom(ng, x0, _resG);
    if (dg < 0) { _resErr = 2; return 0; }

    var i = 0;

    if (df < 1 || dg < 1) { _resErr = 2; return 0; }     /* 某方不含 y:明确拒绝 */
    if (df + dg > GB_RES_MAX_DIM) { _resErr = 1; return 0; }  /* 阶数 >12:绝不截断 */

    _resN = buildSylvesterM(_resF, df, _resG, dg);
    if (_resN < 1) { _resErr = 2; return 0; }

    var det = 0.0;
    if (buildOnly === 1) { return 1; }                   /* build 模式:只构造 */
    for (i = 0; i < _resN * _resN; i++) {
      if (ApolloMatrix.proSetA(_resN, _resN, i, _sylv[i]) !== 0) { _resErr = 2; return 0; }
    }
    det = ApolloMatrix.proDet();
    if (det !== det || det === Infinity || det === -Infinity) { _resErr = 3; return 0; }
    _resDet = det;
    return 1;
  }

  /* 在 x0 处提取 y 系数(升幂;Horner 累乘),返回 y 次数 */
  function resYCoeffsAtFrom(cnt, x0, out) {
    var i = 0;
    var deg = -1;
    var yd = 0;
    var xd = 0;
    var xp = 0;
    var k = 0;
    for (i = 0; i <= GB_RES_MAX_DIM; i++) { out[i] = 0; }
    for (i = 0; i < cnt; i++) {
      yd = _eyOf(_tEp[i]);
      xd = _exOf(_tEp[i]);
      xp = 1.0;
      for (k = 0; k < xd; k++) { xp = xp * x0; }
      out[yd] = out[yd] + _resParseC[i] * xp;
      if (yd > deg) { deg = yd; }
    }
    return deg;
  }

  function getResultant() { return _resDet; }
  function getResultantError() { return _resErr; }

  /* 复位 */
  function reset() {
    var i = 0;
    for (i = 0; i < GB_BASIS; i++) { _cnt[i] = 0; _dead[i] = 0; }
    _gbN = 0;
    _cpN = 0;
    _result = "";
    _resDet = 0.0;
    _resErr = 0;
    _resN = 0;
    return GB_OK;
  }


  /* ============================================================
   * ApolloCalc · core_groebner.js 扩段 — sysolve 二元方程组 GB 消元数值全解
   * (v2.5.0 F2,JS 侧;契约 docs/contracts/sysolve-format-v25.md 冻结镜像)
   *
   * 管线(C 侧 ac_pro_sysolve.c 互为镜像):
   *   f,g(gb 文法,总次数 ≤ 4)→ 既有 groebner()(lex x>y)→ 经本模块既有
   *   访问器把 y-单变元 h 与混合元素拷入池(copy-out 后 GB 状态即弃)→
   *   h 经 SFF 预处理去重根 → h 求根 1 直接式 / 2..4 求解层精确根式 /
   *   5..8 Durand-Kerner(本段移植,池镜像 C nr 视图 264 B)→ 解耦 GB
   *   交叉积;耦合 GB 逐 y 根代回混合元得复系数一元式,一律复数 DK →
   *   候选流式回验 f,g 残差 ≤1e-9(相对)→ 去重 → 冻结排序 → 渲染。
   *   GB={1} → 成功 0 解 "[]";无 y-单变元 / 某 y 根处全部混合元恒零
   *   → EZDIM。错误码与 C 同值(OK=0,EARG=−1…EINTERNAL=−11,契约 §7)。
   *
   * 零 GC:以下池全部加载期一次分配(_allocCount 计入);回验前的候选
   * 不落池——逐候选生成后立即回验/去重插入(与 C 的先汇集后回验在
   * 结果序与首错序上逐位一致:候选彼此独立,插入序 = 生成序)。
   * 系数域:JS 侧全程 Float64 安全整数,|v| > 2^53−1 → EOVER(契约 §3
   * 成文:C int64 与 JS 2^53 的公共域内零分叉,域外是 C 独有能力)。 */

  var SYS_SAFE = 9007199254740991;   /* 2^53−1(GB_SAFE 同线) */
  var SYS_TERMS = GB_TERMS;          /* 16 */
  var SYS_MIX_MAX = GB_BASIS - 1;    /* 7 */
  var SYS_ROOTS = 8;
  var SYS_SOL_CAP = 16;              /* Bézout 界 deg f·deg g ≤ 16 */

  /* 错误码(与 C AC_PRO_SYS_* 同值) */
  var SYS_OK = 0;
  var SYS_EARG = -1;
  var SYS_EDEG = -2;
  var SYS_EOVER = -6;
  var SYS_EZDIM = -8;
  var SYS_ESOLT = -9;
  var SYS_EVERIFY = -10;
  var SYS_EINTERNAL = -11;

  /* 池(加载期一次分配;sysolve 段 allocCount = 18) */
  var _syH = new Float64Array(SYS_ROOTS + 1);        /* 1 h 升幂 */
  var _syW = new Float64Array(SYS_ROOTS + 1);        /* 2 prem 工作区 */
  var _syV = new Float64Array(SYS_ROOTS + 1);        /* 3 rem 暂存 */
  var _syT = new Float64Array(SYS_ROOTS + 1);        /* 4 gcd 轮换 */
  var _syQ = new Float64Array(SYS_ROOTS + 1);        /* 5 精确除商 */
  var _syD = new Float64Array(SYS_ROOTS + 1);        /* 6 导数 */
  var _syMc = new Float64Array(SYS_MIX_MAX * SYS_TERMS);       /* 7 混合元系数 */
  var _syMe = new Uint8Array(SYS_MIX_MAX * SYS_TERMS);         /* 8 混合元指数(ep ≤ 255) */
  var _syMn = new Uint8Array(SYS_MIX_MAX);                     /* 9 混合元项数(≤ 16) */
  var _syPwr = new Float64Array(SYS_ROOTS + 1);      /* 10 y 幂表实部 */
  var _syPwi = new Float64Array(SYS_ROOTS + 1);      /* 11 y 幂表虚部 */
  var _syCr = new Float64Array(SYS_ROOTS + 1);       /* 12 代回多项式系数实部 */
  var _syCi = new Float64Array(SYS_ROOTS + 1);       /* 13 虚部 */
  var _syXr = new Float64Array(SYS_ROOTS);           /* 14 x 根实部 */
  var _syXi = new Float64Array(SYS_ROOTS);           /* 15 x 根虚部 */
  var _dkRe = new Float64Array(SYS_ROOTS);           /* 16 DK 池(镜像 C nr 视图) */
  var _dkIm = new Float64Array(SYS_ROOTS);           /* 17 */
  var _dkDen = new Float64Array(SYS_ROOTS);          /* 18 */
  var _dkC = new Float64Array(SYS_ROOTS + 1);        /* 19 实系数入参暂存 */
  var _dkCre = new Float64Array(SYS_ROOTS + 1);      /* 20 复系数实部 */
  var _dkCim = new Float64Array(SYS_ROOTS + 1);      /* 21 虚部 */
  var _solXr = new Float64Array(SYS_SOL_CAP);        /* 22 回验后解池 x 实 */
  var _solXi = new Float64Array(SYS_SOL_CAP);        /* 23 x 虚 */
  var _solYr = new Float64Array(SYS_SOL_CAP);        /* 24 y 实 */
  var _solYi = new Float64Array(SYS_SOL_CAP);        /* 25 y 虚 */
  var _syPwx = new Float64Array(SYS_ROOTS + 1);      /* 26 x 幂表实部 */
  var _syPwx2 = new Float64Array(SYS_ROOTS + 1);     /* 27 x 幂表虚部 */
  var _syOut = new Uint8Array(512);                  /* 28 渲染缓冲(charCode ≤ 255;16 解最坏 ~480) */
  var _syDig = new Uint8Array(24);                   /* 29 整数位数字垫 */
  var _syXc = new Float64Array(SYS_ROOTS + 1);       /* 30 纯 x 元素系数(专用:_syT 被
                                                        syPolyGcd 的轮换占用,复用即踩) */

  /* F0 全池水位账本：sysolve 段 30 池加载期自报（docs/contracts/f0-governance-v25.md §3；
   * 合并 feat/solvers 时补履行登记义务——分支分叉于 F0 前，未含自报） */
  if (typeof _modulePoolSum === "function") {
    _modulePoolSum(_syH, _syW, _syV, _syT, _syQ, _syD, _syMc, _syMe, _syMn,
                   _syPwr, _syPwi, _syCr, _syCi, _syXr, _syXi, _dkRe, _dkIm,
                   _dkDen, _dkC, _dkCre, _dkCim, _solXr, _solXi, _solYr, _solYi,
                   _syPwx, _syPwx2, _syOut, _syDig, _syXc);
  }

  var _sysOutLen = 0;
  var _sysResult = "";                               /* 成功后有效(结果槽复用) */
  var _sysCode = 0;

  function sysolveAllocCount() { return 30; }
  function getSysolveString() { return _sysResult; }


  /* ---- sysolve 标量算术(2^53 审计;返回值或负错误码) ---- */

  /* 溢出哨兵用 NaN:有限整数的和/积永不 NaN,而 −6 等合法值会与错误码
   * 撞车(实测踩坑:系数 −6 被误判 EOVER)。调用方以 isNaN 判溢出。 */
  function sySafeAdd(a, b) {
    var r = a + b;
    if (r > SYS_SAFE || r < -SYS_SAFE) { return NaN; }
    return r;
  }
  function sySafeMul(a, b) {
    var r = a * b;
    if (r > SYS_SAFE || r < -SYS_SAFE) { return NaN; }
    return r;
  }
  function syIabs(x) { return x < 0 ? -x : x; }
  function syIgcd(a, b) {
    var x = a, y = b, t = 0;
    while (y !== 0) { t = x % y; x = y; y = t; }
    return x;
  }

  /* ---- f/g 残差表:解析进 GB 基槽(copy-out 后槽 0/1 即草稿) ---- */

  function syParseInto(src, slot) {
    var n = 0;
    var i = 0;
    var p = 0;
    var len = src.length;
    var rc;
    while (p < len && (src.charCodeAt(p) === 32 || src.charCodeAt(p) === 9)) { p++; }
    if (p >= len) { return GB_EARG; }
    for (;;) {
      rc = gbParseTerm(src, p, len);
      if (rc !== 1) { return rc; }
      p = _ptNext;
      if (_ptC !== 0) {
        if (n >= SYS_TERMS) { return GB_ETERM; }
        _coef[slot * SYS_TERMS + n] = _ptNeg ? -_ptC : _ptC;
        _ep[slot * SYS_TERMS + n] = (_ptNx << 4) | _ptNy;
        n++;
      }
      while (p < len && (src.charCodeAt(p) === 32 || src.charCodeAt(p) === 9)) { p++; }
      if (p >= len) { break; }
      if (src.charCodeAt(p) !== 43 && src.charCodeAt(p) !== 45) { return GB_EARG; }
    }
    /* ep 降序插入排序 + 同类项合并 + 去零(gb_parse_into 同构) */
    for (i = 1; i < n; i++) {
      var kc = _coef[slot * SYS_TERMS + i];
      var ke = _ep[slot * SYS_TERMS + i];
      var j = i - 1;
      while (j >= 0 && _ep[slot * SYS_TERMS + j] < ke) {
        _coef[slot * SYS_TERMS + j + 1] = _coef[slot * SYS_TERMS + j];
        _ep[slot * SYS_TERMS + j + 1] = _ep[slot * SYS_TERMS + j];
        j--;
      }
      _coef[slot * SYS_TERMS + j + 1] = kc;
      _ep[slot * SYS_TERMS + j + 1] = ke;
    }
    {
      var out = 0;
      i = 0;
      while (i < n) {
        var sum = _coef[slot * SYS_TERMS + i];
        var e2 = _ep[slot * SYS_TERMS + i];
        var k2 = i + 1;
        while (k2 < n && _ep[slot * SYS_TERMS + k2] === e2) {
          sum = sySafeAdd(sum, _coef[slot * SYS_TERMS + k2]);
          if (isNaN(sum)) { return SYS_EOVER; }
          k2++;
        }
        if (sum !== 0) {
          _coef[slot * SYS_TERMS + out] = sum;
          _ep[slot * SYS_TERMS + out] = e2;
          out++;
        }
        i = k2;
      }
      _cnt[slot] = out;
    }
    return 1;
  }

  /* ---- h 的 SFF 预处理(_syH 升幂;返回 h_sf 次数或负错误码) ---- */

  function syPrimitive(arr, dg) {
    var g = 0;
    var neg = 0;
    var i;
    var v;
    if (dg < 0) { return dg; }
    for (i = 0; i <= dg; i++) {
      v = syIabs(arr[i]);
      if (v !== 0) { g = (g === 0) ? v : syIgcd(g, v); }
    }
    if (g === 0) { g = 1; }
    neg = arr[dg] < 0 ? 1 : 0;
    for (i = 0; i <= dg; i++) { arr[i] = neg ? -(arr[i] / g) : (arr[i] / g); }
    return dg;
  }

  function syPdivmod(f, df, g, dg, qout) {
    var w = _syW;
    var i;
    var k;
    var lfw;
    var lcg;
    var steps = 0;
    var t;
    var da0 = df;
    if (dg < 0) { return SYS_EINTERNAL; }
    for (i = 0; i <= SYS_ROOTS; i++) { w[i] = 0; if (qout !== null) { qout[i] = 0; } }
    if (df < 0) { return 0; }
    for (i = 0; i <= df; i++) { w[i] = f[i]; }
    lcg = g[dg];
    while (df >= dg && df >= 0) {
      lfw = w[df];
      k = df - dg;
      for (i = df; i >= 0; i--) {
        t = sySafeMul(lcg, w[i]);
        if (isNaN(t)) { return SYS_EOVER; }
        w[i] = t;
        if (i >= k && i - k <= dg) {
          t = sySafeMul(lfw, g[i - k]);
          if (isNaN(t)) { return SYS_EOVER; }
          t = sySafeAdd(w[i], -t);
          if (isNaN(t)) { return SYS_EOVER; }
          w[i] = t;
        }
      }
      if (qout !== null) {
        for (i = df; i >= 0; i--) {
          t = sySafeMul(qout[i], lcg);
          if (isNaN(t)) { return SYS_EOVER; }
          qout[i] = t;
        }
        t = sySafeAdd(qout[k], lfw);
        if (isNaN(t)) { return SYS_EOVER; }
        qout[k] = t;
      }
      steps++;
      while (df >= 0 && w[df] === 0) { df--; }
    }
    if (qout !== null) { qout.dg = da0 - dg; }   /* 精确除的商次数 = da−db */
    return df;                                   /* 余式次数(−1 = 零) */
  }

  function syPolyGcd(a, da, b, db) {
    var t = _syT;
    var v = _syW;   /* syPdivmod 的余式工作区就是 _syW(实测踩坑:读错槽 = gcd 全错) */
    var dt;
    var i;
    var rc;
    while (db >= 0) {
      rc = syPdivmod(a, da, b, db, null);
      if (rc === SYS_EOVER) { return SYS_EOVER; }
      if (rc === SYS_EINTERNAL) { return SYS_EINTERNAL; }
      dt = rc;
      for (i = 0; i <= SYS_ROOTS; i++) { t[i] = v[i]; }
      da = db;
      for (i = 0; i <= SYS_ROOTS; i++) { a[i] = b[i]; }
      db = dt;
      for (i = 0; i <= SYS_ROOTS; i++) { b[i] = t[i]; }
      syPrimitive(b, db);
    }
    syPrimitive(a, da);
    return da;
  }

  function sySff() {
    /* _syH(升幂,次数 _syHd)→ SFF 去重根;返回 h_sf 次数或负错误码。
     * gcd 在 _syV 副本上跑(syPolyGcd 会改写入参),原 h 保留在 _syH 供精确除 */
    var dg = _syHd;
    var i;
    var rc;
    var dgc;
    var dq;
    var scale;
    var qq;
    syPrimitive(_syH, dg);
    if (dg >= 1) {
      for (i = 0; i <= SYS_ROOTS; i++) { _syD[i] = 0; _syV[i] = 0; }
      for (i = 1; i <= dg; i++) {
        var t = sySafeMul(_syH[i], i);
        if (isNaN(t)) { return SYS_EOVER; }
        _syD[i - 1] = t;
      }
      for (i = 0; i <= dg; i++) { _syV[i] = _syH[i]; }
      dgc = syPolyGcd(_syV, dg, _syD, dg - 1);
      if (dgc === SYS_EOVER || dgc === SYS_EINTERNAL) { return dgc; }
      if (dgc >= 1) {
        /* 精确除:h_sf = h / gcd(prem 商,scale = lc(gcd)^steps 显式重建) */
        qq = _syQ;
        rc = syPdivmod(_syH, dg, _syV, dgc, qq);
        if (rc === SYS_EOVER) { return SYS_EOVER; }
        if (rc !== -1) { return SYS_EINTERNAL; }   /* 余式非零 */
        dq = qq.dg;
        scale = syPdivScale(_syH, dg, _syV, dgc);
        if (isNaN(scale)) { return SYS_EOVER; }
        for (i = 0; i <= dq; i++) {
          if (scale !== 1) {
            if (qq[i] % scale !== 0) { return SYS_EINTERNAL; }
            qq[i] = qq[i] / scale;
          }
        }
        for (i = 0; i <= SYS_ROOTS; i++) { _syH[i] = 0; }
        for (i = 0; i <= dq; i++) { _syH[i] = qq[i]; }
        dg = dq;
      }
    }
    return dg;
  }

  function syPdivScale(f, df, g, dg) {
    /* 重放 prem 的消去轮数,scale = lc(g)^steps(C sy_pdivmod 的 scale 语义) */
    var w = _syW;
    var i;
    var steps = 0;
    var df2 = df;
    var lcg = g[dg];
    var lfw;
    var k;
    var t;
    var scale = 1;
    if (df2 < 0) { return 1; }
    for (i = 0; i <= df2; i++) { w[i] = f[i]; }
    while (df2 >= dg && df2 >= 0) {
      lfw = w[df2];
      k = df2 - dg;
      for (i = df2; i >= 0; i--) {
        t = sySafeMul(lcg, w[i]);
        if (isNaN(t)) { return SYS_EOVER; }
        w[i] = t;
        if (i >= k && i - k <= dg) {
          t = sySafeMul(lfw, g[i - k]);
          if (isNaN(t)) { return SYS_EOVER; }
          t = sySafeAdd(w[i], -t);
          if (isNaN(t)) { return SYS_EOVER; }
          w[i] = t;
        }
      }
      steps++;
      while (df2 >= 0 && w[df2] === 0) { df2--; }
    }
    for (i = 0; i < steps; i++) {
      t = sySafeMul(scale, lcg);
      if (isNaN(t)) { return SYS_EOVER; }
      scale = t;
    }
    return scale;
  }

  /* ---- Durand-Kerner 移植(镜像 ac_pro_numroots 与 C sy_dk_complex) ---- */

  function dkEval(cre, cim, n, zre, zim, ore, oim) {
    var re = cre[n];
    var im = cim[n];
    var i;
    var tr;
    var ti;
    for (i = n - 1; i >= 0; i--) {
      tr = re * zre - im * zim + cre[i];
      ti = re * zim + im * zre + cim[i];
      re = tr;
      im = ti;
    }
    ore.v = re;
    oim.v = im;
  }

  var _evRe = { v: 0.0 };   /* 加载期一次分配的求值出参槽 */
  var _evIm = { v: 0.0 };

  function syDkRun(cre, cim, n) {
    /* 池 _dkRe/_dkIm 迭代;返回 0 成功 / SYS_EINTERNAL 不收敛 */
    var rmax = 0.0;
    var cn = Math.sqrt(cre[n] * cre[n] + cim[n] * cim[n]);
    var radius;
    var twoPiOverN;
    var maxDelta = 0.0;
    var i;
    var j;
    var iter;
    if (!(cn > 1e-300)) { return SYS_EINTERNAL; }
    for (i = 0; i < n; i++) {
      var a = Math.sqrt(cre[i] * cre[i] + cim[i] * cim[i]);
      if (a > rmax) { rmax = a; }
    }
    radius = 1.0 + rmax / cn;
    if (!isFinite(radius) || radius < 1.0) { return SYS_EINTERNAL; }
    twoPiOverN = 6.283185307179586 / n;
    for (i = 0; i < n; i++) {
      var ang = twoPiOverN * (i + 0.5);
      _dkRe[i] = radius * Math.cos(ang);
      _dkIm[i] = radius * Math.sin(ang);
      _dkDen[i] = 0.0;
    }
    for (iter = 0; iter < 200; iter++) {
      maxDelta = 0.0;
      for (i = 0; i < n; i++) {
        var pre, pim, dre = 1.0, dim = 0.0, den2, qre, qim, dre2, dim2, dabs;
        dkEval(cre, cim, n, _dkRe[i], _dkIm[i], _evRe, _evIm);
        pre = _evRe.v;
        pim = _evIm.v;
        for (j = 0; j < n; j++) {
          var er, ei, tr, ti;
          if (j === i) { continue; }
          er = _dkRe[i] - _dkRe[j];
          ei = _dkIm[i] - _dkIm[j];
          tr = dre * er - dim * ei;
          ti = dre * ei + dim * er;
          dre = tr;
          dim = ti;
        }
        den2 = dre * dre + dim * dim;
        if (den2 < 1e-28) {
          dre = 1e-7 * (i + 1);
          dim = 1e-7 * (i + 1);
          den2 = dre * dre + dim * dim;
        }
        qre = (pre * dre + pim * dim) / den2;
        qim = (pim * dre - pre * dim) / den2;
        dre2 = -qre;
        dim2 = -qim;
        dabs = Math.sqrt(dre2 * dre2 + dim2 * dim2);
        if (dabs > maxDelta) { maxDelta = dabs; }
        _dkRe[i] += dre2;
        _dkIm[i] += dim2;
      }
      if (!isFinite(maxDelta)) { return SYS_EINTERNAL; }
      if (maxDelta < 1e-12) { break; }
    }
    if (maxDelta >= 1e-12) {
      /* 残差门(重根步长停滞出口,ac_pro_numroots 同式) */
      var residual = 0.0;
      var scale = 0.0;
      for (i = 0; i < n; i++) {
        var mag, sc = 0.0, pw = 1.0, zabs;
        dkEval(cre, cim, n, _dkRe[i], _dkIm[i], _evRe, _evIm);
        mag = Math.sqrt(_evRe.v * _evRe.v + _evIm.v * _evIm.v);
        if (mag > residual) { residual = mag; }
        zabs = Math.sqrt(_dkRe[i] * _dkRe[i] + _dkIm[i] * _dkIm[i]);
        for (j = 0; j <= n; j++) {
          var aj = Math.sqrt(cre[j] * cre[j] + cim[j] * cim[j]);
          sc += aj * pw;
          pw *= zabs;
        }
        if (sc > scale) { scale = sc; }
      }
      if (residual > 1e-12 * (1.0 + scale)) { return SYS_EINTERNAL; }
    }
    for (i = 0; i < n; i++) {
      if (Math.abs(_dkIm[i]) < 1e-10) { _dkIm[i] = 0.0; }
    }
    return 0;
  }

  /* ---- 单个整系数多项式求根路由(出参 _syXr/_syXi;返回根数或负码) ---- */

  function syRootsInt(c, d) {
    var k;
    if (d === 1) {
      _syXr[0] = -c[0] / c[1];
      _syXi[0] = 0.0;
      return 1;
    }
    for (k = 0; k <= d; k++) { _dkC[k] = c[k]; }
    if (d <= 4) {
      for (k = 0; k < 5; k++) { _coeffs[k] = 0.0; }
      for (k = 0; k <= d; k++) { _coeffs[k] = c[k]; }
      if (d === 2) { solveQuadratic(); }
      else if (d === 3) { solveCubic(); }
      else { solveQuartic(); }
      if (_solveError !== 0 || _rootCount !== d) { return SYS_EINTERNAL; }
      for (k = 0; k < d; k++) { _syXr[k] = _rootRe[k]; _syXi[k] = _rootIm[k]; }
      return d;
    }
    for (k = 0; k <= d; k++) { _dkCre[k] = c[k]; _dkCim[k] = 0.0; }
    if (syDkRun(_dkCre, _dkCim, d) !== 0) { return SYS_EINTERNAL; }
    for (k = 0; k < d; k++) { _syXr[k] = _dkRe[k]; _syXi[k] = _dkIm[k]; }
    return d;
  }

  /* ---- 回验 + 去重插入(候选流式;返回 0 / 负码) ---- */

  function syVerifyDedup(xr, xi, yr, yi) {
    var gi;
    var t;
    var i;
    var pwxe = _syPwx;
    var pwxi = _syPwx2;
    var pwye = _syPwr;
    var pwyi = _syPwi;
    pwxe[0] = 1.0; pwxi[0] = 0.0;
    pwye[0] = 1.0; pwyi[0] = 0.0;
    for (i = 1; i <= SYS_ROOTS; i++) {
      pwxe[i] = pwxe[i - 1] * xr - pwxi[i - 1] * xi;
      pwxi[i] = pwxe[i - 1] * xi + pwxi[i - 1] * xr;
      pwye[i] = pwye[i - 1] * yr - pwyi[i - 1] * yi;
      pwyi[i] = pwye[i - 1] * yi + pwyi[i - 1] * yr;
    }
    for (gi = 0; gi < 2; gi++) {
      var re = 0.0;
      var im = 0.0;
      var scale = 0.0;
      var resid;
      var nt = _cnt[gi];
      var base = gi * SYS_TERMS;
      for (t = 0; t < nt; t++) {
        var ex = (_ep[base + t] >> 4) & 15;
        var ey = _ep[base + t] & 15;
        var cr = _coef[base + t];
        var xr2 = pwxe[ex], xi2 = pwxi[ex];
        var yr2 = pwye[ey], yi2 = pwyi[ey];
        re += cr * (xr2 * yr2 - xi2 * yi2);
        im += cr * (xr2 * yi2 + xi2 * yr2);
        scale += Math.abs(cr) * Math.sqrt(xr2 * xr2 + xi2 * xi2) * Math.sqrt(yr2 * yr2 + yi2 * yi2);
      }
      resid = Math.sqrt(re * re + im * im) / (scale > 1.0 ? scale : 1.0);
      if (!(resid <= 1e-9)) { return SYS_EVERIFY; }
    }
    for (i = 0; i < _nsol; i++) {
      if (Math.abs(_solXr[i] - xr) <= 1e-9 && Math.abs(_solXi[i] - xi) <= 1e-9 &&
          Math.abs(_solYr[i] - yr) <= 1e-9 && Math.abs(_solYi[i] - yi) <= 1e-9) {
        return 0;   /* 去重:保留先插入者 */
      }
    }
    if (_nsol >= SYS_SOL_CAP) { return SYS_ESOLT; }
    _solXr[_nsol] = xr;
    _solXi[_nsol] = xi;
    _solYr[_nsol] = yr;
    _solYi[_nsol] = yi;
    _nsol++;
    return 0;
  }

  /* ---- 渲染(fmt 三级 = sqfree 契约同算法;复根 = sqfree §6.1 同形) ---- */

  function syPutCC(cc) {
    if (_sysOutLen >= _syOut.length - 1) { return SYS_EOVER; }
    _syOut[_sysOutLen++] = cc;
    return 0;
  }
  function syPutS(str) {
    var i;
    for (i = 0; i < str.length; i++) {
      if (syPutCC(str.charCodeAt(i)) !== 0) { return SYS_EOVER; }
    }
    return 0;
  }
  function syPutI(v) {
    var tmp = _syDig;
    var i = 0;
    var m = v < 0 ? -v : v;
    if (v < 0) { if (syPutCC(45) !== 0) { return SYS_EOVER; } }
    if (m === 0) { tmp[i++] = 0; }
    while (m > 0) { tmp[i++] = m % 10; m = (m - (m % 10)) / 10; }
    while (i > 0) { if (syPutCC(48 + tmp[--i]) !== 0) { return SYS_EOVER; } }
    return 0;
  }
  function syFmt(x) {
    /* 三级:整判 1e-9 / 有理重建 q ≤ 1000 / 定点 12 位(输出进 _syOut) */
    var r = x < 0 ? -x : x;
    var s = "";
    if (r < 1e12) {
      var nr = x < 0 ? -Math.round(-x) : Math.round(x);
      if (Math.abs(x - nr) < 1e-9) {
        return syPutI(nr);
      }
    }
    s = syFmtRat(x);
    if (s !== null) { return syPutS(s); }
    return syFmtFix(x);
  }
  function syFmtRat(x) {
    var q;
    var bestP = 0;
    var bestQ = 0;
    var bestErr = 1e300;
    for (q = 1; q <= 1000; q++) {
      var p = Math.round(x * q);
      var g = syIgcd(p < 0 ? -p : p, q);
      var pp = p;
      var qq = q;
      var err;
      if (g > 1) { pp = p / g; qq = q / g; }
      err = Math.abs(x - pp / qq);
      if (err <= 1e-9 && (err < bestErr || (err === bestErr && qq < bestQ) ||
          (err === bestErr && qq === bestQ && Math.abs(pp) < Math.abs(bestP)))) {
        bestErr = err;
        bestP = pp;
        bestQ = qq;
      }
    }
    if (bestQ > 0) {
      var s = bestP < 0 ? "-" + (-bestP) : "" + bestP;
      if (bestQ !== 1) { s = s + "/" + bestQ; }
      return s;
    }
    return null;
  }
  function syFmtFix(x) {
    /* 定点 12 位(C sq_fmt 第三级同式:四舍五入到 1e-12,ip/fp 拆分,尾零剥离);
     * 写 _syOut 并返回状态码(syFmtCoord 按 !==0 判错——返回串是实测踩坑) */
    var ax = x < 0 ? -x : x;
    var s = Math.round(ax * 1e12);
    var ip = Math.floor(s / 1e12);
    var fp = s - ip * 1e12;
    var digits = "" + fp;
    var i;
    if (x < 0) { if (syPutCC(45) !== 0) { return SYS_EOVER; } }
    if (syPutI(ip) !== 0) { return SYS_EOVER; }
    while (digits.length < 12) { digits = "0" + digits; }
    while (digits.length > 0 && digits.charAt(digits.length - 1) === "0") { digits = digits.slice(0, -1); }
    if (digits.length > 0) {
      if (syPutCC(46) !== 0) { return SYS_EOVER; }
      for (i = 0; i < digits.length; i++) {
        if (syPutCC(digits.charCodeAt(i)) !== 0) { return SYS_EOVER; }
      }
    }
    return 0;
  }
  function syFmtCoord(re, im) {
    var aim = im < 0 ? -im : im;
    if (aim < 1e-9) { return syFmt(re); }
    if (Math.abs(re) >= 1e-9) {
      if (syPutCC(91) !== 0) { return SYS_EOVER; }
      if (syFmt(re) !== 0) { return SYS_EINTERNAL; }
      if (syPutCC(93) !== 0) { return SYS_EOVER; }
      if (syPutCC(im > 0 ? 43 : 45) !== 0) { return SYS_EOVER; }
    } else if (im < 0) {
      if (syPutCC(45) !== 0) { return SYS_EOVER; }
    }
    if (Math.abs(aim - 1.0) >= 1e-9) {
      if (syFmt(aim) !== 0) { return SYS_EINTERNAL; }
    }
    return syPutCC(105);
  }

  /* ---- 对外:sysolve 主入口 ---- */

  var _syHd = -1;
  var _nsol = 0;

  function sysolve(fsrc, gsrc) {
    var rc;
    var k;
    var t;
    var i;
    var j;
    var e;
    var dh = -1;
    var dhsf = -1;
    var decoupled = 1;
    var hasXEl = 0;
    var dxc = -1;
    var xc = _syXc;                /* 纯 x 元素(专用池:_syT 是 syPolyGcd 轮换区) */
    var nx = 0;
    var ny = 0;
    var nmix = 0;
    for (i = 0; i <= SYS_ROOTS; i++) { _syXc[i] = 0; }   /* 纯 x 槽跨调用清零 */
    _sysOutLen = 0;
    _sysResult = "";
    _nsol = 0;

    if (typeof fsrc !== "string" || typeof gsrc !== "string") { return SYS_EARG; }

    /* [1] GB 阶段 */
    rc = groebner(fsrc, gsrc);
    if (rc !== GB_OK) {
      if (rc === GB_EINTERNAL || rc === GB_EBUF) { return SYS_EINTERNAL; }
      return rc;                   /* −1..−6 与 SYS 同值同义(契约 §7 透传) */
    }

    /* [2] copy-out + 分类 */
    for (i = 0; i <= SYS_ROOTS; i++) { _syH[i] = 0; }
    for (i = 0; i < SYS_MIX_MAX; i++) { _syMn[i] = 0; }
    {
      var n = gbCount();
      for (k = 0; k < n; k++) {
        var nt = gbTerms(k);
        var allX0 = 1;
        var isConst = 1;
        if (nt <= 0) { return SYS_EINTERNAL; }
        for (t = 0; t < nt; t++) {
          var ep = gbExps(k, t);
          if (((ep >> 4) & 15) !== 0) { allX0 = 0; isConst = 0; }
          else if ((ep & 15) !== 0) { isConst = 0; }
        }
        if (isConst) { _sysResult = "[]"; return SYS_OK; }   /* GB={1} → 0 解 */
        if (allX0) {
          if (dh >= 0) { return SYS_EINTERNAL; }
          for (t = 0; t < nt; t++) {
            var ey = gbExps(k, t) & 15;
            if (ey > SYS_ROOTS) { return SYS_EINTERNAL; }
            _syH[ey] = gbCoef(k, t);
            if (ey > dh) { dh = ey; }
          }
        } else {
          var pureX = 1;
          if (nmix >= SYS_MIX_MAX) { return SYS_EINTERNAL; }
          _syMn[nmix] = nt;
          for (t = 0; t < nt; t++) {
            var ep2 = gbExps(k, t);
            _syMc[nmix * SYS_TERMS + t] = gbCoef(k, t);
            _syMe[nmix * SYS_TERMS + t] = ep2;
            if ((ep2 & 15) !== 0) { pureX = 0; }
          }
          if (!pureX) { decoupled = 0; }
          else if (!hasXEl) {
            for (t = 0; t < nt; t++) {
              var ex = (_syMe[nmix * SYS_TERMS + t] >> 4) & 15;
              xc[ex] = _syMc[nmix * SYS_TERMS + t];
              if (ex > dxc) { dxc = ex; }
            }
            hasXEl = 1;
          } else {
            return SYS_EINTERNAL;
          }
          nmix++;
        }
      }
    }
    /* f,g 残差表(copy-out 后槽 0/1 即草稿) */
    rc = syParseInto(fsrc, 0);
    if (rc !== 1) { return rc === GB_EOVER ? SYS_EOVER : SYS_EINTERNAL; }
    rc = syParseInto(gsrc, 1);
    if (rc !== 1) { return rc === GB_EOVER ? SYS_EOVER : SYS_EINTERNAL; }

    if (dh < 0) { return SYS_EZDIM; }
    _syHd = dh;

    /* [3] SFF 预处理 */
    dhsf = sySff();
    if (dhsf === SYS_EOVER || dhsf === SYS_EINTERNAL) { return dhsf; }
    if (dhsf < 1) { return SYS_EINTERNAL; }

    /* [4] h_sf 求根 */
    rc = syRootsInt(_syH, dhsf);
    if (rc < 0) { return rc; }
    ny = rc;
    /* y 根暂存(求 x 侧会覆写 _syXr/_syXi,先拷走) */
    var yre = _syQ;     /* 草稿槽复用:Q 尚未使用 */
    var yim = _syD;
    for (i = 0; i < ny; i++) { yre[i] = _syXr[i]; yim[i] = _syXi[i]; }

    /* [5] 解耦交叉积 或 耦合代回(候选流式回验) */
    if (decoupled) {
      if (!hasXEl) { return SYS_EZDIM; }
      /* xc = _syXc 已在入口清零、copy-out 填充;此前「zero 后再自拷」把
       * 系数全数抹零(自拷贝实测踩坑),直接传 _syXc */
      rc = syRootsInt(xc, dxc);
      if (rc < 0) { return rc; }
      nx = rc;
      for (i = 0; i < nx; i++) {
        for (j = 0; j < ny; j++) {
          rc = syVerifyDedup(_syXr[i], _syXi[i], yre[j], yim[j]);
          if (rc !== 0) { return rc; }
        }
      }
    } else {
      for (j = 0; j < ny; j++) {
        var allZero = 1;
        _syPwr[0] = 1.0; _syPwi[0] = 0.0;
        for (e = 1; e <= SYS_ROOTS; e++) {
          _syPwr[e] = _syPwr[e - 1] * yre[j] - _syPwi[e - 1] * yim[j];
          _syPwi[e] = _syPwr[e - 1] * yim[j] + _syPwi[e - 1] * yre[j];
        }
        for (i = 0; i < nmix && allZero; i++) {
          var mxE = 0.0;
          var nonzero = 0;
          for (e = 0; e <= SYS_ROOTS; e++) { _syCr[e] = 0.0; _syCi[e] = 0.0; }
          for (t = 0; t < _syMn[i]; t++) {
            var ex2 = (_syMe[i * SYS_TERMS + t] >> 4) & 15;
            var ey2 = _syMe[i * SYS_TERMS + t] & 15;
            var c2 = _syMc[i * SYS_TERMS + t];
            _syCr[ex2] += c2 * _syPwr[ey2];
            _syCi[ex2] += c2 * _syPwi[ey2];
          }
          for (e = 0; e <= SYS_ROOTS; e++) {
            var m2 = Math.sqrt(_syCr[e] * _syCr[e] + _syCi[e] * _syCi[e]);
            if (m2 > mxE) { mxE = m2; }
          }
          for (e = 0; e <= SYS_ROOTS; e++) {
            var m3 = Math.sqrt(_syCr[e] * _syCr[e] + _syCi[e] * _syCi[e]);
            if (m3 > 1e-9 * (mxE > 1.0 ? mxE : 1.0)) { nonzero = 1; break; }
          }
          if (nonzero) { allZero = 0; }
        }
        if (allZero) { return SYS_EZDIM; }
        for (i = 0; i < nmix; i++) {
          var deg = -1;
          var mx2 = 0.0;
          for (e = 0; e <= SYS_ROOTS; e++) { _syCr[e] = 0.0; _syCi[e] = 0.0; }
          for (t = 0; t < _syMn[i]; t++) {
            var ex3 = (_syMe[i * SYS_TERMS + t] >> 4) & 15;
            var ey3 = _syMe[i * SYS_TERMS + t] & 15;
            var c3 = _syMc[i * SYS_TERMS + t];
            _syCr[ex3] += c3 * _syPwr[ey3];
            _syCi[ex3] += c3 * _syPwi[ey3];
          }
          for (e = 0; e <= SYS_ROOTS; e++) {
            var m4 = Math.sqrt(_syCr[e] * _syCr[e] + _syCi[e] * _syCi[e]);
            if (m4 > mx2) { mx2 = m4; }
          }
          for (e = SYS_ROOTS; e >= 0; e--) {
            var m5 = Math.sqrt(_syCr[e] * _syCr[e] + _syCi[e] * _syCi[e]);
            if (m5 > 1e-9 * mx2) { deg = e; break; }
          }
          if (deg < 1) { continue; }
          if (syDkRun(_syCr, _syCi, deg) !== 0) { return SYS_EINTERNAL; }
          for (e = 0; e < deg; e++) {
            rc = syVerifyDedup(_dkRe[e], _dkIm[e], yre[j], yim[j]);
            if (rc !== 0) { return rc; }
          }
        }
      }
    }

    /* [6] 排序(y Re ↓ → y Im ↓ → x Re ↓ → x Im ↓,1e-9 并列;插入排序稳定) */
    for (i = 1; i < _nsol; i++) {
      var xr = _solXr[i], xi = _solXi[i], yr = _solYr[i], yi = _solYi[i];
      j = i - 1;
      while (j >= 0) {
        var less = 0;
        if (_solYr[j] < yr - 1e-9) { less = 1; }
        else if (Math.abs(_solYr[j] - yr) <= 1e-9) {
          if (_solYi[j] < yi - 1e-9) { less = 1; }
          else if (Math.abs(_solYi[j] - yi) <= 1e-9) {
            if (_solXr[j] < xr - 1e-9) { less = 1; }
            else if (Math.abs(_solXr[j] - xr) <= 1e-9) {
              if (_solXi[j] < xi - 1e-9) { less = 1; }
            }
          }
        }
        if (less === 0) { break; }
        _solXr[j + 1] = _solXr[j]; _solXi[j + 1] = _solXi[j];
        _solYr[j + 1] = _solYr[j]; _solYi[j + 1] = _solYi[j];
        j--;
      }
      _solXr[j + 1] = xr; _solXi[j + 1] = xi;
      _solYr[j + 1] = yr; _solYi[j + 1] = yi;
    }

    /* [7] 渲染 */
    if (syPutCC(91) !== 0) { return SYS_EOVER; }
    for (k = 0; k < _nsol; k++) {
      if (k > 0) {
        if (syPutCC(44) !== 0) { return SYS_EOVER; }
        if (syPutCC(32) !== 0) { return SYS_EOVER; }
      }
      if (syPutCC(40) !== 0) { return SYS_EOVER; }
      if (syFmtCoord(_solXr[k], _solXi[k]) !== 0) { return SYS_EINTERNAL; }
      if (syPutCC(44) !== 0) { return SYS_EOVER; }
      if (syPutCC(32) !== 0) { return SYS_EOVER; }
      if (syFmtCoord(_solYr[k], _solYi[k]) !== 0) { return SYS_EINTERNAL; }
      if (syPutCC(41) !== 0) { return SYS_EOVER; }
    }
    if (syPutCC(93) !== 0) { return SYS_EOVER; }
    {
      var s2 = "";
      for (i = 0; i < _sysOutLen; i++) { s2 = s2 + String.fromCharCode(_syOut[i]); }
      _sysResult = s2;
    }
    return SYS_OK;
  }

  return {
    groebner: groebner,
    getString: getString,
    count: gbCount,
    terms: gbTerms,
    coef: gbCoef,
    exps: gbExps,
    resultant: resultant,
    getResultant: getResultant,
    getResultantError: getResultantError,
    buildSylvester: buildSylvester,
    getSylvester: getSylvester,
    reset: reset,
    allocCount: allocCount,

    /* ---- v2.5.0 F2:二元方程组 GB 消元数值全解(契约 sysolve-format-v25) ---- */
    sysolve: sysolve,
    getSysolveString: getSysolveString,
    sysolveAllocCount: sysolveAllocCount
  };
})();

if (typeof module === "object" && module && module.exports) {
  module.exports.ApolloGroebner = ApolloGroebner;
}
