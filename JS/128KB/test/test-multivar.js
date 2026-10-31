/* ApolloCalc · test-multivar.js — 多变量多项式 + 高阶三角/复数补充 测试

   被测模块：src/core_multivar.js（自包含，不依赖既有 9 个 core）
   运行：node test/test-multivar.js   （JS/128KB 目录下）
   全过输出：==== multivar: N pass, 0 fail ==== */
"use strict";

var M = require("../src/core_multivar.js");

var pass = 0, fail = 0;
function approx(a, b, tol) {
  if (a === b) { return true; }
  if (typeof a !== "number" || typeof b !== "number") { return false; }
  if (a !== a || b !== b) { return false; }              /* NaN 不相等 */
  if (!isFinite(a) || !isFinite(b)) { return a === b; }
  return Math.abs(a - b) <= (tol === undefined ? 1e-9 : tol);
}
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}

/* TypedArray 指数编码（低4bit=y 高4bit=x） */
ok("编码 x^2y -> exps=33", M.encodeExps(2, 1) === 33, "got " + M.encodeExps(2, 1));
ok("编码 x^1y^1 -> exps=17", M.encodeExps(1, 1) === 17, "got " + M.encodeExps(1, 1));
ok("编码 x^1 -> exps=16", M.encodeExps(1, 0) === 16, "got " + M.encodeExps(1, 0));
ok("编码 常数 -> exps=0", M.encodeExps(0, 0) === 0);
ok("解码 x(33)=2", M.decodeX(33) === 2, "got " + M.decodeX(33));
ok("解码 y(33)=1", M.decodeY(33) === 1, "got " + M.decodeY(33));
ok("编码/解码往返 x=6,y=6", M.decodeX(M.encodeExps(6, 6)) === 6 &&
   M.decodeY(M.encodeExps(6, 6)) === 6);
ok("常量 MAX_EXP=6 / MAX_TOTAL_DEG=6 / MAX_TERMS=28",
   M.MAX_EXP === 6 && M.MAX_TOTAL_DEG === 6 && M.MAX_TERMS === 28,
   M.MAX_EXP + "/" + M.MAX_TOTAL_DEG + "/" + M.MAX_TERMS);

/* 解析：x^2y 类表达式 */
var n = M.parse("3x^2y + 2xy - 5", 0);
ok("解析返回 3 项", n === 3, "got " + n);
ok("解析无错误", M.getLastError() === 0, "err=" + M.getLastError());
ok("项数 termCount=3", M.termCount(0) === 3, "got " + M.termCount(0));

/* 规范序是 exps 降序：3x^2y(33) / 2xy(17) / -5(0) */
ok("第0项系数 3", approx(M.getCoef(0, 0), 3), "got " + M.getCoef(0, 0));
ok("第0项指数 33", M.getExps(0, 0) === 33, "got " + M.getExps(0, 0));
ok("第1项系数 2", approx(M.getCoef(0, 1), 2), "got " + M.getCoef(0, 1));
ok("第1项指数 17", M.getExps(0, 1) === 17, "got " + M.getExps(0, 1));
ok("第2项系数 -5", approx(M.getCoef(0, 2), -5), "got " + M.getCoef(0, 2));
ok("第2项指数 0", M.getExps(0, 2) === 0, "got " + M.getExps(0, 2));

/* 隐式乘 / 显式乘 / 小数 / 前导负号 / 裸变量 */
M.parse("3*x^2*y", 1);
ok("3*x^2*y 等价 3x^2y", M.getExps(1, 0) === 33 && approx(M.getCoef(1, 0), 3),
   "e=" + M.getExps(1, 0) + " c=" + M.getCoef(1, 0));
M.parse("2.5x^2 - 0.5y", 2);
ok("小数系数 2.5", approx(M.getCoef(2, 0), 2.5), "got " + M.getCoef(2, 0));
ok("小数系数 -0.5", approx(M.getCoef(2, 1), -0.5), "got " + M.getCoef(2, 1));
M.parse("-x^2", 3);
ok("前导负号 -x^2 -> -1", approx(M.getCoef(3, 0), -1) && M.getExps(3, 0) === 32,
   "c=" + M.getCoef(3, 0) + " e=" + M.getExps(3, 0));
M.parse("x", 4);
ok("裸 x 系数默认 1", approx(M.getCoef(4, 0), 1) && M.getExps(4, 0) === 16);
M.parse("y", 5);
ok("裸 y -> exps=1", M.getExps(5, 0) === 1, "got " + M.getExps(5, 0));
M.parse("x^2y", 6);
ok("x^2y 单项 -> exps=33 系数 1", M.getExps(6, 0) === 33 && approx(M.getCoef(6, 0), 1),
   "e=" + M.getExps(6, 0));
M.parse("y^2x", 7);
ok("y^2x 顺序无关 -> x^1y^2 exps=18", M.getExps(7, 0) === 18, "got " + M.getExps(7, 0));
M.parse("xy + xy", 8);
ok("隐式乘 xy + xy 合并 -> 2xy", M.termCount(8) === 1 && approx(M.getCoef(8, 0), 2) &&
   M.getExps(8, 0) === 17, "got " + M.polyToString(8));

