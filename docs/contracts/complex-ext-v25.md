# 复变函数扩展契约 v2.5 —— 主值分支公式表与双端误差带（15 API）

> **状态**：冻结（v2.5.0 功能 F1 开工前置，先于两侧实现）。**归属**：feat/math-ext 产出
> （机制同 sqfree-format-v24 先例：开工第 1 个 commit 落契约，ROADMAP-v2.5-v2.6.md §6.3-3）。
> 实现侧 C `ac_pro_complex_ext.c`（新，PRO_EXTRA 无条件接线）与 JS `core_complex.js`
> 就地扩展 + EXPORT_BLOCK `complex` 命名空间，与本文件**互为镜像**；任何变更须先改本
> 文件再改码。
> **成文依据**：ROADMAP-v2.5-v2.6.md §F1、§2.2（表达式层复数求值明确不在本功能）、
> §6.3 开工 checklist；C99 Annex G（N1256 §G.5/G.6/G.7）主值分支语义；
> ac_complex.h / core_complex.js 既有六寄存器协议。

---

## 1. 动机与用途

v2.5.0 F1 在既有复数四则/开方/整数幂/指数/对数/实幂（csqrt/cExp/cLn/cPowReal 等）
之上补齐复域超越函数族：复三角、复双曲、六 个反函数、全复幂、主值 k 次根、复 Gamma。
这些函数的**主值分支、割线 ±0 侧约定、奇点分类**若两侧各自理解，同一输入会产出
不同分支值，共享对拍即失配且无法定位。因此先于两侧任何代码，一次定死：

1. **公式表**（§4）——每个函数的逐表达式定义，两侧逐字同构实现（表达式书写顺序
   冻结，IEEE binary64 下才能稳定进误差带，G2 同款纪律）；
2. **奇点分类**（§5）——哪些输入返回域错误 EDOM，判据逐条成文；
3. **误差带**（§8）——每函数的对比容差与比较公式，±0 不比符号；
4. **溯源**（§9）——契约 JSON 期望值由 mpmath 40dps 独立生成，生成器入库可复算。

用途：两侧实现者按本文件写码与写测试；差分驱动 `tests/bench/diff-complex.js`
与验收测试（C `test_pro_complex_ext.c`、JS `test-complex-ext.js`）以本文件为唯一
语义出处；后续版本扩复变面时本文件是唯一变更入口。

## 2. 适用范围与总原则

- **数学范围**：双精度浮点复数域上的主值分支；约 15 个寄存器式 API。表达式层的
  i 字面量解析、复值 RPN 求值栈**不在本功能**（§2.2 落选声明）。
- **零分配原则**：C 侧零 static 可变对象（Lanczos 系数 static const 落 .rodata）、
  零 malloc、零 VLA、零递归（cgamma 反射式经 `cx_gamma_direct` 直算因子，无自
  调用，ac_special.c 同款结构）、错误码一律走返回值（一个 static = TU 净 +32 B
  .bss，ac_pro.h eig 节实测论证）；JS 侧纯标量局部变量 + Math.*，加载期与热路径
  分配均为 0，无对象/数组字面量、无递归、无热路径字符串拼接（CONTRIBUTING.md:82-86）。
- **从严拒绝**：奇点/溢出/参数域之外一律拒绝并返回 §7 错误码，绝不硬凑、绝不输出
  未定义分支值。
- **双端零分叉**：本功能 15 API 全部双侧同构，无 cOnly/JS-only 分叉区。公式表
  （§4）即分叉仲裁依据：两侧结果差超 §8 误差带即实现 bug，不许兜底。

## 3. 寄存器协议与调用序

两侧同构（C 符号 ↔ JS 全局）：

| 语义 | C（ac_complex.h 既有） | JS（core_complex.js 既有） |
|---|---|---|
| 输入 A | `g_c_a_re` / `g_c_a_im` | `_aRe` / `_aIm` |
| 输入 B | `g_c_b_re` / `g_c_b_im` | `_bRe` / `_bIm` |
| 结果 TMP | `g_c_tmp_re` / `g_c_tmp_im` | `_tmpRe` / `_tmpIm` |

