# WASM 前景调研存档（F5）

> **状态**：v2.5.0 功能 F5 的结项存档。v3 产品侧裁决（2026-11-03，
> ROADMAP-v2.5-v2.6.md 修订记录 v3 / §6.2 R5）**拒绝引入 LLVM/clang 工具链**——
> 项目定位极致轻量、零外部重型依赖，纯 C 与纯 JS 双通道已满足「拉取源码即用」；
> F5 由「WASM 编译产物 spike」降级为**纯文档调研**，WASM / npm / TypeScript /
> API 文档主题（原 G1 四件套）**整体顺延 v2.7+**。
>
> **本文边界**：只存档调研结论与复用数据。不做任何工具链安装、不做编译实验、
> 不改任何 C/JS 源码；C/JS 源码与全部账本零触碰。若产品侧未来翻案重启 WASM，
> 按本文第 2–5 节的存档数据直接进入工具链 spike，不必重做调研。
>
> **数字溯源标注**（沿用 ROADMAP-v2.5-v2.6.md §前言 口径）：
> 【实测】= 本文成文时在本机直接执行所得；【账本·调研】= 转引自
> ROADMAP-v2.5-v2.6.md §F5/§G1 记录的调研时点实测；【估算】= 外推值，
> 启动 spike 时必须以实测定数，不得作为承诺。

---

## 1. 裁决成文（为什么不做的当时理由）

1. **零外部重型依赖是项目定位的一部分**。交付物形态为「纯 C 源码 + 纯 JS
   单文件内核」，开发者拉取源码即用（C 侧只需任意 C99 编译器，JS 侧零依赖）；
   引入 LLVM/clang 会把「可复现构建」从约定变成环境要求。
2. **双通道已覆盖目标场景**。嵌入式走 native C（Keil / ESP-IDF 移植指南见
   [getting-started.md](getting-started.md)），浏览器/Node 走 JS 内核
   （`core_kernel.js`）；WASM 通道属于「第三分发形态」，非能力缺口。
3. **JS 大整数移植转正**（v2.6 G4'）补掉了当时 wasm 通道的最大说服点
   （C 独有 bigint 面）；通道级替代 ≠ 能力对等（路线图 §6.2 R8 口径成文）。

## 2. 工具链现状实测（2026-11-04 复核【实测】）

| 检查项 | 命令 | 结果 |
|---|---|---|
| clang 是否在 PATH | `command -v clang clang-cl emcc gcc` | 仅 `/d/MinGw64/bin/gcc`，**无 clang / clang-cl / emcc** |
| MinGW 目录内 clang 二进制 | `ls /d/MinGw64/bin \| grep -i clang` | 0 个命中（仅 gcc.exe + binutils） |
| gcc 版本 | `gcc --version` | **gcc 8.1.0**（x86_64-posix-seh-rev0, MinGW-W64 project） |
| gcc wasm 目标后端 | `gcc -print-targets \| grep wasm` | **0 命中**（8.1.0 无 wasm32 后端，无法 `- target` 改造） |
| MSYS2 环境（ucrt64 / clang64） | `ls /c/msys64` | 目录不存在（本机为独立 MinGW-W64，非 MSYS2） |

与调研时点结论一致【账本·调研：本机 PATH/mingw64/ucrt64/clang64 均无 clang】；
即 **本机现状 = 无法产出 wasm 产物，且 gcc 侧无改造路径**。

**未来启动所需的安装/构建路径**（届时二选一，均为【估算】外的标准路径）：

- **clang 直配**：安装 LLVM/clang（wasm32-unknown-unknown 是默认启用的
  目标，无需自编后端），配 `--target=wasm32 -nostdlib -Wl,--no-entry` +
  `-Wl,--export-dynamic`；链接器为 lld（wasm-ld，随 LLVM 一起）。
- **emsdk 路线**：安装 Emscripten（emcc），自带 sysroot 与 libc 调度，但
  默认引入 JS glue 与更多内置假设——与「零 import、freestanding」架构目标
  相性更差，届时若走此路线需先评估裁剪。

## 3. freestanding 垫层清单（spike 时逐项转正）

clang 以 freestanding 方式编 C 时，编译器会把若干惯用法**折叠成对运行时
函数的调用**；`-nostdlib` 下链接期即失败。垫层最小集（调研口径【账本·调研】，
含评审 6 增补的 ctype/strtod 类）：

| 类别 | 函数 | 原因 |
|---|---|---|
| mem\* | `memcpy` / `memmove` / `memset` / `memcmp` | LLVM 把定长拷贝/清零/比较惯用法折叠为内置调用 |
| str\* | `strlen` / `strcmp` / `strncmp` 等内核实际引用者 | 同上；以 spike 期 `nm` 缺符号清单为准逐个补 |
| libm | `exp` / `log` / `pow` / `tan` / `asin` / `acos` / `atan` / `atan2` | wasm 内不自带 libm；建议 musl 系实现逐个搬入（`sin`/`cos` 内核侧已有查表常量自持，v2.0.0 LUT 迁 `.rdata` 先例） |
| ctype/strtod | 字符分类与十进制转换最小集 | 评审 6 增补；内核解析器自持词法，预计仅格式化侧需要 |
| printf 子集 | `%.15g` / `%d` / `%s` 最小格式化器 | smoke/diff 管线按文本比对输出；`%.15g` 跨编译器一致性是已知风险（路线图 §6.2 R4） |

