# C/128KB-ProTEXT · Pro v1.4.0 落地说明

> **基线**：ApolloCalc-CAS v1.1.0（`C/128KB/`，C99 / Zero-GC / 全静态分配）
> **上游状态**：`C/128KB/` **零改动**（`git status --porcelain -- C/128KB` 输出为空）
> **门禁**：`.bss` ≤ 114688 B（112 KiB = 128 KiB × 87.5%）
> **v1.2 范围**：阶段一（修上游门禁缺陷）+ 阶段二（Pro union + 起手三件套 B1/A4/A6）+ 阶段三（验证与对拍）
> **v1.3 增量**：方向 A 多变量层（A1 GCD / A2 因式分解 / A3 结式），复用同一块 union
> **v1.3.5 增量**：A5 实特征向量 + Hotelling 收缩 deflation、B2 符号积分（64 项表 + 一层分部积分）
> **v1.4.0 增量**（组件版本，对应全局版本节点 v2.2.0）：2 变量 Groebner 基（M4GB 逐对归约，
> lex，deg ≤ 4，膨胀熔断）+ `verify.ps1` 三处修复；gb 视图 1888 B 被 sparse 峰值完全吸收，
> `.bss` 零增长（内核口径 42 608 B），详见 [CHANGELOG.md](CHANGELOG.md)

---

## 0. 结论速览

| 指标 | 目标 | 实测 | 结论 |
|:---|:---|:---|:---|
| `sizeof(ac_pro_scratch_t)` | 2688 | **2688** | ✅ v1.3.5 未增长 |
| `sizeof(g_ac_pro_storage)` | 2784 = 2688 + 80 + 16 | **2784** | ✅ |
| 内核口径 `.bss` | 41456 | **41456** | ✅ 精确命中 |
| runner 口径 `.bss` | 41488 = 41456 + 32 | **41488** | ✅ |
| 上游对照 `.bss` | 39408 | **39408** | ✅ 未漂移 |
| 退化构建 `.bss` | 38672 | **38672** | ✅ 与上游同构 |
| 测试数 | — | **259 checks / 0 failed** | ✅ |
| 上游回归 | 全绿 | **1171 项测试 / 16 套件全绿** | ✅ |
| 上游目录改动 | 0 | **0** | ✅ |

**账本闭合**：`38672 + 80 + 2688 + 16 = 41456`，占 114688 的 **36.15%**，余量 73232 B。

v1.3.5 加入 A5 后 `eig` 视图为 2592 B，比 v1.3 的峰值 `mv`（2688 B）**小 96 B**，
被完全吸收，因此 union 尺寸与 `.bss` 一分未涨 —— 这是「union 吸收」判据的第二次兑现。

---

## 1. 阶段一：修复上游遗留缺陷

### 1.1 内存水位门禁（上游死代码 → Pro 两段式）

**上游缺陷**（设计说明 §6.3 论证）：`ac_check_memory_watermark`（`ac_solvers.c:679-724`）
估算上限 17320 B、阈值 98304 B，比值 5.68× —— **任何输入都不触发**；
且漏算 `g_ac_scratch`（8224）与 `g_sym_nodes`（12288），低估 27496 B。

**Pro 修正**（`src/ac_pro_solvers_ext.c`）：

```c
double ac_pro_memory_used(double extra_bytes) {
    double used = (double)AC_PRO_STATIC_FOOTPRINT;   /* 41456，编译期真实静态量 */
    used += (double)(g_token_count   << 3);          /* 动态项 */
    used += (double)(g_number_count  << 3);
    used += extra_bytes;                             /* 测试注入的假池 */
    return used;
}
```

三个关键设计：

1. **静态量取真实值**，不是上游那个 11176 B 的残缺基数 —— `AC_STATIC_FOOTPRINT = 38672`
   已包含 `g_ac_scratch` 与 `g_sym_nodes`，两块漏算一次性补齐。
2. **Pro 增量单独加**：`AC_PRO_STATIC_EXTRA = 80 + 2688 + 16`，所以 idle 时 `used == 41456`。
3. **`extra_bytes` 参数**让门禁边界可以被真正测出来，而不是靠改全局变量造数据。

**编译期硬约束**（不只是 Makefile 里的一行 echo）：

```c
AC_STATIC_ASSERT(AC_PRO_STATIC_FOOTPRINT <= AC_PRO_GATE_BSS, pro_footprint_under_gate);
AC_STATIC_ASSERT(AC_PRO_STATIC_EXTRA == (int)sizeof(ac_pro_storage_t), pro_extra_matches_real_sizes);
```

**测试**（`test_memory_watermark`，13 条测试）：
- `used >= 38672`（不再低估）
- `used == 41456`（idle）
- 满池 `used == 47600`，仍 < 114688
- 边界两侧各差 1000 必触发/必放行
- **边界 ±1 B 的极限测试**：`114688` 恰好放行、`114689` 触发
- 上游死代码对照：返回 0 且不置错误码

