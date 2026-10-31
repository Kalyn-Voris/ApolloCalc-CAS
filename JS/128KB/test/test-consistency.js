/* ApolloCalc · test-consistency.js — 家族一致性测试（128KB 内核 vs 64KB）

   跑家族共享 tests/ 下的 polynomials.json / exact-forms.json / edge-cases.json，
   验证 128KB 内核输出与 64KB 一致（或更强）。

   一致性口径：
     - 数值根不一致            → FAIL（真失败）
     - 精确形式从无到有        → IMPROVE（128KB 五分量混合形式更强，不算失败）
     - preCheck 放行 64KB 拒绝 → IMPROVE（128KB 阈值翻倍：深度≤6 函数≤4 变量≤10）
     - 错误码不同但均报错      → 以 expect:"error" 宽松判定（跨实现约定）

   运行：node test/test-consistency.js   （JS/128KB 目录下）
   全过输出：==== consistency: N pass, M improve, 0 fail ==== */
var fs = require("fs");
var path = require("path");

/* 加载内核（9 文件按拼接序） */
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_calculus.js", "core_base.js", "core_solvers.js", "core_ratroot.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

var pass = 0, fail = 0, improve = 0;

/* 家族根 tests/：先按标准布局（test → 128KB → JS → 根）探测，未命中则回退一级 */
var testsDir = __dirname + "/../../../tests";
if (!fs.existsSync(testsDir)) {
  testsDir = __dirname + "/../../tests";
  console.log("[warn] tests/ 路径回退到 " + testsDir);
}
if (!fs.existsSync(testsDir)) {
  console.error("[fatal] 找不到 tests/ 目录");
  process.exit(1);
}

function loadJSON(name) {
  return JSON.parse(fs.readFileSync(path.join(testsDir, name), "utf8"));
}

angleMode = "RAD";

/* 通用判定助手 */

/* 期望 (re,im) 根多重集合是否全部命中（乱序、一对一消耗） */
function rootsMatch(wantPairs, tol) {
  if (_rootCount !== wantPairs.length) { return false; }
  var used = [];
  var i, w, hit;
  for (i = 0; i < _rootCount; i++) { used[i] = 0; }
  for (w = 0; w < wantPairs.length; w++) {
    hit = -1;
    for (i = 0; i < _rootCount; i++) {
      if (used[i] === 0 &&
          Math.abs(_rootRe[i] - wantPairs[w][0]) <= tol &&
          Math.abs(_rootIm[i] - wantPairs[w][1]) <= tol) { hit = i; break; }
    }
    if (hit < 0) { return false; }
    used[hit] = 1;
  }
  return true;
}

/* 五分量精确形式 → 显示串（显示约定与 exact-forms.json 一致：
   整数 "3"、分数 "1/2"、纯根式 "√(2)" / "2√(2)" / "-√(2)"；
   混合形式与科学计数法为家族表外通用写法） */
function fmtExact(i) {
  if (_rootExactValid[i] !== 1) { return null; }
  var rat = _rootExactRat[i];
  var rn = _rootExactRadNum[i];
  var rad = _rootExactRad[i];
  var den = _rootExactDen[i];
  var exp = _rootExactExp[i];
  var expTail = (exp === 0) ? "" : "×10^" + exp;
  var s, an;

  if (rat !== 0 && rn !== 0) {
    /* 混合形式（表外）：(rat±k√(rad))/den */
    an = rn < 0 ? -rn : rn;
    return "(" + rat + (rn < 0 ? "-" : "+") + (an === 1 ? "" : an) + "√(" + rad + "))/" + den + expTail;
  }
  if (rn === 0) {
    /* 纯有理：rat/den */
    s = rat < 0 ? "-" : "";
    an = rat < 0 ? -rat : rat;
    if (den === 1) { return s + an + expTail; }
    return s + an + "/" + den + expTail;
  }
  /* 纯根式：±k√(rad)/den（符号随根式部系数） */
  s = rn < 0 ? "-" : "";
  an = rn < 0 ? -rn : rn;
  if (den === 1) { return s + (an === 1 ? "" : an) + "√(" + rad + ")" + expTail; }
  return s + (an === 1 ? "" : an) + "√(" + rad + ")/" + den + expTail;
}

