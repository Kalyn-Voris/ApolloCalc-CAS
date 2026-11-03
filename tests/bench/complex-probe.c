/* ApolloCalc · complex-probe.c — F1 复变函数扩展差分对拍 C 探针（独立工具，
 * 不进任何构建账本；由 tests/bench/diff-complex.js 以 gcc 直编三 TU 链接：
 * 本文件 + C/128KB-ProTEXT/src/ac_pro_complex_ext.c + C/128KB/src/ac_complex.c）
 *
 * 语料：tests/bench/complex-corpus.csv（tools/gen-complex-corpus.js 生成，
 * 列 id,fn,zre,zim,wre,wim,k）。逐行设置寄存器、调 API、输出
 *   id \t fn \t rc \t tmpRe \t tmpIm    （%.17g，错误行值列无意义）
 * 与 JS 侧逐行按契约 complex-ext-v25.md §8 比较。
 *
 * Zero-GC：无 malloc、无 static 可变对象、无递归；语料逐行读入逐行输出，
 * 行缓冲在栈上。退出码 0 = 语料消费完成（一致性判定在驱动脚本）。 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../../C/128KB-ProTEXT/src/ac_pro.h"
#include "../../C/128KB/src/ac_complex.h"

#if AC_PRO_ACTIVE

int main(int argc, char **argv) {
    char line[512];
    FILE *f;
    int n = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: cx_probe <corpus.csv>\n");
        return 2;
    }
    f = fopen(argv[1], "r");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        char id[64];
        char fn[16];
        double zre, zim, wre, wim;
        int k;
        int rc = -99;
        int got;
        /* 去行尾换行 */
        got = (int)strlen(line);
        while (got > 0 && (line[got - 1] == '\n' || line[got - 1] == '\r')) {
            line[--got] = '\0';
        }
        if (got == 0 || line[0] == '#') { continue; }
        if (sscanf(line, "%63[^,],%15[^,],%lf,%lf,%lf,%lf,%d",
                   id, fn, &zre, &zim, &wre, &wim, &k) != 7) {
            fprintf(stderr, "bad corpus line: %s\n", line);
            fclose(f);
            return 2;
        }
        g_c_a_re = zre;
        g_c_a_im = zim;
        g_c_b_re = wre;
        g_c_b_im = wim;
        if (strcmp(fn, "csin") == 0) { rc = ac_pro_csin(); }
        else if (strcmp(fn, "ccos") == 0) { rc = ac_pro_ccos(); }
        else if (strcmp(fn, "ctan") == 0) { rc = ac_pro_ctan(); }
        else if (strcmp(fn, "csinh") == 0) { rc = ac_pro_csinh(); }
        else if (strcmp(fn, "ccosh") == 0) { rc = ac_pro_ccosh(); }
        else if (strcmp(fn, "ctanh") == 0) { rc = ac_pro_ctanh(); }
        else if (strcmp(fn, "casin") == 0) { rc = ac_pro_casin(); }
        else if (strcmp(fn, "cacos") == 0) { rc = ac_pro_cacos(); }
        else if (strcmp(fn, "catan") == 0) { rc = ac_pro_catan(); }
        else if (strcmp(fn, "casinh") == 0) { rc = ac_pro_casinh(); }
        else if (strcmp(fn, "cacosh") == 0) { rc = ac_pro_cacosh(); }
        else if (strcmp(fn, "catanh") == 0) { rc = ac_pro_catanh(); }
        else if (strcmp(fn, "cpow") == 0) { rc = ac_pro_cpow(); }
        else if (strcmp(fn, "cnthroot") == 0) { rc = ac_pro_cnthroot(k); }
        else if (strcmp(fn, "cgamma") == 0) { rc = ac_pro_cgamma(); }
        else {
            fprintf(stderr, "unknown fn: %s\n", fn);
            fclose(f);
            return 2;
        }
        printf("%s\t%s\t%d\t%.17g\t%.17g\n", id, fn, rc, g_c_tmp_re, g_c_tmp_im);
        n++;
    }
    fclose(f);
    fprintf(stderr, "cx_probe: %d rows\n", n);
    return 0;
}

#else /* !AC_PRO_ACTIVE */

int main(void) {
    fprintf(stderr, "cx_probe requires AC_PRO=1\n");
    return 2;
}

#endif /* AC_PRO_ACTIVE */
