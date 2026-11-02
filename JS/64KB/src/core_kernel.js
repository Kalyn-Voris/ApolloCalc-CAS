// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · core_parser.js — Token 解析器（文件 1/6）
 * Lite Wearable / JerryScript / 堆上限 64KB / Zero-GC
 * Token 编码: word = (type << 8) | payload；word === 0 即 T_END 哨兵
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象字面量 */

/* 全局内存池：全内核唯一声明处，后续文件直接引用 */
var tokenPool = new Int32Array(256);
var tokenCount = 0;
var numberPool = new Float64Array(128);  /* Token 只存索引 */
var numberCount = 0;
var evalStack = new Float64Array(64);    /* RPN 求值栈 */
var matrixPool = new Float64Array(42);   /* 6 元线性方程组增广矩阵 (6x7) */

/* 精确数值系统（core_exact.js 使用） */
var exactNum = 0;          /* 分子 */
var exactDen = 1;          /* 分母，恒为正 */
var exactRad = 1;          /* 根号内数（无平方因子），1 表示无根式 */
var exactIsValid = false;
var approxValue = 0.0;

/* 复数临时量（core_complex.js 使用） */
var _tmpRe = 0.0;
var _tmpIm = 0.0;

/* 迭代运算寄存器 */
var iterVar = 0;
var iterStart = 0;
var iterEnd = 0;
var iterAccum = 0;
var iterProd = 1;

/* 全局状态 */
var angleMode = "DEG";             /* "DEG" | "RAD" */
var DEG_TO_RAD = Math.PI / 180;
var RAD_TO_DEG = 180 / Math.PI;

var T_END = 0;
var T_NUM = 1;       /* payload: numberPool 索引 */
var T_VAR = 2;       /* payload: 0 = x（预留多变量扩展） */
var T_CONST = 3;     /* payload: 0 = e, 1 = π */
var T_OP = 4;        /* payload: OP_* */
var T_LP = 5;
var T_RP = 6;
var T_FUNC = 7;      /* payload: F_* */
var T_EQ = 8;
var T_COMMA = 9;
var T_UMINUS = 10;

/* 二元运算符 payload */
var OP_ADD = 0;
var OP_SUB = 1;
var OP_MUL = 2;
var OP_DIV = 3;
var OP_POW = 4;

/* 函数 payload */
var F_SIN = 1;
var F_COS = 2;
var F_TAN = 3;
var F_ASIN = 4;
var F_ACOS = 5;
var F_ATAN = 6;
var F_ATAN2 = 7;
var F_SQRT = 8;
var F_LN = 9;
var F_LOG = 10;
var F_EXP = 11;
var F_ABS = 12;

/* 常数 payload */
var C_E = 0;
var C_PI = 1;

/* 解析错误码 */
var ERR_NONE = 0;
var ERR_BADCHAR = 1;
var ERR_OVERFLOW = 2;    /* tokenPool / numberPool 溢出 */
var ERR_PAREN = 3;
var ERR_EMPTY = 4;

/* 扫描游标 */
var parseError = ERR_NONE;
var scanPos = 0;
var scanLen = 0;

function emitToken(type, payload) {
  if (tokenCount >= 256) { parseError = ERR_OVERFLOW; return -1; }
  tokenPool[tokenCount] = (type << 8) | payload;
  tokenCount++;
  return 0;
}

function pushNumber(v) {
  if (numberCount >= 128) { parseError = ERR_OVERFLOW; return -1; }
  numberPool[numberCount] = v;
  numberCount++;
  return numberCount - 1;
}

/* prev 收尾一个操作数 且 next 开启一个操作数 → 注入隐式乘号 */
function canFollowImplicit(prevType, nextType) {
  if (prevType === T_NUM || prevType === T_VAR || prevType === T_CONST || prevType === T_RP) {
    if (nextType === T_NUM || nextType === T_VAR || nextType === T_CONST || nextType === T_FUNC || nextType === T_LP) {
      return 1;
    }
  }
  return 0;
}

/* 一元上下文：开头 / 运算符后 / 左括号后 / 等号后 / 逗号后 / 一元负号后 */
function isUnaryContext(prevType) {
  return (prevType === T_END || prevType === T_OP || prevType === T_LP || prevType === T_EQ || prevType === T_COMMA || prevType === T_UMINUS) ? 1 : 0;
}

/* 数字扫描（小数 + 科学计数法）
 * 直接写 numberPool，返回索引；失败返回 -1
 * Horner 累积 v = v*10 + d；小数只记位数，最后一次性除权还原
 * 'e' 歧义：后跟数字（或 +/- 数字）→ 科学计数法；否则终止扫描，'e' 交回主循环当自然常数 */
function scanNumber(src) {
  var v = 0;
  var fracDigits = 0;
  var seenDot = 0;
  var hadDigit = 0;
  var c = 0;

  while (scanPos < scanLen) {
    c = src.charCodeAt(scanPos);
    if (c >= 48 && c <= 57) {                /* '0'..'9' */
      v = v * 10 + (c - 48);
      if (seenDot === 1) { fracDigits++; }
      hadDigit = 1;
      scanPos++;
    } else if (c === 46 && seenDot === 0) {  /* '.' 首次 */
      seenDot = 1;
      scanPos++;
    } else if (c === 101 || c === 69) {      /* 'e' / 'E' 科学计数法歧义判定 */
      var j = scanPos + 1;
      var c1 = (j < scanLen) ? src.charCodeAt(j) : 0;
      var expNeg = 0;
      var isExp = 0;
      if (c1 >= 48 && c1 <= 57) {
        isExp = 1;
      } else if ((c1 === 43 || c1 === 45) && (j + 1 < scanLen)) {
        var c2 = src.charCodeAt(j + 1);
        if (c2 >= 48 && c2 <= 57) {
          isExp = 1;
          if (c1 === 45) { expNeg = 1; }
          j = j + 1;
        }
      }
      if (isExp === 1) {
        var expVal = 0;
        scanPos = j;
        while (scanPos < scanLen) {
          var dc = src.charCodeAt(scanPos);
          if (dc < 48 || dc > 57) { break; }
          expVal = expVal * 10 + (dc - 48);
          scanPos++;
        }
        if (expNeg === 1) { expVal = -expVal; }
        v = v * Math.pow(10, expVal);
      }
      break;  /* 非指数 → 'e' 归主循环；是指数 → 已消费完，数字到此终止 */
    } else {
      break;
    }
  }

  if (hadDigit === 0) { parseError = ERR_BADCHAR; return -1; }
  if (fracDigits > 0) { v = v / Math.pow(10, fracDigits); }
  return pushNumber(v);
}

/* 函数名硬编码字符树
 * 命中：返回函数 ID 并消费字符；未命中：返回 0 且不消费 */
function matchFunction(src) {
  var c0 = src.charCodeAt(scanPos);
  var c1 = (scanPos + 1 < scanLen) ? src.charCodeAt(scanPos + 1) : 0;
  var c2 = (scanPos + 2 < scanLen) ? src.charCodeAt(scanPos + 2) : 0;
  var c3 = (scanPos + 3 < scanLen) ? src.charCodeAt(scanPos + 3) : 0;
  var f = 0;
  var n = 0;

  if (c0 === 115) {                                                          /* 's' */
    if (c1 === 105 && c2 === 110) { f = F_SIN; n = 3; }                      /* sin */
    else if (c1 === 113 && c2 === 114 && c3 === 116) { f = F_SQRT; n = 4; }  /* sqrt */
  } else if (c0 === 99) {                                                    /* 'c' */
    if (c1 === 111 && c2 === 115) { f = F_COS; n = 3; }                      /* cos */
  } else if (c0 === 116) {                                                   /* 't' */
    if (c1 === 97 && c2 === 110) { f = F_TAN; n = 3; }                       /* tan */
  } else if (c0 === 97) {                                                    /* 'a' */
    if (c1 === 115 && c2 === 105 && c3 === 110) { f = F_ASIN; n = 4; }       /* asin */
    else if (c1 === 99 && c2 === 111 && c3 === 115) { f = F_ACOS; n = 4; }   /* acos */
    else if (c1 === 116 && c2 === 97 && c3 === 110) {                        /* atan / atan2 */
      var c4 = (scanPos + 4 < scanLen) ? src.charCodeAt(scanPos + 4) : 0;
      if (c4 === 50) { f = F_ATAN2; n = 5; }                                 /* atan2 */
      else { f = F_ATAN; n = 4; }
    }
    else if (c1 === 98 && c2 === 115) { f = F_ABS; n = 3; }                  /* abs */
  } else if (c0 === 108) {                                                   /* 'l' */
    if (c1 === 110) { f = F_LN; n = 2; }                                     /* ln */
    else if (c1 === 111 && c2 === 103) { f = F_LOG; n = 3; }                 /* log */
  } else if (c0 === 101) {                                                   /* 'e' */
    if (c1 === 120 && c2 === 112) { f = F_EXP; n = 3; }                      /* exp */
  }

  if (f > 0) { scanPos += n; }
  return f;
}

/* 主入口：parseExpression
 * 输入：表达式字符串（"2x^2+3x-5=0" / "sin(30)"）
 * 输出：tokenPool / numberPool / tokenCount / parseError（全局）
 * 返回：parseError（0 = 成功） */
