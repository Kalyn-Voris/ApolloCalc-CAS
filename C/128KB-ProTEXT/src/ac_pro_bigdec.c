/* ============================================================
 * ApolloCalc-CAS · C/128KB-ProTEXT/src/ac_pro_bigdec.c
 * ------------------------------------------------------------
 * 【状态 / Status —— 先读这一段】
 *   v2.1.0 方向⑤「任意精度小数」的**骨架文件**，由
 *   tools/scaffold_v2_1_0.py 生成。当前只有文件结构、编译守卫和一个
 *   探针占位函数；**不含任何算法实现**，调用探针只会得到
 *   AC_PRO_BIGDEC_ENIMPL（-1002，定义见 ac_pro.h）。真实 API 落地时
 *   整体替换本文件。
 *
 * 【规划要点备忘 / Planning notes（落地时照此执行，数字均为估算）】
 *   出处：设计说明。
 *   - dec 视图 5 × {bigint 88 B + scale 8 B} ≈ 480 B，被 Pro union 的
 *     mv 峰值（2688 B）完全吸收 → .bss 保持 41456 **零增长**，Makefile
 *     门禁常量无需改动；
 *   - 容量上限：180 有效位、标度 ±2^31、除法位数上限；
 *   - 除法走 divmod 余数扩标长除；依赖上游 bigint 的 alias-safe
 *     mul/divmod 与可复用的 ac_bi_to_string；
 *   - 注意与上游 g_bi_* 草稿契约不冲突：Pro 视图与上游 bigint 池
 *     物理分离。
 *   - 落地动作：ac_pro_scratch.h 增加 dec 视图（480 < 2688，被吸收，
 *     测试链更新属机械动作）。
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
 * 占位契约成立」。返回 AC_PRO_BIGDEC_ENIMPL（ac_pro.h 定义）。
 * 实现落地时由真实 API（dec 定点十进制入口）取代，本函数删除。
 * 错误码走返回值而非静态变量：任何静态对象 = .bss +32 B（同 eig）。 */
int ac_pro_bigdec_probe(void) {
    return AC_PRO_BIGDEC_ENIMPL;
}

#endif /* AC_PRO_ACTIVE */
