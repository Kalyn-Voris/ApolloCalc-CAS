/* tools/gen-int-table.js — 积分表常量两侧共享生成器（F0，feat/js-gate-f0）

   单一规范源 CANON（128 项 kind/func/pow/anti）↔ C 侧
   C/128KB-ProTEXT/src/ac_pro_integrate.c 的 s_pro_int_table[] ↔ JS 侧
   JS/128KB/src/core_integrate.js 的 _T_SPEC/_T_ANTI。历史 ROADMAP 未兑现项①
   「积分表常量两侧共享生成脚本落 tools/」的清偿，机制见
   docs/contracts/f0-governance-v25.md §4。

   用法：
     node tools/gen-int-table.js            # --check（默认）：两侧在树常量逐项比对
                                            #   规范表，全一致 exit 0，漂移 exit 1
                                            #   （发版 checklist 核对项）
     node tools/gen-int-table.js --emit-c   # 按规范表输出 C 表文本（改表后重贴用）
     node tools/gen-int-table.js --emit-js  # 按规范表输出 _T_SPEC/_T_ANTI 文本

   改表流程：①改 CANON → ②--emit-c/--emit-js 重贴两侧 → ③--check exit 0 →
   ④两侧各跑符号积分回归（test_pro_runner / test-integrate.js）。
   零依赖，Node ≥ 14。 */
var fs = require("fs");
var path = require("path");

var ROOT = path.join(__dirname, "..");
var C_FILE = path.join(ROOT, "C", "128KB-ProTEXT", "src", "ac_pro_integrate.c");
var H_FILE = path.join(ROOT, "C", "128KB-ProTEXT", "src", "ac_pro.h");
var JS_FILE = path.join(ROOT, "JS", "128KB", "src", "core_integrate.js");

/* 规范表（CANON）：[kind, func, pow, anti]，id 数值 = C 侧 PRO_INT_K_* /
 * PRO_INT_F_* #define（ac_pro_integrate.c）。项序 = 表项 1..128。 */
