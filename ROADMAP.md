# ApolloCalc-CAS · v2.3.0 / v2.4.0 数学功能路线图

> 成文基准:全局 v2.2.0(2026-10-27 发布,C 侧 Pro 层组件 v1.4.0、JS 内核组件 v2.2.0,双轨制)。
> C 侧基线数字为 2026-10-30 本机实跑;JS 侧基线随 terser 单遍压缩管线落地于 2026-10-31 复测更新,均为本机实测:
> - `powershell -NoProfile -ExecutionPolicy Bypass -File C/128KB-ProTEXT/verify.ps1` → **RESULT: 31 passed, 0 failed**(含 runner 396 checks 0 failed、.bss 账本 42608/42640/39408、degraded 38672、每功能用例数 B1=7/A4=6/A6=9/A5=6/B2=6/SP=12/BD=12/GB=12;2026-10-30 实跑)
> - `node test/run-all.js`(JS/128KB)→ **TOTAL: 603 pass, 0 fail**(12/12 文件;2026-10-31 在 34 510 B 新内核上复跑确认)
> - `node tests/bench/diff-smoke128.js` → **50/50 用例一致**,exit 0(2026-10-30 实跑)
> - `JS/128KB/src/core_kernel.js` 实测 **34 510 B**(2026-10-31,stat 与 `git cat-file -s HEAD:JS/128KB/src/core_kernel.js` 一致;压缩管线仍在并行演进,本文件字节预算一律写**增量**,绝对值以各功能开工时 HEAD 实测为准)
>
> 修订记录:v2(2026-10-31)按 13 条独立评审意见修订——JS 基线更新、verify.ps1 文案修正任务归组并废除写死总数、run-all.js 改「自动发现」口径、有理函数/square-free 补钉整系数域契约、Francis QR 的 Makefile 接线改无条件、静态池口径统一到 docs/js-128kb.md、对标表标注仓库外来源;关键修订处均已就地注明「评审修订」。

---

## 一、行业对标

综合 SymPy / Giac(Xcas) / Eigenmath 三份调研,提炼三库共有的最常用核心功能 15 条,逐条对照本仓库现状。
标注口径:**【已具备·C】**= C 侧有完整实现;**【已具备·JS】**= JS 侧有完整实现;**【部分】**= 有实现但能力边界明显;**【缺失】**= 无。

| # | 功能 | 代表库 | C 侧现状 | JS 侧现状 |
|---|---|---|---|---|
| 1 | 表达式解析与数值求值 | 三库共同地基(SymPy 基础、Giac §2 开篇、Eigenmath 全库基础) | 【已具备·C】上游 ac_parser/ac_evaluator,UTF-8 符号、隐式乘法、RAD/DEG | 【已具备·JS】parseExpression/evalExpressionAt,池 512 token/256 数值 |
| 2 | 低次方程精确求解(2/3/4 次,含复根) | SymPy solve、Giac solve(§2.51)、Eigenmath roots | 【已具备·C】ac_solvers 二/三/四次(不可约三角法/Cardano/费拉里)+ 有理根降次 | 【已具备·JS】solveQuadratic/Cubic/Quartic + ratRootDescent |
| 3 | 高次/通用数值求根 | SymPy nroots/nsolve、Giac proot/fsolve(§4.4)、Eigenmath nroots | 【已具备·C】牛顿迭代 + Durand-Kerner 5..8 次(Aberth 初始化),>8 拒绝 | 【部分】止步 4 次,guardDegree 对 ≥5 永久拒绝(features.md:54);无 DK 对应物 |
| 4 | 线性方程组 | SymPy linsolve、Giac linsolve/simult(§2.52)、Eigenmath dot(inv(A),B) | 【已具备·C】稠密 12 元(列主元)+ 稀疏 n≤32/nnz≤320(ac_pro_sparse.c) | 【部分】仅稠密 12 元;无稀疏对应物 |
| 5 | 符号微分 | 三库极高频(SymPy diff、Giac diff §2.17、Eigenmath d) | 【已具备·C】单变量表达式树(ac_symbolic,512 节点)+ 2 变量多项式逐项 + 隐函数 dy/dx | 【部分】仅多项式项表式 d/dx d/dy(core_multivar.diff);无表达式树、无一般函数、无隐函数 |
| 6 | 符号积分 | 三库极高频(SymPy integrate、Giac integrate §2.18、Eigenmath integral) | 【部分】64 项积分表 + 一层分部(ac_pro_integrate.c);两层分部(∫x²eˣ)与嵌套超越被 ETABLE 拒(README:611-614) | 【缺失】仅 Simpson 数值积分(calcIntegral) |
| 7 | 数值微积分(导数/定积分/极限/泰勒) | SymPy series、Giac series/limit、Eigenmath taylor | 【已具备·C】ac_calculus:中心差分/辛普森/左右极限/泰勒 ≤16 阶 | 【已具备·JS】calcDerivative/calcIntegral/calcLimit/calcTaylor,同规格 |
| 8 | 因式分解 | SymPy factor(§2.12)、Giac factor、Eigenmath(经典版,现行版内化进 roots) | 【部分】内容提取 + 有理根下降(ac_pro_multi_var_factor);不可约因子(x²+1)原样返回,Zassenhaus 已明确排除(README §6.6) | 【部分】仅有理根降次,无因式列表输出接口 |
| 9 | 矩阵(det/inv/rank/特征值/特征向量) | 三库极高频(SymPy Matrix 章、Giac §2.43/§2.46、Eigenmath det/inv/eigenvec) | 【已具备·C】12×12 全套(ac_matrix)+ LU(部分主元)+ 实对称全特征对(Hotelling deflation);非对称矩阵返 -2(ac_pro.h:269-270) | 【部分】仅 ≤3×3 闭式乘/转置/det/inv(core_matrix.js);无 rank/QR/特征值 |
| 10 | Groebner 基 / 结式 | SymPy groebner/resultant、Giac gbasis/greduce/resultant;Eigenmath 无(全源码零命中) | 【部分】2 变量 Groebner(M4GB,输入 deg≤4,熔断语义)+ Sylvester 结式(数值路径,阶≤12) | 【缺失】无对应物 |
| 11 | 数论(素性/质因数分解/gcd/组合数) | SymPy factorint/isprime、Giac ifactor/is_prime、Eigenmath mod(现行版唯一数论面) | 【已具备·C】ac_numtheory:gcd/lcm、6k±1 试除、Miller-Rabin、质因数分解、nCr/nPr | 【缺失】无任何对应模块(src/ 13 文件无 numtheory) |
| 12 | 大整数 / 高精度 | SymPy evalf(十万位)、Eigenmath 精确算术(212^17 招牌示例)、Giac | 【已具备·C】bigint 20 limb×1e9(180 位)+ bigdec 定点十进制(ac_pro_bigdec.c) | 【缺失】仅 Float64,>2^53 降级 |
| 13 | 特殊函数 | SymPy/Giac/Eigenmath 函数族(erf/gamma/bessel) | 【已具备·C】gamma/lgamma/Beta/erf/不完全 Gamma/Bessel J_n(ac_special) | 【部分】erf/gamma/lgamma(core_special.js);缺 Beta/igamma/Bessel |
| 14 | 常微分方程 ODE | SymPy dsolve(求解指南独立章节)、Giac desolve(§2.53)、Eigenmath 经典版(仅一阶线性) | 【缺失】双侧均无 | 【缺失】 |
| 15 | 通用化简 simplify / 三角化简 trigsimp | SymPy 化简章开篇(自称"无唯一性保证")、Giac normal/simplify/tlin | 【缺失】仅有精确形式的根式化简 | 【缺失】 |

