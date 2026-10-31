/* ApolloCalc · test-base.js — 进制转换 + SI 前缀冒烟测试
   Node 下从 ../src 按序拼接文件 1-8（与 build-kernel.js 顺序一致）后执行 */
var fs = require("fs");
var dir = __dirname + "/../src";
var code = ["core_parser.js", "core_math_functions.js", "core_complex.js", "core_exact.js",
            "core_evaluator.js", "core_calculus.js", "core_base.js", "core_solvers.js"]
  .map(function (f) { return fs.readFileSync(dir + "/" + f, "utf8"); }).join("\n");
eval(code);

var pass = 0, fail = 0;

/* decToBase 成功 + _baseLen 命中 + baseToString 逆序拼串得正序 */
function expectBase(name, n, base, want) {
  var rc = decToBase(n, base);
  var s = rc === 1 ? baseToString() : "";
  if (rc === 1 && _baseLen === want.length && s === want) { pass++; }
  else { fail++; console.log("FAIL " + name + " rc=" + rc + " len=" + _baseLen + " got=\"" + s + "\" want=\"" + want + "\""); }
}

function expectBaseFail(name, n, base) {
  var rc = decToBase(n, base);
  var s = baseToString();
  if (rc === 0 && _baseLen === 0 && s === "") { pass++; }
  else { fail++; console.log("FAIL " + name + " rc=" + rc + " len=" + _baseLen + " got=\"" + s + "\""); }
}

function expectSi(name, v, wantM, wantCode, wantExp) {
  var rc = siNormalize(v);
  if (rc === 1 && _siValid === 1 && Math.abs(_siMantissa - wantM) <= 1e-12 &&
      _siPrefixCode === wantCode && _siPrefixExp === wantExp) { pass++; }
  else { fail++; console.log("FAIL " + name + " rc=" + rc + " m=" + _siMantissa +
    " code=" + _siPrefixCode + " exp=" + _siPrefixExp + " valid=" + _siValid); }
}

/* 无效路径：四元组全零，不残留上次结果 */
function expectSiFail(name, v) {
  var rc = siNormalize(v);
  if (rc === 0 && _siValid === 0 && _siMantissa === 0 && _siPrefixCode === 0 && _siPrefixExp === 0) { pass++; }
  else { fail++; console.log("FAIL " + name + " rc=" + rc + " m=" + _siMantissa +
    " code=" + _siPrefixCode + " exp=" + _siPrefixExp + " valid=" + _siValid); }
}

function expectSiStr(name, v, want) {
  siNormalize(v);
  var s = siToString();
  if (s === want) { pass++; }
  else { fail++; console.log("FAIL " + name + " got=\"" + s + "\" want=\"" + want + "\""); }
}

/* 模块一：进制转换（任务卡向量） */
expectBase("255→16", 255, 16, "FF");
expectBase("255→2", 255, 2, "11111111");
expectBase("255→8", 255, 8, "377");
expectBase("0→16", 0, 16, "0");
expectBase("-42→16", -42, 16, "-2A");
expectBase("1000000→16", 1000000, 16, "F4240");
expectBase("10→2", 10, 2, "1010");
expectBase("123→10", 123, 10, "123");

if (decToBin(255) === 1 && baseToString() === "11111111") { pass++; }
else { fail++; console.log("FAIL decToBin 255"); }
if (decToOct(255) === 1 && baseToString() === "377") { pass++; }
else { fail++; console.log("FAIL decToOct 255"); }
if (decToHex(-42) === 1 && baseToString() === "-2A") { pass++; }
else { fail++; console.log("FAIL decToHex -42"); }

/* 边界与取整约定：53 位安全整数上边界、非整数 Math.round 收敛 */
expectBase("2^53-1→16", 9007199254740991, 16, "1FFFFFFFFFFFFF");
expectBase("255.7→16 取整", 255.7, 16, "100");

/* 模块二：进制转换失败路径 */
expectBaseFail("base=7 拦截", 255, 7);
expectBaseFail("base=0 拦截", 255, 0);
expectBaseFail("NaN 拦截", NaN, 16);
expectBaseFail("Infinity 拦截", Infinity, 16);
expectBaseFail("2^53 上溢拦截", 9007199254740992, 16);
expectBaseFail("-2^53 下溢拦截", -9007199254740992, 2);