var CANON = (function () {
  var raw = "0,0,0,n*u\u001e1,0,1,u^2/2\u001e1,0,2,u^3/3\u001e1,0,3,u^4/4\u001e1,0,4,u^5/5\u001e1,0,5,u^6/6\u001e1,0,6,u^7/7\u001e1,0,7,u^8/8\u001e1,0,8,u^9/9\u001e1,0,-1,u^(n+1)/(n+1)\u001e2,0,0,ln(u)/a\u001e3,0,0,-1/u\u001e4,0,0,atan(u)\u001e5,0,0,asin(u)\u001e6,0,0,asinh(u)\u001e7,0,0,acosh(u)\u001e8,0,0,atanh(u)\u001e9,0,0,(u*sqrt(1-u^2)+asin(u))/2\u001e10,0,0,(u*sqrt(u^2+1)+asinh(u))/2\u001e11,1,0,-cos(u)/a\u001e11,2,0,sin(u)/a\u001e11,3,0,-ln(cos(u))/a\u001e11,11,0,exp(u)/a\u001e11,9,0,(u*ln(u)-u)/a\u001e11,10,0,(u*log(u)-u/ln(10))/a\u001e11,8,0,2*u*sqrt(u)/(3*a)\u001e11,13,0,cosh(u)/a\u001e11,14,0,sinh(u)/a\u001e11,15,0,ln(cosh(u))/a\u001e11,4,0,(u*asin(u)+sqrt(1-u^2))/a\u001e11,5,0,(u*acos(u)-sqrt(1-u^2))/a\u001e11,6,0,(u*atan(u)-ln(1+u^2)/2)/a\u001e11,16,0,(u*asinh(u)-sqrt(1+u^2))/a\u001e11,17,0,(u*acosh(u)-sqrt(u^2-1))/a\u001e11,18,0,(u*atanh(u)+ln(1-u^2)/2)/a\u001e12,1,2,u/(2*a)-sin(2*u)/(4*a)\u001e12,2,2,u/(2*a)+sin(2*u)/(4*a)\u001e12,3,2,tan(u)/a-u/a\u001e12,1,3,(-cos(u)+cos(u)^3/3)/a\u001e12,2,3,(sin(u)-sin(u)^3/3)/a\u001e12,13,2,sinh(2*u)/(4*a)-u/(2*a)\u001e12,14,2,sinh(2*u)/(4*a)+u/(2*a)\u001e12,15,2,u/a-tanh(u)/a\u001e13,1,1,-x*cos(x)+sin(x)\u001e13,2,1,x*sin(x)+cos(x)\u001e13,11,1,exp(x)*(x-1)\u001e13,9,1,x^2*ln(x)/2-x^2/4\u001e13,13,1,x*cosh(x)-sinh(x)\u001e13,14,1,x*sinh(x)-cosh(x)\u001e13,6,1,((x^2+1)*atan(x)-x)/2\u001e13,8,1,2*x^2*sqrt(x)/5\u001e13,1,2,-x^2*cos(x)+2*x*sin(x)+2*cos(x)\u001e13,2,2,x^2*sin(x)+2*x*cos(x)-2*sin(x)\u001e13,11,2,exp(x)*(x^2-2*x+2)\u001e13,9,2,x^3*ln(x)/3-x^3/9\u001e13,13,2,x^2*cosh(x)-2*x*sinh(x)+2*cosh(x)\u001e13,14,2,x^2*sinh(x)-2*x*cosh(x)+2*sinh(x)\u001e13,11,3,exp(x)*(x^3-3*x^2+6*x-6)\u001e13,11,4,exp(x)*(x^4-4*x^3+12*x^2-24*x+24)\u001e13,1,3,-x^3*cos(x)+3*x^2*sin(x)+6*x*cos(x)-6*sin(x)\u001e13,2,3,x^3*sin(x)+3*x^2*cos(x)-6*x*sin(x)-6*cos(x)\u001e13,9,3,x^4*ln(x)/4-x^4/16\u001e13,13,3,x^3*cosh(x)-3*x^2*sinh(x)+6*x*cosh(x)-6*sinh(x)\u001e13,14,3,x^3*sinh(x)-3*x^2*cosh(x)+6*x*sinh(x)-6*cosh(x)\u001e14,0,0,atan(u)/a\u001e14,0,1,atanh(u)/a\u001e14,0,2,asin(u)/a\u001e14,0,3,asinh(u)/a\u001e14,0,4,acosh(u)/a\u001e14,0,5,(u*sqrt(1-u^2)+asin(u))/(2*a)\u001e14,0,6,(u*sqrt(1+u^2)+asinh(u))/(2*a)\u001e14,0,7,(u*sqrt(u^2-1)-acosh(u))/(2*a)\u001e15,0,0,atan(u/n)/(a*n)\u001e15,0,1,atanh(u/n)/(a*n)\u001e15,0,2,asin(u/n)/a\u001e15,0,3,asinh(u/n)/a\u001e15,0,4,acosh(u/n)/a\u001e15,0,5,(u*sqrt(m-u^2)+m*asin(u/n))/(2*a)\u001e15,0,6,(u*sqrt(u^2+m)+m*asinh(u/n))/(2*a)\u001e15,0,7,(u*sqrt(u^2-m)-m*acosh(u/n))/(2*a)\u001e15,0,8,ln((u-n)/(u+n))/(2*a*n)\u001e16,0,0,ln(u^2+m)/(2*a)\u001e16,0,1,ln(u^2-m)/(2*a)\u001e16,0,2,-ln(m-u^2)/(2*a)\u001e17,2,0,ln(1/cos(u)+sin(u)/cos(u))/a\u001e17,1,0,-ln(1/sin(u)+cos(u)/sin(u))/a\u001e18,2,1,ln(sin(u))/a\u001e18,13,14,ln(sinh(u))/a\u001e19,2,2,sin(u)/cos(u)/a\u001e19,1,2,-cos(u)/sin(u)/a\u001e19,3,2,-cos(u)/sin(u)/a-u/a\u001e19,14,2,sinh(u)/cosh(u)/a\u001e19,13,2,-cosh(u)/sinh(u)/a\u001e19,15,2,u/a-cosh(u)/sinh(u)/a\u001e20,1,2,sin(u)^2/(2*a)\u001e20,13,14,sinh(u)^2/(2*a)\u001e21,2,3,1/cos(u)/a\u001e21,1,2,-1/sin(u)/a\u001e22,1,2,1/cos(u)/a\u001e22,2,1,-1/sin(u)/a\u001e22,13,14,-1/cosh(u)/a\u001e22,14,13,-1/sinh(u)/a\u001e23,11,2,exp(n*u)/(n*a)\u001e24,1,2,u/(8*a)-sin(4*u)/(32*a)\u001e25,9,0,ln(u)^2/(2*a)\u001e26,9,0,ln(ln(u))/a\u001e12,3,3,tan(u)^2/(2*a)+ln(cos(u))/a\u001e12,15,3,tanh(u)^2/(2*a)-ln(cosh(u))/a\u001e12,1,4,3*u/(8*a)-sin(2*u)/(4*a)+sin(4*u)/(32*a)\u001e12,2,4,3*u/(8*a)+sin(2*u)/(4*a)+sin(4*u)/(32*a)\u001e12,1,5,-cos(u)/a+2*cos(u)^3/(3*a)-cos(u)^5/(5*a)\u001e12,2,5,sin(u)/a-2*sin(u)^3/(3*a)+sin(u)^5/(5*a)\u001e12,13,3,cosh(u)^3/(3*a)-cosh(u)/a\u001e12,14,3,sinh(u)/a+sinh(u)^3/(3*a)\u001e12,13,4,sinh(4*u)/(32*a)-sinh(2*u)/(4*a)+3*u/(8*a)\u001e12,14,4,sinh(4*u)/(32*a)+sinh(2*u)/(4*a)+3*u/(8*a)\u001e12,9,2,(u*ln(u)^2-2*u*ln(u)+2*u)/a\u001e13,1,4,-x^4*cos(x)+4*x^3*sin(x)+12*x^2*cos(x)-24*x*sin(x)-24*cos(x)\u001e13,2,4,x^4*sin(x)+4*x^3*cos(x)-12*x^2*sin(x)-24*x*cos(x)+24*sin(x)\u001e13,11,5,exp(x)*(x^5-5*x^4+20*x^3-60*x^2+120*x-120)\u001e13,9,4,x^5*ln(x)/5-x^5/25\u001e13,9,5,x^6*ln(x)/6-x^6/36\u001e13,13,4,x^4*cosh(x)-4*x^3*sinh(x)+12*x^2*cosh(x)-24*x*sinh(x)+24*cosh(x)\u001e13,14,4,x^4*sinh(x)-4*x^3*cosh(x)+12*x^2*sinh(x)-24*x*cosh(x)+24*sinh(x)\u001e13,6,2,((2*x^3-x^2-1)*atan(x)+x)/6\u001e13,6,3,((6*x^4-2*x^3+x^2+1)*atan(x)-x)/24\u001e13,8,2,2*x^3*sqrt(x)/7\u001e13,8,3,2*x^4*sqrt(x)/9";
  return raw.split("\x1e").map(function (s) {
    var i = s.indexOf(",");
    var j = s.indexOf(",", i + 1);
    var k = s.indexOf(",", j + 1);
    return [+s.slice(0, i), +s.slice(i + 1, j), +s.slice(j + 1, k), s.slice(k + 1)];
  });
})();

