/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ApolloCalc-CAS · ac_parser.c — Token 解析器
 * C99 64KB / 静态内存 / Zero-GC：禁 malloc、禁 VLA、禁递归、
 * 禁热路径字符串函数（strlen 仅入口一次）
 * 对等基准：JS/64KB/src/core_parser.js（逐分支行为一致）
 * Token 编码: word = (type << 8) | payload；word === 0 即 T_END 哨兵
 * 唯一实现差异：JS 读 UTF-16 码元（charCodeAt），C 按字节读输入，
 * decode_special 把 π/×/÷/√/− 五个 UTF-8 序列还原为与 JS 相同的码点 */
#include <string.h>   /* 仅 strlen：入口测长一次，热路径零字符串函数 */
#include "ac_parser.h"

/* 全局内存池：全内核唯一声明处，后续模块直接引用 */
int32_t g_token_pool[TOKEN_POOL_SIZE];
int     g_token_count = 0;
double  g_number_pool[NUMBER_POOL_SIZE];
int     g_number_count = 0;
double  g_eval_stack[EVAL_STACK_SIZE];
double  g_matrix_pool[MATRIX_POOL_SIZE];

/* 扫描游标（文件内私有） */
int g_parse_error = ERR_NONE;
static int s_pos = 0;
static int s_len = 0;

/* [English] Append one token to the global pool; -1 on pool overflow. */
static int emit_token(int type, int payload) {
    if (g_token_count >= TOKEN_POOL_SIZE) { g_parse_error = ERR_OVERFLOW; return -1; }
    g_token_pool[g_token_count] = ac_token_make(type, payload);
    g_token_count++;
    return 0;
}

/* [English] Append one numeric constant to the pool; returns its index, -1 on overflow. */
static int push_number(double v) {
    if (g_number_count >= NUMBER_POOL_SIZE) { g_parse_error = ERR_OVERFLOW; return -1; }
    g_number_pool[g_number_count] = v;
    g_number_count++;
    return g_number_count - 1;
}

/* prev 收尾一个操作数 且 next 开启一个操作数 → 注入隐式乘号 */
static int can_follow_implicit(int prev_type, int next_type) {
    if (prev_type == T_NUM || prev_type == T_VAR || prev_type == T_CONST || prev_type == T_RP) {
        if (next_type == T_NUM || next_type == T_VAR || next_type == T_CONST ||
            next_type == T_FUNC || next_type == T_LP) {
            return 1;
        }
    }
    return 0;
}

/* 一元上下文：开头 / 运算符后 / 左括号后 / 等号后 / 逗号后 / 一元负号后 */
static int is_unary_context(int prev_type) {
    return (prev_type == T_END || prev_type == T_OP || prev_type == T_LP ||
            prev_type == T_EQ || prev_type == T_COMMA || prev_type == T_UMINUS) ? 1 : 0;
}

/* UTF-8 特殊符号 → JS 码点还原
 * 仅识别 5 个数学符号，其余高位字节一律 -1（ERR_BADCHAR）：
 *   π U+03C0 = CF 80        × U+00D7 = C3 97
 *   ÷ U+00F7 = C3 B7        √ U+221A = E2 88 9A
 *   − U+2212 = E2 88 92
 * ASCII（< 0x80）原样返回，width = 1 */
static int decode_special(const char *src, int pos, int len, int *width) {
    unsigned char c0 = (unsigned char)src[pos];
    unsigned char c1;
    *width = 1;
    if (c0 < 0x80) { return c0; }
    if (pos + 1 >= len) { return -1; }
    c1 = (unsigned char)src[pos + 1];
    if (c0 == 0xCF && c1 == 0x80) { *width = 2; return 960; }   /* π */
    if (c0 == 0xC3 && c1 == 0x97) { *width = 2; return 215; }   /* × */
    if (c0 == 0xC3 && c1 == 0xB7) { *width = 2; return 247; }   /* ÷ */
    if (c0 == 0xE2 && c1 == 0x88 && pos + 2 < len) {
        unsigned char c2 = (unsigned char)src[pos + 2];
        if (c2 == 0x9A) { *width = 3; return 8730; }            /* √ */
        if (c2 == 0x92) { *width = 3; return 8722; }            /* − */
    }
    return -1;
}

