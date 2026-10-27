# JS/128KB · 组件变更记录

本文件从**组件视角**记录 JS/128KB 的版本递进，格式遵循
[Keep a Changelog](https://keepachangelog.com/)。与根 [CHANGELOG.md](../../CHANGELOG.md)
的口径区分：根记录**全局版本节点**（仓库级 Tag，一次发布可只覆盖部分组件），本文件
记录**组件版本**（见本目录 [VERSION](VERSION)）。

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
