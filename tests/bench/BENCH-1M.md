# ApolloCalc-CAS · 五版本 100 万次压测

- 数据来源：`tests/bench/result-{js64,js128,c64,c128,pro}-1M.csv`（本目录）
- 表达式池：`pool.json`（650 条，`gen_pool.py` 生成）；C 端读 `pool_c.h`（`gen_pool_c.py` 生成）
- 迭代口径：**每 stage 独立 100 万次**，3 轮取中位数；实际迭代数按整遍对齐（略高于 100 万）
- 环境：Windows 11 x64，MinGW-w64 gcc 8.1.0（`-std=c99 -O2 -Wall`），Node v24.19.0
- 池有效性：`node tests/bench/validate-pool.js` 四基础版本全部通过（无 parse/extract/solve 错误短路）

> **v1.1.0 更新（2026-10-21）**：C/128KB 完成零堆内存压缩（`.bss` 59024 → 39408 B，−33.24%）。
> 新增 [§6.5 v1.0.0 vs v1.1.0 性能回归](#65-v110-内存压缩后的性能回归c-128kb)，
> 用同机同编译器、16 次采样中位数对比两个版本。§6.1 的四版本横向表仍为 v1.0.0 数据（未重测 JS 两版）。
>
> **v2.4.0 复测（2026-10-31）**：五件套全量复跑（650 条池与口径不变），§6.1–6.3 已按本次中位数整体刷新——
> solve/exact 段与上一轮记录差在噪声带内；js128 parse（1.00→1.08）与 c128 eval（0.47→0.54）
> 偏移超出带内，如实按实测刷新。
>
> **v2.6.0 终版（2026-11-04，G3）**：口径扩为**五列**——新增 `pro_runner` 列
> （C/128KB-ProTEXT 内核，ProTEXT Makefile `probench` 目标构建 `pro_bench.exe`），
> 五列同机同轮全量复跑，§6.1–6.3 按本次中位数整体刷新；**预热丢首轮口径自此成文**
> （见口径说明第 5 条）。wasm 列随 v3 裁决顺延 v2.7+，本版不设。

## 口径说明（与旧版 `cases.json` 压测的差异）

1. **eval 段口径变了**：旧版每个用例只 `parseExpression` 一次，环内只测 `evalExpressionAt`；
   本次池内 250 条表达式各不相同，环内必须重设 `angleMode` + `parse` + 求值。
   因此 **eval 列 = 解析 + 求值合计成本**，与旧版 eval 数值不可直接比较。
2. **solve/exact 段**：环内跑完整规范链路
   `solversReset → parseExpression → extractCoefficients → solveQuadratic/Cubic/Quartic`。
3. **exact 段只测耗时**（不校验精确形式），池偏向精确形式路径：纯分数根 20 / 纯根式根 20 /
   五分量混合形式 10。四基础版本都跑这 50 条（64KB 两版混合形式 `valid=0`，路径不同恰好可对比）。
4. `name` 列是 `pool<池大小>`：64KB 两版过滤掉 30 条 128KB 专属条目（双曲函数），池为 220。
5. **预热丢首轮口径（v2.6.0 起成文）**：每 stage 在计时环之前，先按池顺序预热 **1000 次**
   （`WARMUP`，池顺序循环、覆盖全部条目）——冷启动成本（首次页入 / 缓存填充 / 分支预测）
   由预热吸收、**不计入计时，等效于丢弃首轮**；正式计时 3 轮取中位数，进一步压制轮间抖动。
   五列口径一致（`bench-c.c` 的 `WARMUP=1000` / `bench-js.js` 的 `--warmup=1000` 缺省同值）。
6. **pro_runner 列（v2.6.0 G3 新增）**：与 C 128KB 列共用同一份 `bench-c.c` 与 `pool_c.h`，
   唯一差异是链接的内核对象来自 `C/128KB-ProTEXT`（上游 16 模块包装 TU + Pro 增量，
   `C/128KB-ProTEXT/Makefile` 的 `probench` 目标构建 `pro_bench.exe`；对象与 pro_runner /
   pro_kernel 共享同一批 obj 缓存）。四 stage、池过滤、预热、迭代口径与其余各列逐字一致；
   STATIC 行 `kernel_tag` 打印 `ProTEXT` 与上游 C 128KB 列区分。

## 6.1 五版本横向对比（µs/op，越小越快）

| stage | JS 64KB | JS 128KB | C 64KB | C 128KB | C 128KB-ProTEXT（pro_runner） |
|:---|---:|---:|---:|---:|---:|
| parse | 0.0409 | 0.0394 | 0.0194 | 0.0186 | 0.0185 |
| eval（解析+求值） | 0.1253 | 0.1143 | 0.0628 | 0.0609 | 0.0555 |
| solve | 0.6867 | 1.2045 | 0.3582 | 0.4275 | 0.4343 |
| exact | 0.3956 | 0.6486 | 0.1995 | 0.2459 | 0.2563 |
| **全 stage 均值** | **0.3121** | **0.5017** | **0.1600** | **0.1882** | **0.1912** |

**各版本实际池大小与迭代数**

| 版本 | parse | eval | solve | exact | 合计迭代 |
|:---|---:|---:|---:|---:|---:|
| JS 64KB | pool220 / 1,000,120 | pool220 / 1,000,120 | pool100 / 1,000,000 | pool50 / 1,000,000 | 4,000,240 |
| JS 128KB | pool250 / 1,000,000 | pool250 / 1,000,000 | pool100 / 1,000,000 | pool50 / 1,000,000 | 4,000,000 |
| C 64KB | pool220 / 1,000,120 | pool220 / 1,000,120 | pool100 / 1,000,000 | pool50 / 1,000,000 | 4,000,240 |
| C 128KB | pool250 / 1,000,000 | pool250 / 1,000,000 | pool100 / 1,000,000 | pool50 / 1,000,000 | 4,000,000 |
| C 128KB-ProTEXT | pool250 / 1,000,000 | pool250 / 1,000,000 | pool100 / 1,000,000 | pool50 / 1,000,000 | 4,000,000 |

> 64KB 两版少 30 条双曲函数条目（`sinh`/`cosh`/`tanh`/`asinh`/`acosh`/`atanh` 仅 128KB 支持），
> 故池为 220；`ceil(1e6/220)=4546` 遍 × 220 = 1,000,120 次。

## 6.2 加速比矩阵（基线 = JS 64KB；>1 = 比 JS 64KB 慢，<1 = 更快）

| stage | JS128 / JS64 | C64 / JS64 | C128 / JS64 | Pro / JS64 |
|:---|---:|---:|---:|---:|
| parse | 0.96（持平） | **0.47（快 2.1×）** | **0.45（快 2.2×）** | **0.45（快 2.2×）** |
| eval | 0.91（持平） | **0.50（快 2.0×）** | **0.49（快 2.0×）** | **0.44（快 2.3×）** |
| solve | **1.75（慢 1.8×）** | **0.52（快 1.9×）** | **0.62（快 1.6×）** | **0.63（快 1.6×）** |
| exact | **1.64（慢 1.6×）** | **0.50（快 2.0×）** | **0.62（快 1.6×）** | **0.65（快 1.5×）** |

同族内对比：

| stage | JS 128KB / JS 64KB | C 128KB / C 64KB | Pro / C 128KB |
|:---|---:|---:|---:|
| parse | 0.96 | 0.96 | 0.99 |
| eval | 0.91 | 0.97 | 0.91 |
| solve | **1.75** | **1.19** | 1.02 |
| exact | **1.64** | **1.23** | 1.04 |

## 6.3 关键观察

1. **C 版比同预算 JS 版快 1.5 – 2.9×**：以 `C64/JS64` 计，parse / eval / solve / exact 四个 stage 的
   比值都落在 0.47 – 0.52，即快 **1.9 – 2.1×**；按同预算配对 `C128/JS128` 计为 0.35 – 0.58，
   即快 **1.7 – 2.9×**（eval 段本轮 js128 偏快所致），**solve/exact 段 C 的领先幅度最大**。
   零分配单遍扫描 + 静态池对 V8 字符串/对象开销的优势在 100 万次规模上稳定复现
   （与旧版小规模结论方向一致）。
2. **JS 128KB 的 solve/exact 比 JS 64KB 慢 1.6 – 1.8×**：这是五分量精确形式 + 有理根（ratRoot）
   钩子的固定开销，与旧版 `cases.json` 测得的 1.57 – 2.18× 区间吻合。池扩大到 250/100/50 条
   **没有放大也没有缩小**这一差距，说明它是每条求解的固定成本，不是池构成导致的。
3. **C 128KB 的 solve/exact 比 C 64KB 慢 19% – 23%**（0.4275 vs 0.3582 / 0.2459 vs 0.1995），
   远小于 JS 同族的 64% – 75%：同样是有理根钩子 + 五分量装配，但 C 版该开销明显更小
   （实测只能确认差距本身，无法区分归因于静态内存还是编译期优化）。
4. **parse 与 eval 的版本间差异在 ±8% 内**：C128/C64 = 0.96 / 0.97，JS128/JS64 = 0.96 / 0.91。
   池从 220 扩到 250 条（含双曲函数解析）未见成倍开销，**池扩大不影响单次操作量级**。
5. **双曲函数不是性能异常点**：128KB 池多出的 30 条双曲表达式，使 parse/eval 的单次成本
   与 64KB 差异 ≤8%（方向不一），未见成倍额外开销。
6. **Pro 层对上游四 stage 零可测回归（G3 新增 pro_runner 列的落点结论）**：ProTEXT 内核
   与上游 C/128KB 是同一批上游 TU（包装文件编译恰好一次）+ Pro 增量，四 stage 对比
   `Pro/C128 = 0.99 / 0.91 / 1.02 / 1.04`——方向不一致、全部落在 §6.5 记录的 3–10%
   单次采样抖动带内，即 Pro 层的 union 静态化、Pro 增量 TU 与链接布局对基础四 stage
   **无可测系统性影响**；eval 的 −9% 与 solve/exact 的 +2%/+4% 属布局级噪声，不作为性能结论。
7. **对同预算 JS 128KB，Pro 快 2.1 – 2.8×**：Pro/JS128 = parse 0.47 / eval 0.49 /
   solve 0.36 / exact 0.40，solve/exact 的领先（2.5 – 2.8×）与观察 1/2 的归因一致——
   C 侧把五分量 + 有理根钩子的固定开销压得更小。

## 6.1 四版本横向对比（µs/op，越小越快）

| stage | JS 64KB | JS 128KB | C 64KB | C 128KB |
|:---|---:|---:|---:|---:|
| parse | 0.0402 | 0.0435 | 0.0194 | 0.0192 |
| eval（解析+求值） | 0.1267 | 0.1178 | 0.0638 | 0.0681 |
| solve | 0.6980 | 1.2375 | 0.3566 | 0.4374 |
| exact | 0.3998 | 0.6676 | 0.2023 | 0.2497 |
| **全 stage 均值** | **0.3162** | **0.5166** | **0.1605** | **0.1936** |

**各版本实际池大小与迭代数**

| 版本 | parse | eval | solve | exact | 合计迭代 |
|:---|---:|---:|---:|---:|---:|
| JS 64KB | pool220 / 1,000,120 | pool220 / 1,000,120 | pool100 / 1,000,000 | pool50 / 1,000,000 | 4,000,240 |
| JS 128KB | pool250 / 1,000,000 | pool250 / 1,000,000 | pool100 / 1,000,000 | pool50 / 1,000,000 | 4,000,000 |
| C 64KB | pool220 / 1,000,120 | pool220 / 1,000,120 | pool100 / 1,000,000 | pool50 / 1,000,000 | 4,000,240 |
| C 128KB | pool250 / 1,000,000 | pool250 / 1,000,000 | pool100 / 1,000,000 | pool50 / 1,000,000 | 4,000,000 |

> 64KB 两版少 30 条双曲函数条目（`sinh`/`cosh`/`tanh`/`asinh`/`acosh`/`atanh` 仅 128KB 支持），
> 故池为 220；`ceil(1e6/220)=4546` 遍 × 220 = 1,000,120 次。

## 6.2 加速比矩阵（基线 = JS 64KB；>1 = 比 JS 64KB 慢，<1 = 更快）

| stage | JS128 / JS64 | C64 / JS64 | C128 / JS64 |
|:---|---:|---:|---:|
| parse | 1.08（慢 8%） | **0.48（快 2.1×）** | **0.48（快 2.1×）** |
| eval | 0.93（持平） | **0.50（快 2.0×）** | **0.54（快 1.9×）** |
| solve | **1.77（慢 1.8×）** | **0.51（快 2.0×）** | **0.63（快 1.6×）** |
| exact | **1.67（慢 1.7×）** | **0.51（快 2.0×）** | **0.62（快 1.6×）** |

同族内对比（128KB / 64KB）：

| stage | JS 128KB / JS 64KB | C 128KB / C 64KB |
|:---|---:|---:|
| parse | 1.08 | 0.99 |
| eval | 0.93 | 1.07 |
| solve | **1.77** | **1.23** |
| exact | **1.67** | **1.23** |

## 6.3 关键观察

1. **C 版比同预算 JS 版快 1.7 – 2.9×**：以 `C64/JS64` 计，parse / eval / solve / exact 四个 stage 的
   比值都落在 0.48 – 0.51，即快 **2.0 – 2.1×**；按同预算配对 `C128/JS128` 计为 0.35 – 0.58，
   即快 **1.7 – 2.9×**（eval 段本轮 js128 偏快所致），**solve/exact 段 C 的领先幅度最大**。
   零分配单遍扫描 + 静态池对 V8 字符串/对象开销的优势在 100 万次规模上稳定复现
   （与旧版小规模结论方向一致）。
2. **JS 128KB 的 solve/exact 比 JS 64KB 慢 1.7 – 1.8×**：这是五分量精确形式 + 有理根（ratRoot）
   钩子的固定开销，与旧版 `cases.json` 测得的 1.57 – 2.18× 区间吻合。池扩大到 250/100/50 条
   **没有放大也没有缩小**这一差距，说明它是每条求解的固定成本，不是池构成导致的。
3. **C 128KB 的 solve/exact 比 C 64KB 慢 23%**（0.4374 vs 0.3566 / 0.2497 vs 0.2023），
   远小于 JS 同族的 67% – 77%：同样是有理根钩子 + 五分量装配，但 C 版该开销明显更小
   （实测只能确认差距本身，无法区分归因于静态内存还是编译期优化）。
4. **parse 与 eval 的版本间差异在 ±8% 内**：C128/C64 = 0.99 / 1.07，JS128/JS64 = 1.08 / 0.93。
   池从 220 扩到 250 条（含双曲函数解析）未见成倍开销，**池扩大不影响单次操作量级**。
5. **双曲函数不是性能异常点**：128KB 池多出的 30 条双曲表达式，使 parse/eval 的单次成本
   与 64KB 差异 ≤8%（方向不一），未见成倍额外开销。

## 6.4 内存对比

| 版本 | 堆增量（10 万次 parse 后） | 表达式池静态占用 |
|:---|---:|---:|
| JS 64KB | **-48.09 KB（≈0，GC 噪声）** | 5,504 B（5.4 KB，TypedArray 口径） |
| JS 128KB | **-51.17 KB（≈0，GC 噪声）** | 11,736 B（11.5 KB，TypedArray 口径） |
| C 64KB | —（静态） | 5,336 B（表达式类池，`sizeof` 口径） |
| C 128KB | —（静态） | 11,208 B（表达式类池，`sizeof` 口径） |

**整版 `.bss`（`size` 实测，C 端）**

| 版本 | .text | .data | **.bss** | 水位门禁 | 门禁占用率 |
|:---|---:|---:|---:|---:|---:|
| C 64KB | 101,140 | 2,568 | **10,576 B（10.33 KB）** | — | — |
| C 128KB（v1.0.0） | 201,480 | 2,716 | **59,024 B（57.64 KB）** | 98,304（96 KB） | 60.03% |
| **C 128KB（v1.1.0）** | 202,804 | 2,716 | **39,408 B（38.48 KB）** | 98,304（96 KB） | **40.09%** |
| C 128KB-ProTEXT（v1.8.0，内核口径） | — | — | **42,608 B（41.61 KB）** | 114,688（112 KiB） | **37.15%** |

> ProTEXT 行是**内核口径**（pro_kernel.exe：38672 上游 + 80 var_val + 3848 Pro union +
> 8 对齐填充）；pro_runner.exe 口径再 + 32 B（测试 TU）= 42640 B，见 verify.ps1 账本。
> ProTEXT 列的「表达式池静态占用」与 C 128KB 行同为 8,648 B（同一批上游 TU，`sizeof` 口径）。

> `size` 的 text 列含 `.rdata`。真实代码段：v1.0.0 `.text` 146,424 / `.rdata` 46,656；
> v1.1.0 `.text` 146,376（−48）/ `.rdata` 48,048（+1392，CRC/Base64 常量表入 ROM）。

**说明**

- JS 端堆增量在 `--expose-gc` 下两次 `global.gc()` 后测得，**负值即噪声**（本次 -48 / -51 KB），
  符合 Zero-GC 内核预期：无逐次迭代分配。
- 上表「表达式池静态占用」为**表达式类池**（token/number/eval_stack/matrix/rpn/op/lhs/rhs/coeffs/root）
  的同类对比：C 与 JS 两族口径一致，C 128KB / C 64KB = 2.10×，JS 128KB / JS 64KB = 2.13×。
  v1.1.0 起 C 128KB 的该口径为 **8,648 B**（栈别名 + lhs/rhs 移除，见 `STATIC,expr_pools_bytes`）。
- C 端 `.bss` 与上表「表达式池」不同：`.bss` 含符号/矩阵/大整数等 C 独有模块，故 38.48 KB ≫ 8.6 KB。
- JS 端 `heap_before/after` 明细见各 CSV 的 `MEM` 行。

## 6.5 v1.1.0 内存压缩后的性能回归（C 128KB）

**问题**：v1.1.0 把 20 个 scratch 池折叠进一个 `union`，并移除了 lhs/rhs 拷贝、把两张查表改成 `const`。
必须确认**性能没有衰退**（甚至因工作集变小而提升）。

**方法**：同一台机器、同一编译器（gcc 8.1.0 `-std=c99 -O2 -Wall`）、同一份 `pool_c.h`。
v1.0.0 基线来自 `git archive HEAD` 解出的独立快照（`result-c128-1M-v100.csv`），
v1.1.0 来自当前工作区（`result-c128-1M.csv`）。两版**交替各跑 16 次**，取中位数。

| stage | v1.0.0 µs/op | v1.0.0 区间 | v1.1.0 µs/op | v1.1.0 区间 | Δµs | **Δ%** | 判定 |
|:---|---:|:---|---:|:---|---:|---:|:---|
| parse | 0.0190 | 0.0186–0.0196 | 0.0191 | 0.0186–0.0197 | +0.0001 | **+0.53%** | ✅ 噪声内 |
| eval（解析+求值） | 0.0624 | 0.0612–0.0672 | 0.0635 | 0.0608–0.0652 | +0.0012 | **+1.92%** | ✅ 噪声内 |
| solve | 0.4586 | 0.4492–0.4651 | 0.4441 | 0.4361–0.4497 | −0.0145 | **−3.15%** | ✅ 小幅提速 |
| exact | 0.2607 | 0.2570–0.2653 | 0.2533 | 0.2508–0.2594 | −0.0074 | **−2.84%** | ✅ 小幅提速 |
| **均值** | **0.8007** | — | **0.7801** | — | **−0.0206** | **−2.57%** | ✅ **无回归** |

**结论**

1. **四个 stage 全部落在 ±3% 合理误差带内，无可测性能衰退。**
2. solve（−3.15%）与 exact（−2.84%）是**提速**，方向与预期一致：`.bss` 减少 19.6 KB 后
   scratch 工作集更集中，缓存局部性改善；solve/exact 是访存最密集的两段，收益也最明显。
3. parse（+0.53%）与 eval（+1.92%）的差异**小于测量抖动**（两版各自的轮间极差为 5.8% / 9.6%），
   不能判定为真实变化。eval 段是「解析 + 求值」合计，本身包含 parse，抖动会被放大。
4. **判定阈值的说明**：本轮单次采样抖动达 3–10%，故 ±3% 是「无系统性回归」的判据，
   不是「逐次可分辨」。要分辨 1% 级差异需要更多采样或固定频率/隔离 CPU 核。

**`.bss` 对照**：`size test_runner.exe` → v1.0.0 `text 201480 / data 2716 / bss 59024`；
v1.1.0 `text 202804 / data 2716 / bss 39408`。`.text` 增加 1324 B（+0.66%），
来自 `ac_solvers.c` 水位估算的三分支与新增 include，未影响热路径。

**复现**：

```bash
# v1.0.0 基线（独立快照，勿污染工作区）
mkdir -p /tmp/v100 && git archive HEAD | tar -x -C /tmp/v100
cd /tmp/v100/tests/bench && mingw32-make -f bench-c-makefile KERNEL=../../C/128KB/src && \
  ./bench-c.exe > /tmp/result-c128-1M-v100.csv

# v1.1.0
cd F:/ApolloCalc-CAS/tests/bench && \
  mingw32-make -f bench-c-makefile KERNEL=../../C/128KB/src clean && \
  mingw32-make -f bench-c-makefile KERNEL=../../C/128KB/src && \
  ./bench-c.exe > result-c128-1M.csv
```

## 复现

```bash
cd F:/ApolloCalc-CAS

# 池生成（一次性；pool.json / pool_c.h 已入库，正常无需重跑）
python tests/bench/gen_pool.py        # -> pool.json
python tests/bench/gen_pool_c.py      # -> pool_c.h

# 池有效性校验（压测前建议跑一次）
node tests/bench/validate-pool.js

# 五版本（每版本 < 10 秒）
node --expose-gc tests/bench/bench-js.js JS/64KB/src/core_kernel.js  > tests/bench/result-js64-1M.csv
node --expose-gc tests/bench/bench-js.js JS/128KB/src/core_kernel.js > tests/bench/result-js128-1M.csv

cd tests/bench
mingw32-make -f bench-c-makefile KERNEL=../../C/64KB/src clean && \
mingw32-make -f bench-c-makefile KERNEL=../../C/64KB/src && \
./bench-c.exe > result-c64-1M.csv
mingw32-make -f bench-c-makefile KERNEL=../../C/128KB/src clean && \
mingw32-make -f bench-c-makefile KERNEL=../../C/128KB/src && \
./bench-c.exe > result-c128-1M.csv

# v2.6.0 G3 新增：pro_runner 列（链接 ProTEXT 内核对象；首次构建编译 ~40 个 TU，
# 之后与 pro_runner 共享 obj 缓存）
mingw32-make -C ../../C/128KB-ProTEXT SHELL=cmd.exe probench && \
../../C/128KB-ProTEXT/pro_bench.exe > result-pro-1M.csv
```

一键：`bash tests/bench/run-all.sh`（Windows 上 bash 不在 PATH 时用 `F:/Git/bin/bash.exe`）
→ 五份 CSV + `summary-ratio.csv`。

## 附：原始汇总行

```
result-js64-1M.csv  SUMMARY,total_ops,4000240,total_ms,1255.477,avg_us_per_op,0.3139
result-js128-1M.csv SUMMARY,total_ops,4000000,total_ms,2052.089,avg_us_per_op,0.5130
result-c64-1M.csv   SUMMARY,total_ops,4000240,total_ms,635.047,avg_us_per_op,0.1588
result-c128-1M.csv  SUMMARY,total_ops,4000000,total_ms,765.400,avg_us_per_op,0.1913

# v2.4.0 复测（2026-10-31）
result-js64-1M.csv  SUMMARY,total_ops,4000240,total_ms,1264.706,avg_us_per_op,0.3162
result-js128-1M.csv SUMMARY,total_ops,4000000,total_ms,2066.399,avg_us_per_op,0.5166
result-c64-1M.csv   SUMMARY,total_ops,4000240,total_ms,642.136,avg_us_per_op,0.1605
result-c128-1M.csv  SUMMARY,total_ops,4000000,total_ms,774.437,avg_us_per_op,0.1936

# v2.6.0 终版（2026-11-04，G3 五列口径；pro 列为首发基线）
result-js64-1M.csv  SUMMARY,total_ops,4000240,total_ms,1248.588,avg_us_per_op,0.3121
result-js128-1M.csv SUMMARY,total_ops,4000000,total_ms,2006.949,avg_us_per_op,0.5017
result-c64-1M.csv   SUMMARY,total_ops,4000240,total_ms,639.872,avg_us_per_op,0.1600
result-c128-1M.csv  SUMMARY,total_ops,4000000,total_ms,752.822,avg_us_per_op,0.1882
result-pro-1M.csv   SUMMARY,total_ops,4000000,total_ms,764.634,avg_us_per_op,0.1912
```
