# JS/128KB — 零 GC 计算代数内核（增强版）

- **状态**：v2.0.0（8/8 步 + 多变量引擎升级；12 文件内核拼接）
- **堆限制**：128KB（全局状态水位目标 ≤ 96KB）
- **目标平台**：不限——任何支持 ES5 + TypedArray 的 JS 环境（Lite Wearable / JerryScript / 浏览器 / Node）
- **内存模型**：与 64KB 版一致——TypedArray + 全局标量，零临时对象，禁止递归与热路径字符串拼接

## 功能清单

### Tier 1（继承自 JS/64KB，基线）

- 表达式解析与求值（隐式乘法、右结合幂运算、RAD/DEG 角度模式）
- 二次 / 三次 / 四次方程求解（三角代换 + Cardano + 费拉里，含复根）
- 线性方程组（12 元，列主元高斯消元 + 部分主元）
- 精确形式（五分量 (a + b√c)/d × 10^e：rat / radNum / rad / den / exp）
- 系数采样提取、残差回验、preCheck 防护层、内存水位门禁

### Tier 2（128KB 新增，已落地 14 项）

1. 五分量精确形式（(a + b√c)/d × 10^e，替代三分量，支持混合形式与科学计数法）
2. 数值微积分：数值导数（中心差分）与定积分（Simpson）
3. 双曲函数扩展：`sinh` / `cosh` / `tanh`
4. 反双曲函数：`asinh` / `acosh` / `atanh`
5. 复数扩展：复数域函数求值与复根精确形式
6. 进制转换（2/8/10/16）与 SI 词头
7. 更大 token/数值池（512/256）、求值栈翻倍（128）、preCheck 阈值翻倍（深度≤6 / 函数≤4 / 变量≤10）
8. 家族共享一致性用例接入（tests/ 三套 JSON，`test/test-consistency.js`）
9. 有理根检验 + 低次多项式因式分解（`core_ratroot.js`：候选 p/q 枚举 + 综合除法 +
   迭代降次显式栈；三次/四次整数与分数根获得精确形式）
10. 泰勒级数展开（`core_calculus.js` 模块七：≤16 阶，逐阶数值导数 + 中心差分表，Horner 求值）
11. 线性方程组扩 12 元（矩阵池 156 = 12×13，根结果池同步扩 12 槽）
12. **多变量多项式 + 高阶三角/复数补充**（`core_multivar.js`，v1.3.5 落地、v2.0.0 增符号求导）：见下节
13. **矩阵基础运算**（`core_matrix.js`，v2.0.0）：乘法 / 转置 / ≤3×3 行列式（Sarrus）与逆矩阵（伴随矩阵法）
14. **高阶特殊函数**（`core_special.js`，v2.0.0）：`erf`（A&S 26.2.17，|ε| ≤ 1.5e-7）、
    `gamma` / `lgamma`（Lanczos g=7 手工展开，相对误差 ~1e-13，负半轴反射公式）

> 原计划的"有理根检验、多项式因式分解、线性方程组扩 12 元"已于第 8 步补做兑现
> （见下）；"结果历史缓存"仍未落地：UI 层职责边界未定，另立任务卡。

### `core_multivar.js`（v1.3.5 落地，v2.0.0 增 `diff` 与内核接入）

自包含的多变量多项式模块，不依赖也不修改既有 core 文件。顶层 `var` IIFE 导出，
三通道可用：`require()`（module.exports）、浏览器 `<script>`（顶层 var 即全局）、
内核拼接与 ESM（`ApolloMultivar` 进模块作用域，经导出块 `multivar` 字段访问）。

| 能力 | 接口 |
|---|---|
| 解析 `"3x^2y + 2xy - 5"` 成稀疏项表 | `parse(src, slot)` |
| 求值 | `evalSlot(slot, x, y)`（槽位）/ `evalTerms(coefs, exps, n, x, y)`（平行数组）/ `evalPoly` 按 `arguments.length` 分派到前两者 |
| 加 / 减 / 乘（卷积） | `add / sub / mul(dst, a, b)` |
| 单项式乘 / 数乘 / 取负 | `mulMonomial / scale / neg` |
| **多变量符号求导（v2.0.0，对齐 C 侧 `ac_pro_diff_multi`）** | `diff(a, varIndex, dst)`：0 = d/dx，1 = d/dy；逐项幂法则，结果项数严格不增，`dst` 可与 `a` 同槽 |
| 同类项合并 / 排序 | `normalize(slot)` / `sortTerms(slot)` |
| 总次数 / 分次数 / 项数 | `totalDegree / degreeX / degreeY / termCount` |
| 文本输出（写预分配缓冲） | `toString(slot)` / `polyToString`（隐式乘法格式 `3x^2y`，与自身解析器互逆） |
| 零 GC 证据 | `stats()` / `allocCount` |

