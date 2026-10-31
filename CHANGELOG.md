# Changelog

本项目的所有重要变更记录于此。格式遵循 [Keep a Changelog](https://keepachangelog.com/)，
版本号遵循 [语义化版本](https://semver.org/)。

## v2.4.0 (2026-10-31)

本全局版本节点同时更新 C、JS 两侧，主题为「符号引擎 + 数值深度」：JS 侧补上
符号微分、符号积分、结式与 Groebner 三个代数面，C 侧吃下一般矩阵特征值，
双侧落地 square-free 分解与重根重数。开发方式：双 Git worktree 并行
（`feat/c-eigen` / `feat/js-symbol`），square-free 输出格式契约
（[docs/contracts/sqfree-format-v24.md](docs/contracts/sqfree-format-v24.md)）
由 C 组开工第 1 个 commit 先行冻结、JS 组按其实现（机制同 v2.3.0 ODE 契约）；
收尾组串行收口。

### Added（C 侧 · C/128KB-ProTEXT · 组件 v1.5.0 → v1.6.0）

- **一般实矩阵全部特征值**（`ac_pro_schur.c` 新）：Householder→Hessenberg→
  Francis 隐式双移 QR（bulge chasing，全显式循环零递归）+ 2×2 块闭式解；
  幂 2 精确缩放（纯指数加减零舍入）防 H² 溢出；单特征值 50 轮预算 +
  EISPACK 同款异常位移，不收敛返 −2 **绝不输出未收敛的垃圾特征值**；可选
  反迭代实特征向量（残差 ≤ 1e-9·(1+‖A‖)，6 组尝试全失败返 −3；复特征值不
  产出向量）。`ac_pro.h:283-284`「非对称返 −2」成文缺口就此关闭。
  schur 视图 2744 B ≤ 3848 吸收线，**.bss 零增长**。13 例 / 46 checks。
- **单变量多项式 square-free 分解与重根重数**（`ac_pro_sqfree.c` 新）：整系数
  循环式 SFF（f 与 f′ 逐层 GCD），重数由分解层指标精确决定、浮点永不参与
  重数合并；输出 (根, 重数) 列表，≤8 次（1..4 精确根式 @e / 5..8 次
  Durand-Kerner @d）。字符串格式冻结于
  [sqfree-format-v24](docs/contracts/sqfree-format-v24.md)（该契约为本组开工
  第 1 个 commit 先行落库，与 JS 侧互为镜像）。复用 poly/rat 槽零新视图，
  **.bss 零增长**。10 例 / 40 checks。

### Added（JS 侧 · JS/128KB · 组件 v2.3.0 → v2.4.0）

- **符号微分引擎**（`core_symboltree.js` 新）：parseExpression 的 RPN token 流
  → 扁平表达式树（512×4 节点池）→ 符号求导（四则/幂三支路 + 17 种单参函数）；
  多项式子树接 ratRootDescent 输出因式列表（factor 语义，镜像
  ac_sym_factor_poly）；隐函数 dy/dx 对齐 C 侧 A6（错误码逐值复刻）。
  116 项测试，池 35232 B。
- **符号积分**（`core_integrate.js` 新）：积分表 128 项与 C 侧逐项对齐
  （anti 模板串 + 偏移表加载期解析，加载期断言恰 128 项）+ 固定两层分部
  （无自调用，不违反禁递归红线）；错误码复刻 AC_PRO_INT_\*。75 项测试，
  池 93676 B。
- **square-free (根, 重数)**（`core_ratroot.js` 扩展 ApolloSqfree 段）：按契约
  逐字实现；JS 整题 ≥5 → EDEG（契约 §5.1 成文分叉，唯一双侧行为差异）。
  70 项测试，池 2240 B。
- **结式 + 2 变量 Groebner 基**（`core_groebner.js` 新）：结式 = Sylvester
  矩阵（阶 ≤12）复用 v2.3.0 矩阵 pro 层 proDet；Groebner = M4GB 逐对归约
  1:1 移植（lex 序、输入 deg ≤4、基 8 槽/16 项/pair 32 熔断、Buchberger
  第一判据、interreduce 不动点），错误码 = AC_PRO_GB_\* 全序。60 项测试，
  池 6648 B。

### Added（跨语言契约与文档）

- **`tests/polynomials.json` sqfree 区（新）**：square-free (根, 重数) 跨语言
  契约用例——**70 例双侧实跑核对零分歧冻结**（契约 §10-1 两层结构：双侧区
  64 例〔成功 31 期望串逐字 + 错误 33 按契约 §7 数值精确比较〕+ C 侧区 6 例
  〔整题 5..8 次共 5 例仅 C 断言期望串、JS 断言 −2；EOVER 缓冲语义 1 例
  cOnly，JS 消费方跳过〕）。**无 knownDeviations**（两侧输出不一致即实现 bug
  的口径下，本版零上报）；用例以两侧内嵌套件为准提炼，收尾组未发明新用例。
- **docs/features.md 增补**：v2.4.0 全部新功能与实测数字刷新（含「如实口径」
  节，见下）。

### Changed（版本管理 · 双轨递进）

- `C/128KB-ProTEXT/VERSION` v1.5.0 → **v1.6.0**（Pro 层新特性按 semver minor
  递进）；`JS/128KB/VERSION` v2.3.0 → **v2.4.0**；全局 Tag **v2.4.0**。
- JS 压缩内核重建：`core_kernel.js` **49747 → 103325 B**（预算 131072 B，
  余量 27747 B）；全项目 JS 静态池 30112 → **167908 B**（符号微分 35232 +
  符号积分 93676 + 结式/Groebner 6648 + sqfree 2240，实测各模块加载期
  TypedArray byteLength 之和）。

### Known issues（如实口径 · 收尾对拍与 JS 组交付说明转写）

- **JS 静态池水位**：167908 B 已超 ROADMAP §五 口径的 98304 B 水位门禁；
  门禁函数 `checkMemoryWatermark`（core_solvers.js:622-644）只累计基础内核池、
  不含模块池，故测试仍全绿。**后续任何 JS 功能落位前应重估水位**。
- **Groebner 系数域收窄（不称「对等」）**：JS 无 int64，系数域为 Float64
  安全整数，\|中间量\| > 2^53−1 即 EOVER（−6）熔断（实现文件头与测试文件头
  成文）；经典用例幅值远低于熔断线，但输入幅值 >1e6 量级的中间量两侧能力
  不对等。
- **结式为数值路径**（C 同款）：det 条件数敏感，主元阈值 1e-12（proDet 契约）；
  **结式为 0 是公共根判据而非错误**——与 C 逐值同语义。词法无总次数上限
  （与 C parse_into 同域），每变量指数 ≤15。
- **sqfree 求解层副作用**：逐层求根复用求解层 `_coeffs`/`_root*`（C 侧
  ac_sym_factor_poly CLOBBERS g_coeffs 同款副作用，两头已成文）；调用方若
  持有求解层会话语据须重算。
- **符号积分已知超集偏差 3 条**（JS 复用内核解析器，能力超集；对拍用例避开，
  实现文件头成文）：空白分隔数字按内核隐式乘法接受（C EPARSE）、e^(完整
  表达式) 按标准优先级读（C 的 e^ 只吃一个 pow 层）、x^2^3 右结合读成
  (x^2)^3（C 文法止于单指数）。
- v2.3.0 遗留 3 条 ODE 已知偏差（1 条 JS 渲染缺陷 + 2 条契约文本歧义各执
  一端）仍待双组裁决，见 tests/ode-solutions.json `knownDeviations`。
- `verify.ps1` 头注 banner 仍写「ProTEXT v1.4.0 verification」——该文件归
  C 组（feat/c-eigen）独占，收尾组只跑不改，如实记录。

### 测试口径

- C 侧：`verify.ps1` **35 passed, 0 failed**（v2.3.0 基线 33 + SCHUR/SQFREE
  两行）；pro_runner **574 checks, 0 failed**（488 + schur 46 + sqfree 40）；
  `.bss` 三门禁 42608 / 42640 / 39408 逐字命中，degraded 38672（零增长）。
- JS 侧：20 测试文件 **TOTAL: 1183 pass, 0 fail**（862 + 符号微分 116 +
  符号积分 75 + sqfree 70 + 结式/Groebner 60）。
- 跨语言：`node tests/bench/diff-smoke128.js` **50/50 用例一致**（exit 0）；
  polynomials.json sqfree 区 70 例双侧实跑核对零分歧（见上）。

## v2.3.0 (2026-10-31)

本全局版本节点同时更新 C、JS 两侧，主题为「补齐短板 + 低风险纵深」：JS 侧补上
数论、矩阵 12×12 两个最刺眼的不对称，双侧落地常系数线性 ODE，C 侧做积分与
有理函数两个高复用数学面。开发方式：双 Git worktree 并行
（`feat/c-symbolic` / `feat/js-pack`），ODE 字符串格式契约
（[docs/contracts/ode-format-v23.md](docs/contracts/ode-format-v23.md)）由 C 组
开工第 1 个 commit 先行冻结、JS 组按其实现；收尾组 feat/release-v2.3.0 串行
收口（对标调研落库、跨语言契约文件、版本双轨递进）。

### Added（C 侧 · C/128KB-ProTEXT · 组件 v1.4.0 → v1.5.0）

- **常系数线性齐次 ODE 通解**（`ac_pro_ode.c` 新）：`y''+3y'+2y=0` 型字符串进、
  通解字符串出；特征方程走 ac_solvers 1..4 次精确根式，单根 e^{λx}、共轭对
  cos/sin、实重根 x^k·e^{λx}、重根×共轭四类渲染模板；非齐次/变系数/阶 ≥5
  按错误码 −1..−5 从严拒绝，绝不硬解。零 static 对象，**.bss 零增长**。
- **B2 符号积分增强**（`ac_pro_integrate.c`）：积分表 64 → **128 项**（tan/sec/
  csc/cot 族、双曲族、1/(a²±x²)、√(a²±x²) 类，带线性内层替换）；分部积分
  一层 → **固定两层**（深度硬编码的展开式实现，无自调用，不违反禁递归红线；
  显式修订 README「只做一层」契约）。测试 31 → 58 项。
- **单变量有理函数工具**（`ac_pro_rational.c` 新）：欧几里得 GCD（整系数域，
  伪除 + primitive 归一）、cancel 约分、apart 部分分式（1..4 次精确根 /
  5..8 次 DK 数值根，仅互异单根；回乘验证残差 ≤ 1e-9 才输出）。32 项测试。

### Added（JS 侧 · JS/128KB · 组件 v2.2.0 → v2.3.0）

- **数论模块**（`core_numtheory.js` 新）：gcd/lcm、6k±1 试除 + Miller-Rabin
  （底数集与 C 侧逐字一致）、质因数分解、nCr/nPr；安全整数域 ≤ 2^53−1。
  89 项测试，池 288 B。
- **矩阵 12×12 pro 层**（`core_matrix.js` 升级）：部分主元 det/inv/rank、
  LU(P·A=L·U+piv，对齐 C 侧 A4)、幂法 + Hotelling deflation 实对称全特征对、
  QR；≤3×3 闭式寄存器层红线行为逐字节保留。82 项测试，pro 池 5944 B。
- **ODE 通解 JS 侧**（`core_ode.js` 新）：与 C 侧逐字镜像（同解析文法、同排序
  聚类、同 fmt 三分支、同渲染模板、同错误码数值）。72 项测试，池 1464 B。
- **guardMultivariateNonlinear 接入求解管线**：≥2 变量非线性多元组在
  solveQuadratic/Cubic/Quartic 入口拒绝（v2.0.0 起的已知死代码，唯一可行
  方向为接入）；新增 test-guard.js 16 例，测试文件 12 → 16。

### Added（跨语言契约与文档）

- **`tests/ode-solutions.json`（新）**：ODE 通解跨语言契约——**70 个双侧实跑
  逐字节一致的用例冻结**（契约 §5.4 全部 11 行、§6 错误码全序、§7 拒绝情形、
  §9-4 边界值，含 √2/√3/∛2 无理根 fmt 12 位路径）；**3 个两侧输出不一致的
  输入逐条记录于 `knownDeviations`**（见下方 Known issues），不冻结进 cases。
- **三份对标调研落库**：[docs/refs/benchmark-sympy.md](docs/refs/benchmark-sympy.md) /
  [benchmark-giac.md](docs/refs/benchmark-giac.md) /
  [benchmark-eigenmath.md](docs/refs/benchmark-eigenmath.md)（SymPy / Giac(Xcas) /
  Eigenmath，ROADMAP §一 的可回查底稿）。
- **docs/features.md 重写**：补齐 v2.3.0 全部新功能；修复停留在 v2.0.0 的滞后
  数字（「281 项测试 / .bss 41456 B」→ 实测 488 checks / 42608 B）。

### Changed（版本管理 · 双轨递进）

- `C/128KB-ProTEXT/VERSION` v1.4.0 → **v1.5.0**（Pro 层新特性按 semver minor
  递进）；`JS/128KB/VERSION` v2.2.0 → **v2.3.0**；全局 Tag **v2.3.0**。
- JS 压缩内核重建：`core_kernel.js` **34510 → 49747 B**（预算 131072 B）；
  全项目 JS 静态池 22416 → **30112 B**（≈ 29.4 KiB，水位门禁 98304 B 内）。

### Known issues（收尾对拍发现，如实上报）

- **JS 侧 ODE 小正值实根渲染缺陷**：实特征根 0 < |λ| < 0.5 且 llround 到 0 时
  （如 1/3、1/999），`core_ode.js` `_isZeroInt` 混返 boolean/数字、实根渲染门
  `=== 0` 比较失配，exp 因子被误省略、根被当作 λ=0 渲染（`999y''+998y'-y=0`
  C 侧正确、JS 侧输出裸 `C₁`）。负小根与共轭对分支不受影响，两侧内嵌测试均未
  暴露。详见 tests/ode-solutions.json `knownDeviations[0]`。
- **两条错误码分歧（契约文本歧义各执一端）**：`x^2*y=0` C −3 / JS −1
  （契约 §6 表示例与 §3.3-3/§6 全序自相矛盾）；`y''x=0` C −1 / JS −3
  （契约 §3.2「任意位置」与 §3.3-1「导数记号后」冲突）。须双组确认后统一，
  详见 tests/ode-solutions.json `knownDeviations[1]/[2]`。
- 契约文档自洽化：ode-format-v23.md §5.1-3 的 `fmt(-0.5)="-0.5"` 示例与
  §5.1-2 规则及 §5.4 第 11 行矛盾，两侧实现均已按 p/q 形态交付，示例改正为
  `"-1/2"`（无行为变更）。

### 测试口径

- C 侧：`verify.ps1` **33 passed, 0 failed**（8 节验收，本版第 4 节改按各功能用例数断言）；pro_runner **488 checks, 0 failed**
  （396 + B2 27 + rational 32 + ODE 33）；`.bss` 三门禁 42608 / 42640 / 39408
  逐字命中，degraded 38672（本版零增长）。
- JS 侧：16 测试文件 **TOTAL: 862 pass, 0 fail**。
- 跨语言：`node tests/bench/diff-smoke128.js` **50/50 用例一致**（exit 0）；
  ODE 契约 70 用例双侧冻结（见上）。

## v2.2.0 (2026-10-27)

本全局版本节点同时更新 C、JS 两侧，并落地「全局 Tag + 组件独立 VERSION」双轨制版本
管理。开发方式：双 Git worktree 并行（`feat/groebner` / `feat/js-minify`）。
自本版起口径分层：根 README / 根 CHANGELOG 记**全局版本节点**（仓库级 Tag，一次发布
可只覆盖部分组件）；各组件目录以独立 `VERSION` + `CHANGELOG.md` 记**组件版本**
（按该组件自身变更递进 semver，C 侧 Pro 层新特性按 minor 递进）。

### Added（C 侧 · C/128KB-ProTEXT · 组件 v1.3.5 → v1.4.0）

- **2 变量 Groebner 基**：M4GB 逐对归约、lex 序、deg ≤ 4、膨胀熔断。gb 视图
  1888 B（≤ 1960 预算线）被 sparse 峰值 3848 B 完全吸收，**`.bss` 零增长**——
  账本闭合：内核口径 **42608 B** / runner 口径 **42640 B** / 上游对照 39408 B
  （`bss-check` 实测逐字命中）。新增 68 项测试，pro_runner **396 checks, 0 failed**
  （RESULT: PASS）。

### Fixed（C 侧 · verify.ps1 全量验收脚本）

- **相对 `-OutDir` 在任何 `Push-Location` 之前锚定为绝对路径**：修复第 7 节
  `Push-Location $repoRoot` 后 `neg*.o` 输出路径解析到不存在目录、gcc 无法创建
  输出而失败的环境性问题——该失败此前被 neg1/neg2「非零 = 预期拒绝」判定掩盖
  成通过。
- **三条负向编译 verdict 携带 gcc 退出码与 stderr 摘要**：失败自述原因，不再静默。
- **文件存为 UTF-8 with BOM**：BOM-less 时 PowerShell 5.1 在 zh-CN 系统按 CP936
  解码，新增中文注释行的多字节标点会吞掉后续字节（含换行）破坏解析结构（实测
  解析错误「意外的标记 }」，二分定位到 5 行）；加 BOM 后按 UTF-8 正确解码，与
  注释内容无关——根治存量隐患。
- 复验：三种调用模式（默认绝对 OutDir / 仓库根相对 OutDir / ProTEXT 目录相对
  OutDir）verify.ps1 全部 **31 passed, 0 failed**；两道门禁复跑 exit 0。

### Added（JS 侧 · JS/128KB · 组件 v2.2.0，首个组件版本记录）

- **深度压缩管线**并入 `test/build-kernel.js --minify` 唯一重建入口：
  `core_kernel.js` **175142 → 57523 B**（压缩率 **67.2%**，越过 > 50% 目标，
  低于 ~87.6 KB 冲刺线；预算 131072 B）。
- **terser 两段式压缩**（实测依据：terser 以 `module: true` 解析整文件时顶层符号
  按 ESM 语义非 global，即使 `mangle.toplevel: false` 也会改写顶层名——实测
  36747 B 但压缩体 eval 回归无法按名运行）：(a) 内核体按经典 script 压缩且
  `mangle.toplevel: false`，顶层名全保留；(b) EXPORT_BLOCK 单独按 module 压缩
  （其自由引用为 undeclared 符号不会改写，`mangle.properties` 默认关闭 → 对象
  属性名原样）。两段拼接后 `export default` 契约不变，压缩体可做全量 eval 回归。
- **回退路径**：terser require 失败或压缩异常时自动改用内置词法级剥离器（同入口、
  同全量校验）：单遍逐态扫描字符串/模板串（状态栈处理 `${}` 插值内回到代码态）/
  正则字面量（经典 prev-token 启发式：return/typeof/new 等关键字后允许，标识符/
  数字/字符串/)/]/++/-- 后视为除号）/注释边界，绝不盲目正则去注释；token 文本
  逐字零改写、原换行位置一律保留换行符（ASI/restricted production 零改动）、
  行内空白仅在相邻 token 可能粘连成词或合字成运算符（++/--/=>/<=）时保留一个
  空格、未闭合一律 throw fail-fast；已通过 16 项词法边界单测。回退实测
  175142 → 79435 B（**54.6%**），同样达标——无 terser/无网络环境用同一命令
  仍可复现（已做 node_modules 移走演练验证）。

