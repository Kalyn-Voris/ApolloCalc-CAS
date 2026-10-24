/* ============================================================
 * ApolloCalc-CAS · C/128KB-ProTEXT/src/ac_pro_extra_globals.c
 * ------------------------------------------------------------
 * 【职责 / Responsibility】
 *   Pro 层存储块的**唯一**定义处（设计说明 §2.3 判据 2 / 判据 3）：
 *
 *       ac_pro_storage_t g_ac_pro_storage;
 *         ├─ scratch  (ac_pro_scratch_t union)   2688 B
 *         ├─ var_val  (double[10])                 80 B
 *         └─ align_pad (double[2])                 16 B
 *       ------------------------------------------------
 *       合计                                      2784 B
 *
 * 【为什么是一个 struct 而不是两个全局 / Why one object】
 *   PE/COFF 链接器把**每个**全局对象按 16 B 对齐起址：两个独立全局之间
 *   会被插入 16 B 填充。装进一个 struct 后整块只占一个分配单元，填充
 *   无从产生，设计说明 §2.3 的逐字节账本才闭合。
 *
 *   struct 是「顺序排列」而非「重叠」：g_var_val 仍在 union 之外，
 *   判据 3（跨调用会话态禁入 union）完全满足，只是与 union 同处一个
 *   .bss 分配单元。
 *
 * 【为什么没有 `= {0}`】
 *   带初始化器的对象落 .data 而非 .bss。那会让 .bss 少 2784、.data 多
 *   2784，`size` 的 text+data+bss 总量纹丝不动 —— 最难发现的一类漂移。
 *   账本要求它必须落在 .bss，所以这里裸定义，靠 C 的零初始化保证。
 *
 * 【Zero-GC】
 *   本文件只有一个静态对象定义，无函数、无 malloc、无 VLA、无递归。
 * ============================================================ */

#include "ac_pro_scratch.h"

#if AC_PRO_ACTIVE

/* 单个存储对象：尺寸由 ac_pro_scratch.h 的 `== 2784` 校验钉死。 */
ac_pro_storage_t g_ac_pro_storage;

#endif /* AC_PRO_ACTIVE */
