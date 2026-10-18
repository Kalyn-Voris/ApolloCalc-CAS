# JS/64KB — 已验证内核

零 GC 计算代数内核的已完成版本，源自华为 WATCH FIT 4 鸿蒙应用并经实机验证。详见 [docs/js-64kb.md](../../docs/js-64kb.md)。

## 目录内容

- `src/`：6 个内核模块（parser → math_functions → complex → exact → evaluator → solvers）+ `core_kernel.js`（`build-kernel.js` 自动拼接的分发版本，禁止手动编辑）
- `test/`：`test-evaluator.js`（求值层 41 项）、`test-solvers.js`（求解层 39 项）、`build-kernel.js`（拼接 + 语法校验 + 80 项回归）

## 构建与测试

```bash
node test/build-kernel.js     # 重新拼接 core_kernel.js 并跑全量回归（输出 BUILD OK）
node test/test-evaluator.js   # 求值层冒烟：全部通过输出 41 passed, 0 failed
node test/test-solvers.js     # 求解层冒烟：全部通过输出 39 pass, 0 fail
```

零依赖，Node ≥ 14 即可。共享一致性用例在 [tests/](../../tests/)。
