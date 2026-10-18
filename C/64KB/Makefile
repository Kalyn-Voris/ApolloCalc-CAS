CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror -O2
LDFLAGS = -Wl,--wrap=malloc -Wl,--wrap=free

# 可选：启用 sin/cos 查表加速（Part B，默认关闭走 libm）
# 用法：mingw32-make LUT=1
# （注意：命令行 CFLAGS+=-DAC_MATH_USE_LUT 会整体替换 CFLAGS 丢掉 -std/-W，勿用）
ifeq ($(LUT),1)
CFLAGS += -DAC_MATH_USE_LUT
endif

SRC     = src/ac_parser.c src/ac_evaluator.c src/ac_exact.c src/ac_solvers.c src/ac_math.c src/ac_complex.c src/ac_numtheory.c src/ac_base.c src/ac_util.c
TEST    = test/test_main.c test/test_parser_smoke.c test/test_evaluator.c test/test_exact.c test/test_solvers.c test/test_math.c test/test_complex.c test/test_numtheory.c test/test_base.c test/test_util.c

# 可选：启用 sin/cos 查表加速（命令行覆盖：mingw32-make CFLAGS+=-DAC_MATH_USE_LUT）
# CFLAGS += -DAC_MATH_USE_LUT

all: test_runner

test_runner: $(SRC) $(TEST)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm

# 全量冒烟 CSV（第 10 轮 Part B）：跨版本比对用，独立 main
smoke_runner: $(SRC) test/test_smoke_csv.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm

# 删除工具：Windows 上 rm 通常不在 PATH（MinGW 未提供 sh/rm），改用 cmd 内建 del。
# clean 配方统一加 `-` 前缀忽略"文件不存在"的失败。
ifeq ($(OS),Windows_NT)
RM = del /Q
else
RM = rm -f
endif

clean:
	-$(RM) test_runner.exe
	-$(RM) test_runner
	-$(RM) smoke_runner.exe
	-$(RM) smoke_runner
	-$(RM) map.txt

.PHONY: all clean