### Added（版本管理 · 双轨制落地）

- **全局 Tag + 组件独立 VERSION 双轨制**：`C/128KB-ProTEXT/VERSION` v1.3.5 →
  **v1.4.0**（Pro 层新增特性按 semver minor 递进）；`JS/128KB/VERSION` 新建为
  **v2.2.0**（JS 侧首个组件版本记录，与本次全局版本节点对齐）。
- **组件细分 CHANGELOG**：新建 `C/128KB-ProTEXT/CHANGELOG.md` 与
  `JS/128KB/CHANGELOG.md`，自组件视角记录；C 侧写明组件 v1.4.0 与全局 v2.2.0
  的对应关系。
- **根 README 版本对照表改造为双轨制**：新增「全局版本节点 (Global Tag)」与
  「组件版本 (Component Version)」两列，一眼分清全局 Tag 与组件版本是两个口径。

### 测试口径

- C 侧：verify.ps1 三种调用模式 **31 passed, 0 failed**；`bss-check` 三门禁
  42640 / 42608 / 39408 逐字命中；pro_runner **396 checks, 0 failed**（基线 328
  + Groebner 68）。
- JS 侧：12 测试文件 **TOTAL: 603 pass, 0 fail**；重建命令
  `node test/build-kernel.js --minify`（JS/128KB 目录下）实跑输出末行
  **BUILD OK, all tests passed**，57523 B / 预算 131072 B。