/* 求值正确性（含 x=0 / y=0 / 负值） */
M.parse("3x^2y + 2xy - 5", 0);
ok("eval(1,1)=0", approx(M.evalSlot(0, 1, 1), 0), "got " + M.evalSlot(0, 1, 1));
ok("eval(2,3)=43", approx(M.evalSlot(0, 2, 3), 43), "got " + M.evalSlot(0, 2, 3));
ok("eval(0,7)=-5 (x=0)", approx(M.evalSlot(0, 0, 7), -5), "got " + M.evalSlot(0, 0, 7));
ok("eval(9,0)=-5 (y=0)", approx(M.evalSlot(0, 9, 0), -5), "got " + M.evalSlot(0, 9, 0));
ok("eval(0,0)=-5", approx(M.evalSlot(0, 0, 0), -5), "got " + M.evalSlot(0, 0, 0));
ok("eval(-1,2)=-3 (负值)", approx(M.evalSlot(0, -1, 2), -3), "got " + M.evalSlot(0, -1, 2));
/* 3*4*(-3) + 2*(-2)*(-3) - 5 = -36 + 12 - 5 = -29 */
ok("eval(-2,-3)=-29", approx(M.evalSlot(0, -2, -3), -29), "got " + M.evalSlot(0, -2, -3));

/* 直接对项表求值，不经槽位 */
var evalCoef = new Float64Array([3, 2, -5]);
var evalExps = new Int32Array([33, 17, 0]);
ok("evalPoly 直接项表 (2,3)=43", approx(M.evalPoly(evalCoef, evalExps, 3, 2, 3), 43),
   "got " + M.evalPoly(evalCoef, evalExps, 3, 2, 3));
ok("evalPoly (-1,2)=-3", approx(M.evalPoly(evalCoef, evalExps, 3, -1, 2), -3),
   "got " + M.evalPoly(evalCoef, evalExps, 3, -1, 2));
ok("evalPoly 空项表=0", M.evalPoly(evalCoef, evalExps, 0, 5, 5) === 0);
ok("evalTerms 与 evalPoly 一致", M.evalTerms(evalCoef, evalExps, 3, 2, 3) === 43);
M.clear(9);
ok("evalSlot 空多项式=0", M.evalSlot(9, 3, 4) === 0, "got " + M.evalSlot(9, 3, 4));

/* (x+y)^2 = x^2+2xy+y^2 在多点采样一致 */
M.parse("x^2 + 2xy + y^2", 10);
var samples = [[0, 0], [1, 2], [-3, 4], [2.5, -1.5], [-1, -1]];
var identOk = true;
for (var si = 0; si < samples.length; si++) {
  var sx = samples[si][0], sy = samples[si][1];
  if (!approx(M.evalSlot(10, sx, sy), (sx + sy) * (sx + sy), 1e-12)) { identOk = false; }
}
ok("(x+y)^2 展开在 5 点采样一致", identOk);

/* 加 / 减 */
M.parse("x^2 + 2xy", 0);
M.parse("y^2 - 2xy + 1", 1);
ok("add 返回 3 项", M.add(0, 1, 2) === 3, "got " + M.add(0, 1, 2));
ok("add 结果字符串", M.polyToString(2) === "x^2 + y^2 + 1", "got '" + M.polyToString(2) + "'");
ok("add 消去 2xy（3 项）", M.termCount(2) === 3, "got " + M.termCount(2));
ok("add 在 (2,3) 上等于两式之和",
   approx(M.evalSlot(2, 2, 3), M.evalSlot(0, 2, 3) + M.evalSlot(1, 2, 3), 1e-12));

ok("sub 返回 4 项", M.sub(0, 1, 3) === 4, "got " + M.sub(0, 1, 3));
ok("sub 结果字符串", M.polyToString(3) === "x^2 + 4xy - y^2 - 1",
   "got '" + M.polyToString(3) + "'");
ok("sub 在 (-1,2) 上等于两式之差",
   approx(M.evalSlot(3, -1, 2), M.evalSlot(0, -1, 2) - M.evalSlot(1, -1, 2), 1e-12));

/* 别名安全：dst 与 src 同一槽 */
M.parse("x + 1", 11);
M.add(11, 11, 11);
ok("add 原地别名 (x+1)+(x+1)=2x+2", M.polyToString(11) === "2x + 2",
   "got '" + M.polyToString(11) + "'");
M.parse("x + 1", 12);
M.sub(12, 12, 12);
ok("sub 原地别名 (x+1)-(x+1)=0", M.termCount(12) === 0 && M.polyToString(12) === "0",
   "got '" + M.polyToString(12) + "'");

/* 乘（卷积）/ 单项式乘 / 数乘 / 取负 */
M.parse("x + y", 0);
M.parse("x - y", 1);
ok("mul 返回 2 项", M.mul(0, 1, 2) === 2, "got " + M.mul(0, 1, 2));
ok("(x+y)(x-y)=x^2-y^2", M.polyToString(2) === "x^2 - y^2", "got '" + M.polyToString(2) + "'");
ok("平方差在 (3,2) 上=5", approx(M.evalSlot(2, 3, 2), 5), "got " + M.evalSlot(2, 3, 2));

