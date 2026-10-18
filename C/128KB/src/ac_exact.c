/* ============================================================
 * ApolloCalc-CAS · ac_exact.c — 双模态精确数值系统（C 128KB 第 1 轮）
 * 行为基准：JS/128KB/src/core_exact.js（五分量模型），逐函数对齐。
 * 内部表示固定为五分量：
 *   value = ((g_exact_rat + g_exact_rad_num·√g_exact_rad) / g_exact_den) × 10^g_exact_exp
 * 五分量相对 64KB 三分量的表达力升级：混合形式 (1+√5)/2、
 * 科学计数法 v×10^e、同类根式的合并（√2+√2=2√2）。
 * C 与 JS 的差异：分量为 int64_t（63 位）而非 double（53 位），
 *   溢出阈值 INT64_MAX；唯一越界形态 INT64_MIN 在一切取负前拒绝。
 * Zero-GC：无 malloc、无 VLA、无递归。
 * ============================================================ */
#include <stdint.h>
#include <math.h>
#include "ac_types.h"
#include "ac_exact.h"

ac_int_t g_exact_rat = 0;
ac_int_t g_exact_rad_num = 0;
ac_int_t g_exact_rad = 1;
ac_int_t g_exact_den = 1;
int      g_exact_exp = 0;
int      g_exact_valid = 0;
double   g_approx_value = 0.0;

/* ============================================================
 * 模块一：基础工具（安全原语 / gcd / 根式化简）
 * ============================================================ */

int ac_safe_mul(ac_int_t a, ac_int_t b, ac_int_t *out) {
    if (a == 0 || b == 0) { *out = 0; return 1; }
    if (a > 0) {
        if (b > 0) { if (a > INT64_MAX / b) { return 0; } }
        else       { if (b < INT64_MIN / a) { return 0; } }
    } else {
        if (b > 0) { if (a < INT64_MIN / b) { return 0; } }
        else       { if (a < INT64_MAX / b) { return 0; } }
    }
    *out = a * b;
    return 1;
}

int ac_safe_add(ac_int_t a, ac_int_t b, ac_int_t *out) {
    if (b > 0)       { if (a > INT64_MAX - b) { return 0; } }
    else if (b < 0)  { if (a < INT64_MIN - b) { return 0; } }
    *out = a + b;
    return 1;
}

int ac_safe_sub(ac_int_t a, ac_int_t b, ac_int_t *out) {
    if (b < 0) { if (a > INT64_MAX + b) { return 0; } }
    else       { if (a < INT64_MIN + b) { return 0; } }
    *out = a - b;
    return 1;
}

ac_int_t ac_gcd(ac_int_t a, ac_int_t b) {
    ac_int_t t;
    /* [English] |INT64_MIN| = 2^63 overflows int64: unreachable from call
       sites (all components pre-guarded), clamp to keep negation defined. */
    if (a == INT64_MIN) { a = INT64_MAX; }
    if (b == INT64_MIN) { b = INT64_MAX; }
    if (a < 0) { a = -a; }
    if (b < 0) { b = -b; }
    while (b != 0) {
        t = a % b;
        a = b;
        b = t;
    }
    return a;
}

ac_int_t ac_simplify_radical(ac_int_t D) {
    ac_int_t d = D;
    ac_int_t k = 1;
    ac_int_t i;
    ac_int_t sq;
    if (d <= 0) {
        /* 调用方保证 D >= 0；D == 0 时外系数为 0，根号内置 1 */
        g_exact_rad = 1;
        return 0;
    }
    /* i <= d/i 等价于 i*i <= d 且不计算 i*i（避免 i 接近 3.04e9 时溢出）；
       循环体内 sq <= d <= INT64_MAX，乘法本身安全 */
    for (i = 2; i <= d / i; i++) {
        sq = i * i;
        while (d % sq == 0) {
            d = d / sq;
            /* 不变量 d*k*k == D 保证 k*i <= sqrt(D)，此分支防御性不可达 */
            if (k > INT64_MAX / i) { g_exact_valid = 0; return 0; }
            k = k * i;
        }
    }
    g_exact_rad = d;
    return k;
}

/* ============================================================
 * 模块二：规范化（唯一表示入口，每次构造/运算后调用）
 * ============================================================ */

