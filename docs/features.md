# ApolloCalc-CAS 功能对照表

四版本家族（JS 与 C99 × 64KB 与 128KB）+ Pro 层的内核功能矩阵。
JS/128KB（v2.4.0）为浏览器侧功能最全实现；C99 128KB-ProTEXT（组件 v1.6.0）
为代数能力最强的参考实现（`.bss` 内核口径 42608 B，上游 `C/128KB/` 冻结）。
版本文档：[js-128kb.md](js-128kb.md) / [c99-128kb.md](c99-128kb.md) /
设计说明。

**基线数字（2026-10-31 实测，非转抄）**：

- C 侧 `verify.ps1` → **RESULT: 35 passed, 0 failed**（含 runner `.bss` 账本
  42608 / 42640 / 上游对照 39408、degraded 38672）；`pro_runner` **574 checks,
  0 failed**（v2.3.0 基线 488 + 功能 8 schur 46 + 功能 9 sqfree 40；每功能
  用例数 B1=7 / A4=6 / A6=9 / A5=6 / B2=10 / SP=12 / BD=12 / GB=12 /
  RAT=7 / ODE=7 / SCHUR=13 / SQFREE=10）。
- JS 侧 `node test/run-all.js` → **TOTAL: 1183 pass, 0 fail**（20/20 文件；
  v2.3.0 基线 862（16 文件）+ 符号微分引擎 116 + 符号积分 75 + sqfree 70 +
  结式/Groebner 60）。
- 压缩内核 `JS/128KB/src/core_kernel.js` 实测 **103 325 B**（预算 131 072 B，
  余量 27 747 B；v2.4.0 四块新功能后由 49 747 B 重建，累计 +53 578 B——
  JS 组风险注记：后续任何 JS 功能落位前应重估水位）。
- 跨语言对拍 `node tests/bench/diff-smoke128.js` → **50/50 用例一致**，exit 0。

## 内核功能

| 功能 | JS 64KB | JS 128KB（v2.4.0） | C99 64KB | C99 128KB |
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
| **矩阵基础运算**（乘/转置/行列式/逆，v2.0.0；**pro 层 12×12，v2.3.0**） | ❌ | ✅ ≤3×3 闭式（core_matrix）+ **12×12 pro 层**（det/inv/rank、LU(P·A=L·U+piv)、QR、幂法+Hotelling 全特征对） | ❌ | ✅ 12×12 完整（ac_matrix） |
| **特殊函数 erf / gamma / lgamma**（v2.0.0） | ❌ | ✅（core_special，A&S 26.2.17 + Lanczos g=7） | ❌ | ✅（ac_special，超集） |
| **数论 gcd/lcm、素性检测、质因数分解、nCr/nPr**（v2.3.0，JS 侧补齐） | ❌ | ✅（core_numtheory，Miller-Rabin 底数集与 C 侧逐字一致） | ✅ 进制 + 分数显示（**无 Miller-Rabin**） | ✅（ac_numtheory，含 Miller-Rabin） |
| **常系数线性齐次 ODE 通解**（v2.3.0，双侧新功能） | ❌ | ✅（core_ode，特征根复用 solveQuadratic/Cubic/Quartic） | ❌ | ❌（Pro 层具备，见下节） |
| **符号微分引擎**（表达式树 + 一般函数求导 + 因式列表 + 隐函数，v2.4.0，JS 补齐 C） | ❌ | ✅（core_symboltree：RPN→扁平树、17 种函数、factor 因式列表、A6 隐函数 dy/dx） | ❌ | ✅（Pro 层 ac_symbolic + A6，见下节） |
| **符号积分**（128 项表 + 固定两层分部，v2.4.0，JS 对齐 C） | ❌ | ✅（core_integrate，错误码复刻 AC_PRO_INT_\*） | ❌ | ✅（Pro 层 ac_pro_integrate） |
| **square-free 分解 (根, 重数)**（v2.4.0，双侧新功能） | ❌ | ✅（core_ratroot.js ApolloSqfree 段，整题 ≥5 → EDEG） | ❌ | ✅（Pro 层 ac_pro_sqfree，契约 [sqfree-format-v24](contracts/sqfree-format-v24.md)） |
| **结式 + 2 变量 Groebner 基**（JS 侧补齐，v2.4.0） | ❌ | ✅（core_groebner：Sylvester ≤12 阶 + M4GB；系数域 Float64 安全整数收窄，不称「对等」） | ❌ | ✅（Pro 层 ac_pro_resultant + ac_pro_groebner） |
| 进制转换（2/8/10/16）+ SI 词头 | ❌ | ✅（core_base.js） | ✅ 进制 + 分数显示（**无 SI**） | ✅（ac_base，含 SI + 工程记数 + 分数显示） |
| preCheck 阈值（深度/函数/变量） | ≤3 / ≤2 / ≤5 | **≤6 / ≤4 / ≤10** | ≤3 / ≤2 / ≤5 | ✅ **≤6 / ≤4 / ≤10** |
| 内存水位门禁 | 48KB（64KB × 75%） | 96KB（128KB × 75%） | ✅ 48KB | ✅ **96KB** |
| 家族共享一致性用例（tests/ 共享 JSON） | ❌（直连用例内嵌） | ✅（test-consistency.js） | ✅（smoke_runner） | ✅（smoke_runner + diff-smoke128.js） |

