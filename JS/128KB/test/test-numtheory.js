/* ApolloCalc-CAS · JS/128KB 数论模块测试(v2.3.0 功能 1)
 * 被测模块:src/core_numtheory.js(自包含,不依赖既有 core)
 * 锚点:数学手册标准值 + C/128KB ac_numtheory 同款用例语义;
 *       大素数取公认事实值(2^31−1、F6 因子 67280421310721 等)。
 * 运行:node test/test-numtheory.js   (JS/128KB 目录下)
 * 全过输出:==== numtheory: N pass, 0 fail ====
 */
"use strict";

var NT = require("../src/core_numtheory.js");

var pass = 0, fail = 0;
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}

/* ==================== 一、gcd ==================== */
ok("gcd(48,18)=6", NT.gcd(48, 18) === 6, NT.gcd(48, 18));
ok("gcd(18,48)=6 交换律", NT.gcd(18, 48) === 6);
ok("gcd(0,5)=5", NT.gcd(0, 5) === 5);
ok("gcd(5,0)=5", NT.gcd(5, 0) === 5);
ok("gcd(0,0)=0", NT.gcd(0, 0) === 0);
ok("gcd(-48,18)=6 负号", NT.gcd(-48, 18) === 6);
ok("gcd(48,-18)=6 负号", NT.gcd(48, -18) === 6);
ok("gcd(7,13)=1 互素", NT.gcd(7, 13) === 1);
ok("gcd(2^53-1,2)=1 域上界奇数", NT.gcd(9007199254740991, 2) === 1);
ok("gcd 非整数拒绝", isNaN(NT.gcd(3.5, 7)) && NT.getError() === 1);
ok("gcd 越域拒绝(2^53)", isNaN(NT.gcd(Math.pow(2, 53), 1)) && NT.getError() === 1);

/* ==================== 二、lcm ==================== */
ok("lcm(4,6)=12", NT.lcm(4, 6) === 12);
ok("lcm(0,5)=0", NT.lcm(0, 5) === 0);
ok("lcm(-4,6)=12 负号", NT.lcm(-4, 6) === 12);
ok("lcm(2^51,3)=6.75e15 域内", NT.lcm(2251799813685248, 3) === 6755399441055744);
ok("lcm(2^52,3) 溢出→0+err2", NT.lcm(4503599627370496, 3) === 0 && NT.getError() === 2,
   "lcm=" + NT.lcm(4503599627370496, 3) + " err=" + NT.getError());
ok("lcm 非整数拒绝", isNaN(NT.lcm(1.5, 2)) && NT.getError() === 1);

/* ==================== 三、isPrime(6k±1 试除) ==================== */
ok("isPrime(-7)=0", NT.isPrime(-7) === 0);
ok("isPrime(0)=0 isPrime(1)=0", NT.isPrime(0) === 0 && NT.isPrime(1) === 0);
ok("isPrime(2)=1 isPrime(3)=1", NT.isPrime(2) === 1 && NT.isPrime(3) === 1);
ok("isPrime(4)=0 isPrime(5)=1", NT.isPrime(4) === 0 && NT.isPrime(5) === 1);
ok("isPrime(25)=0 轮边界平方", NT.isPrime(25) === 0);
ok("isPrime(49)=0 isPrime(121)=0", NT.isPrime(49) === 0 && NT.isPrime(121) === 0);
ok("isPrime(7919)=1 第1000个素数", NT.isPrime(7919) === 1);
ok("isPrime(1000003)=1", NT.isPrime(1000003) === 1);
ok("isPrime(1000000007)=1", NT.isPrime(1000000007) === 1);
ok("isPrime(2147483647)=1 M31", NT.isPrime(2147483647) === 1);
ok("isPrime(2147483646)=0", NT.isPrime(2147483646) === 0);
ok("isPrime(7.5) 非整数拒绝", NT.isPrime(7.5) === 0 && NT.getError() === 1);

/* ==================== 四、isPrimeMr(Miller-Rabin) ==================== */
ok("isPrimeMr(-3)=0 isPrimeMr(0)=0 isPrimeMr(1)=0",
   NT.isPrimeMr(-3) === 0 && NT.isPrimeMr(0) === 0 && NT.isPrimeMr(1) === 0);
