#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* [English] Linker wrappers (--wrap=malloc --wrap=free).
   Kernel phase (from main() entry to main() return): ANY heap call aborts —
   this enforces Zero-GC for all kernel/test code.
   Host CRT phase (before main: __tmainCRTStartup in crt2.o, TLS callbacks in
   tlssup.o) legitimately allocates on a hosted toolchain; those calls are
   forwarded to msvcrt's own allocator resolved at runtime via GetProcAddress,
   which also keeps every static malloc/free symbol out of the binary.
   On the bare-metal target (Cortex-M4F) no heap and no CRT exist at all. */
#ifdef _WIN32
typedef void *(*ac_host_malloc_t)(size_t);
typedef void  (*ac_host_free_t)(void *);

static int s_kernel_live = 0;

static ac_host_malloc_t ac_host_malloc(void) {
    static ac_host_malloc_t fn = NULL;
    if (fn == NULL) {
        fn = (ac_host_malloc_t)(void *)GetProcAddress(GetModuleHandleA("msvcrt.dll"), "malloc");
        if (fn == NULL) {
            fprintf(stderr, "[FATAL] cannot resolve host allocator\n");
            abort();
        }
    }
    return fn;
}

static ac_host_free_t ac_host_free(void) {
    static ac_host_free_t fn = NULL;
    if (fn == NULL) {
        fn = (ac_host_free_t)(void *)GetProcAddress(GetModuleHandleA("msvcrt.dll"), "free");
    }
    return fn;
}

void *__wrap_malloc(size_t size) {
    if (s_kernel_live) {
        fprintf(stderr, "[FATAL] malloc called at runtime!\n");
        abort();
    }
    return ac_host_malloc()(size);
}
void __wrap_free(void *ptr) {
    if (s_kernel_live) {
        fprintf(stderr, "[FATAL] free called at runtime!\n");
        abort();
    }
    ac_host_free()(ptr);
}
#else
/* [English] Non-Windows host: forward pre-main CRT calls to the real allocator. */
extern void *__real_malloc(size_t size);
extern void __real_free(void *ptr);
static int s_kernel_live = 0;

void *__wrap_malloc(size_t size) {
    if (s_kernel_live) {
        fprintf(stderr, "[FATAL] malloc called at runtime!\n");
        abort();
    }
    return __real_malloc(size);
}
void __wrap_free(void *ptr) {
    if (s_kernel_live) {
        fprintf(stderr, "[FATAL] free called at runtime!\n");
        abort();
    }
    __real_free(ptr);
}
#endif

/* Test entries defined in test_parser_smoke.c / test_evaluator.c / test_exact.c / test_solvers.c / test_ratroot.c / test_newton.c / test_math.c / test_complex.c / test_calculus.c / test_numtheory.c / test_base.c / test_bigint.c / test_matrix.c / test_special.c / test_symbolic.c / test_util.c */
int test_parser_run(void);
int test_evaluator_run(void);
int test_exact_run(void);
int test_solvers_run(void);
int test_ratroot_run(void);
int test_newton_run(void);
int test_math_run(void);
int test_complex_run(void);
int test_calculus_run(void);
int test_numtheory_run(void);
int test_base_run(void);
int test_bigint_run(void);
int test_matrix_run(void);
int test_special_run(void);
int test_symbolic_run(void);
int test_util_run(void);

int main(void) {
    int r1;
    int r2;
    int r3;
    int r4;
    int r5;
    int r6;
    int r7;
    int r8;
    int r9;
    int r10;
    int r11;
    int r12;
    int r13;
    int r14;
    int r15;
    int r16;
    s_kernel_live = 1;      /* Zero-GC enforcement ON for kernel + tests */
    r1 = test_parser_run();
    r2 = test_evaluator_run();
    r3 = test_exact_run();
    r4 = test_solvers_run();
    r5 = test_ratroot_run();
    r6 = test_newton_run();
    r7 = test_math_run();
    r8 = test_complex_run();
    r9 = test_calculus_run();
    r10 = test_numtheory_run();
    r11 = test_base_run();
    r12 = test_bigint_run();
    r13 = test_matrix_run();
    r14 = test_special_run();
    r15 = test_symbolic_run();
    r16 = test_util_run();
    s_kernel_live = 0;      /* CRT teardown may free */
    return (r1 || r2 || r3 || r4 || r5 || r6 || r7 || r8 || r9 || r10 || r11 || r12 || r13 || r14 || r15 || r16) ? 1 : 0;
}
