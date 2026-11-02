# F0 治理前置契约 v2.5 —— JS 全池水位门禁、账本门禁化与回归资产固化

> **状态**：冻结（v2.5.0 F0 开工第 1 个 commit，feat/js-gate-f0，合并序第 0 位）。
> **归属**：feat/js-gate-f0（JS/128KB core_solvers.js、docs/js-128kb.md、docs/ACCEPTANCE.md 独占；
> 路线图 §五 分支表）。**成文依据**：ROADMAP-v2.5-v2.6.md §三 F0 节、§6.2 R1/R7、§6.3 开工
> checklist；docs/features.md:137-142（全项目池 170 304 B 先例口径）；JS/128KB CHANGELOG
> v2.4.0 待办「后续任何 JS 功能落位前应重估水位」。
> 本契约不是字符串进/出的对拍契约（先例 ode-format-v23 / sqfree-format-v24），
> 而是**门禁口径契约**：门禁线的数值、推导、登记机制与再验收命令在此一次写死；
> 任何变更须先改本文件再改码（先例机制同）。

---

## 1. 动机与范围

v2.4.0 起全项目 JS 静态池已实质突破 98 304 B 口径（features.md:137-142：167 908 B，
v2.4.x 增至 170 304 B），而 `checkMemoryWatermark`（core_solvers.js）的降级估算只累计
基础内核池（11 992 B），外挂模块池（core_integrate / core_symboltree / core_groebner /
core_multivar / core_matrix / core_ode / core_numtheory / core_ratroot(sqfree) /
core_calculus）完全不设防——v2.5 四个 JS 功能（F1–F4）每个都要加池，门禁不改造即失去
保护力。同时 `.auto/` 的 ODE 差分 fuzz 资产（370 行语料 + 两侧探针 + 确定性生成器）
是 gitignored 会话级残留，复用前必须固化进 git（R7）。

本契约覆盖 F0 五件事：(a) 全池口径水位门禁与逐模块账本；(b) 文档数字修回清单；
(c) ODE fuzz 资产固化与再验收命令；(d) 发版 checklist「文档数字与 HEAD 一致」核对项与
积分表常量共享生成脚本；(e) 水位自检测试文件。

## 2. 全池水位门禁口径（两线独立，checkMemoryWatermark）

`checkMemoryWatermark`（JS/128KB/src/core_solvers.js，导出名不变）改为**两条独立的线**，
任一越线即拒绝（`_solveError = 4`，返回 1）：

1. **堆线（不变）**：`performance.memory.usedJSHeapSize` 存在且 > **98 304 B**
   （128 KB × 75%）→ 拒绝。浏览器通道语义与 v2.4.x 完全一致。
2. **全池静态线（F0 新增）**：

   ```
   used = 基础池 live byteLength 合计（11 992 B，core_parser/evaluator/base/solvers）
        + _modulePoolBytes（各池模块加载期自报合计，F0 实测 158 312 B）
        + (tokenCount << 3) + (numberCount << 3)（动态游标，显式计入）
   used > 178 496 B → 拒绝
   ```

   **门禁线推导**：170 304 B（F0 开工实测全项目加载期 TypedArray byteLength 之和，
   143 组分配、逐分配 stack 归属，`test-watermark.js` 可复现；与 features.md:137-142 /
   docs/js-128kb.md 账本口径一致）+ 8 192 B（v2.5 JS 新增池预算封顶 2^13 B，
   ROADMAP §三：F1 0 + F2 1 024–2 048 + F3 1 024–1 638 + F4 ~2 816，对分项和
   +4 864–6 502 B 留 26–41% 余量）= **178 496 B**。旧降级估算中的 +2 048 B 冗余随
   96 KB 堆代理口径退役：游标已在新口径显式计入，不再重复留冗余。

**口径澄清（沿 docs/js-128kb.md）**：静态池是 TypedArray 外部内存，不计入 V8
`heapUsed`；堆线与全池静态线是两条独立的线，全池静态线在有无 `performance.memory`
的环境下都执行。

## 3. 逐模块池账本（账本门禁化）

- **机器可读账本**：`tests/js-pool-ledger.json`（本契约的数字附件：基线 170 304 B /
  143 组、逐文件 pools/bytes、登记方式、门禁线推导）。
- **人读账本**：`docs/js-128kb.md`「全池水位门禁与逐模块池账本」节（同表 + 机制说明）。
- **登记机制**：`core_parser.js`（拼接序第 1，一切 eval/拼接环境的最先加载点）声明
  `var _modulePoolBytes = 0;`（全局标量，零分配）；基础池四文件（parser/evaluator/
  base/solvers）由门禁函数 live 读 `byteLength`；其余池模块在**自身文件**内、池声明
  之后以

  ```js
  if (typeof _modulePoolBytes === "number") {
    _modulePoolBytes += <自身全部池 byteLength 合计>;
  }
  ```

  加载期自报（零分配、零递归、非热路径）。`typeof` 守卫保证模块三通道
  （CommonJS `require()` / 浏览器经典 `<script>` / 内核拼接 + 部分拼接 eval）全部兼容：
  独立加载环境无该标量即跳过，行为与 v2.4.x 逐字节一致。