## v2.1.0 (2026-10-27)

C 侧 Pro 层按设计说明的高价值项（④⑤）落地两大能力。
开发方式：双 Git worktree 并行（`feat/sparse-linsolve` / `feat/precision-decimal`），
骨架由幂等生成器 `tools/scaffold_v2_1_0.py` 产出，多代理工作流自动执行分支门禁、
合并与全量回归；合并冲突面收敛在 Makefile / ac_pro.h / ac_pro_scratch.h /
test_pro_runner.c / verify.ps1 五文件并集解决。上游 `C/128KB/` 逐字节未动，
JS 侧零改动。

### Added（C 侧 · C/128KB-ProTEXT）

- **稀疏线性方程组**（`src/ac_pro_sparse.c`）：`ac_pro_sparse_solve(n, nnz, tri_r,
  tri_c, tri_v, rhs)` 接受**三元组**输入（顺序任意、重复坐标求和合并、精确 0 丢弃），
  内部完成 CSR 压缩（计数排序 + 行内排序 + 同列合并）、阈值主元消元（τ = 0.1，
  候选中取行非零元最少者，主元 ≥ max(τ·pmax, 1e-12)）与回代；32 格稠密散射行
  迭代更新，无递归/无 malloc/无 VLA。能力边界 n ≤ 32、nnz ≤ 320，**填充与输入
  共享 320 总容量，绝不静默丢填充**。错误码走返回值（零静态存储通道——任何
  static 对象都会让 TU 净增 32 B `.bss`，直接破坏门禁）：OK 1 / EN −1 / ENNZ −2 /
  EPIVOT −3 / ESING −4 / EFILL −5 / EINDEX −6；结果经 `ac_pro_sparse_x/rhs/perm`
  访问器读取。sparse 视图 3848 B 成为 Pro union 新峰值。12 用例 35 项测试
  （[case SP-1..12]，期望值 node 离线核算后以常量写入，随机稠密对照组与 C 逐位一致）。
