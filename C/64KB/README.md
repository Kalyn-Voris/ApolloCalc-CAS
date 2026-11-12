# ApolloCalc-CAS · C99 64KB

[English] A C99 implementation of the JS 64KB core, targeting Cortex-M4F
and compatible MCUs (Cortex-M0+ / ESP32). Zero malloc, zero recursion,
static memory only. Design doc: [docs/c99-64kb.md](../../docs/c99-64kb.md).

**状态：已完成（v1.0.0）**（9 轮内核 + 全量回归与跨版本验收，2026-10-12）。

C 64KB 的功能范围按 `docs/features.md` 的 C 64KB 列实现（不是严格对等 JS 64KB），分两批：
- **第一批**（对等 JS 64KB 核心）：解析器、求值器、方程求解、三分量精确形式、基础数学函数、复数基础
- **第二批**（C 独有优势）：三角函数查表、进制转换、分数显示、质因数分解、CRC32/SHA-256/Base64、定点数、int64 精度

**不做**：五分量混合形式、双曲函数、复数 cExp/cLn/cPowReal、数值微积分、SI 前缀、矩阵乘法/行列式/特征值、MD5/AES/RSA。

## Zero-GC 铁律

[English] No malloc/free/calloc/realloc, no VLA, no recursion, no hot-path
sprintf/printf. All memory statically allocated; all module-private globals
`static`. Verified at link time via `-Wl,--wrap=malloc --wrap=free`: any heap
call from `main()` entry onward aborts (`__wrap_malloc` / `__wrap_free`);
pre-main host-CRT calls (crt2.o/tlssup.o, absent on bare metal) are forwarded
to the host allocator so the binary contains no static malloc/free symbol.

禁 malloc / VLA / 递归 / 热路径字符串函数；全部内存静态分配。链接期 `--wrap` 验证：
main 入口之后任何 malloc/free 立即 abort（内核 + 测试全程零堆）；main 之前的宿主 CRT
启动调用（裸机不存在）转发给宿主分配器，二进制中无静态 malloc/free 符号
（达标口径：`nm test_runner.exe | grep -iE "malloc|free|realloc" | grep -v __wrap` 无命中）。

## 精度模型

[English] Exact-form three components use `int64_t` (19 decimal digits) —
higher precision than the JS 64KB 53-bit double model. Out-of-range values
downgrade to double with an exact-form-invalid flag. True arbitrary
precision (100+ digits) is reserved for C 128KB.

三分量精确形式用 `int64_t`（19 位十进制），超出范围降级 double 并置失效标志；任意精度（100+ 位）留给 C 128KB。

## 构建

```bash
cd F:/ApolloCalc-CAS/C/64KB
mingw32-make                # test_runner（全量校验测试）
./test_runner.exe           # 9 段测试 + 零堆分配验证，期望 565 pass 0 fail
mingw32-make LUT=1          # 三角查表构建（578 pass：math 段多 13 项 LUT 校验）
mingw32-make smoke_runner   # 冒烟 CSV 输出器（跨版本比对用，见下）
./smoke_runner.exe > ../../tests/bench/c-smoke.csv
```

编译选项：`-std=c99 -Wall -Wextra -Wstrict-prototypes -Werror -O2`，编译零警告是硬约束。

## Token 编码（与 JS 版一致）

`int32_t w = (type << 8) | (payload & 0xFF)`，`w == 0` 即 T_END 哨兵；
访问器 `ac_token_type(w)` / `ac_token_payload(w)`，构造 `ac_token_make(type, payload)`。

## 目录结构