> **口径修正**：设计说明 §9.5 写的「满池 44816」是**上游口径**（38672 + 6144）。
> Pro 必须按 Pro 真实足迹算，故为 `41456 + 6144 = 47600`，差 2784 正是 Pro 增量。
> 门禁边界随之下移到 `114688 - 47600 = 67088`，测试按此钉死。

### 1.2 编译期断言从严（`>=` → `==`）

上游 `ac_scratch.h:181` 用 `>= 8224`，允许静默增长（设计说明 §8.3）。
Pro 侧对称改为**精确相等**，且逐视图分别钉死：

```c
AC_STATIC_ASSERT(sizeof(ac_pro_scratch_t) == 2688, pro_scratch_is_exactly_peak);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->lu)  == 2352, pro_view_lu_is_2352);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->nr)  ==  264, pro_view_nr_is_264);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->sym) == 1792, pro_view_sym_is_1792);
AC_STATIC_ASSERT(sizeof(((ac_pro_scratch_t *)0)->mv)  == 2688, pro_view_mv_is_2688);
AC_STATIC_ASSERT(sizeof(ac_pro_storage_t) == 2784, pro_storage_is_exactly_2784);
```

**受控实验**（`.tmp-build/verify_final.ps1`）：人为把 union 撑到 2360 B，
编译立刻失败 —— `error: size of array 'ac_sa_pro_scratch_is_exactly_peak' is negative`。
校验是真的会响，不是摆设。

---

## 2. 阶段二：Pro Union 与起手三件套

### 2.1 接入模式 β（Pro 独立 union）

`src/ac_pro_scratch.h` 定义 `ac_pro_scratch_t`，六个视图：

| 视图 | 尺寸 | 归属 | 被吸收 |
|:---|---:|:---|:---|
| `nr` | 264 B | B1 数值求根 | ✅ |
| `sym` | 1792 B | A6 隐函数求导工作区 | ✅ |
| `lu` | 2352 B | A4 LU 分解 | ✅（v1.2 峰值） |
| `res` | 1152 B | A3 Sylvester 缓冲 | ✅ |
| **`mv`** | **2688 B** | **A1/A2/A3 多变量项池（6 槽）** | **← 峰值（v1.3 起）** |
| `eig` | 2592 B | A5 特征向量 + Hotelling deflation | ✅（v1.3.5，比峰值小 96 B） |

8 字节对齐由 `offsetof(ac_pro_align_probe_t, s) == 8` 在编译期证明。
`eig` 的 96 B 差额由 `pro_eig_is_96_bytes_under_mv` 单独钉死 —— 否则把 `mv`
缩到和 `eig` 一样大也能让 absorbed 校验通过，却悄悄改变了 union 尺寸。

### 2.2 存储布局：为什么是一个 struct 而不是两个全局

PE/COFF 链接器把**每个**全局对象按 16 B 对齐起址，两个独立全局之间因此
会被插入 16 B 填充。把两块装进一个 `ac_pro_storage_t` 后，整块只占一个
分配单元，填充无从产生：

| 写法 | `.bss` 增量 |
|:---|---:|
| 两个独立全局 `g_ac_pro_scratch` + `g_var_val` | 多 16 B ❌ |
| 单个 `ac_pro_storage_t` 含两块 | 2784 = 2688 + 80 + 16 ✅ |

**语义没有被牺牲**：struct 是「顺序排列」而非「重叠」，`g_var_val` 仍在 union
之外，判据 3（跨调用会话态禁入 union）完全满足。只是把它们放进同一个 `.bss`
分配单元，让对齐填充无从产生。

### 2.3 B1：五次及以上数值求根

`ac_pro_numroots(const double *coef, int degree)`，Durand-Kerner（Aberth 初始化）。

**次数路由**（`ac_guard_degree` 三态，通过宏重命名拦截上游实现）：

| degree | 路径 | 实现方式 |
|:---|:---|:---|
| 1..4 | 精确/根式 | **直接调用上游原件** `ac_pro_upstream_guard_degree` |
| 5..8 | Durand-Kerner | Pro 新增 |
| > 8 | 拒绝（`g_solve_error = 1`） | 与上游同码 |

> 1..4 不是「抄一遍判断」，而是真的调用上游函数 —— 这样「1..4 行为与上游
> 逐位一致」是结构保证，不是靠人工核对。

**收敛判据的修正（实测驱动的改动）**：

初版只用步长判据 `max|Δz| < 1e-12`，在 `(x-1)³(x+2)²` 上**误报不收敛**。
原因不是 bug 而是重根的固有性质：

```
m 重根附近：|Δz| ≈ ε^(1/m)
m = 3 时，1e-16 的机器精度只能换来 1e-5 的步长 —— 永远达不到 1e-12
```

但根其实早就找对了（残差 8.9e-16）。修正为**步长判据 OR 残差判据**：

