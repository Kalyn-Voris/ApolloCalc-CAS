# 二元多项式方程组 GB 消元数值全解契约 v2.5 —— 字符串进、解列表字符串出

> **状态**:冻结(v2.5.0 F2 开工前置,先于两侧实现;feat/solvers 开工第 1 个 commit)。
> **归属**:feat/solvers 产出;实现侧 C `ac_pro_sysolve.c`(新)、JS `core_groebner.js`
> 扩段(ApolloGroebner 增 sysolve 子段,捆绑 Durand-Kerner 移植)与本文件**互为镜像**;
> 任何变更须先改本文件再改码。
> **成文依据**:ROADMAP-v2.5-v2.6.md §F2(输入/管线/回验/错误码)、§6.3(契约先行)、
> docs/contracts/sqfree-format-v24.md(渲染 fmt 与字符串契约范式)、
> CONTRIBUTING.md:96(数值容差 1e-9、错误码精确比较)。
> 本契约是 **C/JS 双侧契约**:同一输入必须产出同一错误码、同一解集合
> (按 §6 排序后逐解 |Δ| ≤ 1e-9;CONTRIBUTING.md:96)。

---

## 1. 动机与用途

F2 = SymPy `solve_poly_system` 数值域同构:f、g 两多项式串 → 现有 Groebner 基
(lex x>y,M4GB)→ 经 GB 访问器把 y-单变元消元多项式 h 与混合元素**拷入栈缓冲**
(copy-out 后 gb 视图即不再持有,遵守 ac_pro.h 访问器互斥纪律)→ h 经 square-free
预处理去重根 → h 求根(1..4 精确根式 / 5..8 Durand-Kerner)→ 逐 y 根代入混合元
得 x 一元多项式再求根 → 每个 (x,y) 候选代回 f,g 残差 ≤ 1e-9 回验才输出。
GB={1} → 成功语义 0 解(对齐 SymPy EmptySet);无 y-单变元元 → 正维数诚实错误码。

字符串进、字符串出,没有共享数据结构,只有共享文本与共享错误码;先于两侧任何
代码把以下边界一次定死:输入文法(§3)、消元语义(§4)、解集合语义(§5)、
输出渲染(§6)、错误码全序(§7)。

## 2. 适用范围与总原则

- **数学范围**:两生成元 f、g 的理想 <f,g> ⊂ Q[x,y],输入单项式序 = lex(x > y),
  输入多项式**总次数 ≤ 4**(GB 视图既有边界 AC_PRO_GB_IN_DEG)。消元产物域:
  GB 视图内一切多项式总次数 ≤ 8 ⟹ deg(h) ∈ [1,8]。
- **零存储原则(C)**:错误码走返回值;h/混合元素/根候选全部在栈缓冲
  (h ≤ 9 个 int64、混合元素 ≤ 7 个 × 16 项、根候选 ≤ 16×2 double);
  复用 gb 视图(GB 阶段)与 nr 视图(DK 阶段,不同期);零 static 对象、
  零新视图,.bss 零增长。JS 侧池按 ROADMAP §F2 预算 +1–2 KB(含 DK 池 264 B)。
- **全程显式循环**:零递归、零 malloc(C)/零热路径堆分配(JS)。
- **从严拒绝**:文法与域之外一律拒绝,绝不硬解;回验不过绝不输出。
- **确定性优先**:同一输入两侧逐位同路径;排序与去重规则机械执行(§5.3/§5.4)。

## 3. 输入文法

两个输入串 f、g 均为 **gb 解析器语法**(ac_pro_groebner.c / core_groebner.js 同款,
本契约不改写其文法,只冻结面向用户的判定):

- 项 = `[符号] [系数] [*] x[^n] [*] y[^m]`;变量顺序可换、可缺省;系数缺省 1、
  指数缺省 1;空白(空格/tab)任意;项分隔 `+`/`-`,项首可再带符号。
- 单变量指数 ∈ [0,8];**项总次数(指数和)≤ 4**,超界 → `EDEG`(词法层当场)。
- 同类项允许重复出现(gb 解析器负责合并),与 sqfree 文法的「同次项重复 EPARSE」
  **刻意不同**(本契约沿用 gb 行为,不引入第二套解析器)。
- `=`、括号、除法、第三变量、空串、NULL(C)→ `EARG`(空串)或 `EDEG`/`EOVER`
  (按 gb 词法判定,见 §7 映射)。
- **系数域(成文的能力边界,不称对等)**:C 侧系数中间量走 int64
  (ac_safe_* 溢出即 `EOVER`);JS 侧同域为 2^53−1(> 线即 `EOVER`)。
  共享契约用例幅值全部落在公共安全域(int64 ∩ 2^53 = 2^53 域)内;
  系数更大的输入是 C 独有能力,不设分叉用例区(features.md:143-145 先例)。

## 4. 消元语义(管线逐段冻结)

