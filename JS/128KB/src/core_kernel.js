/* ============================================================
 * ApolloCalc · core_parser.js — Token 解析器（文件 1/4）
 * Lite Wearable / JerryScript / 堆上限 128KB / Zero-GC
 * Token 编码: word = (type << 8) | payload ；word === 0 即 T_END 哨兵
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象字面量
 * ============================================================ */

/* ==================== 全局内存池（全内核唯一声明处，后续文件直接引用） ====================
 * 溢出检查一律读 xxPool.length，池尺寸调整时检查值自动跟随 */
var tokenPool = new Int32Array(512);     /* Token 流（256 → 512） */
var tokenCount = 0;
var numberPool = new Float64Array(256);  /* 数字常量池，Token 只存索引（128 → 256） */
var numberCount = 0;
var evalStack = new Float64Array(128);   /* RPN 求值栈（64 → 128） */
var matrixPool = new Float64Array(156);  /* 12 元线性方程组增广矩阵 (12x13)：按 128KB 家族功能对等承诺放开到 12 元 */

/* ---------- 精确数值系统（core_exact.js 使用，五分量模型） ----------
 * value = ((exactRat + exactRadNum·√exactRad) / exactDen) × 10^exactExp */
var exactRat = 0;          /* 有理部分子 */
var exactRadNum = 0;       /* 根式部系数（0 表示无根式） */
var exactRad = 1;          /* 根号内数（无平方因子），1 表示无根式 */
var exactDen = 1;          /* 公共分母，恒为正 */
var exactExp = 0;          /* 科学计数法指数（10^exactExp） */
var exactIsValid = false;  /* 精确模式有效标志 */
var approxValue = 0.0;     /* 双精度近似值 */

/* ---------- 复数临时量（core_complex.js 使用） ---------- */
var _tmpRe = 0.0;
var _tmpIm = 0.0;

/* ---------- 迭代运算寄存器 ---------- */
var iterVar = 0;
var iterStart = 0;
var iterEnd = 0;
var iterAccum = 0;
var iterProd = 1;

/* ---------- 全局状态 ---------- */
var angleMode = "DEG";             /* "DEG" | "RAD" */
var DEG_TO_RAD = Math.PI / 180;
var RAD_TO_DEG = 180 / Math.PI;

/* ==================== Token 类型 ==================== */
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

/* ---------- 二元运算符 payload ---------- */
var OP_ADD = 0;
var OP_SUB = 1;
var OP_MUL = 2;
var OP_DIV = 3;
var OP_POW = 4;

/* ---------- 函数 payload ---------- */
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

/* ---------- 常数 payload ---------- */
var C_E = 0;
var C_PI = 1;

/* ---------- 解析错误码 ---------- */
var ERR_NONE = 0;
var ERR_BADCHAR = 1;     /* 非法字符 */
var ERR_OVERFLOW = 2;    /* tokenPool / numberPool 溢出 */
var ERR_PAREN = 3;       /* 括号不匹配 */
var ERR_EMPTY = 4;       /* 空表达式 */

/* ---------- 扫描游标 ---------- */
var parseError = ERR_NONE;
var scanPos = 0;
var scanLen = 0;

/* ==================== 内联助手 ==================== */
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

/* ==================== 数字扫描（小数 + 科学计数法） ====================
 * 直接写 numberPool，返回索引；失败返回 -1
 * Horner 累积 v = v*10 + d；小数只记位数，最后一次性除权还原
 * 'e' 歧义：后跟数字（或 +/- 数字）→ 科学计数法；否则终止扫描，'e' 交回主循环当自然常数
 */
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

/* ==================== 函数名硬编码字符树 ====================
 * 命中：返回函数 ID 并消费字符；未命中：返回 0 且不消费
 */
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

/* ==================== 主入口：parseExpression ====================
 * 输入：表达式字符串（"2x^2+3x-5=0" / "sin(30)"）
 * 输出：tokenPool / numberPool / tokenCount / parseError（全局）
 * 返回：parseError（0 = 成功）
 */
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

    /* ---- 空白 ---- */
    if (c === 32 || c === 9 || c === 13 || c === 10) { scanPos++; continue; }

    /* ---- 数字 / 小数点 ---- */
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

    /* ---- 负号：'-' / '−'(8722) ---- */
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

    /* ---- 加号：一元正号丢弃 / 二元加 ---- */
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

    /* ---- 字母：函数名 / 变量 x / 常数 e / pi ---- */
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

    /* ---- 数学特殊符号 ---- */
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

/* ==================== UI 层调试输出（唯一允许转字符串处，内核路径禁调） ==================== */
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

/* ============================================================
 * ApolloCalc · core_math_functions.js — 数学执行层（文件 2/4）
 * Lite Wearable / JerryScript / 堆上限 64KB / Zero-GC
 * 依赖文件 1 全局（只引用，不重复声明）：
 *   angleMode("DEG"|"RAD") / DEG_TO_RAD / RAD_TO_DEG
 *   F_SIN..F_ATANH 函数 ID
 * 必须在 core_parser.js 之后加载
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象字面量
 * ============================================================ */

/* ==================== 文件 2 专属全局变量（唯一声明处） ==================== */
var _dmsDeg = 0;    /* 度：带符号整数。分秒恒非负，符号全挂在度上 */
var _dmsMin = 0;    /* 分：非负整数 0..59 */
var _dmsSec = 0.0;  /* 秒：非负浮点 [0,60) */

/* ---------- Math.log10 兜底（仅加载期执行一次，热路径不经过） ----------
 * 个别 JerryScript 裁剪构建未启用 ES6 Math.log10，缺失时以 log*LOG10E 顶替 */
if (Math.log10 === undefined) {
  Math.log10 = function (v) { return Math.log(v) * Math.LOG10E; };
}

/* ---------- 双曲 / 反双曲函数兜底（仅加载期执行一次，热路径不经过） ----------
 * 个别 JerryScript 裁剪构建未启用 ES6 双曲函数，缺失时按恒等式以 exp/log 组装：
 *   sinh(x) = (e^x − e^−x)/2    cosh(x) = (e^x + e^−x)/2
 *   tanh(x) = (e^2x − 1)/(e^2x + 1)
 *   asinh(x) = ln(x + √(x²+1))，奇函数，负半轴按 asinh(−x) = −asinh(x)
 *     对称直算，避开大数相减的精度损失
 *   acosh(x) = ln(x + √(x²−1))，x ≥ 1
 *   atanh(x) = ½·ln((1+x)/(1−x))，|x| < 1 */
if (Math.sinh === undefined) {
  Math.sinh = function (x) { return (Math.exp(x) - Math.exp(-x)) / 2; };
}
if (Math.cosh === undefined) {
  Math.cosh = function (x) { return (Math.exp(x) + Math.exp(-x)) / 2; };
}
if (Math.tanh === undefined) {
  Math.tanh = function (x) {
    /* |x| ≥ 20 时 tanh 在双精度下已饱和到 ±1；直套公式 exp(2x) 上溢得
       Inf/Inf = NaN，会误触上层域错误判定，故先行钳位 */
    if (x >= 20) { return 1; }
    if (x <= -20) { return -1; }
    var e2 = Math.exp(2 * x);
    return (e2 - 1) / (e2 + 1);
  };
}
if (Math.asinh === undefined) {
  Math.asinh = function (x) {
    /* 负数走对称公式：asinh(-x) = -asinh(x)，直接算不调自身 */
    if (x < 0) {
      var ax = -x;
      return -Math.log(ax + Math.sqrt(ax * ax + 1));
    }
    return Math.log(x + Math.sqrt(x * x + 1));
  };
}
if (Math.acosh === undefined) {
  Math.acosh = function (x) {
    return Math.log(x + Math.sqrt(x * x - 1));
  };
}
if (Math.atanh === undefined) {
  Math.atanh = function (x) {
    return 0.5 * Math.log((1 + x) / (1 - x));
  };
}

/* ==================== 模块一：三角函数（DEG / RAD 双模式） ==================== */

function mathSin(v) {
  /* 零分配：原生 sin，DEG 模式入参乘一次常量，全程栈上完成 */
  if (angleMode === "DEG") { return Math.sin(v * DEG_TO_RAD); }
  return Math.sin(v);
}

function mathCos(v) {
  /* 零分配：原生 cos，DEG 模式入参乘一次常量，全程栈上完成 */
  if (angleMode === "DEG") { return Math.cos(v * DEG_TO_RAD); }
  return Math.cos(v);
}

function mathTan(v) {
  /* 零分配：原生 tan + 奇点防护，无 GC 压力 */
  var r = 0.0;
  if (angleMode === "DEG") { r = Math.tan(v * DEG_TO_RAD); }
  else { r = Math.tan(v); }
  /* 90°/270° 等奇点：π/2 非精确表示致 tan 返回 1e16 量级浮点，钳为 Infinity */
  if (Math.abs(r) > 1e10) { return Infinity; }
  return r;
}

function mathAsin(v) {
  /* 零分配：原生 asin，DEG 模式出参乘 RAD_TO_DEG；|v|>1 由原生返回 NaN 交上层判错 */
  if (angleMode === "DEG") { return Math.asin(v) * RAD_TO_DEG; }
  return Math.asin(v);
}

function mathAcos(v) {
  /* 零分配：原生 acos，DEG 模式出参乘 RAD_TO_DEG；|v|>1 由原生返回 NaN 交上层判错 */
  if (angleMode === "DEG") { return Math.acos(v) * RAD_TO_DEG; }
  return Math.acos(v);
}

function mathAtan(v) {
  /* 零分配：原生 atan，DEG 模式出参乘 RAD_TO_DEG */
  if (angleMode === "DEG") { return Math.atan(v) * RAD_TO_DEG; }
  return Math.atan(v);
}

function mathAtan2(y, x) {
  /* 零分配：原生 atan2 自带全象限判定（含 x=0 边界），DEG 模式出参乘 RAD_TO_DEG */
  if (angleMode === "DEG") { return Math.atan2(y, x) * RAD_TO_DEG; }
  return Math.atan2(y, x);
}

/* ==================== 模块二：对数 / 指数 / 绝对值 ==================== */

function mathLn(v) {
  /* 零分配：原生 log（自然对数），直通 C 底层数学库，不手写泰勒级数 */
  return Math.log(v);
}

function mathLog10(v) {
  /* 零分配：原生 log10（缺失时已由加载期兜底接管） */
  return Math.log10(v);
}

function mathExp(v) {
  /* 零分配：原生 exp，直接走 C 底层数学库 */
  return Math.exp(v);
}

function mathAbs(v) {
  /* 零分配：原生 abs */
  return Math.abs(v);
}

/* ==================== 模块二补充：双曲 / 反双曲函数 ====================
 * 双曲函数不是角度量纲，DEG/RAD 模式均直通原生，不做换算 */

function mathSinh(v) {
  /* 零分配：原生 sinh，直通 C 底层（缺失时已由加载期兜底接管） */
  return Math.sinh(v);
}

function mathCosh(v) {
  /* 零分配：原生 cosh（缺失时已由加载期兜底接管） */
  return Math.cosh(v);
}

function mathTanh(v) {
  /* 零分配：原生 tanh（缺失时已由加载期兜底接管） */
  return Math.tanh(v);
}

function mathAsinh(v) {
  /* 零分配：反双曲正弦，全实轴有定义 */
  return Math.asinh(v);
}

function mathAcosh(v) {
  /* 零分配：反双曲余弦；v < 1 时原生返回 NaN 交上层判域错误 */
  return Math.acosh(v);
}

function mathAtanh(v) {
  /* 零分配：反双曲正切；|v| ≥ 1 时原生返回 NaN/±Infinity 交上层判错 */
  return Math.atanh(v);
}

/* ==================== 模块三：度分秒（D°M′S″）转换 ==================== */

function degToDms(deg) {
  /* 零分配：floor + 取余纯算术，结果写全局 _dmsDeg/_dmsMin/_dmsSec，无堆对象无字符串 */
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

/* ==================== 模块四：函数派发入口 ==================== */

/* funcId：文件 1 的 F_* 常量。
 * atan2 双参：arg1 = y（对边），arg2 = x（邻边）；其余函数只读 arg1。
 * 零分配：switch 整型派发直通原生数学，全程栈上完成，无对象创建 */
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
    case F_SINH: return mathSinh(arg1);
    case F_COSH: return mathCosh(arg1);
    case F_TANH: return mathTanh(arg1);
    case F_ASINH: return mathAsinh(arg1);
    case F_ACOSH: return mathAcosh(arg1);
    case F_ATANH: return mathAtanh(arg1);
    default: return NaN;
  }
}

/*
 * ApolloCalc 文件 3: core_complex.js
 * 极简复数运算层（零分配），供文件 6 方程求解器使用。
 * 依赖文件 1 已声明的全局结果寄存器 _tmpRe / _tmpIm，此处不重复声明。
 */

/* ============================================================
 * 文件 3 专属全局变量（唯一声明处）
 * ============================================================ */
var _aRe = 0.0;   /* 操作数 A 实部 */
var _aIm = 0.0;   /* 操作数 A 虚部 */
var _bRe = 0.0;   /* 操作数 B 实部 */
var _bIm = 0.0;   /* 操作数 B 虚部 */

/* ============================================================
 * 模块一：基础四则运算
 * ============================================================ */

function cAdd() {
  /* 零分配：仅操作全局标量，无 GC */
  _tmpRe = _aRe + _bRe;
  _tmpIm = _aIm + _bIm;
}

function cSub() {
  /* 零分配：仅操作全局标量，无 GC */
  _tmpRe = _aRe - _bRe;
  _tmpIm = _aIm - _bIm;
}

function cMul() {
  /* 零分配：4 次浮点乘加原地完成，用局部 let 暂存避免读写序污染 */
  /* 零分配：局部标量存放于寄存器/JerryScript 值槽，不进堆 */
  let re = _aRe * _bRe - _aIm * _bIm;
  let im = _aRe * _bIm + _aIm * _bRe;
  _tmpRe = re;
  _tmpIm = im;
}

function cDiv() {
  /* 零分配：分母模平方 + 分子复数乘法展开，无临时对象 */
  let d = _bRe * _bRe + _bIm * _bIm;
  if (d === 0.0) {
    /* 分母为零：整体置 NaN，交由上层判定无效根 */
    _tmpRe = NaN;
    _tmpIm = NaN;
    return;
  }
  let re = (_aRe * _bRe + _aIm * _bIm) / d;
  let im = (_aIm * _bRe - _aRe * _bIm) / d;
  _tmpRe = re;
  _tmpIm = im;
}

/* ============================================================
 * 模块二：模长、辐角与共轭
 * ============================================================ */

function cAbs() {
  /* 返回普通 Number，零分配 */
  return Math.sqrt(_aRe * _aRe + _aIm * _aIm);
}

function cArg() {
  /* 返回普通 Number，零分配 */
  return Math.atan2(_aIm, _aRe);
}

function cConj() {
  /* 零分配：仅翻转虚部符号 */
  _tmpRe = _aRe;
  _tmpIm = -_aIm;
}