```c
if (max_delta >= PRO_NR_TOL &&
    residual > 1e-12 * (1.0 + scale)) {   /* scale = Σ|c_k|·|z|^k */
    g_solve_error = 3;
    return 0;
}
```

**没有**放宽步长阈值，残差只是给重根开的一扇门；两者都不满足仍然返回 3。

**7 个测试用例**：

| # | 多项式 | 次数 | 最大残差 | 结果 |
|:--|:---|---:|---:|:---|
| 1 | x⁵ − 1 | 5 | 2.22e−16 | ✅ |
| 2 | x⁵ − x − 1 | 5 | 3.78e−16 | ✅ |
| 3 | x⁵ + 3x⁴ − 2x³ + 7x² − 5x + 11 | 5 | 3.55e−15 | ✅ |
| 4 | x⁶ + x³ − 1 | 6 | 2.04e−15 | ✅ |
| 5 | x⁸ − 1 | 8 | 6.66e−16 | ✅ |
| 6 | (x−1)³(x+2)²（重根） | 5 | 8.92e−16 | ✅ |
| 7 | 次数 9（负向） | 9 | — | ✅ 正确拒绝 |

配套 `ac_pro_extract_coefficients(1..8)`：上游硬限 1..4，Pro 扩到 8 次
（9×10 = 90 double，`g_matrix_pool` 156 装得下，**零新增存储**）。

### 2.4 A4：LU 分解

`ac_mat_lu(const double *a, int n)`，部分主元 Doolittle，契约 `P·A = L·U`。

**为什么重写而不是复用 `mat_echelon`**：上游那个是 `static`（`ac_matrix.c:74`），
Pro 拿不到；且它把乘数写在原矩阵里、置换只记成一个交换次数 `int`，拿不回
置换向量。重写版本保持与上游一致的约定（主元阈值 1e-12、奇异返回 0）。

L/U/piv 全落 `lu` 视图（2352 B），无栈上大数组。
**6 个测试用例**：

| # | 矩阵 | 验证点 | 结果 |
|:--|:---|:---|:---|
| 1 | 3×3（需换行） | P·A == L·U，误差 **0.0** | ✅ |
| 2 | 4×4 Hilbert 式病态 | 重建误差 2.78e−17 | ✅ |
| 3 | 对角矩阵 | 置换为恒等、U == A | ✅ |
| 4 | 12×12（上限） | 重建误差 3.55e−15 | ✅ |
| 5 | 3×3 奇异（rank 2） | 正确返回 0 | ✅ |
| 6 | 3×3 与 `ac_mat_det` 交叉验证 | det 两侧均为 −16.000000000000007 | ✅ |

### 2.5 A6：隐函数求导 —— 填充洞的正确用法

**这是任务书要求上报的那处陷阱，结论是：陷阱可以完全绕过，无需上报阻断。**

**陷阱**：给 `ac_sym_node_t` 加 `int32_t var` 字段需要改 `ac_symbolic.h`，
而该头文件被上游 6 个 TU 包含。改完后「Pro TU 与上游 TU 对 sizeof/布局的
认知」会分叉 —— 典型的 ODR 式错配，1000 条测试全绿也发现不了。

**解法**：**结构体定义一个字都不改**，用「字节偏移 + memcpy」访问设计说明
§1.4 判据 2 发现的那块填充洞：

```
offsetof(type)  = 0   1 B
offsetof(pad)   = 1   3 B
offsetof(left)  = 4   4 B
offsetof(right) = 8   4 B
---- 偏移 12..15：4 B 填充洞 ----
offsetof(value) = 16  8 B
```

六条编译期断言把布局钉死，于是「12..15 是洞」由**编译器证明**，而不是由注释声称：

```c
AC_STATIC_ASSERT(sizeof(ac_sym_node_t) == 24, pro_sym_node_is_24_bytes);
AC_STATIC_ASSERT(offsetof(ac_sym_node_t, right) + sizeof(int32_t) == 12, pro_sym_node_hole_starts_at_12);
AC_STATIC_ASSERT(offsetof(ac_sym_node_t, value) - 12 == 4, pro_sym_node_hole_is_4_bytes);
```

**为什么 memcpy 是合法的**：经 `unsigned char*` 访问任意对象的字节表示是
标准明确允许的（C99 §6.2.6 / §7.21.2），无严格别名问题；memcpy 对任意对齐
都合法；该 4 字节从未被上游代码读写（上游只写 `type`/`pad[0..2]`/`left`/
`right`/`value`）。

**收益**：`sizeof` 仍为 24，`g_sym_nodes` 仍为 12288 B，`.bss` **零增长**。
若改成尾部追加，`sizeof` 变 32，arena 变 16384 B，`.bss` 直接 +4096。

**9 个测试用例**：