var KIND_NAMES = {"0":"PRO_INT_K_CONST","1":"PRO_INT_K_XN","2":"PRO_INT_K_INV","3":"PRO_INT_K_INV2","4":"PRO_INT_K_ATAN_KER","5":"PRO_INT_K_ASIN_KER","6":"PRO_INT_K_ASINH_KER","7":"PRO_INT_K_ACOSH_KER","8":"PRO_INT_K_ATANH_KER","9":"PRO_INT_K_CIRCLE","10":"PRO_INT_K_HYPER","11":"PRO_INT_K_FUNC","12":"PRO_INT_K_FUNC_POW","13":"PRO_INT_K_XN_FUNC","14":"PRO_INT_K_QUAD_L","15":"PRO_INT_K_QUAD_K","16":"PRO_INT_K_XQUAD","17":"PRO_INT_K_FUNC_INV","18":"PRO_INT_K_FUNC_DIV","19":"PRO_INT_K_FUNC_INVP","20":"PRO_INT_K_FUNC_FUNC","21":"PRO_INT_K_SECTAN","22":"PRO_INT_K_FUNCOVERFP","23":"PRO_INT_K_EXP_POW","24":"PRO_INT_K_SIN2COS2","25":"PRO_INT_K_LN_OVER_X","26":"PRO_INT_K_ONE_OVER_ULN"};
var FUNC_NAMES = {"1":"PRO_INT_F_SIN","2":"PRO_INT_F_COS","3":"PRO_INT_F_TAN","4":"PRO_INT_F_ASIN","5":"PRO_INT_F_ACOS","6":"PRO_INT_F_ATAN","8":"PRO_INT_F_SQRT","9":"PRO_INT_F_LN","10":"PRO_INT_F_LOG","11":"PRO_INT_F_EXP","13":"PRO_INT_F_SINH","14":"PRO_INT_F_COSH","15":"PRO_INT_F_TANH","16":"PRO_INT_F_ASINH","17":"PRO_INT_F_ACOSH","18":"PRO_INT_F_ATANH"};

