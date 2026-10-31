# 对标调研：Giac / Xcas

> **来源与口径**：任务下发的仓库外调研材料（ROADMAP §一 三份对标输入之一），由
> feat/release-v2.3.0 收尾组于 2026-10-31 落库。落库仅做结构整理与行文顺滑，
> **不新增事实**；原文的查证锚点（官方手册《Symbolic algebra and Mathematics
> with Xcas》(De Graeve & Parisse) 的章节/页码、fourier.univ-grenoble-alpes.fr
> 官方页）逐条保留。本仓库无法在本机复核这些外部文档的现行内容，引用以原文为
> 准；ROADMAP.md:39 的「未经本仓库核实的转述」声明在此适用。
>
> **定位**：C++ 内核 + Xcas 图形前端的符号/数值计算系统（HP Prime 计算器同源
> 引擎），本仓库对标其手册章节结构所代表的「计算器级 CAS 功能面」——与本项目
> 嵌入式定位最接近的一库。

## 一、初等运算

**normal(expr) / simplify(expr) / expand(expr)** — 有理式/代数式的范式化简、约分
通分与乘积展开，是 Xcas 最基础的化简入口。极高频；
`normal((x^2-1)/(x-1)) → x+1`；另配 `subst`（替换，§2.12.16）、`ratnormal`、
`propfrac`。出处：官方手册 §2.12.5 / §2.12.13-15（p.80-87）。
→ 本仓库对应物：C 侧 v2.3.0 `ac_pro_rational.c` 的 cancel（同 normalize 家族）。

**factor(poly)** — 多项式因式分解（在 Z、Q、Z/pZ、代数数域上），亦可分解整数。
极高频；`factor(x^2-1) → (x-1)*(x+1)`。出处：同手册 §2.12.9（p.82）、
§2.24.13（p.151）、§2.31.14 Z/pZ 上分解（p.196）。

**texpand(expr) / trigexpand / tlin / halftan** — 超越/三角表达式重写族：展开
（texpand）、三角展开（trigexpand）、三角线性化（tlin）、半角化（halftan）。
常用；`tlin(sin(x)^2)` 类线性化。出处：同手册 §2.20.1（p.119）、§2.21.2-3
（p.122-123）；halftan/hyp2exp 见官方页
fourier.univ-grenoble-alpes.fr/~parisse/giac_us.html。

## 二、方程求解

**solve(eq, x) / cSolve(eq, x)** — 代数方程精确求解，返回解列表；也接受方程
向量组（多项式方程组）；cSolve 在复数域求解。最高频；手册示例
`solve(x^4-1=3) → [sqrt(2), -sqrt(2)]`。出处：同手册 §2.51.6-7（p.306-308）。

**fsolve(eq, x) / nSolve(eq, x)** — 方程/方程组数值求根，提供多种求解器选项
（dnewton_solver、hybridj_solver 等）。高频；支持非线性方程组。出处：同手册
§4.4-§4.5（p.359-362）。

**linsolve(system, vars) / simult(A, B)** — 线性方程组求解：linsolve 直接解方程组
向量，simult 解矩阵形式 A·X=B。高频；syst2mat 可把方程组化为增广矩阵。出处：
同手册 §2.52.4 / §2.52.6（p.311-312）。

**desolve(ode, y) / deSolve / dsolve** — 常微分方程解析求解：常系数线性、一阶
可分离/齐次/Bernoulli/Clairaut 等，支持初值条件与方程组；配套 `laplace`/
`ilaplace` 变换。常用；手册示例 `desolve([y''+2*y'+y, y(0)=1, y'(0)=0], y)`；
数值 ODE 另有 odesolve（§4.3.4）。出处：同手册 §2.53.1-2（p.314-321）。
→ 本仓库对应物：v2.3.0 双侧 ODE（范围收窄到常系数线性齐次，Giac 的
可分离/Bernoulli 级需要任意函数符号积分做积分因子，ROADMAP 已声明不做）。

**proot(poly) / sturm(poly, x)** — 多项式数值求根（proot）与基于 Sturm 序列的
实根个数统计/隔离（sturm、sturmab）。常用。出处：同手册 §4.6（p.362）、
§2.25.16-17（p.171-172）。

## 三、微积分

**diff(f, x[, x…])** — 符号求导：一阶/n 阶导数与偏导数，支持多元向量形式；
另有 function_diff、梯度/散度/旋度（§2.50）。极高频；`diff(x*y^2*z^3, x)` 类。
出处：同手册 §2.17.5（p.107）、§2.50.1 梯度（p.301）。