M.parse("x + 1", 3);
M.parse("x - 1", 4);
ok("(x+1)(x-1)=x^2-1", M.mul(3, 4, 5) === 2 && M.polyToString(5) === "x^2 - 1",
   "got '" + M.polyToString(5) + "'");

M.parse("x^2 + 2xy + y^2", 6);
ok("mulMonomial 乘 x", M.mulMonomial(6, 1, 1, 0, 7) === 3 &&
   M.polyToString(7) === "x^3 + 2x^2y + xy^2", "got '" + M.polyToString(7) + "'");
ok("mulMonomial 乘 3y", M.mulMonomial(6, 3, 0, 1, 8) === 3 &&
   M.polyToString(8) === "3x^2y + 6xy^2 + 3y^3", "got '" + M.polyToString(8) + "'");

M.parse("x^2 + 2xy + y^2", 9);
ok("scale 2 倍", M.scale(9, 2, 13) === 3 &&
   M.polyToString(13) === "2x^2 + 4xy + 2y^2", "got '" + M.polyToString(13) + "'");
M.parse("x^2 + 2xy + y^2", 14);
ok("neg 取负", M.neg(14, 15) === 3 &&
   M.polyToString(15) === "-x^2 - 2xy - y^2", "got '" + M.polyToString(15) + "'");
M.parse("x^2 + 2xy + y^2", 9);
ok("scale 0 得零多项式", M.scale(9, 0, 12) === 0 && M.termCount(12) === 0,
   "cnt=" + M.termCount(12));

/* 乘法分配律在采样点上成立 */
M.parse("2x - 3y + 1", 0);
M.parse("x + 4y - 2", 1);
M.mul(0, 1, 2);
var distOk = true;
var ps = [[0, 0], [1, 1], [-2, 3], [1.5, -0.5]];
for (var pi = 0; pi < ps.length; pi++) {
  var px = ps[pi][0], py = ps[pi][1];
  var want = M.evalSlot(0, px, py) * M.evalSlot(1, px, py);
  if (!approx(M.evalSlot(2, px, py), want, 1e-9)) { distOk = false; }
}
ok("mul 结果与逐点乘积一致（4 点）", distOk);
ok("(2x-3y+1)(x+4y-2) 字符串",
   M.polyToString(2) === "2x^2 + 5xy - 3x - 12y^2 + 10y - 2",
   "got '" + M.polyToString(2) + "'");

/* 同类项合并 */
M.parse("x + x^2 + x", 0);
ok("合并 x+x^2+x -> 2 项", M.termCount(0) === 2, "got " + M.termCount(0));
ok("合并结果 x^2 + 2x", M.polyToString(0) === "x^2 + 2x", "got '" + M.polyToString(0) + "'");
M.parse("3x - x", 1);
ok("合并 3x-x -> 2x", M.termCount(1) === 1 && approx(M.getCoef(1, 0), 2) &&
   M.polyToString(1) === "2x", "got '" + M.polyToString(1) + "'");
M.parse("x - x", 2);
ok("合并 x-x -> 零多项式", M.termCount(2) === 0 && M.polyToString(2) === "0",
   "cnt=" + M.termCount(2) + " str='" + M.polyToString(2) + "'");
M.parse("2xy + 3xy + 4xy", 3);
ok("合并 2xy+3xy+4xy -> 9xy", M.termCount(3) === 1 && approx(M.getCoef(3, 0), 9) &&
   M.polyToString(3) === "9xy", "got '" + M.polyToString(3) + "'");
M.parse("x^2 - x^2 + y", 4);
ok("合并抵消后只剩 y", M.termCount(4) === 1 && M.getExps(4, 0) === 1,
   "cnt=" + M.termCount(4) + " e=" + M.getExps(4, 0));

/* 手工构造乱序项表后 normalize 合并 */
M.clear(5);
M.setTerm(5, 0, 2, M.encodeExps(1, 0));
M.setTerm(5, 1, 3, M.encodeExps(0, 1));
M.setTerm(5, 2, 5, M.encodeExps(1, 0));
M.setTerm(5, 3, -3, M.encodeExps(0, 1));
ok("setTerm 后 4 项", M.termCount(5) === 4, "got " + M.termCount(5));
ok("normalize 合并 2x+5x 与 3y-3y", M.normalize(5) === 1 && approx(M.getCoef(5, 0), 7) &&
   M.getExps(5, 0) === M.encodeExps(1, 0), "got '" + M.polyToString(5) + "'");