/* ---- C 侧解析 ---- */
function parseC(src, defines) {
  var cs = src.indexOf("s_pro_int_table[AC_PRO_INT_TABLE_SIZE]");
  if (cs < 0) { throw new Error("C 表未定位: s_pro_int_table"); }
  var ce = src.indexOf("};", cs);
  var region = src.slice(cs, ce).replace(/\s+/g, " ");   /* SIN2COS2 条目跨两行 */
  var out = [];
  var m;
  var reE = /\{\s*(PRO_INT_K_\w+)\s*,\s*(PRO_INT_F_\w+|0)\s*,\s*(PRO_INT_\w+|-?\d+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}/g;
  while ((m = reE.exec(region))) {
    var kind = defines[m[1]];
    var func = m[2] === "0" ? 0 : defines[m[2]];
    var pow = m[3];
    pow = pow.charAt(0) === "P" ? defines[pow] : +pow;
    if (kind === undefined || func === undefined || pow === undefined || isNaN(pow)) {
      throw new Error("C 表条目 id 解析失败: " + m[0].slice(0, 60));
    }
    out.push([kind, func, pow, m[4]]);
  }
  return out;
}

/* ---- JS 侧解析 ---- */
function jsString(src, varName, stopVar) {
  var s = src.indexOf("var " + varName + " =");
  var e = src.indexOf(stopVar, s);
  var region = src.slice(s, e);
  var lits = [];
  var m;
  var reL = /"((?:[^"\\]|\\.)*)"/g;
  while ((m = reL.exec(region))) { lits.push(m[1]); }
  return lits.join("").replace(/\\x([0-9a-fA-F]{2})|\\u([0-9a-fA-F]{4})|\\(.)/g,
    function (all, hx, uu, ch) {
      if (hx) { return String.fromCharCode(parseInt(hx, 16)); }
      if (uu) { return String.fromCharCode(parseInt(uu, 16)); }
      return ch === "n" ? "\n" : ch === "t" ? "\t" : ch;
    });
}

function parseJS(src) {
  var spec = jsString(src, "_T_SPEC", "var _T_ANTI");
  var anti = jsString(src, "_T_ANTI", "var _T_OFFS");
  var rows = [];
  spec.split(";").forEach(function (row) {
    if (row.trim() === "") { return; }
    var p = row.split(",");
    rows.push([+p[0], +p[1], +p[2], null]);
  });
  var antis = anti.split("\x01");
  if (antis.length !== rows.length) {
    throw new Error("JS anti 数 " + antis.length + " != spec 数 " + rows.length);
  }
  rows.forEach(function (r, i) { r[3] = antis[i]; });
  return rows;
}

