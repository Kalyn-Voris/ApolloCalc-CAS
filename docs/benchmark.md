# ApolloCalc-CAS · 跨版本压测汇总

数据来源：`tests/bench/`（`pool.json` / `gen_pool.py` / `gen_pool_c.py` / `bench-js.js` /
`bench-c.c` / `bench-c-makefile` / `run-all.sh` / `validate-pool.js`）。
压测口径：单操作平均耗时（`us_per_op`，越小越快）；同机顺序采集。

- 环境：Windows 11 x64，MinGW-w64 gcc 8.1.0（`-std=c99 -O2 -Wall`），Node v24.19.0
- 规模：**每 stage 独立 100 万次**，3 轮取中位数；四版本合计约 1600 万次操作
- 池：650 条（parse 250 / eval 250 / solve 100 / exact 50），`versions` 字段做版本门控
- 有效性：`node tests/bench/validate-pool.js` 四版本全部通过（无错误短路）
- **完整报告见 [`tests/bench/BENCH-1M.md`](../tests/bench/BENCH-1M.md)**

## 各版本单操作耗时（µs/op，100 万次口径）

| 阶段 | JS 64KB | JS 128KB | C 64KB | C 128KB |
|:---|---:|---:|---:|---:|
| parse | 0.0400 | 0.0398 | 0.0192 | 0.0189 |
| eval（解析+求值） | 0.1262 | 0.1217 | 0.0628 | 0.0595 |
| solve | 0.6920 | 1.2266 | 0.3532 | 0.4353 |
| exact | 0.3972 | 0.6640 | 0.1999 | 0.2517 |
| **全 stage 均值** | **0.3139** | **0.5130** | **0.1588** | **0.1913** |

池大小：64KB 两版 parse/eval 为 pool220（过滤掉 30 条 128KB 专属双曲条目），
128KB 两版为 pool250；solve/exact 四版本均为 pool100 / pool50。

## 跨版本加速比矩阵（基线 = JS 64KB；<1 = 更快）

| 阶段 | JS128 / JS64 | C64 / JS64 | C128 / JS64 |
|:---|---:|---:|---:|
| parse | 1.00（持平） | **0.48（快 2.1×）** | **0.47（快 2.1×）** |
| eval | 0.96（持平） | **0.50（快 2.0×）** | **0.47（快 2.1×）** |
| solve | **1.77（慢 1.8×）** | **0.51（快 2.0×）** | **0.63（快 1.6×）** |
| exact | **1.67（慢 1.7×）** | **0.50（快 2.0×）** | **0.63（快 1.6×）** |

**要点**：

- **C 版比同预算 JS 版快 2 – 3×**：`C64/JS64` 四个 stage 比值都在 0.47 – 0.51（快 2.0 – 2.1×），
  按同预算配对 `C128/JS128` 为 0.35 – 0.49（快 2.0 – 2.9×）——功能越复杂，C 的领先越大。
  零分配单遍扫描 + 静态池对 V8 字符串/对象开销的优势，在 100 万次规模上稳定复现。
- **JS 128KB 的 solve/exact 比 JS 64KB 慢 1.7 – 1.8×**：五分量精确形式 + 有理根（ratRoot）
  钩子的**每条固定开销**。池从 8 条扩到 250/100/50 条后该比值未变（旧小规模口径为
  1.57 – 2.18×），说明差距来自单次求解路径而非池构成。
- **C 128KB 的 solve/exact 比 C 64KB 慢 23% – 26%**，明显小于 JS 同族的 67% – 77%：
  同样是有理根钩子 + 五分量装配，C 版该开销明显更小（实测只能确认差距本身，
  无法区分归因于静态内存还是编译期优化）。
- **parse / eval 几乎不受版本影响**（C128/C64 = 0.98 / 0.95，JS128/JS64 = 1.00 / 0.96）：
  池从 220 扩到 250 条（含双曲函数解析）对单次操作无可测影响；双曲函数不是性能异常点。

## 内存对比