ok("isPrimeMr(2)=1 命中小素数表", NT.isPrimeMr(2) === 1);
ok("isPrimeMr(37)=1 底数表尾", NT.isPrimeMr(37) === 1);
ok("isPrimeMr(41)=1 表后首素数", NT.isPrimeMr(41) === 1);
ok("isPrimeMr(2047)=0 强伪素(base2)", NT.isPrimeMr(2047) === 0);
ok("isPrimeMr Carmichael 561/1105/2465/6601 全 0",
   NT.isPrimeMr(561) === 0 && NT.isPrimeMr(1105) === 0 &&
   NT.isPrimeMr(2465) === 0 && NT.isPrimeMr(6601) === 0);
ok("isPrimeMr(2147483647)=1 M31", NT.isPrimeMr(2147483647) === 1);
ok("isPrimeMr(274177)=1 F6 小因子", NT.isPrimeMr(274177) === 1);
ok("isPrimeMr(67280421310721)=1 F6 大因子", NT.isPrimeMr(67280421310721) === 1);
ok("isPrimeMr(2^53-1)=0 M53 合数", NT.isPrimeMr(9007199254740991) === 0);
ok("isPrimeMr(9007199254740881)=1 域顶素数", NT.isPrimeMr(9007199254740881) === 1);
ok("isPrimeMr(9007199254740992) 越域拒绝",
   NT.isPrimeMr(9007199254740992) === 0 && NT.getError() === 1);
/* 双实现的确定性互证:2..2000 全域一致(试除在该范围即确定性) */
var sweepOK = true;
var sv;
for (sv = 2; sv <= 2000; sv++) {
  if (NT.isPrime(sv) !== NT.isPrimeMr(sv)) { sweepOK = false; break; }
}
ok("isPrime×isPrimeMr 2..2000 互证一致", sweepOK, "first mismatch @" + sv);

/* ==================== 五、powMod / invMod ==================== */
ok("powMod(2,10,1000)=24", NT.powMod(2, 10, 1000) === 24);
ok("powMod(2,52,53)=1 费马小定理", NT.powMod(2, 52, 53) === 1);
ok("powMod(67280421310721,1,2)=1", NT.powMod(67280421310721, 1, 2) === 1);
ok("powMod mod=1 拒绝", NT.powMod(2, 3, 1) === 0 && NT.getError() === 1);
ok("powMod 负底数拒绝", NT.powMod(-2, 3, 7) === 0 && NT.getError() === 1);
ok("invMod(3,7)=5", NT.invMod(3, 7) === 5);
ok("invMod(10,17)=12", NT.invMod(10, 17) === 12);
ok("invMod 逆元自洽 (3·5)%7=1", (3 * NT.invMod(3, 7)) % 7 === 1);
ok("invMod(6,9)=0 无逆元", NT.invMod(6, 9) === 0);
ok("invMod m=1 拒绝", NT.invMod(5, 1) === 0 && NT.getError() === 1);
ok("invMod 负 a 归一 invMod(-3,7)=2", NT.invMod(-3, 7) === 2 && ((-3 * 2) % 7 + 7) % 7 === 1);

/* ==================== 六、factorize ==================== */
ok("factorize(720) 状态", NT.factorize(720) === 1 && NT.factorCount() === 3);
ok("factorize(720)=2^4·3^2·5",
   NT.factorPrimeAt(0) === 2 && NT.factorExpAt(0) === 4 &&
   NT.factorPrimeAt(1) === 3 && NT.factorExpAt(1) === 2 &&
   NT.factorPrimeAt(2) === 5 && NT.factorExpAt(2) === 1);
ok("factorize(97) 单素数", NT.factorize(97) === 1 && NT.factorCount() === 1 &&
   NT.factorPrimeAt(0) === 97 && NT.factorExpAt(0) === 1);
ok("factorize(2^52)=2^52", NT.factorize(4503599627370496) === 1 &&
   NT.factorCount() === 1 && NT.factorPrimeAt(0) === 2 && NT.factorExpAt(0) === 52);
ok("factorize(67280421310721) 单大素数", NT.factorize(67280421310721) === 1 &&
   NT.factorCount() === 1 && NT.factorPrimeAt(0) === 67280421310721);
