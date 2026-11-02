/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
#ifndef AC_TYPES_H
#define AC_TYPES_H

#include <stdint.h>
#include <math.h>
#include <stddef.h>

/* Token 类型（与 JS 64KB 版一一对应） */
#define T_END     0
#define T_NUM     1
#define T_VAR     2
#define T_CONST   3
#define T_OP      4
#define T_LP      5
#define T_RP      6
#define T_FUNC    7
#define T_EQ      8
#define T_COMMA   9
#define T_UMINUS  10

/* 运算符 */
#define OP_ADD 0
#define OP_SUB 1
#define OP_MUL 2
#define OP_DIV 3
#define OP_POW 4

/* 函数 ID（与 JS 64KB 一致） */
#define F_SIN 1
#define F_COS 2
#define F_TAN 3
#define F_ASIN 4
#define F_ACOS 5
#define F_ATAN 6
#define F_ATAN2 7
#define F_SQRT 8
#define F_LN 9
#define F_LOG 10
#define F_EXP 11
#define F_ABS 12

/* 常量 ID */
#define C_E   0
#define C_PI  1

/* 池尺寸（64KB 版本，与 JS 64KB 一致） */
#define TOKEN_POOL_SIZE    256
#define NUMBER_POOL_SIZE   128
#define EVAL_STACK_SIZE     64
#define RPN_POOL_SIZE      256
#define OP_STACK_SIZE       64
#define MATRIX_POOL_SIZE    42   /* 6 元线性方程组增广矩阵 (6x7) */

/* [English] Root result pool: max(4 polynomial roots, 6 linear unknowns).
   Sized to 6 to avoid the overflow bug fixed in JS 64KB. */
#define ROOT_POOL_SIZE       6

/* 解析错误码 */
#define ERR_NONE      0
#define ERR_BADCHAR   1
#define ERR_OVERFLOW  2
#define ERR_PAREN     3
#define ERR_EMPTY     4

/* [English] Token encoding: 1 int32 = (type << 8) | payload. */
static inline int32_t ac_token_make(int type, int payload) {
    return (int32_t)((type << 8) | (payload & 0xFF));
}
static inline int ac_token_type(int32_t w) { return (int)(w >> 8); }
static inline int ac_token_payload(int32_t w) { return (int)(w & 0xFF); }

/* [English] Integer type for exact-form numerators/denominators.
   64-bit signed; downgrade to double when overflow. */
typedef int64_t ac_int_t;

#endif /* AC_TYPES_H */
