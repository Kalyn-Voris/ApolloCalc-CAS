# 对标调研：SymPy

> **来源与口径**：任务下发的仓库外调研材料（ROADMAP §一 三份对标输入之一），由
> feat/release-v2.3.0 收尾组于 2026-10-31 落库。落库仅做结构整理与行文顺滑，
> **不新增事实**；原文的查证锚点（docs.sympy.org 各章节、`sympy/matrices/`
> 等源码路径）逐条保留。本仓库无法在本机复核这些外部文档的现行内容，
> 引用以原文为准；文首ROADMAP.md:39 的「未经本仓库核实的转述」声明在此适用。
>
> **定位**：Python 生态全功能符号计算库，本仓库对标其「教程化简章覆盖面」——
> 即官方入门教程与求解指南认为一个 CAS 必须有的功能清单。

## 一、初等运算

**simplify(expr)** — 对任意表达式做多策略启发式化简，逼近最简形式。官方文档明言
结果**无唯一性保证**（SymPy 自己拒绝承诺最简性，这是全行业化简难题的参照点）。
官方入门教程化简章的开篇功能，使用频率极高；典型例子
`simplify(sin(x)**2 + cos(x)**2) → 1`（查证：docs.sympy.org tutorials/intro-tutorial/simplification）。

**expand(expr) / factor(expr)** — 多项式展开；与在有理数域上的因式分解（可代数
扩张 `extension=sqrt(2)`、`modulus`、`gaussian`）。教程化简章必讲，频率极高；
`factor` 另有 `factor_list` 结构化输出与 `sqf` 无平方分解（polys/reference 查证）。

**cancel(expr) / apart(expr)** — 有理函数化为 p/q 标准形并约去公因子；`apart`
做部分分式分解。教程化简章示例，积分前预处理常用；示例
`cancel(1/x + (3*x/2 - 2)/(x - 4))`（docs simplification 章查证）。
→ 本仓库对应物：C 侧 v2.3.0 `ac_pro_rational.c` 的 cancel/apart。

**trigsimp(expr)** — 用三角/双曲恒等式化简；配套 `expand_trig` 做和角倍角展开。
教程三角节；典型 `trigsimp(sin(x)*tan(x)/sec(x)) → sin(x)**2`（docs 查证）。

## 二、方程求解

**solve(eq_or_system, *vars)** — 通用求解器：代数方程、方程组、函数零点，输出
列表/字典。官方求解指南首选入口，频率极高；多项式求根另有
`roots` / `nroots` / `real_roots` / `all_roots`（polys/reference 查证）。
`solveset` 文档指出 `solve` 的空列表无法区分「无解」与「未解出」。

**solveset(f, x, domain)** — 集合版单变量方程/不等式求解，返回 Set：有限集、
ImageSet（无穷解）、ConditionSet（未完全解出），实/复数域由 domain 参数分离。
官方定位为 `solve` 的改进接口（输出一致、可表示无穷解集）；典型
`solveset(exp(x) > 1, x, S.Reals) → (0, ∞)`（docs modules/solvers/solveset 查证）。

**linsolve(system, syms) / nonlinsolve(system, syms)** — 线性方程组专用求解器
（Gauss-Jordan，支持欠定/超定，三种输入形式）与非线性方程组求解器（多项式系统
内部走 Gröbner 基）。官方求解指南并列入口；`linsolve((A, b), syms)` 支持增广
矩阵形式；`nonlinsolve` 支持正维数系统与参数化解（docs 查证）。

**nsolve(f, x, x0)** — 给定初值的数值求根（单变量/方程组），如
`nsolve(cos(x) - x, x, 1) → 0.739085…`。官方数值求解指南入口，需初值；系统无法
代数求解时的退路（docs guides/solving 查证）。

**dsolve(ode, y(x))** — 常微分方程符号求解（通解与初值特解），如
`dsolve(y'' + 9y, y(x))`。官方求解指南的独立 ODE 章节，ODE 求解为独立子系统，
属代表性大功能。→ 本仓库对应物：v2.3.0 双侧 ODE（范围收窄到常系数线性齐次）。

## 三、微积分

**limit(f, x, x0, dir)** — 符号极限，支持单侧方向（'+'/'-'）与无穷点；文档建议
奇异点求值用它代替 subs。教程微积分章必讲；典型 `limit(sin(x)/x, x, 0) → 1`、
`limit(1/x, x, 0, '+') → ∞`（docs 查证）。

**diff(expr, x, n) / expr.diff(...)** — 任意阶、多变量混合偏导的符号微分；未求值
Derivative 对象配 `.doit()`。教程微积分章开篇，频率极高；示例
`diff(cos(x), x) → -sin(x)`、`diff(expr, x, y, 2, z, 4)`（docs 查证）。