> 共享契约 JSON 自 v2.3.0 起为四套：`polynomials.json`（数值求根；**v2.4.0 起
> 增补 sqfree (根, 重数) 契约区，见下节**）、`exact-forms.json`（精确形式）、
> `edge-cases.json`（边界与错误码）、`ode-solutions.json`（ODE 通解逐字契约，
> v2.3.0 新建）。

## C99 独有模块（v2.0.0 后 JS 128KB 已补齐基础版的以「基础版」标注）

| 模块 | 功能 | JS 128KB（v2.3.0） | C99 64KB | C99 128KB |
|---|---|---|---|---|
| 大整数 | 20 limb × base 1e9（180 位十进制）：加减乘除 + 阶乘（n≤100）+ 斐波那契（n≤200） | ❌ | ❌ | ✅（ac_bigint） |
| 矩阵 | 最大 12×12：乘法 / 转置 / 秩 / 行列式 / 逆 / 特征值（幂法）/ QR | ✅ pro 层 12×12（v2.3.0：部分主元 det/inv/rank、LU(P·A=L·U+piv)、幂法+Hotelling 实对称全特征对、QR；≤3×3 闭式寄存器层原行为保留） | ❌ | ✅（ac_matrix） |
| 特殊函数 | Gamma / lgamma / Beta / erf / 不完全 Gamma / Bessel J_n | 基础组（erf / gamma / lgamma） | ❌ | ✅（ac_special） |
| 符号求导 + 因式分解 | 扁平数组表达式树符号求导；整系数有理因式分解 | ✅ 基础版（v2.4.0 core_symboltree：表达式树 + 一般函数求导 + factor 因式列表；项表式 d/dx d/dy 并存） | ❌ | ✅（ac_symbolic） |
| 数论 | gcd/lcm、6k±1 试除素数、Miller-Rabin、质因数分解、nCr/nPr | ✅ 基础版（v2.3.0 core_numtheory，算法与底数集镜像 ac_numtheory；声明安全整数域 ≤2^53−1） | ✅（**无 Miller-Rabin**） | ✅（含 Miller-Rabin） |
| 工具 | CRC32 / SHA-256 / Base64 / 定点数 | ❌ | ✅ | ✅（另含单位换算 + 物理常数） |

## Pro 层（C/128KB-ProTEXT，冻结上游之上叠加，其余版本均无）

