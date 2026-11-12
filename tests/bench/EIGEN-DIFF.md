# C 128KB vs JS 128KB 特征值对拍（F4 Francis QR）

- 用例来源：`tests/eigen-cases.json`（47 例，dual 全量零分叉）
- C 侧：`C/128KB-ProTEXT/test/eig_csv_probe.c` → `tests/bench/eig128-c.csv`（随探针提交）
- JS 侧：动态 import `JS/128KB/src/core_kernel.js` → `matrix.proSchur`
- 数值容差 per-case（缺省 1e-9）；rc/count/hasvec/mat 精确比对；输出契约见 `docs/contracts/eigen-format-v25.md`

| 用例 | JS rc | C rc | 状态 |
|:---|---:|---:|:---|
| n1_pos | 1 | 1 | ✅ |
| n1_neg | 1 | 1 | ✅ |
| n1_zero | 1 | 1 | ✅ |
| diag3 | 1 | 1 | ✅ |
| diag3_neg | 1 | 1 | ✅ |
| diag12 | 1 | 1 | ✅ |
| diag_frac | 1 | 1 | ✅ |
| p2_big | 1 | 1 | ✅ |
| p2_small | 1 | 1 | ✅ |
| p2_mixed4 | 1 | 1 | ✅ |
| zero2 | 1 | 1 | ✅ |
| rotation90 | 1 | 1 | ✅ |
| rotation60 | 1 | 1 | ✅ |
| rot_scaled | 1 | 1 | ✅ |
| mixed_block3 | 1 | 1 | ✅ |
| mixed4b | 1 | 1 | ✅ |
| skew3 | 1 | 1 | ✅ |
| skew4 | 1 | 1 | ✅ |
| two_pairs | 1 | 1 | ✅ |
| asym_tri2 | 1 | 1 | ✅ |
| asym_tri4 | 1 | 1 | ✅ |
| nonnorm2 | 1 | 1 | ✅ |
| fib2 | 1 | 1 | ✅ |
| int_asym3 | 1 | 1 | ✅ |
| mixed4 | 1 | 1 | ✅ |
| jordan2 | 1 | 1 | ✅ |
| jordan3 | 1 | 1 | ✅ |
| degen222 | 1 | 1 | ✅ |
| degen5511 | 1 | 1 | ✅ |
| nilpotent4 | 1 | 1 | ✅ |
| tri3 | 1 | 1 | ✅ |
| tri4 | 1 | 1 | ✅ |
| tri12 | 1 | 1 | ✅ |
| hilbert4 | 1 | 1 | ✅ |
| tol_pass | 1 | 1 | ✅ |
| tol_iterate | 1 | 1 | ✅ |
| neardef2 | 1 | 1 | ✅ |
| c3 | 1 | 1 | ✅ |
| c4 | 1 | 1 | ✅ |
| c8 | 1 | 1 | ✅ |
| c12 | 1 | 1 | ✅ |
| perm4_fail3 | -3 | -3 | ✅ |
| dense3 | 1 | 1 | ✅ |
| dense_sym3 | 1 | 1 | ✅ |
| dense4 | 1 | 1 | ✅ |
| rej_n0 | -1 | -1 | ✅ |
| rej_n13 | -1 | -1 | ✅ |

**47/47 通过**，零分叉
