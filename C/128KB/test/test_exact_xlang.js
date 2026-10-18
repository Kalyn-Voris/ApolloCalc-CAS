/* ============================================================
 * ApolloCalc-CAS · test_exact_xlang.js — 精确形式跨语言一致性驱动（C 128KB 第 1 轮）
 * 基准：JS/128KB/src/core_exact.js（五分量模型，只读，严禁修改 JS 目录）
 * 覆盖：XL01-XL12 纯分数/纯根式回归（期望与 64KB 一致，字段重命名）+
 *       五分量新能力（混合形式 / 科学计数法 / 同类根式合并 / 共轭除法 /
 *       异类根式失效）。全部期望值由本脚本在 JS 圣经上输出，
 *       C/128KB/test/test_exact.c 逐一转录（勿凭记忆改值）。
 * 运行：node test/test_exact_xlang.js
 * 输出：逐用例 rat/radNum/rad/den/exp/valid/approx 对照表
 * ============================================================ */
/* 注意：本文件必须保持非严格模式（eval 展开的 core_exact.js 依赖隐式全局） */
var fs = require("fs");
var path = require("path");

/* 复刻 core_parser.js 的精确数值全局声明（core_exact.js 的引用目标） */
var exactRat = 0;
var exactRadNum = 0;
var exactRad = 1;
var exactDen = 1;
var exactExp = 0;
var exactIsValid = false;
var approxValue = 0.0;

/* 在本作用域内展开基准实现（非严格 eval：函数声明进入当前作用域） */
var src = fs.readFileSync(path.join(__dirname, "..", "..", "..", "JS", "128KB", "src", "core_exact.js"), "utf8");
eval(src);

var pass = 0;
var fail = 0;

function chk(id, msg, rat, rn, rad, den, exp, v, av) {
    var ok = exactRat === rat && exactRadNum === rn && exactRad === rad &&
             exactDen === den && exactExp === exp && exactIsValid === v &&
             Math.abs(approxValue - av) <= 1e-9;
    if (ok) { pass++; console.log("  OK   " + id + " " + msg); }
    else {
        fail++;
        console.log("  FAIL " + id + " " + msg +
            " got=(" + exactRat + "+" + exactRadNum + "rad" + exactRad + ")/" +
            exactDen + "e" + exactExp + " valid=" + (exactIsValid ? 1 : 0) +
            " approx=" + approxValue);
    }
}

/* ==================== XL01-XL12：纯分数 / 纯根式回归 ==================== */
exactReset();
exactSetFraction(2, 4);
chk("XL01", "set_fraction(2,4)=1/2", 1, 0, 1, 2, 0, true, 0.5);
exactSetFraction(-6, 3);
chk("XL02", "set_fraction(-6,3)=-2", -2, 0, 1, 1, 0, true, -2.0);
exactSetRadical(2, 8);
chk("XL03", "set_radical(2,8)=4rad2", 0, 4, 2, 1, 0, true, 4 * Math.sqrt(2));
exactSetRadical(1, 4);
chk("XL04", "set_radical(1,4)=2", 2, 0, 1, 1, 0, true, 2.0);
exactSetFraction(1, 2); exactAdd(1, 0, 1, 3);
chk("XL05", "1/2+1/3=5/6", 5, 0, 1, 6, 0, true, 5 / 6);
exactSetRadical(1, 2); exactMul(0, 1, 2, 1);
chk("XL07", "sqrt2*sqrt2=2", 2, 0, 1, 1, 0, true, 2.0);
exactSetFraction(3, 1); exactDiv(1, 0, 1, 2);
chk("XL08", "3/(1/2)=6", 6, 0, 1, 1, 0, true, 6.0);
exactSetRadical(2, 3); exactDiv(0, 1, 3, 2);
chk("XL09", "2rad3/(rad3/2)=4", 4, 0, 1, 1, 0, true, 4.0);
exactSetFraction(1, 2); exactSub(1, 0, 1, 3);
chk("XL10", "1/2-1/3=1/6", 1, 0, 1, 6, 0, true, 1 / 6);
exactFromDiscriminant(8.0);
chk("XL11", "sqrt(8)=2rad2", 0, 2, 2, 1, 0, true, 2 * Math.sqrt(2));
exactFromDiscriminant(0.25);
chk("XL12", "sqrt(0.25)=1/2", 1, 0, 1, 2, 0, true, 0.5);

/* ==================== 五分量新能力：混合形式 ==================== */
exactReset();
exactSetMixed(1, 1, 5, 2);
chk("MX01", "golden (1+rad5)/2", 1, 1, 5, 2, 0, true, (1 + Math.sqrt(5)) / 2);
exactSetMixed(3, -2, 7, 5);
chk("MX02", "(3-2rad7)/5", 3, -2, 7, 5, 0, true, (3 - 2 * Math.sqrt(7)) / 5);
exactSetMixed(1, 1, 8, 2);
chk("MX03", "sqrt8 extract -> (1+2rad2)/2", 1, 2, 2, 2, 0, true, (1 + 2 * Math.sqrt(2)) / 2);
exactSetMixed(2, 4, 9, 4);
chk("MX04", "perfect-sq chain -> 7/2", 7, 0, 1, 2, 0, true, 3.5);
exactSetMixed(0, 0, 5, 3);
chk("MX05", "zero -> 0/0/1/1/0", 0, 0, 1, 1, 0, true, 0);
exactSetMixed(1, 1, 5, -2);
chk("MX06", "neg den -> sign flipped", -1, -1, 5, 2, 0, true, -(1 + Math.sqrt(5)) / 2);
exactSetMixed(5, 9, 0, 2);
chk("MX07", "c=0 -> pure fraction 5/2", 5, 0, 1, 2, 0, true, 2.5);
exactSetMixed(1, 1, 5, 0);
chk("MX08", "d=0 invalid, state untouched", 5, 0, 1, 2, 0, false, 2.5);
exactSetMixed(1, 1, -5, 2);
chk("MX09", "c<0 invalid, state untouched", 5, 0, 1, 2, 0, false, 2.5);