| 版本 | 堆增量（10 万次 parse 后） | 表达式池静态占用 | 整版 `.bss` |
|:---|---:|---:|---:|
| JS 64KB | −48.06 KB（≈0，GC 噪声） | 5,504 B | —（无静态段概念） |
| JS 128KB | −51.13 KB（≈0，GC 噪声） | 11,736 B | — |
| C 64KB | —（静态） | 5,336 B（`sizeof`） | **10,576 B（10.33 KB）** |
| C 128KB | —（静态） | 11,208 B（`sizeof`） | **59,024 B（57.64 KB）** |

- JS 堆增量在 `--expose-gc` 下两次 `global.gc()` 后测得，**负值即噪声**，符合 Zero-GC 预期。
- 「表达式池静态占用」为表达式类池（token/number/eval_stack/matrix/rpn/op/lhs/rhs/coeffs/root）
  的同类对比：C 128KB/C 64KB = **2.10×**，JS 128KB/JS 64KB = **2.13×**，两族同步翻倍。
- C 端 `.bss` 远大于表达式池，因为含符号（`ac_symbolic`）、矩阵（`ac_matrix`）、
  大整数（`ac_bigint`）等 C 独有模块。

## C 128KB 静态内存分布（`.bss` 实测）

`size C/128KB/test_runner.exe`：`.text 201480` / `.data 2716` / **`.bss 59024`（57.6KB）**，
水位门禁 98304（96KB）。

| 模块/池组 | 字节 | 占比 |
|:---|---:|---:|
| 符号（`ac_symbolic`：节点池 + 三栈 + 值表 + 因式输出） | 27008 | 45.8% |
| 表达式（token/number/eval_stack/matrix/rpn/op/lhs/rhs） | 10976 | 18.6% |
| 矩阵（`ac_matrix`：4 全局 + 增广 + 3 工作区） | 10368 | 17.6% |
| 泰勒（`s_taylor_work` + `g_taylor_coeffs`） | 2176 | 3.7% |
| CRC 表 + Base64 反查表 | 1280 | 2.2% |
| 有理根（cand/work/trial/divs + 根栈） | 1216 | 2.1% |
| 大整数（`ac_bigint`：5 操作数 + 4 工作区 + 2 缓冲） | 1108 | 1.9% |
| 根结果池（12 槽 × re/im/五分量/exp/valid） | 712 | 1.2% |
| 进制缓冲 / 数论 / `g_coeffs` | 488 | 0.8% |
| CRT/GS 记录、emu 桩、对齐填充 | 3732 | 6.3% |
| **合计（实测 `.bss`）** | **59024** | 100% |

`LUT=1` 构建另加 sin/cos 两表 16384 B，`.bss` → **75440（73.7KB）**，仍低于 96KB 门禁。

对比：C 64KB `.bss` 10576 B（10.3KB）；C 128KB 是它的 **5.6×**，主要来自符号模块
（C 独有，24.7KB）与矩阵模块（C 独有，10.1KB）。

## C128 vs JS128 冒烟一致性（第 11 轮）

| 版本对 | 用例数 | 根数一致 | 数值 < 1e-9 | 精确形式 | 状态 |
|:---|---:|:---|:---|:---|:---|
| C128 vs JS128 | 50 | 100% | 100%（0 处超差） | 五分量逐字段精确相等 | ✅ |
| C64 vs JS64 | 50 | 100% | 100% | 三分量逐字段精确相等 | ✅ |

- C 侧：`C/128KB/test/test_smoke_csv.c` → `mingw32-make smoke_runner` → `c128-smoke.csv`
- JS 侧：`tests/bench/js128-smoke.js` → `js128-smoke.csv`
- 比对：`node tests/bench/diff-smoke128.js` → `tests/bench/SMOKE-DIFF-128.md`（退出码 0）
- 注意：128KB 精确形式为**五分量 12 字段**（`valid,rat,rad_num,rad,den,exp`），与 64KB 的
  三分量 8 字段不兼容，故 `diff-smoke128.js` 独立于 `diff-smoke.js`
