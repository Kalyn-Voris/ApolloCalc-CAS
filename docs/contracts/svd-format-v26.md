# 奇异值分解输出契约 v2.6 —— one-sided Jacobi（G2，本版 C-only）

> **状态**：冻结（v2.6.0 G2 生效，开工第 1 个 commit）。**归属**：feat/matrix-advanced 产出。
> **本版范围（用户已裁决）**：**C-only**——C 参照实现 `C/128KB-ProTEXT/src/ac_pro_svd.c`
> 本版落地；JS 面（`core_matrix.js` pro 层 `proSvd*` API）**顺延 v2.7**（预算熔断线 R2
> 预案，先例：v2.4.0 schur 的反向分叉）。本契约即为 v2.7 JS 面的实现蓝本：**算法公式、
> 表达式书写顺序、排序全序、符号规范化、错误码在本文件一次定死，JS 侧逐字转录，
> 不得各自发明**。任何变更须双组确认后先改本文件再改码。

---

## 1. 动机与用途

v2.6.0 G2 落地 m×n 实矩阵奇异值分解（one-sided Jacobi / Hestenes，LAPACK dgesvj 同族），
补齐 ROADMAP.md:75「v2.4 后观察」窗口点名的能力面。SVD 的数值输出（奇异值谱、左右
奇异向量、错误码）是跨版本/跨语言对拍对象：one-sided Jacobi 的旋转公式在 IEEE754
binary64 下只有**表达式书写顺序逐字一致**才能把两侧数值差稳进 1e-9（F4 契约 §3 的
实测教训直接沿用）；奇异向量有 ± 符号自由度与退化子空间自由度，不一次定死则对拍
被符号翻转与子空间旋转淹没。

因此先于任何实现，把以下内容一次定死：

1. **算法骨架与数值常量**——幂 2 缩放、固定循环序列 one-sided Jacobi、deflation、
   列 2-范数排序，全部本文件 §3 逐字冻结；
2. **旋转公式表达式书写顺序**——§3.4 逐字（v2.7 JS 面同式同序才能稳进 1e-9）；
3. **输出全序**——奇异值降序的稳定插入排序（§3.7，ac_pro_schur.c:440-486 同款）；
4. **错误码**——1 / −1 / −2 / −3 的精确语义（§3.9）；
5. **奇异向量符号规范化**——每列最大 |·| 分量置正、并列取最小索引（§4，
   eigen-format-v25.md §4 同式同序复用）；
6. **退化谱语义**——重奇异值 / σ=0 的向量子空间数学上不唯一，契约只断言 w 精确
   一致 + 重构/正交不变量，逐向量严格对拍仅限良分离谱（§5，ODE knownDeviations
   教训的直接应用）。

## 2. 适用范围与总原则

- **数学范围**：一般 m×n **实**稠密矩阵，m, n ∈ [1, 12]（`AC_PRO_MATRIX_MAX_DIM`
  同款）；对称性不限，m < n 合法（内部走转置路径，§3.2）。复矩阵不在域内。
  **不做 lstsq / 最小二乘管线**（ROADMAP.md:92 成文边界，G2 只落 SVD 本体）。
- **零存储（C）**：错误码走返回值、数据在 Pro union 的 svd 槽（3752 B ≤ 3848 吸收线，
  构成：a 工作副本 144×8=1152 + U 1152 + V 1152 + w 12×8=96 + work 24×8=192 +
  n_st/pad 8，margin 96 B）；零新 static 可变对象（ac_pro.h eig 节论证：一个 static
  = TU 净 +32 B .bss）。sparse（3848）仍是 union 峰值，.bss 账本 42608/42640/39408
  不动（v2.6 开工实测基线精确命中）。
- **零 malloc / 零 VLA / 零递归**（C 侧；`--wrap=malloc` 链接拦截 + pro_runner live
  旗标同源证明）。
- **确定性优先**：一切次序由 §3 的机械规则决定，精确并列保持提取序；凡规则与
  美观冲突，以规则为准。
- **从严失败**：30 轮 sweep 预算耗尽返 −2、双闸门回乘验证不过返 −3——绝不输出
  未验证的分解、绝不硬凑。

## 3. 算法与输出契约（本节即 ac_pro_svd.c 的逐字规范）

### 3.1 常量（冻结）

