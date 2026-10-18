# C/128KB — C99 增强版

C99 对等 JS 128KB + 大整数 / 矩阵 / 特殊函数 / 符号求导四大 C 独有能力。

- **状态**：**已完成（v1.0.0，2026-10-13）**
- **堆限制**：128KB（全部静态分配，`.bss` 段，禁止 malloc/free）
- **目标平台**：Cortex-M7 / ESP32-S3 级别 MCU，或任何有 128KB 以上 RAM 的嵌入式 Linux
- **编译门禁**：`-std=c99 -Wall -Wextra -Wstrict-prototypes -Werror -O2`（零警告）

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

- CRC32（0xEDB88320 反射多项式，懒初始化表）
- SHA-256（FIPS 180-4）、Base64（RFC 4648）
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

`size test_runner.exe`（默认构建）：

| 段 | 字节 |
|:---|---:|
| `.text` | 201480 |
| `.data` | 2716 |
| **`.bss`（静态池）** | **59024（57.6KB）** |
| 水位门禁 | 98304（128KB × 75%） |

`LUT=1` 构建：`.text` 203268 / `.bss` **75440（73.7KB）**（sin/cos 两表 +16416B），仍低于门禁。

主要静态池（由 `ac_types.h` 与各模块声明推导，与 `nm --size-sort` 实测一致）：

| 池 | 元素数 × 单元素 | 字节 |
|:---|:---|---:|
| `g_token_pool` | 512 × 4 | 2048 |
| `g_number_pool` | 256 × 8 | 2048 |
| `g_eval_stack` | 128 × 8 | 1024 |
| `g_matrix_pool` | 156 × 8 | 1248 |
| `g_rpn_token` | 512 × 4 | 2048 |
| `g_op_stack` | 128 × 4 | 512 |
| `g_lhs_token` + `g_rhs_token` | 256 × 4 × 2 | 2048 |
| （表达式池七项小计） | — | 10976 |
| 根结果池（12 槽 × re/im/五分量/exp/valid） | — | 672 |
| `g_coeffs` | 5 × 8 | 40 |
| **符号模块小计** | — | **27008** |
| ↳ `g_sym_nodes` | 512 × 24 | 12288 |
| ↳ `s_sym_stack` | 1032 × 4 | 4128 |
| ↳ `s_sym_val` | 512 × 8 | 4096 |
| ↳ `s_sym_pa` / `s_sym_pb` / `s_sym_dmap` | 512 × 4 × 3 | 6144 |
| ↳ 因式分解输出数组（五分量 × 8 + 余式） | — | 352 |
| **矩阵模块小计** | — | **10368** |
| ↳ `g_mat_a/b/c/work` | 144 × 8 × 4 | 4608 |
| ↳ `s_mat_aug`（逆矩阵增广 12×24） | 288 × 8 | 2304 |
| ↳ `s_mat_mul` / `s_mat_tr` / `s_qr_a` | 144 × 8 × 3 | 3456 |
| **大整数小计** | — | **1108** |
| ↳ `g_bi_*` 五个操作数/结果 | 96 × 5 | 480 |
| ↳ `s_bi_da/db/q/r` | 96 × 4 | 384 |
| ↳ `s_bi_prod`（2×LIMBS）/ `s_bi_t2` | 160 + 84 | 244 |
| 泰勒工作区 `s_taylor_work` | 256 × 8 | 2048 |
| `g_taylor_coeffs` | 16 × 8 | 128 |
| 有理根工作区（cand 64 + work 16 + trial 16 + divs 128） | — | 1024 |
| 有理根结果栈（values/num/den × 8） | — | 192 |
| CRC 表 + Base64 反查表 | 256 × 4 + 256 × 1 | 1280 |
| `g_base_buf` | 64 × 4 | 256 |
| `g_nt_primes` / `g_nt_exps` | 16 × 8 / 16 × 4 | 192 |
| **上列池合计** | — | **55292** |
| CRT/GS 记录、emu 桩、对齐填充 | — | 3732 |
| **实测 `.bss` 总计** | — | **59024** |

## Zero-GC 验证

三层保障：

1. **静态分配**：所有池为 `static` 全局数组或模块内 `static` 工作区，无 VLA、无递归
2. **链接期拦截**：`-Wl,--wrap=malloc -Wl,--wrap=free`
3. **运行期强制**：`test_main.c` 的 `__wrap_malloc` / `__wrap_free` 在 `s_kernel_live`（main 进入到返回）期间命中即 `abort()`；CRT 启动期（main 之前）的合法堆调用经 `GetProcAddress` 转发到 msvcrt，因此二进制内不残留任何静态 malloc/free 符号

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

**vs JS 128KB**：**功能对等**（JS 128KB 的全部功能均已实现），并额外提供上述四大 C 独有模块。唯一不提供的是 `guardMultivariateNonlinear`——该函数在 JS 侧已定义但未被任何求解流水线调用（死代码）。

## 测试规模

16 模块 **1171 项测试全绿**，0 失败，编译零警告（`LUT=1` 为 1184）。

| 模块 | pass | 模块 | pass | 模块 | pass | 模块 | pass |
|:---|---:|:---|---:|:---|---:|:---|---:|
| parser | 15 | ratroot | 39 | numtheory | 168 | matrix | 41 |
| evaluator | 53 | newton | 12 | base | 142 | special | 55 |
| exact | 128 | math | 99 | bigint | 70 | symbolic | 55 |
| solvers | 78 | complex | 63 | calculus | 59 | util | 94 |

跨版本一致性：**C128 vs JS128 = 50/50 用例一致**（根数 100%、数值 100% < 1e-9），见 [tests/bench/SMOKE-DIFF-128.md](../../tests/bench/SMOKE-DIFF-128.md)。

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
