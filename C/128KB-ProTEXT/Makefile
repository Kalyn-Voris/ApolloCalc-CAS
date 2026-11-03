# ============================================================
# ApolloCalc-CAS · C/128KB-ProTEXT/Makefile — ProTEXT 包装层构建
# ------------------------------------------------------------
# 【设计意图 / Design intent】
#   1) 上游 C/128KB/ 是 v1.1.0 定版，**只读**。Pro 层不复制、不改写
#      上游源码，而是用「两行包装文件」把上游每个 .c 作为独立 TU
#      编译恰好一次：
#          src/ac_pro_<mod>.c  ->  #include "../../../C/128KB/src/ac_<mod>.c"
#      引号 include 以**包含者所在目录**为基准解析：包装文件在
#      C/128KB-ProTEXT/src/，故 ../../../C/128KB/src/ 才指向上游
#      （.. = 128KB-ProTEXT，../.. = C，../../.. = 仓库根）。
#      上游 .c 里的 `#include "ac_xxx.h"` 再以**上游目录**为基准命中，
#      全程无需 -I。
#      注意：下面 UPSTREAM_DIR 写两级是相对**本 Makefile 所在目录**
#      （C/128KB-ProTEXT/）的基准，与包装文件的三级基准不同，别混淆。
#   2) 包装清单由 patsubst 从 wildcard 自动推导：上游新增模块时，
#      本 Makefile 不用改一行（只需补一个同名包装文件）。
#   3) 【绝不能】把 $(UPSTREAM_SRC) 放进链接列表！
#      上游 .c 已经通过包装文件被编译进各自的 TU；若再把上游 .c
#      直接列入 $^，同一批全局符号（g_token_pool、ac_solve_quadratic
#      等）就会出现两份定义 -> 链接期 multiple definition。
#      链接列表里只允许出现：$(SRC)（包装文件 + Pro 增量 + Pro 冒烟）
#      + $(TEST_SRC)。
#      自检命令：nm pro_runner.exe | findstr /C:"ac_solve_quadratic"
#      必须只有一个 T 符号。
#   4) OUTDIR：产物输出目录，默认 `.`（原地构建，正常环境直接用）。
#      某些受限沙箱里源码目录只读，可用
#          mingw32-make OUTDIR=F:/ApolloCalc-CAS/.tmp-build/pro
#      把 .exe 重定向到可写位置；源文件仍从本目录读取，目录结构不变。
#
# 【Pro v2.1.0 的 .bss 三层账本 / Three-tier .bss ledger】
#   内核口径（16 上游模块 + trivial main）      38672 B
#     + g_var_val[10]                            80 B
#     + g_ac_pro_scratch（Pro union，峰值 sparse v2.1.0 ④）
#                                              3848 B
#     + 对齐填充（align_pad[1]）                  8 B
#   ------------------------------------------------
#   = 42608 B   <- 内核口径门禁值（KERNEL_BSS_EXPECT）
#
#   v2.1.0 方向④ 稀疏线性方程组（方案 a：n ≤ 32 / nnz ≤ 320）落地了
#   3848 B 的 sparse 视图，首次推高 Pro union 峰值（mv 2688 → sparse
#   3848，+1160 B）；同时 3928 距 32 的倍数只差 8，对齐填充从 16 B 收窄
#   到 8 B，内核口径净增 +1152 B（41456 → 42608，size 实测）。设计说明
#   （§3 功能④）预估视图 ≈3.9 KB、
#   .bss ≈42684 —— 实测 3848 B / 42608 B，同量级略优。
#   同为 v2.1.0 的方向⑤ 任意精度小数落地 dec 视图 480 B（5 × 96），低于
#   sparse 峰值被 union 完全吸收（对 mv 2688 亦然），账本两值不变。
#   v2.2.0 方向⑥ 2 变量 Groebner 基（M4GB 逐对归约）落地 gb 视图 1888 B
#   （基 8 槽 × 16 项紧凑项表 1152 + 稠密归约工作区 648 + pair 队列 64
#   + 计数/填充 24），≤ 任务书预算 1960 B，低于 sparse 峰值 3848 被 union
#   完全吸收 —— .bss 维持 42608/42640 零增长（size 实测复核）。
#
#   pro_runner.exe 额外带上 test/test_pro_runner.c 这个 TU：
#     + 16 B（该 TU 的 .bss 段：s_kernel_live 等）
#     + 16 B（链接期对齐填充）
#   = 42640 B   <- runner 口径门禁值（PRO_BSS_EXPECT）
#
#   两个数都对，只是口径不同 —— 与上游「38672 内核口径 / 39408 README
#   口径」的区分方式完全一致。账本的 42608 对应内核口径，由
#   `make kernel` + `make bss-check` 一起校验。
#
#   实测命令（本仓库 gcc 8.1.0 / MinGW-w64，size 第 3 列 = bss）：
#     mingw32-make bss-check    -> kernel 42608 / runner 42640 / upstream 39408
# ============================================================

