/* ============================================================
 * ApolloCalc-CAS · test_solvers_xlang.js — 求解层跨语言一致性驱动
 * 基准：JS/64KB/src（只读，严禁修改 JS 目录）
 * 用例 XL01-XL19 与 C/64KB/test/test_solvers.c 逐一对应，
 * 两侧的 root_count 与 6 槽根 re/im 必须一致（容差 1e-9）。
 * 运行：node test/test_solvers_xlang.js
 * 输出：XL01-XL15 直接打印可粘贴进 test_solvers.c 的 C 表行；
 *       XL16-XL19 线性方程组打印结果供 C 侧显式测试比对。
 * ============================================================ */
/* 注意：本文件必须保持非严格模式（eval 展开的内核依赖隐式全局） */
var fs = require("fs");
var path = require("path");

var dir = path.join(__dirname, "..", "..", "..", "JS", "64KB", "src");
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_solvers.js"]
  .map(function (f) { return fs.readFileSync(path.join(dir, f), "utf8"); }).join("\n");
eval(code);

angleMode = "RAD";

/* XL01-XL15 多项式方程（deg = 拟合次数，XL15 为降次链：三次方程按四次拟合） */
var CASES = [
  ["x^2-5x+6=0", 2],                 /* XL01 */
  ["x^2-2=0", 2],                    /* XL02 */
  ["x^2+1=0", 2],                    /* XL03 */
  ["2x^2+3x-2=0", 2],                /* XL04 */
  ["x^2-4x+4=0", 2],                 /* XL05 */
  ["x^3-6x^2+11x-6=0", 3],           /* XL06 */
  ["x^3-1=0", 3],                    /* XL07 */
  ["x^3-3x=0", 3],                   /* XL08 不可约 → 三角法 */
  ["x^3-6x^2+12x-8=0", 3],           /* XL09 三重根 */
  ["2x^3-x-1=0", 3],                 /* XL10 卡尔达诺复对 */
  ["x^4-5x^2+4=0", 4],               /* XL11 双二次 */
  ["x^4-1=0", 4],                    /* XL12 双二次复根 */
  ["x^4-10x^3+35x^2-50x+24=0", 4],   /* XL13 */
  ["2x^4-x^3-14x^2-5x+6=0", 4],      /* XL14 一般路径（笛卡尔分解） */
  ["x^3-6x^2+11x-6=0", 4]            /* XL15 四次 a=0 → 三次委托 */
];

function n2s(x) {
  /* JS Number → C double 字面量（toString 为最短往返表示；-0 归一为 0） */
  if (x === 0) { return "0"; }
  return String(x);
}

function runPoly(idx, src, deg) {
  solversReset();
  parseExpression(src);
  extractCoefficients(deg);
  if (deg === 2) { solveQuadratic(); }
  else if (deg === 3) { solveCubic(); }
  else if (deg === 4) { solveQuartic(); }
  var re = [];
  var im = [];
  var i = 0;
  for (i = 0; i < 6; i++) {
    re.push(i < _rootCount ? _rootRe[i] : 0);
    im.push(i < _rootCount ? _rootIm[i] : 0);
  }
  console.log('    { "' + src + '", ' + deg + ', ' + _rootCount + ',');
  console.log('     {' + re.map(n2s).join(", ") + '},');
  console.log('     {' + im.map(n2s).join(", ") + '} },               /* XL' + (idx < 10 ? "0" + idx : idx) + ' */');
}

console.log("==== JS 64KB solvers baseline: XL01-XL15 as C table rows ====");
for (var k = 0; k < CASES.length; k++) {
  runPoly(k + 1, CASES[k][0], CASES[k][1]);
}

/* XL16-XL19 线性方程组（C 侧显式校验，这里打印结果供比对） */
function runLin(tag, n) {
  solveLinearSystem(n);
  var s = tag + " err=" + _solveError + " count=" + _rootCount + " re=[";
  var i = 0;
  for (i = 0; i < 6; i++) { s += (i < _rootCount ? _rootRe[i] : 0) + (i < 5 ? ", " : ""); }
  s += "] im=[";
  for (var j = 0; j < 6; j++) { s += (j < _rootCount ? _rootIm[j] : 0) + (j < 5 ? ", " : ""); }
  console.log(s + "]");
}

console.log("==== XL16-XL19 linear systems ====");

solversReset();
matrixPool[0] = 2; matrixPool[1] = 3; matrixPool[2] = 8;
matrixPool[3] = 1; matrixPool[4] = -1; matrixPool[5] = -1;
runLin("XL16 lin2", 2);

solversReset();
matrixPool[0] = 1; matrixPool[1] = 1; matrixPool[2] = 1; matrixPool[3] = 6;
matrixPool[4] = 1; matrixPool[5] = 2; matrixPool[6] = 3; matrixPool[7] = 14;
matrixPool[8] = 2; matrixPool[9] = 3; matrixPool[10] = -1; matrixPool[11] = 5;
runLin("XL17 lin3", 3);

solversReset();
/* 6 元对角方程组：x_i = i（根池 6 槽上限，回归 JS 4 槽越界 bug） */
matrixPool[0] = 1; matrixPool[1] = 0; matrixPool[2] = 0; matrixPool[3] = 0; matrixPool[4] = 0; matrixPool[5] = 0; matrixPool[6] = 1;
matrixPool[7] = 0; matrixPool[8] = 1; matrixPool[9] = 0; matrixPool[10] = 0; matrixPool[11] = 0; matrixPool[12] = 0; matrixPool[13] = 2;
matrixPool[14] = 0; matrixPool[15] = 0; matrixPool[16] = 1; matrixPool[17] = 0; matrixPool[18] = 0; matrixPool[19] = 0; matrixPool[20] = 3;
matrixPool[21] = 0; matrixPool[22] = 0; matrixPool[23] = 0; matrixPool[24] = 1; matrixPool[25] = 0; matrixPool[26] = 0; matrixPool[27] = 4;
matrixPool[28] = 0; matrixPool[29] = 0; matrixPool[30] = 0; matrixPool[31] = 0; matrixPool[32] = 1; matrixPool[33] = 0; matrixPool[34] = 5;
matrixPool[35] = 0; matrixPool[36] = 0; matrixPool[37] = 0; matrixPool[38] = 0; matrixPool[39] = 0; matrixPool[40] = 1; matrixPool[41] = 6;
runLin("XL18 lin6", 6);

solversReset();
matrixPool[0] = 1; matrixPool[1] = 1; matrixPool[2] = 1;
matrixPool[3] = 1; matrixPool[4] = 1; matrixPool[5] = 2;
runLin("XL19 lin-singular", 2);