- 调用序（同既有 cExp/cLn 先例）：写 A（双操作数再写 B）→ 调函数 → 读 TMP。
- **A 覆写约定**：仅 `cpow` 覆写 A（内部复用 A 暂存 `w·ln z`，沿袭 cPowReal 同款
  约定，头注成文；调用后如需再用 A 必须重载）。其余 14 API 不改写 A/B。
- **JS 导出面**：`ApolloComplexExt` 命名空间（core_complex.js 末尾定义，加载期
  对象字面量一次分配），经 build-kernel.js EXPORT_BLOCK 以 `complex` 键导出；
  方法名 = §4 行名（`csin`/`ccos`/.../`cgamma`），`cnthroot(k)`、`cpow()` 同签名。
  EXPORTED_NAMES 名册不动（v2.3.0 功能 2 先例：命名空间整体导出不进名册）。
- 函数签名（双侧一致）：除 `cnthroot(k)`（k 为整数）与 `cpow()`（读 A、B）外，
  全部单操作数读 A 写 TMP；返回值 int 错误码（§7）。

## 4. 主值分支公式表（两侧逐字同构实现）

记输入 A = (x, y)，双操作数 B = (u, v)，输出 TMP = (R, I)。常量：
`PI = 3.141592653589793`（C `M_PI` / JS `Math.PI`，同一 float64）、
`HALF_PI = PI/2`（精确折半）、`LN_SQRT_2PI = 0.9189385332046727`。
辅助式（表达式顺序冻结）：

- **H1 cln**(p, q) = (0.5·log(p²+q²), atan2(q, p))——仅 (p,q) ≠ (0,0) 可调；
  各 API 在调用前已排除 (0,0)（§5）。
- **H2 csqrt**(p, q) → (sr, si)，符号零敏感：
  1. p == 0 && q == 0 → (0, q)（保 ±0，C99 G.5：csqrt(±0±i0)=±0 侧）；
  2. p ≥ 0：r := sqrt(p²+q²)；t := sqrt((r+p)/2)；→ (t, q/(2t))；
  3. p < 0：r := sqrt(p²+q²)；t := sqrt((r−p)/2)；si 的符号取 s := (1/q < 0)
     ? −t : t（`1/q` 区分 ±0，两侧皆可移植、无 signbit 依赖）；→ (fabs(q)/(2t), s)。
- **H3 cexp**(p, q) = (exp(p)·cos(q), exp(p)·sin(q))（先算 ea := exp(p)）。
- **H4 cdiv**(a,b)/(c,d)：den := c²+d²；(R, I) := ((a·c+b·d)/den, (b·c−a·d)/den)。
  域上限：|c|²+|d|² ≤ ~1.8e308，超出即 EOVER（§6；语料域 |z| ≤ 10 恒不触发）。