**integrate(f, x[, a, b])** — 不定积分（原函数）与定积分的精确计算（四参数形式
返回精确值）。极高频；`integrate(x^2) → x^3/3`；数值积分用 romberg/nInt
（§4.3.3）。出处：同手册 §2.18.1（p.109）。

**limit(f, x, a)** — 极限计算，含单侧极限与无穷远处极限。高频；官方页
giac_us.html 说明 limit 与 series 均基于 mrv 算法实现。出处：同手册 §2.19.1
（p.117）。

**series(f, x=a, n) / taylor(f, x, n)** — 泰勒/渐近级数展开，可控制阶数并处理
展开点邻域行为。高频。出处：同手册 §2.33.2-3（p.204-205）。

**sum(f, n, a, b)** — 离散求和的精确计算；配套 sum_riemann 黎曼和、ibpdv/ibpu
分部积分辅助。常用。出处：同手册 §2.18.2-4（p.111-116）。

## 四、线性代数

**det(A) / inv(A) / tran(A) / rank(A)** — 矩阵基本量：行列式、逆矩阵、转置、秩
（另有 trace、det_minor、trn 共轭转置）。极高频。出处：同手册 §2.43.1-7
（p.269-271）。

**rref(A) / ker(A)** — 高斯-若尔当消元化约化阶梯形（rref/ref）与核/零空间
（ker/nullspace），配套 basis、image、colspace。高频。出处：同手册 §2.52.2-3
（p.309-310）、§2.43.9-14（p.272-274）。

**egvl(A) / egv(A)** — 特征值（egvl/eigenvalues/eigVl）与特征向量（egv/
eigenvectors/eigVc）计算。高频；精确系数矩阵无精确根时返回数值对角化。出处：
同手册 §2.46.2-3（p.279）。

**jordan(A) / pcar(A) / pmin(A)** — Jordan 标准形分解、特征多项式（pcar/
charpoly，Hessenberg 加速）与最小多项式。常用；另有 rat_jordan、companion、
ihermite/ismith 标准形。出处：同手册 §2.46.4-8（p.280-285）。

**lu(A) / qr(A) / cholesky(A) / svd(A)** — 矩阵分解族：LU、QR、Cholesky、奇异值
分解，另有 lll 格基约减。常用；LU/QR 返回分解因子组合。出处：同手册 §2.48.1-7
（p.291-295）。
→ 本仓库对应物：双侧 LU / QR / 对称特征对（C ac_matrix + ac_pro_eigen；
JS core_matrix pro 层 v2.3.0 对齐）。SVD/Cholesky 在 ROADMAP 落选声明中。

## 五、代数几何

**gbasis([eqns], [vars]) / greduce(f, [eqns], [vars])** — 计算多项式理想的
Gröbner 基（gbasis，支持 lex/tdeg 等单项序）与按 Gröbner 基的范式约化
（greduce）；lex 序基可对方程组消元。代数几何代表功能；手册示例
`gbasis([2*x*y-y^2, x^2-2*x*y], [x, y])`。出处：同手册 §2.27.1-2（p.180-181）。
→ 本仓库对应物：C 侧 2 变量 Groebner（M4GB，lex 序，deg ≤ 4）。

**resultant(p, q, x) / sylvester(p, q, x)** — 两多项式的结式（公共根判据与消元
工具）及其 Sylvester 矩阵。常用；常与 gbasis 配合做消元。出处：同手册
§2.25.18-19（p.173-174）。

## 六、数论

**ifactor(n) / is_prime(n) / nextprime(n)** — 整数素因子分解、素性测试、相邻
素数；配套 gcd/egcd、iquo/irem/smod、ichinrem 中国剩余、euler φ 函数。常用。
出处：同手册 §2.6.6 / §2.6.16-17（p.41, 46-47）、§2.6.19-25（p.48-51）。
→ 本仓库对应物：双侧数论模块（JS 侧 v2.3.0 补齐）；中国剩余与 euler φ
超出本仓范围。

## 七、对本仓库的映射要点

- Giac 的手册章节结构（初等 → 方程 → 微积分 → 线性代数 → 代数几何 → 数论）
  与 ROADMAP §一 的对标维度切分一致；其「计算器级」裁剪（desolve 覆盖面、
  数值求解器选项族）是本仓功能边界决策的直接参照。
- `desolve` 的广覆盖（可分离/Bernoulli/Clairaut）反衬本仓 ODE 只做常系数线性
  齐次的取舍：那些类型需要任意函数的符号积分做积分因子，超出两侧积分引擎能力。
- `proot`/`sturm` 对应本仓 Durand-Kerner + 牛顿迭代的高次数值求根路径；
  Sturm 隔离未纳入（禁递归约束下的工程取舍）。
