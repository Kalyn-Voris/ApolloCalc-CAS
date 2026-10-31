/* ApolloCalc-CAS · JS/128KB 多元非线性门禁接入测试 —— v2.3.0(ROADMAP §二)
 * 被测行为:guardMultivariateNonlinear 已接入多项式求解管线
 * (solveQuadratic / solveCubic / solveQuartic 入口;线性方程组
 * solveLinearSystem 不拦——守卫语义:线性多元组交 solveLinearSystem)。
 *
 * 注入说明:当前解析器仅产出 x(单变量),管线拒绝路径只能经 tokenPool
 * 直接装填触发(w = (type<<8)|payload;T_VAR=2/T_OP=4/T_FUNC=7,
 * OP_POW=4/OP_ADD=0,F_SIN=1——core_parser.js 常量)。这也是多变量
 * 解析器扩展落地后 UI 流的预期行为预演。
 * 运行:node test/test-guard.js   (JS/128KB 目录下)
 * 全过输出:==== guard: N pass, 0 fail ====
 */

var fs = require("fs");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_base.js", "core_solvers.js", "core_ratroot.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

var pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}

/* tokenPool 装填助手:type/payload 对列表 → 池 + tokenCount */
function loadTokens(pairs) {
  var i;
  for (i = 0; i < pairs.length; i += 2) {
    tokenPool[i / 2] = (pairs[i] << 8) | pairs[i + 1];
  }
  tokenCount = pairs.length / 2;
}
function clearTokens() { tokenCount = 0; }

/* ==================== 一、守卫本体语义(注入) ==================== */
loadTokens([T_VAR, 0, T_VAR, 1, T_OP, OP_POW]);
ok("双变量+^ 拒绝", guardMultivariateNonlinear() === 1 && _solveError === 2,
   "_solveError=" + _solveError);
clearTokens();
loadTokens([T_VAR, 0, T_VAR, 1]);
ok("双变量线性不拦", guardMultivariateNonlinear() === 0 && _solveError === 0);
clearTokens();
loadTokens([T_VAR, 0, T_OP, OP_POW]);
ok("单变量非线性不拦", guardMultivariateNonlinear() === 0);
clearTokens();
loadTokens([T_VAR, 0, T_VAR, 2, T_FUNC, F_SIN]);
ok("双变量+函数拒绝", guardMultivariateNonlinear() === 1 && _solveError === 2);
clearTokens();
loadTokens([T_VAR, 0, T_OP, OP_ADD]);
ok("单变量线性不拦", guardMultivariateNonlinear() === 0);
clearTokens();
ok("空 token 流不拦", guardMultivariateNonlinear() === 0);
ok("守卫仍在导出契约(typeof function)", typeof guardMultivariateNonlinear === "function");

/* ==================== 二、管线接入:三个多项式求解入口 ==================== */
var BAD = [T_VAR, 0, T_VAR, 1, T_OP, OP_POW];   /* 双变量非线性 token 态 */

loadTokens(BAD);
_coeffs[0] = -6; _coeffs[1] = -5; _coeffs[2] = 1;   /* x²−5x−6,合法二次 */
solversReset();
ok("solveQuadratic 接入拒绝", solveQuadratic() === 2 && _solveError === 2 && _rootCount === 0,
   "_solveError=" + _solveError + " count=" + _rootCount);

loadTokens(BAD);
_coeffs[0] = -6; _coeffs[1] = -11; _coeffs[2] = 6; _coeffs[3] = 1;  /* x³−6x²+11x−6 */
solversReset();
ok("solveCubic 接入拒绝", solveCubic() === 2 && _solveError === 2 && _rootCount === 0);

loadTokens(BAD);
_coeffs[0] = -24; _coeffs[1] = -50; _coeffs[2] = 35; _coeffs[3] = -10; _coeffs[4] = 1;
solversReset();
ok("solveQuartic 接入拒绝", solveQuartic() === 2 && _solveError === 2 && _rootCount === 0);

/* 三次降次委托链同样被拦(入口先扫):a=0 → solveQuadratic 内层再扫,幂等 */
loadTokens(BAD);
_coeffs[0] = -6; _coeffs[1] = -5; _coeffs[2] = 1; _coeffs[3] = 0;
solversReset();
ok("solveCubic 退化委托仍拒绝", solveCubic() === 2 && _solveError === 2 && _rootCount === 0);

/* ==================== 三、线性方程组豁免(守卫语义:线性多元交 solveLinearSystem) ==================== */
loadTokens(BAD);
solversReset();
matrixPool[0] = 2; matrixPool[1] = 3; matrixPool[2] = 8;
matrixPool[3] = 1; matrixPool[4] = -1; matrixPool[5] = -1;
ok("solveLinearSystem 不受守卫影响", solveLinearSystem(2) === 0 && _rootCount === 2 &&
   Math.abs(_rootRe[0] - 1) < 1e-9 && Math.abs(_rootRe[1] - 2) < 1e-9,
   "err=" + _solveError);

/* ==================== 四、单变量管线不回归 ==================== */
solversReset();
parseExpression("x^2-5x+6=0");
extractCoefficients(2);
ok("单变量二次照常求解", solveQuadratic() === 0 && _rootCount === 2 &&
   Math.abs(_rootRe[0] - 3) < 1e-9 && Math.abs(_rootRe[1] - 2) < 1e-9,
   "err=" + _solveError + " r0=" + _rootRe[0]);
solversReset();
parseExpression("x^3-6x^2+11x-6=0");
extractCoefficients(3);
ok("单变量三次照常求解(有理根快路径)", solveCubic() === 0 && _rootCount === 3 &&
   Math.abs(_rootRe[0] - 1) < 1e-9 && Math.abs(_rootRe[1] - 2) < 1e-9);
solversReset();
parseExpression("x^4-10x^3+35x^2-50x+24=0");
extractCoefficients(4);
ok("单变量四次照常求解", solveQuartic() === 0 && _rootCount === 4 &&
   Math.abs(_rootRe[0] - 1) < 1e-7 && Math.abs(_rootRe[1] - 2) < 1e-7 &&
   Math.abs(_rootRe[2] - 3) < 1e-7 && Math.abs(_rootRe[3] - 4) < 1e-7);
/* 拒绝后恢复:清 token 流,求解管线回到正常 */
clearTokens();
solversReset();
_coeffs[0] = 6; _coeffs[1] = -5; _coeffs[2] = 1;
ok("清空 token 后恢复求解", solveQuadratic() === 0 && _rootCount === 2 &&
   Math.abs(_rootRe[0] - 3) < 1e-9 && Math.abs(_rootRe[1] - 2) < 1e-9);

console.log("==== guard: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
