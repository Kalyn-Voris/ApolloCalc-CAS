// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · core_ratroot.js — 有理根检验与低次多项式因式分解（文件 9）
 * Lite Wearable / JerryScript / 堆上限 128KB / Zero-GC
 * 职责：有理根定理——整系数多项式 an·x^n+…+a0 的有理根 p/q 满足
 *       p 整除 a0、q 整除 an。枚举候选（约简去重）→ 综合除法验证 →
 *       降次迭代（显式栈，无递归）。供文件 6 solveCubic/solveQuartic
 *       开头调用：有理根走 exactSetFraction 精确形式，商系数写回 _coeffs
 *       交低次求解器收尾；无有理根时 _coeffs 原样保留，调用方走原路径。
 * 依赖：文件 1 _coeffs（系数布局 [常数项..最高次项]）、
 *       文件 4 exactSetFraction / exactIsValid 等五分量精确系统
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象/数组字面量
 *       全部中间量为函数内 var 标量 + 本模块预分配 TypedArray
 * 数值约定：采样提取的系数经 |c − round(c)| ≤ 1e-6·max(1,|c|) 判为整数；
 *   真有理根的综合除法余项为浮点噪声（< 1e-6·尺度），非根余项 O(1)，
 *   两者相距多个数量级；商系数按高斯引理必为整数，提交前逐项近整复核。 */

/* 文件 9 专属全局变量（唯一声明处） */
var _ratRootCandidates = new Int32Array(64);   /* p/q 交错存放（至多 32 组） */
var _ratRootCount = 0;
var _ratRootFound = 0;                         /* 显式栈深度，0..8 */
var _ratRootValues = new Float64Array(8);      /* 发现顺序，栈 */
var _ratRootIsExact = new Int32Array(8);       /* 有理根恒 1 */
var _ratRootNum = new Int32Array(8);           /* 分子 p（与 _ratRootValues 配对） */
var _ratRootDen = new Int32Array(8);           /* 分母 q（>0，已约简） */
var _ratRootWork = new Float64Array(16);       /* 当前整系数多项式（升幂） */
var _ratRootTrial = new Float64Array(16);      /* 综合除法试除区（失败即弃，不污染 work） */
var _ratRootDivs = new Int32Array(128);        /* 常数项 |a0| 的因数表（试除到 √） */

/* F0 全池水位账本：顶层 8 池加载期自报（sqfree IIFE 池另行自报，见 ApolloSqfree）
 * （docs/contracts/f0-governance-v25.md §3） */
if (typeof _modulePoolSum === "function") {
  _modulePoolSum(_ratRootCandidates, _ratRootValues, _ratRootIsExact, _ratRootNum,
                 _ratRootDen, _ratRootWork, _ratRootTrial, _ratRootDivs);
}

/* 整数判定与枚举上限：采样误差容差 1e-6（相对）；|an|/|a0| 超过 1e7 跳过枚举 */
var RATROOT_INT_TOL = 1e-6;
var RATROOT_ENUM_MAX = 10000000;

/* 模块一：基础工具 */

/* 欧几里得 gcd（迭代，无递归；入参非负） */
/* 显式栈入栈：根值 p/q（自动约简、分母翻正）；栈满（8）丢弃 */
function ratRootPush(p, q) {
  if (_ratRootFound >= 8) { return; }
  if (q < 0) { p = -p; q = -q; }
  var g = igcd(p < 0 ? -p : p, q);
  if (g > 1) { p = p / g; q = q / g; }
  _ratRootValues[_ratRootFound] = p / q;
  _ratRootNum[_ratRootFound] = p;
  _ratRootDen[_ratRootFound] = q;
  _ratRootIsExact[_ratRootFound] = 1;
  _ratRootFound++;
}

/* 工作区尺度：max |_ratRootWork[0..cd]|（整系数快照保证 ≥ 1） */
function ratRootScale(cd) {
  var mx = 1.0;
  var k = 0;
  for (k = 0; k <= cd; k++) {
    var a = _ratRootWork[k] < 0 ? -_ratRootWork[k] : _ratRootWork[k];
    if (a > mx) { mx = a; }
  }
  return mx;
}

/* 模块二：整系数快照与提交 */

/* 快照：_coeffs[0..cd] 逐项近整判定后舍入存入 _ratRootWork；
 * 任一系数非近整（非整系数多项式）返回 -1，调用方放弃降次走原路径 */
function ratRootSnap(cd) {
  var k = 0;
  for (k = 0; k <= cd; k++) {
    var c = _coeffs[k];
    if (!isFinite(c)) { return -1; }
    var r = Math.round(c);
    var diff = c - r;
    if (diff < 0) { diff = -diff; }
    var sc = c < 0 ? -c : c;
    if (sc < 1.0) { sc = 1.0; }
    if (diff > RATROOT_INT_TOL * sc) { return -1; }
    _ratRootWork[k] = r;
  }
  return 0;
}

/* 提交：_ratRootWork[0..cd]（近整）舍入写回 _coeffs[0..cd]，其余槽清零
 * （_coeffs 共 5 槽：多项式最高 4 次；降次后的尾槽清零防低次求解器误读） */
function ratRootCommit(cd) {
  var k = 0;
  for (k = 0; k <= cd; k++) {
    _coeffs[k] = Math.round(_ratRootWork[k]);
  }
  for (k = cd + 1; k < 5; k++) {
    _coeffs[k] = 0;
  }
}

/* 模块三：候选枚举 */

/* 因数表：|m| 的全部因数存入 _ratRootDivs（试除到 √m），返回个数；表满返回 -1 */
function ratRootDivisors(m) {
  var d = m < 0 ? -m : m;
  var cnt = 0;
  var i = 1;
  while (i * i <= d) {
    if (d % i === 0) {
      if (cnt >= 128) { return -1; }
      _ratRootDivs[cnt++] = i;
      if (i !== d / i) {
        if (cnt >= 128) { return -1; }
        _ratRootDivs[cnt++] = d / i;
      }
    }
    i++;
  }
  return cnt;
}

/* 单个 q 的候选全组：±p/q（p 遍历因数表） */
function ratRootAddCands(q, dc) {
  var di = 0;
  for (di = 0; di < dc; di++) {
    ratRootTryAdd(_ratRootDivs[di], q);
    ratRootTryAdd(-_ratRootDivs[di], q);
  }
}

/* 候选入表（约简 + 线性去重）；_ratRootCandidates 满 32 组即止 */
function ratRootTryAdd(p, q) {
  if (q < 0) { p = -p; q = -q; }
  var g = igcd(p < 0 ? -p : p, q);
  if (g > 1) { p = p / g; q = q / g; }
  var ci = 0;
  for (ci = 0; ci < _ratRootCount; ci++) {
    if (_ratRootCandidates[ci * 2] === p && _ratRootCandidates[ci * 2 + 1] === q) { return; }
  }
  if (_ratRootCount >= 32) { return; }
  _ratRootCandidates[_ratRootCount * 2] = p;
  _ratRootCandidates[_ratRootCount * 2 + 1] = q;
  _ratRootCount++;
}

/* 枚举 an、a0 的全部因数组合 p/q（p|a0、q|an），填 _ratRootCandidates；
 * 超上限（|an|/|a0| > 1e7 或因数表溢出）返回 0 = 放弃本轮枚举 */
function ratRootEnumerate(an, a0) {
  _ratRootCount = 0;
  var n = an < 0 ? -an : an;
  var m = a0 < 0 ? -a0 : a0;
  if (n < 1 || m < 1 || n > RATROOT_ENUM_MAX || m > RATROOT_ENUM_MAX) { return 0; }
  var dc = ratRootDivisors(m);
  if (dc < 0) { return 0; }
  var i = 1;
  while (i * i <= n) {
    if (n % i === 0) {
      ratRootAddCands(i, dc);
      var q2 = n / i;
      if (q2 !== i) { ratRootAddCands(q2, dc); }
    }
    i++;
  }
  return _ratRootCount;
}

/* 模块四：综合除法（升幂布局）
 * P(x) = (x−r)·Q(x) + rem，Q 升幂落 _ratRootTrial[1..cd]：
 *   q_{cd-1} = c_cd；q_{k-1} = c_k + r·q_k（k = cd−1..1）；rem = c_0 + r·q_0
 * 试除区失败即弃（work 不动），余项返回给调用方判定 */
function ratRootTrialDivide(cd, r) {
  var k = 0;
  for (k = 0; k <= cd; k++) {
    _ratRootTrial[k] = _ratRootWork[k];
  }
  for (k = cd - 1; k >= 1; k--) {
    _ratRootTrial[k] = _ratRootTrial[k] + r * _ratRootTrial[k + 1];
  }
  return _ratRootTrial[0] + r * _ratRootTrial[1];
}

/* 模块五：主入口（迭代降次，显式栈）
 * ratRootDescent(deg)：在 _coeffs[0..deg] 上迭代找有理根并降次。
 * 返回找到的有理根个数：
 *   0  = 无有理根（_coeffs 原样保留，调用方走原求解路径）
 *   >0 = 根按发现顺序入栈（_ratRootValues/_ratRootNum/_ratRootDen），
 *        _coeffs[0..deg−k] 为降次后的剩余多项式（近整整系数），
 *        _coeffs 尾槽清零
 * 循环不变式：每轮要么消去一个根（零根剥离 / 候选命中 / 线性吸收），
 * 要么 break 交还原求解器——无递归、无函数自调用 */