垫层纪律（沿用仓库既有红线）：零 malloc、零递归、全部静态池——
与 CONTRIBUTING.md 的 C 侧铁律一致，wasm 目标不得引入例外。

## 4. 体积估算存档

| 时点 | 口径 | .text | wasm 未压缩估算 | gzip 估算 |
|---|---|---:|---:|---:|
| 调研时点（v2.4.3 期） | 全内核 **33 TU** | **204 764 B**【账本·调研，`size` 统计】 | 220–300 KB【估算】 | ~1/3【估算】 |
| **本文复核（v2.5.0，2026-11-04【实测】）** | `size obj/src/*.o` 全部 **36 TU** 求和 | **235 492 B** | **≈253–345 KB**（按 235492/204764 = 1.150 线性外推【估算】） | ≈84–115 KB【估算】 |

读法与警告：

- `.text` 只是 wasm 体积的一个输入项（wasm 节结构有开销、x86 函数序言被
  削减、垫层另加），上表数字**只能当量级参考**，实数以 spike 为准。
- v2.5.0 新增三个 TU（complex_ext / sysolve / sturm）使 .text 增长
  **+30 728 B**——若翻案重启，G1 的「全内核口径文件线」须按当时的实数重定，
  路线图 §G1 存档的「上游 16 模块口径 ≤131 072 B」作为保守下界仍成立。

## 5. 目标架构与验收线方案存档（原 G1 差分方案要点）

**零 import 模块架构**（【账本·调研】成文口径，届时按 `docs/contracts/
wasm-abi-v26.md` 先冻结再实现）：

- wasm 模块**零 import 节**：线性内存静态池自持（对应 C 侧零 malloc 账本），
  全部能力经导出函数暴露（f64/i32 标量进出）；字符串走双缓冲 ABI
  （TextEncoder.encodeInto 零 GC 进出，契约逐项成文）。
- 目录形态：`wasm/`（与 C/128KB-ProTEXT 平级），沿用「上游零改动包装 TU」
  架构——`C/128KB/` 冻结红线与 verify.ps1 §5 git 双证**不受影响**；
  C 侧 `.bss` 四口径 == 断言不动（wasm 是独立目录独立对象，每 static
  +32 B 规则不触发）。
- **`.wasm` 产物不进 git**（构建产物，200–300 KB 级）；可复现性由
  `wasm/build.sh` + CI 保证。npm 包以发布现场打包（`dist-npm/` 暂存目录）
  为准，`exports` 在包内自洽（路线图 §G1 存档原文有完整 package.json 草案）。

**验收线（三层差分，全部进发版门禁）**：

1. **native C vs wasm C**（同源码，最严）：err/count 精确 + 数值 ≤1e-9 +
   bigint/精确形式整数域全等；字符串字段单列宽松比，隔离 `%.15g`
   跨编译器差异。
2. **wasm vs JS 128KB**：复用 `tests/bench/diff-smoke128.js` 的 **50 用例**
   语料与判定口径（solve err/count 精确 + 12 数值 ≤1e-9 + exact 五分量
   精确）——既有 50 例管线一行不动。
3. **tests/ 共享契约 JSON 零增改**：wasm 首期只暴露与 JS128 已有 API
   同语义的面，不新开功能（CONTRIBUTING.md 契约同步纪律）。

附加验收关：`tests/wasm/wasm-smoke.js`（50 用例复用 c128-smoke.csv 语料 +
10–20 条 ABI/边界专项 + 构建层断言：零 import 节、文件线、双目标链接）；
落 `tests/wasm/` 而非 JS/128KB/test/，与发版三关解耦（无 clang 机器照常可跑
既有三关）。

## 6. 顺延裁决与翻案条件

**顺延范围（v3 裁决成文）**：G1 四件套（WASM 全内核交付 + npm 发布 +
TypeScript 声明 + 最终版 API 文档）整体顺延 **v2.7+**；npm 包版本届时与
全局 Tag 对齐（双轨制）。F5 本身以本文存档**结项**，不进 v2.6 范围
（v2.6 主题 = §6.2 R5 预案 B：G2 SVD 双端核心攻坚 + G3 基准终版 +
G4' 纯 JS 大整数移植）。

**翻案条件**（满足其一即建议产品侧重估，届时按 §6.2 R5 重排 v2.7+ 计划）：

1. **定位修订**：产品侧修订「零外部重型依赖」约束，或改由 CI / 发布基建
   统一承担工具链（开发者本机与仓库均不装 clang，产物可复现）——工具链
   成本由基建摊薄后，第 1 节理由 1 不再成立。
2. **G4' spike no-go**：v2.6 G4' 纯 JS 大整数的内核字节预算 spike 对照
   熔断线（126 976 B）判 no-go，且存量 C 独有六块（bigint / bigdec /
   sparse / rational / integrate_multi / special 完整组）被认定必须以某种
   形态补上——wasm 是当时唯一不写 JS 实现的替代通道（注意 R8 口径：
   属通道级替代，宣传上不得称「JS 原生对等」）。
3. **明确的 wasm 消费场景**：出现 native C（无解释器）与 JS（需运行时）
   双通道都无法覆盖的分发需求（如同一产物同时服务浏览器与无 JS 宿主的
   边缘运行时），且该需求对发布是阻塞项。

**无论是否翻案均不变的约束**：上游 `C/128KB/` 冻结零改动；C 侧 `.bss`
四口径 == 断言不动；零 import 节目标；`.wasm` 不进 git；发版三关与
wasm 门禁解耦；所有【估算】数字在 spike 期以实测定数并写进 PR。