| # | 函数 | 定义（运算顺序逐字冻结） | 路径注记 |
|---|---|---|---|
| 1 | `csin` | R := sin(x)·cosh(y)；I := cos(x)·sinh(y) | 闭式恒等式 |
| 2 | `ccos` | R := cos(x)·cosh(y)；I := −sin(x)·sinh(y) | 闭式恒等式 |
| 3 | `ctan` | \|y\| ≤ 1：sr := sin(x)·cosh(y)；si := cos(x)·sinh(y)；cr := cos(x)·cosh(y)；ci := −sin(x)·sinh(y)；(R,I) := H4(sr,si)/(cr,ci)。<br>\|y\| > 1：den := cos(2.0·x) + cosh(2.0·y)；R := sin(2.0·x)/den；I := sinh(2.0·y)/den | **混合式，阈值 1.0 冻结**：比值形避开实极点灾难性消去（x=π̂/2 时和式形分母恰好舍入为 0），和式形保证大 \|y\| 无中间溢出（cosh(y) 在 y≈710 即溢出而 cosh(2y)/2 形仍稳）；den 在 \|y\|>1 支恒 > 0，无除零 |
| 4 | `csinh` | R := sinh(x)·cos(y)；I := cosh(x)·sin(y) | 闭式恒等式 |
| 5 | `ccosh` | R := cosh(x)·cos(y)；I := sinh(x)·sin(y) | 闭式恒等式 |
| 6 | `ctanh` | \|x\| ≤ 1：sr := sinh(x)·cos(y)；si := cosh(x)·sin(y)；cr := cosh(x)·cos(y)；ci := sinh(x)·sin(y)；(R,I) := H4(sr,si)/(cr,ci)。<br>\|x\| > 1：den := cosh(2.0·x) + cos(2.0·y)；R := sinh(2.0·x)/den；I := sin(2.0·y)/den | 与 ctan 对称的混合式，阈值 1.0 冻结；den 在 \|x\|>1 支恒 > 0 |
| 7 | `casin` | tre := 1.0 − x·x + y·y；tim := −2.0·x·y；(wr,wi) := H2(tre,tim)；uz := −y + wr；vi := x + wi；(lr,li) := H1(uz,vi)；R := li；I := −lr | C99 G.7 复合式 −i·cln(iz+csqrt(1−z²))。tim 直算（不经复数乘平方）保证 ±0 侧正确流入 H2：z=x±0i（x>1）时 tim=∓0 → si=±(−√..) 共轭成对（§9 实测锚） |
| 8 | `cacos` | 先按 #7 得 (ur,ui)；R := HALF_PI − ur；I := −ui | 恒等式 acos = π/2 − asin（主值支全域成立，含割线 ±0 侧；§9 实测锚） |
| 9 | `catan` | 支点先行（§5 EDOM）；num := x·x + (y+1)·(y+1)；den := x·x + (y−1)·(y−1)；R := 0.5·atan2(2.0·x, 1.0 − x·x − y·y)；I := 0.25·(log(num) − log(den)) | Hull 稳定式；I 用对数差避免 num/den 除法溢出；x=±0 落虚割线时 atan2(±0, 负) = ±π 自动给出 ±π/2 侧（C99 G.6 一致） |
| 10 | `casinh` | iw := (−y, x)；按 #7 对 (iw) 得 (ur,ui)；R := ui；I := −ur | 恒等式 asinh = −i·asin(iz)；iw 以 (−im, re) 直取保 ±0 |
| 11 | `cacosh` | (p,q) := H2(x−1, y)；(m,n) := H2(x+1, y)；pr := p·m − q·n；pi := p·n + q·m；(lr,li) := H1(x+pr, y+pi)；R := lr；I := li | C99 G.7 乘积式 cln(z+csqrt(z−1)·csqrt(z+1))；z±1 分量直取保 ±0 |
| 12 | `catanh` | iw := (−y, x)；按 #9 对 (iw) 得 (ur,ui)（其支点判据见 §5）；R := ui；I := −ur | 恒等式 atanh = −i·atan(iz)；支点 z=±1 经 iw=±i 落入 catan 支点 |
| 13 | `cpow` | z==0 分支先行（§5）；(lr,li) := H1(x,y)；**A 覆写**：A := (u·lr − v·li, u·li + v·lr)；(R,I) := H3(A) | cpow(z,w) = cexp(w·cln z)，cPowReal 全复推广；A 被覆写（§3） |
| 14 | `cnthroot` | k ≤ 0 → EPARAM；z==0 → (0,0) OK；r := sqrt(x·x + y·y)；th := atan2(y, x)/(double)k；base := pow(r, 1.0/(double)k)；R := base·cos(th)；I := base·sin(th) | de Moivre 主值根；θ 取 atan2 主值再除 k，±0 侧共轭自动成对（cx-cnth-02/03） |
| 15 | `cgamma` | 非正整数极点先行（§5 EDOM）。<br>**直算支**（x ≥ 0.5，`cx_gamma_direct(x, y)`）：ar := c0；ai := 0；k = 1..8：dr := (x−1.0) + (double)k；(qr,qi) := H4(c_k,0)/(dr,y)；ar += qr；ai += qi；(lr,li) := H1(x+6.5, y)；m1r := (x−0.5)·lr − y·li；m1i := (x−0.5)·li + y·lr；lgr := LN_SQRT_2PI + m1r − (x+6.5) + 0.5·log(ar·ar+ai·ai)；lgi := m1i − y + atan2(ai, ar)；(R,I) := H3(lgr,lgi)。<br>**反射支**（x < 0.5）：sx := PI·x；sy := PI·y；sr := sin(sx)·cosh(sy)；si := cos(sx)·sinh(sy)；(gr,gi) := cx_gamma_direct(1.0−x, −y)；dr2 := sr·gr − si·gi；di2 := sr·gi + si·gr；(R,I) := H4(PI,0)/(dr2,di2) | Lanczos g=7/n=9（§9 系数表）；复 Lanczos 核 t^{z−0.5} 以 exp((z−0.5)·Lnt) 形态直算；反射式 Γ(z)=π/(sin(πz)·Γ(1−z))，1−z 的虚部取 **−y**（conj），全程无自调用（ac_special.c 同款直算因子结构） |

