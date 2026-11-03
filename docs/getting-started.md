# ApolloCalc-CAS · 十分钟接入

> 本文档所有代码块与输出均为**实跑逐字实录**(2026-11-03,v2.4.2 首录;2026-11-04 v2.6.0
> 复跑逐字一致)。
> ApolloCalc-CAS 是零依赖的 C99 / JavaScript 双内核计算机代数系统:
> 全静态分配(Zero-GC)、无递归、无第三方库,面向嵌入式与受限环境。

## 0. 你需要什么

- **C 侧**:任意 C99 编译器(gcc / clang / MSVC / Keil ARMCC / ESP-IDF 皆可),无第三方依赖,零 `malloc`
- **JS 侧**:Node ≥ 14,或任何支持 ES5 + TypedArray 的环境(浏览器 / JerryScript / 小程序容器)

---

## 1. 极简 Demo(复制粘贴即可跑)

### C99:3×3 实对称矩阵特征值 + 二阶 ODE 通解

```c
/* demo.c —— 5 行逻辑,链接全部内核对象即可 */
#include "ac_pro.h"
#include <stdio.h>

int main(void) {
    double a[9] = {2, 1, 0, 1, 3, 0, 0, 0, 4};   /* 实对称 3x3,行主序 */
    ac_pro_eig_all(a, 3);                        /* 特征值按 |λ| 降序写入视图 */
    for (int k = 0; k < ac_pro_eig_count(); k++) {
        printf("lambda[%d] = %.6f\n", k, ac_pro_eig_value(k));
    }
    char out[256];
    ac_pro_ode("y''+3y'+2y=0", out, sizeof out); /* 常系数线性齐次 ODE 通解 */
    printf("%s\n", out);
    return 0;
}
```

编译并运行(在仓库根,利用已构建的对象文件):

```bash
gcc -O2 -std=c99 -I C/128KB-ProTEXT/src demo.c C/128KB-ProTEXT/obj/src/*.o -lm -o demo
./demo
```

**实跑输出**(逐字):

```
lambda[0] = 4.000000
lambda[1] = 3.618034
lambda[2] = 1.381966
y = C₁*exp(-1*x) + C₂*exp(-2*x)
```

特征值是解析精确的:(5±√5)/2 与 4;ODE 通解为 y″+3y′+2y=0 的特征方程解。

### JS(ESM):同一任务,3 行逻辑

```js
// demo.mjs —— core_kernel.js 是零依赖单文件 ESM
import ApolloCalc from "../JS/128KB/src/core_kernel.js";

const M = [2, 1, 0, 1, 3, 0, 0, 0, 4];           // 实对称 3x3,行主序
for (let i = 0; i < 9; i++) { ApolloCalc.matrix.proSetA(3, 3, i, M[i]); }
ApolloCalc.matrix.proEig();                       // 特征值按 |λ| 降序
for (let k = 0; k < ApolloCalc.matrix.proEigCount(); k++) {
  console.log("lambda[" + k + "] =", ApolloCalc.matrix.proEigValue(k).toFixed(6));
}
console.log(ApolloCalc.ode.solveOde("y''+3y'+2y=0").out);
```

```bash
node demo.mjs
```

**实跑输出**(与 C 侧逐字一致):

```
lambda[0] = 4.000000
lambda[1] = 3.618034
lambda[2] = 1.381966
y = C₁*exp(-1*x) + C₂*exp(-2*x)
```

---

## 2. 移植指南

### C99 → Keil MDK / 任意 ARM 工具链

1. 把 `C/128KB-ProTEXT/src/` 下**全部 `.c` 与 `.h`**(37 个文件)加入工程;头文件搜索路径加该目录。
2. 编译选项与仓库 Makefile 逐字一致:`-std=c99 -Wall -Wextra -Wstrict-prototypes -O2`(仓库额外加 `-Werror` 兜底零警告)。源码**不使用**任何编译器扩展、无 VLA、无 `malloc`。
3. 链接数学库(Keil 的 Math 库 / gcc 的 `-lm`):内核用到 `sin/cos/exp/log/sqrt/pow`。
4. 内存预算:内核 `.bss` **42 608 B** + `.data` 2 716 B,栈预留 ≥ 2 KB——全部静态,启动期一次到位,运行期零分配。
5. 验收捷径:同族的 `C/64KB/` 是 **10 576 B `.bss`** 的参考实现,面向 Cortex-M4F / M0+ / ESP32 约定验证过(见 `docs/c99-64kb.md`);移植遇到问题时先对它跑一遍。

> 移植说明:以上 MinGW(gcc 8.1.0)路径在本仓库全量验收过;Keil / ESP-IDF 的工程组织属标准操作,内核代码本身不含任何平台相关调用。

### C99 → ESP-IDF

```cmake
# components/apollocalc/CMakeLists.txt
idf_component_register(SRCS "src/ac_parser.c" ... # 全部 src/*.c
                       INCLUDE_DIRS "src")
```

零堆 + 静态池在启动期一次分配,天然适配 ESP-IDF 的组件模型;不需要 `heap_caps` 配置。

### JS → 你的项目

- **零依赖直接引入**(推荐):把 `JS/128KB/src/core_kernel.js`(128 846 B,压缩后单文件,v2.5.0
  起功能面;v2.6.0 无 JS 变更)拷进工程,
  `import ApolloCalc from "./core_kernel.js"` 即用——它是纯 ESM 默认导出,
  webpack / esbuild / vite 均可直接处理。
