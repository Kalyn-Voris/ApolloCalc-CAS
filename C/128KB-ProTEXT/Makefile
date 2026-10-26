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
# 【Pro v1.3.5 的 .bss 三层账本 / Three-tier .bss ledger】
#   内核口径（16 上游模块 + trivial main）      38672 B
#     + g_var_val[10]                            80 B
#     + g_ac_pro_scratch（Pro union，峰值 mv）  2688 B
#     + 对齐填充                                  16 B
#   ------------------------------------------------
#   = 41456 B   <- 内核口径门禁值（KERNEL_BSS_EXPECT）
#
#   pro_runner.exe 额外带上 test/test_pro_runner.c 这个 TU：
#     + 16 B（该 TU 的 .bss 段：s_kernel_live 等）
#     + 16 B（链接期对齐填充）
#   = 41488 B   <- runner 口径门禁值（PRO_BSS_EXPECT）
#
#   两个数都对，只是口径不同 —— 与上游「38672 内核口径 / 39408 README
#   口径」的区分方式完全一致。任务书要求「精确等于 41456」对应的是
#   内核口径，由 `make kernel` + `make bss-check` 一起校验。
#
#   实测命令（本仓库 gcc 8.1.0 / MinGW-w64）：
#     mingw32-make bss-check    -> kernel 41456 / runner 41488 / upstream 39408
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
                src/ac_pro_sparse.c src/ac_pro_bigdec.c
SRC          := $(PRO_SRC) $(PRO_EXTRA) src/ac_pro_smoke.c
TEST_SRC     := test/test_pro_runner.c test/test_pro_multi.c \
                test/test_pro_eigen.c test/test_pro_integrate.c \
                test/test_pro_diff_multi.c test/test_pro_integrate_multi.c \
                test/test_pro_sparse.c test/test_pro_bigdec.c
# 内核口径构建用的 trivial main（零静态存储，只为了让 .bss 口径可比）
KERNEL_MAIN  := test/pro_kernel_main.c

# 产物输出目录（默认原地；受限环境用命令行覆盖）
OUTDIR ?= .
EXE    := $(OUTDIR)/pro_runner.exe
KEXE   := $(OUTDIR)/pro_kernel.exe
DEXE   := $(OUTDIR)/degraded/pro_kernel.exe

# ---- .bss 门禁常量 ----
# v1.3.5：41456 = 38672（上游 16 模块内核口径）+ 80（g_var_val）+ 2688（Pro union）
#                 + 16（AC_PRO_ALIGN_PAD_BYTES，对齐填充）
# v1.2 是 41104（Pro union 峰值 lu 2352）；v1.3 引入 mv 视图（6 槽 × 28 项 × 16 B
# = 2688）后峰值上移，.bss 相应 +352（2688-2352 = 336 的视图增量 + 16 对齐填充）。
# v1.3.5 新增 eig 视图 2592 B，比 mv 小 96 B 被完全吸收，此三行数字不变。
# v2.1.0 新增 dec 视图 480 B（⑤ 任意精度小数：5 × {bigint 88 + scale 8}），
#   480 ≤ 2688 同样被 mv 峰值完全吸收，实测 .bss 仍为 41456/41488，数字不变。
KERNEL_BSS_EXPECT := 41456
# 41488 = 41456 + 32（test/test_pro_runner.c 的 .bss 段 16 + 链接对齐 16）
PRO_BSS_EXPECT    := 41488
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

# 对外目标名：pro_runner（产物文件名带 .exe）
pro_runner: $(EXE)
	@echo "[build] $(EXE) ready"

$(EXE): $(SRC) $(TEST_SRC)
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm

# 内核口径构建：16 上游包装 TU + Pro 增量 + trivial main（零静态）
kernel: $(KEXE)
	@echo "[build] $(KEXE) ready"

$(KEXE): $(SRC) $(KERNEL_MAIN)
	@if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"
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
	@echo "    38672 (16 upstream modules) + 80 (g_var_val[10]) + 2688 (Pro union, peak view mv)"
	@echo "     + 16 (align pad) = 41456  <- pro_kernel.exe  (kernel caliber, task-book target)"
	@echo "    41456 + 32 (Pro test TU: 16 B statics + 16 B alignment)"
	@echo "        = 41488  <- pro_runner.exe"
	@echo "    38672 (16 upstream modules) + 736 (upstream test/ side: 448 + 272 + 16)"
	@echo "        = 39408  <- upstream test_runner.exe"

# 退化验证：Pro 增量全关，产物必须与上游 16 模块内核口径逐字节同 bss。
degraded-check: $(DEXE)
	@echo [degraded] Pro kernel .bss = $(DEGRADED_BSS_ACTUAL) B   (expect $(DEGRADED_BSS_EXPECT) B)
	$(if $(filter $(DEGRADED_BSS_EXPECT),$(DEGRADED_BSS_ACTUAL)),,$(error [degraded] FAIL: degraded .bss is $(DEGRADED_BSS_ACTUAL) B, expected $(DEGRADED_BSS_EXPECT) B))
	@echo "[degraded] PASS: Pro increment is fully compiled out (no g_ac_pro_scratch, no g_var_val)."

$(DEXE): $(SRC) $(KERNEL_MAIN)
	@if not exist "$(OUTDIR)/degraded" mkdir "$(OUTDIR)/degraded"
	$(CC) $(CFLAGS) -DAC_PRO_DEGRADED=1 $(KERNEL_LDFLAGS) -o $@ $^ -lm

# 删除工具：Windows 上 rm 通常不在 PATH（MinGW 未提供 sh/rm），改用 cmd 内建 del。
# 配方统一加 `-` 前缀忽略「文件不存在」的失败（与上游 C/128KB/Makefile 同风格）。
ifeq ($(OS),Windows_NT)
RM = del /Q
else
RM = rm -f
endif

clean:
	-$(RM) $(EXE)
	-$(RM) $(KEXE)
	-$(RM) $(DEXE)

.PHONY: all pro_runner kernel bss-check degraded-check clean
