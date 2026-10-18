# ApolloCalc-CAS 家族验收清单

四版本（JS 与 C99 × 64KB 与 128KB）的完成度、跨版本一致性、Zero-GC 与内存账本汇总。
所有数字均为本机实测，采集口径见各节。

## 版本完整度

| 版本 | 状态 | 测试数 | 文档 |
|:---|:---|:---|:---|
| JS 64KB | ✅ 已完成 | 80（build-kernel 回归）+ 19 ESM 接线 | [js-64kb.md](js-64kb.md) |
| JS 128KB | ✅ 已完成 | 327（各套件合计，见下）+ 83 回归 + 35 ESM 接线 | [js-128kb.md](js-128kb.md) |
| C 64KB | ✅ 已完成（v1.0.0） | 565（默认）/ 578（`LUT=1`） | [c99-64kb.md](c99-64kb.md) |
| C 128KB | ✅ 已完成（v1.0.0） | 1171（默认）/ 1184（`LUT=1`） | [c99-128kb.md](c99-128kb.md) |

**JS 128KB 测试明细**（实测，`node test/<suite>.js`）：

| 套件 | 校验 |
|:---|---:|
| test-evaluator.js | 49 |
| test-solvers.js | 46 |
| test-ratroot.js | 33 |
| test-complex.js | 23 |
| test-calculus.js | 28 |
| test-taylor.js | 26 |
| test-base.js | 63 |
| test-consistency.js | 59 pass + 4 improve |
| **合计** | **327 pass + 4 improve** |

另：`build-kernel.js` 拼接后跑 83 项回归（evaluator 41 + solvers 42）+ 静态声明扫描 75/75 +
ESM 导出链路 35/35。

**JS 64KB 测试明细**：`test-evaluator.js` 41 + `test-solvers.js` 40（单跑）/ 39（build-kernel 口径）
+ 静态扫描 34/34 + ESM 导出链路 19/19。

## 跨版本一致性

| 版本对 | 用例数 | 根数一致 | 数值 < 1e-9 | 状态 |
|:---|---:|:---|:---|:---|
| C64 vs JS64 | 50 | 100% | 100% | ✅ |
| C128 vs JS128 | 50 | 100% | 100% | ✅ |

- **C128 vs JS128**：`tests/bench/c128-smoke.csv` vs `js128-smoke.csv`，
  五分量精确形式（12 字段）与错误码逐字段精确比对，报告
  [tests/bench/SMOKE-DIFF-128.md](../tests/bench/SMOKE-DIFF-128.md)
- **C64 vs JS64**：`tests/bench/c-smoke.csv` vs `js-smoke.csv`（三分量口径），报告
  [tests/bench/SMOKE-DIFF.md](../tests/bench/SMOKE-DIFF.md)
- 用例构成：多项式求解 30 例（二次/三次/四次 + 五次次数门禁）+ 线性方程组 4 例 +
  preCheck 门禁 3 例 + 精确形式 13 例
- **token 流一致性**：`test_parser_smoke.c` 逐字节验证 C vs JS 的 token 数量与编码
  （`2x` / `xπ` / `(2+3)x` / `2ex` / `-2` / `2^-1` 等，15 项）
- 已知良性差异：libm vs V8 内建仅末位 ulp（原始 CSV 有 3 处最末位数字不同，数值差为 0）

复现：

```bash
cd C/128KB && mingw32-make smoke_runner && ./smoke_runner.exe > ../../tests/bench/c128-smoke.csv
cd ../.. && node tests/bench/js128-smoke.js > tests/bench/js128-smoke.csv
node tests/bench/diff-smoke128.js        # 退出码 0 = 全部一致
```

## Zero-GC 验证

