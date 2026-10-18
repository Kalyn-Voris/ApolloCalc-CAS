#ifndef AC_BIGINT_H
#define AC_BIGINT_H

#include "ac_types.h"

/* ============================================================
 * ApolloCalc-CAS · ac_bigint.h — 大整数（C 128KB 第 6 轮，C 独有）
 * JS 128KB 完全没有此层。目标：100!（158 位十进制）、F(200)、
 * 后续轮次的 RSA 教学模幂。
 * ----
 * 基数决策（任务卡授权 AI 裁决）：**基数 10^9、limbs[20]**。
 * 任务卡首推 2^32 × 16 = 512 位 ≈ 154 位十进制，但其自列的验收项
 * 100! 有 158 位 ≈ 525 位二进制，**超出 512 位**——自身矛盾。
 * 10^9 × 20 = 180 位十进制容量覆盖 100!（158）且留余量，三个红利：
 *   1) 十进制输出零成本（limb 即 9 位十进制组，无反复除 10）；
 *   2) 乘法单步 1e9 × 1e9 ≈ 1e18 < 2^63，uint64 中间量一次装下
 *      （基数 2^32 需 64 位截断技巧）；
 *   3) 除法按 limb 做 schoolbook + 二分试商，无需跨基换算。
 * 代价：无位运算直用（本层不需要）。
 * ----
 * Zero-GC：无 malloc、无 VLA、无递归；全部中间量预分配
 * （全局池 g_bi_* + 模块内 static 工作区，栈上无大对象）。
 * 溢出契约：结果超 180 位为未定义（受控入口 factorial n≤100 /
 * fibonacci n≤200 不可达）；乘法内部缓冲按 2×LIMBS 防御。
 * ============================================================ */

#define AC_BIGINT_LIMBS   20      /* 20 × 9 = 180 位十进制容量 */
#define AC_BIGINT_BASE    1000000000u   /* 1e9，每 limb 9 位十进制 */

typedef struct {
    uint32_t limbs[AC_BIGINT_LIMBS];   /* 低位在前，每 limb < 1e9 */
    int      sign;                     /* +1 / -1 / 0（零） */
    int      top;                      /* 最高非零 limb 下标 + 1，0 = 零 */
} ac_bigint_t;

/* [English] Global operand/result pool (preallocated, per task contract). */
extern ac_bigint_t g_bi_a;
extern ac_bigint_t g_bi_b;
extern ac_bigint_t g_bi_tmp;
extern ac_bigint_t g_bi_work1;
extern ac_bigint_t g_bi_work2;

/* [English] Construct / convert. */
void ac_bi_zero(ac_bigint_t *x);
void ac_bi_copy(ac_bigint_t *dst, const ac_bigint_t *src);
void ac_bi_from_int64(ac_bigint_t *x, int64_t v);
/* [English] Convert back to int64. Returns 0 on overflow (incl. 2^63-scale
   magnitudes beyond int64; -2^63 itself converts to INT64_MIN). */
int  ac_bi_to_int64(const ac_bigint_t *x, int64_t *out);

/* [English] Arithmetic (full signed; divmod truncates toward zero,
   remainder carries the dividend's sign — C semantics).
   mul/divmod are alias-safe (out/q/r may alias a/b); add/sub are
   single-pass and alias-safe for ascending reads. b == 0 in divmod
   zeroes q and r (contract: caller checks). */
void ac_bi_add(const ac_bigint_t *a, const ac_bigint_t *b, ac_bigint_t *out);
void ac_bi_sub(const ac_bigint_t *a, const ac_bigint_t *b, ac_bigint_t *out);
void ac_bi_mul(const ac_bigint_t *a, const ac_bigint_t *b, ac_bigint_t *out);
void ac_bi_divmod(const ac_bigint_t *a, const ac_bigint_t *b,
                  ac_bigint_t *q, ac_bigint_t *r);

/* [English] Three-way comparison: -1 / 0 / 1. */
int  ac_bi_cmp(const ac_bigint_t *a, const ac_bigint_t *b);

/* [English] Bounded exact sequences. Return 1 on success, 0 on
   out-of-range n. F(0) = 0, F(1) = 1.
   Scratch contract (pinned by tests): factorial leaves its last
   multiplier in g_bi_work1; fibonacci iterates in g_bi_work1 (a, ends
   at F(n-1)) and g_bi_work2 (c, ends at F(n)) — its OUT argument must
   NOT be g_bi_work1 or g_bi_work2. */
int  ac_bi_factorial(int n, ac_bigint_t *out);   /* 0 <= n <= 100 */
int  ac_bi_fibonacci(int n, ac_bigint_t *out);   /* 0 <= n <= 200 */

/* [English] Decimal string exit (one-shot UI-layer call; snprintf allowed
   here). "0" for zero, leading '-' for negatives, no zero padding on the
   top group, %09u on the rest. Always NUL-terminated within buf_size. */
char *ac_bi_to_string(const ac_bigint_t *x, char *buf, int buf_size);

#endif /* AC_BIGINT_H */
