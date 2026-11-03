// Coroutine context switch primitives.
//
// `__cxy_coro_setjmp` returns 0 when the context is saved and 1 when it is
// resumed through `__cxy_coro_longjmp`. Both are real functions, so callers
// treat caller-saved registers as clobbered and the result is returned in the
// ABI return register. Declare them in Cxy with `returns_twice` and
// `noreturn` respectively.
//
// Context layout (u64 slots), kept in sync with CXY_CTX_SIZE in coro.cxy:
//   x86_64   8: rbx rbp r12 r13 r14 r15 rsp rip
//   aarch64 22: pc sp x19-x28 x29 x30 d8-d15
//   arm     10: pc sp lr r4-r11 (one 32-bit value per slot, untested)

#include <stdint.h>

#if defined(__x86_64__)

__attribute__((naked, returns_twice)) int64_t __cxy_coro_setjmp(void *ctx)
{
    __asm__ volatile(
        "movq %rbx,  0(%rdi)\n"
        "movq %rbp,  8(%rdi)\n"
        "movq %r12, 16(%rdi)\n"
        "movq %r13, 24(%rdi)\n"
        "movq %r14, 32(%rdi)\n"
        "movq %r15, 40(%rdi)\n"
        "leaq 8(%rsp), %rdx\n" // sp once we have returned
        "movq %rdx, 48(%rdi)\n"
        "movq (%rsp), %rdx\n" // return address
        "movq %rdx, 56(%rdi)\n"
        "xorl %eax, %eax\n"
        "ret\n");
}

__attribute__((naked, noreturn)) void __cxy_coro_longjmp(void *ctx)
{
    __asm__ volatile(
        "movq  0(%rdi), %rbx\n"
        "movq  8(%rdi), %rbp\n"
        "movq 16(%rdi), %r12\n"
        "movq 24(%rdi), %r13\n"
        "movq 32(%rdi), %r14\n"
        "movq 40(%rdi), %r15\n"
        "movq 48(%rdi), %rsp\n"
        "movq 56(%rdi), %rdx\n"
        "movl $1, %eax\n"
        "jmp *%rdx\n");
}

#elif defined(__aarch64__)

__attribute__((naked, returns_twice)) int64_t __cxy_coro_setjmp(void *ctx)
{
    __asm__ volatile(
        "mov x16, sp\n"
        "stp x30, x16, [x0]\n" // resume at our caller, saved sp
        "stp x19, x20, [x0, #16]\n"
        "stp x21, x22, [x0, #32]\n"
        "stp x23, x24, [x0, #48]\n"
        "stp x25, x26, [x0, #64]\n"
        "stp x27, x28, [x0, #80]\n"
        "stp x29, x30, [x0, #96]\n"
        "stp d8,  d9,  [x0, #112]\n"
        "stp d10, d11, [x0, #128]\n"
        "stp d12, d13, [x0, #144]\n"
        "stp d14, d15, [x0, #160]\n"
        "mov x0, #0\n"
        "ret\n");
}

__attribute__((naked, noreturn)) void __cxy_coro_longjmp(void *ctx)
{
    __asm__ volatile(
        "ldp x17, x16, [x0]\n" // slot 0 is the pc, x30 comes from slot 12
        "mov sp, x16\n"
        "ldp x19, x20, [x0, #16]\n"
        "ldp x21, x22, [x0, #32]\n"
        "ldp x23, x24, [x0, #48]\n"
        "ldp x25, x26, [x0, #64]\n"
        "ldp x27, x28, [x0, #80]\n"
        "ldp x29, x30, [x0, #96]\n"
        "ldp d8,  d9,  [x0, #112]\n"
        "ldp d10, d11, [x0, #128]\n"
        "ldp d12, d13, [x0, #144]\n"
        "ldp d14, d15, [x0, #160]\n"
        "mov x0, #1\n"
        "br x17\n");
}

#elif defined(__arm__)

__attribute__((naked, returns_twice)) int64_t __cxy_coro_setjmp(void *ctx)
{
    __asm__ volatile(
        "mov r1, sp\n"
        "str lr, [r0, #0]\n"
        "str r1, [r0, #8]\n"
        "str lr, [r0, #16]\n"
        "str r4, [r0, #24]\n"
        "str r5, [r0, #32]\n"
        "str r6, [r0, #40]\n"
        "str r7, [r0, #48]\n"
        "str r8, [r0, #56]\n"
        "str r9, [r0, #64]\n"
        "str r10, [r0, #72]\n"
        "str r11, [r0, #80]\n"
        "mov r0, #0\n"
        "mov r1, #0\n"
        "bx lr\n");
}

__attribute__((naked, noreturn)) void __cxy_coro_longjmp(void *ctx)
{
    __asm__ volatile(
        "ldr r1, [r0, #0]\n"
        "ldr r2, [r0, #8]\n"
        "mov sp, r2\n"
        "ldr lr, [r0, #16]\n"
        "ldr r4, [r0, #24]\n"
        "ldr r5, [r0, #32]\n"
        "ldr r6, [r0, #40]\n"
        "ldr r7, [r0, #48]\n"
        "ldr r8, [r0, #56]\n"
        "ldr r9, [r0, #64]\n"
        "ldr r10, [r0, #72]\n"
        "ldr r11, [r0, #80]\n"
        "mov r0, #1\n"
        "mov r2, #0\n"
        "bx r1\n");
}

#else
#error "coroutine context switching is not implemented for this architecture"
#endif