CC      = gcc
# 基础 CFLAGS 与上游 C/128KB/Makefile 逐字一致；-Werror 兜底零警告要求
CFLAGS  = -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror -O2
# Zero-GC 硬性校验：kernel 期任何 malloc/free 命中即 abort（包装函数见 test/）
LDFLAGS = -Wl,--wrap=malloc -Wl,--wrap=free
# 内核口径构建**不**链接 --wrap：那个 main 里没有任何 __wrap_* 定义，
# 而 MinGW 的 crt2.o 会无条件引用它们。.bss 口径与是否 wrap 无关，
# 故内核口径用裸链接；Zero-GC 由 runner（带 --wrap）负责验证。
KERNEL_LDFLAGS =

# ---- Pro 层开关：必须用 += 追加，绝不能写成 CFLAGS = ... 把基础选项冲掉 ----
CFLAGS += -DAC_PRO=1

# 退化构建开关：-DAC_PRO_DEGRADED=1 时 Pro 增量全部编译成空对象，
# 包装层只把上游 16 个 .c 编成 16 个 TU，产物与上游 v1.1.0 同构。
# 用法：mingw32-make DEGRADED=1 kernel
ifeq ($(DEGRADED),1)
CFLAGS += -DAC_PRO_DEGRADED=1
endif

# 上游源码目录（相对本 Makefile 所在目录）
UPSTREAM_DIR := ../../C/128KB/src
UPSTREAM_SRC := $(wildcard $(UPSTREAM_DIR)/ac_*.c)
# 【patsubst 的模式必须吃掉 ac_ 前缀，否则模块名重复】
#   wildcard 返回的是  ../../C/128KB/src/ac_parser.c
#   1) 直接套任务书给的
#        $(patsubst $(UPSTREAM_DIR)/%.c,src/ac_pro_%.c,$(UPSTREAM_SRC))
#      时 % 捕获的是 "ac_parser"（含 ac_），拼出 src/ac_pro_ac_parser.c；
#   2) 只做 notdir 再套 $(patsubst %.c,src/ac_pro_%.c,...) 同理，
#      % 仍捕获 "ac_parser"，结果依然是 src/ac_pro_ac_parser.c。
#      实测报错：No rule to make target 'src/ac_pro_ac_calculus.c'。
#   正确做法：模式写成 ac_%.c，让 % 只捕获模块名 "parser"：
PRO_SRC      := $(patsubst ac_%.c,src/ac_pro_%.c,$(notdir $(UPSTREAM_SRC)))
# Pro 增量 TU：不与任何上游模块同名，故不在 PRO_SRC 的映射里，显式追加。
# 这些文件在 AC_PRO_DEGRADED=1 时整体编译成空对象（零 .bss）。
PRO_EXTRA    := src/ac_pro_extra_globals.c src/ac_pro_solvers_ext.c \
                src/ac_pro_symbolic_ext.c \
                src/ac_pro_multi.c src/ac_pro_resultant.c \
                src/ac_pro_eigen.c src/ac_pro_integrate.c \
                src/ac_pro_diff_multi.c src/ac_pro_integrate_multi.c \
                src/ac_pro_sparse.c src/ac_pro_bigdec.c \
                src/ac_pro_groebner.c src/ac_pro_rational.c \
                src/ac_pro_ode.c \
                src/ac_pro_schur.c src/ac_pro_sqfree.c