| 常量 | 值 | 语义 |
|---|---|---|
| `PRO_SVD_MAX_SWEEPS` | 30 | sweep 预算封顶，超限返 −2（语料实测峰值 12 轮，含 12×12 秩亏与 2^±150 尺度谱；2.5× 余量） |
| `PRO_SVD_ORTHO_TOL` | 1e-14 | 列对正交判据的相对阈值（≈45×eps，正常列对收敛后 fp 噪声 ~2.2e-16·√(αβ) 稳定低于此线） |
| `PRO_SVD_ZERO_TOL` | 1e-12 | 数值零列判据（相对）：①sweep 内列对跳过：√min(α,β) ≤ 此值·√max(α,β) → 该对已数值正交（ ghost 尘埃对的 γ/√(αβ) ≡ 1，相对正交判据永不触发，不设此跳过则 ζ=inf → t=0 → 恒等旋转死循环 → −2，dense12 实测复现）；②出口数值零 σ：w_j ≤ 此值·w_0 → w_j 精确置 0、方向走补全（被弃质量 ≤ 1e-12·σ₁ ≪ 1e-9 闸门，诚实且确定性） |
| `PRO_SVD_COMPLETE_MIN` | 0.25 | 零 σ 列 Gram-Schmidt 补全的残量范数验收线（Bessel 恒等式保证 m ≤ 12 时必有候选：候选基上补空间投影平方和 = p_rows − rank ≥ 1，故最优候选残量 ≥ √(1/12) ≈ 0.2887 > 0.25） |
| `PRO_SVD_RESID` | 1e-9 | 出口双闸门容差（R1 相对、R2 绝对，§3.8） |

### 3.2 几何与转置路径

记 k = min(m, n)（奇异值个数）。m ≥ n 时 p = m（工作行数）；**m < n 时转置**：
在 Aᵗ（p = n 行、k = m 列）上做 Jacobi，出口把 U/V 角色对调（§3.7 步骤 f）。
全程只动 A 的**副本**，调用方 const 矩阵在整个调用期间只读。

### 3.3 幂 2 精确缩放（ac_pro_schur.c:342-369 同纪律）

设 mmax = max|a_ij|（先做非有限检查，§3.9）。mmax = 0（零矩阵）时 scale = 1 不缩放；
否则 `frexp(mmax, &e)`（mmax = f·2^e，f ∈ [0.5,1)），`scale = ldexp(1.0, e)`，
副本 B = A / scale（逐元素除以 2 的幂 = 纯指数加减，零舍入），列范数平方项绝无溢出。
**scale 非有限（e = 1024，即 max|a| ≥ 2^1023）→ −1 拒绝**（域界诚实拒绝，绝不在
scale = inf 下产出 NaN 谱）。出口 w 逐个 ×scale 还原（乘幂 2，精确）。
（JS 无 frexp：用 eigen-format-v25.md §3.1 的 log2 初值 + 双侧精确校正环逼近同一
整数，与 frexp 逐位一致——v2.7 同款。）

### 3.4 one-sided Jacobi 紧循环（表达式书写顺序逐字冻结）

B（p×k 行主序，b[i*k+j]）固定循环序列扫描列对 (p, q)，p = 0..k−2，q = p+1..k−1。
V 初始 = I_k（列主序 v[j*k+i]，即第 j 列 = &v[j*k]）。每对按下式（**两侧逐字一致**）：

```
alpha = 0.0;  for (i = 0; i < p_rows; i++) alpha += b[i*k+p] * b[i*k+p];
beta  = 0.0;  for (i = 0; i < p_rows; i++) beta  += b[i*k+q] * b[i*k+q];
gamma = 0.0;  for (i = 0; i < p_rows; i++) gamma += b[i*k+p] * b[i*k+q];
if (gamma == 0.0)  -> 跳过（已精确正交 / 精确零列，不计数）
smin = sqrt(min(alpha, beta)); smax = sqrt(max(alpha, beta));
if (smin <= PRO_SVD_ZERO_TOL * smax)  -> 跳过（数值零列对，不计数——见 §3.1 该常量语义①）
if (fabs(gamma) <= PRO_SVD_ORTHO_TOL * sqrt(alpha*beta)) -> 跳过（已正交，不计数）
zeta = (beta - alpha) / (2.0 * gamma);
t = ((zeta >= 0.0) ? 1.0 : -1.0) / (fabs(zeta) + sqrt(1.0 + zeta * zeta));
c = 1.0 / sqrt(1.0 + t * t);
s = c * t;
for (i = 0; i < p_rows; i++) {
    bp = b[i*k+p];  bq = b[i*k+q];
    b[i*k+p] = c * bp - s * bq;
    b[i*k+q] = s * bp + c * bq;
}
for (i = 0; i < k; i++) {
    vp = v[p*k+i];  vq = v[q*k+i];
    v[p*k+i] = c * vp - s * vq;
    v[q*k+i] = s * vp + c * vq;
}
rotated++;   /* 本 sweep 实际旋转计数 */
```