function parseExpression(src) {
  tokenCount = 0;
  numberCount = 0;
  parseError = ERR_NONE;
  scanPos = 0;
  scanLen = src.length;
  var depth = 0;
  var prevType = T_END;
  var c = 0;
  var f = 0;

  if (scanLen === 0) { parseError = ERR_EMPTY; return parseError; }

  while (scanPos < scanLen) {
    c = src.charCodeAt(scanPos);

    /* 空白 */
    if (c === 32 || c === 9 || c === 13 || c === 10) { scanPos++; continue; }

    /* 数字 / 小数点 */
    if ((c >= 48 && c <= 57) || c === 46) {
      f = scanNumber(src);
      if (f < 0) { break; }
      if (canFollowImplicit(prevType, T_NUM) === 1) {
        if (emitToken(T_OP, OP_MUL) < 0) { break; }
      }
      if (emitToken(T_NUM, f) < 0) { break; }
      prevType = T_NUM;
      continue;
    }

    /* 负号：'-' / '−'(8722) */
    if (c === 45 || c === 8722) {
      if (isUnaryContext(prevType) === 1) {
        if (emitToken(T_UMINUS, 0) < 0) { break; }
        prevType = T_UMINUS;
      } else {
        if (emitToken(T_OP, OP_SUB) < 0) { break; }
        prevType = T_OP;
      }
      scanPos++;
      continue;
    }

    /* 加号：一元正号丢弃 / 二元加 */
    if (c === 43) {
      if (isUnaryContext(prevType) === 1) { scanPos++; continue; }
      if (emitToken(T_OP, OP_ADD) < 0) { break; }
      prevType = T_OP;
      scanPos++;
      continue;
    }

    if (c === 42 || c === 215) {           /* '*' / '×' */
      if (emitToken(T_OP, OP_MUL) < 0) { break; }
      prevType = T_OP;
      scanPos++;
      continue;
    }
    if (c === 47 || c === 247) {           /* '/' / '÷' */
      if (emitToken(T_OP, OP_DIV) < 0) { break; }
      prevType = T_OP;
      scanPos++;
      continue;
    }
    if (c === 94) {                        /* '^' */
      if (emitToken(T_OP, OP_POW) < 0) { break; }
      prevType = T_OP;
      scanPos++;
      continue;
    }

    if (c === 40) {                        /* '(' */
      if (canFollowImplicit(prevType, T_LP) === 1) {
        if (emitToken(T_OP, OP_MUL) < 0) { break; }
      }
      if (emitToken(T_LP, 0) < 0) { break; }
      depth++;
      prevType = T_LP;
      scanPos++;
      continue;
    }
    if (c === 41) {                        /* ')' */
      if (depth <= 0) { parseError = ERR_PAREN; break; }
      if (emitToken(T_RP, 0) < 0) { break; }
      depth--;
      prevType = T_RP;
      scanPos++;
      continue;
    }

    if (c === 61) {                        /* '=' */
      if (emitToken(T_EQ, 0) < 0) { break; }
      prevType = T_EQ;
      scanPos++;
      continue;
    }
    if (c === 44) {                        /* ',' */
      if (emitToken(T_COMMA, 0) < 0) { break; }
      prevType = T_COMMA;
      scanPos++;
      continue;
    }

    /* 字母：函数名 / 变量 x / 常数 e / pi */
    if ((c >= 97 && c <= 122) || c === 88) {
      f = matchFunction(src);
      if (f > 0) {
        if (canFollowImplicit(prevType, T_FUNC) === 1) {
          if (emitToken(T_OP, OP_MUL) < 0) { break; }
        }
        if (emitToken(T_FUNC, f) < 0) { break; }
        prevType = T_FUNC;
        continue;
      }
      if (c === 120 || c === 88) {         /* 'x' / 'X' 变量 */
        if (canFollowImplicit(prevType, T_VAR) === 1) {
          if (emitToken(T_OP, OP_MUL) < 0) { break; }
        }
        if (emitToken(T_VAR, 0) < 0) { break; }
        prevType = T_VAR;
        scanPos++;
        continue;
      }
      if (c === 101) {                     /* 'e' 自然常数（exp 已被上方消费） */
        if (canFollowImplicit(prevType, T_CONST) === 1) {
          if (emitToken(T_OP, OP_MUL) < 0) { break; }
        }
        if (emitToken(T_CONST, C_E) < 0) { break; }
        prevType = T_CONST;
        scanPos++;
        continue;
      }
      if (c === 112) {                     /* 'p' → 期待 "pi" */
        var cn = (scanPos + 1 < scanLen) ? src.charCodeAt(scanPos + 1) : 0;
        if (cn === 105) {
          if (canFollowImplicit(prevType, T_CONST) === 1) {
            if (emitToken(T_OP, OP_MUL) < 0) { break; }
          }
          if (emitToken(T_CONST, C_PI) < 0) { break; }
          prevType = T_CONST;
          scanPos += 2;
          continue;
        }
        parseError = ERR_BADCHAR;
        break;
      }
      parseError = ERR_BADCHAR;
      break;
    }

    /* 数学特殊符号 */
    if (c === 8730) {                      /* '√' → sqrt */
      if (canFollowImplicit(prevType, T_FUNC) === 1) {
        if (emitToken(T_OP, OP_MUL) < 0) { break; }
      }
      if (emitToken(T_FUNC, F_SQRT) < 0) { break; }
      prevType = T_FUNC;
      scanPos++;
      continue;
    }
    if (c === 960) {                       /* 'π' */
      if (canFollowImplicit(prevType, T_CONST) === 1) {
        if (emitToken(T_OP, OP_MUL) < 0) { break; }
      }
      if (emitToken(T_CONST, C_PI) < 0) { break; }
      prevType = T_CONST;
      scanPos++;
      continue;
    }

    parseError = ERR_BADCHAR;
    break;
  }

  if (parseError !== ERR_NONE) { tokenCount = 0; numberCount = 0; return parseError; }
  if (depth !== 0) { parseError = ERR_PAREN; tokenCount = 0; numberCount = 0; return parseError; }
  if (tokenCount === 0) { parseError = ERR_EMPTY; return parseError; }
  return ERR_NONE;
}

/* UI 层调试输出（唯一允许转字符串处，内核路径禁调） */
function funcName(id) {
  if (id === F_SIN) { return "sin"; }
  if (id === F_COS) { return "cos"; }
  if (id === F_TAN) { return "tan"; }
  if (id === F_ASIN) { return "asin"; }
  if (id === F_ACOS) { return "acos"; }
  if (id === F_ATAN) { return "atan"; }
  if (id === F_ATAN2) { return "atan2"; }
  if (id === F_SQRT) { return "sqrt"; }
  if (id === F_LN) { return "ln"; }
  if (id === F_LOG) { return "log"; }
  if (id === F_EXP) { return "exp"; }
  if (id === F_ABS) { return "abs"; }
  return "?";
}

