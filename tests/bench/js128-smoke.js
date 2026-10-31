/* ApolloCalc · js128-smoke.js — JS 128KB 侧冒烟 CSV

   与 C/128KB/test/test_smoke_csv.c 同一组用例、同一 CSV 三段式格式、
   同一条流水线（reset → parse → preCheck → guardDegree → extract → solve；
   线性组：reset → setMatrixElement → solveLinearSystem）。
   内核：动态 import ESM 的 JS/128KB/src/core_kernel.js（只读，禁止修改 JS 目录）。
   运行：node tests/bench/js128-smoke.js > tests/bench/js128-smoke.csv
   精确形式为五分量（valid,rat,rad_num,rad,den,exp），与 C 128KB 口径一致；
   这与 64KB 版的三分量（valid,num,den,rad）不同，故本脚本独立于 js-smoke.js。
   输出精度：String(x) 最短往返（信息量 ≥ %.17g），diff 脚本按数值比对。
*/
"use strict";
const path = require("path");
const { pathToFileURL } = require("url");

const KERNEL = path.join(__dirname, "..", "..", "JS", "128KB", "src", "core_kernel.js");

/* 用例表：与 C/128KB/test/test_smoke_csv.c 的 POLY / LIN / PRE / EXACT 逐行一致 */
const POLY = [
  ["quad_56", "x^2-5x+6=0", 2],
  ["quad_cplx", "x^2+1=0", 2],
  ["quad_rhs9", "x^2=9", 2],
  ["quad_dup", "x^2-4x+4=0", 2],
  ["quad_frac", "2x^2+3x-2=0", 2],
  ["quad_cplx5", "x^2+2x+5=0", 2],
  ["quad_352", "3x^2-5x+2=0", 2],
  ["quad_12", "x^2+x-12=0", 2],
  ["quad_cplx13", "x^2-4x+13=0", 2],
  ["quad_irr2x", "x^2-2x-1=0", 2],
  ["quad_rhs2x", "x^2=2x+3", 2],
  ["quad_degen", "x^2=0", 2],
  ["quad_cplx2", "x^2+2x+2=0", 2],
  ["quad_gold", "x^2-x-1=0", 2],
  ["cubic_123", "x^3-6x^2+11x-6=0", 3],
  ["cubic_8", "x^3=8", 3],
  ["cubic_trip", "x^3-6x^2+12x-8=0", 3],
  ["cubic_irr", "x^3-3x=0", 3],
  ["cubic_cardano", "2x^3-x-1=0", 3],
  ["cubic_1", "x^3-1=0", 3],
  ["cubic_1111", "x^3+x^2+x+1=0", 3],
  ["quart_biquad", "x^4-5x^2+4=0", 4],
  ["quart_1", "x^4-1=0", 4],
  ["quart_1234", "x^4-10x^3+35x^2-50x+24=0", 4],
  ["quart_cplx", "x^4+5x^2+4=0", 4],
  ["quart_general", "2x^4-x^3-14x^2-5x+6=0", 4],
  ["quart_quad4", "x^4-4x^3+6x^2-4x+1=0", 4],
  ["quart_4", "x^4+4=0", 4],
  ["quart_deleg", "x^3-6x^2+11x-6=0", 4],
  ["quintic_gate", "x^5-1=0", 5]
];

const LIN = [
  ["lin2_ok", 2, [2, 3, 8, 1, -1, -1]],
  ["lin3_ok", 3, [1, 1, 1, 6, 1, 2, 3, 14, 2, 3, -1, 5]],
  ["lin6_ok", 6, [1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 2, 0, 0, 1, 0, 0, 0, 3,
                  0, 0, 0, 1, 0, 0, 4, 0, 0, 0, 0, 1, 0, 5, 0, 0, 0, 0, 0, 1, 6]],
  ["lin2_sing", 2, [1, 1, 1, 1, 1, 2]]
];