| 版本 | 验证方式 | 证据 |
|:---|:---|:---|
| JS 64KB / 128KB | 预分配 TypedArray + 全局标量；禁对象/数组字面量、禁递归、禁热路径字符串拼接 | [js-64kb.md](js-64kb.md) / [js-128kb.md](js-128kb.md) |
| C 64KB / 128KB | 链接期 `--wrap=malloc/free` + 运行期命中即 `abort()` + `nm` 无堆符号 | 见下 |

JS 侧堆增量实测（`node --expose-gc bench-js.js`，见 `tests/bench/result-js*.csv` 的 `MEM` 行）：

| 版本 | heap_before | heap_after | delta |
|:---|---:|---:|---:|
| JS 64KB | 4.575 MB | 4.578 MB | **+3.16 KB** |
| JS 128KB | 4.811 MB | 4.806 MB | **−5.84 KB**（GC 回收后为负） |

两版压测全程堆增量均在 KB 量级（远小于 10KB 门禁），确认 Zero-GC 内核不产生累积分配。

C 侧三层保障：

1. **静态分配**：全部池为 `static` 全局数组或模块内 `static` 工作区；无 VLA、无递归
2. **链接期拦截**：`-Wl,--wrap=malloc -Wl,--wrap=free`
3. **运行期强制**：`__wrap_malloc` / `__wrap_free` 在 kernel-live（`main` 进入到返回）期间
   命中即 `abort()`；CRT 启动期的合法堆调用经 `GetProcAddress` 转发到 msvcrt

验收命令与预期：

```bash
cd C/128KB && nm test_runner.exe | grep -iE "malloc|free|realloc" | grep -v __wrap
# 预期：无输出（实测：仅 __wrap_malloc / __wrap_free 两个符号，排除后零命中）
```

## 内存账本

| 版本 | 静态池总量 | 水位门禁 | 采集口径 |
|:---|---:|---:|:---|
| JS 64KB | ~5.2 KB | 48 KB | 源码 TypedArray 声明加总 |
| JS 128KB | 11992 B ≈ 11.7 KB | 96 KB | `byteLength` 加总（见 js-128kb.md） |
| C 64KB | 10576 B ≈ 10.3 KB | 48 KB | `size` 的 `.bss` |
| C 128KB | 59024 B ≈ **57.6 KB** | 96 KB | `size` 的 `.bss` |

C 128KB 分段（`size test_runner.exe`）：

| 段 | 默认 | `LUT=1` |
|:---|---:|---:|
| `.text` | 201480 | 203268 |
| `.data` | 2716 | 2716 |
| `.bss` | **59024** | **75440** |
| 门禁 | 98304 | 98304 |

C 128KB 的主要占用（实测 `nm --size-sort`）：符号模块 27008 B + 表达式池 10976 B +
矩阵模块 10368 B + 泰勒 2176 B + CRC/Base64 表 1280 B + 有理根 1216 B + 大整数 1108 B
+ 根结果池 712 B，另有 CRT/GS 记录与对齐填充 3732 B。`LUT=1` 另加 sin/cos 两表 16384 B。
四版本均远低于各自预算与水位门禁。

## 家族价值主张

同一 CAS 引擎、两种语言、两种内存预算，输出一致。四版本各具独特价值：

- **JS 64KB**：最小可行 CAS，已在华为 WATCH FIT 4 鸿蒙应用实机验证，JerryScript 可跑
- **JS 128KB**：五分量精确形式 + 数值微积分 + 泰勒展开 + 有理根 + 12 元线性方程组
- **C 64KB**：MCU 入门版（Cortex-M4 级），含数论 / 进制分数 / CRC/SHA/Base64 工具
- **C 128KB**：嵌入式 CAS 完整形态（Cortex-M7 / ESP32-S3 级），含大整数 / 矩阵 /
  特殊函数 / 符号求导

## 验收结论

四版本均达到各自的验收标准：编译零警告、测试全绿、`nm` 无堆符号、跨版本数值 100% < 1e-9。
C 64KB 与 C 128KB 均具备 v1.0.0 发布条件。
