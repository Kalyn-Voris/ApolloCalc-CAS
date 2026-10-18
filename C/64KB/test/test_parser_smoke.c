#include <stdio.h>
#include <string.h>
#include "../src/ac_parser.h"

static int s_pass = 0;
static int s_fail = 0;

static void expect_parse(const char *src, int want_err) {
    int err = ac_parse_expression(src);
    if (err == want_err) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL parse \"%s\": got err=%d want err=%d\n", src, err, want_err);
    }
}

static void expect_token_count(const char *src, int want_count) {
    ac_parse_expression(src);
    if (g_token_count == want_count) { s_pass++; }
    else {
        s_fail++;
        printf("FAIL token count \"%s\": got %d want %d\n", src, g_token_count, want_count);
    }
}

int test_parser_run(void) {
    expect_parse("2x^2+3x-5=0", ERR_NONE);
    expect_parse("sin(30)", ERR_NONE);
    expect_parse("atan2(3,4)", ERR_NONE);
    expect_parse("", ERR_EMPTY);
    expect_parse("((2+3)", ERR_PAREN);
    expect_parse("2+α", ERR_BADCHAR);

    /* 隐式乘法（token 数量验证） */
    expect_token_count("2x", 3);         /* NUM MUL VAR */
    expect_token_count("xπ", 3);         /* VAR MUL CONST */
    expect_token_count("(2+3)x", 7);     /* LP NUM ADD NUM RP MUL VAR */

    ac_parse_expression("2e5");
    if (g_number_count == 1 && g_number_pool[0] == 200000.0) { s_pass++; }
    else { s_fail++; printf("FAIL sci 2e5: count=%d val=%f\n", g_number_count, g_number_pool[0]); }

    ac_parse_expression("2ex");
    if (g_number_count == 1 && g_number_pool[0] == 2.0 && g_token_count == 5) { s_pass++; }
    else { s_fail++; printf("FAIL 2ex: count=%d n0=%f tokens=%d\n", g_number_count, g_number_pool[0], g_token_count); }

    expect_token_count("-2", 2);         /* UMINUS NUM */
    expect_token_count("2^-1", 4);       /* NUM OP_POW UMINUS NUM */

    ac_parse_expression("sinh(1)");      /* 应失败，C 64KB 无双曲 */
    if (g_parse_error != ERR_NONE && g_parse_error != ERR_PAREN) { s_pass++; }
    else { s_fail++; printf("FAIL sinh should error in C 64KB\n"); }

    ac_parse_expression("sin(1)");
    if (g_parse_error == ERR_NONE) { s_pass++; }
    else { s_fail++; printf("FAIL sin(1) parse: err=%d\n", g_parse_error); }

    printf("==== C99 parser smoke: %d pass, %d fail ====\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