/* 期望精确形式串多重集合是否全部命中（乱序、一对一消耗） */
function exactMatch(wantStrings) {
  if (_rootCount !== wantStrings.length) { return false; }
  var used = [];
  var i, w, hit, s;
  for (i = 0; i < _rootCount; i++) { used[i] = 0; }
  for (w = 0; w < wantStrings.length; w++) {
    hit = -1;
    for (i = 0; i < _rootCount; i++) {
      if (used[i] === 0) {
        s = fmtExact(i);
        if (s !== null && s === wantStrings[w]) { hit = i; break; }
      }
    }
    if (hit < 0) { return false; }
    used[hit] = 1;
  }
  return true;
}

/* 根结果快照（失败报告用） */
function rootsSnapshot() {
  var s = "err=" + _solveError + " count=" + _rootCount + " roots=[";
  var i;
  for (i = 0; i < _rootCount; i++) {
    if (i > 0) { s += " "; }
    s += _rootRe[i] + (_rootIm[i] === 0 ? "" : (_rootIm[i] > 0 ? "+" : "") + _rootIm[i] + "i");
    var e = fmtExact(i);
    if (e !== null) { s += " (" + e + ")"; }
  }
  return s + "]";
}

/* 求解管线：parse → extract → solveXxx（数值用例均不含括号/函数，preCheck 必过） */
function solvePoly(src, deg) {
  solversReset();
  parseExpression(src);
  if (parseError !== 0) { return; }
  extractCoefficients(deg);
  if (deg === 2) { solveQuadratic(); }
  else if (deg === 3) { solveCubic(); }
  else if (deg === 4) { solveQuartic(); }
}

function reportPass(id, note) {
  pass++;
  if (note) { console.log("pass   " + id + " [" + note + "]"); }
}

function reportImprove(id, why) {
  improve++;
  console.log("IMPROVE " + id + ": " + why);
}

function reportFail(id, want, got, note) {
  fail++;
  console.log("FAIL   " + id + ": want=" + want + " got=" + got + (note ? " | " + note : ""));
}

/* 内核池尺寸与门禁边界自检（128KB 收尾验收） */
if (tokenPool.length === 512 && numberPool.length === 256 && evalStack.length === 128 &&
    matrixPool.length === 156 && _rpnToken.length === 512 && _opStack.length === 128 &&
    _lhsToken.length === 256 && _rhsToken.length === 256) {
  pass++;
} else {
  reportFail("pool-sizes", "512/256/128/156/512/128/256/256",
    tokenPool.length + "/" + numberPool.length + "/" + evalStack.length + "/" + matrixPool.length +
    "/" + _rpnToken.length + "/" + _opStack.length + "/" + _lhsToken.length + "/" + _rhsToken.length);
}

/* preCheck 边界：5 层括号通过、7 层拒绝（阈值 depth > 6） */
solversReset();
parseExpression("(((((x)))))");
var depth5OK = (parseError === 0 && preCheckExpression() === 0);
parseExpression("(((((((x)))))))");
var depth7Rej = (parseError === 0 && preCheckExpression() === 1 && _solveError === 2);
if (depth5OK && depth7Rej) { pass++; }
else { reportFail("precheck-boundary", "5 层通过且 7 层拒绝", "depth5OK=" + depth5OK + " depth7Rej=" + depth7Rej); }

/* polynomials.json：数值根一致性 */
var polys = loadJSON("polynomials.json");
for (var pi = 0; pi < polys.cases.length; pi++) {
  var c = polys.cases[pi];
  solvePoly(c.input, c.degree);
  if (_solveError !== 0) {
    reportFail(c.input, JSON.stringify(c.expectedRoots), rootsSnapshot(), "求解器报错");
    continue;
  }
  if (rootsMatch(c.expectedRoots, c.tolerance)) { reportPass(c.input); }
  else { reportFail(c.input, JSON.stringify(c.expectedRoots), rootsSnapshot(), "tol=" + c.tolerance); }
}

/* exact-forms.json：精确形式一致性
   128KB 五分量需复现 64KB 显示约定（整数/分数/纯根式）；
   内核给不出精确形式或串不匹配 → 真失败（这些用例 64KB 全部可表达）。 */
