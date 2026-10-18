/* ============================================================
 * ApolloCalc-CAS · test_exact_xlang.js — 精确形式跨语言一致性驱动
 * 基准：JS/64KB/src/core_exact.js（只读，严禁修改 JS 目录）
 * 用例 XL01-XL12 与 C/64KB/test/test_exact.c 逐一对应，
 * 两侧的 g_exact_num/den/rad/valid 必须完全一致（approx 1e-9 容差）。
 * 运行：node test/test_exact_xlang.js
 * ============================================================ */
/* 注意：本文件必须保持非严格模式（eval 展开的 core_exact.js 依赖隐式全局） */
var fs = require("fs");
var path = require("path");

/* 复刻 core_parser.js 的精确数值全局声明（core_exact.js 的引用目标） */
var exactNum = 0;
var exactDen = 1;
var exactRad = 1;
var exactIsValid = false;
var approxValue = 0.0;

/* 在本作用域内展开基准实现（非严格 eval：函数声明进入当前作用域） */
var src = fs.readFileSync(path.join(__dirname, "..", "..", "..", "JS", "64KB", "src", "core_exact.js"), "utf8");
eval(src);

var pass = 0;
var fail = 0;

function chk(id, msg, n, d, r, v, av) {
    var ok = exactNum === n && exactDen === d && exactRad === r &&
             exactIsValid === v && Math.abs(approxValue - av) <= 1e-9;
    if (ok) { pass++; console.log("  OK   " + id + " " + msg); }
    else {
        fail++;
        console.log("  FAIL " + id + " " + msg +
            " got=(" + exactNum + "/" + exactDen + ")rad=" + exactRad +
            " valid=" + exactIsValid + " approx=" + approxValue);
    }
}

console.log("==== JS 64KB exact baseline: XL01-XL12 ====");

/* XL01 */ exactSetFraction(2, 4);
chk("XL01", "setFraction(2,4)=1/2", 1, 2, 1, true, 0.5);

/* XL02 */ exactSetFraction(-6, 3);
chk("XL02", "setFraction(-6,3)=-2", -2, 1, 1, true, -2);

/* XL03 */ exactSetRadical(2, 8);          /* 2·sqrt(8) = 4·sqrt(2) */
chk("XL03", "setRadical(2,8)=4rad2", 4, 1, 2, true, 4 * Math.SQRT2);

/* XL04 */ exactSetRadical(1, 4);          /* sqrt(4) = 2 */
chk("XL04", "setRadical(1,4)=2", 2, 1, 1, true, 2);

/* XL05 */ exactSetFraction(1, 2); exactAdd(1, 3, 1);
chk("XL05", "1/2+1/3=5/6", 5, 6, 1, true, 5 / 6);

/* XL06 */ exactSetRadical(1, 2); exactAdd(1, 1, 2);
chk("XL06", "rad+rad add invalid, triple retained", 1, 1, 2, false, Math.SQRT2);

/* XL07 */ exactSetRadical(1, 2); exactMul(1, 1, 2);
chk("XL07", "sqrt2*sqrt2=2", 2, 1, 1, true, 2);

/* XL08 */ exactSetFraction(3, 1); exactDiv(1, 2, 1);
chk("XL08", "3/(1/2)=6", 6, 1, 1, true, 6);

/* XL09 */ exactSetRadical(2, 3); exactDiv(1, 2, 3);
chk("XL09", "2rad3/(rad3/2)=4", 4, 1, 1, true, 4);

/* XL10 */ exactSetFraction(1, 2); exactSub(1, 3, 1);
chk("XL10", "1/2-1/3=1/6", 1, 6, 1, true, 1 / 6);

/* XL11 */ exactFromDiscriminant(8);
chk("XL11", "sqrt(8)=2rad2", 2, 1, 2, true, 2 * Math.SQRT2);

/* XL12 */ exactFromDiscriminant(0.25);
chk("XL12", "sqrt(0.25)=1/2", 1, 2, 1, true, 0.5);

console.log("==== JS 64KB exact baseline: " + pass + " pass, " + fail + " fail ====");
process.exit(fail > 0 ? 1 : 0);