function ratRootDescent(deg) {
  _ratRootFound = 0;
  _ratRootCount = 0;
  if (deg < 1 || deg > 4) { return 0; }
  var cd = deg;
  var guard = 0;
  while (cd >= 1 && guard < 8) {
    guard++;
    if (ratRootSnap(cd) !== 0) { break; }        /* 非整系数：交还原求解器 */
    if (_ratRootWork[cd] === 0.0) { break; }     /* 首项舍入为零：次数实低，不处理 */

    if (_ratRootWork[0] === 0.0) {
      /* 零根剥离：x=0 是根，整体下移一次降次 */
      ratRootPush(0, 1);
      var s = 0;
      for (s = 0; s < cd; s++) { _ratRootWork[s] = _ratRootWork[s + 1]; }
      cd--;
      ratRootCommit(cd);
      continue;
    }
    if (cd === 1) {
      /* 线性吸收：root = −c0/c1（整系数下必为有理根，分数精确） */
      ratRootPush(-_ratRootWork[0], _ratRootWork[1]);
      cd = 0;
      ratRootCommit(0);
      continue;                                  /* cd=0 → 循环退出 */
    }

    /* 候选枚举 + 综合除法逐个验证（首个命中即提交、重启枚举） */
    var nc = ratRootEnumerate(_ratRootWork[cd], _ratRootWork[0]);
    var scale = ratRootScale(cd);
    var hit = 0;
    var ci = 0;
    for (ci = 0; ci < nc && hit === 0; ci++) {
      var p = _ratRootCandidates[ci * 2];
      var q = _ratRootCandidates[ci * 2 + 1];
      var r = p / q;
      var rem = ratRootTrialDivide(cd, r);
      var arem = rem < 0 ? -rem : rem;
      if (arem > RATROOT_INT_TOL * scale) { continue; }        /* 余项判定：非根 */
      /* 商近整复核（高斯引理；防裕度内假阳性根污染系数） */
      var ok = 1;
      var k = 0;
      for (k = 1; k <= cd; k++) {
        var c = _ratRootTrial[k];
        var rr = Math.round(c);
        var df = c - rr;
        if (df < 0) { df = -df; }
        var scc = c < 0 ? -c : c;
        if (scc < 1.0) { scc = 1.0; }
        if (df > RATROOT_INT_TOL * scc) { ok = 0; break; }
      }
      if (ok === 0) { continue; }
      /* 接受：商左移入工作区，降次提交 */
      ratRootPush(p, q);
      for (k = 0; k < cd; k++) { _ratRootWork[k] = _ratRootTrial[k + 1]; }
      cd--;
      ratRootCommit(cd);
      hit = 1;
    }
    if (hit === 0) { break; }                    /* 候选全部落空：剩余部分无有理根 */
  }
  return _ratRootFound;
}

/* 模块六：收尾合并（供 solveCubic/solveQuartic 调用）
 * ratRootFinish(k, deg)：栈中 k 个有理根排在结果前段，剩余 dd = deg−k 次
 * 多项式（已在 _coeffs）交低次求解器，其根整体右移 k 位拼接：
 *   dd=0：全有理，直接填栈；dd≤2：二次求解器（含 dd=1 防御：首项零退化线性）；
 *   dd=3：三次核（skipRational=1，降次循环已证明无剩余有理根）
 * 精确形式：有理根走 exactSetFraction（规范化/溢出由文件 4 兜底） */
function ratRootFinish(k, deg) {
  var dd = deg - k;
  if (dd >= 1 && dd <= 2) {
    solveQuadratic();
    if (_solveError !== 0) { return _solveError; }
  } else if (dd === 3) {
    solveCubic(1);
    if (_solveError !== 0) { return _solveError; }
  }

  var m = _rootCount;                            /* 子求解器根数（dd=0 时为 0） */
  var i = 0;
  /* 子求解器根整体右移 k 位（倒序复制防覆盖），有理根（栈序）占前段 */
  for (i = m - 1; i >= 0; i--) {
    _rootRe[i + k] = _rootRe[i];
    _rootIm[i + k] = _rootIm[i];
    _rootExactRat[i + k] = _rootExactRat[i];
    _rootExactRadNum[i + k] = _rootExactRadNum[i];
    _rootExactRad[i + k] = _rootExactRad[i];
    _rootExactDen[i + k] = _rootExactDen[i];
    _rootExactExp[i + k] = _rootExactExp[i];
    _rootExactValid[i + k] = _rootExactValid[i];
  }
  for (i = 0; i < k; i++) {
    _rootRe[i] = _ratRootValues[i];
    _rootIm[i] = 0.0;
    exactSetFraction(_ratRootNum[i], _ratRootDen[i]);
    _rootExactValid[i] = exactIsValid ? 1 : 0;
    _rootExactRat[i] = exactRat;
    _rootExactRadNum[i] = exactRadNum;
    _rootExactRad[i] = exactRad;
    _rootExactDen[i] = exactDen;
    _rootExactExp[i] = exactExp;
  }
  _rootCount = m + k;
  return 0;
}

/* ApolloCalc · core_ratroot.js 扩展 — sqfree 单变量多项式
 * square-free 分解与重根重数(v2.4.0 功能 9,JS 侧)
 * 契约:docs/contracts/sqfree-format-v24.md(冻结),与 C 侧
 * ac_pro_sqfree.c 互为镜像;唯一双侧分叉:JS 整题次数 ≥5 → EDEG
 * (guardDegree 口径,契约 §5.1 成文例外),C 侧 5..8 次走 DK。
 *
 * 管线(契约 §3→§6):
 *   词法(§3:仅小写 x/整系数 |c|≤999/x^n n∈[1,8] n≥9 当场 EDEG/
 *     同次项重复、=、/、.、括号、非 ASCII、>128 B → EPARSE,首错即返)
 *   → 本原化(§4.1:容度除净、首项化正)
 *   → 循环式 SFF(§4.2:g_i = gcd(f_i,f_i') 本原归一,h_i = f_i/g_i
 *     精确除,余式非零 → EINTERNAL;f_{i+1} = g_i,显式循环零递归)
 *   → q_j = h_j/h_{j+1}(重数唯一来源 §4.3;账本 Σ j·deg(q_j)=deg f
 *     不平 → EINTERNAL)
 *   → 逐层求根(deg 1 直接 −c0/c1;deg 2..4 复用求解层
 *     solveQuadratic/Cubic/Quartic,_coeffs 逐层临时写入——与 C 侧
 *     ac_sym_factor_poly CLOBBERS g_coeffs 同款副作用,已成文)
 *   → 排序(§5.3:Re 降序、1e-9 并列 Im 降序,稳定插入)
 *   → 共轭完整性(§5.2:非实根必存在配对,残缺 → EINTERNAL)
 *   → fmt 三分支(§5.4:整数/有理重建 q≤1000/定点 12 位半连舍,
 *     Cauchy 界 |r| < 1000 ⟹ x·1e12 < 2^53 无损)
 *   → 渲染 "[r@em, ...]"(§6.1:复根 re≡0 省略、|im|=1 只写 i;
 *     失败 out 恒 "")
 *
 * 整系数域:中间量全整数(Float64 承载),伪除每步提 content 控膨胀;
 *   溢出(>2^53−1)→ ECOEF(§7:SFF 中间整数溢出)。
 * 零 GC:17 组 TypedArray 加载期一次分配(allocCount=17);词法/SFF/
 *   排序/渲染全显式循环,零递归、零对象字面量热路径;结果对象
 *   {code,out} 加载期一次分配复用(core_ode _result 先例)。
 * 导出三通道:Node require / 浏览器 <script> 顶层 var / 内核拼接 ESM。 */

