# C/128KB — C99 增强版

C99 对等 JS 128KB + 大整数 / 矩阵 / 特殊函数 / 符号求导四大 C 独有能力。

- **状态**：**已完成（v1.1.0，2026-10-21）**
- **堆限制**：128KB（全部静态分配，`.bss` 段，禁止 malloc/free）
- **目标平台**：Cortex-M7 / ESP32-S3 级别 MCU，或任何有 128KB 以上 RAM 的嵌入式 Linux
- **编译门禁**：`-std=c99 -Wall -Wextra -Wstrict-prototypes -Werror -O2`（零警告）
- **静态内存**：**`.bss` 39408 B（38.48 KiB / 39.4 KB）**，`.data + .bss` = 39680 B（38.75 KiB）

> v1.1.0 起引入**零堆内存（Zero-GC）scratch 复用**：20 个模块私有工作区折叠进一个
> 编译期 `union`，`.bss` 由 59024 B 降至 **39408 B（−33.24%）**。设计论证、实测轨迹与
> 回退策略见设计说明。

## 零堆内存（Zero-GC）与编译器级 union 重叠

本版本把「不分配」从**运行时约定**推进到**编译期结构**：
| 层次 | 手段 | 保障 |
|:---|:---|:---|
| 静态分配 | 全部池为全局 / 模块内 `static` 数组，无 VLA、无递归 | 编译期确定 `.bss` 大小 |
| **编译器级 union 重叠** | 20 个互斥 scratch 池并入 `ac_scratch_t`；C99 §6.7.2.1p14 保证每个成员自偏移 0 开始 | 零运行时开销、零 API 变更 |
| 栈别名 | `g_op_stack`（int32×128）与 `g_eval_stack`（double×128）共享 `union ac_stk_u`（1024 B） | 二者以 `g_rpn_token` 交接，永不并发 |
| 原地切片 | 等号两侧不再拷入 `g_lhs_token`/`g_rhs_token`，直接切片 `g_token_pool` | 省 2048 B |
| 只读化 | CRC32 表 / Base64 反查表改为 `static const`（`.rdata`） | 省 1280 B `.bss`，且**物理不可写** |
| 链接期拦截 | `-Wl,--wrap=malloc -Wl,--wrap=free` | 二进制内不残留 malloc/free 符号 |
| 运行期强制 | `__wrap_malloc` 在 `s_kernel_live` 期间命中即 `abort()` | 零分配是硬性校验，不是约定 |

**union 的互斥性靠结构事实，不靠运行时检查**：`ac_sym_derivative` / `ac_sym_eval` /
`ac_sym_equal` 三组视图在同一调用路径上永不并发（无递归 + 模块边界清晰），故共享
一块 8224 B 存储。所有开关（`AC_SCRATCH_UNION` 及 `SYM/MAT/CALC/RATROOT/BIGINT/STK/LRHS`）
都可用 `-D...=0` 单独回退，源码不用改。

> **准入判据（教训）**：能进 union 的不是「谁调用它」，而是「它是否持有跨调用可变状态」。
> 懒初始化缓存（带 `ready` 标志的查表）**必须排除**：放进 union 会让 CRC32 在被其它模块
> 覆写后静默返回错值（实测 `CBF43926` → `C4D28698`），且 1000+ 测试全绿也发现不了。

## 功能清单

### 核心（对等 JS 128KB）

| 模块 | 内容 |
|:---|:---|
| `ac_parser` | UTF-8 多字节符号还原（π/×/÷/√/−）、隐式乘法、科学计数法、最长前缀函数名匹配 |
| `ac_evaluator` | Shunting-yard 转 RPN + 显式栈求值、12 函数、DEG/RAD、池化无堆 |
| `ac_exact` | 五分量精确形式 `(a + b√c)/d × 10^e`，int64 + safe 溢出原语 |
| `ac_solvers` | 二次精确组装 / 三次（不可约三角法）/ 四次（笛卡尔分解降次链）、12 元线性方程组、残差回验、preCheck + guardDegree + 内存水位三重门禁 |
| `ac_ratroot` | 有理根定理（枚举 p 整除常数项、q 整除首项）+ 综合除法验证 + 显式栈迭代降次 |
| `ac_math` | 三角 / 反三角 / 对数 / 指数 / 绝对值 + 双曲 / 反双曲（sinh..atanh）；可选 sin/cos 查表（`AC_MATH_USE_LUT`，1024 项） |

### 数学函数

