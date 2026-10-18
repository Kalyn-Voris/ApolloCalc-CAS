# JS/128KB — 零 GC 计算代数内核（增强版）

- **状态**：已完成（8/8 步，含对照表承诺兑现补做）
- **堆限制**：128KB（全局状态水位目标 ≤ 96KB）
- **目标平台**：不限——任何支持 ES5 + TypedArray 的 JS 环境（Lite Wearable / JerryScript / 浏览器 / Node）
- **内存模型**：与 64KB 版一致——TypedArray + 全局标量，零临时对象，禁止递归与热路径字符串拼接

## 功能清单

### Tier 1（继承自 JS/64KB，基线）

- 表达式解析与求值（隐式乘法、右结合幂运算、RAD/DEG 角度模式）
- 二次 / 三次 / 四次方程求解（三角代换 + Cardano + 费拉里，含复根）
- 线性方程组（12 元，列主元高斯消元 + 部分主元）
- 精确形式（五分量 (a + b√c)/d × 10^e：rat / radNum / rad / den / exp）
- 系数采样提取、残差回验、preCheck 防护层、内存水位门禁

### Tier 2（128KB 新增，已落地 11 项）

1. 五分量精确形式（(a + b√c)/d × 10^e，替代三分量，支持混合形式与科学计数法）
2. 数值微积分：数值导数（中心差分）与定积分（Simpson）
3. 双曲函数扩展：`sinh` / `cosh` / `tanh`
4. 反双曲函数：`asinh` / `acosh` / `atanh`
5. 复数扩展：复数域函数求值与复根精确形式
6. 进制转换（2/8/10/16）与 SI 词头
7. 更大 token/数值池（512/256）、求值栈翻倍（128）、preCheck 阈值翻倍（深度≤6 / 函数≤4 / 变量≤10）
8. 家族共享一致性用例接入（tests/ 三套 JSON，`test/test-consistency.js`）
9. 有理根检验 + 低次多项式因式分解（`core_ratroot.js`：候选 p/q 枚举 + 综合除法 +
   迭代降次显式栈；三次/四次整数与分数根获得精确形式）
10. 泰勒级数展开（`core_calculus.js` 模块七：≤16 阶，逐阶数值导数 + 中心差分表，Horner 求值）
11. 线性方程组扩 12 元（矩阵池 156 = 12×13，根结果池同步扩 12 槽）

> 原计划的"有理根检验、多项式因式分解、线性方程组扩 12 元"已于第 8 步补做兑现
> （见下）；"结果历史缓存"仍未落地：UI 层职责边界未定，另立任务卡。

## 内存账本（静态池实际 byteLength，扩容后实测）

| 池 | 64KB 版 | 128KB 版（实际） |
|---|---|---|
| tokenPool（Int32） | 256×4 = 1024 B | 512×4 = 2048 B |
| numberPool（Float64） | 128×8 = 1024 B | 256×8 = 2048 B |
| evalStack（Float64） | 64×8 = 512 B | 128×8 = 1024 B |
| matrixPool（Float64） | 42×8 = 336 B | 156×8 = 1248 B（12×13） |
| _rpnToken（Int32） | 256×4 = 1024 B | 512×4 = 2048 B |
| _opStack（Int32） | 64×4 = 256 B | 128×4 = 512 B |
| _lhsToken + _rhsToken（Int32） | 128×4×2 = 1024 B | 256×4×2 = 2048 B |
| _coeffs + 根/精确形式结果池（12 槽 × 五分量） | ~0.3 KB（4 槽） | ~0.7 KB（12 槽） |
| _baseBuf（Int32，进制/SI） | — | 64×4 = 256 B |
| **静态池合计** | **~5.2 KB** | **11 992 B ≈ 11.7 KB** |

> 水位门禁降级估算口径（含 _baseBuf）为 11 992 B；计入最坏游标
> （512 token × 8 + 256 number × 8）与 2KB 运行时冗余约 19.7 KB，
> 距 96KB 门禁仍有充足余量。门禁阈值维持 96KB（128KB × 75%）不变。

## 与 64KB 的架构差异

