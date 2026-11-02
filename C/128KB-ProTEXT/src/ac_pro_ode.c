/* ApolloCalc · ac_pro_ode.c — 常系数线性齐次 ODE 的字符串进,字符串出

   v2.3.0 功能 3(C 侧):解析 "y''+3y'+2y=0" 形态 → 特征多项式 → 上游
   ac_solvers 1..4 次精确根式求特征根 → 渲染通解字符串。单根 e^{λx},
   共轭对 e^{αx}(C₁cos βx + C₂sin βx),实重根 x^k·e^{λx};非齐次/变系数/
   次数 >4 如实报错,不硬解。

   本文件与契约的镜像关系(ROADMAP.md:178):行为边界由
   docs/contracts/ode-format-v23.md 逐字冻结:输入文法(§3),特征多项式
   映射(§3.4),根排序与聚类(§4),渲染规则(§5),错误码表与判定全序(§6)。
   本头注是该契约的镜像;任何变更须 C/JS 双组确认后先改契约再改码。JS 侧
   对应物为 core_ode.js,两侧对同一输入必须产出逐字节一致的输出串与同一
   错误码。

   零存储纪律:零 static 对象,零 malloc,零 VLA,零递归;错误码走返回值
   (ac_pro.h:292 论证:任何 static 都让 TU 净增 32 B .bss)。特征根,系数,
   渲染缓冲全部栈上(n ≤ 4)。无新 Pro 视图(渲染缓冲由调用方提供,
   ROADMAP.md:108「.bss 零增长」)。上游 ac_solvers 用上游 g_ac_scratch,
   与本文件无视图交集。 */

#include <string.h>
#include <stdint.h>
#include <math.h>

#include "ac_pro_config.h"
#include "ac_pro.h"
#include "../../../C/128KB/src/ac_solvers.h"

#if AC_PRO_ACTIVE

#define ODE_SRC_MAX      256   /* 契约 §3.1：输入长度上限（字节，不含 NUL） */
#define ODE_OUTBUF       512   /* 渲染临时缓冲（调用方缓冲 ≥ 256 冻结值） */
#define ODE_EPS          1e-9  /* 契约 §4 的相等判定容差（CONTRIBUTING.md:96） */
#define ODE_FMT_MAX       32

/* 任意常数下标 ₁..₄（U+2081..2084，UTF-8 三字节；UTF-8 是本仓库既有输出
 * 风格,exact-forms.json 的 √(2) 同款）。static const 落 .rdata 零 .bss。 */
static const char s_ode_sub[4][4] = {
    "\xe2\x82\x81", "\xe2\x82\x82", "\xe2\x82\x83", "\xe2\x82\x84"
};

static const char *ode_csub(int i) { return s_ode_sub[i]; }   /* i = 0..3 */

static long long ode_igcd(long long a, long long b) {
    long long t;
    while (b != 0) {
        t = a % b;
        a = b;
        b = t;
    }
    return (a < 0) ? -a : a;
}

/* 输出缓冲追加 */
static int ode_puts(char *buf, int cap, int *pos, const char *s) {
    while (*s != '\0') {
        if (*pos >= cap - 1) { return AC_PRO_ODE_EOVER; }
        buf[(*pos)++] = *s++;
    }
    buf[*pos] = '\0';
    return 0;
}

static int ode_puti(char *buf, int cap, int *pos, long long v) {
    char tmp[24];
    int i = 0;
    if (v < 0) {
        if (ode_puts(buf, cap, pos, "-") != 0) { return AC_PRO_ODE_EOVER; }
        v = -v;
    }
    if (v == 0) { tmp[i++] = '0'; }
    while (v > 0) {
        tmp[i++] = (char)('0' + (int)(v % 10));
        v /= 10;
    }
    while (i > 0) {
        char one[2];
        one[0] = tmp[--i];
        one[1] = '\0';
        if (ode_puts(buf, cap, pos, one) != 0) { return AC_PRO_ODE_EOVER; }
    }
    return 0;
}

