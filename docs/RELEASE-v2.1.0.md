# ApolloCalc-CAS v2.1.0 Release Notes（草稿）

> 发布日期：2026-10-27 · 分支 master（本地，暂缓 push）

## 一句话

C 侧 Pro 层按压缩边界设计说明的高价值项（④⑤）落地两大能力：n ≤ 32 的
稀疏线性方程组求解与 180 位任意精度定点十进制。前者让 Pro union 峰值
上移到 sparse 视图（3 848 B，涨幅远小于研究估算——42608 < 42684），
后者被现有 union 余量完全吸收（零 `.bss` 增长）；上游 `C/128KB/` 依旧
逐字节未动。

## 亮点

开发方式：双 Git worktree 并行（`feat/sparse-linsolve` / `feat/precision-decimal`），
骨架由 `tools/scaffold_v2_1_0.py` 幂等生成，多代理工作流自动执行分支门禁、
冲突合并与全量回归；冲突面收敛在 Makefile / ac_pro.h / ac_pro_scratch.h /
test_pro_runner.c / verify.ps1 五文件，并集解决。

### 稀疏线性方程组（`ac_pro_sparse_solve`，n ≤ 32 / nnz ≤ 320）

| 项 | 契约 |
|---|---|
| 输入 | 三元组平行数组 `tri_r/tri_c/tri_v` + `rhs`（顺序任意；重复坐标求和合并；精确 0 丢弃） |
| 算法 | CSR 压缩（计数排序 + 行内排序 + 同列合并）→ 阈值主元消元（τ = 0.1，候选中取行非零元最少者，主元 ≥ max(τ·pmax, 1e-12)）→ 回代 |
| 结果 | `ac_pro_sparse_x()` 解向量 / `ac_pro_sparse_rhs()` 消元后右端 / `ac_pro_sparse_perm()` 行交换记录（int16_t，与 A4 的 lu.piv 同语义） |
| 内存 | sparse 视图 3 848 B（Pro union 新峰值）；**填充与输入共享 320 总容量，绝不静默丢填充** |
| 纪律 | 无递归 / 无 malloc / 无 VLA / 无静态对象（Zero-GC）；调用期间不得嵌套其它 Pro 视图 API（视图持有纪律，跨 Pro 视图 API 调用后访问器指针失效） |

错误码走**返回值**（零静态存储通道——任何 static 都让 TU 净增 32 B `.bss`，
直接破坏 42608/42640 门禁）：

| 码 | 常量 | 语义 |
|---:|---|---|
| 1 | `AC_PRO_SP_OK` | 成功（x/perm/rhs 视图可读） |
| −1 | `AC_PRO_SP_EN` | n 超限（n < 1 或 n > 32） |
| −2 | `AC_PRO_SP_ENNZ` | nnz 超限（nnz < 1 或 nnz > 320） |
| −3 | `AC_PRO_SP_EPIVOT` | 主元低于下限（某步列最大模 ∈ (0, 1e-12)） |
| −4 | `AC_PRO_SP_ESING` | 结构奇异（某步列在待消行中无非零元） |
| −5 | `AC_PRO_SP_EFILL` | 消元填充溢出（总非零元将超 320） |
| −6 | `AC_PRO_SP_EINDEX` | 三元组非法（下标越界或指针为空） |

失败时视图内容未定义，调用方不得读取。

### 任意精度定点十进制（`ac_pro_dec_*`，180 位）

**表示**：value = m · 10^(−scale)——m 为上游 `ac_bigint_t`（基数 1e9、
20 limb、180 位十进制容量），scale 为 int32（±2^31）。dec 视图
5 × 96 B = 480 B 挂在 Pro union，低于 sparse 峰值 → **`.bss` 零增长**
（设计说明 ⑤「完全吸收」的估算兑现）。

| 接口 | 签名 | 说明 |
|---|---|---|
| 窄 | `ac_pro_dec_parse(src, slot)` / `ac_pro_dec_render(slot, out, out_len)` | 字符串 ↔ 5 个 dec 槽（0..4）；四则内部走同对函数（槽 0 = A、1 = B、2 = 结果） |
| 宽 | `ac_pro_dec_add/sub/mul/div(a, b, out, out_len)` | 解析 a/b → 运算 → 渲染规范形到调用方缓冲 |

