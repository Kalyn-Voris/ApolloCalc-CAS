// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · core_symboltree.js — 符号微分引擎
   (v2.4.0 功能 6,JS 侧;补齐 C 侧 ac_symbolic + A6 隐函数求导)

   职责:parseExpression 的 RPN token 流 → 扁平表达式树(Int32Array
   节点池,type/left/right/value 四字段)→ 符号求导 → 规范字符串渲染;
   多项式子树接 core_ratroot 输出因式列表(factor 语义,镜像 C 侧
   ac_sym_factor_poly 的 g_sym_factor_*);隐函数 dy/dx = −F_x/F_y
   对齐 C 侧 A6(ac_pro_symbolic_ext.c,错误码 0..4 逐值复刻)。

   preCheck 深度≤6 与树深的联动(先定义,后写码)
   preCheckExpression(core_solvers.js)的「深度 ≤6」只度量括号嵌套,
   函数 ≤4、变量 ≤10 各自独立计数。它不度量运算符链:纯数字链
   "1+1+…+1" 无括号、无变量,preCheck 放行,而左结合 ADD 链的树深随
   项数线性增长。因此树侧自带两道上限,联动定义如下:
     1. build(src) 内部顺序:parseExpression → preCheckExpression
        (拒绝 → ST_EGUARD)→ toRPN → RPN 建树(逐节点跟踪树深);
     2. ST_MAX_TREE_NODES = 100:源树节点上限,超 → ST_EBUDGET。
        依据:导数节点数 ≤ 4×源树 + 8(C ac_symbolic.c:154 同式),
        100×4+8 = 408 < 512 池 → 求导必然可容纳,前置可证;
     3. ST_MAX_DEPTH = 48:树深上限,超 → ST_EDEPTH。依据:6 层括号
        × 每层 ~4 级运算 + 10 变量链的树深 ≤ ~40,48 留余量;
        两道上限任一触发即拒绝(preCheck 管括号嵌套,树深管运算链,
        必要不充分,不能互相替代)。
   隐函数通道(独立词法器,不经 preCheck——它读的是内核 tokenPool,
   不适用于 x/y 私有 token 流):token ≤ 128、树节点 ≤ 100、树深 ≤ 48,
   超限一律映射 EBUDGET(2),与 C A6 的 AC_PRO_SYM_NODE_CAP=256 →
   AC_PRO_IMP_EBUDGET 同语义。

   节点编码
   _pool = Int32Array(512×4),节点 i 占 [i*4 .. i*4+3]:
     [type, left, right, value]。type ∈ ST_CONST..ST_FUN(0..5 与 C 侧
     NODE_CONST..NODE_POW 同值,6/7 为 JS 扩展);left/right 为子节点
     索引,叶子 −1;value:CONST = numberPool 索引(经 symConst 统一落
     内核池,输入常量与求导新生常量同源)、VAR = 变量标签(主通道恒
     0 = x,隐函数通道 0 = x / 1 = y,对齐 C A6 的填充洞标签)、
     FUN = F_* id、POW = 未用(指数走 right 子节点,支持任意指数)。
   会话语义与 tokenPool 同款:树在下次 build/implicitDyDx/reset 前有效;
   implicitDyDx 自毁主树会话(独立管线的 C A6 同款隔离)。

   错误码(getError)
     0 ST_OK      成功
     1 ST_EPARSE  词法/RPN/建树失败(含 atan2、隐通道函数/除法/负指数)
     2 ST_EGUARD  preCheckExpression 拒绝(括号深/函数/变量超限)
     3 ST_EDEPTH  树深 > 48(联动定义第 3 条)
     4 ST_EBUDGET 节点 > 100、numberPool 溢出或导数预算不足
     5 ST_EOVER   渲染缓冲(1024 字符)溢出——常规输入不可达,防御位
   隐函数通道独立编码(返回值即错误码):0 OK / 1 EPARSE / 2 EBUDGET /
     3 EFLAT(F_y≈0)/ 4 EEVAL,与 C A6 逐值复刻,不经 getError。
   另一实现边界:常底 c ≤ 0 的 c^v 求导(ln(c) 非实)按预算失败路径
   返回 ST_EBUDGET,测试域不覆盖。

   求导规则(C ac_symbolic.c 同款 + JS 扩展)
     dmake 化简:0+x→x、x+0→x、x−0→x、0·x→0、1·x→x、常数×常数折叠
     与常数头左收(C integrate pro_int_mul 同款,输出 6*x 而非 3*(2*x))、
     SUB(0,x) 负号外提(迭代)、幂法则经 symPowInt 继承 u^1→u、u^0→1
     (d(x^1)/d(x^0) 直接坍缩,不残留 POW(x,0))。
     POW 三分支:常指数 → n·u^(n−1)·du(u 取原始基节点索引,支持负/
     分数常指数);常底 c(e 特判 ln e = 1)→ c^v·ln(c)·dv;
     一般 u^v → u^v·(dv·ln(u) + v·du/u)(复用原 POW 节点,append-only)。
     FUN 17 种单参函数全规则(sin..atanh + abs);atan2 建树期拒绝。

   factor 语义(镜像 ac_sym_factor_poly)
     结构化多项式判定(clean/hasVar 两趟标记)→ 升幂系数提取
     (MUL/DIV 的无变量侧经求值折叠为标量乘子)→ 次数 1..4 → 写
     _coeffs → ratRootDescent(会改写 _coeffs,C 同款副作用)→ 有理根
     五分量精确形式 + 剩余多项式(dd=0 → 系数 [1],C「完全分解约定」)。
     非多项式/次数 > 4 → 错误码 1;非整系数由 descent 的快照复核诚实
     降级为 0 因式 + 原式剩余。

   零 GC:20 组 TypedArray 加载期一次分配(allocCount=20,明细见各声明);
     全路径无对象/数组字面量、无递归(建树/求导/求值/渲染/取系数全部
     显式栈)、热路径零字符串操作;字符串只经 charCodeAt/
     String.fromCharCode 通道(core_ode 同款),渲染出口一次性拼接。
   依赖(内核拼接序):parseExpression/pushNumber/funcName(parser)、
     toRPN/_rpnToken/_evalError(evaluator)、preCheckExpression(solvers)、
     ratRootDescent/_coeffs(ratroot)、exactSetFraction(exact)、
     applyFunction(math_functions)。
   导出三通道:Node require / 浏览器 <script> 顶层 var / 内核拼接 ESM。
 */