多变量 GCD / 因式分解 / Sylvester 结式、LU 分解、实特征向量（Hotelling deflation）、
隐函数求导、高次数值求根（Durand-Kerner）、符号积分（**128 项表 + 固定两层分部**，
v2.3.0 前为 64 项 + 一层）、**多变量符号求导 / 多变量多项式积分（精确整除约束）**、
sin/cos LUT 常量表化（`.rdata`，查表零 `.bss`）、稀疏线性方程组（n ≤ 32）、
任意精度定点小数（180 位）、2 变量 Groebner 基（M4GB，v2.2.0）、
**单变量有理函数工具 GCD / cancel 约分 / apart 部分分式（整系数域，apart 仅互异单根，
v2.3.0）**、**常系数线性齐次 ODE 通解（字符串进字符串出，契约
[ode-format-v23](contracts/ode-format-v23.md)，v2.3.0）**、
**一般实矩阵全部特征值（Householder→Hessenberg→Francis 隐式双移 QR，非对称输入
不再返 −2，可选反迭代实特征向量，v2.4.0）**、**单变量多项式 square-free 分解与
重根重数（整系数 SFF，契约 [sqfree-format-v24](contracts/sqfree-format-v24.md)，
v2.4.0）**。
`.bss` 内核口径 **42 608 B** / runner 口径 42640 B / 上游对照 39408 B / 退化 38672 B
（v2.2.0 起 Groebner 落地后零增长，2026-10-31 `bss-check` 复测），pro_runner
**574 checks, 0 failed**（2026-10-31 实测）。
详见设计说明与 [RELEASE-v2.0.0.md](RELEASE-v2.0.0.md)。

## v2.3.0 双侧新功能（2026-10-31）

对标调研见 [refs/benchmark-sympy.md](refs/benchmark-sympy.md) /
[refs/benchmark-giac.md](refs/benchmark-giac.md) /
[refs/benchmark-eigenmath.md](refs/benchmark-eigenmath.md)（SymPy / Giac(Xcas) /
Eigenmath 三库高频功能逐条对标，ROADMAP §一 的可回查底稿）。

| 功能 | 侧 | 落点 | 说明 |
|---|---|---|---|
| 数论模块 | JS | `core_numtheory.js`（新） | gcd/lcm、6k±1 试除 + Miller-Rabin（底数集与 C 侧 `ac_numtheory.c` 逐字一致）、质因数分解、nCr/nPr；安全整数域 ≤2^53−1，溢出报错绝不返回错值。89 项测试。池 288 B |
| 矩阵 12×12 | JS | `core_matrix.js` pro 层 | 部分主元 det/inv/rank、LU(P·A=L·U+piv，契约对齐 C 侧 A4)、幂法+Hotelling deflation 实对称全特征对（\|λ\| 降序，对齐 ac_pro_eig_all）、QR；≤3×3 闭式寄存器层红线行为逐字节保留。82 项测试。pro 池 5944 B |
| ODE 齐次通解 | C+JS | `ac_pro_ode.c` / `core_ode.js`（新） | 常系数线性齐次，阶 1..4；特征方程走两侧既有精确根式管线；单根 e^{λx}、共轭对 cos/sin、实重根 x^k·e^{λx}、重根×共轭四类模板；非齐次/变系数/阶 ≥5 按契约 §6 错误码从严拒绝。字符串格式冻结于 [ode-format-v23](contracts/ode-format-v23.md) |
| 符号积分增强 | C | `ac_pro_integrate.c` | 积分表 64 → 128 项（tan/sec/csc/cot 族、双曲族、1/(a²±x²)、√(a²±x²) 类，均带线性内层替换）；分部积分「固定两层」（深度硬编码 2 的展开式实现，无自调用，不违反禁递归红线）；测试 31 → 58 项 |
| 有理函数工具 | C | `ac_pro_rational.c`（新） | 单变量多项式四则 + 欧几里得 GCD（整系数域，伪除 + primitive 归一）+ cancel 约分 + apart 部分分式（分母根 1..4 次精确根式 / 5..8 次 DK 数值根，仅互异单根；结果回乘验证残差 ≤ 1e-9 才输出）。32 项测试 |
| guardMultivariateNonlinear 接入 | JS | `core_solvers.js` | 死代码唯一可行方向：≥2 变量且含 `^`/函数调用的非线性多元组在 solveQuadratic/Cubic/Quartic 入口 `_solveError = 2` 拒绝（函数名被红线测试与构建导出契约引用，不可删除） |

**压缩内核**：四块 JS 新功能并入后经 `node test/build-kernel.js --minify` 重建，
`core_kernel.js` **34 510 → 49 747 B**（预算 131 072 B）；全项目 JS 静态池
22 416 → **30 112 B**（≈ 29.4 KiB，按各模块池账本累加：矩阵 pro +5944、
数论 +288、ODE +1464），水位门禁 98 304 B 内。