**Lanczos 系数表**（C：`static const double CX_LANCZOS[9]` 落 .rodata；JS：9 个
标量 var，同 core_special.js 手工展开先例——与 C/128KB ac_special.c 同表）：

```
c0=0.99999999999980993  c1=676.5203681218851   c2=-1259.1392167224028
c3=771.32342877765313   c4=-176.61502916214059 c5=12.507343278686905
c6=-0.13857109526572012 c7=9.9843695780195716e-6  c8=1.5056327351493116e-7
```

## 5. 奇点/极点/域错误表（EDOM 判据）

输入含任何非有限分量（NaN/±Inf）→ 一律 EDOM。其余按函数：

| 函数 | EDOM 条件 | 数学语义 |
|---|---|---|
| `catan` | x == 0 && (y == 1 \|\| y == −1)（x=±0 均命中，`==0` 对 ±0 同真） | 支点 ±i，对数发散 |
| `catanh` | y == 0 && abs(x) == 1（经 iw 判据等价于 iw 落 ±i） | 支点 ±1，对数发散 |
| `cgamma` | y == 0 && x ≤ 0 && x == floor(x)（x=−0 命中：floor(−0)=−0） | Γ 的非正整数极点 |
| `cpow` | z=(0,0) 且非「w=(0,0) → 1」或「Re w>0 且 Im w==0 → 0」两种约定值 | 0^w 无极限 |
| 其余 11 个 | 无 EDOM（整函数无奇点；casin/cacos/casinh/cacosh 支点值有限：casin(±1)=±π/2、cacosh(1)=0、casinh(±i)=±iπ/2，由公式自然给出） | — |

**cpow 零底约定值**（写死，非错误）：z=(0,0) && w=(0,0) → TMP=(1,0) 返回 OK
（0⁰:=1，Python `0**0=1` 同款）；z=(0,0) && u>0 && v==0 → TMP=(0,0) 返回 OK。

## 6. 溢出与非有限语义（EOVER）

- 返回前统一终检：TMP 两分量全有限 → OK；任一分量非有限（±Inf/NaN）→ EOVER(-2)，
  TMP 内容为该次计算的自然 IEEE 产物（调用方只可信错误码）。
- 中间量溢出（如 H4 分母、cgamma 直算支 exp(lgr) 上溢）最终必使 TMP 非有限，
  与终检合并，无中间态错误码。
- **已知域上限**（语料域 |z| ≤ 10 恒不触发，成文备查）：H1/H2/H4/cnthroot 的
  平方和 p²+q² 在 |p| 或 |q| ≳ 1.34e154 时溢出；ctan/ctanh 和式形在 2|y| 或 2|x|
  ≳ 710.5 时 cosh 上溢；cgamma 直算支 |Γ(z)| ≥ ~1.8e308（x ≳ 171.6）上溢；
  反射支 Γ(1−z) 上溢而真值 |Γ(z)| < DBL_MIN 的深负实部输入会**下溢为 0**
  （round-to-nearest 的正确 float64 答案，判 OK；契约 JSON 不收录此类行）。

