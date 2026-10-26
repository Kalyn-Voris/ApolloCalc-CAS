# ApolloCalc-CAS

> A Family of Zero-GC Computer Algebra Kernels

同一个数学引擎，用 JS 和 C99 两种语言、两种内存预算（64KB / 128KB）分别实现，输出一致，作为极限编程和嵌入式 CAS 的开源参考。JS/64KB 版本已在华为 WATCH FIT 4 鸿蒙应用中完成验证。

## 版本对照

| 版本 | 堆限制 | 语言 | 状态 | 功能范围 |
|---|---|---|---|---|
| [JS/64KB](JS/64KB/) | 64KB | JavaScript | ✅ 已完成 | 解析求值、2-4 次方程、6 元线性方程组、精确形式 |
| [JS/128KB](JS/128KB/) | 128KB | JavaScript | ✅ v2.0.0 | 对等 JS/64KB + 五分量精确形式/数值微积分/泰勒/有理根/12 元线性方程组 + **多变量多项式与符号求导/矩阵基础运算/特殊函数**，见 [docs/js-128kb.md](docs/js-128kb.md) |
| [C/64KB](C/64KB/) | 64KB | C99 | ✅ 已完成 | 对等 JS/64KB + 数论/进制分数/工具层三个 C 独有优势，见 [docs/c99-64kb.md](docs/c99-64kb.md) |
| [C/128KB](C/128KB/) | 128KB | C99 | ✅ 已完成 | 对等 JS/128KB + 大整数/矩阵/特殊函数/符号求导四个 C 独有优势，见 [docs/c99-128kb.md](docs/c99-128kb.md) |
| [C/128KB-ProTEXT](C/128KB-ProTEXT/) | 128KB | C99 | ✅ v2.0.0 | Pro 层：冻结上游之上叠加多变量 GCD/因式分解/结式、LU 分解、实特征向量、隐函数求导、高次数值求根、符号积分、**多变量符号求导/多变量积分/LUT 常量表化**，`.bss` 41456 B，见设计说明 |

**当前版本：v2.0.0（2026-10-26）** — 双侧同步的多变量引擎升级：

- **C 侧（C/128KB-ProTEXT）**：多变量符号求导（`ac_pro_diff_multi`）与多变量
  多项式积分（`ac_pro_integrate_multi`，精确整除约束）以**零 `.bss` 增长**落地
  （复用 mv 项池槽 0/1）；sin/cos LUT 经包装层迁为 `const` 预计算表（`.bss`
  迁出收益 16 416 B，门禁 41456 B 不变）。281 项测试全绿，退化对拍 38672 B。
- **JS 侧（JS/128KB）**：`core_multivar.js`（194 项测试）+ 新增 `core_matrix.js`
  （39 项测试）与 `core_special.js`（33 项测试，erf/gamma/lgamma 零分配），
  12 文件内核拼接 + 浏览器经典脚本通道验证；12 个测试文件 603 pass, 0 fail。

详见 [CHANGELOG.md](CHANGELOG.md)。

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
| JS 128KB | —（TypedArray 静态池 22.4 KB，含 multivar/matrix） | 128KB | — |
| C 64KB | **10,576 B（10.33 KiB）** | 48 KB | 21.5% |
| C 128KB | **39,408 B（38.48 KiB / 39.4 KB）** | 96 KB | **40.1%** |
| C 128KB-ProTEXT | **41,456 B（40.48 KiB）** | 112 KiB | **36.15%** |

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
│   └── 128KB/           功能增强版（v2.0.0：+ 多变量多项式/符号求导/矩阵/特殊函数，
│                        12 文件内核，见 docs/js-128kb.md）
└── C/
    ├── 64KB/            C99 对等实现（已完成 v1.0.0）
    ├── 128KB/           C99 增强版（已完成 v1.0.0，冻结）
    └── 128KB-ProTEXT/   C99 Pro 层（v2.0.0）：冻结上游内核，叠加多变量 GCD /
                         有理根因式分解 / Sylvester 结式 / LU 分解 / 实特征向量
                         (Hotelling deflation) / 隐函数求导 / 高次数值求根 /
                         符号积分 / 多变量符号求导 / 多变量积分 /
                         LUT 常量表化；`.bss` 41456 B，上游零改动
```

## 快速开始

### JS（Node ≥ 14，零依赖）

```bash
cd JS/64KB
node test/test-evaluator.js    # 求值层冒烟测试
node test/test-solvers.js      # 求解层冒烟测试
node test/build-kernel.js      # 重新拼接 core_kernel.js 并跑 80 项全量回归
```

### JS 128KB（v2.0.0，含多变量引擎）

```bash
cd JS/128KB
node test/test-multivar.js     # 多变量多项式 + 符号求导 + 高阶三角/复数（194 项）
node test/test-matrix.js       # 矩阵基础运算（39 项）
node test/test-special.js      # 特殊函数 erf/gamma/lgamma（33 项）
node test/build-kernel.js      # 12 文件拼接 + 83 项回归 + ESM 43 项冒烟
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

### C99 Pro（v2.0.0，多变量引擎 + 零 `.bss` 增长）

```bash
cd C/128KB-ProTEXT
mingw32-make            # 编译 pro_runner（cmd 直接可用；Git Bash 下加 SHELL=cmd.exe）
./pro_runner.exe        # 281 项测试全绿（含多变量求导/积分）+ Zero-GC --wrap 验证
mingw32-make bss-check  # .bss 三门禁精确命中：41456 / 41488 / 39408
```

## 测试

`tests/` 下的三份 JSON 用例是四版本共享的一致性测试：`polynomials.json`（数值求根）、`exact-forms.json`（精确形式）、`edge-cases.json`（边界与错误码）。任何语言、任何堆预算的实现都必须对同一输入给出一致结果。

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

> **口径注记（v2.0.0）**：本表描述四基础版本（JS/C99 × 64KB/128KB）。Pro 层与
> JS v2.0.0 新增能力（多变量求导/积分、矩阵、特殊函数）尚未纳入 100 万次压测
> 口径，无实测数据即不列数字；C 侧新增功能带零 `.bss` 增长门禁（41456 B，
> 见 [ACCEPTANCE.md](docs/ACCEPTANCE.md)）。

> 口径：每 stage 独立 100 万次，池大小按版本门控——64KB 两版过滤掉 30 条 128KB 专属双曲条目，
> 故 parse/eval 为 220 条，128KB 两版为 250 条；solve 100 条、exact 50 条四版本相同。
> 「求值」列为**解析 + 求值合计**（池内表达式各异，环内须重新解析），非纯求值耗时。

详见 [tests/bench/BENCH-1M.md](tests/bench/BENCH-1M.md)，复现见 `tests/bench/run-all.sh`。

## 许可

[MIT](LICENSE) © 2026 ApolloCalc-CAS Contributors
