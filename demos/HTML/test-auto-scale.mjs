// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · demos/HTML Y 轴自适应量程沙箱测试
 * 用法：node demos/HTML/test-auto-scale.mjs   （仅依赖 Node 内置 API，退出码 0 = 全过）
 *
 * A) 结构：draw() 首遍收集 minY/maxY、10% padding、常数 ±1、状态栏 toFixed(2)、网格不再硬编码
 * B) 数值：从 index.html 原地抽取 setView/niceStep/fmtTick 求值，验证坐标映射与刻度步长
 * C) 成型：按页面同一公式复算 sin(RAD/DEG)/x^2/常数，核对任务书要求的视野区间
 * D) 零 GC：首遍求值循环内无字符串/对象/数组分配
 */

import fs from 'node:fs';

const HTML = new URL('./index.html', import.meta.url);
const html = fs.readFileSync(HTML, 'utf8');

let fails = 0;
function ok(cond, name) {
  console.log((cond ? 'PASS  ' : 'FAIL  ') + name);
  if (!cond) fails++;
}

function extract(name) {
  const re = new RegExp('  function ' + name + '\\([^)]*\\) \\{([\\s\\S]*?)\\n  \\}');
  const m = html.match(re);
  return m ? m[0] : '';
}

/* ---------- A) 结构 ---------- */
ok(/var minY = 0, maxY = 0, yRange = 0;/.test(html), 'draw() declares minY/maxY/yRange');
ok(/if \(drawn === 0\) \{\s*minY = y; maxY = y;\s*\}/.test(html), 'first valid point seeds minY/maxY');
ok(/if \(maxY === minY\) \{\s*setView\(minY - 1, maxY \+ 1\);/.test(html),
  'constant function gets ±1.0 margin');
ok(/setView\(minY - yRange \* 0\.1, maxY \+ yRange \* 0\.1\)/.test(html),
  '10% padding added on both sides');
ok(/SY = \(H - 2 \* PAD\) \/ \(maxY - minY\);/.test(html), 'SY derived from adaptive range');
ok(/OY = H - PAD \+ minY \* SY;/.test(html), 'OY anchored on minY (min → bottom margin)');
ok(/T\.view \+ V_MIN\.toFixed\(2\) \+ T\.viewSep \+ V_MAX\.toFixed\(2\)/.test(html),
  'status bar shows the real y viewport');
ok(/step = niceStep\(V_MAX - V_MIN\);/.test(html), 'grid step derived from adaptive range');
ok(!/for \(gy = -5; gy <= 5; gy\+\+\)/.test(html), 'hard-coded -5..5 grid removed');
ok(!/var LBL = /.test(html), 'hard-coded x tick labels removed');
ok(/V_MIN\.toFixed\(2\)/.test(html) && /V_MAX\.toFixed\(2\)/.test(html), 'viewport printed with 2 decimals');

/* ---------- B) 数值（原地抽取真实源码求值） ---------- */
const setViewSrc = extract('setView');
const niceSrc = extract('niceStep');
const fmtSrc = extract('fmtTick');
ok(!!setViewSrc && !!niceSrc && !!fmtSrc, 'setView/niceStep/fmtTick extracted from index.html');
if (!setViewSrc || !niceSrc || !fmtSrc) {
  console.log(fails + ' FAILED');
  process.exit(1);
}

const api = new Function(
  'var H = 540, PAD = 34, SY = 1, OY = 0, V_MIN = 0, V_MAX = 0;\n' +
  setViewSrc + '\n' + niceSrc + '\n' + fmtSrc + '\n' +
  'return { setView: setView, niceStep: niceStep, fmtTick: fmtTick, ' +
  'state: function () { return { V_MIN: V_MIN, V_MAX: V_MAX, SY: SY, OY: OY }; } };'
)();

/* 映射契约：minY→H-PAD（下边距），maxY→PAD（上边距），y 轴铺满整幅高度 */
api.setView(-1.2, 1.2);
let s = api.state();
ok(Math.abs((s.OY - (-1.2) * s.SY) - (540 - 34)) < 1e-6, 'minY maps to H - PAD');
ok(Math.abs((s.OY - 1.2 * s.SY) - 34) < 1e-6, 'maxY maps to PAD');
ok(Math.abs(s.SY - 472 / 2.4) < 1e-9, 'SY fills full height (equal-ratio ratio broken)');

/* 常数函数 y=5：视野 [4,6]，直线落在画布正中 */
api.setView(4, 6);
s = api.state();
ok(Math.abs((s.OY - 5 * s.SY) - 270) < 1e-6, 'y = 5 lands at canvas center');

/* 退化量程防护：min == max 也不产生 Inf/NaN */
api.setView(3, 3);
s = api.state();
ok(s.V_MAX > s.V_MIN && isFinite(s.SY) && isFinite(s.OY), 'degenerate range guarded');

/* 1/2/5 系列步长 */
const near = (a, b) => Math.abs(a - b) <= Math.max(Math.abs(b) * 1e-9, 1e-12);
ok(near(api.niceStep(2), 0.5), 'niceStep(2) == 0.5');
ok(near(api.niceStep(1), 0.2), 'niceStep(1) == 0.2');
ok(near(api.niceStep(100), 20), 'niceStep(100) == 20');
ok(near(api.niceStep(0.347), 0.05), 'niceStep(0.347) == 0.05');
ok(api.niceStep(0) === 1 && api.niceStep(NaN) === 1, 'niceStep guards 0 / NaN');

/* 刻度文本：不留浮点尾巴、不留 ".0" */
ok(api.fmtTick(4, 0.5) === '4', 'fmtTick(4) trims trailing zeros');
ok(api.fmtTick(-10, 5) === '-10', 'fmtTick(-10) == -10');
ok(api.fmtTick(0.30000000000000004, 0.1) === '0.3', 'fmtTick kills float noise');
ok(api.fmtTick(0.05, 0.05) === '0.05', 'fmtTick keeps small magnitudes');

/* ---------- C) 成型：按页面同一公式复算视野 ---------- */
const X_MIN = -10, X_MAX = 10, N = 200;
function viewFor(f) {
  var drawn = 0, minY = 0, maxY = 0, i, x, y;
  for (i = 0; i <= N; i++) {
    x = X_MIN + i * (X_MAX - X_MIN) / N;
    y = f(x);
    if (y === y && y > -1e6 && y < 1e6) {
      if (drawn === 0) { minY = y; maxY = y; }
      else { if (y < minY) minY = y; if (y > maxY) maxY = y; }
      drawn++;
    }
  }
  if (drawn === 0) return null;
  if (maxY === minY) return [minY - 1, maxY + 1];
  var r = maxY - minY;
  return [minY - r * 0.1, maxY + r * 0.1];
}

const rad = viewFor(Math.sin);
ok(Math.abs(rad[1] - 1.1995) < 0.01 && Math.abs(rad[0] + 1.1995) < 0.01,
  'sin(x) RAD → y ≈ [-1.20, 1.20]');
const deg = viewFor((x) => Math.sin(x * Math.PI / 180));
ok(Math.abs(deg[1] - 0.2084) < 0.01 && Math.abs(deg[0] + 0.2084) < 0.01,
  'sin(x) DEG → y ≈ [-0.21, 0.21] (near-linear diagonal)');
const par = viewFor((x) => x * x);
ok(Math.abs(par[0] + 10) < 1e-9 && Math.abs(par[1] - 110) < 1e-9,
  'x^2 → y = [-10, 110] (bottom padded, opens upward)');
const flat = viewFor(() => 5);
ok(flat[0] === 4 && flat[1] === 6, 'y = 5 → y = [4, 6] (centered horizontal line)');
ok(viewFor((x) => Math.sqrt(-1 - x * x)) === null, 'all-NaN expression → no view (default path)');

/* ---------- D) 零 GC 纪律 ---------- */
const start = html.indexOf('/* 第一遍');
const end = html.indexOf('/* 由数据决定');
const scan = start >= 0 && end > start ? html.slice(start, end) : '';
ok(scan.length > 0, 'first-pass scan block located');
ok(!/toFixed|`|"|'|\bnew\b|\[/.test(scan), 'first-pass scan allocates nothing');

console.log(fails === 0 ? 'ALL PASS' : fails + ' FAILED');
process.exit(fails === 0 ? 0 : 1);