**语法**：`[+-]?digits[.digits]`（".5"/"5." 也合法）；无指数记法、无空白、
无千分位；前导零不计有效位，小数尾部零按标度剥离（"1.50" 解析为 15·10^−1）；
输出一律规范形（无尾随小数零、零无符号、整数无小数点、scale < 0 补整数百零）。

**四则算法**：加减先对齐到 max(scale)（对齐溢出 = EOVER，绝不截断），带
NINES(10^180−1) 进位预检——兜住上游 `bi_mag_add` 收尾进位静默截弃的契约
（|ta|+|tb| == 10^180 精确特例落 (1, s−180)）；乘法走上游 alias-safe
`ac_bi_mul`（尾数位数之和 ≤ 180）；除法走 divmod 余数扩标长除
（N = m_a·10^e 恰占满 180 位）。

错误码：

| 码 | 常量 | 语义 |
|---:|---|---|
| 1 | `AC_PRO_DEC_OK` | 成功且精确（结果已写入 out） |
| 2 | `AC_PRO_DEC_INEXACT` | 成功但除法在位数上限处截断（结果已写入） |
| −1010 | `AC_PRO_DEC_EPARSE` | 语法错误 |
| −1011 | `AC_PRO_DEC_EDIGITS` | 解析超 180 有效位（位数上限熔断） |
| −1012 | `AC_PRO_DEC_EDIV0` | 除数为零 |
| −1013 | `AC_PRO_DEC_EOVER` | 对齐/乘积/标度超出容量（精确值无法在 180 位尾数 + int32 标度内表示，绝不返回错值） |
| −1014 | `AC_PRO_DEC_EBUF` | out/out_len 不足（需 = 渲染长度 + 1） |
| −1015 | `AC_PRO_DEC_ESLOT` | 槽号越界（仅窄接口） |

**隔离**：只调用上游公开 `ac_bi_add/sub/mul/divmod/cmp/from_int64/to_string` 等
纯函数 API，不触碰 `g_bi_*` 草稿池，与上游 bigint scratch 契约（work1/work2
遗留内容约定）物理无关。

## 升级与兼容性

- **C 侧**：无既有 API 变更。`.bss` 门禁 41456/41488 → **42608/42640**
  （账本：38 672 上游 + 80 + 3 848 sparse 峰值 + 8 = 42 608；+32 测试 TU = 42 640），
  上游对照 39408 与退化对拍 38 672 不变；`AC_PRO_DEGRADED=1` 下两新模块整体
  编译出局（零 `.bss`），退化产物与上游同构的性质保持有效。
- **JS 侧**：零改动（本迭代纯 C 侧），12 文件 603 pass 不变，
  `build-kernel.js` 83 项回归 + 78 标识符 + 43 项 ESM 复核通过。

## 边界（诚实声明）

- 稀疏求解按设计说明④**方案 a** 交付（n ≤ 32 / nnz ≤ 320）；方案 b
  （n ≤ 48 / nnz ≤ 512）未实现。320 为输入与填充共享的总容量——逼近填充
  上限时返回 EFILL 而非静默丢精度。
- dec 除法在 180 位上限处截断并**如实返回 INEXACT**（截断值仍已写入）；
  无指数记法解析；表示域之外一律 EOVER 熔断，绝不返回错值。
- 两特性为 **C 独有**，JS 侧无对等实现（与矩阵/特殊函数的 C 独有口径一致）。
- 上游 `C/128KB/` 冻结红线未破（合并全程 `git diff/status -- C/128KB/` 双证为空）。

## 下一步（见设计说明 §5）

1. 完整多变量 Groebner（M4GB 逐对归约路线，C 侧 mv 基础设施的自然终点）；
2. JS 内核压缩构建（回到 128 KiB 文件线，预估空白/注释剥离 ~30%）；
3. 稀疏求解④方案 b（n ≤ 48 / nnz ≤ 512，≈6.1 KB 视图）按需立项。

---

*测试统计口径：C 侧 `pro_runner.exe` 328 项（基线 281 + 稀疏 35 + 定点小数 12）；
JS 侧 12 文件 603 项不变。两侧全部本地实测，无一处凭记忆抄写。*
