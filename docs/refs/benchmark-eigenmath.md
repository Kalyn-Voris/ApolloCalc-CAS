# 对标调研：Eigenmath

> **来源与口径**：任务下发的仓库外调研材料（ROADMAP §一 三份对标输入之一），由
> feat/release-v2.3.0 收尾组于 2026-10-31 落库。落库仅做结构整理与行文顺滑，
> **不新增事实**；原文的查证锚点（官方 README / help.html / doc/*.tex、
> GitHub 仓库源码路径与行号、Casio Prizm 经典移植版源码文件名）逐条保留，
> 其中标注「已抓取核对 / 已 grep」者为原文自称的核实状态。本仓库无法在本机
> 复核这些外部资源的现行内容，引用以原文为准；ROADMAP.md:39 的
> 「未经本仓库核实的转述」声明在此适用。
>
> **定位**：单文件级轻量 CAS（C 与 JavaScript 双实现），「薄内核 + 脚本层」
> 设计样本——与本项目的双内核架构和 128KB 预算意识直接可比，是三库中
> 工程形态最相近的参照。

## 一、初等运算

**精确算术与符号求值（+ − * / ^，大整数/有理数）** — 任意精度整数与精确分数
运算，结果自动约简为最简代数形式。全库基础、最高频；README 示例
`212^17 = 3529471145760275132301897342055866171392`（已抓取核对）；同仓库含
逐函数对应的 JS 移植（js/src 约 1.85 万行，与 C 内核约 1.9 万行），双实现
同构——**与本项目双内核架构直接对标**。

**simplify(x)；rationalize(x) / numerator(x) / denominator(x)** — 通用表达式
化简；通分为单一分式并提取分子/分母。高频；官方 help.html 逐条目含示例；
现行版以此替代经典版 condense/collect 类函数（已删）。

**factor() / expand()（仅经典版）** — 整数与多项式因式分解、乘积展开。经典版
核心（Casio Prizm 源码 factor.cpp / expand.cpp / factorpoly.cpp）；现行重写版
（2019 起）内建表 161 项中已无此二关键字（grep 零命中），多项式自动展开，
因式分解仅作 roots 的内部例程（src/factor.c）。

**复数运算 polar(z) / rect(z) / conj / real / imag / arg / mag / clock** —
直角↔极坐标互化、共轭、实虚部、模与辐角；`i = sqrt(-1)` 由启动脚本定义。
README 示例 `polar(1+i) → 2^(1/2)·exp(iπ/4)`；i 的定义见 js/src/initscript.js:1。

**初等函数与特殊函数族（sin/cos/tan/arcsin…arctanh、exp、log、sqrt；erf/erfc/
tgamma/lgamma/incbeta；fdist/tdist/tdistinv）** — 完整初等函数族加误差函数/
伽马族/不完全贝塔/统计分布。高频；help.html 每函数一条目；经典版另有
besselj/bessely/gamma/dirac/laplace（见 Prizm 源码文件清单）。

**mod(a, b) 与 abs/ceiling/floor/sgn** — 取余、上下取整、绝对值、符号。
现行版唯一数论面；经典版另有 gcd/lcm/isprime/prime/divisors（Prizm 文件清单），
现行版 gcd 仅作内部大数例程（src/bignum.c）不对用户开放。
→ 对照：本仓 JS 侧 v2.3.0 补齐的数论模块覆盖了 Eigenmath 现行版没有的
素性检测与质因数分解面。

**factorial(n) / choose(n, k) / binomial 与 hermite/legendre/laguerre** —
阶乘、组合数与三类正交多项式。现行版正交多项式全部由 init 脚本以 d/sum 派生
定义（js/src/initscript.js:6-8），是「薄内核 + 脚本层」设计样本，值得借鉴。

## 二、方程求解

**roots(p, x)** — 多项式精确根（有理根），重根以向量返回。代数方程主力；
help.html 示例 (x+1)(x-2) → (−1, 2)；src/eval_roots.c 内部先做多项式因式分解。
**⚠ 全库无 Gröbner 基/结式/消元（全源码 grep 无命中），此维度 Eigenmath 为空白**。

**nroots(p, x)** — 数值求根，支持实/复系数多项式，重根以向量返回。help.html
示例 x^5−1 给出全部 5 个复根；src/eval_nroots.c。
→ 对照：本仓 C 侧 DK 求根同能力面；「重根以向量返回」的语义是 v2.4.0
功能 9（square-free 与重根重数）的参照。

**线性方程组 X = dot(inv(A), B)** — 用矩阵逆与点积解 AX=B，得精确有理解。
官方文档即此法（doc/linear-algebra.tex:43-55，示例 A=((3,7),(1,-9)) →
(−5/17, 41/17)）；全库无 solve/lsolve/linsolve 关键字。
→ 对照：本仓双侧的列主元消元专用管线（6/12 元 + C 侧稀疏）在此维度强于
Eigenmath 现行版。

**dsolve（一阶线性 ODE）（仅经典版）** — 解 q(x)y′ + p(x)y = g(x) 型一阶线性
常微分方程。仅经典 Casio Prizm 移植（src/dsolve.cpp，注明 u = exp(∫p) 解法）；
**现行重写版无任何 ODE 功能**。
→ 对照：本仓 v2.3.0 双侧 ODE（常系数线性齐次，阶 1..4）在阶数与根类覆盖上
超出 Eigenmath 经典版的一阶线性，但仍不含其非齐次 q(x)y′+p(x)y=g(x) 型。

## 三、微积分

**d(f, x, …)** — 一条命令覆盖符号微分：偏导 d(f,x,y)、梯度 d(f,(x,y))、雅可比
d(F,X)、n 阶导 d(f,x,n)。最高频微积分功能；n 阶逻辑 src/eval_derivative.c:30-46；
grad/curl/div 由 init 脚本派生。
**⚠ 现行版无 limit（极限）函数（内建表与全源码 grep 无命中），经典移植版有
limit.cpp（eval_limit）**。
→ 对照：本仓双侧的数值微积分（导数/定积分/极限/泰勒）在「极限」维度强于
Eigenmath 现行版；符号微分表达式树本仓双侧均缺（v2.4.0 规划）。

**integral(f, x, …)** — 符号不定积分（有理函数、exp/log/三角族；与 d 互逆可
识别）。积分引擎约 1500 行规则（src/eval_integral.c）；help.html 示例
`integral(x^2, x) = x³/3`。

**defint(f, x, a, b, …)** — 定积分，参数可扩展为多重积分。help.html 示例
`defint(sin(theta), theta, 0, pi, phi, 0, 2pi) = 4π`。

**taylor(f, x, n, a) 与 sum(k, a, b, f) / product(k, a, b, f)** — 泰勒级数展开
（默认 0 点 n 阶）加符号求和/连乘。help.html 各条目；级数类功能仅此，
无傅里叶/渐近展开。

## 四、线性代数

**矩阵运算 det/inv/adj/transpose/contract(迹)/minor/minormatrix/unit(n)/zero** —
行列式、逆、伴随、转置、缩并、子式/余子阵、单位阵与零张量。help.html +
linear-algebra.tex 恒等式 inv(A) = adj(A)/det(A)；矩阵字面量形如 ((1,2),(3,4))。

**dot/inner/outer/hadamard/kronecker** — 任意阶张量的内积（dot 右结合求值）、
外积、逐元素积、克罗内克积；矩阵乘法即 dot(A,B)。linear-algebra.tex:1-35；
另有 cross（3 维向量积）由 init 脚本定义（js/src/initscript.js:3）。

**eigenvec(m)（经典版另有 eigenval(m)）** — 数值对称实方阵特征向量（Jacobi
迭代）；特征值经 D = dot(transpose(Q), A, Q) 取对角线。src/eval_eigenvec.c 要求
float 对称方阵（容差 1e-10，100 步迭代）；help.html 示例 A=((3,5),(5,3)) →
D 对角 8, −2；经典版 eval_eigenval 直接返回特征值（Prizm eigen.cpp:67）。
**⚠ 无 LU/QR/Cholesky 等分解接口，矩阵分解仅此数值对称对角化**。
→ 对照：本仓 JS v2.3.0 的幂法+Hotelling deflation 与其 Jacobi 迭代同为
「对称特征对」路线；本仓另有 LU/QR 分解接口。

## 五、其他

**draw(f, x) 与脚本编程（f(x)=…、for/do/loop/break/test/check、run(script)）** —
函数绘图；用户自定义函数、脚本文件执行、循环/条件/断言。js/README.md 给出
浏览器嵌入用法（sinc.html 绘图示例）；官方演示脚本 4 个：physical-constants
（CODATA 2022）、dirac-hydrogen-atom 等。

**物理单位制与量纲运算** — 单位作为符号参与运算、缩写显示与量纲换算。
doc/units-of-measure.tex:9-49 示例 `v = 1.2 meter/second`、
`h = 6.626e−34 joule second`、`joule = kilogram meter^2/second^2`；对轻量物理
计算 CAS 是差异化能力。
→ 对照：单位制属显示层/UI 职责，ROADMAP 落选声明明确不做。

## 六、对本仓库的映射要点

- **架构同构性**：C 内核 ≈ 1.9 万行 + JS 逐函数移植 ≈ 1.85 万行的双实现形态，
  与本项目「同一数学引擎、JS/C99 双语言、跨语言对拍」的家族结构直接可比；
  其「薄内核 + initscript.js 脚本层派生（正交多项式/grad/cross）」是零 GC
  预算下扩展功能的另一条路。
- **空白即定位**：Eigenmath 现行版无 Groebner/结式、无 limit、无 ODE（重写版）、
  数论面极薄——本仓在这些维度的取舍（C 侧 Groebner/结式已落地、双侧数值
  极限已落地、v2.3.0 补双侧 ODE 与 JS 数论）构成差异化。
- **版本演化的警示**：经典版 → 现行版重写删掉了 factor/expand/limit/dsolve
  等关键字（内建表 161 项收敛），说明轻量 CAS 的功能面会随架构重写主动收缩；
  本仓以契约文件（tests/*.json）冻结行为后再演进，正是为了规避这类静默收缩。
