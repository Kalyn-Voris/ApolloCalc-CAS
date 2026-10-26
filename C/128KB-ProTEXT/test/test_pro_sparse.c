/* ============================================================
 * ApolloCalc-CAS · C/128KB-ProTEXT/test/test_pro_sparse.c
 * ------------------------------------------------------------
 * 【职责 / Responsibility】
 *   v2.1.0 骨架冒烟测试（tools/scaffold_v2_1_0.py 生成）：证明
 *   src/ac_pro_sparse.c 已被链接进 pro_runner、AC_PRO_ACTIVE 守卫生效、
 *   探针契约（返回 AC_PRO_SPARSE_ENIMPL）成立。共 1 条测试。
 *   真实实现落地时，本文件由真实的单元测试整体替换。
 *
 * 【接口约定 / Interface】
 *   照抄 test/test_pro_multi.c：本文件定义自己的 pro_tally_t 与
 *   pcheck 助手，导出 test_sparse_scaffold(void) 返回计数结构，runner
 *   只汇总、不感知内部类型（test_pro_runner.c 的跨 TU 约定）。入口
 *   函数不带 static（external linkage），与 runner 的外部原型一致。
 *
 * 【零静态存储纪律 / Zero static storage】
 *   本文件不声明任何静态对象（pcheck 是 static 函数，不占 .bss），
 *   链接进来后 runner 口径的 .bss 增量仍是 0。
 * ============================================================ */

#include <stdio.h>

#include "../src/ac_pro.h"

#if AC_PRO_ACTIVE

typedef struct {
    int checks;
    int failed;
} pro_tally_t;

static void pcheck(pro_tally_t *t, int cond, const char *name, const char *detail) {
    t->checks++;
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        t->failed++;
        printf("  [FAIL] %s  (%s)\n", name, detail);
    }
}

/* ==================== 骨架冒烟（1 项测试） ==================== */
pro_tally_t test_sparse_scaffold(void) {
    pro_tally_t t;
    int rc;

    t.checks = 0;
    t.failed = 0;

    printf("[case SP-1] scaffold probe: ac_pro_sparse_probe() == AC_PRO_SPARSE_ENIMPL\n");
    rc = ac_pro_sparse_probe();
    pcheck(&t, rc == AC_PRO_SPARSE_ENIMPL,
           "sparse scaffold TU linked, probe reports ENIMPL",
           "probe returned unexpected value");
    printf("         rc = %d\n", rc);

    return t;
}

#endif /* AC_PRO_ACTIVE */
