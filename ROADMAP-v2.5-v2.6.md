# ApolloCalc-CAS · v2.5.0 / v2.6.0 路线图 —— 代数纵深与应用落地

> 成文基准：全局 Tag **v2.4.3**（2026-11-03；双轨制组件版本：C/128KB-ProTEXT **v1.6.3**、JS/128KB **v2.4.3**；冻结组件 C/64KB v1.0.0、C/128KB v1.1.0、JS/64KB 未建轨道）。
> 本文是**独立成篇**的新路线图，与历史 `ROADMAP.md`（成文基准 v2.2.0，其 10/10 功能已全部交付）并行存在，**不覆盖、不修改**它。历史文件的两项机制在本文沿用：①「契约先行」（C 组开工第 1 个 commit 先冻结契约文档合 master，JS 组按其实现，docs/features.md:120-124）；②「收尾组串行收口」（根 CHANGELOG / docs/features.md / README 由收尾组按 commit 链提炼，feat 分支不碰，ROADMAP.md:171、:238）。历史 ROADMAP.md 的未兑现条目（积分表常量共享生成脚本、v2.5 候选池「JS 大整数」「JS 一般实矩阵特征值」）在本文 §二 逐条处置，不悬空。
> **数字溯源标注（全文遵守）**：**【实跑·上游 segment】**= 本工作流前一 segment 在本机执行并记录的命令与输出（本次未复跑，按盘点材料转引）；**【实读】**= 本会话直接读文件 / `wc -c` / `grep` 所得；**【账本】**= 仓库文档记载的既往实测数（本次未复跑）；**【估算】**= 外推值，开工时必须以 HEAD 实测复核并写进 PR 描述。行号引用除标注【实读】外均转引自盘点材料（v2.4.3 时点），以开工时实读为准——历史 ROADMAP.md:8 的「绝对值以各功能开工时 HEAD 实测为准」原则继续有效。
>
> 修订记录：v1（2026-11-03）首版定稿——基于五方向独立调研（SVD / 非线性方程组求解 / Sturm 序列 / WASM 产物 / 复变函数扩展）与 v2.4.3 全仓盘点。
> 修订记录：v2（2026-11-03）按独立评审 12 条意见修订——①F0 拆独立早合分支消除依赖/合并序自相矛盾（评审 1，采纳）；②F3 补 JS 浮点精确域闸门与契约三层分叉区（评审 2，采纳其方案 a「收窄成文」，不做 JS 多精度层并写明理由）；③熔断线按字节口径重推导为 126 976 B 并附触线降载序（评审 3，采纳）；④npm 包改 dist-npm 暂存布局、wasm 产物不进 git、wasm 门禁与发版三关解耦（评审 4，采纳；其中「wasm-smoke.js 会被 run-all 自动发现」的机制描述按 run-all.js:22-24 实读修正——发现模式 `/^test-.*\.js$/` 不匹配该文件名，但结构性风险成立仍迁移）；⑤F0 门禁线算术修正 178 496 B、三方池口径统一（评审 5，采纳）；⑥F5 增阶段 B 全内核 33 TU 粗编、垫层清单补 ctype/strtod 类（评审 6，采纳）；⑦F5 难度升 M、v2.6 B 主题预案成文（评审 7，采纳）；⑧docs/tools/tests-bench 改文件级归属并补 F1 差分驱动落点名（评审 8，采纳）；⑨ESM 冒烟基线 43→49（评审 9，采纳：build-kernel.js:993 运行时报文【实读】）；⑩C checks 汇总闭合修正（评审 10，采纳）；⑪v2.6 组件版本递进修正（评审 11，采纳）；⑫worktree 路径改 .worktrees/ 惯例（评审 12，采纳：.gitignore:50【实读】）。
> 修订记录：v3（2026-11-03）产品侧裁决——**拒绝安装 LLVM/clang**（项目定位极致轻量、零外部重型依赖，纯 C 与纯 JS 双通道已满足「拉取源码即用」）：①**F5 降级为纯文档调研**（docs/wasm-future.md，归收尾组），WASM/npm/TypeScript/API 文档主题**整体顺延 v2.7+**；②v2.6.0 主题切换为 §6.2 R5 **预案 B**：**G2 SVD 双端升为核心攻坚**、G3 基准终版、**G4 主路线改为原 fallback 转正——纯 JS 大整数移植**（20 limb TypedArray，开工前先出内核字节预算 spike 过熔断线 go/no-go；连带解锁 JS Sturm 5..8 与 sqfree 5..8 收口）；③worktree 重排：feat/js-parity-eco 改名 **feat/js-eigen**（只装 F4），F5 文档归收尾组；v2.6 切分改为 matrix-advanced + js-bigint + 收尾组。

---

## 〇、摘要（TL;DR）

- **v2.5.0（6 项）**：F0 治理前置（主笔补充，**独立早合分支 feat/js-gate-f0**）→ F1 复变函数扩展（双端）→ F2 非线性方程组 GB 消元数值全解（双端）→ F3 Sturm 序列与实根隔离（C 全功能 + JS ≤4 镜像）→ F4 JS 一般实矩阵特征值（Francis 移植，补齐不对等）→ F5 WASM 前景文档（v3 裁决降级为纯文档调研，归收尾组）。
- **v2.6.0（v3 裁决按预案 B 执行，主题：应用落地·纯数学）**：G2 SVD 双端（one-sided Jacobi）**核心攻坚**；G3 压测口径扩展与基准终版；G4' 纯 JS 大整数移植（原 fallback 转正，先出内核字节预算 spike，连带收口 JS Sturm 5..8 与 sqfree 5..8）；G1 WASM/npm/TS/API 文档四件套整体顺延 v2.7+。
- **预算主线（全部可追溯，统一字节口径）**：C 侧 `.bss` 全程零增长——所有新视图/缓冲压在 3848 B union 吸收线内（ac_pro_scratch.h:10-13【实读】sparse 视图峰值）；JS 压缩内核 103 599 B【实读】起步，v2.5 末约 **114 761–121 519 B**【估算区间，单项 ±20% 粗度，开工逐项实测】；v2.6 末态预算由 G4' 纯 JS 大整数的预算 spike 定数（G1 loader 已随 v3 裁决移出本期），距 131 072 B 文件线（build-kernel.js:213【实读】）设**熔断线 126 976 B（= 131 072 − 4 096 B 应急余量，按字节定义，不写 KB/KiB 混读值）**，触线降载序成文（§6.2 R2）；JS 全项目静态池由 F0（**独立早合分支**）重估门禁，是 v2.5 一切 JS 功能的开工前置。

---

## 一、基线与预算地基

### 1.1 C 侧（C/128KB-ProTEXT，组件 v1.6.3）

| 口径 | 值 | 性质 | 来源 |
|---|---|---|---|
| 内核 .bss | **42 608 B** | `==` 级断言 | `mingw32-make bss-check` PASS（42608/42640/39408 三口径精确命中）【实跑·上游 segment】；Makefile:179 `KERNEL_BSS_EXPECT := 42608`【实读】 |
| runner / 上游对照 / degraded | 42 640 / 39 408 / 38 672 B | 全等断言 | 同上；degraded 属 verify.ps1 §6，本次未复跑【账本】 |
| 门禁上限 | 114 688 B（112 KiB = 128 KiB × 87.5%） | 编译期硬断言 `AC_PRO_STATIC_FOOTPRINT ≤ AC_PRO_GATE_BSS` | ac_pro_config.h:141、ac_pro_solvers_ext.c:54【实读+账本】 |
| 内核余量 | 114 688 − 42 608 = 72 080 B | 纪律意义大于物理意义 | 账本推导 |
| **union 吸收线** | **3848 B**（现峰值视图 sparse） | 新视图 ≤ 3848 B 即被 union 吸收 → `.bss` 零增长；超线必须 Makefile×4 + ac_pro_config.h + verify.ps1 **一次改齐**重走 verify.ps1 | ac_pro_scratch.h:10-13「`sizeof(ac_pro_scratch_t) == 3848` 编译期钉死」【实读】 |
| 每 static 对象 | TU 净 +32 B（16 B 段 + 16 B 链接对齐） | 错误码一律走返回值、不新增全局对象 | ac_pro.h 头注实测论证【账本】 |
| 编译门禁 | `-std=c99 -Wall -Wextra -Wstrict-prototypes -Werror -O2` 固定；禁 malloc（`--wrap=malloc/free` 链接拦截）、禁递归、禁 VLA；上游 `C/128KB/` 一行不改 | 不可弱化 | CONTRIBUTING.md:60-66、ProTEXT Makefile:68/:70【账本】 |
| 测试基线 | pro_runner **583 checks / 0 failed**；verify.ps1 **35 项 verdict** | 发版三关之一 | 根 CHANGELOG v2.4.2 行 32、README.md:27、docs/features.md:85【账本，本次未复跑】 |

### 1.2 JS 侧（JS/128KB，组件 v2.4.3）

| 口径 | 值 | 性质 | 来源 |
|---|---|---|---|
| 压缩内核 | **103 599 B** | 实测 | `wc -c JS/128KB/src/core_kernel.js` → 103599【实读】 |
| 文件线 | 131 072 B，余量 **27 473 B**（占用 79.0%） | 超线即构建失败 die | build-kernel.js:213 `BYTE_BUDGET = 131072`、:996-1002【实读】 |
| 全项目静态池 | **170 304 B**（2026-11-01 实测） | 已**实质突破** 98 304 B 水位口径 | docs/js-128kb.md:130-131【账本】 |
| 水位门禁 | `checkMemoryWatermark`（core_solvers.js:614【实读】）门禁线 98 304（core_solvers.js:636【实读】）**只累计基础内核池、不含模块池** → 测试仍绿但保护力失效 | JS 侧新增功能当前最紧的一条 | features.md:137-142「后续任何 JS 功能落位前应重估水位」【账本】 |
| 零 GC 铁律 | 禁对象/数组字面量、禁递归、禁热路径字符串拼接；计算数据一律加载期一次性分配的 TypedArray 或全局标量 | 不可弱化 | CONTRIBUTING.md:82-86【账本】 |
| 测试红线 | 现有 20 个 `test-*.js` 不可改；新测试加新文件，run-all.js readdirSync 自动发现（run-all.js:22-24）；任何内核改动必须重跑 `node test/build-kernel.js --minify`，复跑后 `git status` 核对、有差异即 checkout 还原 | — | ROADMAP.md:198、:201【账本】 |
| 测试基线 | `node test/run-all.js` **1194 pass / 0 fail**（20/20 文件）+ build-kernel 83 项 eval 回归 + **49 项 ESM 冒烟** | 发版三关之二 | 49 的来源：build-kernel.js:993 运行时报文「导出接口链路 49/49 通过」【实读，固定文案】；该文件注释 :16/:414 仍写 43 系滞后（导出面扩 6 项未跟上），F0(b) 一并修回。1194 见根 CHANGELOG v2.4.2、README.md:27【账本，本次未复跑】 |

### 1.3 门禁与发版三关（命令原样照抄即可开工）