# ==== v2.5 ANCHOR: CX (feat/math-ext) ====
# v2.5.0 功能 F1 复变函数扩展（契约 docs/contracts/complex-ext-v25.md §11）。
# 1) 新 TU 无条件进 PRO_EXTRA：内核/runner 双口径编译；AC_PRO_DEGRADED=1 时
#    整体编译为空对象（零 .bss，38672 退化口径不受影响）。
PRO_EXTRA += src/ac_pro_complex_ext.c
# 2) OUTDIR ?= . 与下方既有行逐字相同（?= 首次赋值生效，此处先行定义供本
#    锚点内变量在解析期展开；命令行/环境 OUTDIR 的优先级语义完全不变）。
#    ac_pro_special.o 仅为 cx_runner 的实轴交叉回归锚 ac_special_gamma 链接
#    （上游 TU 自包含：AC_PI 是宏，无 ac_math.o 依赖）。
OUTDIR ?= .
# 3) C 侧验收走独立 cx_runner 目标：锚点纪律下 test_pro_runner.c 的 main()
#    聚合区没有预埋锚点，故不改主 runner；cx_runner 把测试 TU 与其依赖的
#    两个对象（本 TU + 上游 ac_complex 包装 TU，提供 g_c_* 六寄存器）链成
#    独立验收二进制，由 verify.ps1 的 CX 锚点门禁（exit 0 / RESULT: PASS /
#    [case CX-n] >= 10 / 0 failed）。裸链接（KERNEL_LDFLAGS，同 pro_kernel）：
#    测试 TU 零 malloc、零 static，Zero-GC 为结构性保证。对象路径显式写
#    $(OUTDIR)/obj（避开第 154 行 OBJDIR 的解析顺序），与既有模式规则的
#    $(OBJDIR)/src/%.o 逐字符一致（OUTDIR 已在本锚点先行定义）。
CXRUNNER    := $(OUTDIR)/cx_runner.exe
CX_OBJS     := $(OUTDIR)/obj/src/ac_pro_complex.o \
               $(OUTDIR)/obj/src/ac_pro_complex_ext.o \
               $(OUTDIR)/obj/src/ac_pro_special.o
CX_TEST_OBJ := $(patsubst test/%.c,$(OUTDIR)/obj/test/%.o,test/test_pro_complex_ext.c)
.PHONY: cxrunner
cxrunner: $(CXRUNNER)
$(CXRUNNER): $(CX_OBJS) $(CX_TEST_OBJ)
	$(CC) $(CFLAGS) $(KERNEL_LDFLAGS) -o $@ $^ -lm
# ==== END ANCHOR CX ====

# ==== v2.5 ANCHOR: SYSOLVE (feat/solvers) ====
# v2.5.0 F2:二元方程组 GB 消元数值全解(契约 sysolve-format-v25)
PRO_EXTRA += src/ac_pro_sysolve.c
PRO_EXTRA += src/ac_pro_sturm.c
# ==== END ANCHOR SYSOLVE ====

# ==== v2.5 ANCHOR: STURM (feat/solvers) ====

# ==== END ANCHOR STURM ====

# ==== v2.6 ANCHOR: SVD (feat/matrix-advanced) ====
# v2.6 预埋占位（仅注释，零行为变化）：svd_runner 独立探针目标与 SVD 增量 TU
# 的落位锚点，参照 CX 锚点的 cx_runner 先例（PRO_EXTRA 追加 + OUTDIR 域内
# 独立 runner 目标 + verify.ps1 SVD 锚点门禁）。feat/matrix-advanced 分支
# 合入时在上下标记之间填写；本版不携带任何变量/配方。
# ==== END ANCHOR SVD ====
SRC          := $(PRO_SRC) $(PRO_EXTRA) src/ac_pro_smoke.c
TEST_SRC     := test/test_pro_runner.c test/test_pro_multi.c \
                test/test_pro_eigen.c test/test_pro_integrate.c \
                test/test_pro_diff_multi.c test/test_pro_integrate_multi.c \
                test/test_pro_sparse.c test/test_pro_bigdec.c \
                test/test_pro_groebner.c test/test_pro_rational.c \
                test/test_pro_ode.c test/test_pro_schur.c \
                test/test_pro_sqfree.c \
                test/test_pro_sysolve.c \
                test/test_pro_sturm.c