| # | 场景 | 期望 | 实测 |
|:--|:---|:---|:---|
| 1 | x²+y²−25 at (3,4) | −0.75 | **−0.75** ✅ |
| 2 | x²+y²−25 at (−3,4) | +0.75 | **+0.75** ✅ |
| 3 | x·y−6 at (2,3) | −1.5 | **−1.5** ✅ |
| 4 | x³+y³−9 at (1,2) | −0.25 | **−0.25** ✅ |
| 5 | 与中心差分对拍 | < 1e−6 | **1.17e−10** ✅ |
| 6 | (5,0) 处 F_y = 0（负向） | 报 EFLAT，输出不改写 | ✅ |
| 7 | 表达式不含 y（负向） | 报 EPARSE | ✅ |
| 8 | sin(x)+y（负向，超边界） | 报 EPARSE | ✅ |
| 9 | 填充洞自证 | x→0、y→1 落在偏移 12..15 | ✅ |

**`g_var_val[10]` 的落位**：作为 `ac_pro_storage_t` 的第二个成员，**不在 union 内**
（判据 3：跨调用会话态）。求值前写入，求值后可读。

---

## 3. v1.3 方向 A：多变量层

三个新能力都跑在同一块 `mv` 视图（6 槽 × 28 项 × 16 B = 2688 B）上，没有新增
任何全局对象 —— 这正是 union 尺寸从 2352 涨到 2688 的原因。

### 3.1 A1：多变量 GCD

`ac_pro_multi_var_gcd(fsrc, gsrc, out, out_len)`，子结式 PRS（prem 序列）。

**主变量分层**：`mv_pick_main_var` 选出两式共有的变量作为主变量（优先 x），
按一元多项式做伪除法。`x` 轴退化时换 `y` 轴重跑，两轴都退化才落到公共单项式。

**收敛条件**：伪余式的**主变量次数严格小于除式**才继续，否则换轴或收敛。
这里有一处反直觉的坑：中间步骤**绝不能**提取单项式因子 —— 提取 x 因子会让
`deg_x` 直接掉到 0，跳过该有的消元步骤，后续 PRS 全部错位（实测
`gcd(x²−y², x²+2xy+y²)` 因此算成 `y⁴`）。整数 content 归一化则安全，它是常数倍。

**最终校验**：PRS 结果在 `K(y)[x]` 上整除 f，但这弱于 `Z[x,y]` 上的整除
（实测 `f = x²+2xy+y²`、`g = y(x+y)` 的余式为 0 但 g 不整除 f）。所以
`mv_validate_gcd` 会拿**原始输入的快照**重算一遍，并对候选因子搜索单项式商，
直到找到真正的公共因子。校验在**栈上**的 `mv_sp_t` 上做（28 项 × 16 B = 448 B
/ 个），不占 union 槽，也就不会与任何槽别名。

**8 个测试用例**（A1-1..A1-8，16 条测试）：

| # | 输入 | 期望 | 实测 |
|:--|:---|:---|:---|
| 1 | gcd(x²−y², x−y) | x−y | **x - y** ✅ |
| 2 | gcd(x³+y³, x²−xy+y²) | x²−xy+y² | **x^2 - x*y + y^2** ✅ |
| 3 | gcd(2x²−2y², 4x²−4xy)（含 content） | x−y | **x - y** ✅ |
| 4 | gcd(x+y, x−y)（互素） | 1 | **1** ✅ |
| 5 | gcd(x²−y², 0)（零操作数） | x²−y² | **x^2 - y^2** ✅ |
| 6 | gcd(xy+x, xy+x)（相同） | xy+x | **x*y + x** ✅ |
| 7 | gcd(x²y, xy²)（单项式） | xy | **x*y** ✅ |
| 8 | 总次数 7（负向） | 拒绝，错误码 1 | ✅ |

### 3.2 A2：内容提取式因式分解

`ac_pro_multi_var_factor(src, out, out_len, content)`：整数 content 提取 +
有理根下降（复用上游 `ac_ratroot`），每个候选因子都要重新展开
`c · Π(den·x − num) · remainder` 并验证它精确整除原式。

**8 项上限的边界**：校验阶段的结构体尺寸取 `AC_PRO_MV_TERMS`（28）而不是 8。
2 变量、总次数 ≤ 6 的单项式数是 `C(8,2) = 28`，卡到 8 会让合法的 9 项输入
被误判失败（实测 rc = -1）。真正的规模限制由次数预检负责。

**4 个测试用例**（A2-1..A2-4，9 条测试）：

| # | 输入 | 期望 | 实测 |
|:--|:---|:---|:---|
| 1 | x²−1 | content 1，(x−1)(x+1) | **(x - 1) * (x + 1) * 1** ✅ |
| 2 | 2x²−8 | content 2，(x−2)(x+2) | **2 * (x - 2) * (x + 2) * 1** ✅ |
| 3 | x²+1（在 Q 上不可约） | 原式返回 | **x^2 + 1** ✅ |
| 4 | 真二元式（超出 v1.3 边界） | 返回 0 | ✅ |

### 3.3 A3：Sylvester 结式

