# ApolloCalc-CAS 功能对照表

四版本家族（JS 与 C99 × 64KB 与 128KB）+ Pro 层的内核功能矩阵。
JS/128KB（v2.0.0）为浏览器侧功能最全实现；C99 128KB-ProTEXT（v2.0.0）为
代数能力最强的参考实现（`.bss` 41456 B，上游 `C/128KB/` 冻结）。
版本文档：[js-128kb.md](js-128kb.md) / [c99-128kb.md](c99-128kb.md) /
设计说明。

## 内核功能

| 功能 | JS 64KB | JS 128KB（v2.0.0） | C99 64KB | C99 128KB |
|---|---|---|---|---|
| 表达式解析与求值（隐式乘法 / 右结合幂 / RAD-DEG） | ✅ | ✅ | ✅ | ✅ |
| 二次 / 三次 / 四次方程求解（含复根） | ✅ | ✅ | ✅ | ✅ |
| 精确形式 | 三分量 (n/d)·√r | **五分量 (a+b√c)/d ×10^e** | ✅ 三分量 | ✅ **五分量** |
| 有理根检验 + 低次多项式因式分解 | ❌ | ✅（core_ratroot.js，文件 9） | ❌ | ✅（ac_ratroot + ac_symbolic） |
| 三次 / 四次方程整数与分数根精确形式 | ❌ | ✅（有理根快路径） | ❌ | ✅（随有理根） |
| 泰勒级数展开（数值，≤16 阶，系数缓存 + Horner 求值） | ❌ | ✅（core_calculus.js 模块七） | ❌ | ✅（ac_calculus） |
| 线性方程组（列主元高斯消元） | 6 元（42 = 6×7） | **12 元（156 = 12×13）** | ✅ 6 元（42） | ✅ **12 元（156）** |
| 数值微积分：一阶/二阶导数、定积分、极限 | ❌ | ✅ | ❌ | ✅（ac_calculus） |
| 双曲 / 反双曲函数（sinh..atanh） | ❌ | ✅（另含 sec..atanh 高阶三角，v1.3.5 起） | ❌ | ✅（ac_math） |
| 复数域函数（cExp / cLn / cPowReal） | ❌（仅求解器内部 cSqrt） | ✅ | ❌ | ✅（ac_complex，12 API） |
| **多变量多项式项表**（解析/求值/四则，2 变量 deg ≤6，v2.0.0） | ❌ | ✅（core_multivar，与 C Pro 层同 exps 编码） | ❌ | ❌（Pro 层具备，见下节） |
| **多变量符号求导 d/dx、d/dy**（逐项幂法则，v2.0.0） | ❌ | ✅（`diff`） | ❌ | ❌（Pro 层具备） |
| **矩阵基础运算**（乘/转置/行列式/逆，v2.0.0） | ❌ | ✅ ≤3×3 闭式（core_matrix） | ❌ | ✅ 12×12 完整（ac_matrix） |
| **特殊函数 erf / gamma / lgamma**（v2.0.0） | ❌ | ✅（core_special，A&S 26.2.17 + Lanczos g=7） | ❌ | ✅（ac_special，超集） |
| 进制转换（2/8/10/16）+ SI 词头 | ❌ | ✅（core_base.js） | ✅ 进制 + 分数显示（**无 SI**） | ✅（ac_base，含 SI + 工程记数 + 分数显示） |
| preCheck 阈值（深度/函数/变量） | ≤3 / ≤2 / ≤5 | **≤6 / ≤4 / ≤10** | ≤3 / ≤2 / ≤5 | ✅ **≤6 / ≤4 / ≤10** |
| 内存水位门禁 | 48KB（64KB × 75%） | 96KB（128KB × 75%） | ✅ 48KB | ✅ **96KB** |
| 家族共享一致性用例（tests/ 三套 JSON） | ❌（直连用例内嵌） | ✅（test-consistency.js） | ✅（smoke_runner） | ✅（smoke_runner + diff-smoke128.js） |

## C99 独有模块（v2.0.0 后 JS 128KB 已补齐基础版的以「基础版」标注）

| 模块 | 功能 | JS 128KB（v2.0.0） | C99 64KB | C99 128KB |
|---|---|---|---|---|
| 大整数 | 20 limb × base 1e9（180 位十进制）：加减乘除 + 阶乘（n≤100）+ 斐波那契（n≤200） | ❌ | ❌ | ✅（ac_bigint） |
| 矩阵 | 最大 12×12：乘法 / 转置 / 秩 / 行列式 / 逆 / 特征值（幂法）/ QR | 基础版（≤3×3 闭式，无秩/QR/特征值） | ❌ | ✅（ac_matrix） |
| 特殊函数 | Gamma / lgamma / Beta / erf / 不完全 Gamma / Bessel J_n | 基础组（erf / gamma / lgamma） | ❌ | ✅（ac_special） |
| 符号求导 + 因式分解 | 扁平数组表达式树符号求导；整系数有理因式分解 | ❌（项表式求导见上表，无表达式树） | ❌ | ✅（ac_symbolic） |
| 数论 | gcd/lcm、6k±1 试除素数、Miller-Rabin、质因数分解、nCr/nPr | ❌ | ✅（**无 Miller-Rabin**） | ✅（含 Miller-Rabin） |
| 工具 | CRC32 / SHA-256 / Base64 / 定点数 | ❌ | ✅ | ✅（另含单位换算 + 物理常数） |

## Pro 层（C/128KB-ProTEXT v2.0.0，冻结上游之上叠加，其余版本均无）

多变量 GCD / 因式分解 / Sylvester 结式、LU 分解、实特征向量（Hotelling deflation）、
隐函数求导、高次数值求根（Durand-Kerner）、符号积分（64 项表 + 分部积分）、
**多变量符号求导 / 多变量多项式积分（精确整除约束）**、sin/cos LUT 常量表化
（`.rdata`，查表零 `.bss`）。`.bss` 41 456 B，281 项测试。
详见设计说明与 [RELEASE-v2.0.0.md](RELEASE-v2.0.0.md)。

## 未落地项

- 结果历史环形缓存：四版本均未规划落地时间（UI 层职责边界未定）。
- 五次及以上多项式求根：阿贝尔-鲁菲尼定理，无根式解，`guardDegree` 永久拒绝。

## 已知接口差异

- `guardMultivariateNonlinear`：JS 64KB / JS 128KB 中已定义并导出，但**未被任何求解流水线
  调用**（死代码，仅在测试中以"单变量解析器：恒通过"调用）。C99 两版均不提供该函数。

## 显示层功能边界

科学计数法显示、工程记数法（SI 词头）、分数显示、精确形式渲染（`√`、上标、
混合形式排版）等**属于 UI 层职责，不在本内核仓库范围**。内核侧只提供数据契约：

- 精确形式五分量 getter：`getRootExactRat / RadNum / Rad / Den / Exp / Valid`（每根一组）；
- 近似值：`getRootRe / getRootIm`（双精度）；
- 进制与 SI 的数据出口：`decToXxx → baseToString`、`siNormalize → getSiMantissa / getSiPrefixExp / siToString`；
- 泰勒系数：`getTaylorCoeff(k) / getTaylorOrder`（UI 层自行排版为多项式文本）。

显示串的格式约定（如 `tests/exact-forms.json` 的 "√(2)"、"1/2" 写法）仅用于
一致性测试比对，不构成内核的显示承诺。