var exacts = loadJSON("exact-forms.json");
for (var ei = 0; ei < exacts.cases.length; ei++) {
  var ce = exacts.cases[ei];
  solvePoly(ce.input, ce.degree);
  if (_solveError !== 0) {
    reportFail(ce.input, JSON.stringify(ce.expectedExact), rootsSnapshot(), "求解器报错");
    continue;
  }
  if (exactMatch(ce.expectedExact)) { reportPass(ce.input); }
  else { reportFail(ce.input, JSON.stringify(ce.expectedExact), rootsSnapshot(), "精确形式不匹配"); }
}

/* edge-cases.json：边界与错误 */
var edges = loadJSON("edge-cases.json");
for (var gi = 0; gi < edges.cases.length; gi++) {
  var g = edges.cases[gi];

  /* 线性方程组用例 */
  if (g.linearSystem) {
    var n = g.linearSystem.n;
    if (n <= 12) {
      var stride = n + 1;
      for (var r = 0; r < n; r++) {
        for (var q = 0; q < stride; q++) {
          matrixPool[r * stride + q] = g.linearSystem.augmented[r][q];
        }
      }
      solversReset();
      solveLinearSystem(n);
    } else {
      /* 13 元超 12 元边界：矩阵池装不下（156 < 13×14），直接调门禁验证拒绝 */
      solversReset();
      solveLinearSystem(n);
    }
    if (g.expect === "error") {
      if (_solveError !== 0) { reportPass(g.id, "solveError=" + _solveError); }
      else { reportFail(g.id, "error", "solveError=0"); }
    } else if (g.solveError && g.solveError.indexOf(_solveError) >= 0) {
      reportPass(g.id);
    } else {
      reportFail(g.id, "solveError∈" + JSON.stringify(g.solveError), "solveError=" + _solveError);
    }
    continue;
  }

  /* parse 段用例 */
  if (g.stage === "parse") {
    parseExpression(g.input);
    if (g.expect === "error") {
      /* 必须报错但码未约定：JS 解析器不做语法校验（如 x^^2），
         错误后移到 eval 段由 RPN 栈拦截，与 64KB 行为一致，视为通过 */
      if (parseError !== 0) { reportPass(g.id, "parse 段拦截 parseError=" + parseError); }
      else {
        evalExpressionAt(0);
        if (_evalError !== 0) { reportPass(g.id, "错误后移 eval 段 evalError=" + _evalError); }
        else { reportFail(g.id, "error", "parseError=0 且 evalError=0"); }
      }
    } else if (g.parseError && g.parseError.indexOf(parseError) >= 0) {
      reportPass(g.id);
    } else {
      reportFail(g.id, "parseError∈" + JSON.stringify(g.parseError), "parseError=" + parseError);
    }
    continue;
  }

  /* eval 段用例 */
  if (g.stage === "eval") {
    parseExpression(g.input);
    if (parseError !== 0) {
      reportFail(g.id, "evalError∈" + JSON.stringify(g.evalError), "意外在 parse 段报错 parseError=" + parseError);
      continue;
    }
    evalExpressionAt(0);
    if (g.evalError && g.evalError.indexOf(_evalError) >= 0) { reportPass(g.id); }
    else { reportFail(g.id, "evalError∈" + JSON.stringify(g.evalError), "evalError=" + _evalError); }
    continue;
  }

  /* extract 段用例 */
  if (g.stage === "extract") {
    parseExpression(g.input);
    if (parseError !== 0) { reportFail(g.id, "error", "parse 段报错 parseError=" + parseError); continue; }
    if (preCheckExpression() === 1) { reportFail(g.id, "error", "preCheck 拒绝，未到 extract"); continue; }
    var rcEx = extractCoefficients(4);
    var coeffsZero = (_coeffs[0] === 0 && _coeffs[1] === 0 && _coeffs[2] === 0 && _coeffs[3] === 0 && _coeffs[4] === 0);
    if (g.expect === "error") {
      if (rcEx !== 0 && coeffsZero) { reportPass(g.id, "rc=" + rcEx + " 系数已清零"); }
      else { reportFail(g.id, "error 且系数清零", "rc=" + rcEx + " zero=" + coeffsZero, g.note); }
    } else {
      reportFail(g.id, "预期错误约定缺失", "rc=" + rcEx);
    }
    continue;
  }

  /* solve 段用例（UI 真实顺序：parse → preCheck → guardDegree → extract → solve） */
  if (g.stage === "solve") {
    solversReset();
    parseExpression(g.input);
    if (parseError !== 0) { reportFail(g.id, "可解析", "parseError=" + parseError); continue; }

    /* 次数门禁优先于提取（degree > 4 时不得进入采样） */
    if (g.degree && g.degree > 4) {
      if (preCheckExpression() === 1) {
        if (g.expect === "error" || (g.solveError && g.solveError.indexOf(_solveError) >= 0)) { reportPass(g.id, "preCheck 拦截"); }
        else { reportFail(g.id, "solveError∈" + JSON.stringify(g.solveError), "_solveError=" + _solveError); }
        continue;
      }
      var gd = guardDegree(g.degree);
      if (g.expect === "error") {
        if (gd === 1) { reportPass(g.id, "guardDegree 拦截"); }
        else { reportFail(g.id, "error", "guardDegree 通过"); }
      } else if (gd === 1 && g.solveError && g.solveError.indexOf(_solveError) >= 0) {
        reportPass(g.id);
      } else {
        reportFail(g.id, "solveError∈" + JSON.stringify(g.solveError), "_solveError=" + _solveError);
      }
      continue;
    }

    /* preCheck 拒绝类：期望 _solveError = 2 */
    if (preCheckExpression() === 1) {
      if (g.solveError && g.solveError.indexOf(_solveError) >= 0) { reportPass(g.id); }
      else { reportFail(g.id, "solveError∈" + JSON.stringify(g.solveError), "_solveError=" + _solveError); }
      continue;
    }
    /* expectedImprove 用例：JSON 声明 64KB 拒绝（solveError=2）而本内核（128KB 阈值翻倍）
       preCheck 放行（128KB 返回成功）→ 判 IMPROVE；
       若 128KB 也拒绝，上面 preCheck===1 分支已按 solveError 判定 */
    if (g.expectedImprove === true) {
      reportImprove(g.id, "128KB preCheck 阈值翻倍（深度≤6 函数≤4 变量≤10），64KB 拒绝的输入放行");
      continue;
    }

    /* 常规求解：数值根必须一致；exactValid:false 且内核给出精确形式 → 改进 */
    if (!g.degree) { reportFail(g.id, "degree 字段", "缺失"); continue; }
    extractCoefficients(g.degree);
    if (g.degree === 2) { solveQuadratic(); }
    else if (g.degree === 3) { solveCubic(); }
    else if (g.degree === 4) { solveQuartic(); }
    if (_solveError !== 0) {
      reportFail(g.id, JSON.stringify(g.expectedRoots), rootsSnapshot(), "求解器报错");
      continue;
    }
    if (!rootsMatch(g.expectedRoots, g.tolerance)) {
      reportFail(g.id, JSON.stringify(g.expectedRoots), rootsSnapshot(), "数值根不一致 tol=" + g.tolerance);
      continue;
    }
    if (g.exactValid === false) {
      var validCount = 0;
      for (var vi = 0; vi < _rootCount; vi++) { if (_rootExactValid[vi] === 1) { validCount++; } }
      if (validCount > 0) {
        reportImprove(g.id, "数值根一致，精确形式从无到有（五分量混合形式，" + validCount + " 根有效）");
      } else {
        reportPass(g.id, "数值根一致，精确形式同样置无效");
      }
    } else {
      reportPass(g.id);
    }
    continue;
  }

  reportFail(g.id, "已知 stage", "未知 stage=" + g.stage);
}

/* 内存账本（水位门禁降级估算口径，含 _baseBuf） */
var ledger = tokenPool.byteLength + numberPool.byteLength + evalStack.byteLength +
             matrixPool.byteLength +
             _rpnToken.byteLength + _opStack.byteLength +
             _lhsToken.byteLength + _rhsToken.byteLength + _coeffs.byteLength +
             _rootRe.byteLength + _rootIm.byteLength +
             _rootExactRat.byteLength + _rootExactRadNum.byteLength +
             _rootExactRad.byteLength + _rootExactDen.byteLength +
             _rootExactExp.byteLength + _rootExactValid.byteLength +
             _baseBuf.byteLength;
console.log("---- 内存账本：水位门禁静态池（含 _baseBuf） " + ledger + " bytes（" + (ledger / 1024).toFixed(2) + " KB），" +
            "最坏游标估算 " + (ledger + 4096 + 2048 + 2048) + " bytes，门禁 96KB，" +
            "checkMemoryWatermark=" + checkMemoryWatermark() + " ----");

console.log("==== consistency: " + pass + " pass, " + improve + " improve, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
