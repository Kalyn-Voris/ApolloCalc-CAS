/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ac_pro_scratch.c — ProTEXT wrapper: upstream TU compiled exactly once
 *
 * 本文件只做一件事：把上游 ac_scratch.c 作为独立 TU 编译一次。
 * Pro 自己的存储实体在 ac_pro_extra_globals.c —— 刻意分开放，这样
 * `AC_PRO_ACTIVE=0` 的退化构建里，本文件仍与上游逐位一致，而 Pro 增量
 * 那个 TU 整体编译成空对象（零 .bss），退化验证才有干净的对照。
 */
#include "ac_pro_config.h"
#include "../../../C/128KB/src/ac_scratch.c"