其中 p_rows = p（几何行数，非循环变量对 p）。跳过次序即上列次序（三道跳过
全部不计数）。数值零列对跳过是终止性保证的一半：数值秩亏列对（含幽灵尘埃
对）的 γ² = αβ 精确成立，相对正交判据永不触发；没有这道跳过，尘埃对会在
ζ = ±inf → t = 0 的恒等旋转上永久空转（30 轮预算耗尽 → −2）。有了它，尘埃
对一经判定即冻结，正常列对按平方收敛，30 轮预算对实测峰值 12 轮有 2.5×
余量。NaN 不可能产生：γ == 0 已被先行拦截，ζ 有界的算术证明见 §3.1 语义①
（|ζ| ≤ (√(β/α)+√(α/β))/2·1e14，pair-zero 跳过把 α/β 比钉在 1e-24 之外）。

### 3.5 sweep 终止与预算

一个 sweep = 全部 k(k−1)/2 个列对按 §3.4 扫一遍。**sweep 结束时 rotated == 0 即收敛**
（含零矩阵、k == 1、全对角等退化情形：首个 sweep 即 0 旋转收敛）。收敛前最多
30 个 sweep；**第 30 个 sweep 结束仍 rotated > 0 → 返 −2**（诚实失败，绝不输出
未收敛的分解）。

### 3.6 奇异值与排序

收敛后：w_j = sqrt(Σ_i b[i*k+j]²)（求和按 i 升序累加，k 列各一，缩放域）。
随后按 w **降序稳定插入排序**（ac_pro_schur.c:440-486 同款）：键 = w_j（仅按值，
严格 `<` 才前移，精确并列保持提取序 → 确定性）；U 列（b 的列）与 V 列同步换序
（列临时 = work 槽）。

### 3.7 收敛后出口序列（顺序冻结）

a. w_j = 列 2-范数（§3.6），排序 + U/V 列同步换序；
a2. **数值零 σ 判定**：对 j = 0..k−1，w_j ≤ PRO_SVD_ZERO_TOL·w_0（w_0 为排序后
   最大值）→ w_j **精确置 0.0**。被弃的质量 ≤ 1e-12·σ₁ ≪ R1 闸门容差（诚实
   语义：σ < 1e-12·σ₁ 一律报 0，确定性优于「保留 fp 噪声谱尾」——尘埃 σ 的
   方向本就无数学意义，§5 退化谱语义的同一立场）；
b. **数据列归一化**：对 w_j > 0 的列，b 列 j ÷ w_j（缩放域除法）；
c. **零 σ 列补全**：对 w_j == 0.0 的列（排序后居尾部），按候选
   e_0, e_1, …, e_{p_rows−1} 顺序扫描：r = e_t − Σ_{j'≠j} (u_{j'}ᵗe_t)·u_{j'}
   （投影求和按 j' = 0..k−1 升序、跳过 j；u_{j'} 为当前已归一/已补全的列），
   ‖r‖ ≥ 0.25 即取 b 列 j ← r/‖r‖（**首个可收候选即停**）。Bessel 恒等式保证
   m ≤ 12 时必有候选（Σ_t ‖r_t‖² = p_rows − rank ≥ 1）；若扫描完仍无候选可收
   → 返 −3（内部不变量失败，诚实路径）。
d. **出口构造**：m ≥ n 时 U 缓冲 ← b 转置逐元素重排（u[j*m+i] = b[i*k+j]，纯
   数据搬移零舍入），V 缓冲已是最终 V（k×k 列主序，k = n）；m < n 时 U 缓冲 ←
   V 缓冲逐元素复制（V' 为 m×m 正交阵即最终 U），V 缓冲 ← b 转置逐元素重排
   （v[j*n+i] = b[i*k+j]，即 V = U'）；