**对标结论**:三库的高频核心高度重合:求值、低次方程、数值微积分、矩阵、求根这几条双侧都已站住。缺口集中在四处:**JS 侧的数论/矩阵/符号代数**、**双侧的 ODE**、**符号积分的深度**、**一般矩阵的特征值**。Groebner/结式是 C 有 JS 无的"半缺口";simplify/trigsimp 是全行业最难啃、SymPy 自己都拒绝承诺唯一性的方向,本路线图不做(理由见下节)。

> 来源说明(评审修订):上表的节号/示例/命令名摘自任务下发的三份调研输入(SymPy/Giac/Eigenmath),属仓库外材料,当前无法从本仓库逐条复核(docs/refs/ 现仅 miller-rabin-reference.js);命令名与库定位经查与三库公开文档常识一致,但在落库前本表按「未经本仓库核实的转述」对待。收尾组(feat/release-v2.3.0)负责把三份调研落库为 `docs/refs/benchmark-sympy.md` / `benchmark-giac.md` / `benchmark-eigenmath.md`,此后本表逐条可回查。

---

## 二、缺口评估(价值 × 可行性)

约束(全部实测/实读):

- **C 侧**:.bss 内核口径精确钉死 42608 B(编译期断言 ac_pro_solvers_ext.c:42-49),门禁 114 688 B(ac_pro_config.h:139),余量 72 080 B——**余量是够的,但每加一个 static 对象 TU 净涨 32 B**(16 B 段 + 16 B 链接对齐,ac_pro.h:290-294 实测论证),所以纪律是:错误码走返回值、新视图必须 ≤ 现峰值 sparse 3848 B(ac_pro_scratch.h:422)才能被 union 吸收零增长;超了就得同步改 Makefile:132-138、ac_pro_config.h 账本与 verify.ps1 目标值,账本闭合要重走。
- **C 侧算法**:禁递归(CONTRIBUTING.md:62,显式栈 + 后序标记)、零 malloc(--wrap=malloc/free 链接拦截,Makefile:70)、-Werror 零警告(Makefile:68)、上游 C/128KB/ 一行不改(verify.ps1 §5 git 双证)。
- **JS 侧**:内核压缩产物 34 510 B(2026-10-31 HEAD 实测;压缩管线并行优化中,57 523 → 34 510 已落地)≤ 131 072 B 文件线(build-kernel.js:193),**可读源码预算宽松、压缩后每 KB 都是净增量**;水位门禁 98 304 B(core_solvers.js:622),现全项目静态池 21.9 KiB(22 416 B,docs/js-128kb.md:135),余量充足;禁对象/数组字面量、禁热路径字符串拼接、禁递归(CONTRIBUTING.md:82-86);test-*.js 12 文件是红线不可改,新功能测试加新文件即可——run-all.js 以 readdirSync 自动发现 test-*.js(run-all.js:20),零改动纳入。
- **每功能测试代价**:C 侧每功能 ≥5-10 例进 verify.ps1 第 4 节(现有 GB=12/SP=12 为先例);JS 侧一个新测试文件 30-60 例的量级是常态(test-multivar.js 194 条为上限参照)。

### 入选候选(10 个)