var ApolloSqfree = (function () {
  "use strict";

  var SQ_MAX_DEG = 8;            /* 词法域 x^n ∈ [1,8](n≥9 当场 EDEG) */
  var SQ_JS_DEG = 4;             /* JS 整题上限(guardDegree 口径,≥5 → EDEG) */
  var SQ_COEF_MAX = 999;         /* 系数域(§3.4,比 rat 9999 严) */
  var SQ_LEN_MAX = 128;          /* 输入长度上限(§3.1) */
  var SQ_TOL = 1e-9;             /* §5.2/§5.3/§5.4 统一容差 */
  var SQ_RAT_Q = 1000;           /* 有理重建 q 上限(§5.4) */
  var SQ_SAFE = 9007199254740991; /* 2^53−1 */

  var SQ_OK = 0;
  var SQ_EPARSE = -1;
  var SQ_EDEG = -2;
  var SQ_ECOEF = -3;
  var SQ_EINTERNAL = -5;         /* −4 EOVER 为 C 侧缓冲码,JS 不产生(§7) */

  /* 池(加载期一次分配;allocCount = 17) */
  var _coef = new Float64Array(SQ_MAX_DEG + 1);         /* 1 输入系数(升幂) */
  var _f = new Float64Array(SQ_MAX_DEG + 1);            /* 2 SFF 当前 f */
  var _g = new Float64Array(SQ_MAX_DEG + 1);            /* 3 gcd 出参/导数工作区 */
  var _h = new Float64Array(SQ_MAX_DEG + 1);            /* 4 精确除出参 */
  var _t = new Float64Array(SQ_MAX_DEG + 1);            /* 5 q_j 工作区 */
  var _t2 = new Float64Array(SQ_MAX_DEG + 1);           /* 6 prem 乘减暂存 */
  var _ga = new Float64Array(SQ_MAX_DEG + 1);           /* 7 gcd 欧几里得 a */
  var _gb = new Float64Array(SQ_MAX_DEG + 1);           /* 8 gcd 欧几里得 b */
  var _gr = new Float64Array(SQ_MAX_DEG + 1);           /* 9 gcd 欧几里得 r */
  var _q = new Float64Array(SQ_MAX_DEG + 1);            /* 18 精确除商(与余量位分离) */
  var _layerC = new Float64Array((SQ_JS_DEG + 1) * 5);  /* 10 各层 h_j/q_j(升幂,行宽 5) */
  var _layerD = new Int32Array(SQ_JS_DEG + 1);          /* 11 各层多项式次数 */
  var _itRe = new Float64Array(8);                      /* 12 项:根实部 */
  var _itIm = new Float64Array(8);                      /* 13 项:根虚部 */
  var _itMult = new Int32Array(8);                      /* 14 项:重数 */
  var _outBuf = new Int32Array(256);                    /* 15 渲染缓冲(charCode) */
  var _digits = new Int32Array(20);                     /* 16 fmt 位数字垫 */
  var _seen = new Int32Array(SQ_MAX_DEG + 1);           /* 17 同次项重复掩码 */

  /* F0 全池水位账本：sqfree 段 18 池加载期自报（docs/contracts/f0-governance-v25.md §3） */
  if (typeof _modulePoolSum === "function") {
    _modulePoolSum(_coef, _f, _g, _h, _t, _t2, _ga, _gb, _gr, _q,
                   _layerC, _layerD, _itRe, _itIm, _itMult, _outBuf, _digits, _seen);
  }

  var _allocCount = 18;
  var _outLen = 0;
  var _deg = -1;
  var _result = { code: 0, out: "" };                   /* 加载期一次分配,复用 */

  function allocCount() { return _allocCount; }

  function _resetWork() {
    var i = 0;
    for (i = 0; i <= SQ_MAX_DEG; i++) {
      _coef[i] = 0; _f[i] = 0; _g[i] = 0; _h[i] = 0; _t[i] = 0; _t2[i] = 0;
      _ga[i] = 0; _gb[i] = 0; _gr[i] = 0; _seen[i] = 0;
    }
    for (i = 0; i < 5; i++) { _layerD[i] = -1; }
    for (i = 0; i < 8; i++) { _itRe[i] = 0; _itIm[i] = 0; _itMult[i] = 0; }
  }

  /* 词法(§3,单趟左→右,首错即返) */

  function lex(src) {
    var n = src.length;
    var i = 0;
    var cc = 0;
    var sepSign = 1;
    var sign = 0;
    var sawCoef = 0;
    var sawX = 0;
    var c = 0;
    var exp = 0;
    var anyTerm = 0;

    if (n === 0 || n > SQ_LEN_MAX) { return SQ_EPARSE; }
    for (i = 0; i <= SQ_MAX_DEG; i++) { _coef[i] = 0; _seen[i] = 0; }
    _deg = -1;

    i = 0;
    for (;;) {
      /* 项首:分隔符与项首符号逐字符消费(§3.2:项首可再带符号,+−3x 合法) */
      sign = sepSign;
      for (;;) {
        if (i >= n) { break; }
        cc = src.charCodeAt(i);
        if (cc === 43) { i++; continue; }                /* '+' */
        if (cc === 45) { sign = -sign; i++; continue; }  /* '-' 翻号 */
        break;
      }
      if (i >= n) { return SQ_EPARSE; }                  /* 裸符号/串尾悬空 */
      while (i < n) {
        cc = src.charCodeAt(i);
        if (cc === 32 || cc === 9) { i++; } else { break; }
      }
      if (i >= n) { return SQ_EPARSE; }
      cc = src.charCodeAt(i);

      /* 系数 [0-9]+(前导零允许;值 >999 → ECOEF;数字后空白再数字 → EPARSE) */
      sawCoef = 0;
      c = 0;
      while (i < n) {
        cc = src.charCodeAt(i);
        if (cc < 48 || cc > 57) { break; }
        c = c * 10 + (cc - 48);
        sawCoef = 1;
        i++;
        if (c > SQ_COEF_MAX) { return SQ_ECOEF; }
      }
      if (sawCoef === 1) {
        while (i < n) {
          cc = src.charCodeAt(i);
          if (cc === 32 || cc === 9) { i++; } else { break; }
        }
        if (i < n) {
          cc = src.charCodeAt(i);
          if (cc >= 48 && cc <= 57) { return SQ_EPARSE; }  /* "1 2x" */
        }
      }

      /* 可选 '*':仅一个,夹在系数与 x 之间(§3.2;悬空 "*x" → EPARSE) */
      while (i < n) {
        cc = src.charCodeAt(i);
        if (cc === 32 || cc === 9) { i++; } else { break; }
      }
      if (i < n && src.charCodeAt(i) === 42) {           /* '*' */
        if (sawCoef === 0) { return SQ_EPARSE; }         /* "*x":无系数可夹 */
        i++;
        while (i < n) {
          cc = src.charCodeAt(i);
          if (cc === 32 || cc === 9) { i++; } else { break; }
        }
        if (i >= n) { return SQ_EPARSE; }                /* "3*" */
        if (src.charCodeAt(i) !== 120) { return SQ_EPARSE; }  /* '*' 后必须 x */
      }

      /* 变量 x(仅小写;X/其他字母/'='/"/"/'.'/'('/非 ASCII → EPARSE) */
      sawX = 0;
      exp = 0;
      if (i >= n) {
        cc = 0;                                          /* 串尾:纯系数项(合法) */
      } else {
        cc = src.charCodeAt(i);
      }
      if (cc === 120) {
        sawX = 1;
        i++;
        if (i < n && src.charCodeAt(i) === 94) {         /* '^' 紧连 */
          i++;
          if (i >= n) { return SQ_EPARSE; }              /* "x^" */
          cc = src.charCodeAt(i);
          if (cc < 48 || cc > 57) { return SQ_EPARSE; }  /* "x^ 2"/"x^a" */
          exp = 0;
          while (i < n) {
            cc = src.charCodeAt(i);
            if (cc < 48 || cc > 57) { break; }
            exp = exp * 10 + (cc - 48);
            if (exp > SQ_MAX_DEG) { return SQ_EDEG; }    /* n≥9 当场 EDEG */
            i++;
          }
          if (exp === 0) { return SQ_EPARSE; }           /* x^0 */
        } else {
          exp = 1;
        }
        /* x 之后仅允许 ^(已消费)/空白/'+'/'-'/串尾(§3.3-2) */
        while (i < n) {
          cc = src.charCodeAt(i);
          if (cc === 32 || cc === 9) { i++; } else { break; }
        }
        if (i < n) {
          cc = src.charCodeAt(i);
          if (cc !== 43 && cc !== 45) { return SQ_EPARSE; }  /* "x*3"/"x3" */
        }
      } else if (i < n && cc !== 43 && cc !== 45) {
        return SQ_EPARSE;                                /* 纯系数项之外的非法字符 */
      }

      if (sawCoef === 0 && sawX === 0) { return SQ_EPARSE; }
      anyTerm = 1;

      /* 累加:同次项重复 → EPARSE(§3.3-3) */
      if (sawX === 1) {
        if (_seen[exp] === 1) { return SQ_EPARSE; }
        _seen[exp] = 1;
        _coef[exp] = sign * (sawCoef === 1 ? c : 1);
        if (exp > _deg) { _deg = exp; }
      } else {
        if (_seen[0] === 1) { return SQ_EPARSE; }
        _seen[0] = 1;
        _coef[0] = _coef[0] + sign * c;
        if (_deg < 0 && _coef[0] !== 0) { _deg = 0; }
      }

      /* 分隔符:有 → 置号进下一项;无 → 必须串尾 */
      while (i < n) {
        cc = src.charCodeAt(i);
        if (cc === 32 || cc === 9) { i++; } else { break; }
      }
      if (i >= n) { break; }
      cc = src.charCodeAt(i);
      if (cc === 43) { sepSign = 1; i++; }
      else if (cc === 45) { sepSign = -1; i++; }
      else { return SQ_EPARSE; }
    }
    if (anyTerm === 0) { return SQ_EPARSE; }
    return SQ_OK;
  }

  /* 整系数多项式工具(升幂) */

  function _degOf(a, cap) {
    var k = cap;
    while (k >= 0) {
      if (a[k] !== 0) { return k; }
      k--;
    }
    return -1;
  }
  function _copyTo(dst, src, d) {
    var k = 0;
    for (k = 0; k <= SQ_MAX_DEG; k++) { dst[k] = k <= d ? src[k] : 0; }
  }
  /* 本原化:容度除净、首项化正(§4.1)。返回次数;全零 −1;中间量溢出 −2 */
  function _primitive(a) {
    var d = _degOf(a, SQ_MAX_DEG);
    var g = 0;
    var k = 0;
    if (d < 0) { return -1; }
    for (k = 0; k <= d; k++) {
      g = igcd(g, a[k] < 0 ? -a[k] : a[k]);
      if (g > SQ_SAFE) { return -2; }
    }
    if (g > 1) {
      for (k = 0; k <= d; k++) { a[k] = a[k] / g; }
    }
    if (a[d] < 0) {
      for (k = 0; k <= d; k++) { a[k] = -a[k]; }
    }
    return d;
  }
  /* 导数(整系数):b[k−1] = k·a[k];溢出 −1 */
  function _deriv(a, d, b) {
    var k = 0;
    for (k = 0; k <= SQ_MAX_DEG; k++) { b[k] = 0; }
    for (k = 1; k <= d; k++) {
      b[k - 1] = k * a[k];
      if (b[k - 1] > SQ_SAFE || b[k - 1] < -SQ_SAFE) { return -1; }
    }
    return d >= 1 ? d - 1 : 0;
  }
  /* 本原伪余 premod(f, g):交叉相乘消首项、每步本原化控膨胀(§4.2 GCD 用)。
   * 写入 r,返回次数;余为零 −1;溢出 −2。 */
  function _prem(f, fd, g, gd, r) {
    var rd = fd;
    var k = 0;
    var lg = g[gd];
    var lead = 0;
    if (gd < 0) { return -1; }
    _copyTo(r, f, fd);
    for (;;) {
      rd = _degOf(r, rd);
      if (rd < gd) { break; }
      lead = r[rd];
      /* r ← lc(g)·r − lead·x^(rd−gd)·g(首项精确相消) */
      for (k = 0; k <= SQ_MAX_DEG; k++) { _t2[k] = 0; }
      for (k = 0; k <= gd; k++) {
        _t2[k + (rd - gd)] = _t2[k + (rd - gd)] + lead * g[k];
        if (_t2[k + (rd - gd)] > SQ_SAFE || _t2[k + (rd - gd)] < -SQ_SAFE) { return -2; }
      }
      for (k = 0; k <= rd; k++) {
        r[k] = r[k] * lg - _t2[k];
        if (r[k] > SQ_SAFE || r[k] < -SQ_SAFE) { return -2; }
      }
      rd = _degOf(r, rd);
      if (rd < 0) { break; }
      var cs = _primitive(r);                          /* 每步本原化(控膨胀) */
      if (cs === -2) { return -2; }
      rd = cs;
    }
    return rd;
  }
  /* 精确除 h = f / g(整除性由 Gauss 引理保证;余非零/除不尽 −1,溢出 −2) */
  function _exactdiv(f, fd, g, gd, h) {
    var k = 0;
    var hd = fd - gd;
    var lead = 0;
    var j = 0;
    var qk = 0;
    if (gd < 0 || hd < 0) { return -1; }
    _copyTo(h, f, fd);
    for (k = hd; k >= 0; k--) {
      lead = h[k + gd];
      if (lead % g[gd] !== 0) { return -1; }
      qk = lead / g[gd];
      _q[k] = qk;
      for (j = 0; j <= gd; j++) {                      /* 含 j=gd:首项位归零 */
        h[k + j] = h[k + j] - qk * g[j];
        if (h[k + j] > SQ_SAFE || h[k + j] < -SQ_SAFE) { return -2; }
      }
    }
    for (k = 0; k < gd; k++) {
      if (h[k] !== 0) { return -1; }                   /* 余式非零(§4.2) */
    }
    _copyTo(h, _q, hd);                                /* 商写回(h[0..hd],余量弃) */
    return hd;
  }
  /* 本原 gcd:欧几里得 + 每步本原化(§4.2「整系数欧几里得 GCD,本原归一」)。
   * 写入 out,返回次数;溢出 −2。 */
  function _gcdPoly(f, fd, gIn, gd, out) {
    var ad = fd;
    var bd = gd;
    var rd = 0;
    if (gd < 0) { return -1; }
    _copyTo(_ga, f, fd);
    ad = _primitive(_ga);
    if (ad === -2) { return -2; }
    _copyTo(_gb, gIn, gd);
    bd = _primitive(_gb);
    if (bd === -2) { return -2; }
    while (bd >= 0) {
      if (bd === 0) { _copyTo(out, _gb, 0); return 0; }
      rd = _prem(_ga, ad, _gb, bd, _gr);
      if (rd === -2) { return -2; }
      if (rd < 0) { _copyTo(out, _gb, bd); return bd; }  /* r = 0 → gcd = b */
      _copyTo(_ga, _gb, bd);                           /* a ← b */
      ad = bd;
      _copyTo(_gb, _gr, rd);                           /* b ← r */
      bd = rd;
    }
    return -1;
  }

  /* fmt 三分支(§5.4,与 ODE _fmt 同款) */

  function _pushCC(ch) {
    if (_outLen < 256) { _outBuf[_outLen] = ch; }
    _outLen++;
  }
  function _pushInt(m) {
    emitInt(m, _pushCC, _digits);
  }
  function _fmt(x) {
    var m = 0;
    var d = 0;
    var bestP = 0;
    var bestQ = 0;
    var bestErr = 0;
    var haveBest = 0;
    var q = 0;
    var p = 0;
    var pq = 0;
    var err = 0;
    var g = 0;
    var s = 0;
    var neg = 0;
    var mag = 0;
    var fp = 0;
    var ip = 0;
    var pos = 0;
    var last = 0;
    if (x !== x || x === Infinity || x === -Infinity) { return -1; }
    m = Math.round(x);
    d = x - m;
    if (d < 0) { d = -d; }
    if (d < SQ_TOL) {                                  /* 整数判定(含 "0",无 -0) */
      if (m < 0) { _pushCC(45); m = -m; }
      _pushInt(m);
      return 0;
    }
    /* 有理重建 q ≤ 1000(平局取 q 小、再 |p| 小——升序首遇即保证) */
    for (q = 1; q <= SQ_RAT_Q; q++) {
      p = Math.round(x * q);
      pq = p / q;
      err = x - pq;
      if (err < 0) { err = -err; }
      if (err <= SQ_TOL) {
        if (haveBest === 0 || err < bestErr) {
          bestP = p; bestQ = q; bestErr = err; haveBest = 1;
        }
      }
    }
    if (haveBest === 1) {
      g = igcd(bestP < 0 ? -bestP : bestP, bestQ);
      bestP = bestP / g;
      bestQ = bestQ / g;
      if (bestP < 0) { _pushCC(45); bestP = -bestP; }
      _pushInt(bestP);
      if (bestQ !== 1) { _pushCC(47); _pushInt(bestQ); }
      return 0;
    }
    /* 定点 12 位半连舍(§5.4;|r| < 1000 ⟹ |s| < 1e15 < 2^53) */
    s = x * 1e12;
    s = s >= 0 ? Math.floor(s + 0.5) : -Math.floor(-s + 0.5);
    neg = s < 0;
    mag = neg ? -s : s;
    fp = mag % 1000000000000;
    ip = (mag - fp) / 1000000000000;
    if (neg) { _pushCC(45); }
    _pushInt(ip);
    for (pos = 0; pos < 12; pos++) {
      _digits[11 - pos] = fp % 10;
      fp = (fp - (fp % 10)) / 10;
    }
    last = -1;
    for (pos = 0; pos < 12; pos++) { if (_digits[pos] !== 0) { last = pos; } }
    if (last >= 0) {
      _pushCC(46);
      for (pos = 0; pos <= last; pos++) { _pushCC(48 + _digits[pos]); }
    }
    return 0;
  }
  function _isZeroInt(x) {
    var m = Math.round(x);
    if (m !== 0) { return 0; }
    var d = x - m;
    return d < 0 ? -d < SQ_TOL : d < SQ_TOL;
  }
  function _isOneInt(x) {
    var m = Math.round(x);
    if (m !== 1) { return 0; }
    var d = x - m;
    return d < 0 ? -d < SQ_TOL : d < SQ_TOL;
  }
  /* §5.3 键比较:候选 (kr,ki) 应排到 (re,im) 之前 ⟺ Re 降序、Re 并列 Im 降序 */
  function _keyBefore(re, im, kr, ki) {
    if (re < kr - SQ_TOL) { return 1; }
    if (kr < re - SQ_TOL) { return 0; }
    return im < ki - SQ_TOL;
  }

  /* 主入口 */

  function sqfree(src) {
    var rc = 0;
    var fd = 0;
    var fd0 = 0;
    var gd = 0;
    var fpd = 0;
    var round = 0;
    var L = 0;
    var j = 0;
    var k = 0;
    var hd = 0;
    var hnD = 0;
    var ledger = 0;
    var cnt = 0;
    var d = 0;
    var i = 0;
    var i2 = 0;
    var good = 0;
    var s = "";
    var kr = 0;
    var ki = 0;
    var km = 0;
    var imAbs = 0;

    _result.code = SQ_EINTERNAL;
    _result.out = "";
    _outLen = 0;
    _resetWork();

    if (typeof src !== "string") { _result.code = SQ_EPARSE; return _result; }
    rc = lex(src);
    if (rc !== SQ_OK) { _result.code = rc; return _result; }
    if (_deg < 0) { _result.code = SQ_ECOEF; return _result; }        /* 全零(§4.4) */
    if (_deg > SQ_JS_DEG) { _result.code = SQ_EDEG; return _result; } /* JS ≥5 拒(§5.1) */

    fd = _primitive(_coef);
    if (fd === -2) { _result.code = SQ_ECOEF; return _result; }
    if (fd === 0) {                                                   /* 非零常数(§4.4) */
      _result.code = SQ_OK;
      _result.out = "[]";
      return _result;
    }
    fd0 = fd;

    /* 循环式 SFF(§4.2;零递归) */
    _copyTo(_f, _coef, fd);
    L = 0;
    for (round = 0; round <= SQ_JS_DEG; round++) {
      if (fd < 1) { break; }
      fpd = _deriv(_f, fd, _g);
      if (fpd === -1) { _result.code = SQ_ECOEF; return _result; }
      gd = _gcdPoly(_f, fd, _g, fpd, _g);
      if (gd === -2) { _result.code = SQ_ECOEF; return _result; }
      if (gd < 0) { _result.code = SQ_EINTERNAL; return _result; }
      hd = _exactdiv(_f, fd, _g, gd, _h);
      if (hd === -2) { _result.code = SQ_ECOEF; return _result; }
      if (hd < 0) { _result.code = SQ_EINTERNAL; return _result; }
      L++;
      for (k = 0; k <= hd; k++) { _layerC[(L - 1) * 5 + k] = _h[k]; }
      _layerD[L - 1] = hd;
      _copyTo(_f, _g, gd);                             /* f_{i+1} = g_i */
      fd = gd;
    }

    /* q_j = h_j / h_{j+1}(§4.2;h_{L+1} = f_{L+1} = 常数槽 _f) */
    for (j = 1; j <= L; j++) {
      var hjD = _layerD[j - 1];
      if (j < L) {
        hnD = _layerD[j];
        for (k = 0; k <= hnD; k++) { _h[k] = _layerC[j * 5 + k]; }
        for (k = hnD + 1; k <= SQ_MAX_DEG; k++) { _h[k] = 0; }
      } else {
        hnD = 0;
        for (k = 0; k <= SQ_MAX_DEG; k++) { _h[k] = k === 0 ? _f[0] : 0; }
      }
      for (k = 0; k <= hjD; k++) { _g[k] = _layerC[(j - 1) * 5 + k]; }
      for (k = hjD + 1; k <= SQ_MAX_DEG; k++) { _g[k] = 0; }
      hd = _exactdiv(_g, hjD, _h, hnD, _t);
      if (hd === -2) { _result.code = SQ_ECOEF; return _result; }
      if (hd < 0) { _result.code = SQ_EINTERNAL; return _result; }
      for (k = 0; k <= SQ_JS_DEG; k++) { _layerC[(j - 1) * 5 + k] = k <= hd ? _t[k] : 0; }
      _layerD[j - 1] = hd;
      ledger = ledger + j * hd;                        /* Σ j·deg(q_j)(§4.3) */
    }
    if (ledger !== fd0) { _result.code = SQ_EINTERNAL; return _result; }

    /* 逐层求根(deg 1 直接;deg 2..4 复用求解层,_coeffs 临时写入) */
    cnt = 0;
    tokenCount = 0;                                    /* guardMultivariateNonlinear 防御 */
    for (j = 1; j <= L; j++) {
      d = _layerD[j - 1];
      if (d < 1) { continue; }
      for (k = 0; k < 5; k++) { _coeffs[k] = k <= d ? _layerC[(j - 1) * 5 + k] : 0; }
      if (d === 1) {
        _solveError = 0;
        _rootRe[0] = -_coeffs[0] / _coeffs[1];
        _rootIm[0] = 0.0;
        _rootCount = 1;
      } else if (d === 2) {
        solveQuadratic();
      } else if (d === 3) {
        solveCubic(1);
      } else {
        solveQuartic();
      }
      if (_solveError !== 0 || _rootCount !== d) {
        _result.code = SQ_EINTERNAL; return _result;   /* 求根失败(§5.1) */
      }
      for (k = 0; k < d; k++) {
        _itRe[cnt] = _rootRe[k];
        _itIm[cnt] = _rootIm[k];
        _itMult[cnt] = j;
        cnt++;
      }
    }

    /* 排序(§5.3,稳定插入) */
    for (i = 1; i < cnt; i++) {
      kr = _itRe[i];
      ki = _itIm[i];
      km = _itMult[i];
      i2 = i - 1;
      while (i2 >= 0 && _keyBefore(_itRe[i2], _itIm[i2], kr, ki)) {
        _itRe[i2 + 1] = _itRe[i2];
        _itIm[i2 + 1] = _itIm[i2];
        _itMult[i2 + 1] = _itMult[i2];
        i2--;
      }
      _itRe[i2 + 1] = kr;
      _itIm[i2 + 1] = ki;
      _itMult[i2 + 1] = km;
    }

    /* 共轭完整性(§5.2) */
    for (i = 0; i < cnt; i++) {
      imAbs = _itIm[i] < 0 ? -_itIm[i] : _itIm[i];
      if (imAbs < SQ_TOL) { continue; }
      good = 0;
      for (j = 0; j < cnt; j++) {
        if (j === i) { continue; }
        if (Math.abs(_itRe[i] - _itRe[j]) < SQ_TOL &&
            Math.abs(_itIm[i] + _itIm[j]) < SQ_TOL &&
            ((_itIm[i] > 0 && _itIm[j] < 0) || (_itIm[i] < 0 && _itIm[j] > 0))) {
          good = 1; break;
        }
      }
      if (good === 0) { _result.code = SQ_EINTERNAL; return _result; }
    }

    /* 渲染(§6.1:实根 fmt(re);复根 [fmt(re)][±][fmt(|im|)]i) */
    _pushCC(91);                                       /* '[' */
    for (i = 0; i < cnt; i++) {
      if (i > 0) { _pushCC(44); _pushCC(32); }         /* ", " */
      imAbs = _itIm[i] < 0 ? -_itIm[i] : _itIm[i];
      if (imAbs < SQ_TOL) {
        if (_fmt(_itRe[i]) !== 0) { _result.code = SQ_EINTERNAL; return _result; }
      } else {
        var reZero = _isZeroInt(_itRe[i]);
        if (!reZero) {
          if (_fmt(_itRe[i]) !== 0) { _result.code = SQ_EINTERNAL; return _result; }
          _pushCC(_itIm[i] > 0 ? 43 : 45);             /* 连接符:有 re 才写 '+'/'-' */
        } else if (_itIm[i] < 0) {
          _pushCC(45);                                 /* re 省略:仅负号 */
        }
        if (!_isOneInt(imAbs)) {
          if (_fmt(imAbs) !== 0) { _result.code = SQ_EINTERNAL; return _result; }
        }
        _pushCC(105);                                  /* 'i' */
      }
      _pushCC(64);                                     /* '@' */
      _pushCC(101);                                    /* 'e'(JS 全走精确管线) */
      _pushInt(_itMult[i]);
    }
    _pushCC(93);                                       /* ']' */
    if (_outLen > 256) { _result.code = SQ_EINTERNAL; return _result; }
    for (i = 0; i < _outLen; i++) { s = s + String.fromCharCode(_outBuf[i]); }
    _result.code = SQ_OK;
    _result.out = s;
    return _result;
  }

  function reset() {
    _resetWork();
    _result.code = 0;
    _result.out = "";
    return SQ_OK;
  }


  /* ============================================================
   * ApolloCalc · core_ratroot.js 扩段 — Sturm 序列与实根隔离
   * (v2.5.0 F3,JS 镜像 deg ≤ 4;契约 docs/contracts/sturm-format-v25.md
   *  冻结;C 侧 ac_pro_sturm.c 互为镜像,5..8 次 → EDEG 成文分叉 cOnly-DEG)
   *
   * 数学语义(契约 §2,C 侧文件头同款):
   *   p₀ = p_sf(SFF 预归一),p₁ = p_sf′,p_{k+1} = −rem(p_{k−1},p_k) 的
   *   正倍数(精确有理余 → 正 lcm 清母线 → 正 content 约减 → 不变号);
   *   count(a,b) = V(a)−V(b)+[p₀(a)=0](闭区间,端点根计入;±∞ = lc/奇偶
   *   逐成员);隔离 = (−B,B] 半开格显式栈二分(深度 ≤ 48,格内 ≥2 根 →
   *   未解析 "!";端点既约分数)。
   *
   * JS 精确域(契约 §5.2,ECOEF 双语义的 JS 侧):
   *   链构造:精确有理数(_stN/_stD Float64 整数,每算术步 |v| > 2^53−1
   *   即 ECOEF,绝不输出失真值);
   *   计数端点(整数):精确整数 Horner,每步同审计 —— 大端点用例由此
   *   触发 cOnly-OVF(x⁴+1 在 9742 处 Horner 中间量 ~9.0e15 破 2^53);
   *   隔离二分点:端点分子 num ≤ 2^53 审计(num/2^dep 除幂精确)→ 浮点
   *   Horner + 前向误差界(|v| > 2·γ·Σ|c_i||t|^i 才定号,γ = deg·2⁻⁵²),
   *   界内不可判定 → ECOEF —— dualCases 域内(相异根间隔 ≫ 机器精度)
   *   全程可判定,与 C 精确定号同结果(dual 零分叉先决条件)。
   *
   * 零 GC:池加载期一次分配(sturm 段 allocCount = 14);词法复用本模块
   * 既有 lex(sqfree 词法域同款:小写 x、|c| ≤ 999、ASCII、≤128 B);
   * JS 整题 ≥5 → EDEG(guardDegree 口径,cOnly-DEG 分叉)。 */

  var ST_SAFE = 9007199254740991;  /* 2^53−1 */
  var ST_BDEPTH = 48;              /* 契约冻结二分深度(双侧统一) */
  var ST_BSTACK = 49;              /* DFS 待处理格 ≤ 深度+1 ≤ 49 */
  var ST_CELLS = 16;               /* 隔离输出格 ≤ 8 根格 + 4 未解析 + 1 端点格 */

  /* 池(加载期一次分配;sturm 段 allocCount = 14) */
  var _stC = new Float64Array(15);           /* 1 链系数(三角:5+4+3+2+1,带符号) */
  var _stOff = new Int32Array(5);            /* 2 成员系数起始下标 */
  var _stDeg = new Int32Array(5);            /* 3 成员次数 */
  var _stN = new Float64Array(5);            /* 4 有理数分子工作区 */
  var _stD = new Float64Array(5);            /* 5 有理数分母工作区(d > 0) */
  var _stA = new Float64Array(5);            /* 6 欧几里得 a 槽(整数) */
  var _stB = new Float64Array(5);            /* 7 欧几里得 b / 除数槽(整数) */
  var _stW = new Float64Array(5);            /* 8 清母线余式工作区 */
  var _stQn = new Float64Array(5);           /* 8b 精确除商分子(有理) */
  var _stLo = new Float64Array(ST_BSTACK);   /* 9 二分栈左端分子 */
  var _stHi = new Float64Array(ST_BSTACK);   /* 10 二分栈右端分子 */
  var _stDep = new Uint8Array(ST_BSTACK);    /* 11 二分栈深度(≤ 48) */
  var _stOut = new Uint8Array(256);          /* 12 渲染缓冲(charCode) */
  var _stCellLo = new Float64Array(ST_CELLS);        /* 13 输出格 lo */
  var _stCellHi = new Float64Array(ST_CELLS);        /* 14 输出格 hi */
  var _stCellD = new Uint8Array(ST_CELLS * 2);       /* (dep ≤ 48, unres 0/1) 交错 */

  /* F0 全池水位账本：sturm 段 16 池加载期自报（docs/contracts/f0-governance-v25.md §3；
   * 合并 feat/solvers 时补履行登记义务——分支分叉于 F0 前，未含自报） */
  if (typeof _modulePoolSum === "function") {
    _modulePoolSum(_stC, _stOff, _stDeg, _stN, _stD, _stA, _stB, _stW, _stQn,
                   _stLo, _stHi, _stDep, _stOut, _stCellLo, _stCellHi, _stCellD);
  }

  var _stMem = 0;
  var _stOutLen = 0;
  var _stResult = { code: 0, out: "" };      /* 加载期一次分配,复用 */

  function sturmAllocCount() { return 15; }
  function getSturmString() { return _stResult.out; }

  function stMul(a, b) {
    var r = a * b;
    if (r > ST_SAFE || r < -ST_SAFE) { return NaN; }
    return r;
  }
  function stAdd(a, b) {
    var r = a + b;
    if (r > ST_SAFE || r < -ST_SAFE) { return NaN; }
    return r;
  }
  /* 精确有理余:被除式 = (_stN,_stD)(有理,d>0);除式 = _stB(整数,deg db)。
   * 就地消去得 Q 上余式;返回余式次数(−1 = 零);溢出 → SQ_ECOEF。
   * negOut ≠ 0 时输出 −rem 的清母线整数式到 _stW(链步用),否则保持有理式。 */
  function stRatRem(db, negOut) {
    var k;
    var j;
    var qn;
    var qd;
    var tn;
    var td;
    var g;
    var ki;
    var nn;
    var dd;
    var rd;
    var L;
    var mx;
    var mul;
    var tn2;
    var g2;
    for (k = 4; k >= db; k--) {
      if (_stN[k] === 0) { continue; }
      qn = _stN[k];
      qd = stMul(_stD[k], _stB[db]);
      if (isNaN(qd)) { return SQ_ECOEF; }
      g = igcd(qn < 0 ? -qn : qn, qd);
      if (g > 1) { qn = qn / g; qd = qd / g; }
      for (j = 0; j <= db; j++) {
        if (_stB[j] === 0) { continue; }
        tn = stMul(qn, _stB[j]);
        if (isNaN(tn)) { return SQ_ECOEF; }
        td = qd;
        g = igcd(tn < 0 ? -tn : tn, td);
        if (g > 1) { tn = tn / g; td = td / g; }
        ki = k - db + j;
        nn = stAdd(stMul(_stN[ki], td), -stMul(_stD[ki], tn));
        if (isNaN(nn)) { return SQ_ECOEF; }
        dd = stMul(_stD[ki], td);
        if (isNaN(dd)) { return SQ_ECOEF; }
        g = igcd(nn < 0 ? -nn : nn, dd);
        if (g > 1) { nn = nn / g; dd = dd / g; }
        _stN[ki] = nn;
        _stD[ki] = dd;
      }
      _stN[k] = 0;
      _stD[k] = 1;
    }
    rd = db - 1;
    while (rd >= 0 && _stN[rd] === 0) { rd--; }
    if (rd < 0) { return -1; }
    if (negOut === 0) { return rd; }
    /* 清母线:L = lcm(d_0..d_rd)(正);w_i = ±n_i·(L/d_i) */
    L = 1;
    for (k = 0; k <= rd; k++) {
      g = igcd(L, _stD[k]);
      mx = stMul(L / g, _stD[k]);
      if (isNaN(mx)) { return SQ_ECOEF; }
      L = mx;
    }
    for (k = 0; k <= rd; k++) {
      mul = L / _stD[k];
      tn2 = stMul(_stN[k], mul);
      if (isNaN(tn2)) { return SQ_ECOEF; }
      _stW[k] = negOut > 0 ? -tn2 : tn2;
    }
    /* 正 content 约减(不变号) */
    g2 = 0;
    for (k = 0; k <= rd; k++) {
      var aw = _stW[k] < 0 ? -_stW[k] : _stW[k];
      g2 = igcd(g2, aw);
    }
    if (g2 > 1) {
      for (k = 0; k <= rd; k++) { _stW[k] = _stW[k] / g2; }
    }
    return rd;
  }

  /* 链构建(_coef 由 lex 填充,deg ≤ 4);返回 0 或负错误码 */
  function sturmBuild() {
    var dg = _deg;
    var i;
    var j;
    var rc;
    var da;
    var db;
    var dgc;
    var k;
    /* 成员 0 = p(本原化 + 首项化正;计数对标量缩放不变) */
    {
      var g = 0;
      for (i = 0; i <= dg; i++) {
        var aw = _coef[i] < 0 ? -_coef[i] : _coef[i];
        if (aw !== 0) { g = (g === 0) ? aw : igcd(g, aw); }
      }
      if (g > 1) {
        for (i = 0; i <= dg; i++) { _coef[i] = _coef[i] / g; }
      }
      if (_coef[dg] < 0) {
        for (i = 0; i <= dg; i++) { _coef[i] = -_coef[i]; }
      }
    }
    _stOff[0] = 0;
    _stDeg[0] = dg;
    for (i = 0; i <= dg; i++) { _stC[i] = _coef[i]; }
    _stMem = 1;
    if (dg === 0) { return 0; }

    /* SFF:g = gcd(p, p′)(有理余欧几里得,逐步本原;JS 域 2^53 审计) */
    da = dg;
    for (i = 0; i <= da; i++) { _stA[i] = _stC[i]; }
    db = dg - 1;
    for (i = 0; i <= db; i++) { _stB[i] = stMul(_stC[i + 1], i + 1); if (isNaN(_stB[i])) { return SQ_ECOEF; } }
    for (;;) {
      /* a ← 有理化 b;rem → w(−rem 正倍数;gcd 只看幅值,翻号无妨) */
      for (i = 0; i <= da; i++) { _stN[i] = _stA[i]; _stD[i] = 1; }
      rc = stRatRem(db, 1);
      if (rc === SQ_ECOEF) { return SQ_ECOEF; }
      /* (a, b) ← (b, rem) —— 先 a←b 再判零:余式为零时 gcd = a(旧 b),
       * 与 C 侧「无条件 a←b 再退出」同序(实测踩坑:先 break 误取 2x 为 gcd) */
      da = db;
      for (i = 0; i <= da; i++) { _stA[i] = _stB[i]; }
      if (rc === -1) { break; }
      db = rc;
      for (i = 0; i <= db; i++) { _stB[i] = _stW[i]; }
      if (_stB[db] < 0) {
        for (i = 0; i <= db; i++) { _stB[i] = -_stB[i]; }
      }
    }
    dgc = da;
    /* gcd = _stA(次数 dgc;首项化正) */
    if (_stA[dgc] < 0) {
      for (i = 0; i <= dgc; i++) { _stA[i] = -_stA[i]; }
    }
    if (dgc >= 1) {
      /* 精确除:p_sf = p / gcd(有理长除,商清母线成整数;余式非零 → EINTERNAL) */
      for (i = 0; i <= dg; i++) { _stN[i] = _stC[i]; _stD[i] = 1; }
      for (i = 0; i <= dgc; i++) { _stB[i] = _stA[i]; }
      for (i = 0; i < 5; i++) { _stQn[i] = 0; }
      var qdG = 1;   /* 商的统一分母(逐步 lcm) */
      var kk;
      for (kk = dg; kk >= dgc; kk--) {
        if (_stN[kk] === 0) { continue; }
        var cn = _stN[kk];
        var cd = stMul(_stD[kk], _stB[dgc]);
        if (isNaN(cd)) { return SQ_ECOEF; }
        var gg = igcd(cn < 0 ? -cn : cn, cd);
        if (gg > 1) { cn = cn / gg; cd = cd / gg; }
        _stQn[kk - dgc] = cn;
        /* 分母并入 qdG:lcm */
        gg = igcd(qdG, cd);
        var mx2 = stMul(qdG / gg, cd);
        if (isNaN(mx2)) { return SQ_ECOEF; }
        qdG = mx2;
        for (j = 0; j <= dgc; j++) {
          if (_stB[j] === 0) { continue; }
          var tn = stMul(cn, _stB[j]);
          if (isNaN(tn)) { return SQ_ECOEF; }
          var td = cd;
          gg = igcd(tn < 0 ? -tn : tn, td);
          if (gg > 1) { tn = tn / gg; td = td / gg; }
          var ki = kk - dgc + j;
          var nn = stAdd(stMul(_stN[ki], td), -stMul(_stD[ki], tn));
          if (isNaN(nn)) { return SQ_ECOEF; }
          var dd = stMul(_stD[ki], td);
          if (isNaN(dd)) { return SQ_ECOEF; }
          gg = igcd(nn < 0 ? -nn : nn, dd);
          if (gg > 1) { nn = nn / gg; dd = dd / gg; }
          _stN[ki] = nn;
          _stD[ki] = dd;
        }
        _stN[kk] = 0;
        _stD[kk] = 1;
      }
      for (i = 0; i < dgc; i++) {
        if (_stN[i] !== 0) { return SQ_EINTERNAL; }   /* 余式非零 */
      }
      /* 商 = _stQn/qdG → 清母线成整数(商必为整系数,Gauss) */
      var dq = dg - dgc;
      for (i = 0; i <= dq; i++) {
        if (_stQn[i] % qdG !== 0) { return SQ_EINTERNAL; }
        _stC[i] = _stQn[i] / qdG;
      }
      for (i = dq + 1; i <= 4; i++) { _stC[i] = 0; }
      dg = dq;
      _stDeg[0] = dg;
    }

    /* 链成员 1 = p_sf′ */
    _stOff[1] = dg + 1;
    _stDeg[1] = dg - 1;
    for (i = 1; i <= dg; i++) {
      _stC[_stOff[1] + i - 1] = stMul(_stC[i], i);
      if (isNaN(_stC[_stOff[1] + i - 1])) { return SQ_ECOEF; }
    }
    _stMem = 2;
    /* 迭代:p_{k+1} = −rem(p_{k−1}, p_k) 的正倍数 */
    k = 1;
    while (_stDeg[k] >= 1) {
      var da2 = _stDeg[k - 1];
      var db2 = _stDeg[k];
      if (k + 1 >= 5) { return SQ_EINTERNAL; }
      for (i = 0; i <= da2; i++) { _stN[i] = _stC[_stOff[k - 1] + i]; _stD[i] = 1; }
      for (i = 0; i <= db2; i++) { _stB[i] = _stC[_stOff[k] + i]; }
      rc = stRatRem(db2, 1);
      if (rc === SQ_ECOEF) { return SQ_ECOEF; }
      if (rc === -1) { return SQ_EINTERNAL; }   /* sf 后整除 = 不变式破坏 */
      k++;
      _stOff[k] = _stOff[k - 1] + db2 + 1;
      _stDeg[k] = rc;
      for (i = 0; i <= rc; i++) { _stC[_stOff[k] + i] = _stW[i]; }
      _stMem = k + 1;
    }
    return 0;
  }

  /* 符号:成员 k 在 num/2^e 处。e = 0 → 精确整数 Horner(2^53 审计);
   * e > 0 → 浮点 Horner + 前向误差界(不可判定 → ECOEF) */
  function stSignAt(k, num, e) {
    var dgg = _stDeg[k];
    var off = _stOff[k];
    var i;
    var v;
    if (e === 0) {
      v = _stC[off + dgg];
      for (i = dgg - 1; i >= 0; i--) {
        v = stAdd(stMul(v, num), _stC[off + i]);
        if (isNaN(v)) { return SQ_ECOEF; }
      }
      return v > 0 ? 1 : (v < 0 ? -1 : 0);
    }
    var t = num / Math.pow(2, e);   /* num ≤ 2^53 ⟹ 除幂精确 */
    v = _stC[off + dgg];
    var s = v < 0 ? -v : v;
    for (i = dgg - 1; i >= 0; i--) {
      var ci = _stC[off + i];
      v = v * t + ci;
      s = s * (t < 0 ? -t : t) + (ci < 0 ? -ci : ci);
    }
    if (!isFinite(v)) { return SQ_ECOEF; }
    var bound = 2 * (dgg + 1) * Math.pow(2, -52) * s;
    if (Math.abs(v) > bound) { return v > 0 ? 1 : -1; }
    /* 浮点不可判定(t 恰为根/极近根)→ 精确整数 Horner 重试:
     * N = p(num/2^e)·2^{e·dg} = Σ c_i·num^i·2^{e(dg−i)},逐项 ≤ 2^53 审计
     * (幂表 2^{e·dg} > 2^53 的深端点 → ECOEF:cOnly-OVF 域,成文)。
     * 根点处 N ≡ 0(每步中间量同为零)⟹ 精确判零,dualCases 零分叉关键 */
    if (e * dgg > 53) { return SQ_ECOEF; }
    var pw = 1;
    var j2;
    var pws = [1, 1, 1, 1, 1];
    for (j2 = 1; j2 <= dgg; j2++) {
      pw = pw * e2pow(e);
      if (pw > ST_SAFE) { return SQ_ECOEF; }
      pws[j2] = pw;
    }
    var nn = _stC[off + dgg];
    for (i = dgg - 1; i >= 0; i--) {
      nn = nn * num;
      if (!isFinite(nn) || Math.abs(nn) > ST_SAFE) { return SQ_ECOEF; }
      nn = nn + _stC[off + i] * pws[dgg - i];
      if (!isFinite(nn) || Math.abs(nn) > ST_SAFE) { return SQ_ECOEF; }
    }
    return nn > 0 ? 1 : (nn < 0 ? -1 : 0);
  }

  function e2pow(e) { return Math.pow(2, e); }

  function stVariations(num, e) {
    var last = 0;
    var v = 0;
    var k;
    for (k = 0; k < _stMem; k++) {
      var sg = stSignAt(k, num, e);
      if (sg === SQ_ECOEF) { return SQ_ECOEF; }
      if (sg === 0) { continue; }
      if (last !== 0 && sg !== last) { v++; }
      last = sg;
    }
    return v;
  }

  /* ±∞:sign(p_k(±∞)) = (±1)^deg·sign(lc) 逐成员 */
  function stVarInf(neg) {
    var last = 0;
    var v = 0;
    var k;
    for (k = 0; k < _stMem; k++) {
      var lc = _stC[_stOff[k] + _stDeg[k]];
      var sg = lc > 0 ? 1 : -1;
      if (neg && (_stDeg[k] % 2) !== 0) { sg = -sg; }
      if (last !== 0 && sg !== last) { v++; }
      last = sg;
    }
    return v;
  }

  function stParseEndpoint(s) {
    if (s === "inf") { return { kind: 1, v: 0 }; }
    if (s === "-inf") { return { kind: -1, v: 0 }; }
    if (typeof s !== "string" || s.length === 0 || s.length > 8) { return null; }
    var i = 0;
    var neg = 0;
    var v = 0;
    var nd = 0;
    if (s.charAt(0) === "-") { neg = 1; i = 1; }
    else if (s.charAt(0) === "+") { i = 1; }
    for (; i < s.length; i++) {
      var cc = s.charCodeAt(i);
      if (cc < 48 || cc > 57) { return null; }
      v = v * 10 + (cc - 48);
      nd++;
      if (nd > 6 || v > 999999) { return null; }
    }
    if (nd === 0) { return null; }
    return { kind: 0, v: neg ? -v : v };
  }

  function sturmPrepare(src) {
    var rc = lex(src);
    if (rc !== SQ_OK) { return rc; }
    if (_deg >= 5) { return SQ_EDEG; }   /* JS 镜像:cOnly-DEG 分叉(≥9 已在 lex 拒) */
    if (_deg < 0) { return SQ_ECOEF; }   /* 全零多项式 */
    _stMem = 0;
    _stOutLen = 0;
    return sturmBuild();
  }

  /* 对外:计数(闭区间 [a,b],端点根计入;成功值 = 非负根数) */
  function sturmCount(src, a, b) {
    var rc = sturmPrepare(src);
    if (rc !== 0) { return rc; }
    var ea = stParseEndpoint(a);
    var eb = stParseEndpoint(b);
    if (ea === null || eb === null) { return -4; }   /* EPARAM */
    var ka = ea.kind;
    var kb = eb.kind;
    var cmp;
    if (ka === -1) { cmp = (kb === -1) ? 0 : -1; }
    else if (ka === 1) { cmp = (kb === 1) ? 0 : 1; }
    else if (kb === -1) { cmp = 1; }
    else if (kb === 1) { cmp = -1; }
    else { cmp = (ea.v < eb.v) ? -1 : ((ea.v === eb.v) ? 0 : 1); }
    if (cmp > 0) { return -4; }   /* a > b → EPARAM */
    var va = (ka === 0) ? stVariations(ea.v, 0) : stVarInf(ka < 0);
    if (va === SQ_ECOEF) { return SQ_ECOEF; }
    var vb = (kb === 0) ? stVariations(eb.v, 0) : stVarInf(kb < 0);
    if (vb === SQ_ECOEF) { return SQ_ECOEF; }
    var n = va - vb;
    if (ka === 0) {
      var z = stSignAt(0, ea.v, 0);
      if (z === SQ_ECOEF) { return SQ_ECOEF; }
      if (z === 0) { n += 1; }
    }
    return n;
  }

  function stPutSCC(cc) {
    if (_stOutLen >= _stOut.length - 1) { return SQ_EINTERNAL; }
    _stOut[_stOutLen++] = cc;
    return 0;
  }
  function stPutI(v) {
    var buf = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    var i = 0;
    var m = v < 0 ? -v : v;
    if (v < 0) { if (stPutSCC(45) !== 0) { return SQ_EINTERNAL; } }
    if (m === 0) { buf[i++] = 0; }
    while (m > 0) { buf[i++] = m % 10; m = (m - (m % 10)) / 10; }
    while (i > 0) { if (stPutSCC(48 + buf[--i]) !== 0) { return SQ_EINTERNAL; } }
    return 0;
  }
  function stPutRat(num, dep) {
    if (num < 0) { if (stPutSCC(45) !== 0) { return SQ_EINTERNAL; } num = -num; }
    if (stPutI(num) !== 0) { return SQ_EINTERNAL; }
    if (dep > 0) {
      if (stPutSCC(47) !== 0) { return SQ_EINTERNAL; }
      if (stPutI(Math.pow(2, dep)) !== 0) { return SQ_EINTERNAL; }
    }
    return 0;
  }
  function stFlushResult() {
    var s = "";
    var i;
    for (i = 0; i < _stOutLen; i++) { s = s + String.fromCharCode(_stOut[i]); }
    _stResult.code = 0;
    _stResult.out = s;
    return 0;
  }

  /* 对外:隔离(显式栈二分,深度 ≤ 48;结果串入 _stResult.out) */
  function sturmIsolate(src) {
    var rc = sturmPrepare(src);
    _stResult.code = rc;
    _stResult.out = "";
    if (rc !== 0) { return rc; }
    _stOutLen = 0;
    var dg = _stDeg[0];
    var i;
    var j;
    var q;
    if (dg === 0) {
      if (stPutSCC(91) !== 0 || stPutSCC(93) !== 0) { return SQ_EINTERNAL; }
      return stFlushResult();
    }
    /* Cauchy 界上取整到 2 的幂:全整数精确比较 snap·|c_n| ≥ |c_n| + max|c_i| */
    var cn = _stC[_stOff[0] + dg];
    if (cn < 0) { cn = -cn; }
    var mxc = 0;
    for (i = 0; i < dg; i++) {
      var ai = _stC[_stOff[0] + i];
      if (ai < 0) { ai = -ai; }
      if (ai > mxc) { mxc = ai; }
    }
    var snap = 1;
    while (snap * cn < cn + mxc) {
      snap = snap * 2;
      if (snap > 1048576) { return SQ_EINTERNAL; }
    }
    var ncells = 0;
    var z = stSignAt(0, -snap, 0);
    if (z === SQ_ECOEF) { return SQ_ECOEF; }
    if (z === 0) {
      _stCellLo[0] = -snap;
      _stCellHi[0] = -snap;
      _stCellD[0] = 0;
      _stCellD[1] = 0;
      ncells = 1;
    }
    var sp = 0;
    _stLo[sp] = -snap;
    _stHi[sp] = snap;
    _stDep[sp] = 0;
    sp++;
    while (sp > 0) {
      sp--;
      var lo = _stLo[sp];
      var hi = _stHi[sp];
      var dep = _stDep[sp];
      var nlo = stVariations(lo, dep);
      if (nlo === SQ_ECOEF) { return SQ_ECOEF; }
      var nhi = stVariations(hi, dep);
      if (nhi === SQ_ECOEF) { return SQ_ECOEF; }
      var n = nlo - nhi;
      if (n === 0) { continue; }
      if (n === 1) {
        if (ncells >= ST_CELLS) { return SQ_EINTERNAL; }
        _stCellLo[ncells] = lo;
        _stCellHi[ncells] = hi;
        _stCellD[ncells * 2] = dep;
        _stCellD[ncells * 2 + 1] = 0;
        ncells++;
        continue;
      }
      if (dep >= ST_BDEPTH) {
        if (ncells >= ST_CELLS) { return SQ_EINTERNAL; }
        _stCellLo[ncells] = lo;
        _stCellHi[ncells] = hi;
        _stCellD[ncells * 2] = dep;
        _stCellD[ncells * 2 + 1] = 1;
        ncells++;
        continue;
      }
      if (sp + 2 > ST_BSTACK) { return SQ_EINTERNAL; }
      _stLo[sp] = lo * 2; _stHi[sp] = lo + hi; _stDep[sp] = dep + 1; sp++;
      _stLo[sp] = lo + hi; _stHi[sp] = hi * 2; _stDep[sp] = dep + 1; sp++;
    }
    /* 按 lo 升序(格不交,值比较可分辨;插入排序稳定) */
    for (i = 1; i < ncells; i++) {
      var klo = _stCellLo[i];
      var kdep = _stCellD[i * 2];
      var kv = klo / Math.pow(2, kdep);
      var khi = _stCellHi[i];
      var kund = _stCellD[i * 2 + 1];
      j = i - 1;
      while (j >= 0) {
        var jv = _stCellLo[j] / Math.pow(2, _stCellD[j * 2]);
        if (jv <= kv) { break; }
        _stCellLo[j + 1] = _stCellLo[j];
        _stCellHi[j + 1] = _stCellHi[j];
        _stCellD[(j + 1) * 2] = _stCellD[j * 2];
        _stCellD[(j + 1) * 2 + 1] = _stCellD[j * 2 + 1];
        j--;
      }
      _stCellLo[j + 1] = klo;
      _stCellHi[j + 1] = khi;
      _stCellD[(j + 1) * 2] = kdep;
      _stCellD[(j + 1) * 2 + 1] = kund;
    }
    /* 渲染:既约分数;未解析 "!" */
    if (stPutSCC(91) !== 0) { return SQ_EINTERNAL; }
    for (i = 0; i < ncells; i++) {
      var lo2 = _stCellLo[i];
      var hi2 = _stCellHi[i];
      var dlo = _stCellD[i * 2];
      var dhi = _stCellD[i * 2];
      if (i > 0) {
        if (stPutSCC(44) !== 0 || stPutSCC(32) !== 0) { return SQ_EINTERNAL; }
      }
      if (stPutSCC(91) !== 0) { return SQ_EINTERNAL; }
      while (dlo > 0 && (lo2 % 2) === 0) { lo2 = lo2 / 2; dlo--; }
      while (dhi > 0 && (hi2 % 2) === 0) { hi2 = hi2 / 2; dhi--; }
      if (stPutRat(lo2, dlo) !== 0) { return SQ_EINTERNAL; }
      if (stPutSCC(44) !== 0 || stPutSCC(32) !== 0) { return SQ_EINTERNAL; }
      if (stPutRat(hi2, dhi) !== 0) { return SQ_EINTERNAL; }
      if (stPutSCC(93) !== 0) { return SQ_EINTERNAL; }
      if (_stCellD[i * 2 + 1] === 1) {
        if (stPutSCC(33) !== 0) { return SQ_EINTERNAL; }
      }
    }
    if (stPutSCC(93) !== 0) { return SQ_EINTERNAL; }
    return stFlushResult();
  }

  return {
    sqfree: sqfree,
    reset: reset,
    allocCount: allocCount,

    /* ---- v2.5.0 F3:Sturm 序列与实根隔离(契约 sturm-format-v25,deg ≤ 4 镜像) ---- */
    sturmCount: sturmCount,
    sturmIsolate: sturmIsolate,
    getSturmString: getSturmString,
    sturmAllocCount: sturmAllocCount
  };
})();

if (typeof module === "object" && module && module.exports) {
  module.exports.ApolloSqfree = ApolloSqfree;
}
