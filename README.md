# ApolloCalc-CAS

> A Family of Zero-GC Computer Algebra Kernels

同一个数学引擎，用 JS 和 C99 两种语言、两种内存预算（64KB / 128KB）分别实现，输出一致，作为极限编程和嵌入式 CAS 的开源参考。JS/64KB 版本已在华为 WATCH FIT 4 鸿蒙应用中完成验证。

## 版本对照

| 组件 | 堆限制 | 语言 | 状态 | 全局版本节点 (Global Tag) | 组件版本 (Component Version) | 功能范围 |
|:---|:---|:---|:---|:---|:---|:---|
| [JS/64KB](JS/64KB/) | 64KB | JavaScript | ✅ 已完成 | v1.0.0 | —（未建轨道） | 解析求值、2-4 次方程、6 元线性方程组、精确形式 |
| [JS/128KB](JS/128KB/) | 128KB | JavaScript | ✅ 活跃 | **v2.5.0** | **v2.5.0** | 对等 JS/64KB + 五分量精确形式/数值微积分/泰勒/有理根/12 元线性方程组 + 多变量多项式与符号求导/矩阵（基础 ≤3×3 + **12×12 pro 层** + **一般实矩阵全部特征值 proSchur**）/特殊函数/数论/常系数线性齐次 ODE 通解（v2.3.0）/符号微分/符号积分/square-free 分解/结式与 Groebner 基（v2.4.0）/**复域超越函数库/方程组 GB 消元数值全解/Sturm 实根隔离 ≤4 次**（v2.5.0；SVD 与纯 JS 大整数因压缩内核触熔断线顺延 v2.7，见根 CHANGELOG v2.6.0）+ 压缩内核构建（**126 KB**）+ 全池水位门禁，见 [docs/js-128kb.md](docs/js-128kb.md) |
| [C/64KB](C/64KB/) | 64KB | C99 | ✅ 已完成 | v1.0.0 | v1.0.0 | 对等 JS/64KB + 数论/进制分数/工具层三个 C 独有优势，见 [docs/c99-64kb.md](docs/c99-64kb.md) |
| [C/128KB](C/128KB/) | 128KB | C99 | ✅ 已完成（冻结） | v1.1.0 | v1.1.0 | 对等 JS/128KB + 大整数/矩阵/特殊函数/符号求导四个 C 独有优势，见 [docs/c99-128kb.md](docs/c99-128kb.md) |
| [C/128KB-ProTEXT](C/128KB-ProTEXT/) | 128KB | C99 | ✅ 活跃 | **v2.6.0** | **v1.8.0** | Pro 层：冻结上游之上叠加多变量 GCD/因式分解/结式、LU 分解、实特征向量、隐函数求导、高次数值求根、符号积分（128 项表）、多变量符号求导/多变量积分、稀疏线性方程组 (n≤32)/任意精度定点小数 (180 位)、2 变量 Groebner 基、单变量有理函数工具、常系数线性齐次 ODE 通解、**一般实矩阵全部特征值与特征向量（支持非对称，v2.4.0）**、**square-free 分解与重根重数（v2.4.0）**、**复域超越函数库（15 API）/方程组 GB 消元数值全解/Sturm 实根计数与隔离 1..8 次（v2.5.0）**、**one-sided Jacobi SVD 奇异值分解（m,n≤12，C 独有，v2.6.0）**；内核静态内存 42608 B，pro_runner 935 项测试 |

> **双轨制口径（v2.2.0 起）**：「全局版本节点 (Global Tag)」是仓库级发布标签，对应根
> [CHANGELOG.md](CHANGELOG.md) 的条目，一次发布可只覆盖部分组件；「组件版本
> (Component Version)」记录在各组件目录的 `VERSION` 文件，按该组件自身变更独立
> 递进 semver（C 侧 Pro 层新特性按 minor 递进）。**两者是两个口径，不必相等**：
> v2.6.0 全局节点只覆盖 C 侧（JS 侧本版无功能变更），故 JS/128KB 两列保持全局
> v2.5.0 / 组件 v2.5.0，C/128KB-ProTEXT 为全局 v2.6.0 / 组件 v1.8.0。未建轨道的
> 组件标「—」（仅 JS/64KB），沿革见根 CHANGELOG；C/64KB 与 C/128KB 的组件版本
> 冻结于 v1.0.0 / v1.1.0，不再递进；组件级细分记录见各组件 `CHANGELOG.md`。

**当前版本：v2.6.0（2026-11-04，全局 Tag，只覆盖 C 侧）** — 应用落地·纯数学（v2.6 实际交付收窄为数值线性代数 + 基准终版）。

- **one-sided Jacobi SVD 奇异值分解**（C 独有，ProTEXT v1.8.0）：m,n≤12 实矩阵
  （m<n 内部转置），奇异值降序 + U/V 出口、符号规范化与双闸门回乘验证；
  不收敛 / 超域按错误码诚实拒绝，绝不输出未验证结果。svd 视图 3760 B 在
  3848 B 吸收线内被 union 完全吸收，内核静态内存 42608 B 零增长。
  **JS 侧 SVD 面与纯 JS 大整数（G4'）因压缩内核触及熔断线（128 846 B >
  126 976 B）按降载序顺延 v2.7**，见 [ROADMAP-v2.5-v2.6.md](ROADMAP-v2.5-v2.6.md)
  附录「v2.6 执行记录」。
- **基准口径终版**：100 万次压测扩为五列（新增 pro_runner 列，四 stage 口径不变，
  预热丢首轮口径成文），Pro 层对上游四 stage 零可测回归（Pro/C128 =
  0.99/0.91/1.02/1.04，抖动带内）；见 [tests/bench/BENCH-1M.md](tests/bench/BENCH-1M.md)。
- JS 侧本版零功能变更（压缩内核 128 846 B、1563 pass / 25 文件不变）。
- 三关门禁：C verify.ps1 43 项 verdict 全绿（pro_runner 935 checks）/
  JS run-all 1563 pass（25 文件）/ diff-smoke 50/50。

详见 [CHANGELOG.md](CHANGELOG.md)（全局口径）与
[C/128KB-ProTEXT/CHANGELOG.md](C/128KB-ProTEXT/CHANGELOG.md) /
[JS/128KB/CHANGELOG.md](JS/128KB/CHANGELOG.md)（组件口径）。

## 十分钟接入

**C99**（5 行逻辑，零依赖零 malloc）：

```c
double a[9] = {2, 1, 0, 1, 3, 0, 0, 0, 4};   /* 实对称 3x3,行主序 */
ac_pro_eig_all(a, 3);                         /* 特征值按 |λ| 降序 */
for (int k = 0; k < ac_pro_eig_count(); k++)
    printf("lambda[%d] = %.6f\n", k, ac_pro_eig_value(k));
