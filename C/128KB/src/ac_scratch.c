/* ============================================================
 * ApolloCalc-CAS · ac_scratch.c — 共享 scratch 存储的唯一定义处
 * ------------------------------------------------------------
 * 该文件只做一件事：在**确有模块启用 union 复用**时，把共享存储
 * 落成一个实体（.bss 中的一份）。
 *
 * 为什么要用条件编译包住定义：
 *   如果无条件定义 g_ac_scratch / g_ac_stk，即便所有分组开关都是 0、
 *   没有任何模块引用它们，PE/COFF 链接器仍会把这两个未引用的全局
 *   符号保留在 .bss 里（+9248 B），导致 Step 1「.bss 不变」的验证
 *   条件不成立。包一层条件即可保证：开关全 0 时本文件不产生任何
 *   静态存储，项目与改造前逐位一致。
 *
 * 由谁引用：各模块通过 ac_scratch.h 里的视图宏访问这些实体。
 * ============================================================ */

#include "ac_scratch.h"

/* 任一数据池分组启用，就需要一份共享存储 */
#if AC_SCRATCH_UNION && \
    (AC_SCRATCH_SYM || AC_SCRATCH_MAT  || AC_SCRATCH_CALC || \
     AC_SCRATCH_RATROOT || AC_SCRATCH_BIGINT || AC_SCRATCH_UTIL)
ac_scratch_t g_ac_scratch;
#endif

/* 仅当启用栈别名时才需要 */
#if AC_SCRATCH_UNION && AC_SCRATCH_STK
union ac_stk_u g_ac_stk;
#endif