**跨语言 ODE 契约**：[tests/ode-solutions.json](../tests/ode-solutions.json)
（v2.3.0 新建）——成功用例期望串逐字比对、错误用例按契约 §6 数值精确比较。
**70 个双侧实跑逐字节一致的用例冻结**（契约 §5.4 全部 11 行、§6 错误码全序、
§7 拒绝情形、§9-4 边界值，另含 √2/√3/∛2 无理根 fmt 12 位定点路径的
§9-2 双侧核对项）；**3 个双侧输出不一致的输入不冻结进 cases**，逐条记录于
同文件 `knownDeviations`（1 条 JS 侧实现缺陷 + 2 条契约文本歧义各执一端，
见下「已知接口差异」），处置遵循 ROADMAP.md:178（不一致即实现 bug，
不许返工兜底）。

## v2.4.0 双侧新功能（2026-10-31）

主题「符号引擎 + 数值深度」。开发方式：双 Git worktree 并行（`feat/c-eigen` /
`feat/js-symbol`），square-free 输出格式契约
（[docs/contracts/sqfree-format-v24.md](contracts/sqfree-format-v24.md)）由 C 组
开工第 1 个 commit 先行冻结、JS 组按其实现（机制同 v2.3.0 ODE 契约）；
收尾组 feat/release-v2.4.0 串行收口。

| 功能 | 侧 | 落点 | 说明 |
|---|---|---|---|
| 符号微分引擎 | JS | `core_symboltree.js`（新） | parseExpression 的 RPN token 流 → 扁平表达式树（512×4 节点池）→ 符号求导：四则/幂三支路（常指数/常底/一般 u^v）+ 17 种单参函数；多项式子树接 ratRootDescent 输出因式列表（factor 语义，镜像 ac_sym_factor_poly）；隐函数 dy/dx = −F_x/F_y 对齐 C 侧 A6（错误码逐值复刻）。树侧两道上限（节点 ≤100 / 树深 ≤48）与 preCheck 括号深度的联动定义成文。116 项测试。池 35 232 B |
| 符号积分 | JS | `core_integrate.js`（新） | 积分表 128 项与 C 侧逐项对齐（anti 模板串 + 偏移表加载期解析，加载期断言恰 128 项）+ 固定两层分部（parts_left 2→1→0 关死，无自调用）；错误码复刻 AC_PRO_INT_\*。与 C 的文法边界如实成文（已知超集偏差 3 条，见下「如实口径」，对拍用例避开）。75 项测试。池 93 676 B |
| 一般实矩阵特征值 | C | `ac_pro_schur.c`（新） | Householder→Hessenberg→Francis 隐式双移 QR（bulge chasing，全显式循环）+ 2×2 块闭式解；幂 2 精确缩放（纯指数加减零舍入）防 H² 溢出；单特征值 50 轮预算 + EISPACK 同款异常位移，不收敛返 −2 绝不输出垃圾；可选反迭代实特征向量（残差 ≤ 1e-9·(1+‖A‖)，6 组尝试全失败返 −3；复特征值不产出向量）。ac_pro.h:283-284「非对称返 −2」成文缺口关闭。schur 视图 2744 B ≤ 3848 吸收线，`.bss` 零增长。13 例 / 46 checks |
| square-free 分解与重根重数 | C+JS | `ac_pro_sqfree.c`（新）/ `core_ratroot.js` ApolloSqfree 段 | 整系数循环式 SFF（f 与 f′ 逐层 GCD），重数由分解层指标精确决定、浮点永不参与重数合并；输出 (根, 重数) 列表，C ≤8 次（1..4 精确 @e / 5..8 DK @d）、JS 整题 ≥5 → EDEG（契约 §5.1 成文分叉）。契约 [sqfree-format-v24](contracts/sqfree-format-v24.md)。C 10 例（SQFREE）/ JS 70 项。JS 池 2 240 B |
| 结式 + 2 变量 Groebner 基 | JS | `core_groebner.js`（新） | 结式：Sylvester 矩阵（阶 ≤12）复用 v2.3.0 矩阵 pro 层 proDet，数值路径与 C 同语义；Groebner：M4GB 逐对归约 1:1 移植（lex 序、输入 deg ≤4、基 8 槽/16 项/pair 32 熔断、Buchberger 第一判据、interreduce 不动点），错误码 = AC_PRO_GB_\* 全序。**系数域收窄**：JS 无 int64，Float64 安全整数 \|v\| > 2^53−1 即 EOVER（−6）熔断——两侧能力边界不对等，不称「对等」。60 项测试。池 6 648 B |