/* 总次数 / 分次数 */
M.parse("3x^2y + 2xy - 5", 0);
ok("totalDegree=3", M.totalDegree(0) === 3, "got " + M.totalDegree(0));
ok("degreeX=2", M.degreeX(0) === 2, "got " + M.degreeX(0));
ok("degreeY=1", M.degreeY(0) === 1, "got " + M.degreeY(0));
M.parse("x^3y^3", 1);
ok("x^3y^3 totalDegree=6", M.totalDegree(1) === 6, "got " + M.totalDegree(1));
M.parse("5", 2);
ok("常数多项式 totalDegree=0", M.totalDegree(2) === 0, "got " + M.totalDegree(2));
M.clear(3);
ok("零多项式 totalDegree=-1（-∞ 语义）", M.totalDegree(3) === -1, "got " + M.totalDegree(3));
M.parse("y^4", 4);
ok("y^4: degX=0 / degY=4", M.degreeX(4) === 0 && M.degreeY(4) === 4,
   "dx=" + M.degreeX(4) + " dy=" + M.degreeY(4));
M.parse("x^5 + y", 6);
ok("x^5+y: degX=5 / degY=1 / total=5",
   M.degreeX(6) === 5 && M.degreeY(6) === 1 && M.totalDegree(6) === 5);

/* 排序（按 exps 降序：先 x 再 y） */
M.clear(7);
M.setTerm(7, 0, 1, M.encodeExps(0, 2));
M.setTerm(7, 1, 2, M.encodeExps(1, 0));
M.setTerm(7, 2, 3, M.encodeExps(2, 1));
M.setTerm(7, 3, 4, M.encodeExps(1, 1));
ok("排序前首项是 y^2（乱序）", M.getExps(7, 0) === M.encodeExps(0, 2),
   "got e=" + M.getExps(7, 0));
ok("sortTerms 返回 4 项", M.sortTerms(7) === 4, "got " + M.sortTerms(7));
ok("排序后顺序 33/17/16/2",
   M.getExps(7, 0) === 33 && M.getExps(7, 1) === 17 &&
   M.getExps(7, 2) === 16 && M.getExps(7, 3) === 2,
   "got " + M.getExps(7, 0) + "," + M.getExps(7, 1) + "," +
   M.getExps(7, 2) + "," + M.getExps(7, 3));
ok("排序保持系数配对", approx(M.getCoef(7, 0), 3) && approx(M.getCoef(7, 1), 4) &&
   approx(M.getCoef(7, 2), 2) && approx(M.getCoef(7, 3), 1),
   "c=" + M.getCoef(7, 0) + "," + M.getCoef(7, 1) + "," +
   M.getCoef(7, 2) + "," + M.getCoef(7, 3));
M.parse("y^2 + x + x^2y + xy", 8);
ok("解析输入乱序仍规范排序", M.getExps(8, 0) === 33 && M.getExps(8, 3) === 2,
   "got " + M.getExps(8, 0) + ".." + M.getExps(8, 3) + " str='" + M.polyToString(8) + "'");
ok("规范序字符串 x^2y + xy + x + y^2",
   M.polyToString(8) === "x^2y + xy + x + y^2", "got '" + M.polyToString(8) + "'");

/* toString（写预分配缓冲，不拼接） */
M.parse("3x^2y + 2xy - 5", 0);
ok("toString 全文", M.polyToString(0) === "3x^2y + 2xy - 5", "got '" + M.polyToString(0) + "'");
ok("toString 返回长度与 getString().length 一致",
   M.toString(0) === M.getString().length,
   "ret=" + M.toString(0) + " len=" + M.getString().length);
ok("toString 返回值 = 实际字符数 15", M.toString(0) === 15, "got " + M.toString(0));
ok("charAt 逐字符可读（字符码）",
   M.charAt(0) === 51 && M.charAt(1) === 120 && M.charAt(2) === 94 && M.charAt(3) === 50,
   "got " + M.charAt(0) + "," + M.charAt(1) + "," + M.charAt(2) + "," + M.charAt(3));
ok("charAtStr 返回单字符",
   M.charAtStr(0) === "3" && M.charAtStr(1) === "x" && M.charAtStr(2) === "^" &&
   M.charAtStr(3) === "2", "got '" + M.charAtStr(0) + M.charAtStr(1) +
   M.charAtStr(2) + M.charAtStr(3) + "'");
ok("charAt 越界返回 -1", M.charAt(9999) === -1);
M.parse("x + 1", 1);
ok("toString x + 1", M.polyToString(1) === "x + 1", "got '" + M.polyToString(1) + "'");
M.parse("x - 1", 2);
ok("toString x - 1", M.polyToString(2) === "x - 1", "got '" + M.polyToString(2) + "'");
M.parse("-x^2", 3);
ok("toString -x^2（系数 1 省略）", M.polyToString(3) === "-x^2", "got '" + M.polyToString(3) + "'");
M.parse("-1", 4);
ok("toString -1", M.polyToString(4) === "-1", "got '" + M.polyToString(4) + "'");
M.parse("2.5x^2 - 0.5y", 5);
ok("toString 小数保留", M.polyToString(5) === "2.5x^2 - 0.5y", "got '" + M.polyToString(5) + "'");
M.clear(6);
ok("toString 零多项式 = \"0\"", M.polyToString(6) === "0", "got '" + M.polyToString(6) + "'");
M.parse("x^2y", 7);
ok("toString 单项无多余符号", M.polyToString(7) === "x^2y", "got '" + M.polyToString(7) + "'");
M.parse("3", 8);
ok("toString 纯常数", M.polyToString(8) === "3", "got '" + M.polyToString(8) + "'");