`ac_pro_resultant(fsrc, gsrc, x0, out)`：在 `x = x0` 处把 f、g 视为 y 的一元式
（Horner 提 y 系数），拼 Sylvester 矩阵，再用上游 `ac_mat_det` 求行列式。
`df + dg > 12` 直接拒绝，而不是让矩阵越界。

矩阵落 `res` 视图（1152 B），`saved_f` / `saved_g` 两份快照在栈上。

**4 个测试用例**（A3-1..A3-4，12 条测试）：

| # | 输入 | 期望 | 实测 |
|:--|:---|:---|:---|
| 1 | Res_y(y²−x, y−1) @ x=4 | −3 | **−3** ✅ |
| 2 | Sylvester 行结构 | 3 阶，行 `[1,0,-4]` `[1,-1,0]` `[0,1,-1]` | ✅ |
| 3 | Res_y(y²−x, y²−4) @ x=4（公根） | 0 | **0** ✅ |
| 4 | 阶 13（负向） | 拒绝，输出不被改写 | ✅ |

---

## 3A. v1.3.5：A5 特征向量与 B2 符号积分

两个新能力都跑在既有 union 上：A5 用新加的 `eig` 视图（2592 B），B2 的 64 项
积分表是 `static const`，落 `.rdata` 不占任何视图。因此 `.bss` 仍是 41456 B。

### 3A.1 A5：实特征向量 + Hotelling 收缩 deflation

```c
int            ac_pro_eig_all(const double *a, int n);   /* a 行主序、n×n、实对称 */
double         ac_pro_eig_value(int k);                  /* |λ| 降序，k = 0..n−1 */
const double  *ac_pro_eig_vector(int k);                 /* 列主序第 k 列起始指针 */
int            ac_pro_eig_count(void);
```

**三条路径互相印证**，而不是只验一条：

1. **主特征对**用带 Rayleigh 商的幂迭代。
2. **特征向量第二条路径**：解 `(A − λI)v = 0` 的零空间 —— 把 `A − λI` 做行阶梯
   消元（复用 `ac_mat_rank` 同款 `mat_echelon` 逻辑），自由变量取 1 回代出 v。
   幂迭代给的 x₁ 与零空间解必须平行，这是「`(A−λI)v = 0` 确实成立」的直接证据，
   比只验 `Av ≈ λv` 强 —— 后者对任何近似特征向量都成立。
3. **deflation**：Hotelling 收缩 `A ← A − λ₁·x₁·x₁ᵀ`，收缩后 λ₁ 被压成 0、
   其余特征值不变，再求主特征对，重复 n 轮得全部 n 对。

**为什么只需要两块 n×n 缓冲**：朴素实现要同时驻留「原始 A + deflated 矩阵 +
消元工作区」三块（3 × 1152 = 3456 B），超出 2592 B 预算。本实现利用签名里的
`const double *a`（调用期间有效且只读），每轮从调用方矩阵重拷一份再逐项扣除
`D_k = A − Σ_{j<k} λ_j·v_j·v_jᵀ`。代价是 O(n³) 的减法（n ≤ 12 时 3456 次乘加），
换来省掉一整个 n×n 缓冲。

**收敛判据盯残差而不是 |Δλ|** —— 这是实测驱动的修正。Rayleigh 商是二阶精确的，
`|Δλ|` 会先于角度误差塌到 1e-16，而残差 `‖r‖ = gap·sinθ` 还在 1e-7：用 |Δλ| 判停
会提前 2~3 个数量级收工，3×3 用例的残差卡在 4.6e-8，过不了 1e-9 的闸门。改为盯
残差本身的下降停滞（`res > 0.9·res_prev`）后，残差落到 3.8e-13 ~ 4.7e-13。

**错误码走返回值，不做 getter**：一个 `static int` 会让该 TU 产生 16 B 的 `.bss`
段，叠加链接对齐后全程序净涨 32 B（41456 → 41488），门禁直接炸。返回值不需要任何
静态存储。错误码：1 成功 / −1 n 越界 / −2 非对称 / −3 不收敛 / −4 零向量。

**5 个测试用例，76 条测试**（要求 ≥12）：

| # | 用例 | 校验 | 实测 |
|:--|:---|---:|:---|
| 1 | 2×2 `[[2,1],[1,2]]` | 12 | λ = {3, 1} ✅ |
| 2 | 3×3 对称 | 12 | 全对 ✅ |
| 3 | 重根 `diag(2,2,1)` | 13 | 残差 3.8e−13 ~ 4.7e−13 ✅ |
| 4 | Hilbert 3×3（cond ≈ 5.2e5） | 12 | `1.408318927123654 / 0.122327065853906 / 0.002687340355774`，与解析解 17 位一致 ✅ |
| 5 | deflation 正确性 | 27 | 两两正交、单位范数、`Av = λv` 残差 ✅ |

另有边界：1×1、12×12 满维、零矩阵 → −4、负定矩阵、非对称 → −2、n 越界 → −1。

### 3A.2 B2：符号积分