/* ==================== 五分量新能力：科学计数法 ==================== */
exactReset();
exactSetSci(25, 3);
chk("SC01", "sci(25,3)=2.5e4", 25, 0, 1, 1, 3, true, 25000);
exactSetSci(0, 5);
chk("SC02", "sci(0,5) zero keeps exp=0", 0, 0, 1, 1, 0, true, 0);
exactSetSci(1, -3);
chk("SC03", "sci(1,-3)=1e-3", 1, 0, 1, 1, -3, true, 0.001);
exactSetSci(25, 3); exactAdd(1, 0, 1, 1);
chk("SC04", "sci then add -> invalid (exp gate)", 25, 0, 1, 1, 3, false, 25000);

/* ==================== 五分量新能力：四则 ==================== */
exactReset();
exactSetMixed(1, 1, 5, 2); exactAdd(1, -1, 5, 2);
chk("OP01", "golden + conj = 1", 1, 0, 1, 1, 0, true, 1);
exactSetMixed(1, 1, 5, 2); exactMul(1, -1, 5, 2);
chk("OP02", "golden * conj = -1", -1, 0, 1, 1, 0, true, -1);
exactSetMixed(1, 1, 5, 2); exactAdd(1, 0, 1, 1);
chk("OP03", "golden + 1 = (3+rad5)/2", 3, 1, 5, 2, 0, true, (3 + Math.sqrt(5)) / 2);
exactSetRadical(1, 2); exactAdd(0, 1, 2, 1);
chk("OP04", "sqrt2+sqrt2 = 2sqrt2", 0, 2, 2, 1, 0, true, 2 * Math.sqrt(2));
exactSetRadical(1, 2); exactAdd(1, 1, 3, 1);
chk("OP05", "sqrt2+sqrt3 invalid", 0, 1, 2, 1, 0, false, Math.sqrt(2));
exactSetRadical(1, 2); exactMul(1, 1, 3, 1);
chk("OP06", "sqrt2*sqrt3 invalid (three-radical path)", 0, 1, 2, 1, 0, false, Math.sqrt(2));
exactReset();
exactSetFraction(1, 1); exactDiv(1, 1, 2, 1);
chk("OP07", "1/(1+rad2)=rad2-1", -1, 1, 2, 1, 0, true, Math.sqrt(2) - 1);
exactSetRadical(1, 2); exactAdd(1, 0, 1, 2);
chk("OP08", "rad + frac = (1+2rad2)/2", 1, 2, 2, 2, 0, true, (1 + 2 * Math.sqrt(2)) / 2);
exactReset();
exactSetFraction(1, 1); exactDiv(3, 2, 7, 1);
chk("OP09", "1/(3+2rad7) conjugate", -3, 2, 7, 19, 0, true, (-3 + 2 * Math.sqrt(7)) / 19);
exactSetMixed(3, 2, 7, 5); exactDiv(2, 0, 1, 1);
chk("OP10", "mixed / 2", 3, 2, 7, 10, 0, true, (3 + 2 * Math.sqrt(7)) / 10);
exactSetMixed(1, 1, 5, 2); exactMul(2, 0, 1, 1);
chk("OP11", "golden * 2 = 1+rad5", 1, 1, 5, 1, 0, true, 1 + Math.sqrt(5));
exactSetMixed(1, 1, 2, 1); exactMul(-1, 1, 2, 1);
chk("OP12", "(1+rad2)(rad2-1)=1", 1, 0, 1, 1, 0, true, 1);
exactSetMixed(1, 1, 2, 1); exactMul(2, 1, 2, 1);
chk("OP13", "(1+rad2)(2+rad2)=4+3rad2", 4, 3, 2, 1, 0, true, 4 + 3 * Math.sqrt(2));
exactSetMixed(1, 1, 5, 2); exactDiv(3, 0, 1, 1);
chk("OP14", "golden / 3 = (1+rad5)/6", 1, 1, 5, 6, 0, true, (1 + Math.sqrt(5)) / 6);

/* ==================== 语义升级确认（64KB 旧期望的翻转点） ==================== */
exactReset();
exactSetRadical(0, 5);
chk("LG01", "set_radical(0,5): rad normalized to 1 (64KB kept 5)", 0, 0, 1, 1, 0, true, 0);
exactReset();
exactSetFraction(3, 1); exactMul(1, 0, 0, 1);
chk("LG02", "mul rad2=0 invalid (64KB was zero path)", 3, 0, 1, 1, 0, false, 3);

console.log("==== exact xlang (JS/128KB bible): " + pass + " pass, " + fail + " fail ====");
process.exit(fail > 0 ? 1 : 0);
