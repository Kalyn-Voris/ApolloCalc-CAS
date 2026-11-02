/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
#ifndef AC_EVALUATOR_H
#define AC_EVALUATOR_H

#include "ac_types.h"

/* [English] RPN buffer and evaluation state (mirrors core_evaluator.js). */
extern int32_t  g_rpn_token[RPN_POOL_SIZE];
extern int      g_rpn_len;
extern int32_t  g_op_stack[OP_STACK_SIZE];
extern int      g_op_sp;
extern int      g_eval_sp;
extern double   g_eval_x;
extern int      g_eval_error;
extern double   g_eval_result;

/* [English] LHS/RHS token buffers for equation splitting. */
extern int32_t  g_lhs_token[TOKEN_POOL_SIZE / 2];
extern int32_t  g_rhs_token[TOKEN_POOL_SIZE / 2];
extern int      g_lhs_count;
extern int      g_rhs_count;
extern double   g_lhs_result;
extern double   g_rhs_result;

/* [English] Angle mode g_angle_deg moved to ac_math.h in round 5. */

/* 错误码（与 JS 版一致） */
#define EVAL_OK         0
#define EVAL_DIV_ZERO   1
#define EVAL_DOMAIN     2
#define EVAL_OVERFLOW   3
#define EVAL_INTERNAL   4

/* [English] Convert infix token stream to RPN. Returns length, writes g_rpn_token. */
int ac_to_rpn(const int32_t *src, int src_count, int32_t *dst);

/* [English] Evaluate RPN stream with current g_eval_x. Returns result, sets g_eval_error. */
double ac_eval_rpn(const int32_t *rpn, int rpn_len);

/* [English] Top-level: evaluate current g_token_pool at given x. */
double ac_eval_expression_at(double x);

/* [English] Evaluate equation f(x)=g(x) at given x; returns f(x)-g(x). */
double ac_eval_equation_at(double x);

/* [English] Reset evaluator state. */
void ac_evaluator_reset(void);

#endif /* AC_EVALUATOR_H */