**指数编码与 C 版一致**：`exps = (xdeg << 4) | (ydeg & 0x0F)`，与
`C/128KB-ProTEXT` 的 `ac_pro_term_t.exps` 同构；`MAX_TERMS = 28 = C(8,2)`、
`MAX_EXP = 6` 也与 `AC_PRO_MV_TERMS` / `AC_PRO_MV_MAX_DEG` 对齐。

**零 GC**：模块初始化时一次性分配 6 个 TypedArray（共 **10 208 B**），
`allocCount` 此后恒为 6；热路径无 `{}`、`[]`、`new`、`.map/.filter/.reduce`、
字符串拼接与递归。测试里直接校验 `allocCount` 在热循环前后不变；
`--expose-gc` 探针实测 20 000 次混合调用（含 diff/matrix/special）堆增长 0.17 MB
（JIT 分层噪声量级），强制 GC 后 allocCount 恒 6/3/0。

> 池账：`_coef` 448×8 = 3584、`_exps` 448×4 = 1792、`_cnt` 16×4 = 64、
> `_strBuf` 1024×4 = 4096、`_tmpC` 56×8 = 448、`_tmpE` 56×4 = 224，合计 10 208 B。
> 这是**独立于既有 11 992 B 静态池**的增量 —— 模块自包含，不共享既有 core 的池。

**高阶三角 / 复数补充**：`sec / csc / cot / asec / acsc / acot / sech / csch / coth /
asinh / acosh / atanh`，复数域 `cSqrt / cExp / cLn / cPowInt / cAsinh / cAcosh /
cAtanh / cAbs`（实部虚部分别经 `getZRe / getZIm` 读取）。

`test/test-multivar.js` 覆盖 **194 条测试**（解析、求值、四则、合并、排序、toString、
错误与溢出路径、零 GC；v2.0.0 新增 19 条 `diff` 校验：链式求导、原地求导、
错误路径、槽位复用、热循环零分配）。

### `core_matrix.js`（v2.0.0）

基础矩阵运算，寄存器模型与复数域同构：`setA/setB` → `mul/transpose/det/inv` → `getR*`。
A/B/R 三个 3×3 行主序 Float64Array（3 × 72 = 216 B 加载期一次分配）。

| 能力 | 接口 |
|---|---|
| 乘法 R = A×B（m×k·k×n → m×n，全维 ≤3） | `mul()` |
| 转置 R = Aᵀ | `transpose()` |
| 行列式（1..3 阶闭式：直接展开 / ad−bc / Sarrus） | `det()`，奇异返回 0 是合法结果 |
| 逆矩阵（伴随矩阵法闭式，\|det\| ≤ 1e-12 拒绝） | `inv()` |
| 结果读取 | `getR(r, c)` / `getRRows()` / `getRCols()` / `getLastDet()` |
| 错误通道 | 返回值 + `getError()`：1 维度超界 / 2 内维不匹配 / 3 非方阵 / 4 奇异 |

边界诚实声明：≤3×3 闭式，无消元无选主元；12 元稠密消元是 C/128KB
`ac_matrix`（C 独有）的能力，本模块是浏览器侧基础块。`test/test-matrix.js`
39 条测试（含 A·A⁻¹ ≈ I 对拍、奇异/非方阵拒绝、寄存器独立性、热循环零分配）。

### `core_special.js`（v2.0.0）

高阶特殊函数，纯标量实现——无池、无寄存器，加载期与热路径分配均为 **0**
（Lanczos 级数与 A&S 系数全部手工展开为标量常量，模块内无数组字面量）。

| 函数 | 算法 | 精度 |
|---|---|---|
| `erf(x)` | A&S 26.2.17 有理逼近，奇对称手工分支，大 \|x\| 自然饱和 ±1 | \|ε\| ≤ 1.5e-7（实测节点最大 1.385e-7） |
| `gamma(x)` | Lanczos g=7 / n=9，x < 0.5 反射公式手工展开（无自调用，与 C/128KB `ac_special.c` 同表同条款） | 相对 ~1e-13（实测 γ(7.5) 相对 4e-15） |
| `lgamma(x)` | 同核对数形态；反射走 ln π − ln\|sin πx\| − lgamma(1−x) | 同上 |

