# C/128KB-ProTEXT · 组件变更记录

本文件从**组件视角**记录 Pro 层自身的版本递进，格式遵循
[Keep a Changelog](https://keepachangelog.com/)。与根 [CHANGELOG.md](../../CHANGELOG.md)
的口径区分：根记录**全局版本节点**（仓库级 Tag，一次发布可只覆盖部分组件），本文件
记录**组件版本**（见本目录 [VERSION](VERSION)，按 Pro 层自身变更独立递进 semver）。
v1.4.0 之前的 Pro 层沿革（v1.2.0 起手三件套、v1.3.0 方向 A 多变量层、v1.3.5
特征向量与符号积分，以及 v2.1.0 全局版的稀疏线性方程组与任意精度定点小数）见根
CHANGELOG 对应条目，此处不重复。

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