char out[256];
ac_pro_ode("y''+3y'+2y=0", out, sizeof out); /* → y = C₁*exp(-1*x) + C₂*exp(-2*x) */
```

**JS**（ESM 单文件，零依赖）：

```js
import ApolloCalc from "./core_kernel.js";
for (let i = 0; i < 9; i++) ApolloCalc.matrix.proSetA(3, 3, i, [2,1,0,1,3,0,0,0,4][i]);
ApolloCalc.matrix.proEig();
console.log(ApolloCalc.ode.solveOde("y''+3y'+2y=0").out); // y = C₁*exp(-1*x) + C₂*exp(-2*x)
```

**复制粘贴即可跑的完整步骤**（编译命令 / 移植 Keil·ESP-IDF / npm 引入）见
[docs/getting-started.md](docs/getting-started.md)。

## 与 SymPy / Eigenmath 的实测对比

同一台 Windows 11 x64、同一任务（解 x²+3x+2=0 + 3×3 实对称特征值），
2026-11-03 实测（SymPy 1.14.0、Eigenmath master 本机构建，禁转抄）；
「交付物体积」JS 行与「常驻内存」JS 行的池值已按 v2.5.0 交付物（2026-11-04
实测）更新，时序数字保持 2026-11-03 实测：

```
交付物体积
SymPy        ████████████████████████████████████████  74.0 MB（pip 安装）
Eigenmath    ████████████                              326 329 B（exe）
ApolloCalc C ███████████                               300 266 B（pro_kernel.exe）
ApolloCalc JS █████                                    128 846 B（core_kernel.js，v2.5.0）

