/* ApolloCalc-CAS · JS/128KB · core_ode.js — 常系数线性齐次 ODE 通解
 * (v2.3.0 功能 3,JS 侧)契约:docs/contracts/ode-format-v23.md(冻结),
 * 与 C/128KB-ProTEXT/src/ac_pro_ode.c 互为镜像,任何偏差即实现 bug。
 *
 * 数学范围:仅常系数、线性、齐次,y(x) 的 1..4 阶;其余按 §6 错误码
 * 从严拒绝,绝不硬解、绝不输出近似通解。
 *
 * 管线(契约 §3→§5):
 *   预扫描(字符集/长度,U+0000 与非 ASCII → EPARSE)
 *   → 结构词法(单趟左→右,EPARSE/EDEG 当场,x 记号置变系数标志)
 *   → 延迟裁决全序:EVARCOEF → ENONHOMO → ECOEF(§6)
 *   → 特征多项式 a[k](k 阶导数系数 = λ^k 系数,低位在前)
 *   → n=1 线性根 λ = −a[0]/a[1];n=2..4 复用 solveQuadratic/Cubic/Quartic
 *     (core_solvers.js,_coeffs 低位在前 [c,b,a] 布局,契约 §4.1)
 *   → 稳定插入排序(Re 降序,1e-9 容差;次键 Im 降序,§4.2)
 *   → 聚类(实根合并 / 共轭对 + 重数层累加,§4.3;自检 Σ = n)
 *   → 渲染(fmt 三分支逐字实现:整数/有理重建 q≤1000/定点 12 位,§5.1;
 *     四类项模板机械拼接,§5.2/5.3;常数下标 U+2081..2084 连续分配)
 *
 * fmt 的契约内矛盾如实记录:§5.1-3 例「fmt(-0.5) = "-0.5"(有理重建分支)」
 * 与 §5.1-2 规则「否则 p/q」及 §5.4 第 11 行期望串 "-1/2" 相互矛盾;
 * 本实现按 §5.1-2 规则 + §5.4 逐字表:有理重建命中一律输出 p/q 形态
 * (fmt(-0.5) = "-1/2"),§5.4 全部 11 行逐字通过。
 *
 * 错误码(§6,双侧同值):0 OK / −1 EPARSE / −2 EDEG / −3 EVARCOEF /
 * −4 ENONHOMO / −5 ECOEF / −6 EOVER(C 侧缓冲不足,JS 无此失败,不产生)/
 * −7 EINTERNAL(聚类失败、共轭残缺、fmt 域外、求解层失败——实现缺陷或
 * 数值病态,绝不硬凑渲染)。失败时 out 恒为 ""。
 *
 * 接口(契约 §8):solveOde(src) → { code: number, out: string }。
 * 返回对象为加载期一次分配的单例(每次调用覆写同对象):调用方按
 * 「调用 → 读 → 弃」消费,不得跨调用持有引用;这是零 GC 纪律下对
 * 契约对象返回形态的唯一合规实现,别名语义已在头注声明。
 *
 * 零 GC:11 个 TypedArray 加载期一次分配(共 1464 B),解析/聚类/渲染
 * 全路径标量化,无对象/数组字面量、无递归;字符串只经 charCodeAt/
 * String.fromCharCode 通道(与 core_multivar polyToString 同款)。
 *
 * 导出:内核拼接 ESM(export 块 ode 命名空间)/ Node require / 浏览器 var。
 * 独立 require 下 solveOde 依赖内核拼接作用域中的 _coeffs 与
 * solveQuadratic/solveCubic/solveQuartic(core_solvers.js 前置)。 */

