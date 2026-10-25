# Changelog

本项目的所有重要变更记录于此。格式遵循 [Keep a Changelog](https://keepachangelog.com/)，
版本号遵循 [语义化版本](https://semver.org/)。

## v1.3.5 (2026-10-25)

Pro 层两个新方向落地：A5 实特征向量 + Hotelling 收缩 deflation、B2 符号积分。
`C/128KB-ProTEXT` 的 `.bss` **仍是 41456 B，零增长**；上游 `C/128KB/` 的既有文件
逐字节未动。本版是**纯 C99 Pro 升级**，不含任何 JS 代码变更。

### Added

- **A5 实特征向量 + Hotelling 收缩 deflation**（`src/ac_pro_eigen.c`，381 行）：
  `ac_pro_eig_all` 求实对称矩阵 n×n（n ≤ 12）的全部特征对，特征值按 |λ| 降序，
  特征向量列主序存放。三条路径互相印证：带 Rayleigh 商的幂迭代求主特征对、
  解 `(A − λI)v = 0` 零空间作为第二条独立路径、Hotelling 收缩 `A ← A − λ₁x₁x₁ᵀ`
  逐轮 deflate。76 条测试。
- **B2 符号积分**（`src/ac_pro_integrate.c`，1485 行）：`ac_pro_integrate` 支持
  多项式、基本三角函数、指数与对数，64 项标准积分表（`static const`，落 `.rdata`）
  + 一层分部积分 `∫u dv = u·v − ∫v du`。31 条测试。
- **`eig` scratch 视图**（`src/ac_pro_scratch.h`）：2592 B = V 1152 + A 副本 1152
  + λ 96 + work 192。被 `mv` 峰值（2688 B）吸收，union 尺寸不变。

### Changed

- **`ac_pro.h` 新增 A5/B2 两个 API 块**（+89 行）：A5 的 4 个入口与 B2 的 1 个入口
  加 5 个错误码。A5 的错误码走返回值而非 `ac_pro_eig_error()` getter —— 实测一个
  `static int` 会让该 TU 产生 16 B `.bss` 段、叠加链接对齐后全程序净涨 32 B
  （41456 → 41488），直接破门禁。
- **`Makefile` 接线 2 个新 TU**：`PRO_EXTRA` += `ac_pro_eigen.c` `ac_pro_integrate.c`，
  `TEST_SRC` += 对应两个测试文件。
- **`test/test_pro_runner.c`**：校验从 152 条增至 **259 条**，新增 11 个测试入口
  （A5 五项、B2 六项）。
- **A5 收敛判据由 |Δλ| 改为盯残差**：Rayleigh 商是二阶精确的，`|Δλ|` 会先于角度
  误差塌到 1e-16 而残差仍在 1e-7，用 |Δλ| 判停会提前 2~3 个数量级收工 ——
  实测 3×3 残差卡在 4.6e-8，过不了 1e-9 闸门。改为盯残差下降停滞
  （`res > 0.9·res_prev`）后落到 3.8e-13 ~ 4.7e-13。

### Fixed

- **B2 首版四个解析缺陷**：`e^x`、`x*e^x`、`2x+3`、`x^3+2x^2-5x+1` 全部误报
  EPARSE。独立探针发现后修复并复验。

### Performance

- **静态内存**：`.bss` 41456 B **未变**（v1.3 → v1.3.5 零增长）。`eig` 视图 2592 B
  比 `mv` 峰值 2688 B 小 96 B，被完全吸收。
- **代码体积**：新增两个 TU 的 `size` 总计 5340（A5）+ 17492（B2）= 22832 B。
  拆开看，A5 是 `.text` 4912 + `.rdata` 160 + 异常表 188，B2 是 `.text` 13776 +
  `.rdata` 3072（64 项积分表）+ 异常表 564。单 TU `bss` 与 `data` 均为 0。
- **运行性能（100 万次，同机）**：

  | 路径 | 迭代 | 单次耗时 |
  |:---|---:|---:|
  | `ac_pro_eig_all` 2×2 `[[2,1],[1,2]]` | 1,000,000 | **0.073 µs** |
  | `ac_pro_eig_all` 3×3 `[[4,1,2],[1,3,0],[2,0,5]]` | 1,000,000 | **1.593 µs** |
  | `ac_pro_integrate("x^2")` | 1,000,000 | **0.343 µs** |
  | `ac_pro_integrate("x*e^x")` | 100,000 | **1.000 µs** |

  上表是典型用例，不是上界。换矩阵后单次耗时差一个数量级 —— 幂迭代收敛
  速度取决于特征值比值，与框架开销无关：2×2 单位阵 0.070 µs、
  2×2 Hilbert-2 0.230 µs、**最坏 2×2 `diag(2,3)` 1.070 µs**（比值 2/3 需
  ~30 轮）、3×3 Hilbert-3 0.530 µs、**最坏 4×4 2.780 µs**（`r ≈ 0.84`，
  这是 `PRO_EIG_MAX_ITER = 400` 的推导依据）。常见 2×2/3×3 输入全部
  低于 2 µs/op，最坏 1~3 µs/op，未观察到数量级退化。

### Verification

- C 全量：**259 checks / 0 failed，RESULT: PASS**；`-Werror` 零警告。
- `.bss` 三层账本全部命中：kernel **41456** / runner **41488** / upstream **39408**。
- 退化构建（`AC_PRO_DEGRADED=1`）：**38672 B**，两个新 TU 编译成空对象、零导出符号。
- 上游回归：16 套件 / 1171 项测试全绿，`git status -- C/128KB` 为空。
- `nm pro_runner.exe`：`ac_pro_eig_all` 与 `ac_pro_integrate` 各只有一个 `T` 符号。

### Known issues

- **`x^-1` 返回 EPARSE**：`1/x` 正常返回 `ln(x)`，但负指数写法不支持。补上它需要
  在解析层改写为除法并牵动节点预算，收益不足以抵风险。
- **`sin(cos(x))` 返回 ETABLE(−2)**：嵌套超越函数无初等原函数，表匹配失败后
  直接拒绝，是正确行为而非缺陷。
- **A5 只接受实对称矩阵**：Hotelling 收缩在非对称矩阵上不保谱，非对称输入返回 −2。
- **B2 只做一层分部积分**：`∫x²·eˣ dx` 这类需要两层的会被 ETABLE 拒绝；递归展开
  会突破 96 节点额外预算。

### Deferred

- **JS/128KB 的功能扩展推迟到 v2.0.0**：多变量多项式层（`core_multivar.js`）、
  高阶三角函数 `asec/acsc/acot/sech/csch/coth`、复数域 `cSqrt/cExp/cLn/cPowInt/cAsinh/cAcosh/cAtanh/cAbs`
  均已实现并通过 175 条测试，但**不随本版发布**，与 JS 侧其它跨端改动一并在
  v2.0.0 推出。`JS/128KB/` 在本版保持 v1.1.0 状态，逐字节未动。

## v1.1.0 (2026-10-21)

C/128KB 零堆内存压缩：`.bss` **57.64 KiB → 38.48 KiB（39.4 KB）**，减少 19,616 B（**−33.24%**）。
功能零变更、性能零衰退（四个 stage 全部落在 ±3% 噪声带内）。

### Added

- **`ac_scratch.h` 编译期 union overlay 机制**：20 个模块私有 scratch 池折叠进一个顶层
  `union ac_scratch_t`（峰值 8224 B），C99 §6.7.2.1p14 保证每个成员自偏移 0 开始，
  等价于「编译期段重叠」——零运行时开销、零 API 变更、无 arena、无链接脚本、无编译器插件。
- **`ac_scratch_cfg.h` 零依赖开关头**：`AC_SCRATCH_UNION`（总开关）+
  `SYM` / `MAT` / `CALC` / `RATROOT` / `BIGINT` / `STK` / `LRHS` 七个分组开关，
  任一置 0 即恢复原始静态数组，源码无需改回。
- **`ac_scratch.c`**：`g_ac_scratch` / `g_ac_stk` 两个符号的唯一定义处。
- **C99 编译期断言**：`AC_STATIC_ASSERT` 守住 `sizeof(ac_scratch_t) >= 8224`、
  栈 union 覆盖求值栈、矩阵侧容纳增广矩阵三个不变量。
- **`tests/bench/result-c128-1M-v100.csv`**：v1.0.0 同机基线压测数据，供后续版本对比。

### Changed

- **CRC32 表改为 `static const`**：256 × 4 B 由 `.bss`（可变、懒初始化）移入 `.rdata`（ROM）。
- **Base64 反查表改为 `static const`**：256 × 1 B 同样移入 `.rdata`。
- **token 原地切片**：等号两侧不再拷入 `g_lhs_token` / `g_rhs_token`（2 × 1024 B 已移除），
  直接切片 `g_token_pool` —— `ac_to_rpn` 的 `src` 本就是 `const`，拷贝是纯冗余。
  钳制逻辑（`lhs_n` / `rhs_n` 取 `min(count, half)`）逐字保留，可观测行为不变。
- **`g_op_stack` / `g_eval_stack` 栈别名**：二者以 `g_rpn_token` 交接、永不并发，
  合并为 `union ac_stk_u`（1024 B），省 512 B。
- **符号模块三视图折叠**：`ac_sym_derivative` 用 `{stack, dmap}`、`ac_sym_eval` 用
  `{stack, val}`、`ac_sym_equal` 用 `{pa, pb}`，三者永不并发，14336 B → 8224 B。
- **矩阵 / 微积分 / 有理根 / 大整数 scratch 并入 union**：全部被 8224 B 峰值吸收，额外成本 0。
- **`ac_check_memory_watermark()` 口径同步**：水位估算改为按实际静态占用计算
  （栈 union 只计一次、lhs/rhs 不再计入），分三个编译期分支。
- **`tests/bench/bench-c.c` / `bench-c-makefile`**：压测夹具跟进 scratch 重构
  （`__has_include("ac_scratch.h")` 分支 + SOURCES 补 `ac_scratch.c`）。

### Fixed

- **CRC32 静默错值隐患**（严重，无崩溃、无错误码）：`s_crc_table` / `s_b64_rev` 原为
  懒初始化缓存 + `ready` 标志。若并入 union，`ac_crc32()` → `ac_mat_inverse()` → `ac_crc32()`
  的序列会让第二步覆写表、第三步因 `ready` 仍为 1 而跳过重建，**静默返回错值**。
  实测 `crc32("123456789")` 从 `CBF43926` 变为 `C4D28698`；在 `[compute][checksum]` 循环里
  从第 2 轮起退化为常量 `FFFFFFFF`，完整性校验静默变成 no-op。
  既有 1000+ 校验无法发现（唯一的 `ac_util_reset()` 在最后一个 suite 末尾）。
  **修复方式**：两表改为 `static const` 存 `.rdata`，物理不可写，失效模式从「靠纪律避免」
  变为「由构造消除」。该方案比「并入 union」多 144 B，但消除隐患。
- **头文件宏展开炸弹**：`ac_parser.h` 的 `extern double g_eval_stack[EVAL_STACK_SIZE];`
  在栈别名宏生效后会展开成 `extern double (g_ac_stk.ev)[...]` 这种非法语法，
  此前只因 `ac_parser.h` 恰好先于 `ac_scratch.h` 被包含而侥幸编译通过。
  新增 `ac_scratch_cfg.h`（零依赖）供模块头文件查询开关，并给相关 `extern` 加守卫。
- **`ac_ratroot.c` 的 `s_rr_count` 定义位置**：从 scratch 守卫块内移出，保持为普通标量
  （它不是存储池，不该被 overlay）。

### Performance

- **静态内存**：`.bss` 59024 → **39408 B（−19616，−33.24%）**；
  `.data + .bss` 59296 → **39680 B（−33.11%）**；门禁占用率 60.03% → **40.09%**。
- **代码体积**：真正的代码段 `.text` **146424 → 146376 B（−48 B）**，零代码膨胀；
  `size` 报告的 text 列 201480 → 202804 B（+1324）全部来自 `.rdata` 增加 1392 B
  （CRC32 表 1024 + Base64 反查表 256 + 对齐），即常量表从 RAM 搬进 ROM 的代价。
- **运行性能（100 万次 × 4 stage，v1.0.0 vs v1.1.0，16 次采样中位数）**：

  | stage | v1.0.0 µs/op | v1.1.0 µs/op | 变化 | 判定 |
  |:---|---:|---:|---:|:---|
  | parse | 0.0190 | 0.0191 | +0.53% | ✅ 噪声内 |
  | eval（解析+求值） | 0.0624 | 0.0635 | +1.92% | ✅ 噪声内 |
  | solve | 0.4586 | 0.4441 | **−3.15%** | ✅ 小幅提速 |
  | exact | 0.2607 | 0.2533 | **−2.84%** | ✅ 小幅提速 |
  | **均值** | **0.8007** | **0.7801** | **−2.57%** | ✅ **无回归** |

  solve / exact 的提速与工作集收缩（`.bss` 减 19.6 KB）带来的缓存局部性改善一致。

### Verification

- 16 模块 **1171 项测试全绿**（`LUT=1` 为 1184），`-Werror` 零警告。
- 跨版本对拍：与 `git archive HEAD`（v1.0.0）基线 **53 行 smoke CSV 逐位一致**（`diff` 为空）。
- 跨语言对拍：C128 vs JS128 **50/50 用例一致**（含错误码与五分量精确比对，0 处 > 1e-9）。
- 毒化测试：每次 API 调用前把 `g_ac_scratch` / `g_ac_stk` 填 `0xFF`，校验和不变
  ⇒ 无函数在写入前读取 scratch。
- 钳制测试：10–600 项的等式、`=` 在首/尾/缺失，输出与基线一致。
- 对齐：31 个 double/int64 池全部 `addr % 8 == 0`。
- 优化级别扫描：`-O0/-O1/-O2/-Os/-O3` 全部 16/16，布局稳定。

> 完整设计论证、风险矩阵、回退策略与四个被实测推翻的初始假设见
> 设计说明。

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