e. **符号规范化**（§4，作用于最终 U/V）；
f. **双闸门回乘验证**（§3.8，作用于最终 U/V/w/原 a）；
g. w_j ×= scale（幂 2 精确还原），count = k，返回 1。

### 3.8 出口双闸门（缩放域；回乘对调用方原始 a）

- **R1 重构残差**：对 i = 0..m−1、l = 0..n−1，acc_l = Σ_j (U[i][j]·w_j)·V[l][j]
  （j = 0..k−1 升序；乘法结合顺序固定为 (U·w)·V）；R1 = max_i Σ_l |a_s[i][l] − acc_l|，
  其中 a_s = a/scale（幂 2 除法逐元素即时重算，零缓冲）、行绝对和按 l 升序累加。
  闸门：**R1 ≤ 1e-9 · (1 + ‖a_s‖∞)**，‖a_s‖∞ = max_i Σ_l |a_s[i][l]|（同序累加）。
- **R2 左向量正交性**：R2 = max_{p,q ∈ [0,k)²} |δ_pq − Σ_i U[i][p]·U[i][q]|
  （i 升序累加）。闸门：**R2 ≤ 1e-9（绝对）**。
- 任一闸门不过 → 返 **−3**，视图内容未定义、调用方不得读取（绝不硬凑）。
- V 正交性（VᵗV = I_k）是旋转乘积的结构性质，不设闸门；由探针/测试作不变量
  断言（§6）。

### 3.9 错误码（返回值，C 定义、v2.7 JS 同值同语义）

| 返回值 | 语义 | 附带状态 |
|---:|---|---|
| 1 | 成功：k 个奇异值 + 左右奇异向量全部就绪且过双闸门 | count = k，视图/accessor 可读 |
| −1 | 参数拒绝：a == NULL、m < 1、n < 1、m > 12、n > 12、任一元素 NaN/±Inf、缩放指数 e ≥ 1024（scale 非有限） | count = 0，内容不得读取 |
| −2 | 不收敛：30 个 sweep 预算耗尽仍 rotated > 0 | count = 0，**绝不输出部分结果** |
| −3 | 回乘验证失败：R1/R2 闸门不过，或零 σ 列补全无候选可收 | count = 0，内容不得读取 |

诚实披露：在 1..12 阶、有限输入的**契约域内**，one-sided Jacobi 的二次收敛使 −2
与 −3 的闸门失败路径**实际不可触发**（全语料 200+ 条 + 验收套件实测零出现）；
这两个码是「不过闸绝不硬凑」纪律的成文出口，供硬件/编译器偏差与未来扩容兜底。
测试面以「域内全语料断言 rc ∉ {−2, −3}」反向覆盖，而非伪造触发。

### 3.10 API（ac_pro.h SVD 锚点；v2.7 JS 面同构镜像）

```c
int            ac_pro_svd_all(const double *a, int m, int n);
double         ac_pro_svd_value(int k);   /* σ_k，降序，k = 0..count−1；越界返 0.0 */
const double  *ac_pro_svd_u(int k);       /* U 第 k 列（m 维），越界返 NULL */
const double  *ac_pro_svd_v(int k);       /* V 第 k 列（n 维），越界返 NULL */
int            ac_pro_svd_count(void);    /* 成功 = k；任何失败路径 = 0 */
```

#define AC_PRO_SVD_OK 1 / AC_PRO_SVD_EPARAM (−1) / AC_PRO_SVD_ENOCONV (−2) /
AC_PRO_SVD_EVERIFY (−3)。accessor 返回视图内部指针，跨 API 调用后失效
（ac_pro_scratch.h 视图持有纪律③同款）。

## 4. 奇异向量符号规范化（本契约 §一次定死，v2.7 同式同序）

对每个 j = 0..k−1：在 U 第 j 列（m 分量）取最大 |·| 分量；**并列取最小索引**
（严格 `>` 线性扫描，首达即最大）；若该分量为负，则 **U 第 j 列与 V 第 j 列同时
取反**（A = UΣVᵗ 的成对符号自由度，翻转重构不变）。零 σ 列（补全向量）同规则。
两侧实现逐字同式：线性扫描 + 严格大于 + 条件整列取反。理由与
eigen-format-v25.md §4 完全一致：不定死则差分对拍被符号翻转淹没；本节即 F4 §4
的「一次定死两处复用」承诺的第二处兑现。