# 内核口径构建用的 trivial main（零静态存储，只为了让 .bss 口径可比）
KERNEL_MAIN  := test/pro_kernel_main.c

# 产物输出目录（默认原地；受限环境用命令行覆盖）
OUTDIR ?= .
EXE    := $(OUTDIR)/pro_runner.exe
KEXE   := $(OUTDIR)/pro_kernel.exe
DEXE   := $(OUTDIR)/degraded/pro_kernel.exe

# ---- 构建调度：编译一次、链接两次 + 对象级并行 ----
# 原规则每个目标一次性 gcc -o $@ $^，同一批 TU 在 pro_runner / pro_kernel /
# degraded 三个链接里各编译一遍（~90 次 TU 编译/全量验收）。拆 .o 后：
#   · SRC 对象在 runner 与 kernel 口径间共享（同 CFLAGS，逐 TU 只编译一次）；
#   · degraded 因 -DAC_PRO_DEGRADED=1 代码生成不同，独立对象集 objdeg；
#   · 对外 phony 目标以 -j$(JOBS) 递归自呼实现对象编译并行（见下）。
# CFLAGS 变量逐字未动（.bss 账本与编译开关绑定）；配方额外追加 -MMD -MP 只产出
# 依赖文件、不影响代码生成——.o 缓存引入的陈旧风险由 depfile 对冲（头文件变更
# 会触发重编）。链接命令的 $^ 顺序与原规则一致，产物内容等价。
#
# 并行化说明：GNU make 不认可 makefile 内 MAKEFLAGS 赋值携带的 -j（实测对当前
# 实例无效、只传子 make），故在 pro_runner / kernel 入口配方里 -j$(JOBS) 递归自呼。
# `make pro_runner kernel` 两个递归顺序执行，obj 缓存使第二个仅剩链接；命令行
# 变量（OUTDIR/SHELL）经 MAKEFLAGS 自动透传给子 make，无需重述。
# 并行度取逻辑核数（Windows 环境变量自带；异常缺失时回退 8 档）。
JOBS := $(if $(NUMBER_OF_PROCESSORS),$(NUMBER_OF_PROCESSORS),8)
OBJDIR    := $(OUTDIR)/obj
DEGOBJDIR := $(OUTDIR)/objdeg

OBJS_SRC      := $(patsubst src/%.c,$(OBJDIR)/src/%.o,$(SRC))
OBJS_TEST     := $(patsubst test/%.c,$(OBJDIR)/test/%.o,$(TEST_SRC))
OBJS_KMAIN    := $(patsubst test/%.c,$(OBJDIR)/test/%.o,$(KERNEL_MAIN))
DEGOBJS_SRC   := $(patsubst src/%.c,$(DEGOBJDIR)/src/%.o,$(SRC))
DEGOBJS_KMAIN := $(patsubst test/%.c,$(DEGOBJDIR)/test/%.o,$(KERNEL_MAIN))

$(OBJDIR)/src $(OBJDIR)/test $(DEGOBJDIR)/src $(DEGOBJDIR)/test:
	@if not exist "$@" mkdir "$@"

$(OBJDIR)/src/%.o: src/%.c | $(OBJDIR)/src
	$(CC) $(CFLAGS) -pipe -MMD -MP -c $< -o $@

$(OBJDIR)/test/%.o: test/%.c | $(OBJDIR)/test
	$(CC) $(CFLAGS) -pipe -MMD -MP -c $< -o $@

$(DEGOBJDIR)/src/%.o: src/%.c | $(DEGOBJDIR)/src
	$(CC) $(CFLAGS) -DAC_PRO_DEGRADED=1 -pipe -MMD -MP -c $< -o $@