```c
int ac_pro_integrate(const char *expr, char *out, int out_len);
/* 0 = OK；−1 EPARSE / −2 ETABLE / −3 EBUDGET / −4 EOVER / −5 EDEGREE */
#define AC_PRO_INT_TABLE_SIZE 64
```

**64 项积分表必须是 `static const`**：任何非 const 的文件级对象都会让本 TU 产生
`.bss`，叠加链接期对齐后全程序净涨 32 B。这条不是洁癖 —— 上游曾因 CRC 表懒初始化
出过静默错值事故（见 CHANGELOG v1.1.0），`static const` 让失效模式从「靠纪律避免」
变成「由构造消除」。自检命令就一条：`size x.o` 的 bss 列必须是 0。

**节点预算真的检查**：复用上游 `g_sym_nodes`（`AC_SYM_MAX_NODES` = 512，被上游 6 个
TU 共享）。表匹配路径 ≤ 256 节点，分部积分路径额外 ≤ 96 节点（合计 352 ≤ 512，
有编译期断言）。超限返回 `AC_PRO_INT_EBUDGET`，绝不越界写 arena —— 越界会静默破坏
其它模块的表达式树。

**分部积分只做一层**：`∫u dv = u·v − ∫v du`，对 `∫v du` 再试一次表匹配，仍失败就
直接返回错误码，绝不递归展开。

**表按函数名长度降序排列**：否则 `"atanh"` 会被拆成 `"atan"` + `"h"`。

**6 个测试用例，31 条测试**（要求 ≥16）：多项式、三角、一层分部积分（`∫x·eˣ dx`）、
表匹配、节点预算耗尽、错误路径（空输入 / 非法字符 / 超次数 / 输出缓冲不足）。

### 3A.3 v1.3.5 的 `.bss` 零增长证明

```
sizeof(ac_pro_scratch_t) = 2688      ← v1.3 峰值，v1.3.5 未变
sizeof(eig view)         = 2592
sizeof(mv view)          = 2688
mv − eig                 = 96        ← eig 完全被吸收
```

编译期断言 `pro_view_eig_absorbed`（`eig <= mv`）与 `pro_eig_is_96_bytes_under_mv`
（差额恰为 96）两条同时把守：前者保证零增长，后者防止有人把 `mv` 缩到和 `eig`
一样大、让 absorbed 通过却改了 union 尺寸。

### 3A.4 性能（100 万次调用，同机）

| 路径 | 迭代 | 总耗时 | 单次 |
|:---|---:|---:|---:|
| `ac_pro_eig_all` 2×2 `[[2,1],[1,2]]` | 1,000,000 | 0.073 s | **0.073 µs** |
| `ac_pro_eig_all` 3×3 `[[4,1,2],[1,3,0],[2,0,5]]` | 1,000,000 | 1.593 s | **1.593 µs** |
| `ac_pro_integrate("x^2")` | 1,000,000 | 0.343 s | **0.343 µs** |
| `ac_pro_integrate("x*e^x")` | 100,000 | 0.100 s | **1.000 µs** |

上表是三个用例里最典型的一组，不代表上界。换矩阵后单次耗时差一个数量级，
原因在幂迭代的收敛速度而非框架开销：

| 矩阵 | 单次 | 说明 |
|:---|---:|:---|
| 2×2 单位阵 | 0.070 µs | 初值即特征向量，一轮收敛 |
| 2×2 `[[2,1],[1,2]]` | 0.073 µs | 同上（1 与 2 张成的初值正好落在特征方向上） |
| 2×2 Hilbert-2 | 0.230 µs | 条件数 ~30，需要几轮 |
| 2×2 `diag(2,3)` | **1.070 µs** | **最坏 2×2**：初值落在特征方向之间，比值 2/3 需 ~30 轮 |
| 3×3 Hilbert-3 | 0.530 µs | 条件数 ~500 |
| 3×3 三对角 | 1.690 µs | |
| 4×4 三对角 | **2.780 µs** | **最坏 4×4**：`r ≈ 0.84`，是 `PRO_EIG_MAX_ITER = 400` 的推导依据 |

结论：**2×2/3×3 常见输入全部低于 2 µs/op**，最坏情况（`diag(2,3)` 这类
特征值比值接近 1 的矩阵）约 1~3 µs/op。符号表查找与零空间消元没有引入
隐藏的 O(n³) 热路径。

### 3A.5 v1.3.5 已知缺口

- **`x^-1` 返回 EPARSE**：`1/x` 正常返回 `ln(x)`，但负指数写法不支持。任务书要求的是
  多项式 + 基本三角函数 + 一层分部积分，不构成缺陷，记录备查。
- **`sin(cos(x))` 返回 ETABLE(−2)**：嵌套超越函数无初等原函数，表匹配失败后直接拒绝，
  这是正确行为。
- **A5 只接受实对称矩阵**：Hotelling 收缩在非对称矩阵上不保谱，非对称输入返回 −2。
  这是适用范围声明，不是实现缺陷。

