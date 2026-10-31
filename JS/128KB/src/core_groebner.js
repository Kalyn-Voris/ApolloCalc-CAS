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

  function _safeAdd(a, b) {
    var r = a + b;
    if (r > GB_SAFE || r < -GB_SAFE) { return GB_EOVER; }
    return r;
  }
  function _safeSub(a, b) {
    var r = a - b;
    if (r > GB_SAFE || r < -GB_SAFE) { return GB_EOVER; }
    return r;
  }
  function _safeMul(a, b) {
    var r = a * b;
    if (r > GB_SAFE || r < -GB_SAFE) { return GB_EOVER; }
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
        if (sum === GB_EOVER) { return GB_EOVER; }
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
              if (v === GB_EOVER) { return GB_EOVER; }
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
            if (prod === GB_EOVER) { return GB_EOVER; }
            var cur = _dense[_idx(tx, ty)];
            var sub = _safeSub(cur, prod);
            if (sub === GB_EOVER) { return GB_EOVER; }
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
      if (prod === GB_EOVER) { return GB_EOVER; }
      cur = _dense[_idx(tx, ty)];
      sum = _safeAdd(cur, prod);
      if (sum === GB_EOVER) { return GB_EOVER; }
      _dense[_idx(tx, ty)] = sum;
    }
    for (t = 0; t < _cnt[j]; t++) {
      var tej = _ep[j * GB_TERMS + t];
      tx = _exOf(tej) + (lcx - jx);
      ty = _eyOf(tej) + (lcy - jy);
      if (tx + ty > GB_MAX_DEG) { return GB_EDEG; }
      prod = _safeMul(lci, _coef[j * GB_TERMS + t]);
      if (prod === GB_EOVER) { return GB_EOVER; }
      cur = _dense[_idx(tx, ty)];
      sub = _safeSub(cur, prod);
      if (sub === GB_EOVER) { return GB_EOVER; }
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
    allocCount: allocCount
  };
})();

if (typeof module === "object" && module && module.exports) {
  module.exports.ApolloGroebner = ApolloGroebner;
}