```
# ① C 全量验收（7 节 35 项 verdict，含 .bss 四口径、每功能用例数门禁、上游零改动 git 双证）
powershell -NoProfile -ExecutionPolicy Bypass -File C/128KB-ProTEXT/verify.ps1

# C 快速门禁（不能替代 ①）
cd C/128KB-ProTEXT && mingw32-make SHELL=cmd.exe bss-check degraded-check

# ② JS 全量（工作目录 JS/128KB，或仓库根 node JS/128KB/test/run-all.js）
node test/run-all.js

# JS 内核重建（压缩产物唯一重建入口；复跑后必须 git status 核对防工具链漂移）
node test/build-kernel.js --minify

# ③ 差分对拍（50/50 exit 0）
node tests/bench/diff-smoke128.js
```

三关全过才可发版（ROADMAP.md:227）；本工作流本次**只实跑了 bss-check 与字节/版本实测**，verify.ps1 / run-all / diff-smoke128 均未复跑（按 ask 不跑全量测试），基线数字取【账本】口径。

### 1.4 双端同步纪律

- 共享契约 `tests/` 四套 JSON（polynomials / exact-forms / edge-cases / ode-solutions）任何增删两侧同步并重跑差分（CONTRIBUTING.md:105-106）；数值容差 1e-9、精确形式与错误码精确比较（CONTRIBUTING.md:96）。
- 两侧输出不一致即实现 bug，**不许返工兜底**（ROADMAP.md:178）；能力不对等（如 JS 无 int64）必须如实成文、不称「对等」（features.md:143-145）。
- 新跨语言输出格式先冻结契约文档进 `docs/contracts/`（现有 ode-format-v23.md、sqfree-format-v24.md，本会话 `ls` 实读确认）再双侧实现。

---

## 二、候选评估（五方向调研结论 + 落选/转通道裁决）

### 2.1 五方向判定总表

| # | 方向 | 调研判定 | 本文落位 | 一句话理由 |
|---|---|---|---|---|
| 1 | 复变函数高级运算（复三角/反三角/复双曲/反双曲 + 全复幂 + 复 Gamma，约 15 API） | 适合 v2.5.0——全仓少有的 .bss +0 / JS 池 +0 的纯纵深数学面 | **F1（v2.5.0，双端）** | 不触两条最紧红线（3848 吸收线、JS 池水位），是 features.md:37 既有复数域 ✅ 行的自然纵深 |
| 2 | 非线性方程组求解（GB 消元全解 / Newton） | 适合 v2.5.0，收窄为只做 GB 消元数值全解；Newton 不做 | **F2（v2.5.0，双端，含 JS DK 移植）** | 构件双端现成 90%（GB 访问器/sqfree 去重根/求解层/复根渲染）；Newton 缺多变量一般表达式求值地基且初值依赖在计算器场景无解 |
| 3 | Sturm 序列与实根隔离 | 适合 v2.5.0，C 侧全功能先行，JS ≤4 镜像同窗交付 | **F3（v2.5.0，C 全功能 + JS ≤4）** | .bss 零增长、sqfree+bigint 地基齐全，补上 benchmark-giac.md:128-129 已成文的对标缺口；DK 求根首次获得可验证的实根计数保证 |
| 4 | WASM 编译产物 | v3 裁决：拒绝引入 clang 工具链（零外部重型依赖），F5 降级纯文档调研、G1 顺延 v2.7+ | **F5（v2.5.0 纯文档调研，归收尾组）+ G1（顺延 v2.7+）** | 调研结论与垫层/体积估算存档 docs/wasm-future.md，产品侧翻案时可即时复用 |
| 5 | SVD（奇异值分解） | v2.6.0 **核心攻坚**（双端 Jacobi 本体、不做最小二乘） | **G2（v2.6.0 核心，双端）** | ROADMAP.md:75 原文是「v2.4 后观察」而非永久拒绝，v2.4.3 恰在观察窗口上；先补齐候选池已承诺的不对等项再开新数值面，符合「不称对等」成文伦理 |

### 2.2 落选 / 搁置 / 转通道（逐条理由，不许悬空）

- **Newton 迭代数值解方程组（调研内的子方向）：不做。** 多项式系被 GB 消元全解严格覆盖（不需初值、给全部解）；超越系需扩 T_FUNC 建树（约 +50% 工作量，ac_pro_symbolic_ext.c:303 现拒 T_FUNC【账本】）且初值来源在计算器场景无解（SymPy nsolve 在奇异初值点直接失败的同款问题）。
- **SVD 配套 lstsq / 最小二乘管线：不做**（维持 ROADMAP.md:75 的边界）。G2 只落 SVD 本体；上游 QR→求解管线的存量缺口维持原状。
- **JS 大整数纯 JS 移植（历史 ROADMAP.md:71 候选池、features.md:174-175 未落地项）：v3 裁决转正为 v2.6 G4' 主路线。** 原稿裁决「主路线 WASM 通道、纯 JS 移植降 fallback」；产品侧拒绝引入 clang 工具链（零外部重型依赖）后，纯 JS 移植（20 limb×1e9 TypedArray 直接搬 ac_bigint，历史评估不变）转正——开工前必须先出内核字节预算 spike（对照熔断线 126 976 B 给 go/no-go，spike 定数前不编造预算），并按 F0 改造后的水位门禁走全池审批；连带解锁 JS Sturm 5..8 与 sqfree 5..8 分叉收口。
- **结果历史环形缓存：继续不碰。** UI 层职责边界未定（features.md:167），内核越界。
- **simplify / trigsimp、Groebner 扩维、ODE 非齐次/变系数、五次以上根式解：维持禁做**（ROADMAP.md:69-78、features.md:168/:176-177 成文落选声明，不可翻案项）。
- **表达式层复数求值（i 字面量进解析器 + 复值 RPN 求值栈）：F1 明确排除**，属另一 L 级方向（ac_apply_function 返回 double 的现状要整体复值化），另立候选，不捆绑。
- **Sturm 的 JS 5..8 次路径：本期不做**，依赖 JS 大整数 → v3 裁决后由 v2.6 G4' 纯 JS 大整数落地时收口，此前 JS 侧 5..8 维持契约成文分叉（EDEG），机制与 sqfree-format-v24 §5.1、features.md:199-202 先例完全同款；JS ≤4 域内另有链系数超 2^53 的 ECOEF 域闸门（F3 契约三层，评审 2 采纳成文）。
- **历史 ROADMAP 未兑现项①「积分表常量两侧共享生成脚本落 tools/」：并入 F0 治理包**（tools/ 现仅 ref_prs.py、scaffold_v2_1_0.py【ls 实读】），由收尾组顺带清账，不单独立项。

---

## 三、v2.5.0 版本规划（6 项）

> 规则（沿用 ROADMAP.md:84）：每版 4-6 个功能，同功能双侧实现算一个；每功能开工前两件事：①冻结契约文档（如适用）合 master；②C 侧跑 `bss-check`、JS 侧跑 `build-kernel --minify` 记录 HEAD 基线进 PR 描述。
> 版本主题：**v2.5.0 = 代数纵深 + 不对等清账**（复变与方程组两条新数学面、Sturm 补对标缺口、JS 补齐最后一个矩阵不对等项，同时为 v2.6 应用落地铺 WASM 与水位地基）。
> 全局增量预算封顶【估算汇总，开工逐项复核，1 K = 1024 B】：C `.bss` +0 B（全部功能零新 static 对象/视图压线内）；C checks 583 → 约 **770–1010**（F1 +60–120、F2 +80–150、F3 +50–160〔12–20 例 × 每例 4–8 checks 口径〕，F4/F5 零）；JS 压缩内核 103 599 → 约 **114 761–121 519 B**（F0 +512–1 024、F1 +1 536–2 560、F2 +2 560–4 096、F3 +3 482–5 120、F4 +3 072–5 120，分项相加闭合）；JS 池 **+4 864–6 502 B**（F1 0、F2 1 024–2 048、F3 1 024–1 638、F4 ~2 816，F0 零），封顶 8 192 B；JS pass 1194 → 约 1360–1460。

### F0 治理前置：JS 水位门禁重估、账本门禁化与回归资产固化（主笔补充）

- **端归属**：生态基建（JS/128KB + docs + tools）。
- **主笔补充依据**：①JS/128KB CHANGELOG v2.4.0 待办原话「后续任何 JS 功能落位前应重估水位」，而 `checkMemoryWatermark`（core_solvers.js:614【实读】）只计基础池，全项目池 170 304 B 已实质突破 98 304 B 口径（core_solvers.js:636【实读】、features.md:137-142）——本版四个 JS 功能项每个都要加池，门禁不改造就失去保护力；②文档漂移：docs/js-128kb.md:145 仍写 v2.4.1 的 103 575 B（现 103 599 B【实读】），docs/ACCEPTANCE.md 部分行停留在 v2.4.1/v2.0.0（ProTEXT 579、JS128 603）；③v2.4.2 ODE 差分 fuzz 的语料与探针（370 行 .auto/ode-corpus.txt、odefuzz.c/.js、gen-ode-corpus.js，本会话 `ls .auto/`【实读】确认存在）在 gitignored 工作区（.gitignore:61），是会话级残留——回归资产会随工作目录丢失。
- **任务清单**：(a) 全池账本化——逐模块 allocCount/byteLength 表落 docs/js-128kb.md（core_multivar/matrix/special 的 allocCount 先例），`checkMemoryWatermark` 扩为全池口径（或新增独立门禁函数），新门禁线 = 重估基线（F0 开工实测，暂记 170 304 B）+ v2.5 JS 新增池预算封顶 8 192 B（2^13；对 §三 汇总 +4 864–6 502 B 的估算留 26–41% 余量）→ **178 496 B**（评审修订：原稿 184 320 B 系算术错误，170 304 + 8 192 = 178 496；v2.6 开工时按 G2/G1 实测再调一次，预记 G2 +1 248 B → ~179 744 B，沿用「一次改齐」纪律并成文推导）；(b) 修回 docs/js-128kb.md:145、docs/ACCEPTANCE.md 滞后行与 build-kernel.js:16/:414 的 ESM 冒烟计数注释（43 → 49，运行时报文 build-kernel.js:993【实读】）；(c) 把 `.auto/` ODE fuzz 固化：语料 → `tests/ode-fuzz-corpus.txt`、生成器 → `tools/gen-ode-corpus.js`、探针 → `tools/odefuzz.c` + `tools/odefuzz.js`，挂进发版 checklist（可选项）；(d) 发版 checklist 增「文档数字与 HEAD 一致」核对项；顺带清偿历史 ROADMAP 未兑现项①（积分表常量共享生成脚本落 tools/）。
- **预算账**：JS 池 **+0**（门禁改造只动检查逻辑与文档）；压缩内核 **+0.5–1 KB**【估算】（门禁函数扩展）；C 侧零触碰、账本零影响。
- **难度**：S。**依赖顺序与并行化解法（评审 1 采纳）**：F0 **拆为独立早合分支 `feat/js-gate-f0`，固定合并序第 0 位先合 master**（不随 js-parity-eco 排队）——F1–F4 的一切 JS 侧工作以它合入 master 为前置，并行度不受损；合入前不受阻即可开工的：F1/F2/F3 的 **C 侧**（仅依赖各自契约冻结）、F5 全部。原稿把 F0 放进 js-parity-eco（合并序第 3 位）与前置声明自相矛盾，系分组错误，已纠正。
- **触碰文件**：JS/128KB/src/core_solvers.js（门禁函数）、docs/js-128kb.md、docs/ACCEPTANCE.md、tests/ode-fuzz-corpus.txt（新）、tools/gen-ode-corpus.js（新）、tools/odefuzz.c（新）、tools/odefuzz.js（新）、JS/128KB/test/ 新增 1 个水位自检测试文件（现有 20 个 test-*.js 一字不改）。**分支归属：`feat/js-gate-f0`（早合序 0）。**
- **验收**：门禁改造后全池口径自检通过；`tools/gen-ode-corpus.js` 重新生成的语料与固化语料 diff 为空；run-all 1194 基线不回退。