/* ============================================================
 * 模块三：复数开方（核心，精度敏感）
 * ============================================================ */

function cSqrt() {
  /* 零分配：仅全局标量与局部 let，Math.* 走原生 C 底层 */
  let re = _aRe;
  let im = _aIm;

  /* 零元情形：0 + 0i 的开方仍为 0 + 0i */
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

/* ============================================================
 * 模块四：复数乘幂（快速幂，平方-乘法）
 * ============================================================ */

function cPowInt(n) {
  /* 零分配：累加器全部为局部 let 标量，平方-乘法无对象产生 */
  let baseRe = _aRe;
  let baseIm = _aIm;
  let exp = n;

  /* 负指数：先取倒数，再按正指数求幂 */
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

  /* 结果累加器初始化为 1 + 0i */
  let accRe = 1.0;
  let accIm = 0.0;

  /* 平方-乘法：从最低位起逐位消费指数 */
  let e = exp;
  for (;;) {
    if ((e & 1) === 1) {
      /* acc = acc * base（4 次浮点乘加，局部标量暂存） */
      let nRe = accRe * baseRe - accIm * baseIm;
      let nIm = accRe * baseIm + accIm * baseRe;
      accRe = nRe;
      accIm = nIm;
    }
    e = e >> 1;
    if (e === 0) {
      break;
    }
    /* base = base * base（原地平方） */
    let sqRe = baseRe * baseRe - baseIm * baseIm;
    let sqIm = 2.0 * baseRe * baseIm;
    baseRe = sqRe;
    baseIm = sqIm;
  }

  _tmpRe = accRe;
  _tmpIm = accIm;
}

/* ============================================================
 * 模块五：复数指数 / 对数 / 实数幂（128KB 扩展）
 * ============================================================ */

function cExp() {
  /* 零分配：e^(a+bi) = e^a · (cos b + i sin b)，仅局部标量 */
  var ea = Math.exp(_aRe);
  var re = ea * Math.cos(_aIm);
  var im = ea * Math.sin(_aIm);
  _tmpRe = re;
  _tmpIm = im;
}

function cLn() {
  /* 零分配：主值对数 ln(z) = ln|z| + i·arg(z)
     零元时 ln(0) = -Infinity 交上层判错 */
  var re = _aRe;
  var im = _aIm;
  if (re === 0 && im === 0) {
    /* ln(0) 无定义：返回 -Infinity + 0i，上层据 isFinite 判错 */
    _tmpRe = -Infinity;
    _tmpIm = 0;
    return;
  }
  var lnR = 0.5 * Math.log(re * re + im * im);   /* = ln|z|，避免先开方再取对数 */
  var arg = Math.atan2(im, re);
  _tmpRe = lnR;
  _tmpIm = arg;
}

function cPowReal(w) {
  /* 零分配：z^w = e^(w·ln z)，w 为实数
     内部临时复用 _aRe/_aIm 与 cLn/cExp */
  var re = _aRe;
  var im = _aIm;
  /* z = 0 且 w > 0 → 0；z = 0 且 w <= 0 → 无定义 */
  if (re === 0 && im === 0) {
    if (w > 0) { _tmpRe = 0; _tmpIm = 0; }
    else { _tmpRe = NaN; _tmpIm = NaN; }
    return;
  }
  /* 保存 z，用 _aRe/_aIm 暂存 w·ln z */
  cLn();                         /* _tmpRe = ln|z|, _tmpIm = arg z */
  var lnRe = _tmpRe;
  var lnIm = _tmpIm;
  _aRe = w * lnRe;
  _aIm = w * lnIm;
  cExp();                        /* 结果回 _tmpRe/_tmpIm */
}

/* ============================================================
 * 模块六：复位辅助
 * ============================================================ */

function cReset() {
  /* 零分配：六寄存器整体清零，供方程求解器每轮迭代前调用 */
  _aRe = 0.0;
  _aIm = 0.0;
  _bRe = 0.0;
  _bIm = 0.0;
  _tmpRe = 0.0;
  _tmpIm = 0.0;
}

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

/* ============================================================
 * ApolloCalc · core_evaluator.js — RPN 求值器（文件 5/6）
 * Lite Wearable / JerryScript / 堆上限 128KB / Zero-GC
 * 职责：Shunting-yard 中缀→后缀（显式栈迭代）、RPN 栈式求值、
 *       方程 f(x)=g(x) → f(x)-g(x)、数值采样法提取多项式系数
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象/数组字面量
 * ============================================================ */

/* ==================== 文件 5 专属全局变量（唯一声明处） ====================
 * 池溢出检查一律读 xxStack.length / xxToken.length，尺寸调整时检查值自动跟随 */
var _rpnLen = 0;                          /* 逆波兰 Token 流长度 */
var _rpnToken = new Int32Array(512);      /* 逆波兰 Token 流（与 tokenPool 同编码，256 → 512） */
var _evalX = 0.0;                         /* 当前代入的 x 值 */
var _evalError = 0;                       /* 0=OK 1=除零 2=域错误 3=溢出 4=内部错 */
var _evalResult = 0.0;                    /* 最近一次求值结果 */
var _evalSp = 0;                          /* evalStack 栈指针 */

var _opStack = new Int32Array(128);       /* Shunting-yard 运算符栈（存完整 Token word，64 → 128） */
var _opSp = 0;                            /* 运算符栈指针 */

var _lhsToken = new Int32Array(256);      /* 方程等号左侧 Token 段（128 → 256） */
var _rhsToken = new Int32Array(256);      /* 方程等号右侧 Token 段（128 → 256） */
var _lhsCount = 0;
var _rhsCount = 0;
var _lhsResult = 0.0;
var _rhsResult = 0.0;

var _coeffs = new Float64Array(5);        /* 多项式系数 [常数项..最高次项] */

/* ==================== 内联助手：运算符优先级（整数化，×2 避开 2.5） ====================
 * +− = 2   ×÷ = 4   一元负号 = 5（高于乘除低于乘方）   ^ = 6
 * 右结合：^ 与 T_UMINUS；其余左结合
 * 返回 0 表示该 Token 不是可比较优先级的运算符（LP / FUNC 等）
 */
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

/* ==================== 模块一：Shunting-yard 中缀转后缀 ====================
 * 读取 srcPool[0..srcCount-1]，输出到 dstPool[0.._rpnLen-1]，返回 _rpnLen
 * 运算符栈 _opStack 存完整 Token word（一元负号入栈时无条件压栈，
 * 保证 a^-b 不弹缺右操作数的 ^）；方程等号必须由 evalEquationAt 分段后传入
 */
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

    /* ---- 操作数：直接输出 ---- */
    if (t === T_NUM || t === T_VAR || t === T_CONST) {
      if (_rpnLen >= _rpnToken.length) { _evalError = 4; return _rpnLen; }
      dstPool[_rpnLen] = w;
      _rpnLen++;
      continue;
    }

    /* ---- 函数 / 左括号 / 一元负号：无条件压栈 ---- */
    if (t === T_FUNC || t === T_LP || t === T_UMINUS) {
      if (_opSp >= _opStack.length) { _evalError = 4; return _rpnLen; }
      _opStack[_opSp] = w;
      _opSp++;
      continue;
    }

    /* ---- 右括号：弹到左括号；括号前若是函数则一并弹出 ---- */
    if (t === T_RP) {
      while (_opSp > 0) {
        _opSp--;
        t = _opStack[_opSp] >> 8;
        if (t === T_LP) { break; }
        if (_rpnLen >= _rpnToken.length) { _evalError = 4; return _rpnLen; }
        dstPool[_rpnLen] = _opStack[_opSp];
        _rpnLen++;
      }
      if (t !== T_LP) { _evalError = 4; return _rpnLen; }   /* 无匹配左括号 */
      if (_opSp > 0 && (_opStack[_opSp - 1] >> 8) === T_FUNC) {
        _opSp--;
        if (_rpnLen >= _rpnToken.length) { _evalError = 4; return _rpnLen; }
        dstPool[_rpnLen] = _opStack[_opSp];
        _rpnLen++;
      }
      continue;
    }

    /* ---- 逗号：弹到左括号（双参函数参数分隔，如 atan2） ---- */
    if (t === T_COMMA) {
      while (_opSp > 0 && (_opStack[_opSp - 1] >> 8) !== T_LP) {
        _opSp--;
        if (_rpnLen >= _rpnToken.length) { _evalError = 4; return _rpnLen; }
        dstPool[_rpnLen] = _opStack[_opSp];
        _rpnLen++;
      }
      if (_opSp === 0) { _evalError = 4; return _rpnLen; }  /* 逗号不在函数括号内 */
      continue;
    }

    /* ---- 二元运算符：按优先级弹栈后压栈 ---- */
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
          if (_rpnLen >= _rpnToken.length) { _evalError = 4; return _rpnLen; }
          dstPool[_rpnLen] = w;
          _rpnLen++;
        } else {
          break;
        }
      }
      if (_opSp >= _opStack.length) { _evalError = 4; return _rpnLen; }
      _opStack[_opSp] = cur;
      _opSp++;
      continue;
    }

    /* ---- 等号不该流到这里：必须先经 evalEquationAt 分段 ---- */
    if (t === T_EQ) { _evalError = 4; return _rpnLen; }
  }

  /* ---- 收尾：清空运算符栈；残余左括号 = 括号不匹配 ---- */
  while (_opSp > 0) {
    _opSp--;
    if ((_opStack[_opSp] >> 8) === T_LP) { _evalError = 4; return _rpnLen; }
    if (_rpnLen >= _rpnToken.length) { _evalError = 4; return _rpnLen; }
    dstPool[_rpnLen] = _opStack[_opSp];
    _rpnLen++;
  }
  return _rpnLen;
}

/* ==================== 模块二：RPN 求值器 ====================
 * 读取 rpnPool[0..rpnLen-1]，evalStack + _evalSp 栈式求值
 * 结果写 _evalResult 并返回；任何错误置 _evalError 后立即返回 NaN
 */
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

    /* ---- 压栈类操作数 ---- */
    if (t === T_NUM) {
      if (_evalSp >= evalStack.length) { _evalError = 3; return NaN; }    /* 栈溢出 */
      evalStack[_evalSp] = numberPool[p];
      _evalSp++;
      continue;
    }
    if (t === T_VAR) {
      if (_evalSp >= evalStack.length) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = _evalX;
      _evalSp++;
      continue;
    }
    if (t === T_CONST) {
      if (_evalSp >= evalStack.length) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = (p === C_PI) ? Math.PI : Math.E;
      _evalSp++;
      continue;
    }

    /* ---- 一元负号 ---- */
    if (t === T_UMINUS) {
      if (_evalSp < 1) { _evalError = 4; return NaN; }      /* 栈下溢 */
      evalStack[_evalSp - 1] = -evalStack[_evalSp - 1];     /* 原地取负，栈深不变 */
      continue;
    }

    /* ---- 双参函数 atan2(y, x)：先弹 x 后弹 y ---- */
    if (t === T_FUNC && p === F_ATAN2) {
      if (_evalSp < 2) { _evalError = 4; return NaN; }
      b = evalStack[--_evalSp];                              /* x */
      a = evalStack[--_evalSp];                              /* y */
      r = applyFunction(F_ATAN2, a, b);
      if (r !== r) { _evalError = 2; return NaN; }           /* 域错误 */
      if (_evalSp >= evalStack.length) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = r;
      _evalSp++;
      continue;
    }

    /* ---- 单参函数 ---- */
    if (t === T_FUNC) {
      if (_evalSp < 1) { _evalError = 4; return NaN; }
      a = evalStack[--_evalSp];
      r = applyFunction(p, a, 0);
      if (r !== r && a === a) { _evalError = 2; return NaN; }   /* 域错误，如 sqrt(-1) ln(-1) */
      if (r === Infinity || r === -Infinity) { _evalError = 3; return NaN; }  /* 溢出 */
      if (_evalSp >= evalStack.length) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = r;
      _evalSp++;
      continue;
    }

    /* ---- 二元运算符：右操作数先弹 ---- */
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
      if (_evalSp >= evalStack.length) { _evalError = 3; return NaN; }
      evalStack[_evalSp] = r;
      _evalSp++;
      continue;
    }
  }

  /* ---- 收尾：栈上必须恰好剩一个结果 ---- */
  if (_evalSp !== 1) { _evalError = 4; return NaN; }
  _evalResult = evalStack[0];
  return _evalResult;
}

/* ==================== 模块三：求值入口 ==================== */

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

  /* 左侧 f(x) */
  _rpnLen = toRPN(_lhsToken, _lhsCount, _rpnToken);
  if (_evalError !== 0) { _evalResult = NaN; return NaN; }
  _lhsResult = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _evalResult = NaN; return NaN; }

  /* 右侧 g(x) */
  _rpnLen = toRPN(_rhsToken, _rhsCount, _rpnToken);
  if (_evalError !== 0) { _evalResult = NaN; return NaN; }
  _rhsResult = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _evalResult = NaN; return NaN; }

  _evalResult = _lhsResult - _rhsResult;
  return _evalResult;
}

