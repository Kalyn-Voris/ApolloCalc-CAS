/*
 * Miller-Rabin 参考实现（开发过程遗留，用于验证 C 版 ac_nt_is_prime_mr）
 *
 * 用途：
 *   - 用 BigInt 实现，与 C 版 int64 实现形成"高精度参考-受限实现"对照
 *   - 自带 trial() 对拍函数，0..5000 全量验证
 *   - 用固定底数 {2,3,5,7,11,13,17,19,23,29,31,37} 对 64 位整数确定性
 *
 * 运行：node docs/refs/miller-rabin-reference.js
 *
 * 对应内核实现：C/128KB/src/ac_numtheory.c 的 ac_nt_is_prime_mr
 * 已归档：原位于 C/128KB/mr_ref.js（2026-10-13 移入）
 */

// 原文件首行（保留）：
// mr_ref.js — Miller-Rabin 参考（修正版：x 从 1 起方乘 + mulmod 带全参）
function mulmod(a, b, m) {
  let r = 0n;
  a %= m; b %= m;
  while (b > 0n) {
    if (b & 1n) r = (r + a) % m;
    a = (a + a) % m;
    b >>= 1n;
  }
  return r;
}
const BASES = [2n, 3n, 5n, 7n, 11n, 13n, 17n, 19n, 23n, 29n, 31n, 37n];
function isPrimeMR(n) {
  if (n < 2n) return false;
  for (const p of BASES) {
    if (n === p) return true;
    if (n % p === 0n) return false;
  }
  let d = n - 1n, s = 0n;
  while (d % 2n === 0n) { d >>= 1n; s++; }
  for (const a of BASES) {
    if (a >= n) continue;
    let x = 1n, e = d, base = a;
    while (e > 0n) {
      if (e & 1n) x = mulmod(x, base, n);
      base = mulmod(base, base, n);
      e >>= 1n;
    }
    if (x === 1n || x === n - 1n) continue;
    let ok = false;
    for (let r = 1n; r < s; r++) {
      x = mulmod(x, x, n);
      if (x === n - 1n) { ok = true; break; }
    }
    if (!ok) return false;
  }
  return true;
}
function trial(n) {
  if (n < 2n) return false;
  for (let i = 2n; i * i <= n; i++) if (n % i === 0n) return false;
  return true;
}
console.log('1e9+9       :', isPrimeMR(1000000009n), '(已知素数)');
console.log('1e9+7       :', isPrimeMR(1000000007n), '(已知素数)');
console.log('3215031751  :', isPrimeMR(3215031751n), '(spsp 2,3,5,7 → 应 false)');
console.log('2047        :', isPrimeMR(2047n));
console.log('561         :', isPrimeMR(561n));
console.log('2^61-1      :', isPrimeMR(2305843009213693951n));
console.log('max<2^63    :', isPrimeMR(9223372036854775783n));
let bad = 0;
for (let n = 0n; n <= 5000n; n++) if (isPrimeMR(n) !== trial(n)) bad++;
console.log('mismatch 0..5000:', bad);