void ac_exact_normalize(void) {
    ac_int_t ar;
    ac_int_t abn;
    ac_int_t abk;
    ac_int_t g1;
    ac_int_t g2;
    ac_int_t g;
    ac_int_t k;

    /* 0. 防御：根号内数非正整数非法；INT64_MIN 是 int64 唯一越界形态
       （|INT64_MIN| = 2^63），分母翻正/根式部并入前先拒绝（JS isFinite
       分支的 C 对应物；JS 溢出阈值 2^53-1 在 C 宽展为 INT64_MAX） */
    if (g_exact_rad < 1) {
        g_exact_valid = 0;
        return;
    }
    if (g_exact_rat == INT64_MIN || g_exact_rad_num == INT64_MIN ||
        g_exact_den == INT64_MIN) {
        g_exact_valid = 0;
        return;
    }

    /* 1. 分母恒正：den < 0 时所有分子变号 */
    if (g_exact_den < 0) {
        g_exact_rat = -g_exact_rat;
        g_exact_rad_num = -g_exact_rad_num;
        g_exact_den = -g_exact_den;
    }
    if (g_exact_den == 0) {
        g_exact_valid = 0;
        return;
    }

    /* 2. 根号内数化简：提取完全平方因子，外系数并入根式部系数 */
    if (g_exact_rad_num != 0 && g_exact_rad > 1) {
        k = ac_simplify_radical(g_exact_rad);   /* 原地写 g_exact_rad */
        if (k > 1) {
            abk = (g_exact_rad_num < 0) ? -g_exact_rad_num : g_exact_rad_num;
            if (abk > INT64_MAX / k) {
                g_exact_valid = 0;
                return;
            }
            g_exact_rad_num = g_exact_rad_num * k;
        }
    }

    /* 3. 根式部系数为 0 → 根号内置 1 */
    if (g_exact_rad_num == 0) {
        g_exact_rad = 1;
    } else if (g_exact_rad == 1) {
        /* 4. 根号内数为 1 → 根式部并入有理部（JS 同款 |a|+|b| 保守预检） */
        ac_int_t ar0 = (g_exact_rat < 0) ? -g_exact_rat : g_exact_rat;
        ac_int_t ab0 = (g_exact_rad_num < 0) ? -g_exact_rad_num : g_exact_rad_num;
        if (ar0 > INT64_MAX - ab0) {
            g_exact_valid = 0;
            return;
        }
        g_exact_rat = g_exact_rat + g_exact_rad_num;
        g_exact_rad_num = 0;
        g_exact_rad = 1;
    }

    /* 5. 三数（rat, rad_num, den）取最大公约数约分 */
    ar = (g_exact_rat < 0) ? -g_exact_rat : g_exact_rat;
    abn = (g_exact_rad_num < 0) ? -g_exact_rad_num : g_exact_rad_num;
    g1 = ac_gcd(ar, g_exact_den);
    g2 = ac_gcd(abn, g_exact_den);
    g = ac_gcd(g1, g2);
    if (g > 1) {
        g_exact_rat = g_exact_rat / g;
        g_exact_rad_num = g_exact_rad_num / g;
        g_exact_den = g_exact_den / g;
        ar = ar / g;
        abn = abn / g;
    }

    /* 6. 零规范化为 0/0/1/1/0 */
    if (g_exact_rat == 0 && g_exact_rad_num == 0) {
        g_exact_rat = 0;
        g_exact_rad_num = 0;
        g_exact_rad = 1;
        g_exact_den = 1;
        g_exact_exp = 0;
    }

    /* 7. 溢出缓解（JS 结构保真）：分子两项同除 10 且 exp+1，或分母除 10
       且 exp-1，值恒不变。int64 域内分量不可能超过 INT64_MAX（唯一越界
       形态 INT64_MIN 已在步骤 0 拒绝），本循环为防御性死代码；若未来
       分量改为宽类型仍保持正确。根号内数不参与转移（√(r/10) 会改变
       根式基）。 */
    while (ar > INT64_MAX || abn > INT64_MAX || g_exact_den > INT64_MAX) {
        if ((ar > INT64_MAX || abn > INT64_MAX) &&
            ar % 10 == 0 && abn % 10 == 0) {
            g_exact_rat = g_exact_rat / 10;
            g_exact_rad_num = g_exact_rad_num / 10;
            g_exact_exp = g_exact_exp + 1;
            ar = ar / 10;
            abn = abn / 10;
        } else if (g_exact_den > INT64_MAX && g_exact_den % 10 == 0) {
            g_exact_den = g_exact_den / 10;
            g_exact_exp = g_exact_exp - 1;
        } else {
            break;
        }
    }

    ac_exact_update_approx();
}