$(DEGOBJDIR)/test/%.o: test/%.c | $(DEGOBJDIR)/test
	$(CC) $(CFLAGS) -DAC_PRO_DEGRADED=1 -pipe -MMD -MP -c $< -o $@

-include $(OBJS_SRC:.o=.d) $(OBJS_TEST:.o=.d) $(OBJS_KMAIN:.o=.d) \
         $(DEGOBJS_SRC:.o=.d) $(DEGOBJS_KMAIN:.o=.d)

# ---- .bss 门禁常量 ----
# v2.1.0：42608 = 38672（上游 16 模块内核口径）+ 80（g_var_val）+ 3848（Pro union，
#                 峰值视图 sparse，v2.1.0 方向④）
#                 + 8（AC_PRO_ALIGN_PAD_BYTES，对齐填充）
# v1.3.5 是 41456（Pro union 峰值 mv 2688 + pad 16）；v2.1.0 稀疏求解落地
# sparse 视图 3848 B，峰值上移 +1160，对齐填充收窄 16→8，净增 +1152
#（size 实测 42608，设计说明预估 ≈42684）。
# v2.1.0 方向⑤ dec 视图 480 B（5 × 96）低于 sparse 峰值，被 union 完全
# 吸收（对 mv 2688 亦然），.bss 维持 42608/42640 不变（size 实测复核）。
KERNEL_BSS_EXPECT := 42608
# 42640 = 42608 + 32（test/test_pro_runner.c 的 .bss 段 16 + 链接对齐 16）
PRO_BSS_EXPECT    := 42640
# 退化构建：Pro 增量全关，等价于上游 16 模块内核口径
DEGRADED_BSS_EXPECT := 38672
# 上游基线对照（防上游漂移）
UP_BSS_EXPECT := 39408
UP_EXE        := ../128KB/test_runner.exe

# size 默认 Berkeley 格式：先 1 行表头（text data bss dec hex filename = 6 词），
# 再 1 行数值；$(shell) 把换行折成空格后，数值行的 bss 落在第 9 个 word
# （6 表头 + text/data = 第 7、8 词，bss = 第 9 词）。
# 两个变量都用递归展开（=），以便在 recipe 执行时（产物已生成后）才取值。
PRO_BSS_ACTUAL    = $(word 9,$(shell size $(EXE)))
KERNEL_BSS_ACTUAL = $(word 9,$(shell size $(KEXE)))
DEGRADED_BSS_ACTUAL = $(word 9,$(shell size $(DEXE)))
UP_BSS_ACTUAL     = $(word 9,$(shell size $(UP_EXE)))

all: pro_runner

# 对外目标名：pro_runner（产物文件名带 .exe）。phony 入口以 -j$(JOBS) 递归自呼实现
# 对象编译并行；`+` 前缀标记递归 make。
# E9：两入口共享同一递归配方，一次 make 内对象编译与 EXE/KEXE 两条链接全部并行
# （KEXE 链接不再等 EXE 串行完成）。语义注记：`make pro_runner` 也会构建
# pro_kernel.exe（对象与链接本就共享，单独造一半没有意义）；验收链路
# （verify.ps1 / measure）恒以 `pro_runner kernel` 双目标调用，配方第二次
# 执行为全量 up-to-date 的 no-op。
pro_runner:
	+$(MAKE) -j$(JOBS) $(EXE) $(KEXE)
	@echo "[build] $(EXE) ready"

$(EXE): $(OBJS_SRC) $(OBJS_TEST)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm

# 内核口径构建：16 上游包装 TU + Pro 增量 + trivial main（零静态）
kernel:
	+$(MAKE) -j$(JOBS) $(EXE) $(KEXE)
	@echo "[build] $(KEXE) ready"

$(KEXE): $(OBJS_SRC) $(OBJS_KMAIN)
	$(CC) $(CFLAGS) $(KERNEL_LDFLAGS) -o $@ $^ -lm