/* 数值三级 fmt（契约 §5.1，与 rational 模块同算法）
 * 整判 1e-9 → 整串；有理重建 q ≤ 1000 → "p/q"（分数间距 ≥ 1/(q1q2) ≥ 1e-6
 * ≫ 2e-9，1e-9 邻域互不相交，重建对求根噪声不变）；否则定点 12 位
 * （半连舍远离零）。系数域 |a| ≤ 999 保证根的 Cauchy 界 ≤ 1000 < 4096，
 * x·1e12 < 2^53（双精度缩放无损）且 < 2^63−1（int64 无溢出）。 */
static int ode_fmt(double x, char *out) {
    double r = (x < 0) ? -x : x;
    if (r < 1e12) {
        double nr = (x < 0) ? -(double)((long long)(-x + 0.5))
                            : (double)((long long)(x + 0.5));
        if ((x - nr) < ODE_EPS && (nr - x) < ODE_EPS) {
            long long v = (long long)nr;
            int pos = 0;
            char tmp[ODE_FMT_MAX];
            tmp[0] = '\0';
            if (ode_puti(tmp, ODE_FMT_MAX, &pos, v) != 0) {
                return AC_PRO_ODE_EOVER;
            }
            strcpy(out, tmp);
            return 0;
        }
    }
    {
        int q;
        long long best_p = 0;
        int best_q = 0;
        double best_err = 1e300;
        for (q = 1; q <= 1000; q++) {
            long long p = (long long)((x < 0) ? (x * (double)q - 0.5)
                                              : (x * (double)q + 0.5));
            double err;
            long long g;
            long long pp = p;
            int qq = q;
            if (pp < 0) { g = ode_igcd(-pp, (long long)qq); }
            else { g = ode_igcd(pp, (long long)qq); }
            if (g > 1) { pp /= g; qq = (int)((long long)qq / g); }
            err = x - (double)pp / (double)qq;
            if (err < 0) { err = -err; }
            if (err <= ODE_EPS && (err < best_err ||
                                   (err == best_err && qq < best_q))) {
                best_err = err;
                best_p = pp;
                best_q = qq;
            }
        }
        if (best_q > 0) {
            int pos = 0;
            char tmp[ODE_FMT_MAX];
            tmp[0] = '\0';
            if (ode_puti(tmp, ODE_FMT_MAX, &pos, best_p) != 0) {
                return AC_PRO_ODE_EOVER;
            }
            if (best_q != 1) {
                char qbuf[ODE_FMT_MAX];
                int qpos = 0;
                if (ode_puts(tmp, ODE_FMT_MAX, &pos, "/") != 0) {
                    return AC_PRO_ODE_EOVER;
                }
                qbuf[0] = '\0';
                if (ode_puti(qbuf, ODE_FMT_MAX, &qpos, best_q) != 0) {
                    return AC_PRO_ODE_EOVER;
                }
                if (ode_puts(tmp, ODE_FMT_MAX, &pos, qbuf) != 0) {
                    return AC_PRO_ODE_EOVER;
                }
            }
            strcpy(out, tmp);
            return 0;
        }
    }
    {
        double ax = (x < 0) ? -x : x;
        long long s = (long long)(ax * 1e12 + 0.5);
        long long ip = s / 1000000000000LL;
        long long fp = s % 1000000000000LL;
        char tmp[ODE_FMT_MAX];
        int pos = 0;
        char fpbuf[16];
        int digits = 12;
        int i;
        if (x < 0) {
            if (ode_puts(tmp, ODE_FMT_MAX, &pos, "-") != 0) {
                return AC_PRO_ODE_EOVER;
            }
        }
        if (ode_puti(tmp, ODE_FMT_MAX, &pos, ip) != 0) {
            return AC_PRO_ODE_EOVER;
        }
        for (i = 0; i < 12; i++) {
            fpbuf[11 - i] = (char)('0' + (int)(fp % 10));
            fp /= 10;
        }
        fpbuf[12] = '\0';
        while (digits > 0 && fpbuf[digits - 1] == '0') { digits--; }
        if (digits > 0) {
            if (ode_puts(tmp, ODE_FMT_MAX, &pos, ".") != 0) {
                return AC_PRO_ODE_EOVER;
            }
            for (i = 0; i < digits; i++) {
                char one[2];
                one[0] = fpbuf[i];
                one[1] = '\0';
                if (ode_puts(tmp, ODE_FMT_MAX, &pos, one) != 0) {
                    return AC_PRO_ODE_EOVER;
                }
            }
        }
        strcpy(out, tmp);
        return 0;
    }
}