## 5. 契约用例与退化谱语义（tests/svd-cases.json）

- 文件：`tests/svd-cases.json`，v2.6.0 G2 冻结 52 例。**本版消费方 = C 探针**
  （tools/gen-svd-corpus.js 把全部 52 例以原 id 收录进语料 CSV，svd_runner 逐条
  断言）；v2.7 JS 面以同文件为对拍语料（dual 对齐）。
- 字段：`id` / `m` / `n` / `matrix`（行主序扁平；拒绝维度的用例可省略；非有限
  条目以字符串 `"NaN"` / `"Infinity"` / `"-Infinity"` 编码——JSON 无这些字面量，
  JS `Number(str)` 与 C `strtod` 同值）/ `tol`（缺省 1e-9，混合相对-绝对口径
  err = |a−b| / max(1,|b|) ≤ tol）/ `expectRc`（缺省 1，精确比较）/
  `expectW`（可选，长度必须 == k，降序逐位；含数值零 σ 的用例写精确 0）/
  `uvCompare`（缺省 false）/ `expectU` / `expectV`（可选，行主序 m×k / n×k
  扁平，uvCompare = true 时逐分量）。
- **退化谱语义（成文核心）**：重奇异值 / σ = 0 的奇异向量子空间数学上不唯一
  （酉群自由度）。契约对这类用例只断言：① w 逐位一致（降序、值精确）；② 重构
  残差 ≤ tol；③ UᵗU ≈ I、VᵗV ≈ I。**逐向量严格对拍仅限良分离谱**（相邻 σ 间距
  > 1e-6 量级，此时子空间唯一至 §4 符号）。这不是分叉，是对子空间自由度的诚实
  防御，两侧同规。
- 关键用例族：1×1 / 对角 / 置换 / 恒等（w = |diag|、U 列 = +e_i 钉 §4 规范化）、
  已知闭式谱对称阵（[[2,1],[1,2]] 谱 {3,1}、三对角 2+2cos(kπ/(n+1))、
  [[2,1,1],[1,2,1],[1,1,2]] 谱 {4,1,1}）、符号置换×对角×置换ᵗ 合成（全链精确
  可验）、1×n / n×1 / 2×3 / 3×2 形状边界、秩亏（rank-1、零矩阵、补全列）、
  重 σ / 近重 σ、Hilbert 5/12 病态、1e±150 幂 2 极端尺度与 2^40 往返、
  正交判据「刚好跳过（1e-15，V 恒等）/刚好旋转（1e-13，V 非恒等）」双侧、
  维度「刚好通过（12×12）/刚好拒绝（13、0）」双侧、NaN/±Inf 拒绝。

## 6. 语料、探针与对拍（v2.7 JS 面的金标准通道）

- **语料生成器** `tools/gen-svd-corpus.js`（固定种子 mulberry32，种子 20261104，
  确定性可重生成）：输出 `tests/svd-corpus.csv`，200–400 行；结构谱 = 对角 /
  符号置换正交×对角×正交ᵗ / 已知谱对称阵 / 秩亏 / 近重 σ / Hilbert 病态 /
  随机稠密（全形状组合）/ 1e±150 幂 2 极端尺度 / 拒绝类（>12 阶、0 阶、NaN、
  ±Inf）。**契约锚**：tests/svd-cases.json 全部用例以原 id、原期望收录
  （gen 时逐 id 校验 1:1，缺一即 fail）。CSV 行格式（冻结）：
  `id,m,n,kind,tol,expect,a00,a01,…`——kind ∈ {anchor, gen}；expect 为空 = 仅
  不变量断言；`"-1"` = 期望参数拒绝；分号分隔的降序 w 列表（%.17g）= 期望谱。