- **任意精度定点十进制**（`src/ac_pro_bigdec.c`）：value = m·10^(−scale)，m 为上游
  `ac_bigint_t`（基数 1e9、20 limb、180 位十进制容量），标度 int32 ±2^31。
  窄接口 `ac_pro_dec_parse` / `ac_pro_dec_render`（5 槽）+ 宽接口
  `ac_pro_dec_add/sub/mul/div`（字符串出入、规范形输出）。加减带 NINES(10^180−1)
  进位预检——上游 `bi_mag_add` 收尾进位是静默截弃契约，|ta|+|tb| == 10^180 的
  精确特例落 (1, s−180)；除法走 divmod 余数扩标长除（R ≠ 0 返 INEXACT 截断近似，
  Q = 0 且 R ≠ 0 返 EOVER 熔断）；解析超 180 有效位（EDIGITS）与乘积尾数超容量
  （EOVER）均熔断——**绝不返回错值**。只调用上游公开 `ac_bi_*` 纯函数 API，
  不触碰 `g_bi_*` 草稿池。dec 视图 5 × 96 B = 480 B 完全吸收进 Pro union，
  **`.bss` 零增长**。12 项测试（[case BD-1..12]，期望值 node BigInt 离线核算；
  开发中自抓并修复解析 span 公式与 limb 分块右对齐两处缺陷，均被用例钉住）。
- **骨架生成器**（`tools/scaffold_v2_1_0.py`）：幂等 Python 生成器，产出两特性
  的 TU/测试骨架与 Makefile 接线（真实 API 落地后骨架探针按既定计划整体取代）。

### 测试口径

- C 侧：`pro_runner.exe` **328 checks, 0 failed**（基线 281 + 稀疏 35 + 定点小数 12）；
  `bss-check` 三门禁精确命中（kernel **42608** / runner **42640** / upstream 39408），
  账本 38672 + 80 + 3848（sparse 峰值）+ 8 = 42608（研究估算 42684，实际更省）；
  退化对拍 38672 B 不变；`verify.ps1` 全量验收通过（v2.1.0 banner，含 SP/BD 覆盖段）。
- JS 侧：12 个测试文件 **603 pass, 0 fail** 不变（本迭代纯 C 侧）；
  `build-kernel.js` 83 项回归 + 78 标识符扫描 + 43 项 ESM 冒烟复核通过。

## v2.0.0 (2026-10-26)

双侧同步的多变量引擎升级：C 侧 Pro 层三项能力以零 `.bss` 增长落地，
JS 侧多变量模块转正并补齐矩阵/特殊函数。上游 `C/128KB/` 逐字节未动。

### Added（C 侧 · C/128KB-ProTEXT）

- **多变量符号求导**（`src/ac_pro_diff_multi.c`，211 行）：`ac_pro_diff_multi(src, var_index, out, out_len)`
  对 mv 项表逐项幂法则求导（d/dx、d/dy），复用 Pro union mv 视图的槽 0/1，
  溢出走 `ac_safe_mul` + `g_solve_error = 5`。12 条测试。**`.bss` 零增长**。
- **多变量多项式积分**（`src/ac_pro_integrate_multi.c`，257 行）：
  `ac_pro_integrate_multi` 逐项 ∫c·x^a·y^b dx，**精确整除约束**（c % (指数+1) ≠ 0
  拒绝，专用错误码 `AC_PRO_MVI_ENONEXACT (-6)` 直接走返回值——实测 static 错误槽
  会净涨 32 B `.bss`）；结果总次数 ≤ 6 门禁。10 条测试。**零 `.bss` 增长**，
  任务卡预留的 frac 视图（480 B）都未动用。
- **sin/cos LUT 常量表化**（`src/ac_pro_math.c` 包装重写 + `tools/gen_lut_tables.js`）：
  1024 项 `const double` 预计算表经 `<math.h>` 先行 + 函数式宏劫持进上游 TU，
  `#undef AC_MATH_USE_LUT` 阻断旧可变表。`.bss` 迁出收益 **−16416 B**（对照组
  实测 57904 → 41456），插值精度与运行时填充逐位一致（max |err| = 4.706e-6 ≈ h²/8）。
  `AC_PRO_MATH_LUT_CONST` 开关默认开，=0 回退旧行为。