/* 数字扫描（小数 + 科学计数法）
 * 直接写 numberPool，返回索引；失败返回 -1
 * Horner 累积 v = v*10 + d；小数只记位数，最后一次性除权还原
 * 'e' 歧义：后跟数字（或 +/- 数字）→ 科学计数法；否则终止扫描，'e' 交回主循环当自然常数 */
static int scan_number(const char *src) {
    double v = 0.0;
    int frac_digits = 0;
    int seen_dot = 0;
    int had_digit = 0;
    int c;

    while (s_pos < s_len) {
        c = (unsigned char)src[s_pos];
        if (c >= 48 && c <= 57) {                /* '0'..'9' */
            v = v * 10.0 + (double)(c - 48);
            if (seen_dot == 1) { frac_digits++; }
            had_digit = 1;
            s_pos++;
        } else if (c == 46 && seen_dot == 0) {   /* '.' 首次 */
            seen_dot = 1;
            s_pos++;
        } else if (c == 101 || c == 69) {        /* 'e' / 'E' 科学计数法歧义判定 */
            int j = s_pos + 1;
            int c1 = (j < s_len) ? (unsigned char)src[j] : 0;
            int exp_neg = 0;
            int is_exp = 0;
            if (c1 >= 48 && c1 <= 57) {
                is_exp = 1;
            } else if ((c1 == 43 || c1 == 45) && (j + 1 < s_len)) {
                int c2 = (unsigned char)src[j + 1];
                if (c2 >= 48 && c2 <= 57) {
                    is_exp = 1;
                    if (c1 == 45) { exp_neg = 1; }
                    j = j + 1;
                }
            }
            if (is_exp == 1) {
                int exp_val = 0;
                s_pos = j;
                while (s_pos < s_len) {
                    int dc = (unsigned char)src[s_pos];
                    if (dc < 48 || dc > 57) { break; }
                    exp_val = exp_val * 10 + (dc - 48);
                    s_pos++;
                }
                if (exp_neg == 1) { exp_val = -exp_val; }
                v = v * pow(10.0, (double)exp_val);
            }
            break;  /* 非指数 → 'e' 归主循环；是指数 → 已消费完，数字到此终止 */
        } else {
            break;
        }
    }

    if (had_digit == 0) { g_parse_error = ERR_BADCHAR; return -1; }
    if (frac_digits > 0) { v = v / pow(10.0, (double)frac_digits); }
    return push_number(v);
}

/* 函数名硬编码字符树
 * 命中：返回函数 ID 并消费字符；未命中：返回 0 且不消费
 * 字符比较与 JS 版 charCodeAt 判定逐位一致（小写字母 + 前缀歧义手工展开） */
static int match_function(const char *src) {
    int c0 = (s_pos < s_len) ? (unsigned char)src[s_pos] : 0;
    int c1 = (s_pos + 1 < s_len) ? (unsigned char)src[s_pos + 1] : 0;
    int c2 = (s_pos + 2 < s_len) ? (unsigned char)src[s_pos + 2] : 0;
    int c3 = (s_pos + 3 < s_len) ? (unsigned char)src[s_pos + 3] : 0;
    int f = 0;
    int n = 0;

    if (c0 == 115) {                                                          /* 's' */
        if (c1 == 105 && c2 == 110) { f = F_SIN; n = 3; }                     /* sin */
        else if (c1 == 113 && c2 == 114 && c3 == 116) { f = F_SQRT; n = 4; }  /* sqrt */
    } else if (c0 == 99) {                                                    /* 'c' */
        if (c1 == 111 && c2 == 115) { f = F_COS; n = 3; }                     /* cos */
    } else if (c0 == 116) {                                                   /* 't' */
        if (c1 == 97 && c2 == 110) { f = F_TAN; n = 3; }                      /* tan */
    } else if (c0 == 97) {                                                    /* 'a' */
        if (c1 == 115 && c2 == 105 && c3 == 110) { f = F_ASIN; n = 4; }       /* asin */
        else if (c1 == 99 && c2 == 111 && c3 == 115) { f = F_ACOS; n = 4; }   /* acos */
        else if (c1 == 116 && c2 == 97 && c3 == 110) {                        /* atan / atan2 */
            int c4 = (s_pos + 4 < s_len) ? (unsigned char)src[s_pos + 4] : 0;
            if (c4 == 50) { f = F_ATAN2; n = 5; }                             /* atan2 */
            else { f = F_ATAN; n = 4; }                                       /* atan */
        }
        else if (c1 == 98 && c2 == 115) { f = F_ABS; n = 3; }                 /* abs */
    } else if (c0 == 108) {                                                   /* 'l' */
        if (c1 == 110) { f = F_LN; n = 2; }                                   /* ln */
        else if (c1 == 111 && c2 == 103) { f = F_LOG; n = 3; }                /* log */
    } else if (c0 == 101) {                                                   /* 'e' */
        if (c1 == 120 && c2 == 112) { f = F_EXP; n = 3; }                     /* exp */
    }

    if (f > 0) { s_pos += n; }
    return f;
}