极点契约：0 与负整数处返回 `+Infinity`（lgamma 在极点附近趋向 +∞）。
NaN 输入返回 NaN 且 `getError() = 1`。`test/test-special.js` 33 条测试。

## 内存账本（静态池实际 byteLength，扩容后实测）

| 池 | 64KB 版 | 128KB 版（实际） |
|---|---|---|
| tokenPool（Int32） | 256×4 = 1024 B | 512×4 = 2048 B |
| numberPool（Float64） | 128×8 = 1024 B | 256×8 = 2048 B |
| evalStack（Float64） | 64×8 = 512 B | 128×8 = 1024 B |
| matrixPool（Float64） | 42×8 = 336 B | 156×8 = 1248 B（12×13） |
| _rpnToken（Int32） | 256×4 = 1024 B | 512×4 = 2048 B |
| _opStack（Int32） | 64×4 = 256 B | 128×4 = 512 B |
| _lhsToken + _rhsToken（Int32） | 128×4×2 = 1024 B | 256×4×2 = 2048 B |
| _coeffs + 根/精确形式结果池（12 槽 × 五分量） | ~0.3 KB（4 槽） | ~0.7 KB（12 槽） |
| _baseBuf（Int32，进制/SI） | — | 64×4 = 256 B |
| **既有静态池合计** | **~5.2 KB** | **11 992 B ≈ 11.7 KB** |
| `core_multivar.js` 池（独立模块） | — | 10 208 B（9.97 KiB） |
| `core_matrix.js` 池（独立模块） | — | 216 B（3 × 3×3 Float64Array） |
| `core_special.js` 池（独立模块） | — | **0 B**（纯标量，无池） |

> 水位门禁降级估算口径（含 _baseBuf）为 11 992 B；计入最坏游标
> （512 token × 8 + 256 number × 8）与 2KB 运行时冗余约 19.7 KB，
> 距 96KB 门禁仍有充足余量。门禁阈值维持 96KB（128KB × 75%）不变。
>
> 三个 v2.0.0 自包含模块的池是**独立**增量，与上表不重叠。全项目静态池相加
> 22 416 B（21.9 KiB），仍在 96KB 门禁内。但若未来把 multivar
> 直接挂到主求值器上，两池会同时常驻，届时需要重新核算水位。
>
> **内核文件体积口径**：12 文件拼接后的 `core_kernel.js` 为 175 142 B，
> 越过 128 KiB 文件线。这是 v2.0.0 的**有意裁决**而非漂移：项目名中的
> "128KB" 指目标设备堆预算（内存水位门禁 96KB，实测静态池 21.9 KiB，
> 余量充足），文件线随多变量引擎的加入被功能完整性覆盖；如需回到
> 128 KiB 文件线，单独走一轮压缩构建（空白/注释剥离预估可减 ~30%）。
> C 侧对照：`C/128KB-ProTEXT` v2.0.0 同期以 `.bss` 41 456 B 零增长
> 落地同等能力（`feat/multi-diff`、`feat/multi-integrate`、`feat/lut-const`）。

## 与 64KB 的架构差异

- **五分量精确形式**：`core_exact.js` 从 num/den/rad 三分量重写为
  (rat + radNum·√rad)/den × 10^exp 五分量，黄金比例 (1+√5)/2 等
  "有理部 + 根式部"混合形式可直接精确表达；根结果池的 `_rootExact*`
  数组同步扩为 rat/radNum/rad/den/exp 五组；分量超出 2^53-1 时先尝试
  把 10 的因子转移进指数（值不变），仍超限则降级为纯浮点并置无效标志。
  水位门禁阈值同步从 48KB 上调至 96KB（128KB × 75%）。
- **新模块**：`core_calculus.js`（数值导数 + 定积分，固定步长表 + 工作区复用，零分配）、
  `core_base.js`（进制转换 + SI 词头，输出走预分配字符缓冲）。