/* ---- 生成文本 ---- */
function emitC() {
  var lines = ["static const pro_int_entry_t s_pro_int_table[AC_PRO_INT_TABLE_SIZE] = {"];
  CANON.forEach(function (e) {
    var k = KIND_NAMES[e[0]];
    var f = e[1] === 0 ? "0" : FUNC_NAMES[e[1]];
    var padK = "                    ".slice(k.length);
    var padF = "                ".slice(f.length);
    lines.push("    { " + k + "," + padK + " " + f + "," + padF + " " + e[2] + ", " +
               JSON.stringify(e[3]) + " },");
  });
  lines.push("};");
  return lines.join("\n");
}

function emitJS() {
  var specParts = [];
  var row = [];
  CANON.forEach(function (e) {
    row.push(e[0] + "," + e[1] + "," + e[2]);
    if (row.length === 10) { specParts.push(row.join(";") + ";"); row = []; }
  });
  if (row.length > 0) { specParts.push(row.join(";") + ";"); }
  return "var _T_SPEC =\n    \"" + specParts.join("\" +\n    \"") + "\";\n\n" +
         "var _T_ANTI =\n    \"" + CANON.map(function (e) { return e[3]; }).join("\x01") + "\";";
}

/* ---- 主流程 ---- */
function main() {
  var mode = process.argv[2] || "--check";
  var cSrc = fs.readFileSync(C_FILE, "utf8");
  var jsSrc = fs.readFileSync(JS_FILE, "utf8");
  if (mode === "--emit-c") { process.stdout.write(emitC() + "\n"); return 0; }
  if (mode === "--emit-js") { process.stdout.write(emitJS() + "\n"); return 0; }
  if (mode !== "--check") {
    console.error("用法: node tools/gen-int-table.js [--check|--emit-c|--emit-js]");
    return 2;
  }
  /* id 表（C 侧事实来源，每次校验现读，不内置副本） */
  var defines = {};
  var m;
  var reD = /^#define (PRO_INT_[KF]\w*)\s+(\d+)/gm;
  while ((m = reD.exec(cSrc))) { defines[m[1]] = +m[2]; }
  var hSrc = fs.readFileSync(H_FILE, "utf8");
  var mh = hSrc.match(/#define AC_PRO_INT_TABLE_SIZE\s+(\d+)/);
  var tableSize = mh ? +mh[1] : -1;
  var problems = 0;
  if (tableSize !== CANON.length) {
    problems++;
    console.log("FAIL AC_PRO_INT_TABLE_SIZE=" + tableSize + " != CANON " + CANON.length);
  }
  var cE = null, jE = null;
  try { cE = parseC(cSrc, defines); } catch (e) { console.log("FAIL C 解析: " + e.message); }
  try { jE = parseJS(jsSrc); } catch (e) { console.log("FAIL JS 解析: " + e.message); }
  if (!cE || cE.length !== CANON.length) {
    problems++;
    console.log("FAIL C 侧条目数 " + (cE ? cE.length : "-") + " != " + CANON.length);
  }
  if (!jE || jE.length !== CANON.length) {
    problems++;
    console.log("FAIL JS 侧条目数 " + (jE ? jE.length : "-") + " != " + CANON.length);
  }
  var n = Math.min(CANON.length, cE ? cE.length : 0, jE ? jE.length : 0);
  var shown = 0;
  for (var i = 0; i < n; i++) {
    var c = cE[i], j = jE[i], w = CANON[i];
    var cBad = c[0] !== w[0] || c[1] !== w[1] || c[2] !== w[2] || c[3] !== w[3];
    var jBad = j[0] !== w[0] || j[1] !== w[1] || j[2] !== w[2] || j[3] !== w[3];
    if (cBad || jBad) {
      problems++;
      if (shown < 5) {
        shown++;
        console.log("FAIL 项 " + (i + 1) + (cBad ? " C=" + JSON.stringify(c) : "") +
                    (jBad ? " JS=" + JSON.stringify(j) : "") + " 规范=" + JSON.stringify(w));
      }
    }
  }
  if (problems === 0) {
    console.log("INT-TABLE CHECK OK: 规范表 " + CANON.length + " 项 = C s_pro_int_table = JS _T_SPEC/_T_ANTI（逐项一致）");
    return 0;
  }
  console.log("INT-TABLE CHECK FAILED: " + problems + " 处不一致");
  return 1;
}
process.exit(main());
