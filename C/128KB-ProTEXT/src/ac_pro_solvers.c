/* ac_pro_solvers.c — ProTEXT wrapper: upstream TU compiled exactly once
 *
 * 【本文件是唯一的「拦截层」/ The only interception point】
 *
 * 上游 ac_guard_degree（ac_solvers.c:670-673）硬编码 `degree > 4 拒绝`，
 * 而 Pro 的 B1 要求 5..8 次走数值路径。不能改上游，也不能在链接期
 * 覆盖同名符号（multiple definition）。
 *
 * 解法：在包含上游 .c **之前**用宏把它的定义改名。宏会同时作用于
 * ac_solvers.h 里的声明（该头文件由下面的 #include 引入），所以声明与
 * 定义一起改名，是自洽的 —— 不会出现「头文件声明 ac_guard_degree、
 * 目标文件里却是另一个名字」的悬空引用。
 *
 * 之后由 src/ac_pro_solvers_ext.c 提供真正对外的 ac_guard_degree。
 *
 * AC_PRO_ACTIVE=0（退化构建）时宏不生效，本 TU 与上游逐位一致。
 */
#include "ac_pro_config.h"

#if AC_PRO_ACTIVE
/* 只改名，不改语义：上游那份仍然存在，只是不再占用对外符号名。
   前缀 ac_pro_upstream_ 让「这是上游原件」在 nm 输出里一眼可见。 */
#define ac_guard_degree ac_pro_upstream_guard_degree
#endif

#include "../../../C/128KB/src/ac_solvers.c"
