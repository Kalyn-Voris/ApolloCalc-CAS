/* odefuzz.c — C 侧 ODE 差分探针:stdin 每行一个输入,输出 "code\tout" */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include "../C/128KB-ProTEXT/src/ac_pro.h"

int main(void) {
    _setmode(_fileno(stdout), _O_BINARY);
    char line[1024];
    char out[256];
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) { line[--n] = 0; }
        memset(out, 0, sizeof out);
        int rc = ac_pro_ode(line, out, (int)sizeof(out));
        if (rc == 0) { printf("0\t%s\n", out); } else { printf("%d\t\n", rc); }
    }
    return 0;
}