# ---- .bss 门禁 ----
# 门禁用 make 自身的 $(if)/$(error) 实现，不依赖 sh/awk（Windows 上两者都常缺），
# 失败时 make 直接以非 0 退出，符合「不符则 make 失败」的要求。
# 112 KiB 硬门禁（114688）由 ac_pro_config.h + ac_pro_solvers_ext.c 的
# 编译期断言 AC_PRO_STATIC_FOOTPRINT <= AC_PRO_GATE_BSS 保证 —— 这里只需
# 校验三个精确值，任何一处漂移都会让 make 失败。
bss-check: $(EXE) $(KEXE)
	@echo [bss-check] Pro runner (kernel + test TU) bss = $(PRO_BSS_ACTUAL) B   (expect $(PRO_BSS_EXPECT) B)
	@echo [bss-check] Pro kernel caliber          bss = $(KERNEL_BSS_ACTUAL) B   (expect $(KERNEL_BSS_EXPECT) B)
	@echo [bss-check] Upstream control            bss = $(UP_BSS_ACTUAL) B   (expect $(UP_BSS_EXPECT) B)
	$(if $(filter $(PRO_BSS_EXPECT),$(PRO_BSS_ACTUAL)),,$(error [bss-check] FAIL: pro_runner .bss is $(PRO_BSS_ACTUAL) B, expected $(PRO_BSS_EXPECT) B))
	$(if $(filter $(KERNEL_BSS_EXPECT),$(KERNEL_BSS_ACTUAL)),,$(error [bss-check] FAIL: kernel .bss is $(KERNEL_BSS_ACTUAL) B, expected $(KERNEL_BSS_EXPECT) B))
	$(if $(filter $(UP_BSS_EXPECT),$(UP_BSS_ACTUAL)),,$(error [bss-check] FAIL: upstream baseline .bss is $(UP_BSS_ACTUAL) B, expected $(UP_BSS_EXPECT) B))
	@echo "[bss-check] PASS. Ledger:"
	@echo "    38672 (16 upstream modules) + 80 (g_var_val[10]) + 3848 (Pro union, peak view sparse, v2.1.0)"
	@echo "     + 8 (align pad) = 42608  <- pro_kernel.exe  (kernel caliber, ledger target)"
	@echo "    42608 + 32 (Pro test TU: 16 B statics + 16 B alignment)"
	@echo "        = 42640  <- pro_runner.exe"
	@echo "    38672 (16 upstream modules) + 736 (upstream test/ side: 448 + 272 + 16)"
	@echo "        = 39408  <- upstream test_runner.exe"

# 退化验证：Pro 增量全关，产物必须与上游 16 模块内核口径逐字节同 bss。
degraded-check: $(DEXE)
	@echo [degraded] Pro kernel .bss = $(DEGRADED_BSS_ACTUAL) B   (expect $(DEGRADED_BSS_EXPECT) B)
	$(if $(filter $(DEGRADED_BSS_EXPECT),$(DEGRADED_BSS_ACTUAL)),,$(error [degraded] FAIL: degraded .bss is $(DEGRADED_BSS_ACTUAL) B, expected $(DEGRADED_BSS_EXPECT) B))
	@echo "[degraded] PASS: Pro increment is fully compiled out (no g_ac_pro_scratch, no g_var_val)."

$(DEXE): $(DEGOBJS_SRC) $(DEGOBJS_KMAIN) | $(OUTDIR)/degraded
	$(CC) $(CFLAGS) -DAC_PRO_DEGRADED=1 $(KERNEL_LDFLAGS) -o $@ $^ -lm

$(OUTDIR)/degraded:
	@if not exist "$@" mkdir "$@"

# 删除工具：Windows 上 rm 通常不在 PATH（MinGW 未提供 sh/rm），改用 cmd 内建 del。
# 配方统一加 `-` 前缀忽略「文件不存在」的失败（与上游 C/128KB/Makefile 同风格）。
ifeq ($(OS),Windows_NT)
RM    = del /Q
RMDIR = rmdir /S /Q
else
RM    = rm -f
RMDIR = rm -rf
endif

clean:
	-$(RM) $(EXE)
	-$(RM) $(KEXE)
	-$(RM) $(DEXE)
	-$(RMDIR) $(OBJDIR)
	-$(RMDIR) $(DEGOBJDIR)

.PHONY: all pro_runner kernel bss-check degraded-check clean