var ApolloOde = (function () {
  "use strict";

  /* 常量(契约逐字) */
  var MAX_LEN = 256;               /* §3.1 长度上限(不含 NUL) */
  var TOL = 1e-9;                  /* §4.2/4.3/5.1 统一容差 */
  var COEF_MAX = 999;              /* §3.4 系数域 */
  var MAX_ROOTS = 4;               /* 阶数上限 */
  var OUT_CAP = 300;               /* 最长形态静态估算 < 230(§8),300 留余量 */

  /* 加载期一次分配的池(allocCount = 11,合计 1464 B) */
  var _a = new Float64Array(5);          /* 特征多项式系数 a[0..4] */
  var _seen = new Int32Array(5);         /* 同阶项出现标志(§3.3-2) */
  var _re = new Float64Array(MAX_ROOTS); /* 排序后根实部 */
  var _im = new Float64Array(MAX_ROOTS); /* 排序后根虚部 */
  var _used = new Int32Array(MAX_ROOTS); /* 聚类已配标志 */
  var _cType = new Int32Array(MAX_ROOTS);/* 簇类型:1 实根 2 共轭对 */
  var _cLam = new Float64Array(MAX_ROOTS);  /* 实根 λ / 共轭 α */
  var _cBeta = new Float64Array(MAX_ROOTS); /* 共轭 β(恒正) */
  var _cMult = new Int32Array(MAX_ROOTS);
  var _outBuf = new Int32Array(OUT_CAP); /* 渲染缓冲(字符码) */
  var _workI = new Int32Array(16);       /* fmt 数字位寄存(定点 12 位需 12 槽) */

  var _outLen = 0;
  var _err = 0;                          /* 最近一次 solveOde 的 code */
  var _opCount = 0;
  var _allocCount = 11;

  /* 结果单例(契约 §8 返回形态;别名语义见头注) */
  var _result = { code: 0, out: "" };

  function allocCount() { return _allocCount; }
  function getOpCount() { return _opCount; }

  function reset() {
    var i;
    for (i = 0; i < 5; i++) { _a[i] = 0; _seen[i] = 0; _workI[i] = 0; }
    for (i = 0; i < MAX_ROOTS; i++) {
      _re[i] = 0; _im[i] = 0; _used[i] = 0;
      _cType[i] = 0; _cLam[i] = 0; _cBeta[i] = 0; _cMult[i] = 0;
    }
    _outLen = 0;
    _err = 0;
    _opCount = 0;
    return 0;
  }

  /* 词法/解析(契约 §3)
   * 返回 0 = 成功;否则 = 契约错误码。成功时 _a[] 已装填且 n 已写 _deg。 */

  function _fail(code) { _err = code; return code; }

  /* 预扫描:长度 + 字符集(§3.1)。ASCII 白名单之外的任何码元
   * (含 U+0000 与全部 ≥0x80)→ EPARSE。 */
  function _charOK(cc) {
    if (cc === 32 || cc === 9) { return 1; }            /* 空白 */
    if (cc >= 48 && cc <= 57) { return 1; }             /* 0-9 */
    if (cc === 43 || cc === 45) { return 1; }           /* + - */
    if (cc === 42 || cc === 61 || cc === 39) { return 1; }  /* * = ' */
    if (cc === 94) { return 1; }                        /* ^(仅系数位置 x 幂,§3.3-3) */
    if (cc === 120 || cc === 121) { return 1; }         /* x y(只认小写) */
    return 0;
  }

  var _deg = 0;              /* 特征多项式最高阶 n */
  var _varcoef = 0;          /* 左端 x 标志(§3.2) */

  function _parse(src) {
    var len = src.length;
    var i, cc, j;
    _varcoef = 0;
    for (i = 0; i < 5; i++) { _a[i] = 0; _seen[i] = 0; }

    if (len === 0 || len > MAX_LEN) { return _fail(-1); }
    for (i = 0; i < len; i++) {
      cc = src.charCodeAt(i);
      if (cc === 0 || !_charOK(cc)) { return _fail(-1); }
    }

    /* 结构词法:单趟左→右
     * 项累积器:sgn 项符号(初始 +1)/ coef 系数 / coefSeen / starSeen /
     * hasY y 部已消费(引号计数在 y 消费时同趟连扫) / k 当前导数阶 /
     * termOpen 本项已消费内容 token / signRun 项首连续符号计数(1=项首
     * 符号,2=分隔符后再带一个符号(§3.2 "+-3y'" 合法),≥3 → EPARSE)。
     * 分隔符语义(§3.2 + gb 先例):项间单个 +/- 即下一项的符号
     * (y''-y' → −y');其后紧跟的第二个 +/- 为"项首再带的符号",
     * 覆盖符号值(+−3y' → −3)。 */
    var sgn = 1;
    var coef = 0;
    var coefSeen = 0;
    var starSeen = 0;
    var hasY = 0;
    var k = 0;
    var termOpen = 0;
    var termEver = 0;
    var signRun = 0;
    var xSeen = 0;              /* 本项已消费 x 因子(镜像 C saw_x:含 x 的项缺 y 时 '+'/'=' → EPARSE) */
    var xPow = 0;               /* ^ 紧跟 x 的许可(§3.3-3 多项式语义,v2.4.1) */
    var coefOverflow = 0;
    var wsGap = 0;                      /* 内容 token 之间出现过空白(语法从严:系数 token
                                         * 被空白打断后再遇数字 = 第二个系数单元 → EPARSE) */
    var COEF_GUARD = 900719925474098;   /* (2^53−1 − 9)/10,镜像 C 的 int64 早退守卫 */

    i = 0;
    while (i < len) {
      cc = src.charCodeAt(i);
      if (cc === 32 || cc === 9) { wsGap = 1; i++; continue; }
      if (cc === 61) { break; }                          /* '=' 右端开始 */
      if (cc === 120) {
        if (hasY === 1) { return _fail(-1); }  /* y''x:导数记号后尾随 x → EPARSE(§3.3-1,v2.4.1 裁决) */
        _varcoef = 1; xSeen = 1; xPow = 1; i++; continue;   /* 左端 x:置标志;xPow 许可其后 ^幂 */
      }
      if (cc === 94) {
        /* ^ 仅允许紧跟 x(可选空白,可选空白后再数字):消费幂值(可空)、不报结构错——
         * 与 C 侧 ac_pro_ode.c:270-273 逐字一致(v2.4.1 裁决,多项式语义) */
        if (xPow === 0) { return _fail(-1); }
        i++;
        while (i < len) {
          var w = src.charCodeAt(i);
          if (w !== 32 && w !== 9) { break; }
          i++;
        }
        while (i < len) {
          var dv = src.charCodeAt(i);
          if (dv < 48 || dv > 57) { break; }
          i++;
        }
        xPow = 0;
        continue;
      }
      if (cc === 43 || cc === 45) {
        if (hasY === 1) {
          /* 前项完成:提交;此字符即下一项的符号(单字符规则) */
          if (_commit(sgn, coef, coefSeen, k) !== 0) { return _err; }
          sgn = (cc === 45) ? -1 : 1;
          coef = 0; coefSeen = 0; starSeen = 0; hasY = 0; k = 0; xSeen = 0; xPow = 0;
          termOpen = 1; termEver = 1; signRun = 1;
          i++;
          continue;
        }
        if (coefSeen === 0 && starSeen === 0 && xSeen === 0 && signRun < 2) {
          /* 项首符号(输入头第一个,或分隔符后再带的一个) */
          sgn = (cc === 45) ? -1 : 1;
          signRun++;
          termOpen = 1; termEver = 1; xSeen = 0; xPow = 0;
          i++;
          continue;
        }
        return _fail(-1);            /* 项中符号 / 三连符号 */
      }
      if (cc >= 48 && cc <= 57) {
        if (hasY === 1) { return _fail(-1); }   /* y 后出现数字 → EPARSE */
        if (starSeen === 1) { return _fail(-1); }  /* * 后必须 y(§3.3-1) */
        if (coefSeen === 1 && wsGap === 1) { return _fail(-1); }  /* 系数单元被空白打断 */
        if (coef > COEF_GUARD) { coefOverflow = 1; }
        else { coef = coef * 10 + (cc - 48); }
        coefSeen = 1; termOpen = 1; termEver = 1; wsGap = 0; xPow = 0;
        i++;
        continue;
      }
      if (cc === 42) {                               /* '*' */
        if (hasY === 1) { return _fail(-1); }        /* y''*3 → EPARSE */
        if (starSeen === 1) { return _fail(-1); }    /* 每项至多一个 * */
        /* 星号无条件消费(gb 先例 ac_pro_groebner.c:136 同款:
         * 系数缺省 1,契约 §3.2 例 "x*y''" 依赖此语义) */
        starSeen = 1; termOpen = 1; termEver = 1; xPow = 0;
        i++;
        continue;
      }
      if (cc === 39) {                               /* ''' */
        /* 引号只允许紧连 y(y 消费时已同趟连扫);此处再遇 ' 即
         * 撇号序列内部有空白(§3.1)或无 y 的撇号 → EPARSE */
        return _fail(-1);
      }
      if (cc === 121) {                              /* 'y' */
        if (hasY === 1) { return _fail(-1); }        /* 一项一个导数记号 */
        hasY = 1; termOpen = 1; termEver = 1; k = 0; xPow = 0;
        i++;
        while (i < len && src.charCodeAt(i) === 39) {
          k++;
          if (k >= 5) { return _fail(-2); }          /* 撇号计到 5 当场 EDEG */
          i++;
        }
        continue;
      }
      return _fail(-1);                              /* 防御位(预扫描已滤) */
    }

    /* 左端扫完(或遇 '='):收尾项 */
    if (i >= len) { return _fail(-1); }              /* 无 '=' → EPARSE(§3.3-5) */
    if (hasY === 1) {
      if (_commit(sgn, coef, coefSeen, k) !== 0) { return _err; }
    } else if (termOpen === 1) {
      return _fail(-1);                              /* 缺 y 的项(3、+2)→ EPARSE */
    }
    if (termEver === 0) { return _fail(-1); }        /* 左端无任何项 → EPARSE */

    /* 右端(§3.3-4):去首尾空白后必须恰为单个 '0';
     * 含 x 属自由项非零(不是变系数,变系数只看左端);
     * 右端出现 '=' 即第二个 '=' → EPARSE(§3.3-5,v2.4.2 差分收口:
     * y''=x=0 / y''=0=0 此前误报 ENONHOMO,C 侧按 §3.3-5 报 EPARSE) */
    var nz = 0;
    var first = 0;
    for (j = i + 1; j < len; j++) {
      cc = src.charCodeAt(j);
      if (cc === 32 || cc === 9) { continue; }
      if (cc === 61) { return _fail(-1); }   /* 第二个 '=' → EPARSE(§3.3-5) */
      if (nz === 0) { first = cc; }
      nz++;
    }
    if (nz === 0) { return _fail(-1); }              /* 右端空 → EPARSE */

    /* 延迟裁决全序(§6):EVARCOEF → ENONHOMO → ECOEF */
    if (_varcoef === 1) { return _fail(-3); }
    if (nz !== 1 || first !== 48) { return _fail(-4); }  /* ENONHOMO */

    /* 特征多项式域(§3.4):|a[k]| > 999 → ECOEF(只读检,符号参与求根) */
    var bad = coefOverflow;
    var n;
    for (j = 0; j < 5; j++) {
      var av = _a[j];
      if (av < 0) { av = -av; }
      if (av > COEF_MAX) { bad = 1; }
    }
    if (bad === 1) { return _fail(-5); }
    n = 0;
    for (j = 4; j >= 1; j--) {
      if (_a[j] !== 0) { n = j; break; }
    }
    if (_a[0] === 0 && n === 0) { return _fail(-5); }   /* 恒等式(全零)→ ECOEF */
    if (n === 0) { return _fail(-5); }                  /* 无导数项(y=0)→ ECOEF */
    _deg = n;
    return 0;
  }

  /* 项提交:同阶重复 → EPARSE(§3.3-2);a[k] += sgn·c(系数缺省 1) */
  function _commit(sgn, coef, coefSeen, k) {
    if (_seen[k] === 1) { return _fail(-1); }
    _seen[k] = 1;
    var c = coefSeen === 1 ? coef : 1;
    _a[k] += sgn * c;
    return 0;
  }

  /* fmt(契约 §5.1,跨语言确定性)
   * 直接写渲染缓冲;域外(|x| ≥ 4096 或非有限)→ −1(EINTERNAL)。 */

  function _llround(x) {
    /* 平局取远离零一侧(C llround 语义) */
    return x >= 0 ? Math.floor(x + 0.5) : Math.ceil(x - 0.5);
  }

  function _igcd(a, b) {
    while (b !== 0) {
      var t = a % b;
      a = b;
      b = t;
    }
    return a;
  }

  /* 整数十进制(负号在前),写入 _outBuf */
  function _pushInt(m) {
    if (m === 0) { _pushCC(48); return; }
    if (m < 0) { _pushCC(45); m = -m; }
    var nd = 0;
    while (m > 0) {
      _workI[nd] = m % 10;
      nd++;
      m = (m - (m % 10)) / 10;
    }
    while (nd > 0) { nd--; _pushCC(48 + _workI[nd]); }
  }

  function _pushCC(code) {
    if (_outLen < OUT_CAP) { _outBuf[_outLen] = code; }
    _outLen++;
  }
  function _pushStr(s) {
    var k;
    for (k = 0; k < s.length; k++) { _pushCC(s.charCodeAt(k)); }
  }

  /* fmt(x):§5.1 三分支。返回 0 / −1(域外)。 */
  function _fmt(x) {
    if (x !== x || x === Infinity || x === -Infinity) { return -1; }
    if (x < -4096 || x > 4096) { return -1; }        /* §5.1-4 有效域 */

    /* 整数判定(同时覆盖 |x| < 1e-9 → "0",不输出 -0) */
    var m = _llround(x);
    var d = x - m;
    if (d < 0) { d = -d; }
    if (d < TOL) { _pushInt(m); return 0; }

    /* 有理重建:q = 1..1000,p = llround(x·q),既约化;
     *    |x − p/q| ≤ 1e-9 的候选取误差最小者,平局取 q 小者(升序首遇)。
     *    论证(§5.1-2):不同既约分数距离 ≥ 1/(q1·q2) ≥ 1e-6 ≫ 2e-9,
     *    同真值的 1e-9 邻域必命中同一分数 → 同串。 */
    var bestP = 0;
    var bestQ = 0;
    var bestErr = 0;
    var haveBest = 0;
    var q, p, pq, err, g, p2, q2;
    for (q = 1; q <= 1000; q++) {
      p = _llround(x * q);
      pq = p / q;
      err = x - pq;
      if (err < 0) { err = -err; }
      if (err <= TOL) {
        if (haveBest === 0 || err < bestErr) {
          g = _igcd(p < 0 ? -p : p, q);
          p2 = p / g;
          q2 = q / g;
          bestP = p2;
          bestQ = q2;
          bestErr = err;
          haveBest = 1;
        }
      }
    }
    if (haveBest === 1) {
      _pushInt(bestP);
      if (bestQ !== 1) { _pushCC(47); _pushInt(bestQ); }   /* '/' */
      return 0;
    }

    /* 定点 12 位:s = llround(x·1e12)(半连舍),|s| < 4.096e15 < 2^53 */
    var s = _llround(x * 1e12);
    var neg = s < 0;
    var mag = neg ? -s : s;
    var fp = mag % 1000000000000;                    /* |s| % 1e12 */
    var ip = (mag - fp) / 1000000000000;             /* 整除精确 */
    if (neg) { _pushCC(45); }
    _pushInt(ip);
    /* 12 位左补零后去尾零 */
    var nd = 0;
    var mag2 = fp;
    var pos;
    for (pos = 0; pos < 12; pos++) {
      _workI[11 - pos] = mag2 % 10;
      mag2 = (mag2 - (mag2 % 10)) / 10;
    }
    var last = -1;
    for (pos = 0; pos < 12; pos++) {
      if (_workI[pos] !== 0) { last = pos; }
    }
    if (last >= 0) {
      _pushCC(46);                                   /* '.' */
      for (pos = 0; pos <= last; pos++) { _pushCC(48 + _workI[pos]); }
    }
    return 0;
  }

  /* α/λ 的整数零判定(§5.2-2 唯二特例):|λ − round(λ)| < 1e-9 且 round=0。
   * 恒返 0/1 数字(v2.4.1 修复:原三条退出路径混返 boolean/数字,实根渲染门
   * _isZeroInt(lam)===0 严格比较失配,0<|λ|<0.5 的实根 exp 因子被误省略)。 */
  function _isZeroInt(x) {
    var m = _llround(x);
    if (m !== 0) { return 0; }
    var d = x - m;
    if (d < 0) { d = -d; }
    return d < TOL ? 1 : 0;
  }

  /* x 幂因子(§5.2-1):j=0 省;j=1 "*x";j≥2 "*x^j" */
  function _pushXPow(j) {
    if (j === 0) { return; }
    _pushStr("*x");
    if (j >= 2) { _pushCC(94); _pushCC(48 + j); }    /* '^' + 数字 */
  }

  /* e 指数因子(§5.2-2):α 整数零 → 整段省略;否则 "*exp(fmt(α)*x)" */
  function _pushExp(alpha) {
    if (_isZeroInt(alpha)) { return; }
    _pushStr("*exp(");
    if (_fmt(alpha) !== 0) { _odeFail = -7; return; }
    _pushStr("*x)");
  }

  var _odeFail = 0;      /* fmt 域外 → EINTERNAL */

  /* 排序(§4.2)/ 聚类(§4.3) */

  function _sortRoots(n) {
    var i, j;
    for (i = 1; i < n; i++) {
      var kr = _re[i];
      var ki = _im[i];
      j = i - 1;
      while (j >= 0 && _keyAfter(_re[j], _im[j], kr, ki)) {
        _re[j + 1] = _re[j];
        _im[j + 1] = _im[j];
        j--;
      }
      _re[j + 1] = kr;
      _im[j + 1] = ki;
    }
  }

  /* b 是否严格排在 a 之后(Re 降序,平 Re 按 Im 降序;容差内视为相等→稳定) */
  function _keyAfter(aRe, aIm, bRe, bIm) {
    var d = aRe - bRe;
    if (d < 0) { d = -d; }
    if (d >= TOL) { return bRe > aRe; }
    d = aIm - bIm;
    if (d < 0) { d = -d; }
    if (d >= TOL) { return bIm > aIm; }
    return 0;
  }

  /* 聚类;成功返回簇数,失败 −1(EINTERNAL)。
   * 实根簇按排序序相邻合并(等 Re 组内 Im 降序使实根天然连片);
   * 共轭对在后续未配根中找首个满足 |Δre|<tol、|im(u)+im(v)|<tol、
   * 一正一负者;同 (α,β) 的下一对并入同组(重数层累加)。 */
  function _cluster(n) {
    var i, j, u, v;
    for (i = 0; i < n; i++) { _used[i] = 0; }
    var nC = 0;
    i = 0;
    while (i < n) {
      if (_used[i] === 1) { i++; continue; }
      if (Math.abs(_im[i]) < TOL) {
        /* 实根簇 */
        var lam = _re[i];
        var mult = 1;
        j = i + 1;
        while (j < n && _used[j] === 0 && Math.abs(_im[j]) < TOL &&
               Math.abs(_re[j] - lam) < TOL) {
          _used[j] = 1;
          mult++;
          j++;
        }
        _used[i] = 1;
        _cType[nC] = 1; _cLam[nC] = lam; _cBeta[nC] = 0; _cMult[nC] = mult;
        nC++;
        i = j;
        continue;
      }
      /* 共轭对:首个满足条件的后续未配根 */
      u = i;
      v = -1;
      for (j = i + 1; j < n; j++) {
        if (_used[j] === 1) { continue; }
        if (Math.abs(_re[j] - _re[u]) < TOL &&
            Math.abs(_im[j] + _im[u]) < TOL &&
            _im[u] * _im[j] < 0) { v = j; break; }
      }
      if (v < 0) { return -1; }
      _used[u] = 1; _used[v] = 1;
      var alpha = _re[u];
      var beta = _im[u] < 0 ? -_im[u] : _im[u];      /* β 恒取正(§4.3) */
      var cmult = 1;
      /* 同 (α,β) 的下一对并入同组 */
      for (;;) {
        var u2 = -1;
        for (j = u + 1; j < n; j++) {
          if (_used[j] === 1) { continue; }
          if (Math.abs(_re[j] - alpha) < TOL &&
              Math.abs(Math.abs(_im[j]) - beta) < TOL) { u2 = j; break; }
        }
        if (u2 < 0) { break; }
        var v2 = -1;
        for (j = u2 + 1; j < n; j++) {
          if (_used[j] === 1) { continue; }
          if (Math.abs(_re[j] - alpha) < TOL &&
              Math.abs(_im[j] + _im[u2]) < TOL &&
              _im[u2] * _im[j] < 0) { v2 = j; break; }
        }
        if (v2 < 0) { return -1; }                   /* 共轭残缺 → EINTERNAL */
        _used[u2] = 1; _used[v2] = 1;
        cmult++;
      }
      _cType[nC] = 2; _cLam[nC] = alpha; _cBeta[nC] = beta; _cMult[nC] = cmult;
      nC++;
      i++;
    }
    /* 自检恒等式(§4.3):Σ实重数 + Σ共轭 2×重数 = n */
    var sum = 0;
    for (i = 0; i < nC; i++) {
      sum += _cType[i] === 1 ? _cMult[i] : 2 * _cMult[i];
    }
    if (sum !== n) { return -1; }
    return nC;
  }

  /* 渲染(契约 §5)
   * 项间一律 " + " 连接;前缀 "y = ";四类模板机械拼接。 */

  function _render(nC) {
    _outLen = 0;
    _odeFail = 0;
    _pushStr("y = ");
    var c = 1;                                       /* 任意常数下标,起点 1 */
    var ci;
    for (ci = 0; ci < nC; ci++) {
      if (ci > 0) { _pushStr(" + "); }
      var t = _cType[ci];
      var lam = _cLam[ci];
      var beta = _cBeta[ci];
      var mult = _cMult[ci];
      var j;
      if (t === 1) {
        /* 实根:重数 k → j=0..k−1,C*x^j*exp(λx);λ=0 省 exp */
        for (j = 0; j < mult; j++) {
          if (j > 0) { _pushStr(" + "); }
          _pushCC(67); _pushCC(0x2080 + c);          /* C₁.. */
          c++;
          _pushXPow(j);
          if (_isZeroInt(lam) === 0) {
            _pushStr("*exp(");
            if (_fmt(lam) !== 0) { _odeFail = -7; return; }
            _pushStr("*x)");
          }
        }
      } else {
        /* 共轭对:每层先 cos 后 sin,常数连续递增;α=0 省 exp;β 恒正 */
        for (j = 0; j < mult; j++) {
          if (j > 0) { _pushStr(" + "); }
          _pushCC(67); _pushCC(0x2080 + c);
          c++;
          _pushXPow(j);
          _pushExp(lam);
          _pushStr("*cos(");
          if (_fmt(beta) !== 0) { _odeFail = -7; return; }
          _pushStr("*x)");
          _pushStr(" + ");
          _pushCC(67); _pushCC(0x2080 + c);
          c++;
          _pushXPow(j);
          _pushExp(lam);
          _pushStr("*sin(");
          if (_fmt(beta) !== 0) { _odeFail = -7; return; }
          _pushStr("*x)");
        }
      }
    }
    if (c - 1 !== _sumConsts(nC)) { _odeFail = -7; return; }  /* 常数总数 = n */
  }

  function _sumConsts(nC) {
    var s = 0;
    var i;
    for (i = 0; i < nC; i++) { s += _cType[i] === 1 ? _cMult[i] : 2 * _cMult[i]; }
    return s;
  }

  /* 入口(契约 §8) */

  function solveOde(src) {
    _result.code = 0;
    _result.out = "";
    _err = 0;
    _opCount++;

    var rc = _parse(src);
    if (rc !== 0) { _result.code = rc; return _result; }

    var n = _deg;
    var i;
    if (n === 1) {
      _re[0] = -_a[0] / _a[1];                       /* n=1 线性根 λ = −a[0]/a[1] */
      _im[0] = 0;
    } else {
      /* n=2..4 复用求解层(§4.1):_coeffs 低位在前 [c,b,a] 布局 */
      for (i = 0; i <= n; i++) { _coeffs[i] = _a[i]; }
      if (n === 2) { solveQuadratic(); }
      else if (n === 3) { solveCubic(); }
      else { solveQuartic(); }
      if (_solveError !== 0 || _rootCount !== n) {
        _result.code = -7;                           /* 数值病态/实现缺陷,绝不硬凑 */
        return _result;
      }
      for (i = 0; i < n; i++) { _re[i] = _rootRe[i]; _im[i] = _rootIm[i]; }
    }

    _sortRoots(n);
    var nC = _cluster(n);
    if (nC < 0) { _result.code = -7; return _result; }

    _render(nC);
    if (_odeFail !== 0) { _result.code = _odeFail; return _result; }
    if (_outLen > OUT_CAP) { _result.code = -7; return _result; }  /* 防御:估算上界被破 */

    var s = "";
    for (i = 0; i < _outLen; i++) { s = s + String.fromCharCode(_outBuf[i]); }
    _result.out = s;
    return _result;
  }

  return {
    solveOde: solveOde,
    reset: reset,
    getLastError: function () { return _err; },
    getOpCount: getOpCount,
    allocCount: allocCount
  };
})();

if (typeof module === "object" && module && module.exports) {
  module.exports = ApolloOde;
}