| 候选 | 价值 | 可行性 | 判定 |
|---|---|---|---|
| JS 数论模块 | 三库全有;JS 全缺,是 JS 侧与 C 侧差距中最容易补的一块 | 极高:ac_numtheory 算法可 1:1 移植,纯标量+小缓冲,参照 core_special.js 先例 | **入 v2.3.0** |
| JS 矩阵升级 12×12 | 三库极高频;JS 仅 ≤3×3 闭式是四版本家族里最刺眼的不对称 | 高:消元/幂法/deflation 全是显式循环,C 侧同款算法是现成参照与对拍锚 | **入 v2.3.0** |
| 常系数线性 ODE | SymPy 求解指南独立章节、Giac desolve、Eigenmath 经典版唯一 ODE;双侧均为空白 | 高:n≤4 特征方程求根两侧管线现成(精确根式+复根),通解只是渲染问题;范围收窄到齐次常系数 | **入 v2.3.0(双侧)** |
| C 符号积分增强 | integrate 三库极高频;两层分部(∫x²eˣ)是教科书最常见型,现被 ETABLE 拒 | 中高:表扩容落 .rdata 零 .bss;"固定两层、非递归"的展开式实现不触碰禁递归红线,但要显式修订 README「只做一层」契约 | **入 v2.3.0** |
| C 单变量有理函数工具(GCD/cancel/apart) | SymPy cancel/apart 教程化简章、Giac normal/propfrac;积分前预处理标配 | 中:单变量欧几里得 GCD 比多变量 PRS 简单一个量级;apart 互异单根情形有闭式(A_i=p(r_i)/q'(r_i));新视图可压进 3848 吸收线 | **入 v2.3.0** |
| JS 符号微分引擎(表达式树) | diff/factor 极高频;JS 侧最大单块短板;C ac_symbolic 是现成参照 | 中高:RPN token 流现成(parseExpression),RPN→树是标准栈算法;~512 节点扁平池零 GC 可行 | **入 v2.4.0** |
| JS 符号积分 | integrate 极高频;JS 只有数值版 | 中:依赖上项的树;表与策略直接对齐 C 侧(含 v2.3.0 增强后的表) | **入 v2.4.0** |
| C 一般实矩阵特征值 | SymPy eigenvals/Giac egvl 面向一般矩阵;C 侧非对称返 -2 是成文缺口(ac_pro.h:269-270) | 中高:Householder→Hessenberg→Francis 隐式双移 QR 全显式循环;隐式 QR 不需 Q 累计阵,视图可压进吸收线 | **入 v2.4.0** |
| square-free 分解与重根重数(C+JS) | SymPy roots 返回重数字典、Eigenmath nroots 重根向量;C 侧 DK 现在不分重数(README §6.4) | 中:v2.3.0 的单变量 GCD 是直接地基,递推式 SFF 无递归 | **入 v2.4.0** |
| JS 结式 + Groebner 基 | SymPy/Giac 都有;C 侧已有,补齐后双内核代数面对等 | 中:结式只需 Sylvester 矩阵 + det(复用 v2.3.0 的 12×12 det);M4GB 移植按 C 侧 847 行参照,工作量大但有完整蓝本 | **入 v2.4.0(带降级预案)** |

### 落选候选(如实说明为什么不做)

- **通用化简 simplify / trigsimp**:价值公认最高,但需要表达式归一化排序 + 模式匹配引擎,工程量不小于本路线图全部功能之和;SymPy 文档自己都写明"结果无唯一性保证"。在 Zero-GC + 禁递归 + 128KB 的约束下不现实,整条路线图范围内明确放弃,后续也不作为近期目标。
- **结果历史环形缓存**:features.md:53 明示四版本均未落地、UI 层职责边界未定——内核越界,不碰。
- **大整数 / 高精度(JS 补齐)**:价值高(Eigenmath 的招牌),但两个版本的窗口装不下全部补齐项。按"教学常用频次"排序,数论/矩阵/符号引擎先走;bigint/bigdec 进 v2.5 候选池,移植成本评估不变(20 limb TypedArray 直接搬)。
- **多变量 Groebner 扩维(3+ 变量 / 更高次)**:gb 视图熔断语义已定(基 8 槽/16 项/pair 32,ac_pro.h:501-544),扩容直接推高 union 峰值与整本账,中间量膨胀在禁递归 + 节点预算模型下风险大。维持 2 变量边界。
- **递归符号积分(∫x²eˣ 的通用解法)**:禁递归是红线。v2.3.0 用"固定两层、深度硬编码、无自调用"的展开式实现取其利;更深的能力不做。
- **ODE 大而全(可分离/齐次/Bernoulli/Clairaut)**:Giac desolve 级需要"任意函数的符号积分"做积分因子,远超两侧符号积分能力。只做常系数线性齐次,其余诚实报错。
- **SVD / 最小二乘 / Cholesky**:专业数值线性代数,不在"最常用"梯队;上游 QR 分解未接求解管线是存量缺口,优先级低于特征值通用化。v2.4 后观察。
- **绘图 / 物理单位制(Eigenmath 差异化)**:UI 层职责,内核仓库范围外(features.md「显示层功能边界」)。
- **guardMultivariateNonlinear 死代码**(JS 已导出但无管线调用,features.md:58-59):不是新功能,归入 v2.3.0 JS 组处理,但只有「接入」一个可行方向——函数名被红线测试(test-solvers.js:262)与构建导出契约(build-kernel.js:171)引用,删除即 ReferenceError;原文「接入或移除,组内裁决」的「移除」分支不成立(评审修订)。
- **五次以上根式解**:阿贝尔-鲁菲尼,数学上不存在,guardDegree 永拒是对的,不翻案。

---

## 三、版本规划

> 规则:每版 4-6 个功能,同功能双侧实现算一个;C 侧目标与 JS 侧目标分开列。
> 版本主题:**v2.3.0 = 补齐短板 + 低风险纵深**(把 JS 侧最缺的三块补上,C 侧做两个高复用的新数学面);**v2.4.0 = 符号引擎 + 数值深度**(JS 符号能力跃迁,C 侧补上一般矩阵特征值)。

### v2.3.0(5 个功能)

**功能 1:JS 数论模块(side=JS,补齐 C 侧已有)**

- C 侧目标:无(已具备,ac_numtheory 不动)。
- JS 侧目标:新建 `core_numtheory.js`(自包含 IIFE,参照 core_special.js 纯标量先例):gcd/lcm、素性检测(6k±1 试除 + Miller-Rabin,底数集与 C 侧逐字一致)、质因数分解、nCr/nPr。明确声明安全整数域 ≤2^53−1,溢出返回错误码绝不返回错值。
- value:三库全有(SymPy factorint/isprime、Giac ifactor/is_prime、Eigenmath mod);四版本家族中 JS 是唯一无数论面的实现。
- feasibility:算法 1:1 移植 ac_numtheory,零新大池(因子表静态 Int32Array ~256 B);可读 ~5 KB → 压缩 ~2 KB(HEAD 34 510 B + ~2 KB ≈ 36.5 KB,距 131 072 线余量巨大);水位 ~25 KB ≪ 98 304。触碰:`src/core_numtheory.js`(新)、`test/build-kernel.js`(FILES 数组 + EXPORT_BLOCK 加 numtheory 命名空间;命名空间导出不新增顶层名,EXPORTED_NAMES 可不动)、`test/test-numtheory.js`(新,≥40 例;run-all.js 自动发现新测试文件,零改动纳入;评审修订指出原文「同步 run-all.js」系虚构触碰)。风险:低;唯一注意点是 Miller-Rabin 底数集必须与 C 侧逐字一致,否则跨语言对拍失配。

**功能 2:JS 矩阵升级 12×12(side=JS,补齐 C 侧已有)**

- C 侧目标:无(ac_matrix + A4/A5 不动)。
- JS 侧目标:`core_matrix.js` 的 MAX_DIM 3→12,保留 1..3 阶闭式快路径;新增:部分主元消元的 det/inv/rank、LU 分解(契约对齐 C 侧 A4:P·A=L·U + piv 访问器)、幂法 + Hotelling deflation 的实对称全特征对(对齐 ac_pro_eig_all 的 |λ| 降序 + 访问器语义)、QR 分解。全显式循环,零递归。
- value:矩阵是三库极高频(SymPy 矩阵章全部内容、Giac §2.43/2.46/2.48);JS 仅 ≤3×3 闭式,连 4 阶 det 都算不了。
- feasibility:3 个 12×12 用户缓冲 Float64Array = 3 456 B(现 216 B,+3 240 B);LU/QR/幂法+deflation 的工作区另需 ~2.6 KB(C 侧同款 eig 视图 2592 B 可作上界参照,ac_pro_scratch.h:427——评审修订,原预算漏计工作区),合计 ~6 KB ≪ 98 304;可读 +8-10 KB → 压缩 +3-4 KB。触碰:`core_matrix.js`、`build-kernel.js`(EXPORT_BLOCK 的 matrix 命名空间内加方法)、新 `test-matrix-pro.js`(≥50 例,现有 test-matrix.js 是红线不可改;run-all.js 自动发现,零改动纳入)。风险:中低——幂法收敛边角与 C 侧同款容差;12 阶 inv 稳定性靠部分主元;与 C 侧 A4/A5 逐例对拍是现成的正确性锚。

**功能 3:常系数线性 ODE 齐次通解(side=C+JS,双侧新功能)**

- C 侧目标:新建 `ac_pro_ode.c`——解析 "y''+3y'+2y=0" 型字符串(仿 gb 自带解析器先例,不借 mv 视图)→ 特征多项式 → 走 ac_solvers 1..4 次精确根式(复根现成)→ 渲染通解字符串:单根 e^{λx}、共轭对 e^{αx}(C₁cos βx + C₂sin βx)、实重根 x^k·e^{λx}。非齐次/变系数/次数 >4 返回明确错误码,不硬解。
- JS 侧目标:新建 `core_ode.js`,同构实现,特征根复用 solveQuadratic/Cubic/Quartic,输出格式与 C 侧逐字一致。
- value:dsolve/desolve 是 SymPy 求解指南独立章节、Giac §2.53;双侧均空白,而特征方程管线两侧现成——是复用度最高的全新数学面。
- feasibility:C 侧渲染缓冲由调用方提供,零新视图,**.bss 零增长**;JS 可读 +5-6 KB → 压缩 +2 KB。触碰:C:`ac_pro_ode.c`(新)、`ac_pro.h`、`test/test_pro_ode.c`(新)、`Makefile`(PRO_EXTRA 接线)、`verify.ps1`(第 4 节挂 ODE 行,≥5 例实际给 10+);JS:`core_ode.js`(新)、`build-kernel.js`、`test-ode.js`(新,≥10 例;run-all.js 自动发现)。风险:中——通解字符串是**新的跨语言契约**(tests/ode-solutions.json,收尾组统一建立);重根×共轭的渲染边界要一次定死,超范围诚实报错。

**功能 4:C 符号积分增强——表扩容 + 固定两层分部(side=C)**

- C 侧目标:(a) 积分表 64 → ~128 项(tan/sec/csc/cot 族、双曲族、1/(a²+x²)、1/(x²−a²)、√(a²±x²) 类,均带线性内层替换);(b) 分部积分从"一层"扩到"固定两层"——深度硬编码 2 的展开式实现,无自调用,不违反禁递归红线。
- JS 侧目标:无(JS 符号积分整体在 v2.4.0,直接按增强后的表实现,不返工)。
- value:integrate 三库极高频;两层分部(∫x²eˣ、∫x²sin x)是教科书最常见型,现被 ETABLE 拒(README:613-614)。
- feasibility:表全 static const 落 .rdata 零 .bss;节点预算 256+96+96=448 ≤ 512 仍兜得住(g_sym_nodes 上游 arena 硬顶 512);**栈上节点池 `PRO_INT_POOL_NODES` 须同步 384 → ≥448**(ac_pro_integrate.c:34,现为「> 352 够一层分部用」——评审修订补钉;栈上扩容不触 .bss 账本)。触碰:`ac_pro_integrate.c`(含上述栈池常量)、`ac_pro.h`(AC_PRO_INT_TABLE_SIZE 宏与注释)、`test/test_pro_integrate.c`(31 条扩到 ~50)、README §3A/§6。**风险与显式声明**:这是对 README:408「分部积分只做一层」成文契约的修订——固定两层不是递归,但设计说明必须同步改写;负指数入口(x^-1→EPARSE)维持现状不动,README:611-612 已裁决"收益不足以抵风险",本版不翻案。

**功能 5:C 单变量有理函数工具——GCD / cancel 约分 / apart 部分分式(side=C)**

- C 侧目标:新建 `ac_pro_rational.c`:单变量多项式四则(系数数组 + 综合除法,ratroot 有先例)→ 欧几里得 GCD(单变量不需要 PRS)→ cancel(p/q 约分输出);apart:分母 1..4 次走精确根、5..8 次走 DK,**仅支持互异单根**情形(A_i = p(r_i)/q'(r_i) 闭式),重根/复分母显式拒绝,留 v2.4.0 功能 9 接力。
- JS 侧目标:无(其单变量多项式需求由 v2.4.0 功能 9 顺带覆盖)。
- value:SymPy cancel/apart 教程化简章、积分前预处理常用;Giac normal/propfrac 同位;同时是 v2.4.0 square-free 分解的地基。
- feasibility:新 poly 视图目标 ≤1.5 KB(< 3848 吸收线,.bss 零增长;若超线必须同步 Makefile:132-138 + ac_pro_config.h + verify.ps1 三处账本——设计上避免)。**系数域契约(评审修订补钉)**:GCD/cancel 这类算法只在整系数域良定——全程整系数(int64 + ac_safe_*),单变量欧几里得用伪除 + primitive 归一(与 A1 prem 同纪律),浮点系数输入一律拒绝;apart 的「整系数进、浮点根出」混合语义成文进 ac_pro.h:分母根 1..4 次为精确根式、5..8 次为 DK 数值根,A_i 求解与渲染可走浮点,但结果必须回乘验证(各部分分式 × q_i 重构分子与 p 的数值残差 ≤ 1e-9),验证不过即报错,绝不输出未验证的分解。触碰:`ac_pro_rational.c`(新)、`ac_pro_scratch.h`(新视图 + 全套编译期断言)、`ac_pro.h`、`Makefile`、`verify.ps1`、`test/test_pro_rational.c`(新,≥12 例)。风险:中——apart 的"互异单根"前置判定是主要雷区(整系数域内 GCD 无容差问题,容差只在 DK 数值根端);输出字符串契约对齐 A1 归一化格式。

### v2.4.0(5 个功能)

**功能 6:JS 符号微分引擎——表达式树 + 一般函数求导 + 因式分解输出(side=JS,补齐 ac_symbolic)**

- C 侧目标:无(ac_symbolic + 隐函数求导不动)。
- JS 侧目标:新建 `core_symboltree.js`:parseExpression 的 RPN token 流 → 扁平表达式树(Int32Array 节点池,type/left/right/value 四字段)→ 符号求导(+−×÷^ 与 sin/cos/tan/exp/ln/sqrt/双曲族规则)→ 规范字符串渲染;多项式子树接 core_ratroot 输出**因式列表**(factor 语义,补齐 JS 侧"无因式列表输出接口"缺口);隐函数 dy/dx = −F_x/F_y 对齐 C 侧 A6。
- value:diff/factor 三库极高频;JS 现状只有多项式项表式求导,无一般函数、无表达式树——JS 侧最大单块短板。
- feasibility:节点池 512×4×4 B ≈ 8 KB TypedArray;可读 +12-15 KB → 压缩 +4-5 KB;测试 ≥60 例。触碰:`core_symboltree.js`(新)、`build-kernel.js`、`test-symboltree.js`(新;run-all.js 自动发现)。风险:中高——建树器对隐式乘法/一元负号/右结合幂的覆盖是工作量主体;preCheck 深度 ≤6 与树深的联动要先定义。与 C 方案的最大差异:C 自建解析器建树,JS 复用现成 RPN——这是省工作量的关键,也是要重点测试的地方。

**功能 7:JS 符号积分(side=JS,对齐 C 侧 B2)**

- C 侧目标:无(可选:把 v2.3.0 增强后的积分表常量整理成两侧共享的生成脚本,落 tools/)。
- JS 侧目标:依赖功能 6 的树;积分表与 C 侧逐项对齐(~128 项),一层分部(若 C 侧固定两层已验证稳定则对齐两层);错误码语义复刻 AC_PRO_INT_*。
- value:integrate 极高频;JS 只有 Simpson 数值,符号积分零覆盖。
- feasibility:表常量压缩后 ~1-2 KB;可读 +6-8 KB → 压缩 +2-3 KB;测试 ≥30 例且与 C 侧逐项对拍。触碰:`core_integrate.js`(新,或并入 core_symboltree.js)、`build-kernel.js`、测试新文件(run-all.js 自动发现)。风险:中——依赖功能 6 的交付质量;表项两侧漂移要靠共享 JSON 用例钉死。

**功能 8:C 一般实矩阵特征值——Francis 隐式双移 QR(side=C)**

- C 侧目标:新 schur 视图:Householder 化 Hessenberg → Francis 隐式双移 QR 迭代(bulge chasing,全显式循环零递归)→ 全部特征值(实/复共轭对);实特征向量用反迭代可选交付;非对称输入不再返回 -2。
- JS 侧目标:无(JS 矩阵特征对仍限实对称,与 C 侧 v2.2.0 口径一致;一般矩阵特征值随 v2.5 评估)。
- value:SymPy eigenvals/Giac egvl 面向一般矩阵;非对称返 -2 是成文缺口(ac_pro.h:269-270、README §6.7)——数值线性代数最后一块明显空白。
- feasibility:隐式 QR 不需要 Q 累计阵:H 1152 + wr/wi 192 + work ~256 ≈ 1.6 KB;加反迭代向量 +1 152 ≈ 2.8 KB,均 < 3848 吸收线 → **.bss 零增长设计目标**。触碰:`ac_pro_schur.c`(新,或扩展 ac_pro_eigen.c)、`Makefile`(**PRO_EXTRA 接线,无条件**——新 TU 不接线不参与编译,Makefile:101/108;评审修订,吸收线只决定 .bss 账本动不动,不决定构建接线)、`ac_pro_scratch.h`、`ac_pro.h`;`verify.ps1` 仅超吸收线才动账本。测试 ≥12 例(旋转矩阵、友矩阵、复共轭对、边界 ±1 B 式断言)。风险:中高——Francis 迭代的收敛边角(简并特征值、复对分离)是经典坑;建议特征值先落、特征向量单独评审后并入。

**功能 9:多项式 square-free 分解与重根重数(side=C+JS)**

- C 侧目标:复用 v2.3.0 功能 5 的单变量 GCD:f 与 f' 做 GCD → 经典循环式 SFF 递推(f_{i+1} = f_i / gcd(f_i, f_i'),无递归)→ 逐无重因子求根(1..4 精确 / 5..8 DK)→ (根, 重数) 输出,重根只出一次。**SFF 全程整系数域(评审修订补钉)**:重数由整系数 GCD 精确决定,不存在「浮点判重」问题;浮点只出现在最终根值输出端,与 core_ratroot.js 的「整系数多项式」先例同域。
- JS 侧目标:core_ratroot.js 扩展(或新 core_poly.js):单变量多项式求导 + 欧几里得 GCD 小实现 + 同款 SFF;5 次以上拒绝(与 guardDegree 口径一致)。
- value:SymPy roots 返回 {根: 重数}、Eigenmath nroots 重根以向量返回;C 侧 DK 现在不分重数(README §6.4),重根重复输出是已知不完整语义。
- feasibility:C 复用 poly 视图零增量;JS 可读 +3-4 KB → 压缩 +1 KB;两侧测试各 ≥10,进共享契约 JSON。触碰:C:`ac_pro_sqfree.c`(新);JS:`core_ratroot.js` 扩展;契约:`tests/polynomials.json` 扩展(收尾组统一收口)。风险:中——容差只出现在 DK 数值根与既有步长/残差双判据的衔接处;整系数域内重数由 GCD 精确决定,无「浮点判重」问题(评审修订)。