/* ============================================================
 * 模块三：智能构造函数（先赋五分量初值，再经 normalize 规范化）
 * ============================================================ */

void ac_exact_set_fraction(ac_int_t n, ac_int_t d) {
    /* 设置精确数值为分数 n/d */
    if (d == 0) {
        /* 分母为零非法：仅失效，五分量与 approx 全保持（JS 同款契约） */
        ac_exact_invalidate();
        return;
    }
    if (n == INT64_MIN || d == INT64_MIN) {
        /* |x| = 2^63 超出 int64 分量上限 */
        g_exact_valid = 0;
        g_approx_value = (double)n / (double)d;
        return;
    }
    g_exact_rat = n;
    g_exact_rad_num = 0;
    g_exact_rad = 1;
    g_exact_den = d;
    g_exact_exp = 0;
    g_exact_valid = 1;
    ac_exact_normalize();
}

void ac_exact_set_radical(ac_int_t k, ac_int_t m) {
    /* 设置精确数值为 k·√m */
    ac_int_t s;
    ac_int_t p;
    ac_int_t k2;
    ac_int_t n;
    if (m < 1) {
        /* 负数开方产生复数；m == 0 时 k·√0 = 0 按纯分数零处理 */
        if (m == 0) { ac_exact_set_fraction(0, 1); return; }
        ac_exact_invalidate();
        return;
    }
    if (k == INT64_MIN) {
        g_exact_valid = 0;
        g_approx_value = (double)k * sqrt((double)m);
        return;
    }
    /* 完全平方判定：s = round(√m)，s*s == m 则化为纯分数 k·s。
       s*s 先做除法预检（s 接近 3.04e9 时 s*s 会溢 int64，而
       m <= INT64_MAX 时真平方不可能溢出） */
    s = (ac_int_t)floor(sqrt((double)m) + 0.5);
    if (s > 0 && s <= INT64_MAX / s && s * s == m) {
        if (!ac_safe_mul(k, s, &p)) {
            g_exact_valid = 0;
            g_approx_value = (double)k * sqrt((double)m);
            return;
        }
        ac_exact_set_fraction(p, 1);
        return;
    }
    /* 非完全平方：先化简根号内数（原地写 g_exact_rad），外系数并入根式部系数 */
    k2 = ac_simplify_radical(m);
    if (!ac_safe_mul(k, k2, &n)) {
        g_exact_valid = 0;
        g_approx_value = (double)k * sqrt((double)m);
        return;
    }
    g_exact_rat = 0;
    g_exact_rad_num = n;
    g_exact_den = 1;
    g_exact_exp = 0;
    g_exact_valid = 1;
    ac_exact_normalize();
}

void ac_exact_set_mixed(ac_int_t a, ac_int_t b, ac_int_t c, ac_int_t d) {
    /* 设置精确数值为 (a + b·√c)/d —— 五分量新增的混合形式入口
       黄金比例即 ac_exact_set_mixed(1, 1, 5, 2) */
    if (d == 0) {
        ac_exact_invalidate();
        return;
    }
    if (c == 0) {
        /* b·√0 = 0：退化为纯分数 a/d */
        ac_exact_set_fraction(a, d);
        return;
    }
    if (c < 1) {
        /* 负数开方产生复数，不进入精确系统 */
        ac_exact_invalidate();
        return;
    }
    if (a == INT64_MIN || b == INT64_MIN || d == INT64_MIN || c == INT64_MIN) {
        g_exact_valid = 0;
        g_approx_value = ((double)a + (double)b * sqrt((double)c)) / (double)d;
        return;
    }
    g_exact_rat = a;
    g_exact_rad_num = b;
    g_exact_rad = c;
    g_exact_den = d;
    g_exact_exp = 0;
    g_exact_valid = 1;
    ac_exact_normalize();
}

