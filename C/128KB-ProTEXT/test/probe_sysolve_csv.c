/* ApolloCalc · probe_sysolve_csv.c — F2 差分对拍 C 侧探针(独立可执行,不进
 * Makefile 的 TEST_SRC;由 tests/bench/diff-sysolve.js 用与内核相同的 obj
 * 缓存直链)。协议:stdin 每行 "f|g"(UTF-8,| 分隔);stdout 每行
 * "<rc>|<out>"。跑完 exit 0。 */

#include <stdio.h>
#include <string.h>

#include "../src/ac_pro.h"

int main(void) {
    char line[1024];
    char out[2048];
    int rc;

    while (fgets(line, (int)sizeof(line), stdin) != 0) {
        char *sep;
        size_t len;
        len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) { line[--len] = '\0'; }
        if (len == 0) { continue; }
        sep = strchr(line, '|');
        if (sep == 0) {
            printf("EARG|\n");
            fflush(stdout);
            continue;
        }
        *sep = '\0';
        rc = ac_pro_sysolve(line, sep + 1, out, (int)sizeof(out));
        printf("%d|%s\n", rc, out);
        fflush(stdout);
    }
    return 0;
}