const PRE = [
  ["pre_depth", "(((((((x)))))))"],
  ["pre_funcs", "sin(x)+cos(x)+tan(x)+sin(x)+cos(x)"],
  ["pre_vars", "x+x+x+x+x+x+x+x+x+x+x"]
];

const EXACT = [
  ["exact_56", "x^2-5x+6=0"],
  ["exact_2", "x^2-2=0"],
  ["exact_3", "x^2-3=0"],
  ["exact_5", "x^2-5=0"],
  ["exact_9", "x^2=9"],
  ["exact_49", "x^2-49=0"],
  ["exact_dup4", "x^2-4x+4=0"],
  ["exact_frac", "2x^2+3x-2=0"],
  ["exact_352", "3x^2-5x+2=0"],
  ["exact_41", "4x^2-1=0"],
  ["exact_6x1", "6x^2-x-1=0"],
  ["exact_2x", "x^2+2x=0"],
  ["exact_gold", "x^2-x-1=0"]
];

/* -0 归一为 0（与 C 侧 norm0 对齐） */
function fmt(x) { return x === 0 ? "0" : String(x); }

function runPoly(k, expr, deg) {
  let err;
  k.solversReset();
  k.parseExpression(expr);
  if (k.preCheckExpression() === 1) {
    err = k.getSolveError();
  } else if (k.guardDegree(deg) === 1) {
    err = k.getSolveError();
  } else {
    const ee = k.extractCoefficients(deg);
    if (ee !== 0) {
      err = ee;
    } else {
      if (deg === 2) { k.solveQuadratic(); }
      else if (deg === 3) { k.solveCubic(0); }
      else { k.solveQuartic(); }
      err = k.getSolveError();
    }
  }
  return err;
}

function printSolveRow(k, name, err) {
  const count = k.getRootCount();
  const cells = ["solve", name, String(err), String(count)];
  for (let i = 0; i < 6; i++) {
    if (i < count) {
      cells.push(fmt(k.getRootRe(i)), fmt(k.getRootIm(i)));
    } else {
      cells.push("0", "0");
    }
  }
  console.log(cells.join(","));
}

/* 五分量精确形式：valid,rat,rad_num,rad,den,exp（C 128KB 口径） */
function printExactRow(k, name, err) {
  const count = k.getRootCount();
  const cells = ["exact", name, String(err)];
  for (let s = 0; s < 2; s++) {
    if (s < count && k.getRootExactValid(s) === 1) {
      cells.push("1",
                 String(k.getRootExactRat(s)),
                 String(k.getRootExactRadNum(s)),
                 String(k.getRootExactRad(s)),
                 String(k.getRootExactDen(s)),
                 String(k.getRootExactExp(s)));
    } else {
      cells.push("0", "0", "0", "0", "0", "0");
    }
  }
  console.log(cells.join(","));
}

function main(k) {
  console.log("stage,name,err,count,re0,im0,re1,im1,re2,im2,re3,im3,re4,im4,re5,im5");
  for (const [name, expr, deg] of POLY) {
    const err = runPoly(k, expr, deg);
    printSolveRow(k, name, err);
  }
  for (const [name, n, aug] of LIN) {
    k.solversReset();
    for (let i = 0; i < n * (n + 1); i++) { k.setMatrixElement(i, aug[i]); }
    k.solveLinearSystem(n);
    printSolveRow(k, name, k.getSolveError());
  }

  console.log("precheck,name,err");
  for (const [name, expr] of PRE) {
    k.solversReset();
    k.parseExpression(expr);
    const err = k.preCheckExpression() === 1 ? k.getSolveError() : 0;
    console.log("precheck," + name + "," + err);
  }

  console.log("exact,name,err,valid0,rat0,rn0,rad0,den0,e0,valid1,rat1,rn1,rad1,den1,e1");
  for (const [name, expr] of EXACT) {
    const err = runPoly(k, expr, 2);
    printExactRow(k, name, err);
  }
}

import(pathToFileURL(KERNEL)).then((m) => {
  main(m.default);
}).catch((e) => {
  console.error("[FATAL] cannot load kernel:", e.message);
  process.exit(1);
});