- 三角 / 反三角 / 对数 / 指数 / 绝对值（`ac_apply_function` 统一入口）
- 双曲 / 反双曲：`sinh` / `cosh` / `tanh` / `asinh` / `acosh` / `atanh`
- 复数（12 个 API）：`cAdd` / `cSub` / `cMul` / `cDiv` / `cAbs` / `cArg` / `cConj` / `cSqrt` / `cPowInt` + `cExp` / `cLn` / `cPowReal`
- 三角函数查表：编译期开关 `AC_MATH_USE_LUT`（1024 项，实测最大误差 ~4.7e-6）

### 微积分（`ac_calculus`）

- 数值微分：一阶（中心差分）/ 二阶（五点公式）
- 数值积分：定积分（自适应复合辛普森）
- 数值极限：左右逼近
- 泰勒展开：最高 16 阶，系数缓存 + Horner 求值

### 显示（`ac_base`）

- 进制转换 2 / 8 / 10 / 16（对齐 JS 128KB `decToBase` 行为）
- 分数显示三模式：假分数 `3/2` / 带分数 `1 1/2` / 自动（C 独有）
- SI 词头：17 档（y..Y，含 µ）；工程记数法（尾数 [1,1000) + 3 的倍数指数）

### 工具（`ac_util`）

- CRC32（0xEDB88320 反射多项式，**编译期常量表**，存 `.rdata`）
- SHA-256（FIPS 180-4）、Base64（RFC 4648，**常量反查表**）
- 定点数 `int64 × 10^6`（safe 原语溢出预检）
- 单位换算：长度 7 / 质量 5 / 时间 4 / 温度 3（跨类别拒绝）
- 物理常数库 6 项：光速 / 普朗克 / 阿伏伽德罗 / 元电荷 / 玻尔兹曼 / 万有引力

### C 独有（JS 两版均无）

- **大整数 `ac_bigint`**：20 limb × base 1e9（180 位十进制容量），加减乘除 + 比较 + 阶乘（n ≤ 100）+ 斐波那契（n ≤ 200）
- **矩阵 `ac_matrix`**：最大 12×12；乘法 / 转置 / 秩 / 行列式 / 逆矩阵 / 特征值（幂法）/ QR 分解
- **特殊函数 `ac_special`**：Gamma / lgamma / Beta / erf / 不完全 Gamma / Bessel J_n
- **符号求导 + 因式分解 `ac_symbolic`**：扁平数组表达式树（512 节点）符号求导（显式栈后序，零递归）；整系数多项式有理因式分解

## 构建与测试

```bash
mingw32-make                # 默认构建（libm 三角函数）→ test_runner
./test_runner.exe           # 1171 项测试（16 模块）

mingw32-make LUT=1          # 启用 sin/cos 查表 → 1184 项测试（math 99 → 112）
mingw32-make clean          # 清理（Windows 走 del /Q，非 rm）

mingw32-make smoke_runner   # 跨版本冒烟 runner（独立 main）
./smoke_runner.exe > ../../tests/bench/c128-smoke.csv
```

跨版本比对：

```bash
node ../../tests/bench/js128-smoke.js > ../../tests/bench/js128-smoke.csv
node ../../tests/bench/diff-smoke128.js     # → tests/bench/SMOKE-DIFF-128.md
```

## 内存账本（实测）

`size test_runner.exe`（默认构建，v1.1.0）：

| 段 | v1.1.0 字节 | v1.0.0 字节 | 变化 |
|:---|---:|---:|---:|
| `.text`（`size` 口径，含 `.rdata`） | 202804 | 201480 | +1324 |
| ↳ 其中真正的代码段 `.text` | 146376 | 146424 | **−48** |
| ↳ 其中 `.rdata`（常量表） | 48048 | 46656 | +1392 |
| `.data` | 2716 | 2716 | 0 |
| **`.bss`（静态池）** | **39408（38.48 KiB / 39.4 KB）** | 59024（57.64 KiB） | **−19616（−33.24%）** |
| **`.data + .bss`（RAM）** | **39680（38.75 KiB）** | 59296 | **−19616（−33.11%）** |
| 水位门禁 | 98304（128KB × 75%） | 98304 | — |
| **门禁占用率** | **40.09%** | 60.03% | −19.94 pp |

`LUT=1` 构建：`.text` 204592 / `.bss` **55824（54.5 KiB）**（sin/cos 两表 +16416 B 入 `.bss`），
仍低于门禁；相对 v1.0.0 的 75440 B 同样降低 19616 B。

