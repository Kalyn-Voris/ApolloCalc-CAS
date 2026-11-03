# 一般实矩阵特征值输出契约 v2.5 —— Francis 隐式双移 QR（F4）

> **状态**：冻结（v2.5.0 功能 F4 生效）。**归属**：feat/js-eigen 产出（JS 侧实现）；
> C 侧参照实现 `C/128KB-ProTEXT/src/ac_pro_schur.c`（v2.4.0 功能 8，**本功能零 C 改动**）
> 与 JS 侧实现（`JS/128KB/src/core_matrix.js` pro 层 `proSchur*` API）互为镜像。
> 排序全序、共轭对配对、错误码以 **ac_pro_schur.c 既有输出契约为准逐字转录**（两侧
> 不得各自发明）；特征向量符号规范化为本契约 §4 一次定死、两侧同式同序。
> 任何变更须双组确认后先改本文件再改码。

---

## 1. 动机与用途

v2.5.0 F4 把 C 侧 v2.4.0 的 Francis 隐式双移 QR（`ac_pro_schur.c`，540 行）移植到
JS 内核（`core_matrix.js` pro 层），补齐 features.md:171-173 成文的「一般矩阵特征值」
不对等——这是 JS 侧最后一个矩阵能力不对等项。数值输出（特征值谱、向量、错误码）
是 C/JS 跨语言对拍对象：没有共享契约，两侧各自理解排序或配对语义的后果不是
"略有差异"，而是 `tests/eigen-cases.json` 对拍全面失配且无法定位（ROADMAP.md:178
明文：两侧输出不一致即实现 bug，不许返工兜底）。

因此先于任何对拍，把以下内容一次定死：

1. **算法骨架与数值常量**——幂 2 缩放、Hessenberg、Francis 双移、deflation 判据、
   反迭代，全部逐字对照 `ac_pro_schur.c`；
2. **输出全序**——特征值排序键与共轭对展开规则（§3.6），机械可复现；
3. **错误码**——1 / −1 / −2 / −3 的精确语义（§3.7）；
4. **特征向量符号规范化**——每列最大 |·| 分量置正、并列取最小索引（§4），
   使「差一个符号」的自由度不毁掉对拍；
5. **退化谱语义**——重特征值/亏损谱只断言特征值与残差不变量，不做逐向量对拍（§5）。

用途：JS 实现者按本文件写码与写测试；C 探针（§6）按本文件导出对拍 CSV；
后续版本扩矩阵能力（v2.6 G2 SVD 的向量符号规范化同式复用）时，本文件是唯一变更入口。

## 2. 适用范围与总原则

- **数学范围**：一般 n×n **实**稠密方阵，n ∈ [1, 12]（`AC_PRO_MATRIX_MAX_DIM` 同款）。
  对称性不限——非对称输入合法（这正是本功能与 A5 `ac_pro_eig_all`/JS `proEig`
  的分界；本契约的 −2 是「QR 迭代不收敛」，与 A5 的 −2「非对称」语义不同）。
  复矩阵、长方形矩阵不在域内。
- **零存储**：C 侧错误码走返回值、视图在 Pro union 的 schur 槽（2744 B）；
  JS 侧全部计算数据在加载期一次性分配的共享池（`_luBuf`/`_vBuf`/`_piv`/`_eigLam`/
  `_work` 槽位复用，零新池——红线 `test-matrix-pro.js:95` 钉死 `proAllocCount()===7`）。
- **零 GC（JS）**：全路径显式循环，零递归、零热路径对象/数组字面量、零字符串拼接。
- **确定性优先**：一切次序由 §3.6 的机械键决定，精确并列保持提取序；
  凡规则与美观冲突，以规则为准。
- **从严失败**：50 轮预算耗尽返 −2、6 组反迭代全败返 −3——绝不输出未收敛的
  垃圾特征值、绝不硬凑复特征向量（`ac_pro_schur.c` 头注逐字语义）。

## 3. 算法与输出契约（逐字转录 ac_pro_schur.c）

### 3.1 幂 2 精确缩放

设 m = max|a_ij|。m = 0（零矩阵）时 scale = 1，不缩放。否则取 frexp 口径的
整数 e：**m = f·2^e，f ∈ [0.5, 1)**（等价于唯一整数满足 2^(e−1) ≤ m < 2^e），
scale = 2^e，H = A / scale（逐元素除以 2 的幂 = 纯指数加减，零舍入）。
全部条目压进 [≈0.5, 1]，Francis 步中的 H² 项绝无溢出。

> 实现注记（JS）：无 frexp，用 `log2` 初值 + 双侧精确校正环逼近同一整数
> （`2^e ≤ m` 则 e++、`2^(e-1) > m` 则 e--），与 frexp 逐位一致。
> **实测教训**：若把判据写成「最小 2^e ≥ m」，m=2 时得 e=1（frexp 给 e=2），
> 缩放域差一倍，tri4 的反迭代残差即失配——缩放域两侧必须逐位相同。