void ac_exact_set_sci(ac_int_t v, int e) {
    /* 设置精确数值为 v × 10^e：整数 v 走分数构造后挂指数。
       C 入参 e 恒为整数（JS 的非整数指数检查不适用） */
    if (v == INT64_MIN) {
        g_exact_valid = 0;
        g_approx_value = (double)v * pow(10.0, (double)e);
        return;
    }
    ac_exact_set_fraction(v, 1);
    if (g_exact_valid) {
        if (g_exact_rat != 0 || g_exact_rad_num != 0) {
            g_exact_exp = e;      /* 零值不挂指数（零规范化后恒 0/0/1/1/0） */
        }
        ac_exact_update_approx();
    } else {
        g_approx_value = (double)v * pow(10.0, (double)e);
    }
}

void ac_exact_set_pure_real(double v) {
    /* 仅可表为 int64 的整数值进入精确系统，其余只保留近似值。
       强转前在 double 层判界（[-2^63, 2^63)），再要求往返还原精确 */
    if (v >= -9223372036854775808.0 && v < 9223372036854775808.0 &&
        v == (double)(ac_int_t)v) {
        ac_exact_set_fraction((ac_int_t)v, 1);
        return;
    }
    g_exact_valid = 0;
    g_approx_value = v;
}

void ac_exact_invalidate(void) {
    /* 精确模式失效：仅置标志，五分量与 approx 全保持（调用方负责同步） */
    g_exact_valid = 0;
}

void ac_exact_reset(void) {
    g_exact_rat = 0;
    g_exact_rad_num = 0;
    g_exact_rad = 1;
    g_exact_den = 1;
    g_exact_exp = 0;
    g_exact_valid = 0;
    g_approx_value = 0.0;
}

/* ============================================================
 * 模块四：近似值同步
 * ============================================================ */

void ac_exact_update_approx(void) {
    g_approx_value = (((double)g_exact_rat +
                       (double)g_exact_rad_num * sqrt((double)g_exact_rad)) /
                      (double)g_exact_den) * pow(10.0, (double)g_exact_exp);
}

/* ============================================================
 * 模块五：四则运算（原地更新五分量）
 * 操作数为四分量 (r2 + rn2·√rd2)/d2（指数视为 0）。
 * 当前值带非零指数（set_sci 产物）时不参与混合运算，置失效。
 * ============================================================ */

/* [English] Shared entry gate for the four ops: valid state, exp == 0,
   and a legal (den2 != 0, rad2 >= 1) operand free of INT64_MIN. */
static int exact_op_gate(ac_int_t rat2, ac_int_t rad_num2,
                         ac_int_t rad2, ac_int_t den2) {
    if (!g_exact_valid || g_exact_exp != 0) { return 0; }
    if (den2 == 0 || rad2 < 1) { return 0; }
    if (rat2 == INT64_MIN || rad_num2 == INT64_MIN ||
        rad2 == INT64_MIN || den2 == INT64_MIN) { return 0; }
    return 1;
}

/* [English] Operand invariant fold: rad2 == 1 with nonzero rad_num2 merges
   the radical part into the rational part (JS 保守 |a|+|b| 预检同款）。
   Returns 0 on overflow. */
static int exact_fold_operand(ac_int_t *n2, ac_int_t *b2, ac_int_t *r2) {
    if (*r2 == 1 && *b2 != 0) {
        ac_int_t an = (*n2 < 0) ? -*n2 : *n2;
        ac_int_t ab = (*b2 < 0) ? -*b2 : *b2;
        if (an > INT64_MAX - ab) { return 0; }
        *n2 = *n2 + *b2;
        *b2 = 0;
    }
    return 1;
}