---

## 3B. v1.4.0：2 变量 Groebner 基

组件版本 v1.3.5 → **v1.4.0**（Pro 层新特性按 semver minor 递进），对应全局版本节点
v2.2.0——全局 Tag 与组件版本是两个口径，不必同号。组件细分记录见
[CHANGELOG.md](CHANGELOG.md)。

- **算法**：M4GB 逐对归约，lex 序，deg ≤ 4，膨胀熔断。
- **内存**：gb 视图 1888 B（≤ 1960 预算线），被 sparse 峰值 3 848 B 完全吸收，
  **`.bss` 零增长**——「union 吸收」判据的又一次兑现。
- **账本**：内核口径 42 608 B / runner 口径 42 640 B / 上游对照 39 408 B
  （`bss-check` 实测逐字命中）。
- **校验**：pro_runner **396 checks, 0 failed**（新增 68 项测试）+ RESULT: PASS。
- **验收脚本 `verify.ps1` 三处修复**：① 相对 `-OutDir` 在任何 `Push-Location`
  之前锚定为绝对路径（第 7 节 `neg*.o` 曾解析到不存在目录，且被 neg1/neg2 的
  「非零 = 预期拒绝」判定掩盖成通过）；② 负向编译 verdict 携带 gcc 退出码与
  stderr 摘要；③ 文件存为 UTF-8 with BOM（BOM-less 时 PowerShell 5.1 在 zh-CN
  按 CP936 解码，中文注释多字节标点吞字节破坏解析结构）。三种调用模式全部
  **31 passed, 0 failed**，两道门禁复跑 exit 0。

> 本节为 v1.4.0 增量；第 0–4 节的 41 456 B / 259 checks 等数字是 v1.3.5 时点快照
> （v2.1.0 稀疏求解/定点小数与当前账本见 [CHANGELOG.md](CHANGELOG.md) 与根
> CHANGELOG 的 v2.1.0 / v2.2.0 条目）。

---

## 4. 阶段三：验证与对拍

### 4.1 `.bss` 三层账本

```
38672  16 个上游模块（内核口径，size 实测）
  + 80  g_var_val[10]
+ 2688  g_ac_pro_scratch（Pro union，峰值视图 mv）
+  16   align_pad（对齐到 32 的整数倍）
= 41456  <- pro_kernel.exe（内核口径，任务书目标）

41456
  + 32  test/test_pro_runner.c 的 .bss 段 16 B + 链接对齐 16 B
= 41488  <- pro_runner.exe（runner 口径）

38672
 + 736  上游 test/ 侧：448 + 272 + 16
= 39408  <- 上游 test_runner.exe（对照，防上游漂移）
```

`nm --print-size` 逐符号实测（`pro_kernel.exe`）：

```
00000000004272e0  0000000000002020  B  g_ac_scratch      8224   ← 上游 union
00000000004242e0  0000000000003000  B  g_sym_nodes      12288   ← 上游 arena
0000000000429700  0000000000000ae0  B  g_ac_pro_storage  2784   ← Pro 增量
```

### 4.2 `AC_PRO=0` 退化验证

三种构建模式，用 `AC_PRO_ACTIVE` 派生开关守卫：

| 构建 | 宏 | 结果 |
|:---|:---|:---|
| 默认 | `AC_PRO=1` | Pro 增量全开，`.bss` = 41456 |
| 退化 | `AC_PRO=1 AC_PRO_DEGRADED=1` | Pro 增量全关，`.bss` = **38672** |
| 非法 | `AC_PRO=2` | 编译失败：`AC_PRO must be exactly 0 or 1.` |
| 非法 | `AC_PRO=0`（无 DEGRADED） | 编译失败：需要显式选择模式 |

**逐位对拍**（`.tmp-build/parity_degraded.ps1`）：把上游全部 17 个 test 文件
链到退化构建的 Pro 包装层上，与上游 runner 对比：

```
upstream : text 202804  data 2716  bss 39408
degraded : text 202804  data 2716  bss 39408     ← 完全相同
output   : 636 chars vs 636 chars
PARITY: PASS (byte-identical output)
```

退化构建里三个 Pro TU 全部编译成**空对象**（`nm` 无任何符号），
`ac_guard_degree` 符号名回到上游原样，`ac_pro_upstream_guard_degree` 不存在。

### 4.3 上游零改动

```
$ git status --porcelain -- C/128KB
（空）
$ git diff --stat -- C/128KB
（空）
```

上游 `test_runner.exe` 重建后仍为 39408 B、1171 项测试 / 16 套件全绿。

---

## 5. 文件清单

### 新增