/* ==================== 模块四：多项式系数提取（数值采样 + 高斯消元） ====================
 * 把当前方程 f(x)=0（或 f(x)=g(x) 已转差）在 maxDegree+1 个采样点上求值，
 * 解范德蒙德方程组得幂基系数，写入 _coeffs[0..maxDegree]（常数项→最高次）
 * 内存策略：增广矩阵复用 matrixPool 前 30 个元素（5x6，行主序，步长 maxDegree+2）
 * 返回 0 = 成功；否则返回错误码且 _coeffs 已清零
 */
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

  /* ---- 采样并构造增广矩阵：第 k 行 = [1, xk, xk², .., xkⁿ⁻¹ | y_k] ----
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

  /* ---- 高斯消元（列主元部分选主，显式循环，零递归） ---- */
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
    if (piv !== col) {                 /* 整行交换 */
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

  /* ---- 回代（内存策略：结果直接落 _coeffs，不建临时数组） ---- */
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

/* ==================== 模块五：复位 ====================
 * 方程求解器每轮求解前调用（内存策略：只清标量与预分配数组，零分配） */
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

/* ============================================================
 * ApolloCalc · core_calculus.js — 数值微积分（文件 7）
 * Lite Wearable / JerryScript / 堆上限 128KB / Zero-GC
 * 职责：一阶导数（中心差分）、二阶导数（五点公式）、
 *       定积分（自适应复合辛普森）、数值极限（左右逼近）、
 *       泰勒展开（逐阶数值导数 + 中心差分表，系数缓存 Horner 求值）
 * 依赖：文件 1（全局池 tokenPool / tokenCount）、
 *       文件 5（evalRPN / toRPN / _rpnToken / _evalX / _evalError 全局状态，
 *       复用是有意为之的性能优化：积分最坏 ~1.3 万次求值只做一次 toRPN）
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象/数组字面量
 * UI 契约：表达式变更后必须先 calculusReset() 再 calcXxx，
 *          否则 _calcRpnReady 会复用旧表达式的缓存 RPN
 * ============================================================ */

/* ==================== 文件 7 专属全局变量（唯一声明处） ==================== */
var _calcResult = 0.0;          /* 最近一次微积分结果 */
var _calcError = 0;             /* 0=OK 1=域错误 2=不收敛 3=迭代超限 4=内部错 */
var _calcIterations = 0;        /* 最近一次计算的求值次数（诊断用） */
var _calcRpnReady = 0;          /* 0=RPN 未缓存 1=已缓存（表达式变更前必须经 calculusReset 清零） */

/* ==================== 模块一：域检查与内部求值 ==================== */

/* 扫描 tokenPool 是否含等号；含等号表示这是方程而不是函数 → 微积分域错误 */
function calcCheckFunction() {
  var i = 0;
  for (i = 0; i < tokenCount; i++) {
    if ((tokenPool[i] >> 8) === T_EQ) { return 1; }
  }
  return 0;
}

/* 缓存 RPN 并求值一次（首次调用做 toRPN，后续微积分操作直接复用） */
function calcPrepareRPN() {
  if (_calcRpnReady === 1) { return 0; }
  _evalError = 0;   /* toRPN 成功时不清全局 _evalError，须先清零再查（与 evalExpressionAt 契约一致），否则上游遗留的域错误码会毒化缓存准备 */
  _rpnLen = toRPN(tokenPool, tokenCount, _rpnToken);
  if (_evalError !== 0) { return _evalError; }
  _calcRpnReady = 1;
  return 0;
}

/* 轻量复位：只清 RPN 缓存与诊断量，不清 _calcResult */
function calcResetRPN() {
  _calcRpnReady = 0;
  _calcIterations = 0;
  _calcError = 0;
}

/* ==================== 模块二：一阶导数（中心差分） ====================
 * f'(x) ≈ (f(x+h) − f(x−h)) / (2h)
 * h = 1e-5 · max(|x|, 1)：x 近 0 时 h=1e-5；x 大时 h 正比于 x，
 * 避免 x+h 与 x 因浮点精度无法区分
 */
function calcDerivative(x) {
  if (!isFinite(x)) { _calcError = 1; return NaN; }
  if (calcCheckFunction() !== 0) { _calcError = 1; return NaN; }
  if (calcPrepareRPN() !== 0) { _calcError = 4; return NaN; }

  var ax = x < 0 ? -x : x;
  var h = 1e-5 * (ax > 1 ? ax : 1);

  _evalX = x + h;
  _evalError = 0;
  var yPlus = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }

  _evalX = x - h;
  _evalError = 0;
  var yMinus = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }

  _calcIterations = 2;
  _calcResult = (yPlus - yMinus) / (2 * h);
  if (!isFinite(_calcResult)) { _calcError = 1; return NaN; }
  _calcError = 0;
  return _calcResult;
}

/* ==================== 模块三：二阶导数（五点公式） ====================
 * f''(x) ≈ (−f(x+2h) + 16f(x+h) − 30f(x) + 16f(x−h) − f(x−2h)) / (12h²)
 * h = 1e-4 · max(|x|, 1)（二阶差分舍入更敏感，h 略大于一阶）
 */
function calcDerivative2(x) {
  if (!isFinite(x)) { _calcError = 1; return NaN; }
  if (calcCheckFunction() !== 0) { _calcError = 1; return NaN; }
  if (calcPrepareRPN() !== 0) { _calcError = 4; return NaN; }

  var ax = x < 0 ? -x : x;
  var h = 1e-4 * (ax > 1 ? ax : 1);
  var h2 = h * h;

  _evalX = x + 2 * h; _evalError = 0; var fp2 = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }
  _evalX = x + h;     _evalError = 0; var fp1 = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }
  _evalX = x;         _evalError = 0; var f0 = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }
  _evalX = x - h;     _evalError = 0; var fm1 = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }
  _evalX = x - 2 * h; _evalError = 0; var fm2 = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }

  _calcIterations = 5;
  _calcResult = (-fp2 + 16 * fp1 - 30 * f0 + 16 * fm1 - fm2) / (12 * h2);
  if (!isFinite(_calcResult)) { _calcError = 1; return NaN; }
  _calcError = 0;
  return _calcResult;
}

/* ==================== 模块四：定积分（自适应复合辛普森） ====================
 * ∫[a,b] f dx ≈ (h/3)[f(a) + 4Σf(奇内点) + 2Σf(偶内点) + f(b)]
 * 自适应：n 从 100 起每轮加倍（上限 10000），前后两轮差 < 1e-8 即停；
 * 每轮单次遍历累加 sumOdd/sumEven，不缓存采样点（辛普森节点随 n 变化不可复用）；
 * 采样点 x = a + i·h 用乘法重算，不做 x += h 累积（避免浮点误差漂移）；
 * n 加倍到上限仍未收敛时返回当前结果且 _calcError=0，由 UI 层自行判断
 */
function calcIntegral(a, b) {
  if (!isFinite(a) || !isFinite(b)) { _calcError = 1; return NaN; }
  if (a === b) { _calcResult = 0; _calcError = 0; return 0; }
  if (calcCheckFunction() !== 0) { _calcError = 1; return NaN; }
  if (calcPrepareRPN() !== 0) { _calcError = 4; return NaN; }

  var fa = 0.0;
  var fb = 0.0;
  var i = 0;
  var n = 0;
  var h = 0.0;
  var x = 0.0;
  var y = 0.0;
  var sumOdd = 0.0;
  var sumEven = 0.0;
  var prev = 0.0;
  var curr = 0.0;
  var diff = 0.0;
  var iterTotal = 0;

  /* 端点 f(a)、f(b) */
  _evalX = a; _evalError = 0;
  fa = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }
  _evalX = b; _evalError = 0;
  fb = evalRPN(_rpnToken, _rpnLen);
  if (_evalError !== 0) { _calcError = 1; return NaN; }
  iterTotal = 2;

  n = 100;
  prev = 0.0;
  curr = 0.0;
  while (n <= 10000) {
    h = (b - a) / n;
    sumOdd = 0.0;
    sumEven = 0.0;
    for (i = 1; i < n; i++) {
      x = a + i * h;
      _evalX = x;
      _evalError = 0;
      y = evalRPN(_rpnToken, _rpnLen);
      if (_evalError !== 0) { _calcError = 1; return NaN; }
      iterTotal++;
      if (i % 2 === 1) { sumOdd += y; }
      else { sumEven += y; }
    }
    curr = (h / 3) * (fa + 4 * sumOdd + 2 * sumEven + fb);
    if (!isFinite(curr)) { _calcError = 1; return NaN; }

    if (n > 100) {
      diff = curr - prev;
      if (diff < 0) { diff = -diff; }
      if (diff < 1e-8) { break; }
    }
    prev = curr;
    n = n * 2;
  }

  _calcIterations = iterTotal;
  _calcResult = curr;
  _calcError = 0;
  return _calcResult;
}

/* ==================== 模块五：极限（左右逼近） ====================
 * side: -1 左极限 / +1 右极限 / 0 双侧（左右都收敛且相差 < 1e-6 才返回均值）
 *
 * 单侧逼近：ε 从 1e-1 逐级缩到 1e-8（8 个采样点），相邻两级函数值差 < 1e-6 判收敛。
 * 注：收敛阈值取 1e-6 而非 1e-8 —— 一阶逼近型极限（如 (x²−1)/(x−1)→2，
 * x=1±ε 时值 ≈ 2±ε）相邻采样差 ≈ 9·10⁻ᵏ，6 步方案（ε≥1e-6）下最小差 9e-6，
 * 1e-8 阈值恒不收敛；ε 再往下压时 (x²−1) 的消去误差 ~1e-16/ε 又会淹没信号。
 * 8 步 + 1e-6 下 sin(x)/x 类仍达 ~1e-9 精度，(x²−1)/(x−1) 稳定收敛。
 */
function calcLimit(a, side) {
  if (!isFinite(a)) { _calcError = 1; return NaN; }
  if (calcCheckFunction() !== 0) { _calcError = 1; return NaN; }
  if (calcPrepareRPN() !== 0) { _calcError = 4; return NaN; }

  if (side === 0) {
    var limL = _calcLimitOneSide(a, -1);
    if (_calcError !== 0) { return NaN; }
    var limR = _calcLimitOneSide(a, +1);
    if (_calcError !== 0) { return NaN; }
    var dl = limL - limR;
    if (dl < 0) { dl = -dl; }
    if (dl > 1e-6) { _calcError = 2; return NaN; }   /* 左右极限不一致 */
    _calcResult = (limL + limR) / 2;
    _calcError = 0;
    return _calcResult;
  }
  return _calcLimitOneSide(a, side);
}

/* 单侧逼近内核（仅供 calcLimit 调用，非递归） */
function _calcLimitOneSide(a, side) {
  var eps = 0.1;
  var prev = NaN;
  var curr = 0.0;
  var x = 0.0;
  var step = 0;
  var iterTotal = 0;

  for (step = 0; step < 8; step++) {
    x = a + side * eps;
    _evalX = x;
    _evalError = 0;
    curr = evalRPN(_rpnToken, _rpnLen);
    if (_evalError !== 0) { _calcError = 1; return NaN; }
    if (curr !== curr) { _calcError = 1; return NaN; }   /* 采样点落出域（如 0^负数经 pow 产生 NaN） */
    iterTotal++;

    if (step > 0 && prev === prev) {
      var d = curr - prev;
      if (d < 0) { d = -d; }
      if (d < 1e-6) {
        _calcIterations = iterTotal;
        _calcResult = curr;
        _calcError = 0;
        return curr;
      }
    }
    prev = curr;
    eps = eps * 0.1;
  }
  _calcIterations = iterTotal;
  _calcError = 2;
  return NaN;
}

/* ==================== 模块六：复位 ====================
 * UI 层在每次表达式变更后的首个微积分操作前调用（零分配；
 * 泰勒系数属于旧表达式，必须一并失效） */
function calculusReset() {
  _calcResult = 0.0;
  _calcError = 0;
  _calcIterations = 0;
  _calcRpnReady = 0;
  _taylorOrder = 0;
  _taylorCoeffs[0] = 0.0;
}

/* ==================== 模块七：泰勒展开 ====================
 * f(x) 在 a 点的 n 阶泰勒展开，系数 c_k = f⁽ᵏ⁾(a)/k! 缓存于 _taylorCoeffs：
 *   k=0   直接求值 f(a)
 *   k=1   复用一阶中心差分（calcDerivative，h=1e-5·max(|a|,1)）
 *   k=2   复用二阶五点公式（calcDerivative2，h=1e-4·max(|a|,1)）
 *   k≥3   中心差分表：网格 x_j = a+(j−K)h（j=0..2K，K=阶数），
 *         逐层前向差分（原地覆盖，单缓冲），偶数阶取中心表元素
 *         Δᵏf_{K−k/2}，奇数阶取跨中心两表元素平均——均为 O(h²) 精度；
 *         步长 h = ε^(1/(K+2))·max(|a|,1)（ε=2.22e-16，逐阶舍入/截断折中）
 * 数值边界：k≥10 的高阶导数差分舍入放大（~ε·2ᵏ/hᵏ）不可避免，
 *   高阶系数精度随阶数衰减是数值求导的固有极限，UI 层按需取低阶
 * 返回 0 = 成功；-1 = 参数/域错误（_calcError：1=域错误 3=阶数超限 4=内部错）
 */
var _taylorCoeffs = new Float64Array(16);   /* 展开系数 c_k（最多 16 阶） */
var _taylorOrder = 0;                       /* 当前展开阶数 */
var _taylorCenter = 0.0;                    /* 展开中心 a */
var _taylorWork = new Float64Array(256);    /* 差分表工作区（2K+1 ≤ 33，用完即弃） */

function calcTaylor(x0, order) {
  _calcError = 0;
  if (!isFinite(x0)) { _calcError = 1; return -1; }
  if (order < 0 || order > 16 || order !== Math.floor(order)) { _calcError = 3; return -1; }
  if (calcCheckFunction() !== 0) { _calcError = 1; return -1; }
  if (calcPrepareRPN() !== 0) { _calcError = 4; return -1; }

  var K = order;
  var k = 0;
  var j = 0;
  var iterTotal = 0;
  var scale = x0 < 0 ? -x0 : x0;
  if (scale < 1.0) { scale = 1.0; }

  _taylorCenter = x0;
  _taylorOrder = K;

  /* k=0：f(x0) */
  _evalX = x0;
  _evalError = 0;
  var f0 = evalRPN(_rpnToken, _rpnLen);
  iterTotal++;
  if (_evalError !== 0 || f0 !== f0) { _calcError = 1; return -1; }
  _taylorCoeffs[0] = f0;
  _calcResult = f0;

  /* k=1,2：复用一阶/二阶专用差分公式（c_k = f⁽ᵏ⁾/k!） */
  if (K >= 1) {
    var d1 = calcDerivative(x0);
    iterTotal = iterTotal + _calcIterations;
    if (d1 !== d1) { return -1; }              /* _calcError 已由 calcDerivative 置位 */
    _taylorCoeffs[1] = d1;
  }
  if (K >= 2) {
    var d2 = calcDerivative2(x0);
    iterTotal = iterTotal + _calcIterations;
    if (d2 !== d2) { return -1; }
    _taylorCoeffs[2] = d2 * 0.5;
  }

  /* k≥3：中心差分表（原地逐层前向差分，取中心/跨中心元素） */
  if (K >= 3) {
    var h = Math.pow(2.22e-16, 1.0 / (K + 2)) * scale;
    for (j = 0; j <= 2 * K; j++) {
      _evalX = x0 + (j - K) * h;
      _evalError = 0;
      var yj = evalRPN(_rpnToken, _rpnLen);
      iterTotal++;
      if (_evalError !== 0 || yj !== yj) { _calcError = 1; return -1; }
      _taylorWork[j] = yj;
    }
    var fact = 2.0;                            /* 进入 k=3 时乘 3 得 3! */
    for (k = 1; k <= K; k++) {
      var t = 0;
      for (t = 0; t <= 2 * K - k; t++) {
        _taylorWork[t] = _taylorWork[t + 1] - _taylorWork[t];
      }
      if (k >= 3) {
        var der = 0.0;
        if (k % 2 === 0) {
          der = _taylorWork[K - k / 2];        /* Δᵏf 的中点恰为 x0 */
        } else {
          var s = K - (k + 1) / 2;             /* 相邻两中点 x0∓h/2，平均回到 x0 */
          der = (_taylorWork[s] + _taylorWork[s + 1]) * 0.5;
        }
        fact = fact * k;
        _taylorCoeffs[k] = der / (Math.pow(h, k) * fact);
      }
    }
  }

  _calcIterations = iterTotal;
  _calcError = 0;
  return 0;
}

