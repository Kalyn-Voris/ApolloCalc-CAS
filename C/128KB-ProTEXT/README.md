# C/128KB-ProTEXT — C99 Pro 数学层

> 基于冻结上游的零 GC 高阶数学层：上游 `C/128KB/` 内核逐字节冻结，Pro 层在同一块
> 静态存储上叠加多变量代数、数值线性代数、符号微积分与高精度计算：零 malloc、
> 零递归、`.bss` 分毫必较。

- **组件版本**：v1.8.0（本目录 [VERSION](VERSION)，对应全局节点 v2.6.0；双轨口径不必同号）
- **上游零改动**：16 个上游模块经 3 行接线桩接入，`git status -- C/128KB` 恒空
- **零 GC**：全部静态分配，`-Wl,--wrap=malloc/free` 二进制级验证；错误码一律走返回值，零 static 对象
- **静态内存**：`.bss` **42608 B**（内核口径，精确账本，见 [Memory Ledger](#memory-ledger)）

## Overview

ApolloCalc-CAS 的 C99 128KB 产品线分两层。上游 `C/128KB/`（v1.1.0，冻结）提供解析求值、
1..4 次方程精确根式、12 元线性方程组等基础内核；本目录在其上叠加 19 个功能翻译单元、
78 个公开 API（[ac_pro.h](src/ac_pro.h)），把能力面推进到多变量代数、一般矩阵特征值、
SVD、Sturm 序列与 180 位定点小数。

两层之间只有三种接触面：3 行接线桩、一处宏重命名拦截（`ac_guard_degree` 次数路由：
1..4 次直接调用上游原件保证逐位一致，5..8 次走 Pro 新增路径）、以及内存水位门禁的
重写。上游源码一个字节不动；退化构建（`AC_PRO_DEGRADED=1`）与上游逐位同构：
text/data/bss 三段完全一致，测试输出 636 字节逐字节相同。

## Core Features

**多项式与方程**，从精确到数值的完整梯队：

- 5..8 次方程 Durand–Kerner 数值求根（Aberth 初始化）。重根场景靠**残差判据**兜底
  而不是放宽步长阈值：m 重根附近步长只能到 ε^(1/m)，残差 1e-15 级即放行，判据未过
  依旧报错。
- **Sturm 序列**实根精确计数与隔离（1..8 次，区间端点支持 ±∞）：链系数膨胀由
  大整数层承载，计数与数值求根交叉校验；输出为精确二分区间，不经过浮点。
- square-free 分解给出 (根, 重数) 列表，重数由整数运算精确决定，浮点不参与合并。
- 二元方程组 **Groebner 基消元**（M4GB，lex，deg ≤ 4）→ 全部解（含复数解），每个
  候选代回原方程组残差 ≤ 1e-9 才输出；理想 = 全环时返回「0 解」而非报错。

**多变量代数**（同一块 6 槽项池承载）：多变量 GCD（子结式 PRS，主变量自动分层、
退化换轴）、content 提取式因式分解（每个候选因子重新展开并验证整除）、Sylvester
结式、多变量符号求导与多项式积分。

**矩阵**：LU（部分主元，P·A = L·U 重建误差 0 量级）、实对称特征向量（幂迭代 +
零空间消元两条路径互相印证，Hotelling 收缩 deflation 求全部特征对）、一般实矩阵
全部特征值（Householder → Hessenberg → **Francis 隐式双移 QR**，复特征值成对给出，
50 轮预算不收敛返错误码）、**one-sided Jacobi SVD**（m,n ≤ 12，C 独有；JS 面因
压缩内核触熔断线顺延 v2.7）：出口双闸门（重构残差 + 正交性）验证不过不输出。

**微积分**：隐函数求导在不动上游结构体的前提下，用 memcpy 往 `ac_sym_node_t`
的 4 字节填充洞里存变量值（六条编译期断言把布局钉死，`.bss` 零增长）；符号积分
（128 项 `static const` 表 + 固定两层分部，`∫x⁶·sin x dx` 可解、需三层的拒绝）；
常系数线性齐次 ODE 通解：特征多项式走 1..4 次精确根式，共轭对渲染 cos/sin、
重根渲染 `x^k·e^{λx}`，非齐次/变系数/阶 ≥5 一律按错误码拒绝。

**高精度与稀疏**：180 位任意精度定点小数四则（容量熔断：放不进 180 位就报
EOVER，不返回错值）；稀疏线性方程组（n ≤ 32 / nnz ≤ 320，消元填充与输入共享
总容量）；复域超越函数 15 API（复三角/双曲/反三角/反双曲/全复幂/主值 k 次根/
复 Gamma，主值分支与割线侧冻结成契约）；单变量有理函数工具（GCD/约分/部分分式，
浮点系数拒绝，分解回乘验证残差不过即报错）。

贯穿全部功能的同一条纪律：**诚实拒绝**。sweep 预算耗尽、闸门验证不过、超域输入
一律返回错误码，不输出未验证结果。

## Memory Ledger

内核口径 `.bss` **42608 B**，账本闭合（`bss-check` 实测逐字命中）：

```
38672   16 个上游模块（上游内核口径，冻结未动）
  + 80  g_var_val[10]（会话态，不入 union）
+ 3848  g_ac_pro_scratch（Pro union，峰值视图 sparse）
  +  8  对齐填充
= 42608   占 112 KiB 门禁的 37.15%，余量 72080 B
```

Pro 层全部新功能共享一个编译期 `union`：13 个互斥工作区视图折叠进同一块
3848 B 存储，每块内存只在互斥的调用路径间重叠（跨调用会话态排除在外）。
v2.1.0 起 `sparse` 成为峰值，此后 `gb`（1888）/ `dec`（480）/ `rat`（464）/
`schur`（2744）/ `sturm`（2912）/ `svd`（3760）全部落在峰值线内被吸收，
**新功能零 `.bss` 增长**，这个判据从 v2.1.0 到 v2.6.0 连续六个版本成立。

| 视图 | 尺寸 | 承载能力 |
|:---|---:|:---|
| `sparse` | **3848 B** | 稀疏线性方程组（← 峰值） |
| `svd` | 3760 B | one-sided Jacobi SVD（margin 88 B） |
| `sturm` | 2912 B | Sturm 序列实根计数与隔离 |
| `schur` | 2744 B | 一般实矩阵特征值（Francis QR） |
| `mv` | 2688 B | 多变量项池（6 槽，v1.3–v2.0.0 峰值） |
| `eig` | 2592 B | 实对称特征向量 + deflation |
| `lu` | 2352 B | LU 分解 |
| `gb` | 1888 B | 2 变量 Groebner 基 |
| `sym` | 1792 B | 隐函数求导工作区 |
| `res` | 1152 B | Sylvester 结式矩阵 |
| `dec` | 480 B | 180 位定点小数（5 槽） |
| `rat` | 464 B | 有理函数工具（6 槽） |
| `nr` | 264 B | 5..8 次数值求根 |

每个视图尺寸由 `ac_pro_scratch.h` 里的编译期断言**精确钉死**（`==` 而非 `>=`，
上游语义允许静默增长，Pro 不允许；受控实验里把 union 人为撑大，编译立刻失败）。
两条口径并行受控：runner 口径 42640 B（+32 为测试 TU）、
上游对照 39408 B（防上游漂移）、退化构建 38672 B（Pro 增量全部编译出）。

## Verification

一条命令跑完全部硬指标：

```bash
cd C/128KB-ProTEXT
powershell -NoProfile -ExecutionPolicy Bypass -File verify.ps1
```

- `verify.ps1` **43 项 verdict 全绿**：构建、四层 `.bss` 账本精确比对、负向编译
  （非法宏组合必须编不过）、8 个关键符号唯一性、上游零改动双证
- `pro_runner` **935 checks, 0 failed**（含 SVD 16 例 243 checks）+
  `cx_runner` 复变专项 **134 checks**
- 上游回归全绿（1171 项测试 / 16 套件）；退化构建与上游输出逐字节一致
- 跨语言契约冻结：[docs/contracts/](../../docs/contracts/)（svd / sturm / sysolve /
  complex-ext / sqfree / ode 等九份）与 [tests/](../../tests/) 共享 JSON，
  C 与 JS 两侧对同一输入给出一致结果

## Quick Start

```c
#include "ac_pro.h"

double A[6] = {3, 0, 0, 2, 0, 0};          /* 3x2 行主序 */
ac_pro_svd_all(A, 3, 2);                    /* one-sided Jacobi SVD */
for (int k = 0; k < ac_pro_svd_count(); k++)
    printf("sigma[%d] = %.6f\n", k, ac_pro_svd_value(k));
/* → sigma[0] = 3.000000, sigma[1] = 2.000000 */

char out[1024];
ac_pro_sturm_isolate("x^2-2", out, sizeof out);       /* → [[-4, 0], [0, 4]] */
ac_pro_sysolve("x^2+y^2-4", "x-y", out, sizeof out);
/* → [(1.414213562373, 1.414213562373), (-1.414213562373, -1.414213562373)] */
ac_pro_dec_div("1", "7", out, sizeof out);            /* → 0.142857142857...（共 180 位，返回 INEXACT） */
ac_pro_ode("y''+3y'+2y=0", out, sizeof out);          /* → y = C₁*exp(-1*x) + C₂*exp(-2*x) */
```

以上输出为实机实测（`1/7` 的定点除法共输出 180 位，此处缩略）。构建与回归
（MinGW-w64 / GCC，零外部依赖）：
```bash
mingw32-make              # pro_runner + pro_kernel（-Werror 零警告门禁；Git Bash 下加 SHELL=cmd.exe）
./pro_runner.exe          # 935 checks + Zero-GC --wrap 验证
mingw32-make bss-check    # .bss 四层账本：42608 / 42640 / 39408 / 38672
```

移植到 Keil / ESP-IDF、逐 API 文档与契约细节见
[docs/getting-started.md](../../docs/getting-started.md) 与
[docs/c99-128kb.md](../../docs/c99-128kb.md)；组件版本沿革见
[CHANGELOG.md](CHANGELOG.md)。

---

[MIT](../../LICENSE) © 2026 ApolloCalc-CAS Contributors
