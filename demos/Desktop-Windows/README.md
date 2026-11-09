# ApolloCalc Desktop Demo — C# WinForms × C99 DLL

**English** | [中文](#中文)

## Overview

A native Windows graphing-calculator demo built on top of the ApolloCalc-CAS
C99 engine: expression parsing, evaluation and sampling all run inside the C
kernel (`ApolloCalc.dll`); the C# WinForms layer only handles input, the
sampling loop and GDI+ drawing.

The demo is **physically isolated** from the core library — `C/128KB-ProTEXT/`
and `C/128KB/` stay byte-identical. The DLL is compiled straight from the core
sources by a standalone script in this directory; the core build system is
never invoked.

## Directory layout

```
demos/Desktop-Windows/
├── README.md                 ← this file (English + 中文)
├── .gitignore                ← bin/ obj/ publish/ and the bridge DLL are not committed
├── publish.bat               ← one-click packaging: dotnet publish -> publish/
├── publish/                  ← packaging output (gitignored)
├── c_bridge/                 ← C-side DLL bridge (standalone build)
│   ├── ac_gui_bridge.h       ← exported surface: double ac_gui_eval(const char*, double)
│   ├── ac_gui_bridge.c       ← bridge: reset -> parse -> eval_at(x)
│   ├── build.bat             ← one-click build of ApolloCalc.dll (MinGW gcc)
│   └── ApolloCalc.dll        ← build artifact (gitignored)
└── ApolloCalc.Desktop/       ← C# .NET 8 WinForms project
    ├── ApolloCalc.Desktop.csproj
    ├── Program.cs            ← entry point, optional --lang=en|zh override
    ├── Strings.cs            ← UI string dictionary (default English, zh-* auto)
    └── MainForm.cs           ← input box + Plot button + PictureBox (GDI+)
```

## Quick start

```bat
demos\Desktop-Windows\c_bridge\build.bat
dotnet run --project demos\Desktop-Windows\ApolloCalc.Desktop
```

Two commands: the first compiles `ApolloCalc.dll` from the C99 sources, the
second builds and launches the GUI. The `.csproj` automatically copies
`c_bridge\ApolloCalc.dll` into the output directory when present, and the
first evaluation acts as a probe — if the DLL is missing you get an explicit
dialog telling you to run `build.bat`.

## Get the ready-to-run EXE

```bat
demos\Desktop-Windows\c_bridge\publish.bat
```

then double-click `demos\Desktop-Windows\publish\ApolloCalc.Desktop.exe` —
no build tools needed on the machine. The publish output is
**framework-dependent** (small, a few MB): it requires the
[.NET 8 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/8.0)
and carries the native `ApolloCalc.dll` with it. Manual equivalent:

```bat
dotnet publish demos\Desktop-Windows\ApolloCalc.Desktop -c Release -r win-x64 --self-contained false
```

The output lands in `demos/Desktop-Windows/publish/` (set by `PublishDir`
in the `.csproj`); that directory is gitignored.

## FAQ: Windows shows a protection warning (SmartScreen)

When you double-click `ApolloCalc.Desktop.exe` for the first time, Windows
Defender SmartScreen may pop up a blue **"Windows protected your PC"** dialog
and block the launch. This is expected behavior, not a virus warning:

- **Why it happens**: this is a free, open-source, non-profit demo. Commercial
  code-signing certificates cost hundreds of dollars a year, so the EXE
  carries no signature — Windows treats any unsigned, never-seen-before
  binary as an "unknown publisher".
- **Safety statement**: the program is built entirely from the open source
  code in this repository (C99 kernel, bridge layer, C# UI — all three layers
  are in the repo). It contains no malware: no installer, no telemetry, no
  network access; everything runs offline on your machine. Feel free to audit
  every line, or just build it yourself with `build.bat` + `publish.bat`.
- **How to run it anyway**:
  1. In the blue dialog, click **More info**;
  2. Click the **Run anyway** button that appears at the bottom right — the
     app then starts normally (the prompt only shows on first launch).

## Prerequisites

| Tool | Version | Notes |
| --- | --- | --- |
| MinGW-w64 gcc | 8.1.0 (repo baseline) | must be on `PATH`; builds the bridge DLL |
| .NET SDK | 8.0.x | `dotnet build` / `dotnet run` |

## How the DLL is built (build.bat)

1. Checks that `gcc` is on `PATH` and the core headers exist;
2. Compiles the **bridge TU + every `C/128KB-ProTEXT/src/*.c` wrapper /
   extension TU** in one shot with the same flags the core Makefile uses
   (`-std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror -DAC_PRO=1`),
   producing `c_bridge\ApolloCalc.dll` (`-shared`,
   `__declspec(dllexport)` export, no import lib needed);
3. **Never** links `C/128KB/src/*.c` directly — upstream `.c` files are
   already compiled exactly once inside the wrapper TUs via `#include`;
   adding them again means "multiple definition" (same discipline as note 3
   in the core Makefile header).

## Bridge API

```c
double ac_gui_eval(const char *expr, double x);
void   ac_gui_set_angle_mode(int deg);   /* 0 = RAD, non-zero = DEG */
```

- `ac_gui_eval` runs the same sequence as the core self-check
  (`ac_pro_smoke.c`): `ac_solvers_reset()` → `ac_parse_expression()` →
  `ac_eval_expression_at(x)`;
- returns a finite `double` = `f(x)`; `NaN` = empty expression / parse error /
  evaluation error (`EVAL_DIV_ZERO`, `EVAL_DOMAIN`, `EVAL_OVERFLOW`);
- the kernel uses global pools — single-threaded by design (as the core
  library always has): call from the UI thread only;
- trigonometric functions default to **degrees** (`g_angle_deg = 1`, mirroring
  the JS `angleMode="DEG"`); `ac_gui_set_angle_mode` is a demo-layer
  convenience switch and does not change the core default.

C# side (`MainForm.cs`):

```csharp
[DllImport("ApolloCalc.dll", CharSet = CharSet.Ansi)]
private static extern double ac_gui_eval(string expr, double x);
```

## UI & localization

- Type any `f(x)` (`sin(x)*exp(-x/5)`, `x^3-3x`, `ln(abs(x)+1)` …), press
  **Plot** (or Enter);
- the app samples `[-10, 10]` at step `0.1` through `ac_gui_eval` (201
  points), auto-scales the y-axis, and draws grid, axes and the curve in the
  `PictureBox.Paint` event; `NaN` samples break the curve
  (`ln(x)` simply does not draw for `x < 0`);
- a **Radians (RAD)** checkbox switches the angle mode and re-plots;
- a small MIT copyright notice sits in the bottom-right corner of the window;
- **UI language defaults to English**; if the system language is `zh-*` the
  UI switches to Chinese automatically. `--lang=en` / `--lang=zh` overrides
  the detection (language is resolved once at startup).

## License

MIT © 2026 Kalyn-Voris — see the root [`LICENSE`](../../LICENSE). Both demos
display the notice in-app (HTML footer / desktop status bar).

## Isolation red lines

- Only files under `demos/Desktop-Windows/` may change; the core library
  directories stay untouched;
- the bridge DLL is a **demo artifact**: it never enters the core build
  system and is not committed (see `.gitignore`);
- the core library's `.bss` ledger, gates and Makefiles are none of this
  demo's business.

---

# 中文

## 概述

一个跑在 ApolloCalc-CAS C99 内核之上的 Windows 原生图形计算器 Demo：
表达式解析、求值与采样全部在 C 内核（`ApolloCalc.dll`）里完成，C# WinForms
层只负责输入、采样循环和 GDI+ 绘图。

Demo 与核心库**物理隔离** —— `C/128KB-ProTEXT/` 与 `C/128KB/` 逐字节不动。
DLL 由本目录下的独立脚本直接编译核心源码产出，全程不调用核心库构建系统。

## 目录结构

```
demos/Desktop-Windows/
├── README.md                 ← 本文件（英文 + 中文）
├── .gitignore                ← bin/ obj/ publish/ 与桥接 DLL 不入库
├── publish.bat               ← 一键打包：dotnet publish → publish/
├── publish/                  ← 打包输出（gitignore）
├── c_bridge/                 ← C 侧 DLL 桥接（独立构建）
│   ├── ac_gui_bridge.h       ← 导出面：double ac_gui_eval(const char*, double)
│   ├── ac_gui_bridge.c       ← 桥接实现：reset -> parse -> eval_at(x)
│   ├── build.bat             ← 一键编译 ApolloCalc.dll（MinGW gcc）
│   └── ApolloCalc.dll        ← 构建产物（gitignore）
└── ApolloCalc.Desktop/       ← C# .NET 8 WinForms 项目
    ├── ApolloCalc.Desktop.csproj
    ├── Program.cs            ← 入口，可选 --lang=en|zh 语言覆盖
    ├── Strings.cs            ← UI 文案字典（默认英文，zh-* 自动切中文）
    └── MainForm.cs           ← 输入框 + 绘图按钮 + PictureBox(GDI+)
```

## 快速开始（两条命令）

```bat
demos\Desktop-Windows\c_bridge\build.bat
dotnet run --project demos\Desktop-Windows\ApolloCalc.Desktop
```

第一条命令把 C99 源码独立编译成 `ApolloCalc.dll`，第二条直接编译并运行
GUI。`csproj` 检测到 `c_bridge\ApolloCalc.dll` 存在时会自动复制到输出目录；
首次求值是一次探针调用，DLL 缺失时会弹窗提示先跑 `build.bat`。

## 获取打包好的 EXE

```bat
demos\Desktop-Windows\c_bridge\publish.bat
```

然后双击 `demos\Desktop-Windows\publish\ApolloCalc.Desktop.exe` 即可运行，
目标机器**无需任何构建工具**。publish 产物是**依赖框架**的精简包（几 MB）：
需要机器装有 [.NET 8 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/8.0)，
原生 `ApolloCalc.dll` 会随包一起带出。等价的手动命令：

```bat
dotnet publish demos\Desktop-Windows\ApolloCalc.Desktop -c Release -r win-x64 --self-contained false
```

输出落在 `demos/Desktop-Windows/publish/`（由 `.csproj` 的 `PublishDir` 指定），
该目录已 gitignore。

## 常见问题：Windows 弹出"已保护你的电脑"怎么办？

首次双击 `ApolloCalc.Desktop.exe` 时，Windows Defender SmartScreen 可能弹出
蓝色拦截窗（"Windows 已保护你的电脑"，提示发布者未知）。这是正常现象，
不是病毒告警：

- **原因**：本程序是免费、开源、非营利的演示软件。商业代码签名证书每年
  需要数百美元，因此 EXE 未签名 —— Windows 会把任何「无签名 + 信誉度为零
  的本地新构建」一律当作"未知发布者"拦截。
- **安全声明**：程序完全由本仓库的开源代码在本地构建（C99 内核、桥接层、
  C# 界面三层都在仓库里），不含任何恶意软件：无安装器、无遥测、无网络
  访问，完全在本地离线运行。欢迎逐行审计，或直接自己构建
  （`build.bat` + `publish.bat`）。
- **放行步骤**：
  1. 在蓝色弹窗中点击 **更多信息**（More info）；
  2. 点击右下角出现的 **仍要运行**（Run anyway）按钮，即可正常体验
     （该提示只在首次启动时出现）。

## 前置要求

| 工具 | 版本 | 说明 |
| --- | --- | --- |
| MinGW-w64 gcc | 8.1.0（仓库基线） | 需在 `PATH`；用于编译桥接 DLL |
| .NET SDK | 8.0.x | `dotnet build` / `dotnet run` |

## build.bat 如何独立编译 C99 源码

1. 检查 `gcc` 在 `PATH` 中、核心库头文件存在；
2. 用与核心库 Makefile 同款旗标
   `-std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror -DAC_PRO=1`，
   把 **桥接 TU + `C/128KB-ProTEXT/src/*.c` 全部包装/增量 TU** 一次编译成
   `c_bridge\ApolloCalc.dll`（`-shared`，`__declspec(dllexport)` 导出，
   不需要导入库）；
3. **绝不**把 `C/128KB/src/*.c` 直接列入链接 —— 上游 `.c` 已被包装 TU
   `#include` 编译恰好一次，重复列入会 multiple definition（核心 Makefile
   头注第 3 条的同款纪律）。

## 桥接 API

```c
double ac_gui_eval(const char *expr, double x);
void   ac_gui_set_angle_mode(int deg);   /* 0 = RAD，非零 = DEG */
```

- `ac_gui_eval` 的调用序列与核心库自检 `ac_pro_smoke.c` 同款：
  `ac_solvers_reset()` → `ac_parse_expression()` → `ac_eval_expression_at(x)`；
- 返回有限 `double` = `f(x)`；返回 `NaN` = 表达式为空 / 解析失败 /
  求值错误（除零 `EVAL_DIV_ZERO`、定义域 `EVAL_DOMAIN`、溢出 `EVAL_OVERFLOW`）；
- 内核使用全局池，与核心库既有单线程约定一致：只在 UI 线程调用；
- 三角函数默认**角度制**（`g_angle_deg = 1`，镜像 JS `angleMode="DEG"`）；
  `ac_gui_set_angle_mode` 是演示层的便捷开关，不改核心默认值。

C# 侧签名（`MainForm.cs`）：

```csharp
[DllImport("ApolloCalc.dll", CharSet = CharSet.Ansi)]
private static extern double ac_gui_eval(string expr, double x);
```

## 界面与国际化

- 输入框输入 `f(x)`（支持 `sin(x)*exp(-x/5)`、`x^3-3x`、`ln(abs(x)+1)` 等），
  点 **绘图**（或输入框内按 Enter）；
- 程序在 `[-10, 10]` 上按 `0.1` 步长调 `ac_gui_eval` 采样 201 点，y 轴
  自适应量程，在 `PictureBox.Paint` 事件里用 GDI+ 画网格、坐标轴与曲线；
  `NaN` 采样点断笔（`ln(x)` 在 x < 0 段不连线）；
- **弧度制 (RAD)** 复选框切换角度模式并立即重绘；
- 窗口右下角有一行小字号的 MIT 版权声明；
- **界面默认英文**；系统语言为 `zh-*` 时自动显示中文；
  `--lang=en` / `--lang=zh` 可显式覆盖（语言在启动时解析一次）。

## 许可证

MIT © 2026 Kalyn-Voris —— 见仓库根目录 [`LICENSE`](../../LICENSE)。两个
Demo 均在界面内展示版权声明（HTML 页脚 / 桌面状态栏）。

## 隔离红线（本 Demo 的边界约定）

- 只允许改动 `demos/Desktop-Windows/` 下的文件；核心库目录零改动；
- 桥接 DLL 是**演示层产物**，不进核心库构建系统，也不入库（见 `.gitignore`）；
- 核心库的 `.bss` 账本、门禁与 Makefile 均与本 Demo 无关。
