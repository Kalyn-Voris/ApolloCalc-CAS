/* ============================================================
 * ApolloCalc · core_base.js — 进制转换 + SI 前缀（文件 8）
 * Lite Wearable / JerryScript / 堆上限 128KB / Zero-GC
 * 职责：整数 → 2/8/10/16 进制字符串（字符码缓冲输出）
 *       数值 → SI 前缀规范化（尾数 + 前缀字符码）
 * 依赖：无（自包含，仅用 Math.round / Math.floor / isFinite / String.fromCharCode）
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象/数组字面量
 *       字符串拼接只在 baseToString / siToString 两个"UI 层调用一次"的出口
 * UI 契约：decToXxx(n) 返回 1 后调 baseToString() 取串（缓冲逆序存放，出口内反转）；
 *          返回 0 表示失败（_baseLen=0，baseToString() 得空串）；
 *          siNormalize(v) 返回 1 后读 _siMantissa / _siPrefixCode / _siPrefixExp
 *          或直接 siToString()；_siValid=0 时 siToString() 得空串
 * ============================================================ */

/* ==================== 文件 8 专属全局变量（唯一声明处） ==================== */
var _baseBuf = new Int32Array(64);   /* 字符码缓冲：0-9 存 48-57，A-F 存 65-70，负号存 45 */
var _baseLen = 0;                    /* 有效字符数（逆序：低位在前，出口反转） */

var _siMantissa = 0.0;               /* SI 尾数（1 ≤ |m| < 1000） */
var _siPrefixCode = 0;               /* SI 前缀字符码（如 'k' = 107；空串用 0 表示） */
var _siPrefixExp = 0;                /* 10 的幂（3 的倍数，范围 -24..+24） */
var _siValid = 0;                    /* 0=无效（0 或非有限或超范围） 1=有效 */

/* ==================== 模块一：进制转换 ====================
 * 只支持 2/8/10/16；超过 53 位安全整数范围拒绝。
 * _baseBuf 逆序存放（低位在前），baseToString 反向遍历得正序字符串。
 */

/* 内核：十进制整数 → base 进制，字符码写入 _baseBuf（返回 1 成功 / 0 失败） */
function decToBase(n, base) {
  if (base !== 2 && base !== 8 && base !== 10 && base !== 16) {
    _baseLen = 0;
    return 0;
  }
  if (!isFinite(n)) {
    _baseLen = 0;
    return 0;
  }
  if (n > 9007199254740991 || n < -9007199254740991) {
    _baseLen = 0;
    return 0;
  }
  var i = 0;
  var neg = 0;
  var v = Math.round(n);   /* 非整数入参按就近取整收敛（Math.round(-0.5)=-0 → 按 0 输出） */
  if (v === 0) {
    _baseBuf[0] = 48;      /* '0' */
    _baseLen = 1;
    return 1;
  }
  if (v < 0) { neg = 1; v = -v; }
  while (v > 0 && i < 60) {
    var d = v % base;
    if (d < 10) { _baseBuf[i] = 48 + d; }        /* '0'-'9' */
    else { _baseBuf[i] = 65 + (d - 10); }        /* 'A'-'F' */
    v = Math.floor(v / base);
    i++;
  }
  if (neg) { _baseBuf[i] = 45; i++; }            /* '-'（逆序存放 → 反转后位于最前） */
  _baseLen = i;
  return 1;
}

/* 出口：把 _baseBuf 逆序拼成正序字符串（UI 层调用一次，此处允许字符串拼接） */
function baseToString() {
  var s = "";
  var i = 0;
  for (i = _baseLen - 1; i >= 0; i--) {
    s = s + String.fromCharCode(_baseBuf[i]);
  }
  return s;
}

/* 便捷封装（返回 1 成功 / 0 失败，字符串经 baseToString() 取） */
function decToBin(n) { return decToBase(n, 2); }
function decToOct(n) { return decToBase(n, 8); }
function decToHex(n) { return decToBase(n, 16); }