### F1 复变函数扩展：复域超越函数库（side=双端）

- **端归属**：双端（C 侧新建 Pro TU `ac_pro_complex_ext.c`，PRO_EXTRA 无条件接线；JS 侧就地扩展 core_complex.js）。
- **内容**：约 15 个寄存器式 API，复用既有 g_c_a/g_c_b/g_c_tmp 六寄存器（ac_complex.h:8-13【账本】）：csin/ccos/ctan（闭式恒等式 sin(re)cosh(im)±i·cos(re)sinh(im)）、csinh/ccosh/ctanh、casin/cacos/catan/casinh/cacosh/catanh（cln+csqrt 复合式，C99 Annex G 主值分支）、cpow(z,w)=cexp(w·cln z)（cPowReal 全复推广，「A 寄存器被覆写」约定沿袭并写进头注）、cnthroot(z,k)（de Moivre 极坐标式）、cgamma（复用 ac_special.c 同一张 Lanczos g=7/n=9 系数表，Pro TU 自持 static const 副本落 .rodata；Re z≥0.5 判据 + 反射式，极点/割线返回值契约逐项成文）。调研口径「约 14 个」，csinh/ccosh/ctanh 为闭式恒等式实现成本近零，**建议全收 15 个**，最终以契约冻结为准。表达式层暴露（i 字面量）明确不在本功能（§2.2）。
- **入选理由**：全仓少有的 .bss +0 / JS 池 +0 的纯纵深新数学面，不触两条最紧红线；不在任何禁做清单；三库对标常识水位（SymPy/Giac/Eigenmath 初等函数族复参数即用）；教学定位契合，是 features.md:37「复数域函数」行的自然纵深。
- **预算账**：C `.bss` **+0**（新 TU 零 static 可变对象，Lanczos 系数 static const 落 .rodata；风险项：链接对齐垫片最坏 +32 B 触发 == 断言 → 按 ROADMAP.md:189 一次改齐流程）；C `.text` +2.5–4 KB（12 API=2200 B 实测外推【账本·调研 segment nm 探针】），.text 无门禁。JS 压缩内核 **+1.5–2.5 KB**（core_complex.js 现 5664 B【账本·调研 stat】体量约翻倍）；JS 池 **+0**（纯标量寄存器，allocCount 保持 0）。
- **难度**：M——公式全是教科书恒等式，无迭代收敛坑；成本在纪律面（分支切语义成文、双端 1e-9 逐位对齐、−0.0 与 IEEE 边角）。
- **依赖顺序**：JS 侧依赖 F0；C 侧仅依赖契约冻结，可与 F0 并行。与 F2–F5 无依赖。
- **触碰文件**：C：ac_pro_complex_ext.c（新）、ac_pro.h、Makefile（PRO_EXTRA 一行）、test/test_pro_complex_ext.c（新，12–20 例）+ test_pro_runner.c tally、verify.ps1 §4 新行；JS：core_complex.js、build-kernel.js EXPORT_BLOCK（complex 命名空间加方法，EXPORTED_NAMES 不动——v2.3.0 功能 2 先例）、test-complex-ext.js（新，30–60 例，run-all 自动发现）。
- **差分对照测试方案（零分叉）**：①开工第 1 个 commit 冻结 `docs/contracts/complex-ext-v25.md`（主值分支表 + 每函数误差带，仿 core_special.js 分函数容差先例：三角/双曲 1e-9、cgamma 单独标定）先合 master；②新建第五套共享契约 `tests/complex-functions.json`（ode-solutions.json v2.3.0 新建先例）：成功用例数值 ≤1e-9、错误码/极点约定精确；③差分 fuzz：`tools/gen-complex-corpus.js` 生成固定网格语料（|z|∈[0.1,10] 全象限 × 每函数族 ~50 点 + 四条割线两侧采样 + 极点/溢出边界 + −0.0 边角），**生成器落 tools/ 不进 .auto/**（吸取会话残留教训），C/JS 同语料 CSV 逐行 `code\tout` 比对，exit 0=一致；④±0 边角用 1e-9 容差吸收，不比符号零；⑤cgamma 与 ac_special/core_special 实 Gamma 在 Im=0 时逐点一致（交叉回归锚）。
- **测试面**：C 12–20 例（每例 4–8 checks → 583 基线 +60–120）、JS 30–60 例、差分语料 300–800 行、契约 JSON 80–150 条。关键用例：恒等式锚点（csin(i)=i·sinh(1)、cpow(e,iπ)=−1、cgamma(1)=1）、割线采样（cln(−1±0i)、casin(2)、cacos(−2)、catan(2i)）、极点契约（cgamma(0)、cgamma(−2)）、溢出契约（cexp(1000+0i)）、−0.0 边角、既有层交叉回归（cSqrt/cPowReal 行为零变化、四阶方程复根路径零扰动）。
- **风险与预案**：cgamma 若实测标定不达双端 1e-9 → 降级 C 独有（ac_special 先例）或后撤 v2.6，其余 14 API 不受牵连；`==` 账本对齐垫片风险 → 见 §六 R3。

### F2 非线性方程组求解：Groebner 消元数值全解（side=双端，不含 Newton）

- **端归属**：双端（C 侧新建 `ac_pro_sysolve.c`；JS 侧 core_groebner.js 扩段或新 core_sysolve.js，**捆绑 Durand-Kerner 移植**）。
- **内容**：SymPy solve_poly_system 的数值域同构：f,g 两多项式串（语法同 gb）→ 进现有 ac_pro_groebner/core_groebner → 经 GB 访问器把 y-单变元消元多项式 h 与混合元素拷入栈缓冲（copy-out 后 gb 视图即不再持有，遵守 ac_pro.h 访问器互斥纪律）→ h 经 sqfree 预处理去重根（切相系统实测产生 h=(y−1)²，DK 对重根的扰动弱点由符号层消灭）→ h 求根：1..4 走求解层精确根式、5..8 走 DK（**JS 捆绑移植 DK：约 150 行/池 264 B，镜像 C nr 视图**）→ 逐 y 根代入混合元得 x 一元多项式再求根（无混合元的解耦 GB 走交叉积）→ 每个 (x,y) 候选代回 f,g 残差 ≤1e-9 回验才输出（apart 回乘验证先例）。GB={1} → 成功语义 0 解（对齐 SymPy EmptySet）；无 y-单变元元 → 正维数 → 诚实错误码。
- **入选理由**：SymPy/Giac 共有面（nonlinsolve/gbasis 消元全解），本仓构件已就位约 90%；「双内核代数面对等」是 v2.4.0 功能 10 的既定目标延伸；不做则是对高频面的无故放弃。
- **预算账**：C `.bss` 目标 **+0**——GB 元素经访问器拷入栈缓冲（h/混合元各 ≤9 个 int64 + 根候选 ≤16×2 double ≈ 400 B 栈，栈数组先例：pro_eval_vars stack_val、积分栈池），随后调用的 DK 用 nr 视图 264 B；最坏情形每个不可避免的 static 会话标志 +32 B。C `.text` +15–25 KB（参照 ac_pro_groebner.c 844 行 / ac_pro_sqfree.c 717 行【账本】），无 .text 门禁。JS 池 **+1–2 KB**（含 DK 264 B）；压缩内核 **+2.5–4 KB**（含 DK +1–1.5 KB；外推锚：ROADMAP.md:108 ODE「压缩 +2 KB」、:159 Groebner「+5–6 KB」同量级）。
- **难度**：M（双侧都做则 M 偏 L 收口在 JS DK 移植）。算法无新发明，工程量在消元抽取边界类（正维数判定、解耦 GB 交叉积、切相重根、EDEG 可达失败）与契约冻结。
- **依赖顺序**：JS 侧依赖 F0；依赖契约冻结；与 F1/F3/F4/F5 无依赖。
- **触碰文件**：C：ac_pro_sysolve.c（新）、ac_pro.h、Makefile、test/test_pro_sysolve.c（新，16–24 例）+ test_pro_runner.c tally、verify.ps1 §4 新行（≥10 例门禁）；JS：core_groebner.js 扩段（或新 core_sysolve.js）、build-kernel.js FILES/EXPORT_BLOCK、test-sysolve.js（新，40–70 例）。
- **差分对照测试方案（零分叉）**：①开工第 1 个 commit 冻结 `docs/contracts/sysolve-format-v25.md`：输入两多项式串 + 输出解列表字符串（复用 sqfree 复根渲染格式 [re](+/-)|im|i）+ 错误码全序（建议 OK / 0 解 / EARG / EDEG / EZDIM 正维数 / ESOLT 候选超容 / EOVER / EVERIFY 残差不过 / EINTERNAL，契约冻结定稿）；②新建契约 `tests/systems.json`：成功用例解列表按序数值 ≤1e-9、错误码精确比对（CONTRIBUTING.md:96）；③独立差分驱动 `tests/bench/diff-sysolve.js`（机制同 diff-smoke128.js，**既有 50 例管线一行不动**）；④**JS 捆绑 DK 后 h 次数 5..8 两侧同走 DK——本功能全域零分叉**；⑤系数域：契约用例幅值全部落在 C int64 与 JS 2^53 公共安全域内，EOVER 语义两侧一致；超出公共域的输入能力不对等在契约成文（不称对等，features.md:143-145 先例），不设分叉用例区；⑥求解层 clobber 副作用（复用 _coeffs/g_coeffs）按 sqfree 先例成文。
- **测试面**：C 16–24 例（约 +80–150 checks）、JS 40–70 例、契约 JSON 40–70 条。关键用例类：经典二次系 4 解（x²+y²−4; xy−1）、2 解（x²+y²−1; x²−y）、线性系与 solveLinearSystem 交叉验证、GB={1} 无解=成功 0 解（x−1; x−2）、正维数诚实拒绝（x+y−1; 2x+2y−2）、解耦 GB 交叉积（x²−1; y）、切相重根（x²+y²−2; x+y−2 → h=(y−1)²）、EDEG 可达失败且输入仅 3 次（x³+y³−1; xy−1，证明 GB 消元可达集窄于 deg≤4 输入空间，契约成文）、h 次数 4/5 路由边界、残差回验拒绝近失解、错误码全序遍历、「刚好通过/刚好拒绝」双侧边界对（CONTRIBUTING.md:107-111 硬要求）。
- **风险与预案**：JS 系数域 2^53 熔断中间量膨胀 → 公共域用例设计 + EOVER 如实成文；进度超支 → 降级预案：只交 C 侧 + 契约，JS 侧顺延 v2.6（届时 JS 断言 EUNSUPPORTED 成文，同 sqfree 分叉机制）。
- **可选联动（次优先级，不阻塞验收）**：DK 落地后存量 sqfree 5..8 分叉（features.md:199-202）可收口——JS sqfree 整题 5..8 从 EDEG 改走 @d 求根。该行为变更需契约修订：polynomials.json 的 cOnly 5 例迁入 dual 区、JS 侧新测试文件断言新行为（test-sqfree.js 是红线文件不可改）、由收尾组在 merge 后统一执行并双侧重跑。窗口不够则顺延 v2.6，成文即可。

### F3 Sturm 序列与实根隔离（side=C 全功能 + JS ≤4 镜像）

- **端归属**：双端，**能力域不对称**：C 全功能 deg 1..8（5..8 由 ac_bigint 承载），JS 镜像 deg ≤4（5..8 契约成文分叉返 EDEG，消除路径 = G4）。
- **内容**：单变量整系数多项式（词法同 sqfree 域：deg≤8、|c|≤999、ASCII、≤128 B，core_ratroot.js:338-339【账本】）→ 本原化+首项化正 → square-free 预归一（复用两侧既有 gcd(p,p′) SFF 链）→ Sturm 链 p_{k+1}=−rem(p_{k−1},p_k) 用**精确有理余**（num/den 对，C 侧 ac_bigint 承载，每步 content 约减；规避伪余 prem 的符号因子陷阱——调研实测确认 prem 归一与正 lc 不变式的相互作用是主要正确性雷区）→ V(a)−V(b) 计数：端点精确 bigint Horner，±∞ 端点用首项符号+次数奇偶规则（零求值成本）→ 隔离：dyadic 网格 + 显式栈二分（禁递归，ac_ratroot.c 显式栈先例），一格含 ≥2 根时如实输出「未解析」而不出假区间。C 视图用扁平 limb 池（每系数上限 8 limb=240 bits；调研 node+BigInt 实验：随机 8 次 |c|≤999 峰值中位 ~100 bits、最大 140 bits，8 limb 余量 70%；离群 ECOEF 诚实拒绝）。
- **入选理由**：Giac sturm/sturmab 是 benchmark-giac.md:128-129 已成文的对标缺口（其当时「禁递归约束下工程取舍」的弃用理由被本次调研证伪——PRS 是显式循环、二分用显式栈，真实约束是系数膨胀域）；且 DK 路径首次获得可验证的实根计数保证（可选 cOnly 增强：Sturm 计数 vs DK 实根数交叉校验，把 ac_pro_solvers_ext.c:262-270 记录的 DK 重根弱点变成被验证对象）。**附带交付（收尾组）**：修订 benchmark-giac.md:129 的弃用理由成文。
- **预算账**：C `.bss` **+0**——sturm 视图估算 ~2.0–2.8 KB < 3848 吸收线（构成：PRS 链 45 系数×8 limb×4 B=1440 B + 序号/符号/次数 ~400 B + bigint 工作槽 3×88 B + 二分显式栈 ~512 B【估算，开工前按 ac_pro_scratch.h 断言链复核】）；对照先例：schur 视图 2744 B、gb 1888 B 均被吸收【账本】。零新 static 对象。C `.text` 500–800 行（参照 ac_pro_sqfree.c 717 行）。JS 池 **+1 024–1 638 B**（PRS Float64Array 组 + 网格/栈 + 词法缓冲 + 溢出审计计数器，参照 sqfree 段 17 组池先例）；压缩内核 **+3 482–5 120 B**（压缩比锚：仓库 sqfree 段 23 325 B 可读 → terser 后 6 603 B，比率 0.28【实测·调研 segment】）。**JS 不做 Sturm 多精度层（评审 2 方案 b 的保留理由，成文）**：零 GC 铁律排除 JS BigInt（CONTRIBUTING.md:82-86，计算数据一律 TypedArray/全局标量），固定宽度多精度整数层等于先建半套 bigint——与 G4「纯 JS 大整数降 fallback」的裁决自相矛盾且预算失控；精确性改由域闸门 + 契约三层保障，F3 的 JS 侧定位本就是「一致性/教学面，价值如实披露不夸大」。
- **难度**：M（C）/ M–L（若 JS 镜像同窗）。真实工程量四处：精确有理 PRS 的符号正确性、端点含根的开闭语义冻结（PARI 旧版 ]a,b] + squarefree 前科）、近根「未解析」边界定义、JS 成文分叉纪律。
- **依赖顺序**：JS 侧依赖 F0；C 侧依赖契约冻结；无跨功能依赖。JS 5..8 依赖 G4（跨版，成文分叉兜底）。
- **触碰文件**：C：ac_pro_sturm.c（新）、ac_pro_scratch.h（sturm 视图 + AC_STATIC_ASSERT 链）、ac_pro.h、Makefile、test/test_pro_sturm.c（新，12–20 例）+ test_pro_runner.c tally、verify.ps1 §4 新行；JS：core_ratroot.js 扩段（ApolloSqfree 同文件先例）、build-kernel.js、test-sturm.js（新，50–70 断言）。
- **差分对照测试方案（评审 2 采纳：契约三层，零分叉区收窄为 JS 精确域）**：①开工第 1 个 commit 冻结 `docs/contracts/sturm-format-v25.md`：**计数不是根式解，不触 guardDegree 的阿贝尔-鲁菲尼边界**（features.md:168）；端点开闭语义一次定死（建议 [a,b] 闭区间计数 + 端点为根时计入，±∞ 支持）；隔离区间输出有理端点精确比对；近根「未解析」标记语义；**二分深度双侧统一上限 48 层**（超深输出「未解析」，两侧同规——避免深度成为第二处隐蔽分叉）；错误码全序（EDEG>8 / ECOEF / EPARSE / EPARAM，**ECOEF 双语义成文**：C=系数膨胀离群、JS=链中间量超 2^53 精确域）；②新建契约 `tests/sturm.json` 三层：**dualCases（deg≤4 且 Sturm 链系数全域 ≤ 2^53−1 的输入）双侧零分叉**——同语料、计数为精确整数、隔离区间端点有理数精确、错误码精确；**cOnly-DEG（5..8 次）**——C 断言计数/隔离期望、JS 断言 EDEG（polynomials.json §10-1 先例同机制）；**cOnly-OVF（链系数突破 2^53 的敌意用例，Mignotte 型构造钉死「刚好拒绝」侧）**——JS 断言 ECOEF、C 断言真实计数/隔离。**JS 镜像实现必须带链中间量溢出审计（每步 |v| > 2^53−1 即返 ECOEF，绝不输出失真值）**；③差分驱动 `tests/bench/diff-sturm.js`（diff-sysolve.js 机制，不动既有管线）；④JS ≤4 的独立价值如实披露（≤4 次两侧已有精确根式解，属一致性/教学面，不夸大）。域判定依据：随机 deg=4、|c|≤999 输入的链系数峰值实测 46 bits 安全（调研 node+BigInt 实验），但次结式行列式界允许敌意输入突破 2^53——float64 不承诺、闸门拦截、契约成文。
- **测试面**：双侧合计 ~120–190 断言（契约 JSON 60–80〔dual + cOnly-DEG + cOnly-OVF 三层〕、C 12–20 例 ≈ +50–160 checks〔每例 4–8 checks 口径〕、JS 50–70）。关键用例：全 5 实根（x^5−5x^3+4x）、零实根（x^8+1、x²+1）、重根经 SFF 后计数（(x−1)³(x+2)² 相异 2）、端点恰为根的开闭语义、±∞ 与混合端点、相邻根正常隔离、近根→「未解析」、cOnly 5..8 次 Sturm vs DK 交叉校验、负向（>8 次 EDEG、非整系数、端点超域、零多项式、大写 X、系数膨胀离群 ECOEF 的「刚好拒绝」侧钉死）。
- **风险与预案**：精确有理 PRS 符号陷阱 → 契约用 prem-free 的精确有理余 + 500 样本域判定实验复跑；**JS 精确域翻车（评审 2 指出的最坏情形）→ 已按域闸门 + cOnly-OVF 层收口，不做多精度层**；视图超 3848 吸收线（不应发生）→ 一次改齐流程（§六 R3）；JS 镜像若窗口容不下 → 顺延 v2.6 不阻塞 C 侧验收（C-only 先例：v2.4.0 schur）。

### F4 JS 一般实矩阵特征值：Francis 隐式双移 QR 移植（side=JS，补齐不对等）

- **端归属**：JS（C 侧 ac_pro_schur.c 已于 v2.4.0 交付，本功能零 C 改动）。
- **内容**：core_matrix.js pro 层扩展：Householder 化 Hessenberg → Francis 隐式双移 QR（bulge chasing，全显式循环零递归）→ 全部特征值（实/复共轭对）；可选反迭代实特征向量。算法逐条对照 ac_pro_schur.c（540 行【账本】：幂 2 精确缩放、单特征值 50 轮预算 + EISPACK 同款异常位移、不收敛返 −2 绝不输出垃圾、反迭代残差 ≤1e-9·(1+‖A‖) 六组尝试全失败返 −3、复特征值不产出向量）。
- **入选理由**：features.md:171-173 成文未落地项「一般矩阵随 v2.5 评估」——本次就是该评估，判定入版；这是 JS 侧最后一个矩阵能力不对等项，先补已承诺的不对等再开新数值面（G2 SVD 因此排 v2.6）；C 参照实现完整，移植风险可控。
- **预算账**【估算·主笔，开工前必须复核】：JS 池 **+~2.75 KB**——锚 = ROADMAP.md:145 对 C 侧的原估算（H 1152 + wr/wi 192 + work ~256 ≈ 1.6 KB；加反迭代向量 +1152 ≈ 2.8 KB），core_matrix.js pro 池现 5944 B（features.md:98【账本】）→ 约 8.7 KB，allocCount 7→9 可审计；压缩内核 **+3–5 KB**（锚 = ac_pro_schur.c 540 行 → JS 可读估 +10–14 KB × 压缩比 0.28–0.35【实测锚：sqfree 段 0.28】）。
- **难度**：M——无未解理论风险（C 侧同算法已验证），工作量在 JS 化改写与收敛边角复现；零 GC 化改造（显式循环 + TypedArray 工作区）。
- **依赖顺序**：依赖 F0（JS 池前置）；与 F1/F2/F3/F5 无依赖。
- **触碰文件**：core_matrix.js、build-kernel.js EXPORT_BLOCK（matrix 命名空间加方法）、test-matrix-eig.js（新，50–70 例；红线文件 test-matrix-pro.js 一字不改）；C 侧零触碰。
- **差分对照测试方案（零分叉）**：①新建共享契约 `tests/eigen-cases.json`（dual 全量，本功能双侧全对等、纯 Float64 域无 int64 问题 → 全域零分叉）：特征值 |λ| 降序 ≤1e-9、复共轭对配对语义、错误码精确（−1 参数 / −2 不收敛 / −3 向量失败）；**排序与复对配对全序以 ac_pro_schur.c 既有输出契约为准逐字转录进契约文档，两侧不得各自发明**；②C 探针：C/128KB-ProTEXT/test/ 新增独立 eig csv 导出探针（不改 smoke_runner），生成 tests/bench/eig128-c.csv，JS 侧直读契约 JSON，驱动脚本 `tests/bench/diff-eigen.js` 比对 exit 0；③特征向量符号规范化契约：每列取最大 |·| 分量置正、并列取最小索引——两侧同式同序（v2.6 G2 SVD 同款规则，一次定死两处复用）；④退化谱（重特征值/复对密集）只对特征值断言，向量走残差不变量 ‖Av−λv‖ ≤ 1e-9·(1+‖A‖)（C 侧既有判据逐字复用）——ODE knownDeviations 教训的直接应用。
- **测试面**：JS 50–70 例；契约 JSON 40–60 条；关键用例：旋转矩阵、友矩阵（特征值=多项式根，与求解层交叉验证）、复共轭对、已知闭式谱对称阵（2+2cos(kπ/5) 三对角先例）、病态矩阵、幂 2 极端尺度、1×1/2×2 形状边界、超 12 阶/NaN 拒绝、「刚好通过/刚好拒绝」双侧。
- **风险与预案**：收敛边角（简并特征值）→ C 侧 50 轮预算 + 异常位移逐字移植；池超预算 → 水位门禁（F0 改造后）当场拦截，回退方案为向量功能降可选。

### F5 WASM 前景文档（side=生态基建；v3 裁决降级为纯文档调研）

- **端归属**：生态基建（仅新增 docs；归收尾组在 master 上串行执行）。
- **v3 裁决**：产品侧**拒绝安装 LLVM/clang**——项目定位极致轻量、零外部重型依赖，纯 C 与纯 JS 双通道已满足「开发者直接拉取源码使用」；WASM/npm/TypeScript/API 文档主题整体顺延 v2.7+。本功能不做任何工具链安装、编译实验或代码改动。
- **内容**：把原 spike 调研结论存档为 `docs/wasm-future.md`：①工具链现状实测（本机 PATH/mingw64/ucrt64/clang64 均无 clang，gcc 8.1.0 无 wasm 后端【实测·调研 segment】）与未来启动所需的安装/构建路径；②freestanding 垫层清单（mem*/str* 防 LLVM 回退内置调用、musl 系 exp/log/pow/tan/asin/acos/atan/atan2、%.15g/%d/%s 最小格式化）；③体积估算存档（全内核 33 TU `.text` 204 764 B【账本】→ wasm 估 220–300 KB 未压缩、gzip ~1/3【估算】）；④零 import 模块架构、50 用例 native-vs-wasm 差分验收线方案存档；⑤顺延裁决与翻案条件成文。C/JS 源码与全部账本零触碰。
- **入选理由**：调研数据已产出，存档防漂移；产品侧未来翻案时可即时复用，不必重做调研。
- **预算账**：仓库账本零影响（纯文档）。
- **难度**：S。**依赖顺序**：无前置；归收尾组。
- **触碰文件**：docs/wasm-future.md（新）。**不碰** C/128KB-ProTEXT 与 JS/128KB 任何文件。
- **差分对照测试方案**：不适用（纯文档）。
- **风险与预案**：若产品侧未来翻案重启 WASM，按本文存档数据直接进入工具链 spike（届时按 §6.2 R5 重估 v2.7+ 计划）。