### Added（JS 侧 · JS/128KB）

- **`core_multivar.js` 转正 + 多变量符号求导**：多变量多项式模块（v1.3.5 期间
  本地开发，175 条测试）正式入库；新增 `diff(a, varIndex, dst)`——与 C 侧
  `ac_pro_diff_multi` 同逐项幂法则、同 exps 编码（高 4 bit = x，低 4 bit = y），
  dst 可与 a 同槽。194 条测试。导出包装重构为三通道（require / 浏览器 `<script>`
  顶层 var / 内核 ESM）。
- **`core_matrix.js`**（新模块）：乘法 / 转置 / ≤3×3 行列式（Sarrus 闭式）/
  逆矩阵（伴随矩阵法，|det| ≤ 1e-12 拒绝），A/B/R 三寄存器 Float64Array
  （216 B），UI 契约与复数域 setComplexA → cXxx → getComplexTmp* 同构。
  39 条测试。
- **`core_special.js`**（新模块）：`erf`（A&S 26.2.17，|ε| ≤ 1.5e-7，实测节点
  最大 1.385e-7）、`gamma` / `lgamma`（Lanczos g=7 手工展开，与 C 侧 ac_special
  同表同条款，γ(7.5) 相对误差 4e-15）。**纯标量零分配**（模块内无数组字面量）。
  33 条测试。
- **内核 12 文件拼接**（`test/build-kernel.js`）：三模块进 FILES 与导出块
  （`multivar` / `matrix` / `special` 命名空间），静态扫描 78 标识符，ESM 严格
  模式冒烟扩到 43 项。`core_kernel.js` 175142 B（越过 128 KiB 文件线为 v2.0.0
  有意裁决，见 docs/js-128kb.md 内存账本节）。
- **`test/test-browser.js`**：vm 沙箱模拟浏览器经典脚本环境（无 require/module），
  验证三模块第四加载通道 + 功能探针，10 条测试。
- **设计说明**：极限压缩与功能边界
  调研（Q1 压缩空间 / Q2 功能可行性 / Q3 外部参考），v2.0.0 功能选型的依据。

### 测试口径

- C 侧：`pro_runner.exe` **281 checks, 0 failed**（基线 259 + 求导 12 + 积分 10），
  `bss-check` 三门禁精确命中（41456 / 41488 / 39408），退化对拍 38672 B。
- JS 侧：12 个测试文件 **603 pass, 0 fail**（含一致性 59 pass + 4 improve）；
  `--expose-gc` 三模块联合探针 20,000 次混合调用 allocCount 恒 6/3/0，
  堆增长 0.17 MB（JIT 噪声量级）。
- Node 与浏览器（模拟经典脚本 + ESM import 双通道）全过。

## v1.3.5 (2026-10-25)

Pro 层两个新方向落地：A5 实特征向量 + Hotelling 收缩 deflation、B2 符号积分。
`C/128KB-ProTEXT` 的 `.bss` **仍是 41456 B，零增长**；上游 `C/128KB/` 的既有文件
逐字节未动。本版是**纯 C99 Pro 升级**，不含任何 JS 代码变更。

### Added

- **A5 实特征向量 + Hotelling 收缩 deflation**（`src/ac_pro_eigen.c`，381 行）：
  `ac_pro_eig_all` 求实对称矩阵 n×n（n ≤ 12）的全部特征对，特征值按 |λ| 降序，
  特征向量列主序存放。三条路径互相印证：带 Rayleigh 商的幂迭代求主特征对、
  解 `(A − λI)v = 0` 零空间作为第二条独立路径、Hotelling 收缩 `A ← A − λ₁x₁x₁ᵀ`
  逐轮 deflate。76 条测试。
- **B2 符号积分**（`src/ac_pro_integrate.c`，1485 行）：`ac_pro_integrate` 支持
  多项式、基本三角函数、指数与对数，64 项标准积分表（`static const`，落 `.rdata`）
  + 一层分部积分 `∫u dv = u·v − ∫v du`。31 条测试。
- **`eig` scratch 视图**（`src/ac_pro_scratch.h`）：2592 B = V 1152 + A 副本 1152
  + λ 96 + work 192。被 `mv` 峰值（2688 B）吸收，union 尺寸不变。

### Changed

- **`ac_pro.h` 新增 A5/B2 两个 API 块**（+89 行）：A5 的 4 个入口与 B2 的 1 个入口
  加 5 个错误码。A5 的错误码走返回值而非 `ac_pro_eig_error()` getter —— 实测一个
  `static int` 会让该 TU 产生 16 B `.bss` 段、叠加链接对齐后全程序净涨 32 B
  （41456 → 41488），直接破门禁。
- **`Makefile` 接线 2 个新 TU**：`PRO_EXTRA` += `ac_pro_eigen.c` `ac_pro_integrate.c`，
  `TEST_SRC` += 对应两个测试文件。
- **`test/test_pro_runner.c`**：校验从 152 条增至 **259 条**，新增 11 个测试入口
  （A5 五项、B2 六项）。
- **A5 收敛判据由 |Δλ| 改为盯残差**：Rayleigh 商是二阶精确的，`|Δλ|` 会先于角度
  误差塌到 1e-16 而残差仍在 1e-7，用 |Δλ| 判停会提前 2~3 个数量级收工 ——
  实测 3×3 残差卡在 4.6e-8，过不了 1e-9 闸门。改为盯残差下降停滞
  （`res > 0.9·res_prev`）后落到 3.8e-13 ~ 4.7e-13。

### Fixed

- **B2 首版四个解析缺陷**：`e^x`、`x*e^x`、`2x+3`、`x^3+2x^2-5x+1` 全部误报
  EPARSE。独立探针发现后修复并复验。

### Performance

- **静态内存**：`.bss` 41456 B **未变**（v1.3 → v1.3.5 零增长）。`eig` 视图 2592 B
  比 `mv` 峰值 2688 B 小 96 B，被完全吸收。
- **代码体积**：新增两个 TU 的 `size` 总计 5340（A5）+ 17492（B2）= 22832 B。
  拆开看，A5 是 `.text` 4912 + `.rdata` 160 + 异常表 188，B2 是 `.text` 13776 +
  `.rdata` 3072（64 项积分表）+ 异常表 564。单 TU `bss` 与 `data` 均为 0。