**如实口径（能力边界与副作用，收尾组如实转写 JS 组交付说明）**：

- **压缩水位**：压缩内核 103 325 B，距 128 KB 文件线余量 27 747 B；v2.4.0
  JS 组累计 +53 578 B（自 49 747 B）。全项目 JS 静态池 30 112 → **167 908 B**
  （符号微分 35 232 + 符号积分 93 676 + 结式/Groebner 6 648 + sqfree 2 240，
  实测各模块加载期 TypedArray byteLength 之和）——已超 ROADMAP §五 口径的
  98 304 B 水位门禁；门禁函数 `checkMemoryWatermark`（core_solvers.js:622-644）
  只累计基础内核池、不含模块池，故测试仍全绿。后续任何 JS 功能落位前应重估
  水位。
- **Groebner 系数域收窄（不称「对等」）**：JS 无 int64，系数域为 Float64
  安全整数，\|中间量\| > 2^53−1 即 EOVER 熔断（实现文件头与测试文件头成文）；
  经典用例幅值远低于熔断线，但输入幅值 >1e6 量级的中间量两侧能力不对等。
- **结式为数值路径**（C 同款）：det 条件数敏感，主元阈值 1e-12（proDet 契约）；
  **结式为 0 是公共根判据而非错误**——与 C 逐值同语义。词法无总次数上限
  （与 C parse_into 同域），每变量指数 ≤15。
- **sqfree 求解层副作用**：逐层求根复用求解层 `_coeffs`/`_root*`（C 侧
  ac_sym_factor_poly CLOBBERS g_coeffs 同款副作用，两头已成文）；调用方若
  持有求解层会话语据须重算。
- **符号积分已知超集偏差 3 条**（JS 复用内核解析器，能力超集）：空白分隔数字
  按内核隐式乘法接受（C EPARSE）、e^(完整表达式) 按标准优先级读（C 的 e^
  只吃一个 pow 层）、x^2^3 右结合读成 (x^2)^3（C 文法止于单指数）。

**跨语言 square-free 契约**：[tests/polynomials.json](../tests/polynomials.json)
sqfree 区（v2.4.0 收口）——契约 §10-1 两层结构：**双侧区 64 例**（整题 ≤4 次；
成功用例期望串逐字比对、错误用例按契约 §7 数值精确比较）+ **C 侧区 6 例**
（整题 5..8 次共 5 例：C 断言 @e/@d 期望串、JS 断言 −2；EOVER 缓冲语义
1 例 cOnly，JS 消费方跳过）。**70 例 2026-10-31 双侧实跑核对零分歧**
（无 knownDeviations）。用例以两侧内嵌套件（C test_pro_sqfree.c
SQFREE-1..10 / JS test-sqfree.js）为准提炼，收尾组未发明新用例；契约 §10-4
的 q=1000/1001 有理重建 q 边界两侧内嵌套件均未覆盖，如实注明未纳入。

## 未落地项

- 结果历史环形缓存：四版本均未规划落地时间（UI 层职责边界未定）。
- 五次及以上多项式求根：阿贝尔-鲁菲尼定理，无根式解，`guardDegree` 永久拒绝。
- ODE 非齐次 / 变系数 / 阶 ≥5 / Euler 方程：双侧均按契约 §6 错误码诚实拒绝，
  不硬解（ROADMAP v2.3.0 落选声明）。
- JS 一般实矩阵特征值（Francis QR）：v2.4.0 仅 C 侧交付（ROADMAP 功能 8
  JS 侧目标为「无」）；JS 侧矩阵特征对仍限实对称（幂法 + Hotelling），
  一般矩阵随 v2.5 评估。
