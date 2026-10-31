/* ApolloCalc · test-matrix.js — 矩阵基础运算测试

   被测模块：src/core_matrix.js（自包含，不依赖既有 core）
   运行：node test/test-matrix.js   （JS/128KB 目录下）
   全过输出：==== matrix: N pass, 0 fail ==== */
"use strict";

var M = require("../src/core_matrix.js");

var pass = 0, fail = 0;
function approx(a, b, tol) {
  if (a === b) { return true; }
  if (typeof a !== "number" || typeof b !== "number") { return false; }
  if (a !== a || b !== b) { return false; }              /* NaN 不相等 */
  return Math.abs(a - b) <= (tol === undefined ? 1e-9 : tol);
}
function ok(name, cond, detail) {
  if (cond) { pass++; }
  else { fail++; console.log("FAIL " + name + (detail !== undefined ? ": " + detail : "")); }
}

/* data 按行主序展开；仅测试期使用，不在被测热路径 */
function setAMat(rows, cols, data) {
  var i;
  for (i = 0; i < rows * cols; i++) {
    if (M.setA(rows, cols, i, data[i]) !== 0) { return -1; }
  }
  return 0;
}
function setBMat(rows, cols, data) {
  var i;
  for (i = 0; i < rows * cols; i++) {
    if (M.setB(rows, cols, i, data[i]) !== 0) { return -1; }
  }
  return 0;
}

/* 乘法 */
setAMat(2, 2, [1, 2, 3, 4]);
setBMat(2, 2, [5, 6, 7, 8]);
ok("2x2 乘法状态", M.mul() === 1 && M.getError() === 0);
ok("2x2 乘法 R[0][0]=19", approx(M.getR(0, 0), 19), M.getR(0, 0));
ok("2x2 乘法 R[0][1]=22", approx(M.getR(0, 1), 22));
ok("2x2 乘法 R[1][0]=43", approx(M.getR(1, 0), 43));
ok("2x2 乘法 R[1][1]=50", approx(M.getR(1, 1), 50));

setAMat(2, 3, [1, 2, 3, 4, 5, 6]);
setBMat(3, 2, [7, 8, 9, 10, 11, 12]);
ok("2x3·3x2 状态", M.mul() === 1);
ok("2x3·3x2 R[1][1]=154", approx(M.getR(1, 1), 154), M.getR(1, 1));
ok("2x3·3x2 形状 2x2", M.getRRows() === 2 && M.getRCols() === 2);

