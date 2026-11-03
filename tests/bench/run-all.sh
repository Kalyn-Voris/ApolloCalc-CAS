#!/bin/bash
# ApolloCalc-CAS · 一键跑全部版本 100 万次压测
# 用法：bash run-all.sh        （Windows 上 bash 不在 PATH 时：F:/Git/bin/bash.exe run-all.sh）
# 产物：result-js64-1M.csv / result-js128-1M.csv / result-c64-1M.csv / result-c128-1M.csv /
#       result-pro-1M.csv（v2.6.0 G3 新增 pro_runner 列：C/128KB-ProTEXT 内核，
#       经 ProTEXT Makefile 的 probench 目标构建 pro_bench.exe）
#       summary-ratio.csv（跨版本耗时比值，基线 = JS 64KB 的 us_per_op）
#
# 池：pool.json（650 条，gen_pool.py 生成）；C 端读 pool_c.h（gen_pool_c.py 生成）
# 迭代：每 stage 独立 100 万次（pool.iterations），3 轮取中位数
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT/tests/bench"

HAVE_NODE=0
if command -v node >/dev/null 2>&1; then HAVE_NODE=1; fi

# make 在部分 Windows 环境叫 mingw32-make（MinGW），双探测
MAKE_BIN=""
if command -v make >/dev/null 2>&1; then MAKE_BIN=make
elif command -v mingw32-make >/dev/null 2>&1; then MAKE_BIN=mingw32-make
fi
HAVE_GCC=0
if command -v gcc >/dev/null 2>&1; then HAVE_GCC=1; fi

# 池是生成产物：C 端头文件若缺失，先用 Python 生成（存在则跳过，避免覆盖手工改动）
if [ ! -f pool_c.h ] && [ -f pool.json ]; then
  if command -v python >/dev/null 2>&1; then
    python gen_pool_c.py && echo "  → pool_c.h 已生成"
  else
    echo "  (警告：pool_c.h 缺失且 python 不可用，C 端将编译失败)"
  fi
fi

echo "=== JS 64KB ==="
if [ "$HAVE_NODE" = 1 ] && [ -f "../../JS/64KB/src/core_kernel.js" ]; then
  node --expose-gc bench-js.js ../../JS/64KB/src/core_kernel.js > result-js64-1M.csv
  echo "  → result-js64-1M.csv"
else
  echo "  (跳过：node 缺失，或 JS/64KB/src/core_kernel.js 未构建 —— 先跑 node test/build-kernel.js)"
fi

echo "=== JS 128KB ==="
if [ "$HAVE_NODE" = 1 ] && [ -f "../../JS/128KB/src/core_kernel.js" ]; then
  node --expose-gc bench-js.js ../../JS/128KB/src/core_kernel.js > result-js128-1M.csv
  echo "  → result-js128-1M.csv"
else
  echo "  (跳过：node 缺失，或 JS/128KB/src/core_kernel.js 未构建 —— 先跑 node test/build-kernel.js)"
fi

echo "=== C 64KB ==="
if [ -z "$(ls ../../C/64KB/src/*.c 2>/dev/null)" ]; then
  echo "  (跳过：C 内核未实现 —— C/64KB/src 下无 .c 源文件)"
elif [ -z "$MAKE_BIN" ] || [ "$HAVE_GCC" = 0 ]; then
  echo "  (跳过：make 或 gcc 不可用)"
else
  rm -f result-c64-1M.csv
  if "$MAKE_BIN" -f bench-c-makefile KERNEL=../../C/64KB/src clean >/dev/null 2>&1 &&
     "$MAKE_BIN" -f bench-c-makefile KERNEL=../../C/64KB/src &&
     { [ -f bench-c.exe ] && ./bench-c.exe || ./bench-c; } > result-c64-1M.csv; then
    echo "  → result-c64-1M.csv"
  else
    echo "  (失败：C/64KB 编译或运行未通过 —— 内核可能只落地了部分轮次)"
  fi
fi

echo "=== C 128KB ==="
if [ -z "$(ls ../../C/128KB/src/*.c 2>/dev/null)" ]; then
  echo "  (跳过：C 内核未实现 —— C/128KB/src 下无 .c 源文件)"
elif [ -z "$MAKE_BIN" ] || [ "$HAVE_GCC" = 0 ]; then
  echo "  (跳过：make 或 gcc 不可用)"
else
  rm -f result-c128-1M.csv
  if "$MAKE_BIN" -f bench-c-makefile KERNEL=../../C/128KB/src clean >/dev/null 2>&1 &&
     "$MAKE_BIN" -f bench-c-makefile KERNEL=../../C/128KB/src &&
     { [ -f bench-c.exe ] && ./bench-c.exe || ./bench-c; } > result-c128-1M.csv; then
    echo "  → result-c128-1M.csv"
  else
    echo "  (失败：C/128KB 编译或运行未通过)"
  fi
fi

echo "=== C 128KB-ProTEXT (pro_bench, v2.6.0 G3) ==="
if [ ! -f "../../C/128KB-ProTEXT/src/ac_pro.h" ]; then
  echo "  (跳过：C/128KB-ProTEXT 不存在)"