/* 用 _taylorCoeffs 在 x 处求泰勒多项式值（Horner 法，纯标量） */
function taylorEval(x) {
  var t = x - _taylorCenter;
  var sum = _taylorCoeffs[_taylorOrder];
  var k = 0;
  for (k = _taylorOrder - 1; k >= 0; k--) {
    sum = sum * t + _taylorCoeffs[k];
  }
  return sum;
}

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

/* ============================================================
 * ApolloCalc · core_solvers.js — 方程求解器（文件 6/9）
 * Lite Wearable / JerryScript / 堆上限 128KB / Zero-GC
 * 职责：二次/三次/四次多项式求根、线性方程组高斯消元（12 元）、残差回验、
 *       OOM 安全防护（深度/函数数/变量数/次数/多元非线性/内存水位）
 * 依赖：文件 1 tokenPool/_coeffs/matrixPool 等全局池
 *       文件 3 cSqrt（双二次路径复数开方）
 *       文件 4 exactFromDiscriminant / exactSetFraction / exactSetMixed
 *       文件 5 evalEquationAt（残差回验）
 *       文件 9 ratRootDescent / ratRootFinish（三次/四次有理根快路径，跨文件提升）
 * 铁律：热路径零堆分配、零字符串操作、零递归、零对象/数组字面量
 *       全部中间量为函数内 var 标量，求解过程不产生任何临时 JS 堆对象
 * ============================================================ */

/* ==================== 文件 6 专属全局变量（唯一声明处） ====================
 * 根结果池 12 槽：多项式至多 4 根，线性方程组至多 12 元（12 元解须完整落池，
 * 4 槽时代 n>4 的越界写会被 TypedArray 静默丢弃） */
var _rootCount = 0;                       /* 当前根数量（0..12） */
var _rootRe = new Float64Array(12);       /* 各根实部 */
var _rootIm = new Float64Array(12);       /* 各根虚部 */
var _rootExactRat = new Float64Array(12); /* 精确形式有理部分子 */
var _rootExactRadNum = new Float64Array(12); /* 精确形式根式部系数（0=无根式） */
var _rootExactRad = new Float64Array(12); /* 精确形式根号内数（无平方因子） */
var _rootExactDen = new Float64Array(12); /* 精确形式分母（恒为正） */
var _rootExactExp = new Float64Array(12); /* 精确形式科学计数法指数（10^exp） */
var _rootExactValid = new Int32Array(12); /* 各根精确形式有效标志（1=支持 0=不支持） */
var _solveError = 0;                      /* 0=OK 1=次数过高 2=多元非线性拒绝 3=求解失败 4=内存水位过高 */
var _residualMax = 0.0;                   /* 残差回验最大相对误差（UI 层决定是否告警） */

/* 精确系统分量上限（2^53-1，与文件 4 保持一致） */
var SOLVER_INT_MAX = 9007199254740991;

/* ==================== 内联助手：带符号立方根 ====================
 * Math.cbrt 在 JerryScript 上不保证可用，用 pow(1/3) 手工实现
 * 零分配：纯标量
 */
function cbrtSigned(x) {
  if (x === 0.0) { return 0.0; }
  if (x > 0.0) { return Math.pow(x, 1.0 / 3.0); }
  return -Math.pow(-x, 1.0 / 3.0);
}

/* ==================== 模块一：二次方程求解器 ====================
 * 输入 _coeffs[0..2] = [c, b, a]，即 ax² + bx + c = 0
 * D ≥ 0：两实根，精确形式经 exactFromDiscriminant(D) 组装
 * D < 0：共轭复根，精确形式置无效
 * 退化 a=0：降为线性（单根，整数系数时保留精确分数）
 * 返回 _solveError（0 = 成功）
 */
