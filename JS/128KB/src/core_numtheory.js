/* ApolloCalc-CAS · JS/128KB · core_numtheory.js — 数论模块(v2.3.0 功能 1)
 * 算法 1:1 移植 C/128KB/src/ac_numtheory.c(+ ac_numtheory.h 契约注释),
 * 与 C 侧唯一能力差异:安全整数域从 int64 收窄为 Float64 安全整数
 * ≤ 2^53−1(MAX_SAFE),溢出/越域置错误码,绝不返回错值。
 *
 * 功能:gcd/lcm(独立实现,不复用 exact 层,与 C 侧模块隔离先例一致)、
 *       isPrime(6k±1 试除,除法形式上界 i ≤ ⌊n/i⌋ 与 C 逐字同款)、
 *       isPrimeMr(Miller-Rabin,底数集 {2,3,5,7,11,13,17,19,23,29,31,37}
 *       与 C 侧 NT_MR_BASES 逐字一致——该集合在 3.3e24 以下无强伪素数,
 *       对整个安全整数域确定性)、powMod/invMod(模算术,C 第 9 轮同款)、
 *       factorize(试除:先提 2 再扫奇数,产物 16 槽访问器镜像 g_nt_*)、
 *       nCr/nPr(迭代乘除 + 每步 gcd 约减 + 每步溢出预检,C 同款)。
 *
 * 溢出判定:乘法 a·b(0 ≤ a,b ≤ 2^53−1)的真积 ≤ 2^53−1 时 float64 精确,
 *   真积 ≥ 2^53 时舍入结果 ≥ 2^53 > MAX_SAFE,故「p = a*b; p > MAX_SAFE」
 *   即为精确溢出判定,无误报无漏报(_safeMul 内注)。
 *
 * 模乘 mulmod:俄民乘法(加倍 + 条件减法)。加法 a+b 可能达 2^54−4 越出
 *   精确整数域,用 m−b 预减技巧:(a+b) mod m = a ≥ m−b ? a−(m−b) : a+b,
 *   两侧都是 ≤ 2^53 的精确整数运算。位移一律 %2 + 乘 0.5(位运算会截到
 *   32 位,≥ 2^32 的模数/指数不能用 >>)。
 *
 * 错误码(getError):0 OK / 1 参数域(非整数、越域 ≤2^53−1、n<0、r>n、
 *   n≤0、m≤1 等) / 2 溢出(结果或中间量 > 2^53−1)。
 * 返回值约定:gcd/isPrime/isPrimeMr/powMod/invMod/factorize 与 C 同语义
 *   (lcm 溢出返 0、invMod 无逆返 0 均为 C 侧成文哨兵值);nCr/nPr 成功返回
 *   精确值(合法结果恒 ≥ 1),失败返回 −1 且 _err 区分 1=域 2=溢出。
 *
 * 零 GC:3 个 TypedArray 加载期一次分配(MR 底数表 96 B + 因子表
 *   128 B + 指数表 64 B = 288 B),全路径无对象/数组字面量、无字符串操作、
 *   无递归(ncr 调 gcd 是迭代函数调用,非自调用,与 C 侧先例同判)。
 *
 * 导出三通道:Node require / 浏览器 <script> 顶层 var / 内核拼接 ESM。 */