### 3.2 Householder 化 Hessenberg

逐列 k = 0..n−3，反射器作用于行 k+1..n−1（左，列 k..n−1）与列 k+1..n−1
（右，行 0..n−1）；变换后把该列显式置成 (a, 0, …, 0)，舍入残渣不留巢。
u 槽布局：u[0] = v₀/‖·‖，行 k+1+t ↔ u[t]（行 k+2 的分量写 u[1]——
`ac_pro_schur.c:129-131` 点名的 off-by-one 陷阱，两侧同款写法）。

### 3.3 Francis 隐式双移 QR（bulge chasing）

窗口 [l..q]，位移和 s、位移积 t 显式传入：

- 常规位移取窗口尾 2×2：s = h[q−1][q−1] + h[q][q]，t = det（窗口尾 2×2）；
- 每 10 轮不 deflation 注入 EISPACK 同款异常位移：
  s0 = |h[q][q−1]| + |h[q−1][q−2]|，s = 1.5·s0，t = −0.4375·s0²；
- 隐式起步 P₀ 从 M = H² − sH + tI 第一列构造（x, y, z 三元），k ≥ l+1 逐列
  追赶；左乘列域 jlo = max(l, k−1)（k = l 时含 l−1，保持相似性对次对角残条的
  精确缩放），右乘行域 0..min(k+3, q)；k ≥ l+1 左乘后被零化的凸出条显式清零；
- 末步 2 行反射器消 H[q][q−2] 残余凸出，H[q][q−2] 显式置 0。

### 3.4 deflation 判据与预算

- 次对角可忽略：|h[i][i−1]| ≤ **1e-12**·(|h[i−1][i−1]| + |h[i][i]|)；分母为 0 时
  退到 ‖H‖∞（缩放域，Hessenberg 后重算的 max|H_ij|）；两者皆 0 则次对角必为 0，
  直接 deflation。判据命中即把该条显式置 0。
- 1×1 deflation：λ = h[q][q]（实）；2×2 块闭式解（Vieta 稳定形，§3.5）。
- 每个特征值（每轮 Francis 步）最多 **50** 轮；deflation 成功即 its 归零；
  超限返 **−2**，绝不返回未收敛的垃圾特征值。
- 零矩阵：hnorm = 0、次对角全 0，判定式 0 ≤ 0 成立 → 逐位 1×1 deflation，
  特征值全 0，无需特例。

### 3.5 2×2 实块闭式解（Vieta 稳定形）

tr = a + d，det = ad − bc，disc = tr² − 4·det：

- disc ≥ 0（实）：l1 = (tr ≥ 0) ? (tr+√disc)/2 : (tr−√disc)/2；
  l2 = (l1 ≠ 0) ? det/l1 : tr − l1。两根均记 Im = **0**（精确 +0，非 −0）。
- disc < 0（复）：Re = tr/2，Im = √(−disc)/2；输出 (Re, +Im) 与 (Re, −Im)——
  两槽 Re **逐位相同**、Im 互为相反数（±Im 在 deflation 时即相邻成对）。

### 3.6 单元化、排序全序、共轭对展开

1. **单元化**：按 QR 输出自底向上扫描——实值（wi == 0，精确零测试）自成单元；
   wi ≠ 0 的相邻两槽合成一个单元（结构保证成对），代表值取 (wr, |wi|)，即
   单元 Im ≥ 0。
2. **排序**：键 = (|λ|, Re, Im) **降序**的**稳定插入排序**。|λ| = sqrt(Re² + Im²)
   按值比较；比较规则逐字为：
   `m < kmod || (m == kmod && Re_j < Re_k) || (m == kmod && Re_j == Re_k && Im_j < Im_k)`
   则前移。**精确并列保持提取序**（严格小于才移动）→ 确定性。
3. **展开**：排序后的单元逐个写回——实单元占 1 槽（Im = 0）；Im ≠ 0 的单元
   占相邻 2 槽：**+Im 在前、−Im 在后，两槽 Re 逐位相同**（共轭对相邻且 +Im 在前）。
4. **缩放还原**：wr/wi 逐个 ×scale（乘幂 2，精确）。

### 3.7 错误码（返回值，两侧同值同语义）

| 返回值 | 语义 | 附带状态 |
|---:|---|---|
| 1 | 成功：特征值 + 实特征向量全部就绪 | count = n，视图/访问器可读 |
| −1 | 空指针 / n < 1 / n > 12（JS 侧另含：非方阵、未置输入、非有限输入，§7） | count = 0，内容不得读取 |
| −2 | QR 迭代不收敛（单特征值 50 轮预算耗尽） | count = 0，**绝不输出部分结果** |
| −3 | 实特征向量反迭代 6 组尝试全败 | count = 0；此时特征值已正确但**一并放弃**，视图内容未定义，调用方不得读取 |