- **运行性能（100 万次，同机）**：

  | 路径 | 迭代 | 单次耗时 |
  |:---|---:|---:|
  | `ac_pro_eig_all` 2×2 `[[2,1],[1,2]]` | 1,000,000 | **0.073 µs** |
  | `ac_pro_eig_all` 3×3 `[[4,1,2],[1,3,0],[2,0,5]]` | 1,000,000 | **1.593 µs** |
  | `ac_pro_integrate("x^2")` | 1,000,000 | **0.343 µs** |
  | `ac_pro_integrate("x*e^x")` | 100,000 | **1.000 µs** |

  上表是典型用例，不是上界。换矩阵后单次耗时差一个数量级 —— 幂迭代收敛
  速度取决于特征值比值，与框架开销无关：2×2 单位阵 0.070 µs、
  2×2 Hilbert-2 0.230 µs、**最坏 2×2 `diag(2,3)` 1.070 µs**（比值 2/3 需
  ~30 轮）、3×3 Hilbert-3 0.530 µs、**最坏 4×4 2.780 µs**（`r ≈ 0.84`，
  这是 `PRO_EIG_MAX_ITER = 400` 的推导依据）。常见 2×2/3×3 输入全部
  低于 2 µs/op，最坏 1~3 µs/op，未观察到数量级退化。

### Verification

- C 全量：**259 checks / 0 failed，RESULT: PASS**；`-Werror` 零警告。
- `.bss` 三层账本全部命中：kernel **41456** / runner **41488** / upstream **39408**。
- 退化构建（`AC_PRO_DEGRADED=1`）：**38672 B**，两个新 TU 编译成空对象、零导出符号。
- 上游回归：16 套件 / 1171 项测试全绿，`git status -- C/128KB` 为空。
- `nm pro_runner.exe`：`ac_pro_eig_all` 与 `ac_pro_integrate` 各只有一个 `T` 符号。

### Known issues

- **`x^-1` 返回 EPARSE**：`1/x` 正常返回 `ln(x)`，但负指数写法不支持。补上它需要
  在解析层改写为除法并牵动节点预算，收益不足以抵风险。
- **`sin(cos(x))` 返回 ETABLE(−2)**：嵌套超越函数无初等原函数，表匹配失败后
  直接拒绝，是正确行为而非缺陷。
- **A5 只接受实对称矩阵**：Hotelling 收缩在非对称矩阵上不保谱，非对称输入返回 −2。
- **B2 只做一层分部积分**：`∫x²·eˣ dx` 这类需要两层的会被 ETABLE 拒绝；递归展开
  会突破 96 节点额外预算。

### Deferred

- **JS/128KB 的功能扩展推迟到 v2.0.0**：多变量多项式层（`core_multivar.js`）、
  高阶三角函数 `asec/acsc/acot/sech/csch/coth`、复数域 `cSqrt/cExp/cLn/cPowInt/cAsinh/cAcosh/cAtanh/cAbs`
  均已实现并通过 175 条测试，但**不随本版发布**，与 JS 侧其它跨端改动一并在
  v2.0.0 推出。`JS/128KB/` 在本版保持 v1.1.0 状态，逐字节未动。

## v1.3.0 (2026-10-24)

`C/128KB-ProTEXT`（Pro 层）**首次入库**：在冻结的 16 模块上游内核之上落地方向 A
多变量层——A1 多变量 GCD、A2 有理根因式分解、A3 Sylvester 结式。全部状态跑在既有
Pro scratch union 上；union 峰值视图由 `lu`（2352 B）移到 `mv`（2688 B），
`.bss` 41104 → **41456 B**；上游 `C/128KB/` 既有文件逐字节未动。
本提交同时把 v1.2 的全部成果（门禁修复 + 起手三件套，见下节）一并带入 git。

### Added

- **A1 多变量 GCD** `ac_pro_multi_var_gcd`（`src/ac_pro_multi.c`）：主变量上的
  subresultant PRS + 单项式因子兜底 + 对原始输入的精确除法回验。
- **A2 有理根因式分解** `ac_pro_multi_var_factor`：整数 content 提取 + 有理根降次，
  每个候选因子展开 `c * prod(den*x - num) * remainder` 并要求被 f 精确整除来复验。
- **A3 Sylvester 结式** `ac_pro_resultant / _build / _sylvester`：x0 处 Horner 提取
  y 系数、Sylvester 矩阵组装、经既有矩阵核求行列式；`df + dg > 12` 拒绝而非溢出。
- **`mv` scratch 视图**：2688 B = 6 槽 × 28 项 × 16 B。`AC_PRO_MV_TERMS = 28` 不是 8：
  总次数 ≤ 6 的二元多项式有 C(8,2) = 28 个单项式，把验证阶段钳到 8 项会误判合法
  输入（实测 9 项 GCD 被误拒 rc = −1）。
- 整个 `C/128KB-ProTEXT/` 目录（16 个上游模块 Pro 接线 + ext 层 + Makefile 的
  bss-check / degraded-check + verify.ps1）、设计说明、
  `tools/ref_prs.py`（PRS 参考实现）首次入库。

### Fixed

开发过程中发现并修复的七个缺陷（记录于设计说明）：

1. **项解析器系数初值为 1 而非 0**：`mv_parse_term` 以 `c = 1` 起步、仅见到数字才
   覆写，每个字面整数都被前缀一个 1（"2" → 12、"0" → 10）；毒化 A2 的 content
   提取（`2*x^2-8` 被解析成 `122*x^2-18`）。初值改 0，隐式系数由后续
   `if (!saw_coef) c = 1` 补上。
2. **指数上界被当成语法错误**：`x^7 + y` 误报 EPARSE（`g_solve_error = 2`）而非
   语义拒绝（= 1）；上界改为字段宽度（`EXP_MASK`），由 GCD 入口的度预检负责拒绝。
3. **零操作数不可达**：`mv_pick_main_var` 在两输入无共享变量时返回 2，调用方把它当
   "互素，答案 1"；缺陷 1 修复后 "0" 解析为空项表同样命中该分支，`gcd(f, 0)` 答 1
   而非 f。零操作数测试移到共享变量分派之前。
4. **`mv_selfcheck_factor` 拿精确除法助手做整除测试**：`mv_div_exact` 返回的是商的
   项数而非状态码，之前"通过"纯属巧合；改为显式比对"重建乘积等于 f"。
5. **验证阶段栈垃圾**：`mv_validate_gcd` 的四个局部 `mv_sp_t` 只被部分写入（写
   `[0, n)`），属潜伏问题；全部先 memset，且项数先做边界检查再写入。
6. **两条 A1 测试期望值数学上就是错的**（实现本来就对）：
   `gcd(x²−y², x−y) = x−y` 而非 `x+y`；`gcd(x³+y³, x²−xy+y²) = x²−xy+y²` 而非 `x+y`。
7. **测试 runner 里 v1.2 的账本常量过期**：`sizeof(ac_pro_scratch_t)` 2352 → 2688、
   `sizeof(g_ac_pro_storage)` 2432 → 2784、足迹 41104 → 41456 等随 union 峰值
   迁移同步更新。

### Performance

- **静态内存**：`.bss` 内核口径 41104 → **41456 B**（+352 B 含对齐垫：union 峰值
  视图 lu 2352 → mv 2688，`g_ac_pro_storage` 2432 → 2784）；runner 口径
  41136 → 41488；降级构建 38672 B 不变。
