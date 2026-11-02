// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · core_exact.js — 双模态精确数值系统（精确分数/根式 <-> 8 位小数近似）
 * 内部表示固定为：(exactNum / exactDen) * √(exactRad)
 *   - exactDen 恒为正整数
 *   - exactRad 为无平方因子的正整数（1 表示无根式）
 *   - 任一分量超出 9007199254740991（2^53-1）即置 exactIsValid = false
 * 直接引用文件 1（core_parser.js）已声明的全局变量：
 *   exactNum / exactDen / exactRad / exactIsValid / approxValue
 * 无需额外全局暂存：所有中间量均为函数内 let 标量（零分配）
 */

/* 模块一：基础工具 */

function exactGcd(a, b) {
  /* 欧几里得算法（迭代版，无递归） */
  let x = a;
  let y = b;
  let t = 0;
  while (y !== 0) {
    t = x % y;
    x = y;
    y = t;
  }
  return x;
}

function exactSimplifyFraction() {
  /* 原地约分；保证分母恒为正 */
  if (exactDen === 0) {
    /* 分母为零非法：精确模式失效 */
    exactIsValid = false;
    return;
  }
  if (exactDen < 0) {
    /* 分母为负：分子分母同时翻转符号 */
    exactNum = -exactNum;
    exactDen = -exactDen;
  }
  if (exactNum === 0) {
    /* 零规范化为 0/1 */
    exactDen = 1;
    return;
  }
  let an = exactNum < 0 ? -exactNum : exactNum;
  let g = exactGcd(an, exactDen);
  if (g > 1) {
    exactNum = exactNum / g;
    exactDen = exactDen / g;
  }
}

function exactSimplifyRadical(D) {
  /* 提取完全平方因子：返回外系数 k，原地写 exactRad 为无平方因子部分
     试除法：命中 i*i 即从 D 中除尽、外系数乘 i
     仅局部标量 d / k / i（零分配） */
  let d = D;
  let k = 1;
  if (d <= 0) {
    /* 调用方保证 D >= 0；D === 0 时外系数 0、根号内 1 */
    exactRad = 1;
    return 0;
  }
  for (let i = 2; i * i <= d; i++) {
    while (d % (i * i) === 0) {
      d = d / (i * i);
      k = k * i;
    }
  }
  exactRad = d;
  return k;
}

/* 模块二：智能构造函数（规范化入口） */

function exactSetFraction(n, d) {
  /* 设置精确数值为分数 n/d（原地更新三个全局量） */
  if (d === 0) {
    exactInvalidate();
    return;
  }
  let an = n < 0 ? -n : n;
  let ad = d < 0 ? -d : d;
  if (an > 9007199254740991 || ad > 9007199254740991) {
    /* 分量超限：精确模式失效，仍写 approxValue 供显示层兜底 */
    exactIsValid = false;
    approxValue = n / d;
    return;
  }
  exactNum = n;
  exactDen = d;
  exactRad = 1;
  exactSimplifyFraction();
  exactIsValid = true;
  exactUpdateApprox();
}

function exactSetRadical(k, m) {
  /* 设置精确数值为 k√m（原地更新） */
  if (m < 0) {
    /* 负数开方产生复数，不进入精确系统 */
    exactInvalidate();
    return;
  }
  let ak = k < 0 ? -k : k;
  if (ak > 9007199254740991 || m > 9007199254740991) {
    exactIsValid = false;
    approxValue = k * Math.sqrt(m);
    return;
  }
  if (m === 0) {
    /* k·√0 = 0，按纯分数零处理 */
    exactSetFraction(0, 1);
    return;
  }
  /* 完全平方判定：s = round(√m)，s*s === m 则化为纯分数 k·s */
  let s = Math.floor(Math.sqrt(m) + 0.5);
  if (s * s === m) {
    let p = k * s;
    if (p > 9007199254740991 || p < -9007199254740991) {
      exactIsValid = false;
      approxValue = k * Math.sqrt(m);
      return;
    }
    exactSetFraction(p, 1);
    return;
  }
  /* 非完全平方：先化简根号内数（原地写 exactRad），外系数并入分子 */
  let k2 = exactSimplifyRadical(m);
  let n = k * k2;
  if (n > 9007199254740991 || n < -9007199254740991) {
    exactIsValid = false;
    approxValue = k * Math.sqrt(m);
    return;
  }
  exactNum = n;
  exactDen = 1;
  exactIsValid = true;
  exactUpdateApprox();
}

function exactSetPureReal(v) {
  /* 设置精确数值为浮点 v：仅整数可进入精确系统，其余只留近似值 */
  if (v === Math.floor(v) && v <= 9007199254740991 && v >= -9007199254740991) {
    exactSetFraction(v, 1);
    return;
  }
  exactIsValid = false;
  approxValue = v;
}

function exactInvalidate() {
  /* 精确模式失效（调用方负责同步 approxValue） */
  exactIsValid = false;
}

function exactReset() {
  /* 复位全部精确数值状态 */
  exactNum = 0;
  exactDen = 1;
  exactRad = 1;
  exactIsValid = false;
  approxValue = 0;
}