---

## 四、v2.6.0 版本规划（4 项，主题：应用落地）

> 版本主题：**v2.6.0 = 让能力抵达使用者**——WASM 全内核 + npm 发布 + TypeScript 声明 + 终版 API 文档构成应用落地主线；SVD 收口数值线性代数对标缺口；基准口径补齐为文档供数。
> 组件版本：C/128KB-ProTEXT **v1.7.0→v1.8.0**、JS/128KB **v2.5.0→v2.6.0**（v2.5 收尾组已把两组件升至 v1.7.0 / v2.5.0，本版从该基线递进；双轨制；npm 包版本与全局 Tag 对齐）。**实际执行修正：JS/128KB 版本不动（维持 v2.5.0）**——本版全局 Tag 只覆盖 C 侧，见文末附录「v2.6 执行记录」。
> 全局增量预算封顶【估算，字节口径，1 K = 1024 B；v3 裁决后按预案 B 执行】：C `.bss` +0（G2 svd 视图 3752 B 吸收线内）；JS 压缩内核再 +3 072–4 096 B（G2）+ **G4' 纯 JS 大整数移植（L 级）——开工前先出内核字节预算 spike，spike 定数前不列封顶**；熔断线 **126 976 B** 纪律不变（§6.2 R2），G4' spike 须对照熔断线给出 go/no-go。