void ac_exact_add(ac_int_t rat2, ac_int_t rad_num2, ac_int_t rad2, ac_int_t den2) {
    /* 当前精确数 + (rat2 + rad_num2·√rad2)/den2
       同类根式（r == r'，或一方根式部为零）才能合并：
         newRat = a·d' + a'·d ; newRadNum = b·d' + b'·d
         newRad = (b == 0) ? r' : r ; newDen = d·d'
       根式不匹配（√2+√3）五分量无法表达 → 失效 */
    ac_int_t n2;
    ac_int_t b2;
    ac_int_t r2;
    ac_int_t a;
    ac_int_t b;
    ac_int_t r;
    ac_int_t d;
    ac_int_t an;
    ac_int_t ad2;
    ac_int_t an2;
    ac_int_t ab;
    ac_int_t ab2;
    ac_int_t p1;
    ac_int_t p2;
    ac_int_t q1;
    ac_int_t q2;
    ac_int_t sum;

    if (!exact_op_gate(rat2, rad_num2, rad2, den2)) {
        ac_exact_invalidate();
        return;
    }
    n2 = rat2;
    b2 = rad_num2;
    r2 = rad2;
    if (!exact_fold_operand(&n2, &b2, &r2)) {
        ac_exact_invalidate();
        return;
    }
    a = g_exact_rat;
    b = g_exact_rad_num;
    r = g_exact_rad;
    d = g_exact_den;
    an = (a < 0) ? -a : a;
    ab = (b < 0) ? -b : b;
    an2 = (n2 < 0) ? -n2 : n2;
    ad2 = (den2 < 0) ? -den2 : den2;
    ab2 = (b2 < 0) ? -b2 : b2;
    /* 根式不匹配：双方根式部均非零且 rad 不同 */
    if (b != 0 && b2 != 0 && r != r2) {
        ac_exact_invalidate();
        return;
    }
    /* 溢出预检：|a·d'|、|a'·d|、d·d' 均不得越界（各除数 ≥ 1） */
    if (an > INT64_MAX / ad2 ||
        an2 > INT64_MAX / d ||
        d > INT64_MAX / ad2) {
        ac_exact_invalidate();
        return;
    }
    p1 = a * den2;                 /* 模已验，精确 */
    p2 = n2 * d;                   /* 模已验，精确 */
    /* safe_add 对同号（可能溢出）与异号（必不溢出）统一判定，
       语义与 JS 的同号预检 + 异号直加完全等价；两次求和全部通过后才
       写全局（失效路径保持旧值契约） */
    if (!ac_safe_add(p1, p2, &sum)) {
        ac_exact_invalidate();
        return;
    }
    q1 = 0;
    q2 = 0;
    if (b != 0) {
        if (ab > INT64_MAX / ad2) {
            ac_exact_invalidate();
            return;
        }
        q1 = b * den2;
    }
    if (b2 != 0) {
        if (ab2 > INT64_MAX / d) {
            ac_exact_invalidate();
            return;
        }
        q2 = b2 * d;
    }
    if (!ac_safe_add(q1, q2, &p2)) {   /* p2 复用为 radNum 和的暂存 */
        ac_exact_invalidate();
        return;
    }
    g_exact_rat = sum;
    g_exact_rad_num = p2;
    g_exact_rad = (b == 0) ? r2 : r;   /* 左方纯有理时保留右方根式 */
    g_exact_den = d * den2;            /* 模已验，符号交给规范化翻转 */
    g_exact_valid = 1;
    ac_exact_normalize();
}

void ac_exact_sub(ac_int_t rat2, ac_int_t rad_num2, ac_int_t rad2, ac_int_t den2) {
    /* 当前精确数 - (rat2 + rad_num2·√rad2)/den2：操作数整体变号复用加法。
       INT64_MIN 已在 add 的门禁拒绝，此处取负安全 */
    if (rat2 == INT64_MIN || rad_num2 == INT64_MIN) {
        if (g_exact_valid) { ac_exact_invalidate(); }
        return;
    }
    ac_exact_add(-rat2, -rad_num2, rad2, den2);
}