常驻内存（静态预算 / 对象峰值）
SymPy        ██████████████████████████████  ~34 MB（Python 对象峰值）
Eigenmath    ——————————————                  无静态预算（malloc 自由增长）
ApolloCalc JS ██████████                     175 104 B（v2.5.0 全池口径，门禁 178 496 B）
ApolloCalc C  ███                            42 608 B（.bss 精确账本）
ApolloCalc C64 █                             10 576 B（.bss，完整功能基线）

启动 → 首个结果（含各自运行时启动）
SymPy        ████████████████████████████████████  ~970 ms（import 0.86 s）
Eigenmath    ███                                   ~46 ms
ApolloCalc JS ██                                   9.3 ms（加载 8.1 + 任务 1.2）
ApolloCalc C  █                                    ~15 ms（进程；纯求解 µs 级）
```

**一句话**：ApolloCalc C/64KB 用 **10 576 B（≈10 KB）静态 RAM** 跑出完整 CAS 能力；
128KB Pro 层在同级内存上继续叠加符号积分、Groebner 基、一般矩阵特征值、SVD 与 ODE 通解——
同类能力在 SymPy 中需要 74 MB 安装 + ~34 MB 运行内存。不是替代关系：
SymPy 面向全功能桌面符号计算，ApolloCalc 解决「同样能力在 10 KB 级静态内存、
µs 级启动下可用」。完整口径与实测方法见 [docs/getting-started.md](docs/getting-started.md)。

## 设计哲学

- **零 GC 铁律**：JS 版禁止创建临时对象，C 版禁止 malloc/free
- **扁平化数据**：所有计算数据用 TypedArray（JS）或静态数组（C），不用对象/树
- **数值采样法**：多项式系数提取用采样 + 高斯消元，避免符号展开
- **功能边界**：4 次以下方程（阿贝尔-鲁菲尼定理），6 元以下线性方程组
- **编译器级内存重叠**（v1.1.0）：C 128KB 用 C99 `union` 把 20 个互斥 scratch 池折叠到同一块存储，
  `.bss` 从 57.64 KiB 压到 **38.48 KiB（39.4 KB，−33.24%）**：零运行时开销、零 API 变更

## 内存预算（实测 `.bss`）

| 版本 | `.bss` | 水位门禁 | 占用率 |
|:---|---:|---:|---:|
| JS 64KB | —（TypedArray 静态池 5.4 KB） | 64KB | — |
| JS 128KB | —（TypedArray 静态池 **175104 B ≈ 171.0 KiB**，v2.5.0 全池口径，含 multivar/matrix pro/numtheory/ode/符号微分/符号积分/结式与 Groebner/sqfree/复变/方程组/Sturm/特征值；**v2.5.0 起全池水位门禁线 178496 B**，逐模块账本见 [tests/js-pool-ledger.json](tests/js-pool-ledger.json)） | 128KB | — |
| C 64KB | **10576 B（10.33 KiB）** | 48 KB | 21.5% |
| C 128KB | **39408 B（38.48 KiB / 39.4 KB）** | 96 KB | **40.1%** |
| C 128KB-ProTEXT | **42608 B（41.61 KiB）** | 112 KiB | **37.15%** |

> C 128KB 的 v1.1.0 极简内存数据：**39.4 KB `.bss`，比 v1.0.0 的 57.64 KB 少 19616 B（−33.24%）**，
> `.data + .bss` 合计 39680 B（38.75 KiB）。详见 [C/128KB/README.md](C/128KB/README.md) 与
> [docs/c99-128kb.md](docs/c99-128kb.md)。

## 目录结构

```
ApolloCalc-CAS/
├── README.md            本文件
├── LICENSE              MIT
├── docs/                各版本设计文档
├── tests/               四版本共享的一致性测试用例（JSON）
├── JS/
│   ├── 64KB/            已验证内核（src + test + 说明）
│   └── 128KB/           功能增强版（v2.5.0：v2.4.0 四块之上再 + 复域超越函数库/
│                        方程组 GB 消元数值全解/Sturm 实根隔离 ≤4 次/一般实矩阵
│                        特征值，25 文件测试，内核 128846 B，见 docs/js-128kb.md）
└── C/
    ├── 64KB/            C99 对等实现（已完成 v1.0.0）
    ├── 128KB/           C99 增强版（已完成 v1.1.0，冻结）
    └── 128KB-ProTEXT/   C99 Pro 层（组件 v1.8.0）：冻结上游内核，叠加多变量 GCD /
                         因式分解 / 结式 / LU 分解 / 实特征向量 / 隐函数求导 /
                         高次数值求根 / 符号积分 (128 项表) / 多变量符号求导 /
                         多变量积分 / 稀疏线性方程组 (n≤32) /
                         任意精度定点小数 (180 位) / 2 变量 Groebner 基 /
                         单变量有理函数工具 / 常系数线性齐次 ODE 通解 /
                         一般实矩阵全部特征值 / square-free 分解 (根,重数) /
                         复域超越函数库 / 方程组 GB 消元数值全解 /
                         Sturm 实根计数与隔离 (1..8 次) /
                         one-sided Jacobi SVD (m,n≤12，C 独有)；
                         内核静态内存 42608 B，上游零改动
