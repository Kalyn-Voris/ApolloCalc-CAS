# ApolloCalc-CAS · JS 128KB

JS 64KB 的扩展版本，堆限制 128KB，在保持 Zero-GC 铁律的前提下支持更多功能。设计文档见 [docs/js-128kb.md](../../docs/js-128kb.md)。

组件版本：**v2.2.0**（见 [VERSION](VERSION) 与 [CHANGELOG.md](CHANGELOG.md)；
双轨制自 v2.2.0 起——全局版本节点是仓库级 Tag，见根 [CHANGELOG.md](../../CHANGELOG.md)，
两者是不同口径）。

## 相对 64KB 的新增功能（Tier 2）

- 五分量精确形式（(rat + radNum·√rad)/den × 10^exp，替代三分量，支持科学计数法）
- 数值微积分：数值导数（中心差分）与定积分（Simpson）
- 泰勒级数展开（≤16 阶，逐阶数值导数 + 中心差分表，Horner 求值）
- 双曲函数扩展：`sinh` / `cosh` / `tanh`
- 反双曲函数：`asinh` / `acosh` / `atanh`
- 复数扩展：复数域函数求值与复根精确形式
- 有理根检验（rational root theorem）：高次方程的额外精确形式覆盖
- 多项式因式分解（整系数，低次数）
- 进制转换（2/8/10/16）与 SI 词头
- 更大 token/数值池（512/256）、求值栈翻倍（128）、更宽 preCheck 阈值（深度≤6 / 函数≤4 / 变量≤10）
- 线性方程组扩 12 元（矩阵池 156 = 12×13，根结果池同步 12 槽）
- 家族共享一致性用例接入（tests/ 三套 JSON）

## Zero-GC 铁律（与 64KB 相同）

- 禁止对象字面量、数组字面量
- 禁止递归
- 禁止在热路径中字符串拼接
- 所有计算数据用预分配 TypedArray

## 目录结构

```
JS/128KB/
├── README.md               本文件
├── src/
│   ├── core_parser.js           解析层（池已扩容：token 512 / 数值 256 / 栈 128 / 矩阵 156）
│   ├── core_math_functions.js   数学函数层（含双曲/反双曲函数）
│   ├── core_complex.js          复数层（含复数域函数 cExp/cLn/cPowReal）
│   ├── core_exact.js            精确形式层（五分量 (rat + radNum·√rad)/den × 10^exp）
│   ├── core_evaluator.js        求值层
│   ├── core_calculus.js         数值微积分（导数/积分/极限/泰勒展开）
│   ├── core_base.js             进制转换 + SI 前缀（字符码缓冲输出）
│   ├── core_solvers.js          求解层（12 元线性方程组）
│   ├── core_ratroot.js          有理根检验 + 低次多项式因式分解
│   └── core_kernel.js           构建产物，禁止手动编辑
└── test/
    ├── test-evaluator.js        求值层冒烟测试
    ├── test-solvers.js          求解层冒烟测试
    ├── test-complex.js          复数域函数测试
    ├── test-calculus.js         数值微积分冒烟测试
    ├── test-base.js             进制转换 + SI 前缀测试
    ├── test-ratroot.js          有理根检验 / 因式分解测试
    ├── test-taylor.js           泰勒展开测试
    ├── test-consistency.js      家族共享一致性测试（tests/ 三套 JSON）
    └── build-kernel.js          拼接 + 语法校验 + 回归
```

## 构建方式

```bash
node test/build-kernel.js --minify   # 唯一重建入口：生成压缩版 src/core_kernel.js 并跑全量回归
```

### 压缩管线（v2.2.0）

- `--minify` 是 `core_kernel.js` 的唯一重建入口：**175 142 → 57 523 B**（压缩率
  67.2%，越过 > 50% 目标；预算 131 072 B）。
- 主路径 **terser 两段式**：(a) 内核体按经典 script 压缩、`mangle.toplevel: false`
  （顶层名全保留）；(b) EXPORT_BLOCK 单独按 module 压缩（`mangle.properties` 默认
  关闭，对象属性名原样）。两段拼接后 `export default` 契约不变，压缩体可做全量
  eval 回归。
- **回退路径**：terser 不可用（未安装/无网络）时自动改用内置词法级剥离器——同
  入口、同全量校验：逐态扫描字符串/模板串（状态栈处理 `${}` 插值）/正则字面量
  （prev-token 启发式）/注释边界，绝不盲目正则去注释；token 文本逐字零改写、
  原换行位置一律保留（ASI/restricted production 零改动）、未闭合一律 fail-fast，
  16 项词法边界单测。实测 175 142 → 79 435 B（54.6%），同样达标。
- 测试口径：12 测试文件 **603 pass, 0 fail**。组件细分记录见
  [CHANGELOG.md](CHANGELOG.md)。

## 测试

```bash
node test/test-evaluator.js
node test/test-solvers.js
node test/test-complex.js
node test/test-calculus.js
node test/test-base.js
node test/test-ratroot.js
node test/test-taylor.js
node test/test-consistency.js
```

零依赖，Node ≥ 14。共享一致性用例在 [tests/](../../tests/)。

## 开发状态（8/8 步全部完成）

- [x] 骨架搭建
- [x] core_exact.js 五分量重写
- [x] core_calculus.js 数值微积分
- [x] core_math_functions.js 双曲函数扩展
- [x] core_complex.js 复数扩展
- [x] core_base.js 进制/SI
- [x] 池扩容 + preCheck 阈值翻倍 + 一致性测试接入
- [x] 对照表承诺兑现补做：12 元线性方程组 + 有理根检验/因式分解（core_ratroot.js）+ 泰勒展开