| 文件 | 行数 | 职责 |
|:---|---:|:---|
| `src/ac_pro.h` | 283 | Pro 层唯一公开头文件 |
| `src/ac_pro_scratch.h` | 352 | `ac_pro_scratch_t` union（6 视图）+ 全部编译期断言 |
| `src/ac_pro_extra_globals.c` | 39 | `g_ac_pro_storage` 唯一定义处 |
| `src/ac_pro_solvers_ext.c` | 533 | 水位门禁重写 + B1 数值求根 + A4 LU |
| `src/ac_pro_symbolic_ext.c` | 594 | A6 隐函数求导（填充洞方案） |
| `src/ac_pro_multi.c` | 2482 | A1 多变量 GCD + A2 因式分解（v1.3） |
| `src/ac_pro_resultant.c` | 253 | A3 Sylvester 结式（v1.3） |
| `src/ac_pro_eigen.c` | 381 | A5 特征向量 + Hotelling deflation（v1.3.5） |
| `src/ac_pro_integrate.c` | 1485 | B2 符号积分：64 项表 + 一层分部积分（v1.3.5） |
| `test/test_pro_multi.c` | 242 | A1/A2/A3 用例（v1.3） |
| `test/test_pro_eigen.c` | 460 | A5 用例，76 条测试（v1.3.5） |
| `test/test_pro_integrate.c` | 223 | B2 用例，31 条测试（v1.3.5） |
| `test/pro_kernel_main.c` | 26 | 内核口径测量的 trivial main |

### 修改

| 文件 | 改动 |
|:---|:---|
| `src/ac_pro_config.h` | 两级开关 `AC_PRO` / `AC_PRO_ACTIVE`，账本常量 |
| `src/ac_pro_solvers.c` | 宏重命名拦截 `ac_guard_degree` |
| `Makefile` | `PRO_EXTRA`、`kernel`/`bss-check`/`degraded-check` 目标；v1.3.5 接线 2 个新 TU |
| `test/test_pro_runner.c` | 259 条测试（原 152 条），v1.3.5 账本常量 + 11 个新入口 |

---

## 6. 遗留与建议

1. **runner 口径 41488 vs 任务书 41456**：两个数都对，口径不同。41456 是
   **内核口径**（不链接测试 TU），由 `pro_kernel.exe` 校验；41488 是 runner
   口径，多出测试 TU 的 16 B 段 + 16 B 对齐。这与上游「38672 内核 / 39408
   README」的区分方式完全一致。若任务书要求 runner 也精确等于 41456，
   需把 `s_kernel_live` 挪出 `.bss`（例如用 TLS 或链接期常量），但会牺牲
   Zero-GC 校验的可靠性 —— 不建议。
2. **A6 不支持函数调用**：`sin(x)+y` 这类表达式会被拒绝。上游 `NODE_*` 只有
   CONST/VAR/ADD/SUB/MUL/POW 五种，加 `NODE_FUNC` 会改上游语义，超出 v1.2 边界。
   若要支持，建议在 Pro 侧建独立的节点类型表，而不是改上游枚举。
3. **A6 的 sym 视图 1792 B**：设计说明 §3.4.2 记为「8 B 栈上」，那是按复用上游
   `s_sym_stack`/`s_sym_dmap` 估算的。本实现刻意不复用（避免与上游内部实现
   耦合），自带 1792 B，被 2688 B 的 `mv` 峰值完全吸收，`.bss` 零增长。
   这是判据 1「union 吸收」的正用法。
4. **DK 对重根的处理**：残差兜底能正确接受重根，但**不保证**收敛到重根
   的正确重数（DK 本身不区分重数）。若将来需要重数信息，需引入
   square-free 分解。
5. **A1/A2 的规模上限**：总次数 ≤ 6、最多 28 项、6 个 union 槽。超过即由
   次数预检拒绝（错误码 1），不会越界。扩到 4 变量 / 8 次需要更大的项池，
   届时应同时调整 `AC_PRO_MV_TERMS` 与 union 尺寸，并重新走一遍 `.bss` 账本。
6. **A2 只覆盖有理根**：不可约但无有理因子的多项式（如 x²+1）按原式返回，
   这是 v1.3 的契约而不是缺陷。真正的高次不可约分解需要 Zassenhaus，
   会引入堆分配，与 Zero-GC 冲突。
7. **A5 只接受实对称矩阵**：Hotelling 收缩在非对称矩阵上不保谱，非对称输入
   返回 −2。需要一般矩阵特征值时得另走 Schur 或 Hessenberg-QR，不在 v1.3.5 范围。
8. **B2 不支持负指数**：`1/x` 走 `ln(x)` 正常，但 `x^-1` 报 EPARSE。补上它需要
   在解析层把负指数改写成除法，会牵动节点预算，收益不足以抵风险。
9. **B2 只做一层分部积分**：`∫x²·eˣ dx` 这类需要两层的会被 ETABLE 拒绝。
   递归展开会突破 96 节点的额外预算，也与「绝不无限展开」的设计相冲突。

---

*文档版本：v1.4.0 · 基线 ApolloCalc-CAS v1.1.0 · Pro v1.4.0（全局 v2.2.0）· 实测 `.bss` 42608 B / 37.15%*
