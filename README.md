# ApolloCalc-CAS

> A Family of Zero-GC Computer Algebra Kernels

同一个数学引擎，用 JS 和 C99 两种语言、两种内存预算（64KB / 128KB）分别实现，输出一致，作为极限编程和嵌入式 CAS 的开源参考。JS/64KB 版本已在华为 WATCH FIT 4 鸿蒙应用中完成验证。

## 版本对照

| 组件 | 堆限制 | 语言 | 状态 | 全局版本节点 (Global Tag) | 组件版本 (Component Version) | 功能范围 |
|:---|:---|:---|:---|:---|:---|:---|
| [JS/64KB](JS/64KB/) | 64KB | JavaScript | ✅ 已完成 | v1.0.0 | —（未建轨道） | 解析求值、2-4 次方程、6 元线性方程组、精确形式 |
| [JS/128KB](JS/128KB/) | 128KB | JavaScript | ✅ 活跃 | **v2.4.0** | **v2.4.0** | 对等 JS/64KB + 五分量精确形式/数值微积分/泰勒/有理根/12 元线性方程组 + 多变量多项式与符号求导/矩阵（基础 ≤3×3 + **pro 12×12**）/特殊函数/**数论**（v2.3.0）/**常系数线性齐次 ODE 通解**（v2.3.0）/**符号微分引擎**（表达式树+因式列表+隐函数，v2.4.0）/**符号积分**（128 项表+固定两层，v2.4.0）/**square-free (根,重数)**（v2.4.0）/**结式+2 变量 Groebner 基**（v2.4.0，系数域收窄披露）+ 深度压缩管线（`build-kernel.js --minify`，当前 175 142 → **103 325 B**，−41.0%）+ guardMultivariateNonlinear 接入求解管线，见 [docs/js-128kb.md](docs/js-128kb.md) |
| [C/64KB](C/64KB/) | 64KB | C99 | ✅ 已完成 | v1.0.0 | —（未建轨道） | 对等 JS/64KB + 数论/进制分数/工具层三个 C 独有优势，见 [docs/c99-64kb.md](docs/c99-64kb.md) |
| [C/128KB](C/128KB/) | 128KB | C99 | ✅ 已完成（冻结） | v1.0.0 | —（未建轨道） | 对等 JS/128KB + 大整数/矩阵/特殊函数/符号求导四个 C 独有优势，见 [docs/c99-128kb.md](docs/c99-128kb.md) |
| [C/128KB-ProTEXT](C/128KB-ProTEXT/) | 128KB | C99 | ✅ 活跃 | **v2.4.0** | **v1.6.0** | Pro 层：冻结上游之上叠加多变量 GCD/因式分解/结式、LU 分解、实特征向量、隐函数求导、高次数值求根、**符号积分（128 项表 + 固定两层分部，v2.3.0 增强）**、多变量符号求导/多变量积分/LUT 常量表化、稀疏线性方程组 (n≤32)/任意精度定点小数 (180 位)、2 变量 Groebner 基（M4GB 逐对归约，lex，deg≤4，膨胀熔断）、**单变量有理函数工具 GCD/cancel/apart（v2.3.0）**、**常系数线性齐次 ODE 通解（v2.3.0，契约 [ode-format-v23](docs/contracts/ode-format-v23.md)）**、**一般实矩阵全部特征值（Francis 隐式双移 QR，非对称不再返 −2，v2.4.0）**、**单变量多项式 square-free 分解 (根,重数)（v2.4.0，契约 [sqfree-format-v24](docs/contracts/sqfree-format-v24.md)）**；`.bss` 零增长（内核 42608 B / runner 42640 B / 上游对照 39408 B，pro_runner 574 checks），见设计说明 |

> **双轨制口径（v2.2.0 起）**：「全局版本节点 (Global Tag)」是仓库级发布标签，对应根
> [CHANGELOG.md](CHANGELOG.md) 的条目，一次发布可只覆盖部分组件；「组件版本
> (Component Version)」记录在各组件目录的 `VERSION` 文件，按该组件自身变更独立
> 递进 semver（C 侧 Pro 层新特性按 minor 递进）。**两者是两个口径，不必相等**——
> 本版 C/128KB-ProTEXT 即为全局 v2.4.0 / 组件 v1.6.0。未建轨道的组件标「—」，
> 沿革见根 CHANGELOG；组件级细分记录见各组件 `CHANGELOG.md`。

**当前版本：v2.4.0（2026-10-31，全局 Tag）** — 本轮全局版本节点同时覆盖 C、JS
两侧，主题「符号引擎 + 数值深度」（双 Git worktree 并行：`feat/c-eigen` /
`feat/js-symbol`，square-free 契约先行冻结，收尾组串行收口）：

- **C 侧（C/128KB-ProTEXT，组件 v1.5.0 → v1.6.0）**：两个新数学面——
  **一般实矩阵全部特征值**（`ac_pro_schur.c`，Householder→Hessenberg→
  Francis 隐式双移 QR，非对称输入不再返 −2，可选反迭代实特征向量，
  不收敛绝不输出垃圾）；**单变量多项式 square-free 分解 (根,重数)**
  （`ac_pro_sqfree.c`，整系数 SFF，1..4 精确 @e / 5..8 DK @d，字符串格式
  冻结于 [docs/contracts/sqfree-format-v24.md](docs/contracts/sqfree-format-v24.md)）。
  `.bss` 零增长：内核口径 42 608 B / runner 42 640 B / 上游对照
  39 408 B（`bss-check` 实测逐字命中）；pro_runner **574 checks, 0 failed**
  （488 + 86）；`verify.ps1` **35 passed, 0 failed**。
- **JS 侧（JS/128KB，组件 v2.3.0 → v2.4.0）**：**符号微分引擎**
  （`core_symboltree.js`，RPN→表达式树 + 17 种函数求导 + factor 因式列表 +
  隐函数 dy/dx）；**符号积分**（`core_integrate.js`，积分表 128 项逐项对齐
  C + 固定两层分部）；**square-free (根,重数)**（`core_ratroot.js` 扩展，
  整题 ≥5 → EDEG 成文分叉）；**结式 + 2 变量 Groebner 基**（`core_groebner.js`，
  Sylvester ≤12 阶 + M4GB 1:1 移植；系数域收窄为 Float64 安全整数，
  \|中间量\| > 2^53−1 即 EOVER 熔断——两侧能力边界不对等，不称「对等」）。
  压缩内核重建 49 747 → **103 325 B**（预算 131 072 B，余量 27 747 B），
  静态池 30 112 → 167 908 B（已超 ROADMAP 98 304 B 水位口径；门禁函数
  只计基础内核池，后续 JS 功能落位前应重估水位）；20 测试文件
  **1183 pass, 0 fail**。
- **跨语言契约**：[tests/polynomials.json](tests/polynomials.json) sqfree 区
  （新建）——**70 个双侧实跑零分歧的 square-free 用例冻结**（契约 §10-1
  两层结构：双侧区 64 例 + C 侧区 6 例，整题 5..8 次仅 C 断言期望串、
  JS 断言 −2）；无 knownDeviations。v2.3.0 的
  [tests/ode-solutions.json](tests/ode-solutions.json) 3 条已知偏差仍待双组
  裁决。

详见 [CHANGELOG.md](CHANGELOG.md)（全局口径）与
[C/128KB-ProTEXT/CHANGELOG.md](C/128KB-ProTEXT/CHANGELOG.md) /
[JS/128KB/CHANGELOG.md](JS/128KB/CHANGELOG.md)（组件口径）。

## 设计哲学

- **零 GC 铁律**：JS 版禁止创建临时对象，C 版禁止 malloc/free
- **扁平化数据**：所有计算数据用 TypedArray（JS）或静态数组（C），不用对象/树
- **数值采样法**：多项式系数提取用采样 + 高斯消元，避免符号展开
- **功能边界**：4 次以下方程（阿贝尔-鲁菲尼定理），6 元以下线性方程组
- **编译器级内存重叠**（v1.1.0）：C 128KB 用 C99 `union` 把 20 个互斥 scratch 池折叠到同一块存储，
  `.bss` 从 57.64 KiB 压到 **38.48 KiB（39.4 KB，−33.24%）**——零运行时开销、零 API 变更

## 内存预算（实测 `.bss`）

| 版本 | `.bss` | 水位门禁 | 占用率 |
|:---|---:|---:|---:|
| JS 64KB | —（TypedArray 静态池 5.4 KB） | 64KB | — |
| JS 128KB | —（TypedArray 静态池 167 908 B ≈ 164.0 KB，含 multivar/matrix pro/numtheory/ode/符号微分/符号积分/结式与 Groebner/sqfree；已超 ROADMAP 98 304 B 水位口径——门禁函数只计基础内核池，见 CHANGELOG） | 128KB | — |
| C 64KB | **10,576 B（10.33 KiB）** | 48 KB | 21.5% |
| C 128KB | **39,408 B（38.48 KiB / 39.4 KB）** | 96 KB | **40.1%** |
| C 128KB-ProTEXT | **42,608 B（41.61 KiB）** | 112 KiB | **37.15%** |

> C 128KB 的 v1.1.0 极简内存数据：**39.4 KB `.bss`，比 v1.0.0 的 57.64 KB 少 19,616 B（−33.24%）**，
> `.data + .bss` 合计 39,680 B（38.75 KiB）。详见 [C/128KB/README.md](C/128KB/README.md) 与
> 设计说明。

## 目录结构

```
ApolloCalc-CAS/
├── README.md            本文件
├── LICENSE              MIT
├── docs/                各版本设计文档
├── tests/               四版本共享的一致性测试用例（JSON）
├── JS/
│   ├── 64KB/            已验证内核（src + test + 说明）
│   └── 128KB/           功能增强版（v2.4.0：+ 符号微分引擎/符号积分/square-free/
│                        结式与 Groebner，20 文件测试，内核 103 325 B，见 docs/js-128kb.md）
└── C/
    ├── 64KB/            C99 对等实现（已完成 v1.0.0）
    ├── 128KB/           C99 增强版（已完成 v1.0.0，冻结）
    └── 128KB-ProTEXT/   C99 Pro 层（v2.4.0）：冻结上游内核，叠加多变量 GCD /
                         有理根因式分解 / Sylvester 结式 / LU 分解 / 实特征向量
                         (Hotelling deflation) / 隐函数求导 / 高次数值求根 /
                         符号积分 (128 项表 + 固定两层分部) / 多变量符号求导 /
                         多变量积分 / LUT 常量表化 / 稀疏线性方程组 (n≤32) /
                         任意精度定点小数 (180 位) / 2 变量 Groebner 基 (M4GB) /
                         单变量有理函数工具 (GCD/cancel/apart) /
                         常系数线性齐次 ODE 通解 /
                         一般实矩阵全部特征值 (Francis 隐式双移 QR) /
                         单变量多项式 square-free 分解 (根,重数)；
                         `.bss` 42608 B，上游零改动
```

## 快速开始

### JS（Node ≥ 14，零依赖）

```bash
cd JS/64KB
node test/test-evaluator.js    # 求值层冒烟测试
node test/test-solvers.js      # 求解层冒烟测试
node test/build-kernel.js      # 重新拼接 core_kernel.js 并跑 80 项全量回归
```

### JS 128KB（v2.4.0，含多变量引擎与数论/矩阵 pro/ODE/符号引擎）

```bash
cd JS/128KB
node test/test-multivar.js     # 多变量多项式 + 符号求导 + 高阶三角/复数（194 项）
node test/test-numtheory.js    # 数论 gcd/lcm/素性/质因数分解/组合数（89 项）
node test/test-matrix-pro.js   # 矩阵 12×12 pro 层 det/inv/rank/LU/eig/QR（82 项）
node test/test-ode.js          # 常系数线性齐次 ODE 通解（72 项）
node test/test-symboltree.js   # 符号微分引擎：表达式树 + 因式列表 + 隐函数（116 项）
node test/test-integrate.js    # 符号积分：128 项表 + 固定两层分部（75 项）
node test/test-sqfree.js       # square-free (根,重数)：契约逐字（70 项）
node test/test-groebner.js     # 结式 + 2 变量 Groebner 基（60 项）
node test/run-all.js           # 20 文件全量：1183 pass / 0 fail
node test/build-kernel.js      # 拼接 + 83 项回归 + ESM 冒烟（--minify 重建压缩内核）
```

### C99（MinGW / GCC，零依赖）

```bash
cd C/64KB
mingw32-make            # 编译 test_runner（-Werror 零警告门禁）
./test_runner.exe       # 565 项测试全绿 + 零堆分配验证
mingw32-make LUT=1      # 三角查表构建（578 项测试）
mingw32-make smoke_runner && ./smoke_runner.exe > ../../tests/bench/c-smoke.csv
node ../../tests/bench/js-smoke.js  > ../../tests/bench/js-smoke.csv
node ../../tests/bench/diff-smoke.js    # C vs JS 跨版本比对 → SMOKE-DIFF.md
```

### C99 Pro（v2.4.0，特征值 + square-free + 积分增强 + 有理函数工具 + ODE）

```bash
cd C/128KB-ProTEXT
mingw32-make            # 编译 pro_runner（cmd 直接可用；Git Bash 下加 SHELL=cmd.exe）
./pro_runner.exe        # 574 项测试全绿（含 schur 特征值/sqfree/ODE/有理函数工具/积分增强）+ Zero-GC --wrap 验证
mingw32-make bss-check  # .bss 三门禁精确命中：42608 / 42640 / 39408
```

## 测试

`tests/` 下的共享 JSON 用例是跨实现的一致性契约：`polynomials.json`（数值求根；v2.4.0 起增补 sqfree (根,重数) 契约区——70 例双侧实跑零分歧，含双侧区 64 + C 侧区 6）、`exact-forms.json`（精确形式）、`edge-cases.json`（边界与错误码）、`ode-solutions.json`（ODE 通解逐字契约，v2.3.0 新建，含 3 条 knownDeviations 如实上报）。任何语言、任何堆预算的实现都必须对同一输入给出一致结果。

## 性能

100 万次混合表达式压测（每 stage 遍历 50–250 条不同表达式，3 轮取中位数；单操作平均耗时 µs）：

| stage | JS 64KB | JS 128KB | C 64KB | C 128KB |
|:---|---:|---:|---:|---:|
| 解析 | 0.040 µs | 0.040 µs | 0.019 µs | 0.019 µs |
| 求值 | 0.126 µs | 0.122 µs | 0.063 µs | 0.060 µs |
| 求解 | 0.692 µs | 1.227 µs | 0.353 µs | 0.435 µs |
| 精确形式 | 0.397 µs | 0.664 µs | 0.200 µs | 0.252 µs |

**读法**：

- C 版比**同预算**的 JS 版快 **2–3×**（C64/JS64 为 2.0–2.1×；C128/JS128 为 2.0–2.9×，
  功能越复杂差距越大）
- 128KB 的 solve/exact 慢是**为更强的精确形式付费**（五分量 vs 三分量）：JS 同族 +77% / +67%，
  C 同族仅 +23% / +26%——C 版该开销明显更小
- 解析与求值几乎不受版本影响（同族差异 ≤5.3%），池从 220 扩到 250 条对单次操作无可测影响

**v1.1.0 内存压缩后无性能回归**（C 128KB，v1.0.0 → v1.1.0，同机同编译器，16 次采样中位数）：

| stage | v1.0.0 | v1.1.0 | 变化 | 判定 |
|:---|---:|---:|---:|:---|
| parse | 0.0190 µs | 0.0191 µs | +0.53% | ✅ |
| eval | 0.0624 µs | 0.0635 µs | +1.92% | ✅ |
| solve | 0.4586 µs | 0.4441 µs | **−3.15%** | ✅ |
| exact | 0.2607 µs | 0.2533 µs | **−2.84%** | ✅ |
| **均值** | **0.8007 µs** | **0.7801 µs** | **−2.57%** | ✅ |

四个 stage 全部落在 ±3% 噪声带内；solve/exact 有小幅提速（工作集收缩 → 缓存局部性改善）。

> **口径注记（v2.1.0）**：本表描述四基础版本（JS/C99 × 64KB/128KB）。Pro 层与
> JS v2.0.0 起新增能力（多变量求导/积分、矩阵、特殊函数、稀疏求解、定点小数）
> 尚未纳入 100 万次压测口径，无实测数据即不列数字；C 侧 Pro 层带 `.bss` 精确
> 相等门禁（当前 42 608 B，见 [ACCEPTANCE.md](docs/ACCEPTANCE.md)）。

> 口径：每 stage 独立 100 万次，池大小按版本门控——64KB 两版过滤掉 30 条 128KB 专属双曲条目，
> 故 parse/eval 为 220 条，128KB 两版为 250 条；solve 100 条、exact 50 条四版本相同。
> 「求值」列为**解析 + 求值合计**（池内表达式各异，环内须重新解析），非纯求值耗时。

详见 [tests/bench/BENCH-1M.md](tests/bench/BENCH-1M.md)，复现见 `tests/bench/run-all.sh`。

## 许可

[MIT](LICENSE) © 2026 ApolloCalc-CAS Contributors