- **拼接顺序**：parser → math_functions → complex → exact → evaluator → calculus → base → solvers → ratroot → multivar → matrix → special（12 文件；后三个为自包含 IIFE 模块，仅 `ApolloMultivar` / `ApolloMatrix` / `ApolloSpecial` 三个命名空间进入导出块）。
- 池尺寸与 preCheck 阈值（深度/函数数/变量数上限翻倍）按上表放大，算法骨架不变；
  所有池溢出检查改为读 `xxPool.length`（尺寸调整时检查值自动跟随），
  矩阵池扩为 156（12×13，根结果池同步扩 12 槽承载 12 元解），
  `checkMemoryWatermark` 静态池加总走 `byteLength` 自动跟随，门禁维持 96KB。
- **有理根快路径**：solveCubic/solveQuartic 开头经 `ratRootDescent` 枚举整数候选
  （p|a0、q|an，约简去重）综合除法验证，迭代降次（显式栈，无递归）；
  有理根走 exactSetFraction 获得精确形式，剩余部分交低次求解器，
  无有理根时原路径（Cardano/三角/笛卡尔）不变。

## 开发顺序（8 步，全部完成）

1. [x] 骨架搭建：从 64KB 复制 src/test，路径与文档就位
2. [x] `core_exact.js` 五分量重写（rat/radNum/rad/den/exp）+ 求解层接线（混合形式与科学计数法落地）
3. [x] `core_math_functions.js` 双曲函数扩展（sinh/cosh/tanh + asinh/acosh/atanh）
4. [x] `core_complex.js` 复数扩展（复数域求值、复根精确形式）
5. [x] 新增 `core_calculus.js` 数值微积分（导数 + 定积分）
6. [x] 新增 `core_base.js` 进制转换与 SI 词头
7. [x] 池扩容（token 512 / 数值 256 / 栈 128，矩阵池保持 6×7）+ preCheck 阈值翻倍 +
       全量回归对齐 `tests/` 一致性用例（`test/test-consistency.js`）
8. [x] 对照表承诺兑现补做：线性方程组扩 12 元（根结果池同步 12 槽）+
       新增 `core_ratroot.js`（有理根检验 + 因式分解，三次/四次精确形式补全）+
       泰勒展开（`core_calculus.js` 模块七）

## 测试与构建

```bash
node test/test-evaluator.js      # 求值层冒烟（49 项）
node test/test-solvers.js        # 求解层冒烟（46 项，含 128KB 门禁边界 + 12 元线性方程组）
node test/test-complex.js        # 复数域函数（23 项）
node test/test-calculus.js       # 数值微积分（28 项）
node test/test-base.js           # 进制转换 + SI 前缀（63 项）
node test/test-ratroot.js        # 有理根检验 / 因式分解（33 项）
node test/test-taylor.js         # 泰勒展开（26 项）
node test/test-consistency.js    # 家族共享一致性（tests/ 三套 JSON，59 pass + 4 improve）
node test/test-multivar.js       # 多变量多项式 + 符号求导 + 高阶三角/复数（194 项）
node test/test-matrix.js         # 矩阵基础运算（39 项）
node test/test-special.js        # 特殊函数 erf/gamma/lgamma（33 项）
node test/test-browser.js        # 浏览器经典脚本环境模拟（vm 沙箱，10 项）
node test/build-kernel.js        # 拼接 12 文件 core_kernel.js + 83 项回归 + ESM 严格模式冒烟（43 项）
```

零依赖，Node ≥ 14 即可（`test-browser.js` 的 vm 沙箱与 `--expose-gc` 探针
在任意主流 Node 版本可用）。共享一致性用例在 [tests/](../tests/)。
12 个测试文件合计 **603 pass, 0 fail**（49+46+63+28+23+33+26+59+194+39+33+10），
另有 build-kernel 内嵌 83 项 eval 回归 + 43 项 ESM 导出链路冒烟全过。

## 一致性测试判定规则

`test-consistency.js` 对比 128KB 与 64KB 的行为差异，分三类：

- **PASS**：数值根一致，精确形式一致或更强
- **IMPROVE**：数值根一致，但 128KB 在以下方面更强：
  - preCheck 阈值放行（深度/函数/变量上限翻倍）
  - 五分量精确形式覆盖三分量无法表达的场景（如黄金比例）
- **FAIL**：数值根不一致（真回归）

IMPROVE 判定数据驱动：用例在 `tests/edge-cases.json` 声明
`"expectedImprove": true`（64KB 拒绝而 128KB 放行），读到该字段且 128KB
成功即判 IMPROVE，不靠用例命名猜测。

当前 4 个 IMPROVE：3 个 precheck 放行 + 1 个黄金比例精确化。