/* 错误与溢出路径 */
ok("x^7 单变量越界报错 E_DEGREE", (M.parse("x^7", 0) === -1 && M.getLastError() === 1),
   "err=" + M.getLastError());
ok("x^4y^3 单变量越界报错", (M.parse("x^4y^3", 0) === -1 && M.getLastError() === 1),
   "err=" + M.getLastError());
ok("x^3y^3 总次数 6 合法", (M.parse("x^3y^3", 0) === 1 && M.getLastError() === 0),
   "err=" + M.getLastError());
ok("未知变量 z 报错 E_SYNTAX", (M.parse("z", 0) === -1 && M.getLastError() === 2),
   "err=" + M.getLastError());
ok("空串报错", (M.parse("", 0) === -1 && M.getLastError() === 2), "err=" + M.getLastError());
ok("x^ 缺指数报错", (M.parse("x^", 0) === -1 && M.getLastError() === 2),
   "err=" + M.getLastError());
ok("x + 悬挂运算符报错", (M.parse("x +", 0) === -1 && M.getLastError() === 2),
   "err=" + M.getLastError());
ok("括号不支持报错", (M.parse("(x+1)", 0) === -1 && M.getLastError() === 2),
   "err=" + M.getLastError());
ok("两个小数点报错", (M.parse("1.2.3x", 0) === -1 && M.getLastError() === 2),
   "err=" + M.getLastError());
ok("非法 slot 返回 -1 而非崩溃", M.parse("x", 999) === -1 && M.getLastError() === 3,
   "err=" + M.getLastError());
ok("解析失败后目标槽位保持为空", M.termCount(0) === 0, "cnt=" + M.termCount(0));

/* 乘法结果总次数越界必须报错，不能静默截断 */
M.parse("x^3", 0);
ok("x^3 * x^4 溢出报错", (M.mulMonomial(0, 1, 4, 0, 1) === -1 && M.getLastError() === 1),
   "err=" + M.getLastError());
M.parse("x^3 + y^3", 2);
M.parse("x^3 + y^3", 3);
ok("(x^3+y^3)^2 = x^6+2x^3y^3+y^6 合法",
   (M.mul(2, 3, 4) === 3 && M.getLastError() === 0 && M.totalDegree(4) === 6),
   "err=" + M.getLastError() + " deg=" + M.totalDegree(4));
ok("(x^3+y^3)^2 字符串", M.polyToString(4) === "x^6 + 2x^3y^3 + y^6",
   "got '" + M.polyToString(4) + "'");
M.parse("x^6", 5);
M.parse("x^1", 6);
ok("x^6 * x^1 溢出报错（不静默回绕）",
   (M.mul(5, 6, 7) === -1 && M.getLastError() === 1), "err=" + M.getLastError());

/* 高阶三角（正 / 反） */
ok("sec(0)=1", approx(M.sec(0), 1), "got " + M.sec(0));
ok("csc(pi/2)=1", approx(M.csc(Math.PI / 2), 1), "got " + M.csc(Math.PI / 2));
ok("cot(pi/4)=1", approx(M.cot(Math.PI / 4), 1, 1e-12), "got " + M.cot(Math.PI / 4));
ok("asec(2)=pi/3", approx(M.asec(2), Math.PI / 3, 1e-12), "got " + M.asec(2));
ok("asec(-2)=2pi/3", approx(M.asec(-2), 2 * Math.PI / 3, 1e-12), "got " + M.asec(-2));
ok("asec(1)=0", approx(M.asec(1), 0));
ok("asec(0.5) 越域 -> NaN", M.asec(0.5) !== M.asec(0.5), "got " + M.asec(0.5));
ok("acsc(2)=pi/6", approx(M.acsc(2), Math.PI / 6, 1e-12), "got " + M.acsc(2));
ok("acsc(-2)=-pi/6", approx(M.acsc(-2), -Math.PI / 6, 1e-12), "got " + M.acsc(-2));
ok("acsc(0.5) 越域 -> NaN", M.acsc(0.5) !== M.acsc(0.5), "got " + M.acsc(0.5));
ok("acot(1)=pi/4", approx(M.acot(1), Math.PI / 4, 1e-12), "got " + M.acot(1));
ok("acot(-1)=3pi/4（值域 (0,pi)）", approx(M.acot(-1), 3 * Math.PI / 4, 1e-12),
   "got " + M.acot(-1));
ok("acot(0)=pi/2", approx(M.acot(0), Math.PI / 2, 1e-12), "got " + M.acot(0));
ok("cot(acot(x)) 往返 x=2.5", approx(M.cot(M.acot(2.5)), 2.5, 1e-12),
   "got " + M.cot(M.acot(2.5)));
ok("sec(asec(x)) 往返 x=1.7", approx(M.sec(M.asec(1.7)), 1.7, 1e-12),
   "got " + M.sec(M.asec(1.7)));