- **校验**：115 → **152 条**（A1 16 + A2 9 + A3 12 = 37 新增），0 失败。

### Verification

- `-std=c99 -Wall -Wextra -Wstrict-prototypes -Werror -O2` 零警告。
- pro_runner 152 checks / 0 failed，RESULT: PASS；bss-check 三档命中
  41488 / 41456 / 39408；degraded-check 38672 B。
- 上游 `C/128KB/`：16 套件全绿，`git status -- C/128KB` 为空。

## v1.2.0 (未单独发版；代码随 v1.3.0 于 2026-10-24 首次入库)

Pro 层起点：上游门禁缺陷修复（阶段一）+ Pro 独立 scratch union 与起手三件套
B1/A4/A6（阶段二）+ 验证与对拍（阶段三）。内核口径 `.bss` 目标 **41104 B**
（38672 + 80 + 2352 + 16），占 114688 B（112 KiB）门禁的 **35.84%**。
v1.2 只存在于工作区、从未单独提交或打 tag——git 中它的代码与账本随 v1.3.0
一并出现；校验基线 **115 条**（v1.3.0 的 verify.ps1 校验数从 115 调到 152）。

### Added

- **阶段一：上游门禁缺陷修复**（设计说明 §6.3 判定的两处）：
  - **内存水位门禁在上游是死代码**：估算上限 17320 B、阈值 98304 B，比值 5.68×，
    任何输入都不触发；且漏算 `g_ac_scratch`（8224）与 `g_sym_nodes`（12288）共
    27496 B。Pro 侧以 `ac_pro_memory_used`（`src/ac_pro_solvers_ext.c`）重建口径：
    静态量取编译期真实足迹（含两块漏算），Pro 增量单独叠加，`extra_bytes` 参数让
    门禁边界可以被真正测出来而不是靠改全局变量造数据。
  - **编译期断言从严 `>=` → `==`**：union 总尺寸与逐视图尺寸精确钉死，并以受控
    实验（人为撑大 union）证明校验真的会响，不是摆设。
- **阶段二：Pro union（模式 β）+ 起手三件套**：
  - `ac_pro_scratch_t`（`src/ac_pro_scratch.h`）首版三视图：`nr` 264 B /
    `sym` 1792 B / `lu` 2352 B（峰值）；存储实体 `g_ac_pro_storage`
    2432 = 2352 + 80 + 16；8 字节对齐由 `offsetof` 探针在编译期证明。
  - **B1 五次以上数值求根**（牛顿迭代，`nr` 视图 264 B；不收敛 →
    `g_solve_error = 3`）——解除用户最可见的 degree > 4 直接拒绝限制。
  - **A4 LU 分解**（`lu` 视图 2352 B，决定 v1.2 union 尺寸；消元核复用上游
    `ac_matrix.c`，只把乘数抄出，人日成本最低）。
  - **A6 隐函数求导**（`g_var_val[10]` +80 B，工作区在栈上零增量；复用上游
    `ac_sym_derivative` + `ac_sym_eval`，`T_VAR` payload 从恒 0 变为变量 ID，
    8 bit 免费）。
  - 三件套共同点：零新解析器语义、零 arena 预算、零 bigint——风险最低、收益最
    直接的起点（设计说明 §7.1）。
- **阶段三：验证与对拍**：verify.ps1 一键验收脚本、pro_kernel_main 账本与
  test_pro_runner 规模账本初版。

### Performance

- **静态内存**：内核口径 **41104 B**（35.84% of 114688）/ runner 口径
  **41136 B**；相对上游增量 = 80 + 2352 + 16 = **2432 B**。
- 校验 **115 条**，0 失败；上游 16 模块回归全绿。

## v1.1.0 (2026-10-21)

C/128KB 零堆内存压缩：`.bss` **57.64 KiB → 38.48 KiB（39.4 KB）**，减少 19616 B（**−33.24%**）。
功能零变更、性能零衰退（四个 stage 全部落在 ±3% 噪声带内）。

### Added

- **`ac_scratch.h` 编译期 union overlay 机制**：20 个模块私有 scratch 池折叠进一个顶层
  `union ac_scratch_t`（峰值 8224 B），C99 §6.7.2.1p14 保证每个成员自偏移 0 开始，
  等价于「编译期段重叠」——零运行时开销、零 API 变更、无 arena、无链接脚本、无编译器插件。
- **`ac_scratch_cfg.h` 零依赖开关头**：`AC_SCRATCH_UNION`（总开关）+
  `SYM` / `MAT` / `CALC` / `RATROOT` / `BIGINT` / `STK` / `LRHS` 七个分组开关，
  任一置 0 即恢复原始静态数组，源码无需改回。
- **`ac_scratch.c`**：`g_ac_scratch` / `g_ac_stk` 两个符号的唯一定义处。
- **C99 编译期断言**：`AC_STATIC_ASSERT` 守住 `sizeof(ac_scratch_t) >= 8224`、
  栈 union 覆盖求值栈、矩阵侧容纳增广矩阵三个不变量。
- **`tests/bench/result-c128-1M-v100.csv`**：v1.0.0 同机基线压测数据，供后续版本对比。

### Changed

- **CRC32 表改为 `static const`**：256 × 4 B 由 `.bss`（可变、懒初始化）移入 `.rdata`（ROM）。
- **Base64 反查表改为 `static const`**：256 × 1 B 同样移入 `.rdata`。
- **token 原地切片**：等号两侧不再拷入 `g_lhs_token` / `g_rhs_token`（2 × 1024 B 已移除），
  直接切片 `g_token_pool` —— `ac_to_rpn` 的 `src` 本就是 `const`，拷贝是纯冗余。
  钳制逻辑（`lhs_n` / `rhs_n` 取 `min(count, half)`）逐字保留，可观测行为不变。
- **`g_op_stack` / `g_eval_stack` 栈别名**：二者以 `g_rpn_token` 交接、永不并发，
  合并为 `union ac_stk_u`（1024 B），省 512 B。
- **符号模块三视图折叠**：`ac_sym_derivative` 用 `{stack, dmap}`、`ac_sym_eval` 用
  `{stack, val}`、`ac_sym_equal` 用 `{pa, pb}`，三者永不并发，14336 B → 8224 B。
- **矩阵 / 微积分 / 有理根 / 大整数 scratch 并入 union**：全部被 8224 B 峰值吸收，额外成本 0。
- **`ac_check_memory_watermark()` 口径同步**：水位估算改为按实际静态占用计算
  （栈 union 只计一次、lhs/rhs 不再计入），分三个编译期分支。
- **`tests/bench/bench-c.c` / `bench-c-makefile`**：压测夹具跟进 scratch 重构
  （`__has_include("ac_scratch.h")` 分支 + SOURCES 补 `ac_scratch.c`）。

### Fixed