/* 模块三：近似值同步 */

function exactUpdateApprox() {
  /* approxValue 原地重算 */
  approxValue = (exactNum / exactDen) * Math.sqrt(exactRad);
}

/* 模块四：精确数值的四则运算（原地更新 exactNum/exactDen/exactRad） */

function exactAdd(num2, den2, rad2) {
  /* 当前精确数 + (num2/den2)·√rad2
     仅纯分数（exactRad === rad2 === 1）可精确相加，其余失效
     交叉相乘用局部标量；先溢出预检，再写全局 */
  if (!exactIsValid) {
    exactInvalidate();
    return;
  }
  if (den2 === 0 || rad2 !== 1 || exactRad !== 1) {
    exactInvalidate();
    return;
  }
  let an = exactNum < 0 ? -exactNum : exactNum;
  let an2 = num2 < 0 ? -num2 : num2;
  let ad2 = den2 < 0 ? -den2 : den2;
  if (an2 > 9007199254740991 || ad2 > 9007199254740991) {
    exactInvalidate();
    return;
  }
  /* 溢出预检：|n1*d2|、|n2*d1|、|d1*d2| 均不得超限 */
  if (an > 9007199254740991 / ad2 ||
      an2 > 9007199254740991 / exactDen ||
      exactDen > 9007199254740991 / ad2) {
    exactInvalidate();
    return;
  }
  let p1 = exactNum * den2;      /* n1*d2，模已验 ≤ 2^53-1，精确 */
  let p2 = num2 * exactDen;      /* n2*d1，同上 */
  /* 同号相加可能越过 2^53：改用减法预检（差值本身精确） */
  if ((p1 >= 0 && p2 >= 0) || (p1 < 0 && p2 < 0)) {
    let ap1 = p1 < 0 ? -p1 : p1;
    let ap2 = p2 < 0 ? -p2 : p2;
    if (ap1 > 9007199254740991 - ap2) {
      exactInvalidate();
      return;
    }
  }
  exactNum = p1 + p2;            /* 和或差的模 ≤ 2^53-1，精确 */
  exactDen = exactDen * den2;    /* 模已验，符号交给约分翻转 */
  exactSimplifyFraction();
  exactUpdateApprox();
}

function exactSub(num2, den2, rad2) {
  /* 当前精确数 - (num2/den2)·√rad2；与 exactAdd 对称但展开硬编码 */
  if (!exactIsValid) {
    exactInvalidate();
    return;
  }
  if (den2 === 0 || rad2 !== 1 || exactRad !== 1) {
    exactInvalidate();
    return;
  }
  let an = exactNum < 0 ? -exactNum : exactNum;
  let an2 = num2 < 0 ? -num2 : num2;
  let ad2 = den2 < 0 ? -den2 : den2;
  if (an2 > 9007199254740991 || ad2 > 9007199254740991) {
    exactInvalidate();
    return;
  }
  if (an > 9007199254740991 / ad2 ||
      an2 > 9007199254740991 / exactDen ||
      exactDen > 9007199254740991 / ad2) {
    exactInvalidate();
    return;
  }
  let p1 = exactNum * den2;      /* n1*d2 */
  let p2 = num2 * exactDen;      /* n2*d1 */
  /* 异号相减等价于同号相加：ap1 + ap2 不得超限 */
  if ((p1 >= 0 && p2 < 0) || (p1 < 0 && p2 >= 0)) {
    let ap1 = p1 < 0 ? -p1 : p1;
    let ap2 = p2 < 0 ? -p2 : p2;
    if (ap1 > 9007199254740991 - ap2) {
      exactInvalidate();
      return;
    }
  }
  exactNum = p1 - p2;            /* 同号相减的模 ≤ max(|p1|,|p2|)，精确 */
  exactDen = exactDen * den2;
  exactSimplifyFraction();
  exactUpdateApprox();
}

function exactMul(num2, den2, rad2) {
  /* 当前精确数 * (num2/den2)·√rad2
     分子乘分子、分母乘分母、根号内数乘根号内数，再统一约分与化简根式
     零分配：先溢出预检再写全局 */
  if (!exactIsValid) {
    exactInvalidate();
    return;
  }
  if (den2 === 0 || rad2 < 0) {
    exactInvalidate();
    return;
  }
  if (num2 === 0 || exactNum === 0 || rad2 === 0) {
    /* 零乘任何数仍为零，精确可表示 */
    exactSetFraction(0, 1);
    return;
  }
  let an = exactNum < 0 ? -exactNum : exactNum;
  let an2 = num2 < 0 ? -num2 : num2;
  let ad2 = den2 < 0 ? -den2 : den2;
  if (an2 > 9007199254740991 || ad2 > 9007199254740991 || rad2 > 9007199254740991) {
    exactInvalidate();
    return;
  }
  /* 溢出预检：|n1*n2|、|d1*d2|、r1*r2 均不得超限 */
  if (an > 9007199254740991 / an2 ||
      exactDen > 9007199254740991 / ad2 ||
      exactRad > 9007199254740991 / rad2) {
    exactInvalidate();
    return;
  }
  let newNum = exactNum * num2;      /* 模已验，精确 */
  let newDen = exactDen * den2;      /* 模已验，符号交给约分翻转 */
  let newRad = exactRad * rad2;      /* 模已验 */
  /* 根式化简：提取完全平方因子，外系数并入分子（原地写 exactRad） */
  exactDen = newDen;
  let k = exactSimplifyRadical(newRad);
  if (k > 1) {
    let aNewNum = newNum < 0 ? -newNum : newNum;
    if (aNewNum > 9007199254740991 / k) {
      exactInvalidate();
      return;
    }
    newNum = newNum * k;
  }
  exactNum = newNum;
  exactSimplifyFraction();
  exactUpdateApprox();
}