## 7. 错误码全序

| 值 | 宏（C，ac_pro.h CX 锚点） | 语义 |
|---|---|---|
| 0 | `AC_PRO_CX_OK` | 成功，TMP 已写入且全有限 |
| −1 | `AC_PRO_CX_EDOM` | 输入落在 §5 奇点表 / 输入非有限 |
| −2 | `AC_PRO_CX_EOVER` | 计算非有限（§6） |
| −3 | `AC_PRO_CX_EPARAM` | cnthroot 的 k ≤ 0 |

错误行比较：错误码**精确相等**；不比较错误时的 TMP 内容。

## 8. 误差带（每函数标定）

比较公式（逐分量）：`err = |a − b| / max(1.0, |a|, |b|) ≤ 1e-9`（相对-绝对混合：
O(1) 值退化为绝对 1e-9，大值如 ctan(π̂/2+0i)=1.63e16 按相对计）。**±0 不比符号**
（|0−(−0)|=0 恒过）。错误码精确比较。

| 函数族 | 误差带 | 标定依据 |
|---|---|---|
| csin/ccos/ctan/csinh/ccosh/ctanh | 1e-9 | 闭式恒等式，仅 libm 一层舍入（sin/cos/exp/log/atan2 双端典型 ≤2 ulp）；1e-9 余量 ≥6 个量级 |
| casin/cacos/catan/casinh/cacosh/catanh | 1e-9 | 复合式两层 libm；近支点行由 mpmath 期望值（§9）逐条约束 |
| cpow/cnthroot | 1e-9 | cln+cexp 两层；cx-cpow-01 欧拉锚独立可验（真值 −1） |
| **cgamma** | **1e-9（单独标定）** | 标定依据：①与文献 Γ(1+i)=0.49801566811835604−0.15494982830181069i 的 mpmath 值一致（ref-complex.py 内建文献自检，阈值 1e-28）；②实轴交叉回归锚：cgamma(x+0i) 与既有 ac_special.c `ac_special_gamma` / core_special.js `special.gamma` 同表 Lanczos，两侧测试各含此锚（1e-9）；③**实现落地后以 diff-complex.js 实测复核**：全部 JSON 行与语料行双端 err 实测值随差分提交登记，若实测 >1e-9 则按路线图预案降级 C 独有并申报（其余 14 API 不受牵连） |

公式表表达式两侧逐字同构是 1e-9 的前提；libm 实现差异（MinGW gcc 8.1.0 vs
V8/JerryScript）典型 ≤2 ulp，对 1e-9 带 余量 ≥ 6 个量级；落地后以差分实测复核。

## 9. 契约 JSON 与差分语料

- **`tests/complex-functions.json`**（117 条）= 本契约的机器可读子集：`meta` 记
  容差/错误码/溯源；`cases[]` 每条 `{id, fn, z, [w, k,] want}`，want 为
  `{re, im}` 或 `{err}`。fn/k/w 语义同 §4。生成器 **`tools/ref-complex.py`**
  （mpmath 40dps，精确 float64 输入；带符号零分量按 C99 G 系列割线约定沿零符号
  扰动 ±1e-35——割线点值 = 该侧解析极限；整函数/亚纯函数/cgamma 不扰动，防极点
  邻域 |f'|~1e32 放大扰动）。内建文献 Γ(1+i) 自检，漂移即拒绝生成。
  重生成：`python tools/ref-complex.py > tests/complex-functions.json`，diff 应为空。