setAMat(3, 3, [1, 0, 0, 0, 1, 0, 0, 0, 1]);
setBMat(3, 3, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
M.mul();
ok("单位阵 × B = B", approx(M.getR(1, 2), 6) && approx(M.getR(2, 0), 7));

setAMat(2, 3, [1, 2, 3, 4, 5, 6]);
setBMat(2, 2, [1, 2, 3, 4]);
ok("内维不匹配拒绝", M.mul() === -1 && M.getError() === 2);
ok("维度超 3 拒绝", M.setA(4, 4, 0, 1) === -1 && M.getError() === 1);
ok("下标越界拒绝", M.setA(2, 2, 4, 1) === -1 && M.getError() === 1);

/* 转置 */
setAMat(2, 3, [1, 2, 3, 4, 5, 6]);
ok("转置状态", M.transpose() === 1);
ok("转置形状 3x2", M.getRRows() === 3 && M.getRCols() === 2);
ok("转置 R[0][1]=4", approx(M.getR(0, 1), 4), M.getR(0, 1));
ok("转置 R[2][0]=3", approx(M.getR(2, 0), 3), M.getR(2, 0));

setAMat(3, 3, [2, 1, 0, 1, 3, 0, 0, 0, 4]);   /* 对称阵 */
M.transpose();
ok("对称阵转置 = 自身",
   approx(M.getR(0, 1), 1) && approx(M.getR(1, 0), 1) && approx(M.getR(2, 2), 4));

/* 行列式 */
setAMat(1, 1, [7]);
ok("det 1x1 = 7", M.det() === 7);
setAMat(2, 2, [1, 2, 3, 4]);
ok("det 2x2 = -2", M.det() === -2, M.det());
setAMat(3, 3, [6, 1, 1, 4, -2, 5, 2, 8, 7]);
ok("det 3x3 = -306", approx(M.det(), -306), M.det());
setAMat(3, 3, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
ok("det 奇异阵 = 0", M.det() === 0, M.det());
setAMat(2, 3, [1, 2, 3, 4, 5, 6]);
ok("det 非方阵 → NaN + err 3", M.det() !== M.det() && M.getError() === 3);

/* 逆矩阵 */
setAMat(2, 2, [4, 7, 2, 6]);
ok("inv 2x2 状态", M.inv() === 1 && M.getError() === 0);
ok("inv 2x2 R[0][0]=0.6", approx(M.getR(0, 0), 0.6), M.getR(0, 0));
ok("inv 2x2 R[0][1]=-0.7", approx(M.getR(0, 1), -0.7));
ok("inv 2x2 R[1][1]=0.4", approx(M.getR(1, 1), 0.4));

setAMat(3, 3, [2, 0, 0, 0, 4, 0, 0, 0, 8]);
M.inv();
ok("inv 3x3 对角 R[1][1]=0.25", approx(M.getR(1, 1), 0.25) &&
   approx(M.getR(2, 2), 0.125), M.getR(2, 2));

/* 一般 3x3：inv 后把 R 逐元素搬回 B 再乘，验证 A·A⁻¹ ≈ I */
setAMat(3, 3, [3, 0, 2, 0, 1, 4, 5, 1, 2]);
M.inv();
var invOK = M.getError() === 0;
var i;
for (i = 0; i < 9; i++) { if (M.setB(3, 3, i, M.getR((i / 3) | 0, i % 3)) !== 0) { invOK = false; } }
setAMat(3, 3, [3, 0, 2, 0, 1, 4, 5, 1, 2]);
M.mul();
ok("inv 3x3 一般阵状态", invOK && M.getError() === 0);
ok("A·A⁻¹ 对角 ≈ 1",
   approx(M.getR(0, 0), 1, 1e-9) && approx(M.getR(1, 1), 1, 1e-9) &&
   approx(M.getR(2, 2), 1, 1e-9),
   M.getR(0, 0) + "/" + M.getR(1, 1) + "/" + M.getR(2, 2));
ok("A·A⁻¹ 非对角 ≈ 0",
   approx(M.getR(0, 1), 0, 1e-9) && approx(M.getR(2, 0), 0, 1e-9));

setAMat(3, 3, [1, 2, 3, 4, 5, 6, 7, 8, 9]);
ok("inv 奇异拒绝", M.inv() === -1 && M.getError() === 4);
setAMat(2, 3, [1, 2, 3, 4, 5, 6]);
ok("inv 非方阵拒绝", M.inv() === -1 && M.getError() === 3);

/* 寄存器独立性与边界 */
setAMat(2, 2, [1, 2, 3, 4]);
M.det();
setBMat(2, 2, [0, 0, 0, 0]);
ok("det 与 B 寄存器无关", M.getLastDet() === -2, M.getLastDet());
ok("getR 越界 → NaN", M.getR(3, 0) !== M.getR(3, 0) && M.getR(-1, 0) !== M.getR(-1, 0));
M.reset();
ok("reset 后维度清零", M.getRRows() === 0 && M.getError() === 0);
ok("reset 后 mul 无效输入拒绝", M.mul() === -1 && M.getError() === 1);

/* 零 GC：加载期 3 个 TypedArray，热循环分配计数不变 */
ok("加载期分配 = 3（3 个 Float64Array）", M.allocCount() === 3, M.allocCount());
ok("池为 TypedArray", M.aBuffer() instanceof Float64Array &&
   M.rBuffer() instanceof Float64Array && M.rBuffer().length === 9);
var allocBefore = M.allocCount();
var di;
for (di = 0; di < 5000; di++) {
  M.setA(2, 2, 0, 1); M.setA(2, 2, 1, 2); M.setA(2, 2, 2, 3); M.setA(2, 2, 3, 4);
  M.setB(2, 2, 0, 5); M.setB(2, 2, 1, 6); M.setB(2, 2, 2, 7); M.setB(2, 2, 3, 8);
  M.mul();
  M.transpose();
  M.det();
  M.inv();
}
ok("热循环零分配", M.allocCount() === allocBefore,
   "delta=" + (M.allocCount() - allocBefore));

console.log("==== matrix: " + pass + " pass, " + fail + " fail ====");
if (fail > 0) { process.exit(1); }