- **C 探针** `C/128KB-ProTEXT/test/svd_runner.c`：Makefile SVD 锚点的独立
  `svd_runner` 目标（参照 cx_runner 先例：SVD TU 进 PRO_EXTRA + OUTDIR 域内
  独立 runner + 裸 KERNEL_LDFLAGS 链接）。用法：
  `svd_runner.exe <corpus.csv> <out.csv>`；逐条跑 `ac_pro_svd_all` 并**在探针内
  独立重算**不变量断言（不信任被测代码的闸门）：rc == 期望；w 降序；期望谱
  逐位（混合容差）；重构残差 ≤ 1e-9·max(1,‖A‖)；‖UᵗU−I‖ ≤ 1e-9；‖VᵗV−I‖ ≤ 1e-9；
  符号规范化（U 每列首个最大 |·| 分量 > 0）。任何一条失败 → 非零退出。
  输出 `tests/bench/svd128-c.csv`（随探针提交，%.17g 保证 f64 往返逐位一致）：
  `svd,<id>,<rc>,<k>,<w…k>,<U 行主序 m×k>,<V 行主序 n×k>`（拒绝用例只写前三列）。
  **该 CSV 即 v2.7 JS 面对拍金标准**：JS 侧同语料对拍 = w 逐条 ≤1e-9 + 错误码
  精确 + U/V 经 §4 规范化后良分离谱逐分量、其余走不变量。
- **验收线**：svd_runner exit 0；`verify.ps1` §4 SVD 锚点行断言 pro_runner 输出
  `[case SVD-n] ≥ 13`（n = 验收套件用例号）。

## 7. 测试面（C 侧本版）

`test/test_pro_svd.c` 16 例五组（[case SVD-1..16]，自带 svd_tally_t，零 static
对象，pro_runner 聚合；≥ 60 CHECK）：

1. **basic 方阵 thin**（5 例）：1×1 正/负/零、diag3（w = |diag| + U 列 = +e_i
   钉符号规范化 + V 符号）、[[2,1],[1,2]] 闭式谱 {3,1}（良分离谱 uv 逐分量）、
   三对角 2+2cos(kπ/5) 已知谱、恒等/置换（重 σ 谱 w 精确 + 不变量）。
2. **rectangular 矩形**（3 例）：3×2 符号置换合成（全链精确 uv）、2×3 转置路径
   （m < n，同谱对偶）、1×n / n×1 形状边界（w = 范数、U 归一化钉符号）。
3. **degenerate 秩亏 + 重 σ**（3 例）：rank-1（[[1,1],[1,1]]，w = {2,0} +
   补全列正交不变量）、零矩阵 3×2（w = 0 + U 补全正交）、重 σ / 近重 σ
   （invariants-only 的契约语义实证）。
4. **scale 极端幂 2**（3 例）：2^±150 对角（w 精确 + uv 恒等）、±2^40 往返
   （scale 前后 w 比值逐位 = 2^40）、Hilbert 12×12（刚好通过维度边界 + 病态谱）。
5. **reject 全拒绝码**（2 例 + 双侧边界）：参数拒绝全集（m=0/n=0/m=13/n=13/NULL）、
   非有限拒绝（NaN/±Inf）；正交判据双侧（1e-15 跳过 → V 逐位恒等；1e-13 旋转
   → V 非恒等）；12 阶刚好通过 / 13 阶刚好拒绝；域内 rc ∉ {−2,−3} 反向断言。

接线锚：SCHUR 13 例/55 CHECK（test_pro_schur.c 同构：自带 tally、main() 聚合区
注册块、verify.ps1 §4 计数门禁）。JS 40–70 ok() 归 v2.7（本契约 §5/§6 已备齐
语料与金标准 CSV）。

## 8. 本版范围成文（C-only 裁决）

- **本版交付**：ac_pro_svd.c（~500 行）+ svd 视图（3752 B）+ API + Makefile SVD
  锚点（内核 TU + svd_runner 探针目标）+ test_pro_svd.c（16 例）+ runner tally +
  verify.ps1 §4 门禁行 + gen-svd-corpus.js + tests/svd-corpus.csv +
  tests/bench/svd128-c.csv + 本契约 + tests/svd-cases.json。
- **顺延 v2.7**：JS 面 core_matrix.js pro 层 `proSvd*`（按 §3 逐字移植）、
  build-kernel.js EXPORT matrix 块、test-svd.js、diff-svd.js（读 §6 金标准
  CSV 对拍）。**R2 熔断线预案先例**：v2.4.0 schur 的 C-first 分叉反向版
  （ROADMAP.md:315 ②「G2 的 JS 面顺延 v2.7、SVD 成文 C-only」）。
- 红线：上游 C/128KB、根账本文件（CHANGELOG/features/README/VERSION）、
  JS 侧 20 个 test-*.js 一概不碰。
