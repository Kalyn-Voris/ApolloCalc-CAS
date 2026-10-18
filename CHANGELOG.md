# Changelog

本项目的所有重要变更记录于此。格式遵循 [Keep a Changelog](https://keepachangelog.com/)，
版本号遵循 [语义化版本](https://semver.org/)。

## v1.0.0 (2026-10-13)

四版本家族首个完整版本：同一 CAS 引擎，JS / C99 两种语言 × 64KB / 128KB 两种内存预算，
跨语言数值输出一致（< 1e-9）。

### Added

- **JS 64KB**：核心 CAS 内核（parser / evaluator / exact / solvers / math / complex）
  — 表达式解析求值、2-4 次方程求解（含复根）、6 元线性方程组、三分量精确形式。
  已在华为 WATCH FIT 4 鸿蒙应用中实机验证。
- **JS 128KB**：五分量精确形式 `(a+b√c)/d ×10^e` + 数值微积分 + 双曲/复数扩展 +
  有理根检验与低次因式分解 + 泰勒展开 + 12 元线性方程组 + 进制转换与 SI 词头。
- **C 64KB**：C99 对等实现（Cortex-M4 级）+ 数论 / 进制分数 / CRC32·SHA-256·Base64·定点数
  三个 C 独有层。
- **C 128KB**：对等 JS 128KB（五分量精确形式、双曲/反双曲、复数域扩展、数值微积分与泰勒、
  有理根降次、12 元线性方程组、表达式池与 preCheck 阈值翻倍、96KB 水位门禁）
  + 大整数（180 位十进制）+ 矩阵（12×12：乘法/转置/秩/行列式/逆/特征值/QR）
  + 特殊函数（Gamma/Beta/erf/不完全 Gamma/Bessel）+ 符号求导与因式分解。

### Zero-GC

所有版本严格零 GC。JS 靠"无临时对象"（预分配 TypedArray + 全局标量，禁对象/数组字面量、
禁递归、禁热路径字符串拼接）；C 靠"无 malloc"（全静态预分配，禁 malloc/VLA/递归，
链接期 `--wrap=malloc/free` 拦截 + 运行期命中即 `abort()`）。

验证证据：`nm` 无 `malloc`/`free`/`realloc` 符号（除 `__wrap_*`）；
JS 压测堆增量在 KB 量级。详见 [docs/ACCEPTANCE.md](docs/ACCEPTANCE.md)。

### 测试规模

| 版本 | 测试数 |
|:---|---:|
| JS 64KB | 80 + 19 ESM 接线 |
| JS 128KB | 327 + 83 回归 + 35 ESM 接线 |
| C 64KB | 565（`LUT=1` 为 578） |
| C 128KB | 1171（`LUT=1` 为 1184） |

跨版本冒烟一致性：C64 vs JS64 = 50/50，C128 vs JS128 = 50/50（根数 100% 一致，
数值 100% < 1e-9）。

### 已知差异

- `guardMultivariateNonlinear` 在 JS 64KB / JS 128KB 中已定义并导出，但未被任何求解
  流水线调用（死代码）。C99 两版均不提供该函数。