elif [ -z "$MAKE_BIN" ] || [ "$HAVE_GCC" = 0 ]; then
  echo "  (跳过：make 或 gcc 不可用)"
else
  rm -f result-pro-1M.csv
  if "$MAKE_BIN" -C ../../C/128KB-ProTEXT SHELL=cmd.exe probench &&
     ../../C/128KB-ProTEXT/pro_bench.exe > result-pro-1M.csv; then
    echo "  → result-pro-1M.csv"
  else
    echo "  (失败：C/128KB-ProTEXT probench 编译或运行未通过)"
  fi
fi

echo "=== 汇总 ==="
echo "--- 表达式池静态占用（C 端编译期口径） ---"
grep "STATIC" result-c*-1M.csv result-pro-1M.csv 2>/dev/null || echo "  (无 C 结果)"
echo "--- 运行时堆增量（JS 端，--expose-gc 口径，Zero-GC 内核 delta 应≈0） ---"
grep "MEM" result-js*-1M.csv 2>/dev/null || echo "  (无 JS 结果)"
echo "--- 全 stage 汇总行 ---"
grep "SUMMARY" result-*-1M.csv 2>/dev/null || echo "  (无 SUMMARY 行)"
echo "--- CSV 列名一致性 ---"
H64="$(grep -m1 '^stage,' result-js64-1M.csv 2>/dev/null)"
H128="$(grep -m1 '^stage,' result-js128-1M.csv 2>/dev/null)"
HC="$(grep -m1 '^stage,' result-c64-1M.csv 2>/dev/null)"
HC128="$(grep -m1 '^stage,' result-c128-1M.csv 2>/dev/null)"
HP="$(grep -m1 '^stage,' result-pro-1M.csv 2>/dev/null)"
CONSIST_OK=1
for h in "$H128" "$HC" "$HC128" "$HP"; do
  if [ -n "$h" ] && [ "$h" != "$H64" ]; then CONSIST_OK=0; fi
done
if [ -n "$H64" ] && [ "$CONSIST_OK" = 1 ]; then
  echo "  各列列名一致（pro 列存在时为五列口径）: $H64"
else
  echo "  列名不一致或结果缺失: js64=[$H64] js128=[$H128] c64=[$HC] c128=[$HC128] pro=[$HP]"
fi
echo "--- 跨版本耗时比值（基线 = JS 64KB 的 us_per_op） ---"
if [ "$HAVE_NODE" = 1 ] && [ -f result-js64-1M.csv ]; then
  node -e '
(function () {
"use strict";
var fs = require("fs");
var STAGES = ["parse", "eval", "solve", "exact"];
var vers = [["js64","result-js64-1M.csv"],["js128","result-js128-1M.csv"],
            ["c64","result-c64-1M.csv"],["c128","result-c128-1M.csv"],
            ["pro","result-pro-1M.csv"]];
var data = {}, order = [];
vers.forEach(function (ve) {
  var f = ve[1];
  if (!fs.existsSync(f)) { return; }
  var lines = fs.readFileSync(f, "utf8").split(/\r?\n/);
  lines.forEach(function (ln) {
    var c = ln.split(",");
    if (c.length < 5) { return; }
    if (STAGES.indexOf(c[0]) < 0) { return; }
    /* 按 stage 归并：64KB 与 128KB 的池大小不同（220 vs 250），
       不能用 stage,name 做键，否则 parse/eval 行会跨版本劈成两半 */
    var key = c[0];
    if (!data[key]) { data[key] = {}; order.push(key); }
    var us = parseFloat(c[4]);
    if (!isNaN(us)) {
      data[key][ve[0]] = us;
      data[key][ve[0] + "_pool"] = c[1];
      data[key][ve[0] + "_iter"] = c[2];
    }
  });
});
if (order.length === 0) { console.log("  (无计时行，无法汇总)"); return; }
var head = "stage";
vers.forEach(function (ve) { head += "," + ve[0] + "_us_per_op"; });
vers.slice(1).forEach(function (ve) { head += "," + ve[0] + "_over_js64"; });
head += ",js64_pool,js128_pool,c64_pool,c128_pool,pro_pool";
var out = [head];
order.forEach(function (key) {
  var row = key;
  var base = data[key].js64;
  vers.forEach(function (ve) {
    var v = data[key][ve[0]];
    row += "," + (v === undefined ? "" : v.toFixed(4));
  });
  vers.slice(1).forEach(function (ve) {
    var v = data[key][ve[0]];
    row += "," + (v === undefined || !base ? "" : (v / base).toFixed(2));
  });
  vers.forEach(function (ve) {
    row += "," + (data[key][ve[0] + "_pool"] === undefined ? "" : data[key][ve[0] + "_pool"]);
  });
  out.push(row);
});
fs.writeFileSync("summary-ratio.csv", out.join("\n") + "\n");
out.forEach(function (ln) { console.log("  " + ln); });
})();
'
  echo "  → summary-ratio.csv"
else
  echo "  (跳过：无 node 或缺 result-js64-1M.csv 基线)"
fi
