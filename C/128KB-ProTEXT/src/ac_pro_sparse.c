/* ============================================================
 * ApolloCalc-CAS · C/128KB-ProTEXT/src/ac_pro_sparse.c
 * ------------------------------------------------------------
 * 【状态 / Status —— 先读这一段】
 *   v2.1.0 方向④「稀疏线性方程组（n > 12）」的**骨架文件**，由
 *   tools/scaffold_v2_1_0.py 生成。当前只有文件结构、编译守卫和一个
 *   探针占位函数；**不含任何算法实现**，调用探针只会得到
 *   AC_PRO_SPARSE_ENIMPL（-1001，定义见 ac_pro.h）。真实 API 落地时
 *   整体替换本文件。
 *
 * 【规划要点备忘 / Planning notes（落地时照此执行，数字均为估算）】
 *   出处：设计说明。
 *   - 方案 a：n ≤ 32 / nnz ≤ 320，CSR 三元组 + perm + rhs/x 视图
 *     ≈ 3.9 KB → 预估 .bss ≈ 42684（视图 3.9 KB > mv 峰值 2688，会
 *     推高 union 峰值，须同步抬高 Makefile 门禁常量）；
 *   - 方案 b：n ≤ 48 / nnz ≤ 512 ≈ 6.1 KB → 预估 .bss ≈ 44882；
 *   - 算法：阈值主元 CSR 消元，显式迭代循环、无递归（Zero-GC 兼容），
 *     .text 预算 +2–3 KB（无门禁）；
 *   - preCheck 新增：n/nnz 上限、主元下限、结构奇异检测；
 *   - 落地动作（机械，doc:131-132）：ac_pro_scratch.h 增加 sparse 视图
 *     与 == 测试链，Makefile 的 KERNEL_BSS_EXPECT / PRO_BSS_EXPECT 及
 *     账本注释同步。
 *
 * 【编译守卫 / Guards】
 *   TU 体整体包在 #if AC_PRO_ACTIVE 内（AC_PRO_ACTIVE = AC_PRO &&
 *   !AC_PRO_DEGRADED，见 ac_pro_config.h）。DEGRADED 构建下本文件编成
 *   只含 include 的空对象，零 .bss、零 .text —— 与 ac_pro_multi.c 等
 *   既有 Pro 增量 TU 同一纪律。
 *
 * 【Zero-GC 铁律 / Zero-GC】
 *   无 malloc、无 VLA、无递归。本骨架的探针函数体是常量返回，自然零
 *   分配；实现落地后同样不得引入任何静态对象（一个 static 就是
 *   .bss +32 B，见 ac_pro.h eig 节的实测论证）。
 * ============================================================ */

#include "ac_pro.h"

#if AC_PRO_ACTIVE

/* ==================== 探针占位 / Scaffold probe ====================
 * 唯一的对外函数，仅用于冒烟测试证明「本 TU 已被链接、守卫生效、
 * 占位契约成立」。返回 AC_PRO_SPARSE_ENIMPL（ac_pro.h 定义）。
 * 实现落地时由真实 API（CSR 求解入口）取代，本函数删除。
 * 错误码走返回值而非静态变量：任何静态对象 = .bss +32 B（同 eig）。 */
int ac_pro_sparse_probe(void) {
    return AC_PRO_SPARSE_ENIMPL;
}

#endif /* AC_PRO_ACTIVE */
