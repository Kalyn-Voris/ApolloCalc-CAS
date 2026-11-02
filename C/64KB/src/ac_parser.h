/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
#ifndef AC_PARSER_H
#define AC_PARSER_H

#include "ac_types.h"

/* [English] Global pools defined in ac_parser.c. */
extern int32_t  g_token_pool[TOKEN_POOL_SIZE];
extern int      g_token_count;
extern double   g_number_pool[NUMBER_POOL_SIZE];
extern int      g_number_count;
extern double   g_eval_stack[EVAL_STACK_SIZE];
extern double   g_matrix_pool[MATRIX_POOL_SIZE];

/* [English] Parser state. */
extern int      g_parse_error;

/* [English] Parse a mathematical expression string.
   Returns 0 on success, error code on failure. */
int ac_parse_expression(const char *src);

#endif /* AC_PARSER_H */