void ac_exact_mul(ac_int_t rat2, ac_int_t rad_num2, ac_int_t rad2, ac_int_t den2) {
    /* 当前精确数 * (rat2 + rad_num2·√rad2)/den2
       展开 (a + b√r)(a' + b'√r') = a·a' + (a·b' + a'·b)√r + b·b'·r（r == r'）
       一方根式部为零退化为有理×混合；r ≠ r' 且双方根式部非零时出现
       √r、√r'、√(r·r') 三个根式项（含 √2·√3），五分量无法表达 → 失效 */
    ac_int_t n2;
    ac_int_t b2;
    ac_int_t r2;
    ac_int_t a;
    ac_int_t b;
    ac_int_t r;
    ac_int_t d;
    ac_int_t an;
    ac_int_t ab;
    ac_int_t an2;
    ac_int_t ad2;
    ac_int_t ab2;
    ac_int_t pRat;
    ac_int_t pRad;
    ac_int_t pDen;
    ac_int_t newRad;
    ac_int_t bb;
    ac_int_t cross;
    ac_int_t u1;
    ac_int_t u2;

    if (!exact_op_gate(rat2, rad_num2, rad2, den2)) {
        ac_exact_invalidate();
        return;
    }
    n2 = rat2;
    b2 = rad_num2;
    r2 = rad2;
    if (!exact_fold_operand(&n2, &b2, &r2)) {
        ac_exact_invalidate();
        return;
    }
    if ((g_exact_rat == 0 && g_exact_rad_num == 0) || (n2 == 0 && b2 == 0)) {
        /* 零乘任何数仍为零，精确可表示 */
        ac_exact_set_fraction(0, 1);
        return;
    }
    a = g_exact_rat;
    b = g_exact_rad_num;
    r = g_exact_rad;
    d = g_exact_den;
    an = (a < 0) ? -a : a;
    ab = (b < 0) ? -b : b;
    an2 = (n2 < 0) ? -n2 : n2;
    ad2 = (den2 < 0) ? -den2 : den2;
    ab2 = (b2 < 0) ? -b2 : b2;
    if (b != 0 && b2 != 0 && r != r2) {
        /* 三根式项不可表达 */
        ac_exact_invalidate();
        return;
    }
    /* 公共预检：有理部主项 |a·a'| 与分母 d·d'（an2 可为 0，此时主项为 0） */
    if (an2 != 0 && an > INT64_MAX / an2) {
        ac_exact_invalidate();
        return;
    }
    if (d > INT64_MAX / ad2) {
        ac_exact_invalidate();
        return;
    }
    pRat = a * n2;
    pDen = d * den2;
    newRad = (b == 0) ? r2 : r;
    pRad = 0;
    if (b != 0 && b2 != 0) {
        /* r == r'：有理部补交叉项 b·b'·r，根式部 a·b' + a'·b */
        if (ab > INT64_MAX / ab2) {
            ac_exact_invalidate();
            return;
        }
        bb = b * b2;
        if ((bb < 0 ? -bb : bb) > INT64_MAX / r) {
            ac_exact_invalidate();
            return;
        }
        cross = bb * r;
        if (!ac_safe_add(pRat, cross, &pRat)) {
            ac_exact_invalidate();
            return;
        }
        /* u1 = b·n2（n2 可为 0）、u2 = a·b2 */
        if (an2 != 0 && ab > INT64_MAX / an2) {
            ac_exact_invalidate();
            return;
        }
        if (an != 0 && an > INT64_MAX / ab2) {
            ac_exact_invalidate();
            return;
        }
        u1 = b * n2;
        u2 = a * b2;
        if (!ac_safe_add(u1, u2, &pRad)) {
            ac_exact_invalidate();
            return;
        }
    } else if (b != 0) {
        /* 左根式 × 右有理：根式部 b·a'（n2 可为 0） */
        if (an2 != 0 && ab > INT64_MAX / an2) {
            ac_exact_invalidate();
            return;
        }
        pRad = b * n2;
    } else if (b2 != 0) {
        /* 左有理 × 右根式：根式部 a·b' */
        if (an != 0 && an > INT64_MAX / ab2) {
            ac_exact_invalidate();
            return;
        }
        pRad = a * b2;
    }
    g_exact_rat = pRat;
    g_exact_rad_num = pRad;
    g_exact_rad = newRad;
    g_exact_den = pDen;
    g_exact_valid = 1;
    ac_exact_normalize();
}