function dumpTokenStream() {
  var s = "";
  var w = 0;
  var t = 0;
  var p = 0;
  for (var i = 0; i < tokenCount; i++) {
    w = tokenPool[i];
    t = w >> 8;
    p = w & 255;
    if (i > 0) { s = s + " "; }
    if (t === T_NUM) { s = s + numberPool[p]; }
    else if (t === T_VAR) { s = s + "x"; }
    else if (t === T_CONST) { s = (p === C_PI) ? s + "π" : s + "e"; }
    else if (t === T_OP) {
      if (p === OP_ADD) { s = s + "+"; }
      else if (p === OP_SUB) { s = s + "-"; }
      else if (p === OP_MUL) { s = s + "*"; }
      else if (p === OP_DIV) { s = s + "/"; }
      else { s = s + "^"; }
    }
    else if (t === T_LP) { s = s + "("; }
    else if (t === T_RP) { s = s + ")"; }
    else if (t === T_EQ) { s = s + "="; }
    else if (t === T_COMMA) { s = s + ","; }
    else if (t === T_UMINUS) { s = s + "neg"; }
    else if (t === T_FUNC) { s = s + funcName(p); }
  }
  return s;
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · core_math_functions.js — 数学执行层（文件 2/6）
 * Lite Wearable / JerryScript / 堆上限 64KB / Zero-GC
 * 依赖文件 1 全局（只引用，不重复声明）：
 *   angleMode("DEG"|"RAD") / DEG_TO_RAD / RAD_TO_DEG
 *   F_SIN..F_ABS 函数 ID
 * 必须在 core_parser.js 之后加载
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象字面量 */

/* 文件 2 专属全局变量（唯一声明处） */
var _dmsDeg = 0;    /* 度：带符号整数。分秒恒非负，符号全挂在度上 */
var _dmsMin = 0;    /* 分：非负整数 0..59 */
var _dmsSec = 0.0;  /* 秒：非负浮点 [0,60) */

/* Math.log10 兜底（仅加载期执行一次，热路径不经过）
 * 个别 JerryScript 裁剪构建未启用 ES6 Math.log10，缺失时以 log*LOG10E 顶替 */
if (Math.log10 === undefined) {
  Math.log10 = function (v) { return Math.log(v) * Math.LOG10E; };
}

/* 模块一：三角函数（DEG / RAD 双模式） */

function mathSin(v) {
  if (angleMode === "DEG") { return Math.sin(v * DEG_TO_RAD); }
  return Math.sin(v);
}

function mathCos(v) {
  if (angleMode === "DEG") { return Math.cos(v * DEG_TO_RAD); }
  return Math.cos(v);
}

function mathTan(v) {
  var r = 0.0;
  if (angleMode === "DEG") { r = Math.tan(v * DEG_TO_RAD); }
  else { r = Math.tan(v); }
  /* 90°/270° 等奇点：π/2 非精确表示致 tan 返回 1e16 量级浮点，钳为 Infinity */
  if (Math.abs(r) > 1e10) { return Infinity; }
  return r;
}

function mathAsin(v) {
  if (angleMode === "DEG") { return Math.asin(v) * RAD_TO_DEG; }
  return Math.asin(v);
}

function mathAcos(v) {
  if (angleMode === "DEG") { return Math.acos(v) * RAD_TO_DEG; }
  return Math.acos(v);
}

function mathAtan(v) {
  if (angleMode === "DEG") { return Math.atan(v) * RAD_TO_DEG; }
  return Math.atan(v);
}

function mathAtan2(y, x) {
  if (angleMode === "DEG") { return Math.atan2(y, x) * RAD_TO_DEG; }
  return Math.atan2(y, x);
}

/* 模块二：对数 / 指数 / 绝对值 */

function mathLn(v) {
  return Math.log(v);
}

function mathLog10(v) {
  return Math.log10(v);
}

function mathExp(v) {
  return Math.exp(v);
}

function mathAbs(v) {
  return Math.abs(v);
}

/* 模块三：度分秒（D°M′S″）转换 */

function degToDms(deg) {
  /* 结果写全局 _dmsDeg/_dmsMin/_dmsSec（零分配，无字符串） */
  var sign = 1;
  var a = deg;
  var d = 0;
  var fm = 0.0;
  var m = 0;
  var s = 0.0;

  if (deg < 0) { sign = -1; a = -deg; }
  d = Math.floor(a);
  fm = (a - d) * 60.0;
  m = Math.floor(fm);
  s = (fm - m) * 60.0;

  /* 浮点抖动折算：秒位 59.999999999… 折入分位，避免出现 29′60″ 形态输出 */
  if (s >= 59.999999999) { s = 0.0; m = m + 1; }
  if (m >= 60) { m = 0; d = d + 1; }

  /* sign=-1 且 d=0 时得 IEEE754 负零 -0，算术上等价 0，符号信息不丢 */
  _dmsDeg = sign * d;
  _dmsMin = m;
  _dmsSec = s;
}

function dmsToDeg(d, m, s) {
  /* 零分配：纯算术合并，符号由 d 自带（-30°30′ → d=-30 返回 -29.5） */
  return d + m / 60.0 + s / 3600.0;
}

/* 模块四：函数派发入口 */

/* funcId：文件 1 的 F_* 常量。
 * atan2 双参：arg1 = y（对边），arg2 = x（邻边）；其余函数只读 arg1。
 * 零分配：switch 整型派发直通原生数学，无对象创建 */
function applyFunction(funcId, arg1, arg2) {
  switch (funcId) {
    case F_SIN: return mathSin(arg1);
    case F_COS: return mathCos(arg1);
    case F_TAN: return mathTan(arg1);
    case F_ASIN: return mathAsin(arg1);
    case F_ACOS: return mathAcos(arg1);
    case F_ATAN: return mathAtan(arg1);
    case F_ATAN2: return mathAtan2(arg1, arg2);
    case F_SQRT: return Math.sqrt(arg1);
    case F_LN: return mathLn(arg1);
    case F_LOG: return mathLog10(arg1);
    case F_EXP: return mathExp(arg1);
    case F_ABS: return mathAbs(arg1);
    default: return NaN;
  }
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · core_complex.js — 极简复数运算层（零分配），供文件 6 方程求解器使用。
 * 依赖文件 1 已声明的全局结果寄存器 _tmpRe / _tmpIm，此处不重复声明 */

/* 文件 3 专属全局变量（唯一声明处） */
var _aRe = 0.0;
var _aIm = 0.0;
var _bRe = 0.0;
var _bIm = 0.0;

/* 模块一：基础四则运算 */

function cAdd() {
  _tmpRe = _aRe + _bRe;
  _tmpIm = _aIm + _bIm;
}

function cSub() {
  _tmpRe = _aRe - _bRe;
  _tmpIm = _aIm - _bIm;
}

function cMul() {
  /* 局部 let 暂存：直接写 _tmpRe 会污染后用的 _aRe/_bRe */
  let re = _aRe * _bRe - _aIm * _bIm;
  let im = _aRe * _bIm + _aIm * _bRe;
  _tmpRe = re;
  _tmpIm = im;
}

function cDiv() {
  let d = _bRe * _bRe + _bIm * _bIm;
  if (d === 0.0) {
    /* 分母为零：置 NaN，由上层判无效根 */
    _tmpRe = NaN;
    _tmpIm = NaN;
    return;
  }
  let re = (_aRe * _bRe + _aIm * _bIm) / d;
  let im = (_aIm * _bRe - _aRe * _bIm) / d;
  _tmpRe = re;
  _tmpIm = im;
}

/* 模块二：模长、辐角与共轭 */

function cAbs() {
  return Math.sqrt(_aRe * _aRe + _aIm * _aIm);
}

function cArg() {
  return Math.atan2(_aIm, _aRe);
}

function cConj() {
  _tmpRe = _aRe;
  _tmpIm = -_aIm;
}

/* 模块三：复数开方（核心，精度敏感） */

function cSqrt() {
  let re = _aRe;
  let im = _aIm;

  if (re === 0.0 && im === 0.0) {
    _tmpRe = 0.0;
    _tmpIm = 0.0;
    return;
  }

  /* 纯实数退化情形：虚部为 0 时直接开实部，绕开 atan2(0, x) 的精度损失 */
  if (im === 0.0) {
    if (re >= 0.0) {
      _tmpRe = Math.sqrt(re);
      _tmpIm = 0.0;
    } else {
      /* 负实数：根为纯虚数 ±√|re|·i，约定取正虚部分支 */
      _tmpRe = 0.0;
      _tmpIm = Math.sqrt(-re);
    }
    return;
  }

  /* 极坐标法：√r · (cos(θ/2) + i·sin(θ/2))，另一根取反（求解器按需取反） */
  let r = Math.sqrt(re * re + im * im);
  let theta = Math.atan2(im, re);
  let half = theta * 0.5;
  let sr = Math.sqrt(r);
  _tmpRe = sr * Math.cos(half);
  _tmpIm = sr * Math.sin(half);
}

/* 模块四：复数乘幂（快速幂，平方-乘法） */

function cPowInt(n) {
  let baseRe = _aRe;
  let baseIm = _aIm;
  let exp = n;

  /* 负指数：先取倒数转为正指数幂 */
  if (exp < 0) {
    let d = baseRe * baseRe + baseIm * baseIm;
    if (d === 0.0) {
      /* 0 的负次幂无定义：置 NaN 交由上层处理 */
      _tmpRe = NaN;
      _tmpIm = NaN;
      return;
    }
    let invRe = baseRe / d;
    let invIm = -baseIm / d;
    baseRe = invRe;
    baseIm = invIm;
    exp = -exp;
  }

  let accRe = 1.0;
  let accIm = 0.0;

  /* 平方-乘法：从最低位起逐位消费指数 */
  let e = exp;
  for (;;) {
    if ((e & 1) === 1) {
      let nRe = accRe * baseRe - accIm * baseIm;
      let nIm = accRe * baseIm + accIm * baseRe;
      accRe = nRe;
      accIm = nIm;
    }
    e = e >> 1;
    if (e === 0) {
      break;
    }
    let sqRe = baseRe * baseRe - baseIm * baseIm;
    let sqIm = 2.0 * baseRe * baseIm;
    baseRe = sqRe;
    baseIm = sqIm;
  }

  _tmpRe = accRe;
  _tmpIm = accIm;
}

/* 模块五：复位辅助 */

function cReset() {
  /* 六寄存器整体清零；求解器每轮迭代前调用 */
  _aRe = 0.0;
  _aIm = 0.0;
  _bRe = 0.0;
  _bIm = 0.0;
  _tmpRe = 0.0;
  _tmpIm = 0.0;
}

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

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ============================================================
 * ApolloCalc · core_evaluator.js — RPN 求值器（文件 5/6）
 * Lite Wearable / JerryScript / 堆上限 64KB / Zero-GC
 * 职责：Shunting-yard 中缀→后缀（显式栈迭代）、RPN 栈式求值、
 *       方程 f(x)=g(x) → f(x)-g(x)、数值采样法提取多项式系数
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象/数组字面量
 * ============================================================ */

/* 文件 5 专属全局变量（唯一声明处） */
var _rpnLen = 0;
var _rpnToken = new Int32Array(256);      /* 逆波兰 Token 流（与 tokenPool 同编码） */
var _evalX = 0.0;
var _evalError = 0;                       /* 0=OK 1=除零 2=域错误 3=溢出 4=内部错 */
var _evalResult = 0.0;
var _evalSp = 0;

var _opStack = new Int32Array(64);        /* Shunting-yard 运算符栈（存完整 Token word） */
var _opSp = 0;

var _lhsToken = new Int32Array(128);
var _rhsToken = new Int32Array(128);
var _lhsCount = 0;
var _rhsCount = 0;
var _lhsResult = 0.0;
var _rhsResult = 0.0;

var _coeffs = new Float64Array(5);        /* 多项式系数 [常数项..最高次项] */

/* 运算符优先级（整数化 ×2，避开 2.5；+− = 2  ×÷ = 4  一元负号 = 5  ^ = 6）
 * 右结合：^ 与 T_UMINUS；其余左结合
 * 返回 0 表示该 Token 不是可比较优先级的运算符（LP / FUNC 等） */
function opPrec(word) {
  var t = word >> 8;
  var p = word & 255;
  if (t === T_OP) {
    if (p === OP_ADD || p === OP_SUB) { return 2; }
    if (p === OP_MUL || p === OP_DIV) { return 4; }
    return 6;                              /* OP_POW */
  }
  if (t === T_UMINUS) { return 5; }
  return 0;
}

/* 模块一：Shunting-yard 中缀转后缀
 * 读 srcPool[0..srcCount-1]，写 dstPool[0.._rpnLen-1]，返回 _rpnLen
 * 一元负号入栈时无条件压栈：保证 a^-b 不会把 ^ 弹走导致缺右操作数
 * 方程等号必须先由 evalEquationAt 分段后传入 */
function toRPN(srcPool, srcCount, dstPool) {
  _rpnLen = 0;
  _opSp = 0;
  var w = 0;
  var t = 0;
  var p = 0;
  var q = 0;
  var cur = 0;
  var i = 0;

  for (i = 0; i < srcCount; i++) {
    w = srcPool[i];
    t = w >> 8;
    p = w & 255;

    /* 操作数：直接输出 */
    if (t === T_NUM || t === T_VAR || t === T_CONST) {
      if (_rpnLen >= 256) { _evalError = 4; return _rpnLen; }
      dstPool[_rpnLen] = w;
      _rpnLen++;
      continue;
    }

    /* 函数 / 左括号 / 一元负号：无条件压栈 */
    if (t === T_FUNC || t === T_LP || t === T_UMINUS) {
      if (_opSp >= 64) { _evalError = 4; return _rpnLen; }
      _opStack[_opSp] = w;
      _opSp++;
      continue;
    }

    /* 右括号：弹到左括号；括号前若是函数则一并弹出 */
    if (t === T_RP) {
      while (_opSp > 0) {
        _opSp--;
        t = _opStack[_opSp] >> 8;
        if (t === T_LP) { break; }
        if (_rpnLen >= 256) { _evalError = 4; return _rpnLen; }
        dstPool[_rpnLen] = _opStack[_opSp];
        _rpnLen++;
      }
      if (t !== T_LP) { _evalError = 4; return _rpnLen; }   /* 无匹配左括号 */
      if (_opSp > 0 && (_opStack[_opSp - 1] >> 8) === T_FUNC) {
        _opSp--;
        if (_rpnLen >= 256) { _evalError = 4; return _rpnLen; }
        dstPool[_rpnLen] = _opStack[_opSp];
        _rpnLen++;
      }
      continue;
    }

    /* 逗号：弹到左括号（双参函数参数分隔，如 atan2） */
    if (t === T_COMMA) {
      while (_opSp > 0 && (_opStack[_opSp - 1] >> 8) !== T_LP) {
        _opSp--;
        if (_rpnLen >= 256) { _evalError = 4; return _rpnLen; }
        dstPool[_rpnLen] = _opStack[_opSp];
        _rpnLen++;
      }
      if (_opSp === 0) { _evalError = 4; return _rpnLen; }  /* 逗号不在函数括号内 */
      continue;
    }

    /* 二元运算符：按优先级弹栈后压栈 */
    if (t === T_OP) {
      cur = w;
      /* OP_POW 右结合，+−×÷ 左结合 */
      while (_opSp > 0) {
        w = _opStack[_opSp - 1];
        t = w >> 8;
        if (t === T_LP || t === T_FUNC) { break; }          /* 函数/括号屏障 */
        q = opPrec(w);
        if (q > opPrec(cur) || (q === opPrec(cur) && p !== OP_POW)) {
          _opSp--;
          if (_rpnLen >= 256) { _evalError = 4; return _rpnLen; }
          dstPool[_rpnLen] = w;
          _rpnLen++;
        } else {
          break;
        }
      }
      if (_opSp >= 64) { _evalError = 4; return _rpnLen; }
      _opStack[_opSp] = cur;
      _opSp++;
      continue;
    }

    /* 等号不该流到这里：必须先经 evalEquationAt 分段 */
    if (t === T_EQ) { _evalError = 4; return _rpnLen; }
  }

  /* 收尾：清空运算符栈；残余左括号 = 括号不匹配 */
  while (_opSp > 0) {
    _opSp--;
    if ((_opStack[_opSp] >> 8) === T_LP) { _evalError = 4; return _rpnLen; }
    if (_rpnLen >= 256) { _evalError = 4; return _rpnLen; }
    dstPool[_rpnLen] = _opStack[_opSp];
    _rpnLen++;
  }
  return _rpnLen;
}

/* 模块二：RPN 求值器
 * 读 rpnPool[0..rpnLen-1]，用 evalStack + _evalSp 栈式求值
 * 结果写 _evalResult 并返回；任何错误置 _evalError 后立即返回 NaN */
function evalRPN(rpnPool, rpnLen) {
  _evalSp = 0;
  var w = 0;
  var t = 0;
  var p = 0;
  var a = 0.0;
  var b = 0.0;
  var r = 0.0;
  var i = 0;

  for (i = 0; i < rpnLen; i++) {
    w = rpnPool[i];
    t = w >> 8;
    p = w & 255;

    /* 压栈类操作数 */
    if (t === T_NUM) {
      if (_evalSp >= 64) { _evalError = 3; return NaN; }    /* 栈溢出 */
      evalStack[_evalSp] = numberPool[p];
      _evalSp++;
      continue;
    }
    if (t === T_VAR) {
      if (_evalSp >= 64) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = _evalX;
      _evalSp++;
      continue;
    }
    if (t === T_CONST) {
      if (_evalSp >= 64) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = (p === C_PI) ? Math.PI : Math.E;
      _evalSp++;
      continue;
    }

    /* 一元负号 */
    if (t === T_UMINUS) {
      if (_evalSp < 1) { _evalError = 4; return NaN; }      /* 栈下溢 */
      evalStack[_evalSp - 1] = -evalStack[_evalSp - 1];     /* 原地取负，栈深不变 */
      continue;
    }

    /* 双参函数 atan2(y, x)：先弹 x 后弹 y */
    if (t === T_FUNC && p === F_ATAN2) {
      if (_evalSp < 2) { _evalError = 4; return NaN; }
      b = evalStack[--_evalSp];                              /* x */
      a = evalStack[--_evalSp];                              /* y */
      r = applyFunction(F_ATAN2, a, b);
      if (r !== r) { _evalError = 2; return NaN; }           /* 域错误 */
      if (_evalSp >= 64) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = r;
      _evalSp++;
      continue;
    }

    /* 单参函数 */
    if (t === T_FUNC) {
      if (_evalSp < 1) { _evalError = 4; return NaN; }
      a = evalStack[--_evalSp];
      r = applyFunction(p, a, 0);
      if (r !== r && a === a) { _evalError = 2; return NaN; }   /* 域错误，如 sqrt(-1) ln(-1) */
      if (r === Infinity || r === -Infinity) { _evalError = 3; return NaN; }  /* 溢出 */
      if (_evalSp >= 64) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = r;
      _evalSp++;
      continue;
    }

    /* 二元运算符：右操作数先弹 */
    if (t === T_OP) {
      if (_evalSp < 2) { _evalError = 4; return NaN; }
      b = evalStack[--_evalSp];
      a = evalStack[--_evalSp];

      if (p === OP_ADD) { r = a + b; }
      else if (p === OP_SUB) { r = a - b; }
      else if (p === OP_MUL) { r = a * b; }
      else if (p === OP_DIV) {
        if (b === 0) { _evalError = 1; return NaN; }         /* 除零 */
        r = a / b;
      } else {                                               /* OP_POW */
        if (a === 0 && b < 0) { _evalError = 2; return NaN; } /* 0^负数 = 域错误 */
        r = Math.pow(a, b);
      }
      if (r === Infinity || r === -Infinity) { _evalError = 3; return NaN; }  /* 溢出 */
      if (_evalSp >= 64) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = r;
      _evalSp++;
      continue;
    }
  }

  /* 收尾：栈上必须恰好剩一个结果 */
  if (_evalSp !== 1) { _evalError = 4; return NaN; }
  _evalResult = evalStack[0];
  return _evalResult;
}

/* 模块三：求值入口 */

/* 单表达式入口：tokenPool / tokenCount 已由 parseExpression 填充（无等号） */
function evalExpressionAt(x) {
  _evalX = x;
  _evalError = 0;
  _rpnLen = toRPN(tokenPool, tokenCount, _rpnToken);
  if (_evalError !== 0) { _evalResult = NaN; return NaN; }
  return evalRPN(_rpnToken, _rpnLen);
}

/* 方程入口：f(x)=g(x) → f(x)-g(x)；无等号时退化为 evalExpressionAt */
function evalEquationAt(x) {
  _evalX = x;
  _evalError = 0;
  var eqPos = -1;
  var i = 0;

  /* 定位等号（内存策略：扫描在原 tokenPool 上进行，不复制） */
  for (i = 0; i < tokenCount; i++) {
    if ((tokenPool[i] >> 8) === T_EQ) { eqPos = i; break; }
  }

  if (eqPos < 0) { return evalExpressionAt(x); }

  /* 等号两段拷入预分配区（内存策略：零新分配，Int32Array 定长拷贝） */
  _lhsCount = eqPos;
  _rhsCount = tokenCount - eqPos - 1;
  if (_lhsCount === 0 || _rhsCount === 0) { _evalError = 4; _evalResult = NaN; return NaN; }
  for (i = 0; i < _lhsCount; i++) { _lhsToken[i] = tokenPool[i]; }
  for (i = 0; i < _rhsCount; i++) { _rhsToken[i] = tokenPool[eqPos + 1 + i]; }

  _rpnLen = toRPN(_lhsToken, _lhsCount, _rpnToken);
  if (_evalError !== 0) { _evalResult = NaN; return NaN; }
  _lhsResult = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _evalResult = NaN; return NaN; }

  _rpnLen = toRPN(_rhsToken, _rhsCount, _rpnToken);
  if (_evalError !== 0) { _evalResult = NaN; return NaN; }
  _rhsResult = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _evalResult = NaN; return NaN; }

  _evalResult = _lhsResult - _rhsResult;
  return _evalResult;
}