var ApolloNumtheory = (function () {
  "use strict";

  var MAX_SAFE = 9007199254740991;          /* 2^53−1,安全整数域上界 */

  /* 加载期一次分配的池(allocCount = 3) */
  /* MR 底数集:与 C 侧 NT_MR_BASES(ac_numtheory.c:184-186)逐字一致 */
  var MR_BASES = new Float64Array(12);
  MR_BASES[0] = 2;  MR_BASES[1] = 3;  MR_BASES[2] = 5;  MR_BASES[3] = 7;
  MR_BASES[4] = 11; MR_BASES[5] = 13; MR_BASES[6] = 17; MR_BASES[7] = 19;
  MR_BASES[8] = 23; MR_BASES[9] = 29; MR_BASES[10] = 31; MR_BASES[11] = 37;

  /* 因子表:镜像 g_nt_primes / g_nt_exps / g_nt_count(AC_NT_MAX_FACTORS = 16)。
   * 素因子可达 2^53−1 量级,Int32 装不下,必须 Float64。 */
  var MAX_FACTORS = 16;
  var _fPrimes = new Float64Array(MAX_FACTORS);
  var _fExps = new Int32Array(MAX_FACTORS);
  var _fCount = 0;

  var _err = 0;                   /* 0 OK / 1 参数域 / 2 溢出 */
  var _opCount = 0;
  var _allocCount = 3;

  function allocCount() { return _allocCount; }
  function getOpCount() { return _opCount; }
  function getError() { return _err; }

  function reset() {
    var i;
    for (i = 0; i < MAX_FACTORS; i++) { _fPrimes[i] = 0; _fExps[i] = 0; }
    _fCount = 0;
    _err = 0;
    _opCount = 0;
    return 0;
  }

  /* 整数域守卫:整数且 |x| ≤ 2^53−1 → 1,否则置 _err = 1 返 0 */
  function _domainOK(x) {
    if (x !== x || x === Infinity || x === -Infinity) { _err = 1; return 0; }
    if (Math.floor(x) !== x) { _err = 1; return 0; }
    if (x > MAX_SAFE || x < -MAX_SAFE) { _err = 1; return 0; }
    return 1;
  }

  /* 精确乘法:真积 ≤ 2^53−1 时 float64 精确;真积 ≥ 2^53 时舍入结果
   * ≥ 2^53 > MAX_SAFE(向最近舍入不可能低于真值下确界 2^53),故单比较即
   * 精确判定。溢出返回 −1(真积恒 ≥ 0,−1 哨兵无歧义),调用方置 _err = 2。 */
  function _safeMul(a, b) {
    var p = a * b;
    if (p > MAX_SAFE) { return -1; }
    return p;
  }

  /* gcd / lcm(ac_numtheory.c:20-49 同款) */

  function gcd(a, b) {
    _err = 0;
    _opCount++;
    if (!_domainOK(a) || !_domainOK(b)) { return NaN; }
    if (a < 0) { a = -a; }
    if (b < 0) { b = -b; }
    while (b !== 0) {
      var t = a % b;
      a = b;
      b = t;
    }
    return a;
  }

  function lcm(a, b) {
    _err = 0;
    _opCount++;
    if (!_domainOK(a) || !_domainOK(b)) { return NaN; }
    if (a === 0 || b === 0) { return 0; }
    var g = gcd(a, b);              /* gcd 重置 _err,此处入参已过域检,恒 0 */
    var ag = a / g;                 /* g ≥ 1 且 |a| ≤ 2^53−1:除法精确 */
    var prod = _safeMul(ag < 0 ? -ag : ag, b < 0 ? -b : b);
    if (prod === -1) { _err = 2; return 0; }   /* 溢出哨兵 0,与 C 侧成文约定同 */
    return prod;
  }

  /* is_prime:6k±1 试除(ac_numtheory.c:53-65 同款) */

  function isPrime(n) {
    _err = 0;
    _opCount++;
    if (n !== n || n === Infinity || n === -Infinity) { _err = 1; return 0; }
    if (n < 2) { return 0; }
    if (Math.floor(n) !== n) { _err = 1; return 0; }        /* 非整数:域错 */
    if (n > MAX_SAFE) { _err = 1; return 0; }               /* 越安全整数域 */
    if (n < 4) { return 1; }                                /* 2, 3 */
    if (n % 2 === 0 || n % 3 === 0) { return 0; }
    /* 6k±1 轮;除法形式上界 i ≤ n/i 防大数平方越域。相等必须放行:
     * n = 25 需要 i = 5(5 == 25/5)。float 除法在最坏边界只可能多算一轮
     * (真商整数部分 k 的浮点下取整不会变小),不会漏因子。 */
    var i = 5;
    while (i <= Math.floor(n / i)) {
      if (n % i === 0 || n % (i + 2) === 0) { return 0; }
      i += 6;
    }
    return 1;
  }

  /* 模算术(ac_numtheory.c:188-219 同款,float64 版) */

  /* (a + b) mod m:0 ≤ a,b < m ≤ 2^53−1。a+b 可达 2^54−4 越精确域,
   * 用 m−b 预减:a ≥ m−b 时 (a+b)−m == a−(m−b)(两侧皆 ≤ 2^53 精确);
   * 否则 a+b < m ≤ 2^53−1 本身精确。 */
  function _addMod(a, b, m) {
    var t = m - b;                  /* t ∈ (0, m] */
    if (a >= t) { return a - t; }   /* a+b ≥ m:(a+b) mod m = a−(m−b) */
    return a + b;                   /* a+b < m:精确 */
  }

  /* (a · b) mod m:俄民乘法 53 轮以内;奇偶用 %2、折半用 floor(x/2)
   * (x·0.5 对 float64 恒精确,位运算 >> 会截断 32 位,不可用)。 */
  function _mulMod(a, b, m) {
    var r = 0;
    a = a % m;
    b = b % m;
    while (b > 0) {
      if (b % 2 === 1) { r = _addMod(r, a, m); }
      a = _addMod(a, a, m);
      b = Math.floor(b / 2);
    }
    return r;
  }

  function powMod(base, exp, mod) {
    _err = 0;
    _opCount++;
    if (!_domainOK(base) || !_domainOK(exp) || !_domainOK(mod)) { return 0; }
    if (mod <= 1 || base < 0 || exp < 0) { _err = 1; return 0; }
    var r = 1;
    base = base % mod;
    while (exp > 0) {
      if (exp % 2 === 1) { r = _mulMod(r, base, mod); }
      base = _mulMod(base, base, mod);
      exp = Math.floor(exp / 2);
    }
    return r;
  }

  function invMod(a, m) {
    _err = 0;
    _opCount++;
    if (!_domainOK(a) || !_domainOK(m)) { return 0; }
    if (m <= 1) { _err = 1; return 0; }
    a = a % m;                      /* 负 a 落 (−m, 0],+m 无溢出 */
    if (a < 0) { a += m; }
    if (a === 0) { return 0; }
    /* 扩展欧几里得,迭代;中间量 |t|,|rr| ≤ m ≤ 2^53−1,线性组合精确 */
    var t = 0;
    var newt = 1;
    var rr = m;
    var newr = a;
    var q, tmp;
    while (newr !== 0) {
      q = Math.floor(rr / newr);
      tmp = t - q * newt; t = newt; newt = tmp;
      tmp = rr - q * newr; rr = newr; newr = tmp;
    }
    if (rr !== 1) { return 0; }     /* gcd ≠ 1:无逆元,哨兵 0(C 同款) */
    if (t < 0) { t += m; }
    return t;
  }

  /* Miller-Rabin(ac_numtheory.c:246-284 逐字镜像) */

  function isPrimeMr(n) {
    _err = 0;
    _opCount++;
    if (n !== n || n === Infinity || n === -Infinity) { _err = 1; return 0; }
    if (n < 2) { return 0; }
    if (Math.floor(n) !== n) { _err = 1; return 0; }
    if (n > MAX_SAFE) { _err = 1; return 0; }
    if (n % 1 !== 0) { return 0; }  /* 不可达,防御 */
    var un = n;
    var i, b, r;
    for (i = 0; i < 12; i++) {
      if (un === MR_BASES[i]) { return 1; }        /* 命中小素数表 */
      if (un % MR_BASES[i] === 0) { return 0; }    /* 小因子整除 */
    }
    /* n−1 = d·2^s(d 奇);d ≤ 2^53,折半用除法(位运算截 32 位) */
    var d = un - 1;
    var s = 0;
    while (d % 2 === 0) { d = d / 2; s++; }        /* d 偶时除法精确 */
    var one = 1;
    var nm1 = un - 1;
    for (b = 0; b < 12; b++) {
      var a = MR_BASES[b];
      var hitNm1 = 0;               /* 通过标志:仅命中 n−1 时置位(C 同款注释:
                                       初值必须为"未通过",否则 s=1 且
                                       x0 ∉ {±1} 的合成数漏判) */
      if (a >= un) { continue; }                   /* 底数 ≥ n 跳过 */
      var x = powMod(a, d, un);                    /* 会重置 _err,入参已检,恒 0 */
      if (x === one || x === nm1) { continue; }    /* 本底数通过 */
      for (r = 1; r < s; r++) {
        x = _mulMod(x, x, un);
        if (x === nm1) { hitNm1 = 1; break; }      /* 命中 n−1:通过并停止 */
      }
      if (hitNm1 === 0) { return 0; }              /* 合成数 */
    }
    return 1;
  }

  /* factorize(ac_numtheory.c:69-108 同款) */

  function factorize(n) {
    _err = 0;
    _opCount++;
    _fCount = 0;
    var i;
    for (i = 0; i < MAX_FACTORS; i++) { _fPrimes[i] = 0; _fExps[i] = 0; }
    if (n !== n || n === Infinity || n === -Infinity) { _err = 1; return 0; }
    if (Math.floor(n) !== n) { _err = 1; return 0; }
    if (n > MAX_SAFE) { _err = 1; return 0; }
    if (n <= 0) { _err = 1; return 0; }

    /* 提出所有 2 的因子(此后 n 为奇数) */
    if (n % 2 === 0) {
      var e2 = 0;
      while (n % 2 === 0) { n = n / 2; e2++; }     /* n 偶时除法精确 */
      _fPrimes[_fCount] = 2;
      _fExps[_fCount] = e2;
      _fCount++;
    }

    /* 奇数试除;除法形式上界,n 随因子剥离而收紧 */
    var odd = 3;
    while (odd <= Math.floor(n / odd)) {
      if (n % odd === 0) {
        if (_fCount >= MAX_FACTORS) { _err = 1; return 0; }
        var e = 0;
        while (n % odd === 0) { n = n / odd; e++; }
        _fPrimes[_fCount] = odd;
        _fExps[_fCount] = e;
        _fCount++;
      }
      odd += 2;
    }

    /* 剩余 n > 1 是最后一个素数因子 */
    if (n > 1) {
      if (_fCount >= MAX_FACTORS) { _err = 1; return 0; }
      _fPrimes[_fCount] = n;
      _fExps[_fCount] = 1;
      _fCount++;
    }
    return 1;
  }

  function factorCount() { return _fCount; }
  function factorPrimeAt(i) {
    if (i < 0 || i >= _fCount) { return 0; }
    return _fPrimes[i];
  }
  function factorExpAt(i) {
    if (i < 0 || i >= _fCount) { return 0; }
    return _fExps[i];
  }
  function factorReset() {
    var i;
    for (i = 0; i < MAX_FACTORS; i++) { _fPrimes[i] = 0; _fExps[i] = 0; }
    _fCount = 0;
    return 0;
  }

  /* nCr / nPr(ac_numtheory.c:112-165 同款)
   * 不变式:第 i 步后 result == C(base+i, i),单调升至 C(n, r)。
   * 先乘后除会在可表示答案上溢出中间量(C(66,33) 在 int64 域的经典反例,
   * 本域更早),对 (n−r+i)/i 与 result 逐级 gcd 约减保证每次乘法都是
   * 精确二项式 ≤ C(n, r),溢出判定因此精确无误报。 */

  function nCr(n, r) {
    _err = 0;
    _opCount++;
    if (!_domainOK(n) || !_domainOK(r)) { return -1; }
    if (n < 0 || r < 0 || r > n) { _err = 1; return -1; }
    if (r === 0 || r === n) { return 1; }
    if (r > n - r) { r = n - r; }                   /* 对称性,减少迭代 */
    var base = n - r;
    var result = 1;
    var i;
    for (i = 1; i <= r; i++) {
      var m = base + i;                             /* 乘子 n−r+i */
      var d = i;                                    /* 除子 i */
      var g = gcd(m, d);                            /* 约掉乘子与除子的公因子 */
      m = m / g;
      d = d / g;
      g = gcd(result, d);                           /* 再约掉 result 里已有因子 */
      result = result / g;
      d = d / g;
      if (d !== 1) {
        /* 数学保证 d | m(gcd(d, result) = 1 且 d | result·m),防御分支 */
        if (m % d !== 0) { _err = 2; return -1; }
        m = m / d;
      }
      var nm = _safeMul(result, m);
      if (nm === -1) { _err = 2; return -1; }
      result = nm;
    }
    return result;
  }

  function nPr(n, r) {
    _err = 0;
    _opCount++;
    if (!_domainOK(n) || !_domainOK(r)) { return -1; }
    if (n < 0 || r < 0 || r > n) { _err = 1; return -1; }
    if (r === 0) { return 1; }
    /* 中间值 P(n,k) 单调升至 P(n,r):最终答案可表示则中间必可表示,
     * safeMul 判定即精确判定,无误报(C 同款论证) */
    var result = 1;
    var i;
    for (i = 0; i < r; i++) {
      var nm = _safeMul(result, n - i);
      if (nm === -1) { _err = 2; return -1; }
      result = nm;
    }
    return result;
  }

  return {
    MAX_SAFE: MAX_SAFE,

    gcd: gcd,
    lcm: lcm,

    isPrime: isPrime,
    isPrimeMr: isPrimeMr,
    powMod: powMod,
    invMod: invMod,

    factorize: factorize,
    factorCount: factorCount,
    factorPrimeAt: factorPrimeAt,
    factorExpAt: factorExpAt,
    factorReset: factorReset,

    nCr: nCr,
    nPr: nPr,

    reset: reset,
    getError: getError,
    getOpCount: getOpCount,
    allocCount: allocCount
  };
})();

if (typeof module === "object" && module && module.exports) {
  module.exports = ApolloNumtheory;
}
