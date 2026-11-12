# C 128KB vs JS 128KB 奇异值分解对拍（v2.7.0 G2 one-sided Jacobi）

- 用例来源：`tests/bench/svd-corpus.csv`（235 例 = 52 anchor + 183 gen，种子 20261104）
- C 侧：`C/128KB-ProTEXT/test/svd_runner.c` → `tests/bench/svd128-c.csv`（随探针提交）
- JS 侧：动态 import `JS/128KB/src/core_kernel.js` → `matrix.proSvd`
- 数值容差 per-case（缺省 1e-9，混合口径 err=|a−b|/max(1,|b|)）；rc/count 精确比对；
  不变量探针独立重算 ≤1e-9；输出契约见 `docs/contracts/svd-format-v26.md`

| 用例 | JS rc | C rc | 状态 |
|:---|---:|---:|:---|
| sv-n1-pos | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-n1-neg | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-n1-zero | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-diag3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-diag3-neg | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-mixed-diag | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-eye4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-sym2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-swap2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-tri4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-tri3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-perm3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-permcombo | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-r32 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-r23 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-r13-row | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-r31-col | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-r12 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-r21 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-r43 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-r26 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-rank1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-zero32 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-zero23 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-rep2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-rep3-signs | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-rank2diag4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-nearrep | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-sym3rep | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-widerankdef | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-pow2up | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-pow2dn | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-pow2mix | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-pow2sym | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-pow2rank1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-tinyspread | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-hugerank | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-roundtrip | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-tallscale | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-hilbert5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-bound-1212 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-bound-skip | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-bound-rot | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-dense56 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| sv-rej-m13 | -1 | -1 | ✅  |
| sv-rej-n13 | -1 | -1 | ✅  |
| sv-rej-1313 | -1 | -1 | ✅  |
| sv-rej-m0 | -1 | -1 | ✅  |
| sv-rej-n0 | -1 | -1 | ✅  |
| sv-rej-nan | -1 | -1 | ✅  |
| sv-rej-inf | -1 | -1 | ✅  |
| sv-rej-neginf | -1 | -1 | ✅  |
| g-diag-1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-diag-2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-diag-3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-diag-4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-diag-5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-diag-6 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-diag-7 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-diag-8 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-diag-9 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-diag-10 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-diag-11 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-diag-12 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-orthdiag-2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-orthdiag-3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-orthdiag-4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-orthdiag-5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-orthdiag-6 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-orthdiag-7 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-orthdiag-8 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-orthdiag-9 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-orthdiag-10 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-orthdiag-11 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-orthdiag-12 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-tri-2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-tri-3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-tri-4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-tri-5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-tri-6 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-tri-7 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-tri-8 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-tri-9 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-tri-10 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-tri-11 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-tri-12 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-0-9x1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-1-3x11 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-2-5x12 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-3-11x2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-4-11x1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-5-4x4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-6-9x9 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-7-8x7 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-8-7x12 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-9-10x11 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-10-2x10 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-11-2x9 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-12-5x7 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-13-3x2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-14-5x8 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-15-12x8 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-16-9x5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-17-7x3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-18-6x10 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-19-6x3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-20-7x7 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-21-8x8 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-22-5x12 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-23-12x4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-24-12x1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-25-3x5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-26-3x10 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-27-12x10 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-28-4x4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-29-12x12 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-30-9x2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-31-9x5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-32-1x11 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-33-5x8 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-34-8x11 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-35-5x6 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-36-11x3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-37-8x5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-38-5x4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-39-6x1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-40-5x11 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-41-7x12 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-42-7x7 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-43-9x5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-44-2x6 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-45-12x9 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-46-6x2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-47-8x2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-48-9x9 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-49-4x12 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-50-5x12 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-51-9x4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-52-5x10 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-53-10x4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-54-5x9 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-55-10x8 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-56-10x2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-57-3x5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-58-5x4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dense-59-3x9 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-0-8x7r5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-1-12x4r3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-2-9x4r2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-3-9x9r6 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-4-4x7r1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-5-3x11r1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-6-12x11r7 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-7-8x12r3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-8-6x7r3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-9-4x5r3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-10-5x10r1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-11-10x11r3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-12-9x7r6 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-13-5x11r2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-14-12x2r1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-15-3x7r1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-16-12x9r7 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-17-2x11r1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-18-11x12r4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-19-11x12r8 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-20-7x4r1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-21-6x3r1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-22-2x7r1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-23-3x6r2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-24-10x11r6 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-25-6x9r2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-26-5x2r1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-27-12x2r1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-28-6x7r1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rankdef-29-4x12r1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dupcol-0 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dupcol-1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dupcol-2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dupcol-3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dupcol-4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dupcol-5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dupcol-6 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-dupcol-7 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-nearrep-0 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-nearrep-1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-nearrep-2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-nearrep-3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-nearrep-4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-nearrep-5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-nearrep-6 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-nearrep-7 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-nearrep-8 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-nearrep-9 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-nearrep-10 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-nearrep-11 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-hilbert-2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-hilbert-3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-hilbert-4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-hilbert-5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-hilbert-6 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-hilbert-7 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-hilbert-8 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-hilbert-9 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-hilbert-10 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-hilbert-11 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-hilbert-12 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2-0-3x1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2-1-3x5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2-2-2x4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2-3-6x5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2-4-1x3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2-5-2x4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2-6-5x6 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2-7-4x2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2-8-3x3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2-9-1x1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2-10-2x1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2-11-6x2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2spread-0 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2spread-1 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2spread-2 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2spread-3 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2spread-4 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2spread-5 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2spread-6 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-pow2spread-7 | 1 | 1 | ✅ wErr=0.0e+0 bit-eq |
| g-rej-m0 | -1 | -1 | ✅  |
| g-rej-n0 | -1 | -1 | ✅  |
| g-rej-m13 | -1 | -1 | ✅  |
| g-rej-n13 | -1 | -1 | ✅  |
| g-rej-nan | -1 | -1 | ✅  |
| g-rej-inf | -1 | -1 | ✅  |
| g-rej-neginf | -1 | -1 | ✅  |
| g-rej-nanbig | -1 | -1 | ✅  |

**235/235 通过**；w 逐位一致 219/219；uv 逐分量对拍 20 例，零分叉
