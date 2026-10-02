#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <execinfo.h>

void __cxy_symbolize_stack(void *const *stack, int count)
{
    if (count <= 0 || stack == NULL)
        return;

#if defined(__APPLE__)
    Dl_info info;
    if (dladdr(stack[0], &info) && info.dli_fname != NULL) {
        char cmd[1024];
        int pos = snprintf(cmd, sizeof(cmd), "atos -o \"%s\" -l %p", info.dli_fname, info.dli_fbase);
        for (int i = 0; i < count && pos < (int)sizeof(cmd) - 30; i++) {
            pos += snprintf(cmd + pos, sizeof(cmd) - pos, " %p", stack[i]);
        }
        FILE *pipe = popen(cmd, "r");
        if (pipe != NULL) {
            char line[256];
            int frame = 0;
            while (fgets(line, sizeof(line), pipe) != NULL) {
                printf("    #%-2d %s", frame++, line);
            }
            pclose(pipe);
            return;
        }
    }
#elif defined(__linux__)
    Dl_info info;
    if (dladdr(stack[0], &info) && info.dli_fname != NULL) {
        char cmd[1024];
        int pos = snprintf(cmd, sizeof(cmd), "addr2line -e \"%s\" -f -C", info.dli_fname);
        for (int i = 0; i < count && pos < (int)sizeof(cmd) - 30; i++) {
            pos += snprintf(cmd + pos, sizeof(cmd) - pos, " %p", stack[i]);
        }
        FILE *pipe = popen(cmd, "r");
        if (pipe != NULL) {
            char func[128], fileline[256];
            int frame = 0;
            while (fgets(func, sizeof(func), pipe) != NULL && fgets(fileline, sizeof(fileline), pipe) != NULL) {
                func[strcspn(func, "\r\n")] = 0;
                printf("    #%-2d %s @ %s", frame++, func, fileline);
            }
            pclose(pipe);
            return;
        }
    }
#endif

    backtrace_symbols_fd((void *const *)stack, count, 1);
}