/* 模块四：多项式系数提取（数值采样 + 高斯消元）
 * 在 maxDegree+1 个采样点上求值当前方程 f(x)=0（f(x)=g(x) 已转差），
 * 解范德蒙德方程组得幂基系数，写入 _coeffs[0..maxDegree]（常数项→最高次）
 * 内存策略：增广矩阵复用 matrixPool 前 30 元素（5x6，行主序，步长 maxDegree+2）
 * 返回 0 = 成功；否则返回错误码且 _coeffs 已清零 */
function extractCoefficients(maxDegree) {
  var n = maxDegree + 1;               /* 采样点数 = 方程组元数 */
  var stride = n + 1;                  /* 增广矩阵行宽 */
  var i = 0;
  var j = 0;
  var k = 0;
  var xk = 0.0;
  var pw = 0.0;
  var base = 0;
  var y = 0.0;

  for (i = 0; i < 5; i++) { _coeffs[i] = 0.0; }
  if (maxDegree < 1 || maxDegree > 4) { _evalError = 4; return _evalError; }

  /* 采样并构造增广矩阵：第 k 行 = [1, xk, xk², .., xkⁿ⁻¹ | y_k]
   * 幂用循环累乘（内存策略：仅标量累加器，避免 Math.pow 调用开销） */
  for (k = 0; k < n; k++) {
    xk = k - maxDegree / 2;            /* 小整数/半整数采样点，如 4 次取 −2..2 */
    base = k * stride;
    pw = 1.0;
    for (j = 0; j < n; j++) {
      matrixPool[base + j] = pw;
      pw = pw * xk;
    }
    y = evalEquationAt(xk);
    if (_evalError !== 0) {
      for (i = 0; i < 5; i++) { _coeffs[i] = 0.0; }
      return _evalError;
    }
    matrixPool[base + maxDegree + 1] = y;
  }

  /* 高斯消元（列主元部分选主，显式循环，零递归） */
  var col = 0;
  var row = 0;
  var piv = 0;
  var maxAbs = 0.0;
  var v = 0.0;
  var f = 0.0;
  var c = 0;

  for (col = 0; col < n; col++) {
    piv = col;
    maxAbs = matrixPool[col * stride + col];
    if (maxAbs < 0.0) { maxAbs = -maxAbs; }
    for (row = col + 1; row < n; row++) {
      v = matrixPool[row * stride + col];
      if (v < 0.0) { v = -v; }
      if (v > maxAbs) { maxAbs = v; piv = row; }
    }
    if (maxAbs < 1e-12) {              /* 奇异：方程含非多项式成分或次数超限 */
      _evalError = 4;
      return _evalError;
    }
    if (piv !== col) {                 
      for (c = 0; c < stride; c++) {
        v = matrixPool[col * stride + c];
        matrixPool[col * stride + c] = matrixPool[piv * stride + c];
        matrixPool[piv * stride + c] = v;
      }
    }
    for (row = col + 1; row < n; row++) {
      f = matrixPool[row * stride + col] / matrixPool[col * stride + col];
      if (f !== 0.0) {
        for (c = col; c < stride; c++) {
          matrixPool[row * stride + c] = matrixPool[row * stride + c] - f * matrixPool[col * stride + c];
        }
      }
    }
  }

  /* 回代（内存策略：结果直接落 _coeffs，不建临时数组） */
  var s = 0.0;
  for (i = n - 1; i >= 0; i--) {
    s = matrixPool[i * stride + n];
    for (j = i + 1; j < n; j++) {
      s = s - matrixPool[i * stride + j] * _coeffs[j];
    }
    _coeffs[i] = s / matrixPool[i * stride + i];
  }

  _evalError = 0;
  return 0;
}

