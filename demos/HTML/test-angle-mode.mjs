// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
/* ApolloCalc · demos/HTML 角度制切换沙箱测试
 * 用法：node demos/HTML/test-angle-mode.mjs   （仅依赖 Node 内置 API，退出码 0 = 全过）
 *
 * 三层断言：
 *   A) 内核语义：RAD/DEG 切换真的改变 sin 求值（DEG sin(90)=1、RAD sin(π/2)=1）
 *   B) 页面结构：下拉框存在且默认 RAD、change 处理器绑定 setAngleMode+draw、
 *      选项文案走 i18n 字典、状态栏后缀跟随模式
 *   C) 零 GC 纪律：切换处理器内无 new / 模板串 / 数组 / 对象字面量 —— 切换路径零分配
 */

import fs from 'node:fs';

const KERNEL = new URL('../../JS/128KB/src/core_kernel.js', import.meta.url);
const HTML = new URL('./index.html', import.meta.url);

/* 内核 sin/cos 走 const LUT 线性插值，最坏误差 h²/8 ≈ 4.7e-6，容差取 1e-5 */
const TOL = 1e-5;

let fails = 0;
function ok(cond, name) {
  console.log((cond ? 'PASS  ' : 'FAIL  ') + name);
  if (!cond) fails++;
}

const K = (await import(KERNEL)).default;

/* ---------- A) 内核语义 ---------- */
K.setAngleMode('DEG');
if (K.parseExpression('sin(x)') !== 0) {
  ok(false, 'parse sin(x)');
} else {
  ok(Math.abs(K.evalExpressionAt(90) - 1) <= TOL, 'DEG: sin(90) == 1');
}

K.setAngleMode('RAD');
ok(Math.abs(K.evalExpressionAt(Math.PI / 2) - 1) <= TOL, 'RAD: sin(PI/2) == 1');

/* π/2 的 4 位近似：|1.5708 - π/2| ≈ 3.7e-6，sin 偏差同级，仍在容差带内 */
K.setAngleMode('RAD');
ok(Math.abs(K.evalExpressionAt(1.5708) - 1) <= TOL, 'RAD: sin(1.5708) ~= 1');

K.setAngleMode('DEG');
ok(K.getAngleMode() === 'DEG', 'getAngleMode reflects DEG');
K.setAngleMode('RAD');
ok(K.getAngleMode() === 'RAD', 'getAngleMode reflects RAD');

/* ---------- B) 页面结构 ---------- */
const html = fs.readFileSync(HTML, 'utf8');

ok(/<select id="anglemode" aria-label="Angle mode">/.test(html), 'select#anglemode present');
ok(/<option id="opt-rad" value="RAD" selected>/.test(html), 'default option is RAD');
ok(/<option id="opt-deg" value="DEG">/.test(html), 'DEG option present');

const handler = html.match(/selMode\.addEventListener\('change', function \(\) \{([\s\S]*?)\n  \}\);/);
ok(!!handler, 'change handler bound');
ok(!!handler && /K\.setAngleMode\(m\)/.test(handler[1]) && /draw\(\)/.test(handler[1]),
  'handler calls setAngleMode + draw (canvas redraw)');
ok(/optRad\.textContent = T\.modeRad/.test(html) && /optDeg\.textContent = T\.modeDeg/.test(html),
  'option labels wired to i18n dict');
ok(/modeRad: 'Radians \(RAD\)'/.test(html) && /modeRad: '弧度制 \(RAD\)'/.test(html),
  'LANG has modeRad/modeDeg entries (en + zh)');
ok(/MODE_TAG = m;/.test(html) && /T\.points \+ MODE_SEP \+ MODE_TAG/.test(html),
  'status bar suffix follows mode');
ok(/K\.setAngleMode\('RAD'\)/.test(html) && /selMode\.value = 'RAD';/.test(html),
  'init: kernel RAD + select RAD 同态');

/* ---------- C) 零 GC 纪律 ---------- */
/* 切换处理器只允许：读 .value、内核赋值、标签赋值、重绘调用。
   禁 new / 模板串 / 数组与对象字面量 —— 'RAD'/'DEG' 均为字面量，零分配。 */
ok(!!handler && !/\bnew\b/.test(handler[1]) && !handler[1].includes('`') &&
   !/\[/.test(handler[1]) && !/\{/.test(handler[1]),
  'change handler allocates nothing (no new/template/array/object literal)');

console.log(fails === 0 ? 'ALL PASS' : fails + ' FAILED');
process.exit(fails === 0 ? 0 : 1);