### 3.8 实特征向量（反迭代）

对每个实特征值（缩放域 lam_s = wr[k]/scale，幂 2 除法精确；mode = k mod 3）：

1. M = A_s − (lam_s + eps)·I，每次从 const 源重拷（eps：偶数组 0，奇数组
   anorm·1e-13；anorm = ‖A_s‖∞ 逐条 |·| 行和的最大值）；
2. 部分主元 LU（就地），零主元替换为 tiny（anorm·1e-30，anorm = 0 时 1e-30）；
3. 确定性初值：mode 0 = 全 1；mode 1 = 1 + 0.5i；mode 2 = (−1)^i·(1 + 0.25i)；
   mode ≥ 3 = 单位基 e_{mode−3}（注：k mod 3 ∈ {0,1,2}，e_i 分支为死码，两侧同构保留）；
4. 两步反迭代（w = M⁻¹b 归一，v = M⁻¹w 归一；归一化失败即换组）；
5. 残差闸门：‖A_s v − lam_s v‖∞ ≤ **1e-9·(1 + ‖A_s‖∞)** 才接受；
6. 6 组（扰动 × 初值）全失败 → 返 **−3**（诚实失败）。
7. 亏损（Jordan）特征值按几何重数重复给出同一条特征向量，不做正交化修饰
   ——修饰会产出非特征向量。
8. **复特征值不产出向量**：向量列显式全零；C `ac_pro_schur_vector` 返 NULL，
   JS `proSchurVector` 返 NaN、`proSchurHasVec` 返 0。绝不硬凑复向量。

## 4. 特征向量符号规范化（本契约新增，两侧同式同序）

对每个实特征值向量列 v（n 分量）：取最大 |v_i| 的分量；**并列取最小索引**
（严格 `>` 扫描，首达即最大）；若该分量为负，整列取反。

- 应用点：JS `proSchur` 出口（`proSchurVector` 交付即已规范化）；C 侧在
  **探针导出侧**（`test/eig_csv_probe.c`）应用——`ac_pro_schur.c` API 本体
  零改动，原始向量仍由 `ac_pro_schur_vector` 提供，规范化只发生在对拍导出。
- 两侧实现逐字同式：线性扫描 + 严格大于 + 条件整列取反。
- 理由：特征向量有 ± sign 自由度，不定死则差分对拍被符号翻转淹没；
  v2.6 G2 SVD 的奇异向量符号规范化**同式同序复用本节**（一次定死两处使用）。

## 5. 契约用例与退化谱语义（tests/eigen-cases.json）

- 文件：`tests/eigen-cases.json`，v2.5.0 F4 冻结 47 例，**dual 全量零分叉**
  （每例双侧同跑；本功能纯 Float64 域、无 int64 问题，不设 cOnly 分叉区）。
- 字段：`id` / `n` / `matrix`（行主序扁平；rej_n0 无矩阵）/ `order` / `vecCompare` /
  `tol`（per-case 绝对容差，缺省 1e-9）/ `expectRc` / `expectRe` / `expectIm`。
- `order = true` 的用例按 §3.6 全序**逐位**断言——仅限相邻单元 |λ| 有 >1e-6
  量级间距、或并列结构精确（单一共轭对、重复单元）的用例；谱密集（单位圆上
  根、模长并列）的用例一律 `order = false` 多重集匹配——**这不是分叉**，
  是对「浮点模长在并列边缘可能换序」的诚实防御，两侧同规。
- `vecCompare = true`：§4 规范化后逐分量 ≤ tol；`false`（缺省）：只断言特征值 +
  两侧残差不变量（§3.8.5 同式）——重特征值/亏损/病态谱的向量子空间数学上
  不唯一（ODE knownDeviations 教训），逐向量严格对拍仅限良分离谱。
- `expectRe/expectIm`：闭式谱（整数、单位圆根、2+2cos(kπ/n)、φ/ψ、Hilbert 实测
  钉值），对拍驱动对 **C 与 JS 双侧独立自证**（防「同错不异」）。
- 关键用例族：旋转矩阵（纯虚对）、友矩阵（特征值 = 多项式根，与求解层交叉）、
  复共轭对、已知闭式谱对称阵（2+2cos(kπ/n) 三对角）、Hilbert 病态、幂 2 极端
  尺度（2^±40、2^±60、1e±150）、1×1/2×2/12 阶形状边界、deflation 判据
  「刚好通过（1e-13）/刚好拒绝（1e-11）」双侧、近亏损（disc ≈ 4e-9）、
  参数拒绝 n=0/n=13，以及 **perm4_fail3**——4 阶循环置换矩阵，近亏损谱使
  部分主元 LU 有效枢轴 ≈ 2.8e-15 > tiny 阈值、反迭代方向被污染，6 组全败，
  **双侧一致返 −3**（诚实失败路径的跨语言对拍用例）。

