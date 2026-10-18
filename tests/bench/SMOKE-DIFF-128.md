# C 128KB vs JS 128KB 全量冒烟比对

- 用例来源：`tests/polynomials.json` / `tests/exact-forms.json` / `tests/edge-cases.json` 手工转录（50 例）
- C 侧：`C/128KB/test/test_smoke_csv.c` → `make smoke_runner` → `c128-smoke.csv`
- JS 侧：`tests/bench/js128-smoke.js`（动态 import `JS/128KB/src/core_kernel.js`，只读）→ `js128-smoke.csv`
- 数值容差 1e-9（绝对）；五分量精确形式与错误码为精确比对

## 求解用例（多项式 + 线性方程组）

| 用例 | JS 根数 | C 根数 | 最大差异 | 状态 |
|:---|---:|---:|---:|:---|
| quad_56 | 2 | 2 | 0 | ✅ |
| quad_cplx | 2 | 2 | 0 | ✅ |
| quad_rhs9 | 2 | 2 | 0 | ✅ |
| quad_dup | 2 | 2 | 0 | ✅ |
| quad_frac | 2 | 2 | 0 | ✅ |
| quad_cplx5 | 2 | 2 | 0 | ✅ |
| quad_352 | 2 | 2 | 0 | ✅ |
| quad_12 | 2 | 2 | 0 | ✅ |
| quad_cplx13 | 2 | 2 | 0 | ✅ |
| quad_irr2x | 2 | 2 | 0 | ✅ |
| quad_rhs2x | 2 | 2 | 0 | ✅ |
| quad_degen | 2 | 2 | 0 | ✅ |
| quad_cplx2 | 2 | 2 | 0 | ✅ |
| quad_gold | 2 | 2 | 0 | ✅ |
| cubic_123 | 3 | 3 | 0 | ✅ |
| cubic_8 | 3 | 3 | 0 | ✅ |
| cubic_trip | 3 | 3 | 0 | ✅ |
| cubic_irr | 3 | 3 | 0 | ✅ |
| cubic_cardano | 3 | 3 | 0 | ✅ |
| cubic_1 | 3 | 3 | 0 | ✅ |
| cubic_1111 | 3 | 3 | 0 | ✅ |
| quart_biquad | 4 | 4 | 0 | ✅ |
| quart_1 | 4 | 4 | 0 | ✅ |
| quart_1234 | 4 | 4 | 0 | ✅ |
| quart_cplx | 4 | 4 | 0 | ✅ |
| quart_general | 4 | 4 | 0 | ✅ |
| quart_quad4 | 4 | 4 | 0 | ✅ |
| quart_4 | 4 | 4 | 0 | ✅ |
| quart_deleg | 3 | 3 | 0 | ✅ |
| quintic_gate | 0 | 0 | 0 | ✅ |
| lin2_ok | 2 | 2 | 0 | ✅ |
| lin3_ok | 3 | 3 | 0 | ✅ |
| lin6_ok | 6 | 6 | 0 | ✅ |
| lin2_sing | 0 | 0 | 0 | ✅ |

## 门禁用例（preCheck 拒绝码）

| 用例 | JS err | C err | 状态 |
|:---|---:|---:|:---|
| pre_depth | 2 | 2 | ✅ |
| pre_funcs | 2 | 2 | ✅ |
| pre_vars | 2 | 2 | ✅ |

## 精确形式用例（五分量，根槽 0/1 的 valid 标志）

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
| exact_gold | 1,1 | 1,1 | ✅ |

**统计**：50/50 用例一致（含错误码与五分量精确比对），0 处数值差异 > 1e-9。

**结论**：C 128KB 与 JS 128KB 在 50 个冒烟用例上行为完全一致。