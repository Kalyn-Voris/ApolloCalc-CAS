# C/128KB-ProTEXT · 组件变更记录

本文件从**组件视角**记录 Pro 层的版本递进，格式遵循
[Keep a Changelog](https://keepachangelog.com/)。与根
[CHANGELOG.md](../../CHANGELOG.md) 的口径区分：根记录**全局版本节点**（仓库级 Tag，
一次发布可只覆盖部分组件），本文件记录**组件版本**（见本目录 [VERSION](VERSION)）。
双轨两个口径不必同号：全局版本每次发布都会注明对应的组件版本。

## v1.8.0 (2026-11-04) · 对应全局 v2.6.0

- **one-sided Jacobi SVD 奇异值分解**（新增 `ac_pro_svd.c`）：m,n ≤ 12 实矩阵
  （m < n 内部走转置对偶）的降序奇异值与 U/V 出口。30 轮 sweep 预算超限返 −2、
  出口双闸门（重构残差 ‖A−UΣVᵗ‖∞ + 正交性 ‖UᵗU−I‖∞）不过返 −3、超域输入
  返 −1——一律诚实拒绝，绝不输出未验证结果；幂 2 精确缩放与符号规范化沿用
  schur 同款纪律。输出契约冻结于
  [docs/contracts/svd-format-v26.md](../../docs/contracts/svd-format-v26.md)，
  52 例契约 JSON + 语料探针 `svd_runner`（金标准 `tests/bench/svd128-c.csv`）。
  **SVD 为 C 独有能力**：JS 面因压缩内核实测 128 846 B 已越熔断线 126 976 B
  按降载序顺延 v2.7（v2.4.0 schur C-only 先例），如实标注不对等。
- svd 视图 3760 B ≤ 3848 吸收线（margin 88 B），被 Pro union 完全吸收——
  内核静态内存维持 **42608 B** 零增长（2026-11-04 `bss-check` 复测：
  42608 / 42640 / 39408 精确命中）。
- verify.ps1 新增 SVD 用例数门禁（≥13 例），**43 项 verdict 全绿**；
  pro_runner **692 → 935 checks, 0 failed**（SVD 16 例五组 = 243 checks）。
- 新增 100 万次压测 `probench` 目标（`pro_bench.exe`，链接内核对象 +
  tests/bench/bench-c.c）：供 run-all.sh 的 pro_runner 列（v2.6.0 G3 基准
  终版）使用，与 pro_runner 共享 obj 缓存，实测 Pro 层对上游四 stage 零可测
  回归（Pro/C128 = 0.99/0.91/1.02/1.04，抖动带内）。

## v1.7.0 (2026-11-04) · 对应全局 v2.5.0

- **复域超越函数库**（新增 `ac_pro_complex_ext.c`，15 API）：复三角 / 复双曲 /
  反三角 / 反双曲 / 全复幂（`ac_pro_cpow`，覆写 A 约定沿袭 cPowReal）/ 主值
  k 次根（`ac_pro_cnthroot`）/ 复 Gamma（`ac_pro_cgamma`）。主值分支、割线
  ±0 侧与奇点（EDOM）约定冻结于
  [docs/contracts/complex-ext-v25.md](../../docs/contracts/complex-ext-v25.md)，
  与 JS 侧同语料 789 行差分零分歧（行间最大 err 2.766e-16）。
  专项验收 `cx_runner` **134 checks, 0 failed**。
- **二元多项式方程组 GB 消元数值全解**（新增 `ac_pro_sysolve.c`）：字符串进、
  解列表字符串出，全部解含复数解；GB={1} → 成功语义 0 解，正维数 / 超次数 /
  残差回验不过按错误码诚实拒绝。契约
  [sysolve-format-v25.md](../../docs/contracts/sysolve-format-v25.md)。
  pro_runner 内 20 例，与 JS 侧 20/20 对拍零分歧。
- **Sturm 序列实根计数与隔离**（新增 `ac_pro_sturm.c`）：`ac_pro_sturm_count` /
  `ac_pro_sturm_isolate`，1..8 次整系数多项式（5..8 由 `ac_bigint` 承载链系数
  膨胀）在 [a,b]（含 ±∞ 端点）上的精确实根计数与隔离区间；端点开闭语义与
  二分深度上限 48 层双侧统一，契约
  [sturm-format-v25.md](../../docs/contracts/sturm-format-v25.md)。
  pro_runner 内 16 例，另含 Sturm 计数与 DK 数值求根交叉校验。
- 零新 static 对象：消元产物与根候选走栈缓冲，DK 复用既有 nr 视图，
  内核静态内存维持 **42608 B** 零增长（2026-11-04 `bss-check` 复测）。
- verify.ps1 头注与门禁行同步 v2.5.0 实测（新增 CX / SYSOLVE / STURM 三组），
  **42 项 verdict 全绿**；pro_runner **583 → 692 checks, 0 failed**。

## v1.6.3 (2026-11-03) · 对应全局 v2.4.3

- 生态构建版本：src/ 全部 37 个 .c/.h 统一 MIT 许可证头（SPDX + 版权声明），
  纯注释变更，编译产物与 .bss 账本逐字节不变。无功能性变更。

## v1.6.2 (2026-11-03) · 对应全局 v2.4.2

- ODE 解析器差分收口（370 条确定性语料 C/JS 对拍归零）：
  项首可再带一个符号（`+-3y''`/`--3y''`，后一符号覆盖前一，§3.2 明文合法，
  此前误报 EPARSE）；每项至多一个 `*`（`x*x*y''=0` 此前误报 EVARCOEF）。
- 新增 4 条回归用例。通过 583 项测试，`.bss` 维持 42608 B。

## v1.6.1 (2026-11-01) · 对应全局 v2.4.1

- ODE 契约裁决落地：`x^2*y=0` → EVARCOEF（原行为不变，钉死）、`y''x=0` → EPARSE
  （新增用例）、`x^y''=0` 幂值可空镜像用例。
- 小正有理根渲染回归 3 条（q=999 边界、1/3、1/100）冻结期望串。
- verify.ps1 banner 更新至 v2.4.1。通过 579 项测试，`.bss` 维持 42608 B。

## v1.6.0 (2026-10-31) · 对应全局 v2.4.0

- **一般实矩阵的全部特征值与特征向量**（新增 `ac_pro_schur.c`）：不再只支持实对称
  矩阵；不收敛时返回错误码，不给出未经收敛的结果。特征向量为可选输出，复特征值
  不产出向量。
- **多项式平方自由分解与重根重数**（新增 `ac_pro_sqfree.c`）：输出 (根, 重数) 列表，
  重数为精确整数值；次数上限 8。输出字符串格式冻结于
  [docs/contracts/sqfree-format-v24.md](../../docs/contracts/sqfree-format-v24.md)，
  与 JS 侧共用同一契约。
- 两项能力均复用既有内存池，内核静态内存维持 42608 B 不变。
- 通过 574 项测试。
- 接口口径变化：`ac_pro.h` 原先「非对称矩阵返回 −2」的约束取消，−2 现在表示
  迭代不收敛。

## v1.5.0 (2026-10-31) · 对应全局 v2.3.0

- **常系数线性齐次微分方程通解**（新增 `ac_pro_ode.c`）：输入 `y''+3y'+2y=0` 型字符串，
  输出通解字符串。非齐次、变系数、阶数 ≥ 5 一律按错误码拒绝，不做近似硬解。输出格式
  冻结于 [docs/contracts/ode-format-v23.md](../../docs/contracts/ode-format-v23.md)，
  与 JS 侧共用同一契约。
- **符号积分增强**（`ac_pro_integrate.c`）：积分表 64 → 128 项，新增三角、双曲与含根式
  分式的不定积分；分部积分由一层扩为两层，并同步修订 README 中「只做一层」的成文限制。
- **单变量有理函数工具**（新增 `ac_pro_rational.c`）：最大公因式、约分、部分分式分解；
  浮点系数拒绝，分解结果回代验证不通过时报错而非输出未验证结果。
- 三项能力均零静态内存增量，内核静态内存维持 42608 B。
- 通过 488 项测试。

## v1.4.0 (2026-10-27) · 对应全局 v2.2.0

- **2 变量 Groebner 基**：按字典序，输入总次数 ≤ 4；中间规模超限时返回错误码，
  绝不输出残缺基。
- **验收脚本三处修复**：相对输出目录在不同调用位置下解析到错误目录（该失败此前被
  「预期拒绝」判定掩盖成通过）；负向编译检查失败时改为带出原因；脚本文件补 UTF-8 BOM，
  消除中文注释在中文字符集系统上破坏脚本解析的隐患。
- 静态内存维持 42608 B；通过 396 项测试。

## v1.3.5 及更早

Pro 层的 v1.2.0 起手三件套（内存门禁重建、B1 高次方程数值求根、A4 LU 分解、A6 隐函数
求导）、v1.3.0 多变量 GCD / 因式分解 / 结式、v1.3.5 实对称特征向量与符号积分，以及
全局 v2.1.0 的稀疏线性方程组与任意精度定点小数，见根
[CHANGELOG.md](../../CHANGELOG.md) 对应条目，此处不重复。