/* ==================== 模块二：SI 前缀规范化 ====================
 * y/z/a/f/p/n/µ/m/（空）/k/M/G/T/P/E/Z/Y ↔ 指数 -24..+24（3 的倍数）。
 * 前缀表用 if 链（铁律禁数组字面量）；空串前缀用字符码 0 表示。
 */

/* 指数（3 的倍数）→ 前缀字符码；超范围返回 -1；空串返回 0 */
function siPrefixCode(exp) {
  if (exp === -24) { return 121; }   /* 'y' */
  if (exp === -21) { return 122; }   /* 'z' */
  if (exp === -18) { return 97; }    /* 'a' */
  if (exp === -15) { return 102; }   /* 'f' */
  if (exp === -12) { return 112; }   /* 'p' */
  if (exp === -9)  { return 110; }   /* 'n' */
  if (exp === -6)  { return 181; }   /* 'µ'（U+00B5 微符号，若字体不支持改 956 'μ'） */
  if (exp === -3)  { return 109; }   /* 'm' */
  if (exp === 0)   { return 0; }     /* 空串 */
  if (exp === 3)   { return 107; }   /* 'k' */
  if (exp === 6)   { return 77; }    /* 'M' */
  if (exp === 9)   { return 71; }    /* 'G' */
  if (exp === 12)  { return 84; }    /* 'T' */
  if (exp === 15)  { return 80; }    /* 'P' */
  if (exp === 18)  { return 69; }    /* 'E' */
  if (exp === 21)  { return 90; }    /* 'Z' */
  if (exp === 24)  { return 89; }    /* 'Y' */
  return -1;                         /* 超出范围 */
}

/* 内核：v → 尾数 + 前缀（返回 1 成功 / 0 无效）。
 * 无效（0 / 非有限 / 超 SI 范围 / 前缀缺失）时四元组全部清零，不残留上次结果。 */
function siNormalize(v) {
  if (!isFinite(v) || v === 0) {
    _siMantissa = 0;
    _siPrefixCode = 0;
    _siPrefixExp = 0;
    _siValid = 0;
    return 0;
  }
  var av = v < 0 ? -v : v;
  var exp = 0;
  while (av >= 1000 && exp < 24) { av = av / 1000; exp += 3; }
  while (av < 1 && exp > -24) { av = av * 1000; exp -= 3; }
  if (av > 0.999999999999 && av < 1.000000000001) { av = 1; }
  /* 回吸：×1000 连乘累积舍入会让精确千的幂（如 1e-15/1e-24）的尾数落在 1±2ulp，
   * 不回吸会显示 1.0000000000000002 y；1e-12 窗口远大于 1e-16 量级舍入、
   * 远小于显示精度，只吞并舍入噪声不改写真实输入 */
  if (av >= 1000 || av < 1) {
    /* 缩放后仍越界（|v| ≥ 1e27 或 < 1e-27）或撞到指数限仍不落界 → 无效 */
    _siMantissa = 0;
    _siPrefixCode = 0;
    _siPrefixExp = 0;
    _siValid = 0;
    return 0;
  }
  var pc = siPrefixCode(exp);
  if (pc < 0) {
    _siMantissa = 0;
    _siPrefixCode = 0;
    _siPrefixExp = 0;
    _siValid = 0;
    return 0;
  }
  _siMantissa = (v < 0 ? -av : av);
  _siPrefixCode = pc;
  _siPrefixExp = exp;
  _siValid = 1;
  return 1;
}

/* 出口：尾数 + 空格 + 前缀字符（无前缀时不带空格；无效时空串。UI 层调用一次） */
function siToString() {
  if (_siValid !== 1) { return ""; }
  var s = "" + _siMantissa;
  if (_siPrefixCode !== 0) {
    s = s + " " + String.fromCharCode(_siPrefixCode);
  }
  return s;
}

/* ==================== 模块三：复位 ==================== */
function baseReset() {
  _baseLen = 0;
  _siMantissa = 0.0;
  _siPrefixCode = 0;
  _siPrefixExp = 0;
  _siValid = 0;
}