function solveQuadratic() {
  _rootCount = 0;
  _solveError = 0;

  var c = _coeffs[0];
  var b = _coeffs[1];
  var a = _coeffs[2];
  if (!isFinite(a) || !isFinite(b) || !isFinite(c)) { _solveError = 3; return _solveError; }

  /* ---- 退化：a = 0 → 线性方程 bx + c = 0 ---- */
  if (a === 0.0) {
    if (b === 0.0) {
      if (c === 0.0) { _solveError = 3; return _solveError; }  /* 恒等式：无穷多根无法枚举 */
      _rootCount = 0;                                          /* 矛盾式：无根，非错误 */
      return 0;
    }
    _rootRe[0] = -c / b;
    _rootIm[0] = 0.0;
    _rootCount = 1;
    /* 内存策略：精确形式仅整数系数可组装，复用文件 4 全局五分量 */
    if (b === Math.floor(b) && c === Math.floor(c) &&
        b <= SOLVER_INT_MAX && b >= -SOLVER_INT_MAX &&
        c <= SOLVER_INT_MAX && c >= -SOLVER_INT_MAX) {
      exactSetFraction(-c, b);
    } else {
      exactInvalidate();
    }
    _rootExactValid[0] = exactIsValid ? 1 : 0;
    _rootExactRat[0] = exactRat;
    _rootExactRadNum[0] = exactRadNum;
    _rootExactRad[0] = exactRad;
    _rootExactDen[0] = exactDen;
    _rootExactExp[0] = exactExp;
    return 0;
  }

  var D = b * b - 4.0 * a * c;
  if (!isFinite(D)) { _solveError = 3; return _solveError; }
  var twoA = 2.0 * a;

  if (D >= 0.0) {
    /* ---- 两实根：x = (-b ± √D) / 2a ---- */
    var sq = Math.sqrt(D);
    _rootRe[0] = (-b + sq) / twoA;
    _rootRe[1] = (-b - sq) / twoA;
    _rootIm[0] = 0.0;
    _rootIm[1] = 0.0;
    _rootCount = 2;

    /* ---- 精确形式组装（五分量混合形式） ----
     * √D = (exactRat + exactRadNum·√exactRad)/exactDen（D 有理数时两者至多一个非零）
     * 根 x = (−b ± √D)/(2a) = ((−b·exactDen ± exactRat) ± exactRadNum·√exactRad) / (2a·exactDen)
     * 内存策略：三分量模型无法表达的有理部+根式部混合（如黄金比例
     * (1+√5)/2）由五分量直接组装，逐根借全局 exactSetMixed 规范化 */
    exactFromDiscriminant(D);
    if (!exactIsValid) {
      _rootExactValid[0] = 0;
      _rootExactValid[1] = 0;
    } else if (a === Math.floor(a) && b === Math.floor(b) &&
               a <= SOLVER_INT_MAX && a >= -SOLVER_INT_MAX &&
               b <= SOLVER_INT_MAX && b >= -SOLVER_INT_MAX) {
      var n = exactRadNum;                     /* √D 的根式部系数 */
      var dr = exactRat;                       /* √D 的有理部分（D 为完全平方时非零） */
      var rr = exactRad;
      var dd = exactDen;
      /* 溢出预检：|b·dd| ≤ 2^53-1；|a| ≤ 2^52 保证 2a 精确；|2a·dd| ≤ 2^53-1 */
      if (Math.abs(b) > SOLVER_INT_MAX / dd ||
          a > SOLVER_INT_MAX / 2 || a < -SOLVER_INT_MAX / 2) {
        _rootExactValid[0] = 0;
        _rootExactValid[1] = 0;
      } else {
        var t2a = 2.0 * a;                     /* |a| ≤ 2^52，精确 */
        if (t2a > SOLVER_INT_MAX / dd || t2a < -SOLVER_INT_MAX / dd) {
          _rootExactValid[0] = 0;
          _rootExactValid[1] = 0;
        } else {
          var base = -b * dd;                  /* 有理部分子基值，模已验，精确 */
          var den = t2a * dd;                  /* 公共分母，模已验，精确 */
          /* base ± dr 同号相加预检：|base| + |dr| ≤ 2^53-1 时两根皆安全，
             否则恰有一根（同号者）越界，逐根判定 */
          var abase = base < 0 ? -base : base;
          var adr = dr < 0 ? -dr : dr;
          var roomOK = abase <= SOLVER_INT_MAX - adr;
          var sameSign = (base >= 0 && dr >= 0) || (base < 0 && dr < 0);
          if (!sameSign || roomOK) {
            /* 根 0：x₀ = (base + dr + n·√rr)/den（仅同号相加可能越界），借全局规范化约分 */
            exactSetMixed(base + dr, n, rr, den);
            _rootExactValid[0] = exactIsValid ? 1 : 0;
            _rootExactRat[0] = exactRat;
            _rootExactRadNum[0] = exactRadNum;
            _rootExactRad[0] = exactRad;
            _rootExactDen[0] = exactDen;
            _rootExactExp[0] = exactExp;
          } else {
            _rootExactValid[0] = 0;
          }
          if (sameSign || roomOK) {
            /* 根 1：x₁ = (base − dr − n·√rr)/den（仅异号相减可能越界），根式部整体变号 */
            exactSetMixed(base - dr, -n, rr, den);
            _rootExactValid[1] = exactIsValid ? 1 : 0;
            _rootExactRat[1] = exactRat;
            _rootExactRadNum[1] = exactRadNum;
            _rootExactRad[1] = exactRad;
            _rootExactDen[1] = exactDen;
            _rootExactExp[1] = exactExp;
          } else {
            _rootExactValid[1] = 0;
          }
        }
      }
    } else {
      _rootExactValid[0] = 0;
      _rootExactValid[1] = 0;
    }
  } else {
    /* ---- 共轭复根：x = (-b ± i√|D|) / 2a ---- */
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

/* ==================== 模块二：三次方程求解器（有理根快路径 + 卡尔达诺 + 三角切换） ====================
 * 输入 _coeffs[0..3] = [d, c, b, a]，即 ax³ + bx² + cx + d = 0
 * 有理根快路径：整系数时枚举 p/q 候选（p|d、q|a）综合除法验证，
 *   迭代降次（显式栈，无递归）；全有理直接填根（精确形式 exactSetFraction），
 *   部分有理降次后委托二次；无有理根走原卡尔达诺/三角路径
 * 消去二次项：x = y − b/(3a) → y³ + py + q = 0
 * Δ > 0：一实根 + 共轭复对（卡尔达诺，全实标量路径——Δ>0 时 u³/v³ 皆为实数，
 *        不产生复数中间量，cSqrt/cPowInt 在本路径无调用点）
 * Δ = 0：三实根（含重根）
 * Δ < 0：不可约情形，必须切换三角法（卡尔达诺会因复数中间量丢失精度）
 * skipRational：四次预解式复用本函数时传 1（预解式系数为派生浮点量，跳过整系数枚举）
 */
function solveCubic(skipRational) {
  _rootCount = 0;
  _solveError = 0;

  var d = _coeffs[0];
  var c = _coeffs[1];
  var b = _coeffs[2];
  var a = _coeffs[3];
  if (!isFinite(a) || !isFinite(b) || !isFinite(c) || !isFinite(d)) { _solveError = 3; return _solveError; }

  /* 退化 a = 0 → 降次为二次（_coeffs[0..2] 恰为二次布局，直接委托） */
  if (a === 0.0) { return solveQuadratic(); }

  /* 有理根快路径（0 = 无有理根，_coeffs 未动，继续卡尔达诺） */
  if (skipRational !== 1) {
    var kRat = ratRootDescent(3);
    if (kRat > 0) { return ratRootFinish(kRat, 3); }
  }

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
    /* ---- Δ > 0：卡尔达诺 ----
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
    /* ---- Δ = 0：三实根（y1 = 2∛(−q/2) 单根，y2 = y3 = −∛(−q/2) 二重根） ---- */
    var t0 = cbrtSigned(-qh);
    _rootRe[0] = 2.0 * t0 + off;
    _rootRe[1] = -t0 + off;
    _rootRe[2] = -t0 + off;
    _rootIm[0] = 0.0;
    _rootIm[1] = 0.0;
    _rootIm[2] = 0.0;
    _rootCount = 3;
  } else {
    /* ---- Δ < 0 不可约：三角法 ----
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

/* ==================== 模块三：四次方程求解器（有理根快路径 + 笛卡尔分解法） ====================
 * 输入 _coeffs[0..4] = [e, d, c, b, a]，即 ax⁴ + bx³ + cx² + dx + e = 0
 * 有理根快路径：整系数时枚举 p/q 候选综合除法验证，迭代降次（显式栈，无递归）；
 *   全有理直接填根；部分有理降次后按剩余次数委托二次/三次（预解式路径跳过枚举）；
 *   无有理根走原笛卡尔分解
 * 消去三次项：x = y − b/(4a) → y⁴ + py² + qy + r = 0
 * q ≈ 0 退化：双二次 z² + pz + r = 0（z = y²），逐 z 用 cSqrt 开方
 * 一般情形：预解三次 8m³ + 8pm² + (2p²−8r)m − q² = 0（系数已推导核验：
 *   设 (y²+αy+β)(y²−αy+γ)，比对系数得 β+γ=p+2m、α(γ−β)=q、βγ=r，
 *   消去 βγ 得 2m(p+2m)²−q²=8mr，展开即上式），取最大实根 m（f(0)=−q²≤0
 *   保证存在非负实根），α=√(2m)，β=m+p/2−q/(2α)，γ=m+p/2+q/(2α)，
 *   两个二次因式的 4 个 y 根合成后平移回 x
 * 注意：预解/因式二次复用 _coeffs 传参（原系数已缓存为局部标量），
 *       求解完成后 _coeffs 不再是原四次系数
 */
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

  /* 有理根快路径（0 = 无有理根，_coeffs 未动，继续笛卡尔分解） */
  var kRat = ratRootDescent(4);
  if (kRat > 0) { return ratRootFinish(kRat, 4); }

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
    /* ---- 双二次退化路径：z² + pz + r = 0，z = y² ----
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
    /* ---- 一般路径：预解三次 → 待定系数分解 ----
     * 内存策略：m/α/β/γ 全为标量；两因式根用 _rootRe[0..3] 就地接力
     * （因式1 根暂存 [2..3]，因式2 根落 [0..1]），零临时数组 */
    _coeffs[0] = -q * q / 8.0;
    _coeffs[1] = p * p * 0.25 - r;
    _coeffs[2] = p;
    _coeffs[3] = 1.0;
    solveCubic(1);                           /* 预解式：派生浮点系数，跳过有理根枚举 */
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

  /* ---- 平移回 x = y + off；四次方程不支持精确形式 ---- */
  for (i = 0; i < 4; i++) {
    _rootRe[i] = _rootRe[i] + off;
    _rootExactValid[i] = 0;
  }
  return 0;
}

/* ==================== 模块四：线性方程组求解器（列主元高斯消元） ====================
 * 输入 matrixPool 前 n·(n+1) 个元素：增广矩阵（行主序，1 ≤ n ≤ 12）
 * 列主元选取 → 行交换 → 消元 → 回代，结果写 _rootRe[0..n-1]
 * 主元 |·| < 1e-12 → 奇异，_solveError = 3
 * 内存策略：原地作用于 matrixPool（156 = 12×13），零复制零分配
 */
function solveLinearSystem(n) {
  _rootCount = 0;
  _solveError = 0;
  if (n < 1 || n > 12) { _solveError = 3; return _solveError; }

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

/* ==================== 模块五：残差回验 ====================
 * 对每个实根（_rootIm[i] === 0）代回原方程 f(x)（evalEquationAt，基于 tokenPool）
 * 相对残差 = |f(root)| / max(|f(root−δ)|, |f(root+δ)|, 1e-300)，
 * δ = |root| > 10 时取 0.1·|root| 否则 1 —— 邻域值自然反映系数整体量级，
 * 采样拟合失真（如 sin(x)=0 被拟合成二次）时邻域值 O(1) 而残差 O(1)，比值近 1；
 * 真 根时残差为浮点噪声，比值 ~1e-12
 * 只记录 _residualMax 不拦截，UI 层据 > 1e-6 决定是否提示"结果可能不准确"
 */
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

/* ==================== 模块六：OOM 安全防护与异常拦截 ====================
 * 求解前置门禁：全部只读扫描 tokenPool，零分配
 */

/* Token 流体检：括号嵌套 ≤ 6、函数调用 ≤ 4、变量出现 ≤ 10（64KB 版阈值翻倍，128KB 宽容度）
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
      if (depth > 6) { _solveError = 2; return 1; }
    } else if (t === T_RP) {
      depth--;
    } else if (t === T_FUNC) {
      funcCount++;
      if (funcCount > 4) { _solveError = 2; return 1; }
    } else if (t === T_VAR) {
      varCount++;
      if (varCount > 10) { _solveError = 2; return 1; }
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

/* 多元非线性门禁：检测到 ≥ 2 个不同变量且含非线性运算（^ 或函数调用）拒绝
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

/* 内存水位门禁：堆使用 > 96KB（128KB × 75%）拒绝求解
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
           _rootExactRat.byteLength + _rootExactRadNum.byteLength +
           _rootExactRad.byteLength + _rootExactDen.byteLength +
           _rootExactExp.byteLength + _rootExactValid.byteLength +
           _baseBuf.byteLength +
           (tokenCount << 3) + (numberCount << 3) + 2048;
  }
  if (used > 98304) { _solveError = 4; return 1; }
  return 0;
}

/* ==================== 模块七：复位 ====================
 * 每轮求解前调用（内存策略：只清标量与预分配数组，零分配；
 * 根结果池 12 槽全覆盖，避免上一轮线性方程组的解残留） */
function solversReset() {
  _rootCount = 0;
  var i = 0;
  for (i = 0; i < 12; i++) {
    _rootRe[i] = 0.0;
    _rootIm[i] = 0.0;
    _rootExactRat[i] = 0.0;
    _rootExactRadNum[i] = 0.0;
    _rootExactRad[i] = 1.0;
    _rootExactDen[i] = 1.0;
    _rootExactExp[i] = 0.0;
    _rootExactValid[i] = 0;
  }
  _solveError = 0;
  _residualMax = 0.0;
}

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

/* ApolloCalc-CAS · JS/128KB · core_multivar.js
 * 二元稀疏多项式（x, y）+ 高阶三角/复数补充，自包含。
 *
 * exps 编码与 C 版 ac_pro_term_t 一致：低 4 bit = y，高 4 bit = x；
 * MAX_TOTAL_DEG = 6 时不同单项式正好 N(6) = C(8,2) = 28。
 *
 * 不变式（_normalizeInPlace 保证）：exps 降序、零系数不存、同 exps 唯一。
 * 零 GC：6 个 TypedArray 加载期一次分配，之后求值/解析/运算/格式化全路径不再分配。
 * 槽位：二元运算先算进 _tmpC/_tmpE 再提交 dst，所以 dst 可与 a/b 同槽。
 * 错误码（getLastError）：0 OK / 1 次数溢出 / 2 语法错 / 3 槽号非法 / 4 项表溢出。
 *
 * 导出三通道（v2.0.0 内核拼接起生效）：
 *   Node require（module.exports 赋值）/ 浏览器 <script>（顶层 var 即全局）/
 *   内核拼接与 ESM（var 进模块作用域，export 块引用 ApolloMultivar）。
 *   三通道互斥触发：typeof module 守卫在 ESM/浏览器经典脚本下短路。
 */

var ApolloMultivar = (function () {
  "use strict";

  var EXP_BITS = 4;                 /* 每个变量的指数位宽 */
  var EXP_MASK = 15;                /* 0x0F */
  var MAX_EXP = 6;                  /* 单变量指数上限，同 AC_PRO_MV_MAX_DEG */
  var MAX_TOTAL_DEG = 6;
  var MAX_TERMS = 28;               /* N(6) = C(8,2) */
  var SLOTS = 16;
  var STR_BUF_LEN = 1024;
  var TMP_CAP = MAX_TERMS * 2;      /* 加减法暂存可能到 56 项 */

  /* 项池扁平排布，槽 s 的区间是 [s*MAX_TERMS, ...) */
  var _coef = new Float64Array(SLOTS * MAX_TERMS);
  var _exps = new Int32Array(SLOTS * MAX_TERMS);
  var _cnt = new Int32Array(SLOTS);

  var _strBuf = new Int32Array(STR_BUF_LEN);

  var _tmpC = new Float64Array(TMP_CAP);
  var _tmpE = new Int32Array(TMP_CAP);

  /* 6 次 new 之后不再增长；用普通 number 而非对象，测试可直接校验。 */
  var _allocCount = 6;

  var _opCount = 0;                 /* 热路径调用次数 */
  var _err = 0;                     /* 最近一次操作的错误码 */
  var _strLen = 0;

  function encodeExps(xdeg, ydeg) {
    return ((xdeg & EXP_MASK) << EXP_BITS) | (ydeg & EXP_MASK);
  }

  function decodeX(exps) {
    return (exps >> EXP_BITS) & EXP_MASK;
  }

  function decodeY(exps) {
    return exps & EXP_MASK;
  }

  function _validSlot(s) {
    /* (s|0)===s 顺带挡掉 NaN / 小数 / 字符串 */
    return s >= 0 && s < SLOTS && (s | 0) === s;
  }

  function _isSpace(ch) {
    return ch === 32 || ch === 9 || ch === 10 || ch === 13;
  }

  /* 就地归一化任意 (arrC, arrE, n, base) 视图，槽位与暂存区共用这一份实现。
   * 插入排序而非快排：28 项上限下最坏 378 次比较，且无递归（零 GC 红线）。
   * 返回归一化后的项数。 */
  function _normalizeInPlace(arrC, arrE, n, base) {
    var i, j, kc, ke;

    for (i = 1; i < n; i++) {
      kc = arrC[base + i];
      ke = arrE[base + i];
      j = i - 1;
      while (j >= 0 && arrE[base + j] < ke) {
        arrC[base + j + 1] = arrC[base + j];
        arrE[base + j + 1] = arrE[base + j];
        j--;
      }
      arrC[base + j + 1] = kc;
      arrE[base + j + 1] = ke;
    }

    /* 排序后同 exps 必然相邻 */
    var out = 0;
    i = 0;
    while (i < n) {
      var sum = arrC[base + i];
      var ex = arrE[base + i];
      var k = i + 1;
      while (k < n && arrE[base + k] === ex) {
        sum += arrC[base + k];
        k++;
      }
      if (sum !== 0) {
        arrC[base + out] = sum;
        arrE[base + out] = ex;
        out++;
      }
      i = k;
    }
    return out;
  }

  function _commitTmp(slot, n) {
    var b = slot * MAX_TERMS;
    var i;
    for (i = 0; i < n; i++) {
      _coef[b + i] = _tmpC[i];
      _exps[b + i] = _tmpE[i];
    }
    _cnt[slot] = n;
    return n;
  }

  /* 追加一项，不去重。满 56 项就先归一化腾空间，仍满则 _err = 4。 */
  function _tmpPush(m, c, ex) {
    if (m >= TMP_CAP) {
      m = _normalizeInPlace(_tmpC, _tmpE, m, 0);
      if (m >= TMP_CAP) { _err = 4; return -1; }
    }
    _tmpC[m] = c;
    _tmpE[m] = ex;
    return m + 1;
  }

  function _emitChar(code) {
    if (_strLen < STR_BUF_LEN) { _strBuf[_strLen] = code; _strLen++; }
  }

  /* 无符号整数 → 字符码：逆序写入后原地反转，不借助数组。 */
  function _emitUint(v) {
    if (!(v >= 1)) {                 /* 0 / NaN / 负数兜底 */
      _emitChar(48);                 /* '0' */
      return;
    }
    var start = _strLen;
    while (v >= 1) {
      _emitChar(48 + (v % 10));
      v = Math.floor(v / 10);
    }
    var a = start;
    var b = _strLen - 1;
    while (a < b) {
      var t = _strBuf[a];
      _strBuf[a] = _strBuf[b];
      _strBuf[b] = t;
      a++;
      b--;
    }
  }

  /* double → 字符码。小数取 12 位后裁掉尾零：0.1+0.2 这类二进制噪声
   * 在 1e-12 处被裁掉，输出仍是 "0.3"。 */
  function _emitNumber(v) {
    if (v !== v) {                   /* NaN */
      _emitChar(78); _emitChar(97); _emitChar(78);
      return;
    }
    if (v === Infinity) { _emitChar(73); _emitChar(110); _emitChar(102); return; }
    if (v === -Infinity) { _emitChar(45); _emitChar(73); _emitChar(110); _emitChar(102); return; }

    if (v < 0) { _emitChar(45); v = -v; }
    var ip = Math.floor(v);
    _emitUint(ip);

    var frac = v - ip;
    if (frac > 0) {
      var scaled = Math.round(frac * 1e12);
      if (scaled >= 1e12) { scaled = 1e12 - 1; }   /* 极端进位夹断 */
      var digits = 12;
      while (digits > 0 && scaled % 10 === 0) {
        scaled = scaled / 10;
        digits--;
      }
      if (digits > 0) {
        _emitChar(46);
        var div = 1;
        var t;
        for (t = 1; t < digits; t++) { div = div * 10; }
        while (div >= 1) {
          _emitChar(48 + (Math.floor(scaled / div) % 10));
          div = div / 10;
        }
      }
    }
  }

  function clear(slot) {
    _err = 0;
    if (!_validSlot(slot)) { _err = 3; return -1; }
    _cnt[slot] = 0;
    return 0;
  }

  function normalize(slot) {
    if (!_validSlot(slot)) { _err = 3; return -1; }
    _opCount++;
    var n = _normalizeInPlace(_coef, _exps, _cnt[slot], slot * MAX_TERMS);
    _cnt[slot] = n;
    return n;
  }

  /* 只排序不合并，区别于 normalize。 */
  function sortTerms(slot) {
    _err = 0;
    if (!_validSlot(slot)) { _err = 3; return -1; }
    _opCount++;
    var b = slot * MAX_TERMS;
    var n = _cnt[slot];
    var i, j, kc, ke;
    for (i = 1; i < n; i++) {
      kc = _coef[b + i];
      ke = _exps[b + i];
      j = i - 1;
      while (j >= 0 && _exps[b + j] < ke) {
        _coef[b + j + 1] = _coef[b + j];
        _exps[b + j + 1] = _exps[b + j];
        j--;
      }
      _coef[b + j + 1] = kc;
      _exps[b + j + 1] = ke;
    }
    return n;
  }

  function termCount(slot) {
    if (!_validSlot(slot)) { return -1; }
    return _cnt[slot];
  }

  function getCoef(slot, i) {
    if (!_validSlot(slot) || i < 0 || i >= _cnt[slot]) { return NaN; }
    return _coef[slot * MAX_TERMS + i];
  }

  function getExps(slot, i) {
    if (!_validSlot(slot) || i < 0 || i >= _cnt[slot]) { return -1; }
    return _exps[slot * MAX_TERMS + i];
  }

  function setTerm(slot, i, c, ex) {
    _err = 0;
    if (!_validSlot(slot) || i < 0 || i >= MAX_TERMS) { _err = 3; return -1; }
    var xd = decodeX(ex);
    var yd = decodeY(ex);
    if (xd > MAX_EXP || yd > MAX_EXP || xd + yd > MAX_TOTAL_DEG) { _err = 1; return -1; }
    _coef[slot * MAX_TERMS + i] = c;
    _exps[slot * MAX_TERMS + i] = ex;
    if (i + 1 > _cnt[slot]) { _cnt[slot] = i + 1; }
    return 0;
  }

  /* 零多项式返回 -1，对应数学上的 -∞。 */
  function totalDegree(slot) {
    if (!_validSlot(slot)) { return -1; }
    var b = slot * MAX_TERMS;
    var n = _cnt[slot];
    var d = -1;
    var i;
    for (i = 0; i < n; i++) {
      var e = _exps[b + i];
      var t = decodeX(e) + decodeY(e);
      if (t > d) { d = t; }
    }
    return d;
  }

  function degreeX(slot) {
    if (!_validSlot(slot)) { return -1; }
    var b = slot * MAX_TERMS;
    var n = _cnt[slot];
    var d = -1;
    var i;
    for (i = 0; i < n; i++) {
      var x = decodeX(_exps[b + i]);
      if (x > d) { d = x; }
    }
    return d;
  }

  function degreeY(slot) {
    if (!_validSlot(slot)) { return -1; }
    var b = slot * MAX_TERMS;
    var n = _cnt[slot];
    var d = -1;
    var i;
    for (i = 0; i < n; i++) {
      var y = decodeY(_exps[b + i]);
      if (y > d) { d = y; }
    }
    return d;
  }

  /* src === dst 时是 no-op，不归一化。 */
  function copy(src, dst) {
    _err = 0;
    if (!_validSlot(src) || !_validSlot(dst)) { _err = 3; return -1; }
    var bs = src * MAX_TERMS;
    var bd = dst * MAX_TERMS;
    var n = _cnt[src];
    var i;
    if (src !== dst) {
      for (i = 0; i < n; i++) {
        _coef[bd + i] = _coef[bs + i];
        _exps[bd + i] = _exps[bs + i];
      }
      _cnt[dst] = n;
    }
    return _cnt[dst];
  }

  /* 文法（2 变量、整数指数、十进制系数、隐式乘法）：
   *   poly     := [sign] monomial { ('+'|'-') monomial }
   *   monomial := [number] { '*'? (number | var ['^' uint]) }
   *   var      := 'x' | 'y'
   * 全程线性扫描无递归，写暂存区后统一归一化再提交，
   * 因此解析失败时目标槽位保持为空，不留半成品。
   *
   * 下面几个 _parse* 用模块级变量回传结果（_pcVal / _peVal / _pm*），
   * 是为了不构造返回对象。 */

  var _pcVal = 0;
  function _parseCoeff(s, i) {
    var n = s.length;
    var mant = 0;
    var dec = 0;
    var inFrac = 0;
    var seen = 0;
    while (i < n) {
      var ch = s.charCodeAt(i);
      if (ch >= 48 && ch <= 57) {
        mant = mant * 10 + (ch - 48);
        if (inFrac) { dec++; }
        seen = 1;
        i++;
      } else if (ch === 46 && inFrac === 0) {
        inFrac = 1;
        i++;
      } else {
        break;
      }
    }
    if (!seen) { return -1; }
    if (dec > 0) {
      var p = 1;
      var k;
      for (k = 0; k < dec; k++) { p = p * 10; }
      _pcVal = mant / p;
    } else {
      _pcVal = mant;
    }
    return i;
  }

  var _peVal = 1;
  function _parsePowOpt(s, i) {
    var n = s.length;
    var j = i;
    while (j < n && _isSpace(s.charCodeAt(j))) { j++; }
    if (j >= n || s.charCodeAt(j) !== 94) {   /* 94 = '^' */
      _peVal = 1;
      return i;                                /* 未消费空格，外层会跳过 */
    }
    j++;
    while (j < n && _isSpace(s.charCodeAt(j))) { j++; }
    if (j >= n) { return -1; }
    var d = s.charCodeAt(j);
    if (d < 48 || d > 57) { return -1; }
    var v = 0;
    while (j < n) {
      d = s.charCodeAt(j);
      if (d < 48 || d > 57) { break; }
      v = v * 10 + (d - 48);
      j++;
    }
    _peVal = v;
    return j;
  }

  var _pmCoef = 0;
  var _pmX = 0;
  var _pmY = 0;
  function _parseMonomial(s, i) {
    var n = s.length;
    var c = 1;
    var xd = 0;
    var yd = 0;
    var got = 0;

    /* 前导系数可省：x 等价于 1x */
    if (i < n) {
      var ch = s.charCodeAt(i);
      if ((ch >= 48 && ch <= 57) || ch === 46) {
        i = _parseCoeff(s, i);
        if (i < 0) { _err = 2; return -1; }
        c = _pcVal;
        got = 1;
      }
    }

    for (;;) {
      var j = i;
      while (j < n && _isSpace(s.charCodeAt(j))) { j++; }

      if (j < n && s.charCodeAt(j) === 42) {   /* 42 = '*' */
        j++;
        while (j < n && _isSpace(s.charCodeAt(j))) { j++; }
        if (j >= n) { _err = 2; return -1; }
        var chm = s.charCodeAt(j);
        if ((chm >= 48 && chm <= 57) || chm === 46) {
          /* x*2 这类数字因子，累乘进系数 */
          i = _parseCoeff(s, j);
          if (i < 0) { _err = 2; return -1; }
          c = c * _pcVal;
          got = 1;
          continue;
        }
        i = j;
      } else {
        i = j;
      }

      if (i >= n) { break; }
      var chv = s.charCodeAt(i);
      if (chv !== 120 && chv !== 121) { break; }   /* 120='x', 121='y' */

      i++;
      i = _parsePowOpt(s, i);
      if (i < 0) { _err = 2; return -1; }
      var e = _peVal;
      if (e > MAX_EXP) { _err = 1; return -1; }
      if (chv === 120) { xd += e; } else { yd += e; }
      if (xd > MAX_EXP || yd > MAX_EXP) { _err = 1; return -1; }
      got = 1;
    }

    if (!got) { _err = 2; return -1; }
    if (xd + yd > MAX_TOTAL_DEG) { _err = 1; return -1; }
    _pmCoef = c;
    _pmX = xd;
    _pmY = yd;
    return i;
  }

  function parse(src, slot) {
    _err = 0;
    if (!_validSlot(slot)) { _err = 3; return -1; }
    _cnt[slot] = 0;                              /* 失败时目标槽位保持空 */
    if (typeof src !== "string" || src.length === 0) { _err = 2; return -1; }

    var n = src.length;
    var i = 0;
    var m = 0;
    var first = 1;
    var sawTerm = 0;

    for (;;) {
      while (i < n && _isSpace(src.charCodeAt(i))) { i++; }
      if (i >= n) { break; }

      var sign = 1;
      var ch = src.charCodeAt(i);
      if (ch === 43) {                           /* '+' */
        i++;
      } else if (ch === 45) {                    /* '-' */
        sign = -1;
        i++;
      } else if (!first) {
        _err = 2; return -1;                     /* 缺运算符 */
      }
      while (i < n && _isSpace(src.charCodeAt(i))) { i++; }
      if (i >= n) { _err = 2; return -1; }       /* 悬空符号 */

      i = _parseMonomial(src, i);
      if (i < 0) { return -1; }                  /* _err 已由子解析器置位 */

      m = _tmpPush(m, sign * _pmCoef, encodeExps(_pmX, _pmY));
      if (m < 0) { return -1; }
      sawTerm = 1;
      first = 0;

      var k = i;
      while (k < n && _isSpace(src.charCodeAt(k))) { k++; }
      if (k < n) {
        var chn = src.charCodeAt(k);
        if (chn !== 43 && chn !== 45) { _err = 2; return -1; }
      }
      i = k;
    }

    if (!sawTerm) { _err = 2; return -1; }

    var out = _normalizeInPlace(_tmpC, _tmpE, m, 0);
    if (out > MAX_TERMS) { _err = 4; return -1; }
    return _commitTmp(slot, out);
  }

  function evalSlot(slot, x, y) {
    if (!_validSlot(slot)) { _err = 3; return NaN; }
    _opCount++;
    var b = slot * MAX_TERMS;
    var n = _cnt[slot];
    var sum = 0;
    var i, k;
    for (i = 0; i < n; i++) {
      var c = _coef[b + i];
      if (c === 0) { continue; }                 /* 不变式保证不存在，防御性跳过 */
      var e = _exps[b + i];
      var xd = (e >> EXP_BITS) & EXP_MASK;
      var yd = e & EXP_MASK;
      var px = 1;
      for (k = 0; k < xd; k++) { px = px * x; }
      var py = 1;
      for (k = 0; k < yd; k++) { py = py * y; }
      sum += c * px * py;
    }
    return sum;
  }

  /* 项表拆成 coefs / exps 两个平行数组传入，避免为了一个 terms 对象
   * 而临时构造对象（零 GC 禁止）。 */
  function evalTerms(coefs, exps, n, x, y) {
    _opCount++;
    var sum = 0;
    var i, k;
    for (i = 0; i < n; i++) {
      var c = coefs[i];
      if (c === 0) { continue; }
      var e = exps[i];
      var xd = (e >> EXP_BITS) & EXP_MASK;
      var yd = e & EXP_MASK;
      var px = 1;
      for (k = 0; k < xd; k++) { px = px * x; }
      var py = 1;
      for (k = 0; k < yd; k++) { py = py * y; }
      sum += c * px * py;
    }
    return sum;
  }

  /* 3 参 = 槽位形态，5 参 = 项表形态。按 arguments.length 分派，不构造对象。 */
  function evalPoly(a, b, c, d, e) {
    if (typeof a === "number" && arguments.length === 3) {
      return evalSlot(a, b, c);
    }
    return evalTerms(a, b, c, d, e);
  }

  function _addSub(a, b, dst, bsign) {
    _err = 0;
    if (!_validSlot(a) || !_validSlot(b) || !_validSlot(dst)) { _err = 3; return -1; }
    _opCount++;
    var m = 0;
    var ba = a * MAX_TERMS;
    var bb = b * MAX_TERMS;
    var i;
    var na = _cnt[a];
    var nb = _cnt[b];
    for (i = 0; i < na; i++) { _tmpC[m] = _coef[ba + i]; _tmpE[m] = _exps[ba + i]; m++; }
    for (i = 0; i < nb; i++) { _tmpC[m] = bsign * _coef[bb + i]; _tmpE[m] = _exps[bb + i]; m++; }
    var out = _normalizeInPlace(_tmpC, _tmpE, m, 0);
    if (out > MAX_TERMS) { _err = 4; return -1; }
    return _commitTmp(dst, out);
  }

  function add(a, b, dst) { return _addSub(a, b, dst, 1); }
  function sub(a, b, dst) { return _addSub(a, b, dst, -1); }

  /* 卷积 + 边算边合并，同 C 版 mv_mul 的策略。 */
  function mul(a, b, dst) {
    _err = 0;
    if (!_validSlot(a) || !_validSlot(b) || !_validSlot(dst)) { _err = 3; return -1; }
    _opCount++;
    var ba = a * MAX_TERMS;
    var bb = b * MAX_TERMS;
    var na = _cnt[a];
    var nb = _cnt[b];
    var m = 0;
    var i, j, k;

    for (i = 0; i < na; i++) {
      var ca = _coef[ba + i];
      var ea = _exps[ba + i];
      var ax = (ea >> EXP_BITS) & EXP_MASK;
      var ay = ea & EXP_MASK;
      for (j = 0; j < nb; j++) {
        var cb = _coef[bb + j];
        var eb = _exps[bb + j];
        var nx = ax + ((eb >> EXP_BITS) & EXP_MASK);
        var ny = ay + (eb & EXP_MASK);
        if (nx > MAX_EXP || ny > MAX_EXP || nx + ny > MAX_TOTAL_DEG) {
          _err = 1; return -1;
        }
        var prod = ca * cb;
        var ex = (nx << EXP_BITS) | ny;
        var found = 0;
        for (k = 0; k < m; k++) {
          if (_tmpE[k] === ex) { _tmpC[k] += prod; found = 1; break; }
        }
        if (!found) {
          if (m >= TMP_CAP) { _err = 4; return -1; }
          _tmpC[m] = prod;
          _tmpE[m] = ex;
          m++;
        }
      }
    }

    var out = _normalizeInPlace(_tmpC, _tmpE, m, 0);
    if (out > MAX_TERMS) { _err = 4; return -1; }
    return _commitTmp(dst, out);
  }

  /* dst = a × (c · x^xd · y^yd)，一次遍历，无卷积。 */
  function mulMonomial(a, c, xd, yd, dst) {
    _err = 0;
    if (!_validSlot(a) || !_validSlot(dst)) { _err = 3; return -1; }
    if (xd < 0 || yd < 0 || xd > MAX_EXP || yd > MAX_EXP || xd + yd > MAX_TOTAL_DEG) {
      _err = 1; return -1;
    }
    _opCount++;
    var ba = a * MAX_TERMS;
    var n = _cnt[a];
    var m = 0;
    var i;
    for (i = 0; i < n; i++) {
      var ea = _exps[ba + i];
      var nx = ((ea >> EXP_BITS) & EXP_MASK) + xd;
      var ny = (ea & EXP_MASK) + yd;
      if (nx > MAX_EXP || ny > MAX_EXP || nx + ny > MAX_TOTAL_DEG) {
        _err = 1; return -1;
      }
      m = _tmpPush(m, _coef[ba + i] * c, (nx << EXP_BITS) | ny);
      if (m < 0) { return -1; }
    }
    var out = _normalizeInPlace(_tmpC, _tmpE, m, 0);
    if (out > MAX_TERMS) { _err = 4; return -1; }
    return _commitTmp(dst, out);
  }

  function scale(a, k, dst) {
    _err = 0;
    if (!_validSlot(a) || !_validSlot(dst)) { _err = 3; return -1; }
    _opCount++;
    var ba = a * MAX_TERMS;
    var bd = dst * MAX_TERMS;
    var n = _cnt[a];
    var i;
    var out = 0;
    for (i = 0; i < n; i++) {
      var v = _coef[ba + i] * k;
      if (v !== 0) {
        _coef[bd + out] = v;
        _exps[bd + out] = _exps[ba + i];
        out++;
      }
    }
    _cnt[dst] = out;
    return out;
  }

  function neg(a, dst) { return scale(a, -1, dst); }

  /* ==================== 多变量符号求导（v2.0.0，对齐 C 侧 ac_pro_diff_multi）====================
   * 逐项幂法则：d/dx(c·x^a·y^b) = (c·a)·x^(a-1)·y^b，a === 0 的项整体丢弃（d/dy 同理）。
   * 结果总次数严格下降 → 不会触发项表溢出；系数乘指数 ≤ 6 倍，double 无溢出。
   * 有序性保持：被求导项 exps 同减 1<<位宽（保序保唯一），未求导项全部丢弃，
   * 故结果仍降序且同 exps 唯一，_normalizeInPlace 在此只是防御性过一遍。
   * 槽位纪律同二元运算：先算进 _tmpC/_tmpE 再提交 dst，dst 可与 a 同槽。
   * varIndex：0 = 对 x 求导，1 = 对 y 求导；其他值 _err = 2（语法/参数错类）。
   * 返回 dst 项数（常数的导数 = 0 项 = "0" 多项式），参数/槽位错误返回 -1。 */
  function diff(a, varIndex, dst) {
    _err = 0;
    if (varIndex !== 0 && varIndex !== 1) { _err = 2; return -1; }
    if (!_validSlot(a) || !_validSlot(dst)) { _err = 3; return -1; }
    _opCount++;
    var ba = a * MAX_TERMS;
    var n = _cnt[a];
    var m = 0;
    var shift = varIndex === 0 ? EXP_BITS : 0;
    var i;
    for (i = 0; i < n; i++) {
      var e = _exps[ba + i];
      var d = (e >> shift) & EXP_MASK;
      if (d === 0) { continue; }
      m = _tmpPush(m, _coef[ba + i] * d, e - (1 << shift));
      if (m < 0) { return -1; }
    }
    return _commitTmp(dst, _normalizeInPlace(_tmpC, _tmpE, m, 0));
  }

  /* 写 _strBuf 并返回字符数，不返回字符串。输出格式是外部契约：
   * 降幂、系数 1 省略、项间 " + " / " - "、空多项式输出 "0"。 */
  function toString(slot) {
    _err = 0;
    _strLen = 0;
    if (!_validSlot(slot)) { _err = 3; return 0; }
    _opCount++;
    var b = slot * MAX_TERMS;
    var n = _cnt[slot];
    if (n === 0) { _emitChar(48); return _strLen; }   /* '0' */

    var i;
    for (i = 0; i < n; i++) {
      var c = _coef[b + i];
      var e = _exps[b + i];
      var xd = (e >> EXP_BITS) & EXP_MASK;
      var yd = e & EXP_MASK;
      var isNeg = c < 0;

      if (i === 0) {
        if (isNeg) { _emitChar(45); }
      } else {
        _emitChar(32);
        _emitChar(isNeg ? 45 : 43);
        _emitChar(32);
      }

      var a = isNeg ? -c : c;
      if (a !== 1 || (xd === 0 && yd === 0)) { _emitNumber(a); }
      if (xd > 0) {
        _emitChar(120);
        if (xd > 1) { _emitChar(94); _emitUint(xd); }
      }
      if (yd > 0) {
        _emitChar(121);
        if (yd > 1) { _emitChar(94); _emitUint(yd); }
      }
    }
    return _strLen;
  }

  /* 出口才拼串，UI 层调用一次，不在热路径内。 */
  function getString(len) {
    var n = len;
    if (n === undefined || n < 0 || n > _strLen) { n = _strLen; }
    var s = "";
    var i;
    for (i = 0; i < n; i++) { s = s + String.fromCharCode(_strBuf[i]); }
    return s;
  }

  function polyToString(slot) {
    return getString(toString(slot));
  }

  function charAt(i) {
    if (i < 0 || i >= _strLen) { return -1; }
    return _strBuf[i];
  }

  /* 返回单字符字符串，供测试直观比对；不在热路径内。 */
  function charAtStr(i) {
    if (i < 0 || i >= _strLen) { return ""; }
    return String.fromCharCode(_strBuf[i]);
  }

  function sec(x) { return 1 / Math.cos(x); }
  function csc(x) { return 1 / Math.sin(x); }
  function cot(x) { return 1 / Math.tan(x); }

  function asec(x) {
    if (x > -1 && x < 1) { return NaN; }
    return Math.acos(1 / x);
  }

  function acsc(x) {
    if (x > -1 && x < 1) { return NaN; }
    return Math.asin(1 / x);
  }

  /* 值域 (0, π)。用 atan2(1, x) 一次取到正确分支，
   * 避免 atan(1/x) 在 x<0 时落到 (-π/2, 0) 的错误区间。 */
  function acot(x) { return Math.atan2(1, x); }

  function sech(x) { return 1 / Math.cosh(x); }
  function csch(x) { return 1 / Math.sinh(x); }
  function coth(x) { return 1 / Math.tanh(x); }

  /* 自实现，不依赖 Math.asinh 等新 API。x<0 时借对称性保精度。 */
  function asinh(x) {
    var ax = x < 0 ? -x : x;
    var r = Math.log(ax + Math.sqrt(ax * ax + 1));
    return x < 0 ? -r : r;
  }

  function acosh(x) {
    if (x < 1) { return NaN; }
    return Math.log(x + Math.sqrt(x * x - 1));
  }

  function atanh(x) {
    if (x <= -1 || x >= 1) {
      if (x === 1) { return Infinity; }
      if (x === -1) { return -Infinity; }
      return NaN;
    }
    return 0.5 * Math.log((1 + x) / (1 - x));
  }

  /* 复数域用寄存器而非对象传递，才能保持零分配。
   * _zRe/_zIm 是操作数，_rRe/_rIm 是结果；
   * _sqrtOf/_lnOf/_expOf 各写自己的结果寄存器，调用方必须先拷出再调下一个，
   * 否则后一次 helper 会踩掉前一次的结果。 */

  var _zRe = 0, _zIm = 0;
  var _rRe = 0, _rIm = 0;
  var _sqRe = 0, _sqIm = 0;
  var _lgRe = 0, _lgIm = 0;
  var _exRe = 0, _exIm = 0;

  function setZ(re, im) { _zRe = re; _zIm = im; }
  /* 把结果寄存器搬回操作数寄存器，供链式调用（cExp 后再 cLn）用。 */
  function setZFromResult() { _zRe = _rRe; _zIm = _rIm; }
  function getZRe() { return _zRe; }
  function getZIm() { return _zIm; }
  function getRRe() { return _rRe; }
  function getRIm() { return _rIm; }

  /* 主值平方根，同 core_complex.js 的 cSqrt。 */
  function _sqrtOf(re, im) {
    if (re === 0 && im === 0) { _sqRe = 0; _sqIm = 0; return; }
    if (im === 0) {
      if (re >= 0) { _sqRe = Math.sqrt(re); _sqIm = 0; }
      else { _sqRe = 0; _sqIm = Math.sqrt(-re); }
      return;
    }
    var r = Math.sqrt(re * re + im * im);
    var half = Math.atan2(im, re) * 0.5;
    var sr = Math.sqrt(r);
    _sqRe = sr * Math.cos(half);
    _sqIm = sr * Math.sin(half);
  }

  function _lnOf(re, im) {
    if (re === 0 && im === 0) { _lgRe = -Infinity; _lgIm = 0; return; }
    _lgRe = 0.5 * Math.log(re * re + im * im);
    _lgIm = Math.atan2(im, re);
  }

  function _expOf(re, im) {
    var ea = Math.exp(re);
    _exRe = ea * Math.cos(im);
    _exIm = ea * Math.sin(im);
  }

  function cSqrt() { _sqrtOf(_zRe, _zIm); _rRe = _sqRe; _rIm = _sqIm; }
  function cExp() { _expOf(_zRe, _zIm); _rRe = _exRe; _rIm = _exIm; }
  function cLn() { _lnOf(_zRe, _zIm); _rRe = _lgRe; _rIm = _lgIm; }

  /* 平方-乘法快速幂，负指数先取倒数。 */
  function cPowInt(n) {
    var bRe = _zRe;
    var bIm = _zIm;
    var exp = n;
    if (exp !== (exp | 0)) { _rRe = NaN; _rIm = NaN; return; }

    if (exp < 0) {
      var d = bRe * bRe + bIm * bIm;
      if (d === 0) { _rRe = NaN; _rIm = NaN; return; }
      var iRe = bRe / d;
      var iIm = -bIm / d;
      bRe = iRe;
      bIm = iIm;
      exp = -exp;
    }

    var accRe = 1;
    var accIm = 0;
    var e = exp;
    for (;;) {
      if ((e & 1) === 1) {
        var nRe = accRe * bRe - accIm * bIm;
        var nIm = accRe * bIm + accIm * bRe;
        accRe = nRe;
        accIm = nIm;
      }
      e = e >> 1;
      if (e === 0) { break; }
      var sqRe = bRe * bRe - bIm * bIm;
      var sqIm = 2 * bRe * bIm;
      bRe = sqRe;
      bIm = sqIm;
    }
    _rRe = accRe;
    _rIm = accIm;
  }

  function cAsinh() {
    var re = _zRe;
    var im = _zIm;
    var z2re = re * re - im * im;
    var z2im = 2 * re * im;
    _sqrtOf(z2re + 1, z2im);
    var sr = _sqRe;
    var si = _sqIm;
    _lnOf(re + sr, im + si);
    _rRe = _lgRe;
    _rIm = _lgIm;
  }

  function cAcosh() {
    var re = _zRe;
    var im = _zIm;
    var z2re = re * re - im * im;
    var z2im = 2 * re * im;
    _sqrtOf(z2re - 1, z2im);
    var sr = _sqRe;
    var si = _sqIm;
    _lnOf(re + sr, im + si);
    _rRe = _lgRe;
    _rIm = _lgIm;
  }

  /* z=1 是极点，返回 ±Infinity 由上层判错。 */
  function cAtanh() {
    var re = _zRe;
    var im = _zIm;
    var ar = 1 + re;
    var ai = im;
    var br = 1 - re;
    var bi = -im;
    var d = br * br + bi * bi;
    if (d === 0) { _rRe = Infinity; _rIm = 0; return; }
    var qr = (ar * br + ai * bi) / d;
    var qi = (ai * br - ar * bi) / d;
    _lnOf(qr, qi);
    _rRe = 0.5 * _lgRe;
    _rIm = 0.5 * _lgIm;
  }

  function cAbs() { return Math.sqrt(_zRe * _zRe + _zIm * _zIm); }

  /* 加载后恒为 6，热路径前后不变，测试据此确认零 GC。 */
  function allocCount() { return _allocCount; }

  function stats() { return _allocCount; }

  function getOpCount() { return _opCount; }

  function getLastError() { return _err; }

  /* 返回已有 TypedArray 引用，不分配，供测试确认已预分配。 */
  function coefBuffer() { return _coef; }
  function expsBuffer() { return _exps; }

  /* 不复位 _allocCount，分配确实发生过。 */
  function reset() {
    var i;
    for (i = 0; i < SLOTS; i++) { _cnt[i] = 0; }
    _err = 0;
    _opCount = 0;
    _strLen = 0;
    _zRe = 0; _zIm = 0; _rRe = 0; _rIm = 0;
  }

  return {
    EXP_BITS: EXP_BITS,
    EXP_MASK: EXP_MASK,
    MAX_EXP: MAX_EXP,
    MAX_TOTAL_DEG: MAX_TOTAL_DEG,
    MAX_TERMS: MAX_TERMS,
    SLOTS: SLOTS,

    encodeExps: encodeExps,
    decodeX: decodeX,
    decodeY: decodeY,

    parse: parse,
    evalPoly: evalPoly,
    evalSlot: evalSlot,
    evalTerms: evalTerms,

    clear: clear,
    normalize: normalize,
    sortTerms: sortTerms,
    termCount: termCount,
    totalDegree: totalDegree,
    degreeX: degreeX,
    degreeY: degreeY,
    getCoef: getCoef,
    getExps: getExps,
    setTerm: setTerm,
    copy: copy,

    add: add,
    sub: sub,
    mul: mul,
    mulMonomial: mulMonomial,
    scale: scale,
    neg: neg,
    diff: diff,

    toString: toString,
    getString: getString,
    polyToString: polyToString,
    charAt: charAt,
    charAtStr: charAtStr,

    sec: sec, csc: csc, cot: cot,
    asec: asec, acsc: acsc, acot: acot,
    sech: sech, csch: csch, coth: coth,
    asinh: asinh, acosh: acosh, atanh: atanh,

    setZ: setZ,
    setZFromResult: setZFromResult,
    getZRe: getZRe,
    getZIm: getZIm,
    getRRe: getRRe,
    getRIm: getRIm,
    cSqrt: cSqrt,
    cExp: cExp,
    cLn: cLn,
    cPowInt: cPowInt,
    cAsinh: cAsinh,
    cAcosh: cAcosh,
    cAtanh: cAtanh,
    cAbs: cAbs,

    allocCount: allocCount,
    stats: stats,
    getOpCount: getOpCount,
    getLastError: getLastError,
    coefBuffer: coefBuffer,
    expsBuffer: expsBuffer,
    reset: reset
  };
})();

if (typeof module === "object" && module && module.exports) {
  module.exports = ApolloMultivar;
}

/* ApolloCalc-CAS · JS/128KB · core_matrix.js
 * 基础矩阵运算：乘法 / 转置 / 行列式 / 逆矩阵（≤3×3 闭式），自包含。
 *
 * 寄存器模型：A、B 两个输入寄存器 + R 结果寄存器，各 3×3 Float64Array 行主序。
 * UI 调用序 setA/setB → mul/transpose/det/inv → getR*（与复数域
 * setComplexA → cXxx → getComplexTmp* 的契约同构）。
 *
 * 边界：行列式与逆只支持 1..3 阶方阵 —— Sarrus 法则与伴随矩阵法闭式，
 * 无消元、无递归、无选主元。12 元线性方程组的稠密消元是 C/128KB
 * ac_matrix（C 独有）的能力；本模块是浏览器侧的基础块，不是对等实现。
 *
 * 零 GC：3 个 TypedArray 加载期一次分配（3 × 72 = 216 B），全路径无
 * 对象/数组字面量、无字符串操作、无递归。
 * 错误码（getError）：0 OK / 1 维度超界（>3 或 ≤0 或下标越界）/
 * 2 乘法内维不匹配 / 3 非方阵 / 4 奇异（|det| ≤ 1e-12）。
 *
 * 导出三通道：Node require / 浏览器 <script> 顶层 var / 内核拼接 ESM。
 */

var ApolloMatrix = (function () {
  "use strict";

  var MAX_DIM = 3;
  var CAP = MAX_DIM * MAX_DIM;      /* 9 */
  var SINGULAR_TOL = 1e-12;         /* 与 C/128KB ac_matrix 的主元阈值同值 */

  var _a = new Float64Array(CAP);   /* 输入寄存器 A（m×k） */
  var _b = new Float64Array(CAP);   /* 输入寄存器 B（k×n） */
  var _r = new Float64Array(CAP);   /* 结果寄存器 R */

  var _rowsA = 0, _colsA = 0;
  var _rowsB = 0, _colsB = 0;
  var _rowsR = 0, _colsR = 0;

  var _err = 0;
  var _opCount = 0;
  var _allocCount = 3;
  var _det = 0;                     /* det()/inv() 最近一次的行列式值 */

  function allocCount() { return _allocCount; }
  function getOpCount() { return _opCount; }
  function getError() { return _err; }

  function aBuffer() { return _a; }
  function bBuffer() { return _b; }
  function rBuffer() { return _r; }

  function _dimsOK(r, c) {
    return r >= 1 && r <= MAX_DIM && c >= 1 && c <= MAX_DIM;
  }

  /* 写输入寄存器：dims + 行主序下标一次给定，越界拒绝不写。
   * 重复调用覆盖旧值 —— 换矩阵前不要求 reset。 */
  function _setReg(arr, rr, cc, rows, cols, i, v) {
    _err = 0;
    if (!_dimsOK(rows, cols) || i < 0 || i >= rows * cols) { _err = 1; return -1; }
    rr(rows);
    cc(cols);
    arr[i] = v;
    return 0;
  }

  var _rowsASet = function (v) { _rowsA = v; };
  var _colsASet = function (v) { _colsA = v; };
  var _rowsBSet = function (v) { _rowsB = v; };
  var _colsBSet = function (v) { _colsB = v; };

  function setA(rows, cols, i, v) { return _setReg(_a, _rowsASet, _colsASet, rows, cols, i, v); }
  function setB(rows, cols, i, v) { return _setReg(_b, _rowsBSet, _colsBSet, rows, cols, i, v); }

  function getR(r, c) {
    if (r < 0 || c < 0 || r >= _rowsR || c >= _colsR) { return NaN; }
    return _r[r * _colsR + c];
  }
  function getRRows() { return _rowsR; }
  function getRCols() { return _colsR; }
  function getLastDet() { return _det; }

  function reset() {
    _rowsA = 0; _colsA = 0;
    _rowsB = 0; _colsB = 0;
    _rowsR = 0; _colsR = 0;
    _err = 0;
    _opCount = 0;
    _det = 0;
    return 0;
  }

  /* R = A × B，m×k · k×n → m×n。三重循环无递归。 */
  function mul() {
    _err = 0;
    if (!_dimsOK(_rowsA, _colsA) || !_dimsOK(_rowsB, _colsB)) { _err = 1; return -1; }
    if (_colsA !== _rowsB) { _err = 2; return -1; }
    _opCount++;
    var m = _rowsA;
    var k = _colsA;
    var n = _colsB;
    var i, j, p, s;
    for (i = 0; i < m; i++) {
      for (j = 0; j < n; j++) {
        s = 0;
        for (p = 0; p < k; p++) {
          s = s + _a[i * k + p] * _b[p * n + j];
        }
        _r[i * n + j] = s;
      }
    }
    _rowsR = m;
    _colsR = n;
    return 1;
  }

  /* R = Aᵀ，m×n → n×m。读写不相交（_a 只读，_r 只写）。 */
  function transpose() {
    _err = 0;
    if (!_dimsOK(_rowsA, _colsA)) { _err = 1; return -1; }
    _opCount++;
    var m = _rowsA;
    var n = _colsA;
    var i, j;
    for (i = 0; i < m; i++) {
      for (j = 0; j < n; j++) {
        _r[j * m + i] = _a[i * n + j];
      }
    }
    _rowsR = n;
    _colsR = m;
    return 1;
  }

  /* det(A)。行列式返回值本身可为 0（奇异是合法结果不是错误）；
   * 维度错误返回 NaN 且 _err 置因。 */
  function det() {
    _err = 0;
    if (!_dimsOK(_rowsA, _colsA)) { _err = 1; return NaN; }
    if (_rowsA !== _colsA) { _err = 3; return NaN; }
    _opCount++;
    var n = _rowsA;
    if (n === 1) { _det = _a[0]; return _det; }
    if (n === 2) { _det = _a[0] * _a[3] - _a[1] * _a[2]; return _det; }
    /* 3×3 Sarrus：a(ei − fh) − b(di − fg) + c(dh − eg) */
    _det = _a[0] * (_a[4] * _a[8] - _a[5] * _a[7])
         - _a[1] * (_a[3] * _a[8] - _a[5] * _a[6])
         + _a[2] * (_a[3] * _a[7] - _a[4] * _a[6]);
    return _det;
  }

  /* R = A⁻¹，伴随矩阵法闭式。|det| ≤ 1e-12 视为奇异拒绝（不返回近似解）。
   * 结果经 getR 读取；det 值经 getLastDet 复查。 */
  function inv() {
    _err = 0;
    if (!_dimsOK(_rowsA, _colsA)) { _err = 1; return -1; }
    if (_rowsA !== _colsA) { _err = 3; return -1; }
    _opCount++;
    var n = _rowsA;
    var d;
    var i;
    if (n === 1) {
      d = _a[0];
      if (d > -SINGULAR_TOL && d < SINGULAR_TOL) { _err = 4; return -1; }
      _det = d;
      _r[0] = 1 / d;
      _rowsR = 1;
      _colsR = 1;
      return 1;
    }
    if (n === 2) {
      d = _a[0] * _a[3] - _a[1] * _a[2];
      if (d > -SINGULAR_TOL && d < SINGULAR_TOL) { _err = 4; return -1; }
      _det = d;
      _r[0] = _a[3] / d;
      _r[1] = -_a[1] / d;
      _r[2] = -_a[2] / d;
      _r[3] = _a[0] / d;
      _rowsR = 2;
      _colsR = 2;
      return 1;
    }
    /* 3×3 代数余子式 C_ij（i 行 j 列），det = a·C00 + b·C01 + c·C02，
     * R[j][i] = C_ij / det（伴随矩阵转置关系，行主序下标 R[j*3+i]）。 */
    var c00 = _a[4] * _a[8] - _a[5] * _a[7];
    var c01 = -(_a[3] * _a[8] - _a[5] * _a[6]);
    var c02 = _a[3] * _a[7] - _a[4] * _a[6];
    var c10 = -(_a[1] * _a[8] - _a[2] * _a[7]);
    var c11 = _a[0] * _a[8] - _a[2] * _a[6];
    var c12 = -(_a[0] * _a[7] - _a[1] * _a[6]);
    var c20 = _a[1] * _a[5] - _a[2] * _a[4];
    var c21 = -(_a[0] * _a[5] - _a[2] * _a[3]);
    var c22 = _a[0] * _a[4] - _a[1] * _a[3];
    d = _a[0] * c00 + _a[1] * c01 + _a[2] * c02;
    if (d > -SINGULAR_TOL && d < SINGULAR_TOL) { _err = 4; return -1; }
    _det = d;
    _r[0] = c00 / d;
    _r[1] = c10 / d;
    _r[2] = c20 / d;
    _r[3] = c01 / d;
    _r[4] = c11 / d;
    _r[5] = c21 / d;
    _r[6] = c02 / d;
    _r[7] = c12 / d;
    _r[8] = c22 / d;
    _rowsR = 3;
    _colsR = 3;
    return 1;
  }

  return {
    MAX_DIM: MAX_DIM,

    setA: setA,
    setB: setB,
    getR: getR,
    getRRows: getRRows,
    getRCols: getRCols,
    getLastDet: getLastDet,

    mul: mul,
    transpose: transpose,
    det: det,
    inv: inv,

    reset: reset,
    getError: getError,
    getOpCount: getOpCount,
    allocCount: allocCount,
    aBuffer: aBuffer,
    bBuffer: bBuffer,
    rBuffer: rBuffer
  };
})();

if (typeof module === "object" && module && module.exports) {
  module.exports = ApolloMatrix;
}

/* ApolloCalc-CAS · JS/128KB · core_special.js
 * 高阶特殊函数：误差函数 erf、伽马函数 gamma、对数伽马 lgamma。自包含。
 *
 * 纯标量数值函数：无池、无寄存器、无递归；加载期与热路径分配均为 0
 * （Lanczos 级数与 A&S 系数全部手工展开为标量常量，无数组字面量）。
 *
 * 精度口径：
 *   erf        A&S 26.2.17 有理逼近，|ε| ≤ 1.5e-7，全域（大 |x| 自然饱和到 ±1）
 *   gamma      Lanczos g=7 / n=9，x ≥ 0.5 相对误差 ~1e-13；x < 0.5 反射公式
 *              Γ(x) = π / (sin(πx)·Γ(1−x))，手工展开、无自调用
 *              （与 C/128KB src/ac_special.c 同一条款，系数同表）
 *   lgamma     同一 Lanczos 核取对数形态，反射走 ln π − ln|sin πx| − lgamma(1−x)
 *
 * 极点契约：gamma/lgamma 在 0 与负整数处返回 +Infinity（对数伽马在极点
 * 附近本号趋向 +∞，不是 −∞，故统一 +Infinity 而非 NaN）。
 * 非有限输入：NaN 输入返回 NaN 且 _err = 1；±Infinity 有定义值不设错。
 *
 * 与 C 版的数值对齐口径：erf 双方实现路径不同（C 正项级数 / JS 有理逼近），
 * 对齐容差按各自精度下限取 2e-7；gamma/lgamma 双方同表 Lanczos，容差 1e-9。
 *
 * 导出三通道：Node require / 浏览器 <script> 顶层 var / 内核拼接 ESM。
 */

var ApolloSpecial = (function () {
  "use strict";

  var _opCount = 0;
  var _err = 0;
  var _allocCount = 0;              /* 无池：加载期零分配 */

  /* Lanczos g=7, n=9 系数（与 C/128KB ac_special.c 同表）。
   * p[0] 前置因子之外，级数项为 c_i / (z + i)，i = 1..8。 */
  var LC0 = 0.99999999999980993;
  var LC1 = 676.5203681218851;
  var LC2 = -1259.1392167224028;
  var LC3 = 771.32342877765313;
  var LC4 = -176.61502916214059;
  var LC5 = 12.507343278686905;
  var LC6 = -0.13857109526572012;
  var LC7 = 9.9843695780195716e-6;
  var LC8 = 1.5056327351493116e-7;

  var SQRT_2PI = 2.5066282746310002;      /* √(2π) */
  var LN_SQRT_2PI = 0.9189385332046727;   /* ln √(2π) */
  var LN_PI = 1.1447298858494002;         /* ln π */
  var LANCZOS_G_HALF = 7.5;               /* g + 0.5，级数位移项 z + 7.5 */

  function allocCount() { return _allocCount; }
  function getOpCount() { return _opCount; }
  function getError() { return _err; }

  function reset() {
    _opCount = 0;
    _err = 0;
    return 0;
  }

  /* A&S 26.2.17：t = 1/(1 + p·|x|)，erf = 1 − poly(t)·e^(−x²)，|ε| ≤ 1.5e-7。
   * 奇对称手工分支：erf(−x) = −erf(x)。x ≥ 6 时 poly(t)·e^(−x²) 已低于
   * 1 的半个 ulp，结果自然饱和到 ±1，无需专门分支。 */
  function erf(x) {
    _err = 0;
    _opCount++;
    if (x !== x) { _err = 1; return NaN; }
    if (x === Infinity) { return 1; }
    if (x === -Infinity) { return -1; }
    var neg = x < 0;
    var ax = neg ? -x : x;
    var t = 1 / (1 + 0.3275911 * ax);
    var y = t * (0.254829592 + t * (-0.284496736 + t * (1.421413741 +
           t * (-1.453152027 + t * 1.061405429)))) * Math.exp(-ax * ax);
    var v = 1 - y;
    return neg ? -v : v;
  }

  /* Lanczos 级数 A(z) 的对数，z = x − 1，x ≥ 0.5 保证分母全为正。 */
  function _lanczosLogA(z) {
    var a = LC0
          + LC1 / (z + 1) + LC2 / (z + 2) + LC3 / (z + 3) + LC4 / (z + 4)
          + LC5 / (z + 5) + LC6 / (z + 6) + LC7 / (z + 7) + LC8 / (z + 8);
    return Math.log(a);
  }

  /* ln Γ(x)，x ≥ 0.5。Stirling 核：ln Γ = ln √(2π) + (z+½)ln(z+g+½) − (z+g+½) + ln A(z) */
  function _lgammaHalf(x) {
    var z = x - 1;
    var t = z + LANCZOS_G_HALF;
    return LN_SQRT_2PI + (z + 0.5) * Math.log(t) - t + _lanczosLogA(z);
  }

  /* Γ(x)，x ≥ 0.5。与 _lgammaHalf 同核取指数形态（先算对数再 exp，
   * 大参数下比直接乘积形态少一次上溢风险）。 */
  function _gammaHalf(x) {
    return Math.exp(_lgammaHalf(x));
  }

  function lgamma(x) {
    _err = 0;
    _opCount++;
    if (x !== x) { _err = 1; return NaN; }
    if (x === Infinity) { return Infinity; }
    if (x === -Infinity) { _err = 1; return NaN; }
    if (x <= 0 && x === Math.floor(x)) { return Infinity; }     /* 极点 */
    if (x < 0.5) {
      /* 反射：ln|Γ(x)| = ln π − ln|sin πx| − ln Γ(1−x) */
      return LN_PI - Math.log(Math.abs(Math.sin(Math.PI * x))) - _lgammaHalf(1 - x);
    }
    return _lgammaHalf(x);
  }

  function gamma(x) {
    _err = 0;
    _opCount++;
    if (x !== x) { _err = 1; return NaN; }
    if (x === Infinity) { return Infinity; }
    if (x === -Infinity) { _err = 1; return NaN; }
    if (x <= 0 && x === Math.floor(x)) { return Infinity; }     /* 极点 */
    if (x < 0.5) {
      /* 反射：Γ(x) = π / (sin(πx)·Γ(1−x))，手工展开，无自调用 */
      return Math.PI / (Math.sin(Math.PI * x) * _gammaHalf(1 - x));
    }
    return _gammaHalf(x);
  }

  return {
    erf: erf,
    gamma: gamma,
    lgamma: lgamma,

    reset: reset,
    getError: getError,
    getOpCount: getOpCount,
    allocCount: allocCount
  };
})();

if (typeof module === "object" && module && module.exports) {
  module.exports = ApolloSpecial;
}

/* ============================================================
 * ApolloCalc · core_kernel.js — 内核统一入口
 * 由 build-kernel.js 自动拼接，禁止手动编辑
 * 拼接顺序：parser → math_functions → complex → exact → evaluator → calculus → base → solvers → ratroot → multivar → matrix → special（12 文件）
 * ============================================================ */

/* 供 index.js 引用的内核接口（UI 层唯一访问点） */
export default {
  /* ---- 解析层 ---- */
  parseExpression: parseExpression,
  dumpTokenStream: dumpTokenStream,

  /* ---- 数学执行层 ---- */
  applyFunction: applyFunction,

  /* ---- 精确数值系统（五分量） ---- */
  exactReset: exactReset,
  exactIsValid: function () { return exactIsValid; },
  getExactRat: function () { return exactRat; },
  getExactRadNum: function () { return exactRadNum; },
  getExactDen: function () { return exactDen; },
  getExactRad: function () { return exactRad; },
  getExactExp: function () { return exactExp; },
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
  getRootExactRat: function (i) { return _rootExactRat[i]; },
  getRootExactRadNum: function (i) { return _rootExactRadNum[i]; },
  getRootExactRad: function (i) { return _rootExactRad[i]; },
  getRootExactDen: function (i) { return _rootExactDen[i]; },
  getRootExactExp: function (i) { return _rootExactExp[i]; },
  getRootExactValid: function (i) { return _rootExactValid[i]; },
  getSolveError: function () { return _solveError; },
  getResidualMax: function () { return _residualMax; },

  /* ---- 全局状态切换 ---- */
  setAngleMode: function (mode) { angleMode = mode; },
  getAngleMode: function () { return angleMode; },

  /* ---- 矩阵池写入（UI 层给线性方程组输入系数用，matrixPool 156 = 12×13） ---- */
  setMatrixElement: function (i, v) { matrixPool[i] = v; },

  /* ---- 复数域函数与寄存器访问器（128KB 扩展：UI 层调用序 setComplexA → cXxx → getComplexTmp*） ---- */
  setComplexA: function (re, im) { _aRe = re; _aIm = im; },
  setComplexB: function (re, im) { _bRe = re; _bIm = im; },
  getComplexTmpRe: function () { return _tmpRe; },
  getComplexTmpIm: function () { return _tmpIm; },
  cExp: function () { cExp(); },
  cLn: function () { cLn(); },
  cPowReal: function (w) { cPowReal(w); },

  /* ---- 数值微积分（128KB 扩展：UI 层先 parseExpression 再调 calcXxx，表达式变更前须 calculusReset） ---- */
  calcDerivative: function (x) { return calcDerivative(x); },
  calcDerivative2: function (x) { return calcDerivative2(x); },
  calcIntegral: function (a, b) { return calcIntegral(a, b); },
  calcLimit: function (a, side) { return calcLimit(a, side); },
  calculusReset: function () { calculusReset(); },
  getCalcError: function () { return _calcError; },
  getCalcIterations: function () { return _calcIterations; },

  /* ---- 泰勒展开（128KB 扩展：先 parseExpression，calcTaylor 缓存系数后 taylorEval 求值） ---- */
  calcTaylor: function (x0, order) { return calcTaylor(x0, order); },
  taylorEval: function (x) { return taylorEval(x); },
  getTaylorCoeff: function (i) { return _taylorCoeffs[i]; },
  getTaylorOrder: function () { return _taylorOrder; },

  /* ---- 进制转换与 SI 前缀（128KB 扩展：先 decToXxx/siNormalize 再 toString 出口） ---- */
  decToBin: function (n) { return decToBin(n); },
  decToOct: function (n) { return decToOct(n); },
  decToHex: function (n) { return decToHex(n); },
  baseToString: function () { return baseToString(); },
  siNormalize: function (v) { return siNormalize(v); },
  siToString: function () { return siToString(); },
  getSiMantissa: function () { return _siMantissa; },
  getSiPrefixExp: function () { return _siPrefixExp; },
  getSiValid: function () { return _siValid; },
  getBaseLen: function () { return _baseLen; },
  baseReset: function () { baseReset(); },

  /* ---- v2.0.0 自包含三模块（IIFE 单命名空间，UI 层经 multivar.* / matrix.* / special.* 访问） ---- */
  multivar: ApolloMultivar,
  matrix: ApolloMatrix,
  special: ApolloSpecial
};