**功能 10:JS 结式与 Groebner 基(side=JS,对齐 C 侧 A3+GB)**

- C 侧目标:无(ac_pro_resultant + ac_pro_groebner 不动)。
- JS 侧目标:结式先行(Sylvester 矩阵构造 + det,复用 v2.3.0 功能 2 的 12×12 det——依赖关系顺理成章);Groebner 按 C 侧 ac_pro_groebner.c(847 行)移植 M4GB:lex 序、输入 deg≤4、基 8 槽/16 项/pair 32 的熔断语义逐条复刻。
- value:SymPy groebner/resultant、Giac gbasis/resultant 对应能力;补齐后双内核均具备该代数面(JS 系数域收窄的边界差异在 feasibility 如实披露,不称「对等」——评审修订,避免 value 与收尾口径自相矛盾)。
- feasibility:池 ~2-3 KB;可读 +15-18 KB → 压缩 +5-6 KB;测试 ≥12 例对齐 C 侧 GB 用例 + 跨语言对拍。触碰:`core_groebner.js`(新)、`build-kernel.js`、`test-groebner.js`(新;run-all.js 自动发现)。**风险:高,本版最大工作量项**,且有一处必须如实写进文档的收窄:JS 无 int64,系数域从 C 的 int64+ac_safe_* 收窄为 Float64 安全整数(|中间量| 超阈值即 EOVER 熔断),两侧能力边界不完全对等。降级预案:进度超支时只交付结式,Groebner 顺延 v2.5,版本说明如实注明。