### G1 WASM 全内核交付 + npm 发布 + TypeScript 声明 + 最终版 API 文档（side=生态基建+双端）——**v3 裁决：整体顺延 v2.7+，本期不执行**，以下原文存档供未来复用

- **端归属**：生态基建为主体（wasm 构建、npm 包、types、docs/api）；差分对拍涉及双端。
- **内容与交付物（四件套）**：
  1. **wasm 产物**：新目录 `wasm/`（与 C/128KB-ProTEXT 平级，沿用「上游零改动包装 TU」架构，verify.ps1 §5 git 双证不受影响）：全内核口径（33 obj 204 764 B `.text` 实测【账本·调研 segment size 统计】→ wasm 估 220–300 KB 未压缩、gzip ~1/3【估算，以 F5 实数定稿】）；文件线门禁：上游 16 模块口径 ≤131 072 B 未压缩（对齐 JS 内核物理线惯例）、全内核口径线由 F5 阶段 B 实数定；freestanding 垫层按 F5 清单转正；**零 import 节** + 线性内存静态池 + 双缓冲字符串 ABI（契约 `docs/contracts/wasm-abi-v26.md` 先冻结：f64/i32 导出、TextEncoder.encodeInto 零 GC 字符串进出、错误码/句柄表逐项成文）。原生 C 侧 `.bss` 四口径 == 断言**不动**（wasm 是独立目录独立对象，ROADMAP.md:47 每 static +32 B 规则不触发）。
  2. **npm 发布脚本（评审 4 采纳：dist-npm 暂存布局）**：仓库根**没有** `src/`（JS 源在 JS/128KB/src/，本会话 ls 实读；根目录亦无 package.json/types/wasm）——原稿 exports 指向 `./src/...` 在仓库内不成立。改为发布现场打包：`tools/build-npm-package.js` 把交付物拷贝/生成进暂存目录 `dist-npm/`（gitignore，不进版本库），`npm publish ./dist-npm` 以其为包根，exports 在包内自洽。仓库侧编排成文：`npm run pack:cas` = `node tools/build-npm-package.js && npm publish ./dist-npm`；build-npm-package.js 四步——①（需 clang）`bash wasm/build.sh` 出 apollocalc.wasm；②`node JS/128KB/test/build-kernel.js --minify` 出 core_kernel.js（复跑后 git status 核对还原，防工具链漂移）；③拷贝 + 生成 `dist-npm/{package.json, README.md, LICENSE, src/core_kernel.js, src/core_wasm.js, wasm/apollocalc.wasm, types/, docs/api/}`；④包内自检（exports 路径存在性 + `node -e "import('dist-npm/src/core_kernel.js')"` 冒烟）。`dist-npm/package.json`（发布清单；构建脚本留在仓库侧，不随包走）：
     ```json
     {
       "name": "apollocalc-cas",
       "version": "与全局 Tag 对齐（双轨制）",
       "exports": {
         ".":      { "types": "./types/index.d.ts", "import": "./src/core_kernel.js" },
         "./wasm": { "types": "./types/wasm.d.ts",  "import": "./src/core_wasm.js" }
       },
       "files": ["src/", "wasm/", "types/", "docs/api/", "README.md", "LICENSE"]
     }
     ```
     **.wasm 产物不进 git**（构建产物、体积 200–300 KB 级；可复现性由 wasm/build.sh + CI 保证。对照：tools/gen-types.js 生成物进 git，因其是人工审核过的类型契约）。wasm 构建依赖 clang 的前提写进仓库 README 与 npm 包 README 双处；`tools/api-doc-coverage.js --strict` 挂 pack:cas 链与 CI，强制 API 文档覆盖率 100% 才允许发包。**发版三关与 .wasm 解耦（评审 4 采纳）**：wasm 门禁 `tests/wasm/wasm-smoke.js` 是 G1 附加验收关 + pack:cas 内置步骤，落 `tests/wasm/` 而非 JS/128KB/test/——实读 run-all.js:22-24，发现模式 `/^test-.*\.js$/` 只匹配 test-*.js，wasm-smoke.js 该文件名本不会被自动发现（评审的机制描述据此修正），但迁移可杜绝未来命名踩线、并使发版三关之二在无 clang 机器照常可跑；结构性担忧成立，照改。
  3. **TypeScript 声明文件**：`types/index.d.ts`（JS128 路线）+ `types/wasm.d.ts`（wasm 路线），两路线**同签名**（契约 wasm-abi-v26.md 钉死）：命名空间镜像 EXPORT_BLOCK 结构；五分量精确形式接口（getRootExactRat/RadNum/Rad/Den/Exp/Valid，features.md:213-216 数据契约口径）；`solve` 返回形态（err/count/roots）；ODE 与 sqfree 字符串契约类型别名；错误码 union 类型 + 数值常量表（从契约 JSON 生成）。生成器 `tools/gen-types.js` 从 EXPORTED_NAMES/EXPORT_BLOCK 提取名册 + 人工补类型；**生成物进 git**，发布前 re-diff 校验防漂移。
  4. **最终版 API 文档**：`docs/api/` 逐命名空间页（v2.4.3 全导出面 + v2.5 F1/F2/F3/F4 + v2.6 G2/G1 新增），`tools/api-doc-coverage.js` 校验导出面 100% 覆盖（挂 prepublishOnly）；根 README 汇总表更新；docs/benchmark.md 数字由 G3 供源。