1. **GB 阶段**:调既有 Groebner(lex x>y)。GB 内部失败码按 §7 映射透传
   (EARG/ETERM/ESLOT/EPAIR/EOVER/EBUF 原值;GB EINTERNAL → EINTERNAL)。
2. **{1} 判定**:GB 塌缩为非零常数(等价 {1})→ **成功,输出 `[]`**(0 解,
   对齐 SymPy EmptySet;用例:f=x-1, g=x-2)。
3. **分类(copy-out)**:经 GB 访问器(count/terms/coef/exps)把每个活元素
   拷入栈缓冲,分类:
   - **y-单变元元素**(全部项 ex=0):至多一个(reduced GB 的 antichain 性质);
     记为 h。**没有 h → 正维数 → `EZDIM`**(用例:f=x+y-1, g=2x+2y-2)。
   - **混合元素**(存在 ex>0 的项;含纯 x 元素):全部拷入栈缓冲。
4. **h 预处理(square-free)**:g₀ = gcd(h, h′)(整系数欧几里得 + 逐步 content
   约减,prem 膨胀溢出 → `EOVER`);h_sf = h / g₀(精确整除,余式非零 →
   `EINTERNAL`)。切相系统的 h=(y−1)² 由此归一为 y−1,DK 对重根的扰动弱点
   在符号层消灭。
5. **h 求根路由**(deg 以 h_sf 计):
   - deg 1:x = −c₀/c₁ 直接式;
   - deg 2..4:上游求解层精确根式(ac_solve_quadratic/cubic/quartic;
     JS solveQuadratic/Cubic/Quartic),复根全收;
   - deg 5..8:Durand-Kerner 数值路径(C = ac_pro_numroots / nr 视图;
     JS = 移植同算法,池镜像 nr 264 B)。收敛判据、Aberth 初值、扰动与
     1e-10 虚部归零规则**逐字镜像 ac_pro_numroots**(不收敛 → `EINTERNAL`)。
6. **解耦 GB 交叉积**:若全部混合元素都是纯 x 元素(所有项 ey=0),x 根
   **只求一次**(按次数同 §5 路由:≤4 精确 / 5..8 DK),与全部 y 根做
   笛卡尔积(用例:f=x^2-1, g=y-2)。
7. **耦合代回路径**:对每个 y 根 r(复数),把每个混合元素 m(x,y) 代入
   y=r 得 p_r(x) = Σᵢ(Σⱼ cᵢⱼ rʲ)xⁱ(复系数):
   - **代回恒零判定**:p_r 的全部系数满足 |c| ≤ 1e-9·max(1, max|c|) → 该元素
     在 r 处不约束 x。若**全部**混合元素恒零 → **`EZDIM`**(直线 {y=r} ⊆ V(I),
     维数 ≥ 1,无论 I 是否根式);用例:f=y^2, g=xy。
   - 否则对每个非恒零 p_r:剥离首项(自最高次向下,|c| ≤ 1e-9·该多项式
     max|c| 即剥);剥至 0 次(非零常数)→ 该元素跳过(不约束 x)。
   - p_r 求根:**一律走复系数 Durand-Kerner**(栈上工作区,C 无视图占用;
     JS 同)。迭代格式与 §4.5 的 DK 同款(复系数推广: Aberth 半径按复系数
     模长计)。耦合路径不做「精确根式」路由——r 本身已是数值,根式是伪精度。
8. **残差回验**:候选 (x,y) 代回 **f 与 g 原多项式**(两侧各自把 f,g 解析为
   9×9 int64/安全整数稠密网格,不依赖 GB 中间态):
   resid(P) = |P(x,y)| / max(1, Σ|cᵢⱼ|·|x|ⁱ·|y|ʲ) ≤ 1e-9 必须对 f、g 同时成立;
   任一候选不过 → **`EVERIFY`**(整个查询失败,不输出部分解;
   apart 回乘验证同款纪律)。

## 5. 解集合语义

### 5.1 候选与容量

- 候选池 = 通过回验的 (x,y) 对,容量 16(Bézout 界 deg f·deg g ≤ 16;
  栈缓冲 16×2 double 先例)。**通过回验且去重后超过 16 → `ESOLT`**
  (防御性熔断:正常输入不可能触发)。
- y 根候选重复(h_sf 的 DK 根聚类)由解级去重吸收(§5.4)。

### 5.2 排序(冻结)

解列表按 (y 的 Re,**1e-9 并列** y 的 Im,x 的 Re,**1e-9 并列** x 的 Im)
**降序**稳定排序;完全并列(四分量全部 1e-9 内)保持插入序。
1e-9 并列按四分量逐一比较,比较顺序即上述次序。

### 5.3 去重(冻结)