- **CRC32 静默错值隐患**（严重，无崩溃、无错误码）：`s_crc_table` / `s_b64_rev` 原为
  懒初始化缓存 + `ready` 标志。若并入 union，`ac_crc32()` → `ac_mat_inverse()` → `ac_crc32()`
  的序列会让第二步覆写表、第三步因 `ready` 仍为 1 而跳过重建，**静默返回错值**。
  实测 `crc32("123456789")` 从 `CBF43926` 变为 `C4D28698`；在 `[compute][checksum]` 循环里
  从第 2 轮起退化为常量 `FFFFFFFF`，完整性校验静默变成 no-op。
  既有 1000+ 校验无法发现（唯一的 `ac_util_reset()` 在最后一个 suite 末尾）。
  **修复方式**：两表改为 `static const` 存 `.rdata`，物理不可写，失效模式从「靠纪律避免」
  变为「由构造消除」。该方案比「并入 union」多 144 B，但消除隐患。
- **头文件宏展开炸弹**：`ac_parser.h` 的 `extern double g_eval_stack[EVAL_STACK_SIZE];`
  在栈别名宏生效后会展开成 `extern double (g_ac_stk.ev)[...]` 这种非法语法，
  此前只因 `ac_parser.h` 恰好先于 `ac_scratch.h` 被包含而侥幸编译通过。
  新增 `ac_scratch_cfg.h`（零依赖）供模块头文件查询开关，并给相关 `extern` 加守卫。
- **`ac_ratroot.c` 的 `s_rr_count` 定义位置**：从 scratch 守卫块内移出，保持为普通标量
  （它不是存储池，不该被 overlay）。

### Performance

- **静态内存**：`.bss` 59024 → **39408 B（−19616，−33.24%）**；
  `.data + .bss` 59296 → **39680 B（−33.11%）**；门禁占用率 60.03% → **40.09%**。
- **代码体积**：真正的代码段 `.text` **146424 → 146376 B（−48 B）**，零代码膨胀；
  `size` 报告的 text 列 201480 → 202804 B（+1324）全部来自 `.rdata` 增加 1392 B
  （CRC32 表 1024 + Base64 反查表 256 + 对齐），即常量表从 RAM 搬进 ROM 的代价。
- **运行性能（100 万次 × 4 stage，v1.0.0 vs v1.1.0，16 次采样中位数）**：

  | stage | v1.0.0 µs/op | v1.1.0 µs/op | 变化 | 判定 |
  |:---|---:|---:|---:|:---|
  | parse | 0.0190 | 0.0191 | +0.53% | ✅ 噪声内 |
  | eval（解析+求值） | 0.0624 | 0.0635 | +1.92% | ✅ 噪声内 |
  | solve | 0.4586 | 0.4441 | **−3.15%** | ✅ 小幅提速 |
  | exact | 0.2607 | 0.2533 | **−2.84%** | ✅ 小幅提速 |
  | **均值** | **0.8007** | **0.7801** | **−2.57%** | ✅ **无回归** |

  solve / exact 的提速与工作集收缩（`.bss` 减 19.6 KB）带来的缓存局部性改善一致。

### Verification

- 16 模块 **1171 项测试全绿**（`LUT=1` 为 1184），`-Werror` 零警告。
- 跨版本对拍：与 `git archive HEAD`（v1.0.0）基线 **53 行 smoke CSV 逐位一致**（`diff` 为空）。
- 跨语言对拍：C128 vs JS128 **50/50 用例一致**（含错误码与五分量精确比对，0 处 > 1e-9）。
- 毒化测试：每次 API 调用前把 `g_ac_scratch` / `g_ac_stk` 填 `0xFF`，校验和不变
  ⇒ 无函数在写入前读取 scratch。
- 钳制测试：10–600 项的等式、`=` 在首/尾/缺失，输出与基线一致。
- 对齐：31 个 double/int64 池全部 `addr % 8 == 0`。
- 优化级别扫描：`-O0/-O1/-O2/-Os/-O3` 全部 16/16，布局稳定。

> 完整设计论证、风险矩阵、回退策略与四个被实测推翻的初始假设见
> 设计说明。

## v1.0.0 (2026-10-13)

四版本家族首个完整版本：同一 CAS 引擎，JS / C99 两种语言 × 64KB / 128KB 两种内存预算，
跨语言数值输出一致（< 1e-9）。

### Added

- **JS 64KB**：核心 CAS 内核（parser / evaluator / exact / solvers / math / complex）
  — 表达式解析求值、2-4 次方程求解（含复根）、6 元线性方程组、三分量精确形式。
  已在华为 WATCH FIT 4 鸿蒙应用中实机验证。
- **JS 128KB**：五分量精确形式 `(a+b√c)/d ×10^e` + 数值微积分 + 双曲/复数扩展 +
  有理根检验与低次因式分解 + 泰勒展开 + 12 元线性方程组 + 进制转换与 SI 词头。
- **C 64KB**：C99 对等实现（Cortex-M4 级）+ 数论 / 进制分数 / CRC32·SHA-256·Base64·定点数
  三个 C 独有层。
- **C 128KB**：对等 JS 128KB（五分量精确形式、双曲/反双曲、复数域扩展、数值微积分与泰勒、
  有理根降次、12 元线性方程组、表达式池与 preCheck 阈值翻倍、96KB 水位门禁）
  + 大整数（180 位十进制）+ 矩阵（12×12：乘法/转置/秩/行列式/逆/特征值/QR）
  + 特殊函数（Gamma/Beta/erf/不完全 Gamma/Bessel）+ 符号求导与因式分解。

### Zero-GC

所有版本严格零 GC。JS 靠"无临时对象"（预分配 TypedArray + 全局标量，禁对象/数组字面量、
禁递归、禁热路径字符串拼接）；C 靠"无 malloc"（全静态预分配，禁 malloc/VLA/递归，
链接期 `--wrap=malloc/free` 拦截 + 运行期命中即 `abort()`）。

验证证据：`nm` 无 `malloc`/`free`/`realloc` 符号（除 `__wrap_*`）；
JS 压测堆增量在 KB 量级。详见 [docs/ACCEPTANCE.md](docs/ACCEPTANCE.md)。

### 测试规模

| 版本 | 测试数 |
|:---|---:|
| JS 64KB | 80 + 19 ESM 接线 |
| JS 128KB | 327 + 83 回归 + 35 ESM 接线 |
| C 64KB | 565（`LUT=1` 为 578） |
| C 128KB | 1171（`LUT=1` 为 1184） |

跨版本冒烟一致性：C64 vs JS64 = 50/50，C128 vs JS128 = 50/50（根数 100% 一致，
数值 100% < 1e-9）。

### 已知差异

- `guardMultivariateNonlinear` 在 JS 64KB / JS 128KB 中已定义并导出，但未被任何求解
  流水线调用（死代码）。C99 两版均不提供该函数。
