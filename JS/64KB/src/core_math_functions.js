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