## 6. C 探针与差分驱动

- **C 探针** `C/128KB-ProTEXT/test/eig_csv_probe.c`（独立测试辅助，不进
  runner tally、不进 Makefile/PRO_EXTRA、不触碰 verify.ps1 / smoke_runner /
  任何 C 源码与账本）。编译（worktree 内手跑，产物 gitignored）：
  `gcc -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror -O2 -DAC_PRO=1
  -Wl,--wrap=malloc -Wl,--wrap=free -o eig_csv_probe.exe test/eig_csv_probe.c
  src/ac_pro_schur.c src/ac_pro_extra_globals.c -lm`。
  `--wrap` 拦截 + live 旗标：main 起任何 malloc/free 即 abort（零 GC 的 C 侧
  同源证明；启动期存储池构造放行）。输出 `tests/bench/eig128-c.csv`（随探针提交）：
  `mat,<id>,<n>,<m...>` / `eig,<id>,<rc>,<count>` / `val,<id>,<k>,<re>,<im>,<hasvec>` /
  `vec,<id>,<k>,<v...>`，`%.17g` 保证 f64 往返逐位一致；实特征值向量行经 §4 规范化
  并附导出侧残差自检（超 1e-9·(1+‖A‖∞) 探针退出非 0）。
- **差分驱动** `tests/bench/diff-eigen.js`：读 CSV + 契约 JSON，动态 import
  `JS/128KB/src/core_kernel.js` 的 `matrix.proSchur`，逐例比对（rc/count/hasvec/
  mat 精确；数值 per-case tol；order/vecCompare/expect/结构不变式口径见 §5），
  写 `tests/bench/EIGEN-DIFF.md`。**exit 0 为验收线**；既有 diff-smoke128 50 例
  管线一行不动。

## 7. 已成文的不对等与边界（不称对等）

1. **NaN/±Inf 域闸（JS 独有）**：JS `proSchur` 对输入逐元素做非有限检查
   （`v − v ≠ 0`），命中返 −1（err = 1）。C `ac_pro_schur_all` 无 NaN 检查
   （行为未定义，NaN 输入典型走向 −2）。契约用例域全有限，此项不进对拍；
   属 JS 防御性域闸，如实成文。
2. **长方形输入（JS 独有语义）**：C API 只有 n×n 域；JS `proSetA` 允许矩形
   寄存器，`proSchur` 对非方阵返 −1（err = 3）。C 无此输入域，不构成对拍面。
3. **JS 池而非 union 视图**：C 的 schur 视图（2744 B）在 Pro union 内，失败后
   视图未定义；JS 用共享池槽位复用，语义同构（一次调用使上一次草稿失效），
   `proEig`/`proLU`/`proQR` 与 `proSchur` 互相失效对方的草稿结果（λ 槽独立，
   `proEigValue` 不被 `proSchur` 覆写）。
4. **误差符号**：两侧对同一输入逐位一致（同运算序、同 IEEE754 binary64、
   基线 x86-64 SSE2 无 FMA 收缩）——对拍容差 1e-9 是防御性余量，不是
   预期的发散空间；实测 47 例 C/JS 特征值与向量逐位一致。

## 8. JS API 面（core_matrix.js pro 层，C ac_pro_schur_* 同构镜像）

| JS | C 对应 | 语义 |
|---|---|---|
| `proSchur()` | `ac_pro_schur_all(a, n)` | 输入经 `proSetA` 装入（n×n 方阵口径），返回值 = C 同款 1/−1/−2/−3 |
| `proSchurCount()` | `ac_pro_schur_count` | 成功 = n；任何失败路径 = 0 |
| `proSchurRe(k)` / `proSchurIm(k)` | `ac_pro_schur_re/im` | 越界返 0.0（C 同款） |
| `proSchurHasVec(k)` | `ac_pro_schur_vector(k) != NULL` | 实特征值 1 / 复特征值与越界 0 |
| `proSchurVector(k, i)` | `ac_pro_schur_vector(k)[i]` | 越界或复特征值返 NaN；交付即 §4 规范化 |

常量（与 `ac_pro_schur.c:48-51` 逐字同值）：TOL 1e-12 / MAX_EPOCH 50 /
VEC_RESID 1e-9 / VEC_TRIES 6。JS 侧 `_err` 通道新增：8 = QR 不收敛（−2 时置）、
9 = 反迭代全败（−3 时置）；−1 时 err = 1（维度/非有限）或 3（非方阵）。