/* 双曲与反双曲 */
ok("sech(0)=1", approx(M.sech(0), 1), "got " + M.sech(0));
ok("csch(1)=1/sinh(1)", approx(M.csch(1), 1 / Math.sinh(1), 1e-15),
   "got " + M.csch(1));
ok("coth(1)=1/tanh(1)", approx(M.coth(1), 1 / Math.tanh(1), 1e-15),
   "got " + M.coth(1));
ok("sech(1)*cosh(1)=1", approx(M.sech(1) * Math.cosh(1), 1, 1e-15));
ok("asinh(1)=ln(1+sqrt2)", approx(M.asinh(1), Math.log(1 + Math.SQRT2), 1e-12),
   "got " + M.asinh(1));
ok("asinh(0)=0", approx(M.asinh(0), 0));
ok("asinh 奇函数 asinh(-3)=-asinh(3)", approx(M.asinh(-3), -M.asinh(3), 1e-12),
   M.asinh(-3) + " vs " + (-M.asinh(3)));
ok("acosh(2)=ln(2+sqrt3)", approx(M.acosh(2), Math.log(2 + Math.sqrt(3)), 1e-12),
   "got " + M.acosh(2));
ok("acosh(1)=0", approx(M.acosh(1), 0), "got " + M.acosh(1));
ok("acosh(0.5) 越域 -> NaN", M.acosh(0.5) !== M.acosh(0.5), "got " + M.acosh(0.5));
ok("atanh(0.5)=0.5*ln3", approx(M.atanh(0.5), 0.5 * Math.log(3), 1e-12),
   "got " + M.atanh(0.5));
ok("atanh(0)=0", approx(M.atanh(0), 0));
ok("atanh(1)=+Inf", M.atanh(1) === Infinity, "got " + M.atanh(1));
ok("atanh(-1)=-Inf", M.atanh(-1) === -Infinity, "got " + M.atanh(-1));
ok("atanh(2) 越域 -> NaN", M.atanh(2) !== M.atanh(2), "got " + M.atanh(2));

/* 复数域补充 */
M.setZ(2, 3);
M.cPowInt(2);
ok("(2+3i)^2 = -5+12i", approx(M.getRRe(), -5, 1e-12) && approx(M.getRIm(), 12, 1e-12),
   "got " + M.getRRe() + "+" + M.getRIm() + "i");
M.setZ(2, 3);
M.cPowInt(-1);
ok("(2+3i)^-1 = 2/13 - 3/13 i",
   approx(M.getRRe(), 2 / 13, 1e-12) && approx(M.getRIm(), -3 / 13, 1e-12),
   "got " + M.getRRe() + "+" + M.getRIm() + "i");
M.setZ(0, 0);
M.cPowInt(0);
ok("0^0 = 1", approx(M.getRRe(), 1) && approx(M.getRIm(), 0),
   "got " + M.getRRe() + "+" + M.getRIm() + "i");
M.setZ(0, 0);
M.cPowInt(-1);
ok("0^-1 非有限（不崩）", !isFinite(M.getRRe()) || !isFinite(M.getRIm()),
   "got " + M.getRRe() + "+" + M.getRIm() + "i");
M.setZ(2, 3);
M.cPowInt(0);
ok("(2+3i)^0 = 1", approx(M.getRRe(), 1) && approx(M.getRIm(), 0));
M.setZ(1, 1);
M.cPowInt(1);
ok("(1+i)^1 = 1+i", approx(M.getRRe(), 1) && approx(M.getRIm(), 1));
M.setZ(1, 1);
M.cPowInt(4);
ok("(1+i)^4 = -4", approx(M.getRRe(), -4, 1e-12) && approx(M.getRIm(), 0, 1e-12),
   "got " + M.getRRe() + "+" + M.getRIm() + "i");

M.setZ(0, Math.PI);
M.cExp();
ok("e^(i*pi) = -1", approx(M.getRRe(), -1, 1e-12) && approx(M.getRIm(), 0, 1e-12),
   "got " + M.getRRe() + "+" + M.getRIm() + "i");
M.setZ(-1, 0);
M.cSqrt();
ok("sqrt(-1) = i", approx(M.getRRe(), 0, 1e-12) && approx(M.getRIm(), 1, 1e-12),
   "got " + M.getRRe() + "+" + M.getRIm() + "i");
M.setZ(1, 0);
M.cLn();
ok("ln(1) = 0", approx(M.getRRe(), 0) && approx(M.getRIm(), 0),
   "got " + M.getRRe() + "+" + M.getRIm() + "i");
M.setZ(0, 0);
M.cLn();
ok("ln(0) 实部 -Inf（不崩）", M.getRRe() === -Infinity, "got " + M.getRRe());

M.setZ(1, 0);
M.cAsinh();
ok("cAsinh(1) = ln(1+sqrt2)", approx(M.getRRe(), Math.log(1 + Math.SQRT2), 1e-12) &&
   approx(M.getRIm(), 0, 1e-12), "got " + M.getRRe() + "+" + M.getRIm() + "i");
M.setZ(0, 1);
M.cAsinh();
ok("cAsinh(i) = i*pi/2", approx(M.getRRe(), 0, 1e-12) && approx(M.getRIm(), Math.PI / 2, 1e-12),
   "got " + M.getRRe() + "+" + M.getRIm() + "i");