- **npm**:仓库当前**未发布 npm 包**。下游两种接法:①`npm install` 时用 `file:` 依赖指向本仓库;
  ②自行对 `core_kernel.js` 做 `npm pack`(核心就是这一个文件,无任何传递依赖)。
- 完整公开 API 见 `JS/128KB/test/build-kernel.js` 的 `EXPORT_BLOCK`(`ode` / `matrix` / `gb` /
  `numtheory` / `symboltree` / `integrate` / `special` 命名空间 + 求值/求解层函数)。

---

## 3. 构建与测试命令(复制粘贴即可跑)

```bash
# C 侧全量验收:43 项 verdict 全过(935 checks,.bss 四口径精确断言,上游零改动双证)
powershell -NoProfile -ExecutionPolicy Bypass -File C/128KB-ProTEXT/verify.ps1

# C 侧快速门禁(bss 账本 + 退化构建)
cd C/128KB-ProTEXT && mingw32-make SHELL=cmd.exe bss-check degraded-check

# JS 侧全量:25 个测试文件
node JS/128KB/test/run-all.js

# JS 内核重建(--minify 是压缩交付物的唯一入口)
node JS/128KB/test/build-kernel.js --minify

# 跨语言一致性对拍(50 例,数值 1e-9 容差)
node tests/bench/diff-smoke128.js

# 100 万次压测(五版本 × 四 stage × 3 轮中位数;含 v2.6.0 pro_runner 列)
bash tests/bench/run-all.sh
```

约定:本机无 `pwsh` 用 Windows 自带 `powershell`;MinGW 环境用 `mingw32-make`(Makefile 配方
按 cmd.exe 语法写,必须带 `SHELL=cmd.exe`)。

---

## 4. 与 SymPy / Eigenmath 的对比(2026-11-03 本机实测)

**口径**:同一台 Windows 11 x64 机器;SymPy 1.14.0(pip 官方源)、Eigenmath master
(georgeweigt/eigenmath,gcc -O2 本机构建)、ApolloCalc v2.4.2 首测。
任务统一为「解 x²+3x+2=0 + 求 3×3 实对称矩阵特征值」。所有数字为本机实测,非转抄;
「交付物体积」JS 行与「常驻内存」JS 行的池值已按 v2.5.0 交付物(2026-11-04 实测)刷新,
**时序数字保持 2026-11-03 实测**。

```
交付物体积(单文件/安装,实测字节数,条长示意)
SymPy        ████████████████████████████████████████  74.0 MB(pip 安装)
Eigenmath    ████████████                              326 329 B(exe;源码单文件 328 652 B)
ApolloCalc C ███████████                               300 266 B(pro_kernel.exe;37 源文件)
ApolloCalc JS ████                                     128 846 B(core_kernel.js 压缩后,v2.5.0)

常驻内存(静态预算 / 对象峰值,实测)
SymPy        ██████████████████████████████  ~34 MB(Python 对象,tracemalloc 峰值)
Eigenmath    ——————————————                  无静态预算(运行期 malloc 自由增长)
ApolloCalc JS ██████████                     175 104 B(全项目加载期静态池,门禁 178 496 B)
ApolloCalc C  ███                            42 608 B(.bss 精确账本)
ApolloCalc C64 █                             10 576 B(.bss,同族完整功能基线)

启动 → 首个结果(含各自运行时启动;同任务)
SymPy        ████████████████████████████████████  ~970 ms(import 0.86 s + 任务 0.11 s)
Eigenmath    ███                                   ~46 ms(进程)
ApolloCalc JS ██                                   9.3 ms(ESM 加载 8.1 + 任务 1.2)
ApolloCalc C  █                                    ~15 ms(进程;纯求解 µs 级,见 BENCH 报告)
```

> **一条叙事线读懂这张图**:ApolloCalc C/64KB 用 **10 576 B(≈10 KB)的静态 RAM**
> 跑出了多项式、方程求解、矩阵、数论等完整 CAS 能力;128KB Pro 层在这个量级上继续
> 叠加符号积分、Groebner 基、一般矩阵特征值、SVD、ODE 通解——这些在 SymPy 里需要
> 74 MB 安装 + ~34 MB 运行内存。不是谁替代谁:SymPy 是全功能桌面符号系统,
> ApolloCalc 解决的是「SAME 能力在 10 KB 级静态内存、µs 级启动下可用」。
> Eigenmath 与我们同属轻量 C 阵营,但它运行期 malloc 自由增长,没有静态预算概念。

**性能数字以压测管线为准(防单点快照漂移)**:ApolloCalc 侧各 stage 的 µs/op 级细目
**不自建口径**,统一引用压测报告 [../tests/bench/BENCH-1M.md](../tests/bench/BENCH-1M.md)
——v2.6.0 终版为五版本(JS/C × 64KB/128KB + ProTEXT pro_runner 列)× 四 stage × 3 轮中位数,
由 `bash tests/bench/run-all.sh` 一键复跑供数,口径说明(含预热丢首轮)随报告成文;
概览表见 [benchmark.md](benchmark.md)。本节图表只保留交付物体积 / 常驻内存 /
启动时序三组快照,其中 ApolloCalc 静态预算两行随组件版本刷新。

---

## 5. 下一步阅读

- [features.md](features.md) —— 四版本 + Pro 层的完整功能矩阵
- [ROADMAP.md](../ROADMAP.md) —— 版本规划与已知问题
- [docs/contracts/](contracts/) —— 跨语言冻结契约(ODE / square-free 输出格式)
- [CONTRIBUTING.md](../CONTRIBUTING.md) —— 测试纪律与门禁文化
- [benchmark.md](benchmark.md) —— 压测口径与四版本对比