- **未来功能的登记义务（F1–F4 及此后一切 JS 加池改动）**：在某模块新增池时，同一次
  改动内必须①扩展该模块自己的自报表达式（只改自己模块，core_solvers.js 不再是
  汇合点，避免 R6 并行合并冲突）；②更新 `tests/js-pool-ledger.json` 对应行与合计；
  ③更新 `docs/js-128kb.md` 账本表；④`test-watermark.js` 实测复核对不上即红——
  账本与实现漂移被测试拦截，而非靠自觉。新增池预算超 §2 封顶（全池合计 > 178 496 B）
  时门禁直接拒绝，先改设计再写码（ROADMAP §6.3 第 5 步）。
- **强 integrity 校验**：`JS/128KB/test/test-watermark.js`（run-all 自动发现，新文件，
  现有 20 个 test-*.js 一字不改）在加载期以构造器包装 + stack 行号归属独立实测逐文件
  池量，与 `tests/js-pool-ledger.json` 逐行比对；并对真实门禁做「刚好通过/刚好拒绝」
  双侧边界（全池线 178 496/178 497、堆线 98 304/98 305）与懒分配 0 校验。

## 4. 固化资产与再验收命令（.auto/ 会话资产 → git）

| .auto/ 源（只读参考） | 固化落点 | 说明 |
|---|---|---|
| ode-corpus.txt（370 行） | `tests/ode-fuzz-corpus.txt` | 逐字节原样固化 |
| gen-ode-corpus.js | `tools/gen-ode-corpus.js` | 确定性生成器（stdout） |
| odefuzz.c | `tools/odefuzz.c` | C 侧差分探针（include 相对路径在 tools/ 下同样成立） |
| odefuzz.js | `tools/odefuzz.js` | JS 侧差分探针（src 路径改为按脚本位置解析，任意 cwd 可用） |

**再验收（F0(c) 验收线）**：

```bash
node tools/gen-ode-corpus.js | diff - tests/ode-fuzz-corpus.txt && echo SAME   # 输出为空 = 通过
```

固化前禁止引用 .auto/ 路径（R7）；`tools/odefuzz.c` 的编译链接属 C 侧复用时的验证，
不在 JS 组验收线内（`gcc -fsyntax-only` 语法级自检仍执行）。

**历史 ROADMAP 未兑现项①（顺带清偿）**：积分表常量两侧共享生成脚本落
`tools/gen-int-table.js`——内置 128 项规范表（kind/func/pow/anti），`--check` 解析
C `ac_pro_integrate.c` 的 `s_pro_int_table` 与 JS `core_integrate.js` 的
`_T_SPEC`/`_T_ANTI` 逐项比对规范表，三方一致 exit 0；`--emit-c`/`--emit-js` 输出
两侧常量文本供未来改表。**不改两侧在树常量**（C/128KB-ProTEXT 一行不改；
core_integrate.js 不在本组触碰清单）。

## 5. 发版 checklist 增项（落在 CONTRIBUTING.md「版本与发布」）

新增核对项：**文档数字与 HEAD 一致**——`docs/ACCEPTANCE.md`、`docs/js-128kb.md` 等
引用的字节数（压缩内核、静态池、.bss）与测试数（run-all TOTAL、verify/checks）与 HEAD
实测一致；冻结文件（20 个 test-*.js）内的滞后数字豁免并登记（现存已知项：
test-browser.js:12 的「43 项冒烟」，红线不可改，待组件版本解冻时一并修）。
发版可选项：§4 两条再验收命令（语料再生 diff 为空、`node tools/gen-int-table.js` exit 0）。

## 6. (b) 文档数字修回清单

| 位置 | 修回 |
|---|---|
| docs/js-128kb.md 头部状态行 | 「20 文件内核拼接」→ 17 文件；版本注 v2.4.1 → v2.4.3 |
| docs/js-128kb.md 内核文件体积口径 | 103 575 → 103 599（HEAD `build-kernel --minify` 实测）；余量 27 497 → 27 473；20 文件 → 17 文件 |
| docs/js-128kb.md 口径澄清/测试统计 | 门禁行号引用改函数名引用；1188 → 1194（ode 77 → 83 等，run-all 实测） |
| docs/ACCEPTANCE.md JS128/ProTEXT/内存账本行 | 603 → 1194（20 套件）、43 ESM → 49、579 → 583（C v1.6.3）、静态池 22 416 → 170 304、门禁列改两线口径 |
| JS/128KB/test/build-kernel.js :16 / :446 | 注释「43 项 ESM 冒烟」→ 49（运行时报文 build-kernel.js「导出接口链路 49/49」为准） |
| JS/128KB/src/core_matrix.js :21 | pro 层池注释 5944 B → 4 944 B（4×1152+48+96+192，实测归属；随登记行一并修） |

## 7. 验收口径（本契约的完成判定）

1. `node JS/128KB/test/build-kernel.js --minify`：BUILD OK，字节数登记（开工基线
   103 599 B），重建后 `git status` 仅预期文件差异。
2. `node JS/128KB/test/run-all.js`：基线 1194 pass / 0 fail（20/20 文件）不回退，
   新增 test-watermark.js 后为 21 个文件、TOTAL 相应增加。
3. `node JS/128KB/test/test-watermark.js`：全池口径自检通过（含 §3 双侧边界）。
4. §4 语料再生 diff 为空；`node tools/gen-int-table.js` exit 0。
5. C/128KB-ProTEXT 零触碰（`.bss` 账本 42608/42640/39408 不因本分支改变）。
