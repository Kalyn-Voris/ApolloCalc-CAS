// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · core_parser.js — Token 解析器（文件 1）
 * Lite Wearable / JerryScript / 堆上限 128KB / Zero-GC
 * Token 编码: word = (type << 8) | payload ；word === 0 即 T_END 哨兵
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象字面量 */

/* 全局内存池：全内核唯一声明处，后续文件直接引用
 * 溢出检查一律读 xxPool.length，池尺寸调整时检查值自动跟随 */
var tokenPool = new Int32Array(512);     /* 256 → 512 */
var tokenCount = 0;
var numberPool = new Float64Array(256);  /* Token 只存索引（128 → 256） */
var numberCount = 0;
var evalStack = new Float64Array(128);   /* 64 → 128 */
var matrixPool = new Float64Array(156);  /* 12x13 增广矩阵：按 128KB 家族功能对等承诺放开到 12 元 */

/* F0 全池水位账本（feat/js-gate-f0）：模块池加载期自报合计（全局标量，零分配）
 * 基础池（本文件/evaluator/base/solvers）由 checkMemoryWatermark live 读 byteLength；
 * 其余池模块在自身文件内、池声明之后以
 *   if (typeof _modulePoolSum === "function") { _modulePoolSum(自身全部池…); }
 * 加载期自报（typeof 守卫兼容独立 require / 浏览器经典脚本 / 部分拼接 eval；
 * arguments 仅存在于加载期这一次调用，非热路径）。
 * 门禁线推导与逐模块账本：docs/contracts/f0-governance-v25.md、
 * tests/js-pool-ledger.json、docs/js-128kb.md 池账本节 */
var _modulePoolBytes = 0;
function _modulePoolSum() {
  var s = 0;
  var i = 0;
  for (i = 0; i < arguments.length; i++) { s += arguments[i].byteLength; }
  _modulePoolBytes += s;
}

/* 精确数值系统（core_exact.js 使用，五分量模型）
 * value = ((exactRat + exactRadNum·√exactRad) / exactDen) × 10^exactExp */
var exactRat = 0;
var exactRadNum = 0;       /* 0 表示无根式 */
var exactRad = 1;          /* 无平方因子，1 表示无根式 */
var exactDen = 1;          /* 恒为正 */
var exactExp = 0;          /* 10^exactExp */
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

/* Token 类型 */
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
var F_SINH = 13;
var F_COSH = 14;
var F_TANH = 15;
var F_ASINH = 16;
var F_ACOSH = 17;
var F_ATANH = 18;

/* 常数 payload */
var C_E = 0;
var C_PI = 1;

/* 解析错误码 */
var ERR_NONE = 0;
var ERR_BADCHAR = 1;
var ERR_OVERFLOW = 2;    /* tokenPool / numberPool */
var ERR_PAREN = 3;
var ERR_EMPTY = 4;

/* 扫描游标 */
var parseError = ERR_NONE;
var scanPos = 0;
var scanLen = 0;

/* 内联助手 */
function emitToken(type, payload) {
  if (tokenCount >= tokenPool.length) { parseError = ERR_OVERFLOW; return -1; }
  tokenPool[tokenCount] = (type << 8) | payload;
  tokenCount++;
  return 0;
}

function pushNumber(v) {
  if (numberCount >= numberPool.length) { parseError = ERR_OVERFLOW; return -1; }
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

  /* 最长前缀优先：sinh/cosh/tanh/asinh/acosh/atanh 必须先于 sin/cos/tan/asin/acos/atan 判定，
   * 否则 sin 会吃掉 sinh 的前 3 字符；a 系反双曲靠第 5 字符 'h'(104) 与 asin/acos/atan 区分 */
  if (c0 === 115) {                                                          /* 's' */
    if (c1 === 105 && c2 === 110 && c3 === 104) { f = F_SINH; n = 4; }       /* sinh */
    else if (c1 === 105 && c2 === 110) { f = F_SIN; n = 3; }                 /* sin */
    else if (c1 === 113 && c2 === 114 && c3 === 116) { f = F_SQRT; n = 4; }  /* sqrt */
  } else if (c0 === 99) {                                                    /* 'c' */
    if (c1 === 111 && c2 === 115 && c3 === 104) { f = F_COSH; n = 4; }       /* cosh */
    else if (c1 === 111 && c2 === 115) { f = F_COS; n = 3; }                 /* cos */
  } else if (c0 === 116) {                                                   /* 't' */
    if (c1 === 97 && c2 === 110 && c3 === 104) { f = F_TANH; n = 4; }        /* tanh */
    else if (c1 === 97 && c2 === 110) { f = F_TAN; n = 3; }                  /* tan */
  } else if (c0 === 97) {                                                    /* 'a' */
    var c4 = (scanPos + 4 < scanLen) ? src.charCodeAt(scanPos + 4) : 0;
    if (c1 === 115 && c2 === 105 && c3 === 110) {                            /* asin / asinh */
      if (c4 === 104) { f = F_ASINH; n = 5; }                                /* asinh */
      else { f = F_ASIN; n = 4; }                                            /* asin */
    } else if (c1 === 99 && c2 === 111 && c3 === 115) {                      /* acos / acosh */
      if (c4 === 104) { f = F_ACOSH; n = 5; }                                /* acosh */
      else { f = F_ACOS; n = 4; }                                            /* acos */
    } else if (c1 === 116 && c2 === 97 && c3 === 110) {                      /* atan / atan2 / atanh */
      if (c4 === 104) { f = F_ATANH; n = 5; }                                /* atanh */
      else if (c4 === 50) { f = F_ATAN2; n = 5; }                            /* atan2 */
      else { f = F_ATAN; n = 4; }                                            /* atan */
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
  if (id === F_SINH) { return "sinh"; }
  if (id === F_COSH) { return "cosh"; }
  if (id === F_TANH) { return "tanh"; }
  if (id === F_ASINH) { return "asinh"; }
  if (id === F_ACOSH) { return "acosh"; }
  if (id === F_ATANH) { return "atanh"; }
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