- **五分量精确形式**：`core_exact.js` 从 num/den/rad 三分量重写为
  (rat + radNum·√rad)/den × 10^exp 五分量，黄金比例 (1+√5)/2 等
  "有理部 + 根式部"混合形式可直接精确表达；根结果池的 `_rootExact*`
  数组同步扩为 rat/radNum/rad/den/exp 五组；分量超出 2^53-1 时先尝试
  把 10 的因子转移进指数（值不变），仍超限则降级为纯浮点并置无效标志。
  水位门禁阈值同步从 48KB 上调至 96KB（128KB × 75%）。
- **新模块**：`core_calculus.js`（数值导数 + 定积分，固定步长表 + 工作区复用，零分配）、
  `core_base.js`（进制转换 + SI 词头，输出走预分配字符缓冲）。
- **拼接顺序**：parser → math_functions → complex → exact → evaluator → calculus → base → solvers → ratroot（9 文件）。
- 池尺寸与 preCheck 阈值（深度/函数数/变量数上限翻倍）按上表放大，算法骨架不变；
  所有池溢出检查改为读 `xxPool.length`（尺寸调整时检查值自动跟随），
  矩阵池扩为 156（12×13，根结果池同步扩 12 槽承载 12 元解），
  `checkMemoryWatermark` 静态池加总走 `byteLength` 自动跟随，门禁维持 96KB。
- **有理根快路径**：solveCubic/solveQuartic 开头经 `ratRootDescent` 枚举整数候选
  （p|a0、q|an，约简去重）综合除法验证，迭代降次（显式栈，无递归）；
  有理根走 exactSetFraction 获得精确形式，剩余部分交低次求解器，
  无有理根时原路径（Cardano/三角/笛卡尔）不变。

## 开发顺序（8 步，全部完成）

1. [x] 骨架搭建：从 64KB 复制 src/test，路径与文档就位
2. [x] `core_exact.js` 五分量重写（rat/radNum/rad/den/exp）+ 求解层接线（混合形式与科学计数法落地）
3. [x] `core_math_functions.js` 双曲函数扩展（sinh/cosh/tanh + asinh/acosh/atanh）
4. [x] `core_complex.js` 复数扩展（复数域求值、复根精确形式）
5. [x] 新增 `core_calculus.js` 数值微积分（导数 + 定积分）
6. [x] 新增 `core_base.js` 进制转换与 SI 词头
7. [x] 池扩容（token 512 / 数值 256 / 栈 128，矩阵池保持 6×7）+ preCheck 阈值翻倍 +
       全量回归对齐 `tests/` 一致性用例（`test/test-consistency.js`）
8. [x] 对照表承诺兑现补做：线性方程组扩 12 元（根结果池同步 12 槽）+
       新增 `core_ratroot.js`（有理根检验 + 因式分解，三次/四次精确形式补全）+
       泰勒展开（`core_calculus.js` 模块七）

## 测试与构建

```bash
node test/test-evaluator.js      # 求值层冒烟（49 项）
node test/test-solvers.js        # 求解层冒烟（46 项，含 128KB 门禁边界 + 12 元线性方程组）
node test/test-complex.js        # 复数域函数（23 项）
node test/test-calculus.js       # 数值微积分（28 项）
node test/test-base.js           # 进制转换 + SI 前缀（63 项）
node test/test-ratroot.js        # 有理根检验 / 因式分解（33 项）
node test/test-taylor.js         # 泰勒展开（26 项）
node test/test-consistency.js    # 家族共享一致性（tests/ 三套 JSON，59 pass + 4 improve）
node test/build-kernel.js        # 拼接 core_kernel.js + 83 项回归 + ESM 严格模式冒烟（35 项）
```

零依赖，Node ≥ 14 即可。共享一致性用例在 [tests/](../tests/)。

## 一致性测试判定规则

`test-consistency.js` 对比 128KB 与 64KB 的行为差异，分三类：

- **PASS**：数值根一致，精确形式一致或更强
- **IMPROVE**：数值根一致，但 128KB 在以下方面更强：
  - preCheck 阈值放行（深度/函数/变量上限翻倍）
  - 五分量精确形式覆盖三分量无法表达的场景（如黄金比例）
- **FAIL**：数值根不一致（真回归）

IMPROVE 判定数据驱动：用例在 `tests/edge-cases.json` 声明
`"expectedImprove": true`（64KB 拒绝而 128KB 放行），读到该字段且 128KB
成功即判 IMPROVE，不靠用例命名猜测。

当前 4 个 IMPROVE：3 个 precheck 放行 + 1 个黄金比例精确化。