```
C/64KB/
├── README.md
├── Makefile                 # test_runner / smoke_runner(LUT=1 开关) / clean
├── VERSION                  # v1.0.0
├── src/
│   ├── ac_types.h           # 类型、常量、Token 编码、池尺寸
│   ├── ac_parser.c/h        # 第 1 轮
│   ├── ac_evaluator.c/h     # 第 2 轮
│   ├── ac_exact.c/h         # 第 3 轮
│   ├── ac_solvers.c/h       # 第 4 轮
│   ├── ac_math.c/h          # 第 5 轮（含三角函数查表开关）
│   ├── ac_complex.c/h       # 第 6 轮
│   ├── ac_numtheory.c/h     # 第 7 轮（C 独有）
│   ├── ac_base.c/h          # 第 8 轮（C 独有：进制 + 分数显示）
│   └── ac_util.c/h          # 第 9 轮（C 独有：CRC32/SHA-256/Base64/定点数）
└── test/
    ├── test_main.c          # 入口 + __wrap_malloc/free（内核期 abort / CRT 启动期转发）
    ├── test_parser_smoke.c  # 第 1 轮
    ├── test_evaluator.c     # 第 2 轮
    ├── test_exact.c         # 第 3 轮
    ├── test_solvers.c       # 第 4 轮
    ├── test_math.c          # 第 5 轮
    ├── test_complex.c       # 第 6 轮
    ├── test_numtheory.c     # 第 7 轮
    ├── test_base.c          # 第 8 轮
    ├── test_util.c          # 第 9 轮
    ├── test_smoke_csv.c     # 第 10 轮：冒烟 CSV（独立 main，smoke_runner target）
    ├── test_exact_xlang.js  # 跨语言驱动（JS/64KB 基准，只读）
    ├── test_solvers_xlang.js
    ├── test_math_xlang.js
    └── test_complex_xlang.js
```

## 完整测试统计

| 模块 | 测试数 | 默认构建 | LUT=1 |
|:---|---:|:---|:---|
| parser smoke | 15 | ✅ | ✅ |
| evaluator | 53 | ✅ | ✅ |
| exact（三分量） | 76 | ✅ | ✅ |
| solvers | 61 | ✅ | ✅ |
| math | 54 / 67 | ✅ | ✅（LUT 段多 13 项） |
| complex | 47 | ✅ | ✅ |
| numtheory（C 独有） | 123 | ✅ | ✅ |
| base（C 独有） | 73 | ✅ | ✅ |
| util（C 独有） | 63 | ✅ | ✅ |
| **合计** | | **565 pass, 0 fail** | **578 pass, 0 fail** |

零堆验证：默认与 LUT=1 两种构建下 `nm test_runner.exe | grep -iE "malloc|free|realloc" | grep -v __wrap`
均无命中；运行期由 `__wrap_malloc/free` 双保险（main 期间任何堆调用 abort）。

## 跨版本一致性

- **跨语言用例**（node test/test_*_xlang.js，基准为 JS/64KB/src，只读）：
  XL01-XL12（精确形式，num/den/rad/valid 全一致）、XL01-XL19（求解，6 槽根 re/im 1e-9 内一致）、
  XL-M01-XL-M15（数学函数，1e-9）、XL-C01-XL-C12（复数，1e-9）
- **全量冒烟比对**（第 10 轮）：`tests/polynomials.json` / `exact-forms.json` / `edge-cases.json`
  转录 50 例（30 多项式 + 4 线性组 + 3 门禁 + 13 精确形式），C 侧 `smoke_runner` 与
  JS 侧 `tests/bench/js-smoke.js` 输出同格式 CSV，`tests/bench/diff-smoke.js` 比对：
  **50/50 用例一致，0 处数值差异 > 1e-9**（报告见 tests/bench/SMOKE-DIFF.md；
  唯一非零差异为 cubic_irr 的 ε 级中根 4.04e-21，源于 C cbrt() 与 JS pow(x,1/3) 的 ulp 级差异）。

## 与 JS 64KB 的对等验证

每轮完成后，用相同输入对 C 和 JS 两侧跑，比对：Token 流一致（数量、类型、payload）、
数值结果一致（浮点容差 1e-9）、精确形式分量一致（num/den/rad/valid）。

## 开发状态（9 + 1 轮）

- [x] 骨架搭建 + ac_parser.c
- [x] ac_evaluator.c
- [x] ac_exact.c（三分量，int64_t）
- [x] ac_solvers.c
- [x] ac_math.c（含三角函数查表，AC_MATH_USE_LUT 默认关）
- [x] ac_complex.c
- [x] ac_numtheory.c（C 独有）
- [x] ac_base.c（C 独有）
- [x] ac_util.c（C 独有）
- [x] 全量回归对齐 tests/（第 10 轮：冒烟 CSV + 跨版本 diff + 文档收尾）
