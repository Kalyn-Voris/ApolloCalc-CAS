# 贡献指南

ApolloCalc-CAS 是"极限编程"项目：同一 CAS 引擎用 JS / C99 两种语言、64KB / 128KB 两种内存
预算分别实现，输出一致。欢迎提交 bug 修复、测试用例与文档改进。

## 开发环境

| 组件 | 版本要求 | 用途 |
|:---|:---|:---|
| Node.js | ≥ 14（实测 v24.19.0） | JS 两版的测试与压测，零依赖 |
| GCC（MinGW-w64） | 支持 C99 + `-Wl,--wrap` | C 两版的构建与测试 |
| GNU Make | `make` 或 `mingw32-make` | C 侧构建 |

无需任何第三方库。Windows 下 `make clean` 使用 cmd 内建 `del /Q`（Makefile 内按 `$(OS)`
自动切换，不依赖 `rm`）。

## 构建与测试

### JS

```bash
cd JS/64KB   # 或 JS/128KB
node test/build-kernel.js      # 按序拼接 core_kernel.js + 全量回归 + ESM 严格模式冒烟
node test/test-evaluator.js    # 求值层冒烟
node test/test-solvers.js      # 求解层冒烟
node test/test-consistency.js  # 家族共享一致性用例（JS/128KB）
```

### C（四种构建变体）

```bash
cd C/64KB    # 或 C/128KB

mingw32-make                   # 默认构建（libm 三角函数）→ test_runner
./test_runner.exe              # 测试全绿

mingw32-make LUT=1             # 变体：sin/cos 查表加速（1024 项，误差 ~4.7e-6）
./test_runner.exe

mingw32-make clean             # 清理（`-` 前缀容忍文件不存在）
mingw32-make smoke_runner      # 跨版本冒烟 runner（独立 main，不走校验体系）
./smoke_runner.exe > ../../tests/bench/c128-smoke.csv
```

### 跨版本一致性

```bash
cd tests/bench
node js128-smoke.js > js128-smoke.csv
node diff-smoke128.js          # C128 vs JS128 → SMOKE-DIFF-128.md，退出码 0 = 一致
node diff-smoke.js             # C64 vs JS64 → SMOKE-DIFF.md
```

## Zero-GC 铁律

**这是项目的核心约束，任何 PR 都不得违反。**

### C 版

- **禁止** `malloc` / `calloc` / `realloc` / `free`
- **禁止** 变长数组（VLA）
- **禁止** 递归（改用显式栈 + 后序标记）
- 所有池必须是 `static` 全局数组或模块内 `static` 工作区，容量为编译期常量
- 池溢出检查必须**读宏或 `sizeof`**，不得硬编码容量数字（否则改池尺寸时会漏改）
- 唯一允许的堆相关调用在 `test_main.c` 的 `__wrap_*` 转发（CRT 启动期）
- 仅在出口函数（`*_to_string` / `*_format_*`）允许 `snprintf`；热路径禁止字符串拼接

验证方式（三层）：

```bash
# 1. 链接期拦截
-Wl,--wrap=malloc -Wl,--wrap=free

# 2. 运行期：kernel-live 期间命中即 abort()（test_main.c 已内建）

# 3. 静态符号检查 —— 必须无输出
nm test_runner.exe | grep -iE "malloc|free|realloc" | grep -v __wrap
```

### JS 版

- **禁止** 对象字面量与数组字面量（`{}` / `[]`）
- **禁止** 递归
- **禁止** 热路径字符串拼接
- 所有计算数据用预分配 `TypedArray` 或全局标量
- 压测堆增量应保持在 KB 量级（见 `tests/bench/result-js*.csv` 的 `MEM` 行）

## 代码风格

- **零警告门禁**：C 侧固定 `-std=c99 -Wall -Wextra -Wstrict-prototypes -Werror -O2`；
  **不得为通过编译而删除或弱化这些旗标**
- `static` 优先：模块内部状态一律 `static`，仅对外契约用 `extern`
- 头文件用 include guard；对外函数带 `ac_` 前缀
- 数值统一 `double`；精确形式整数用 `ac_int_t`（`int64_t`）+ safe 溢出原语
- 注释中英双语：文件头用中文说明设计意图，函数契约用 `[English]` 块
- 浮点比较用容差（跨语言比对容差 1e-9），精确形式与错误码用精确比较

## 测试要求

新增功能必须附带校验：

1. **C 侧**：在 `C/<ver>/test/test_<module>.c` 增加 `expect_*` 校验，并在
   `test_main.c` 注册 `test_<module>_run()`
2. **JS 侧**：在对应 `test/test-*.js` 增加校验；若改动内核，重跑 `build-kernel.js`
3. **跨语言**：若功能属于家族共享契约，须在 `C/<ver>/test/test_smoke_csv.c` 与
   `tests/bench/js<ver>-smoke.js` **两侧同步**增删用例，并重跑 diff 脚本
4. **边界值**：门禁类功能必须测「刚好通过」与「刚好拒绝」两侧（如 preCheck 的
   depth 6/7、funcs 4/5、vars 10/11）

**不得为了让测试通过而修改测试期望值** —— 先怀疑实现。若确认是期望值算错，
在 commit message 中说明推导过程。

## PR 流程

1. Fork 并创建特性分支（`fix/...` / `feat/...` / `docs/...`）
2. 提交前本地跑通全部相关测试：
   - 改动的版本：`mingw32-make clean && mingw32-make && ./test_runner.exe`（或对应 JS 命令）
   - **受影响的其他版本**：跨版本共享代码改动后，须跑另一版本确认零回归
   - 跨语言契约改动：重跑 `diff-smoke*.js`
3. 确认 `nm` 检查无输出、编译零警告
4. PR 描述中附：改动文件清单、测试通过数（前后对比）、有无警告、`nm` 结果
5. 若涉及文档承诺的功能，同步更新 `docs/features.md` 与对应版本设计文档

## 版本与发布

- 各版本独立版本号，存于 `C/<ver>/VERSION`（JS 版见设计文档）
- 四版本完成度与验收证据汇总见 [docs/ACCEPTANCE.md](docs/ACCEPTANCE.md)

## 许可

提交即表示同意以 [MIT](LICENSE) 许可发布。