/* 契约 §5.2 唯二特例的判定：指数值经整判为 0（|λ| < 1e-9 即整判 0） */
static int ode_is_zero(double v) {
    return v < ODE_EPS && v > -ODE_EPS;
}

/* 主入口 */
int ac_pro_ode(const char *src, char *out, int out_len) {
    long long a[5];          /* a[k] = y^{(k)} 的系数（契约 §3.4） */
    double re[4];
    double im[4];
    double lre[4];
    double lim[4];
    int used[4];
    char tmp[ODE_OUTBUF];
    char num[ODE_FMT_MAX];
    int pos = 0;
    int n = -1;
    int i;
    int j;
    int k;
    int ci;
    int first_term = 1;
    int saw_x = 0;
    int right_nonzero = 0;
    int has_eq = 0;
    int rc;

    if (out == 0 || out_len <= 0) { return AC_PRO_ODE_EOVER; }
    out[0] = '\0';
    if (src == 0) { return AC_PRO_ODE_EPARSE; }
    if (strlen(src) == 0 || strlen(src) > ODE_SRC_MAX) {
        return AC_PRO_ODE_EPARSE;
    }

    for (k = 0; k < 5; k++) { a[k] = 0; }

    /* 契约 §3：单遍从左到右扫描
     * 判定全序（§6）：EPARSE（字符/长度/结构）→ EDEG（词法层当场）→
     * EVARCOEF（x 标志延迟裁决）→ ENONHOMO → ECOEF（系数域也延迟，
     * 否则 1000y''=1 会错报 ECOEF 而非 ENONHOMO）。 */
    {
        const char *p = src;
        int last_deg = -1;   /* 上一个项的导数阶（同阶重复禁令，§3.3-2） */

        for (;;) {
            int neg = 0;
            long long c = 0;
            int saw_coef = 0;
            int saw_star = 0;
            int saw_y = 0;
            int deg = 0;

            while (*p == ' ' || *p == '\t') { p++; }

            if (*p == '-') { neg = 1; p++; }
            else if (*p == '+') { p++; }
            while (*p == ' ' || *p == '\t') { p++; }
            if (*p == '-' || *p == '+') {
                /* 项首可再带一个符号（§3.2，gb 先例 ac_pro_groebner.c:129）：
                 * 后一个符号覆盖前一个（+-3y'' → −3y''，与 JS signRun 语义一致）；
                 * 第三个符号 → EPARSE。v2.4.2 差分收口：此前 C 侧把 +-3y''
                 * 误判 EPARSE，契约明文合法且 JS 侧已实现。 */
                neg = (*p == '-');
                p++;
                while (*p == ' ' || *p == '\t') { p++; }
                if (*p == '-' || *p == '+') { return AC_PRO_ODE_EPARSE; }
            }
            while (*p == ' ' || *p == '\t') { p++; }

            /* 因子序列：系数 / x 因子（置标志，延迟裁决）/ y 导数记号。
             * x 出现即 EVARCOEF 标志（§3.2），其形态（x、x^2、3x、x*y''）
             * 只需能走到项边界,结构非法字符仍当场 EPARSE。 */
            for (;;) {
                if (*p >= '0' && *p <= '9') {
                    c = c * 10 + (*p - '0');
                    if (c > 1000000000000000LL) {
                        c = 1000000000000000LL;   /* 饱和：ECOEF 延迟裁决 */
                    }
                    saw_coef = 1;
                    p++;
                } else if (*p == 'x') {
                    saw_x = 1;
                    p++;
                    while (*p == ' ' || *p == '\t') { p++; }
                    if (*p == '^') {
                        p++;
                        while (*p == ' ' || *p == '\t') { p++; }
                        while (*p >= '0' && *p <= '9') { p++; }  /* 幂值无关紧要 */
                    }
                } else if (*p == 'y') {
                    if (saw_y) { return AC_PRO_ODE_EPARSE; }
                    saw_y = 1;
                    p++;
                    while (*p == '\'') {
                        deg++;
                        p++;
                        if (deg > 4) { return AC_PRO_ODE_EDEG; }  /* 当场，§6 */
                    }
                    break;   /* y 记号 = 项尾（其后只允许空白/+/-/=/串尾） */
                } else if (*p == '*') {
                    if (saw_star) { return AC_PRO_ODE_EPARSE; }   /* 每项至多一个 *（§3.2，v2.4.2 差分收口） */
                    saw_star = 1;
                    p++;
                    while (*p == ' ' || *p == '\t') { p++; }
                    if (*p == '*' || *p == '=' || *p == '\0' ||
                        *p == '+' || *p == '-') {
                        return AC_PRO_ODE_EPARSE;   /* 悬空 '*' */
                    }
                } else {
                    return AC_PRO_ODE_EPARSE;   /* 空项 / 非法字符 */
                }
            }

            if (!saw_y) { return AC_PRO_ODE_EPARSE; }   /* §3.3-1 缺 y 的项 */

            /* y 记号后只允许 空白 / + - = / 串尾（§3.3-1，y''*3 → EPARSE） */
            while (*p == ' ' || *p == '\t') { p++; }
            if (*p != '+' && *p != '-' && *p != '=' && *p != '\0') {
                return AC_PRO_ODE_EPARSE;
            }

            if (!saw_coef) { c = neg ? -1 : 1; }
            else if (neg) { c = -c; }
            if (last_deg == deg) { return AC_PRO_ODE_EPARSE; }
            last_deg = deg;
            a[deg] = c;

            if (*p == '\0') { return AC_PRO_ODE_EPARSE; }        /* 缺 '=' */
            if (*p == '=') { has_eq = 1; p++; break; }
            /* 分隔符不前进：下一项开头的符号消费处处理（gb 先例，+-3y' 合法） */
        }

        if (!has_eq) { return AC_PRO_ODE_EPARSE; }

        /* 右端（契约 §3.3-4/5）
         * 空或含文法外字符（= 计入）→ EPARSE；去空白后恰为单个 '0' →
         * 齐次；其余（合法记号组成的非 "0" 串）→ ENONHOMO 候选。 */
        {
            char rb[16];
            int rn = 0;
            const char *q = p;
            while (*q == ' ' || *q == '\t') { q++; }
            if (*q == '\0') { return AC_PRO_ODE_EPARSE; }        /* 右端空 */
            for (; *q != '\0'; q++) {
                if (*q == ' ' || *q == '\t') { continue; }
                if (*q < '0' || *q > '9') {
                    if (*q != '+' && *q != '-' && *q != '*' &&
                        *q != 'y' && *q != 'x' && *q != '\'') {
                        return AC_PRO_ODE_EPARSE;                /* 文法外/第二 '=' */
                    }
                }
                if (rn < 15) { rb[rn++] = *q; }
                /* 超 15 字符的右端不可能恰为 "0"，靠 rn 封顶 + 长度判非零 */
            }
            rb[rn] = '\0';
            if (!(rn == 1 && rb[0] == '0')) { right_nonzero = 1; }
        }
    }

    /* 延迟裁决（契约 §6 全序） */
    if (saw_x) { return AC_PRO_ODE_EVARCOEF; }
    if (right_nonzero) { return AC_PRO_ODE_ENONHOMO; }

    /* 系数域（契约 §3.4）：越界 / 全零 / 无导数项 */
    for (k = 0; k < 5; k++) {
        if (a[k] > 999 || a[k] < -999) { return AC_PRO_ODE_ECOEF; }
        if (a[k] != 0) { n = k; }
    }
    if (n < 1) { return AC_PRO_ODE_ECOEF; }

    /* 契约 §4：特征根 */
    for (k = 0; k < 5; k++) { g_coeffs[k] = 0.0; }
    for (k = 0; k <= n; k++) { g_coeffs[k] = (double)a[k]; }
    if (n == 1) {
        re[0] = -g_coeffs[0] / g_coeffs[1];
        im[0] = 0.0;
        g_root_count = 1;
        g_root_re[0] = re[0];
        g_root_im[0] = 0.0;
    } else {
        if (n == 2) { ac_solve_quadratic(); }
        else if (n == 3) { ac_solve_cubic(0); }
        else { ac_solve_quartic(); }
        if (g_solve_error != 0) { return AC_PRO_ODE_EINTERNAL; }
        if (g_root_count != n) { return AC_PRO_ODE_EINTERNAL; }
    }
    for (k = 0; k < n; k++) { re[k] = g_root_re[k]; im[k] = g_root_im[k]; }

    /* 排序（契约 §4.2）：稳定插入，Re 降序，Re 相等（1e-9）Im 降序
     * 渲染顺序由此唯一决定，与底层求根器内部输出顺序无关。 */
    for (k = 0; k < n; k++) { lre[k] = re[k]; lim[k] = im[k]; }
    for (i = 1; i < n; i++) {
        double cr = lre[i];
        double cim = lim[i];
        j = i - 1;
        while (j >= 0) {
            double dr = lre[j] - cr;
            double di = (dr < ODE_EPS && dr > -ODE_EPS) ? (lim[j] - cim) : 0.0;
            if (dr < -ODE_EPS ||
                (dr <= ODE_EPS && dr >= -ODE_EPS && di < -ODE_EPS)) {
                lre[j + 1] = lre[j];
                lim[j + 1] = lim[j];
                j--;
            } else {
                break;
            }
        }
        lre[j + 1] = cr;
        lim[j + 1] = cim;
    }

    /* 聚类 + 渲染（契约 §4.3 / §5.2 / §5.3，一次线性扫描） */
    rc = ode_puts(tmp, ODE_OUTBUF, &pos, "y = ");
    if (rc != 0) { return rc; }
    for (k = 0; k < n; k++) { used[k] = 0; }
    ci = 0;   /* 常数下标 0..3 → C₁..C₄，渲染顺序即分配顺序，总数 = n */

    for (k = 0; k < n; k++) {
        if (used[k]) { continue; }
        if (ode_is_zero(lim[k])) {
            /* 实根簇（λ = re，重数 m） */
            int m = 1;
            used[k] = 1;
            for (j = k + 1; j < n; j++) {
                double d;
                if (used[j]) { continue; }
                if (!ode_is_zero(lim[j])) { continue; }
                d = lre[j] - lre[k];
                if (d < ODE_EPS && d > -ODE_EPS) { used[j] = 1; m++; }
            }
            for (j = 0; j < m; j++) {
                if (!first_term) {
                    rc = ode_puts(tmp, ODE_OUTBUF, &pos, " + ");
                    if (rc != 0) { return rc; }
                }
                first_term = 0;
                rc = ode_puts(tmp, ODE_OUTBUF, &pos, "C");
                if (rc == 0) { rc = ode_puts(tmp, ODE_OUTBUF, &pos, ode_csub(ci++)); }
                if (rc == 0 && j == 1) { rc = ode_puts(tmp, ODE_OUTBUF, &pos, "*x"); }
                if (rc == 0 && j >= 2) {
                    rc = ode_puts(tmp, ODE_OUTBUF, &pos, "*x^");
                    if (rc == 0) { rc = ode_puti(tmp, ODE_OUTBUF, &pos, j); }
                }
                if (rc == 0 && !ode_is_zero(lre[k])) {
                    rc = ode_puts(tmp, ODE_OUTBUF, &pos, "*exp(");
                    if (rc == 0) {
                        rc = ode_fmt(lre[k], num);
                        if (rc == 0) { rc = ode_puts(tmp, ODE_OUTBUF, &pos, num); }
                    }
                    if (rc == 0) { rc = ode_puts(tmp, ODE_OUTBUF, &pos, "*x)"); }
                }
                if (rc != 0) { return rc; }
            }
        } else {
            /* 共轭对（排序后 Im 降序 ⇒ 未配对首根 Im > 0） */
            int v = -1;
            double alpha;
            double beta;
            int m = 1;
            for (j = k + 1; j < n; j++) {
                double dr = lre[j] - lre[k];
                double ds = lim[j] + lim[k];
                if (used[j]) { continue; }
                if (dr < ODE_EPS && dr > -ODE_EPS &&
                    ds < ODE_EPS && ds > -ODE_EPS &&
                    ((lim[k] > 0 && lim[j] < 0) || (lim[k] < 0 && lim[j] > 0))) {
                    v = j;
                    break;
                }
            }
            if (v < 0) { return AC_PRO_ODE_EINTERNAL; }   /* 配不上，绝不硬凑 */
            used[k] = 1;
            used[v] = 1;
            alpha = lre[k];
            beta = (lim[k] > 0) ? lim[k] : -lim[k];       /* 恒取正，§4.3 */
            /* 同 (α,β) 的后续对并入同组（重数层累加，§5.3 行四） */
            for (i = k + 1; i < n; i++) {
                int q2 = -1;
                double d1;
                double db;
                if (used[i]) { continue; }
                if (ode_is_zero(lim[i])) { continue; }
                d1 = lre[i] - alpha;
                db = (lim[i] > 0 ? lim[i] : -lim[i]) - beta;
                if (!(d1 < ODE_EPS && d1 > -ODE_EPS &&
                      db < ODE_EPS && db > -ODE_EPS)) { continue; }
                for (q2 = i + 1; q2 < n; q2++) {
                    double dr = lre[q2] - alpha;
                    double ds = lim[q2] + lim[i];
                    if (used[q2]) { continue; }
                    if (dr < ODE_EPS && dr > -ODE_EPS &&
                        ds < ODE_EPS && ds > -ODE_EPS &&
                        lim[i] * lim[q2] < 0) {
                        used[i] = 1;
                        used[q2] = 1;
                        m++;
                        i = n;   /* 命中后跳出外层本轮 */
                        break;
                    }
                }
            }
            for (j = 0; j < m; j++) {
                int t;
                for (t = 0; t < 2; t++) {   /* 先 cos 后 sin，§5.3 */
                    if (!first_term) {
                        rc = ode_puts(tmp, ODE_OUTBUF, &pos, " + ");
                        if (rc != 0) { return rc; }
                    }
                    first_term = 0;
                    rc = ode_puts(tmp, ODE_OUTBUF, &pos, "C");
                    if (rc == 0) {
                        rc = ode_puts(tmp, ODE_OUTBUF, &pos, ode_csub(ci++));
                    }
                    if (rc == 0 && j == 1) {
                        rc = ode_puts(tmp, ODE_OUTBUF, &pos, "*x");
                    }
                    if (rc == 0 && j >= 2) {
                        rc = ode_puts(tmp, ODE_OUTBUF, &pos, "*x^");
                        if (rc == 0) { rc = ode_puti(tmp, ODE_OUTBUF, &pos, j); }
                    }
                    if (rc == 0 && !ode_is_zero(alpha)) {
                        rc = ode_puts(tmp, ODE_OUTBUF, &pos, "*exp(");
                        if (rc == 0) {
                            rc = ode_fmt(alpha, num);
                            if (rc == 0) {
                                rc = ode_puts(tmp, ODE_OUTBUF, &pos, num);
                            }
                        }
                        if (rc == 0) { rc = ode_puts(tmp, ODE_OUTBUF, &pos, "*x)"); }
                    }
                    if (rc == 0) {
                        rc = ode_puts(tmp, ODE_OUTBUF, &pos,
                                      (t == 0) ? "*cos(" : "*sin(");
                    }
                    if (rc == 0) {
                        rc = ode_fmt(beta, num);
                        if (rc == 0) { rc = ode_puts(tmp, ODE_OUTBUF, &pos, num); }
                    }
                    if (rc == 0) { rc = ode_puts(tmp, ODE_OUTBUF, &pos, "*x)"); }
                    if (rc != 0) { return rc; }
                }
            }
        }
    }

    if (ci != n) { return AC_PRO_ODE_EINTERNAL; }   /* 契约 §4.3 自检恒等式 */

    if (pos >= out_len) { return AC_PRO_ODE_EOVER; }
    memcpy(out, tmp, (size_t)pos + 1);
    return AC_PRO_ODE_OK;
}

#endif /* AC_PRO_ACTIVE */