### 内存预算表（v1.1.0 池构成）

union overlay 生效后，20 个 scratch 池塌缩为 **`g_ac_scratch` 一块 8224 B + `g_ac_stk` 一块 1024 B**。
下表按「逻辑池」列出，并标注它实际落在哪块存储上：

| 池 | 逻辑大小 | 实际存储 | 字节 |
|:---|---:|:---|---:|
| `g_token_pool` | 512 × 4 | 独立 | 2048 |
| `g_number_pool` | 256 × 8 | 独立 | 2048 |
| `g_rpn_token` | 512 × 4 | 独立 | 2048 |
| `g_matrix_pool` | 156 × 8 | 独立 | 1248 |
| `g_ac_stk`（`g_op_stack` ∪ `g_eval_stack`） | max(512, 1024) | **union** | **1024** |
| `g_lhs_token` + `g_rhs_token` | — | **已移除**（原地切片） | **0** |
| `g_sym_nodes` | 512 × 24 | 独立 | 12288 |
| `s_sym_stack` / `s_sym_val` / `s_sym_dmap` / `s_sym_pa` / `s_sym_pb` | 14336 | **`g_ac_scratch`** | **0（吸收）** |
| `s_mat_aug` / `s_mat_mul` / `s_mat_tr` / `s_qr_a` | 5760 | **`g_ac_scratch`** | **0（吸收）** |
| `s_taylor_work` | 2048 | **`g_ac_scratch`** | **0（吸收）** |
| 有理根 `s_rr_cand/work/trial/divs` | 1024 | **`g_ac_scratch`** | **0（吸收）** |
| 大整数 `s_bi_r/da/db/t2/q/prod` | 428 | **`g_ac_scratch`** | **0（吸收）** |
| CRC 表 + Base64 反查表 | 1280 | **`.rdata`（const）** | **0** |
| 根结果池（12 槽 × re/im/五分量/exp/valid） | 672 | 独立 | 672 |
| `g_mat_a/b/c/work` | 144 × 8 × 4 | 独立 | 4608 |
| `g_bi_*` 五个操作数/结果 | 96 × 5 | 独立 | 480 |
| `g_taylor_coeffs` | 16 × 8 | 独立 | 128 |
| 有理根结果栈（values/num/den × 8） | — | 独立 | 192 |
| `g_base_buf` | 64 × 4 | 独立 | 256 |
| `g_nt_primes` / `g_nt_exps` | 16 × 8 / 16 × 4 | 独立 | 192 |
| `g_coeffs` | 5 × 8 | 独立 | 40 |
| **上列池合计** | — | — | **39408 − 3732 = 35676** |
| CRT/GS 记录、emu 桩、对齐填充 | — | — | 3732 |
| **实测 `.bss` 总计** | — | — | **39408** |

> **`g_ac_scratch` 只算一次 8224 B**：`sym_eval` 视图（`stack` 4128 + `val` 4096 = 8224）是并发峰值，
> `sym_deriv`（6176）、`sym_equal`（4096）、`mat`（5760）、`calculus`（2048）、`ratroot`（1024）、
> `bigint`（428）全部被它吸收。`ac_scratch.h` 里有 `AC_STATIC_ASSERT(sizeof(ac_scratch_t) >= 8224)`
> 在编译期守住这个不变量。

## 编译门禁

三层门禁，缺一不可：

```bash
# 1) 编译期：零警告（-Werror 把警告升级为错误）
mingw32-make                 # -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror -O2

# 2) 链接期：拦截堆分配符号
#    LDFLAGS = -Wl,--wrap=malloc -Wl,--wrap=free
nm test_runner.exe | grep -iE "malloc|free|realloc" | grep -v __wrap   # 应无输出

# 3) 运行期：命中即 abort（test_main.c 的 __wrap_malloc / __wrap_free）
./test_runner.exe            # 16 模块 1171 项测试全绿
```

**内存门禁**：`ac_check_memory_watermark()` 在每次求解前估算静态占用，超过
`98304 B`（128KB × 75%）即置 `g_solve_error = 4`。v1.1.0 实测占用 39408 B，
仅用掉门禁的 **40.09%**，余量 58896 B。

## Zero-GC 验证

三层保障：

1. **静态分配**：所有池为 `static` 全局数组或模块内 `static` 工作区，无 VLA、无递归
2. **链接期拦截**：`-Wl,--wrap=malloc -Wl,--wrap=free`
3. **运行期强制**：`test_main.c` 的 `__wrap_malloc` / `__wrap_free` 在 `s_kernel_live`（main 进入到返回）期间命中即 `abort()`；CRT 启动期（main 之前）的合法堆调用经 `GetProcAddress` 转发到 msvcrt，因此二进制内无静态 malloc/free 符号