- **割线 ±0 侧约定实测锚**（本契约起草时以 CPython cmath 逐行核对，两侧实现须
  复现）：casin(2±0i)=(π/2, ±acosh2)、casin(−2+0i)=(−π/2, +acosh2)、
  cacos(2+0i)=(0, −acosh2)、cacos(−2+0i)=(π, −acosh2)、catanh(2±0i)=(½ln3, ±π/2)、
  casinh(2i)=(acosh2, π/2)、casinh(−0+2i)=(−acosh2, π/2)、cacosh(−2+0i)=(acosh2, π)、
  cacosh(0+0i)=(0, π/2)、catan(+0+2i)=(π/2, 0.25ln9)、catan(−0+2i)=(−π/2, 0.25ln9)、
  ctan(π̂/2+0i)=(1.633e16, 0)、ctanh(iπ̂/2)=(0, 1.633e16)（有限，非错误）。
- **差分语料 `tests/bench/complex-corpus.csv`**（输入网格，生成器
  `tools/gen-complex-corpus.js`）：|z| ∈ [0.1,10] 全象限固定网格 × 15 函数族
  （每族 ~50 点）+ 四条割线两侧采样（实轴 x∈[−10,−1]∪[1,10] 的 y=±0；虚轴
  y∈[−10,−1]∪[1,10] 的 x=±0）+ 极点/溢出边界 + −0.0 边角 + cpow/cnthroot 参数
  网格，共 300–800 行，列 `id,fn,zre,zim,wre,wim,k`。差分驱动
  **`tests/bench/diff-complex.js`** 以同语料驱动 C 探针与 JS 侧，逐行按 §8 比较，
  全部一致 exit 0，否则 exit 1。语料为输入网格（无期望列），C↔JS 逐行互证；
  语义真值由本 JSON 的 117 条独立锚定。

## 10. 测试面与验收线

- **C**：`test/test_pro_complex_ext.c` 12–20 例（每例 4–8 checks），经
  test_pro_runner.c CX 锚点声明、Makefile CX 锚点 `cx_runner` 独立目标链接
  （§11 接线说明）、verify.ps1 CX 锚点门禁（exit 0 / RESULT: PASS / ≥10 例 /
  0 failed）。必含：恒等式锚（csin(i)=i·sinh1、cpow(e,iπ)=−1、cgamma(1)=1）、
  割线采样（cln(−1±0i) 经 casin/cacos/atanh 路径、casin(2)、cacos(−2)、catan(2i)）、
  极点契约（cgamma(0)、cgamma(−2)）、溢出契约、「刚好通过/刚好拒绝」双侧边界
  （catanh(1±0i) EDOM vs catanh(0.999999999) OK；catan(i) EDOM vs catan(1+i) OK）、
  cgamma 实轴交叉回归锚。
- **JS**：`test-complex-ext.js` 30–60 例（run-all 自动发现），读本 JSON
  `cases[]` 的可复现子集做全量数值断言 + 命名空间接线（setComplexA →
  complex.cXxx → getComplexTmp\*）+ 交叉回归锚。
- **差分**：`node tests/bench/diff-complex.js` exit 0。
- 既有层零回归：cSqrt/cPowReal/四阶方程复根路径零扰动（run-all 1194 基线 +
  build-kernel 83 项回归 + ESM 49 项冒烟不回退）。

## 11. C 侧接线说明（锚点纪律下的 tally 聚合）

v2.5 锚点基建在 test_pro_runner.c 预埋的 CX 锚点位于跨 TU 声明区（main 之前），
锚点纪律禁止在 main() 聚合区加行，故 C 侧测试接线为：**Makefile CX 锚点**内
`PRO_EXTRA += src/ac_pro_complex_ext.c`（内核/runner 双口径无条件编译）+ 新增
`cx_runner` 独立目标（OBJ_DIR 模式规则编译 test_pro_complex_ext.c，与 OBJS_SRC
链接成独立验收二进制）；**verify.ps1 CX 锚点**构建并运行 cx_runner，门禁
（exit 0 / RESULT: PASS / [case CX-n] ≥ 10 / 0 failed）独立成立。test_pro_runner.c
的 CX 锚点保持空置（留注释指向本节）。cx_runner 的 .bss 与内核口径一致（测试 TU
零 static），不影响 42608/42640/39408 三口径。
