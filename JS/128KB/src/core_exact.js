/* ============================================================
 * ApolloCalc 文件 4：core_exact.js
 * 双模态精确数值系统（精确形式 <-> 8 位小数近似）
 * 内部表示固定为五分量：
 *   value = ((exactRat + exactRadNum·√exactRad) / exactDen) × 10^exactExp
 *   - exactRat：有理部分子（0 表示无有理部）
 *   - exactRadNum：根式部系数（0 表示无根式）
 *   - exactRad：根号内数（无平方因子的正整数，1 表示无根式）
 *   - exactDen：公共分母（恒为正整数）
 *   - exactExp：科学计数法指数（整数，0 为主）
 *   支持黄金比例 (1+√5)/2 这类"有理部 + 根式部"混合形式
 * 规范化不变式（exactNormalize 保证）：
 *   - exactDen > 0
 *   - exactRadNum === 0 ⟺ exactRad === 1
 *   - gcd(|exactRat|, |exactRadNum|, exactDen) = 1
 *   - 值为零 ⟺ 五分量全规范化为 0/0/1/1/0
 *   - 任一分量绝对值超出 9007199254740991（2^53-1）即置 exactIsValid = false
 * 本文件直接引用文件 1（core_parser.js）已声明的全局变量：
 *   exactRat / exactRadNum / exactRad / exactDen / exactExp /
 *   exactIsValid / approxValue
 * 本文件无需额外全局暂存量：所有中间量均为函数内 var 标量（零分配）。
 * ============================================================
 */

/* 精确系统分量上限（2^53-1）：超出即视为无法精确表示 */
var EXACT_INT_MAX = 9007199254740991;

/* ============================================================
 * 模块一：基础工具
 * ============================================================ */

