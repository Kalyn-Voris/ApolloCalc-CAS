# JS/128KB · 组件变更记录

本文件从**组件视角**记录 JS/128KB 的版本递进，格式遵循
[Keep a Changelog](https://keepachangelog.com/)。与根 [CHANGELOG.md](../../CHANGELOG.md)
的口径区分：根记录**全局版本节点**（仓库级 Tag，一次发布可只覆盖部分组件），本文件
记录**组件版本**（见本目录 [VERSION](VERSION)）。

## v2.3.0 (2026-10-31)

**与全局版本节点的对应关系**：本组件版本对应全局 Tag **v2.3.0**（组件 v2.2.0 →
**v2.3.0**）。本版四个功能块由 `feat/js-pack` 分支并行交付（feat/c-symbolic 承担
C 侧积分/有理函数/ODE），收尾组 feat/release-v2.3.0 收口。

### Added

- **数论模块**（`src/core_numtheory.js` 新）：gcd/lcm、6k±1 试除 + Miller-Rabin
  （底数集与 C 侧 `ac_numtheory.c` 逐字一致，保跨语言对拍同源）、质因数分解、
  nCr/nPr；声明安全整数域 ≤ 2^53−1，溢出返回错误码绝不返回错值。
  89 项测试。池 288 B（底数表 96 + 因子表 192）。
- **矩阵 12×12 pro 层**（`src/core_matrix.js` 升级）：部分主元 det/inv/rank、
  LU 分解（契约对齐 C 侧 A4：P·A = L·U + piv 访问器）、幂法 + Hotelling
  deflation 的实对称全特征对（|λ| 降序，对齐 ac_pro_eig_all）、QR 分解；
  ≤3×3 闭式寄存器层红线行为逐字节保留（双层结构，pro 独立池）。82 项测试。
  pro 池 5944 B（ROADMAP 预算 ~6 KB 内）。
- **常系数线性齐次 ODE 通解**（`src/core_ode.js` 新）：与 C 侧 `ac_pro_ode.c`
  依 [docs/contracts/ode-format-v23.md](../../docs/contracts/ode-format-v23.md)
  逐字镜像——特征根复用 solveQuadratic/Cubic/Quartic（_coeffs 低位在前），
  稳定排序（Re 降序、1e-9 容差）→ 聚类 → fmt（整数/有理重建 q≤1000/定点
  12 位）→ 四类项模板渲染；错误码与 C 同值（−6 EOVER JS 侧不产生）。
  72 项测试。池 1464 B（11 个加载期一次分配）。
- **guardMultivariateNonlinear 接入求解管线**（`src/core_solvers.js`）：死代码
  唯一可行方向（函数名被红线测试与构建导出契约引用，不可删除）——
  ≥2 变量且含 `^`/函数调用的非线性多元组在三个多项式求解入口
  `_solveError = 2` 拒绝，solveLinearSystem 不拦。新增 `test/test-guard.js`
  16 例（12 → 16 测试文件）。

### Changed

- **压缩内核重建**：四块新功能并入后经 `node test/build-kernel.js --minify`
  重建，`core_kernel.js` **34 510 → 49 747 B**（预算 131 072 B）；
  全项目静态池 22 416 → **30 112 B**（≈ 29.4 KiB，水位门禁 98 304 B 内）。

### 测试口径

- 16 测试文件 **TOTAL: 862 pass, 0 fail**（v2.2.0 基线 603 + 数论 89 +
  矩阵 pro 82 + ODE 72 + 守卫 16）。
- 跨语言 ODE 契约 [tests/ode-solutions.json](../../tests/ode-solutions.json)
  冻结 70 个双侧一致用例。

### Known issues

- **小正值实根通解渲染缺陷**（收尾对拍发现）：实特征根 0 < |λ| < 0.5 且
  llround 到 0 时（如 1/3、1/999），`core_ode.js` 的 `_isZeroInt` 混返
  boolean/数字、实根渲染门以 `=== 0` 比较，exp 因子被误省略、根被当作 λ=0
  渲染（C 侧同输入正确输出 `exp(1/999*x)` 项）。负小根与共轭对分支不受影响，
  故内嵌测试未暴露。详见 tests/ode-solutions.json
  `knownDeviations[0]`，按 ROADMAP.md:178 如实上报为实现 bug，修复前
  该输入不入契约 cases。

## v2.2.0 (2026-10-27)

**本文件首条记录**：JS/128KB 的组件版本轨道自本版建立，首个组件版本 v2.2.0 与
全局版本节点 v2.2.0 对齐——同号是起点对齐，此后两轨各自独立递进，不必相等。
更早的 JS/128KB 沿革（v1.0.0 首版、v2.0.0 多变量引擎/矩阵/特殊函数）见根
CHANGELOG 对应条目。

### Added

- **深度压缩管线**并入 `test/build-kernel.js --minify` 唯一重建入口：
  `core_kernel.js` **175 142 → 57 523 B**（压缩率 **67.2%**，越过 > 50% 目标，
  低于 ~87.6 KB 冲刺线；预算 131 072 B）。
- **terser 两段式压缩**（实测依据：terser 以 `module: true` 解析整文件时顶层符号
  按 ESM 语义非 global，即使 `mangle.toplevel: false` 也会改写顶层名——实测
  36 747 B 但压缩体 eval 回归无法按名运行）：(a) 内核体按经典 script 压缩且
  `mangle.toplevel: false`，顶层名全保留；(b) EXPORT_BLOCK 单独按 module 压缩
  （其自由引用为 undeclared 符号不会改写，`mangle.properties` 默认关闭 → 对象
  属性名原样）。两段拼接后 `export default` 契约不变，压缩体可做全量 eval 回归。
  terser 经 `npm install terser --no-save --no-package-lock` 装入（实测 5.51.2
  可解析）。
- **回退路径（无 terser / 无网络可复现）**：terser require 失败或压缩异常时自动
  改用内置词法级剥离器，同入口、同全量校验：单遍逐态扫描字符串/模板串（状态栈
  处理 `${}` 插值内回到代码态）/正则字面量（经典 prev-token 启发式：return/
  typeof/new 等关键字后允许，标识符/数字/字符串/)/]/++/-- 后视为除号）/注释
  边界，绝不盲目正则去注释；token 文本逐字零改写、原换行位置一律保留换行符
  （ASI/restricted production 零改动）、行内空白仅在相邻 token 可能粘连成词或
  合字成运算符（++/--/=>/<=）时保留一个空格、未闭合一律 throw fail-fast；已通过
  16 项词法边界单测。回退实测 175 142 → 79 435 B（**54.6%**），同样达标——
  门禁在无 terser/无网络环境用同一命令仍可复现（已做 node_modules 移走演练验证）。

### 测试口径

- 12 测试文件 **TOTAL: 603 pass, 0 fail**。
- 重建命令 `node test/build-kernel.js --minify`（本目录下）实跑输出末行
  **BUILD OK, all tests passed**，57 523 B / 预算 131 072 B。
