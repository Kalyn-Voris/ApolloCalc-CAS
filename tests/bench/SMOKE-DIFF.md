# C 64KB vs JS 64KB 全量冒烟比对

- 用例来源：`tests/polynomials.json` / `tests/exact-forms.json` / `tests/edge-cases.json` 手工转录（50 例）
- C 侧：`C/64KB/test/test_smoke_csv.c` → `make smoke_runner` → `c-smoke.csv`
- JS 侧：`tests/bench/js-smoke.js`（动态 import `JS/64KB/src/core_kernel.js`，只读）→ `js-smoke.csv`
- 数值容差 1e-9（绝对）；精确形式三元组与错误码为精确比对

## 求解用例（多项式 + 线性方程组）

| 用例 | 阶段 | JS 根数 | C 根数 | 最大差异 | 状态 |
|:---|:---|---:|---:|---:|:---|
| quad_56 | solve | 2 | 2 | 0 | ✅ |
| quad_cplx | solve | 2 | 2 | 0 | ✅ |
| quad_rhs9 | solve | 2 | 2 | 0 | ✅ |
| quad_dup | solve | 2 | 2 | 0 | ✅ |
| quad_frac | solve | 2 | 2 | 0 | ✅ |
| quad_cplx5 | solve | 2 | 2 | 0 | ✅ |
| quad_352 | solve | 2 | 2 | 0 | ✅ |
| quad_12 | solve | 2 | 2 | 0 | ✅ |
| quad_cplx13 | solve | 2 | 2 | 0 | ✅ |
| quad_irr2x | solve | 2 | 2 | 0 | ✅ |
| quad_rhs2x | solve | 2 | 2 | 0 | ✅ |
| quad_degen | solve | 2 | 2 | 0 | ✅ |
| quad_cplx2 | solve | 2 | 2 | 0 | ✅ |
| quad_gold | solve | 2 | 2 | 0 | ✅ |
| cubic_123 | solve | 3 | 3 | 0 | ✅ |
| cubic_8 | solve | 3 | 3 | 0 | ✅ |
| cubic_trip | solve | 3 | 3 | 0 | ✅ |
| cubic_irr | solve | 3 | 3 | 4.04e-21 | ✅ |
| cubic_cardano | solve | 3 | 3 | 0 | ✅ |
| cubic_1 | solve | 3 | 3 | 0 | ✅ |
| cubic_1111 | solve | 3 | 3 | 0 | ✅ |
| quart_biquad | solve | 4 | 4 | 0 | ✅ |
| quart_1 | solve | 4 | 4 | 0 | ✅ |
| quart_1234 | solve | 4 | 4 | 0 | ✅ |
| quart_cplx | solve | 4 | 4 | 0 | ✅ |
| quart_general | solve | 4 | 4 | 0 | ✅ |
| quart_quad4 | solve | 4 | 4 | 0 | ✅ |
| quart_4 | solve | 4 | 4 | 0 | ✅ |
| quart_deleg | solve | 3 | 3 | 0 | ✅ |
| quintic_gate | solve | 0 | 0 | 0 | ✅ |
| lin2_ok | solve | 2 | 2 | 0 | ✅ |
| lin3_ok | solve | 3 | 3 | 0 | ✅ |
| lin6_ok | solve | 6 | 6 | 0 | ✅ |
| lin2_sing | solve | 0 | 0 | 0 | ✅ |

## 门禁用例（preCheck 拒绝码）

| 用例 | JS err | C err | 状态 |
|:---|---:|---:|:---|
| pre_depth | 2 | 2 | ✅ |
| pre_funcs | 2 | 2 | ✅ |
| pre_vars | 2 | 2 | ✅ |

## 精确形式用例（根槽 0/1 的 valid 标志）

| 用例 | JS valid | C valid | 状态 |
|:---|:---|:---|:---|
| exact_56 | 1,1 | 1,1 | ✅ |
| exact_2 | 1,1 | 1,1 | ✅ |
| exact_3 | 1,1 | 1,1 | ✅ |
| exact_5 | 1,1 | 1,1 | ✅ |
| exact_9 | 1,1 | 1,1 | ✅ |
| exact_49 | 1,1 | 1,1 | ✅ |
| exact_dup4 | 1,1 | 1,1 | ✅ |
| exact_frac | 1,1 | 1,1 | ✅ |
| exact_352 | 1,1 | 1,1 | ✅ |
| exact_41 | 1,1 | 1,1 | ✅ |
| exact_6x1 | 1,1 | 1,1 | ✅ |
| exact_2x | 1,1 | 1,1 | ✅ |
| exact_gold | 0,0 | 0,0 | ✅ |

**结论**：50/50 用例一致（含错误码与精确形式精确比对），0 处数值差异 > 1e-9；C 64KB 与 JS 64KB 行为完全一致。