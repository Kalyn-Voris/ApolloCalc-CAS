# COMPLEX-DIFF — F1 复变函数扩展 C/JS 差分报告

- 语料：complex-corpus.csv 789 行（含契约锚 117 行）
- 判定：错误码精确（错误行 TMP 不比，契约 §6）+ err = |a-b|/max(1,|a|,|b|) <= 1e-9（NaN==NaN、±0 不比符号）；极点邻域行按 §8 r3 不变量判定
- C/JS 行间最大 err（值行，不含不变量行）：2.766e-16（@grid-csin-6-4）
- 不一致行：0（其中三方校验失败 0）

RESULT: PASS (all rows identical within contract band)

