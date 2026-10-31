# C/128KB-ProTEXT · 组件变更记录

本文件从**组件视角**记录 Pro 层自身的版本递进，格式遵循
[Keep a Changelog](https://keepachangelog.com/)。与根 [CHANGELOG.md](../../CHANGELOG.md)
的口径区分：根记录**全局版本节点**（仓库级 Tag，一次发布可只覆盖部分组件），本文件
记录**组件版本**（见本目录 [VERSION](VERSION)，按 Pro 层自身变更独立递进 semver）。
v1.4.0 之前的 Pro 层沿革（v1.2.0 起手三件套、v1.3.0 方向 A 多变量层、v1.3.5
特征向量与符号积分，以及 v2.1.0 全局版的稀疏线性方程组与任意精度定点小数）见根
CHANGELOG 对应条目，此处不重复。

## v1.5.0 (2026-10-31)

**与全局版本节点的对应关系**：本组件版本对应全局 Tag **v2.3.0**。Pro 层新增
特性按 semver **minor** 递进，组件版本 v1.4.0 → **v1.5.0**（与全局不同号是
双轨制的预期行为）。本版三个新功能由 `feat/c-symbolic` 分支并行交付
（feat/js-pack 承担 JS 侧数论/矩阵/ODE），收尾组 feat/release-v2.3.0 收口。

### Added

- **常系数线性齐次 ODE 通解**（`src/ac_pro_ode.c` 新）：解析 `y''+3y'+2y=0`
  型字符串 → 特征多项式 → 上游 ac_solvers 1..4 次精确根式 → 渲染通解
  （单根 e^{λx}、共轭对 cos/sin、实重根 x^k·e^{λx}、重根×共轭）；非齐次/
  变系数/阶 ≥5 按错误码 −1..−5 从严拒绝。字符串格式冻结于
  [docs/contracts/ode-format-v23.md](../../docs/contracts/ode-format-v23.md)
  （该契约为本组开工第 1 个 commit 先行落库，与 JS 侧互为镜像）。零 static
  对象、渲染缓冲调用方提供，**.bss 零增长**。
- **B2 符号积分增强**（`src/ac_pro_integrate.c`）：积分表 64 → **128 项**
  （tan/sec/csc/cot 族、双曲族、1/(a²±x²)、√(a²±x²) 类，带线性内层替换）；
  分部积分由一层扩为**固定两层**（深度硬编码 2 的展开式实现，无自调用，
  不违反禁递归红线；这是对 README「只做一层」成文契约的显式修订）。
  栈上节点池同步扩容，表全 static const 落 `.rdata`。
- **单变量有理函数工具**（`src/ac_pro_rational.c` 新）：多项式四则 + 欧几里得
  GCD（整系数域，伪除 + primitive 归一，浮点系数拒绝）+ cancel 约分 + apart
  部分分式（分母根 1..4 次精确根式 / 5..8 次 DK 数值根，仅互异单根；分解
  回乘验证残差 ≤ 1e-9，不过即报错绝不输出未验证分解）。新 poly 视图在
  sparse 峰值吸收线内，**.bss 零增长**。

### 测试口径

- pro_runner **396 → 488 checks, 0 failed**（B2 积分 31 → 58 项、rational 32 项、
  ODE 33 项）。
- `.bss` 三门禁实测逐字命中：内核 **42608** / runner **42640** / 上游对照
  39408，退化 38672（2026-10-31 `bss-check` 复测）。
- `verify.ps1` 全量验收 **33 passed, 0 failed**（8 节；feat/c-symbolic 组在本次合并中把第 4 节改为按各功能用例数断言，验收项 31 → 33；2026-10-31 复跑）。
- 跨语言 ODE 契约 [tests/ode-solutions.json](../../tests/ode-solutions.json)
  冻结 70 个双侧一致用例；3 条两侧输出偏差如实上报（见该文件
  knownDeviations，其中 1 条为 JS 侧实现缺陷，2 条为契约文本歧义各执一端）。

## v1.4.0 (2026-10-27)

**与全局版本节点的对应关系**：本组件版本对应全局 Tag **v2.2.0**。两个是不同口径——
全局 v2.2.0 同时覆盖 C、JS 两侧与版本管理双轨制，而 Pro 层新增特性按 semver
**minor** 递进，故组件版本为 v1.3.5 → **v1.4.0**（与全局不同号是双轨制的预期行为）。

### Added

- **2 变量 Groebner 基**（`feat/groebner`）：M4GB 逐对归约，lex 序，deg ≤ 4，
  膨胀熔断。

### Fixed

- **`verify.ps1` 三处修复**（全量验收失败根因定位后的根治）：
  1. 相对 `-OutDir` 在任何 `Push-Location` 之前锚定为绝对路径——消除第 7 节
     `Push-Location $repoRoot` 后 `neg*.o` 输出路径解析到不存在目录、gcc 无法创建
     输出的环境性失败（neg1/neg2 的「非零 = 预期拒绝」判定不再掩盖它）；
  2. 三条负向编译 verdict 携带 gcc 退出码与 stderr 摘要，失败自述原因；
  3. 文件存为 UTF-8 with BOM——BOM-less 时 PowerShell 5.1 在 zh-CN 系统按 CP936
     解码，中文注释行的多字节标点会吞掉后续字节（含换行）破坏解析结构（实测解析
     错误「意外的标记 }」，二分定位到 5 行）；加 BOM 后按 UTF-8 正确解码，与注释
     内容无关。

### 测试口径

- **账本零增长结论不变**：gb 视图 1888 B（≤ 1960 预算线）被 sparse 峰值
  3 848 B 完全吸收；内核口径 **42 608 B** / runner 口径 **42 640 B** /
  上游对照 39 408 B（`bss-check` 实测逐字命中）。
- pro_runner **396 checks, 0 failed**（新增 68 项测试）+ RESULT: PASS。
- verify.ps1 三种调用模式（默认绝对 OutDir / 仓库根相对 OutDir / ProTEXT 目录
  相对 OutDir）全部 **31 passed, 0 failed**；两道门禁复跑 exit 0。