---

## 四、v2.3.0 并行分组(3 个 worktree)

分组原则:每个分支独占一个目录/一组热点文件,组间共享文件交集为零。

| 分支组 | 覆盖功能 | 主要触碰文件 | 独占地盘 |
|---|---|---|---|
| **feat/c-symbolic** | 功能 4(C 积分增强)、功能 5(有理函数工具)、功能 3 的 C 侧(ODE) | `C/128KB-ProTEXT/`:`ac_pro_integrate.c`、`ac_pro_rational.c`(新)、`ac_pro_ode.c`(新)、`ac_pro.h`、`ac_pro_scratch.h`、`test/test_pro_*.c`(新增/扩展)、`Makefile`、`verify.ps1`(含头注/banner 修正:本组**末次触碰时**把 :9 的「328 项测试」与 :74 的「v2.1.0」改为当次实跑的真实总数与组件版本 v1.4.0——**不写死 396**,本组挂新行后总数必然大于它)、README §3A/§6;另:开工第 1 个 commit 落 `docs/contracts/ode-format-v23.md` 先合 master(ODE 契约机制,见下) | 整个 `C/128KB-ProTEXT/` 目录:ac_pro.h / Makefile / verify.ps1 三个热点全在本组,别组不许碰(唯一跨目录文件是先合 master 的契约文档,不构成并行期交集) |
| **feat/js-pack** | 功能 1(数论)、功能 2(矩阵 12×12)、功能 3 的 JS 侧(ODE);把 guardMultivariateNonlinear 接入求解管线(唯一可行方向,见 §二) | `JS/128KB/`:`core_numtheory.js`(新)、`core_matrix.js`、`core_ode.js`(新)、`test/build-kernel.js`(FILES + EXPORT_BLOCK)、`test/test-numtheory.js` / `test-matrix-pro.js` / `test-ode.js`(新);run-all.js 以 readdirSync 自动发现新测试文件(run-all.js:20),零改动纳入 | 整个 `JS/128KB/` 目录:build-kernel.js / EXPORT_BLOCK 热点全在本组(run-all.js 本版无需改动) |
| **feat/release-v2.3.0**(收尾,串行) | 版本收口:`docs/features.md` 重写(含修复 L43-48 停在 v2.0.0 的滞后:实测 396 checks/42608 B 与文档的 281 项/41456 B 不符)、`tests/` 新契约文件(ode-solutions.json 等,从两侧内嵌用例提炼)、三份对标调研落库 `docs/refs/benchmark-{sympy,giac,eigenmath}.md`(使 §一 可复核)、CHANGELOG/VERSION 双轨递进、根 README。verify.ps1 的头注/banner 修正**不在本组**,该文件归 feat/c-symbolic 独占,本组只跑不改(评审修订) | `docs/`、`tests/*.json`、`CHANGELOG.md`、各 `VERSION`、根 `README.md` | 文档与共享契约,前两组 merge 后才开工 |