- **预算账**：原生账本 +0；JS 压缩内核 **+1.5–3 KB**（core_wasm.js loader 200–350 行，对标最小模块 core_special.js【账本】）；JS 池 +0（loader 仅视图）→ 水位门禁压力 0；npm 包体积 ≈ wasm 产物 + loader + types + docs（F5 实数定稿后写入 package.json 与 README）。
- **难度**：M+（全内核口径在 M+/L 边缘：零算法风险，脏点在 %.15g 跨编译器一致性与第三条差分链进发版门禁）。
- **依赖顺序**：**硬依赖 F5**（clang 工具链、体积实数——含阶段 B 全内核 33 TU 数据、垫层清单含 ctype/strtod 类）；依赖契约冻结；与 G2 并行、与 G3/G4 数据耦合。
- **触碰文件**：wasm/（新）、tools/build-npm-package.js（新）、dist-npm/（构建暂存，进 .gitignore）、types/（新）、JS/128KB/src/core_wasm.js（新）、JS/128KB/test/build-kernel.js FILES 清单（loader 入列）、tests/wasm/wasm-smoke.js（新）、JS/128KB/test/test-wasm-loader.js（新，run-all 自动发现）、docs/contracts/wasm-abi-v26.md（新）、docs/api/（新）、tools/gen-types.js、tools/api-doc-coverage.js（均新）、根 README（npm/clang 前提说明）。
- **差分对照测试方案（三层，全部进发版门禁）**：①native C vs wasm C 同源码对拍（最严：err/count 精确 + 数值 ≤1e-9 + bigint/精确形式整数域全等；字符串字段单列宽松比以隔离 %.15g 跨编译器差异）；②wasm vs JS128 复用 diff-smoke128 的 50 用例语料与判定口径（solve err/count 精确 + 12 数值 ≤1e-9 + exact 五分量精确）；③tests/ 四套既有契约 JSON **零增改**（CONTRIBUTING.md:105-106）——wasm 首期只暴露与 JS128 已有 API 同语义面，不新开功能。
- **测试面**：~60–120 checks（wasm-smoke 50 用例复用 c128-smoke.csv 语料 + 10–20 条 ABI/边界专项 + 构建层断言：零 import 节、文件线、双目标链接）+ JS 新测试文件 test-wasm-loader.js 20–40 断言（run-all 自动发现；wasm 门禁本体在 tests/wasm/wasm-smoke.js，属 G1 附加关、不进 run-all）。关键用例：错误码精确、池满 ±1 B / token 池边界的「刚好通过/刚好拒绝」、IEEE 传参边界（±0/±Inf/NaN、DEG/RAD）、180 位 bigint 阶乘/斐波那契（C 独有能力经 wasm 的招牌验证）、字符串缓冲截断、实例化失败与零 import 断言。
- **风险与预案**：%.15g 一致性 → 字符串宽松比 + 数值口径兜底；体积超线 → 上游口径先行、全内核口径按 spike 数据裁剪（垫层函数逐个瘦身）；loader 与 EXPORT 漂移 → gen-types re-diff 门禁。

### G2 SVD 双端：one-sided Jacobi 奇异值分解（side=双端）

- **端归属**：双端（C：新 TU ac_pro_svd.c + svd 视图；JS：core_matrix.js pro 层扩展）——SVD 是纯数值面，两侧算法完全同构，拆单侧会再添一处「C 独有」不对等清单条目。
- **内容**：m×n（m,n≤12；m<n 走转置）one-sided Jacobi（Hestenes，LAPACK dgesvj 同族）：A 副本上固定循环序列正交化，旋转累计进 U/V；收敛后各列 2-范数降序稳定插入排序（ac_pro_schur.c:440-486 同款【账本】）、U/V 列同步换序；全程 frexp/ldexp 幂 2 缩放（ac_pro_schur.c:342-369 同纪律）；出口双闸门：‖A−UΣVᵗ‖∞ 与 ‖UᵗU−I‖ 回乘验证，不过闸返错误码绝不硬凑；sweep 预算封顶（30 轮）超限返 −2 诚实失败。**不做 lstsq/最小二乘**（§2.2 边界）。
- **入选理由**：本工作流调研即 ROADMAP.md:75「v2.4 后观察」窗口的观察动作，现判定落地——**G2 合入即成文关闭该观察窗口**；可行性已由吸收线数学钉死；工程全绿。
- **预算账**：C svd 视图 **3752 B ≤ 3848 吸收线**（构成：a 工作副本 144×8=1152 + U 1152 + V 1152 + w 96 + work 192 + 计数/对齐 8，margin 96 B【估算·调研，开工前按 ac_pro_scratch.h 断言链复核】）→ `.bss` +0，sparse 3848 仍是峰值、union 尺寸与账本 42608 不动；新 TU 零 static 对象（schur 同款 accessor 停泊进视图槽）。JS 池 **+1248 B**（新增 _svdU 1152 + _svdW 96；复用 _luBuf 作缩放工作副本、_vBuf 作 V 的既有轮换约定），allocCount 7→8 可审计；压缩内核 **+3–4 KB**（numtheory 14 KB→~2 KB、symboltree 12–15 KB→+4–5 KB 的压缩比锚外推【账本·ROADMAP.md:94/:131】）→ 131 072 线余量约 24 KB。
- **难度**：M——比 Francis 双移 QR 简单一档（无 bulge chasing/异常位移/2×2 分裂，核心紧循环 ~200 行）；成本在跨语言符号/排序规范化契约与退化谱语义一次定死。
- **依赖顺序**：JS 侧依赖 F0（跨版即已满足）；依赖契约冻结；与 G1/G3/G4 无依赖，可并行。
- **触碰文件**：C：ac_pro_svd.c（新，~500 行）、ac_pro_scratch.h（svd 视图 + 断言链）、ac_pro.h、Makefile、test/test_pro_svd.c（新，13–16 例）+ test_pro_runner.c tally、verify.ps1 §4 新行；JS：core_matrix.js、build-kernel.js、test-svd.js（新，40–70 ok()；红线文件 test-matrix-pro.js 一字不改）。
- **差分对照测试方案（零分叉）**：①开工第 1 个 commit 冻结 `docs/contracts/svd-format-v26.md`：奇异值降序 + 相对容差 1e-9；错误码全表（−1 参数、−2 不收敛、−3 回乘验证失败）；**奇异向量符号规范化与 F4 同式同序**（每列最大 |·| 分量置正、并列取最小索引）；**退化谱语义**：重奇异值/σ=0 的向量子空间数学上不唯一，契约只断言 w 精确一致 + 重构/正交不变量，逐向量严格对拍仅限良分离谱（ODE 三条 knownDeviation 教训的直接应用）；**Jacobi 旋转公式的表达式书写顺序两侧逐字一致**（IEEE754 binary64 下才能稳进 1e-9）；②新建契约 `tests/svd-cases.json`（40–80 例）；③差分 fuzz 150–400 条：生成器 `tools/gen-svd-corpus.js` 落 tools/（固定种子整数矩阵 + 结构谱 diag/正交×diag×正交ᵗ/已知谱对称阵 + 秩亏 + 近重 σ + Hilbert 病态 + 1e±150 幂 2 极端尺度 + 拒绝类 >12 阶/NaN/m=0）；对拍口径 = w 逐条 ≤1e-9 + 错误码精确 + 矩阵量走不变量（UᵗU≈I、重构残差）。
- **测试面**：C 13–16 例五组（basic 方阵 thin / rectangular 矩形 / degenerate 秩亏+重 σ / scale 极端幂 2 / reject 全拒绝码，含双侧边界；SCHUR 13 例/55 CHECK 为接线锚）；JS 40–70；契约 JSON 40–80；fuzz 150–400。关键用例：对角/置换/恒等（w=|diag|、向量列=±e_i 钉符号规范化）、已知闭式谱对称阵、正交×diag×正交ᵗ 合成（全链精确可验）、秩亏与零矩阵、近重奇异值、1×1/1×n/n×1 形状边界、1e±150 缩放往返、超 12 阶/NaN/非有限拒绝。
- **风险与预案**：svd 视图超吸收线（margin 仅 96 B）→ 视图构成开工前复核，超线走一次改齐流程（§六 R3）；退化谱对拍分歧 → 契约不变量口径已兜底。

### G3 压测口径扩展与基准终版（主笔补充）

- **端归属**：生态基建（tests/bench + docs）。
- **主笔补充依据**：docs/benchmark.md 成文注记「Pro 层与 JS v2.0.0 后新能力未纳入该口径，无实测不列数字」【账本】；G1 的「最终版 API 文档」与 README 对比表需要可信基准数字供源（docs/getting-started.md §4 的 2026-11-03 实测是单点快照，需进自动化管线防漂移）。
- **内容**：tests/bench/run-all.sh 从「四版本×四 stage×3 轮中位数」扩为**五列**（新增 pro_runner 列；wasm 列随 v3 裁决顺延 v2.7+ 另定）；四 stage 口径不变；BENCH-1M.md 与 docs/benchmark.md 出 v2.6 终版；getting-started.md 对比表改为从 BENCH 报告引用。
- **预算账**：零 .bss、零池、零内核字节（纯测试与文档）。
- **难度**：S–M。**依赖顺序**：无 wasm 列（v3 裁决顺延）；随 v2.5/v2.6 功能合入扩列；文档终版最后发布。
- **触碰文件**：tests/bench/run-all.sh、tests/bench/BENCH-1M.md、docs/benchmark.md、docs/getting-started.md、根 README 对比表——与 G1 组 docs 有交集 → 归 feat/wasm 组统一触碰（见 §五）。
- **风险与预案**：wasm 计时受实例化噪声 → 预热轮次丢弃首轮，口径成文。

### G4' 纯 JS 大整数移植 + C 独有分叉收口（side=JS；v3 裁决原 fallback 转正）

- **端归属**：决断文档归生态基建；若 fallback 激活则实施归 JS。
- **范围**：C 独有六块 JS 缺口的总收口——bigint（20 limb×1e9，180 位）、bigdec（180 位定点十进制）、sparse（CSR n≤32/nnz≤320）、rational（GCD/cancel/apart）、integrate_multi（多变量积分）、special 完整组（Beta/igamma/Bessel）（features.md:62-67、:165-177、ROADMAP.md:24【账本】）。
- **裁决（v3 产品侧修订成文）**：**主路线 = 纯 JS 大整数移植（原 fallback 转正）**——20 limb×1e9 TypedArray 直接移植 ac_bigint（零 GC：显式循环 + 加载期池，禁 JS BigInt），开工前必须先出内核字节预算 spike（对照熔断线 126 976 B 给 go/no-go，spike 定数前不编造预算），并按 F0 改造后的水位门禁走全池审批。**连带解锁**：JS Sturm 5..8（F3 成文分叉收口）、sqfree 5..8（features.md:199-202 收口）、ncr/bigdec 等数字面；G4' 合入后 features.md 对应条目改写为「JS 原生已实现（纯 JS 移植，与 C 同构）」。原 G1 WASM 通道整体顺延 v2.7+（存档见 docs/wasm-future.md）。
- **v2.6 实际交付物**：纯 JS bigint/bigdec 移植（难度 L）+ JS Sturm 5..8 与 sqfree 5..8 分叉收口（契约修订 + 双侧重跑）+ docs/features.md 对应条目改写。
- **难度**：L（纯 JS 移植本体）。**依赖顺序**：依赖 F0 水位门禁（v2.5 已合）与内核字节预算 spike；与 G2 并行，可并行开工。
- **触碰文件**：docs/features.md（收尾组执行）、本文件附录；fallback 激活时另加 JS 新模块 + build-kernel + 测试新文件 + 契约 JSON。
- **风险与预案**：wasm 通道接受度不及预期（字符串 ABI 摩擦）→ 由 wasm-smoke 与 G3 基准数据实证后再裁；fallback 预算超文件线 → 按熔断线（§六 R2）顺延 v2.7 并如实成文。

---

## 五、Git Worktree 并行切分

> 分组原则（沿用 ROADMAP.md:165）：每个分支独占一个目录/一组热点文件；**共享注册表文件靠「预埋锚点区块 + 固定合并序」而非回避**；根级账本文件（CHANGELOG/features/README/VERSION）一律归收尾组。

