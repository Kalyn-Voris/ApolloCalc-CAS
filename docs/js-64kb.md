# JS/64KB — 零 GC 计算代数内核（已验证版）

- **状态**：已完成，已在华为 WATCH FIT 4 鸿蒙应用中实机验证
- **堆限制**：64KB（运行时全局状态水位 ≤ 48KB，由 `checkMemoryWatermark` 门禁）
- **内存模型**：TypedArray + 全局标量，全程零临时对象、零 GC 触发

## 功能清单

- **解析层**：`parseExpression`（单变量多项式/表达式，token + 数值双池），`dumpTokenStream`
- **数学函数**：`applyFunction`（sin/cos/tan/atan2/sqrt/ln/log/abs 等，角度制可切）
- **精确数值**：三分量模型 `(num/den)·√rad`，化简根式、有理化、四则运算
- **求值层**：`evalExpressionAt` / `evalEquationAt`（RPN 栈式求值）、`extractCoefficients`（数值采样 + 高斯消元提取多项式系数）
- **求解层**：二次（含复根/精确形式）、三次（卡尔达诺 + 三角法）、四次（费拉里降次）、`solveLinearSystem`（高斯消元，≤6 元）、`verifyRoots`（残差回验）
- **防护层**：嵌套深度/函数个数/变量个数 preCheck、次数门禁（>4 拒绝）、内存水位检查

## 已知边界

- 精确形式仅支持纯三分量：混合形式（如 `(1+√5)/2`）置无效，仅给数值根
- 不支持积分/微分、符号展开、多变量方程
- 5 次及以上方程、6 元以上线性方程组直接拒绝（门禁）
- 复数仅出现在方程求根结果中，不支持复数表达式运算

## 构建方式

```bash
node test/build-kernel.js     # 按序拼接 6 个 core 文件 → ../src/core_kernel.js + 80 项回归
node test/test-evaluator.js   # 求值层 41 项冒烟
node test/test-solvers.js     # 求解层 39 项冒烟
```

源码 `core_kernel.js` 约 79KB（含注释与 export 块）；设备端按 64KB 堆约束运行，UI 层仅经 export 块访问内核。