/* 缓冲复用：先填满 16 字符，随后短转换按新 _baseLen 输出，不得残留旧字符 */
expectBase("长值铺满 2^53-1", 9007199254740991, 16, "1FFFFFFFFFFFFF");
expectBase("短值覆盖 15", 15, 16, "F");
expectBase("再短覆盖 9", 9, 16, "9");

/* 模块三：SI 前缀（任务卡向量） */
expectSi("12345→k", 12345, 12.345, 107, 3);
expectSi("0.005→m", 0.005, 5, 109, -3);
expectSi("1000→k", 1000, 1, 107, 3);
expectSi("999→无前缀", 999, 999, 0, 0);
expectSi("1e6→M", 1e6, 1, 77, 6);
expectSi("1e-6→µ", 1e-6, 1, 181, -6);
expectSi("-1234→k", -1234, -1.234, 107, 3);
expectSi("-0.005→m", -0.005, -5, 109, -3);

/* 模块四：SI 无效路径（四元组全零） */
expectSiFail("0 无效", 0);
expectSiFail("NaN 无效", NaN);
expectSiFail("Infinity 无效", Infinity);
expectSiFail("-Infinity 无效", -Infinity);
expectSiFail("1e27 超上界", 1e27);
expectSiFail("1e-27 超下界", 1e-27);

/* 模块五：SI 全前缀表（17 档，y..Y）
   精确千的幂依赖 ×1000 累积舍入回吸（|m−1|<1e-12 归拢为 1），
   否则 1e-15/1e-24 一侧尾数落在 1±2ulp，显示成 1.0000000000000002 y */
expectSi("1e-24→y", 1e-24, 1, 121, -24);
expectSi("1e-21→z", 1e-21, 1, 122, -21);
expectSi("1e-18→a", 1e-18, 1, 97, -18);
expectSi("1e-15→f", 1e-15, 1, 102, -15);
expectSi("1e-12→p", 1e-12, 1, 112, -12);
expectSi("1e-9→n", 1e-9, 1, 110, -9);
expectSi("1e-6→µ 复核", 1e-6, 1, 181, -6);
expectSi("1e-3→m", 1e-3, 1, 109, -3);
expectSi("1e0→空", 1, 1, 0, 0);
expectSi("1e3→k 复核", 1e3, 1, 107, 3);
expectSi("1e6→M 复核", 1e6, 1, 77, 6);
expectSi("1e9→G", 1e9, 1, 71, 9);
expectSi("1e12→T", 1e12, 1, 84, 12);
expectSi("1e15→P", 1e15, 1, 80, 15);
expectSi("1e18→E", 1e18, 1, 69, 18);
expectSi("1e21→Z", 1e21, 1, 90, 21);
expectSi("1e24→Y", 1e24, 1, 89, 24);

if (siPrefixCode(-27) === -1 && siPrefixCode(27) === -1 && siPrefixCode(1) === -1) { pass++; }
else { fail++; console.log("FAIL siPrefixCode 越界 -27/27/1"); }

/* 模块六：siToString 出口 */
expectSiStr("12345", 12345, "12.345 k");
expectSiStr("0.005", 0.005, "5 m");
expectSiStr("999 无前缀无空格", 999, "999");
expectSiStr("-1234", -1234, "-1.234 k");
expectSiStr("1e-24 y", 1e-24, "1 y");
expectSiStr("1e-6 µ(U+00B5)", 1e-6, "1 \u00B5");
siNormalize(0);
if (siToString() === "") { pass++; }
else { fail++; console.log("FAIL siToString 无效得空串 got=\"" + siToString() + "\""); }

/* 模块七：baseReset 复位 */
decToHex(48879);              /* BC00，置脏 _baseBuf/_baseLen */
siNormalize(987654321);       /* 置脏 SI 四元组 */
baseReset();
if (_baseLen === 0 && _siMantissa === 0 && _siPrefixCode === 0 &&
    _siPrefixExp === 0 && _siValid === 0 && baseToString() === "" && siToString() === "") { pass++; }
else { fail++; console.log("FAIL baseReset len=" + _baseLen + " m=" + _siMantissa +
  " code=" + _siPrefixCode + " exp=" + _siPrefixExp + " valid=" + _siValid); }

/* 复位后转换内核仍可用（复位不破坏缓冲可写性） */
expectBase("复位后可继续转换", 4095, 16, "FFF");

console.log("==== base smoke: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exitCode = 1; }
