/* ApolloCalc · probe_sturm_csv.c — F3 差分对拍 C 侧探针(独立可执行,不进
 * Makefile 的 TEST_SRC;由 tests/bench/diff-sturm.js 用与内核相同的 obj
 * 缓存直链)。协议:stdin 每行 "C|p|a|b"(计数)或 "I|p"(隔离);
 * stdout 每行 "<count>" 或 "<rc>|<out>"。跑完 exit 0。 */

#include <stdio.h>
#include <string.h>

#include "../src/ac_pro.h"

int main(void) {
    char line[1024];
    char out[512];
    char *f1;
    char *f2;
    size_t len;

    while (fgets(line, (int)sizeof(line), stdin) != 0) {
        len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) { line[--len] = '\0'; }
        if (len == 0) { continue; }
        if (line[0] == 'C' && line[1] == '|') {
            f1 = strchr(line + 2, '|');
            if (f1 == 0) { printf("-4\n"); fflush(stdout); continue; }
            *f1 = '\0';
            f2 = strchr(f1 + 1, '|');
            if (f2 == 0) { printf("-4\n"); fflush(stdout); continue; }
            *f2 = '\0';
            printf("%d\n", ac_pro_sturm_count(line + 2, f1 + 1, f2 + 1));
            fflush(stdout);
        } else if (line[0] == 'I' && line[1] == '|') {
            printf("%d|%s\n", ac_pro_sturm_isolate(line + 2, out, (int)sizeof(out)), out);
            fflush(stdout);
        } else {
            printf("-4\n");
            fflush(stdout);
        }
    }
    return 0;
}