**热点错开说明**:

- 两侧各自的解析器/LUT/分发表物理隔离(C 的 `ac_pro_*.c` 对 JS 的 `core_*.js`),天然无冲突。
- `tests/` 三套共享 JSON(polynomials/exact-forms/edge-cases)**两组开发期一律冻结不改**:新功能的一致性用例先以内嵌测试落位,merge 后由收尾组统一提炼进契约文件,再让两侧补跑——避免两个 worktree 同时改同一 JSON 的 merge 地狱。
- `tests/bench/diff-smoke128.js` 的 50 例对拍本版冻结;ODE/有理函数的跨语言一致性走 tests/ 新契约文件,不动 smoke 管线。
- 功能 3(ODE)双侧分属两组,契约获取机制(评审修订):feat/c-symbolic **开工第 1 个 commit** 把 ODE 字符串格式契约落成 `docs/contracts/ode-format-v23.md` 并**先合入 master**,JS 组开工前从 master 拉取——契约不能藏在未 merge 的 ac_pro_ode.c 文件头里,并行分支下 JS 组读不到;ac_pro_ode.c 头注与该文档互为镜像,契约变更须双组确认后才生效。收尾组建 ode-solutions.json 时两侧格式不一致即视为实现 bug,不允许返工兜底。
- v2.4.0 的分组等 v2.3.0 收口后按同一原则再切(预告:功能 6/7/10 同属 JS 组,功能 8/9-C 属 C 组,功能 9-JS 归 JS 组)。