- 已知良性差异：原始 CSV 有 3 处最末位 ulp 文本差异（如 `0.66666666666666663` vs
  `0.6666666666666666`），数值差为 0

## 复现

```bash
cd F:/ApolloCalc-CAS

# 池生成（一次性；pool.json / pool_c.h 已入库，正常无需重跑）
python tests/bench/gen_pool.py        # -> pool.json
python tests/bench/gen_pool_c.py      # -> pool_c.h

# 池有效性校验
node tests/bench/validate-pool.js

# 四版本 100 万次（每版本 < 10 秒）
node --expose-gc tests/bench/bench-js.js JS/64KB/src/core_kernel.js  > tests/bench/result-js64-1M.csv
node --expose-gc tests/bench/bench-js.js JS/128KB/src/core_kernel.js > tests/bench/result-js128-1M.csv

cd tests/bench
mingw32-make -f bench-c-makefile KERNEL=../../C/64KB/src clean && \
mingw32-make -f bench-c-makefile KERNEL=../../C/64KB/src && \
./bench-c.exe > result-c64-1M.csv
mingw32-make -f bench-c-makefile KERNEL=../../C/128KB/src clean && \
mingw32-make -f bench-c-makefile KERNEL=../../C/128KB/src && \
./bench-c.exe > result-c128-1M.csv

# 跨版本冒烟一致性（第 11 轮）
cd ../../C/128KB && mingw32-make smoke_runner && ./smoke_runner.exe > ../../tests/bench/c128-smoke.csv
cd ../.. && node tests/bench/js128-smoke.js > tests/bench/js128-smoke.csv
node tests/bench/diff-smoke128.js
```

一键：`bash tests/bench/run-all.sh`（Windows 上 bash 不在 PATH 时用 `F:/Git/bin/bash.exe`）
→ 四份 `result-*-1M.csv` + `summary-ratio.csv`。

原始数据：`result-js64-1M.csv` / `result-js128-1M.csv` / `result-c64-1M.csv` /
`result-c128-1M.csv` / `summary-ratio.csv` / `c128-smoke.csv` / `js128-smoke.csv`。

## 历史数据（小规模口径，已被上表取代）

> 以下为**早期 8 用例 × 1 万次（parse）/ 10 万次（eval）/ 1 千次（solve）**口径的存档结果，
> 保留仅供参考。该口径下 eval 段只测 `evalExpressionAt`（环内不重新 parse），
> 与现口径不可直接比较；C 侧 solve 段当时为 TODO。
> 原始文件：`result-js64.csv` / `result-js128.csv` / `result-c64.csv`。

| 阶段 | 用例 | JS 64KB | JS 128KB | C 64KB |
|:---|:---|---:|---:|---:|
| parse | short | 0.0224 | 0.0230 | 0.0085 |
| parse | funcs | 0.1745 | 0.1584 | 0.0498 |
| parse | nested | 0.1060 | 0.1171 | 0.0389 |
| eval | poly2 | 0.0742 | 0.0592 | 0.0316 |
| eval | trig | 0.0637 | 0.0622 | 0.0806 |
| solve | quad_int | 0.5192 | 0.8177 | —（当时 TODO） |
| solve | cubic | 0.9574 | 1.9943 | — |
| solve | quartic | 0.8661 | 1.8898 | — |

当时的一条结论在此口径下方向仍然成立，但幅度收窄：**纯三角子集的 eval-only 段，C 64KB 仍略慢于
JS 64KB**——用本次同一 pool 的三角子集（79 条，parse 移出计时环）实测
C 64KB **0.0917** µs/op vs JS 64KB **0.0850** µs/op，即慢 **8%**（旧 8 用例口径为慢 27%）。
但 100 万次混合池口径下（`eval` 列含 parse + 求值、覆盖全部 250 条），
C 64KB 反而快 2×（`C64/JS64 = 0.50`）：桌面 libm 的 sin/cos 劣势被池内多项式/对数/指数
等 C 占优的路径摊平。MCU 目标上此差异仍会反转（无 JIT，libm 即最优路径；
或开 `AC_MATH_USE_LUT` 查表）。