**integrate(f, (x, a, b), ...)** — 不定/定/多重符号积分，支持无穷区间；失败时返回
未求值 Integral；`.evalf()` 做数值积分（默认 tanh-sinh 求积，振荡积分
`quad='osc'`）。教程微积分章核心，频率极高；示例
`integrate(exp(-x**2 - y**2), (x, -oo, oo), (y, -oo, oo)) → π`
（docs calculus 章 + modules/evalf 查证）。

**series(expr, x, x0, n)** — Taylor 级数展开至指定阶，带 Landau 大 O 项，
`.removeO()` 去除。教程微积分章；默认 x0=0、n=6，支持任意展开点（docs 查证）。

## 四、线性代数

**Matrix([[...]]) 与 det/inv/T/rref/nullspace** — 符号元素矩阵构造与基本运算
（加减乘幂、求逆 `M**-1`、行列式、转置），`rref` 简化行阶梯形、零空间/列空间。
教程矩阵章全部内容，频率极高；文档提示 row_del/col_del 为原地修改、符号矩阵
零测试是已知难点（docs matrices 教程查证）。

**M.eigenvals() / M.eigenvects() / M.diagonalize()** — 符号特征值（返回
{特征值: 代数重数} 字典）、特征向量、可对角化时返回 (P, D) 满足 M = P·D·P⁻¹；
另有更快的 charpoly。教程矩阵章高级节；返回精确根式值而非浮点（docs 查证）。

**M.LUdecomposition() / M.QRdecomposition() / M.cholesky()** — LU（带置换）、QR、
Cholesky 分解；配套 LUsolve/QRsolve/cholesky_solve/gauss_jordan_solve 解线性
方程组，另有 LDL、SVD、Schur 分解。矩阵模块标准 API（实现见
sympy/matrices/decompositions.py，已 grep 源码查证
`_LUdecomposition`/`_QRdecomposition`/`_cholesky`/`_LDLdecomposition`/
`_singular_value_decomposition`）。

## 五、代数几何

**groebner(F, x, y, order='lex')** — 计算约化 Gröbner 基（lex/grlex/grevlex 序，
Buchberger 与 F5B 算法），返回 GroebnerBasis（方法 `.reduce` / `.fglm` 零维转换 /
`.is_zero_dimensional`）；另有 `reduced(f, G)` 模理想约化。polys 模块官方 API，
`nonlinsolve` 内部亦走此路径；典型
`groebner([x*y - 2*y, 2*y**2 - x**2], x, y, order='lex')`（docs modules/polys/
reference 查证）。注：当前版本无独立公开的 eliminate 消元函数，消元经 lex 序
Gröbner 基/结式实现（据最新文档 API 锚点清单与 master 源码 grep，已查证）。
→ 本仓库对应物：C 侧 2 变量 Groebner（M4GB）。

**resultant(f, g)** — 两多项式结式（可选 includePRS 返回子结式列）；另有
`subresultants`、`discriminant`、`sturm`，以及多元 Dixon/Macaulay 结式模块
`multivariate_resultants`。典型 `resultant(x**2 + 1, x**2 - 1) → 4`，用于判公共根
与变元消元（docs polys/reference 查证；多元结式见
sympy/polys/multivariate_resultants.py 源码 docstring，已查证）。

## 六、其他

**N(expr, n) / expr.evalf() 与 lambdify(args, expr, 'numpy')** — 任意精度数值求值
（底层 mpmath，自动误差跟踪、精度几乎无上限）与符号表达式 → numpy/math/mpmath
数值函数的代码生成。evalf 示例 `N(pi/E, 100000)` 可算十万位；lambdify 支持 cse
公共子表达式提取，是对接数值后端的桥梁（docs modules/evalf +
modules/utilities/lambdify 查证）。

**factorint(n) / isprime(n)** — 整数质因数分解（返回 {素数: 指数} 字典，内置
pollard_rho/pollard_pm1 算法）与 Miller-Rabin 系素性检测。ntheory 模块入口级
功能；典型 `factorint(2000) → {2: 4, 5: 3}`、`isprime(25) → False`；另有
`primerange` / `nextprime` / `crt`（docs modules/ntheory 查证）。
→ 本仓库对应物：双侧数论模块（C `ac_numtheory.c`；JS `core_numtheory.js`
v2.3.0 补齐，Miller-Rabin 底数集与 C 侧逐字一致）。

## 七、对本仓库的映射要点

- SymPy 教程化简章的高频面（求值、低次方程、diff/integrate/limit/series、矩阵、
  数论）与本仓库双侧已具备能力高度重合；差距集中在化简（simplify/trigsimp，
  ROADMAP 明确不做）与 ODE 深度（本仓仅常系数线性齐次）。
- `solveset` 对 `solve` 空列表二义性的批评，与本仓「错误码走返回值、从严拒绝
  绝不硬凑」的纪律同向。
- `factor_list`/`sqf` 的结构化输出形态，是 v2.4.0 功能 9（square-free 分解与
  重根重数）的输出契约参照。