---

## 五、实施注意

### 门禁红线(数字均为 2026-10-30 实跑/实读)

**C 侧(C/128KB-ProTEXT)**

- .bss 精确账本:内核 42 608 B / runner 42 640 B / 上游对照 39 408 B / 退化 38 672 B——全部是 `==` 级断言(Makefile:132-138 + ac_pro_solvers_ext.c:42-49),改 1 字节就炸;**新功能若推高账本,四处常量(Makefile × 4、ac_pro_config.h、verify.ps1 §2/§6)必须一次改齐并重走 verify.ps1**。
- 112 KiB 门禁 114 688 B(ac_pro_config.h:139),当前余量 72 080 B——余量充足,纪律意义大于物理意义:union 吸收线 3848 B(ac_pro_scratch.h:422)之内零增长,超线就要走账本流程。
- 每个 static 对象 = TU 净 +32 B .bss(16 段 + 16 对齐,ac_pro.h:290-294)——错误码一律走返回值,这是全仓已成文的通道纪律。
- 禁递归(CONTRIBUTING.md:62)、零 malloc(--wrap=malloc/free,Makefile:70)、-Werror 零警告(Makefile:68)、上游 `C/128KB/` 一行不改(verify.ps1 §5 git status+diff 双证)。
- 每功能用例数门禁(verify.ps1 第 4 节):现有 B1≥5 / A4≥5 / A6≥5 / A5≥5 / B2≥6 / SP≥8 / BD≥8 / GB≥10;新功能照此挂新行,建议每功能 ≥10 例并含"刚好通过/刚好拒绝"两侧边界(CONTRIBUTING.md:107-111)。