var ApolloSymboltree = (function () {
  "use strict";

  /* 常量 */
  var ST_MAX_NODES = 512;        /* 节点池容量(C AC_SYM_MAX_NODES 同值) */
  var ST_MARK = 512;             /* 后序标记偏移(> 最大节点索引) */
  var ST_MAX_TREE_NODES = 100;   /* 源树节点上限(联动定义第 2 条) */
  var ST_MAX_DEPTH = 48;         /* 树深上限(联动定义第 3 条) */
  var ST_IMP_TOK_MAX = 128;      /* 隐函数 token 上限 */
  var ST_OUT_CAP = 1024;         /* 渲染缓冲(字符) */
  var ST_WALK_CAP = 1032;        /* 遍历栈 = 2×512+8(C 同款) */

  /* 节点类型(0..5 与 C NODE_* 同值) */
  var ST_CONST = 0;
  var ST_VAR = 1;
  var ST_ADD = 2;
  var ST_SUB = 3;
  var ST_MUL = 4;
  var ST_POW = 5;
  var ST_DIV = 6;                /* JS 扩展:C 符号层无除法节点 */
  var ST_FUN = 7;                /* JS 扩展:value = F_* id */

  /* 错误码 */
  var ST_OK = 0;
  var ST_EPARSE = 1;
  var ST_EGUARD = 2;
  var ST_EDEPTH = 3;
  var ST_EBUDGET = 4;
  var ST_EOVER = 5;

  /* 池(加载期一次分配;allocCount = 20) */
  var _pool = new Int32Array(ST_MAX_NODES * 4);      /* 节点池 8192 B */
  var _buildStack = new Int32Array(ST_MAX_NODES);    /* 建树栈:idx | depth<<16 */
  var _walk = new Int32Array(ST_WALK_CAP);           /* 遍历栈(求导/求值/标记共用) */
  var _dmap = new Int32Array(ST_MAX_NODES);          /* 节点→导数节点(兼 factor clean 场) */
  var _valStack = new Float64Array(128);             /* 求值栈 */
  var _outBuf = new Int32Array(ST_OUT_CAP);          /* 渲染缓冲(charCode) */
  var _workI = new Int32Array(24);                   /* pushInt 位数字垫 */
  var _facRat = new Int32Array(8);                   /* 因式列表五分量(镜像 g_sym_factor_*) */
  var _facRadNum = new Int32Array(8);
  var _facRad = new Int32Array(8);
  var _facDen = new Int32Array(8);
  var _facExp = new Int32Array(8);
  var _remCoeffs = new Float64Array(8);              /* 剩余多项式系数(升幂) */
  var _fetchN = new Int32Array(ST_MAX_NODES);        /* 取系数栈:节点 */
  var _fetchM = new Float64Array(ST_MAX_NODES);      /* 取系数栈:乘子 */
  var _hasVar = new Int32Array(ST_MAX_NODES);        /* 无变量子树标记(factor) */
  var _impTok = new Int32Array(ST_IMP_TOK_MAX);      /* 隐函数 token 流 */
  var _impNum = new Float64Array(64);                /* 隐函数整常量池 */
  var _renderStack = new Int32Array(1024);           /* 渲染栈:idx|mp<<16 或 -(char+1) */
  var _polyC = new Float64Array(8);                  /* 多项式系数(升幂,factor) */

  var _allocCount = 20;

  /* 会话态 */
  var _count = 0;        /* 当前池节点数 */
  var _root = -1;        /* 源树根 */
  var _srcCount = 0;     /* 建树完成时节点数 */
  var _srcDepth = 0;     /* 源树深(联动定义可观测值) */
  var _droot = -1;       /* 最近一次导数根;−1 = 尚未求导/失败 */
  var _view = -1;        /* 渲染目标:build 后 = 源根,diff 后 = 导数根 */
  var _err = ST_OK;
  var _facK = 0;         /* 有理因式个数 */
  var _remDeg = 0;       /* 剩余多项式次数 */
  var _dydx = 0.0;
  var _outLen = 0;       /* 渲染游标 */

  function allocCount() { return _allocCount; }
  function getError() { return _err; }
  function getDepth() { return _srcDepth; }
  function getSrcNodes() { return _srcCount; }
  function getNodeCount() { return _count; }

  /* 节点构造 */

  function _alloc(type, l, r, v) {
    var b;
    if (_count >= ST_MAX_NODES) { return -1; }
    b = _count * 4;
    _pool[b] = type;
    _pool[b + 1] = l;
    _pool[b + 2] = r;
    _pool[b + 3] = v;
    _count++;
    return _count - 1;
  }

  function _type(i) { return _pool[i * 4]; }
  function _left(i) { return _pool[i * 4 + 1]; }
  function _right(i) { return _pool[i * 4 + 2]; }
  /* CONST 的值:统一落内核 numberPool(输入与求导新生常量同源) */
  function _cval(i) { return numberPool[_pool[i * 4 + 3]]; }
  function _isConstV(i, v) {
    return _type(i) === ST_CONST && _cval(i) === v;
  }
  function _isSub0(i) {
    return _type(i) === ST_SUB && _isConstV(_left(i), 0);
  }

  /* 常量节点:值 push 进 numberPool(满 → −1,调用方映射 EBUDGET) */
  function symConst(v) {
    var ni = pushNumber(v);
    if (ni < 0) { return -1; }
    return _alloc(ST_CONST, -1, -1, ni);
  }
  function symVar(tag) { return _alloc(ST_VAR, -1, -1, tag); }
  function symAdd(a, b) { return _alloc(ST_ADD, a, b, 0); }
  function symSub(a, b) {
    /* 建树期常数折叠:0 − c → −c(一元负号落在常数上);其余不折 */
    if (a >= 0 && _isConstV(a, 0) && _type(b) === ST_CONST) {
      return symConst(-_cval(b));
    }
    return _alloc(ST_SUB, a, b, 0);
  }
  function symMul(a, b) { return _alloc(ST_MUL, a, b, 0); }
  function symDiv(a, b) { return _alloc(ST_DIV, a, b, 0); }
  function symFun(f, arg) { return _alloc(ST_FUN, arg, -1, f); }

  /* 一般幂:指数节点;常指数继承 u^1→u、u^0→1(C ac_sym_new_pow 同款) */
  function symPow(base, expNode) {
    var v;
    if (base < 0 || expNode < 0) { return -1; }
    if (_type(expNode) === ST_CONST) {
      v = _cval(expNode);
      if (v === 1) { return base; }
      if (v === 0) { return symConst(1); }
    }
    return _alloc(ST_POW, base, expNode, 0);
  }
  /* 常指数幂(C ac_sym_new_pow(base, n) 同款,支持负/分数 n) */
  function symPowInt(base, n) {
    if (base < 0) { return -1; }
    if (n === 1) { return base; }
    if (n === 0) { return symConst(1); }
    return _alloc(ST_POW, base, symConst(n), 0);
  }

  /* dmake:导数节点构造(C sym_dmake_* 同款 + 扩展) */

  /* 加法:0+x→x、x+0→x + 右侧 ADD 子树全展平成左结合扁平链
   * (C integrate pro_int_add 同款形态)。展平用先序收集(显式栈,
   * 栈场复用 _buildStack —— 该栈仅在 buildFromRPN 期占用,dmake 只在
   * 求导/偏导期运行,两者互斥);零递归。容量兜底:项满则退回不展平。 */
  function dmake_add(l, r) {
    var top = 0;
    var idx = 0;
    var e = 0;
    var acc = 0;
    var i = 0;
    var cap = _buildStack.length;
    if (l < 0 || r < 0) { return -1; }
    if (_isConstV(l, 0)) { return r; }
    if (_isConstV(r, 0)) { return l; }
    /* 双端复用 _buildStack:项表 [0..idx) 自底向上,收集栈 [top..cap) 自顶向下 */
    _buildStack[idx] = l; idx++;
    top = cap;
    top--;
    _buildStack[top] = r;
    while (top < cap) {
      e = _buildStack[top];
      top++;
      if (_type(e) === ST_ADD) {
        if (idx + 2 > top) { return symAdd(l, r); }   /* 兜底:不展平 */
        top--;
        _buildStack[top] = _right(e);
        top--;
        _buildStack[top] = _left(e);
      } else {
        if (idx >= top) { return symAdd(l, r); }
        _buildStack[idx] = e;
        idx++;
      }
    }
    /* 项表 [0..idx):左→右;左结合折叠 */
    acc = _buildStack[0];
    for (i = 1; i < idx; i++) {
      e = _buildStack[i];
      if (_isConstV(acc, 0)) { acc = e; continue; }
      if (_isConstV(e, 0)) { continue; }
      acc = symAdd(acc, e);
      if (acc < 0) { return -1; }
    }
    return acc;
  }
  function dmake_sub(l, r) {
    if (l < 0 || r < 0) { return -1; }
    if (_isConstV(r, 0)) { return l; }
    return symSub(l, r);
  }
  function dmake_neg(a) {
    if (a < 0) { return -1; }
    if (_isConstV(a, 0)) { return a; }
    return symSub(symConst(0), a);
  }
  function dmake_div2(num, den) {
    if (num < 0 || den < 0) { return -1; }
    return symDiv(num, den);
  }
  /* 乘法:SUB(0,x) 负号外提(迭代计数)+ 常数折叠 + 常数头左收
   * (C integrate pro_int_mul 同款;输出 -cos(x)*sin(x)、6*x 形态) */
  function dmake_mul(a, b) {
    var neg = 0;
    var ca = 0;
    var t = 0;
    if (a < 0 || b < 0) { return -1; }
    for (;;) {
      if (_isSub0(a)) { neg = 1 - neg; a = _right(a); continue; }
      if (_isSub0(b)) { neg = 1 - neg; b = _right(b); continue; }
      break;
    }
    if (_type(a) === ST_CONST && _type(b) === ST_CONST) {
      ca = symConst(_cval(a) * _cval(b));
      if (ca < 0) { return -1; }
      return neg !== 0 ? dmake_neg(ca) : ca;
    }
    if (_isConstV(a, 0) || _isConstV(b, 0)) {
      ca = symConst(0);
      if (ca < 0) { return -1; }
      return ca;
    }
    /* 主循环:常数折叠单向汇聚(C integrate pro_int_mul 同款次序——
     * 常数×常数头先折,再判 1·x,避免折叠与化简乒乓) */
    for (;;) {
      if (_type(a) === ST_CONST && _type(b) === ST_CONST) {
        ca = symConst(_cval(a) * _cval(b));
        if (ca < 0) { return -1; }
        t = ca;
        break;
      }
      if (_isConstV(a, 0) || _isConstV(b, 0)) {
        ca = symConst(0);
        if (ca < 0) { return -1; }
        t = ca;
        break;
      }
      if (_isConstV(a, 1)) { t = b; break; }
      if (_isConstV(b, 1)) { t = a; break; }
      /* 常数头左收:mul(c1, mul(c2, X)) → mul(c1·c2, X)(迭代) */
      if (_type(a) === ST_CONST && _type(b) === ST_MUL &&
          _type(_left(b)) === ST_CONST) {
        ca = symConst(_cval(a) * _cval(_left(b)));
        if (ca < 0) { return -1; }
        a = ca;
        b = _right(b);
        continue;
      }
      /* 常数尾左收:mul(mul(c, X), c2) → mul(c·c2, X) */
      if (_type(a) === ST_MUL && _type(_left(a)) === ST_CONST &&
          _type(b) === ST_CONST) {
        ca = symConst(_cval(_left(a)) * _cval(b));
        if (ca < 0) { return -1; }
        b = _right(a);
        a = ca;
        continue;
      }
      t = symMul(a, b);
      break;
    }
    if (t < 0) { return -1; }
    if (neg !== 0) { return dmake_neg(t); }
    return t;
  }
  /* 商法则:(dl·v − u·dr)/v²(仅 ST_DIV 求导用);分子恒零 → 0 */
  function dmake_quot(dl, u, v, dr) {
    var t1 = 0;
    var t2 = 0;
    var num = 0;
    var den = 0;
    if (dl < 0 || dr < 0) { return -1; }
    t1 = dmake_mul(dl, v);
    t2 = dmake_mul(u, dr);
    num = dmake_sub(t1, t2);
    if (num < 0) { return -1; }
    if (_isConstV(num, 0)) { return num; }
    den = symPowInt(v, 2);
    if (den < 0) { return -1; }
    return symDiv(num, den);
  }

  /* 建树:RPN → 树(显式栈,逐节点跟踪树深) */

  /* rpn:tokenPool 编码的 RPN 流;numSrc:T_NUM payload 的取值场;
   * implicit:1 = 隐函数通道(只认小写 x/y,拒绝函数/除法/负指数)。
   * 返回根索引;失败 −1 且 _err 已置(隐通道映射 1/2,主通道 1..4)。
   * 栈元素 = 节点索引 | 树深<<16(depth ≤ 48 < 2^16)。 */
  function buildFromRPN(rpn, len, numSrc, implicit) {
    var sp = 0;
    var xvar = -1;
    var yvar = -1;
    var i = 0;
    var w = 0;
    var t = 0;
    var p = 0;
    var a = 0;
    var b = 0;
    var da = 0;
    var db = 0;
    var d = 0;
    var nd = 0;
    var eCap = implicit !== 0 ? 2 : ST_EBUDGET;      /* 容量类错误码映射 */
    var pCap = implicit !== 0 ? 1 : ST_EPARSE;       /* 结构类错误码映射 */

    for (i = 0; i < len; i++) {
      w = rpn[i];
      t = w >> 8;
      p = w & 255;

      if (t === 1) {                                  /* T_NUM */
        if (p >= numSrc.length) { _err = pCap; return -1; }
        nd = symConst(numSrc[p]);
        if (nd < 0) { _err = eCap; return -1; }
        if (sp >= _buildStack.length) { _err = eCap; return -1; }
        _buildStack[sp] = nd;
        sp++;
        continue;
      }
      if (t === 2) {                                  /* T_VAR */
        if (implicit !== 0) {
          if (p === 0) {
            if (xvar < 0) { xvar = symVar(0); }
            nd = xvar;
          } else if (p === 1) {
            if (yvar < 0) { yvar = symVar(1); }
            nd = yvar;
          } else { _err = pCap; return -1; }
        } else {
          if (xvar < 0) { xvar = symVar(0); }
          nd = xvar;
        }
        if (nd < 0) { _err = eCap; return -1; }
        if (sp >= _buildStack.length) { _err = eCap; return -1; }
        _buildStack[sp] = nd;
        sp++;
        continue;
      }
      if (t === 3 && implicit === 0) {                /* T_CONST:主通道 e/π */
        nd = symConst(p === 1 ? Math.PI : Math.E);
        if (nd < 0) { _err = eCap; return -1; }
        if (sp >= _buildStack.length) { _err = eCap; return -1; }
        _buildStack[sp] = nd;
        sp++;
        continue;
      }
      if (t === 10) {                                 /* T_UMINUS → 0−a */
        if (sp < 1) { _err = pCap; return -1; }
        sp--;
        a = _buildStack[sp] & 65535;
        d = (_buildStack[sp] >> 16) + 1;
        if (d > ST_MAX_DEPTH) { _err = implicit !== 0 ? 2 : ST_EDEPTH; return -1; }
        nd = symSub(symConst(0), a);
        if (nd < 0) { _err = eCap; return -1; }
        if (sp >= _buildStack.length) { _err = eCap; return -1; }
        _buildStack[sp] = nd | (d << 16);
        sp++;
        continue;
      }
      if (t === 4) {                                  /* T_OP */
        if (sp < 2) { _err = pCap; return -1; }
        sp--;
        db = _buildStack[sp];
        b = db & 65535;
        sp--;
        da = _buildStack[sp];
        a = da & 65535;
        d = ((da >> 16) > (db >> 16) ? (da >> 16) : (db >> 16)) + 1;
        if (d > ST_MAX_DEPTH) { _err = implicit !== 0 ? 2 : ST_EDEPTH; return -1; }
        if (p === 0) { nd = symAdd(a, b); }
        else if (p === 1) { nd = symSub(a, b); }
        else if (p === 2) { nd = symMul(a, b); }
        else if (p === 3) {
          if (implicit !== 0) { _err = 1; return -1; }   /* C A6:无除法节点 */
          nd = symDiv(a, b);
        } else {                                       /* OP_POW */
          if (implicit !== 0) {
            /* C A6:指数必须是非负整数常量 */
            if (_type(b) !== ST_CONST) { _err = 1; return -1; }
            if (_cval(b) < 0 || _cval(b) !== Math.floor(_cval(b))) { _err = 1; return -1; }
            nd = symPow(a, b);
          } else {
            nd = symPow(a, b);
          }
        }
        if (nd < 0) { _err = eCap; return -1; }
        if (sp >= _buildStack.length) { _err = eCap; return -1; }
        _buildStack[sp] = nd | (d << 16);
        sp++;
        continue;
      }
      if (t === 7 && implicit === 0) {                /* T_FUNC */
        if (p === 7) { _err = ST_EPARSE; return -1; }  /* F_ATAN2 双参拒绝 */
        if (sp < 1) { _err = ST_EPARSE; return -1; }
        sp--;
        da = _buildStack[sp];
        a = da & 65535;
        d = (da >> 16) + 1;
        if (d > ST_MAX_DEPTH) { _err = ST_EDEPTH; return -1; }
        nd = symFun(p, a);
        if (nd < 0) { _err = ST_EBUDGET; return -1; }
        if (sp >= _buildStack.length) { _err = ST_EBUDGET; return -1; }
        _buildStack[sp] = nd | (d << 16);
        sp++;
        continue;
      }
      _err = pCap;                                    /* 其余 token 不入树 */
      return -1;
    }
    if (sp !== 1) { _err = pCap; return -1; }
    d = _buildStack[0] >> 16;
    if (d > ST_MAX_DEPTH) { _err = implicit !== 0 ? 2 : ST_EDEPTH; return -1; }
    return _buildStack[0] & 65535;
  }

  /* 主通道入口:build */

  function build(src) {
    var rc = 0;
    var n = 0;
    var root = -1;

    _err = ST_OK;
    _root = -1;
    _droot = -1;
    _view = -1;
    _facK = 0;
    _remDeg = 0;
    _srcDepth = 0;
    _srcCount = 0;

    rc = parseExpression(src);
    if (rc !== 0) { _err = ST_EPARSE; return _err; }

    /* 联动第 1 条:preCheck 先行(括号深 ≤6 / 函数 ≤4 / 变量 ≤10) */
    if (preCheckExpression() === 1) { _err = ST_EGUARD; return _err; }

    _evalError = 0;
    n = toRPN(tokenPool, tokenCount, _rpnToken);
    if (_evalError !== 0 || n <= 0) { _err = ST_EPARSE; return _err; }

    _count = 0;
    root = buildFromRPN(_rpnToken, n, numberPool, 0);
    if (root < 0) { _count = 0; return _err; }
    if (_count > ST_MAX_TREE_NODES) {
      /* 联动第 2 条:源树节点上限(导数预算 4n+8 = 408 < 512 前置可证) */
      _count = 0;
      _root = -1;
      _err = ST_EBUDGET;
      return _err;
    }

    _root = root;
    _srcCount = _count;
    _srcDepth = _buildStack[0] >> 16;
    _view = root;
    return ST_OK;
  }

  /* 符号求导(显式栈后序,C ac_sym_derivative 同款) */

  function diff() {
    var sp = 0;
    var budget = 0;
    var need = 0;
    var i = 0;
    var nd = 0;
    var n = 0;
    var t = 0;
    var d = -1;

    _err = ST_OK;
    if (_root < 0) { _err = ST_EPARSE; return _err; }

    /* 前置预算(C 同式):导数节点 ≤ 4×源树 + 8 */
    budget = ST_MAX_NODES - _count;
    need = (_count - _root) * 4 + 8;
    if (budget < need) { _err = ST_EBUDGET; return _err; }

    for (i = 0; i < _count; i++) { _dmap[i] = -1; }

    _walk[sp] = _root;
    sp++;
    while (sp > 0) {
      sp--;
      nd = _walk[sp];
      if (nd >= ST_MARK) {
        /* 后序:子节点导数已就绪 */
        n = nd - ST_MARK;
        t = _type(n);
        if (t === ST_CONST) {
          d = symConst(0);
        } else if (t === ST_VAR) {
          d = symConst(_pool[n * 4 + 3] === 0 ? 1 : 0);
        } else if (t === ST_ADD) {
          d = dmake_add(_dmap[_left(n)], _dmap[_right(n)]);
        } else if (t === ST_SUB) {
          d = dmake_sub(_dmap[_left(n)], _dmap[_right(n)]);
        } else if (t === ST_MUL) {
          d = dmake_add(dmake_mul(_dmap[_left(n)], _right(n)),
                        dmake_mul(_left(n), _dmap[_right(n)]));
        } else if (t === ST_DIV) {
          d = dmake_quot(_dmap[_left(n)], _left(n), _right(n), _dmap[_right(n)]);
        } else if (t === ST_POW) {
          d = dmake_pow(n);
        } else if (t === ST_FUN) {
          d = dmake_fun(n);
        } else {
          d = -1;
        }
        if (d < 0) { _err = ST_EBUDGET; return _err; }
        _dmap[n] = d;
      } else {
        /* 先序:压标记 + 子节点(POW 常指数只导基) */
        t = _type(nd);
        if (sp + 3 >= ST_WALK_CAP) { _err = ST_EBUDGET; return _err; }
        _walk[sp] = nd + ST_MARK;
        sp++;
        if (t === ST_ADD || t === ST_SUB || t === ST_MUL || t === ST_DIV) {
          _walk[sp] = _left(nd); sp++;
          _walk[sp] = _right(nd); sp++;
        } else if (t === ST_POW) {
          _walk[sp] = _left(nd); sp++;
          if (_type(_right(nd)) !== ST_CONST) { _walk[sp] = _right(nd); sp++; }
        } else if (t === ST_FUN) {
          _walk[sp] = _left(nd); sp++;
        }
      }
    }
    _droot = _dmap[_root];
    _view = _droot;
    return ST_OK;
  }

  /* 幂法则三分支(n = 原节点) */
  function dmake_pow(n) {
    var base = _left(n);
    var expo = _right(n);
    var du = _dmap[base];
    var dv = _dmap[expo];
    var nv = 0;
    var pw = 0;
    var inner = 0;
    var cn = 0;
    var lnC = 0;
    var t1 = 0;
    var t2 = 0;

    if (du < 0) { return -1; }
    if (_type(expo) === ST_CONST) {
      /* d(u^n) = n·u^(n−1)·du(u 取原始基节点索引,C 同款) */
      nv = _cval(expo);
      pw = symPowInt(base, nv - 1);
      if (pw < 0) { return -1; }
      inner = dmake_mul(pw, du);
      if (inner < 0) { return -1; }
      cn = symConst(nv);
      if (cn < 0) { return -1; }
      return dmake_mul(cn, inner);
    }
    if (_type(base) === ST_CONST) {
      /* d(c^v) = c^v·ln(c)·dv;e 特判 ln e = 1
       * (此处指数必为非常数节点,dv = _dmap[expo] 已由遍历就绪) */
      if (dv < 0) { return -1; }
      if (_cval(base) === Math.E) { return dmake_mul(n, dv); }
      lnC = symConst(Math.log(_cval(base)));
      if (lnC < 0) { return -1; }
      t1 = dmake_mul(n, lnC);
      if (t1 < 0) { return -1; }
      return dmake_mul(t1, dv);
    }
    /* d(u^v) = u^v·(dv·ln(u) + v·du/u)(复用原 POW 节点) */
    t1 = dmake_mul(_dmap[expo], symFun(9, base));            /* 9 = F_LN */
    t2 = dmake_div2(dmake_mul(expo, du), base);
    if (t1 < 0 || t2 < 0) { return -1; }
    inner = dmake_add(t1, t2);
    if (inner < 0) { return -1; }
    return dmake_mul(n, inner);
  }

  /* 17 种单参函数求导规则(u = 原实参节点,du = 其导数) */
  function dmake_fun(n) {
    var f = _pool[n * 4 + 3];
    var u = _left(n);
    var du = _dmap[u];
    var t = 0;
    var t1 = 0;
    var t2 = 0;

    if (du < 0) { return -1; }
    if (f === 1) {                                            /* sin: du·cos(u) */
      t = symFun(2, u);
      return t < 0 ? -1 : dmake_mul(du, t);
    }
    if (f === 2) {                                            /* cos: −du·sin(u) */
      t = symFun(1, u);
      if (t < 0) { return -1; }
      t1 = dmake_mul(du, t);
      return t1 < 0 ? -1 : dmake_neg(t1);
    }
    if (f === 3) {                                            /* tan: du/cos²(u) */
      t = symPowInt(symFun(2, u), 2);
      return t < 0 ? -1 : dmake_div2(du, t);
    }
    if (f === 11) {                                           /* exp: du·exp(u) */
      t = symFun(11, u);
      return t < 0 ? -1 : dmake_mul(du, t);
    }
    if (f === 9) {                                            /* ln: du/u */
      return dmake_div2(du, u);
    }
    if (f === 10) {                                           /* log10: du/(u·ln10) */
      t = dmake_mul(u, symConst(Math.LN10));
      return t < 0 ? -1 : dmake_div2(du, t);
    }
    if (f === 8) {                                            /* sqrt: du/(2·sqrt(u)) */
      t = dmake_mul(symConst(2), symFun(8, u));
      return t < 0 ? -1 : dmake_div2(du, t);
    }
    if (f === 12) {                                           /* abs: u·du/abs(u) */
      t = dmake_mul(u, du);
      if (t < 0) { return -1; }
      t1 = symFun(12, u);
      return t1 < 0 ? -1 : dmake_div2(t, t1);
    }
    if (f === 13) {                                           /* sinh: du·cosh(u) */
      t = symFun(14, u);
      return t < 0 ? -1 : dmake_mul(du, t);
    }
    if (f === 14) {                                           /* cosh: du·sinh(u) */
      t = symFun(13, u);
      return t < 0 ? -1 : dmake_mul(du, t);
    }
    if (f === 15) {                                           /* tanh: du/cosh²(u) */
      t = symPowInt(symFun(14, u), 2);
      return t < 0 ? -1 : dmake_div2(du, t);
    }
    if (f === 4) {                                            /* asin: du/sqrt(1−u²) */
      t = dmake_sub(symConst(1), symPowInt(u, 2));
      if (t < 0) { return -1; }
      t1 = symFun(8, t);
      return t1 < 0 ? -1 : dmake_div2(du, t1);
    }
    if (f === 5) {                                            /* acos: −du/sqrt(1−u²) */
      t = dmake_sub(symConst(1), symPowInt(u, 2));
      if (t < 0) { return -1; }
      t1 = symFun(8, t);
      if (t1 < 0) { return -1; }
      t2 = dmake_div2(du, t1);
      return t2 < 0 ? -1 : dmake_neg(t2);
    }
    if (f === 6) {                                            /* atan: du/(1+u²) */
      t = dmake_add(symConst(1), symPowInt(u, 2));
      return t < 0 ? -1 : dmake_div2(du, t);
    }
    if (f === 16) {                                           /* asinh: du/sqrt(u²+1) */
      t = dmake_add(symPowInt(u, 2), symConst(1));
      if (t < 0) { return -1; }
      t1 = symFun(8, t);
      return t1 < 0 ? -1 : dmake_div2(du, t1);
    }
    if (f === 17) {                                           /* acosh: du/sqrt(u²−1) */
      t = dmake_sub(symPowInt(u, 2), symConst(1));
      if (t < 0) { return -1; }
      t1 = symFun(8, t);
      return t1 < 0 ? -1 : dmake_div2(du, t1);
    }
    if (f === 18) {                                           /* atanh: du/(1−u²) */
      t = dmake_sub(symConst(1), symPowInt(u, 2));
      return t < 0 ? -1 : dmake_div2(du, t);
    }
    return -1;
  }

  /* 求值(显式栈后序,C ac_sym_eval 同款)
   * VAR 标签 0 → xv,1 → yv(主通道标签恒 0)。返回 NaN = 失败。 */

  function evalNode(root, xv, yv) {
    var sp = 0;
    var vsp = 0;
    var nd = 0;
    var n = 0;
    var t = 0;
    var a = 0.0;
    var b = 0.0;

    if (root < 0 || root >= _count) { return NaN; }
    _walk[sp] = root;
    sp++;
    while (sp > 0) {
      sp--;
      nd = _walk[sp];
      if (nd >= ST_MARK) {
        n = nd - ST_MARK;
        t = _type(n);
        if (t === ST_CONST) {
          if (vsp >= _valStack.length) { return NaN; }
          _valStack[vsp] = _cval(n);
          vsp++;
        } else if (t === ST_VAR) {
          if (vsp >= _valStack.length) { return NaN; }
          _valStack[vsp] = _pool[n * 4 + 3] === 0 ? xv : yv;
          vsp++;
        } else if (t === ST_ADD || t === ST_SUB || t === ST_MUL ||
                   t === ST_DIV || t === ST_POW) {
          if (vsp < 2) { return NaN; }
          b = _valStack[--vsp];
          a = _valStack[--vsp];
          if (t === ST_ADD) { a = a + b; }
          else if (t === ST_SUB) { a = a - b; }
          else if (t === ST_MUL) { a = a * b; }
          else if (t === ST_DIV) { a = a / b; }
          else { a = Math.pow(a, b); }
          if (a !== a) { return NaN; }
          if (vsp >= _valStack.length) { return NaN; }
          _valStack[vsp] = a;
          vsp++;
        } else if (t === ST_FUN) {
          if (vsp < 1) { return NaN; }
          a = _valStack[--vsp];
          a = applyFunction(_pool[n * 4 + 3], a, 0);
          if (a !== a) { return NaN; }
          if (vsp >= _valStack.length) { return NaN; }
          _valStack[vsp] = a;
          vsp++;
        } else {
          return NaN;
        }
      } else {
        t = _type(nd);
        if (sp + 3 >= ST_WALK_CAP) { return NaN; }
        _walk[sp] = nd + ST_MARK;
        sp++;
        if (t === ST_ADD || t === ST_SUB || t === ST_MUL ||
            t === ST_DIV || t === ST_POW) {
          _walk[sp] = _right(nd); sp++;
          _walk[sp] = _left(nd); sp++;
        } else if (t === ST_FUN) {
          _walk[sp] = _left(nd); sp++;
        }
      }
    }
    if (vsp !== 1) { return NaN; }
    return _valStack[0];
  }

  function evalTree(x) { _err = ST_OK; return evalNode(_root, x, 0.0); }
  function evalDiff(x) { _err = ST_OK; return evalNode(_droot, x, 0.0); }

  /* 规范字符串渲染(显式栈,优先级括号化)
   * 优先级:ADD/SUB=1、MUL/DIV/SUB(0,x)负形式=2、POW=3、原子/FUN=4;
   * ADD 右侧负形式合并 '+−'→'−'、MUL 右侧 DIV 摊平 a*b/c、SUB(0,x)
   * 渲染 '-child'、SUB(0,SUB(0,x)) 双重否定消去(C integrate
   * pro_int_emit 同款规则)。栈元素:idx|mp<<16(节点)或
   * 0x10000|char(字面字符);左括号立即输出,右括号入口先压栈
   * (LIFO 保证最后发出)。溢出 → 空串 + _err = ST_EOVER。 */

  function _prec(i) {
    var t = _type(i);
    if (t === ST_ADD) { return 1; }
    if (t === ST_SUB) { return _isSub0(i) ? 2 : 1; }
    if (t === ST_MUL || t === ST_DIV) { return 2; }
    if (t === ST_POW) { return 3; }
    return 4;
  }

  function _pushCC(c) {
    if (_outLen < ST_OUT_CAP) { _outBuf[_outLen] = c; }
    _outLen++;
  }
  function _pushInt(v) {
    var nd = 0;
    var m = v;
    if (m < 0) { _pushCC(45); m = -m; }               /* '-' */
    if (m === 0) { _pushCC(48); return; }             /* '0' */
    while (m > 0) {
      _workI[nd] = m % 10;
      nd++;
      m = (m - (m % 10)) / 10;
    }
    while (nd > 0) { nd--; _pushCC(48 + _workI[nd]); }
  }
  function _pushNum(v) {
    if (v === Math.PI) { _pushStr("pi"); return; }
    if (v === Math.E) { _pushStr("e"); return; }
    if (v === Math.floor(v) && v > -1e15 && v < 1e15) { _pushInt(v); return; }
    _pushStr("" + v);
  }
  function _pushStr(s) {
    var k = 0;
    for (k = 0; k < s.length; k++) { _pushCC(s.charCodeAt(k)); }
  }

  function render(root) {
    var sp = 0;
    var e = 0;
    var nd = 0;
    var mp = 0;
    var paren = 0;
    var t = 0;
    var r = 0;
    var rp = 0;
    var name = "";
    var k = 0;

    _outLen = 0;
    _err = ST_OK;
    if (root < 0 || root >= _count) { _err = ST_EPARSE; return ""; }

    _renderStack[sp] = root;                          /* mp = 0:顶层无括号 */
    sp++;
    while (sp > 0) {
      sp--;
      e = _renderStack[sp];
      if (e < 0) { _pushCC(-e - 1); continue; }       /* 字面字符(负域) */
      nd = e & 65535;
      mp = e >> 16;
      paren = (_prec(nd) < mp) ? 1 : 0;
      if (paren !== 0) { _pushCC(40); }               /* '(' 立即 */
      if (paren !== 0) { _renderStack[sp] = -42; sp++; }  /* ')' 压底 */
      if (sp + 8 >= _renderStack.length) { _err = ST_EOVER; return ""; }
      t = _type(nd);
      if (t === ST_CONST) {
        _pushNum(_cval(nd));
      } else if (t === ST_VAR) {
        _pushCC(120);                                 /* 'x' */
      } else if (t === ST_ADD || t === ST_SUB) {
        r = _right(nd);
        if (t === ST_SUB && _isConstV(_left(nd), 0)) {
          /* 负形式 -child;双重否定消去 */
          if (_isSub0(r)) {
            _renderStack[sp] = (_right(r) & 65535) | (2 << 16); sp++;
          } else {
            _renderStack[sp] = (r & 65535) | (2 << 16); sp++;
            _renderStack[sp] = -46; sp++;      /* '-' */
          }
        } else if (t === ST_ADD && _isSub0(r)) {
          /* a + (0−b) → a−b:出栈序 左/'−'/b 的右子 */
          _renderStack[sp] = (_right(r) & 65535) | (2 << 16); sp++;
          _renderStack[sp] = -46; sp++;        /* '-' */
          _renderStack[sp] = (_left(nd) & 65535) | (1 << 16); sp++;
        } else {
          _renderStack[sp] = (r & 65535) | (2 << 16); sp++;
          _renderStack[sp] = (t === ST_ADD ? -44 : -46); sp++;
          _renderStack[sp] = (_left(nd) & 65535) | (1 << 16); sp++;
        }
      } else if (t === ST_MUL) {
        r = _right(nd);
        if (_type(r) === ST_DIV) {
          /* a*(b/c) 摊平 a*b/c:出栈序 左/'*'/b.a/'/'/b.b */
          _renderStack[sp] = (_right(r) & 65535) | (3 << 16); sp++;
          _renderStack[sp] = -48; sp++;        /* '/' */
          _renderStack[sp] = (_left(r) & 65535) | (2 << 16); sp++;
        } else {
          rp = (_type(r) === ST_MUL) ? 2 : 3;
          _renderStack[sp] = (r & 65535) | (rp << 16); sp++;
        }
        _renderStack[sp] = -43; sp++;          /* '*' */
        _renderStack[sp] = (_left(nd) & 65535) | (2 << 16); sp++;
      } else if (t === ST_DIV) {
        _renderStack[sp] = (_right(nd) & 65535) | (3 << 16); sp++;
        _renderStack[sp] = -48; sp++;          /* '/' */
        _renderStack[sp] = (_left(nd) & 65535) | (2 << 16); sp++;
      } else if (t === ST_POW) {
        _renderStack[sp] = (_right(nd) & 65535) | (4 << 16); sp++;
        _renderStack[sp] = -95; sp++;          /* '^' */
        _renderStack[sp] = (_left(nd) & 65535) | (4 << 16); sp++;
      } else if (t === ST_FUN) {
        name = funcName(_pool[nd * 4 + 3]);
        _renderStack[sp] = -42; sp++;          /* ')' */
        _renderStack[sp] = (_left(nd) & 65535) | (1 << 16); sp++;
        _renderStack[sp] = -41; sp++;          /* '(' */
        for (k = name.length - 1; k >= 0; k--) {
          _renderStack[sp] = -(name.charCodeAt(k) + 1);
          sp++;
        }
      } else {
        _err = ST_EPARSE;
        return "";
      }
    }
    if (_outLen > ST_OUT_CAP) { _err = ST_EOVER; return ""; }
    var s = "";
    var k2 = 0;
    for (k2 = 0; k2 < _outLen; k2++) { s = s + String.fromCharCode(_outBuf[k2]); }
    return s;
  }

  function toString() {
    _err = ST_OK;
    if (_view < 0 || _view >= _count) { _err = ST_EPARSE; return ""; }
    return render(_view);
  }

  /* factor:多项式子树 → 因式列表 */

  /* 两趟标记:clean(多项式干净子树)入 _dmap(与求导互斥,复用),
   * hasVar 入 _hasVar。显式栈后序;返回根是否 clean。 */
  function markPoly(root) {
    var sp = 0;
    var nd = 0;
    var n = 0;
    var t = 0;
    var ok = 0;
    var hv = 0;
    var en = 0.0;

    _walk[sp] = root;
    sp++;
    while (sp > 0) {
      sp--;
      nd = _walk[sp];
      if (nd >= ST_MARK) {
        n = nd - ST_MARK;
        t = _type(n);
        if (t === ST_CONST) { ok = 1; hv = 0; }
        else if (t === ST_VAR) { ok = 1; hv = 1; }
        else if (t === ST_ADD || t === ST_SUB || t === ST_MUL) {
          ok = (_dmap[_left(n)] === 1 && _dmap[_right(n)] === 1) ? 1 : 0;
          hv = (_hasVar[_left(n)] + _hasVar[_right(n)] > 0) ? 1 : 0;
        } else if (t === ST_DIV) {
          ok = (_dmap[_left(n)] === 1 && _type(_right(n)) === ST_CONST &&
                _cval(_right(n)) !== 0) ? 1 : 0;
          hv = _hasVar[_left(n)];
        } else if (t === ST_POW) {
          en = _cval(_right(n));
          ok = (_dmap[_left(n)] === 1 && _type(_right(n)) === ST_CONST &&
                en === Math.floor(en) && en >= 2 && en <= 7) ? 1 : 0;
          hv = _hasVar[_left(n)];
        } else { ok = 0; hv = 0; }
        _dmap[n] = ok;
        _hasVar[n] = hv;
      } else {
        t = _type(nd);
        if (sp + 3 >= ST_WALK_CAP) { return 0; }
        _walk[sp] = nd + ST_MARK;
        sp++;
        if (t === ST_ADD || t === ST_SUB || t === ST_MUL || t === ST_DIV ||
            t === ST_POW) {
          _walk[sp] = _right(nd); sp++;
          _walk[sp] = _left(nd); sp++;
        } else if (t === ST_FUN) {
          _walk[sp] = _left(nd); sp++;
        }
      }
    }
    return _dmap[root];
  }

  /* 升幂系数提取:clean 树 → _polyC[0..7];失败 0。MUL/DIV 的无变量
   * 侧经 evalNode 折叠为标量乘子(乘子可为负/分数,符号在乘子里)。 */
  function fetchPoly(root) {
    var sp = 0;
    var k = 0;
    var nd = 0;
    var m = 1.0;
    var sc = 0.0;
    var t = 0;

    for (k = 0; k < 8; k++) { _polyC[k] = 0.0; }
    _fetchN[sp] = root;
    _fetchM[sp] = 1.0;
    sp++;
    while (sp > 0) {
      if (sp + 2 > _fetchN.length) { return 0; }
      sp--;
      nd = _fetchN[sp];
      m = _fetchM[sp];
      t = _type(nd);
      if (t === ST_CONST) {
        _polyC[0] += m * _cval(nd);
      } else if (t === ST_VAR) {
        _polyC[1] += m;
      } else if (t === ST_ADD) {
        _fetchN[sp] = _left(nd); _fetchM[sp] = m; sp++;
        _fetchN[sp] = _right(nd); _fetchM[sp] = m; sp++;
      } else if (t === ST_SUB) {
        if (_isSub0(nd)) {
          _fetchN[sp] = _right(nd); _fetchM[sp] = -m; sp++;
        } else {
          _fetchN[sp] = _left(nd); _fetchM[sp] = m; sp++;
          _fetchN[sp] = _right(nd); _fetchM[sp] = -m; sp++;
        }
      } else if (t === ST_MUL) {
        if (_hasVar[_left(nd)] === 0 && _hasVar[_right(nd)] === 0) {
          sc = evalNode(_left(nd), 0.0, 0.0) * evalNode(_right(nd), 0.0, 0.0);
          if (sc !== sc) { return 0; }
          _polyC[0] += m * sc;
        } else if (_hasVar[_left(nd)] === 0) {
          sc = evalNode(_left(nd), 0.0, 0.0);
          if (sc !== sc) { return 0; }
          _fetchN[sp] = _right(nd); _fetchM[sp] = m * sc; sp++;
        } else if (_hasVar[_right(nd)] === 0) {
          sc = evalNode(_right(nd), 0.0, 0.0);
          if (sc !== sc) { return 0; }
          _fetchN[sp] = _left(nd); _fetchM[sp] = m * sc; sp++;
        } else { return 0; }
      } else if (t === ST_DIV) {
        if (_hasVar[_right(nd)] !== 0) { return 0; }
        sc = _cval(_right(nd));
        if (sc === 0) { return 0; }
        _fetchN[sp] = _left(nd); _fetchM[sp] = m / sc; sp++;
      } else if (t === ST_POW) {
        sc = _cval(_right(nd));
        if (sc < 1 || sc > 7 || sc !== Math.floor(sc)) { return 0; }
        _polyC[sc] += m;
      } else { return 0; }
    }
    return 1;
  }

  /* 主入口:当前源树须为 x 的多项式(次数 1..4)。返回 0 = 成功
   * (getFactor* 访问器读结果),1 = 拒绝(非多项式/次数越域)。 */
  function factor() {
    var k = 0;
    var i = 0;
    var deg = -1;
    var dd = 0;

    _err = ST_OK;
    if (_root < 0) { _err = ST_EPARSE; return _err; }
    _facK = 0;
    _remDeg = 0;

    if (markPoly(_root) !== 1) { _err = 1; return _err; }
    if (fetchPoly(_root) !== 1) { _err = 1; return _err; }

    for (i = 5; i < 8; i++) {
      if (_polyC[i] !== 0.0) { _err = 1; return _err; }   /* 次数 > 4 */
    }
    for (i = 7; i >= 0; i--) {
      if (_polyC[i] !== 0.0) { deg = i; break; }
    }
    if (deg < 1) { _err = 1; return _err; }               /* 非零常数/零 */

    for (i = 0; i < 5; i++) { _coeffs[i] = i <= deg ? _polyC[i] : 0.0; }
    k = ratRootDescent(deg);            /* 会改写 _coeffs(C 同款副作用) */

    _facK = k;
    for (i = 0; i < k; i++) {
      exactSetFraction(_ratRootNum[i], _ratRootDen[i]);
      _facRat[i] = exactRat;
      _facRadNum[i] = exactRadNum;
      _facRad[i] = exactRad;
      _facDen[i] = exactDen;
      _facExp[i] = exactExp;
    }
    dd = deg - k;
    _remDeg = dd;
    for (i = 0; i < 8; i++) { _remCoeffs[i] = 0.0; }
    if (dd === 0) { _remCoeffs[0] = 1.0; }              /* C 完全分解约定 */
    else { for (i = 0; i <= dd; i++) { _remCoeffs[i] = _coeffs[i]; } }
    return ST_OK;
  }

  function getFactorCount() { return _facK; }
  function getFactorRat(i) { return _facRat[i]; }
  function getFactorRadNum(i) { return _facRadNum[i]; }
  function getFactorRad(i) { return _facRad[i]; }
  function getFactorDen(i) { return _facDen[i]; }
  function getFactorExp(i) { return _facExp[i]; }
  function getRemainderDegree() { return _remDeg; }
  function getRemainderCoeff(i) { return _remCoeffs[i]; }

  /* 剩余多项式渲染(降幂;整系数走 pushInt,非整系数走最短十进制) */
  function _pushRemPoly() {
    var k = 0;
    var c = 0.0;
    var mag = 0.0;
    var ci = 0;
    var first = 1;
    for (k = _remDeg; k >= 0; k--) {
      c = _remCoeffs[k];
      if (c === 0) { continue; }
      mag = c < 0 ? -c : c;
      ci = (mag === Math.floor(mag) && mag < 1e15) ? 1 : 0;
      if (first === 0) { _pushCC(c < 0 ? 45 : 43); }    /* '-' / '+' */
      else if (c < 0) { _pushCC(45); }
      if (mag !== 1 || k === 0) {
        if (ci === 1) { _pushInt(mag); }
        else { _pushStr("" + mag); }
        if (k > 0) { _pushCC(42); }                     /* '*' */
      }
      if (k > 0) {
        _pushCC(120);                                   /* 'x' */
        if (k >= 2) { _pushCC(94); _pushInt(k); }       /* '^' + 次数 */
      }
      first = 0;
    }
    if (first === 1) { _pushCC(48); }                   /* 全零 → "0"(防御) */
  }

  /* 因式列表渲染:"(x-3)*(x-2)*(x^2+1)" 形态;k=0 时仅剩余多项式
   * (带括号);dd=0(完全分解)无剩余段;根 p/q:q≠1 渲染 (x-p/q)。 */
  function factorToString() {
    var i = 0;
    var p = 0;
    var q = 0;
    var s = "";
    var k2 = 0;

    _outLen = 0;
    _err = ST_OK;
    if (_root < 0) { _err = ST_EPARSE; return ""; }
    for (i = 0; i < _facK; i++) {
      if (i > 0) { _pushCC(42); }                       /* '*' */
      _pushCC(40);                                      /* '(' */
      _pushCC(120);                                     /* 'x' */
      p = _facRat[i];
      q = _facDen[i];
      if (q < 0) { p = -p; q = -q; }
      if (p < 0) { _pushCC(43); p = -p; }               /* (x+r) */
      else { _pushCC(45); }                             /* (x-r) */
      if (q !== 1) { _pushInt(p); _pushCC(47); _pushInt(q); }  /* p/q */
      else { _pushInt(p); }
      _pushCC(41);                                      /* ')' */
    }
    if (_remDeg >= 1) {
      if (_facK > 0) { _pushCC(42); }                   /* '*'(有因式时) */
      _pushCC(40);                                      /* '(' */
      _pushRemPoly();
      _pushCC(41);                                      /* ')' */
    }
    if (_outLen > ST_OUT_CAP) { _err = ST_EOVER; return ""; }
    for (k2 = 0; k2 < _outLen; k2++) { s = s + String.fromCharCode(_outBuf[k2]); }
    return s;
  }

  /* 隐函数 dy/dx = −F_x/F_y(A6 镜像)
   * 独立词法器(小写 x/y、整常量、+−*×/÷^、括号、一元负号、隐式乘法;
   * 函数名/小数/其它字母一律 EPARSE —— C pro_lex 逐条对齐)。
   * 返回值即错误码:0 OK / 1 EPARSE / 2 EBUDGET / 3 EFLAT / 4 EEVAL;
   * 失败时 _dydx 保持调用前值(C:输出不被改写)。
   * 自毁主树会话(_root/_view 失效,与 C A6 复用 g_sym_nodes 同款)。 */

  function implicitDyDx(expr, xv, yv) {
    var i = 0;
    var len = 0;
    var cc = 0;
    var prev = -1;          /* −1 起始;其余 = token type */
    var tokN = 0;
    var numN = 0;
    var v = 0.0;
    var n = 0;
    var root = -1;
    var fx = -1;
    var fy = -1;
    var vx = 0.0;
    var vy = 0.0;
    var av = 0.0;
    var scale = 0.0;
    var sawY = 0;

    if (xv !== xv || xv === Infinity || xv === -Infinity ||
        yv !== yv || yv === Infinity || yv === -Infinity) { return 4; }

    /* 会话复位:numberPool 归零(自毁主树会话的 CONST 引用,与 C A6
     * 复用 g_sym_nodes 同款;否则多次隐函数调用会累积撑爆 256 槽) */
    numberCount = 0;

    len = expr.length;
    for (i = 0; i < len; i++) {
      cc = expr.charCodeAt(i);
      if (cc === 32 || cc === 9 || cc === 13 || cc === 10) { continue; }
      if (cc >= 48 && cc <= 57) {                     /* 整常量 */
        v = 0.0;
        while (i < len) {
          cc = expr.charCodeAt(i);
          if (cc < 48 || cc > 57) { break; }
          v = v * 10 + (cc - 48);
          i++;
        }
        if (i < len && expr.charCodeAt(i) === 46) { return 1; }   /* '.' 拒 */
        i--;
        if (numN >= _impNum.length) { return 2; }
        _impNum[numN] = v;
        if (tokN >= ST_IMP_TOK_MAX) { return 2; }
        _impTok[tokN] = (1 << 8) | numN;              /* T_NUM */
        tokN++;
        numN++;
        prev = 1;
        continue;
      }
      if (cc === 120) {                               /* 'x' → 标签 0 */
        if (prev === 1 || prev === 2 || prev === 6) {
          if (tokN >= ST_IMP_TOK_MAX) { return 2; }
          _impTok[tokN] = (4 << 8) | 2;               /* 隐式乘 */
          tokN++;
        }
        if (tokN >= ST_IMP_TOK_MAX) { return 2; }
        _impTok[tokN] = (2 << 8) | 0;                 /* T_VAR tag 0 */
        tokN++;
        prev = 2;
        continue;
      }
      if (cc === 121) {                               /* 'y' → 标签 1 */
        if (prev === 1 || prev === 2 || prev === 6) {
          if (tokN >= ST_IMP_TOK_MAX) { return 2; }
          _impTok[tokN] = (4 << 8) | 2;
          tokN++;
        }
        if (tokN >= ST_IMP_TOK_MAX) { return 2; }
        _impTok[tokN] = (2 << 8) | 1;                 /* T_VAR tag 1 */
        tokN++;
        prev = 2;
        sawY = 1;
        continue;
      }
      if ((cc >= 97 && cc <= 122) || (cc >= 65 && cc <= 90)) { return 1; }
      if (cc === 40) {                                /* '(' */
        if (prev === 1 || prev === 2 || prev === 6) {
          if (tokN >= ST_IMP_TOK_MAX) { return 2; }
          _impTok[tokN] = (4 << 8) | 2;
          tokN++;
        }
        if (tokN >= ST_IMP_TOK_MAX) { return 2; }
        _impTok[tokN] = 5 << 8;                       /* T_LP */
        tokN++;
        prev = 5;
        continue;
      }
      if (cc === 41) {                                /* ')' */
        if (tokN >= ST_IMP_TOK_MAX) { return 2; }
        _impTok[tokN] = 6 << 8;                       /* T_RP */
        tokN++;
        prev = 6;
        continue;
      }
      if (cc === 43) {                                /* '+' */
        if (tokN >= ST_IMP_TOK_MAX) { return 2; }
        _impTok[tokN] = (4 << 8) | 0;                 /* OP_ADD */
        tokN++;
        prev = 4;
        continue;
      }
      if (cc === 45 || cc === 8722) {                 /* '-' / '−' */
        if (prev === -1 || prev === 5 || prev === 4 || prev === 10) {
          if (tokN >= ST_IMP_TOK_MAX) { return 2; }
          _impTok[tokN] = 10 << 8;                    /* T_UMINUS */
          tokN++;
          prev = 10;
        } else {
          if (tokN >= ST_IMP_TOK_MAX) { return 2; }
          _impTok[tokN] = (4 << 8) | 1;               /* OP_SUB */
          tokN++;
          prev = 4;
        }
        continue;
      }
      if (cc === 42 || cc === 215) {                  /* '*' / '×' */
        if (tokN >= ST_IMP_TOK_MAX) { return 2; }
        _impTok[tokN] = (4 << 8) | 2;
        tokN++;
        prev = 4;
        continue;
      }
      if (cc === 47 || cc === 247) {                  /* '/' / '÷' */
        if (tokN >= ST_IMP_TOK_MAX) { return 2; }
        _impTok[tokN] = (4 << 8) | 3;                 /* OP_DIV(建树期拒) */
        tokN++;
        prev = 4;
        continue;
      }
      if (cc === 94) {                                /* '^' */
        if (tokN >= ST_IMP_TOK_MAX) { return 2; }
        _impTok[tokN] = (4 << 8) | 4;                 /* OP_POW */
        tokN++;
        prev = 4;
        continue;
      }
      return 1;                                       /* 其余一律 EPARSE */
    }
    if (tokN === 0) { return 1; }
    if (sawY === 0) { return 1; }                     /* C A6-7:不含 y */

    _evalError = 0;
    n = toRPN(_impTok, tokN, _rpnToken);
    if (_evalError !== 0 || n <= 0) { return 1; }

    _count = 0;
    _root = -1;
    _droot = -1;
    _view = -1;
    _facK = 0;
    _remDeg = 0;
    root = buildFromRPN(_rpnToken, n, _impNum, 1);
    if (root < 0) { _count = 0; return _err === 2 ? 2 : 1; }
    if (_count > ST_MAX_TREE_NODES) { _count = 0; return 2; }

    for (i = 0; i < _count; i++) {
      if (_type(i) === ST_VAR && _pool[i * 4 + 3] === 1) { break; }
    }
    if (i === _count) { _count = 0; return 1; }       /* 双保险(词法已查) */

    fx = symDerivPartial(root, 0);
    if (fx < 0) { _count = 0; return 2; }
    fy = symDerivPartial(root, 1);
    if (fy < 0) { _count = 0; return 2; }

    vx = evalNode(fx, xv, yv);
    vy = evalNode(fy, xv, yv);
    if (vx !== vx || vy !== vy || vx === Infinity || vy === Infinity ||
        vx === -Infinity || vy === -Infinity) { _count = 0; return 4; }

    av = vy < 0 ? -vy : vy;
    scale = 1.0 + (vx < 0 ? -vx : vx);
    if (av < 1e-12 * scale) { _count = 0; return 3; }  /* EFLAT:C 同阈值 */

    _dydx = -vx / vy;
    _count = 0;
    return 0;
  }

  /* 偏导(C pro_partial 同款;VAR 按标签分流,显式栈后序,零递归) */
  function symDerivPartial(root, target) {
    var sp = 0;
    var budget = 0;
    var need = 0;
    var i = 0;
    var nd = 0;
    var n = 0;
    var t = 0;
    var d = -1;

    budget = ST_MAX_NODES - _count;
    need = (_count - root) * 4 + 8;
    if (budget < need) { return -1; }
    for (i = 0; i < _count; i++) { _dmap[i] = -1; }

    _walk[sp] = root;
    sp++;
    while (sp > 0) {
      sp--;
      nd = _walk[sp];
      if (nd >= ST_MARK) {
        n = nd - ST_MARK;
        t = _type(n);
        if (t === ST_CONST) {
          d = symConst(0);
        } else if (t === ST_VAR) {
          d = symConst(_pool[n * 4 + 3] === target ? 1 : 0);
        } else if (t === ST_ADD) {
          d = dmake_add(_dmap[_left(n)], _dmap[_right(n)]);
        } else if (t === ST_SUB) {
          d = dmake_sub(_dmap[_left(n)], _dmap[_right(n)]);
        } else if (t === ST_MUL) {
          d = dmake_add(dmake_mul(_dmap[_left(n)], _right(n)),
                        dmake_mul(_left(n), _dmap[_right(n)]));
        } else if (t === ST_POW) {
          d = dmake_pow(n);
        } else {
          d = -1;
        }
        if (d < 0) { return -1; }
        _dmap[n] = d;
      } else {
        t = _type(nd);
        if (sp + 3 >= ST_WALK_CAP) { return -1; }
        _walk[sp] = nd + ST_MARK;
        sp++;
        if (t === ST_ADD || t === ST_SUB || t === ST_MUL) {
          _walk[sp] = _left(nd); sp++;
          _walk[sp] = _right(nd); sp++;
        } else if (t === ST_POW) {
          _walk[sp] = _left(nd); sp++;
          if (_type(_right(nd)) !== ST_CONST) { _walk[sp] = _right(nd); sp++; }
        }
      }
    }
    return _dmap[root];
  }

  function getDyDx() { return _dydx; }

  /* 复位 */
  function reset() {
    _count = 0;
    _root = -1;
    _srcCount = 0;
    _srcDepth = 0;
    _droot = -1;
    _view = -1;
    _err = ST_OK;
    _facK = 0;
    _remDeg = 0;
    _dydx = 0.0;
    return ST_OK;
  }

  return {
    build: build,
    diff: diff,
    toString: toString,
    evalTree: evalTree,
    evalDiff: evalDiff,
    getDepth: getDepth,
    getSrcNodes: getSrcNodes,
    getNodeCount: getNodeCount,
    getError: getError,
    factor: factor,
    getFactorCount: getFactorCount,
    getFactorRat: getFactorRat,
    getFactorRadNum: getFactorRadNum,
    getFactorRad: getFactorRad,
    getFactorDen: getFactorDen,
    getFactorExp: getFactorExp,
    getRemainderDegree: getRemainderDegree,
    getRemainderCoeff: getRemainderCoeff,
    factorToString: factorToString,
    implicitDyDx: implicitDyDx,
    getDyDx: getDyDx,
    reset: reset,
    allocCount: allocCount
  };
})();

if (typeof module === "object" && module && module.exports) {
  module.exports = ApolloSymboltree;
}