```

## 快速开始

### JS（Node ≥ 14，零依赖）

```bash
cd JS/64KB
node test/test-evaluator.js    # 求值层冒烟测试
node test/test-solvers.js      # 求解层冒烟测试
node test/build-kernel.js      # 重新拼接 core_kernel.js 并跑 80 项全量回归
```

### JS 128KB（v2.5.0，含复域超越函数库/方程组消元/Sturm/一般矩阵特征值）

```bash
cd JS/128KB
node test/test-multivar.js     # 多变量多项式 + 符号求导 + 高阶三角/复数（194 项）
node test/test-numtheory.js    # 数论 gcd/lcm/素性/质因数分解/组合数（89 项）
node test/test-matrix-pro.js   # 矩阵 12×12 pro 层 det/inv/rank/LU/eig/QR（82 项）
node test/test-matrix-eig.js   # 一般实矩阵全部特征值 proSchur（93 项）
node test/test-ode.js          # 常系数线性齐次 ODE 通解（83 项）
node test/test-symboltree.js   # 符号微分引擎：表达式树 + 因式列表 + 隐函数（116 项）
node test/test-integrate.js    # 符号积分：128 项表 + 固定两层分部（75 项）
node test/test-sqfree.js       # square-free (根,重数)（70 项）
node test/test-groebner.js     # 结式 + 2 变量 Groebner 基 + 方程组 sysolve（60+40 项）
node test/test-complex-ext.js  # 复域超越函数库 15 API（132 项）
node test/test-sturm.js        # Sturm 实根计数与隔离 ≤4 次（68 项）
node test/test-watermark.js    # F0 全池水位门禁自检（36 项）
node test/run-all.js           # 25 文件全量：1563 pass / 0 fail
node test/build-kernel.js      # 拼接 + 83 项回归 + ESM 冒烟（--minify 重建压缩内核）
```

### C99（MinGW / GCC，零依赖）

```bash
cd C/64KB
mingw32-make            # 编译 test_runner（-Werror 零警告门禁）
./test_runner.exe       # 565 项测试 + 零堆分配验证
mingw32-make LUT=1      # 三角查表构建（578 项测试）
mingw32-make smoke_runner && ./smoke_runner.exe > ../../tests/bench/c-smoke.csv
node ../../tests/bench/js-smoke.js  > ../../tests/bench/js-smoke.csv
node ../../tests/bench/diff-smoke.js    # C vs JS 跨版本比对 → SMOKE-DIFF.md
```

### C99 Pro（v2.6.0，复域超越函数 + 方程组消元 + Sturm + 特征值 + square-free + ODE + SVD）

```bash
cd C/128KB-ProTEXT
mingw32-make            # 编译 pro_runner（cmd 直接可用；Git Bash 下加 SHELL=cmd.exe）
./pro_runner.exe        # 935 项测试（含方程组消元/Sturm 实根隔离/特征值/square-free/ODE/有理函数工具/积分增强/SVD 16 例）+ Zero-GC --wrap 验证
mingw32-make bss-check  # .bss 三门禁精确命中：42608 / 42640 / 39408
```

## 测试

`tests/` 下的共享 JSON 用例是跨实现的一致性契约：`polynomials.json`（数值求根 + v2.4.0 新增 square-free (根,重数) 契约区 70 例）、`exact-forms.json`（精确形式）、`edge-cases.json`（边界与错误码）、`ode-solutions.json`（ODE 通解契约，v2.3.0 新建，knownDeviations 已于 v2.4.1 清零）、以及 v2.5.0 新建的 `complex-functions.json`（复变函数）、`systems.json`（方程组解列表）、`sturm.json`（dual / cOnly-DEG / cOnly-OVF 三层）、`eigen-cases.json`（一般实矩阵特征值 47 例）。任何语言、任何堆预算的实现都必须对同一输入给出一致结果；能力不对等处按契约分叉区成文，不称「对等」。

## 性能

100 万次混合表达式压测（每 stage 遍历 50–250 条不同表达式，预热丢首轮后 3 轮取中位数；
单操作平均耗时 µs；**v2.6.0 起五列口径，含 ProTEXT pro_runner 列**）：

| stage | JS 64KB | JS 128KB | C 64KB | C 128KB | C 128KB-ProTEXT |
|:---|---:|---:|---:|---:|---:|
| 解析 | 0.041 µs | 0.039 µs | 0.019 µs | 0.019 µs | 0.019 µs |
| 求值 | 0.125 µs | 0.114 µs | 0.063 µs | 0.061 µs | 0.056 µs |
| 求解 | 0.687 µs | 1.205 µs | 0.358 µs | 0.428 µs | 0.434 µs |
| 精确形式 | 0.396 µs | 0.649 µs | 0.200 µs | 0.246 µs | 0.256 µs |

**读法**：

- C 版比**同预算**的 JS 版快 **2–3×**（C64/JS64 为 1.9–2.1×；C128/JS128 为 1.7–2.9×，
  功能越复杂差距越大）
- Pro 层与上游 C/128KB 是同一批上游 TU，四 stage 对比 Pro/C128 = 0.99/0.91/1.02/1.04
  全部落在采样抖动带内——**Pro 层对基础四 stage 零可测回归**
- 128KB 的 solve/exact 慢是**为更强的精确形式付费**（五分量 vs 三分量）：JS 同族 +75% / +64%，
  C 同族仅 +19% / +23%；C 版该开销明显更小
- 解析与求值几乎不受版本影响（同族差异 ≤9%），池从 220 扩到 250 条对单次操作无可测影响

**v1.1.0 内存压缩后无性能回归**（C 128KB，v1.0.0 → v1.1.0，同机同编译器，16 次采样中位数）：

| stage | v1.0.0 | v1.1.0 | 变化 | 判定 |
|:---|---:|---:|---:|:---|
| parse | 0.0190 µs | 0.0191 µs | +0.53% | ✅ |
| eval | 0.0624 µs | 0.0635 µs | +1.92% | ✅ |
| solve | 0.4586 µs | 0.4441 µs | **−3.15%** | ✅ |
| exact | 0.2607 µs | 0.2533 µs | **−2.84%** | ✅ |
| **均值** | **0.8007 µs** | **0.7801 µs** | **−2.57%** | ✅ |

四个 stage 全部落在 ±3% 噪声带内；solve/exact 有小幅提速（工作集收缩 → 缓存局部性改善）。

> **口径注记（v2.6.0）**：本表为五列口径（v2.6.0 G3 起 Pro 层经 pro_runner 列纳入压测）。
> Pro 层新增能力（多变量求导/积分、矩阵、特殊函数、稀疏求解、定点小数、SVD 等）
> 不在压测四 stage 的直接测项内——四 stage 测的是两版共有的上游基础面，Pro 列的意义
> 是证明叠加 Pro 层后这些基础面零可测回归；C 侧 Pro 层带 `.bss` 精确相等门禁
> （当前 42608 B，见 [ACCEPTANCE.md](docs/ACCEPTANCE.md)）。完整报告与预热丢首轮口径：
> [tests/bench/BENCH-1M.md](tests/bench/BENCH-1M.md)。

> 口径：每 stage 独立 100 万次，池大小按版本门控：64KB 两版过滤掉 30 条 128KB 专属双曲条目，
> 故 parse/eval 为 220 条，128KB 两版为 250 条；solve 100 条、exact 50 条四版本相同。
> 「求值」列为**解析 + 求值合计**（池内表达式各异，环内须重新解析），非纯求值耗时。

详见 [tests/bench/BENCH-1M.md](tests/bench/BENCH-1M.md)，复现见 `tests/bench/run-all.sh`。

## 许可

[MIT](LICENSE) © 2026 ApolloCalc-CAS Contributors