function exactGcd(a, b) {
  /* 欧几里得算法（迭代版）：零分配，仅局部标量，无递归 */
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

function exactSimplifyRadical(D) {
  /* 提取完全平方因子：返回外系数 k，原地写 exactRad 为无平方因子部分
     试除法：命中 i*i 即从 D 中除尽、外系数乘 i
     零分配：仅局部标量 d / k / i */
  var d = D;
  var k = 1;
  if (d <= 0) {
    /* 调用方保证 D >= 1；D === 0 时外系数为 0，根号内置 1 */
    exactRad = 1;
    return 0;
  }
  for (var i = 2; i * i <= d; i++) {
    while (d % (i * i) === 0) {
      d = d / (i * i);
      k = k * i;
    }
  }
  exactRad = d;
  return k;
}

/* ============================================================
 * 模块二：规范化（唯一表示入口，每次构造/运算后调用）
 * ============================================================ */

function exactNormalize() {
  /* 原地规范化全局五分量；失败路径置 exactIsValid = false 并保留 approxValue。
     成功路径末尾同步 approxValue。零分配：仅局部标量。 */

  /* 0. 防御：非有限分量（NaN/Inf）直接失效 */
  if (!isFinite(exactRat) || !isFinite(exactRadNum) || !isFinite(exactRad) ||
      !isFinite(exactDen) || !isFinite(exactExp)) {
    exactIsValid = false;
    return;
  }
  /* 根号内数非正整数非法（负数开方为复数、0 的根式部恒为零） */
  if (exactRad < 1) {
    exactIsValid = false;
    return;
  }

  /* 1. 分母恒正：den < 0 时所有分子变号 */
  if (exactDen < 0) {
    exactRat = -exactRat;
    exactRadNum = -exactRadNum;
    exactDen = -exactDen;
  }
  if (exactDen === 0) {
    exactIsValid = false;
    return;
  }

  /* 2. 根号内数化简：提取完全平方因子，外系数并入根式部系数 */
  if (exactRadNum !== 0 && exactRad > 1) {
    if (exactRad > EXACT_INT_MAX) {
      exactIsValid = false;
      return;
    }
    var k = exactSimplifyRadical(exactRad);   /* 原地写 exactRad */
    if (k > 1) {
      var abnK = exactRadNum < 0 ? -exactRadNum : exactRadNum;
      if (abnK > EXACT_INT_MAX / k) {
        exactIsValid = false;
        return;
      }
      exactRadNum = exactRadNum * k;
    }
  }

  /* 3. 根式部系数为 0 → 根号内置 1 */
  if (exactRadNum === 0) {
    exactRad = 1;
  } else if (exactRad === 1) {
    /* 4. 根号内数为 1 → 根式部并入有理部（同号相加预检） */
    var ar0 = exactRat < 0 ? -exactRat : exactRat;
    var ab0 = exactRadNum < 0 ? -exactRadNum : exactRadNum;
    if (ar0 > EXACT_INT_MAX - ab0) {
      exactIsValid = false;
      return;
    }
    exactRat = exactRat + exactRadNum;
    exactRadNum = 0;
    exactRad = 1;
  }

  /* 5. 三数（rat, radNum, den）取最大公约数约分 */
  var ar = exactRat < 0 ? -exactRat : exactRat;
  var abn = exactRadNum < 0 ? -exactRadNum : exactRadNum;
  var g1 = exactGcd(ar, exactDen);
  var g2 = exactGcd(abn, exactDen);
  var g = exactGcd(g1, g2);
  if (g > 1) {
    exactRat = exactRat / g;
    exactRadNum = exactRadNum / g;
    exactDen = exactDen / g;
    ar = ar / g;
    abn = abn / g;
  }

  /* 6. 零规范化为 0/0/1/1/0 */
  if (exactRat === 0 && exactRadNum === 0) {
    exactRat = 0;
    exactRadNum = 0;
    exactRad = 1;
    exactDen = 1;
    exactExp = 0;
  }

  /* 7. 溢出缓解：分量超限时把 10 的因子在分子/分母与 10^exactExp 之间转移。
     值恒不变：分子两项同除 10 时 exp 增 1；分母除 10 时 exp 减 1。
     根号内数不参与转移（√(r/10) 会改变根式基）。 */
  while (ar > EXACT_INT_MAX || abn > EXACT_INT_MAX || exactDen > EXACT_INT_MAX) {
    if ((ar > EXACT_INT_MAX || abn > EXACT_INT_MAX) &&
        ar % 10 === 0 && abn % 10 === 0) {
      exactRat = exactRat / 10;
      exactRadNum = exactRadNum / 10;
      exactExp = exactExp + 1;
      ar = ar / 10;
      abn = abn / 10;
    } else if (exactDen > EXACT_INT_MAX && exactDen % 10 === 0) {
      exactDen = exactDen / 10;
      exactExp = exactExp - 1;
    } else {
      break;
    }
  }
  if (ar > EXACT_INT_MAX || abn > EXACT_INT_MAX || exactDen > EXACT_INT_MAX) {
    /* 仍超限（无 10 因子可提取）：精确模式失效 */
    exactIsValid = false;
    return;
  }

  exactUpdateApprox();
}

/* ============================================================
 * 模块三：智能构造函数（先赋五分量初值，再经 exactNormalize 规范化）
 * ============================================================ */

function exactSetFraction(n, d) {
  /* 设置精确数值为分数 n/d；零分配，原地更新五分量 */
  if (d === 0) {
    exactInvalidate();
    return;
  }
  var an = n < 0 ? -n : n;
  var ad = d < 0 ? -d : d;
  if (!(an <= EXACT_INT_MAX) || !(ad <= EXACT_INT_MAX)) {
    /* 分量超限（含 NaN）：精确模式失效，保留近似值供显示层兜底 */
    exactIsValid = false;
    approxValue = n / d;
    return;
  }
  exactRat = n;
  exactRadNum = 0;
  exactRad = 1;
  exactDen = d;
  exactExp = 0;
  exactIsValid = true;
  exactNormalize();
}

function exactSetRadical(k, m) {
  /* 设置精确数值为 k·√m；零分配，原地更新 */
  if (m < 1) {
    /* 负数开方产生复数；m === 0 时 k·√0 = 0 按纯分数零处理 */
    if (m === 0) { exactSetFraction(0, 1); return; }
    exactInvalidate();
    return;
  }
  var ak = k < 0 ? -k : k;
  if (!(ak <= EXACT_INT_MAX) || !(m <= EXACT_INT_MAX)) {
    exactIsValid = false;
    approxValue = k * Math.sqrt(m);
    return;
  }
  /* 完全平方判定：s = round(√m)，s*s === m 则化为纯分数 k·s */
  var s = Math.floor(Math.sqrt(m) + 0.5);
  if (s * s === m) {
    var p = k * s;
    if (!(p <= EXACT_INT_MAX) || !(p >= -EXACT_INT_MAX)) {
      exactIsValid = false;
      approxValue = k * Math.sqrt(m);
      return;
    }
    exactSetFraction(p, 1);
    return;
  }
  /* 非完全平方：先化简根号内数（原地写 exactRad），外系数并入根式部系数 */
  var k2 = exactSimplifyRadical(m);
  var n = k * k2;
  if (!(n <= EXACT_INT_MAX) || !(n >= -EXACT_INT_MAX)) {
    exactIsValid = false;
    approxValue = k * Math.sqrt(m);
    return;
  }
  exactRat = 0;
  exactRadNum = n;
  exactDen = 1;
  exactExp = 0;
  exactIsValid = true;
  exactNormalize();
}

function exactSetMixed(a, b, c, d) {
  /* 设置精确数值为 (a + b·√c)/d —— 五分量模型新增的混合形式入口
     黄金比例即 exactSetMixed(1, 1, 5, 2)；零分配，原地更新 */
  if (d === 0) {
    exactInvalidate();
    return;
  }
  if (c === 0) {
    /* b·√0 = 0：退化为纯分数 a/d */
    exactSetFraction(a, d);
    return;
  }
  if (c < 1) {
    /* 负数开方产生复数，不进入精确系统 */
    exactInvalidate();
    return;
  }
  var aa = a < 0 ? -a : a;
  var ab = b < 0 ? -b : b;
  var ad = d < 0 ? -d : d;
  if (!(aa <= EXACT_INT_MAX) || !(ab <= EXACT_INT_MAX) ||
      !(c <= EXACT_INT_MAX) || !(ad <= EXACT_INT_MAX)) {
    exactIsValid = false;
    approxValue = (a + b * Math.sqrt(c)) / d;
    return;
  }
  exactRat = a;
  exactRadNum = b;
  exactRad = c;
  exactDen = d;
  exactExp = 0;
  exactIsValid = true;
  exactNormalize();
}

function exactSetSci(v, e) {
  /* 设置精确数值为 v × 10^e：整数 v 走分数构造后挂指数，
     非整数 v 无法精确表示，仅保留完整近似值 */
  if (!(e === Math.floor(e))) {
    /* 非整数指数破坏五分量不变式：失效并保留近似值 */
    exactIsValid = false;
    approxValue = v * Math.pow(10, e);
    return;
  }
  if (v === Math.floor(v)) {
    exactSetFraction(v, 1);
  } else {
    exactSetPureReal(v);
  }
  if (exactIsValid) {
    if (exactRat !== 0 || exactRadNum !== 0) {
      exactExp = e;
    }
    exactUpdateApprox();
  } else {
    approxValue = v * Math.pow(10, e);
  }
}

function exactSetPureReal(v) {
  /* 设置精确数值为浮点数 v：仅整数可进入精确系统，其余只保留近似值 */
  if (v === Math.floor(v) && v <= EXACT_INT_MAX && v >= -EXACT_INT_MAX) {
    exactSetFraction(v, 1);
    return;
  }
  exactIsValid = false;
  approxValue = v;
}

function exactInvalidate() {
  /* 精确模式失效：仅保留 approxValue（调用方负责同步） */
  exactIsValid = false;
}

function exactReset() {
  /* 复位全部精确数值状态（零分配，纯标量赋值） */
  exactRat = 0;
  exactRadNum = 0;
  exactRad = 1;
  exactDen = 1;
  exactExp = 0;
  exactIsValid = false;
  approxValue = 0;
}

/* ============================================================
 * 模块四：近似值同步
 * ============================================================ */

function exactUpdateApprox() {
  /* 零分配：仅浮点运算，approxValue 原地重算 */
  approxValue = ((exactRat + exactRadNum * Math.sqrt(exactRad)) / exactDen) *
                Math.pow(10, exactExp);
}

/* ============================================================
 * 模块五：精确数值的四则运算（原地更新五分量）
 * 操作数为四分量 (rat2 + radNum2·√rad2)/den2（指数视为 0）。
 * 当前全局值带非零指数（exactSetSci 产物）时暂不参与混合运算，置失效。
 * ============================================================ */

function exactAdd(rat2, radNum2, rad2, den2) {
  /* 当前精确数 + (rat2 + radNum2·√rad2)/den2
     同类根式（r === r'，或一方根式部为零）才能合并：
       newRat    = a·d' + a'·d
       newRadNum = b·d' + b'·d
       newRad    = b === 0 ? r' : r
       newDen    = d·d'
     根式不匹配（√2 + √3）五分量无法表达 → 失效
     零分配：交叉相乘全部使用局部标量，先溢出预检再写全局 */
  if (!exactIsValid || exactExp !== 0) {
    exactInvalidate();
    return;
  }
  if (den2 === 0 || !(rad2 >= 1)) {
    exactInvalidate();
    return;
  }
  var an2 = rat2 < 0 ? -rat2 : rat2;
  var ab2 = radNum2 < 0 ? -radNum2 : radNum2;
  var ad2 = den2 < 0 ? -den2 : den2;
  if (!(an2 <= EXACT_INT_MAX) || !(ab2 <= EXACT_INT_MAX) ||
      !(rad2 <= EXACT_INT_MAX) || !(ad2 <= EXACT_INT_MAX)) {
    exactInvalidate();
    return;
  }
  /* 局部副本：右操作数根号内为 1 时并入有理部（不变式归一） */
  var n2 = rat2;
  var b2 = radNum2;
  var r2 = rad2;
  if (r2 === 1 && b2 !== 0) {
    if (an2 > EXACT_INT_MAX - ab2) {
      exactInvalidate();
      return;
    }
    n2 = n2 + b2;
    b2 = 0;
    an2 = n2 < 0 ? -n2 : n2;
  }
  var a = exactRat;
  var b = exactRadNum;
  var r = exactRad;
  var d = exactDen;
  var an = a < 0 ? -a : a;
  var ab = b < 0 ? -b : b;
  /* 根式不匹配：双方根式部均非零且 r ≠ r' */
  if (b !== 0 && b2 !== 0 && r !== r2) {
    exactInvalidate();
    return;
  }
  /* 溢出预检：|a·d'|、|a'·d|、d·d' 均不得超限 */
  if (an > EXACT_INT_MAX / ad2 ||
      an2 > EXACT_INT_MAX / d ||
      d > EXACT_INT_MAX / ad2) {
    exactInvalidate();
    return;
  }
  var p1 = a * den2;             /* a·d'，模已验 ≤ 2^53-1，精确 */
  var p2 = n2 * d;               /* a'·d，同上 */
  /* 同号相加结果可能越过 2^53：用减法预检（差值本身精确） */
  if ((p1 >= 0 && p2 >= 0) || (p1 < 0 && p2 < 0)) {
    var ap1 = p1 < 0 ? -p1 : p1;
    var ap2 = p2 < 0 ? -p2 : p2;
    if (ap1 > EXACT_INT_MAX - ap2) {
      exactInvalidate();
      return;
    }
  }
  var q1 = 0;
  var q2 = 0;
  if (b !== 0) {
    if (ab > EXACT_INT_MAX / ad2) {
      exactInvalidate();
      return;
    }
    q1 = b * den2;               /* b·d' */
  }
  if (b2 !== 0) {
    if (ab2 > EXACT_INT_MAX / d) {
      exactInvalidate();
      return;
    }
    q2 = b2 * d;                 /* b'·d */
  }
  if ((q1 >= 0 && q2 >= 0) || (q1 < 0 && q2 < 0)) {
    var aq1 = q1 < 0 ? -q1 : q1;
    var aq2 = q2 < 0 ? -q2 : q2;
    if (aq1 > EXACT_INT_MAX - aq2) {
      exactInvalidate();
      return;
    }
  }
  exactRat = p1 + p2;            /* 和或差的模 ≤ 2^53-1，精确 */
  exactRadNum = q1 + q2;
  exactRad = (b === 0) ? r2 : r; /* 左方纯有理时保留右方根式 */
  exactDen = d * den2;           /* 模已验，符号交给规范化翻转 */
  exactIsValid = true;
  exactNormalize();
}

function exactSub(rat2, radNum2, rad2, den2) {
  /* 当前精确数 - (rat2 + radNum2·√rad2)/den2
     与 exactAdd 对称：操作数整体变号后复用加法全部预检路径 */
  exactAdd(-rat2, -radNum2, rad2, den2);
}

function exactMul(rat2, radNum2, rad2, den2) {
  /* 当前精确数 * (rat2 + radNum2·√rad2)/den2
     展开 (a + b√r)(a' + b'√r') = a·a' + (a·b' + a'·b)√r + b·b'·r （r === r' 时）
     一方根式部为零退化为有理×混合；r ≠ r' 且双方根式部非零时出现
     √r、√r'、√(r·r') 三个根式项，五分量无法表达 → 失效
     零分配：先溢出预检再写全局 */
  if (!exactIsValid || exactExp !== 0) {
    exactInvalidate();
    return;
  }
  if (den2 === 0 || !(rad2 >= 1)) {
    exactInvalidate();
    return;
  }
  var an2 = rat2 < 0 ? -rat2 : rat2;
  var ab2 = radNum2 < 0 ? -radNum2 : radNum2;
  var ad2 = den2 < 0 ? -den2 : den2;
  if (!(an2 <= EXACT_INT_MAX) || !(ab2 <= EXACT_INT_MAX) ||
      !(rad2 <= EXACT_INT_MAX) || !(ad2 <= EXACT_INT_MAX)) {
    exactInvalidate();
    return;
  }
  var n2 = rat2;
  var b2 = radNum2;
  var r2 = rad2;
  if (r2 === 1 && b2 !== 0) {
    if (an2 > EXACT_INT_MAX - ab2) {
      exactInvalidate();
      return;
    }
    n2 = n2 + b2;
    b2 = 0;
    an2 = n2 < 0 ? -n2 : n2;
  }
  if ((exactRat === 0 && exactRadNum === 0) || (n2 === 0 && b2 === 0)) {
    /* 零乘任何数仍为零，精确可表示 */
    exactSetFraction(0, 1);
    return;
  }
  var a = exactRat;
  var b = exactRadNum;
  var r = exactRad;
  var d = exactDen;
  var an = a < 0 ? -a : a;
  var ab = b < 0 ? -b : b;
  if (b !== 0 && b2 !== 0 && r !== r2) {
    /* 三根式项不可表达 */
    exactInvalidate();
    return;
  }
  /* 公共预检：有理部主项 |a·a'| 与分母 d·d' */
  if (an > EXACT_INT_MAX / an2 ||
      d > EXACT_INT_MAX / ad2) {
    exactInvalidate();
    return;
  }
  var pRat = a * n2;             /* 有理部主项，模已验，精确 */
  var pDen = d * den2;           /* 模已验，符号交给规范化翻转 */
  var pRad = 0;
  var newRad = (b === 0) ? r2 : r;
  if (b !== 0 && b2 !== 0) {
    /* r === r'：有理部补交叉项 b·b'·r，根式部 a·b' + a'·b */
    if (ab > EXACT_INT_MAX / ab2) {
      exactInvalidate();
      return;
    }
    var bb = b * b2;             /* 模已验 */
    var abb = bb < 0 ? -bb : bb;
    if (abb > EXACT_INT_MAX / r) {
      exactInvalidate();
      return;
    }
    var cross = bb * r;          /* 模已验，精确 */
    if ((pRat >= 0 && cross >= 0) || (pRat < 0 && cross < 0)) {
      var ap = pRat < 0 ? -pRat : pRat;
      var ac = cross < 0 ? -cross : cross;
      if (ap > EXACT_INT_MAX - ac) {
        exactInvalidate();
        return;
      }
    }
    pRat = pRat + cross;
    if (ab > EXACT_INT_MAX / an2 ||
        an > EXACT_INT_MAX / ab2) {
      exactInvalidate();
      return;
    }
    var u1 = b * n2;             /* a'·b */
    var u2 = a * b2;             /* a·b' */
    if ((u1 >= 0 && u2 >= 0) || (u1 < 0 && u2 < 0)) {
      var au1 = u1 < 0 ? -u1 : u1;
      var au2 = u2 < 0 ? -u2 : u2;
      if (au1 > EXACT_INT_MAX - au2) {
        exactInvalidate();
        return;
      }
    }
    pRad = u1 + u2;
  } else if (b !== 0) {
    /* 左根式 × 右有理：根式部 b·a' */
    if (ab > EXACT_INT_MAX / an2) {
      exactInvalidate();
      return;
    }
    pRad = b * n2;
  } else if (b2 !== 0) {
    /* 左有理 × 右根式：根式部 a·b' */
    if (an > EXACT_INT_MAX / ab2) {
      exactInvalidate();
      return;
    }
    pRad = a * b2;
  }
  exactRat = pRat;
  exactRadNum = pRad;
  exactRad = newRad;
  exactDen = pDen;
  exactIsValid = true;
  exactNormalize();
}

function exactDiv(rat2, radNum2, rad2, den2) {
  /* 当前精确数 ÷ (rat2 + radNum2·√rad2)/den2
     分子分母同乘共轭 (a' - b'√r') 有理化，分母化为纯有理：
       分母 = d·(a'² - b'²·r')          （合并后 a'² ≠ b'²·r'，防御性检查兜底）
       分子 = [(a·a' - b·b'·r) + (b·a' - a·b')√r]·d'   （r === r' 时）
     左方纯有理（b=0）时无需 r === r'，结果保留右方根式 √r'
     r ≠ r' 且双方根式部非零 → 三根式项 → 失效
     零分配：先溢出预检再写全局 */
  if (!exactIsValid || exactExp !== 0) {
    exactInvalidate();
    return;
  }
  if (den2 === 0 || !(rad2 >= 1)) {
    exactInvalidate();
    return;
  }
  var an2 = rat2 < 0 ? -rat2 : rat2;
  var ab2 = radNum2 < 0 ? -radNum2 : radNum2;
  var ad2 = den2 < 0 ? -den2 : den2;
  if (!(an2 <= EXACT_INT_MAX) || !(ab2 <= EXACT_INT_MAX) ||
      !(rad2 <= EXACT_INT_MAX) || !(ad2 <= EXACT_INT_MAX)) {
    exactInvalidate();
    return;
  }
  var n2 = rat2;
  var b2 = radNum2;
  var r2 = rad2;
  if (r2 === 1 && b2 !== 0) {
    if (an2 > EXACT_INT_MAX - ab2) {
      exactInvalidate();
      return;
    }
    n2 = n2 + b2;
    b2 = 0;
    an2 = n2 < 0 ? -n2 : n2;
  }
  if (n2 === 0 && b2 === 0) {
    /* 除数为零值 */
    exactInvalidate();
    return;
  }
  if (exactRat === 0 && exactRadNum === 0) {
    /* 0 除以非零值仍为 0，精确可表示 */
    exactSetFraction(0, 1);
    return;
  }
  var a = exactRat;
  var b = exactRadNum;
  var r = exactRad;
  var d = exactDen;
  var an = a < 0 ? -a : a;
  var ab = b < 0 ? -b : b;
  if (b2 === 0) {
    /* ÷ 纯分数 n2/den2：分子乘 den2，分母乘 n2（n2 符号入分母，规范化翻正） */
    if (an > EXACT_INT_MAX / ad2 ||
        ab > EXACT_INT_MAX / ad2 ||
        d > EXACT_INT_MAX / an2) {
      exactInvalidate();
      return;
    }
    exactRat = a * den2;
    exactRadNum = b * den2;
    exactRad = r;
    exactDen = d * n2;
    exactIsValid = true;
    exactNormalize();
    return;
  }
  /* ÷ 含根式：共轭有理化 */
  if (b !== 0 && r !== r2) {
    /* 三根式项不可表达 */
    exactInvalidate();
    return;
  }
  var newRad = (b === 0) ? r2 : r;
  /* 分母：d·(n2² - b2²·r2)，整数差 |diff| ≤ max(两正项) ≤ 2^53-1，精确 */
  if (!(an2 <= EXACT_INT_MAX / an2) ||
      !(ab2 <= EXACT_INT_MAX / ab2)) {
    exactInvalidate();
    return;
  }
  var n2sq = an2 * an2;          /* 模已验 */
  var b2sq = ab2 * ab2;          /* 模已验 */
  if (b2sq > EXACT_INT_MAX / r2) {
    exactInvalidate();
    return;
  }
  var diff = n2sq - b2sq * r2;   /* 模已验，精确 */
  if (diff === 0) {
    /* 共轭退化（合并后方根式部非零时不可达，防御） */
    exactInvalidate();
    return;
  }
  var adiff = diff < 0 ? -diff : diff;
  if (d > EXACT_INT_MAX / adiff) {
    exactInvalidate();
    return;
  }
  /* 分子有理部：a·n2 - b·b2·r（b=0 时退化为 a·n2） */
  if (an > EXACT_INT_MAX / an2) {
    exactInvalidate();
    return;
  }
  var t1 = a * n2;               /* 模已验 */
  var t2 = 0;
  if (b !== 0) {
    if (ab > EXACT_INT_MAX / ab2) {
      exactInvalidate();
      return;
    }
    var bbp = b * b2;            /* 模已验 */
    var abbp = bbp < 0 ? -bbp : bbp;
    if (abbp > EXACT_INT_MAX / r) {
      exactInvalidate();
      return;
    }
    t2 = bbp * r;                /* 模已验，精确 */
  }
  /* t1 - t2：异号相减等价于同号相加，和检 */
  if ((t1 >= 0 && t2 < 0) || (t1 < 0 && t2 >= 0)) {
    var at1 = t1 < 0 ? -t1 : t1;
    var at2 = t2 < 0 ? -t2 : t2;
    if (at1 > EXACT_INT_MAX - at2) {
      exactInvalidate();
      return;
    }
  }
  var t = t1 - t2;               /* 同号相减的模 ≤ max(|t1|,|t2|)，精确 */
  /* 分子根式部：b·n2 - a·b2 */
  if (ab > EXACT_INT_MAX / an2 ||
      an > EXACT_INT_MAX / ab2) {
    exactInvalidate();
    return;
  }
  var u1 = b * n2;               /* 模已验 */
  var u2 = a * b2;               /* 模已验 */
  if ((u1 >= 0 && u2 < 0) || (u1 < 0 && u2 >= 0)) {
    var au1 = u1 < 0 ? -u1 : u1;
    var au2 = u2 < 0 ? -u2 : u2;
    if (au1 > EXACT_INT_MAX - au2) {
      exactInvalidate();
      return;
    }
  }
  var u = u1 - u2;
  /* 分子整体再乘 den2：|t|、|u| 与 |den2| 交叉预检 */
  var at = t < 0 ? -t : t;
  var au = u < 0 ? -u : u;
  if (at > EXACT_INT_MAX / ad2 ||
      au > EXACT_INT_MAX / ad2) {
    exactInvalidate();
    return;
  }
  exactRat = t * den2;
  exactRadNum = u * den2;
  exactRad = newRad;
  exactDen = d * diff;           /* diff 符号入分母，规范化翻正 */
  exactIsValid = true;
  exactNormalize();
}

/* ============================================================
 * 模块六：从判别式 D 提取精确根
 * ============================================================ */

function exactFromDiscriminant(D) {
  /* 文件 5 方程求解器接口：由判别式 D 提取 √D = (rat + radNum·√rad)/den
     保守策略：双精度入口下 D 只能是有理数，√D 的结果只可能是
     纯有理数（D 为完全平方）或纯根式 k√m，混合形式 √(p + q√r)
     需要无理 D，本入口不涉及（由五分量组装层自然覆盖）
     D < 0（复数根）或非有限值不进入精确系统
     零分配：仅局部标量 */
  if (!isFinite(D) || D < 0) {
    exactInvalidate();
    return;
  }
  if (D === 0) {
    /* √0 = 0，按纯分数零处理 */
    exactSetFraction(0, 1);
    return;
  }
  if (D === Math.floor(D)) {
    /* 整数判别式：直接化简根式 k√m（exactSimplifyRadical 原地写 exactRad） */
    if (!(D <= EXACT_INT_MAX)) {
      exactInvalidate();
      return;
    }
    var k = exactSimplifyRadical(D);
    exactRat = 0;
    exactRadNum = k;
    exactDen = 1;
    exactExp = 0;
    exactIsValid = true;
    exactNormalize();            /* rad === 1（完全平方）时并入有理部 */
    return;
  }
  /* 简单分数判定：寻找最小分母 d 使 D*d 为整数（D>0，无符号负担） */
  var foundDen = 0;
  var foundNum = 0;
  for (var dd = 2; dd <= 1000000; dd++) {
    var v = D * dd;
    if (v > EXACT_INT_MAX) {
      /* 分子超限：放弃精确表示 */
      break;
    }
    var rr = Math.round(v);
    if (v === rr || (v - rr < 1e-9 && rr - v < 1e-9)) {
      foundDen = dd;
      foundNum = rr;
      break;
    }
  }
  if (foundDen === 0) {
    /* 无限小数或超出搜索范围：精确模式失效 */
    exactInvalidate();
    return;
  }
  /* √(num/den) = √(num·den)/den：根号内通分后化简 */
  if (foundNum > EXACT_INT_MAX / foundDen) {
    exactInvalidate();
    return;
  }
  var m = foundNum * foundDen;        /* 模已验，精确 */
  var k2 = exactSimplifyRadical(m);   /* 原地写 exactRad */
  exactRat = 0;
  exactRadNum = k2;
  exactDen = foundDen;
  exactExp = 0;
  exactIsValid = true;
  exactNormalize();                   /* 通分约分 + 完全平方并入有理部 */
}