- JS 大整数 / 高精度：ROADMAP 落选声明（v2.5 候选池，20 limb TypedArray
  移植成本评估不变）。
- 多变量 Groebner 扩维（3+ 变量 / 更高次）与通用化简 simplify / trigsimp：
  维持 ROADMAP 落选声明，不做。

## 已知接口差异

- `guardMultivariateNonlinear`：**v2.3.0 起已接入** solveQuadratic / solveCubic /
  solveQuartic 入口（≥2 变量且含非线性运算 → `_solveError = 2`）；
  C99 两版仍不提供该函数。此前的「死代码」状态（v2.0.0 起）消除。
- **ODE 跨语言已知偏差（v2.3.0 收尾对拍发现，3 条，详见
  [tests/ode-solutions.json](../tests/ode-solutions.json) `knownDeviations`）**：
  1. **JS 小正值实根渲染缺陷**：实特征根 0 < |λ| < 0.5 且四舍五入到 0 时
     （如 1/3、1/999），`core_ode.js` 的 `_isZeroInt` 返回 boolean 而
     实根渲染门以 `=== 0` 比较，exp 因子被误省略、根被当作 λ=0 渲染
     （`999y''+998y'-y=0` C 侧正确输出 `exp(1/999*x)` 项，JS 侧输出裸 `C₁`）；
     负小根（−1/2）与共轭对分支不受影响，故两侧内嵌测试均未暴露。
  2. **`x^2*y=0` 错误码分歧**：C 侧 −3（EVARCOEF，容忍 `x^<digits>` 形态，
     依契约 §6 表 EVARCOEF 行示例）/ JS 侧 −1（EPARSE，依契约 §3.3-3
     「^ 一律非法」+ §6 判定全序）——契约 §6 表示例与 §3.3-3/§6 全序自相矛盾，
     两侧各执一端，须双组确认后统一。
  3. **`y''x=0` 错误码分歧**：C 侧 −1（项尾硬校验，依契约 §3.3-1
     「导数记号后只允许空白/+/−/=/串尾」）/ JS 侧 −3（依 §3.2
     「x 任意位置出现置变系数标志」）——契约 §3.2 与 §3.3-1 歧义，
     两侧各执一端，须双组确认后统一。
- **square-free 双侧分叉（v2.4.0，契约成文例外，非缺陷）**：整题次数 5..8 的
  输入 JS 侧 `EDEG`（−2）、C 侧按因子次数路由走 @e/@d 求根——
  [tests/polynomials.json](../tests/polynomials.json) sqfree 区按契约 §10-1
  分层收录（C 侧区 5 例 JS 断言 −2）。70 例双侧实跑核对零分歧，无实现 bug。
- **v2.4.0 能力边界与副作用（两侧成文，非缺陷）**：Groebner 系数域收窄
  （JS 无 int64，不称「对等」）、结式数值路径 0 = 公共根判据、结式词法边界
  （无总次数上限、每变量指数 ≤15）、sqfree 求解层 clobber 副作用、符号积分
  超集偏差 3 条——详见上节「v2.4.0 双侧新功能 · 如实口径」。

## 显示层功能边界

科学计数法显示、工程记数法（SI 词头）、分数显示、精确形式渲染（`√`、上标、
混合形式排版）等**属于 UI 层职责，不在本内核仓库范围**。内核侧只提供数据契约：

- 精确形式五分量 getter：`getRootExactRat / RadNum / Rad / Den / Exp / Valid`（每根一组）；
- 近似值：`getRootRe / getRootIm`（双精度）；
- 进制与 SI 的数据出口：`decToXxx → baseToString`、`siNormalize → getSiMantissa / getSiPrefixExp / siToString`；
- 泰勒系数：`getTaylorCoeff(k) / getTaylorOrder`（UI 层自行排版为多项式文本）。

显示串的格式约定（如 `tests/exact-forms.json` 的 "√(2)"、"1/2" 写法；
`tests/ode-solutions.json` 的 `C₁*exp(-1*x)` 通解写法；以及
`tests/polynomials.json` sqfree 区的 `[3@e1, 2@e1]` (根, 重数) 列表写法）
仅用于一致性测试比对，不构成内核的显示承诺。
