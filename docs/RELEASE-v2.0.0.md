# ApolloCalc-CAS v2.0.0 Release Notes

> 发布日期：2026-10-26 · 分支 master（本地，暂缓 push）

## 一句话

双侧同步的多变量引擎升级：C 侧 Pro 层三项能力以**零 `.bss` 增长**落地，
JS 侧多变量模块转正并补齐矩阵与特殊函数——同一个数学引擎的 JS/C99
双实现继续逐位对齐。

## 亮点

### C/128KB-ProTEXT（`.bss` 41 456 B，零增长达成）

| 能力 | 入口 | 内存代价 |
|---|---|---|
| 多变量符号求导 d/dx、d/dy | `ac_pro_diff_multi(src, var_index, out, out_len)` | 0 B（复用 mv 项池槽 0/1） |
| 多变量多项式积分（精确整除） | `ac_pro_integrate_multi`，非整除拒绝 `AC_PRO_MVI_ENONEXACT` | 0 B（连预留 frac 视图都未动用） |
| sin/cos LUT 常量表化 | `AC_PRO_MATH_LUT_CONST`（默认开） | `.bss` −16 416 B（16 KB 查表迁 `.rdata`） |

- 281 项测试全绿（基线 259 + 12 + 10）；`bss-check` 三门禁精确命中
  （41456 / 41488 / 39408）；退化对拍 38 672 B 逐位同构。
- LUT 插值精度与运行时填充逐位一致（max |err| = 4.706e-6 ≈ h²/8 理论值）。
- 开发方式：三 Git worktree 并行（`feat/multi-diff` / `feat/multi-integrate` /
  `feat/lut-const`），冲突面收敛在 Makefile / ac_pro.h / test_pro_runner.c
  三个共享文件，并集解决。

### JS/128KB（12 文件内核，浏览器/Node 双通道）

| 能力 | 入口 | 校验 | 零 GC 证据 |
|---|---|---|---|
| 多变量多项式 + **符号求导 `diff`** | `ApolloMultivar.*`（对齐 C 侧同款逐项幂法则与 exps 编码） | 194 | 6 TypedArray 10 208 B，allocCount 恒 6 |
| 矩阵基础运算（乘/转置/≤3×3 行列式与逆） | `ApolloMatrix.*`，寄存器模型 UI 契约 | 39 | 3 TypedArray 216 B，allocCount 恒 3 |
| 特殊函数 erf / gamma / lgamma | `ApolloSpecial.*`（A&S 26.2.17 + Lanczos g=7，与 C 侧同表同条款） | 33 | **0 B**——纯标量，模块内无数组字面量 |

- 12 个测试文件 **603 pass, 0 fail**；build-kernel 内嵌 83 项 eval 回归 +
  43 项 ESM 严格模式导出链路冒烟全过。
- `--expose-gc` 三模块联合探针：20 000 次混合调用，allocCount 恒 6/3/0，
  堆增长 0.17 MB（JIT 分层噪声量级）。
- 浏览器验证：vm 沙箱模拟经典 `<script>` 环境（无 require/module）10 项全过，
  加上 ESM import 冒烟，四条加载通道（require / 全局 var / ESM / 内核拼接）全部有测试钉住。
- `core_kernel.js` 175 142 B：越过 128 KiB 文件线是 v2.0.0 的**有意裁决**
  （项目名指设备堆预算；静态池 22 416 B ≈ 21.9 KiB，距 96KB 水位门禁余量充足），
  详见 docs/js-128kb.md 内存账本节。

## 升级与兼容性

- **C 侧**：无既有 API 变更。`AC_PRO_MATH_LUT_CONST=0` 可整体回退旧 LUT 行为；
  退化构建（`AC_PRO_DEGRADED=1`）产物与上游 v1.1.0 逐位同构的验证保持有效。
- **JS 侧**：既有 9 个 core 文件零改动（三新模块自包含）；`index.js` 消费方
  通过内核导出块的 `multivar` / `matrix` / `special` 三个命名空间字段访问新能力，
  旧接口（`parseExpression` 等全部 75 个标识符）原样保留。

## 边界（诚实声明）

- JS 矩阵为 ≤3×3 闭式（Sarrus/伴随矩阵，无消元无选主元）；12×12 完整矩阵
  运算（秩/LU/QR/特征值）仍是 C 独有能力。12 元线性方程组求解两侧均有，
  不在此列。
- `erf` 按 A&S 26.2.17 的 1.5e-7 上限设计（双侧实现路径不同，跨语言对齐
  容差按 2e-7 口径）；gamma/lgamma 双侧同表 Lanczos，~1e-13 相对精度。
- 多变量积分只支持多项式核且要求系数精确整除——不返回近似值是特性不是缺陷。
- 上游 `C/128KB/` 冻结红线全程未破（三个分支 `git diff master -- C/128KB/` 均为空）。

## 下一步（见设计说明 §5）

1. 完整多变量 Groebner（M4GB 逐对归约路线，C 侧 mv 基础设施的自然终点）；
2. JS 内核压缩构建（回到 128 KiB 文件线，预估空白/注释剥离 ~30%）。

---

*测试统计口径：C 侧 `pro_runner.exe` 281 项；JS 侧 12 文件 603 项 +
build-kernel 内嵌 83 + 43 项；两侧全部本地实测，无一处凭记忆抄写。*
