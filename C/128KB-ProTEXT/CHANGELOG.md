# C/128KB-ProTEXT · 组件变更记录

本文件从**组件视角**记录 Pro 层的版本递进，格式遵循
[Keep a Changelog](https://keepachangelog.com/)。根
[CHANGELOG.md](../../CHANGELOG.md) 记录**全局版本节点**（仓库级 Tag，
一次发布可只覆盖部分组件），本文件记录**组件版本**（见本目录 [VERSION](VERSION)）。
全局版本每次发布都会注明对应的组件版本。

## v1.8.0 (2026-11-04) · 对应全局 v2.6.0

- one-sided Jacobi SVD 奇异值分解（m,n ≤ 12，C 独有），契约 [svd-format-v26](../../docs/contracts/svd-format-v26.md)；svd 视图 3760 B 被 union 吸收，内核静态内存维持 42608 B 零增长，pro_runner 935 checks。另增 100 万次压测 `probench` 目标。

## v1.7.0 (2026-11-04) · 对应全局 v2.5.0

- 复域超越函数库（15 API，契约 [complex-ext-v25](../../docs/contracts/complex-ext-v25.md)）、二元方程组 GB 消元数值全解（契约 [sysolve-format-v25](../../docs/contracts/sysolve-format-v25.md)）、Sturm 序列实根计数与隔离 1..8 次（契约 [sturm-format-v25](../../docs/contracts/sturm-format-v25.md)）；零新 static 对象，内核静态内存维持 42608 B。

## v1.6.3 (2026-11-03) · 对应全局 v2.4.3

- src/ 全部 37 个 .c/.h 统一 MIT 许可证头，纯注释变更，编译产物与 `.bss` 逐字节不变。

## v1.6.2 (2026-11-03) · 对应全局 v2.4.2

- ODE 解析器差分收口（项首双符号、每项至多一个 `*`），370 条确定性语料 C/JS 对拍归零。

## v1.6.1 (2026-11-01) · 对应全局 v2.4.1

- ODE 契约裁决落地（`x^2*y=0` → EVARCOEF、`y''x=0` → EPARSE）并冻结渲染回归用例。

## v1.6.0 (2026-10-31) · 对应全局 v2.4.0

- 一般实矩阵全部特征值（Francis 隐式双移 QR，复特征值不产出向量）与 square-free 分解 (根, 重数)，契约 [sqfree-format-v24](../../docs/contracts/sqfree-format-v24.md)；`ac_pro.h` 的「非对称返 −2」约束取消，−2 改为「迭代不收敛」。

## v1.5.0 (2026-10-31) · 对应全局 v2.3.0

- 常系数线性齐次 ODE 通解（契约 [ode-format-v23](../../docs/contracts/ode-format-v23.md)）、符号积分扩表（64 → 128 项、一层 → 固定两层分部）、单变量有理函数工具（GCD/cancel/apart）。

## v1.4.0 (2026-10-27) · 对应全局 v2.2.0

- 2 变量 Groebner 基（M4GB 逐对归约，lex，deg ≤ 4，超限按错误码拒绝）；验收脚本三处修复（相对输出目录、负向编译诊断、UTF-8 BOM）。

## v1.3.5 及更早

v1.2.0 起手三件套（内存门禁重建、高次数值求根、LU 分解、隐函数求导）、v1.3.0
多变量 GCD / 因式分解 / 结式、v1.3.5 实对称特征向量与符号积分，以及全局 v2.1.0
的稀疏线性方程组与任意精度定点小数，见根
[CHANGELOG.md](../../CHANGELOG.md) 对应条目，此处不重复。