验证命令：

```bash
nm test_runner.exe | grep -iE "malloc|free|realloc" | grep -v __wrap   # 应无输出
```

## 与 C 64KB / JS 128KB 的差异

**vs C 64KB**（对等 JS 64KB + 数论/进制/工具三块 C 独有）：

- 精确形式三分量 → **五分量**（支持 `(a + b√c)/d` 混合形式与科学计数法）
- 新增双曲/反双曲、复数域扩展函数、数值微积分、泰勒展开
- 新增有理根检验与降次（三次/四次整数与分数根获得精确形式）
- 线性方程组 6 元 → **12 元**（矩阵池 156，根池 12 槽）
- 表达式池全部翻倍：token 512 / 数值 256 / 求值栈 128 / RPN 512 / 运算符栈 128
- preCheck 阈值翻倍：深度 ≤ 6 / 函数 ≤ 4 / 变量 ≤ 10
- 水位门禁 48KB → **96KB**
- 新增四大 C 独有模块：`ac_bigint` / `ac_matrix` / `ac_special` / `ac_symbolic`

**vs JS 128KB**：**功能对等**（JS 128KB 的全部功能均已实现），另提供上述四大 C 独有模块。唯一不提供的是 `guardMultivariateNonlinear`，该函数在 JS 侧已定义但未被任何求解流水线调用（死代码）。

## 测试规模

16 模块 **1171 项测试全绿**，0 失败，编译零警告（`LUT=1` 为 1184）。

| 模块 | pass | 模块 | pass | 模块 | pass | 模块 | pass |
|:---|---:|:---|---:|:---|---:|:---|---:|
| parser | 15 | ratroot | 39 | numtheory | 168 | matrix | 41 |
| evaluator | 53 | newton | 12 | base | 142 | special | 55 |
| exact | 128 | math | 99 | bigint | 70 | symbolic | 55 |
| solvers | 78 | complex | 63 | calculus | 59 | util | 94 |

跨版本一致性：**C128 vs JS128 = 50/50 用例一致**（根数 100%、数值 100% < 1e-9），见 [tests/bench/SMOKE-DIFF-128.md](../../tests/bench/SMOKE-DIFF-128.md)。
跨版本对拍：与 `git archive HEAD`（v1.0.0）基线 **53 行 smoke CSV 逐位一致**（`diff` 输出为空）。

## 性能（100 万次 × 4 stage）

v1.1.0 相对 v1.0.0 同机同编译器（gcc 8.1.0 `-std=c99 -O2 -Wall`）16 次采样中位数：

| stage | v1.0.0 µs/op | v1.1.0 µs/op | 变化 |
|:---|---:|---:|---:|
| parse | 0.0190 | 0.0191 | +0.53% |
| eval（解析+求值） | 0.0624 | 0.0635 | +1.92% |
| solve | 0.4586 | 0.4441 | **−3.15%** |
| exact | 0.2607 | 0.2533 | **−2.84%** |
| **全 stage 均值** | **0.8007** | **0.7801** | **−2.57%** |

四个 stage 全部落在 ±3% 噪声带内，**无可测性能衰退**；solve / exact 有小幅提速
（工作集收缩带来的缓存局部性改善）。详见 [tests/bench/BENCH-1M.md](../../tests/bench/BENCH-1M.md)。

## 开发状态

- [x] 骨架搭建 + 池扩容（token 512 / 数值 256 / 栈 128 / 矩阵 156 / 根池 12）
- [x] 五分量精确形式重写 + 求解层接线
- [x] 双曲 / 反双曲函数扩展
- [x] 复数域扩展（cExp / cLn / cPowReal）
- [x] 数值微积分（导数 / 积分 / 极限）+ 泰勒展开
- [x] 进制转换 + SI 词头 + 工程记数 + 分数显示
- [x] 有理根检验 + 降次 + 低次因式分解
- [x] C 独有：大整数 / 矩阵 / 特殊函数 / 符号求导
- [x] 全量回归对齐 `tests/`（冒烟 CSV + 跨版本 diff + 文档收尾）
- [x] **v1.1.0 零堆内存压缩**：union overlay + 栈别名 + 原地切片 + 常量表，`.bss` 59024 → **39408 B（−33.24%）**