### 5.1 v2.5.0：3 个 worktree + 收尾组

| 分支组 | 覆盖功能 | 主要触碰文件 | 独占地盘 |
|---|---|---|---|
| **feat/js-gate-f0**（早合） | F0（治理前置） | JS：core_solvers.js（水位门禁改造）、JS/128KB/test/ 新增水位自检文件；docs/js-128kb.md、docs/ACCEPTANCE.md、build-kernel.js 注释修回；tools/（gen-ode-corpus.js、odefuzz.c/.js）；tests/ode-fuzz-corpus.txt（新） | core_solvers.js、docs/js-128kb.md、docs/ACCEPTANCE.md 独占；**合并序第 0 位先合 master**——F1–F4 全部 JS 侧的前置 |
| **feat/math-ext** | F1（复变扩展） | C：ac_pro_complex_ext.c（新）、ac_pro.h（v2.5 API 区锚点）、Makefile（PRO_EXTRA 锚点）、test_pro_complex_ext.c（新）+ test_pro_runner.c（CX tally 锚点）、verify.ps1（§4 CX 行锚点）；JS：core_complex.js、build-kernel.js（EXPORT_BLOCK complex 块）、test-complex-ext.js（新）；契约：docs/contracts/complex-ext-v25.md、tests/complex-functions.json（均新）、tests/bench/diff-complex.js（新）；tools/gen-complex-corpus.js（新） | core_complex.js 与两个新契约文件独占；C 侧工作量最轻（**C 侧可在 F0 合入前先行开工**，仅依赖契约冻结）；合并序第 1 位 |
| **feat/solvers** | F2（方程组 GB 消元）+ F3（Sturm） | C：ac_pro_sysolve.c、ac_pro_sturm.c（均新）、**ac_pro_scratch.h（全仓唯一触碰者：sturm 视图）**、ac_pro.h/Makefile/test_pro_runner.c/verify.ps1（各自锚点）；JS：core_ratroot.js（sturm 段）、core_groebner.js（sysolve 段 + DK 移植）或新 core_sysolve.js、build-kernel.js、test-sysolve.js/test-sturm.js（新）；契约：sysolve-format-v25.md、sturm-format-v25.md、tests/systems.json、tests/sturm.json（均新） | ac_pro_scratch.h、core_ratroot.js、core_groebner.js 独占；**合并序第 2 位** |
| **feat/js-eigen**（v3 改名，原 js-parity-eco） | F4（Francis 移植） | JS：core_matrix.js（Francis）、build-kernel.js（EXPORT matrix 块）、test-matrix-eig.js（新）；tests/bench/diff-eigen.js、tests/eigen-cases.json、eig128-c.csv 探针（新） | **不碰 C/128KB-ProTEXT 源码与账本**；core_matrix.js 独占（F0 已拆出至 js-gate-f0）；合并序第 3 位 |
| **feat/release-v2.5.0**（收尾，串行，master 直做） | 全部收口 + F5 | 根 CHANGELOG.md、docs/features.md、根 README.md、各组件 CHANGELOG/VERSION（C v1.7.0 / JS v2.5.0）、**F5 纯文档调研（docs/wasm-future.md，v3 降级）**、benchmark-giac.md:129 弃用理由修订、verify.ps1 头注数字更新（若各组末触未改）；F2 可选联动顺延 v2.6（随 G4' 一并收口） | 前四组 merge 后才开工 |

**固定合并序：`feat/js-gate-f0`（序 0，早合）→ math-ext（序 1）→ solvers（序 2）→ js-eigen（序 3）**；各组 C 侧工作（math-ext/solvers）不受 F0 阻塞，仅依赖各自契约冻结；F5 文档归收尾组。

**热点归属与冲突消解（逐文件想清楚）**：

| 共享文件 | v2.5 触碰组 | 消解机制 |
|---|---|---|
| ac_pro.h / Makefile（PRO_EXTRA）/ test_pro_runner.c / verify.ps1（§4） | math-ext、solvers | 开工时先合入一个**锚点基建 commit**（在各文件预埋「v2.5 新增区：CX / SYSOLVE / STURM」标记注释块），各组只在锚点内加行；**固定合并序 math-ext → solvers → js-parity-eco**，后合并者 rebase 解冲突 |
| build-kernel.js（FILES 数组 + EXPORT_BLOCK） | math-ext、solvers、js-eigen 三组都触（js-gate-f0 不触） | EXPORT_BLOCK 每功能一个独立命名空间块天然低冲突；FILES 数组一行冲突由固定合并序（0→1→2→3）解决 |
| ac_pro_scratch.h | 仅 solvers | 无冲突；其余组开工时确认断言链不改 |
| core_complex.js / core_ratroot.js / core_groebner.js / core_matrix.js | 各归一组，两两无交集；core_solvers.js 归 js-gate-f0（F0 水位门禁） | — |
| 既有 tests/ 四套契约 JSON + tests/bench/diff-smoke128.js 50 例 | **开发期全冻结** | 各组一致性用例先以内嵌测试落位，merge 后收尾组统一处置（ROADMAP.md:176 先例）；F2 联动的 polynomials.json 修订也在收尾组 |
| LUT（C/128KB-ProTEXT/tools/gen_lut_tables.js 与 AC_PRO_LUT 变体） | v2.5 **无功能触碰 LUT**（F1 的 Lanczos 表是自持 static const，不进 LUT 管线） | 若实现中确需触碰，只允许 feat/solvers 组（F3 是唯一可能涉及函数表的方向，且预估不需要）并在 PR 描述声明 |
| 根 CHANGELOG / docs/features.md / 根 README / VERSION | **feat 分支一律不碰** | 收尾组按 commit 链提炼（ROADMAP.md:238 先例）；组件内文档（如 C README §6 契约节）随功能提交，C README 双组触碰时按合并序 rebase |
| run-all.js | 零改动 | readdirSync 自动发现新测试文件（run-all.js:22-24） |
| docs/contracts/ 与 tools/ 与 tests/bench/ 的新增文件 | **文件级归属、目录级共享（评审 8 采纳，原「docs/、tools/ 独占」目录级表述作废）**：contracts 新文件 + tests/bench/diff-complex.js（F1 差分驱动落点名）+ tools/gen-complex-corpus.js 归 math-ext；diff-sysolve.js、diff-sturm.js 归 solvers；diff-eigen.js、eig128-c.csv 生成探针归 js-eigen；tools/ 的 odefuzz 固化三件归 js-gate-f0；tools/ref_prs.py 等既有文件零触碰 | 目录级不设独占、冲突只在同名文件——各组文件名互不相同即零冲突 |
| 契约文档（docs/contracts/） | 各组新建各自文件，互不交集 | 契约先行：各组开工第 1 个 commit 冻结契约合 master，JS 侧从 master 读取 |

### 5.2 v2.6.0：2 个 worktree + 收尾组

| 分支组 | 覆盖功能 | 主要触碰文件 | 独占地盘 |
|---|---|---|---|
| **feat/matrix-advanced** | G2（SVD 双端，**核心攻坚**） | C：ac_pro_svd.c（新）、ac_pro_scratch.h（svd 视图）、ac_pro.h、Makefile、test_pro_svd.c（新）+ test_pro_runner.c、verify.ps1 §4 新行；JS：core_matrix.js、build-kernel.js（EXPORT matrix 块）、test-svd.js（新）；契约：svd-format-v26.md、tests/svd-cases.json（均新）、tools/gen-svd-corpus.js | C 侧与 core_matrix.js 的唯一触碰者；合并序第 1 位 |
| **feat/js-bigint** | G4'（纯 JS 大整数移植，含 JS Sturm 5..8 / sqfree 5..8 收口） | JS：core_bigint.js（新，20 limb×1e9 TypedArray 镜像 ac_bigint）、build-kernel.js（FILES+EXPORT bigint 块）、core_ratroot.js 等相关 5..8 扩段、新测试文件；契约修订（sturm.json/polynomials.json cOnly→dual 迁移由收尾组统一执行） | JS 新增面独占；开工前置 = 内核字节预算 spike 过熔断线 go；合并序第 2 位 |
| **feat/release-v2.6.0**（收尾，串行，master 直做） | G3（基准终版）+ 全部收口 | tests/bench/run-all.sh、docs/benchmark.md、docs/getting-started.md、根 README、根 CHANGELOG、docs/features.md（G4' 条目改写）、组件 VERSION（C v1.8.0 / JS v2.6.0） | 两组 merge 后开工 |

冲突面：两目录正交，唯 build-kernel.js 双触（bigint FILES/EXPORT vs svd EXPORT）→ **固定合并序 matrix-advanced → js-bigint**；docs 与基准交集归收尾组。

---

## 六、依赖图、风险登记与开工 checklist

### 6.1 依赖顺序总图

```
feat/js-gate-f0(F0, 早合序 0) ──合入 master──→ F1/F2/F3/F4 的全部 JS 侧
（F1/F2/F3 的 C 侧仅依赖契约冻结，可与 F0 并行开工）
F1、F2、F3、F4 互相独立；F5 已降级纯文档（docs/wasm-future.md），归收尾组
v2.5 全项合入且过发版三关 ──→ v2.6 开工（v3 裁决按预案 B）
G2(SVD 双端，核心攻坚) 与 G4'(纯 JS 大整数，先出预算 spike 过熔断线) 可并行
G4' ──→ JS Sturm 5..8 / sqfree 5..8 分叉收口；G3 基准终版归收尾组最后发布
```