M.setZ(2, 0);
M.cAcosh();
ok("cAcosh(2) = ln(2+sqrt3)", approx(M.getRRe(), Math.log(2 + Math.sqrt(3)), 1e-12) &&
   approx(M.getRIm(), 0, 1e-12), "got " + M.getRRe() + "+" + M.getRIm() + "i");
M.setZ(-1, 0);
M.cAcosh();
ok("cAcosh(-1) = i*pi", approx(M.getRRe(), 0, 1e-12) && approx(M.getRIm(), Math.PI, 1e-12),
   "got " + M.getRRe() + "+" + M.getRIm() + "i");
M.setZ(0.5, 0);
M.cAtanh();
ok("cAtanh(0.5) = 0.5*ln3", approx(M.getRRe(), 0.5 * Math.log(3), 1e-12) &&
   approx(M.getRIm(), 0, 1e-12), "got " + M.getRRe() + "+" + M.getRIm() + "i");
M.setZ(1, 0);
M.cAtanh();
ok("cAtanh(1) 实部发散（不崩）", !isFinite(M.getRRe()), "got " + M.getRRe());

/* cExp -> cLn 往返（setZFromResult 把结果搬回操作数） */
M.setZ(0.7, -1.3);
var zRe0 = M.getZRe(), zIm0 = M.getZIm();
M.cExp();
M.setZFromResult();
M.cLn();
ok("exp -> ln 往返恢复 z", approx(M.getRRe(), zRe0, 1e-12) &&
   approx(M.getRIm(), zIm0, 1e-12),
   "got " + M.getRRe() + "+" + M.getRIm() + "i");
ok("cAbs(3+4i)=5", (M.setZ(3, 4), approx(M.cAbs(), 5)), "got " + M.cAbs());

/* 零 GC：热路径分配计数不变 */
M.parse("3x^2y + 2xy - 5", 0);
M.parse("x^2 - y^2 + 2xy + 1", 1);
var allocBefore = M.allocCount();
var HOT = 20000;
var acc = 0;
for (var hi = 0; hi < HOT; hi++) {
  acc += M.evalSlot(0, 1.5, -2.5);
  acc += M.evalPoly(evalCoef, evalExps, 3, 0.5, 2.5);
  M.add(0, 1, 2);
  M.sub(0, 1, 3);
  M.mul(0, 1, 4);
  M.mulMonomial(0, 2, 1, 1, 5);
  M.scale(0, 2.5, 2);
  M.neg(0, 3);
  acc += M.totalDegree(0) + M.degreeX(0) + M.degreeY(0);
  M.toString(0);
  acc += M.getString().length;
  M.setZ(1.2, -0.7);
  M.cExp(); M.cLn(); M.cPowInt(3); M.cAsinh(); M.cAcosh(); M.cAtanh();
}
var allocAfter = M.allocCount();
ok("热路径 " + HOT + " 轮后分配计数不变（0 分配）", allocAfter === allocBefore,
   "before=" + allocBefore + " after=" + allocAfter);
ok("热路径确实执行了（累加器非 0）", acc !== 0, "acc=" + acc);
ok("热路径调用计数已增长（getOpCount>0）", M.getOpCount() > HOT, "op=" + M.getOpCount());
ok("stats() 与 allocCount() 一致", M.stats() === M.allocCount(),
   "stats=" + M.stats() + " alloc=" + M.allocCount());

/* 解析路径同样零分配（项表写 TypedArray + 游标，不 push 普通数组） */
var allocP0 = M.allocCount();
M.parse("x^2y + 3xy - 2", 6);
var allocP1 = M.allocCount();
ok("单次解析分配增量为 0", allocP1 - allocP0 === 0, "delta=" + (allocP1 - allocP0));
var allocP2 = M.allocCount();
M.parse("x^2y + 3xy - 2", 6);
ok("复用同 slot 再解析增量 = 0", M.allocCount() - allocP2 === 0,
   "delta=" + (M.allocCount() - allocP2));
/* 解析失败路径也不得分配 */
var allocP3 = M.allocCount();
M.parse("z + 1", 6);
M.parse("x^", 6);
M.parse("(x)", 6);
ok("解析失败路径分配增量 = 0", M.allocCount() - allocP3 === 0,
   "delta=" + (M.allocCount() - allocP3));

/* 模块自包含性与预分配 */
ok("模块独立 require 成功", typeof M === "object" && M !== null);
ok("导出 parse/evalSlot/mul/toString 等核心接口",
   typeof M.parse === "function" && typeof M.evalSlot === "function" &&
   typeof M.mul === "function" && typeof M.polyToString === "function" &&
   typeof M.add === "function" && typeof M.sub === "function");
ok("项池为 TypedArray（Float64Array / Int32Array）",
   M.coefBuffer() instanceof Float64Array && M.expsBuffer() instanceof Int32Array);
