#include "tinythread.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct __cxy_thread_closure {
    void *env;
    int (*fn)(void *);
    void (*dtor)(void *);
} __cxy_thread_closure;

// Provided by the generated program; runs this thread's module finalizers
__attribute__((weak)) extern void __cxy_run_thread_finis(void);

static int __cxy_thread_entry(void *arg) {
    __cxy_thread_closure *cl = (__cxy_thread_closure *)arg;
    int res = cl->fn ? cl->fn(cl->env) : 0;
    if (cl->dtor) cl->dtor(cl->env);
    free(cl);
    if (__cxy_run_thread_finis) __cxy_run_thread_finis();
    return res;
}

bool __cxy_thread_launch(thrd_t *hdl, void *closure_struct, unsigned long size) {
    void *mem = malloc(size);
    if (!mem) return -1;
    memcpy(mem, closure_struct, size);
    int status = thrd_create(hdl, __cxy_thread_entry, mem);
    if (status != thrd_success) {
        free(mem);
        return false;
    }
    return true;
}