void ac_exact_div(ac_int_t rat2, ac_int_t rad_num2, ac_int_t rad2, ac_int_t den2) {
    /* 当前精确数 ÷ (rat2 + rad_num2·√rad2)/den2
       分子分母同乘共轭 (a' - b'√r') 有理化，分母化为纯有理：
         分母 = d·(a'² - b'²·r')（合并后 a'² ≠ b'²·r'，防御检查兜底）
         分子 = [(a·a' - b·b'·r) + (b·a' - a·b')√r]·d'（r == r' 时）
       左方纯有理（b=0）时无需 r == r'，结果保留右方根式 √r'
       r ≠ r' 且双方根式部非零 → 三根式项 → 失效 */
    ac_int_t n2;
    ac_int_t b2;
    ac_int_t r2;
    ac_int_t a;
    ac_int_t b;
    ac_int_t r;
    ac_int_t d;
    ac_int_t an;
    ac_int_t ab;
    ac_int_t an2;
    ac_int_t ad2;
    ac_int_t ab2;
    ac_int_t n2sq;
    ac_int_t b2sq;
    ac_int_t diff;
    ac_int_t adiff;
    ac_int_t t1;
    ac_int_t t2;
    ac_int_t t;
    ac_int_t u1;
    ac_int_t u2;
    ac_int_t u;
    ac_int_t at;
    ac_int_t au;
    ac_int_t newRad;

    if (!exact_op_gate(rat2, rad_num2, rad2, den2)) {
        ac_exact_invalidate();
        return;
    }
    n2 = rat2;
    b2 = rad_num2;
    r2 = rad2;
    if (!exact_fold_operand(&n2, &b2, &r2)) {
        ac_exact_invalidate();
        return;
    }
    if (n2 == 0 && b2 == 0) {
        /* 除数为零值 */
        ac_exact_invalidate();
        return;
    }
    if (g_exact_rat == 0 && g_exact_rad_num == 0) {
        /* 0 除以非零值仍为 0，精确可表示 */
        ac_exact_set_fraction(0, 1);
        return;
    }
    a = g_exact_rat;
    b = g_exact_rad_num;
    r = g_exact_rad;
    d = g_exact_den;
    an = (a < 0) ? -a : a;
    ab = (b < 0) ? -b : b;
    an2 = (n2 < 0) ? -n2 : n2;
    ad2 = (den2 < 0) ? -den2 : den2;
    ab2 = (b2 < 0) ? -b2 : b2;
    if (b2 == 0) {
        /* ÷ 纯分数 n2/den2：分子乘 den2，分母乘 n2（n2 符号入分母，规范化翻正）；
           此分支 n2 != 0（零值已拒），an2 >= 1 */
        ac_int_t newRat;
        ac_int_t newRadNum;
        ac_int_t newDen;
        if (an > INT64_MAX / ad2 ||
            ab > INT64_MAX / ad2 ||
            d > INT64_MAX / an2) {
            ac_exact_invalidate();
            return;
        }
        newRat = a * den2;
        newRadNum = b * den2;
        newDen = d * n2;
        g_exact_rat = newRat;
        g_exact_rad_num = newRadNum;
        g_exact_rad = r;
        g_exact_den = newDen;
        g_exact_valid = 1;
        ac_exact_normalize();
        return;
    }
    /* ÷ 含根式：共轭有理化 */
    if (b != 0 && r != r2) {
        /* 三根式项不可表达 */
        ac_exact_invalidate();
        return;
    }
    newRad = (b == 0) ? r2 : r;
    /* 分母：d·(n2² - b2²·r2)，两正项各自 ≤ MAX，差 |diff| ≤ max ≤ MAX 精确。
       n2 可为 0（an2 == 0 → n2sq = 0，无除法） */
    if (an2 != 0 && an2 > INT64_MAX / an2) {
        ac_exact_invalidate();
        return;
    }
    if (ab2 > INT64_MAX / ab2) {
        ac_exact_invalidate();
        return;
    }
    n2sq = an2 * an2;
    b2sq = ab2 * ab2;
    if (b2sq > INT64_MAX / r2) {
        ac_exact_invalidate();
        return;
    }
    diff = n2sq - b2sq * r2;
    if (diff == 0) {
        /* 共轭退化（合并后方根式部非零时不可达，防御） */
        ac_exact_invalidate();
        return;
    }
    adiff = (diff < 0) ? -diff : diff;
    if (d > INT64_MAX / adiff) {
        ac_exact_invalidate();
        return;
    }
    /* 分子有理部：a·n2 - b·b2·r（b=0 时退化为 a·n2） */
    if (an2 != 0 && an > INT64_MAX / an2) {
        ac_exact_invalidate();
        return;
    }
    t1 = a * n2;
    t2 = 0;
    if (b != 0) {
        if (ab > INT64_MAX / ab2) {
            ac_exact_invalidate();
            return;
        }
        {
            ac_int_t bbp = b * b2;
            if ((bbp < 0 ? -bbp : bbp) > INT64_MAX / r) {
                ac_exact_invalidate();
                return;
            }
            t2 = bbp * r;
        }
    }
    /* t = t1 - t2：safe_sub 失败条件恰为 JS 的异号 |t1|+|t2| 越界判定 */
    if (!ac_safe_sub(t1, t2, &t)) {
        ac_exact_invalidate();
        return;
    }
    /* 分子根式部：b·n2 - a·b2 */
    if (an2 != 0 && ab > INT64_MAX / an2) {
        ac_exact_invalidate();
        return;
    }
    if (an != 0 && an > INT64_MAX / ab2) {
        ac_exact_invalidate();
        return;
    }
    u1 = b * n2;
    u2 = a * b2;
    if (!ac_safe_sub(u1, u2, &u)) {
        ac_exact_invalidate();
        return;
    }
    /* 分子整体再乘 den2：|t|、|u| 与 |den2| 交叉预检 */
    at = (t < 0) ? -t : t;
    au = (u < 0) ? -u : u;
    if (at > INT64_MAX / ad2 ||
        au > INT64_MAX / ad2) {
        ac_exact_invalidate();
        return;
    }
    g_exact_rat = t * den2;
    g_exact_rad_num = u * den2;
    g_exact_rad = newRad;
    g_exact_den = d * diff;           /* diff 符号入分母，规范化翻正 */
    g_exact_valid = 1;
    ac_exact_normalize();
}