### 6.2 风险登记表

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| R1 | **JS 池水位失守**：170 304 B 已破 98 304 口径，门禁只计基础池；v2.5 四个 JS 功能再加池 | 高 | F0（feat/js-gate-f0 早合）改造为全池口径门禁并定新线 **178 496 B**（170 304 + 8 192 封顶，对分项和 +4 864–6 502 B 留 26–41% 余量）；每模块 allocCount 可审计；v2.5 JS 新增池合计封顶 8 192 B（F1 0 + F2 1 024–2 048 + F3 1 024–1 638 + F4 ~2 816）；超预算即当场回退该功能池设计 |
| R2 | **压缩内核逼近 131 072 B 文件线**：v2.5 末估 114 761–121 519 B、v2.6 末估 119 369–128 687 B【估算 ±20% 粗度】 | 中高 | **熔断线 126 976 B（= 131 072 − 4 096 B 应急余量；按字节定义，消除 KB/KiB 混读）**：任次 `build-kernel --minify` 实测 ≥126 976 B 即触发降载序——①G1 loader 瘦身（v2.6 新增面中唯一有实现弹性的项：环形缓冲调小、错误文案压缩）；②仍超则 G2 的 JS 面顺延 v2.7、SVD 成文 C-only（先例：v2.4.0 schur）。按估算上限 v2.6 会触线，此为如实量化；每功能压缩后立即实测登记进 PR，末位功能先量后合 |
| R3 | **C `.bss` == 账本漂移**：新视图超 3848 吸收线（SVD margin 仅 96 B、Sturm 估 2.0–2.8 KB）、F1 链接对齐垫片挪动 | 中 | 视图/缓冲构成开工前按 ac_pro_scratch.h 断言链复核并写进 PR；零新 static 对象纪律（错误码走返回值）；一旦超线，Makefile×4 + ac_pro_config.h + verify.ps1 **一次改齐**并重走 verify.ps1（ROADMAP.md:189-190），禁止分次改 |
| R4 | **双端 1e-9 对齐失败**：cgamma 误差包络未标定、Jacobi/公式表达式书写顺序、%.15g 跨编译器 | 中 | 契约钉死表达式书写顺序（两侧逐字一致）；cgamma 预案降 C 独有或后撤（其余 API 不受牵连）；wasm 差分口径避开字符串字段 |
| R5 | **WASM 工具链缺口**：本机无 clang（调研实测） | **已裁决（v3）**：产品侧拒绝安装 LLVM/clang（零外部重型依赖）——F5 降级纯文档调研，G1 整体顺延 v2.7+，**预案 B 即 v2.6 正式主题**（SVD 双端核心 + 基准终版 + G4' 纯 JS 大整数转正）；调研数据存档 docs/wasm-future.md 供未来翻案复用 |
| R6 | **并行合并冲突**：注册表文件（ac_pro.h/Makefile/test_pro_runner.c/verify.ps1/build-kernel.js）多组触碰 | 中 | 预埋锚点区块 + 固定合并序（5.1/5.2 表）；tests JSON 开发期冻结；根账本归收尾组 |
| R7 | **会话级资产丢失**：.auto/ 的 ODE fuzz 语料/探针被复用前丢失 | 低 | F0(c) 固化进 tests//tools/ 后才允许路线图复用；固化前禁止引用 .auto/ 路径 |
| R8 | **能力不对等误称「对等」**：wasm 替代被宣传成 JS 原生对等 | 中 | G4 成文口径「通道级替代，非对等」；所有 cOnly 区/knownDeviations 机制沿用（features.md:143-145、polynomials.json §10-1 先例） |
| R9 | **冻结面被误触**：上游 C/128KB、64KB 家族、20 个 test-*.js、smoke 50 例 | 低 | verify.ps1 §5 git 双证兜底上游；冻结清单写进各组 PR 模板；20 个 test-*.js 与 diff-smoke128 50 例列为红线（ROADMAP.md:198、:177 先例） |
| R10 | **F3 JS 浮点精确域**：deg=4、\|c\|≤999 敌意输入（Mignotte 型）的 Sturm 链系数可破 2^53，float64 承诺不了精确计数与有理端点；「刚好拒绝」边界类必踩（评审 2） | 中 | JS 镜像带链中间量溢出审计（每步 >2^53−1 → ECOEF，绝不输出失真值）；契约三层 dualCases（≤2^53 域内零分叉）/ cOnly-DEG / cOnly-OVF 成文；二分深度双侧统一 48 层；不做 JS 多精度层（零 GC 铁律 + G4 裁决，保留理由见 F3 预算账） |

### 6.3 每功能开工 checklist（不了解上下文的工程师照此执行）

1. `git worktree add .worktrees/wt-<name> -b feat/<name>`（从 master；仓库既有惯例：.worktrees/ 已列 .gitignore:50 且现存 worktree【实读】，勿散落盘符根——评审 12 采纳），对照 §五表确认自己组的地盘与合并序。
2. 读本文对应功能节 + 它引用的契约先例（docs/contracts/ode-format-v23.md、sqfree-format-v24.md 是格式范本）。
3. 契约功能：**开工第 1 个 commit** 冻结 `docs/contracts/<格式>-v2X.md` 合 master，JS 侧从 master 读取；契约变更须双组确认。
4. 复核基线并记录进 PR 描述：C 侧 `cd C/128KB-ProTEXT && mingw32-make SHELL=cmd.exe bss-check`（期望 kernel 42608 / runner 42640 / upstream 39408 精确命中）；JS 侧 `node test/build-kernel.js --minify` 后 `git status` 核对、有差异即 `git checkout -- JS/128KB/src/core_kernel.js` 还原；记录 HEAD 实测字节与池值。
5. 复核本功能的【估算】预算（视图字节数、池增量、压缩增量），与本文封顶值比对，超了先改设计再写码。
6. 实现 → 测试：C 每功能 ≥10 例进 verify.ps1 §4 且含「刚好通过/刚好拒绝」双侧边界；JS 新建测试文件（现有 20 个 test-*.js 一字不改）；双端功能按各功能节的差分方案跑新差分驱动 exit 0。
7. 自测三关：verify.ps1 全量、`node test/run-all.js`、`node tests/bench/diff-smoke128.js` 50/50。
8. 提交信息按 ROADMAP.md:229-238 规范：conventional commits、写清动机/门禁影响（账本是否变化、字节前后值、是否新增视图）/测试口径；期望值修正必须写推导；不夹带规划稿。

---

## 附：v2.5/v2.6 功能 × 端归属速查

| 功能 | 端归属 | 难度 | C .bss | JS 池 | JS 压缩内核 | 契约文件 |
|---|---|---|---|---|---|---|
| F0 治理前置（主笔补充，早合序 0） | 生态基建 | S | 0 | +0 | +512–1 024 B | — |
| F1 复变函数扩展 | 双端 | M | +0 | +0 | +1 536–2 560 B | complex-ext-v25.md |
| F2 方程组 GB 消元全解 | 双端 | M | +0（目标） | +1 024–2 048 B | +2 560–4 096 B | sysolve-format-v25.md |
| F3 Sturm 实根隔离 | C 全功能 + JS ≤4 | M / M–L | +0（视图 2.0–2.8 KB） | +1 024–1 638 B | +3 482–5 120 B | sturm-format-v25.md |
| F4 JS Francis 特征值 | JS | M | 0（C 零触碰） | +~2 816 B | +3 072–5 120 B | tests/eigen-cases.json |
| F5 WASM 前景文档（v3 降级纯文档） | 生态基建 | S | 0 | 0 | 0 | docs/wasm-future.md |
| G1 WASM 全量 + npm + TS + API 文档（v3 顺延 v2.7+） | 生态基建+双端 | M+ | — | — | — | （存档 docs/wasm-future.md） |
| G2 SVD 双端（核心攻坚） | 双端 | M | +0（视图 3752 B） | +1248 B | +3 072–4 096 B | svd-format-v26.md |
| G3 压测口径扩展（主笔补充） | 生态基建 | S–M | 0 | 0 | 0 | — |
| G4' 纯 JS 大整数移植（v3 转正）+ Sturm/sqfree 5..8 收口 | JS | L | 0 | 预算 spike 定数 | 预算 spike 定数 | sturm.json / polynomials.json 修订 |

> 所有【估算】值开工时必须以 HEAD 实测复核并写进 PR 描述；本文预算封顶值即各功能的设计上限，超出即回改设计，不允许「先实现后找预算」。

---

## 附录：v2.6 执行记录（2026-11-04，收尾组成文）

**实际交付（与 §四 规划的出入如实记录）**：

- **G2 SVD——C 侧交付，JS 面降载顺延**（详见下节裁决）：`ac_pro_svd.c` one-sided
  Jacobi（m,n≤12，m<n 内部转置；30 轮 sweep 预算、幂 2 精确缩放、出口双闸门回乘
  验证、错误码 −1/−2/−3 诚实拒绝），svd 视图 **3760 B ≤ 3848 吸收线**（margin 88 B）
  被 union 完全吸收，`.bss` 维持 42608 B 零增长（`bss-check` 实测：42608/42640/39408
  精确命中）。契约 `docs/contracts/svd-format-v26.md` 冻结 + `tests/svd-cases.json`
  52 例 + 语料探针 `svd_runner`（金标准 `tests/bench/svd128-c.csv`，供 v2.7 JS 面对拍）；
  pro_runner 新增 SVD 16 例五组 = 243 checks（总 935 checks, 0 failed），
  verify.ps1 **43 项 verdict 全绿**（新增 SVD 用例数门禁 ≥13）。
- **G3 基准终版交付**：`tests/bench/run-all.sh` 扩为**五列**（新增 pro_runner 列，
  经 ProTEXT Makefile `probench` 目标构建 `pro_bench.exe` 链接 ProTEXT 内核对象；
  四 stage 口径不变，**预热丢首轮口径自此成文**——每 stage 计时前按池顺序预热
  1000 次覆盖全部条目，冷启动成本不计入计时，正式计时 3 轮取中位数，五列一致）；
  五列同机同轮全量复跑，`BENCH-1M.md` 与 `docs/benchmark.md` 出 v2.6 终版，
  `docs/getting-started.md` 对比表改为引用 BENCH 报告（防单点快照漂移）。
  落点结论：Pro 层与上游 C/128KB 同一批上游 TU，四 stage 对比 Pro/C128 =
  0.99/0.91/1.02/1.04，全部落在采样抖动带内——Pro 层对基础四 stage 零可测回归。
  wasm 列随 G1 顺延 v2.7+，本版不设。
- **组件版本**：C/128KB-ProTEXT **v1.7.0 → v1.8.0**；JS/128KB **v2.5.0 不动**
  （本版 JS 零功能变更；全局 Tag v2.6.0 只覆盖 C 侧，双轨制成文）。

**降载裁决（§6.2 R2 触线，实测依据非估算）**：

- **触线事实【实读】**：JS 压缩内核 `JS/128KB/src/core_kernel.js` 实测 **128 846 B**
  （`wc -c`，2026-11-04 收尾复核），已越过熔断线 **126 976 B**；距 131 072 B 文件线
  余量仅 **2 226 B**。v2.5.0 五块 JS 功能落地后预算即告罄，§6.2 R2 的估算区间
  上限（128 687 B）被实测证实并小幅超出。
- 按 §6.2 R2 成文降载序执行：
  - **①G1 loader 瘦身：不适用**——G1 已整体顺延 v2.7+（v3 裁决），本期无新增
    loader 面可瘦身；
  - **②G2 的 JS 面顺延 v2.7，SVD 成文 C-only**（先例：v2.4.0 schur）——C 侧
    `svd_runner` 语料探针与金标准 CSV 已就位，v2.7 JS 面 `core_matrix.js`
    `proSvd*` 按契约 svd-format-v26.md 逐字镜像即可，无需重开契约；
  - **G4' 纯 JS 大整数预算前置判定 no-go，顺延 v2.7**——判定依据 = **实测余量
    不足，非估算**：熔断线纪律要求 G4' 开工前预算 spike 对照 126 976 B 给
    go/no-go，而当前内核实测已在该线之上（余量 2 226 B 只对文件线成立），
    L 级内核增量在触线状态下无可行的 go 判据；spike 定数前不编造预算的纪律
    在此直接收敛为 no-go。**F2 的 sqfree 5..8 联动收口一并顺延**（依赖 G4'），
    JS Sturm 5..8 与 sqfree 5..8 维持契约成文分叉（EDEG），机制与依据不变；
  - **G1 主题维持 v2.7+** 不变。
- **熔断线的后续约束**：在 v2.7 就「JS 内核减负或文件线提升」重新裁决之前，
  JS 侧只允许零内核增量变更；`tests/svd-cases.json` 与 `svd128-c.csv` 是
  v2.7 JS 面的对拍输入，`docs/features.md` 的 JS 缺口清单同步改写为顺延态。