/* 模块五：复位 — 求解器每轮求解前调用（只清标量与预分配数组，零分配） */
function evaluatorReset() {
  _rpnLen = 0;
  _evalSp = 0;
  _opSp = 0;
  _evalError = 0;
  _evalX = 0.0;
  _evalResult = 0.0;
  _lhsCount = 0;
  _rhsCount = 0;
  _lhsResult = 0.0;
  _rhsResult = 0.0;
  _coeffs[0] = 0.0;
  _coeffs[1] = 0.0;
  _coeffs[2] = 0.0;
  _coeffs[3] = 0.0;
  _coeffs[4] = 0.0;
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · core_solvers.js — 方程求解器（文件 6/6，内核最终层）
 * Lite Wearable / JerryScript / 堆上限 64KB / Zero-GC
 * 职责：二次/三次/四次多项式求根、线性方程组高斯消元、残差回验、
 *       OOM 安全防护（深度/函数数/变量数/次数/多元非线性/内存水位）
 * 依赖：文件 1 tokenPool/_coeffs/matrixPool 等全局池
 *       文件 3 cSqrt（双二次路径复数开方）
 *       文件 4 exactFromDiscriminant / exactSetFraction / exactSimplifyFraction
 *       文件 5 evalEquationAt（残差回验）
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象/数组字面量
 *       全部中间量为函数内 var 标量，求解过程不产生任何临时 JS 堆对象 */

/* 文件 6 专属全局变量（唯一声明处） */
var _rootCount = 0;
var _rootRe = new Float64Array(6);        /* 6 槽 = solveLinearSystem 的 n 上限 */
var _rootIm = new Float64Array(6);
var _rootExactNum = new Float64Array(6);
var _rootExactDen = new Float64Array(6);  /* 恒为正 */
var _rootExactRad = new Float64Array(6);  /* 无平方因子 */
var _rootExactValid = new Int32Array(6);  /* 1=支持 0=不支持 */
var _solveError = 0;                      /* 0=OK 1=次数过高 2=多元非线性拒绝 3=求解失败 4=内存水位过高 */
var _residualMax = 0.0;                   /* 残差回验最大相对误差（UI 层决定是否告警） */

/* 精确系统分量上限（2^53-1，与文件 4 保持一致） */
var SOLVER_INT_MAX = 9007199254740991;

/* 内联助手：带符号立方根
 * Math.cbrt 在 JerryScript 上不保证可用，故用 pow(1/3) 手工实现
 * 零分配：纯标量 */
function cbrtSigned(x) {
  if (x === 0.0) { return 0.0; }
  if (x > 0.0) { return Math.pow(x, 1.0 / 3.0); }
  return -Math.pow(-x, 1.0 / 3.0);
}

/* 模块一：二次方程求解器
 * 输入 _coeffs[0..2] = [c, b, a]，即 ax² + bx + c = 0
 * D ≥ 0：两实根，精确形式经 exactFromDiscriminant(D) 组装
 * D < 0：共轭复根，精确形式置无效
 * 退化 a=0：降为线性（单根，整数系数时保留精确分数）
 * 返回 _solveError（0 = 成功） */
function solveQuadratic() {
  _rootCount = 0;
  _solveError = 0;

  var c = _coeffs[0];
  var b = _coeffs[1];
  var a = _coeffs[2];
  if (!isFinite(a) || !isFinite(b) || !isFinite(c)) { _solveError = 3; return _solveError; }

  /* 退化：a = 0 → 线性方程 bx + c = 0 */
  if (a === 0.0) {
    if (b === 0.0) {
      if (c === 0.0) { _solveError = 3; return _solveError; }  /* 恒等式：无穷多根无法枚举 */
      _rootCount = 0;                                          /* 矛盾式：无根，非错误 */
      return 0;
    }
    _rootRe[0] = -c / b;
    _rootIm[0] = 0.0;
    _rootCount = 1;
    /* 内存策略：精确形式仅整数系数可组装，复用文件 4 全局 exactNum/exactDen/exactRad */
    if (b === Math.floor(b) && c === Math.floor(c) &&
        b <= SOLVER_INT_MAX && b >= -SOLVER_INT_MAX &&
        c <= SOLVER_INT_MAX && c >= -SOLVER_INT_MAX) {
      exactSetFraction(-c, b);
    } else {
      exactInvalidate();
    }
    _rootExactValid[0] = exactIsValid ? 1 : 0;
    _rootExactNum[0] = exactNum;
    _rootExactDen[0] = exactDen;
    _rootExactRad[0] = exactRad;
    return 0;
  }

  var D = b * b - 4.0 * a * c;
  if (!isFinite(D)) { _solveError = 3; return _solveError; }
  var twoA = 2.0 * a;

  if (D >= 0.0) {
    /* 两实根：x = (-b ± √D) / 2a */
    var sq = Math.sqrt(D);
    _rootRe[0] = (-b + sq) / twoA;
    _rootRe[1] = (-b - sq) / twoA;
    _rootIm[0] = 0.0;
    _rootIm[1] = 0.0;
    _rootCount = 2;

    /* 精确形式组装
     * √D = (n/d)·√r（d>0，来自 exactFromDiscriminant）
     * 内存策略：r=1（D 为有理数平方）→ 纯分数 (−b·d ± n)/(2a·d)；
     *          b=0 → 纯根式 (±n)/(2a·d)·√r；
     *          其余（有理部分+根式混合，如 (1+√5)/2）三分量模型无法
     *          表达 → 置无效，保留近似值 */
    exactFromDiscriminant(D);
    if (!exactIsValid) {
      _rootExactValid[0] = 0;
      _rootExactValid[1] = 0;
    } else {
      var n = exactNum;
      var d = exactDen;
      var rad = exactRad;
      if (rad === 1) {
        if (a === Math.floor(a) && b === Math.floor(b) &&
            a <= SOLVER_INT_MAX && a >= -SOLVER_INT_MAX &&
            b <= SOLVER_INT_MAX && b >= -SOLVER_INT_MAX &&
            Math.abs(b) <= SOLVER_INT_MAX / d &&
            Math.abs(a) <= SOLVER_INT_MAX / (2.0 * d)) {
          var base = -b * d;                     /* |b·d| ≤ 2^53-1，精确 */
          var den = 2.0 * a * d;                 /* |2a·d| ≤ 2^53-1，精确 */
          var ok0 = Math.abs(base) <= SOLVER_INT_MAX - (n < 0 ? -n : n);
          if (ok0) {
            /* 根 0：(base + n)/den，根 1：(base − n)/den，逐个借全局约分 */
            exactNum = base + n;
            exactDen = den;
            exactRad = 1;
            exactSimplifyFraction();
            exactUpdateApprox();
            _rootExactNum[0] = exactNum;
            _rootExactDen[0] = exactDen;
            _rootExactRad[0] = exactRad;
            _rootExactValid[0] = exactIsValid ? 1 : 0;
            exactNum = base - n;
            exactDen = den;
            exactRad = 1;
            exactSimplifyFraction();
            exactUpdateApprox();
            _rootExactNum[1] = exactNum;
            _rootExactDen[1] = exactDen;
            _rootExactRad[1] = exactRad;
            _rootExactValid[1] = exactIsValid ? 1 : 0;
          } else {
            _rootExactValid[0] = 0;
            _rootExactValid[1] = 0;
          }
        } else {
          _rootExactValid[0] = 0;
          _rootExactValid[1] = 0;
        }
      } else if (b === 0.0) {
        if (a === Math.floor(a) && a <= SOLVER_INT_MAX && a >= -SOLVER_INT_MAX &&
            Math.abs(a) <= SOLVER_INT_MAX / (2.0 * d)) {
          var den2 = 2.0 * a * d;                /* 分母归正：符号翻进分子 */
          var sgn = 1;
          if (den2 < 0.0) { sgn = -1; den2 = -den2; }
          exactNum = sgn * n;
          exactDen = den2;
          exactRad = rad;
          exactSimplifyFraction();
          exactUpdateApprox();
          _rootExactNum[0] = exactNum;
          _rootExactDen[0] = exactDen;
          _rootExactRad[0] = exactRad;
          _rootExactValid[0] = exactIsValid ? 1 : 0;
          _rootExactNum[1] = -exactNum;          /* 共轭：仅分子变号 */
          _rootExactDen[1] = exactDen;
          _rootExactRad[1] = exactRad;
          _rootExactValid[1] = _rootExactValid[0];
        } else {
          _rootExactValid[0] = 0;
          _rootExactValid[1] = 0;
        }
      } else {
        _rootExactValid[0] = 0;                  /* 有理+根式混合：三Fields 模型不可表达 */
        _rootExactValid[1] = 0;
      }
    }
  } else {
    /* 共轭复根：x = (-b ± i√|D|) / 2a */
    var sqI = Math.sqrt(-D);
    var reC = -b / twoA;
    var imC = sqI / twoA;
    _rootRe[0] = reC;
    _rootIm[0] = imC;
    _rootRe[1] = reC;
    _rootIm[1] = -imC;
    _rootCount = 2;
    _rootExactValid[0] = 0;
    _rootExactValid[1] = 0;
  }
  return 0;
}

/* 模块二：三次方程求解器（卡尔达诺 + 三角切换）
 * 输入 _coeffs[0..3] = [d, c, b, a]，即 ax³ + bx² + cx + d = 0
 * 消去二次项：x = y − b/(3a) → y³ + py + q = 0
 * Δ > 0：一实根 + 共轭复对（卡尔达诺，全实标量路径——Δ>0 时 u³/v³ 皆为实数，
 *        不产生复数中间量，cSqrt/cPowInt 在本路径无调用点）
 * Δ = 0：三实根（含重根）
 * Δ < 0：不可约情形，必须切换三角法（卡尔达诺会因复数中间量丢失精度）
 * 三次方程不支持精确形式，_rootExactValid 全 0 */
function solveCubic() {
  _rootCount = 0;
  _solveError = 0;

  var d = _coeffs[0];
  var c = _coeffs[1];
  var b = _coeffs[2];
  var a = _coeffs[3];
  if (!isFinite(a) || !isFinite(b) || !isFinite(c) || !isFinite(d)) { _solveError = 3; return _solveError; }

  /* 退化 a = 0 → 降次为二次（_coeffs[0..2] 恰为二次布局，直接委托） */
  if (a === 0.0) { return solveQuadratic(); }

  var a2 = a * a;
  var a3 = a2 * a;
  var p = (3.0 * a * c - b * b) / (3.0 * a2);
  var q = (2.0 * b * b * b - 9.0 * a * b * c + 27.0 * a2 * d) / (27.0 * a3);
  if (!isFinite(p) || !isFinite(q)) { _solveError = 3; return _solveError; }
  var off = -b / (3.0 * a);                /* x = y + off */

  var qh = q * 0.5;
  var p3 = p / 3.0;
  var p3c = p3 * p3 * p3;
  var dlt = qh * qh + p3c;
  if (!isFinite(dlt)) { _solveError = 3; return _solveError; }

  /* Δ 零判定容差（相对量级），避免浮点抖动把重根误判为三根/一根 */
  var tolD = 1e-14 * (Math.abs(qh * qh) + Math.abs(p3c));

  if (dlt > tolD) {
    /* Δ > 0：卡尔达诺
     * 内存策略：u/v 为带符号实立方根标量，无复数对象 */
    var sD = Math.sqrt(dlt);
    var u = cbrtSigned(-qh + sD);
    var v = cbrtSigned(-qh - sD);
    _rootRe[0] = u + v + off;
    _rootIm[0] = 0.0;
    var re2 = -(u + v) * 0.5 + off;
    var imS = 0.8660254037844386 * (u - v);          /* √3/2 */
    _rootRe[1] = re2;
    _rootIm[1] = imS;
    _rootRe[2] = re2;
    _rootIm[2] = -imS;
    _rootCount = 3;
  } else if (dlt >= -tolD) {
    /* Δ = 0：三实根（y1 = 2∛(−q/2) 单根，y2 = y3 = −∛(−q/2) 二重根） */
    var t0 = cbrtSigned(-qh);
    _rootRe[0] = 2.0 * t0 + off;
    _rootRe[1] = -t0 + off;
    _rootRe[2] = -t0 + off;
    _rootIm[0] = 0.0;
    _rootIm[1] = 0.0;
    _rootIm[2] = 0.0;
    _rootCount = 3;
  } else {
    /* Δ < 0 不可约：三角法
     * y_k = 2√(−p/3)·cos(θ − 2πk/3)，θ = acos((3q)/(2p)·√(−3/p))/3
     * 内存策略：纯标量，acos 入参钳位到 [−1,1] 防浮点越界 */
    var mm = 2.0 * Math.sqrt(-p3);
    var arg = (3.0 * q) / (2.0 * p) * Math.sqrt(-3.0 / p);
    if (arg > 1.0) { arg = 1.0; }
    else if (arg < -1.0) { arg = -1.0; }
    var th = Math.acos(arg) / 3.0;
    var TWO_PI_3 = 2.0943951023931953;               /* 2π/3 */
    _rootRe[0] = mm * Math.cos(th) + off;
    _rootRe[1] = mm * Math.cos(th - TWO_PI_3) + off;
    _rootRe[2] = mm * Math.cos(th - 2.0 * TWO_PI_3) + off;
    _rootIm[0] = 0.0;
    _rootIm[1] = 0.0;
    _rootIm[2] = 0.0;
    _rootCount = 3;
  }

  /* 三次方程不支持精确形式 */
  _rootExactValid[0] = 0;
  _rootExactValid[1] = 0;
  _rootExactValid[2] = 0;
  return 0;
}

/* 模块三：四次方程求解器（笛卡尔分解法）
 * 输入 _coeffs[0..4] = [e, d, c, b, a]，即 ax⁴ + bx³ + cx² + dx + e = 0
 * 消去三次项：x = y − b/(4a) → y⁴ + py² + qy + r = 0
 * q ≈ 0 退化：双二次 z² + pz + r = 0（z = y²），逐 z 用 cSqrt 开方
 * 一般情形：预解三次 8m³ + 8pm² + (2p²−8r)m − q² = 0（系数已推导核验：
 *   设 (y²+αy+β)(y²−αy+γ)，比对系数得 β+γ=p+2m、α(γ−β)=q、βγ=r，
 *   消去 βγ 得 2m(p+2m)²−q²=8mr，展开即上式），取最大实根 m（f(0)=−q²≤0
 *   保证存在非负实根），α=√(2m)，β=m+p/2−q/(2α)，γ=m+p/2+q/(2α)，
 *   两个二次因式的 4 个 y 根合成后平移回 x
 * 注意：预解/因式二次复用 _coeffs 传参（原系数已缓存为局部标量），
 *       求解完成后 _coeffs 不再是原四次系数
 * 四次方程不支持精确形式，_rootExactValid 全 0 */
function solveQuartic() {
  _rootCount = 0;
  _solveError = 0;

  var e = _coeffs[0];
  var d = _coeffs[1];
  var c = _coeffs[2];
  var b = _coeffs[3];
  var a = _coeffs[4];
  if (!isFinite(a) || !isFinite(b) || !isFinite(c) || !isFinite(d) || !isFinite(e)) {
    _solveError = 3; return _solveError;
  }

  /* 退化 a = 0 → 降次为三次 */
  if (a === 0.0) { return solveCubic(); }

  var a2 = a * a;
  var a3 = a2 * a;
  var a4 = a2 * a2;
  var b2 = b * b;
  var p = (8.0 * a * c - 3.0 * b2) / (8.0 * a2);
  var q = (b * b2 - 4.0 * a * b * c + 8.0 * a2 * d) / (8.0 * a3);
  var r = (-3.0 * b * b2 * b + 256.0 * a3 * e - 64.0 * a2 * b * d + 16.0 * a * b2 * c) / (256.0 * a4);
  if (!isFinite(p) || !isFinite(q) || !isFinite(r)) { _solveError = 3; return _solveError; }
  var off = -b / (4.0 * a);                /* x = y + off */

  var qTol = 1e-12 * (1.0 + Math.abs(p) + Math.abs(r));
  var i = 0;

  if (Math.abs(q) < qTol) {
    /* 双二次退化路径：z² + pz + r = 0，z = y²
     * 内存策略：z 根先缓存为局部标量再展开写回，避免读写区重叠；
     * 每个 z 用 cSqrt 开方（负 z 自动落到正虚部分支），正负两根就地取反 */
    _coeffs[0] = r;
    _coeffs[1] = p;
    _coeffs[2] = 1.0;
    solveQuadratic();
    if (_solveError !== 0) { return _solveError; }
    var z0Re = _rootRe[0];
    var z0Im = _rootIm[0];
    var z1Re = _rootRe[1];
    var z1Im = _rootIm[1];
    _aRe = z0Re;
    _aIm = z0Im;
    cSqrt();
    _rootRe[0] = _tmpRe;
    _rootIm[0] = _tmpIm;
    _rootRe[1] = -_tmpRe;
    _rootIm[1] = -_tmpIm;
    _aRe = z1Re;
    _aIm = z1Im;
    cSqrt();
    _rootRe[2] = _tmpRe;
    _rootIm[2] = _tmpIm;
    _rootRe[3] = -_tmpRe;
    _rootIm[3] = -_tmpIm;
    _rootCount = 4;
  } else {
    /* 一般路径：预解三次 → 待定系数分解
     * 内存策略：m/α/β/γ 全为标量；两因式根用 _rootRe[0..3] 就地接力
     * （因式1 根暂存 [2..3]，因式2 根落 [0..1]），零临时数组 */
    _coeffs[0] = -q * q / 8.0;
    _coeffs[1] = p * p * 0.25 - r;
    _coeffs[2] = p;
    _coeffs[3] = 1.0;
    solveCubic();
    if (_solveError !== 0) { return _solveError; }

    var m = -1e308;
    var found = 0;
    for (i = 0; i < _rootCount; i++) {
      if (_rootIm[i] === 0.0 && _rootRe[i] > m) { m = _rootRe[i]; found = 1; }
    }
    if (found === 0 || m <= 0.0 || !isFinite(m)) {
      _solveError = 3;
      _rootCount = 0;
      return _solveError;
    }

    var alpha = Math.sqrt(2.0 * m);
    var halfP = m + p * 0.5;
    var beta = halfP - q / (2.0 * alpha);
    var gamma = halfP + q / (2.0 * alpha);

    /* 因式 1：y² + αy + β = 0 → 根暂存 _rootRe[2..3] */
    _coeffs[0] = beta;
    _coeffs[1] = alpha;
    _coeffs[2] = 1.0;
    solveQuadratic();
    if (_solveError !== 0) { return _solveError; }
    _rootRe[2] = _rootRe[0];
    _rootIm[2] = _rootIm[0];
    _rootRe[3] = _rootRe[1];
    _rootIm[3] = _rootIm[1];

    /* 因式 2：y² − αy + γ = 0 → 根落 _rootRe[0..1] */
    _coeffs[0] = gamma;
    _coeffs[1] = -alpha;
    _coeffs[2] = 1.0;
    solveQuadratic();
    if (_solveError !== 0) { return _solveError; }
    _rootCount = 4;
  }

  /* 平移回 x = y + off；四次方程不支持精确形式 */
  for (i = 0; i < 4; i++) {
    _rootRe[i] = _rootRe[i] + off;
    _rootExactValid[i] = 0;
  }
  return 0;
}

/* 模块四：线性方程组求解器（列主元高斯消元）
 * 输入 matrixPool 前 n·(n+1) 个元素：增广矩阵（行主序，1 ≤ n ≤ 6）
 * 列主元选取 → 行交换 → 消元 → 回代，结果写 _rootRe[0..n-1]
 * 主元 |·| < 1e-12 → 奇异，_solveError = 3
 * 内存策略：原地作用于 matrixPool，零复制零分配 */
function solveLinearSystem(n) {
  _rootCount = 0;
  _solveError = 0;
  if (n < 1 || n > 6) { _solveError = 3; return _solveError; }

  var stride = n + 1;
  var col = 0;
  var row = 0;
  var k = 0;
  var piv = 0;
  var maxAbs = 0.0;
  var v = 0.0;
  var f = 0.0;

  for (col = 0; col < n; col++) {
    /* 列主元：当前列 |·| 最大的行换到对角位 */
    piv = col;
    maxAbs = matrixPool[col * stride + col];
    if (maxAbs < 0.0) { maxAbs = -maxAbs; }
    for (row = col + 1; row < n; row++) {
      v = matrixPool[row * stride + col];
      if (v < 0.0) { v = -v; }
      if (v > maxAbs) { maxAbs = v; piv = row; }
    }
    if (maxAbs < 1e-12) {
      _solveError = 3;                       /* 奇异矩阵 */
      _rootCount = 0;
      return _solveError;
    }
    if (piv !== col) {
      for (k = 0; k < stride; k++) {
        v = matrixPool[col * stride + k];
        matrixPool[col * stride + k] = matrixPool[piv * stride + k];
        matrixPool[piv * stride + k] = v;
      }
    }
    for (row = col + 1; row < n; row++) {
      f = matrixPool[row * stride + col] / matrixPool[col * stride + col];
      if (f !== 0.0) {
        for (k = col; k < stride; k++) {
          matrixPool[row * stride + k] = matrixPool[row * stride + k] - f * matrixPool[col * stride + k];
        }
      }
    }
  }

  /* 回代：解直接落 _rootRe，不建临时数组 */
  var s = 0.0;
  for (row = n - 1; row >= 0; row--) {
    s = matrixPool[row * stride + n];
    for (k = row + 1; k < n; k++) {
      s = s - matrixPool[row * stride + k] * _rootRe[k];
    }
    _rootRe[row] = s / matrixPool[row * stride + row];
  }
  for (row = 0; row < n; row++) {
    _rootIm[row] = 0.0;
    _rootExactValid[row] = 0;
  }
  _rootCount = n;
  return 0;
}

/* 模块五：残差回验
 * 对每个实根（_rootIm[i] === 0）代回原方程 f(x)（evalEquationAt，基于 tokenPool）
 * 相对残差 = |f(root)| / max(|f(root−δ)|, |f(root+δ)|, 1e-300)，
 * δ = |root| > 10 时取 0.1·|root| 否则 1 —— 邻域值自然反映系数整体量级，
 * 采样拟合失真（如 sin(x)=0 被拟合成二次）时邻域值 O(1) 而残差 O(1)，比值近 1；
 * 真 根时残差为浮点噪声，比值 ~1e-12
 * 只记录 _residualMax 不拦截，UI 层据 > 1e-6 决定是否提示"结果可能不准确" */
function verifyRoots() {
  _residualMax = 0.0;
  var i = 0;
  var x = 0.0;
  var f0 = 0.0;
  var f1 = 0.0;
  var f2 = 0.0;
  var del = 1.0;
  var scale = 0.0;
  var rel = 0.0;
  var ax = 0.0;

  for (i = 0; i < _rootCount; i++) {
    if (_rootIm[i] !== 0.0) { continue; }
    x = _rootRe[i];
    f0 = evalEquationAt(x);
    if (_evalError !== 0 || !isFinite(f0)) {
      rel = 1.0;                             /* 求值失败：视作不可信 */
    } else {
      ax = x < 0.0 ? -x : x;
      del = ax > 10.0 ? 0.1 * ax : 1.0;
      f1 = evalEquationAt(x - del);
      f2 = evalEquationAt(x + del);
      scale = f1 < 0.0 ? -f1 : f1;
      if (f2 > scale) { scale = f2; }
      else if (-f2 > scale) { scale = -f2; }
      if (scale < 1e-300) { scale = 1e-300; }
      rel = f0 < 0.0 ? -f0 : f0;
      if (rel < 1e-300) { rel = 0.0; }
      else { rel = rel / scale; }
    }
    if (rel > _residualMax) { _residualMax = rel; }
  }
}

/* 模块六：OOM 安全防护与异常拦截
 * 求解前置门禁：全部只读扫描 tokenPool，零分配 */

/* Token 流体检：括号嵌套 ≤ 3、函数调用 ≤ 2、变量出现 ≤ 5
 * 返回 0 = 通过；1 = 拒绝（_solveError = 2） */
function preCheckExpression() {
  _solveError = 0;
  var depth = 0;
  var funcCount = 0;
  var varCount = 0;
  var i = 0;
  var w = 0;
  var t = 0;

  for (i = 0; i < tokenCount; i++) {
    w = tokenPool[i];
    t = w >> 8;
    if (t === T_LP) {
      depth++;
      if (depth > 3) { _solveError = 2; return 1; }
    } else if (t === T_RP) {
      depth--;
    } else if (t === T_FUNC) {
      funcCount++;
      if (funcCount > 2) { _solveError = 2; return 1; }
    } else if (t === T_VAR) {
      varCount++;
      if (varCount > 5) { _solveError = 2; return 1; }
    }
  }
  return 0;
}

/* 次数门禁：degree > 4 拒绝（阿贝尔-鲁菲尼定理，五次及以上无根式解）
 * 返回 0 = 通过；1 = 拒绝（_solveError = 1） */
function guardDegree(degree) {
  if (degree > 4) { _solveError = 1; return 1; }
  return 0;
}

/* 多元非线性门禁：≥ 2 个不同变量且含非线性运算（^ 或函数调用）即拒绝
 * 变量区分按 T_VAR payload（当前解析器仅 x=0，接口为多变量扩展预留）
 * 线性多元组（如 x+y=3）不拦截，交 solveLinearSystem
 * 返回 0 = 通过；1 = 拒绝（_solveError = 2）
 * 内存策略：已见变量 payload 用 6 个标量寄存，不用数组 */
function guardMultivariateNonlinear() {
  _solveError = 0;
  var seen0 = -1;
  var seen1 = -1;
  var seen2 = -1;
  var seen3 = -1;
  var seen4 = -1;
  var seen5 = -1;
  var distinct = 0;
  var nonlinear = 0;
  var i = 0;
  var w = 0;
  var t = 0;
  var p = 0;

  for (i = 0; i < tokenCount; i++) {
    w = tokenPool[i];
    t = w >> 8;
    p = w & 255;
    if (t === T_VAR) {
      if (p !== seen0 && p !== seen1 && p !== seen2 && p !== seen3 && p !== seen4 && p !== seen5) {
        if (seen0 < 0) { seen0 = p; }
        else if (seen1 < 0) { seen1 = p; }
        else if (seen2 < 0) { seen2 = p; }
        else if (seen3 < 0) { seen3 = p; }
        else if (seen4 < 0) { seen4 = p; }
        else if (seen5 < 0) { seen5 = p; }
        distinct++;
      }
    } else if (t === T_OP && p === OP_POW) {
      nonlinear = 1;
    } else if (t === T_FUNC) {
      nonlinear = 1;
    }
  }
  if (distinct >= 2 && nonlinear === 1) { _solveError = 2; return 1; }
  return 0;
}

/* 内存水位门禁：堆使用 > 48KB（64KB × 75%）拒绝求解
 * 优先平台 Heap API（performance.memory.usedJSHeapSize），
 * 不可用时降级为静态池 byteLength + 动态游标估算（含 2KB 运行时冗余）
 * 返回 0 = 通过；1 = 拒绝（_solveError = 4） */
function checkMemoryWatermark() {
  _solveError = 0;
  var used = 0;

  if (typeof performance !== "undefined") {
    if (performance.memory && performance.memory.usedJSHeapSize) {
      used = performance.memory.usedJSHeapSize;
    }
  }
  if (used === 0) {
    /* 降级估算：全部预分配 TypedArray 之 byteLength + 已用游标 + 冗余 */
    used = tokenPool.byteLength + numberPool.byteLength + evalStack.byteLength +
           matrixPool.byteLength +
           _rpnToken.byteLength + _opStack.byteLength +
           _lhsToken.byteLength + _rhsToken.byteLength + _coeffs.byteLength +
           _rootRe.byteLength + _rootIm.byteLength +
           _rootExactNum.byteLength + _rootExactDen.byteLength +
           _rootExactRad.byteLength + _rootExactValid.byteLength +
           (tokenCount << 3) + (numberCount << 3) + 2048;
  }
  if (used > 49152) { _solveError = 4; return 1; }
  return 0;
}

/* 模块七：复位
 * 每轮求解前调用（内存策略：只清标量与预分配数组，零分配） */
function solversReset() {
  _rootCount = 0;
  var i = 0;
  for (i = 0; i < 6; i++) {
    _rootRe[i] = 0.0;
    _rootIm[i] = 0.0;
    _rootExactNum[i] = 0.0;
    _rootExactDen[i] = 1.0;
    _rootExactRad[i] = 1.0;
    _rootExactValid[i] = 0;
  }
  _solveError = 0;
  _residualMax = 0.0;
}

/* ============================================================
 * ApolloCalc · core_kernel.js — 内核统一入口
 * 由 _build_kernel.js 自动拼接，禁止手动编辑
 * 拼接顺序：parser → math_functions → complex → exact → evaluator → solvers
 * ============================================================ */

/* 供 index.js 引用的内核接口（UI 层唯一访问点） */
export default {
  /* ---- 解析层 ---- */
  parseExpression: parseExpression,
  dumpTokenStream: dumpTokenStream,

  /* ---- 数学执行层 ---- */
  applyFunction: applyFunction,

  /* ---- 精确数值系统 ---- */
  exactReset: exactReset,
  exactIsValid: function () { return exactIsValid; },
  getExactNum: function () { return exactNum; },
  getExactDen: function () { return exactDen; },
  getExactRad: function () { return exactRad; },
  getApproxValue: function () { return approxValue; },

  /* ---- 求值层 ---- */
  evalExpressionAt: evalExpressionAt,
  evalEquationAt: evalEquationAt,
  extractCoefficients: extractCoefficients,
  evaluatorReset: evaluatorReset,

  /* ---- 求解层 ---- */
  solveQuadratic: solveQuadratic,
  solveCubic: solveCubic,
  solveQuartic: solveQuartic,
  solveLinearSystem: solveLinearSystem,
  verifyRoots: verifyRoots,
  solversReset: solversReset,

  /* ---- 安全防护层 ---- */
  preCheckExpression: preCheckExpression,
  guardDegree: guardDegree,
  guardMultivariateNonlinear: guardMultivariateNonlinear,
  checkMemoryWatermark: checkMemoryWatermark,

  /* ---- 求解结果（getter 形式，避免跨作用域赋值） ---- */
  getRootCount: function () { return _rootCount; },
  getRootRe: function (i) { return _rootRe[i]; },
  getRootIm: function (i) { return _rootIm[i]; },
  getRootExactNum: function (i) { return _rootExactNum[i]; },
  getRootExactDen: function (i) { return _rootExactDen[i]; },
  getRootExactRad: function (i) { return _rootExactRad[i]; },
  getRootExactValid: function (i) { return _rootExactValid[i]; },
  getSolveError: function () { return _solveError; },
  getResidualMax: function () { return _residualMax; },

  /* ---- 全局状态切换 ---- */
  setAngleMode: function (mode) { angleMode = mode; },
  getAngleMode: function () { return angleMode; },

  /* ---- 矩阵池写入（UI 层给线性方程组输入系数用） ---- */
  setMatrixElement: function (i, v) { matrixPool[i] = v; }
};