**JS 侧(JS/128KB)**

- 基线 603 pass / 0 fail(12 文件,2026-10-31 在 34 510 B 新内核上复跑确认);任何内核改动必须重跑 `node test/build-kernel.js`,**--minify 是压缩产物唯一重建入口**(terser 单遍全模块 body+export 合一、mangle.toplevel=true 首选,内置词法剥离器兜底;83 项 eval 回归跑可读体、43 项 ESM 冒烟跑压缩产物、131 072 B 文件线,build-kernel.js:193)。
- 现有 12 个 test-*.js 是红线不可改;新功能测试加新文件即可——run-all.js 用 readdirSync 自动发现 test-*.js(run-all.js:20),零改动纳入,唯一可选项是其头注的文件计数文案(评审修订,原文「同步 run-all.js」系虚构触碰)。
- 零 GC:禁对象/数组字面量、禁热路径字符串拼接、禁递归(含加载期 polyfill,CONTRIBUTING.md:82-86);新池全部加载期一次分配的 TypedArray。
- 水位门禁 98 304 B(core_solvers.js:622),现全池 21.9 KiB(22 416 B,docs/js-128kb.md:135);本版全部 JS 功能合计 +10 KB 池内增量,余量充足,但每个新模块的 allocCount 必须可审计(core_multivar/matrix/special 先例)。
- **本机实测陷阱(2026-10-31 更新)**:terser 已安装(node_modules/terser,2026-10-30 起),--minify 走 terser 单遍管线;但复跑仍会**覆盖**库内交付物(HEAD 34 510 B)——工具链版本或配置漂移都会让字节变化,复跑后核对 `git status`,有差异即 `git checkout -- JS/128KB/src/core_kernel.js` 还原。

**跨语言**

- `node tests/bench/diff-smoke128.js` 50/50 一致(exit 0)是发版底线;家族契约改 `tests/` JSON 必须两侧同步增删(CONTRIBUTING.md:105-106——原文引 45-52 系命令示例段,引用已按评审修订改正)。
- 数值容差 1e-9、精确形式与错误码精确比较(CONTRIBUTING.md:94-96);新增功能的精确形式输出(如 ODE 特征根)纳入同一纪律。

### 验证流水线调用方式

```
# C 全量验收(任意 CWD 均可,脚本自定位;本机无 pwsh,用 Windows PowerShell 5.1)
powershell -NoProfile -ExecutionPolicy Bypass -File C/128KB-ProTEXT/verify.ps1     # 期望 RESULT: 31 passed, 0 failed

# C 快速门禁(可选)
cd C/128KB-ProTEXT && mingw32-make SHELL=cmd.exe OUTDIR=<产物目录> bss-check degraded-check

# JS 全量(工作目录 JS/128KB)
node test/run-all.js                                    # 期望 TOTAL: 603 pass, 0 fail(基线数随新测试文件增长)

# JS 内核重建(压缩交付物唯一入口;交付态复跑后见上方"本机实测陷阱")
node test/build-kernel.js --minify

# 跨语言对拍
node tests/bench/diff-smoke128.js                       # 期望 50/50 一致,exit 0
```

发版前必须过 verify.ps1 完整验收 + JS run-all + diff-smoke128 三关(ProTEXT README §3B);bss-check/degraded-check 只是快速门禁,不能替代。

### 提交信息要求

自 v2.2.0 起 git 历史将被清洗,**新提交信息一次写干净**,没有"先提交后整理"的机会:

- 格式:conventional commits(`feat(scope): 摘要`),scope 用组件名——`c-pro` / `js-kernel` / `docs` / `tests`;标题一句话说清做了什么,不写"update some code"式的空话。
- 正文写清三件事:动机(对标依据一句话即可)、**门禁影响**(.bss 账本是否变化、JS 字节数前后值、是否新增视图)、测试口径(新增多少例、verify/run-all 结果数字)。账本有变化时把 42608 → 新值的推导写进去。
- 期望值修正(包括测试断言)必须在 commit message 里写推导,不得为过测试改期望(CONTRIBUTING.md:107-111)。
- **不夹带规划稿**:本文件(ROADMAP.md)以独立的 `docs: ...` 提交进主干,不混进 feat 分支的功能提交;feat 分支的每个 commit 只含该功能的代码 + 测试 + 该功能自己的文档改动。
- 无 AI 腔:不写"本 PR 旨在""综上所述""赋能"式措辞;中英文混排以仓库现有提交为参照(`git log --oneline` 的既有风格:`feat(...)` / `fix(...)` / `docs(...)` + 中文摘要)。
- 每功能一条独立提交链,不跨功能混提交——v2.3.0 收尾时 CHANGELOG 按 commit 链直接提炼,不回头考古。
