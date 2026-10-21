#ifndef AC_PARSER_H
#define AC_PARSER_H

#include "ac_types.h"
#include "ac_scratch_cfg.h"   /* AC_SCRATCH_STK guard */

/* [English] Global pools defined in ac_parser.c.
   NOTE on g_eval_stack: when AC_SCRATCH_STK is enabled it is a macro that
   expands to (g_ac_stk.ev), so an `extern double g_eval_stack[...]`
   declaration here would expand into invalid syntax. The shared storage
   is instead declared (via union ac_stk_u) in ac_scratch.h, which
   ac_evaluator.c / ac_parser.c / ac_solvers.c all include. */
extern int32_t  g_token_pool[TOKEN_POOL_SIZE];
extern int      g_token_count;
extern double   g_number_pool[NUMBER_POOL_SIZE];
extern int      g_number_count;
#if !(AC_SCRATCH_UNION && AC_SCRATCH_STK)
extern double   g_eval_stack[EVAL_STACK_SIZE];
#endif
extern double   g_matrix_pool[MATRIX_POOL_SIZE];

/* [English] Parser state. */
extern int      g_parse_error;

/* [English] Parse a mathematical expression string.
   Returns 0 on success, error code on failure. */
int ac_parse_expression(const char *src);

#endif /* AC_PARSER_H */