ok("factorize(1000003^2)", NT.factorize(1000006000009) === 1 &&
   NT.factorCount() === 1 && NT.factorPrimeAt(0) === 1000003 && NT.factorExpAt(0) === 2);
/* Project Euler #3 锚点:600851475143 = 71 × 839 × 1471 × 6857 */
ok("factorize(600851475143) 状态", NT.factorize(600851475143) === 1 && NT.factorCount() === 4);
ok("factorize(600851475143) 因子链",
   NT.factorPrimeAt(0) === 71 && NT.factorPrimeAt(1) === 839 &&
   NT.factorPrimeAt(2) === 1471 && NT.factorPrimeAt(3) === 6857);
ok("factorize 因子均过 isPrime(自洽)",
   NT.isPrime(NT.factorPrimeAt(0)) === 1 && NT.isPrime(NT.factorPrimeAt(3)) === 1);
ok("factorize(1) 空分解", NT.factorize(1) === 1 && NT.factorCount() === 0);
ok("factorize(0)/(−5)/7.5 拒绝",
   NT.factorize(0) === 0 && NT.getError() === 1 &&
   NT.factorize(-5) === 0 && NT.getError() === 1 &&
   NT.factorize(7.5) === 0 && NT.getError() === 1);
NT.factorReset();
ok("factorReset 清空", NT.factorCount() === 0 && NT.factorPrimeAt(0) === 0);

/* ==================== 七、nCr ==================== */
ok("nCr(0,0)=1 nCr(5,5)=1", NT.nCr(0, 0) === 1 && NT.nCr(5, 5) === 1);
ok("nCr(5,2)=10", NT.nCr(5, 2) === 10);
ok("nCr(10,3)=120", NT.nCr(10, 3) === 120);
ok("nCr(52,5)=2598960", NT.nCr(52, 5) === 2598960);
ok("nCr(40,20)=137846528820", NT.nCr(40, 20) === 137846528820);
ok("nCr(50,25)=126410606437752", NT.nCr(50, 25) === 126410606437752);
ok("nCr(52,26)=495918532948104", NT.nCr(52, 26) === 495918532948104);
ok("nCr 对称性 C(60,7)=C(60,53)", NT.nCr(60, 7) === NT.nCr(60, 53) && NT.nCr(60, 7) === 386206920);
ok("nCr(66,33) 溢出→−1+err2(int64 可容,本域不可)",
   NT.nCr(66, 33) === -1 && NT.getError() === 2);
ok("nCr(70,35) 溢出→−1+err2", NT.nCr(70, 35) === -1 && NT.getError() === 2);
ok("nCr r>n 拒绝", NT.nCr(5, 6) === -1 && NT.getError() === 1);
ok("nCr n<0 拒绝", NT.nCr(-4, 2) === -1 && NT.getError() === 1);
ok("nCr 非整数拒绝", NT.nCr(5.5, 2) === -1 && NT.getError() === 1);

/* ==================== 八、nPr ==================== */
ok("nPr(5,0)=1", NT.nPr(5, 0) === 1);
ok("nPr(5,2)=20", NT.nPr(5, 2) === 20);
ok("nPr(10,3)=720", NT.nPr(10, 3) === 720);
ok("nPr(52,5)=311875200", NT.nPr(52, 5) === 311875200);
ok("nPr(20,10)=670442572800", NT.nPr(20, 10) === 670442572800);
ok("nPr(28,10)=47621141568000 域内", NT.nPr(28, 10) === 47621141568000);
ok("nPr(30,12) 溢出→−1+err2(第12因子·19 已越 4.1e16)",
   NT.nPr(30, 12) === -1 && NT.getError() === 2);
ok("nPr r>n 拒绝", NT.nPr(3, 4) === -1 && NT.getError() === 1);
ok("nPr n<0 拒绝", NT.nPr(-1, 0) === -1 && NT.getError() === 1);

/* ==================== 九、零 GC 与复位 ==================== */
ok("加载期分配 = 3(MR 表 + 因子表 + 指数表)", NT.allocCount() === 3, NT.allocCount());
NT.reset();
ok("reset 清错误与计数", NT.getError() === 0 && NT.getOpCount() === 0 &&
   NT.factorCount() === 0);

console.log("==== numtheory: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