ok("项池长度 = SLOTS × MAX_TERMS",
   M.coefBuffer().length === M.SLOTS * M.MAX_TERMS &&
   M.expsBuffer().length === M.SLOTS * M.MAX_TERMS,
   "SLOTS=" + M.SLOTS + " len=" + M.coefBuffer().length);
ok("加载期分配次数恒为 6（6 个 TypedArray）", M.allocCount() === 6,
   "got " + M.allocCount());
ok("SLOTS 至少 8 槽", M.SLOTS >= 8, "got " + M.SLOTS);

/* reset 清槽但不复位分配计数（分配确实发生过） */
M.parse("x + 1", 0);
M.reset();
var resetClean = true;
for (var ri = 0; ri < M.SLOTS; ri++) {
  if (M.termCount(ri) !== 0) { resetClean = false; }
}
ok("reset 清空全部槽位", resetClean);
ok("reset 不复位分配计数（仍为 6）", M.allocCount() === 6, "got " + M.allocCount());
ok("reset 后 getLastError=0", M.getLastError() === 0);

/* 多变量符号求导（v2.0.0 diff） */
n = M.parse("x^2y + xy^2", 0);
var dn = M.diff(0, 0, 1);
ok("d/dx 返回 2 项", dn === 2, "got " + dn);
ok("d/dx 第0项 = 2xy", approx(M.getCoef(1, 0), 2) && M.getExps(1, 0) === 17,
   "coef=" + M.getCoef(1, 0) + " exps=" + M.getExps(1, 0));
ok("d/dx 第1项 = y^2", approx(M.getCoef(1, 1), 1) && M.getExps(1, 1) === 2,
   "coef=" + M.getCoef(1, 1) + " exps=" + M.getExps(1, 1));
ok("d/dx 文本 = 2*x*y + y^2", M.polyToString(1) === "2xy + y^2",
   "got " + M.polyToString(1));

M.parse("x^2y + xy^2", 0);
M.diff(0, 1, 1);
ok("d/dy 文本 = x^2 + 2*x*y", M.polyToString(1) === "x^2 + 2xy",
   "got " + M.polyToString(1));

M.parse("3x^4", 0);
M.diff(0, 0, 1);
ok("d/dx(3x^4) = 12*x^3", M.polyToString(1) === "12x^3", M.polyToString(1));

M.parse("5", 0);
M.diff(0, 0, 1);
ok("常数导数 = 空多项式（输出 0）",
   M.termCount(1) === 0 && M.polyToString(1) === "0", M.polyToString(1));

M.parse("y^3", 0);
M.diff(0, 1, 1);
ok("d/dy(y^3) = 3*y^2", M.polyToString(1) === "3y^2", M.polyToString(1));

M.parse("x^6", 0);
M.diff(0, 0, 1);
ok("d/dx(x^6) = 6*x^5", M.polyToString(1) === "6x^5", M.polyToString(1));

M.parse("x^3y^2", 0);
M.diff(0, 0, 1);
ok("d/dx(x^3y^2) = 3*x^2*y^2", M.polyToString(1) === "3x^2y^2", M.polyToString(1));

M.parse("x^2", 0);
M.diff(0, 0, 1);
ok("d/dx(x^2) @x=3 求值 = 6", M.evalSlot(1, 3, 0) === 6, M.evalSlot(1, 3, 0));

/* 原地：dst 与 a 同槽（先算进 tmp 再提交的纪律） */
M.parse("x^2y", 0);
M.diff(0, 0, 0);
ok("原地 d/dx(x^2y) = 2*x*y", M.polyToString(0) === "2xy", M.polyToString(0));

/* 链式：(x+y)(x-y) = x^2 - y^2 → d/dx = 2x */
M.parse("x+y", 0);
M.parse("x-y", 1);
M.mul(0, 1, 2);
M.diff(2, 0, 3);
ok("乘积后 d/dx = 2*x", M.polyToString(3) === "2x", M.polyToString(3));

/* 错误路径 */
ok("varIndex=2 拒绝", M.diff(0, 2, 1) === -1 && M.getLastError() === 2);
ok("varIndex=-1 拒绝", M.diff(0, -1, 1) === -1 && M.getLastError() === 2);
ok("a 槽号非法拒绝", M.diff(-1, 0, 1) === -1 && M.getLastError() === 3);
ok("dst 槽号非法拒绝", M.diff(0, 0, 99) === -1 && M.getLastError() === 3);

/* 槽位复用：前次结果不残留 */
M.parse("x^5", 0);
M.diff(0, 0, 1);
M.parse("7", 0);
M.diff(0, 0, 1);
ok("槽位复用：常数覆盖后导数为空",
   M.termCount(1) === 0 && M.polyToString(1) === "0", M.polyToString(1));

/* 零 GC：diff 热循环前后分配计数不变 */
var allocBeforeDiff = M.allocCount();
for (var di = 0; di < 5000; di++) {
  M.parse("x^2y + xy^2", 0);
  M.diff(0, 0, 1);
  M.evalSlot(1, 1.5, 2.5);
}
ok("diff 热循环零分配", M.allocCount() === allocBeforeDiff,
   "delta=" + (M.allocCount() - allocBeforeDiff));

console.log("==== multivar: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