/* 主入口：ac_parse_expression
 * 输入：表达式字符串（"2x^2+3x-5=0" / "sin(30)"）
 * 输出：g_token_pool / g_number_pool / g_token_count / g_parse_error（全局）
 * 返回：g_parse_error（0 = 成功）
 * 注：注入隐式乘号时不更新 prev_type（保持乘号前操作数类型，与 JS 一致） */
int ac_parse_expression(const char *src) {
    int depth = 0;
    int prev_type = T_END;
    int c = 0;
    int f = 0;
    int width = 1;

    g_token_count = 0;
    g_number_count = 0;
    g_parse_error = ERR_NONE;
    s_pos = 0;
    s_len = (int)strlen(src);   /* 全文件唯一一次 strlen */

    if (s_len == 0) { g_parse_error = ERR_EMPTY; return g_parse_error; }

    while (s_pos < s_len) {
        width = 1;
        c = decode_special(src, s_pos, s_len, &width);
        if (c < 0) { g_parse_error = ERR_BADCHAR; break; }

        /* ---- 空白 ---- */
        if (c == 32 || c == 9 || c == 13 || c == 10) { s_pos += width; continue; }

        /* ---- 数字 / 小数点 ---- */
        if ((c >= 48 && c <= 57) || c == 46) {
            f = scan_number(src);
            if (f < 0) { break; }
            if (can_follow_implicit(prev_type, T_NUM) == 1) {
                if (emit_token(T_OP, OP_MUL) < 0) { break; }
            }
            if (emit_token(T_NUM, f) < 0) { break; }
            prev_type = T_NUM;
            continue;
        }

        /* ---- 负号：'-' / '−'(8722) ---- */
        if (c == 45 || c == 8722) {
            if (is_unary_context(prev_type) == 1) {
                if (emit_token(T_UMINUS, 0) < 0) { break; }
                prev_type = T_UMINUS;
            } else {
                if (emit_token(T_OP, OP_SUB) < 0) { break; }
                prev_type = T_OP;
            }
            s_pos += width;
            continue;
        }

        /* ---- 加号：一元正号丢弃 / 二元加 ---- */
        if (c == 43) {
            if (is_unary_context(prev_type) == 1) { s_pos += width; continue; }
            if (emit_token(T_OP, OP_ADD) < 0) { break; }
            prev_type = T_OP;
            s_pos += width;
            continue;
        }

        if (c == 42 || c == 215) {              /* '*' / '×' */
            if (emit_token(T_OP, OP_MUL) < 0) { break; }
            prev_type = T_OP;
            s_pos += width;
            continue;
        }
        if (c == 47 || c == 247) {              /* '/' / '÷' */
            if (emit_token(T_OP, OP_DIV) < 0) { break; }
            prev_type = T_OP;
            s_pos += width;
            continue;
        }
        if (c == 94) {                          /* '^' */
            if (emit_token(T_OP, OP_POW) < 0) { break; }
            prev_type = T_OP;
            s_pos += width;
            continue;
        }

        if (c == 40) {                          /* '(' */
            if (can_follow_implicit(prev_type, T_LP) == 1) {
                if (emit_token(T_OP, OP_MUL) < 0) { break; }
            }
            if (emit_token(T_LP, 0) < 0) { break; }
            depth++;
            prev_type = T_LP;
            s_pos += width;
            continue;
        }
        if (c == 41) {                          /* ')' */
            if (depth <= 0) { g_parse_error = ERR_PAREN; break; }
            if (emit_token(T_RP, 0) < 0) { break; }
            depth--;
            prev_type = T_RP;
            s_pos += width;
            continue;
        }

        if (c == 61) {                          /* '=' */
            if (emit_token(T_EQ, 0) < 0) { break; }
            prev_type = T_EQ;
            s_pos += width;
            continue;
        }
        if (c == 44) {                          /* ',' */
            if (emit_token(T_COMMA, 0) < 0) { break; }
            prev_type = T_COMMA;
            s_pos += width;
            continue;
        }

        /* ---- 字母：函数名 / 变量 x / 常数 e / pi ---- */
        if ((c >= 97 && c <= 122) || c == 88) {
            f = match_function(src);
            if (f > 0) {
                if (can_follow_implicit(prev_type, T_FUNC) == 1) {
                    if (emit_token(T_OP, OP_MUL) < 0) { break; }
                }
                if (emit_token(T_FUNC, f) < 0) { break; }
                prev_type = T_FUNC;
                continue;
            }
            if (c == 120 || c == 88) {          /* 'x' / 'X' 变量 */
                if (can_follow_implicit(prev_type, T_VAR) == 1) {
                    if (emit_token(T_OP, OP_MUL) < 0) { break; }
                }
                if (emit_token(T_VAR, 0) < 0) { break; }
                prev_type = T_VAR;
                s_pos += width;
                continue;
            }
            if (c == 101) {                     /* 'e' 自然常数（exp 已被上方消费） */
                if (can_follow_implicit(prev_type, T_CONST) == 1) {
                    if (emit_token(T_OP, OP_MUL) < 0) { break; }
                }
                if (emit_token(T_CONST, C_E) < 0) { break; }
                prev_type = T_CONST;
                s_pos += width;
                continue;
            }
            if (c == 112) {                     /* 'p' → 期待 "pi" */
                int cn = (s_pos + 1 < s_len) ? (unsigned char)src[s_pos + 1] : 0;
                if (cn == 105) {
                    if (can_follow_implicit(prev_type, T_CONST) == 1) {
                        if (emit_token(T_OP, OP_MUL) < 0) { break; }
                    }
                    if (emit_token(T_CONST, C_PI) < 0) { break; }
                    prev_type = T_CONST;
                    s_pos += 2;
                    continue;
                }
                g_parse_error = ERR_BADCHAR;
                break;
            }
            g_parse_error = ERR_BADCHAR;
            break;
        }

        /* ---- 数学特殊符号 ---- */
        if (c == 8730) {                        /* '√' → sqrt */
            if (can_follow_implicit(prev_type, T_FUNC) == 1) {
                if (emit_token(T_OP, OP_MUL) < 0) { break; }
            }
            if (emit_token(T_FUNC, F_SQRT) < 0) { break; }
            prev_type = T_FUNC;
            s_pos += width;
            continue;
        }
        if (c == 960) {                         /* 'π' */
            if (can_follow_implicit(prev_type, T_CONST) == 1) {
                if (emit_token(T_OP, OP_MUL) < 0) { break; }
            }
            if (emit_token(T_CONST, C_PI) < 0) { break; }
            prev_type = T_CONST;
            s_pos += width;
            continue;
        }

        g_parse_error = ERR_BADCHAR;
        break;
    }

    if (g_parse_error != ERR_NONE) {
        g_token_count = 0; g_number_count = 0;
        return g_parse_error;
    }
    if (depth != 0) {
        g_parse_error = ERR_PAREN;
        g_token_count = 0; g_number_count = 0;
        return g_parse_error;
    }
    if (g_token_count == 0) { g_parse_error = ERR_EMPTY; return g_parse_error; }
    return ERR_NONE;
}