/* ============================================================
 * 模块六：从判别式 D 提取精确根（方程求解器接口）
 * ============================================================ */

void ac_exact_from_discriminant(double D) {
    /* 由判别式 D 提取 √D = (rat + radNum·√rad)/den
       保守策略：双精度入口下 D 只能是有理数，√D 的结果只可能是
       纯有理数（D 为完全平方）或纯根式 k√m；混合形式 √(p + q√r)
       需要无理 D，本入口不涉及（由五分量组装层自然覆盖） */
    ac_int_t d;
    ac_int_t di;
    ac_int_t foundDen;
    ac_int_t foundNum;
    ac_int_t m;
    ac_int_t k;
    ac_int_t k2;
    double v;
    double r;
    if (!isfinite(D) || D < 0.0) {
        /* 复数根或非有限值不进入精确系统 */
        ac_exact_invalidate();
        return;
    }
    if (D == 0.0) {
        /* √0 = 0，按纯分数零处理 */
        ac_exact_set_fraction(0, 1);
        return;
    }
    if (D == floor(D)) {
        /* 整数判别式：直接化简根式 k√m。2^63 及以上无法转 int64 */
        if (D >= 9223372036854775808.0) {
            ac_exact_invalidate();
            return;
        }
        di = (ac_int_t)D;
        k = ac_simplify_radical(di);
        g_exact_rat = 0;
        g_exact_rad_num = k;
        g_exact_den = 1;
        g_exact_exp = 0;
        g_exact_valid = 1;
        ac_exact_normalize();            /* rad == 1（完全平方）时并入有理部 */
        return;
    }
    /* 简单分数判定：寻找最小分母 d 使 D*d 为整数（D>0，无符号负担） */
    foundDen = 0;
    foundNum = 0;
    for (d = 2; d <= 1000000; d++) {
        v = D * (double)d;
        if (v >= 9223372036854775808.0) {
            /* 分子超出 int64 可表示范围：放弃精确表示 */
            break;
        }
        r = floor(v + 0.5);   /* 正数域上的 Math.round */
        if (v == r || (v - r < 1e-9 && r - v < 1e-9)) {
            foundDen = d;
            foundNum = (ac_int_t)r;
            break;
        }
    }
    if (foundDen == 0) {
        /* 无限小数或超出搜索范围：精确模式失效 */
        ac_exact_invalidate();
        return;
    }
    /* √(num/den) = √(num·den)/den：根号内通分后化简 */
    if (!ac_safe_mul(foundNum, foundDen, &m)) {
        ac_exact_invalidate();
        return;
    }
    k2 = ac_simplify_radical(m);
    g_exact_rat = 0;
    g_exact_rad_num = k2;
    g_exact_den = foundDen;
    g_exact_exp = 0;
    g_exact_valid = 1;
    ac_exact_normalize();                 /* 通分约分 + 完全平方并入有理部 */
}
