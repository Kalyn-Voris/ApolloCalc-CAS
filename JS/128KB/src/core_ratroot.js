/* ============================================================
 * ApolloCalc · core_ratroot.js — 有理根检验与低次多项式因式分解（文件 9）
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
 *   两者相距多个数量级；商系数按高斯引理必为整数，提交前逐项近整复核。
 * ============================================================ */

/* ==================== 文件 9 专属全局变量（唯一声明处） ==================== */
var _ratRootCandidates = new Int32Array(64);   /* 候选根缓存：p/q 交错存放（至多 32 组） */
var _ratRootCount = 0;                         /* 最近一次枚举生成的候选数 */
var _ratRootFound = 0;                         /* 实际找到的根数（显式栈深度，0..8） */
var _ratRootValues = new Float64Array(8);      /* 根值（发现顺序，栈） */
var _ratRootIsExact = new Int32Array(8);       /* 每个根是否精确（有理根恒 1） */
var _ratRootNum = new Int32Array(8);           /* 精确形式分子 p（与 _ratRootValues 配对） */
var _ratRootDen = new Int32Array(8);           /* 精确形式分母 q（>0，已约简） */
var _ratRootWork = new Float64Array(16);       /* 降次工作区：当前整系数多项式（升幂） */
var _ratRootTrial = new Float64Array(16);      /* 综合除法试除区（失败即弃，不污染 work） */
var _ratRootDivs = new Int32Array(128);        /* 常数项 |a0| 的因数表（试除到 √） */

/* 整数判定与枚举上限：采样误差容差 1e-6（相对）；|an|/|a0| 超过 1e7 跳过枚举 */
var RATROOT_INT_TOL = 1e-6;
var RATROOT_ENUM_MAX = 10000000;

/* ==================== 模块一：基础工具 ==================== */

/* 欧几里得 gcd（迭代，无递归；入参非负） */
function ratRootGcd(a, b) {
  var x = a;
  var y = b;
  var t = 0;
  while (y !== 0) {
    t = x % y;
    x = y;
    y = t;
  }
  return x;
}

/* 显式栈入栈：根值 p/q（自动约简、分母翻正）；栈满（8）丢弃 */
function ratRootPush(p, q) {
  if (_ratRootFound >= 8) { return; }
  if (q < 0) { p = -p; q = -q; }
  var g = ratRootGcd(p < 0 ? -p : p, q);
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

/* ==================== 模块二：整系数快照与提交 ==================== */

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

/* ==================== 模块三：候选枚举 ==================== */

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
  var g = ratRootGcd(p < 0 ? -p : p, q);
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

/* ==================== 模块四：综合除法（升幂布局） ====================
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

/* ==================== 模块五：主入口（迭代降次，显式栈） ====================
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

/* ==================== 模块六：收尾合并（供 solveCubic/solveQuartic 调用） ====================
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