function exactDiv(num2, den2, rad2) {
  /* 当前精确数 ÷ (num2/den2)·√rad2
     有理化：(n1/d1)√r1 ÷ (n2/d2)√r2 = (n1·d2)/(d1·n2·r2) · √(r1·r2)
     零分配：先溢出预检再写全局 */
  if (!exactIsValid) {
    exactInvalidate();
    return;
  }
  if (num2 === 0 || den2 === 0 || rad2 < 0 || rad2 === 0) {
    /* 除数为零值（num2=0 或 rad2=0）或非法分母：精确模式失效 */
    exactInvalidate();
    return;
  }
  if (exactNum === 0) {
    /* 0 除以非零值仍为 0，精确可表示 */
    exactSetFraction(0, 1);
    return;
  }
  let an = exactNum < 0 ? -exactNum : exactNum;
  let an2 = num2 < 0 ? -num2 : num2;
  let ad2 = den2 < 0 ? -den2 : den2;
  if (an2 > 9007199254740991 || ad2 > 9007199254740991 || rad2 > 9007199254740991) {
    exactInvalidate();
    return;
  }
  /* 溢出预检：|n1*d2| 与 |d1*n2*r2|（两步乘法分别验）、r1*r2 */
  if (an > 9007199254740991 / ad2 ||
      exactDen > 9007199254740991 / an2 ||
      exactRad > 9007199254740991 / rad2) {
    exactInvalidate();
    return;
  }
  let newNum = exactNum * den2;      /* n1*d2，模已验，精确 */
  let t = exactDen * an2;            /* d1*|n2|，模已验，精确 */
  if (t > 9007199254740991 / rad2) {
    exactInvalidate();
    return;
  }
  let newDen = t * rad2;             /* d1*n2*r2 的模（符号随 num2） */
  let newRad = exactRad * rad2;      /* r1*r2，模已验 */
  if (num2 < 0) {
    newDen = -newDen;                /* 恢复 num2 的符号 */
  }
  exactDen = newDen;
  exactNum = newNum;
  /* 根式化简：提取完全平方因子，外系数并入分子（原地写 exactRad） */
  let k = exactSimplifyRadical(newRad);
  if (k > 1) {
    let aNewNum = newNum < 0 ? -newNum : newNum;
    if (aNewNum > 9007199254740991 / k) {
      exactInvalidate();
      return;
    }
    exactNum = newNum * k;
  }
  exactSimplifyFraction();
  exactUpdateApprox();
}

/* 模块五：从判别式 D 提取精确根 */

function exactFromDiscriminant(D) {
  /* 文件 6 方程求解器接口：由判别式 D 提取 √D = (num/den)·√rad
     D < 0（复数根）或非有限值不进入精确系统
     仅局部标量（零分配） */
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
    /* 整数判别式：直接化简为 k√m（exactSimplifyRadical 原地写 exactRad） */
    if (D > 9007199254740991) {
      exactInvalidate();
      return;
    }
    let k = exactSimplifyRadical(D);
    exactNum = k;
    exactDen = 1;
    exactIsValid = true;
    exactUpdateApprox();
    return;
  }
  /* 简单分数判定：找最小 d 使 D*d 为整数（D > 0，无符号负担） */
  let foundDen = 0;
  let foundNum = 0;
  for (let d = 2; d <= 1000000; d++) {
    let v = D * d;
    if (v > 9007199254740991) {
      /* 分子超限：放弃精确表示 */
      break;
    }
    let r = Math.round(v);
    if (v === r || (v - r < 1e-9 && r - v < 1e-9)) {
      foundDen = d;
      foundNum = r;
      break;
    }
  }
  if (foundDen === 0) {
    /* 无限小数或超出搜索范围：精确模式失效 */
    exactInvalidate();
    return;
  }
  /* √(num/den) = √(num*den)/den：根号内通分后化简 */
  if (foundNum > 9007199254740991 / foundDen) {
    exactInvalidate();
    return;
  }
  let m = foundNum * foundDen;        /* 模已验，精确 */
  let k2 = exactSimplifyRadical(m);   /* 原地写 exactRad */
  exactNum = k2;
  exactDen = foundDen;
  exactSimplifyFraction();
  exactIsValid = true;
  exactUpdateApprox();
}