两解 (x₁,y₁)、(x₂,y₂) 合并 ⟺ |Δx_Re| ≤ 1e-9 且 |Δx_Im| ≤ 1e-9 且
|Δy_Re| ≤ 1e-9 且 |Δy_Im| ≤ 1e-9;保留先插入者(插入序确定 ⟹ 两侧一致)。

### 5.4 输出串(冻结)

- 解列表 = `"(" X ", " Y ")"` 以 `", "` 连接,整体包 `"["` `"]"`;0 解 = `"[]"`。
- 坐标渲染 = **sqfree 契约 §5.4/§6.1 同一 fmt 三级**:整判(1e-9,平局取
  |p| 小者)→ 有理重建(q ≤ 1000)→ 定点 12 位;实根写 fmt(re),复根写
  `"[re](+/-)|im|i"`(re≈0 省略、|im|≈1 只写 i,同 sqfree §6.1)。
- 渲染失败(缓冲不足)→ `EOVER`(C)/`EINTERNAL`(JS,不出部分串)。

## 6. 输出缓冲(C)/结果通道(JS)

- C:`int ac_pro_sysolve(const char *fsrc, const char *gsrc, char *out, int out_len);`
  契约冻结 **out_len ≥ 1024**(16 解 × 最坏 ~60 B);实际输出超 out_len →
  `EOVER` 且不写部分内容。失败时 out 内容未定义。
- JS:`ApolloGroebner.sysolve(fsrc, gsrc)` 返回错误码(0 = 成功);
  `ApolloGroebner.getSysolveString()` 取输出串。加载期一次分配的结果槽复用,
  零热路径堆分配。

## 7. 错误码全序(冻结;负值即失败)

| 码 | 名 | 语义 |
|---|---|---|
| 0 | OK | 成功(out 已写入,含 `"[]"`) |
| −1 | EARG | 空指针/空串/gb 词法结构错误 |
| −2 | EDEG | 输入总次数 > 4 或单变量指数 > 8(gb 词法) |
| −3 | ETERM | GB 单元素项数 > 16(GB 透传) |
| −4 | ESLOT | GB 基槽 > 8(GB 透传) |
| −5 | EPAIR | GB critical pair 队列溢出(GB 透传) |
| −6 | EOVER | 中间整数溢出(C int64 / JS 2^53−1)或 C 输出渲染溢出;GB EOVER 透传 |
| −7 | EBUF | C 输出缓冲不足(仅 C;JS 无此码) |
| −8 | EZDIM | 正维数:无 y-单变元元,或某 y 根处全部混合元素代回恒零 |
| −9 | ESOLT | 通过回验的去重解数 > 16(防御熔断,正常域不可达) |
| −10 | EVERIFY | 任一候选残差 > 1e-9(f 或 g) |
| −11 | EINTERNAL | GB 内部失败 / h 除法余式非零 / DK 不收敛 / fmt 越域(绝不硬凑) |

- 与既有 AC_PRO_GB_* 的关系:−1..−7 与 AC_PRO_GB_EARG..EBUF **同值同义**
  (本契约直接透传 GB 阶段失败);GB_EINTERNAL(−8) 映射为 EINTERNAL(−11)。
- ETERM/ESLOT/EPAIR/ESOLT/EVERIFY 为防御码:deg ≤ 4 输入域内无可构造的
  自然触发(ESOLT 由 Bézout 界封顶;ETERM/ESLOT/EPAIR 由 GB 视图容量封顶)。
  契约用例覆盖 OK/"[]"/EARG/EDEG/EOVER/EBUF/EZDIM;防御码语义成文、
  测试以代码审读 + 既有 GB 熔断测试覆盖。

## 8. 求解层 clobber 副作用(成文)

C 侧复用上游求解层全局:g_coeffs(1..4 精确路径)、g_root_re/im、g_root_count、
g_solve_error、g_root_exact_*(DK 路径),以及 Pro union 的 gb 视图(GB 阶段)与
nr 视图(DK 阶段)。调用 ac_pro_sysolve 后这些全局内容未定义(与 sqfree 先例
同款成文);调用方不得假设其保留。

## 9. 契约用例与差分

- `tests/systems.json`:全部为 **dual 区**(本功能全域零分叉;系数域公共部分
  见 §3)。成功用例解列表按 §5.2 排序逐解数值 ≤ 1e-9,错误码精确比对。
- 差分驱动 `tests/bench/diff-sysolve.js`:机制同 diff-smoke128.js(C 探针 CSV
  ↔ JS 侧 CSV 逐行比对),既有 50 例管线一行不动,exit 0 为验收线。
- 关键用例类:经典二次系 4 解、2 解、线性系交叉验证、GB={1} 0 解、正维数、
  解耦交叉积、切相重根、EDEG 可达失败(x³+y³−1; xy−1)、h 次数 4/5 路由边界、
  复数解路径(x²+y²−1; x²−y 的复对)、16 解 Bézout 封顶(x⁴−1; y⁴−1)、
  「刚好通过/刚好拒绝」双侧边界。
